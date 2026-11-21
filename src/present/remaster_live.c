/* remaster_live.c - Remastered's view of a running game through the machine's execution hooks (see remaster_live.h). */
#include "remaster_live.h"
#include "rerun.h"

#include "remaster_raster.h"
#include "decal_faces.h"
#include "remaster_scene.h"
#include "scene_mesh.h"
#include "band_state.h"
#include "diagtag.h"
#include "remaster_project.h"
#include "tls.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "gpu_trace.h"

/* A deep copy of a frame's primitives (the piece-2 blend check, below). */
struct remaster_frame_copy {
    remaster_prim *prim;
    size_t n, cap;
    int origin[2];
    /* Part B Stage 2 (F117_STAGE2): what a step's scene needs besides its primitives, so two of them can be blended into a picture */
    int32_t band[BAND_REC_FINE];
    int have_band, window[4], complete;
    long hud_at;
    int16_t cam[12]; int have_cam;          /* reprojection (F117_STAGE2_REPROJECT): the view matrix at DS:7B28 (9 words) and heading, pitch, roll (2DEE..2DF2) the copy drew with */
};
#ifdef _WIN32
#include <windows.h>
#endif

/* How many coarser levels beyond its own ring an object's dark dots are still drawn: 0 the original's reach, 3 the whole map (the Terrain dots setting). -1: not set yet. */
static volatile int g_dot_levels = -1;
void remaster_live_set_dot_levels(int levels) { g_dot_levels = levels < 0 ? 0 : levels > 3 ? 3 : levels; }

/* A native-path option that is on unless the variable is set to 0 (validated off against on at Near, Far and Maximum on three recorded flights: docs/scenery-distance.md). */
static int env_default_on(const char *name)
{
    const char *e = getenv(name);
    return !(e && e[0] == '0');
}

/* F117_GPU_DUMP=FILE[,FROM] (diagnostic, with F117_REMASTER_THREADS=1): the first composite from clock FROM, as every raster call and the CPU result
 * (gpu_trace.h), for the GPU compositor's harness. */
static FILE *g_trace_file; static F117_TLS gpu_trace_rec *g_trace_recs; static F117_TLS size_t g_trace_n, g_trace_cap;
static F117_TLS int g_record_only, g_record_failed;      /* the GPU compositor: record the raster calls and skip the CPU drawing */
static remaster_gpu_fn g_gpu_fn;
static remaster_gpu_begin_fn g_gpu_begin_fn;
static remaster_gpu_finish_fn g_gpu_finish_fn;
void remaster_live_set_gpu(remaster_gpu_fn run, remaster_gpu_begin_fn begin, remaster_gpu_finish_fn finish)
{ g_gpu_fn = run; g_gpu_begin_fn = begin; g_gpu_finish_fn = finish; }
static char g_trace_path[1024]; static int g_trace_extra;      /* the replace section is still to be appended */
static int gpu_trace_hook(int kind, const remaster_target *t, const remaster_v3 *v, int n, const uint32_t *colors, uint32_t color, double width, int op, unsigned value)
{
    if (g_trace_n == g_trace_cap) {
        g_trace_cap = g_trace_cap ? g_trace_cap * 2 : 4096;
        g_trace_recs = (gpu_trace_rec *)realloc(g_trace_recs, g_trace_cap * sizeof *g_trace_recs);
        if (!g_trace_recs) { g_trace_cap = g_trace_n = 0; g_record_failed = 1; return g_record_only; }
    }
    gpu_trace_rec *r = &g_trace_recs[g_trace_n++];
    memset(r, 0, sizeof *r);
    r->kind = kind; r->n = n; r->op = op; r->color = color; r->value = value; r->width = width;
    for (int i = 0; i < n && i < 16; i++) { r->v[i] = v[i]; if (colors) r->colors[i] = colors[i]; }
    r->cx = t->cx; r->cy = t->cy; r->sx = t->sx; r->sy = t->sy; r->inv_depth_scale = t->inv_depth_scale; r->line_bias = t->line_bias;
    r->near_z = t->near_z; r->line_decal = t->line_decal; r->line_order = t->line_order; r->paint_owner = t->paint_owner; r->paint_index = t->paint_index;
    return g_record_only;
}

/* Milliseconds from an arbitrary start, for F117_REMASTER_TIMING. */
static double now_ms(void)
{
#ifdef _WIN32
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
#else
    return 0.0;
#endif
}

/* the watched entries, as {segment relative to the load segment, offset}: k = 0 the model request, 1 model_faces (VGAME
 * 120A:0C76, the faces of a model that is visible: run after the pose is fully prepared, so the part matrices, planes and
 * vertices are where remaster_scene_add reads them), 2 the per-frame draw routine, 3 the HUD pass, 4 object placement */
static const struct { uint16_t seg, ip; } WATCH[5] = {
    { 0x120A, 0x00CF },
    { 0x120A, 0x0C76 },
    { 0x0000, 0x1450 },
    { 0x1058, 0x000A },                 /* the HUD pass's far entry: the models asked for after it are drawn over the HUD */
    { 0x0000, 0xE2B6 },                 /* placement: capture native units before deferral */
};

#define UNITS_MAX 16384                       /* placements remembered for a frame: the far scenery places thousands */
#define TEMPLATE_AT 0x7B28
#define TEMPLATE_LEN 0x278

typedef struct {
    uint32_t eye[3];
    uint8_t matrix[18];
    int32_t value[3];
    int valid;
} ring_correction;

struct remaster_live {
    remaster_scene *scene;
    remaster_frame frame;
    int pending;
    uint16_t model, bank;
    int16_t position[3];
    double pending_inv_depth;
    double pending_camera_offset[3];
    int pending_shrink;                       /* the bits (2 per ring) by which the object is drawn at a coarser scale than its own ring: its vertices shrink by that much about its origin */
    int pending_terrain;                      /* the pending request was placed by the terrain walker: its pose may become a template */
    struct { uint16_t bank, model; int16_t position[3]; double inv_depth, camera_offset[3]; int shrink, terrain; } units[UNITS_MAX];
    /* Templates for natively drawn static terrain objects: the game's pose state (DS:7B28 .. 7DA0) as it stood for one terrain object of a model
     * this frame; a later object of the same model patches only what depends on its place (remaster_live_native). */
    struct { uint16_t bank, model; int parts; uint8_t bytes[TEMPLATE_LEN]; } tmpl[96];
    unsigned ntmpl, native_drawn, native_missed;
    remaster_frame chk_frame;                 /* F117_NATIVE_CHECK: a native object's primitives are built here, never in the frame */
    struct { uint16_t bank, model; int16_t pos[3]; int shrink; double inv_depth; remaster_prim *prim; size_t n; uint8_t pose[TEMPLATE_LEN]; } *chk;
    unsigned nchk, chk_cap, chk_ok, chk_bad, chk_expired, chk_empty, chk_alloc_failed;
    uint8_t chk_pose_now[TEMPLATE_LEN];
    int chk_return_pending, chk_predicted_range;
    unsigned chk_range_agree, chk_false_reject, chk_false_accept, chk_prediction_unknown;
    unsigned chk_before_match, chk_before_requests, chk_before_added, chk_before_deferred;
    unsigned chk_returns, chk_no_request, chk_no_faces, chk_no_pair, chk_deferred, chk_no_return;
    unsigned chk_range_rejected, chk_range_accepted, chk_range_unknown;
    unsigned nunits;
    int frame_complete, done_complete;
    int replay_copy;
    size_t src_native_prims, src_native_objs;   /* this frame: primitives and objects placed natively (F117_SRC_SPLIT) */
    int gpu_begun;                      /* remaster_live_gpu_begin ran for the picture being made and its compose has not finished it */
    ring_correction native_correction[4];
    unsigned frames, want_frame;
    int w, h;
    char *out_path;
    int written;
    unsigned requests, added, refused, overwritten, ortho_skipped, skipped;
    /* F117_REMASTER_VALIDATE: per model, how many vertices' projections match the game's own (the 8-byte records at
     * DS:D6B4 + 8 * id, signed pixel offsets from the origin) and how many do not */
    struct { uint16_t model; unsigned seen, match, differ, behind_mine, behind_game, off1, off8, farther; } check[256];
    unsigned nchecks;
    int32_t band_rec[BAND_REC_FINE];  /* the sky and ground band pass's starting state (band_state.h), for the frame in progress */
    int have_band;
    int16_t cam_rec[12]; int have_cam, cam_kinds;   /* the frame in progress: its view matrix and attitude (first model request), and how many different matrices it saw */
    remaster_frame done;                /* the last completed frame, with its band state, origin and window, for compositing */
    int32_t done_band[BAND_REC_FINE];
    int done_have_band, have_done, done_origin[2], done_window[4];
    int16_t done_cam[12]; int done_have_cam;
    /* Deferred mode (remaster_live_set_deferred): a copy stages its finished frame here instead of replacing `done`, which a worker may be reading; the main
     * thread makes it the done frame with remaster_live_commit at a point where the worker is idle. */
    remaster_frame staged;
    int32_t staged_band[BAND_REC_FINE];
    int staged_have_band, have_staged, staged_origin[2], staged_window[4], staged_complete;
    int16_t staged_cam[12]; int staged_have_cam;
    long staged_hud_at; int staged_hud_set;   /* Stage 2: the HUD split of a blended frame, handed to done_hud_at by the commit */
    int deferred, copy_open;
    long hud_at, done_hud_at;           /* the frame's primitives before the HUD pass (-1: no HUD pass yet) */
    int origin[2], window[4];       /* the game's screen origin [8602],[8604] and clip window [85FA],[85FC],[85FE],[8600] */
};

remaster_live *remaster_live_new(int w, int h, unsigned frame, const char *out_path)
{
    remaster_live *r = (remaster_live *)calloc(1, sizeof *r);
    if (!r) return NULL;
    r->scene = remaster_scene_new();
    r->hud_at = r->done_hud_at = -1;
    r->w = w; r->h = h; r->want_frame = frame;
    if (out_path) {
        r->out_path = (char *)malloc(strlen(out_path) + 1);
        if (r->out_path) strcpy(r->out_path, out_path);
    }
    if (!r->scene) { remaster_live_free(r); return NULL; }
    return r;
}

static void expire_native_checks(remaster_live *r)
{
    while (r->nchk) {
        const unsigned i = --r->nchk;
        r->chk_empty += r->chk[i].n == 0;
        r->chk_expired++;
        free(r->chk[i].prim);
    }
}

void remaster_live_free(remaster_live *r)
{
    if (!r) return;
    expire_native_checks(r);
    if (r->chk_ok || r->chk_bad || r->chk_expired || r->chk_alloc_failed)
        fprintf(stderr, "[native-check] final: %u match, %u differ, %u expired, %u empty unmatched, %u allocation failures\n",
                r->chk_ok, r->chk_bad, r->chk_expired, r->chk_empty, r->chk_alloc_failed);
    free(r->chk);
    remaster_frame_free(&r->chk_frame);
    remaster_scene_free(r->scene);
    remaster_frame_free(&r->frame);
    remaster_frame_free(&r->done);
    free(r->out_path);
    free(r);
}

void remaster_live_copy_begin(remaster_live *r)
{
    r->replay_copy = 1;
    expire_native_checks(r);
    remaster_frame_clear(&r->frame);
    r->pending = 0;
    r->nunits = 0;
    r->have_band = 0; r->have_cam = 0; r->cam_kinds = 0;
    if (r->deferred) {              /* the done frame stays as the worker has it; the copy's result is staged and committed later */
        r->have_staged = 0; r->staged.n = 0; r->copy_open = 1;
        r->frame_complete = 0;
    } else {
        r->have_done = 0;
        r->frame_complete = r->done_complete = 0;
        r->done.n = 0;
    }
    r->hud_at = r->done_hud_at = -1;
}

void remaster_live_set_deferred(remaster_live *r, int on) { if (r) r->deferred = on != 0; }

/* Deferred mode: make the copy's finished frame the one a composite uses, exactly as the end of a copy did without deferral (a copy that completed
 * no frame leaves the done frame empty). Call it only while no composite is reading the done frame. */
void remaster_live_commit(remaster_live *r)
{
    if (!r || !r->deferred || !r->copy_open) return;
    r->copy_open = 0;
    if (r->have_staged) {
        remaster_frame tmp = r->done; r->done = r->staged; r->staged = tmp;
        memcpy(r->done_band, r->staged_band, sizeof r->done_band);
        r->done_have_band = r->staged_have_band; r->have_done = r->done.n > 0;
        memcpy(r->done_cam, r->staged_cam, sizeof r->done_cam); r->done_have_cam = r->staged_have_cam;
        r->done_complete = r->staged_complete;
        memcpy(r->done_origin, r->staged_origin, sizeof r->origin); memcpy(r->done_window, r->staged_window, sizeof r->window);
        if (r->staged_hud_set) { r->done_hud_at = r->staged_hud_at; r->staged_hud_set = 0; }
        r->have_staged = 0;
    } else {
        r->done.n = 0; r->have_done = 0; r->done_complete = 0;
    }
}

unsigned remaster_live_arm(remaster_live *r, machine_t *m, uint16_t load_seg)
{
    (void)r;
    const unsigned base = m->exec_hook_n;
    for (unsigned k = 0; k < 5 && m->exec_hook_n < EXEC_HOOKS_LIVE_MAX; k++) {
        m->exec_hook[m->exec_hook_n].cs = (uint16_t)(load_seg + WATCH[k].seg);
        m->exec_hook[m->exec_hook_n].ip = WATCH[k].ip;
        m->exec_hook_n++;
    }
    return base;
}

static uint16_t peek16(const machine_t *m, uint16_t seg, uint16_t off)
{
    const uint32_t at = ((uint32_t)seg << 4) + off;
    return (uint16_t)(m->mem[at & 0xFFFFF] | m->mem[(at + 1) & 0xFFFFF] << 8);
}

static uint32_t dac_colour(const machine_t *m, unsigned index);

/* A colour word as the original's fill reads it (model_fill, VGAME 1377:0B09 with model_fill_begin 1377:01E0; the code is
 * documented in src/matched/matched.c). The high byte is the style: FF one colour (the low byte; a low byte of A0h or
 * more is darkened by the object's [48C4], never below its band base, low byte & F0); below FB a colour pair dithered
 * across columns and swapped on odd rows (here the two palette entries blended); FE AND, FD OR and FC stipple (here
 * drawn as the low byte's colour); FB clears without drawing (returns 0: nothing to paint). */
static int word_colour(const machine_t *m, unsigned word, unsigned darken, uint32_t *out)
{
    const unsigned ah = word >> 8 & 255, al = word & 255;
    if (ah == 0xFB) return 0;
    if (ah == 0xFF) {
        unsigned pixel = al;
        if (al >= 0xA0) {
            const unsigned base = al & 0xF0;
            pixel = al >= darken + base ? al - darken : base;      /* al - darken, not below the band base */
        }
        *out = dac_colour(m, pixel);
        return 1;
    }
    if (ah > 0xFB) { *out = dac_colour(m, al); return 1; }
    const uint32_t a = dac_colour(m, ah), b = dac_colour(m, al);
    *out = 0xFF000000u | ((((a >> 16 & 255) + (b >> 16 & 255)) / 2) << 16) | ((((a >> 8 & 255) + (b >> 8 & 255)) / 2) << 8) |
           (((a & 255) + (b & 255)) / 2);
    return 1;
}

/* The fill style of a colour word and the palette index it stands for: 0 solid (index in *idx), 1 AND, 2 OR, 3 stipple (value
 * in *idx), -1 nothing drawn (FBh). Shaded colours are darkened as word_colour says. */
/* R35: lines and dots of objects drawn beyond the original's reach shrink with distance. The original draws a terrain model only while its depth is
 * under 8191 units of its own ring (z >> 14 in the drawn level's units, then >> shrink); beyond that the width is one game pixel x reach / depth,
 * never under one screen pixel. The game's own drawing (shrink 0) and anything within reach keep the full width. The setting Far light size
 * (remaster_live_set_far_light_size); F117_FAR_LIGHT_SIZE overrides it for testing. */
static int g_far_light_size = -1;
void remaster_live_set_far_light_size(int perspective)
{
    const char *e = getenv("F117_FAR_LIGHT_SIZE");
    g_far_light_size = e ? atoi(e) != 0 : perspective != 0;
}
static int far_light_size(void)
{
    if (g_far_light_size < 0) remaster_live_set_far_light_size(1);     /* first use: the default, with the environment override applied */
    return g_far_light_size;
}
static double far_width(const remaster_prim *p, int k0, int k1, double width)
{
    if (!p->shrink || !far_light_size()) return width;
    const double z = ((double)p->v[k0].z + (double)p->v[k1].z) * 0.5;
    const double reach = (double)((8191ll << 14) >> p->shrink);
    if (z <= reach) return width;
    const double w = width * reach / z;
    return w < 1.0 ? 1.0 : w;
}

static int word_style(unsigned word, unsigned darken, unsigned *idx)
{
    const unsigned ah = word >> 8 & 255, al = word & 255;
    if (ah == 0xFB) return -1;
    if (ah == 0xFF) {
        unsigned pixel = al;
        if (al >= 0xA0) { const unsigned base = al & 0xF0; pixel = al >= darken + base ? al - darken : base; }
        *idx = pixel;
        return 0;
    }
    if (ah == 0xFE) { *idx = al; return 1; }
    if (ah == 0xFD) { *idx = al; return 2; }
    if (ah == 0xFC) { *idx = al; return 3; }
    *idx = ah;                                       /* a colour pair: the first colour stands for it */
    return 0;
}

/* the game's palette entry as 0xAARRGGBB; the DAC holds six bits a channel */
static const uint8_t *g_palette_override;                /* the composite on the present worker reads the frame's own palette, never the live machine's (which runs meanwhile); a plain variable, not thread-local: the composite's row helpers read it too */
void remaster_live_set_palette(const uint8_t *dac) { g_palette_override = dac; }
static F117_TLS const uint8_t *g_palette_thread;         /* the pipeline's setup thread reads its own picture's palette while the compose thread reads the previous one's */
void remaster_live_set_palette_thread(const uint8_t *dac) { g_palette_thread = dac; }
/* The thread that runs a copy (the main thread) never uses the override: it belongs to the composite on the worker, which may be running at the same time
 * (the copy overlaps the previous picture's composite), and a copy's colours come from its own machine's palette, as when nothing overlapped. */
static F117_TLS int g_override_ignored;
void remaster_live_ignore_palette_override(int on) { g_override_ignored = on; }
static uint32_t dac_colour(const machine_t *m, unsigned index)
{
    const uint8_t *ov = g_override_ignored ? NULL : g_palette_thread ? g_palette_thread : g_palette_override;
    const uint8_t *d = ov ? &ov[(index & 255) * 3] : &m->dac[(index & 255) * 3];
    const unsigned r = d[0] << 2 | d[0] >> 4, g = d[1] << 2 | d[1] >> 4, b = d[2] << 2 | d[2] >> 4;
    return 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

/* F117_REMASTER_DEBUG: how a polygon's colour word, index and shade relate to the pixel the original drew at the
 * polygon's centre (the machine's last scanned-out picture, 320 x 200). A research aid for the colour mapping. */
static void debug_colours(const remaster_live *r, const machine_t *m)
{
    enum { MAXK = 400 };
    static struct { unsigned word, index, shade, pixel, n; } k[MAXK];
    unsigned nk = 0;
    for (size_t i = 0; i < r->frame.n; i++) {
        const remaster_prim *p = &r->frame.prim[i];
        if (p->kind != SCENE_POLYGON || p->n < 3) continue;
        double x = 0, y = 0, z = 0;
        for (int v = 0; v < p->n; v++) { x += p->v[v].x; y += p->v[v].y; z += p->v[v].z; }
        x /= p->n; y /= p->n; z /= p->n;
        if (z < 0x10000) continue;
        const int sx = (int)(160.0 + 256.0 * x / z), sy = (int)(100.0 + 256.0 * y / z);
        if (sx < 0 || sx >= 320 || sy < 0 || sy >= 200) continue;
        const unsigned pixel = m->scan_pixels[sy * 320 + sx];
        unsigned j = 0;
        while (j < nk && !(k[j].word == p->colour_word && k[j].index == p->colour_index && k[j].shade == p->shade && k[j].pixel == pixel)) j++;
        if (j == nk && nk < MAXK) { k[nk].word = p->colour_word; k[nk].index = p->colour_index; k[nk].shade = p->shade; k[nk].pixel = pixel; k[nk].n = 0; nk++; }
        if (j < nk) k[j].n++;
    }
    for (unsigned j = 0; j < nk; j++)
        fprintf(stderr, "[colour] word %04X index %3u shade %3u -> pixel %3u  x%u\n", k[j].word, k[j].index, k[j].shade, k[j].pixel, k[j].n);
}

static uint32_t mean_colour(uint32_t a, uint32_t b)
{
    return 0xFF000000u | ((((a >> 16 & 255) + (b >> 16 & 255)) / 2) << 16) | ((((a >> 8 & 255) + (b >> 8 & 255)) / 2) << 8) |
           (((a & 255) + (b & 255)) / 2);
}

/* Draw the sky and ground bands, then the primitives of a frame, into t (w and h set). ux and uy are the target's pixels
 * per pixel of the game's 320 x 200 picture across and down: h/240 and h/200 for a picture at the 4:3 shape, N and N for the
 * picture N times finer that the Enhanced presentation keeps (320N x 200N, stretched to 4:3 later). */
static void draw_scene_ex(const machine_t *m, remaster_target *t, const remaster_frame *fr, const int32_t band_rec[],
                          int have_band, const int origin[2], const int window[4], double ux, double uy, double xoff, int wide, int stage,
                          double *timing);
static void draw_scene(const machine_t *m, remaster_target *t, const remaster_frame *fr, const int32_t band_rec[],
                       int have_band, const int origin[2], const int window[4], double ux, double uy)
{
    draw_scene_ex(m, t, fr, band_rec, have_band, origin, window, ux, uy, 0.0, 0, 0, NULL);
}

/* xoff is the target pixel at which the game's own 320-pixel-wide picture starts, and wide asks for the world to fill the
 * target: the bands run off every side and nothing is cut at the game's clip window. stage 0 draws everything; when workers share
 * the rows, stages 1, 2 and 3 are run one after another, each on every worker (the lights' depth test reads the rows above and
 * below, so a stage must be finished everywhere before the next starts): 1 the clear, the bands and the polygons, 2 the whole-line
 * pass, 3 the ordinary line pass and the mask outside the game's window. */
static void draw_scene_ex(const machine_t *m, remaster_target *t, const remaster_frame *fr, const int32_t band_rec[],
                          int have_band, const int origin[2], const int window[4], double ux, double uy, double xoff, int wide, int stage,
                          double *timing)
{
    const int do1 = stage == 0 || stage == 1, do2 = stage == 0 || stage == 2, do3 = stage == 0 || stage == 3;
    const double tq0 = timing ? now_ms() : 0.0;
    t->cx = origin[0] * ux + xoff; t->cy = origin[1] * uy;           /* the game's origin, in its 320 x 200 pixels */
    t->sx = 256.0 * ux; t->sy = 256.0 * uy;
    t->near_z = 0x10000;
    uint32_t palette[256];
    for (unsigned i = 0; i < 256; i++) palette[i] = dac_colour(m, i);
    t->palette = palette;
    t->paint_index = REMASTER_NO_INDEX;
    if (do1) remaster_clear(t, 0xFF000000u);
    const double tq1 = timing ? now_ms() : 0.0;
    if (timing) timing[0] += tq1 - tq0;
    if (do1 && have_band) {                     /* the sky and ground bands, as far as anything: the game's own state model */
        band_state st;
        band_exact_list ex;
        band_fine_list fine;
        const int loaded = band_state_load(band_rec, BAND_REC_FINE, &st);
        const int rx = loaded ? band_exact(&st, &ex) : -1, rf = loaded && !rx ? band_fine(&st, &ex, &fine) : -1;
        if (loaded && !rx && !rf) {
            const double far_z = 10000000.0;
            for (int q = 0; q < fine.n; q++) {
                if (!fine.q[q].valid) continue;
                remaster_v3 v[4];
                double ex[4], ey[4];
                for (int k = 0; k < 4; k++) { ex[k] = fine.q[q].x[k]; ey[k] = fine.q[q].y[k]; }
                if (wide) {                     /* the world has no edge at the clip window: a band is the strip between its two lines */
                    const double K = (double)(t->w + t->h);
                    double ux0 = ex[1] - ex[0], uy0 = ey[1] - ey[0], ux1 = ex[2] - ex[3], uy1 = ey[2] - ey[3];
                    const double l0 = sqrt(ux0 * ux0 + uy0 * uy0), l1 = sqrt(ux1 * ux1 + uy1 * uy1);
                    if (l0 > 1e-6 && l1 > 1e-6) {
                        ux0 /= l0; uy0 /= l0; ux1 /= l1; uy1 /= l1;
                        /* each line continued along its own direction, so a banked horizon stays a straight, banked horizon */
                        ex[0] -= ux0 * K; ey[0] -= uy0 * K; ex[1] += ux0 * K; ey[1] += uy0 * K;
                        ex[3] -= ux1 * K; ey[3] -= uy1 * K; ex[2] += ux1 * K; ey[2] += uy1 * K;
                        if (q % 16 == 15) {     /* the outermost band (the top of the sky, the bottom of the ground) goes on outward along its sides */
                            const double sxl = ex[3] - ex[0], syl = ey[3] - ey[0], ll = sqrt(sxl * sxl + syl * syl);
                            const double sxr = ex[2] - ex[1], syr = ey[2] - ey[1], lr = sqrt(sxr * sxr + syr * syr);
                            if (ll > 1e-6) { ex[3] += sxl / ll * K; ey[3] += syl / ll * K; }
                            if (lr > 1e-6) { ex[2] += sxr / lr * K; ey[2] += syr / lr * K; }
                        }
                    }
                }
                for (int k = 0; k < 4; k++) {   /* a screen point as a camera-space point at a very large depth */
                    v[k].x = (int32_t)((ex[k] * ux + xoff - t->cx) * far_z / t->sx);
                    v[k].y = (int32_t)((ey[k] * uy - t->cy) * far_z / t->sy);
                    v[k].z = (int32_t)far_z;
                }
                /* A smooth gradient instead of 16 flat bands: the edge between two bands takes the mean of their palette
                 * colours, the outer ends keep their own, and the colour runs linearly across each band. Corners 0 and 1
                 * are the edge nearer the horizon, 3 and 2 the farther one (band_state.h). Sky is bands 0 to 15, ground
                 * 16 to 31. */
                const int k = q % 16;
                const uint32_t own = dac_colour(m, fine.q[q].colour);
                const uint32_t before = k > 0 ? mean_colour(dac_colour(m, fine.q[q - 1].colour), own) : own;
                const uint32_t after = k < 15 && q + 1 < fine.n ? mean_colour(own, dac_colour(m, fine.q[q + 1].colour)) : own;
                const uint32_t cols[4] = { before, before, after, after };
                t->paint_index = fine.q[q].colour;      /* the flat band's palette index stands under the smooth colour */
                remaster_polygon_shaded(t, v, cols, 4);
            }
            const int recorded = remaster_trace_hook && remaster_trace_hook(REMASTER_T_ZRESET, t, NULL, 0, NULL, 0, 0.0, 0, 0);   /* recorded for the GPU: nothing was drawn, nothing to zero */
            if (!recorded)
                for (long i = (long)remaster_row_lo(t) * t->w; i < (long)remaster_row_hi(t) * t->w; i++) t->inv_z[i] = 0.0f;   /* the bands are behind everything */
        }
    }
    if (do1 && getenv("F117_REMASTER_LEFT")) {   /* diagnostic: how many primitives lie wholly left of the original's screen (the margin's), by model */
        unsigned count[64] = { 0 }; size_t n_left = 0;
        for (size_t i = 0; i < fr->n; i++) {
            const remaster_prim *p = &fr->prim[i];
            double x1b = -1e9; int front = 0;
            for (int k = 0; k < p->n; k++) if (p->v[k].z > 0) { const double sx = origin[0] + 256.0 * p->v[k].x / p->v[k].z; if (sx > x1b) x1b = sx; front++; }
            if (front && x1b < 0) { n_left++; count[p->model & 63]++; }
        }
        fprintf(stderr, "[left] clock %llu: %zu of %zu primitives wholly left of the screen; by model:", (unsigned long long)m->cpu.icount, n_left, fr->n);
        for (int k = 0; k < 64; k++) if (count[k]) fprintf(stderr, " %d:%u", k, count[k]);
        fprintf(stderr, "\n");
    }
    {   /* F117_REMASTER_PROBE=X,Y (game pixels): every primitive of one frame (the 700th drawn) whose screen box holds that point, and
         * how many primitives the frame has, to see why Remastered draws nothing where the original drew something */
        static int calls;
        const char *probe = getenv("F117_REMASTER_PROBE");
        double px = 0, py = 0, bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
        const char *pbox = getenv("F117_REMASTER_PROBE_BOX");     /* X0,Y0,X1,Y1: every primitive whose box overlaps this rectangle, not one point */
        const int use_box = pbox && sscanf(pbox, "%lf,%lf,%lf,%lf", &bx0, &by0, &bx1, &by1) == 4;
        if (use_box && !probe) probe = "0,0";
        static unsigned long long probe_from; static int probe_done, probe_init;
        if (!probe_init) { probe_init = 1; const char *f = getenv("F117_REMASTER_PROBE_FROM"); probe_from = f ? strtoull(f, NULL, 0) : 0; }   /* the first frame from this clock, else the 700th */
        const int probe_now = probe_from ? (!probe_done && m->cpu.icount >= probe_from && (probe_done = 1)) : ++calls == 700;
        if (do1 && probe && sscanf(probe, "%lf,%lf", &px, &py) == 2 && probe_now) {
            fprintf(stderr, "[probe] frame of %zu primitives, point %.1f,%.1f, clock %llu\n", fr->n, px, py, (unsigned long long)m->cpu.icount);
            for (size_t i = 0; i < fr->n; i++) {
                const remaster_prim *p = &fr->prim[i];
                double x0b = 1e9, x1b = -1e9, y0b = 1e9, y1b = -1e9;
                int front = 0;
                for (int k = 0; k < p->n; k++) {
                    if (p->v[k].z <= 0) continue;
                    const double sx = origin[0] + 256.0 * p->v[k].x / p->v[k].z, sy = origin[1] + 256.0 * p->v[k].y / p->v[k].z;
                    if (sx < x0b) x0b = sx; if (sx > x1b) x1b = sx; if (sy < y0b) y0b = sy; if (sy > y1b) y1b = sy;
                    front++;
                }
                if (front < p->n) { x0b = -1e9; x1b = 1e9; }      /* partly behind the eye: its box is unknown, show it */
                if (use_box ? (!front || x1b < bx0 || x0b > bx1 || y1b < by0 || y0b > by1)
                            : (!front || px < x0b - 1.5 || px > x1b + 1.5 || py < y0b - 1.5 || py > y1b + 1.5)) continue;
                uint32_t colour = 0;
                word_colour(m, p->colour_word, p->darken, &colour);
                fprintf(stderr, "[probe] prim %zu model %u kind %d n %d word %04X colour %06X behind %d box x %.1f..%.1f y %.1f..%.1f z %d inv %.6f true %.0f owner %08X flat %u\n",
                        i, (unsigned)p->model, (int)p->kind, p->n, (unsigned)p->colour_word, (unsigned)(colour & 0xFFFFFF), p->n - front, x0b, x1b, y0b, y1b, front ? (int)p->v[0].z : 0, p->inv_depth_scale, front ? (double)p->v[0].z / (p->inv_depth_scale > 0.0 ? p->inv_depth_scale : 1.0) : 0.0, p->owner, (unsigned)p->flat);
                if (p->kind == SCENE_LINES || p->kind == SCENE_DOTS || getenv("F117_REMASTER_PROBE_VERTICES"))
                    for (int k = 0; k < p->n; ++k)
                        fprintf(stderr, "[probe]   vertex %d: %d,%d,%d\n", k, p->v[k].x, p->v[k].y, p->v[k].z);
            }
        }
    }
    const double tq2 = timing ? now_ms() : 0.0;
    if (timing) timing[1] += tq2 - tq1;
    for (size_t i = 0; do1 && i < fr->n; i++) {
        const remaster_prim *p = &fr->prim[i];
        t->inv_depth_scale = p->inv_depth_scale;
        /* Ground can pass directly under the camera. Clipping it at the
         * model near plane exposes the background below it in tall margins. */
        t->near_z = p->flat ? remaster_ground_near_z(p->inv_depth_scale) : 0x10000;
        uint32_t colour;
        if (!word_colour(m, p->colour_word, p->darken, &colour)) continue;
        unsigned idx = 0;
        const int style = word_style(p->colour_word, p->darken, &idx);
        const double line_w = ux > 1.0 ? ux : 1.0;                    /* one of the original's pixels, at least one of ours */
        if (p->kind == SCENE_POLYGON && p->n >= 3 && style >= 1) {     /* AND, OR, stipple: combine with what is under it */
            remaster_polygon_logic(t, p->v, p->n, style == 1 ? REMASTER_AND : style == 2 ? REMASTER_OR : REMASTER_STIPPLE, idx);
            continue;
        }
        if (t->line_strong && (p->kind == SCENE_LINES || p->kind == SCENE_DOTS)) continue;      /* drawn after the polygons, below */
        t->paint_index = (uint16_t)idx;
        t->paint_owner = p->owner;                     /* the object and face the lines and dots on it belong to */
        {   static int show = -1;                       /* F117_FLAT_SHOW=1 (diagnostic): the level polygons in magenta */
            if (show < 0) show = getenv("F117_FLAT_SHOW") != NULL;
            if (show && p->flat) colour = 0xFFFF00FFu;
        }
        if (p->kind == SCENE_POLYGON && p->n >= 3) remaster_polygon(t, p->v, p->n, colour);
        else if (p->kind == SCENE_LINES)
            for (int k = 0; k + 1 < p->n; k++) remaster_line(t, p->v[k], p->v[k + 1], far_width(p, k, k + 1, line_w), colour);
        else if (p->kind == SCENE_DOTS)
            for (int k = 0; k < p->n; k++) remaster_line(t, p->v[k], p->v[k], far_width(p, k, k, line_w * 1.5), colour);
    }
    const double tq3 = timing ? now_ms() : 0.0;
    if (timing) timing[2] += tq3 - tq2;
    if (t->line_strong) {                       /* the lines, after the polygons: whole (huge bias) into line_strong, then as the depth allows */
        const size_t px = (size_t)(remaster_row_hi(t) - remaster_row_lo(t)) * t->w, off = (size_t)remaster_row_lo(t) * t->w;   /* this call's rows */
        static int decal = -1;
        if (decal < 0) { const char *e = getenv("F117_LINES"); decal = !(e && !strcmp(e, "old")); }
        /* Both decal passes use the same polygon depth and stroke order.
         * Only the legacy slope rules can produce a different whole line. */
        int any_bright = 0;
        for (size_t i = 0; !decal && i < fr->n && !any_bright; i++) {
            const remaster_prim *p = &fr->prim[i];
            uint32_t c;
            if ((p->kind == SCENE_LINES || p->kind == SCENE_DOTS) && word_colour(m, p->colour_word, p->darken, &c))
                any_bright = (int)(c >> 16 & 255) + (int)(c >> 8 & 255) + (int)(c & 255) > 200;
        }
        if (any_bright && do2) {                         /* the whole-line copy starts as the polygons alone */
            memcpy(t->line_strong + off, t->color + off, px * 4);
            memcpy(t->line_tmp_z + off, t->inv_z + off, px * 4);
        }
        t->line_order = 1;
        for (int pass = any_bright ? 0 : 1; pass < 2; pass++) {
            if (pass == 0 ? !do2 : !do3) continue;
            if (t->stroke_z && !g_record_only) memset(t->stroke_z + off, 0, px * sizeof *t->stroke_z);
            uint32_t *c0 = t->color; float *z0 = t->inv_z;
            if (pass == 0) { t->color = t->line_strong; t->inv_z = t->line_tmp_z; t->line_ref_z = z0; }
            else { t->line_bias = 0.0; t->line_slope = 0.0; t->line_ref_z = NULL; t->line_decal = 0; }   /* the ordinary pass has no slope allowance */
            for (size_t i = 0; i < fr->n; i++) {
                const remaster_prim *p = &fr->prim[i];
                uint32_t colour;
                if (p->kind != SCENE_LINES && p->kind != SCENE_DOTS) continue;
                if (!word_colour(m, p->colour_word, p->darken, &colour)) continue;
                const int dark = (int)(colour >> 16 & 255) + (int)(colour >> 8 & 255) + (int)(colour & 255) <= 200;   /* dark: never taken whole, but drawn whole over an earlier bright line (a flag over its pole) as the original's order has it */
                unsigned idx = 0;
                word_style(p->colour_word, p->darken, &idx);
                const double line_w = ux > 1.0 ? ux : 1.0;
                t->paint_index = (uint16_t)idx;
                t->inv_depth_scale = p->inv_depth_scale;
                t->near_z = p->flat ? remaster_ground_near_z(p->inv_depth_scale) : 0x10000;
                /* Lines and dots are decals on the face they belong to (remaster_raster.h, line_decal): over their own face, over the
                 * faces of their own object unless clearly nearer, and behind anything else that is nearer, in both passes.
                 * F117_LINES=old (diagnostic) restores the earlier rules: whole lines with a 50x bias, lights by the ground's slope. */
                t->paint_owner = p->owner;
                if (decal) { t->line_decal = 1; t->line_bias = 1.0; t->line_slope = 0.0; }
                else if (pass == 0) {
                    int point = p->kind == SCENE_DOTS;               /* a light is a line from a point to itself */
                    if (p->kind == SCENE_LINES) {
                        point = 1;
                        for (int k = 1; k < p->n && point; k++) point = p->v[k].x == p->v[0].x && p->v[k].y == p->v[0].y && p->v[k].z == p->v[0].z;
                    }
                    if (!point || dark) { t->line_bias = 50.0; t->line_slope = 0.0; }
                    else { t->line_bias = 1.01; t->line_slope = 1.0 + (ux > 1.0 ? ux : 1.0); }
                }
                if (p->kind == SCENE_LINES) for (int k = 0; k + 1 < p->n; k++) remaster_line(t, p->v[k], p->v[k + 1], far_width(p, k, k + 1, line_w), colour);
                else for (int k = 0; k < p->n; k++) remaster_line(t, p->v[k], p->v[k], far_width(p, k, k, line_w * 1.5), colour);
            }
            t->color = c0; t->inv_z = z0;
        }
        t->line_bias = 0.0; t->line_slope = 0.0; t->line_ref_z = NULL; t->line_order = 0; t->line_decal = 0;
        if (!any_bright && do3 && !g_record_only) memcpy(t->line_strong + off, t->color + off, px * 4);   /* no whole lines: equal to the final picture, so nothing is taken from it */
    }
    if (timing) timing[3] += now_ms() - tq3;
    if (!wide && do3) {                         /* nothing outside the game's clip window */
        const int x0 = (int)(window[0] * ux), x1 = (int)((window[2] + 1) * ux);
        const int y0 = (int)(window[1] * uy), y1 = (int)((window[3] + 1) * uy);
        if (remaster_trace_hook) { const remaster_v3 box[2] = { { x0, y0, 0 }, { x1, y1, 0 } }; remaster_trace_hook(REMASTER_T_WINDOW, t, box, 2, NULL, 0, 0.0, 0, 0); }
        for (int y = remaster_row_lo(t); y < remaster_row_hi(t); y++)
            for (int x = 0; x < t->w; x++)
                if (x < x0 || x >= x1 || y < y0 || y >= y1) t->color[(long)y * t->w + x] = 0xFF000000u;
    }
}

static void write_frame(remaster_live *r, const machine_t *m)
{
    remaster_target t;
    t.w = r->w; t.h = r->h;
    t.color = (uint32_t *)malloc((size_t)r->w * r->h * 4);
    t.inv_z = (float *)malloc((size_t)r->w * r->h * 4);
    t.index = (uint16_t *)malloc((size_t)r->w * r->h * 2);
    t.stroke_z = NULL; t.stroke_aux_x = t.stroke_aux_y = NULL; t.stroke_owner = NULL; t.line_point = 0;
    t.inv_depth_scale = 1.0;
    t.line_bias = 0.0; t.line_slope = 0.0; t.line_ref_z = NULL; t.line_strong = NULL; t.line_tmp_z = NULL; t.line_order = 0; t.owner = NULL; t.paint_owner = 0; t.line_decal = 0;
    if (!t.color || !t.inv_z || !t.index) { free(t.color); free(t.inv_z); free(t.index); return; }
    if (getenv("F117_REMASTER_DEBUG")) {
        debug_colours(r, m);
        fprintf(stderr, "[view] origin %d,%d window x %d..%d y %d..%d\n", r->origin[0], r->origin[1], r->window[0], r->window[2], r->window[1], r->window[3]);
    }
    draw_scene(m, &t, &r->frame, r->band_rec, r->have_band, r->origin, r->window, r->h / 240.0, r->h / 200.0);
    FILE *f = fopen(r->out_path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", r->w, r->h);
        for (long i = 0; i < (long)r->w * r->h; i++) {
            const uint32_t c = t.color[i];
            const unsigned char px[3] = { (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c };
            fwrite(px, 1, 3, f);
        }
        fclose(f);
        r->written = 1;
    }
    free(t.color);
    free(t.inv_z);
    free(t.index);
    if (r->written && strlen(r->out_path) < 900) {                  /* the original's last picture, 320 x 200, beside it */
        char path[1024];
        snprintf(path, sizeof path, "%s.orig.ppm", r->out_path);
        FILE *o = fopen(path, "wb");
        if (o) {
            fprintf(o, "P6\n320 200\n255\n");
            for (int i = 0; i < 64000; i++) {
                const uint8_t *d = &m->scan_dac[m->scan_pixels[i] * 3];
                const unsigned char px[3] = { (unsigned char)(d[0] << 2 | d[0] >> 4), (unsigned char)(d[1] << 2 | d[1] >> 4), (unsigned char)(d[2] << 2 | d[2] >> 4) };
                fwrite(px, 1, 3, o);
            }
            fclose(o);
        }
    }
}

/* Pixels the Enhanced presentation drew for the 3D view (model polygons, the sky and ground strips, and their fallbacks):
 * the ones Remastered replaces. Strokes, text, sprites and blits (the HUD, the panels, the cockpit art) stay. */
static int is_scene_tag(uint8_t tag) { return (tag >= DT_MODEL_SOLID && tag <= DT_SPAN_SHADE) || tag == DT_BAND_STATE; }
/* The tags of span-rasteriser fills: the sky and ground strips, drawn from the band state (DT_BAND_STATE, band_state.h) or
 * refined/coarse (and the map regions, which lie outside the 3D window). */
static int is_span_tag(uint8_t tag) { return tag == DT_SPANPOLY_FINE || tag == DT_BAND_STATE || (tag >= DT_SPAN_NOT_POLY && tag <= DT_SPAN_SHADE); }

/* The nearest of the 256 palette entries (six-bit channels) to an 0xAARRGGBB colour, with a small cache. */
/* The cache is keyed by the palette's contents (a hash of its 768 bytes, made when a worker starts its share), not its address: the same
 * buffer holds a different palette every few pictures, and a stale answer would depend on what was asked before. */
static F117_TLS const uint8_t *g_np_dac;
static F117_TLS uint64_t g_np_key;
static void nearest_reset(void) { g_np_dac = NULL; }
static uint8_t nearest_index(const uint8_t *dac, uint32_t c)
{
    static F117_TLS struct { uint32_t colour; uint8_t index; uint8_t valid; uint64_t palette; } cache[4096];
    if (g_np_dac != dac) {
        uint64_t h = 1469598103934665603ull;
        for (int i = 0; i < 768; i++) h = (h ^ dac[i]) * 1099511628211ull;
        g_np_key = h | 1;
        g_np_dac = dac;
    }
    const unsigned slot = (c * 2654435761u) >> 20 & 4095;
    if (cache[slot].valid && cache[slot].colour == c && cache[slot].palette == g_np_key) return cache[slot].index;
    const int cr = (int)(c >> 16 & 255), cg = (int)(c >> 8 & 255), cb = (int)(c & 255);
    unsigned best = 0;
    long bestd = -1;
    for (unsigned i = 0; i < 256; i++) {
        const int r = dac[i * 3] << 2 | dac[i * 3] >> 4, g = dac[i * 3 + 1] << 2 | dac[i * 3 + 1] >> 4, b = dac[i * 3 + 2] << 2 | dac[i * 3 + 2] >> 4;
        const long d = (long)(r - cr) * (r - cr) + (long)(g - cg) * (g - cg) + (long)(b - cb) * (b - cb);
        if (bestd < 0 || d < bestd) { bestd = d; best = i; }
    }
    cache[slot].colour = c; cache[slot].index = (uint8_t)best; cache[slot].valid = 1; cache[slot].palette = g_np_key;
    return (uint8_t)best;
}

/* The picture's rows are shared among workers when the host offers them (remaster_live_set_parallel). */
static void (*g_par)(void (*fn)(void *, int), void *arg, int parts);
static int g_par_parts = 1;
void remaster_live_set_parallel(void (*run)(void (*fn)(void *, int), void *arg, int parts), int parts)
{
    g_par = run;
    g_par_parts = !run || parts < 1 ? 1 : parts > REMASTER_PARTS ? REMASTER_PARTS : parts;
}

typedef struct {
    const machine_t *m;
    remaster_target base;
    const remaster_frame *fr;
    const int32_t *band;
    int have_band, wide, parts, stage;
    const int *origin, *window;
    double n, xoff;
    /* A worker owns its counters; only the caller combines them after all
     * stage barriers. Never update shared floating-point accumulators here. */
    double (*timing)[4];
    int row_tile;
    decal_face_cache *faces;
} scene_job;

static void scene_rows_part(void *arg, int part)
{
    const scene_job *j = (const scene_job *)arg;
    remaster_target t = j->base;
    void *previous_faces = remaster_faces_use(j->faces ? &j->faces[part] : NULL);
    if (j->parts > 1 && j->row_tile > 0) {
        /* Spread the narrow, busy horizon across workers rather than leaving
         * its lines and models in one contiguous worker band. A stage still
         * finishes all rows before any worker starts the next stage. */
        for (int row = part * j->row_tile; row < t.h; row += j->parts * j->row_tile) {
            t = j->base;        /* every tile starts from the base state, as the untiled call does: a draw leaves paint_owner and the line state set,
                                 * and the sky and ground bands, which set no owner, would otherwise be recorded under the previous tile's last owner */
            t.y_lo = row;
            t.y_hi = row + j->row_tile < t.h ? row + j->row_tile : t.h;
            draw_scene_ex(j->m, &t, j->fr, j->band, j->have_band, j->origin, j->window, j->n, j->n, j->xoff, j->wide, j->stage,
                          j->timing ? j->timing[part] : NULL);
        }
    } else {
        if (j->parts > 1) { t.y_lo = t.h * part / j->parts; t.y_hi = t.h * (part + 1) / j->parts; }
        draw_scene_ex(j->m, &t, j->fr, j->band, j->have_band, j->origin, j->window, j->n, j->n, j->xoff, j->wide, j->stage,
                      j->timing ? j->timing[part] : NULL);
    }
    remaster_faces_use(previous_faces);
}

static void run_parts(void (*fn)(void *, int), void *arg, int parts)
{
    if (parts > 1 && g_par) g_par(fn, arg, parts);
    else fn(arg, 0);
}

typedef struct {
    const uint8_t *src, *tag, *dac;
    uint8_t *dst, *mask;
    uint32_t *rgb, *wide_out;
    const remaster_target *t;
    int w, h, tw, xoff, wide, x0, x1, y0, y1, parts, complete;
    long replaced[REMASTER_PARTS], kept[REMASTER_PARTS][64];
} replace_job;

/* A whole line (the strong pass draws every line at any depth) is taken only where it is not clearly behind a nearer surface: a line that
 * belongs to a surface (a door line on its wall, a marking on the ground) lies at about the surface's depth, but the horizon line is far
 * behind a jet or a hangar and must not show through its thin parts (a tail, a wing). The weak pass left the surface's depth in inv_z where it
 * rejected the line; the strong pass left the line's own depth in line_tmp_z. 1/z: the line is behind when its value is under 0.9 of the
 * surface's. F117_WIDE_LINE_HIDE=0 (diagnostic) takes every whole line as before. */
static int line_behind_surface(const remaster_target *t, long ti)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("F117_WIDE_LINE_HIDE"); on = !(e && e[0] == '0'); }
    if (!on || !t->line_tmp_z) return 0;
    const float surface = t->inv_z[ti], line = t->line_tmp_z[ti];
    return surface > 0.0f && line > 0.0f && line < surface * 0.9f;
}

/* One worker's rows of the replace loop (and of the wide picture): every pixel depends only on its own row and on read-only buffers. */
static void replace_part(void *arg, int part)
{
    replace_job *j = (replace_job *)arg;
    nearest_reset();
    const uint8_t *src = j->src, *tag = j->tag, *dac = j->dac;
    uint8_t *dst = j->dst, *mask = j->mask;
    uint32_t *rgb = j->rgb, *wide_out = j->wide_out;
    const remaster_target t = *j->t;
    const int w = j->w, h = j->h, tw = j->tw, xoff = j->xoff, wide = j->wide, x0 = j->x0, x1 = j->x1, y0 = j->y0, y1 = j->y1;
    const int rlo = h * part / j->parts, rhi = h * (part + 1) / j->parts;
    int ry0 = y0 < 0 ? 0 : y0, ry1 = y1 < h ? y1 : h;
    if (ry0 < rlo) ry0 = rlo;
    if (ry1 > rhi) ry1 = rhi;
    long kept_hist[64] = { 0 };
    long replaced = 0;
    for (int y = ry0; y < ry1; y++)
        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < w; x++) {
            const long i = (long)y * w + x, ti = (long)y * tw + x + xoff;     /* i: the game's picture; ti: the same pixel on the canvas */
            /* A complete reconstructed scene owns its background as well as
             * its surfaces. Retaining a native polygon where a corrected
             * shoreline moved away would paste the old shore back over the
             * new ground and make the centre disagree with the margins.
             * Incomplete captures retain the older conservative fallback. */
            /* Remastered owns model outlines even where its depth test removed the line.
             * Requiring geometry here resurrects the original's stale door strips on
             * the ground just outside the hangar's new silhouette. */
            const int outline = tag[i] == DT_OUTLINE_FINE || tag[i] == DT_OUTLINE_COARSE;
            /* Strokes keep polygon depth read-only. Their own coverage must
             * still replace original model pixels where a light or pole
             * extends beyond the new polygon silhouette. Otherwise an old
             * polygon row cuts a stripe through the correctly drawn light. */
            if ((is_scene_tag(tag[i]) || outline) &&
                (j->complete || outline || t.inv_z[ti] > 0.0f || (t.stroke_z && t.stroke_z[ti] > 0.0f) || is_span_tag(tag[i]))) {
                uint32_t col = t.color[ti];
                /* Only bright lines (the doors' yellow, the lights, the runway markings): the dark dots and lines the original draws
                 * behind a door must stay behind it. */
                const uint32_t ls = t.line_strong[ti];
                const int bright = (int)(ls >> 16 & 255) + (int)(ls >> 8 & 255) + (int)(ls & 255) > 200;
                if (bright && ls != col && !line_behind_surface(&t, ti)) {   /* a line pixel the depth test shortened: whole where the original drew a line next to it */
                    int near_line = 0;
                    /* The original's lines and lights can lag one logic step behind the polygons (stale projection records): up to
                     * about four of its pixels, twelve of ours, from where Remastered draws them. */
                    for (int dy = -12; dy <= 12 && !near_line; dy++)
                        for (int dx = -12; dx <= 12; dx++) {
                            const int yy = y + dy, xx = x + dx;
                            if (yy < 0 || yy >= h || xx < 0 || xx >= w) continue;
                            const long ni = (long)yy * w + xx;
                            const uint8_t tg = tag[ni];
                            if (tg != DT_OUTLINE_FINE && tg != DT_OUTLINE_COARSE) continue;
                            /* the original's line pixel must be this line's colour (the doors' yellow beside the doors' yellow, a
                             * light beside its light), or a line the original hides would show */
                            const unsigned oc = src[ni];
                            const int dr = (int)(dac[oc * 3] << 2) - (int)(ls >> 16 & 255), dg = (int)(dac[oc * 3 + 1] << 2) - (int)(ls >> 8 & 255),
                                      db = (int)(dac[oc * 3 + 2] << 2) - (int)(ls & 255);
                            if (abs(dr) + abs(dg) + abs(db) < 96) { near_line = 1; break; }
                        }
                    if (near_line) col = ls;
                }
                dst[i] = nearest_index(dac, col);       /* the palette index, for anything that wants one */
                if (rgb && mask) { rgb[i] = col; mask[i] = 1; }   /* and the true colour, which is what is shown */
                replaced++;
            } else kept_hist[tag[i] & 63]++;
        }
    if (wide) {                                 /* the wide picture: the margins from the canvas, the middle from the composite */
        for (int y = rlo; y < rhi; y++) {
            uint32_t *row = wide_out + (long)y * tw;
            const uint32_t *col = t.color + (long)y * tw, *ls = t.line_strong + (long)y * tw;
            for (int x = 0; x < tw; x++) {
                /* The margins have no original picture to say where a line is visible, so a bright line (the doors' yellow, the lights)
                 * is taken whole there, as beside an original line in the middle; the rest is the depth-tested render. */
                const uint32_t c = ls[x];
                const int bright = (int)(c >> 16 & 255) + (int)(c >> 8 & 255) + (int)(c & 255) > 200;
                row[x] = bright && c != col[x] && !line_behind_surface(&t, (long)y * tw + x) ? c : col[x];
            }
            for (int x = 0; x < w; x++) {       /* the game's own picture, centred: Remastered's pixel where it replaced one, the original's elsewhere */
                const long i = (long)y * w + x;
                if (mask[i]) { row[xoff + x] = rgb[i]; continue; }
                const unsigned c = src[i];
                const unsigned cr = dac[c * 3], cg = dac[c * 3 + 1], cb = dac[c * 3 + 2];
                row[xoff + x] = 0xFF000000u | (cr << 2 | cr >> 4) << 16 | (cg << 2 | cg >> 4) << 8 | (cb << 2 | cb >> 4);
            }
        }
    }
    j->replaced[part] = replaced;
    memcpy(j->kept[part], kept_hist, sizeof kept_hist);
}

/* Whether the scene owns its background as well as its surfaces (a complete reconstruction: the sky and ground bands can be rebuilt from the recorded state). */
static int scene_is_complete(const remaster_live *r)
{
    int complete = r->done_complete && r->done_have_band && !getenv("F117_REMASTER_NATIVE_FOOTPRINT");
    if (complete) {
        band_state background;
        band_exact_list exact;
        band_fine_list fine;
        complete = band_state_load(r->done_band, BAND_REC_FINE, &background) &&
            !band_exact(&background, &exact) && !band_fine(&background, &exact, &fine) && fine.n > 0;
    }
    return complete;
}

/* The GPU compositor's inputs from the scene alone (not the game's picture, tags or palette): the scene's raster calls are recorded by running its drawing with the per-thread
 * hook set to skip the CPU's fill, and the window and flags are filled in. The drawing never touches the target's buffers while recording, so a stand-in target serves. 0 when
 * recording failed. palette receives the 256 colours the AND, OR and stipple fills read (the current palette). */
/* Part B (16 November 2026): the scene's raster calls are recorded in two parts on two threads. Stage 1 of draw_scene_ex (the clear, the bands and the polygons) is
 * recorded on the calling thread, stages 2 and 3 (the whole-line pass, then the ordinary line pass and the mask) on a helper thread, and the helper's records are appended to
 * the caller's: the order of a serial draw (stage 0 runs 1, 2, 3 in that order). The stages already run one after another on separate calls in the CPU compositor with the
 * target state reset for each (scene_rows_part), so neither reads state the other leaves. The recorder's state (the hook, the record buffer, the flags) is thread-local.
 * F117_RECORD_SPLIT=0 records serially; F117_RECORD_CHECK=1 records both ways every picture and compares the records byte for byte. One caller at a time (the helper's
 * job is a static). */
#ifdef _WIN32
static struct { HANDLE th, go, done; const scene_job *sj; int quit; gpu_trace_rec *recs; size_t n; int failed; } g_rec;
static void rec_run_lines(void)
{
    g_trace_n = 0; g_record_failed = 0; g_record_only = 1; remaster_trace_hook = gpu_trace_hook;
    scene_job b = *g_rec.sj; b.timing = NULL;
    b.stage = 2; run_parts(scene_rows_part, &b, 1);
    b.stage = 3; run_parts(scene_rows_part, &b, 1);
    remaster_trace_hook = NULL; g_record_only = 0;
    g_rec.recs = g_trace_recs; g_rec.n = g_trace_n; g_rec.failed = g_record_failed;
}
static DWORD WINAPI rec_thread_main(LPVOID u)
{
    (void)u;
    for (;;) {
        WaitForSingleObject(g_rec.go, INFINITE);
        if (g_rec.quit) break;
        rec_run_lines();
        ReleaseSemaphore(g_rec.done, 1, NULL);
    }
    return 0;
}
static int rec_split_ready(void)
{
    if (g_rec.th) return 1;
    g_rec.go = CreateSemaphoreA(NULL, 0, 1, NULL); g_rec.done = CreateSemaphoreA(NULL, 0, 1, NULL);
    if (g_rec.go && g_rec.done) g_rec.th = CreateThread(NULL, 0, rec_thread_main, NULL, 0, NULL);
    return g_rec.th != NULL;
}
#endif

/* Records the scene (stage 0) serially, or in two parts when the helper is available. Returns with the records in g_trace_recs / g_trace_n. */
static void record_scene(scene_job *sj)
{
    static int mode = -1;      /* 0 serial, 1 split, 2 split and checked against serial */
    if (mode < 0) {
        const char *e = getenv("F117_RECORD_SPLIT");
        mode = e && e[0] == '0' ? 0 : getenv("F117_RECORD_CHECK") ? 2 : 1;
    }
#ifdef _WIN32
    if (mode && rec_split_ready()) {
        g_rec.sj = sj; g_rec.recs = NULL; g_rec.n = 0; g_rec.failed = 0;
        ReleaseSemaphore(g_rec.go, 1, NULL);
        g_trace_n = 0; g_record_failed = 0; g_record_only = 1; remaster_trace_hook = gpu_trace_hook;
        scene_job a = *sj; a.stage = 1; run_parts(scene_rows_part, &a, 1);
        remaster_trace_hook = NULL; g_record_only = 0;
        WaitForSingleObject(g_rec.done, INFINITE);
        if (g_rec.failed) g_record_failed = 1;
        if (!g_record_failed && g_rec.n) {
            const size_t need = g_trace_n + g_rec.n;
            if (need > g_trace_cap) {
                size_t cap = g_trace_cap ? g_trace_cap : 4096; while (cap < need) cap *= 2;
                gpu_trace_rec *q = (gpu_trace_rec *)realloc(g_trace_recs, cap * sizeof *q);
                if (!q) g_record_failed = 1; else { g_trace_recs = q; g_trace_cap = cap; }
            }
            if (!g_record_failed) { memcpy(g_trace_recs + g_trace_n, g_rec.recs, g_rec.n * sizeof *g_trace_recs); g_trace_n = need; }
        }
        if (mode == 2) {       /* the serial recording, compared with this one byte for byte */
            static unsigned n, bad;
            const size_t n1 = g_trace_n; gpu_trace_rec *saved = (gpu_trace_rec *)malloc((n1 ? n1 : 1) * sizeof *saved);
            if (saved) {
                memcpy(saved, g_trace_recs, n1 * sizeof *saved);
                g_trace_n = 0; g_record_failed = 0; g_record_only = 1; remaster_trace_hook = gpu_trace_hook;
                run_parts(scene_rows_part, sj, 1);
                remaster_trace_hook = NULL; g_record_only = 0;
                const int same = g_trace_n == n1 && !memcmp(saved, g_trace_recs, n1 * sizeof *saved);
                n++; if (!same) bad++;
                if (!same && bad < 4) fprintf(stderr, "[record-check] the split recording differs from the serial one (picture %u: %zu vs %zu records)\n", n, n1, g_trace_n);
                if (n % 120 == 0) fprintf(stderr, "[record-check] %u pictures checked, %u differ\n", n, bad);
                free(saved);
            }
        }
        return;
    }
#endif
    g_trace_n = 0; g_record_failed = 0; g_record_only = 1; remaster_trace_hook = gpu_trace_hook;
    run_parts(scene_rows_part, sj, 1);
    remaster_trace_hook = NULL; g_record_only = 0;
}

static F117_TLS double g_last_record_ms, g_last_record_n;
void remaster_live_last_record(double *ms, double *records) { *ms = g_last_record_ms; *records = g_last_record_n; }
static int gpu_prepare(remaster_live *r, const machine_t *m, int n, int w, int h, int tw, int xoff, int complete, gpu_compose_in *gin, uint32_t *palette)
{
    static uint8_t stand_in[64];
    remaster_target t;
    memset(&t, 0, sizeof t);
    t.w = tw; t.h = h;
    t.color = (uint32_t *)stand_in; t.inv_z = (float *)stand_in; t.index = (uint16_t *)stand_in; t.owner = (uint32_t *)stand_in;
    t.line_strong = (uint32_t *)stand_in; t.line_tmp_z = (float *)stand_in;
    t.stroke_z = (float *)stand_in; t.stroke_aux_x = (float *)stand_in; t.stroke_aux_y = (float *)stand_in; t.stroke_owner = (uint32_t *)stand_in;
    t.inv_depth_scale = 1.0;
    scene_job sj = { m, t, &r->done, r->done_band, r->done_have_band, 1, 1, 0, r->done_origin, r->done_window, (double)n, (double)xoff };
    const int timed = getenv("F117_REMASTER_TIMING") != NULL;
    double tm[4] = { 0 };
    sj.timing = timed ? &tm : NULL; sj.row_tile = 0;
    const double t_prepare = timed ? now_ms() : 0.0;
    const double t_rec0 = now_ms();
    record_scene(&sj);
    g_last_record_ms = now_ms() - t_rec0; g_last_record_n = (double)g_trace_n;
    if (timed) {        /* diagnostic: where the scene recording goes, every 120th picture (stderr): the clear, the bands, the primitives, the lines */
        static double sum[5], recs; static unsigned count;
        for (int k = 0; k < 4; k++) sum[k] += tm[k];
        sum[4] += now_ms() - t_prepare; recs += (double)g_trace_n;
        if (++count % 120 == 0)
            fprintf(stderr, "[gpu-prepare] mean of %u: clear %.2f, bands %.2f, primitives %.2f, lines %.2f, total %.2f ms; %.0f records\n",
                    count, sum[0] / count, sum[1] / count, sum[2] / count, sum[3] / count, sum[4] / count, recs / count);
    }
    if (g_record_failed) return 0;
    for (unsigned i = 0; i < 256; i++) palette[i] = dac_colour(m, i);
    static uint8_t flags[256]; static int flags_done;
    if (!flags_done) {
        for (int g = 0; g < 256; g++)
            flags[g] = (uint8_t)((is_scene_tag((uint8_t)g) ? 1 : 0) | ((g == DT_OUTLINE_FINE || g == DT_OUTLINE_COARSE) ? 2 : 0) | (is_span_tag((uint8_t)g) ? 4 : 0));
        flags_done = 1;
    }
    memset(gin, 0, sizeof *gin);
    gin->recs = g_trace_recs; gin->nrec = (uint32_t)g_trace_n; gin->palette = palette;
    gin->w = w; gin->h = h; gin->tw = tw; gin->xoff = xoff;
    gin->x0 = (int)(r->done_window[0] * n); gin->x1 = (int)((r->done_window[2] + 1) * n);
    gin->y0 = (int)(r->done_window[1] * n); gin->y1 = (int)((r->done_window[3] + 1) * n);
    gin->complete = complete; gin->tagflags = flags;
    return 1;
}

/* Starts the GPU compositor's drawing of the picture about to be made from this scene, before the game's own picture exists (that is made on the CPU meanwhile): the compose that
 * follows finishes it. 0 when there is no GPU compositor, the picture is not wide, or the scene cannot be handled. */
int remaster_live_gpu_begin(remaster_live *r, const machine_t *m, int n, int wide_w)
{
    if (!g_gpu_begin_fn || !g_gpu_finish_fn || !r || !r->have_done || n < 1) return 0;
    r->gpu_begun = 0;
    return r->gpu_begun = remaster_live_gpu_begin_ex(r, m, n, wide_w);
}

int remaster_live_gpu_begin_ex(remaster_live *r, const machine_t *m, int n, int wide_w)
{
    if (!g_gpu_begin_fn || !g_gpu_finish_fn || !r || !r->have_done || n < 1) return 0;
    const int w = 320 * n, h = 200 * n;
    if (!(wide_w > w)) return 0;
    uint32_t palette[256];
    gpu_compose_in gin;
    if (!gpu_prepare(r, m, n, w, h, wide_w, (wide_w - w) / 2, scene_is_complete(r), &gin, palette)) return 0;
    return g_gpu_begin_fn(&gin);
}

void remaster_live_set_gpu_begun(remaster_live *r, int begun) { if (r) r->gpu_begun = begun != 0; }

int remaster_live_compose(remaster_live *r, const machine_t *m, int n, const uint8_t *src, const uint8_t *tag,
                          const uint8_t *dac, uint8_t *dst, uint32_t *rgb, uint8_t *mask, int wide_w, uint32_t *wide_out)
{
    if (!r || !r->have_done || n < 1 || !src || !tag || !dac || !dst) return 0;
    const int w = 320 * n, h = 200 * n;
    /* A wide picture (wide_w > w) is drawn once, on a canvas wide_w across with the game's own picture xoff pixels in: the composite
     * reads its centre, and the margins and the centre together are the wide picture (wide_out). */
    const int tw = wide_w > w && mask && rgb && wide_out ? wide_w : w, xoff = (tw - w) / 2, wide = tw > w;
    int complete = scene_is_complete(r);
    {   /* F117_PRIM_HIST=FROM (diagnostic): the primitives of the first composite from that clock, counted by model and kind (to diff one walk mode against another) */
        static unsigned done_hist; const char *hs = done_hist >= 6 ? NULL : getenv("F117_PRIM_HIST");
        if (hs && r->done.n > 50 && m->cpu.icount >= strtoull(hs, NULL, 0)) {
            done_hist++;
            static unsigned cnt[1024][3];
            memset(cnt, 0, sizeof cnt);
            for (size_t i = 0; i < r->done.n; i++) cnt[r->done.prim[i].model & 1023][r->done.prim[i].kind % 3]++;
            fprintf(stderr, "[prim-hist] #%u clock %llu, %zu primitives, scene %p (model:polygons/lines/dots):", done_hist, (unsigned long long)m->cpu.icount, r->done.n, (void *)r->scene);
            for (int k = 0; k < 1024; k++) if (cnt[k][0] || cnt[k][1] || cnt[k][2]) fprintf(stderr, " %d:%u/%u/%u", k, cnt[k][0], cnt[k][1], cnt[k][2]);
            fprintf(stderr, "\n");
            {   /* z of the lines and dots relative to the original's reach (8191 << 14 in the object's own ring, >> shrink in the drawn level's units), by shrink (R35) */
                double lo[16], hi[16]; unsigned cnt[16]; for (int k = 0; k < 16; k++) { lo[k] = 1e30; hi[k] = 0; cnt[k] = 0; }
                for (size_t i = 0; i < r->done.n; i++) {
                    const remaster_prim *q = &r->done.prim[i];
                    if (q->kind == SCENE_POLYGON || q->n < 1 || q->shrink > 15) continue;
                    const double reach = (double)((8191ll << 14) >> q->shrink);
                    for (int j = 0; j < q->n; j++) {
                        const double ratio = q->v[j].z / reach;
                        if (ratio < lo[q->shrink]) lo[q->shrink] = ratio;
                        if (ratio > hi[q->shrink]) hi[q->shrink] = ratio;
                        cnt[q->shrink]++;
                    }
                }
                for (int k = 0; k < 16; k++) if (cnt[k]) fprintf(stderr, "[prim-z] #%u shrink %d: %u vertices, z / reach min %.3f max %.3f\n", done_hist, k, cnt[k], lo[k], hi[k]);
            }
            {   /* by source (remaster_prim_source): the game's own drawing, the native walk, placed through the game, placed through the game and deferred; models 37, 39, 40, 44, polygons/lines/dots */
                static const int models[4] = { 37, 39, 40, 44 };
                for (int mi = 0; mi < 4; mi++) {
                    unsigned s[4][3]; memset(s, 0, sizeof s);
                    for (size_t i = 0; i < r->done.n; i++) {
                        const remaster_prim *q = &r->done.prim[i];
                        if ((q->model & 1023) == models[mi]) s[q->source & 3][q->kind % 3]++;
                    }
                    fprintf(stderr, "[prim-src] #%u model %d  game %u/%u/%u  native %u/%u/%u  guest %u/%u/%u  guest-deferred %u/%u/%u\n", done_hist, models[mi],
                            s[0][0], s[0][1], s[0][2], s[1][0], s[1][1], s[1][2], s[2][0], s[2][1], s[2][2], s[3][0], s[3][1], s[3][2]);
                }
            }
        }
    }
    remaster_target t;
    t.w = tw; t.h = h;
    /* The buffers are kept between pictures (grown, never freed): allocating and freeing about 13 MB every picture made the
     * system zero them every time. */
    static struct { void *p; size_t cap; } pool[10];
    const size_t need[10] = { (size_t)tw * h * 4, (size_t)tw * h * 4, (size_t)tw * h * 2, (size_t)tw * h * 4, (size_t)tw * h * 4, (size_t)tw * h * 4, (size_t)tw * h * 4, (size_t)tw * h * 4, (size_t)tw * h * 4, (size_t)tw * h * 4 };
    for (int k = 0; k < 10; k++)
        if (pool[k].cap < need[k]) {
            void *q = realloc(pool[k].p, need[k]);
            if (!q) return 0;
            pool[k].p = q; pool[k].cap = need[k];
        }
    t.color = (uint32_t *)pool[0].p;
    t.inv_z = (float *)pool[1].p;
    t.index = (uint16_t *)pool[2].p;
    t.stroke_z = NULL; t.stroke_aux_x = t.stroke_aux_y = NULL; t.stroke_owner = NULL; t.line_point = 0;
    t.inv_depth_scale = 1.0;
    t.line_bias = 0.0; t.line_slope = 0.0; t.line_ref_z = NULL;
    t.line_strong = (uint32_t *)pool[3].p;
    t.line_tmp_z = (float *)pool[4].p;
    t.line_order = 0;
    t.stroke_aux_x = (float *)pool[8].p; t.stroke_aux_y = (float *)pool[9].p;
    t.stroke_z = (float *)pool[6].p; t.stroke_owner = (uint32_t *)pool[7].p;
    t.owner = (uint32_t *)pool[5].p; t.paint_owner = 0; t.line_decal = 0;
    const int timed = getenv("F117_REMASTER_TIMING") != NULL;
    double worker_timing[REMASTER_PARTS][4] = { 0 };
    const double tm0 = timed ? now_ms() : 0.0;
    const int parts = g_par ? g_par_parts : 1;
    t.y_lo = t.y_hi = 0;
    uint32_t *gpu_check = NULL;                  /* F117_GPU_COMPOSE_CHECK: the GPU's wide picture, kept to compare with the CPU's */
    if (g_gpu_fn && wide) {                      /* the GPU compositor (gpu_compose.h): the scene's raster calls, recorded and not drawn here */
        static int check = -1;
        if (check < 0) check = getenv("F117_GPU_COMPOSE_CHECK") != NULL;
        uint32_t *target = wide_out;
        if (check) { gpu_check = (uint32_t *)malloc((size_t)tw * h * 4); target = gpu_check; }
        const double gt0 = timed ? now_ms() : 0.0;
        int gpu_ok = 0;
        if (target) {
            if (r->gpu_begun) {                  /* started before the game's picture was made: finish it with that picture */
                r->gpu_begun = 0;
                gpu_ok = g_gpu_finish_fn && g_gpu_finish_fn(src, tag, dac, target);
            }
            if (!gpu_ok) {                       /* not begun (or its finish declined): record, draw and finish in one go */
                uint32_t palette[256];
                gpu_compose_in gin;
                if (gpu_prepare(r, m, n, w, h, tw, xoff, complete, &gin, palette)) { gin.src = src; gin.tag = tag; gin.dac = dac; gpu_ok = g_gpu_fn(&gin, target); }
            }
        }
        if (timed) {
            static double a_run; static unsigned an;
            a_run += now_ms() - gt0; an++;
            if (an % 120 == 0) fprintf(stderr, "[gpu-compose] mean of %u: the compose's GPU part %.2f ms\n", an, a_run / an);
        }
        if (gpu_ok) {
            if (!check) {
                memcpy(dst, src, (size_t)w * h);
                if (mask) memset(mask, 0, (size_t)w * h);
                return 1;
            }
        } else { free(gpu_check); gpu_check = NULL; }
    }
    {
        scene_job sj = { m, t, &r->done, r->done_band, r->done_have_band, wide, parts, 0, r->done_origin, r->done_window, (double)n, (double)xoff };
        static F117_TLS decal_face_cache part_faces[REMASTER_PARTS];
        if (decal_faces_enabled()) sj.faces = part_faces;
        sj.timing = timed ? worker_timing : NULL;
        const char *tile = getenv("F117_REMASTER_ROW_TILE");
        sj.row_tile = tile ? atoi(tile) : parts >= 8 && h >= 1200 ? 64 : 0;
        if (sj.row_tile < 16 || sj.row_tile > 1024) sj.row_tile = 0;
        static int trace_done; const char *trace_spec = trace_done ? NULL : getenv("F117_GPU_DUMP");
        int tracing = 0;
        if (trace_spec && parts == 1 && r->done.n > 50) {
            const char *comma = strchr(trace_spec, ',');                 /* FILE,FROM[,MIN]: from that clock, with at least MIN primitives (a render-copy frame with far scenery) */
            const char *comma2 = comma ? strchr(comma + 1, ',') : NULL;
            const size_t min_prims = comma2 ? (size_t)strtoull(comma2 + 1, NULL, 0) : 0;
            if ((!comma || m->cpu.icount >= strtoull(comma + 1, NULL, 0)) && r->done.n >= min_prims) { tracing = 1; g_trace_n = 0; remaster_trace_hook = gpu_trace_hook; }
        }
        if (parts > 1) for (sj.stage = 1; sj.stage <= 3; sj.stage++) run_parts(scene_rows_part, &sj, parts);       /* a barrier between the stages */
        else run_parts(scene_rows_part, &sj, 1);
        if (tracing) {
            remaster_trace_hook = NULL; trace_done = 1;
            char path[1024]; const char *comma = strchr(trace_spec, ',');
            snprintf(path, sizeof path, "%.*s", comma ? (int)(comma - trace_spec) : (int)strlen(trace_spec), trace_spec);
            FILE *f = fopen(path, "wb");
            if (f) {
                gpu_trace_header hd; memset(&hd, 0, sizeof hd);
                hd.magic = GPU_TRACE_MAGIC; hd.w = tw; hd.h = h; hd.nrec = (uint32_t)g_trace_n;
                for (unsigned i = 0; i < 256; i++) hd.palette[i] = dac_colour(m, i);
                fwrite(&hd, sizeof hd, 1, f);
                fwrite(g_trace_recs, sizeof *g_trace_recs, g_trace_n, f);
                fwrite(t.color, 4, (size_t)tw * h, f); fwrite(t.index, 2, (size_t)tw * h, f); fwrite(t.owner, 4, (size_t)tw * h, f);
                fclose(f);
                snprintf(g_trace_path, sizeof g_trace_path, "%s", path);
                g_trace_extra = wide;               /* the replace section follows once the replace step has run (a wide picture only) */
                fprintf(stderr, "[gpu-dump] %s: %d x %d, %zu raster calls from %zu primitives, clock %llu\n", path, tw, h, g_trace_n, r->done.n, (unsigned long long)m->cpu.icount);
            }
        }
    }
    const double tm1 = timed ? now_ms() : 0.0;
    {                                           /* F117_REMASTER_DUMP=FILE.ppm: the render itself, once, for a look */
        static int dumped; const char *path = getenv("F117_REMASTER_DUMP");
        const char *from = getenv("F117_REMASTER_DUMP_FROM");
        if (path && !dumped && r->done.n > 50 && (!from || m->cpu.icount >= strtoull(from, NULL, 0))) {
            FILE *f = fopen(path, "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", tw, h);
                for (long i = 0; i < (long)tw * h; i++) { const uint32_t c = t.color[i]; const unsigned char px[3] = { (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c }; fwrite(px, 1, 3, f); }
                fclose(f); dumped = 1;
                char tags_path[1024];
                snprintf(tags_path, sizeof tags_path, "%s.tags.pgm", path);
                f = fopen(tags_path, "wb");
                if (f) {
                    fprintf(f, "P5\n%d %d\n255\n", w, h);
                    fwrite(tag, 1, (size_t)w * h, f);
                    fclose(f);
                }
                fprintf(stderr, "[remaster] dumped %d x %d, %zu primitives; frames %u requests %u read %u refused %u overwritten %u dropped %u/%u/%u\n", w, h, r->done.n, r->frames, r->requests, r->added, r->refused, r->overwritten, remaster_scene_dropped[0], remaster_scene_dropped[1], remaster_scene_dropped[2]);
            }
        }
    }
    memcpy(dst, src, (size_t)w * h);
    if (mask) memset(mask, 0, (size_t)w * h);
    const int x0 = (int)(r->done_window[0] * n), x1 = (int)((r->done_window[2] + 1) * n);
    const int y0 = (int)(r->done_window[1] * n), y1 = (int)((r->done_window[3] + 1) * n);
    long replaced = 0;
    if (getenv("F117_REMASTER_SEA")) {            /* every bluish polygon of the frame: where the sea should be */
        static int said, calls;
        if (!said && ++calls == 60) { said = 1;
            for (size_t i = 0; i < r->done.n; i++) {
                const remaster_prim *q = &r->done.prim[i];
                uint32_t c;
                if (q->kind != SCENE_POLYGON || !word_colour(m, q->colour_word, q->darken, &c)) continue;
                const int cr = (int)(c >> 16 & 255), cg = (int)(c >> 8 & 255), cb = (int)(c & 255);
                if (!(cb > cr + 16 && cb >= cg)) continue;
                double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9, z0 = 1e18, z1 = 0;
                for (int v = 0; v < q->n; v++) {
                    if (q->v[v].z <= 0) continue;
                    const double sx = r->done_origin[0] + 256.0 * q->v[v].x / q->v[v].z, sy = r->done_origin[1] + 256.0 * q->v[v].y / q->v[v].z;
                    if (sx < x0) x0 = sx; if (sx > x1) x1 = sx; if (sy < y0) y0 = sy; if (sy > y1) y1 = sy;
                    if (q->v[v].z < z0) z0 = q->v[v].z; if (q->v[v].z > z1) z1 = q->v[v].z;
                }
                fprintf(stderr, "[blue] prim %zu model %u word %04X colour %06X n %d screen x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f\n",
                        i, (unsigned)q->model, (unsigned)q->colour_word, c & 0xFFFFFF, q->n, x0, x1, y0, y1, z0, z1);
            }
            for (size_t i = 0; i < r->done.n; i++) {                /* the raw vertices of the blue polygons that are never in front */
                const remaster_prim *q = &r->done.prim[i];
                uint32_t c;
                if (q->kind != SCENE_POLYGON || !word_colour(m, q->colour_word, q->darken, &c) || !((int)(c & 255) > (int)(c >> 16 & 255) + 16)) continue;
                int front = 0; for (int v = 0; v < q->n; v++) if (q->v[v].z > 0) front = 1;
                if (front) continue;
                for (int v = 0; v < q->n; v++) fprintf(stderr, "[blue-raw] prim %zu v%d x %d y %d z %d (hex %08X %08X %08X)\n", i, v, (int)q->v[v].x, (int)q->v[v].y, (int)q->v[v].z, (unsigned)q->v[v].x, (unsigned)q->v[v].y, (unsigned)q->v[v].z);
            }
            fprintf(stderr, "[blue] done: %zu primitives in the frame\n", r->done.n);
        }
    }
    if (getenv("F117_REMASTER_DEBUG")) {          /* which origin tags the 3D window holds, and how many have Remastered geometry */
        static int said, calls;
        if (!said && ++calls == 60) {
            said = 1;
            long count[64] = {0}, geom[64] = {0};
            for (int y = y0 < 0 ? 0 : y0; y < y1 && y < h; y++)
                for (int x = x0 < 0 ? 0 : x0; x < x1 && x < w; x++) { const long i = (long)y * w + x; count[tag[i] & 63]++; if (t.inv_z[i] > 0.0f) geom[tag[i] & 63]++; }
            for (int k = 0; k < 64; k++) if (count[k]) fprintf(stderr, "[tags] tag %d: %ld pixels, %ld with model geometry\n", k, count[k], geom[k]);
            for (int y = 150; y < 166; y++) {                 /* one column through the hangar and the horizon strip */
                const int x = 130;
                const long pi = (long)y * w + x;
                fprintf(stderr, "[col] y %d tag %u inv_z %g render %06X\n", y, (unsigned)tag[pi], (double)t.inv_z[pi], (unsigned)(t.color[pi] & 0xFFFFFF));
            }
            {   /* one sky pixel, followed through */
                const int px = 620 % w, py = 60 % h; const long pi = (long)py * w + px;
                fprintf(stderr, "[pixel] (%d,%d) tag %u inv_z %g render %06X scene_tag %d span_tag %d window x %d..%d y %d..%d\n", px, py, (unsigned)tag[pi], (double)t.inv_z[pi], (unsigned)(t.color[pi] & 0xFFFFFF), is_scene_tag(tag[pi]), is_span_tag(tag[pi]), x0, x1, y0, y1);
            }
            fprintf(stderr, "[tags] scene range %d..%d, span tags %d and %d..%d\n", DT_MODEL_SOLID, DT_SPAN_SHADE, DT_SPANPOLY_FINE, DT_SPAN_NOT_POLY, DT_SPAN_SHADE);
        }
    }
    long kept_hist[64] = { 0 };
    {
        replace_job rj;
        memset(&rj, 0, sizeof rj);
        rj.src = src; rj.tag = tag; rj.dac = dac; rj.dst = dst; rj.mask = mask; rj.rgb = rgb; rj.wide_out = wide_out; rj.t = &t;
        rj.w = w; rj.h = h; rj.tw = tw; rj.xoff = xoff; rj.wide = wide; rj.x0 = x0; rj.x1 = x1; rj.y0 = y0; rj.y1 = y1; rj.parts = parts;
        rj.complete = complete;
        run_parts(replace_part, &rj, parts);
        for (int k = 0; k < parts; k++) {
            replaced += rj.replaced[k];
            for (int g = 0; g < 64; g++) kept_hist[g] += rj.kept[k][g];
        }
        if (g_trace_extra && wide) {             /* F117_GPU_DUMP: the replace step's inputs and output, after the raster calls' section (gpu_trace.h) */
            g_trace_extra = 0;
            FILE *tf = fopen(g_trace_path, "ab");
            if (tf) {
                gpu_trace_replace rp; memset(&rp, 0, sizeof rp);
                rp.w = w; rp.h = h; rp.tw = tw; rp.xoff = xoff; rp.x0 = x0; rp.x1 = x1; rp.y0 = y0; rp.y1 = y1; rp.complete = complete;
                for (int g = 0; g < 256; g++)
                    rp.tagflags[g] = (uint8_t)((is_scene_tag((uint8_t)g) ? 1 : 0) | ((g == DT_OUTLINE_FINE || g == DT_OUTLINE_COARSE) ? 2 : 0) | (is_span_tag((uint8_t)g) ? 4 : 0));
                fwrite(&rp, sizeof rp, 1, tf);
                fwrite(src, 1, (size_t)w * h, tf); fwrite(tag, 1, (size_t)w * h, tf); fwrite(dac, 1, 768, tf);
                fwrite(wide_out, 4, (size_t)tw * h, tf);
                fclose(tf);
                fprintf(stderr, "[gpu-dump] replace section appended to %s\n", g_trace_path);
            }
        }
    }
    {   /* F117_REMASTER_KEPT=1: the pixels of the 3D window left as the original drew them, by origin tag, every 120th composite.
         * A tag of the model range (1 to 20) or the outline tags kept in quantity is a primitive Remastered does not draw (or a
         * line drawn twice, as the hangar doors once were); the HUD, text, sprite and blit tags are the original's by design. */
        static int calls;
        if (getenv("F117_REMASTER_KEPT") && ++calls % 120 == 1) {
            fprintf(stderr, "[kept] clock %llu, original pixels kept in the 3D window:", (unsigned long long)m->cpu.icount);
            for (int k = 0; k < 64; k++) if (kept_hist[k]) fprintf(stderr, " tag %d%s: %ld;", k, (k >= DT_MODEL_SOLID && k <= DT_SPAN_SHADE) || k == DT_OUTLINE_FINE || k == DT_OUTLINE_COARSE ? "!" : "", kept_hist[k]);
            fprintf(stderr, "\n");
            if (getenv("F117_REMASTER_KEPT")[0] == '2') {       /* =2: the picture, the kept model pixels in magenta, the rest dimmed */
                char path[64];
                snprintf(path, sizeof path, "kept-%llu.ppm", (unsigned long long)m->cpu.icount);
                FILE *kf = fopen(path, "wb");
                if (kf) {
                    fprintf(kf, "P6\n%d %d\n255\n", w, h);
                    for (long i = 0; i < (long)w * h; i++) {
                        const unsigned c = dst[i];
                        const int in_window = (i % w) >= x0 && (i % w) < x1 && (i / w) >= y0 && (i / w) < y1;
                        const int model = (tag[i] >= DT_MODEL_SOLID && tag[i] <= DT_SPAN_SHADE) || tag[i] == DT_OUTLINE_FINE || tag[i] == DT_OUTLINE_COARSE;
                        const int was_kept = in_window && model && !(rgb && mask && mask[i]);
                        unsigned char px[3];
                        if (was_kept) { px[0] = 255; px[1] = 0; px[2] = 255; }
                        else { px[0] = (unsigned char)((dac[c * 3] << 2) / 2); px[1] = (unsigned char)((dac[c * 3 + 1] << 2) / 2); px[2] = (unsigned char)((dac[c * 3 + 2] << 2) / 2); }
                        fwrite(px, 1, 3, kf);
                    }
                    fclose(kf);
                }
            }
        }
    }
    const char *check_from = getenv("F117_REMASTER_THREADCHECK_FROM");
    if (parts > 1 && getenv("F117_REMASTER_THREADCHECK") &&
        (!check_from || m->cpu.icount >= strtoull(check_from, NULL, 0))) {   /* same picture on one thread, every output pixel compared */
        const size_t npx = (size_t)w * h, nwide = (size_t)tw * h;
        uint8_t *c_dst = (uint8_t *)malloc(npx), *c_mask = (uint8_t *)malloc(npx);
        uint32_t *c_rgb = (uint32_t *)malloc(npx * 4), *c_wide = wide ? (uint32_t *)malloc(nwide * 4) : NULL;
        if (c_dst && c_mask && c_rgb && (c_wide || !wide)) {
            memcpy(c_dst, dst, npx); memcpy(c_mask, mask, npx); memcpy(c_rgb, rgb, npx * 4);
            const size_t nsc = (size_t)tw * h;
            uint32_t *sc_col = (uint32_t *)malloc(nsc * 4), *sc_str = (uint32_t *)malloc(nsc * 4);
            float *sc_z = (float *)malloc(nsc * 4);
            if (sc_col && sc_str && sc_z) { memcpy(sc_col, t.color, nsc * 4); memcpy(sc_str, t.line_strong, nsc * 4); memcpy(sc_z, t.inv_z, nsc * 4); }
            if (wide) memcpy(c_wide, wide_out, nwide * 4);
            t.y_lo = t.y_hi = 0;
            scene_job sj1 = { m, t, &r->done, r->done_band, r->done_have_band, wide, 1, 0, r->done_origin, r->done_window, (double)n, (double)xoff };
            run_parts(scene_rows_part, &sj1, 1);
            memcpy(dst, src, npx);
            memset(mask, 0, npx);
            replace_job rj1;
            memset(&rj1, 0, sizeof rj1);
            rj1.src = src; rj1.tag = tag; rj1.dac = dac; rj1.dst = dst; rj1.mask = mask; rj1.rgb = rgb; rj1.wide_out = wide_out; rj1.t = &t;
            rj1.w = w; rj1.h = h; rj1.tw = tw; rj1.xoff = xoff; rj1.wide = wide; rj1.x0 = x0; rj1.x1 = x1; rj1.y0 = y0; rj1.y1 = y1; rj1.parts = 1;
            rj1.complete = complete;
            run_parts(replace_part, &rj1, 1);
            if (sc_col && sc_str && sc_z && getenv("F117_REMASTER_THREADCHECK")[0] == '2') {
                size_t dc = 0, ds = 0, dz = 0; int fy_c = -1, fy_s = -1, fy_z = -1, ly = -1;
                for (size_t i = 0; i < nsc; i++) {
                    const int y = (int)(i / (size_t)tw);
                    if (sc_col[i] != t.color[i]) { dc++; if (fy_c < 0) fy_c = y; ly = y; }
                    if (sc_str[i] != t.line_strong[i]) { ds++; if (fy_s < 0) fy_s = y; }
                    if (sc_z[i] != t.inv_z[i]) { dz++; if (fy_z < 0) fy_z = y; }
                }
                if (dc || ds || dz) fprintf(stderr, "[threadcheck] scene: colour %zu (from row %d to %d), whole-line layer %zu (from row %d), depth %zu (from row %d)\n", dc, fy_c, ly, ds, fy_s, dz, fy_z);
            }
            free(sc_col); free(sc_str); free(sc_z);
            size_t bad = 0, bad_narrow = 0;
            int first_y = -1, last_y = -1;
            for (size_t i = 0; i < npx; i++) {
                const int diff = dst[i] != c_dst[i] || mask[i] != c_mask[i] || (mask[i] && rgb[i] != c_rgb[i]);
                bad += diff; bad_narrow += diff;
                if (diff) {
                    if (first_y < 0) first_y = (int)(i / (size_t)w);
                    last_y = (int)(i / (size_t)w);
                    static int shown_diffs;
                    if (shown_diffs++ < 4 && getenv("F117_REMASTER_THREADCHECK")[0] == '2')
                        fprintf(stderr, "[threadcheck] pixel %zu,%zu: threaded dst %u mask %u rgb %06X, one thread dst %u mask %u rgb %06X; tag %u inv_z %g line_strong %06X colour %06X\n",
                                i % (size_t)w, i / (size_t)w, c_dst[i], c_mask[i], c_rgb[i] & 0xFFFFFF, dst[i], mask[i], rgb[i] & 0xFFFFFF, tag[i],
                                (double)t.inv_z[(i / (size_t)w) * (size_t)tw + i % (size_t)w + (size_t)xoff], t.line_strong[(i / (size_t)w) * (size_t)tw + i % (size_t)w + (size_t)xoff] & 0xFFFFFF,
                                t.color[(i / (size_t)w) * (size_t)tw + i % (size_t)w + (size_t)xoff] & 0xFFFFFF);
                }
            }
            if (wide) for (size_t i = 0; i < nwide; i++) {
                const int diff = wide_out[i] != c_wide[i];
                bad += diff;
                if (diff) { if (first_y < 0) first_y = (int)(i / (size_t)tw); last_y = (int)(i / (size_t)tw); }
            }
            if (bad && getenv("F117_REMASTER_THREADCHECK")[0] == '2') fprintf(stderr, "[threadcheck] differing rows %d..%d of %d (%zu in the game's picture)\n", first_y, last_y, h, bad_narrow);
            static unsigned checks, mismatches;
            checks++; mismatches += bad != 0;
            if (checks % 60 == 1 || bad || getenv("F117_REMASTER_THREADCHECK")[0] == '3')
                fprintf(stderr, "[threadcheck] %u pictures checked on %d threads against one, %u differ (this one: %zu pixels)\n", checks, parts, mismatches, bad);
        }
        free(c_dst); free(c_mask); free(c_rgb); free(c_wide);
    }
    if (gpu_check) {                             /* F117_GPU_COMPOSE_CHECK: the CPU's wide picture against the GPU's */
        static unsigned checked; static unsigned long long diff_total, big_total;
        size_t bad = 0, big = 0;
        for (size_t i = 0; i < (size_t)tw * h; i++)
            if (gpu_check[i] != wide_out[i]) {
                bad++;
                int mx = 0;
                for (int sh = 0; sh < 24; sh += 8) { int d = (int)(gpu_check[i] >> sh & 255) - (int)(wide_out[i] >> sh & 255); if (d < 0) d = -d; if (d > mx) mx = d; }
                if (mx > 8) big++;
            }
        diff_total += bad; big_total += big;
        if (bad) {                                   /* F117_GPU_CHECK_DUMP=PREFIX: write the first differing pair as PREFIX_gpu.ppm / PREFIX_cpu.ppm (diagnostic) */
            static int dumped; const char *dp = getenv("F117_GPU_CHECK_DUMP");
            if (dp && !dumped) {
                dumped = 1;
                for (int which = 0; which < 2; which++) {
                    char name[512]; snprintf(name, sizeof name, "%s_%s.ppm", dp, which ? "cpu" : "gpu");
                    FILE *f = fopen(name, "wb");
                    if (!f) continue;
                    const uint32_t *px = which ? wide_out : gpu_check;
                    fprintf(f, "P6\n%d %d\n255\n", tw, h);
                    for (size_t i = 0; i < (size_t)tw * h; i++) { const unsigned char rgb3[3] = { (unsigned char)(px[i] >> 16), (unsigned char)(px[i] >> 8), (unsigned char)px[i] }; fwrite(rgb3, 1, 3, f); }
                    fclose(f);
                }
            }
        }
        { static unsigned listed; if (bad && listed < 20) { listed++; size_t first = 0; for (size_t i = 0; i < (size_t)tw * h; i++) if (gpu_check[i] != wide_out[i]) { first = i; break; }
            fprintf(stderr, "[gpu-compose] differs: clock %llu, %zu pixels, first at x %zu y %zu (GPU %08X, CPU %08X)\n", (unsigned long long)m->cpu.icount, bad, first % (size_t)tw, first / (size_t)tw, gpu_check[first], wide_out[first]); } }
        if (++checked % 60 == 1) fprintf(stderr, "[gpu-compose] %u pictures checked: %llu pixels differ in all (%llu by more than 8 levels); this one %zu of %zu\n", checked, diff_total, big_total, bad, (size_t)tw * h);
        free(gpu_check);
    }
    if (timed) {
        static double s_scene, s_loop, s_worker_max[4]; static unsigned s_n;
        s_scene += tm1 - tm0; s_loop += now_ms() - tm1; s_n++;
        for (int phase = 0; phase < 4; ++phase) {
            double longest = 0.0;
            for (int part = 0; part < parts; ++part)
                if (worker_timing[part][phase] > longest) longest = worker_timing[part][phase];
            s_worker_max[phase] += longest;
        }
        if (s_n % 120 == 0)
            fprintf(stderr, "[timing] composite parts (ms, mean of %u): scene %.2f, worker maxima (clear %.2f, bands %.2f, polygons %.2f, lines %.2f), replace loop %.2f\n", s_n,
                    s_scene / s_n, s_worker_max[0] / s_n, s_worker_max[1] / s_n, s_worker_max[2] / s_n, s_worker_max[3] / s_n, s_loop / s_n);
    }
    return replaced > 0;
}

int remaster_live_composite(remaster_live *r, const machine_t *m, int n, const uint8_t *src, const uint8_t *tag,
                            const uint8_t *dac, uint8_t *dst, uint32_t *rgb, uint8_t *mask)
{
    return remaster_live_compose(r, m, n, src, tag, dac, dst, rgb, mask, 0, NULL);
}

static int validate_on(void)
{
    static int on = -1;
    if (on < 0) on = getenv("F117_REMASTER_VALIDATE") != NULL;
    return on;
}

/* Compare the camera-space vertices the Remastered path computed with what the game itself projected for the same model:
 * the game's record for vertex id (DS:D6B4 + 8 * id) holds the projected x and y as longs (a high word of 8000h in x: behind
 * the eye), the pixel offsets from the screen origin. A model whose vertices disagree is posed or placed wrongly here. */
static void validate_model(remaster_live *r, const machine_t *m)
{
    const scene_geometry *g = remaster_scene_geometry(r->scene);
    const uint16_t ds = m->cpu.seg[S_DS];
    unsigned slot = 0;
    while (slot < r->nchecks && r->check[slot].model != r->model) slot++;
    if (slot == r->nchecks) {
        if (r->nchecks == 256) return;
        r->check[slot].model = r->model;
        r->nchecks++;
    }
    const unsigned differ_before = r->check[slot].differ;
    int first_id = -1, first_mine[2] = { 0, 0 }, first_game[2] = { 0, 0 };
    for (unsigned id = 0; id < SCENE_VERTICES; id++) {
        if (!g->present[id]) continue;
        const uint16_t at = (uint16_t)(0xD6B4 + 8 * id);
        const uint32_t gx = (uint32_t)peek16(m, ds, at) | (uint32_t)peek16(m, ds, (uint16_t)(at + 2)) << 16;
        const uint32_t gy = (uint32_t)peek16(m, ds, (uint16_t)(at + 4)) | (uint32_t)peek16(m, ds, (uint16_t)(at + 6)) << 16;
        const int game_behind = (gx >> 16) == 0x8000;
        int px = 0, py = 0;
        const int mine_x = remaster_pixel_exact(g->camera[id][0], g->camera[id][2], &px);
        const int mine_y = remaster_pixel_exact(g->camera[id][1], g->camera[id][2], &py);
        {   /* the first mismatching vertices of the models with nothing matching so far: the raw numbers */
            static int shown;
            if (shown < 12 && r->check[slot].match == 0 && r->check[slot].seen < 3) {
                shown++;
                int32_t sc[3], eye[3], tr[3];
                for (int j = 0; j < 3; j++) {
                    const uint16_t a = (uint16_t)(0x4EE6 + 16 * id + 4 * j), b = (uint16_t)(0x7CD2 + 4 * j), c2 = (uint16_t)(0x7CDE + 4 * j);
                    sc[j] = (int32_t)((uint32_t)peek16(m, ds, a) | (uint32_t)peek16(m, ds, (uint16_t)(a + 2)) << 16);
                    eye[j] = (int32_t)((uint32_t)peek16(m, ds, b) | (uint32_t)peek16(m, ds, (uint16_t)(b + 2)) << 16);
                    tr[j] = (int32_t)((uint32_t)peek16(m, ds, c2) | (uint32_t)peek16(m, ds, (uint16_t)(c2 + 2)) << 16);
                }
                fprintf(stderr, "[raw] model %u id %u: cache %d %d %d | 7CD2 %d %d %d | 7CDE %d %d %d | mine %d %d %d | game px %d,%d (x %08X)\n",
                        (unsigned)r->model, id, sc[0], sc[1], sc[2], eye[0], eye[1], eye[2], tr[0], tr[1], tr[2],
                        g->camera[id][0], g->camera[id][1], g->camera[id][2], (int16_t)(gx & 0xFFFF), (int16_t)(gy & 0xFFFF), (unsigned)gx);
            }
        }
        r->check[slot].seen++;
        if (!mine_x) { r->check[slot].behind_mine++; if (game_behind) { r->check[slot].match++; continue; } r->check[slot].differ++; continue; }
        if (game_behind) { r->check[slot].behind_game++; r->check[slot].differ++; continue; }
        if (mine_y && (int32_t)gx == px && (int32_t)gy == py) r->check[slot].match++;
        else {
            r->check[slot].differ++;
            if (first_id < 0) { first_id = (int)id; first_mine[0] = px; first_mine[1] = py; first_game[0] = (int)gx; first_game[1] = (int)gy; }
            if (mine_y) {                       /* both in front: how far apart, in pixels of the 320 x 200 picture */
                const long dx = labs((long)(int32_t)gx - (long)px), dy = labs((long)(int32_t)gy - (long)py);
                if (dx <= 1 && dy <= 1) r->check[slot].off1++; else if (dx <= 8 && dy <= 8) r->check[slot].off8++; else r->check[slot].farther++;
            }
        }
    }
    {   /* F117_REMASTER_VALIDATE=2: one line per instance of a model with a mismatch (the first thirty) */
        static int shown_inst;
        const char *level = getenv("F117_REMASTER_VALIDATE");
        if (level && level[0] == '2' && r->check[slot].differ > differ_before && shown_inst < 30) {
            shown_inst++;
            fprintf(stderr, "[inst] model %u at %d %d %d (origin %d %d, bank %u): %u differ; first id %d (camera z %d) mine %d,%d game %d,%d\n",
                    (unsigned)r->model, r->position[0], r->position[1], r->position[2], r->origin[0], r->origin[1], (unsigned)r->bank,
                    r->check[slot].differ - differ_before, first_id, first_id >= 0 ? g->camera[first_id][2] : 0, first_mine[0], first_mine[1], first_game[0], first_game[1]);
        }
    }
}

static void report_validation(const remaster_live *r)
{
    fprintf(stderr, "[validate] model: vertices matching the game's own projection / differing (behind the eye here / there)\n");
    for (unsigned i = 0; i < r->nchecks; i++)
        if (r->check[i].differ)
            fprintf(stderr, "[validate] model %u: %u match, %u differ (%u behind here, %u behind there; both in front: %u within 1 px, %u within 8, %u farther) of %u\n", (unsigned)r->check[i].model, r->check[i].match, r->check[i].differ, r->check[i].behind_mine,
                    r->check[i].behind_game, r->check[i].off1, r->check[i].off8, r->check[i].farther, r->check[i].seen);
    unsigned total = 0, bad = 0;
    for (unsigned i = 0; i < r->nchecks; i++) { total += r->check[i].seen; bad += r->check[i].differ; }
    fprintf(stderr, "[validate] %u models, %u vertices, %u differ\n", r->nchecks, total, bad);
}

static void model_camera_correction(machine_t *m, const double camera_offset[3], int32_t correction[3])
{
    correction[0] = correction[1] = correction[2] = 0;
    static int native_ring_eye = -1;
    if (native_ring_eye < 0) native_ring_eye = getenv("F117_REMASTER_NATIVE_RING_EYE") != NULL;
    if (!native_ring_eye) {
        /* Use the prepared view basis, not an object's part rotation. Geometry is already emitted, so facing/material rules and
         * original machine state remain untouched. */
        for (int axis = 0; axis < 3; ++axis) {
            double value = 0.0;
            for (int world = 0; world < 3; ++world)
                value += camera_offset[world] * (int16_t)peek16(m, m->cpu.seg[S_DS], (uint16_t)(0x7B28 + 2 * (3 * world + axis)));
            correction[axis] = (int32_t)llround(value);
        }
    }
}

/* A model's primitives from `first` are in the frame: shrink them about the object's origin when it was drawn at a coarser scale than its own ring
 * (the position is in the coarser units, the model's vertices are not), shift them by the ring-rounding correction, and set the inverse-depth scale. */
static void finish_model(remaster_scene *sc, remaster_frame *f, size_t first, int shrink, double inv_depth, const int32_t correction[3])
{
    if (shrink > 0) {
        int32_t origin[3];
        remaster_scene_origin(sc, origin);
        for (size_t i = first; i < f->n; ++i) {
            remaster_prim *p = &f->prim[i];
            for (int vertex = 0; vertex < p->n; ++vertex) {
                int32_t *coordinates[3] = { &p->v[vertex].x, &p->v[vertex].y, &p->v[vertex].z };
                for (int axis = 0; axis < 3; ++axis)
                    *coordinates[axis] = origin[axis] + (int32_t)((*coordinates[axis] - (int64_t)origin[axis] + ((int64_t)1 << (shrink - 1))) >> shrink);
            }
        }
    }
    for (size_t i = first; i < f->n; ++i) {
        remaster_prim *p = &f->prim[i];
        p->inv_depth_scale = inv_depth;
        p->shrink = (uint8_t)(shrink > 0 ? shrink : 0);
        for (int vertex = 0; vertex < p->n; ++vertex) {
            int32_t *coordinates[3] = { &p->v[vertex].x, &p->v[vertex].y, &p->v[vertex].z };
            for (int axis = 0; axis < 3; ++axis) {
                const uint32_t bits = (uint32_t)*coordinates[axis] + (uint32_t)correction[axis];
                *coordinates[axis] = bits <= INT32_MAX ? (int32_t)bits : -(int32_t)(UINT32_MAX - bits) - 1;
            }
        }
    }
}

static void poke16(machine_t *m, uint16_t seg, uint16_t off, uint16_t v)
{
    const uint32_t at = ((uint32_t)seg << 4) + off;
    m->mem[at & 0xFFFFF] = (uint8_t)v; m->mem[(at + 1) & 0xFFFFF] = (uint8_t)(v >> 8);
}

/* The pose state of the terrain object just placed, kept for its model when it has no parts (the part count [7D58] not above zero) and is a
 * perspective one: every terrain object is placed with zero angles, so a model's pose differs between two of them only by position. */
static void capture_template(remaster_live *r, machine_t *m)
{
    const uint16_t ds = m->cpu.seg[S_DS];
    const int parts = (int16_t)peek16(m, ds, 0x7D58);
    if (parts < 0 || parts > 1 || m->mem[(((uint32_t)ds << 4) + 0x7D8E) & 0xFFFFF]) return;     /* none or the base part only; more parts: the game's own pose */
    unsigned i = 0;
    while (i < r->ntmpl && !(r->tmpl[i].bank == r->bank && r->tmpl[i].model == r->model)) i++;
    if (i == r->ntmpl) {
        if (r->ntmpl == sizeof r->tmpl / sizeof r->tmpl[0]) return;
        r->ntmpl++;
        if (getenv("F117_TEMPLATE_LOG"))     /* diagnostic: when each model's template is first learned (far instances of a model cannot be drawn natively before it) */
            fprintf(stderr, "[template] clock %llu: learned bank %04X model %u (%u templates)\n", (unsigned long long)m->cpu.icount, (unsigned)r->bank, (unsigned)r->model, r->ntmpl);
    }
    r->tmpl[i].bank = r->bank; r->tmpl[i].model = r->model;
    r->tmpl[i].parts = remaster_scene_has_shared(r->scene) ? -1 : parts;      /* -1: shared vertices, always the game's own pose */
    memcpy(r->tmpl[i].bytes, m->mem + (((uint32_t)ds << 4) + TEMPLATE_AT), TEMPLATE_LEN);
}

/* F117_NATIVE_CHECK: an object drawn natively (its primitives kept in chk[], not in the frame) is compared with the game's own drawing of the same
 * model at the same position, which arrives from `guest_first` on (a deferred object arrives later, which is why they are paired by what they are). */
static void check_native(remaster_live *r, machine_t *m, size_t guest_first)
{
    unsigned q = r->nchk;
    while (q > 0) {
        --q;
        if (r->chk[q].bank == r->bank && r->chk[q].model == r->model &&
            r->chk[q].shrink == r->pending_shrink && r->chk[q].inv_depth == r->pending_inv_depth &&
            !memcmp(r->chk[q].pos, r->position, sizeof r->position)) break;
        if (q == 0) return;
    }
    const size_t n_guest = r->frame.n - guest_first;
    /* DS:7D8A is the fill colour of the face the game drew last, which the renderer reads as the model's base shade: a stale value that depends on what was
     * drawn just before, not on the object. When it differs the shades are not comparable. */
    const int shade_comparable = r->chk[q].pose[0x7D8A - TEMPLATE_AT] == m->mem[(((uint32_t)m->cpu.seg[S_DS] << 4) + 0x7D8A) & 0xFFFFF];
    int same = n_guest == r->chk[q].n;
    for (size_t i = 0; same && i < n_guest; i++) {
        const remaster_prim *a = &r->chk[q].prim[i], *g = &r->frame.prim[guest_first + i];
        if (a->kind != g->kind || a->n != g->n || a->colour_index != g->colour_index || a->colour_word != g->colour_word || (shade_comparable && a->shade != g->shade) ||
            a->darken != g->darken || a->inv_depth_scale != g->inv_depth_scale) same = 0;
        for (int v = 0; same && v < a->n; v++)
            if (a->v[v].x != g->v[v].x || a->v[v].y != g->v[v].y || a->v[v].z != g->v[v].z) same = 0;
    }
    if (same) r->chk_ok++; else {
        r->chk_bad++;
        if (r->chk_bad <= 8) {
            fprintf(stderr, "[native-check] model %u differs: native %zu prims, game %zu; game side: shrink %d offset %.1f,%.1f,%.1f depth %g terrain %d\n", (unsigned)r->model, r->chk[q].n, n_guest,
                    r->pending_shrink, r->pending_camera_offset[0], r->pending_camera_offset[1], r->pending_camera_offset[2], r->pending_inv_depth, r->pending_terrain);
            {
                const uint8_t *now = m->mem + (((uint32_t)m->cpu.seg[S_DS] << 4) + TEMPLATE_AT);
                int shown = 0;
                for (unsigned o = 0; o < TEMPLATE_LEN && shown < 24; o += 2) {
                    const unsigned a = r->chk[q].pose[o] | r->chk[q].pose[o + 1] << 8, g = now[o] | now[o + 1] << 8;
                    if (a != g) { fprintf(stderr, "[native-check]   DS:%04X native %04X game %04X\n", (unsigned)(TEMPLATE_AT + o), a, g); shown++; }
                }
            }
            for (size_t i = 0; i < r->chk[q].n && i < n_guest && i < 3; i++) {
                const remaster_prim *a = &r->chk[q].prim[i], *g = &r->frame.prim[guest_first + i];
                fprintf(stderr, "[native-check]   prim %zu kind %d/%d n %d/%d colour %u/%u shade %u/%u darken %u/%u depth %g/%g v0 %d,%d,%d / %d,%d,%d\n", i, a->kind, g->kind, a->n, g->n,
                        a->colour_index, g->colour_index, a->shade, g->shade, a->darken, g->darken, a->inv_depth_scale, g->inv_depth_scale,
                        a->v[0].x, a->v[0].y, a->v[0].z, g->v[0].x, g->v[0].y, g->v[0].z);
            }
        }
    }
    if ((r->chk_ok + r->chk_bad) % 5000 == 0) fprintf(stderr, "[native-check] %u same as the game's own, %u differ, %u expired, %u empty unmatched\n", r->chk_ok, r->chk_bad, r->chk_expired, r->chk_empty);
    free(r->chk[q].prim);
    r->chk[q] = r->chk[--r->nchk];
}

/* Mirror 120A:02B2 against the guest's prepared translation and its own
 * detail record, before any Remastered distance adjustment. */
static int model_range(const machine_t *m, uint16_t bank, uint16_t model, const uint32_t translation[3], uint16_t size)
{
    const uint16_t z = (uint16_t)(translation[2] >> 16);
    const uint16_t depth = z & 0x8000 ? (uint16_t)(0u - z) : z;
    if ((uint16_t)((depth + 1u) * 2u) >= size) return 0;
    const uint32_t bank_at = (uint32_t)bank << 4;
    uint16_t lod;
    if (bank_at > 0x100000u - SCENE_SEGMENT ||
        !scene_mesh_lod(m->mem + bank_at, model, depth, &lod)) return -1;
    const uint16_t limit = (uint16_t)(2u * z + peek16(m, bank, lod));
    if (limit & 0x8000) return 0;
    for (unsigned axis = 0; axis < 2; axis++) {
        uint32_t value = translation[axis];
        if (value & 0x80000000u) value = 0u - value;
        if ((value >> 16) > limit) return 0;
    }
    return 1;
}

static int guest_model_range(const machine_t *m, uint16_t bank, uint16_t model)
{
    const uint16_t ds = m->cpu.seg[S_DS];
    if (m->mem[(((uint32_t)ds << 4) + 0x7D8E) & 0xFFFFF]) return -1;
    uint32_t translation[3];
    for (unsigned axis = 0; axis < 3; axis++) {
        const uint16_t at = (uint16_t)(0x7CD2 + 4 * axis);
        translation[axis] = peek16(m, ds, at) | (uint32_t)peek16(m, ds, at + 2) << 16;
    }
    return model_range(m, bank, model, translation, peek16(m, ds, 0x7D5E));
}

void remaster_live_placement_return(remaster_live *r, machine_t *m, int returned)
{
    if (!r->chk_return_pending) return;
    r->chk_return_pending = 0;
    const int paired = r->chk_ok + r->chk_bad != r->chk_before_match;
    const int missing = returned && !paired &&
        peek16(m, m->cpu.seg[S_DS], 0x49F4) <= r->chk_before_deferred &&
        r->requests != r->chk_before_requests && r->added == r->chk_before_added;
    if (returned && (paired || missing)) {
        if (r->chk_predicted_range < 0) r->chk_prediction_unknown++;
        else if (r->chk_predicted_range == paired) r->chk_range_agree++;
        else if (paired) r->chk_false_reject++;
        else r->chk_false_accept++;
    }
    if (!returned) r->chk_no_return++;
    else if (!paired) {
        const unsigned deferred = peek16(m, m->cpu.seg[S_DS], 0x49F4);
        if (deferred > r->chk_before_deferred) r->chk_deferred++;
        else if (r->requests == r->chk_before_requests) r->chk_no_request++;
        else if (r->added == r->chk_before_added) {
            const int range = guest_model_range(m, r->bank, r->model);
            r->chk_no_faces++;
            if (range < 0) r->chk_range_unknown++;
            else if (range == 0) r->chk_range_rejected++;
            else r->chk_range_accepted++;
        }
        else r->chk_no_pair++;
    }
    if (++r->chk_returns % 50000 == 0) {
        fprintf(stderr, "[range-prediction] %u agree, %u false reject, %u false accept, %u unknown\n",
                r->chk_range_agree, r->chk_false_reject, r->chk_false_accept, r->chk_prediction_unknown);
        fprintf(stderr, "[placement-return] %u checked: %u no request, %u request without capture (range %u rejected, %u accepted, %u unknown), %u captured without pair, %u deferred, %u failed return\n",
                r->chk_returns, r->chk_no_request, r->chk_no_faces, r->chk_range_rejected, r->chk_range_accepted, r->chk_range_unknown, r->chk_no_pair, r->chk_deferred, r->chk_no_return);
    }
}

int remaster_live_native(remaster_live *r, machine_t *m, const void *spec)
{
    const rerun_native *n = (const rerun_native *)spec;
    const uint16_t ds = m->cpu.seg[S_DS];
    static int timing = -1; static double t_patch, t_add, t_fin;
    static unsigned t_n, no_template, unsupported, stale;
    if (timing < 0) timing = getenv("F117_NATIVE_TIMING") != NULL;
    const double tm0 = timing ? now_ms() : 0.0;
    unsigned t = 0;
    while (t < r->ntmpl && !(r->tmpl[t].bank == n->bank && r->tmpl[t].model == n->model)) t++;
    if (t < r->ntmpl && r->tmpl[t].parts < 0) { if (timing) unsupported++; return 0; }
    if (t == r->ntmpl) { if (timing) no_template++; r->native_missed++; { static int shown; if (shown++ < 6) fprintf(stderr, "[native] no template for bank %04X model %u (%u templates)\n", (unsigned)n->bank, (unsigned)n->model, r->ntmpl); } return 0; }
    uint8_t *dsp = m->mem + (((uint32_t)ds << 4) + TEMPLATE_AT);
    /* the template belongs to this frame's camera and light: the matrix and the light words must still be what they were */
    const int matrix_same = !memcmp(dsp, r->tmpl[t].bytes, 18);
    const int light_same = !memcmp(dsp + (0x7C9C - TEMPLATE_AT), r->tmpl[t].bytes + (0x7C9C - TEMPLATE_AT), 6);
    static int refresh_matrix = -1;
    if (refresh_matrix < 0) refresh_matrix = env_default_on("F117_NATIVE_REFRESH_PART");
    const int refresh = refresh_matrix && !matrix_same && r->tmpl[t].parts == 1;
    if ((!matrix_same && !refresh) || !light_same) {
        if (timing) stale++;
        r->native_missed++;
        static int shown2;
        if (shown2++ < 6) fprintf(stderr, "[native] template stale for bank %04X model %u: matrix %s, light %s\n", (unsigned)n->bank, (unsigned)n->model, matrix_same ? "same" : "differs", light_same ? "same" : "differs");
        return 0;
    }
    static int check = -1, range_cull = -1;
    if (check < 0) check = getenv("F117_NATIVE_CHECK") != NULL;
    if (range_cull < 0) range_cull = env_default_on("F117_NATIVE_RANGE_CULL");
    uint32_t translation[3] = { 0, 0, 0 };
    for (int k = 0; k < 3; k++)
        for (int row = 0; row < 3; row++)
            translation[k] += (uint32_t)((int32_t)n->pos[row] *
                (int16_t)peek16(m, ds, (uint16_t)(TEMPLATE_AT + 2 * (3 * row + k))));
    const uint8_t *pose = r->tmpl[t].bytes;
    if (!check && range_cull && !pose[0x7D8E - TEMPLATE_AT]) {
        const uint16_t size = pose[0x7D5E - TEMPLATE_AT] | (uint16_t)pose[0x7D5F - TEMPLATE_AT] << 8;
        if (model_range(m, n->bank, n->model, translation, size) == 0) return 1;
    }
    uint8_t saved[TEMPLATE_LEN], keep48c4 = m->mem[(((uint32_t)ds << 4) + 0x48C4) & 0xFFFFF];
    memcpy(saved, dsp, TEMPLATE_LEN);
    memcpy(dsp, r->tmpl[t].bytes, TEMPLATE_LEN);
    if (refresh) {
        memcpy(dsp, saved, 18);
        /* 120A:0392 calls 1452:0288 with orientation, camera, and output.
         * 7B5E holds the transpose of the unchanged base orientation. */
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 3; col++) {
                uint32_t sum = 0;
                for (int k = 0; k < 3; k++) {
                    const int16_t orientation = (int16_t)peek16(m, ds, (uint16_t)(0x7B5E + 6 * k + 2 * row));
                    const int16_t camera = (int16_t)peek16(m, ds, (uint16_t)(TEMPLATE_AT + 6 * k + 2 * col));
                    sum += (uint32_t)((int32_t)orientation * camera);
                }
                poke16(m, ds, (uint16_t)(0x7B4C + 6 * row + 2 * col), (uint16_t)(sum >> 15));
            }
    }
    for (int i = 0; i < 3; i++) poke16(m, ds, (uint16_t)(0x7CCC + 2 * i), (uint16_t)n->pos[i]);
    {   /* the eye: the position negated (120A:0317); with a base part also through the part's matrix at 7B5E (1452:021B, in place:
         * each element the high words of its three products summed and doubled, 16 bits) */
        uint16_t eye[3];
        for (int i = 0; i < 3; i++) eye[i] = (uint16_t)(0 - (uint16_t)n->pos[i]);
        if (r->tmpl[t].parts == 1) {
            uint16_t out[3];
            for (int k = 0; k < 3; k++) {
                uint16_t sum = 0;
                for (int row = 0; row < 3; row++)
                    sum = (uint16_t)(sum + (uint16_t)(((int32_t)(int16_t)eye[row] * (int16_t)peek16(m, ds, (uint16_t)(0x7B5E + 6 * row + 2 * k))) >> 16));
                out[k] = (uint16_t)(sum << 1);
            }
            for (int i = 0; i < 3; i++) eye[i] = out[i];
        }
        for (int i = 0; i < 3; i++) poke16(m, ds, (uint16_t)(0x7C6C + 2 * i), eye[i]);
    }
    for (int k = 0; k < 3; k++) {                                                              /* the position through the camera matrix, 32 bits (1452:0330) */
        const uint32_t acc = translation[k];
        poke16(m, ds, (uint16_t)(0x7CD2 + 4 * k), (uint16_t)acc);
        poke16(m, ds, (uint16_t)(0x7CD4 + 4 * k), (uint16_t)(acc >> 16));
        if (r->tmpl[t].parts == 1) {                                                           /* a base part's translation is the copy at 7CDE */
            poke16(m, ds, (uint16_t)(0x7CDE + 4 * k), (uint16_t)acc);
            poke16(m, ds, (uint16_t)(0x7CE0 + 4 * k), (uint16_t)(acc >> 16));
        }
    }
    m->mem[(((uint32_t)ds << 4) + 0x48C4) & 0xFFFFF] = n->darken;
    remaster_frame *target = check ? &r->chk_frame : &r->frame;
    if (check) {
        target->n = 0;
        memcpy(r->chk_pose_now, dsp, TEMPLATE_LEN);
        r->chk_predicted_range = guest_model_range(m, n->bank, n->model);
    }
    const size_t first = target->n;
    unsigned dropped[3];
    memcpy(dropped, remaster_scene_dropped, sizeof dropped);
    const double tm1 = timing ? now_ms() : 0.0;
    remaster_scene_lod_shift((unsigned)n->shrink);                         /* the level of detail by the real distance, not the coarser unit's */
    const int added = remaster_scene_add(r->scene, m->mem, 0x100000, ds, n->bank, n->model, n->pos, target);
    if (added < 0 || memcmp(dropped, remaster_scene_dropped, sizeof dropped)) r->frame_complete = 0;
    const double tm2 = timing ? now_ms() : 0.0;
    {   /* F117_FAR_DOTS_OFF (diagnostic): no point primitives from objects drawn at a coarser level than their own ring */
        static int dots_off = -1;
        if (dots_off < 0) { const char *e = getenv("F117_FAR_DOTS_OFF"); dots_off = e ? atoi(e) : 0; }   /* a mask of kinds to drop: 1 polygons, 2 lines, 4 dots */
        if (dots_off && n->shrink > 0 && added >= 0) {
            size_t w = first;
            for (size_t i = first; i < target->n; i++)
                if (!(dots_off & (1 << target->prim[i].kind))) target->prim[w++] = target->prim[i];
            target->n = w;
        }
        /* Terrain dots: a dark point (a light drawn as a line from a point to itself, or a dot) of an object placed at a coarser level than its own ring
         * is a speck on the ground, thousands of them at Maximum. Their reach is the Terrain dots setting's (default: Near); lit points (lights)
         * keep the Draw distance setting's reach. F117_FAR_DARK_DOTS=1 draws them at the full reach. */
        if (g_dot_levels < 0) { const char *e = getenv("F117_FAR_DARK_DOTS"); g_dot_levels = e ? (e[0] == '1' ? 3 : 0) : 1; }
        if (n->shrink / 2 > g_dot_levels && added >= 0) {
            size_t w = first;
            for (size_t i = first; i < target->n; i++) {
                const remaster_prim *q = &target->prim[i];
                uint32_t c;
                int dark = 0;
                if (q->kind == SCENE_DOTS || (q->kind == SCENE_LINES && q->n == 2 && !memcmp(&q->v[0], &q->v[1], sizeof q->v[0])))
                    dark = word_colour(m, q->colour_word, q->darken, &c) && (c >> 16 & 255) < 80 && (c >> 8 & 255) < 80 && (c & 255) < 80;
                if (!dark) target->prim[w++] = target->prim[i];
            }
            target->n = w;
        }
    }
    if (added >= 0 && target->n > first) {
        ring_correction local = {0};
        ring_correction *cached = n->level >= 1 && n->level <= 4 ? &r->native_correction[n->level - 1] : &local;
        if (!cached->valid || memcmp(cached->eye, n->eye, sizeof cached->eye) ||
            memcmp(cached->matrix, dsp, sizeof cached->matrix)) {
            double offset[3];
            remaster_ring_camera_offset(n->eye, (unsigned)n->level, offset);
            model_camera_correction(m, offset, cached->value);
            memcpy(cached->eye, n->eye, sizeof cached->eye);
            memcpy(cached->matrix, dsp, sizeof cached->matrix);
            cached->valid = 1;
        }
        finish_model(r->scene, target, first, n->shrink, ldexp(1.0, 2 * (4 - n->level)), cached->value);
    }
    memcpy(dsp, saved, TEMPLATE_LEN);
    if (timing) {
        const double tm3 = now_ms();
        t_patch += tm1 - tm0; t_add += tm2 - tm1; t_fin += tm3 - tm2;
        if (++t_n % 20000 == 0) fprintf(stderr, "[native-timing] per object: patch %.2f us, scene add %.2f us, finish %.2f us (%u objects), fallback: %u no template, %u unsupported, %u stale\n", t_patch * 1000.0 / t_n, t_add * 1000.0 / t_n, t_fin * 1000.0 / t_n, t_n, no_template, unsupported, stale);
    }
    m->mem[(((uint32_t)ds << 4) + 0x48C4) & 0xFFFFF] = keep48c4;
    static unsigned long long nw_from; static int nw_init, nw_on;
    if (!nw_init) { nw_init = 1; const char *e = getenv("F117_FAR_WHY"); nw_on = e != NULL; const char *f = getenv("F117_FAR_WHY_FROM"); nw_from = f ? strtoull(f, NULL, 0) : 0; }
    if (added < 0 && nw_on && m->cpu.icount >= nw_from && rerun_why_model(n->model)) {
        static unsigned shown; if (shown++ < 20) fprintf(stderr, "[why] native placement of model %u refused by the scene reader (added %d)\n", (unsigned)n->model, added);
    }
    if (added < 0) { r->refused++; return 0; }
    r->added++; r->native_drawn++;
    r->src_native_prims += target->n - first; r->src_native_objs++;
    {   /* F117_FAR_WHY (diagnostic): what the native path made of the models the walk comparison misses */
        static int wl = -1; static unsigned left = 120;
        if (wl < 0) wl = getenv("F117_FAR_WHY") != NULL;
        if (wl && left && m->cpu.icount >= nw_from && rerun_why_model(n->model)) {
            left--;
            fprintf(stderr, "[why] clock %llu native placement of model %u bank %u level %d: %zu primitives added (frame now %zu), first labelled model %u kind %d\n", (unsigned long long)m->cpu.icount, (unsigned)n->model, (unsigned)n->bank, n->level, target->n - first, target->n, target->n > first ? (unsigned)target->prim[first].model : 0u, target->n > first ? target->prim[first].kind : -1);
        }
    }
    static int want_sizes = -1;
    if (want_sizes < 0) want_sizes = getenv("F117_NATIVE_SIZES") != NULL;
    if (want_sizes && target->n > first) {   /* diagnostic: the projected radius, in the original's pixels (256 x / z), of the natively drawn objects */
        static unsigned bucket[8], total;
        int32_t origin[3];
        remaster_scene_origin(r->scene, origin);
        double extent = 0.0;
        for (size_t i = first; i < target->n; i++)
            for (int v = 0; v < target->prim[i].n; v++)
                for (int a = 0; a < 3; a++) {
                    const int32_t c = a == 0 ? target->prim[i].v[v].x : a == 1 ? target->prim[i].v[v].y : target->prim[i].v[v].z;
                    const double d = fabs((double)c - (double)origin[a]);
                    if (d > extent) extent = d;
                }
        const double depth = fabs((double)origin[2]);
        const double radius = depth > 0 ? 256.0 * extent / depth : 1e9;
        static const double edge[7] = { 0.1, 0.25, 0.5, 1, 2, 4, 8 };
        int b = 0; while (b < 7 && radius >= edge[b]) b++;
        bucket[b]++;
        if (++total % 20000 == 0)
            fprintf(stderr, "[sizes] radius in original pixels: <0.1 %u, <0.25 %u, <0.5 %u, <1 %u, <2 %u, <4 %u, <8 %u, more %u (of %u)\n", bucket[0], bucket[1], bucket[2], bucket[3], bucket[4], bucket[5], bucket[6], bucket[7], total);
    }
    if (check) {                                                    /* keep the native primitives for the comparison, and let the game place it too */
        r->chk_return_pending = 1;
        r->chk_before_match = r->chk_ok + r->chk_bad;
        r->chk_before_requests = r->requests;
        r->chk_before_added = r->added;
        r->chk_before_deferred = peek16(m, ds, 0x49F4);
        if (r->nchk == r->chk_cap) {
            const unsigned cap = r->chk_cap ? r->chk_cap * 2 : 256;
            void *next = realloc(r->chk, (size_t)cap * sizeof *r->chk);
            if (!next) { r->chk_alloc_failed++; return 0; }
            r->chk = next;
            r->chk_cap = cap;
        }
        const unsigned q = r->nchk++;
        r->chk[q].bank = n->bank; r->chk[q].model = n->model; memcpy(r->chk[q].pos, n->pos, sizeof n->pos);
        r->chk[q].n = target->n;
        r->chk[q].shrink = n->shrink;
        r->chk[q].inv_depth = ldexp(1.0, 2 * (4 - n->level));
        memcpy(r->chk[q].pose, r->chk_pose_now, TEMPLATE_LEN);
        r->chk[q].prim = (remaster_prim *)malloc((target->n ? target->n : 1) * sizeof *target->prim);
        if (r->chk[q].prim) memcpy(r->chk[q].prim, target->prim, target->n * sizeof *target->prim); else { r->nchk--; r->chk_alloc_failed++; }
        return 0;
    }
    return 1;
}

int remaster_live_exec(remaster_live *r, machine_t *m, unsigned k)
{
    if (k == 0 && !rerun_extra_active()) {   /* the view matrix and attitude of this frame, for Stage 2 reprojection */
        const uint16_t cds = m->cpu.seg[S_DS];
        int16_t c[12];
        for (int a = 0; a < 9; a++) c[a] = (int16_t)peek16(m, cds, (uint16_t)(0x7B28 + 2 * a));
        for (int a = 0; a < 3; a++) c[9 + a] = (int16_t)peek16(m, cds, (uint16_t)(0x2DEE + 2 * a));
        if (!r->have_cam) { memcpy(r->cam_rec, c, sizeof c); r->have_cam = 1; r->cam_kinds = 1; }
        else if (memcmp(r->cam_rec, c, 9 * sizeof c[0])) r->cam_kinds++;
    }
    static int reach_log = -1, extra_count = -1;
    if (reach_log < 0) { reach_log = getenv("F117_REACH_LOG") != NULL; extra_count = getenv("F117_EXTRA_COUNT") != NULL; }
    if (reach_log && !rerun_extra_active()) {   /* diagnostic: where each model's body stops being drawn (the game's own walk only) */
        static struct { unsigned key, req, faced, size; double far_faced; } row[512];
        static unsigned nrow, tick; static int pending = -1; static double pending_real;
        const uint16_t ds = m->cpu.seg[S_DS], ss = m->cpu.seg[S_SS], sp = m->cpu.r[R_SP];
        if (k == 0) {
            const unsigned key = peek16(m, ss, (uint16_t)(sp + 4)) & 0x7FFF;
            unsigned i = 0; while (i < nrow && row[i].key != key) i++;
            if (i == nrow && nrow < 512) row[nrow++].key = key;
            pending = i < nrow ? (int)i : -1;
            if (pending >= 0) {
                int16_t pos[3], cam[9];
                for (int a = 0; a < 3; a++) pos[a] = (int16_t)peek16(m, ss, (uint16_t)(sp + 6 + 2 * a));
                for (int a = 0; a < 9; a++) cam[a] = (int16_t)peek16(m, ds, (uint16_t)(0x7B28 + 2 * a));
                const unsigned e = peek16(m, ds, 0xE328), ring = e >= 1 && e <= 4 ? e : 1;
                pending_real = (double)scene_model_depth(pos, cam) * (double)(1u << (2 * (ring - 1)));
                row[pending].req++;
            }
            if (++tick % 15000 == 0) {
                fprintf(stderr, "[reach] model word: requests, with faces, farthest real depth with faces, size word\n");
                for (unsigned q = 0; q < nrow; q++) if (row[q].req > 200)
                    fprintf(stderr, "[reach] %04X: %u req, %u faced, farthest faced %.0f, size %u\n", row[q].key, row[q].req, row[q].faced, row[q].far_faced, row[q].size);
            }
        } else if (k == 1 && pending >= 0) {
            row[pending].faced++;
            if (pending_real > row[pending].far_faced) row[pending].far_faced = pending_real;
            row[pending].size = peek16(m, ds, 0x7D5E);
            pending = -1;
        }
    }
    if (extra_count) {                       /* diagnostic: what the extra walks ask of the game, and how much survives its cull */
        static unsigned long placed[2], asked[2], faces[2], total;
        static unsigned long long mark, phase_a, phase_b, phase_c, other;
        static int phase = -1;                              /* 0 after a placement, 1 after a model request, 2 after the faces hook */
        const int x = rerun_extra_active() ? 1 : 0;
        const unsigned long long now = m->cpu.icount;
        if (x) {
            if (phase == 0) phase_a += now - mark; else if (phase == 1) phase_b += now - mark; else if (phase == 2) phase_c += now - mark;
            mark = now;
            phase = k == 4 ? 0 : k == 0 ? 1 : k == 1 ? 2 : phase;
        } else phase = -1;
        if (k == 4) placed[x]++; else if (k == 0) asked[x]++; else if (k == 1) faces[x]++;
        (void)other;
        if (k == 4 && ++total % 200000 == 0)
            fprintf(stderr, "[count] game walk: %lu placements, %lu model requests, %lu face draws | extra walks: %lu, %lu, %lu | extra instructions: placement %llu, request to faces %llu, faces onward %llu\n",
                    placed[0], asked[0], faces[0], placed[1], asked[1], faces[1], phase_a, phase_b, phase_c);
    }
    if (k == 0) {                                           /* the model request: return IP, CS, then the model */
        const uint16_t ss = m->cpu.seg[S_SS], sp = m->cpu.r[R_SP];
        {   const uint16_t caller = peek16(m, ss, sp);        /* the deferred pass D87C draws through DC46 (0DE00h to 0DFFFh); the immediate objects return into E2B6 */
            if (caller >= 0xDE00 && caller < 0xE000) rerun_flush_overlays();
        }
        if (r->pending) r->overwritten++;                   /* the previous request never reached a projection */
        r->pending = 1;
        r->requests++;
        r->model = peek16(m, ss, (uint16_t)(sp + 4));
        r->bank = peek16(m, m->cpu.seg[S_DS], 0x8590);
        r->origin[0] = (int16_t)peek16(m, m->cpu.seg[S_DS], 0x8602);
        r->origin[1] = (int16_t)peek16(m, m->cpu.seg[S_DS], 0x8604);
        r->window[0] = (int16_t)peek16(m, m->cpu.seg[S_DS], 0x85FA);
        r->window[1] = (int16_t)peek16(m, m->cpu.seg[S_DS], 0x85FC);
        r->window[2] = (int16_t)peek16(m, m->cpu.seg[S_DS], 0x85FE);
        r->window[3] = (int16_t)peek16(m, m->cpu.seg[S_DS], 0x8600);
        for (int i = 0; i < 3; i++) r->position[i] = (int16_t)peek16(m, ss, (uint16_t)(sp + 6 + 2 * i));
        r->pending_inv_depth = 1.0;
        r->pending_shrink = 0;
        r->pending_terrain = 0;
        memset(r->pending_camera_offset, 0, sizeof r->pending_camera_offset);
        for (unsigned u = r->nunits; u > 0; --u) {
            if (r->units[u - 1].bank == r->bank && r->units[u - 1].model == r->model &&
                !memcmp(r->units[u - 1].position, r->position, sizeof r->position)) {
                r->pending_inv_depth = r->units[u - 1].inv_depth;
                r->pending_shrink = r->units[u - 1].shrink;
                r->pending_terrain = r->units[u - 1].terrain;
                memcpy(r->pending_camera_offset, r->units[u - 1].camera_offset, sizeof r->pending_camera_offset);
                break;
            }
        }
        {   /* F117_MODEL_LOG=MODEL,FROM (diagnostic): every request for that model, its position, and whether its faces follow (see k == 1) */
            static int want = -2; static unsigned long long from;
            if (want == -2) { const char *e = getenv("F117_MODEL_LOG"); want = e ? atoi(e) : -2; const char *c = e ? strchr(e, ',') : NULL; from = c ? strtoull(c + 1, NULL, 0) : 0; }
            if (want >= -1 && (want < 0 || r->model == (unsigned)want) && m->cpu.icount >= from)
                fprintf(stderr, "[req] clock %llu model %u pos %d %d %d\n", (unsigned long long)m->cpu.icount, (unsigned)r->model, r->position[0], r->position[1], r->position[2]);
        }
    } else if (k == 1) {                                    /* the model's faces are about to be drawn: the pose is prepared */
        if (r->pending && m->mem[(((uint32_t)m->cpu.seg[S_DS] << 4) + 0x7D8E) & 0xFFFFF]) {
            r->pending = 0;                                 /* orthographic ([7D8E] not zero): a map tile, not part of the 3D view */
            r->ortho_skipped++;
        } else if (r->pending) {
            r->pending = 0;
            const size_t first = r->frame.n;
            unsigned dropped[3];
            memcpy(dropped, remaster_scene_dropped, sizeof dropped);
            remaster_scene_lod_shift(r->pending_shrink > 0 ? (unsigned)r->pending_shrink : 0);
            const int n = remaster_scene_add(r->scene, m->mem, 0x100000, m->cpu.seg[S_DS], r->bank, r->model, r->position, &r->frame);
            if (n < 0 || memcmp(dropped, remaster_scene_dropped, sizeof dropped)) r->frame_complete = 0;
            if (r->frame.n > first) {
                int32_t correction[3];
                model_camera_correction(m, r->pending_camera_offset, correction);
                finish_model(r->scene, &r->frame, first, n >= 0 ? r->pending_shrink : 0, r->pending_inv_depth, correction);
            }
            if (n >= 0 && r->pending_terrain) capture_template(r, m);
            if (n >= 0 && r->nchk) check_native(r, m, first);
            static int skip_own_faces = -1;
            if (skip_own_faces < 0) skip_own_faces = env_default_on("F117_NATIVE_SKIP_OWN_FACES");
            if (n >= 0 && (rerun_extra_active() || (skip_own_faces && r->replay_copy && r->pending_terrain))) {
                /* The copy's terrain geometry is in the frame now, and the
                 * renderer draws it from that, so the game's own face drawing is skipped (model_faces ends in a plain near RET). */
                static int skip = -1;
                if (skip < 0) { const char *e = getenv("F117_EXTRA_SKIP_FACES"); skip = !(e && e[0] == '0'); }
                if (skip) { r->skipped++; m->cpu.ip = cpu_pop16(&m->cpu); }
            }
            if (n < 0) r->refused++; else r->added++;
            if (n >= 0 && validate_on()) validate_model(r, m);
            {   static int want = -2;
                if (want == -2) { const char *e = getenv("F117_MODEL_LOG"); want = e ? atoi(e) : -1; }
                if (want >= 0 && r->model == (unsigned)want) fprintf(stderr, "[faces] model %u added %d\n", (unsigned)r->model, n);
                if (want >= 0 && r->model == (unsigned)want && r->frame.n > first) {      /* the first polygon's camera z against the scale it carries (the depth the compositor compares is inv_depth_scale / z) */
                    const remaster_prim *q = &r->frame.prim[first];
                    fprintf(stderr, "[faces-z] model %u prims %zu first z %d,%d,%d inv_depth %.6f shrink %d pos %d %d %d inv %.6f\n", (unsigned)r->model, r->frame.n - first, q->v[0].z, q->n > 1 ? q->v[1].z : 0, q->n > 2 ? q->v[2].z : 0, q->inv_depth_scale, (int)q->shrink, r->position[0], r->position[1], r->position[2], r->pending_inv_depth);
                }
            }
        }
    } else if (k == 4) {
        /* compose_emit_object's positions use the current terrain ring's units. Dynamic
         * models additionally shift world positions by their native model scale (BB0D).
         * Remember this at placement: E328 no longer describes a deferred object when
         * its geometry is eventually drawn. Read only; the guest is unchanged. */
        const uint16_t ss = m->cpu.seg[S_SS], sp = m->cpu.r[R_SP], ds = m->cpu.seg[S_DS];
        const unsigned ring = peek16(m, ds, 0xE328);
        int shift = ring >= 1 && ring <= 4 ? 2 * (4 - (int)ring) : 0;
        if (peek16(m, ss, sp) == 0xBC59)
            shift += (int16_t)peek16(m, ss, (uint16_t)(m->cpu.r[R_BP] + 0x16));
        const uint16_t type = peek16(m, ss, (uint16_t)(sp + 2)) & 0x7FFF;
        {   /* F117_CLASS_LOG=1 (test only): every placement's class byte (E2B6: the table at [0A60 + 2 * (model >> 8)] indexed by the low byte; bit 7 = deferred), counted by table and model, printed every 2000 placements */
            static int cl = -1; static unsigned cnt[8][128][2], n;
            if (cl < 0) cl = getenv("F117_CLASS_LOG") != NULL;
            if (cl) {
                const unsigned hi = (type >> 8) & 7, lo = type & 0x7F;
                const uint16_t tab = peek16(m, ds, (uint16_t)(0x0A60 + 2 * hi));
                const unsigned cls = m->mem[(((uint32_t)ds << 4) + (uint16_t)(tab + lo)) & 0xFFFFF];
                cnt[hi][lo][(cls & 0x80) ? 1 : 0]++;
                if (++n % 2000 == 0) { fprintf(stderr, "[class] after %u placements, immediate (bit 7 clear) / deferred by table.model:", n); for (unsigned i = 0; i < 8; i++) for (unsigned j = 0; j < 128; j++) if (cnt[i][j][0] || cnt[i][j][1]) fprintf(stderr, " %u.%u=%u/%u", i, j, cnt[i][j][0], cnt[i][j][1]); fputc(10, stderr); } } }
        if (shift >= -16 && shift <= 16 && r->nunits < UNITS_MAX) {
            const unsigned u = r->nunits++;
            r->units[u].bank = peek16(m, ds, (uint16_t)(0xB78A + 2 * (type >> 8)));
            r->units[u].model = type & 0x7F;
            r->units[u].position[0] = (int16_t)(peek16(m, ss, sp + 10) - peek16(m, ds, 0x49AC));
            r->units[u].position[1] = (int16_t)(peek16(m, ss, sp + 14) - peek16(m, ds, 0x49B0));
            r->units[u].position[2] = (int16_t)(peek16(m, ds, 0x49AE) - peek16(m, ss, sp + 12));
            r->units[u].inv_depth = ldexp(1.0, shift);
            r->units[u].shrink = rerun_scale_shift();
            r->units[u].terrain = peek16(m, ss, sp) == 0x06A6 || peek16(m, ss, sp) == 0x075E;
            memset(r->units[u].camera_offset, 0, sizeof r->units[u].camera_offset);
            const unsigned caller = peek16(m, ss, sp);
            uint32_t view[3] = { 0, 0, 0 };
            if (caller == 0x06A6 || caller == 0x075E) {
                const uint16_t bp = m->cpu.r[R_BP];
                for (int axis = 0; axis < 3; ++axis) {
                    const uint16_t at = (uint16_t)(bp + 8 + 4 * axis);
                    view[axis] = peek16(m, ss, at) | (uint32_t)peek16(m, ss, (uint16_t)(at + 2)) << 16;
                }
                remaster_ring_camera_offset(view, ring, r->units[u].camera_offset);
            }
            /* Read-only diagnostic for terrain-ring camera quantization. */
            static const char *placement_from; static int placement_all = -1;       /* read once: this runs for every placement */
            if (placement_all < 0) { placement_from = getenv("F117_REMASTER_PLACEMENT_LOG_FROM"); placement_all = getenv("F117_REMASTER_PLACEMENT_LOG_ALL") != NULL; }
            static unsigned placements_logged[5];
            if (placement_from && ring >= 1 && ring <= 4 && placements_logged[ring] < (placement_all ? ~0u : 4u) &&
                (caller == 0x06A6 || caller == 0x075E) &&
                m->cpu.icount >= strtoull(placement_from, NULL, 0)) {
                fprintf(stderr, "[placement] clock %llu ring %u caller %04X model %u view %u,%u,%u native-eye %d,%d,%d position %d,%d,%d\n",
                        (unsigned long long)m->cpu.icount, ring, caller, type, view[0], view[1], view[2],
                        (int16_t)peek16(m, ds, 0x49AC), (int16_t)peek16(m, ds, 0x49AE), (int16_t)peek16(m, ds, 0x49B0),
                        r->units[u].position[0], r->units[u].position[1], r->units[u].position[2]);
                ++placements_logged[ring];
            }
        }
    } else if (k == 3) {                                    /* the HUD pass: what follows is drawn over the HUD */
        rerun_flush_overlays();                             /* far overlays no deferred model came for */
        rerun_prof_mark(4);
        {   /* F117_DS_DUMP=N,FILE (diagnostic): the data segment as the Nth render copy reaches its HUD pass, to diff one walk mode against another */
            static int want = -2; static const char *path; static unsigned seen;
            { static unsigned dbg; if (getenv("F117_DS_DEBUG") && dbg++ < 3) fprintf(stderr, "[ds-dump] the HUD pass hook ran (%u)\n", dbg); }
            if (want == -2) { const char *e = getenv("F117_DS_DUMP"); want = e ? atoi(e) : -1; const char *c = e ? strchr(e, ',') : NULL; path = c ? c + 1 : NULL; }
            if (want >= 0 && path && ++seen == (unsigned)want) {
                FILE *df = fopen(path, "wb");
                if (df) {
                    const uint32_t base = ((uint32_t)m->cpu.seg[S_DS] << 4) & 0xFFFFF;
                    for (uint32_t i = 0; i < 65536; i++) fputc(m->mem[(base + i) & 0xFFFFF], df);
                    fclose(df);
                    fprintf(stderr, "[ds-dump] copy %u: data segment written to %s\n", seen, path);
                }
            }
        }
        if (r->hud_at < 0) r->hud_at = (long)r->frame.n;
    } else if (k == 2) {                                    /* the per-frame draw routine: the last frame is complete */
        rerun_prof_mark(1);
        r->nunits = 0;
        {   static int diag = -1;
            if (diag < 0) diag = getenv("F117_HUD_ORDER") != NULL;
            if (diag) fprintf(stderr, "[hud-order] clock %llu: %zu primitives, HUD pass after %ld\n", (unsigned long long)m->cpu.icount, r->frame.n, r->hud_at);
        }
        {   /* F117_SRC_SPLIT=1 (diagnostic, the render copy): how much of the completed frame was placed natively and how much the game drew */
            static int on = -1; static unsigned long long fr_total, fr_native, objs; static unsigned calls;
            if (on < 0) on = getenv("F117_SRC_SPLIT") != NULL;
            if (on && r->replay_copy && r->frame.n > 50) {
                fr_total += r->frame.n; fr_native += r->src_native_prims; objs += r->src_native_objs;
                if (++calls % 120 == 1) fprintf(stderr, "[src-split] %u copy frames: %.1f%% of their primitives placed natively (%.0f objects a frame, %.0f primitives of %.0f)\n", calls, 100.0 * (double)fr_native / (double)fr_total,
                                                (double)objs / calls, (double)fr_native / calls, (double)fr_total / calls);
            }
            r->src_native_prims = 0; r->src_native_objs = 0;
        }
        r->done_hud_at = r->hud_at; r->hud_at = -1;
        {   /* F117_FAR_WHY (diagnostic): the frame being completed, and how many primitives of models 0, 3 and 6 it holds */
            static int wl = -1; static unsigned left = 40;
            if (wl < 0) wl = getenv("F117_FAR_WHY") != NULL;
            static unsigned long long why_from; static int why_from_init;
            if (!why_from_init) { why_from_init = 1; const char *e = getenv("F117_FAR_WHY_FROM"); why_from = e ? strtoull(e, NULL, 0) : 0; }
            if (wl && left && r->replay_copy && r->frame.n >= 380 && r->frame.n <= 700 && m->cpu.icount >= why_from) {
                unsigned c036 = 0;
                for (size_t i = 0; i < r->frame.n; i++) { const unsigned md = r->frame.prim[i].model & 1023; if (md == 0 || md == 3 || md == 6) c036++; }
                left--;
                fprintf(stderr, "[why] frame completed at clock %llu: %zu primitives, %u of models 0, 3 and 6\n", (unsigned long long)m->cpu.icount, r->frame.n, c036);
            }
        }
        {   /* F117_BLEND_CHECK: the live instance's completed steps, newest last, to find the ones the game's copy at t = 0 and t = 1 reproduces */
            static int on = -1;
            if (on < 0) on = getenv("F117_BLEND_CHECK") != NULL;
            if (on && !r->replay_copy && r->frame.n > 0) {
                extern remaster_frame_copy *g_live_ring[8]; extern unsigned long long g_live_ring_steps;
                remaster_frame_copy *c = g_live_ring[g_live_ring_steps % 8];
                if (!c) c = g_live_ring[g_live_ring_steps % 8] = remaster_frame_copy_new();
                if (c->cap < r->frame.n) { remaster_prim *q = (remaster_prim *)realloc(c->prim, r->frame.n * sizeof *q); if (q) { c->prim = q; c->cap = r->frame.n; } }
                if (c->cap >= r->frame.n) { memcpy(c->prim, r->frame.prim, r->frame.n * sizeof *c->prim); c->n = r->frame.n; c->origin[0] = r->origin[0]; c->origin[1] = r->origin[1]; g_live_ring_steps++; }
            }
        }
        {   /* F117_STEP_PAIR=1 (diagnostic, the live instance only): how well the primitives of one logic step pair with the previous step's, by index and by (model, kind, vertex count) */
            static int on = -1;
            if (on < 0) on = getenv("F117_STEP_PAIR") != NULL;
            if (on && !r->replay_copy && r->frame.n > 50) {
                static remaster_prim *prev; static size_t pn, pcap; static unsigned steps; static double sum_idx, sum_key, sum_move; static unsigned long long moved_n;
                size_t same_idx = 0, same_key = 0;
                const size_t n = r->frame.n < pn ? r->frame.n : pn;
                for (size_t i = 0; i < n; i++) {
                    const remaster_prim *a2 = &r->frame.prim[i], *b2 = &prev[i];
                    if (a2->model == b2->model && a2->kind == b2->kind && a2->n == b2->n) {
                        same_idx++;
                        double d = 0; for (int v = 0; v < a2->n; v++) d += fabs((double)a2->v[v].x - b2->v[v].x) / 256.0;
                        sum_move += d / (a2->n ? a2->n : 1); moved_n++;
                    }
                }
                /* by key, in order within the key: a greedy match of each primitive to the first unused previous one with the same (model, kind, n, colour word) */
                static unsigned char *used; static size_t ucap;
                if (ucap < pn) { free(used); used = (unsigned char *)malloc(pn); ucap = used ? pn : 0; }
                if (used) {
                    memset(used, 0, pn);
                    size_t from = 0;
                    for (size_t i = 0; i < r->frame.n; i++) {
                        const remaster_prim *a2 = &r->frame.prim[i];
                        for (size_t j = from; j < pn; j++)
                            if (!used[j] && prev[j].model == a2->model && prev[j].kind == a2->kind && prev[j].n == a2->n && prev[j].colour_word == a2->colour_word) { used[j] = 1; same_key++; if (j == from) from++; break; }
                    }
                }
                steps++; sum_idx += pn ? (double)same_idx / (double)(r->frame.n > pn ? r->frame.n : pn) : 0; sum_key += pn ? (double)same_key / (double)(r->frame.n > pn ? r->frame.n : pn) : 0;
                if (steps % 20 == 1)
                    fprintf(stderr, "[step-pair] step %u: %zu primitives (previous %zu): %zu pair by index, %zu by key; means over %u steps: %.1f%% by index, %.1f%% by key; mean x movement of index pairs %.2f game pixels\n",
                            steps, r->frame.n, pn, same_idx, same_key, steps, 100.0 * sum_idx / steps, 100.0 * sum_key / steps, moved_n ? sum_move / (double)moved_n : 0.0);
                if (pcap < r->frame.n) { prev = (remaster_prim *)realloc(prev, r->frame.n * sizeof *prev); pcap = prev ? r->frame.n : 0; }
                if (prev) { memcpy(prev, r->frame.prim, r->frame.n * sizeof *prev); pn = r->frame.n; }
            }
        }
        if (validate_on() && r->frames == 40) report_validation(r);
        if (r->out_path && !r->written && r->frames == r->want_frame && r->frame.n) write_frame(r, m);
        {   /* F117_FRAME_HASH=FROM (diagnostic): one line per completed frame from that clock, a hash of every primitive, to compare two runs (F117_FAR_PAR=0 against the default) */
            static int hs = -1; static unsigned long long from;
            if (hs < 0) { const char *e = getenv("F117_FRAME_HASH"); hs = e != NULL; from = e ? strtoull(e, NULL, 0) : 0; }
            if (hs && r->frame.n > 2000 && m->cpu.icount >= from) {
                uint64_t h = 1469598103934665603ull, hf[6] = { 1, 1, 1, 1, 1, 1 };
                const unsigned char *b = (const unsigned char *)r->frame.prim;
                for (size_t i = 0; i < r->frame.n; i++) {
                    const remaster_prim *q = &r->frame.prim[i];
                    uint32_t w[6] = { (uint32_t)q->kind, (uint32_t)q->n, (q->owner & 0xFFFu) | (i && (q->owner >> 12) != (r->frame.prim[i - 1].owner >> 12) ? 0x1000u : 0u), q->colour_word, (uint32_t)q->shade | (uint32_t)q->darken << 8 | (uint32_t)q->source << 16 | (uint32_t)q->shrink << 24, q->face | (uint32_t)q->model << 16 };
                    for (int k = 0; k < 6; k++) { h = (h ^ w[k]) * 1099511628211ull; hf[k] = (hf[k] ^ w[k]) * 1099511628211ull; }
                    for (int v = 0; v < q->n; v++) { h = (h ^ (uint32_t)q->v[v].x) * 1099511628211ull; h = (h ^ (uint32_t)q->v[v].y) * 1099511628211ull; h = (h ^ (uint32_t)q->v[v].z) * 1099511628211ull; }
                    uint64_t dbits; memcpy(&dbits, &q->inv_depth_scale, 8); h = (h ^ dbits) * 1099511628211ull;
                }
                (void)b;
                fprintf(stderr, "[frame-hash] clock %llu prims %zu hash %016llx complete %d fields %llx %llx %llx %llx %llx %llx\n", (unsigned long long)m->cpu.icount, r->frame.n, (unsigned long long)h, r->frame_complete, (unsigned long long)hf[0], (unsigned long long)hf[1], (unsigned long long)hf[2], (unsigned long long)hf[3], (unsigned long long)hf[4], (unsigned long long)hf[5]);
            }
        }
        if (r->deferred) {                      /* the completed frame is staged: the main thread commits it when the worker is idle */
            remaster_frame tmp = r->staged; r->staged = r->frame; r->frame = tmp;
            memcpy(r->staged_band, r->band_rec, sizeof r->staged_band);
            r->staged_have_band = r->have_band; r->have_staged = r->staged.n > 0;
            memcpy(r->staged_cam, r->cam_rec, sizeof r->staged_cam); r->staged_have_cam = r->have_cam;
            r->staged_complete = r->frame_complete;
            memcpy(r->staged_origin, r->origin, sizeof r->origin); memcpy(r->staged_window, r->window, sizeof r->window);
        } else {                                /* the completed frame becomes the one a composite uses */
            remaster_frame tmp = r->done; r->done = r->frame; r->frame = tmp;
            memcpy(r->done_band, r->band_rec, sizeof r->done_band);
            r->done_have_band = r->have_band; r->have_done = r->done.n > 0;
            memcpy(r->done_cam, r->cam_rec, sizeof r->done_cam); r->done_have_cam = r->have_cam;
            r->done_complete = r->frame_complete;
            memcpy(r->done_origin, r->origin, sizeof r->origin); memcpy(r->done_window, r->window, sizeof r->window);
        }
        {                                       /* this frame's band pass starts here: its argument is the view block */
            const uint16_t ds = m->cpu.seg[S_DS], view = peek16(m, m->cpu.seg[S_SS], (uint16_t)(m->cpu.r[R_SP] + 2));
            int n = 0;
            r->band_rec[n++] = BAND_REC_VERSION;
            r->band_rec[n++] = view;
            r->band_rec[n++] = peek16(m, ds, (uint16_t)(view + 4));
            for (int i = 0; i < BAND_NWORDS; i++)
                r->band_rec[n++] = band_rec_byte[i] ? m->mem[(((uint32_t)ds << 4) + band_rec_off[i]) & 0xFFFFF] : peek16(m, ds, band_rec_off[i]);
            r->have_band = 1;
        }
        r->frames++;
        { static int ck = -1; static unsigned nck; if (ck < 0) ck = getenv("F117_CAM_KINDS") != NULL; static unsigned nmulti, nall;
          if (ck) { nall++; if (r->cam_kinds > 1) nmulti++;
                    if (r->cam_kinds > 1 && nck < 20) { nck++; fprintf(stderr, "[cam-kinds] frame %u: %d view matrices (heading %d)\n", r->frames, r->cam_kinds, r->cam_rec[9]); }
                    if (nall % 100 == 0) fprintf(stderr, "[cam-kinds] %u frames, %u with more than one view matrix\n", nall, nmulti); } }
        r->have_cam = 0; r->cam_kinds = 0;
        remaster_frame_clear(&r->frame);
        r->frame_complete = 1;
        r->pending = 0;
    }
    return 0;
}

void remaster_live_overlay_dot(remaster_live *r, double x, double y, unsigned colour_word)
{
    remaster_live_overlay_rect(r, x, y, 1.0, 1.0, colour_word, 0, 99);
}

void remaster_live_overlay_rect(remaster_live *r, double x, double y, double w, double h, unsigned colour_word, int clip_rows, int layer)
{
    if (!r) return;
    if (clip_rows) {                            /* the sprite blitter's clip: rows inside the 3D clip window (y0..y1 inclusive) */
        const double lo = r->window[1], hi = (double)r->window[3] + 1.0;
        if (y < lo) { h -= lo - y; y = lo; }
        if (y + h > hi) h = hi - y;
        if (h <= 0.0 || w <= 0.0) return;
    }
    {   /* never under one screen pixel (the far-size rule): a smaller rectangle grows about its centre; the scale is the picture's height over the game's 200 rows */
        const double px = r->h > 200 ? 200.0 / r->h : 1.0;
        if (w < px) { x -= (px - w) / 2; w = px; }
        if (h < px) { y -= (px - h) / 2; h = px; }
    }
    remaster_frame *f = &r->frame;
    if (f->n == f->cap) {
        const size_t cap = f->cap ? f->cap * 2 : 256;
        remaster_prim *q = (remaster_prim *)realloc(f->prim, cap * sizeof *q);
        if (!q) return;
        f->prim = q; f->cap = cap;
    }
    remaster_prim *p = &f->prim[f->n++];
    memset(p, 0, sizeof *p);
    /* The game's far dot is one of its pixels (matched.c: the flat pixel at [4A10],[4A18]), and its pixel x covers x .. x + 1 of the wide picture's
     * scale (draw_scene_ex: cx = origin * ux + xoff), so the dot is that square as a polygon: exactly one game pixel at every scale, where a dot
     * primitive is 1.5 pixels wide and centred on the corner. In front of everything (the near plane is 0x10000), as the flat dot is drawn last;
     * an owner no object has (serial FFFFFh, face FFFh), so no line takes it for its own face. */
    /* layer 0..3: a constant depth in front of everything (the layers, rerun.h: 0 blast on top, 3 far dots at the back), for what the original draws flat and last.
     * layer < 0: the true depth, -layer in the game's B5DC units (-z' of the call that placed it), for what the original depth-sorts (a deferred sprite, a model):
     * the compositor compares inv_depth_scale / z, and the game's own wreck model (BB0D) measured z / inv_depth_scale = 8191 x its B5DC depth at depths 20 to 49
     * (12 Nov 2026, [faces-z]: 4,193,792 / 512 per unit), so the polygon gets z / inv = 8191 x depth, z kept near 2^22 by the scale so int32 coordinates cannot overflow. */
    double z, inv = 0.0;
    if (layer == 99) {                          /* a flat pixel the original paints BEFORE the deferred objects (the far aircraft and projectile dots, 983E; the deferred pass D87C paints nearly all scenery over them at any depth, ground the walk painted earlier it does not): at an infinite depth as an ordinary object, so the rasteriser's own rules hide it behind every object and show it over ground and sky (docs/wide-margin-audit.md, 12 Nov) */
        z = 4194304.0; inv = 1.0 / 20000.0;
    } else if (layer < 0) {
        const double zt = 8191.0 * (double)(-layer);
        const double k = zt > 4194304.0 ? zt / 4194304.0 : 1.0;
        z = zt / k; inv = 1.0 / k;
        if (z < 0x20000) z = 0x20000;
    } else z = 0x20000 + 0x200 * (layer > 3 ? 3 : layer);
    const double cx[4] = { 0, w, w, 0 }, cy[4] = { 0, 0, h, h };
    p->kind = SCENE_POLYGON;
    p->n = 4;
    for (int k = 0; k < 4; k++) {
        p->v[k].x = (int32_t)llround((x + cx[k] - r->origin[0]) * z / 256.0);
        p->v[k].y = (int32_t)llround((y + cy[k] - r->origin[1]) * z / 256.0);
        p->v[k].z = (int32_t)z;
    }
    p->owner = (layer == 99 || layer < 0) ? 0xFFFFF7FEu : 0xFFFFF7FFu;   /* layer 99 (far dots) and true-depth overlays (puffs, wreck dot: deferred, called in 983E like the dots): painted over what is already in the frame, then a later (deferred) object covers them by its own depth */     /* 7FEh: the far dot's mark, drawn over what is there (remaster_raster.c, polys.hlsl); otherwise serial FFFFFh, face 7FFh: no object's, and bit 11 CLEAR: with it set the rasteriser takes the polygon for ground, which never hides another object's lines, so every distant light showed through a margin puff */
    p->colour_word = (uint16_t)colour_word;
    p->colour_index = (uint8_t)(colour_word & 255);
    if (inv > 0.0) p->inv_depth_scale = inv;
}

void remaster_live_overlay_line(remaster_live *r, double x0, double y0, double x1, double y1, unsigned colour_word)
{
    if (!r) return;
    remaster_frame *f = &r->frame;
    if (f->n == f->cap) {
        const size_t cap = f->cap ? f->cap * 2 : 256;
        remaster_prim *q = (remaster_prim *)realloc(f->prim, cap * sizeof *q);
        if (!q) return;
        f->prim = q; f->cap = cap;
    }
    remaster_prim *p = &f->prim[f->n++];
    memset(p, 0, sizeof *p);
    const double z = 0x20200, xs[2] = { x0, x1 }, ys[2] = { y0, y1 };      /* layer 1 */      /* in front of everything, as the flat tracer is drawn last; no object's owner */
    p->kind = SCENE_LINES;
    p->n = 2;
    for (int k = 0; k < 2; k++) {
        p->v[k].x = (int32_t)llround((xs[k] - r->origin[0]) * z / 256.0);
        p->v[k].y = (int32_t)llround((ys[k] - r->origin[1]) * z / 256.0);
        p->v[k].z = (int32_t)z;
    }
    p->owner = 0xFFFFF7FFu;     /* serial FFFFFh, face 7FFh: no object's, and bit 11 CLEAR: with it set the rasteriser takes the polygon for ground, which never hides another object's lines, so every distant light showed through a margin puff */
    p->colour_word = (uint16_t)colour_word;
    p->colour_index = (uint8_t)(colour_word & 255);
}

size_t remaster_live_prims(const remaster_live *r) { return r->frame.n; }
size_t remaster_live_done_prims(const remaster_live *r) { return r->have_done ? r->done.n : 0; }

/* F117_COPY_FP (diagnostic): a fingerprint of the done frame's primitives (every field that reaches a picture, not the padding) and of its band, origin and window. */
unsigned long long remaster_live_done_fingerprint(const remaster_live *r)
{
    unsigned long long h = 1469598103934665603ull;
#define FP(x) do { const unsigned char *b_ = (const unsigned char *)&(x); for (size_t k_ = 0; k_ < sizeof(x); k_++) { h ^= b_[k_]; h *= 1099511628211ull; } } while (0)
    if (!r->have_done) return h;
    for (size_t i = 0; i < r->done.n; i++) {
        const remaster_prim *p = &r->done.prim[i];
        FP(p->kind); FP(p->n); for (int v = 0; v < p->n && v < REMASTER_PRIM_VERTICES; v++) FP(p->v[v]);
        FP(p->colour_index); FP(p->colour_word); FP(p->shade); FP(p->darken); FP(p->face); FP(p->model); FP(p->owner); FP(p->flat);
    }
    for (int i = 0; i < BAND_REC_BASE; i++) FP(r->done_band[i]);
    FP(r->done_have_band); FP(r->done_complete); FP(r->done_origin[0]); FP(r->done_origin[1]);
    for (int i = 0; i < 4; i++) FP(r->done_window[i]);
#undef FP
    return h;
}
unsigned remaster_live_frames(const remaster_live *r) { return r->frames; }

/* ---- Piece 2: the blend check (F117_BLEND_CHECK=1, docs/gpu-compositor.md) ---- */

remaster_frame_copy *remaster_frame_copy_new(void) { return (remaster_frame_copy *)calloc(1, sizeof(remaster_frame_copy)); }
void remaster_frame_copy_free(remaster_frame_copy *c) { if (c) { free(c->prim); free(c); } }

int remaster_live_done_clone(const remaster_live *r, remaster_frame_copy *out)
{
    if (!r || !out || !r->have_done) return 0;
    if (out->cap < r->done.n) {
        remaster_prim *q = (remaster_prim *)realloc(out->prim, r->done.n * sizeof *q);
        if (!q) return 0;
        out->prim = q; out->cap = r->done.n;
    }
    memcpy(out->prim, r->done.prim, r->done.n * sizeof *out->prim);
    out->n = r->done.n;
    out->origin[0] = r->done_origin[0]; out->origin[1] = r->done_origin[1];
    memcpy(out->band, r->done_band, sizeof out->band); out->have_band = r->done_have_band; memcpy(out->window, r->done_window, sizeof out->window);
    out->complete = r->done_complete; out->hud_at = r->done_hud_at;
    memcpy(out->cam, r->done_cam, sizeof out->cam); out->have_cam = r->done_have_cam;
    return 1;
}

/* Part B Stage 2: like remaster_live_done_clone, but the done frame's primitive buffer is handed over to `out` and `out`'s old buffer takes its place: no copy. For an instance that
 * nobody else reads (the scene copies'); its next copy rebuilds the frame, and its done frame is empty until then. */
int remaster_live_done_take(remaster_live *r, remaster_frame_copy *out)
{
    if (!r || !out || !r->have_done) return 0;
    remaster_prim *tp = out->prim; const size_t tc = out->cap;
    out->prim = r->done.prim; out->cap = r->done.cap; out->n = r->done.n;
    r->done.prim = tp; r->done.cap = tc; r->done.n = 0; r->have_done = 0;
    out->origin[0] = r->done_origin[0]; out->origin[1] = r->done_origin[1];
    memcpy(out->band, r->done_band, sizeof out->band); out->have_band = r->done_have_band; memcpy(out->window, r->done_window, sizeof out->window);
    out->complete = r->done_complete; out->hud_at = r->done_hud_at;
    memcpy(out->cam, r->done_cam, sizeof out->cam); out->have_cam = r->done_have_cam;
    return 1;
}

/* ---- Part B Stage 2: a picture's scene from two step scenes (F117_STAGE2; docs/gpu-compositor.md) ----------------------------------------------------------
 * The scene of an in-between picture is the blend of the scenes of the two game steps it lies between, not a render copy at its own t. The blend takes the primitives of the
 * nearer step in their own order (painter order and the HUD split are kept) and moves each one's vertices towards its partner in the other step, paired by (kind, vertex count,
 * model, face, colour word) and, among several of the same key, the unused one whose first vertex is nearest on the screen; a primitive with no partner is drawn as the nearer step
 * has it. The band state is blended by band_blend (the camera height, and the pitch and roll on the shortest arc). Origin, window and the rest come from the nearer step. */
static double blend_pos(const remaster_prim *p, int axis);
typedef struct { uint64_t key; uint32_t i; } stage2_key;
static int g_stage2_mesh_key;
static uint64_t stage2_pkey(const remaster_prim *p)
{
    uint64_t h = 1469598103934665603ull;
#define MIX2(x) do { h ^= (uint64_t)(x); h *= 1099511628211ull; } while (0)
    MIX2(p->kind); MIX2(p->n); MIX2(p->model); MIX2(p->face); MIX2(p->colour_word);
    if (g_stage2_mesh_key) MIX2(p->mesh_item);
#undef MIX2
    return h;
}
static int stage2_cmp(const void *x, const void *y)
{
    const stage2_key *a = (const stage2_key *)x, *b = (const stage2_key *)y;
    if (a->key != b->key) return a->key < b->key ? -1 : 1;
    return a->i < b->i ? -1 : a->i > b->i;
}
#define STAGE2_WINDOW 12              /* the k-th primitive of a key in one step is paired with the best unused one among the k-th +- this many of the same key in the other */
#define STAGE2_MAX_MOVE 96.0          /* game pixels: a pair any of whose vertices would move farther than this between two steps is not a pair (it would draw as a sliver across the picture) */

static double stage2_move(const remaster_prim *p, const remaster_prim *q)
{
    double worst = 0.0;
    for (int v = 0; v < p->n && v < q->n; v++) {         /* every vertex, on the screen; vertices behind the near plane are clamped, as blend_pos does */
        const double z1 = p->v[v].z > 65536 ? (double)p->v[v].z : 65536.0, z2 = q->v[v].z > 65536 ? (double)q->v[v].z : 65536.0;
        const double dx = 256.0 * ((double)p->v[v].x / z1 - (double)q->v[v].x / z2), dy = 256.0 * ((double)p->v[v].y / z1 - (double)q->v[v].y / z2);
        const double d = dx * dx + dy * dy;
        if (d > worst) worst = d;
    }
    return worst;
}

struct remaster_stage2_pairs { int *ab, *ba; size_t na, nb, cap_a, cap_b;
    int ncls, have_delta; double cls[8], delta[8][3];   /* reprojection: the camera's travel from step a to step b in each depth scale's units (median over paired primitives) */ };
remaster_stage2_pairs *remaster_stage2_pairs_new(void) { return (remaster_stage2_pairs *)calloc(1, sizeof(remaster_stage2_pairs)); }
void remaster_stage2_pairs_free(remaster_stage2_pairs *p) { if (p) { free(p->ab); free(p->ba); free(p); } }

/* The pairing on helper threads (17 November 2026). The pairing thread had 41 ms of work at the end of the late flight (keys 3, two qsorts 12, matching 27) and the pictures of a step waited for it.
 * Every part has an order-independent form with the serial result exactly: the keys of each primitive are independent; the sort is by (key, index), a total order, so chunks sorted on
 * their own and merged give the same array; the matching works inside one key group at a time and a group reads and writes only its own entries of ab and ba, so the groups can be
 * split into ranges at group boundaries. F117_STAGE2_PAIR_SERIAL=1 runs the serial reference; F117_STAGE2_PAIR_CHECK=1 runs both every pairing and compares ab and ba byte for byte. */
static void (*g_s2_prun)(void (*)(void *, int), void *, int);
static int g_s2_pparts;
void remaster_live_set_stage2_pair_parallel(void (*run)(void (*)(void *, int), void *, int), int parts) { g_s2_prun = run; g_s2_pparts = parts; }

typedef struct {
    const remaster_frame_copy *a, *b;
    stage2_key *nk, *fk;
    int *ab, *ba;
    int parts;                                      /* jobs of one dispatch (at most 8) */
    size_t start[9];                                /* the matching ranges' first entries of nk, on group boundaries */
} pair_job;

static void pair_keys_part(void *vj, int part)
{
    pair_job *j = (pair_job *)vj;
    const size_t na = j->a->n, nb = j->b->n;
    const size_t a0 = na * (size_t)part / (size_t)j->parts, a1 = na * (size_t)(part + 1) / (size_t)j->parts;
    const size_t b0 = nb * (size_t)part / (size_t)j->parts, b1 = nb * (size_t)(part + 1) / (size_t)j->parts;
    for (size_t i = a0; i < a1; i++) { j->nk[i].key = stage2_pkey(&j->a->prim[i]); j->nk[i].i = (uint32_t)i; j->ab[i] = -1; }
    for (size_t i = b0; i < b1; i++) { j->fk[i].key = stage2_pkey(&j->b->prim[i]); j->fk[i].i = (uint32_t)i; j->ba[i] = -1; }
}

/* sort jobs: parts/2 chunks of nk, then parts/2 chunks of fk */
static void pair_sort_part(void *vj, int job)
{
    pair_job *j = (pair_job *)vj;
    const int half = j->parts / 2;
    stage2_key *arr = job < half ? j->nk : j->fk;
    const size_t n = job < half ? j->a->n : j->b->n;
    const int c = job < half ? job : job - half;
    const size_t from = n * (size_t)c / (size_t)half, to = n * (size_t)(c + 1) / (size_t)half;
    qsort(arr + from, to - from, sizeof *arr, stage2_cmp);
}

static void pair_merge2(const stage2_key *s, size_t a0, size_t a1, size_t b0, size_t b1, stage2_key *d)
{
    size_t i = a0, k = b0, o = a0;
    while (i < a1 && k < b1) d[o++] = stage2_cmp(&s[i], &s[k]) <= 0 ? s[i++] : s[k++];
    while (i < a1) d[o++] = s[i++];
    while (k < b1) d[o++] = s[k++];
}

/* the sorted chunks (half of them) merged into one sorted array, through a scratch array of the same size */
static int pair_merge_chunks(stage2_key *arr, size_t n, int chunks)
{
    if (chunks <= 1) return 1;
    stage2_key *tmp = (stage2_key *)malloc((n ? n : 1) * sizeof *tmp);
    if (!tmp) return 0;
    size_t bounds[9];
    for (int c = 0; c <= chunks; c++) bounds[c] = n * (size_t)c / (size_t)chunks;
    stage2_key *src = arr, *dst = tmp;
    int runs = chunks;
    while (runs > 1) {
        int out = 0;
        size_t nb[9];
        for (int c = 0; c < runs; c += 2) {
            if (c + 1 < runs) { pair_merge2(src, bounds[c], bounds[c + 1], bounds[c + 1], bounds[c + 2], dst); nb[out++] = bounds[c]; }
            else { memcpy(dst + bounds[c], src + bounds[c], (bounds[c + 1] - bounds[c]) * sizeof *dst); nb[out++] = bounds[c]; }
        }
        nb[out] = n;
        for (int c = 0; c <= out; c++) bounds[c] = nb[c];
        runs = out;
        stage2_key *t = src; src = dst; dst = t;
    }
    if (src != arr) memcpy(arr, src, n * sizeof *arr);
    free(tmp);
    return 1;
}

/* the matching of the key groups that start in nk[ns0, ns1): the same loop as the serial one, with fi found by a search */
static void pair_match_range(const remaster_frame_copy *a, const remaster_frame_copy *b, const stage2_key *nk, const stage2_key *fk, int *ab, int *ba, size_t ns0, size_t ns1)
{
    if (ns0 >= ns1) return;
    size_t lo = 0, hi = b->n;                       /* the first entry of fk whose key is not below nk[ns0].key */
    while (lo < hi) { const size_t mid = lo + (hi - lo) / 2; if (fk[mid].key < nk[ns0].key) lo = mid + 1; else hi = mid; }
    size_t fi = lo;
    for (size_t ns = ns0; ns < ns1;) {
        size_t ne = ns;
        while (ne < a->n && nk[ne].key == nk[ns].key) ne++;
        while (fi < b->n && fk[fi].key < nk[ns].key) fi++;
        size_t fs = fi, fe = fi;
        while (fe < b->n && fk[fe].key == nk[ns].key) fe++;
        const long fn = (long)(fe - fs);
        for (size_t q = ns; q < ne && fn > 0; q++) {
            const remaster_prim *p = &a->prim[nk[q].i];
            long best = -1; double bd = 1e300;
            const long centre = (long)(q - ns) < fn ? (long)(q - ns) : fn - 1;
            for (long d = -STAGE2_WINDOW; d <= STAGE2_WINDOW; d++) {
                const long j = centre + d;
                if (j < 0 || j >= fn) continue;
                const uint32_t fj = fk[fs + (size_t)j].i;
                const remaster_prim *c = &b->prim[fj];
                if (ba[fj] >= 0 || c->inv_depth_scale != p->inv_depth_scale) continue;
                const double mv = stage2_move(p, c);
                if (mv <= STAGE2_MAX_MOVE * STAGE2_MAX_MOVE && mv < bd) { bd = mv; best = (long)fj; }
            }
            if (best >= 0) { ba[best] = (int)nk[q].i; ab[nk[q].i] = (int)best; }
        }
        ns = ne;
    }
}

static void pair_match_part(void *vj, int part)
{
    pair_job *j = (pair_job *)vj;
    pair_match_range(j->a, j->b, j->nk, j->fk, j->ab, j->ba, j->start[part], j->start[part + 1]);
}

/* The pairing of the primitives of step scene a with those of step scene b, made once per interval: ab[i] is the primitive of b that a's i-th moves towards (-1: none), ba[j] the inverse.
 * Same key, the k-th of its key in one step with the best unused among the k-th +- STAGE2_WINDOW of the other, refused when a vertex would move more than STAGE2_MAX_MOVE game pixels. */
static int stage2_pair_run(int *ab, int *ba, const remaster_frame_copy *a, const remaster_frame_copy *b, int parallel, double *t_keys, double *t_sort, double *t_match)
{
    stage2_key *fk = (stage2_key *)malloc(b->n * sizeof *fk), *nk = (stage2_key *)malloc(a->n * sizeof *nk);
    if (!fk || !nk) { free(fk); free(nk); return 0; }
    pair_job job; memset(&job, 0, sizeof job);
    job.a = a; job.b = b; job.nk = nk; job.fk = fk; job.ab = ab; job.ba = ba;
    const int par = parallel && g_s2_prun && g_s2_pparts >= 2 && a->n >= 2048 && b->n >= 2048;
    job.parts = par ? (g_s2_pparts > 8 ? 8 : g_s2_pparts) & ~1 : 1;       /* an even number of jobs (the sort splits them between the two arrays) */
    if (job.parts < 2) job.parts = 1;
    const double t0 = now_ms();
    if (job.parts > 1) g_s2_prun(pair_keys_part, &job, job.parts); else pair_keys_part(&job, 0);
    const double t1 = now_ms();
    if (job.parts > 1) {
        g_s2_prun(pair_sort_part, &job, job.parts);
        if (!pair_merge_chunks(nk, a->n, job.parts / 2) || !pair_merge_chunks(fk, b->n, job.parts / 2)) { free(fk); free(nk); return 0; }
    } else {
        qsort(fk, b->n, sizeof *fk, stage2_cmp);
        qsort(nk, a->n, sizeof *nk, stage2_cmp);
    }
    const double t2 = now_ms();
    if (job.parts > 1) {
        job.start[0] = 0;
        for (int c = 1; c < job.parts; c++) {
            size_t s = a->n * (size_t)c / (size_t)job.parts;
            if (s < job.start[c - 1]) s = job.start[c - 1];
            while (s > 0 && s < a->n && nk[s].key == nk[s - 1].key) s++;       /* on a group boundary */
            job.start[c] = s;
        }
        job.start[job.parts] = a->n;
        g_s2_prun(pair_match_part, &job, job.parts);
    } else pair_match_range(a, b, nk, fk, ab, ba, 0, a->n);
    const double t3 = now_ms();
    free(fk); free(nk);
    *t_keys += t1 - t0; *t_sort += t2 - t1; *t_match += t3 - t2;
    return 1;
}

/* ---- Stage 2 reprojection (F117_STAGE2_REPROJECT=1, R45) ----------------------------------------------------------------------------------------------------
 * The game's view matrix at DS:7B28 (element [3 world + axis], 16384 = 1) is M = R diag(1, ys, 1), R = diag(1, -1, -1) Ry(heading) Rx(pitch) Rz(roll) with the angles at
 * DS:2DEE..2DF2 (65536 a turn) and ys about 0.75 (fitted on 714 matrices of the owner's runway bank, worst element 0.0009), and a camera-space vertex is v = M^T w.
 * The rerun copy at t moves each angle and the position linearly (rerun.c nudge). So a primitive that does not move in the world is drawn at t by taking it back to
 * world directions with its own step's camera, w = R S^-1 v, moving it by the camera's travel (t of the way), and forward with the camera at t, v' = S_t R_t^T w:
 * no partner needed. The travel per depth scale is the median of w_a - w_b over the paired primitives (wrong partners do not move a median). A paired primitive whose
 * own w_a - w_b disagrees with it moves in the world (an aircraft, a weapon): its world places are blended. HUD and flat overlays keep the screen blend. */
typedef struct { double R[3][3], s[3]; } s2_cam;
static int g_s2_reproj = -1;
static void s2_mat_rot(int axis, double a, double o[3][3])
{
    const double c = cos(a), sn = sin(a);
    memset(o, 0, 9 * sizeof(double));
    if (axis == 0) { o[0][0] = 1; o[1][1] = c; o[1][2] = -sn; o[2][1] = sn; o[2][2] = c; }
    else if (axis == 1) { o[0][0] = c; o[0][2] = sn; o[1][1] = 1; o[2][0] = -sn; o[2][2] = c; }
    else { o[0][0] = c; o[0][1] = -sn; o[1][0] = sn; o[1][1] = c; o[2][2] = 1; }
}
static void s2_mat_mul(const double x[3][3], const double y[3][3], double o[3][3])
{
    double r[3][3];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r[i][j] = x[i][0] * y[0][j] + x[i][1] * y[1][j] + x[i][2] * y[2][j];
    memcpy(o, r, sizeof r);
}
static double s2_ycol(const int16_t c[12])
{
    return sqrt((double)c[1] * c[1] + (double)c[4] * c[4] + (double)c[7] * c[7]) / 16384.0;
}
/* The camera of copy a moved t of the way to copy b's, as the rerun moves the angles (each by its own signed difference). */
static void s2_cam_at(const int16_t a[12], const int16_t b[12], double t, s2_cam *o)
{
    double ang[3];
    for (int k = 0; k < 3; k++) {
        const int16_t d = (int16_t)(uint16_t)((uint16_t)b[9 + k] - (uint16_t)a[9 + k]);
        const double x = d * t;                       /* as rerun.c lerp_i moves the copy's angles: whole units, halves away from zero */
        ang[k] = ((double)(uint16_t)a[9 + k] + (double)(int32_t)(x < 0 ? x - 0.5 : x + 0.5)) * (6.283185307179586 / 65536.0);
    }
    double ry[3][3], rx[3][3], rz[3][3], m[3][3];
    s2_mat_rot(1, ang[0], ry); s2_mat_rot(0, ang[1], rx); s2_mat_rot(2, ang[2], rz);
    s2_mat_mul(ry, rx, m); s2_mat_mul(m, rz, m);
    for (int j = 0; j < 3; j++) { o->R[0][j] = m[0][j]; o->R[1][j] = -m[1][j]; o->R[2][j] = -m[2][j]; }
    o->s[0] = 1.0; o->s[2] = 1.0;
    o->s[1] = s2_ycol(a) + t * (s2_ycol(b) - s2_ycol(a));
}
static void s2_to_world(const s2_cam *c, const remaster_v3 *v, double w[3])
{
    const double u[3] = { v->x / c->s[0], v->y / c->s[1], v->z / c->s[2] };
    for (int i = 0; i < 3; i++) w[i] = c->R[i][0] * u[0] + c->R[i][1] * u[1] + c->R[i][2] * u[2];
}
static int32_t s2_clamp32(double x) { return x > 2147483000.0 ? 2147483000 : x < -2147483000.0 ? -2147483000 : (int32_t)llround(x); }
static void s2_to_view(const s2_cam *c, const double w[3], remaster_v3 *v)
{
    v->x = s2_clamp32(c->s[0] * (c->R[0][0] * w[0] + c->R[1][0] * w[1] + c->R[2][0] * w[2]));
    v->y = s2_clamp32(c->s[1] * (c->R[0][1] * w[0] + c->R[1][1] * w[1] + c->R[2][1] * w[2]));
    v->z = s2_clamp32(c->s[2] * (c->R[0][2] * w[0] + c->R[1][2] * w[1] + c->R[2][2] * w[2]));
}
static int s2_reprojectable(const remaster_frame_copy *f, size_t i)
{
    const remaster_prim *p = &f->prim[i];
    if ((f->hud_at >= 0 && (long)i >= f->hud_at) || p->owner >= 0xFFFFF000u || p->n < 1) return 0;
    /* a primitive with a vertex near or behind the near plane keeps the screen blend: the game clipped it at its own step's near plane, and that
     * edge moved with another camera opens a gap at the picture's edge (the ground in the left margin went blue in the owner's bank, 20 Nov) */
    static double near_limit = -1;
    if (near_limit < 0) { const char *e = getenv("F117_STAGE2_REPROJECT_NEAR"); near_limit = e ? atof(e) : 4.0; }
    for (int v = 0; v < p->n; v++) if (p->v[v].z < near_limit * 65536.0) return 0;
    return 1;
}
static int s2_cls_find(const remaster_stage2_pairs *pr, double scale)
{
    for (int k = 0; k < pr->ncls; k++) if (pr->cls[k] == scale) return k;
    return -1;
}
static int s2_dcmp(const void *x, const void *y) { const double a = *(const double *)x, b = *(const double *)y; return a < b ? -1 : a > b; }
/* The camera's travel a to b per depth scale: the median of w_a - w_b over paired, reprojectable primitives in front of both cameras. */
static void s2_travel(remaster_stage2_pairs *pr, const remaster_frame_copy *a, const remaster_frame_copy *b)
{
    pr->have_delta = 0; pr->ncls = 0;
    if (!a->have_cam || !b->have_cam) return;
    s2_cam ca, cb;
    s2_cam_at(a->cam, a->cam, 0.0, &ca); s2_cam_at(b->cam, b->cam, 0.0, &cb);
    double *d = (double *)malloc(a->n * 3 * sizeof *d);
    unsigned char *k = (unsigned char *)malloc(a->n);
    if (!d || !k) { free(d); free(k); return; }
    size_t n = 0;
    const size_t stride = a->n > 4096 ? a->n / 4096 : 1;     /* a median needs a few thousand samples, not all (the pairing thread has a deadline) */
    for (size_t i = 0; i < a->n; i += stride) {
        const int j = pr->ab[i];
        if (j < 0 || !s2_reprojectable(a, i) || !s2_reprojectable(b, (size_t)j)) continue;
        const remaster_prim *p = &a->prim[i], *q = &b->prim[j];
        if (p->v[0].z <= 65536 || q->v[0].z <= 65536) continue;
        const double sc = p->inv_depth_scale > 0 ? p->inv_depth_scale : 1.0;
        int c = s2_cls_find(pr, sc);
        if (c < 0) { if (pr->ncls >= 8) continue; c = pr->ncls++; pr->cls[c] = sc; }
        double wa[3], wb[3];
        s2_to_world(&ca, &p->v[0], wa); s2_to_world(&cb, &q->v[0], wb);
        for (int x = 0; x < 3; x++) d[3 * n + x] = wa[x] - wb[x];
        k[n++] = (unsigned char)c;
    }
    double *col = (double *)malloc((n ? n : 1) * sizeof *col);
    int good = 0;
    for (int c = 0; c < pr->ncls && col; c++) {
        size_t m = 0;
        for (int x = 0; x < 3; x++) {
            m = 0;
            for (size_t i = 0; i < n; i++) if (k[i] == c) col[m++] = d[3 * i + x];
            if (m < 8) break;
            qsort(col, m, sizeof *col, s2_dcmp);
            pr->delta[c][x] = col[m / 2];
        }
        if (m < 8) pr->cls[c] = -1.0; else good++;      /* too few to trust: that scale keeps the screen blend */
    }
    pr->have_delta = good > 0;
    free(col); free(d); free(k);
}

int remaster_stage2_pair(remaster_stage2_pairs *pr, const remaster_frame_copy *a, const remaster_frame_copy *b)
{
    /* Experimental: keep repeated lights on their own mesh edges rather than the nearest identical-looking edge. Read before helpers start. */
    static int mesh_key_init;
    if (!mesh_key_init) { const char *e = getenv("F117_STAGE2_MESH_KEY"); g_stage2_mesh_key = e && atoi(e) != 0; mesh_key_init = 1; }
    if (!pr || !a || !b || !a->n || !b->n) return 0;
    if (pr->cap_a < a->n) { int *q = (int *)realloc(pr->ab, a->n * sizeof *q); if (!q) return 0; pr->ab = q; pr->cap_a = a->n; }
    if (pr->cap_b < b->n) { int *q = (int *)realloc(pr->ba, b->n * sizeof *q); if (!q) return 0; pr->ba = q; pr->cap_b = b->n; }
    pr->na = a->n; pr->nb = b->n;
    static int mode = -1;                           /* 0 parallel, 1 serial, 2 parallel and checked against serial */
    if (mode < 0) mode = getenv("F117_STAGE2_PAIR_SERIAL") ? 1 : getenv("F117_STAGE2_PAIR_CHECK") ? 2 : 0;
    double tk = 0, ts = 0, tm = 0;
    if (!stage2_pair_run(pr->ab, pr->ba, a, b, mode != 1, &tk, &ts, &tm)) return 0;
    if (mode == 2) {
        static unsigned n, bad;
        int *ab2 = (int *)malloc(a->n * sizeof *ab2), *ba2 = (int *)malloc(b->n * sizeof *ba2);
        double x = 0, y = 0, z = 0;
        if (ab2 && ba2 && stage2_pair_run(ab2, ba2, a, b, 0, &x, &y, &z)) {
            const int same = !memcmp(ab2, pr->ab, a->n * sizeof *ab2) && !memcmp(ba2, pr->ba, b->n * sizeof *ba2);
            n++; if (!same) bad++;
            if (!same && bad < 4) fprintf(stderr, "[pair-check] the parallel pairing differs from the serial one (pairing %u, %zu and %zu primitives)\n", n, a->n, b->n);
            if (n % 20 == 0) fprintf(stderr, "[pair-check] %u pairings checked, %u differ\n", n, bad);
        }
        free(ab2); free(ba2);
    }
    {   /* F117_STAGE2_PAIR_TIMING (diagnostic): the parts of a pairing, mean of each 20 */
        static int on = -1; static double sk, ss, sm; static unsigned n;
        if (on < 0) on = getenv("F117_STAGE2_PAIR_TIMING") != NULL;
        if (on) { sk += tk; ss += ts; sm += tm; if (++n % 20 == 0) fprintf(stderr, "[pair-timing] mean of %u: keys %.2f ms, sorts %.2f, matching %.2f (%zu and %zu primitives)\n", n, sk / n, ss / n, sm / n, a->n, b->n); }
    }
    if (g_s2_reproj < 0) { const char *e = getenv("F117_STAGE2_REPROJECT"); g_s2_reproj = e && atoi(e) != 0; }
    if (g_s2_reproj) s2_travel(pr, a, b); else pr->have_delta = 0;
    return 1;
}

typedef struct { const remaster_frame_copy *nr, *fr; const int *match; double wf; remaster_prim *out; int parts;
    int reproj, near_is_a; double off; s2_cam cn, cf, ct; const remaster_stage2_pairs *pr; } stage2_blend_job;
static void (*g_s2_run)(void (*)(void *, int), void *, int);
static int g_s2_parts;
void remaster_live_set_stage2_parallel(void (*run)(void (*)(void *, int), void *, int), int parts) { g_s2_run = run; g_s2_parts = parts; }
/* The blend runs on the display loop's thread while the scene copy (native batch, commit) may run on the copy thread: each needs a pool of its own. Same parts as the stage 2 pool. */
static void (*g_blend_run)(void (*)(void *, int), void *, int);
void remaster_live_set_blend_parallel(void (*run)(void (*)(void *, int), void *, int)) { g_blend_run = run; }

/* ---- The far scenery's native placements on helper threads (F117_FAR_PAR). remaster_live_native places one object at a time: it patches the pose into the machine's memory,
 * lets the scene reader build the primitives, and restores the memory. Here the same steps run on private copies: each helper has its own scene (mesh cache), its own copy of
 * the pose segment (taken once per batch; the template patch and its restore touch only that copy) and its own frame, for a contiguous range of the picks. The merge (commit)
 * is in pick order on the calling thread and gives every object the number the serial path would have: the scene's next number. A pick the helper cannot make exactly as
 * native would (no template yet, a stale one, the scene reader refusing) is left to the serial path, which also keeps its counters. */
typedef struct { uint32_t first, n; uint8_t part, took, incomplete; } nat_rec;     /* took: 0 left to the serial path, 1 out of range (nothing drawn, no number used), 2 drawn */
typedef struct { remaster_scene *scene; remaster_frame frame; uint8_t *ds; ring_correction corr[4]; } nat_worker;
static nat_worker g_nw[8];
static nat_rec *g_nrec; static size_t g_nrec_cap;
/* decline reasons (diagnostic): 0 bank range, 1 no template, 2 template parts<0, 3 matrix differs, 4 light differs, 5 refused */
unsigned g_decl[6];
typedef struct { remaster_live *r; const machine_t *m; const rerun_native *spec; const unsigned char *elig; size_t n; int parts; int refresh_matrix, range_cull; size_t ds_at; } nat_job;

static uint16_t nld16(const uint8_t *ds, unsigned off) { return (uint16_t)(ds[off] | ds[off + 1] << 8); }
static void nst16(uint8_t *ds, unsigned off, uint16_t v) { ds[off] = (uint8_t)v; ds[off + 1] = (uint8_t)(v >> 8); }

static void nat_part(void *vj, int part)
{
    nat_job *j = (nat_job *)vj;
    nat_worker *w = &g_nw[part];
    const remaster_live *r = j->r;
    const machine_t *m = j->m;
    const size_t k0 = j->n * (size_t)part / (size_t)j->parts, k1 = j->n * (size_t)(part + 1) / (size_t)j->parts;
    remaster_frame_clear(&w->frame);
    memcpy(w->ds, m->mem + j->ds_at, SCENE_SEGMENT);
    for (int i = 0; i < 4; i++) w->corr[i].valid = 0;
    uint8_t *dsp = w->ds + TEMPLATE_AT;
    for (size_t k = k0; k < k1; k++) {
        nat_rec *rc = &g_nrec[k];
        rc->took = 0; rc->part = (uint8_t)part; rc->first = (uint32_t)w->frame.n; rc->n = 0; rc->incomplete = 0;
        if (!j->elig[k]) continue;
        const rerun_native *n = &j->spec[k];
        if (((size_t)n->bank << 4) + SCENE_SEGMENT > 0x100000) { g_decl[0]++; continue; }
        unsigned t = 0;
        while (t < r->ntmpl && !(r->tmpl[t].bank == n->bank && r->tmpl[t].model == n->model)) t++;
        if (t == r->ntmpl) { g_decl[1]++; continue; }
        if (r->tmpl[t].parts < 0) { g_decl[2]++; continue; }
        const int matrix_same = !memcmp(dsp, r->tmpl[t].bytes, 18);
        const int light_same = !memcmp(dsp + (0x7C9C - TEMPLATE_AT), r->tmpl[t].bytes + (0x7C9C - TEMPLATE_AT), 6);
        const int refresh = j->refresh_matrix && !matrix_same && r->tmpl[t].parts == 1;
        if ((!matrix_same && !refresh) || !light_same) { g_decl[3 + (!light_same)]++; continue; }
        uint32_t translation[3] = { 0, 0, 0 };
        for (int kk = 0; kk < 3; kk++)
            for (int row = 0; row < 3; row++)
                translation[kk] += (uint32_t)((int32_t)n->pos[row] * (int16_t)nld16(w->ds, (unsigned)(TEMPLATE_AT + 2 * (3 * row + kk))));
        const uint8_t *pose = r->tmpl[t].bytes;
        if (j->range_cull && !pose[0x7D8E - TEMPLATE_AT]) {
            const uint16_t size = pose[0x7D5E - TEMPLATE_AT] | (uint16_t)pose[0x7D5F - TEMPLATE_AT] << 8;
            if (model_range(m, n->bank, n->model, translation, size) == 0) { rc->took = 1; continue; }
        }
        uint8_t saved[TEMPLATE_LEN];
        const uint8_t keep48c4 = w->ds[0x48C4];
        memcpy(saved, dsp, TEMPLATE_LEN);
        memcpy(dsp, r->tmpl[t].bytes, TEMPLATE_LEN);
        if (refresh) {
            memcpy(dsp, saved, 18);
            for (int row = 0; row < 3; row++)
                for (int col = 0; col < 3; col++) {
                    uint32_t sum = 0;
                    for (int kk = 0; kk < 3; kk++) {
                        const int16_t orientation = (int16_t)nld16(w->ds, (unsigned)(0x7B5E + 6 * kk + 2 * row));
                        const int16_t camera = (int16_t)nld16(w->ds, (unsigned)(TEMPLATE_AT + 6 * kk + 2 * col));
                        sum += (uint32_t)((int32_t)orientation * camera);
                    }
                    nst16(w->ds, (unsigned)(0x7B4C + 6 * row + 2 * col), (uint16_t)(sum >> 15));
                }
        }
        for (int i = 0; i < 3; i++) nst16(w->ds, (unsigned)(0x7CCC + 2 * i), (uint16_t)n->pos[i]);
        {
            uint16_t eye[3];
            for (int i = 0; i < 3; i++) eye[i] = (uint16_t)(0 - (uint16_t)n->pos[i]);
            if (r->tmpl[t].parts == 1) {
                uint16_t out[3];
                for (int kk = 0; kk < 3; kk++) {
                    uint16_t sum = 0;
                    for (int row = 0; row < 3; row++)
                        sum = (uint16_t)(sum + (uint16_t)(((int32_t)(int16_t)eye[row] * (int16_t)nld16(w->ds, (unsigned)(0x7B5E + 6 * row + 2 * kk))) >> 16));
                    out[kk] = (uint16_t)(sum << 1);
                }
                for (int i = 0; i < 3; i++) eye[i] = out[i];
            }
            for (int i = 0; i < 3; i++) nst16(w->ds, (unsigned)(0x7C6C + 2 * i), eye[i]);
        }
        for (int kk = 0; kk < 3; kk++) {
            const uint32_t acc = translation[kk];
            nst16(w->ds, (unsigned)(0x7CD2 + 4 * kk), (uint16_t)acc);
            nst16(w->ds, (unsigned)(0x7CD4 + 4 * kk), (uint16_t)(acc >> 16));
            if (r->tmpl[t].parts == 1) {
                nst16(w->ds, (unsigned)(0x7CDE + 4 * kk), (uint16_t)acc);
                nst16(w->ds, (unsigned)(0x7CE0 + 4 * kk), (uint16_t)(acc >> 16));
            }
        }
        w->ds[0x48C4] = n->darken;
        const size_t first = w->frame.n;
        unsigned dropped[3];
        memcpy(dropped, remaster_scene_dropped, sizeof dropped);
        remaster_scene_lod_shift((unsigned)n->shrink);
        const int added = remaster_scene_add_at(w->scene, w->ds, m->mem + ((size_t)n->bank << 4), n->model, n->pos, &w->frame);
        if (added < 0) {                                  /* refused: the serial path counts it and lets the game place it */
            w->frame.n = first; g_decl[5]++;
            memcpy(dsp, saved, TEMPLATE_LEN); w->ds[0x48C4] = keep48c4;
            continue;
        }
        const int incomplete = memcmp(dropped, remaster_scene_dropped, sizeof dropped) != 0;
        if (n->shrink / 2 > g_dot_levels) {
            size_t wr = first;
            for (size_t i = first; i < w->frame.n; i++) {
                const remaster_prim *q = &w->frame.prim[i];
                uint32_t c;
                int dark = 0;
                if (q->kind == SCENE_DOTS || (q->kind == SCENE_LINES && q->n == 2 && !memcmp(&q->v[0], &q->v[1], sizeof q->v[0])))
                    dark = word_colour(m, q->colour_word, q->darken, &c) && (c >> 16 & 255) < 80 && (c >> 8 & 255) < 80 && (c & 255) < 80;
                if (!dark) w->frame.prim[wr++] = w->frame.prim[i];
            }
            w->frame.n = wr;
        }
        if (w->frame.n > first) {
            ring_correction local = {0};
            ring_correction *cached = n->level >= 1 && n->level <= 4 ? &w->corr[n->level - 1] : &local;
            if (!cached->valid || memcmp(cached->eye, n->eye, sizeof cached->eye) || memcmp(cached->matrix, dsp, sizeof cached->matrix)) {
                double offset[3];
                remaster_ring_camera_offset(n->eye, (unsigned)n->level, offset);
                model_camera_correction((machine_t *)m, offset, cached->value);
                memcpy(cached->eye, n->eye, sizeof cached->eye);
                memcpy(cached->matrix, dsp, sizeof cached->matrix);
                cached->valid = 1;
            }
            finish_model(w->scene, &w->frame, first, n->shrink, ldexp(1.0, 2 * (4 - n->level)), cached->value);
        }
        memcpy(dsp, saved, TEMPLATE_LEN); w->ds[0x48C4] = keep48c4;
        rc->first = (uint32_t)first; rc->n = (uint32_t)(w->frame.n - first); rc->incomplete = (uint8_t)incomplete;
        rc->took = 2;
    }
}

int remaster_live_native_batch(remaster_live *r, machine_t *m, const void *vspec, const unsigned char *elig, size_t n, unsigned char *took)
{
    static int off = -1;
    if (off < 0) off = getenv("F117_NATIVE_CHECK") || getenv("F117_NATIVE_TIMING") || getenv("F117_NATIVE_SIZES") || getenv("F117_FAR_WHY") || getenv("F117_FAR_DOTS_OFF");
    if (off || !g_s2_run || g_s2_parts < 2) return 0;
    const uint16_t ds = m->cpu.seg[S_DS];
    const size_t ds_at = (size_t)ds << 4;
    if (ds_at + SCENE_SEGMENT > 0x100000) return 0;
    if (g_dot_levels < 0) { const char *e = getenv("F117_FAR_DARK_DOTS"); g_dot_levels = e ? (e[0] == '1' ? 3 : 0) : 1; }
    static int refresh_matrix = -1, range_cull = -1;
    if (refresh_matrix < 0) refresh_matrix = env_default_on("F117_NATIVE_REFRESH_PART");
    if (range_cull < 0) range_cull = env_default_on("F117_NATIVE_RANGE_CULL");
    if (n > g_nrec_cap) {
        nat_rec *q = (nat_rec *)realloc(g_nrec, (n + n / 2) * sizeof *q);
        if (!q) return 0;
        g_nrec = q; g_nrec_cap = n + n / 2;
    }
    const int parts = g_s2_parts > 8 ? 8 : g_s2_parts;
    for (int i = 0; i < parts; i++) {
        if (!g_nw[i].scene) g_nw[i].scene = remaster_scene_new();
        if (!g_nw[i].ds) g_nw[i].ds = (uint8_t *)malloc(SCENE_SEGMENT);
        if (!g_nw[i].scene || !g_nw[i].ds) return 0;
    }
    nat_job job = { r, m, (const rerun_native *)vspec, elig, n, parts, refresh_matrix, range_cull, ds_at };
    g_s2_run(nat_part, &job, parts);
    for (size_t k = 0; k < n; k++) took[k] = g_nrec[k].took != 0;
    return 1;
}

void remaster_live_native_commit(remaster_live *r, machine_t *m, size_t k)
{
    (void)m;
    const nat_rec *rc = &g_nrec[k];
    if (rc->took != 2) return;                             /* out of range: the serial path draws nothing and uses no number */
    const uint32_t serial = remaster_scene_next_serial(r->scene);
    remaster_frame *f = &r->frame;
    if (f->n + rc->n > f->cap) {
        size_t cap = f->cap ? f->cap : 256;
        while (cap < f->n + rc->n) cap *= 2;
        remaster_prim *p = (remaster_prim *)realloc(f->prim, cap * sizeof *p);
        if (!p) return;
        f->prim = p; f->cap = cap;
    }
    const remaster_prim *src = g_nw[rc->part].frame.prim + rc->first;
    for (uint32_t i = 0; i < rc->n; i++) {
        remaster_prim *d = &f->prim[f->n + i];
        *d = src[i];
        d->owner = serial << 12 | (src[i].owner & 0xFFFu);
        d->source = 1;
    }
    f->n += rc->n;
    if (rc->incomplete) r->frame_complete = 0;
    r->added++; r->native_drawn++;
    r->src_native_prims += rc->n; r->src_native_objs++;
}
/* The commit of a run of picks at once: the serial numbers and the destination offsets are taken in pick order (a prefix sum over what each pick drew), then the primitives are copied
 * into their places on the helper threads (disjoint ranges of the frame), so the frame is byte for byte what one commit after another makes. */
typedef struct { remaster_prim *dst; const uint32_t *k; const uint32_t *serial; const size_t *off; size_t n; int parts; } commit_job;
static void commit_part(void *vj, int part)
{
    commit_job *j = (commit_job *)vj;
    const size_t from = j->n * (size_t)part / (size_t)j->parts, to = j->n * (size_t)(part + 1) / (size_t)j->parts;
    for (size_t q = from; q < to; q++) {
        const nat_rec *rc = &g_nrec[j->k[q]];
        const remaster_prim *src = g_nw[rc->part].frame.prim + rc->first;
        remaster_prim *d = j->dst + j->off[q];
        for (uint32_t i = 0; i < rc->n; i++) {
            d[i] = src[i];
            d[i].owner = j->serial[q] << 12 | (src[i].owner & 0xFFFu);
            d[i].source = 1;
        }
    }
}

void remaster_live_native_commit_range(remaster_live *r, machine_t *m, size_t k0, size_t k1)
{
    (void)m;
    static uint32_t *ks, *serials; static size_t *offs; static size_t cap;
    if (k1 <= k0) return;
    const size_t span = k1 - k0;
    if (span > cap) {
        free(ks); free(serials); free(offs);
        cap = span + span / 2;
        ks = (uint32_t *)malloc(cap * sizeof *ks); serials = (uint32_t *)malloc(cap * sizeof *serials); offs = (size_t *)malloc(cap * sizeof *offs);
        if (!ks || !serials || !offs) { free(ks); free(serials); free(offs); ks = NULL; serials = NULL; offs = NULL; cap = 0; for (size_t k = k0; k < k1; k++) remaster_live_native_commit(r, m, k); return; }
    }
    remaster_frame *f = &r->frame;
    size_t n = 0, total = 0; int incomplete = 0;
    for (size_t k = k0; k < k1; k++) {
        const nat_rec *rc = &g_nrec[k];
        if (rc->took != 2) continue;
        ks[n] = (uint32_t)k; serials[n] = remaster_scene_next_serial(r->scene); offs[n] = f->n + total;
        total += rc->n; if (rc->incomplete) incomplete = 1;
        n++;
    }
    if (f->n + total > f->cap) {
        size_t c = f->cap ? f->cap : 256;
        while (c < f->n + total) c *= 2;
        remaster_prim *p = (remaster_prim *)realloc(f->prim, c * sizeof *p);
        if (!p) return;
        f->prim = p; f->cap = c;
    }
    commit_job job = { f->prim, ks, serials, offs, n, 1 };
    if (g_s2_run && g_s2_parts > 1 && total >= 8192) { job.parts = g_s2_parts > 8 ? 8 : g_s2_parts; g_s2_run(commit_part, &job, job.parts); }
    else commit_part(&job, 0);
    f->n += total;
    if (incomplete) r->frame_complete = 0;
    r->added += (unsigned)n; r->native_drawn += (unsigned)n;
    r->src_native_prims += total; r->src_native_objs += n;
}

static void stage2_blend_part(void *arg, int part)
{
    stage2_blend_job *j = (stage2_blend_job *)arg;
    const int parts = j->parts > 0 ? j->parts : g_s2_parts;
    const size_t n = j->nr->n, from = n * (size_t)part / (size_t)parts, to = n * (size_t)(part + 1) / (size_t)parts;
    static int freeze_lights = -1;     /* F117_STAGE2_FREEZE_LIGHTS=1 (experiment): lines and dots (the lights) keep the nearer step's own place, no blend */
    if (freeze_lights < 0) freeze_lights = getenv("F117_STAGE2_FREEZE_LIGHTS") != NULL;
    for (size_t i = from; i < to; i++) {
        remaster_prim p = j->nr->prim[i];
        if (j->reproj && s2_reprojectable(j->nr, i)) {          /* reprojection (see s2_cam): static scenery by the camera, moving things by their world places */
            const int c = s2_cls_find(j->pr, p.inv_depth_scale > 0 ? p.inv_depth_scale : 1.0);
            if (c >= 0) {
                const double *dl = j->pr->delta[c];
                const remaster_prim *q = j->match[i] >= 0 ? &j->fr->prim[j->match[i]] : NULL;
                int moving = 0;
                if (q && q->n == p.n && s2_reprojectable(j->fr, (size_t)j->match[i]) && p.v[0].z > 65536 && q->v[0].z > 65536) {
                    double wn[3], wo[3], r2 = 0, w2 = 0;
                    s2_to_world(&j->cn, &p.v[0], wn); s2_to_world(&j->cf, &q->v[0], wo);
                    for (int x = 0; x < 3; x++) { const double own = j->near_is_a ? wn[x] - wo[x] : wo[x] - wn[x]; r2 += (own - dl[x]) * (own - dl[x]); w2 += wn[x] * wn[x]; }
                    const double tol = 16.0 + 0.004 * sqrt(w2);
                    moving = r2 > tol * tol && r2 < 262144.0 * 262144.0;   /* a quarter of a terrain cell (4096 units, 2^20 here) or more off the travel is a partner in the wrong cell (repeated lights), not motion */
                }
                for (int v = 0; v < p.n; v++) {
                    double w[3];
                    s2_to_world(&j->cn, &j->nr->prim[i].v[v], w);
                    if (moving) { double wo[3]; s2_to_world(&j->cf, &q->v[v], wo); for (int x = 0; x < 3; x++) w[x] += j->wf * (wo[x] - w[x]); }
                    else for (int x = 0; x < 3; x++) w[x] += j->off * dl[x];
                    s2_to_view(&j->ct, w, &p.v[v]);
                }
                j->out[i] = p;
                continue;
            }
        }
        if (j->match[i] >= 0 && !(freeze_lights && p.kind != SCENE_POLYGON)) {
            const remaster_prim *q = &j->fr->prim[j->match[i]];
            /* A light (a line or dot) whose first vertex moves more than F117_STAGE2_LIGHT_MAX game pixels (default 1.5) between the two steps stays where the nearer step has it: such a move is
             * a wrong partner or a near object, and blending it made the coast's lights flicker (a third more flipping pixels than the original path); the small, steady moves of far lights are blended. */
            static double light_max = -1.0;
            if (light_max < 0.0) { const char *e = getenv("F117_STAGE2_LIGHT_MAX"); light_max = e && *e ? atof(e) : 1.5; }
            if (!g_stage2_mesh_key && p.kind != SCENE_POLYGON && p.n >= 1 && q->n >= 1 && light_max < 1e6) {
                const double zr = p.v[0].z > 0 ? (double)p.v[0].z : 1.0;
                const double mx = fabs((double)q->v[0].x - p.v[0].x), my = fabs((double)q->v[0].y - p.v[0].y);
                if ((mx > my ? mx : my) * 256.0 > light_max * zr) { j->out[i] = p; continue; }
            }
            for (int v = 0; v < p.n && v < q->n; v++) {
                p.v[v].x += (int32_t)floor(j->wf * ((double)q->v[v].x - p.v[v].x) + 0.5);
                p.v[v].y += (int32_t)floor(j->wf * ((double)q->v[v].y - p.v[v].y) + 0.5);
                p.v[v].z += (int32_t)floor(j->wf * ((double)q->v[v].z - p.v[v].z) + 0.5);
            }
        }
        j->out[i] = p;
    }
}

/* Fills a blend job's reprojection fields (off when the switch is off, a copy has no camera, or the pairing found no travel). */
static void s2_job_reproj(stage2_blend_job *j, const remaster_frame_copy *a, const remaster_frame_copy *b, const remaster_stage2_pairs *pr, double t, int nb)
{
    if (g_s2_reproj < 0) { const char *e = getenv("F117_STAGE2_REPROJECT"); g_s2_reproj = e && atoi(e) != 0; }
    j->reproj = g_s2_reproj && pr->have_delta && a->have_cam && b->have_cam;
    if (!j->reproj) return;
    j->pr = pr; j->near_is_a = !nb; j->off = nb ? 1.0 - t : -t;
    s2_cam ca, cb;
    s2_cam_at(a->cam, a->cam, 0.0, &ca); s2_cam_at(b->cam, b->cam, 0.0, &cb); s2_cam_at(a->cam, b->cam, t, &j->ct);
    j->cn = nb ? cb : ca; j->cf = nb ? ca : cb;
}
static int s2_u64_cmp(const void *x, const void *y) { const uint64_t a = *(const uint64_t *)x, b = *(const uint64_t *)y; return a < b ? -1 : a > b; }
int remaster_live_stage2_blend(remaster_live *r, const remaster_frame_copy *a, const remaster_frame_copy *b, const remaster_stage2_pairs *pr, double t)
{
    if (!r || !r->deferred || !a || !b || !pr || !a->n || !b->n || pr->na != a->n || pr->nb != b->n) return 0;
    const int nb = t >= 0.5;
    const remaster_frame_copy *nr = nb ? b : a, *fr = nb ? a : b;
    const int *match = nb ? pr->ba : pr->ab;
    const double wf = nb ? 1.0 - t : t;                       /* how far the nearer step's vertices move towards the other step */
    remaster_frame *o = &r->staged;
    if (o->cap < nr->n) {
        remaster_prim *q = (remaster_prim *)realloc(o->prim, nr->n * sizeof *q);
        if (!q) return 0;
        o->prim = q; o->cap = nr->n;
    }
    {   /* the blend of each primitive is independent of the others: on the host's helper threads when it gave them */
        stage2_blend_job job = { nr, fr, match, wf, o->prim, 0 };
        s2_job_reproj(&job, a, b, pr, t, nb);
        if ((g_blend_run ? g_blend_run : g_s2_run) && g_s2_parts > 1 && nr->n >= 2048) (g_blend_run ? g_blend_run : g_s2_run)(stage2_blend_part, &job, g_s2_parts);
        else for (int part = 0; part < 1; part++) { job.parts = 1; stage2_blend_part(&job, part); }
    }
    o->n = nr->n;
    {   /* F117_STAGE2_PAIRSTAT=1 (diagnostic): per blended picture, the nearer step's primitives by kind: paired, unpaired with no primitive of the same key in the other step
         * (vertex count, colour, face or mesh item changed), unpaired although that key exists (refused by the window, the move limit or the depth scale) */
        static int stat = -1;
        if (stat < 0) stat = getenv("F117_STAGE2_PAIRSTAT") != NULL;
        if (stat) {
            uint64_t *keys = (uint64_t *)malloc((fr->n ? fr->n : 1) * sizeof *keys);
            if (keys) {
                for (size_t i = 0; i < fr->n; i++) keys[i] = stage2_pkey(&fr->prim[i]);
                qsort(keys, fr->n, sizeof *keys, s2_u64_cmp);
                unsigned c[3][3] = {{0}};
                for (size_t i = 0; i < nr->n; i++) {
                    const int kd = nr->prim[i].kind >= 0 && nr->prim[i].kind < 3 ? nr->prim[i].kind : 0;
                    if (match[i] >= 0) { c[kd][0]++; continue; }
                    const uint64_t k = stage2_pkey(&nr->prim[i]);
                    size_t lo = 0, hi = fr->n;
                    while (lo < hi) { const size_t mid = lo + (hi - lo) / 2; if (keys[mid] < k) lo = mid + 1; else hi = mid; }
                    c[kd][lo < fr->n && keys[lo] == k ? 2 : 1]++;
                }
                fprintf(stderr, "[s2-pairstat] t %.3f near %c n %zu/%zu poly %u/%u/%u lines %u/%u/%u dots %u/%u/%u\n", t, nb ? 'b' : 'a', nr->n, fr->n,
                        c[0][0], c[0][1], c[0][2], c[1][0], c[1][1], c[1][2], c[2][0], c[2][1], c[2][2]);
                free(keys);
            }
        }
    }
    int32_t tmp[BAND_REC_FINE];
    memset(tmp, 0, sizeof tmp);
    const int bn = a->have_band && b->have_band ? band_blend(a->band, BAND_REC_BASE, b->band, BAND_REC_BASE, t, tmp, BAND_REC_FINE) : 0;
    memset(r->staged_band, 0, sizeof r->staged_band);
    if (bn > 0) memcpy(r->staged_band, tmp, sizeof tmp); else memcpy(r->staged_band, nr->band, sizeof r->staged_band);
    r->staged_have_band = nr->have_band;
    r->staged_complete = nr->complete;
    memcpy(r->staged_origin, nr->origin, sizeof r->staged_origin); memcpy(r->staged_window, nr->window, sizeof r->staged_window);
    r->staged_hud_at = nr->hud_at; r->staged_hud_set = 1;
    r->have_staged = o->n > 0; r->copy_open = 1;
    return 1;
}

/* The greedy, order-preserving pairing of the diagnostic: primitive i of `x` with the first unused primitive of `y` with the same (model, kind, vertex count, colour word); -1 when none. */
static double blend_pos(const remaster_prim *p, int axis)
{
    const double z = p->v[0].z > 65536 ? (double)p->v[0].z : 65536.0;
    return 256.0 * (axis ? (double)p->v[0].y : (double)p->v[0].x) / z;
}

static void pair_by_key(const remaster_frame_copy *x, const remaster_frame_copy *y, int *match)
{
    /* For each primitive of x, the unused primitive of y with the same (model, kind, vertex count, colour word) whose first vertex is nearest on the screen: the first of several identical
     * ones (the windows of one building) is not necessarily the right one. F117_BLEND_FIRST=1 takes the first, as the step-pair diagnostic does. */
    static int first = -1, face_key = -1;
    if (first < 0) first = getenv("F117_BLEND_FIRST") != NULL;
    if (face_key < 0) face_key = getenv("F117_BLEND_NO_FACE") == NULL;
    unsigned char *used = (unsigned char *)calloc(y->n ? y->n : 1, 1);
    for (size_t i = 0; i < x->n; i++) {
        match[i] = -1;
        if (!used) continue;
        double best = 1e300; int bj = -1;
        const double xa = blend_pos(&x->prim[i], 0), ya = blend_pos(&x->prim[i], 1);
        for (size_t j = 0; j < y->n; j++) {
            if (used[j] || y->prim[j].model != x->prim[i].model || y->prim[j].kind != x->prim[i].kind || y->prim[j].n != x->prim[i].n || y->prim[j].colour_word != x->prim[i].colour_word) continue;
            if (face_key && y->prim[j].face != x->prim[i].face) continue;        /* the same face of the same model: F117_BLEND_NO_FACE=1 leaves it out */
            if (first) { bj = (int)j; break; }
            const double dx = blend_pos(&y->prim[j], 0) - xa, dy = blend_pos(&y->prim[j], 1) - ya, d = dx * dx + dy * dy;
            if (d < best) { best = d; bj = (int)j; }
        }
        if (bj >= 0) { used[bj] = 1; match[i] = bj; }
    }
    free(used);
}

void remaster_blend_report(const remaster_frame_copy *a, const remaster_frame_copy *b, const remaster_frame_copy *at, double t)
{
    /* error buckets, in game pixels: below 0.25, 0.5, 1, 2, 4, and 4 or more; for all vertices in front of the camera (all) and those more than 4 near-plane distances out (far) */
    static unsigned calls; static double sum_pair; static unsigned long long hist[2][6]; static unsigned long long tsplit[3][2];   /* vertices and gross errors by t: below 0.15, middle, above 0.85 */ static unsigned long long by_model[1024][3];   /* vertices, 4 or more off, and primitives by model */
    if (!a->n || !b->n || !at->n) return;
    int *ab = (int *)malloc(a->n * sizeof *ab), *aat = (int *)malloc(a->n * sizeof *aat);
    if (!ab || !aat) { free(ab); free(aat); return; }
    pair_by_key(a, b, ab);
    pair_by_key(a, at, aat);
    size_t paired = 0;
    for (size_t i = 0; i < a->n; i++) {
        if (ab[i] < 0 || aat[i] < 0) continue;
        const remaster_prim *pa = &a->prim[i], *pb = &b->prim[ab[i]], *pc = &at->prim[aat[i]];
        paired++;
        for (int v = 0; v < pa->n && v < pb->n && v < pc->n; v++) {
            const double bx = pa->v[v].x + t * ((double)pb->v[v].x - pa->v[v].x), by = pa->v[v].y + t * ((double)pb->v[v].y - pa->v[v].y), bz = pa->v[v].z + t * ((double)pb->v[v].z - pa->v[v].z);
            if (bz < 65536.0 || pc->v[v].z < 65536) continue;
            const double sx = 256.0 * bx / bz, sy = 256.0 * by / bz, cx = 256.0 * pc->v[v].x / pc->v[v].z, cy = 256.0 * pc->v[v].y / pc->v[v].z;
            const double e = sqrt((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy));
            const int k = e < 0.25 ? 0 : e < 0.5 ? 1 : e < 1.0 ? 2 : e < 2.0 ? 3 : e < 4.0 ? 4 : 5;
            hist[0][k]++;
            { const int ti = t < 0.15 ? 0 : t > 0.85 ? 2 : 1; tsplit[ti][0]++; if (k == 5) tsplit[ti][1]++; }
            by_model[pa->model & 1023][0]++; if (k == 5) by_model[pa->model & 1023][1]++;
            if (bz > 4.0 * 65536.0 && pc->v[v].z > 4 * 65536) hist[1][k]++;
        }
    }
    sum_pair += a->n ? (double)paired / (double)a->n : 0.0;
    if (++calls % 30 == 1) {
        for (int g = 0; g < 2; g++) {
            unsigned long long tot = 0; for (int k = 0; k < 6; k++) tot += hist[g][k];
            if (!tot) continue;
            fprintf(stderr, "[blend-check] %u frames, %.1f%% of primitives paired in all three; %s vertices (%llu): error in game pixels < 0.25: %.1f%%, < 0.5: %.1f%%, < 1: %.1f%%, < 2: %.1f%%, < 4: %.1f%%, 4 or more: %.1f%%\n",
                    calls, 100.0 * sum_pair / calls, g ? "far" : "all", tot, 100.0 * (double)hist[g][0] / (double)tot, 100.0 * (double)hist[g][1] / (double)tot, 100.0 * (double)hist[g][2] / (double)tot,
                    100.0 * (double)hist[g][3] / (double)tot, 100.0 * (double)hist[g][4] / (double)tot, 100.0 * (double)hist[g][5] / (double)tot);
        }
    }
    if (calls % 30 == 1)
        fprintf(stderr, "[blend-check] 4 or more game pixels off, by t: below 0.15: %.2f%% of %llu vertices; 0.15 to 0.85: %.2f%% of %llu; above 0.85: %.2f%% of %llu\n",
                tsplit[0][0] ? 100.0 * (double)tsplit[0][1] / (double)tsplit[0][0] : 0.0, tsplit[0][0], tsplit[1][0] ? 100.0 * (double)tsplit[1][1] / (double)tsplit[1][0] : 0.0, tsplit[1][0],
                tsplit[2][0] ? 100.0 * (double)tsplit[2][1] / (double)tsplit[2][0] : 0.0, tsplit[2][0]);
    if (calls % 30 == 1) {                           /* the models with the most vertices off by four pixels or more */
        unsigned top[6] = { 0 }; unsigned long long topn[6] = { 0 };
        for (unsigned m2 = 0; m2 < 1024; m2++)
            for (int q = 0; q < 6; q++)
                if (by_model[m2][1] > topn[q]) { for (int r2 = 5; r2 > q; r2--) { top[r2] = top[r2 - 1]; topn[r2] = topn[r2 - 1]; } top[q] = m2; topn[q] = by_model[m2][1]; break; }
        fprintf(stderr, "[blend-check] models with the most vertices 4 or more game pixels off (of their vertices):");
        for (int q = 0; q < 6; q++) if (topn[q]) fprintf(stderr, " %u: %llu of %llu (%.0f%%);", top[q], topn[q], by_model[top[q]][0], 100.0 * (double)topn[q] / (double)by_model[top[q]][0]);
        fprintf(stderr, "\n");
    }
    free(ab); free(aat);
}

/* F117_BLEND_CHECK with F117_STAGE2_TRUTH=1 (diagnostic, R45): Stage 2's own pairing and blend of the copies at t = 0 and t = 1, judged against the copy at t. For each blended
 * primitive, the distance in game pixels from its first vertex to the nearest primitive of the same key in the copy at t; the same for the nearer step held unblended, for scale.
 * Split by kind (polygons; lines and dots) and by paired or unpaired; a large error with a partner is a wrong partner, a large one without is a snap. */
static double s2t_dist(const remaster_prim *p, const remaster_prim *q)
{
    const double px = blend_pos(p, 0), py = blend_pos(p, 1), qx = blend_pos(q, 0), qy = blend_pos(q, 1);
    return sqrt((px - qx) * (px - qx) + (py - qy) * (py - qy));
}
typedef struct { uint64_t key; uint32_t i; } s2t_key;
static int s2t_dcmp(const void *x, const void *y) { const double a = *(const double *)x, b = *(const double *)y; return a < b ? -1 : a > b; }
static int s2t_cmp(const void *x, const void *y) { const s2t_key *a = (const s2t_key *)x, *b = (const s2t_key *)y; return a->key < b->key ? -1 : a->key > b->key; }
static double s2t_nearest(const remaster_prim *p, const remaster_frame_copy *at, const s2t_key *keys)
{
    const uint64_t k = stage2_pkey(p);
    size_t lo = 0, hi = at->n;
    while (lo < hi) { const size_t mid = lo + (hi - lo) / 2; if (keys[mid].key < k) lo = mid + 1; else hi = mid; }
    double best = 1e9;
    for (size_t j = lo; j < at->n && keys[j].key == k; j++) { const double d = s2t_dist(p, &at->prim[keys[j].i]); if (d < best) best = d; }
    return best;
}
void remaster_stage2_truth_report(const remaster_frame_copy *a, const remaster_frame_copy *b, const remaster_frame_copy *at, double t)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("F117_STAGE2_MESH_KEY"); g_stage2_mesh_key = e && atoi(e) != 0; on = getenv("F117_STAGE2_TRUTH") != NULL; }
    if (!on || !a->n || !b->n || !at->n) return;
    static unsigned calls;
    {   /* the three copies' view matrices and attitudes, for fitting the game's rotation convention */
        static unsigned logged;
        if (logged < 400 && a->have_cam && b->have_cam && at->have_cam) {
            logged++;
            const remaster_frame_copy *c3[3] = { a, b, at };
            fprintf(stderr, "[s2-cam] t %.4f", t);
            for (int q = 0; q < 3; q++) { fprintf(stderr, " |"); for (int w = 0; w < 12; w++) fprintf(stderr, " %d", c3[q]->cam[w]); }
            fprintf(stderr, "%s", "\n");
        }
    }
    /* [blended kind 0 poly / 1 light][0 paired, 1 unpaired][bucket < 0.5, < 1, < 2, < 4, < 16, >= 16]; [kind][bucket] for the nearer step held */
    static unsigned long long h[2][2][6], hold[2][6];
    remaster_stage2_pairs *pr = remaster_stage2_pairs_new();
    s2t_key *keys = (s2t_key *)malloc(at->n * sizeof *keys);
    remaster_prim *out = (remaster_prim *)malloc((a->n > b->n ? a->n : b->n) * sizeof *out);
    if (pr && keys && out && remaster_stage2_pair(pr, a, b)) {
        for (size_t j = 0; j < at->n; j++) { keys[j].key = stage2_pkey(&at->prim[j]); keys[j].i = (uint32_t)j; }
        qsort(keys, at->n, sizeof *keys, s2t_cmp);
        const int nb = t >= 0.5;
        const remaster_frame_copy *nr = nb ? b : a, *fr = nb ? a : b;
        const int *match = nb ? pr->ba : pr->ab;
        stage2_blend_job job = { nr, fr, match, nb ? 1.0 - t : t, out, 1 };
        s2_job_reproj(&job, a, b, pr, t, nb);
        stage2_blend_part(&job, 0);
        static double sv[4096]; size_t ns = 0;
        for (size_t i = 0; i < nr->n; i++) {
            const int kd = nr->prim[i].kind == SCENE_POLYGON ? 0 : 1;
            if (nr->prim[i].v[0].z <= 65536 || out[i].v[0].z <= 65536) continue;      /* behind or at the near plane: no screen position */
            const double e = s2t_nearest(&out[i], at, keys), eh = s2t_nearest(&nr->prim[i], at, keys);
            const int k = e < 0.1 ? 0 : e < 0.25 ? 1 : e < 0.5 ? 2 : e < 1 ? 3 : e < 4 ? 4 : 5, kh = eh < 0.1 ? 0 : eh < 0.25 ? 1 : eh < 0.5 ? 2 : eh < 1 ? 3 : eh < 4 ? 4 : 5;
            h[kd][match[i] < 0][k]++;
            hold[kd][kh]++;
            {   /* by depth scale: count, within 0.5, 4 or more */
                static double cs[16]; static unsigned long long cn[16][3]; static int ncs;
                const double sc = nr->prim[i].inv_depth_scale > 0 ? nr->prim[i].inv_depth_scale : 1.0;
                int ci = 0; while (ci < ncs && cs[ci] != sc) ci++;
                if (ci == ncs && ncs < 16) cs[ncs++] = sc;
                if (ci < 16) { cn[ci][0]++; if (e < 0.5) cn[ci][1]++; if (e >= 4) cn[ci][2]++; }
                if (i + 1 == nr->n && (calls + 1) % 30 == 0)
                    for (int q2 = 0; q2 < ncs; q2++) fprintf(stderr, "[s2-truth-scale] scale %.6g: %llu, <0.5 %.1f%%, >=4 %.1f%%, in travel %d\n", cs[q2], cn[q2][0], 100.0 * cn[q2][1] / cn[q2][0], 100.0 * cn[q2][2] / cn[q2][0], s2_cls_find(pr, cs[q2]) >= 0);
            }
            if (match[i] >= 0 && ns < 4096) {   /* how far along its own a-to-b path the truth is, for primitives that move more than 4 game pixels in the step */
                const remaster_prim *pa = nb ? &fr->prim[match[i]] : &nr->prim[i], *pb = nb ? &nr->prim[i] : &fr->prim[match[i]];
                const double ax = blend_pos(pa, 0), ay = blend_pos(pa, 1), dx = blend_pos(pb, 0) - ax, dy = blend_pos(pb, 1) - ay, L2 = dx * dx + dy * dy;
                if (L2 > 16.0) {
                    double tx = 0, ty = 0, td = 1e9; const uint64_t k3 = stage2_pkey(pa);
                    const double ex = ax + t * dx, ey = ay + t * dy;      /* the truth nearest the linear guess */
                    size_t lo2 = 0, hi2 = at->n;
                    while (lo2 < hi2) { const size_t mid = lo2 + (hi2 - lo2) / 2; if (keys[mid].key < k3) lo2 = mid + 1; else hi2 = mid; }
                    for (size_t j = lo2; j < at->n && keys[j].key == k3; j++) { const remaster_prim *c = &at->prim[keys[j].i]; const double qx = blend_pos(c, 0) - ex, qy = blend_pos(c, 1) - ey; if (qx * qx + qy * qy < td) { td = qx * qx + qy * qy; tx = blend_pos(c, 0); ty = blend_pos(c, 1); } }
                    if (td < 1e8) sv[ns++] = ((tx - ax) * dx + (ty - ay) * dy) / L2;
                }
            }
            {   /* examples of the worst: the nearer step's place, its partner's, the blend, and the nearest in the copy at t */
                static int shown;
                if (e >= 4 && shown < 60 && (calls % 7) == 0) {
                    shown++;
                    const remaster_prim *q = match[i] >= 0 ? &fr->prim[match[i]] : NULL;
                    double bx = 0, by = 0, bd = 1e9; const uint64_t k2 = stage2_pkey(&out[i]);
                    for (size_t j = 0; j < at->n; j++) if (stage2_pkey(&at->prim[j]) == k2) { const double d = s2t_dist(&out[i], &at->prim[j]); if (d < bd) { bd = d; bx = blend_pos(&at->prim[j], 0); by = blend_pos(&at->prim[j], 1); } }
                    double cx = 0, cy = 0, cd = 1e9;            /* the other step's same-key primitive nearest to the truth: the partner it should perhaps have had */
                    for (size_t j = 0; j < fr->n; j++) if (stage2_pkey(&fr->prim[j]) == k2) { const double d = sqrt((blend_pos(&fr->prim[j], 0) - bx) * (blend_pos(&fr->prim[j], 0) - bx) + (blend_pos(&fr->prim[j], 1) - by) * (blend_pos(&fr->prim[j], 1) - by)); if (d < cd) { cd = d; cx = blend_pos(&fr->prim[j], 0); cy = blend_pos(&fr->prim[j], 1); } }
                    fprintf(stderr, "[s2-truth-ex] t %.3f %s kind %d n %d model %d z %.1f: near (%.1f,%.1f) partner %s(%.1f,%.1f) blend (%.1f,%.1f) truth (%.1f,%.1f) err %.1f; other step nearest truth (%.1f,%.1f)\n",
                            t, nb ? "b" : "a", nr->prim[i].kind, nr->prim[i].n, nr->prim[i].model, nr->prim[i].v[0].z / 65536.0, blend_pos(&nr->prim[i], 0), blend_pos(&nr->prim[i], 1),
                            q ? "" : "none ", q ? blend_pos(q, 0) : 0.0, q ? blend_pos(q, 1) : 0.0, blend_pos(&out[i], 0), blend_pos(&out[i], 1), bx, by, e, cx, cy);
                }
            }
        }
        if (pr->have_delta && a->have_cam && b->have_cam) {   /* the model check: step a's paired primitives reprojected to step b (t = 1) against their partners */
            static unsigned long long mk[16][4]; static double mcs[16]; static int mncs; static unsigned mcalls;
            s2_cam ca2, cb2;
            s2_cam_at(a->cam, a->cam, 0.0, &ca2); s2_cam_at(b->cam, b->cam, 0.0, &cb2);
            for (size_t i = 0; i < a->n; i++) {
                const int j2 = pr->ab[i];
                if (j2 < 0 || !s2_reprojectable(a, i) || a->prim[i].v[0].z <= 65536 || b->prim[j2].v[0].z <= 65536) continue;
                const double sc = a->prim[i].inv_depth_scale > 0 ? a->prim[i].inv_depth_scale : 1.0;
                const int c = s2_cls_find(pr, sc);
                if (c < 0) continue;
                double w[3]; remaster_v3 v; remaster_prim pp = a->prim[i];
                s2_to_world(&ca2, &a->prim[i].v[0], w);
                for (int x = 0; x < 3; x++) w[x] -= pr->delta[c][x];
                s2_to_view(&cb2, w, &v); pp.v[0] = v;
                const double e = s2t_dist(&pp, &b->prim[j2]);
                int ci = 0; while (ci < mncs && mcs[ci] != sc) ci++;
                if (ci == mncs && mncs < 16) mcs[mncs++] = sc;
                if (ci < 16) { mk[ci][0]++; if (e < 0.5) mk[ci][1]++; if (e < 2) mk[ci][2]++; if (e >= 16) mk[ci][3]++; }
            }
            if (mcalls == 40 || mcalls == 120) {   /* one frame's pairs, for fitting offline: cams, then scale and the first vertices of a and its partner */
                static int dumped;
                FILE *df = fopen(dumped++ ? "s2_pairs_2.txt" : "s2_pairs_1.txt", "w");
                if (df) {
                    for (int w2 = 0; w2 < 12; w2++) fprintf(df, "%d ", a->cam[w2]);
                    fprintf(df, "%s", "\n");
                    for (int w2 = 0; w2 < 12; w2++) fprintf(df, "%d ", b->cam[w2]);
                    fprintf(df, "%s", "\n");
                    for (size_t i = 0; i < a->n; i++) {
                        const int j2 = pr->ab[i];
                        if (j2 < 0 || !s2_reprojectable(a, i)) continue;
                        const remaster_prim *p3 = &a->prim[i], *q3 = &b->prim[j2];
                        fprintf(df, "%g %d %d %d %d %d %d %d %d\n", p3->inv_depth_scale > 0 ? p3->inv_depth_scale : 1.0, p3->model, p3->v[0].x, p3->v[0].y, p3->v[0].z, q3->v[0].x, q3->v[0].y, q3->v[0].z, p3->kind);
                    }
                    fclose(df);
                }
            }
            if (++mcalls % 30 == 0)
                for (int q2 = 0; q2 < mncs; q2++) fprintf(stderr, "[s2-model] scale %.6g: a reprojected to b against the partner: %llu, <0.5 %.1f%%, <2 %.1f%%, >=16 %.1f%% (wrong partners or moving things)\n",
                                                        mcs[q2], mk[q2][0], 100.0 * mk[q2][1] / mk[q2][0], 100.0 * mk[q2][2] / mk[q2][0], 100.0 * mk[q2][3] / mk[q2][0]);
        }
        if (ns >= 8) {   /* this frame: t, and the quartiles of the truth's place along the a-to-b path (t when the motion is even) */
            qsort(sv, ns, sizeof *sv, s2t_dcmp);
            fprintf(stderr, "[s2-truth-t] t %.3f along p25 %.3f p50 %.3f p75 %.3f (%zu moving)\n", t, sv[ns / 4], sv[ns / 2], sv[3 * ns / 4], ns);
        }
        if (++calls % 30 == 0) {
            static const char *kn[2] = { "poly", "lights" };
            for (int kd = 0; kd < 2; kd++) {
                for (int u = 0; u < 3; u++) {
                    const unsigned long long *x = u < 2 ? h[kd][u] : hold[kd];
                    unsigned long long tot = 0; for (int q = 0; q < 6; q++) tot += x[q];
                    if (!tot) continue;
                    fprintf(stderr, "[s2-truth] %u frames %s %-8s %8llu: <0.1 %.1f%% <0.25 %.1f%% <0.5 %.1f%% <1 %.1f%% <4 %.2f%% >=4 %.2f%%\n", calls, kn[kd], u == 0 ? "paired" : u == 1 ? "unpaired" : "held",
                            tot, 100.0 * x[0] / tot, 100.0 * x[1] / tot, 100.0 * x[2] / tot, 100.0 * x[3] / tot, 100.0 * x[4] / tot, 100.0 * x[5] / tot);
                }
            }
        }
    }
    remaster_stage2_pairs_free(pr); free(keys); free(out);
}

/* The picture-space check: the scene the copy produced at t (r's completed frame) and the scene built by blending the copies at t = 0 and t = 1, each drawn by the same CPU scene renderer into
 * the wide canvas, and compared pixel for pixel. Unpaired primitives of the first step are kept when t is under a half, those of the second added when it is not. */
void remaster_blend_picture_check(remaster_live *r, const machine_t *m, int n, int tw, const remaster_frame_copy *a, const remaster_frame_copy *b, double t)
{
    if (!r || !r->have_done || !a->n || tw < 320 * n) return;
    const int h = 200 * n, xoff = (tw - 320 * n) / 2;
    const size_t px = (size_t)tw * h;
    int *ab = (int *)malloc(a->n * sizeof *ab);
    unsigned char *bused = (unsigned char *)calloc(b->n ? b->n : 1, 1);
    remaster_frame bf; memset(&bf, 0, sizeof bf);
    bf.prim = (remaster_prim *)malloc((a->n + b->n + 1) * sizeof *bf.prim);
    uint32_t *col1 = (uint32_t *)malloc(px * 4), *col2 = (uint32_t *)malloc(px * 4);
    if (!ab || !bused || !bf.prim || !col1 || !col2) { free(ab); free(bused); free(bf.prim); free(col1); free(col2); return; }
    pair_by_key(a, b, ab);
    for (size_t i = 0; i < a->n; i++) {
        remaster_prim p = a->prim[i];
        if (ab[i] >= 0) {
            const remaster_prim *q = &b->prim[ab[i]];
            bused[ab[i]] = 1;
            for (int v = 0; v < p.n && v < q->n; v++) {
                p.v[v].x = (int32_t)((double)p.v[v].x + t * ((double)q->v[v].x - p.v[v].x));
                p.v[v].y = (int32_t)((double)p.v[v].y + t * ((double)q->v[v].y - p.v[v].y));
                p.v[v].z = (int32_t)((double)p.v[v].z + t * ((double)q->v[v].z - p.v[v].z));
            }
            p.inv_depth_scale = a->prim[i].inv_depth_scale + t * (q->inv_depth_scale - a->prim[i].inv_depth_scale);
            bf.prim[bf.n++] = p;
        } else if (t < 0.5) bf.prim[bf.n++] = p;
    }
    if (t >= 0.5) for (size_t j = 0; j < b->n; j++) if (!bused[j]) bf.prim[bf.n++] = b->prim[j];
    bf.cap = bf.n;

    static struct { void *p; size_t cap; } pool[9];
    const size_t need = px * 4;
    for (int k = 0; k < 9; k++) {
        if (pool[k].cap < need) { void *q = realloc(pool[k].p, need); if (!q) goto done; pool[k].p = q; pool[k].cap = need; }
    }
    for (int pass = 0; pass < 2; pass++) {
        remaster_target tg;
        memset(&tg, 0, sizeof tg);
        tg.w = tw; tg.h = h;
        tg.color = (uint32_t *)pool[0].p; tg.inv_z = (float *)pool[1].p; tg.index = (uint16_t *)pool[2].p; tg.owner = (uint32_t *)pool[5].p;
        tg.line_strong = (uint32_t *)pool[3].p; tg.line_tmp_z = (float *)pool[4].p;
        tg.stroke_z = (float *)pool[6].p; tg.stroke_owner = (uint32_t *)pool[7].p; tg.stroke_aux_x = (float *)pool[8].p; tg.stroke_aux_y = (float *)pool[8].p;
        tg.inv_depth_scale = 1.0;
        const remaster_frame *fr = pass ? &bf : &r->done;
        scene_job sj = { m, tg, fr, r->done_band, r->done_have_band, 1, 1, 0, r->done_origin, r->done_window, (double)n, (double)xoff };
        sj.timing = NULL; sj.row_tile = 0;
        run_parts(scene_rows_part, &sj, 1);
        memcpy(pass ? col2 : col1, tg.color, px * 4);
    }
    {
        static unsigned calls; static unsigned long long total, differ, big; static unsigned long long frames_clean;
        size_t d = 0, bg = 0;
        for (size_t i = 0; i < px; i++)
            if (col1[i] != col2[i]) {
                d++;
                int mx = 0;
                for (int sh = 0; sh < 24; sh += 8) { int dd = (int)(col1[i] >> sh & 255) - (int)(col2[i] >> sh & 255); if (dd < 0) dd = -dd; if (dd > mx) mx = dd; }
                if (mx > 32) bg++;
            }
        total += px; differ += d; big += bg; if (d * 1000 < px) frames_clean++;
        if (++calls % 30 == 1)
            fprintf(stderr, "[blend-picture] %u frames (t %.2f, %zu blended primitives against %zu): the blended scene differs from the copy's in %.3f%% of pixels over all frames (%.3f%% by more than 32 levels); "
                            "this frame %zu of %zu; %.0f%% of frames differ in under 0.1%% of their pixels\n",
                    calls, t, bf.n, r->done.n, 100.0 * (double)differ / (double)total, 100.0 * (double)big / (double)total, d, px, 100.0 * (double)frames_clean / calls);
    }
done:
    free(ab); free(bused); free(bf.prim); free(col1); free(col2);
}

remaster_frame_copy *g_live_ring[8];
unsigned long long g_live_ring_steps;

/* How many of the live steps kept (the newest first, 0 the latest) the copy `c` reproduces exactly (same primitive count, models, colours and vertices); -1 when none. */
int remaster_live_ring_match(const remaster_frame_copy *c, int *which_similar, size_t *similar_prims)
{
    int best_sim = -1; size_t best_n = 0;
    for (unsigned age = 0; age < 8 && age < g_live_ring_steps; age++) {
        const remaster_frame_copy *l = g_live_ring[(g_live_ring_steps - 1 - age) % 8];
        if (!l) continue;
        size_t same = 0;
        for (size_t i = 0; i < c->n; i++) {                              /* the copy's primitive i exists in the live step, vertex for vertex, wherever it sits */
            const remaster_prim *p = &c->prim[i];
            for (size_t j = 0; j < l->n; j++) {
                const remaster_prim *q = &l->prim[j];
                if (q->model != p->model || q->n != p->n || q->colour_word != p->colour_word || q->kind != p->kind) continue;
                if (!memcmp(q->v, p->v, sizeof(remaster_v3) * (size_t)(p->n < 16 ? p->n : 16))) { same++; break; }
            }
        }
        if (l->n == c->n && same == c->n) return (int)age;
        if (same > best_n) { best_n = same; best_sim = (int)age; }
    }
    if (which_similar) *which_similar = best_sim;
    if (similar_prims) *similar_prims = best_n;
    return -1;
}

size_t remaster_frame_copy_prims(const remaster_frame_copy *c) { return c ? c->n : 0; }
