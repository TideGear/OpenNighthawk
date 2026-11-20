/* gpu_compose.c - the Remastered composite on the GPU (gpu_compose.h, docs/gpu-compositor.md). The setup is the offline tools' (gpu_stage, gpu_polys, gpu_strokes,
 * gpu_replace): the CPU clips and projects the polygons and strokes, splits them into triangles and bins them into 16 x 16 tiles with the weights evaluated in
 * double at each tile's first pixel; the passes are the shaders in src/present/gpu. */
#include "gpu_compose.h"
#include "tls.h"

#define STROKE_WORDS 24     /* words of one stroke record (strokes_x.hlsl: the same stride) */
#include "exact.h"
#include "decal_faces.h"
#include "square_pen.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef F117_GPU_TIMESTAMPS
#include <windows.h>
#include "gpu_timestamps.h"
#endif

#define TILE 16
#define GSETS 2             /* buffer sets and CPU scratch sets: a picture's setup works in one of them */
#define SETUP_CHUNKS 32     /* the classify loop's record ranges (more than threads, for balance) */
#define MAX_POLY 16

typedef struct { double x, y, z; } dv3;
typedef struct { double x, y, iz; } sv;

typedef struct { SDL_GPUBuffer *buf; Uint32 cap; } gbuf;
typedef struct { size_t first, count; int kind; } hw_seg_t;

typedef struct {
    float a1, b1, a2, b2, ciz, diz1, diz2;
    uint32_t w7, paint_index, paint_owner, kind;
    double cx, cy, a1d, b1d, a2d, b2d;
    int bbox[4];
    long long dbase, dpx, dpy, dox, doy;   /* exact depth: the inverse-depth plane (exact.h) */
    long long q[6], area;           /* exact coverage: the vertices snapped to 1/256 px (ax, ay, bx, by, cx, cy) and twice the signed area */
} ctri;
typedef struct { ctri *t; size_t n, cap; } clist;

typedef struct { void *p; size_t n, cap; } vec;     /* a growing array of bytes */
#define HW_CHUNKS 8
typedef struct { vec poly, line; int nseg, fail; uint32_t from, to; hw_seg_t seg[8192]; } hw_chunk;   /* one slice of the records' vertices, built on a setup thread (hw_begin) */

/* The CPU setup's lists and arrays for one picture: the classified triangles, the per-chunk lists they are merged from, the decal face list and the words packed for the upload. */
typedef struct {
    clist polys, strokes;
    clist cpoly[SETUP_CHUNKS], cstroke[SETUP_CHUNKS];     /* the classify loop's per-chunk lists, merged in order into polys and strokes */
    vec cword[SETUP_CHUNKS];
    decal_face_cache faces;
    vec band_words, poly_words, poly_ents, poly_off, stroke_tri_words, stroke_ents, stroke_off, stroke_words;
    SDL_AtomicInt cdone[SETUP_CHUNKS];                    /* classify chunk k is finished (the face list thread reads the chunks in order as they finish) */
    int faces_early;                                      /* the face list thread was started by the classify, before it ran */
    struct { SDL_Thread *th; SDL_Semaphore *go, *done; int quit, running, chunks; } fth;      /* this set's decal face list thread */
} gscratch;

/* One picture's GPU resources and bookkeeping. The compositor keeps two sets so that the setup of the next picture (a second thread, Part B pipeline) can fill one while the
 * first still holds the GPU's work of the previous picture; which set a call uses is the calling thread's (gpu_compose_use_set), 0 unless it says otherwise. */
typedef struct {
    gbuf bt, pt, pe, po, pal, st, se, so, sw, hid, tf, src, tag, dac;
    gbuf colour, index_, invz, owner, strokez, outpix;
    SDL_GPUTransferBuffer *up, *down;
    Uint32 up_cap, down_cap;
    SDL_GPUTransferBuffer *upb;             /* the band triangles' own upload, for F117_GPU_BANDS_FIRST (the bands pass is submitted before the rest of the setup is done) */
    Uint32 upb_cap;
    double setup_ms, gpu_ms, copy_ms, wait_ms;
    int begun;                              /* gpu_compose_begin has run and gpu_compose_finish has not */
    gpu_compose_in params;                  /* the begun frame's replace parameters (the pointers in it are not kept) */
    Uint32 pbytes, game_bytes;
    size_t npix_game;
    SDL_GPUTransferBuffer *up2;             /* the game's picture, tags and palette, uploaded by the second half */
    Uint32 up2_cap;
    Uint64 t_begin;
    struct {                                /* the hardware-rasterised path's (F117_HW_RASTER) per-set resources */
        SDL_GPUTexture *pt[4], *tout, *tpal, *dpt; int w, h;
        SDL_GPUBuffer *vb; Uint32 vcap;
        SDL_GPUTransferBuffer *up; Uint32 upcap;
        vec v; int nseg; hw_seg_t seg[8192];
        hw_chunk ch[HW_CHUNKS];
    } hw;
    gscratch sc;                            /* this picture's CPU setup lists (Part B, 16 November 2026: per set, so two pictures' setups can run at once) */
} gset;

struct gpu_compose {
    char shader_dir[512];
    struct { int tried, ok; SDL_GPUGraphicsPipeline *pipe[6], *rpipe; SDL_GPUComputePipeline *pexport; SDL_GPUSampler *smp; } hw;
    SDL_GPUDevice *dev;
    SDL_GPUComputePipeline *p_bands, *p_polys, *p_strokes, *p_replace, *p_present;
    int own_device;
    SDL_GPUTexture *targets[2];
    int cur_write, last_written;
    int tgt_w, tgt_h;
    SDL_GPUTransferBuffer *tgt_down;
    gset set[GSETS];
    Uint32 pix_cap;
    int said;
    void *profile;
    void *(*profile_list)(SDL_GPUCommandBuffer *);
    FILE *profile_log;
    int profile_pending, profile_failed;
};

static F117_TLS int g_set_tls;
void gpu_compose_use_set(int k) { g_set_tls = k % GSETS; }
/* F117_SETUP_SLOW diagnostic: the phases of the last setup this thread ran: classify, bin, pack, buffers, copy (ms), triangles, MB uploaded */
static F117_TLS double g_last_phase[9];
void gpu_compose_last_phases(double out[9]) { for (int k = 0; k < 9; k++) out[k] = g_last_phase[k]; }
#define GS(g) (&(g)->set[g_set_tls])
#define SC(g) (&GS(g)->sc)

static void profile_init(gpu_compose *g)
{
#ifdef F117_GPU_TIMESTAMPS
    const char *path = getenv("F117_GPU_TIMESTAMP_LOG");
    if (!path || !*path) return;
    if (strcmp(SDL_GetGPUDeviceDriver(g->dev), "direct3d12") ||
        getenv("F117_GPU_PASS_TIMES") || getenv("F117_GPU_PRESENT_SPLIT") ||
        getenv("F117_GPU_PRESENT_TWICE")) {
        fprintf(stderr, "[gpu-timestamp] unsupported backend or split-pass diagnostic\n");
        return;
    }
    HMODULE module = GetModuleHandleW(L"SDL3.dll");
    if (!module) { fprintf(stderr, "[gpu-timestamp] SDL module unavailable\n"); return; }
    void *(*device)(SDL_GPUDevice *) = (void *(*)(SDL_GPUDevice *))GetProcAddress(module, "F117_GPUProfileDevice");
    void *(*queue)(SDL_GPUDevice *) = (void *(*)(SDL_GPUDevice *))GetProcAddress(module, "F117_GPUProfileQueue");
    g->profile_list = (void *(*)(SDL_GPUCommandBuffer *))GetProcAddress(module, "F117_GPUProfileCommandList");
    if (!device || !queue || !g->profile_list) {
        fprintf(stderr, "[gpu-timestamp] optional SDL profiling bridge unavailable\n");
        return;
    }
    g->profile = f117_timestamp_create(device(g->dev), queue(g->dev));
    if (g->profile) g->profile_log = fopen(path, "w");
    if (!g->profile_log) {
        f117_timestamp_destroy(g->profile); g->profile = NULL;
        fprintf(stderr, "[gpu-timestamp] query resources or output unavailable\n");
        return;
    }
    fprintf(g->profile_log, "from,to,gpu_ms\n");
#else
    (void)g;
#endif
}

static void profile_mark(gpu_compose *g, SDL_GPUCommandBuffer *cb, const char *label)
{
#ifdef F117_GPU_TIMESTAMPS
    if (g->profile && !f117_timestamp_mark(g->profile, g->profile_list(cb), label))
        { g->profile_failed = 1; fprintf(stderr, "[gpu-timestamp] query marker failed: %s\n", label); }
#else
    (void)g; (void)cb; (void)label;
#endif
}

static void profile_resolve(gpu_compose *g, SDL_GPUCommandBuffer *cb)
{
#ifdef F117_GPU_TIMESTAMPS
    if (g->profile && !f117_timestamp_resolve(g->profile, g->profile_list(cb))) g->profile_failed = 1;
#else
    (void)g; (void)cb;
#endif
}

static void say(gpu_compose *g, const char *msg)
{
    if (g->said) return;
    g->said = 1;
    fprintf(stderr, "[gpu-compose] %s\n", msg);
}

static void *vec_reserve(vec *v, size_t bytes)
{
    if (v->cap < bytes) { size_t c = v->cap ? v->cap : 1024; while (c < bytes) c *= 2; void *q = realloc(v->p, c); if (!q) return NULL; v->p = q; v->cap = c; }
    v->n = bytes;
    return v->p;
}

static int gbuf_ensure(SDL_GPUDevice *dev, gbuf *b, SDL_GPUBufferUsageFlags usage, Uint32 bytes)
{
    if (bytes < 256) bytes = 256;
    if (b->buf && b->cap >= bytes) return 1;
    if (b->buf) SDL_ReleaseGPUBuffer(dev, b->buf);
    Uint32 cap = b->cap ? b->cap : 4096;
    while (cap < bytes) cap *= 2;
    SDL_GPUBufferCreateInfo bi = { usage, cap, 0 };
    b->buf = SDL_CreateGPUBuffer(dev, &bi);
    b->cap = b->buf ? cap : 0;
    return b->buf != NULL;
}

static SDL_GPUComputePipeline *load_pipeline(SDL_GPUDevice *dev, const char *dir, const char *name, Uint32 ro, Uint32 rw, Uint32 rw_tex, Uint32 tx, Uint32 ty)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    size_t size = 0;
    void *code = SDL_LoadFile(path, &size);
    if (!code) return NULL;
    SDL_GPUComputePipelineCreateInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.code = (const Uint8 *)code; ci.code_size = size; ci.entrypoint = "main"; ci.format = SDL_GPU_SHADERFORMAT_DXIL;
    ci.num_readonly_storage_buffers = ro; ci.num_readwrite_storage_buffers = rw; ci.num_readwrite_storage_textures = rw_tex; ci.num_uniform_buffers = 1;
    ci.threadcount_x = tx; ci.threadcount_y = ty; ci.threadcount_z = 1;
    SDL_GPUComputePipeline *p = SDL_CreateGPUComputePipeline(dev, &ci);
    SDL_free(code);
    return p;
}

static gpu_compose *compose_new(SDL_GPUDevice *device, const char *shader_dir, int own)
{
    {   /* the adapter the compositor runs on (a different GPU rounds a few pixels differently: the CPU-vs-GPU shots compare at exactly 0) */
        const SDL_PropertiesID pr = SDL_GetGPUDeviceProperties(device);
        fprintf(stderr, "[gpu-compose] device: %s, driver: %s %s\n", SDL_GetStringProperty(pr, SDL_PROP_GPU_DEVICE_NAME_STRING, "?"), SDL_GetStringProperty(pr, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, "?"), SDL_GetStringProperty(pr, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "?")); }
    gpu_compose *g = (gpu_compose *)calloc(1, sizeof *g);
    if (!g) return NULL;
    g->dev = device;
    g->own_device = own;
    snprintf(g->shader_dir, sizeof g->shader_dir, "%s", shader_dir ? shader_dir : ".");
    g->p_bands = load_pipeline(g->dev, shader_dir, "gpu_bands_x.dxil", 1, 4, 0, 64, 1);
    g->p_polys = load_pipeline(g->dev, shader_dir, "gpu_polys_x.dxil", 4, 4, 0, 16, 16);
    g->p_strokes = load_pipeline(g->dev, shader_dir, "gpu_strokes_x.dxil", 4, 6, 0, 16, 16);
    g->p_replace = load_pipeline(g->dev, shader_dir, "gpu_replace.dxil", 4, 4, 0, 16, 16);
    g->p_present = load_pipeline(g->dev, shader_dir, "gpu_present.dxil", 1, 0, 1, 16, 16);
    if (!g->p_bands || !g->p_polys || !g->p_strokes || !g->p_replace) { gpu_compose_destroy(g); return NULL; }
    profile_init(g);
    return g;
}

gpu_compose *gpu_compose_create(const char *shader_dir)
{
    if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_InitSubSystem(SDL_INIT_VIDEO)) return NULL;
    SDL_GPUDevice *dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, false, NULL);
    if (!dev) return NULL;
    gpu_compose *g = compose_new(dev, shader_dir, 1);
    if (!g) SDL_DestroyGPUDevice(dev);
    return g;
}

gpu_compose *gpu_compose_create_on(SDL_GPUDevice *device, const char *shader_dir)
{
    return device ? compose_new(device, shader_dir, 0) : NULL;
}

static void setup_pool_shutdown(void);
static void faces_thread_shutdown(gpu_compose *g);
static int faces_start_chunks(gpu_compose *g);

void gpu_compose_destroy(gpu_compose *g)
{
    if (!g) return;
#ifdef F117_GPU_TIMESTAMPS
    if (g->profile) {
        SDL_WaitForGPUIdle(g->dev);
        f117_timestamp_destroy(g->profile);
    }
    if (g->profile_log) fclose(g->profile_log);
#endif
    if (g->dev) {
        for (int si = 0; si < 2; si++) {
            gset *q = &g->set[si];
            gbuf *all[] = { &q->bt, &q->pt, &q->pe, &q->po, &q->pal, &q->st, &q->se, &q->so, &q->sw, &q->hid, &q->tf, &q->src, &q->tag, &q->dac,
                            &q->colour, &q->index_, &q->invz, &q->owner, &q->strokez, &q->outpix };
            for (size_t i = 0; i < sizeof all / sizeof *all; i++) if (all[i]->buf) SDL_ReleaseGPUBuffer(g->dev, all[i]->buf);
            if (q->up) SDL_ReleaseGPUTransferBuffer(g->dev, q->up);
            if (q->down) SDL_ReleaseGPUTransferBuffer(g->dev, q->down);
            if (q->up2) SDL_ReleaseGPUTransferBuffer(g->dev, q->up2);
            if (q->upb) SDL_ReleaseGPUTransferBuffer(g->dev, q->upb);
            for (int k = 0; k < 4; k++) if (q->hw.pt[k]) SDL_ReleaseGPUTexture(g->dev, q->hw.pt[k]);
            if (q->hw.tout) SDL_ReleaseGPUTexture(g->dev, q->hw.tout);
            if (q->hw.tpal) SDL_ReleaseGPUTexture(g->dev, q->hw.tpal);
            if (q->hw.dpt) SDL_ReleaseGPUTexture(g->dev, q->hw.dpt);
            if (q->hw.vb) SDL_ReleaseGPUBuffer(g->dev, q->hw.vb);
            if (q->hw.up) SDL_ReleaseGPUTransferBuffer(g->dev, q->hw.up);
            free(q->hw.v.p);
        }
        for (int k = 0; k < 6; k++) if (g->hw.pipe[k]) SDL_ReleaseGPUGraphicsPipeline(g->dev, g->hw.pipe[k]);
        if (g->hw.rpipe) SDL_ReleaseGPUGraphicsPipeline(g->dev, g->hw.rpipe);
        if (g->hw.pexport) SDL_ReleaseGPUComputePipeline(g->dev, g->hw.pexport);
        if (g->hw.smp) SDL_ReleaseGPUSampler(g->dev, g->hw.smp);
        if (g->p_bands) SDL_ReleaseGPUComputePipeline(g->dev, g->p_bands);
        if (g->p_polys) SDL_ReleaseGPUComputePipeline(g->dev, g->p_polys);
        if (g->p_strokes) SDL_ReleaseGPUComputePipeline(g->dev, g->p_strokes);
        if (g->p_replace) SDL_ReleaseGPUComputePipeline(g->dev, g->p_replace);
        if (g->p_present) SDL_ReleaseGPUComputePipeline(g->dev, g->p_present);
        for (int k = 0; k < 2; k++) if (g->targets[k]) SDL_ReleaseGPUTexture(g->dev, g->targets[k]);
        if (g->tgt_down) SDL_ReleaseGPUTransferBuffer(g->dev, g->tgt_down);
        if (g->own_device) SDL_DestroyGPUDevice(g->dev);
    }
    faces_thread_shutdown(g);
    setup_pool_shutdown();
    for (int si = 0; si < GSETS; si++) {
        gscratch *sc = &g->set[si].sc;
        free(sc->faces.words);
        free(sc->polys.t); free(sc->strokes.t);
        for (int k = 0; k < SETUP_CHUNKS; k++) { free(sc->cpoly[k].t); free(sc->cstroke[k].t); free(sc->cword[k].p); }
        free(sc->band_words.p); free(sc->poly_words.p); free(sc->poly_ents.p); free(sc->poly_off.p);
        free(sc->stroke_tri_words.p); free(sc->stroke_ents.p); free(sc->stroke_off.p); free(sc->stroke_words.p);
    }
    free(g);
}

void gpu_compose_times(const gpu_compose *g, double *setup_ms, double *gpu_ms, double *copy_ms)
{
    if (copy_ms) *copy_ms = g ? GS(g)->copy_ms : 0.0;
    if (setup_ms) *setup_ms = g ? GS(g)->setup_ms : 0.0;
    if (gpu_ms) *gpu_ms = g ? GS(g)->gpu_ms : 0.0;
}

double gpu_compose_wait_ms(const gpu_compose *g)
{
    return g ? GS(g)->wait_ms : 0.0;
}

/* ---- CPU setup ---- */

/* Exact coverage (remaster_raster.c, on by default, F117_RASTER_EXACT=0 turns it off; docs/gpu-compositor.md "Step 2 design"): the same integers the CPU's itri_covers tests, handed to the shaders. */
static int exact_on(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("F117_RASTER_EXACT"); on = !(e && *e == '0'); }
    return on;
}

static void put64(uint32_t *w, long long v) { const uint64_t u = (uint64_t)v; w[0] = (uint32_t)u; w[1] = (uint32_t)(u >> 32); }

/* words 0 to 11 of wd: for the three edges (a to b, b to c, c to a) the change of the edge value per pixel in x and in y, as int64; word 12: 1 when the area is positive */
static void put_exact_steps(uint32_t *wd, const ctri *tr)
{
    for (int k = 0; k < 3; k++) {
        const long long xi = tr->q[2 * k], yi = tr->q[2 * k + 1], xj = tr->q[(2 * k + 2) % 6], yj = tr->q[(2 * k + 3) % 6];
        put64(wd + 4 * k, -(yj - yi) * 256);
        put64(wd + 4 * k + 2, (xj - xi) * 256);
    }
    wd[12] = tr->area > 0 ? 1u : 0u;
}

/* the three edge values at the centre of the tile's first pixel, as int64 */
static void put_exact_origin(uint32_t *en, const ctri *tr, int tx, int ty)
{
    const long long px = (long long)tx * TILE * 256 + 128, py = (long long)ty * TILE * 256 + 128;
    for (int k = 0; k < 3; k++) {
        const long long xi = tr->q[2 * k], yi = tr->q[2 * k + 1], xj = tr->q[(2 * k + 2) % 6], yj = tr->q[(2 * k + 3) % 6];
        put64(en + 2 * k, EX_SUB(EX_MUL(xj - xi, py - yi), EX_MUL(yj - yi, px - xi)));
    }
}

/* Exact mode: whether any pixel centre of tile (tx, ty) can be covered by the snapped triangle, from the same int64 edge values as ex_covers. */
static int tile_touches_exact(const ctri *tr, int tx, int ty)
{
    const int pos = tr->area > 0;
    for (int k = 0; k < 3; k++) {
        const long long xi = tr->q[2 * k], yi = tr->q[2 * k + 1], xj = tr->q[(2 * k + 2) % 6], yj = tr->q[(2 * k + 3) % 6];
        long long best = 0; int first = 1;
        for (int cy2 = 0; cy2 < 2; cy2++)
            for (int cx2 = 0; cx2 < 2; cx2++) {
                const long long px = ((long long)tx * TILE + cx2 * (TILE - 1)) * 256 + 128, py = ((long long)ty * TILE + cy2 * (TILE - 1)) * 256 + 128;
                long long e = EX_SUB(EX_MUL(xj - xi, py - yi), EX_MUL(yj - yi, px - xi));
                if (!pos) e = -e;
                if (first || e > best) { best = e; first = 0; }
            }
        if (best < 0) return 0;
    }
    return 1;
}

/* Diagnostic cycle counters for the classify loop (F117_REMASTER_TIMING prints them): 0 bands, 1 polygons, 2 strokes, 3 depth planes, 4 snapped vertices, 5 the whole loop. */
#if defined(_MSC_VER)
#include <intrin.h>
#define CYC() (g_cyc_on ? __rdtsc() : 0ull)
#else
#define CYC() 0ull
#endif
static int g_cyc_on;
static unsigned long long g_cyc[6], g_cnt[6];
/* Part B (13 November 2026): the counters run only with F117_CLASSIFY_COUNTERS set (g_cyc_on, read once a picture in gpu_compose_begin). They used to run on
 * every polygon, stroke and vertex on every classify thread: locked adds on the same few cache lines, which kept the classify from scaling past about 8 threads. */
#if defined(_MSC_VER)
#define CADD(a, i, v) do { if (g_cyc_on) _InterlockedExchangeAdd64((volatile __int64 *)&(a)[i], (__int64)(v)); } while (0)
#else
#define CADD(a, i, v) do { if (g_cyc_on) (a)[i] += (v); } while (0)
#endif
/* read once per picture in gpu_compose_begin (the classify loop can run on several threads) */
static int g_q_check, g_plane_full, g_plane_check;
/* The plane setup as it was before ex_llround and ex_pow2 (llround and ldexp), for F117_BIN_CHECK=1 to compare the depth-only setup with. */
static void planes_ref(ex_planes *p, const ex_tri *t, const double v[EX_PLANES][3])
{
    const double xa = (double)t->q[0] / 256.0, ya = (double)t->q[1] / 256.0, xb = (double)t->q[2] / 256.0, yb = (double)t->q[3] / 256.0;
    const double xc = (double)t->q[4] / 256.0, yc = (double)t->q[5] / 256.0;
    const double d = (xb - xa) * (yc - ya) - (xc - xa) * (yb - ya);
    long long mx = t->q[0], my = t->q[1];
    if (t->q[2] < mx) mx = t->q[2];
    if (t->q[4] < mx) mx = t->q[4];
    if (t->q[3] < my) my = t->q[3];
    if (t->q[5] < my) my = t->q[5];
    p->ox = ex_floor_div256(mx); p->oy = ex_floor_div256(my);
    for (int k = 0; k < EX_PLANES; k++) {
        const double scale = ldexp(1.0, k == 0 ? EX_SCALE_IZ : EX_SCALE_RGB);
        const double gx = ((v[k][1] - v[k][0]) * (yc - ya) - (v[k][2] - v[k][0]) * (yb - ya)) / d;
        const double gy = ((v[k][2] - v[k][0]) * (xb - xa) - (v[k][1] - v[k][0]) * (xc - xa)) / d;
        const double b0 = v[k][0] + gx * ((double)p->ox + 0.5 - xa) + gy * ((double)p->oy + 0.5 - ya);
        double r0 = b0 * scale, r1 = gx * scale, r2 = gy * scale;
        if (!(r0 > -9.0e18)) r0 = -9.0e18;
        if (!(r0 < 9.0e18)) r0 = 9.0e18;
        if (!(r1 > -9.0e18)) r1 = -9.0e18;
        if (!(r1 < 9.0e18)) r1 = 9.0e18;
        if (!(r2 > -9.0e18)) r2 = -9.0e18;
        if (!(r2 < 9.0e18)) r2 = 9.0e18;
        p->base[k] = (uint64_t)llround(r0); p->px[k] = (uint64_t)llround(r1); p->py[k] = (uint64_t)llround(r2);
    }
}

static int c_add(clist *l, uint32_t w7, uint32_t paint_index, uint32_t paint_owner, uint32_t kind, sv a, sv b, sv c, int w, int h)
{
    if (exact_on() && ex_outside(a.x, a.y, b.x, b.y, c.x, c.y)) {       /* the same guard-band clip as remaster_raster.c, from the same exact.h code */
        const ex_vtx in[3] = { { a.x, a.y, { a.iz, 0, 0, 0 } }, { b.x, b.y, { b.iz, 0, 0, 0 } }, { c.x, c.y, { c.iz, 0, 0, 0 } } };
        ex_vtx o[16]; const int m = ex_guard_clip(in, 3, o);
        for (int k = 1; k + 1 < m; k++) {
            const sv p = { o[0].x, o[0].y, o[0].a[0] }, q = { o[k].x, o[k].y, o[k].a[0] }, r = { o[k + 1].x, o[k + 1].y, o[k + 1].a[0] };
            if (!c_add(l, w7, paint_index, paint_owner, kind, p, q, r, w, h)) return 0;
        }
        return 1;
    }
    const double den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (fabs(den) < 1e-12) return 1;
    const double minx = fmin(a.x, fmin(b.x, c.x)), maxx = fmax(a.x, fmax(b.x, c.x));
    const double miny = fmin(a.y, fmin(b.y, c.y)), maxy = fmax(a.y, fmax(b.y, c.y));
    int x0 = (int)floor(minx - 0.5), x1 = (int)ceil(maxx - 0.5), y0 = (int)floor(miny - 0.5), y1 = (int)ceil(maxy - 0.5);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w - 1) x1 = w - 1;
    if (y1 > h - 1) y1 = h - 1;
    if (x0 > x1 || y0 > y1) return 1;
    if (l->n == l->cap) {
        const size_t cap = l->cap ? l->cap * 2 : 1024;
        ctri *q = (ctri *)realloc(l->t, cap * sizeof *q);
        if (!q) return 0;
        l->t = q; l->cap = cap;
    }
    ctri *t = &l->t[l->n++];
    memset(t, 0, sizeof *t);
    t->a1d = (b.y - c.y) / den; t->b1d = (c.x - b.x) / den; t->a2d = (c.y - a.y) / den; t->b2d = (a.x - c.x) / den;
    t->a1 = (float)t->a1d; t->b1 = (float)t->b1d; t->a2 = (float)t->a2d; t->b2 = (float)t->b2d;
    t->ciz = (float)c.iz; t->diz1 = (float)(a.iz - c.iz); t->diz2 = (float)(b.iz - c.iz);
    t->w7 = w7; t->paint_index = paint_index; t->paint_owner = paint_owner; t->kind = kind;
    t->cx = c.x; t->cy = c.y;
    t->bbox[0] = x0; t->bbox[1] = x1; t->bbox[2] = y0; t->bbox[3] = y1;
    const unsigned long long cv = CYC();
    t->q[0] = ex_llround(a.x * 256.0); t->q[1] = ex_llround(a.y * 256.0);
    t->q[2] = ex_llround(b.x * 256.0); t->q[3] = ex_llround(b.y * 256.0);
    t->q[4] = ex_llround(c.x * 256.0); t->q[5] = ex_llround(c.y * 256.0);
    CADD(g_cyc, 4, CYC() - cv); CADD(g_cnt, 4, 1);
    {
        if (g_q_check) {        /* F117_BIN_CHECK=1: the snapped vertices against llround, and 2^k against ldexp */
            static unsigned long long n; static unsigned bad;
            const double in[6] = { a.x * 256.0, a.y * 256.0, b.x * 256.0, b.y * 256.0, c.x * 256.0, c.y * 256.0 };
            for (int k = 0; k < 6; k++) if (t->q[k] != llround(in[k])) { bad++; if (bad < 4) fprintf(stderr, "[round-check] differs: %.17g %lld %lld\n", in[k], t->q[k], llround(in[k])); }
            if (ex_pow2(EX_SCALE_IZ) != ldexp(1.0, EX_SCALE_IZ) || ex_pow2(EX_SCALE_RGB) != ldexp(1.0, EX_SCALE_RGB)) bad++;
            n += 6;
            if (n % 120000000ull < 6) fprintf(stderr, "[round-check] %llu values checked, %u differ\n", n, bad);
        }
    }
    t->area = (t->q[2] - t->q[0]) * (t->q[5] - t->q[1]) - (t->q[4] - t->q[0]) * (t->q[3] - t->q[1]);
    if (exact_on() && t->area == 0) { l->n--; return 1; }      /* the CPU fill skips a triangle with no area */
    if (t->area != 0) {
        ex_tri et; memcpy(et.q, t->q, sizeof et.q); et.area = t->area;
        const double pv[EX_PLANES][3] = { { a.iz, b.iz, c.iz }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
        const int plane_full = g_plane_full;     /* F117_PLANE_FULL=1: the full four-plane setup (the reference; for paired A/B timing) */
        ex_planes pl;
        const unsigned long long cq = CYC();
        if (plane_full) ex_planes_make(&pl, &et, pv); else ex_plane0_make(&pl, &et, a.iz, b.iz, c.iz);
        CADD(g_cyc, 3, CYC() - cq); CADD(g_cnt, 3, 1);
        if (g_plane_check) {      /* the full setup must give the same plane 0, origin and zero colour planes */
            ex_planes full; planes_ref(&full, &et, pv);
            static unsigned bad; static unsigned n;
            n++;
            if (memcmp(&full, &pl, sizeof pl)) { bad++; if (bad < 4) fprintf(stderr, "[plane-check] differs (%u of %u)\n", bad, n); }
            else if (n % 200000 == 0) fprintf(stderr, "[plane-check] %u triangles checked, %u differ\n", n, bad);
        }
        t->dbase = (long long)pl.base[0]; t->dpx = (long long)pl.px[0]; t->dpy = (long long)pl.py[0]; t->dox = pl.ox; t->doy = pl.oy;
    }
    return 1;
}

static int clip_near(const remaster_v3 *in, int n, double near_z, dv3 *out)
{
    int m = 0;
    for (int i = 0; i < n && m < MAX_POLY - 1; i++) {
        const remaster_v3 a = in[i], b = in[(i + 1) % n];
        const int ina = a.z >= near_z, inb = b.z >= near_z;
        if (ina) out[m++] = (dv3){ a.x, a.y, a.z };
        if (ina != inb) {
            const double k = (near_z - a.z) / ((double)b.z - a.z);
            out[m++] = (dv3){ a.x + k * ((double)b.x - a.x), a.y + k * ((double)b.y - a.y), near_z };
        }
    }
    return m;
}

/* Read once per picture in gpu_compose_begin, not once per record: an uncached getenv scans the whole environment block (about 1.6 microseconds a record at 36,000 to 70,000
 * records a picture was 59 of the 63 ms of CPU setup on the owner's late flight, 10 November 2026). */
static int g_smooth_sea = 1, g_square_pen = 1;

static int add_polygon(clist *l, const gpu_trace_rec *r, int w, int h)
{
    const int n = r->n;
    if (n < 3 || n > MAX_POLY - 2) return 1;
    dv3 c[MAX_POLY];
    const int m = clip_near(r->v, n, (double)r->near_z, c);
    if (m < 3) return 1;
    sv s[MAX_POLY];
    for (int i = 0; i < m; i++) {
        s[i].x = r->cx + r->sx * c[i].x / c[i].z;
        s[i].y = r->cy + r->sy * c[i].y / c[i].z;
        s[i].iz = (r->inv_depth_scale > 0.0 ? r->inv_depth_scale : 1.0) / c[i].z;
    }
    uint32_t kind = r->kind == REMASTER_T_LOGIC ? (uint32_t)r->op + 1u : 0u;
    if (kind == 2 && r->value == 0x80 && g_smooth_sea) kind = 6;
    const uint32_t cv = r->kind == REMASTER_T_LOGIC ? r->value : r->color;
    for (int i = 1; i + 1 < m; i++) if (!c_add(l, cv, r->paint_index, r->paint_owner, kind, s[0], s[i], s[i + 1], w, h)) return 0;
    return 1;
}

/* remaster_line's setup to its two triangles; sets *drawn when the CPU would go on to draw. */
static int add_stroke(clist *l, const gpu_trace_rec *r, int w, int h, uint32_t id, uint32_t *words, int *drawn)
{
    *drawn = 0;
    const remaster_v3 a = r->v[0], b = r->v[1];
    const double width_px = r->width;
    const int line_point = a.x == b.x && a.y == b.y && a.z == b.z;
    const double near_z = (double)r->near_z;
    double ax = a.x, ay = a.y, az = a.z, bx = b.x, by = b.y, bz = b.z;
    if (az < near_z && bz < near_z) return 1;
    if (az < near_z) { const double k = (near_z - az) / (bz - az); ax += k * (bx - ax); ay += k * (by - ay); az = near_z; }
    else if (bz < near_z) { const double k = (near_z - bz) / (az - bz); bx += k * (ax - bx); by += k * (ay - by); bz = near_z; }
    const double bias = r->line_bias > 0.0 ? r->line_bias : 1.5;
    const double scale = r->inv_depth_scale > 0.0 ? r->inv_depth_scale : 1.0;
    const sv sa = { r->cx + r->sx * ax / az, r->cy + r->sy * ay / az, bias * (scale / az) };
    const sv sb = { r->cx + r->sx * bx / bz, r->cy + r->sy * by / bz, bias * (scale / bz) };
    if ((sa.y + width_px < 0 && sb.y + width_px < 0) || (sa.y - width_px > h && sb.y - width_px > h) ||
        (sa.x + width_px < 0.0 && sb.x + width_px < 0.0) || (sa.x - width_px > w && sb.x - width_px > w)) return 1;
    int radius = (int)ceil(width_px / 2.0);
    if (radius > 4) radius = 4;
    double dx = sb.x - sa.x, dy = sb.y - sa.y;
    const double len = sqrt(dx * dx + dy * dy);
    const double dz = len > 1e-9 ? (sb.iz - sa.iz) / (len * len) : 0.0;
    const double depth_dx = dz * dx, depth_dy = dz * dy;
    if (len < 1e-9) { dx = 1.0; dy = 0.0; } else { dx /= len; dy /= len; }
    const double hw = width_px / 2.0;
    const double px = -dy * hw, py = dx * hw;
    const double ex = dx * hw, ey = dy * hw;
    const double cap_z = depth_dx * ex + depth_dy * ey;
    const sv q0 = { sa.x + px - ex, sa.y + py - ey, sa.iz - cap_z }, q1 = { sa.x - px - ex, sa.y - py - ey, sa.iz - cap_z };
    const sv q2 = { sb.x - px + ex, sb.y - py + ey, sb.iz + cap_z }, q3 = { sb.x + px + ex, sb.y + py + ey, sb.iz + cap_z };
    if (g_square_pen) {
        square_pen_point hull[16]; sv v[16];
        const int count = square_pen_hull(sa.x, sa.y, sb.x, sb.y, hw, hull);
        for (int k=0;k<count;k++) {
            v[k].x=hull[k].x; v[k].y=hull[k].y;
            v[k].iz=sa.iz+depth_dx*(v[k].x-sa.x)+depth_dy*(v[k].y-sa.y);
        }
        for (int k=1;k<count-1;k++) if (!c_add(l,id,0,0,0,v[0],v[k],v[k+1],w,h)) return 0;
    } else {
        if (!c_add(l, id, 0, 0, 0, q0, q1, q2, w, h) || !c_add(l, id, 0, 0, 0, q0, q2, q3, w, h)) return 0;
    }
    memset(words, 0, STROKE_WORDS * 4);
    words[0] = r->color; words[1] = r->paint_index; words[2] = r->paint_owner; words[3] = (uint32_t)line_point; words[4] = (uint32_t)radius;
    const float f[9] = { (float)sa.x, (float)sa.y, (float)sb.x, (float)sb.y, (float)depth_dx, (float)depth_dy, (float)sa.x, (float)sa.y, (float)sa.iz };
    for (int k = 0; k < 9; k++) memcpy(&words[5 + k], &f[k], 4);
    /* words 16 to 23: the segment's ends in double precision, for the decal rule's nearest point (stroke_hidden in strokes_x.hlsl), so it lands in the
     * same pixel as the CPU's decal_stroke_hidden, which works in doubles (6 November: two end pixels of near-clipped outlines differed) */
    const double ends[4] = { sa.x, sa.y, sb.x, sb.y };
    memcpy(&words[16], ends, sizeof ends);
    /* words 5 to 8 (the float copies of the ends above, unused since the ends are doubles): the depth slopes as doubles, for the point-against-edge correction
     * (remaster_raster.c: line_depth_dx * (aux_x - px) + line_depth_dy * (aux_y - py) in double, then to float; 7 November, far lights at camera depth 5.6e7) */
    memcpy(&words[5], &depth_dx, 8); memcpy(&words[7], &depth_dy, 8);
    *drawn = 1;
    return 1;
}

/* Bin the triangles into tiles in their order: offsets (tiles + 1 words) and entries (3 words each: triangle, the two weights at the tile's first pixel centre).
 * The reference form: every tile of each triangle's box is tested (the float weights, and in exact mode tile_touches_exact). Kept for the float path and as the
 * oracle of bin_tiles_fast (F117_BIN_CHECK=1 compares the two on every picture). */
static int bin_tiles_ref(const clist *l, int tilesx, int tilesy, vec *off_v, vec *ents_v, size_t *nents_out)
{
    const int ntiles = tilesx * tilesy;
    uint32_t *off = (uint32_t *)vec_reserve(off_v, ((size_t)ntiles + 1) * 4);
    if (!off) return 0;
    memset(off, 0, ((size_t)ntiles + 1) * 4);
    uint32_t *cursor = (uint32_t *)malloc(((size_t)ntiles + 1) * 4);
    if (!cursor) return 0;
    uint32_t *ents = NULL;
    size_t nents = 0;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            for (int k = 0; k < ntiles; k++) cursor[k] = off[k];
            ents = (uint32_t *)vec_reserve(ents_v, (nents ? nents : 1) * 36);
            if (!ents) { free(cursor); return 0; }
        }
        size_t count = 0;
        for (size_t ti = 0; ti < l->n; ti++) {
            const ctri *tr = &l->t[ti];
            const int tx0 = tr->bbox[0] / TILE, tx1 = tr->bbox[1] / TILE, ty0 = tr->bbox[2] / TILE, ty1 = tr->bbox[3] / TILE;
            for (int ty = ty0; ty <= ty1; ty++)
                for (int tx = tx0; tx <= tx1; tx++) {
                    const double ox = tx * TILE + 0.5, oy = ty * TILE + 0.5;
                    const double l1 = tr->a1d * (ox - tr->cx) + tr->b1d * (oy - tr->cy), l2 = tr->a2d * (ox - tr->cx) + tr->b2d * (oy - tr->cy);
                    double m1 = -1e300, m2 = -1e300, m3 = -1e300;
                    for (int cy2 = 0; cy2 < 2; cy2++)
                        for (int cx2 = 0; cx2 < 2; cx2++) {
                            const double ddx = cx2 * (TILE - 1.0), ddy = cy2 * (TILE - 1.0);
                            const double a = l1 + tr->a1d * ddx + tr->b1d * ddy, b = l2 + tr->a2d * ddx + tr->b2d * ddy, c3 = 1.0 - a - b;
                            if (a > m1) m1 = a;
                            if (b > m2) m2 = b;
                            if (c3 > m3) m3 = c3;
                        }
                    if (exact_on()) {
                        /* Exact mode: decide with the same integer edges the shader tests. Snapping moves a vertex by up to 1/512 px, so the float weights
                         * above can reject a tile whose first pixel the snapped triangle still covers (6 November: a water polygon's corner pixel, drawn by
                         * the CPU and missed by the GPU). An edge is linear, so if even its best tile corner is on the outside, no pixel of the tile is in. */
                        if (!tile_touches_exact(tr, tx, ty)) continue;
                    } else if (m1 < -1e-3 || m2 < -1e-3 || m3 < -1e-3) continue;
                    const int tile = ty * tilesx + tx;
                    if (pass == 0) { off[tile + 1]++; count++; }
                    else {
                        const uint32_t at = cursor[tile]++;
                        const float f1 = (float)l1, f2 = (float)l2;
                        uint32_t *en = &ents[9 * at];
                        en[0] = (uint32_t)ti; memcpy(&en[1], &f1, 4); memcpy(&en[2], &f2, 4);
                        put_exact_origin(en + 3, tr, tx, ty);
                    }
                }
        }
        if (pass == 0) { nents = count; for (int k = 0; k < ntiles; k++) off[k + 1] += off[k]; }
    }
    free(cursor);
    *nents_out = nents;
    return 1;
}

/* floor and ceiling of a / b for any signs (b != 0) */
static long long floor_div_ll(long long a, long long b)
{
    long long q = a / b;
    if (a % b != 0 && ((a < 0) != (b < 0))) q--;
    return q;
}
static long long ceil_div_ll(long long a, long long b) { return -floor_div_ll(-a, b); }

/* Exact mode: the tiles of row ty that tile_touches_exact accepts are an interval of columns. For an edge, the best value over a tile's four corners is
 * sdx * (py - yi) - sdy * (px - xi) at the corner whose offset (0 or 15 pixels, in x and in y) has the better sign of the two steps, the same corner for every tile;
 * with px = (tx * 16 + cx * 15) * 256 + 128 it is C0 - m * tx, linear in tx, so "not negative" is a half-line of columns, solved here with integer division.
 * *lo and *hi come in as the box's columns and go out as the accepted ones (hi < lo: none). Values stay below 2^60 (the guard band is 2^20 pixels). */
static void tile_row_span(const ctri *tr, int ty, int *lo, int *hi)
{
    const int pos = tr->area > 0;
    for (int k = 0; k < 3 && *lo <= *hi; k++) {
        const long long xi = tr->q[2 * k], yi = tr->q[2 * k + 1], xj = tr->q[(2 * k + 2) % 6], yj = tr->q[(2 * k + 3) % 6];
        const long long sdx = pos ? xj - xi : xi - xj, sdy = pos ? yj - yi : yi - yj;
        const int cy2 = sdx > 0 ? 1 : 0, cx2 = sdy < 0 ? 1 : 0;
        const long long py = ((long long)ty * TILE + cy2 * (TILE - 1)) * 256 + 128;
        const long long px0 = (long long)cx2 * (TILE - 1) * 256 + 128;
        const long long c0 = sdx * (py - yi) - sdy * (px0 - xi);
        const long long m = sdy * (TILE * 256);
        if (m > 0) {
            const long long h = floor_div_ll(c0, m);
            if (h < (long long)*hi) *hi = h < -1 ? -1 : (int)h;
        } else if (m < 0) {
            const long long c = ceil_div_ll(c0, m);
            if (c > (long long)*lo) *lo = c > 0x3FFFFFFF ? 0x3FFFFFFF : (int)c;
        } else if (c0 < 0) {
            *hi = *lo - 1;
        }
    }
}

/* Exact mode, the same entries and offsets as bin_tiles_ref, found by columns per row instead of testing every tile of the box. */
static int bin_tiles_fast(const clist *l, int tilesx, int tilesy, vec *off_v, vec *ents_v, size_t *nents_out)
{
    const int ntiles = tilesx * tilesy;
    uint32_t *off = (uint32_t *)vec_reserve(off_v, ((size_t)ntiles + 1) * 4);
    if (!off) return 0;
    memset(off, 0, ((size_t)ntiles + 1) * 4);
    uint32_t *cursor = (uint32_t *)malloc(((size_t)ntiles + 1) * 4);
    if (!cursor) return 0;
    uint32_t *ents = NULL;
    size_t nents = 0;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            for (int k = 0; k < ntiles; k++) cursor[k] = off[k];
            ents = (uint32_t *)vec_reserve(ents_v, (nents ? nents : 1) * 36);
            if (!ents) { free(cursor); return 0; }
        }
        size_t count = 0;
        for (size_t ti = 0; ti < l->n; ti++) {
            const ctri *tr = &l->t[ti];
            const int tx0 = tr->bbox[0] / TILE, tx1 = tr->bbox[1] / TILE, ty0 = tr->bbox[2] / TILE, ty1 = tr->bbox[3] / TILE;
            for (int ty = ty0; ty <= ty1; ty++) {
                int lo = tx0, hi = tx1;
                tile_row_span(tr, ty, &lo, &hi);
                for (int tx = lo; tx <= hi; tx++) {
                    const int tile = ty * tilesx + tx;
                    if (pass == 0) { off[tile + 1]++; count++; }
                    else {
                        const double ox = tx * TILE + 0.5, oy = ty * TILE + 0.5;
                        const double l1 = tr->a1d * (ox - tr->cx) + tr->b1d * (oy - tr->cy), l2 = tr->a2d * (ox - tr->cx) + tr->b2d * (oy - tr->cy);
                        const uint32_t at = cursor[tile]++;
                        const float f1 = (float)l1, f2 = (float)l2;
                        uint32_t *en = &ents[9 * at];
                        en[0] = (uint32_t)ti; memcpy(&en[1], &f1, 4); memcpy(&en[2], &f2, 4);
                        put_exact_origin(en + 3, tr, tx, ty);
                    }
                }
            }
        }
        if (pass == 0) { nents = count; for (int k = 0; k < ntiles; k++) off[k + 1] += off[k]; }
    }
    free(cursor);
    *nents_out = nents;
    return 1;
}

static int bin_tiles_threaded(const clist *l, int tilesx, int tilesy, vec *off_v, vec *ents_v, size_t *nents_out);

/* The binning used: the fast form in exact mode, the reference otherwise. F117_BIN_CHECK=1 also runs the reference and reports any difference in the offsets or the entries. */
static int bin_tiles(const clist *l, int tilesx, int tilesy, vec *off_v, vec *ents_v, size_t *nents_out)
{
    static int bin_ref = -1;            /* F117_BIN_REF=1: the reference binning (for paired A/B timing) */
    if (bin_ref < 0) bin_ref = getenv("F117_BIN_REF") != NULL;
    if (!exact_on() || bin_ref) return bin_tiles_ref(l, tilesx, tilesy, off_v, ents_v, nents_out);
    if (!getenv("F117_BIN_CHECK")) return bin_tiles_threaded(l, tilesx, tilesy, off_v, ents_v, nents_out);
    if (!bin_tiles_fast(l, tilesx, tilesy, off_v, ents_v, nents_out)) return 0;
    vec ro = { 0 }, re = { 0 };
    size_t n2 = 0;
    static unsigned checks, bad;
    const int ok = bin_tiles_ref(l, tilesx, tilesy, &ro, &re, &n2);
    const int same = ok && n2 == *nents_out && !memcmp(ro.p, off_v->p, ((size_t)tilesx * tilesy + 1) * 4) && !memcmp(re.p, ents_v->p, n2 * 36);
    checks++; if (!same) bad++;
    if (!same || checks % 500 == 0) fprintf(stderr, "[bin-check] %u lists checked, %u differ%s\n", checks, bad, same ? "" : " (this one)");
    free(ro.p); free(re.p);
    return 1;
}

static void tri_words(uint32_t *wd, const ctri *tr)
{
    memcpy(&wd[0], &tr->a1, 4); memcpy(&wd[1], &tr->b1, 4); memcpy(&wd[2], &tr->a2, 4); memcpy(&wd[3], &tr->b2, 4);
    memcpy(&wd[4], &tr->ciz, 4); memcpy(&wd[5], &tr->diz1, 4); memcpy(&wd[6], &tr->diz2, 4);
    put_exact_steps(wd + 12, tr);
    wd[25] = (uint32_t)(int32_t)tr->dox; wd[26] = (uint32_t)(int32_t)tr->doy;
    put64(wd + 27, tr->dbase); put64(wd + 29, tr->dpx); put64(wd + 31, tr->dpy);
}

/* one band triangle (the fast form's 24 words); 0 when the CPU draws nothing from it */
static int band_triangle(uint32_t *wd, const gpu_trace_rec *r, const sv s[3], const double rgb[3][3], int w, int h)
{
    const sv a = s[0], b = s[1], c = s[2];
    const double den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (fabs(den) < 1e-12) return 0;
    const double minx = fmin(a.x, fmin(b.x, c.x)), maxx = fmax(a.x, fmax(b.x, c.x));
    const double miny = fmin(a.y, fmin(b.y, c.y)), maxy = fmax(a.y, fmax(b.y, c.y));
    int x0 = (int)floor(minx - 0.5), x1 = (int)ceil(maxx - 0.5), y0 = (int)floor(miny - 0.5), y1 = (int)ceil(maxy - 0.5);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w - 1) x1 = w - 1;
    if (y1 > h - 1) y1 = h - 1;
    const float f[18] = { (float)((b.y - c.y) / den), (float)((c.x - b.x) / den), (float)((c.y - a.y) / den), (float)((a.x - c.x) / den), (float)c.x, (float)c.y,
                          (float)c.iz, (float)(a.iz - c.iz), (float)(b.iz - c.iz),
                          (float)(rgb[0][0] - rgb[2][0]), (float)(rgb[0][1] - rgb[2][1]), (float)(rgb[0][2] - rgb[2][2]),
                          (float)(rgb[1][0] - rgb[2][0]), (float)(rgb[1][1] - rgb[2][1]), (float)(rgb[1][2] - rgb[2][2]),
                          (float)rgb[2][0], (float)rgb[2][1], (float)rgb[2][2] };
    memset(wd, 0, 64 * 4);
    for (int k = 0; k < 18; k++) memcpy(&wd[k], &f[k], 4);
    {   /* exact form: the snapped vertices, the area's sign, the plane origin and the four planes (exact.h) */
        ex_tri et;
        if (!ex_setup(&et, a.x, a.y, b.x, b.y, c.x, c.y)) { if (exact_on()) return 0; }
        else {
            const double pv[EX_PLANES][3] = { { a.iz, b.iz, c.iz }, { rgb[0][0], rgb[1][0], rgb[2][0] }, { rgb[0][1], rgb[1][1], rgb[2][1] }, { rgb[0][2], rgb[1][2], rgb[2][2] } };
            ex_planes pl; ex_planes_make(&pl, &et, pv);
            for (int k = 0; k < 6; k++) put64(wd + 24 + 2 * k, et.q[k]);
            wd[36] = et.area > 0 ? 1u : 0u;
            wd[37] = (uint32_t)(int32_t)pl.ox; wd[38] = (uint32_t)(int32_t)pl.oy;
            for (int k = 0; k < EX_PLANES; k++) { put64(wd + 40 + 6 * k, (long long)pl.base[k]); put64(wd + 42 + 6 * k, (long long)pl.px[k]); put64(wd + 44 + 6 * k, (long long)pl.py[k]); }
        }
    }
    wd[18] = r->paint_index;
    wd[19] = (uint32_t)x0; wd[20] = (uint32_t)x1; wd[21] = (uint32_t)y0; wd[22] = (uint32_t)y1;
    return 1;
}

typedef struct { gbuf *b; const void *data; Uint32 bytes; SDL_GPUBufferUsageFlags usage; } seg;

/* F117_GPU_PASS_TIMES=1 (diagnostic): each compute pass of the begin step is submitted on its own and waited for, so its wall time (GPU work plus one
 * submit and fence) is known; the means are printed every 120 pictures. The sum is longer than the normal run, which overlaps the passes. */
/* F117_DECAL_SIMPLE: on by default (Remastered GPU compositor); =0 restores the exact decal rule. The strokes shader then skips the neighbourhood walk of the rule (not exact: about 216 of 8.3 million pixels
 * differ on the late flight at 4K), which cuts the strokes pass from about 11.9 to 3.0 ms. docs/gpu-compositor.md. */
static Uint32 decal_simple_on(void) { static int on = -1; if (on < 0) { const char *e = getenv("F117_DECAL_SIMPLE"); on = !(e && e[0] == '0'); } return (Uint32)on; }
static int pass_times_on(void) { static int on = -1; if (on < 0) on = getenv("F117_GPU_PASS_TIMES") != NULL; return on; }
static void pass_split(gpu_compose *g, SDL_GPUCommandBuffer **cb, int idx, Uint64 *mark)
{
    static double sum[6]; static unsigned n[6]; static const char *name[6] = { "upload", "bands", "polygons", "strokes (points)", "strokes (pixels)", "-" };
    SDL_GPUFence *f = SDL_SubmitGPUCommandBufferAndAcquireFence(*cb);
    SDL_WaitForGPUFences(g->dev, true, &f, 1);
    SDL_ReleaseGPUFence(g->dev, f);
    const Uint64 now = SDL_GetTicksNS();
    sum[idx] += (double)(now - *mark) / 1e6; n[idx]++;
    *mark = now;
    *cb = SDL_AcquireGPUCommandBuffer(g->dev);
    if (idx == 4 && n[4] % 120 == 0) {
        fprintf(stderr, "[gpu-pass] mean of %u pictures (ms, each pass on its own): ", n[4]);
        for (int k = 0; k < 5; k++) if (n[k]) fprintf(stderr, "%s %.2f%s", name[k], sum[k] / n[k], k < 4 ? ", " : "\n");
    }
}


/* ---- The CPU setup on several threads (10 November 2026) -------------------------------------------------------------------------------------------------
 * The classify loop turns each polygon and line record into triangles independently; only their order matters. The records are split into SETUP_CHUNKS
 * contiguous ranges, each range builds its own lists, and the lists are merged in range order (a stroke's id, which the shader indexes by, is the number of
 * strokes drawn before it, so each range's ids are shifted by the strokes of the earlier ranges), which is exactly the serial order. F117_SETUP_THREADS=1 runs
 * the serial loop (the reference); F117_SETUP_CHECK=1 runs both every picture and compares the lists and the stroke words byte for byte. */
typedef void (*pool_fn)(void *arg, int idx);
typedef struct {
    SDL_Thread *th[32]; int nworkers;
    SDL_Semaphore *go, *done;
    pool_fn fn; void *arg; int njobs;
    SDL_AtomicInt next, quit;
} setup_pool;
static setup_pool *g_pool[GSETS];       /* one fork-join pool per set: the setups of two pictures can run at once */
static int g_setup_threads = -1;

static int pool_worker(void *u)
{
    setup_pool *p = (setup_pool *)u;
    { static int on = -1; if (on < 0) on = getenv("F117_THREAD_PRIO") != NULL && atoi(getenv("F117_THREAD_PRIO")) != 0; if (on) SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH); }   /* F117_THREAD_PRIO, as in main.c */
    for (;;) {
        SDL_WaitSemaphore(p->go);
        if (SDL_GetAtomicInt(&p->quit)) break;
        int j;
        while ((j = SDL_AddAtomicInt(&p->next, 1)) < p->njobs) p->fn(p->arg, j);
        SDL_SignalSemaphore(p->done);
    }
    return 0;
}

static void setup_pool_shutdown(void)
{
    for (int si = 0; si < GSETS; si++) {
        setup_pool *p = g_pool[si];
        if (!p) continue;
        g_pool[si] = NULL;
        SDL_SetAtomicInt(&p->quit, 1);
        for (int i = 0; i < p->nworkers; i++) SDL_SignalSemaphore(p->go);
        for (int i = 0; i < p->nworkers; i++) SDL_WaitThread(p->th[i], NULL);
        SDL_DestroySemaphore(p->go); SDL_DestroySemaphore(p->done);
        free(p);
    }
}

/* How many threads the setup uses (including the caller): F117_SETUP_THREADS, default 8 (at most half the logical cores), 1 = serial. */
static int setup_threads(void)
{
    if (g_setup_threads >= 0) return g_setup_threads;
    int n = SDL_GetNumLogicalCPUCores() / 2; if (n > 8) n = 8; if (n < 1) n = 1;
    const char *e = getenv("F117_SETUP_THREADS");
    if (e && *e) n = atoi(e);
    if (n > 32) n = 32;
    if (n < 1 || getenv("F117_BIN_CHECK")) n = 1;      /* the check modes keep static counters */
    g_setup_threads = n;
    return n;
}

static void pool_run(pool_fn fn, void *arg, int njobs)
{
    const int nt = setup_threads();
    setup_pool **pp = &g_pool[g_set_tls];
    if (nt > 1 && !*pp) {
        setup_pool *p = (setup_pool *)calloc(1, sizeof *p);
        if (p) {
            p->go = SDL_CreateSemaphore(0); p->done = SDL_CreateSemaphore(0);
            for (int i = 0; i < nt - 1 && p->go && p->done; i++) { p->th[i] = SDL_CreateThread(pool_worker, "setup", p); if (!p->th[i]) break; p->nworkers++; }
            *pp = p;
        }
    }
    setup_pool *p = *pp;
    if (!p || p->nworkers == 0) { for (int j = 0; j < njobs; j++) fn(arg, j); return; }
    p->fn = fn; p->arg = arg; p->njobs = njobs;
    SDL_SetAtomicInt(&p->next, 0);
    for (int i = 0; i < p->nworkers; i++) SDL_SignalSemaphore(p->go);
    int j;
    while ((j = SDL_AddAtomicInt(&p->next, 1)) < njobs) fn(arg, j);
    for (int i = 0; i < p->nworkers; i++) SDL_WaitSemaphore(p->done);
}

/* ---- The binning on several threads (14 November 2026) ---------------------------------------------------------------------------------------------------
 * bin_tiles_fast is a two-pass counting sort in triangle order. Here the triangles are split into BIN_CHUNKS contiguous ranges: each range counts its own tiles
 * (pass 0), the counts of the ranges are turned into each range's start inside every tile (the ranges in order, so a tile's entries stay in triangle order: the
 * serial order exactly), and each range then writes its entries (pass 1). F117_SETUP_THREADS=1 and short lists run bin_tiles_fast; F117_SETUP_CHECK=1 compares
 * the offsets and the entries of both byte for byte on every call. */
#define BIN_CHUNKS 16
typedef struct {
    const clist *l; int tilesx, ntiles; uint32_t *off; uint32_t *cnt; uint32_t *ents; size_t per; int pass;
} bin_ctx;

static void bin_job(void *vc, int k)
{
    bin_ctx *c = (bin_ctx *)vc; const clist *l = c->l;
    uint32_t *cnt = c->cnt + (size_t)k * c->ntiles;
    const size_t a = (size_t)k * c->per, b = a + c->per < l->n ? a + c->per : l->n;
    if (c->pass == 0) memset(cnt, 0, (size_t)c->ntiles * 4);
    for (size_t ti = a; ti < b; ti++) {
        const ctri *tr = &l->t[ti];
        const int tx0 = tr->bbox[0] / TILE, tx1 = tr->bbox[1] / TILE, ty0 = tr->bbox[2] / TILE, ty1 = tr->bbox[3] / TILE;
        for (int ty = ty0; ty <= ty1; ty++) {
            int lo = tx0, hi = tx1;
            tile_row_span(tr, ty, &lo, &hi);
            for (int tx = lo; tx <= hi; tx++) {
                const int tile = ty * c->tilesx + tx;
                if (c->pass == 0) cnt[tile]++;
                else {
                    const double ox = tx * TILE + 0.5, oy = ty * TILE + 0.5;
                    const double l1 = tr->a1d * (ox - tr->cx) + tr->b1d * (oy - tr->cy), l2 = tr->a2d * (ox - tr->cx) + tr->b2d * (oy - tr->cy);
                    const uint32_t at = c->off[tile] + cnt[tile]++;
                    const float f1 = (float)l1, f2 = (float)l2;
                    uint32_t *en = &c->ents[9 * (size_t)at];
                    en[0] = (uint32_t)ti; memcpy(&en[1], &f1, 4); memcpy(&en[2], &f2, 4);
                    put_exact_origin(en + 3, tr, tx, ty);
                }
            }
        }
    }
}

/* per tile block: the chunks' counts become each chunk's start inside the tile, and the tile's total goes to off[tile + 1] */
static void bin_prefix_job(void *vc, int blk)
{
    bin_ctx *c = (bin_ctx *)vc;
    const int per = (c->ntiles + 31) / 32, t0 = blk * per, t1 = t0 + per < c->ntiles ? t0 + per : c->ntiles;
    for (int t = t0; t < t1; t++) c->off[t + 1] = 0;
    for (int k = 0; k < BIN_CHUNKS; k++) {
        uint32_t *cnt = c->cnt + (size_t)k * c->ntiles;
        for (int t = t0; t < t1; t++) { const uint32_t n = cnt[t]; cnt[t] = c->off[t + 1]; c->off[t + 1] += n; }
    }
}

static int bin_tiles_threaded(const clist *l, int tilesx, int tilesy, vec *off_v, vec *ents_v, size_t *nents_out)
{
    if (setup_threads() <= 1 || l->n < 2048) return bin_tiles_fast(l, tilesx, tilesy, off_v, ents_v, nents_out);
    const int ntiles = tilesx * tilesy;
    static uint32_t *cnt_s[GSETS]; static size_t cnt_cap_s[GSETS];
    uint32_t *cnt = cnt_s[g_set_tls]; size_t cnt_cap = cnt_cap_s[g_set_tls];
    if (cnt_cap < (size_t)BIN_CHUNKS * ntiles) {
        uint32_t *q = (uint32_t *)realloc(cnt, (size_t)BIN_CHUNKS * ntiles * 4);
        if (!q) return bin_tiles_fast(l, tilesx, tilesy, off_v, ents_v, nents_out);
        cnt = q; cnt_cap = (size_t)BIN_CHUNKS * ntiles;
        cnt_s[g_set_tls] = cnt; cnt_cap_s[g_set_tls] = cnt_cap;
    }
    uint32_t *off = (uint32_t *)vec_reserve(off_v, ((size_t)ntiles + 1) * 4);
    if (!off) return 0;
    bin_ctx c; memset(&c, 0, sizeof c);
    c.l = l; c.tilesx = tilesx; c.ntiles = ntiles; c.off = off; c.cnt = cnt; c.ents = NULL;
    c.per = (l->n + BIN_CHUNKS - 1) / BIN_CHUNKS;
    off[0] = 0;
    c.pass = 0; pool_run(bin_job, &c, BIN_CHUNKS);
    pool_run(bin_prefix_job, &c, 32);
    for (int t = 0; t < ntiles; t++) off[t + 1] += off[t];
    const size_t nents = off[ntiles];
    uint32_t *ents = (uint32_t *)vec_reserve(ents_v, (nents ? nents : 1) * 36);
    if (!ents) return 0;
    c.ents = ents; c.pass = 1; pool_run(bin_job, &c, BIN_CHUNKS);
    *nents_out = nents;
    static int check = -1;
    if (check < 0) check = getenv("F117_SETUP_CHECK") != NULL;
    if (check) {
        static unsigned n, bad; vec ro = { 0 }, re = { 0 }; size_t n2 = 0;
        const int ok = bin_tiles_fast(l, tilesx, tilesy, &ro, &re, &n2);
        const int same = ok && n2 == nents && !memcmp(ro.p, off, ((size_t)ntiles + 1) * 4) && !memcmp(re.p, ents, n2 * 36);
        n++; if (!same) bad++;
        if (!same && bad < 4) fprintf(stderr, "[setup-check] binning differs (call %u, %zu vs %zu entries)\n", n, n2, nents);
        if (n % 240 == 0) fprintf(stderr, "[setup-check] %u binnings checked, %u differ\n", n, bad);
        free(ro.p); free(re.p);
    }
    return 1;
}

static int clist_reserve(clist *l, size_t n)
{
    if (l->cap >= n) return 1;
    size_t cap = l->cap ? l->cap : 1024; while (cap < n) cap *= 2;
    ctri *q = (ctri *)realloc(l->t, cap * sizeof *q);
    if (!q) return 0;
    l->t = q; l->cap = cap;
    return 1;
}

/* The serial loop over the polygon and line records (bands are handled by the caller): the reference. */
static int classify_serial(clist *pl, clist *sl, uint32_t *sw, uint32_t *nstrokes_out, const gpu_compose_in *in, int w, int h)
{
    uint32_t nstrokes = 0;
    for (uint32_t i = 0; i < in->nrec; i++) {
        const gpu_trace_rec *r = &in->recs[i];
        if (r->kind == REMASTER_T_POLYGON || r->kind == REMASTER_T_LOGIC) {
            const unsigned long long cp = CYC();
            if (!add_polygon(pl, r, w, h)) return 0;
            CADD(g_cyc, 1, CYC() - cp); CADD(g_cnt, 1, 1);
        } else if (r->kind == REMASTER_T_LINE) {
            int drawn = 0;
            const unsigned long long cs = CYC();
            if (!add_stroke(sl, r, w, h, nstrokes, sw + (size_t)nstrokes * STROKE_WORDS, &drawn)) return 0;
            CADD(g_cyc, 2, CYC() - cs); CADD(g_cnt, 2, 1);
            if (drawn) nstrokes++;
        }
    }
    *nstrokes_out = nstrokes;
    return 1;
}

typedef struct {
    gpu_compose *g; gscratch *sc; const gpu_compose_in *in; int w, h;
    uint32_t per;                       /* records a chunk */
    volatile int fail;
    uint32_t drawn[SETUP_CHUNKS], sofs[SETUP_CHUNKS];       /* strokes drawn in a chunk; strokes drawn before it */
    size_t pofs[SETUP_CHUNKS], tofs[SETUP_CHUNKS];          /* polygon and stroke triangles before the chunk */
    uint32_t *sw;
} cls_ctx;

static Uint64 g_chunk_ns[SETUP_CHUNKS];   /* F117_CLASSIFY_SPLIT diagnostic: each chunk's own time */
static void classify_job_body(void *vc, int k);
static void classify_job(void *vc, int k) { classify_job_body(vc, k); SDL_SetAtomicInt(&((cls_ctx *)vc)->sc->cdone[k], 1); }
static void classify_job_body(void *vc, int k)
{
    cls_ctx *c = (cls_ctx *)vc; gscratch *sc = c->sc; const gpu_compose_in *in = c->in;
    /* Part B (14 November 2026): the chunk's list headers and its stroke count live in locals and are stored once at the end. The 32 headers (sc->cpoly[k], sc->cstroke[k])
     * and c->drawn[k] sit next to each other in memory, and every triangle and stroke wrote them from a different thread: false sharing, which kept the classify no
     * faster on 8 threads than on one. */
    clist lp = sc->cpoly[k], ls = sc->cstroke[k]; lp.n = 0; ls.n = 0;
    clist *pl = &lp, *sl = &ls; uint32_t drawn_local = 0;
    sc->cpoly[k].n = 0; sc->cstroke[k].n = 0; c->drawn[k] = 0;
    const uint64_t a64 = (uint64_t)k * c->per;
    if (a64 >= in->nrec) return;
    const uint32_t a = (uint32_t)a64, b = (uint32_t)(a64 + c->per < in->nrec ? a64 + c->per : in->nrec);
    const Uint64 t_job0 = SDL_GetTicksNS(); (void)t_job0;
    uint32_t *words = (uint32_t *)vec_reserve(&sc->cword[k], (size_t)(b - a) * STROKE_WORDS * 4);
    if (!words) { c->fail = 1; return; }
    for (uint32_t i = a; i < b; i++) {
        const gpu_trace_rec *r = &in->recs[i];
        if (r->kind == REMASTER_T_POLYGON || r->kind == REMASTER_T_LOGIC) {
            const unsigned long long cp = CYC();
            if (!add_polygon(pl, r, c->w, c->h)) { c->fail = 1; break; }
            CADD(g_cyc, 1, CYC() - cp); CADD(g_cnt, 1, 1);
        } else if (r->kind == REMASTER_T_LINE) {
            int drawn = 0;
            const unsigned long long cs = CYC();
            if (!add_stroke(sl, r, c->w, c->h, drawn_local, words + (size_t)drawn_local * STROKE_WORDS, &drawn)) { c->fail = 1; break; }
            CADD(g_cyc, 2, CYC() - cs); CADD(g_cnt, 2, 1);
            if (drawn) drawn_local++;
        }
    }
    sc->cpoly[k] = lp; sc->cstroke[k] = ls; c->drawn[k] = drawn_local;      /* one store each (the lists may have grown) */
    g_chunk_ns[k] = SDL_GetTicksNS() - t_job0;
}

static void merge_job(void *vc, int k)
{
    cls_ctx *c = (cls_ctx *)vc; gscratch *sc = c->sc;
    const clist *pl = &sc->cpoly[k], *sl = &sc->cstroke[k];
    if (pl->n) memcpy(sc->polys.t + c->pofs[k], pl->t, pl->n * sizeof(ctri));
    for (size_t j = 0; j < sl->n; j++) { ctri *d = &sc->strokes.t[c->tofs[k] + j]; *d = sl->t[j]; d->w7 += c->sofs[k]; }
    if (c->drawn[k]) memcpy(c->sw + (size_t)c->sofs[k] * STROKE_WORDS, sc->cword[k].p, (size_t)c->drawn[k] * STROKE_WORDS * 4);
}

/* The classify of polygons and lines, into SC(g)->polys, SC(g)->strokes and sw (nrec * STROKE_WORDS words). */
static int classify_records(gpu_compose *g, const gpu_compose_in *in, int w, int h, uint32_t *sw, uint32_t *nstrokes_out)
{
    const int nt = setup_threads();
    if (nt <= 1 || in->nrec < 2048) return classify_serial(&SC(g)->polys, &SC(g)->strokes, sw, nstrokes_out, in, w, h);
    cls_ctx c; memset(&c, 0, sizeof c);
    c.g = g; c.sc = SC(g); c.in = in; c.w = w; c.h = h; c.fail = 0; c.sw = sw;
    c.per = (in->nrec + SETUP_CHUNKS - 1) / SETUP_CHUNKS;
    SC(g)->faces_early = 0;
    if (decal_faces_enabled()) { for (int k = 0; k < SETUP_CHUNKS; k++) SDL_SetAtomicInt(&SC(g)->cdone[k], 0); SC(g)->faces_early = faces_start_chunks(g); }
    const Uint64 t_pool0 = SDL_GetTicksNS();
    pool_run(classify_job, &c, SETUP_CHUNKS);
    {   static int on = -1; static unsigned n; static double sp, ss, sm, sx;
        if (on < 0) on = getenv("F117_CLASSIFY_SPLIT") != NULL;
        if (on) {
            double sum = 0, mx = 0; for (int k = 0; k < SETUP_CHUNKS; k++) { const double ms = g_chunk_ns[k] / 1e6; sum += ms; if (ms > mx) mx = ms; }
            sp += (SDL_GetTicksNS() - t_pool0) / 1e6; ss += sum; sm += mx; n++;
            if (n % 60 == 0) fprintf(stderr, "[classify-split] %u: pool wall %.2f ms, chunk sum %.2f, longest chunk %.2f\n", n, sp / n, ss / n, sm / n);
        }
    }
    if (c.fail) return 0;
    size_t np = 0, nsz = 0; uint32_t nd = 0;
    for (int k = 0; k < SETUP_CHUNKS; k++) {
        c.pofs[k] = np; c.tofs[k] = nsz; c.sofs[k] = nd;
        np += SC(g)->cpoly[k].n; nsz += SC(g)->cstroke[k].n; nd += c.drawn[k];
    }
    if (!clist_reserve(&SC(g)->polys, np) || !clist_reserve(&SC(g)->strokes, nsz)) return 0;
    SC(g)->polys.n = np; SC(g)->strokes.n = nsz;
    pool_run(merge_job, &c, SETUP_CHUNKS);
    *nstrokes_out = nd;
    static int check = -1;
    if (check < 0) check = getenv("F117_SETUP_CHECK") != NULL;
    if (check) {                         /* the serial result, compared byte for byte */
        static unsigned n, bad; clist rp = { 0 }, rs = { 0 }; uint32_t rn = 0;
        uint32_t *rw = (uint32_t *)malloc((size_t)(in->nrec ? in->nrec : 1) * STROKE_WORDS * 4);
        int ok = rw && classify_serial(&rp, &rs, rw, &rn, in, w, h);
        if (ok) ok = rp.n == SC(g)->polys.n && rs.n == SC(g)->strokes.n && rn == nd &&
                     (!rp.n || !memcmp(rp.t, SC(g)->polys.t, rp.n * sizeof(ctri))) && (!rs.n || !memcmp(rs.t, SC(g)->strokes.t, rs.n * sizeof(ctri))) &&
                     (!rn || !memcmp(rw, sw, (size_t)rn * STROKE_WORDS * 4));
        n++; if (!ok) bad++;
        if (!ok && bad < 4) fprintf(stderr, "[setup-check] differs at picture %u (polys %zu vs %zu, strokes %zu vs %zu, drawn %u vs %u)\n", n, rp.n, SC(g)->polys.n, rs.n, SC(g)->strokes.n, rn, nd);
        if (n % 120 == 0) fprintf(stderr, "[setup-check] %u pictures checked, %u differ\n", n, bad);
        free(rp.t); free(rs.t); free(rw);
    }
    return 1;
}

#define UP_PIECE ((size_t)1 << 20)
typedef struct { Uint8 *dst; const Uint8 *src; size_t n; } up_piece;
typedef struct { up_piece *p; } up_ctx;
static void up_job(void *vc, int k) { const up_piece *q = &((up_ctx *)vc)->p[k]; memcpy(q->dst, q->src, q->n); }

/* Job 0 of the pack pool builds the decal face list (serial by nature: the chains and the dedup depend on the insertion order, so it stays one loop in triangle
 * order); the pack chunks run beside it on the other threads (14 November 2026). It reads SC(g)->polys and writes only SC(g)->faces; the chunks write only pw and stw. */
typedef struct { gscratch *sc; uint32_t *pw, *stw; int faces, faces_async; } pack_ctx;
static void faces_add_tri(gscratch *sc, const ctri *tr)
{
    if (tr->kind) return; /* logic fills do not write surface depth */
    ex_tri tri; memcpy(tri.q, tr->q, sizeof tri.q); tri.area = tr->area;
    ex_planes plane = {0}; plane.ox = tr->dox; plane.oy = tr->doy;
    plane.base[0] = (uint64_t)tr->dbase; plane.px[0] = (uint64_t)tr->dpx; plane.py[0] = (uint64_t)tr->dpy;
    df_add(&sc->faces, tr->paint_owner, &tri, &plane);
}
static void faces_build(gscratch *sc)
{
    df_reset(&sc->faces);
    for (size_t k = 0; k < sc->polys.n; ++k) faces_add_tri(sc, &sc->polys.t[k]);
}
/* The same list built from the classify's chunk lists (cpoly[0..SETUP_CHUNKS-1], whose concatenation in order is polys), each as soon as its chunk is finished, so
 * the work overlaps the classify. Same triangles in the same order: the same list. */
static void faces_build_chunks(gscratch *sc)
{
    df_reset(&sc->faces);
    for (int k = 0; k < SETUP_CHUNKS; k++) {
        while (!SDL_GetAtomicInt(&sc->cdone[k])) SDL_CPUPauseInstruction();
        const clist *l = &sc->cpoly[k];
        for (size_t j = 0; j < l->n; j++) faces_add_tri(sc, &l->t[j]);
    }
}
/* Part B (16 November 2026): the decal face list is one serial loop in triangle order (5.5 ms at the end of the flight), and it needs only the classified polygons, so
 * it runs on a thread of its own from the end of the classify, beside the binning and the pack chunks. Same loop, same list, byte for byte. One thread per scratch set: a
 * call that returns early leaves it running, and the next call on the set (or the destroy) waits for it before touching the lists. */
typedef struct { gscratch *sc; } fth_arg;
static fth_arg g_fth_arg[GSETS];
static int faces_thread_main(void *u)
{
    gscratch *sc = ((fth_arg *)u)->sc;
    for (;;) {
        SDL_WaitSemaphore(sc->fth.go);
        if (sc->fth.quit) break;
        if (sc->fth.chunks) faces_build_chunks(sc); else faces_build(sc);
        SDL_SignalSemaphore(sc->fth.done);
    }
    return 0;
}
static void faces_wait(gscratch *sc) { if (sc->fth.running) { SDL_WaitSemaphore(sc->fth.done); sc->fth.running = 0; } }
static int faces_start_mode(gpu_compose *g, int chunks)
{
    gscratch *sc = SC(g);
    faces_wait(sc);
    sc->fth.chunks = chunks;
    if (!sc->fth.th) {
        sc->fth.go = SDL_CreateSemaphore(0); sc->fth.done = SDL_CreateSemaphore(0);
        g_fth_arg[g_set_tls].sc = sc;
        if (sc->fth.go && sc->fth.done) sc->fth.th = SDL_CreateThread(faces_thread_main, "faces", &g_fth_arg[g_set_tls]);
        if (!sc->fth.th) return 0;
    }
    sc->fth.running = 1;
    SDL_SignalSemaphore(sc->fth.go);
    return 1;
}
static int faces_start(gpu_compose *g) { return faces_start_mode(g, 0); }
static int faces_start_chunks(gpu_compose *g) { return setup_threads() > 1 && faces_start_mode(g, 1); }
static void faces_thread_shutdown(gpu_compose *g)
{
    for (int si = 0; si < GSETS; si++) {
        gscratch *sc = &g->set[si].sc;
        faces_wait(sc);
        if (!sc->fth.th) continue;
        sc->fth.quit = 1; SDL_SignalSemaphore(sc->fth.go); SDL_WaitThread(sc->fth.th, NULL); sc->fth.th = NULL;
        SDL_DestroySemaphore(sc->fth.go); SDL_DestroySemaphore(sc->fth.done); sc->fth.go = sc->fth.done = NULL; sc->fth.quit = 0;
    }
}
static void pack_job(void *vc, int k)
{
    const pack_ctx *c = (const pack_ctx *)vc;
    if (k == 0) { if (c->faces && !c->faces_async) faces_build(c->sc); return; }
    k--;
    const size_t np = c->sc->polys.n, ns = c->sc->strokes.n;
    const size_t p0 = np * k / SETUP_CHUNKS, p1 = np * (k + 1) / SETUP_CHUNKS, s0 = ns * k / SETUP_CHUNKS, s1 = ns * (k + 1) / SETUP_CHUNKS;
    if (p1 > p0) memset(c->pw + p0 * 40, 0, (p1 - p0) * 160);
    if (s1 > s0) memset(c->stw + s0 * 40, 0, (s1 - s0) * 160);
    for (size_t j = p0; j < p1; j++) {
        const ctri *tr = &c->sc->polys.t[j]; uint32_t *wd = c->pw + j * 40;
        tri_words(wd, tr); wd[7] = tr->w7; wd[8] = tr->paint_index; wd[9] = tr->paint_owner; wd[10] = tr->kind;
    }
    for (size_t j = s0; j < s1; j++) { const ctri *tr = &c->sc->strokes.t[j]; uint32_t *wd = c->stw + j * 40; tri_words(wd, tr); wd[7] = tr->w7; }
}

/* ---- The hardware-rasterised composite (F117_HW_RASTER=1; owner's decision of 17 November 2026: pictures may differ from the exact compositor by a pixel here and there).
 * The records the compute path classifies are drawn here with the graphics pipeline: polygons with a depth buffer, the AND / OR / stipple fills as blend MIN / MAX / 0.5 on index
 * bit planes, the strokes as quads, then a resolve pass (palette lookup, the sea's smoothing) and an export pass into the pixel buffers the replace step reads. Same inputs and
 * outputs as gpu_compose_begin's compute passes; docs/gpu-compositor.md, "the hardware path". The rules still approximated: the ground rule (owner bit 11), the same-object decal
 * tolerance, the neighbour rules of strokes. */
typedef struct { float x, y, d, c[4]; uint32_t idx, own, dith; } hw_vtx;
enum { HK_POLY, HK_LINE, HK_ZRESET, HK_AND, HK_OR, HK_STIP, HK_N };
typedef hw_seg_t hw_seg;
#define HW_SEGS 8192
static int hw_on(void) { static int on = -1; if (on < 0) { const char *e = getenv("F117_HW_RASTER"); on = e && e[0] && e[0] != '0'; } return on; }

static SDL_GPUShader *hw_shader(SDL_GPUDevice *d, const char *dir, const char *name, SDL_GPUShaderStage st, const char *entry, Uint32 samplers)
{
    char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, name);
    size_t sz = 0; void *code = SDL_LoadFile(path, &sz);
    if (!code) return NULL;
    SDL_GPUShaderCreateInfo ci; memset(&ci, 0, sizeof ci);
    ci.code = (const Uint8 *)code; ci.code_size = sz; ci.entrypoint = entry; ci.format = SDL_GPU_SHADERFORMAT_DXIL; ci.stage = st; ci.num_samplers = samplers;
    SDL_GPUShader *s = SDL_CreateGPUShader(d, &ci);
    SDL_free(code);
    return s;
}
enum { HB_OVR, HB_NONE, HB_MIN, HB_MAX, HB_STIP, HB_ADD };
static SDL_GPUColorTargetDescription hw_target(SDL_GPUTextureFormat f, int b)
{
    SDL_GPUColorTargetDescription t; memset(&t, 0, sizeof t); t.format = f;
    SDL_GPUColorTargetBlendState *s = &t.blend_state;
    if (b == HB_NONE) { s->enable_color_write_mask = true; s->color_write_mask = 0; }
    else if (b == HB_MIN || b == HB_MAX || b == HB_ADD) {
        s->enable_blend = true; s->src_color_blendfactor = s->src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE; s->dst_color_blendfactor = s->dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        s->color_blend_op = s->alpha_blend_op = b == HB_MIN ? SDL_GPU_BLENDOP_MIN : b == HB_ADD ? SDL_GPU_BLENDOP_ADD : SDL_GPU_BLENDOP_MAX;
    } else if (b == HB_STIP) {
        s->enable_blend = true; s->src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA; s->dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; s->color_blend_op = SDL_GPU_BLENDOP_ADD;
        s->src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE; s->dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO; s->alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }
    return t;
}

/* the pipelines, once; 0 when a shader or pipeline is missing (the caller then uses the compute path) */
static int hw_init(gpu_compose *g)
{
    if (g->hw.tried) return g->hw.ok;
    g->hw.tried = 1;
    SDL_GPUDevice *d = g->dev; const char *dir = g->shader_dir;
    SDL_GPUShader *vs = hw_shader(d, dir, "gpu_hw_vs.dxil", SDL_GPU_SHADERSTAGE_VERTEX, "vs", 0), *ps = hw_shader(d, dir, "gpu_hw_ps.dxil", SDL_GPU_SHADERSTAGE_FRAGMENT, "ps", 0);
    SDL_GPUShader *rvs = hw_shader(d, dir, "gpu_hw_rvs.dxil", SDL_GPU_SHADERSTAGE_VERTEX, "rvs", 0), *rps = hw_shader(d, dir, "gpu_hw_rps.dxil", SDL_GPU_SHADERSTAGE_FRAGMENT, "rps", 5);
    if (!vs || !ps || !rvs || !rps) { fprintf(stderr, "[hw-raster] shaders missing in %s: the compute path is used\n", dir); return 0; }
    char path[1024]; snprintf(path, sizeof path, "%s/gpu_hw_export.dxil", dir);
    size_t sz = 0; void *code = SDL_LoadFile(path, &sz);
    if (code) {
        SDL_GPUComputePipelineCreateInfo ci; memset(&ci, 0, sizeof ci);
        ci.code = (const Uint8 *)code; ci.code_size = sz; ci.entrypoint = "main"; ci.format = SDL_GPU_SHADERFORMAT_DXIL;
        ci.num_readonly_storage_textures = 2; ci.num_readwrite_storage_buffers = 3; ci.num_uniform_buffers = 1; ci.threadcount_x = 16; ci.threadcount_y = 16; ci.threadcount_z = 1;
        g->hw.pexport = SDL_CreateGPUComputePipeline(d, &ci);
        SDL_free(code);
    }
    if (!g->hw.pexport) { fprintf(stderr, "[hw-raster] the export shader is missing or refused: the compute path is used\n"); return 0; }
    SDL_GPUVertexBufferDescription vb = { 0, sizeof(hw_vtx), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 };
    SDL_GPUVertexAttribute at[6] = { { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0 }, { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, 8 }, { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 12 },
                                     { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_UINT, 28 }, { 4, 0, SDL_GPU_VERTEXELEMENTFORMAT_UINT, 32 }, { 5, 0, SDL_GPU_VERTEXELEMENTFORMAT_UINT, 36 } };
    const SDL_GPUTextureFormat FC = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    static const int blends[HK_N][4] = { { HB_OVR, HB_OVR, HB_OVR, HB_OVR }, { HB_OVR, HB_OVR, HB_OVR, HB_OVR }, { 0, 0, 0, 0 },
                                         { HB_NONE, HB_MIN, HB_MIN, HB_ADD }, { HB_NONE, HB_MAX, HB_MAX, HB_ADD }, { HB_STIP, HB_NONE, HB_NONE, HB_OVR } };
    for (int k = 0; k < HK_N; k++) {
        if (k == HK_ZRESET) continue;
        SDL_GPUColorTargetDescription ct[4]; for (int j = 0; j < 4; j++) ct[j] = hw_target(FC, blends[k][j]);
        SDL_GPUGraphicsPipelineCreateInfo pi; memset(&pi, 0, sizeof pi);
        pi.vertex_shader = vs; pi.fragment_shader = ps;
        pi.vertex_input_state.vertex_buffer_descriptions = &vb; pi.vertex_input_state.num_vertex_buffers = 1; pi.vertex_input_state.vertex_attributes = at; pi.vertex_input_state.num_vertex_attributes = 6;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL; pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE; pi.rasterizer_state.enable_depth_clip = false;
        pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL; pi.depth_stencil_state.enable_depth_test = true; pi.depth_stencil_state.enable_depth_write = (k == HK_POLY);
        pi.target_info.color_target_descriptions = ct; pi.target_info.num_color_targets = 4; pi.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT; pi.target_info.has_depth_stencil_target = true;
        g->hw.pipe[k] = SDL_CreateGPUGraphicsPipeline(d, &pi);
        if (!g->hw.pipe[k]) { fprintf(stderr, "[hw-raster] pipeline %d refused: %s\n", k, SDL_GetError()); return 0; }
    }
    {
        SDL_GPUColorTargetDescription ct = hw_target(FC, HB_OVR);
        SDL_GPUGraphicsPipelineCreateInfo pi; memset(&pi, 0, sizeof pi);
        pi.vertex_shader = rvs; pi.fragment_shader = rps; pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL; pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE; pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pi.target_info.color_target_descriptions = &ct; pi.target_info.num_color_targets = 1;
        g->hw.rpipe = SDL_CreateGPUGraphicsPipeline(d, &pi);
        if (!g->hw.rpipe) { fprintf(stderr, "[hw-raster] resolve pipeline refused: %s\n", SDL_GetError()); return 0; }
    }
    SDL_GPUSamplerCreateInfo si; memset(&si, 0, sizeof si); si.min_filter = si.mag_filter = SDL_GPU_FILTER_NEAREST; si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    g->hw.smp = SDL_CreateGPUSampler(d, &si);
    if (!g->hw.smp) return 0;
    fprintf(stderr, "[hw-raster] on: pictures are drawn with the graphics pipeline (a pixel here and there from the exact compositor)\n");
    g->hw.ok = 1;
    return 1;
}

typedef struct { double x, y, z; } hw_dv;
static int hw_clip(const remaster_v3 *in, int n, double nz, hw_dv *out)
{
    int m = 0;
    for (int i = 0; i < n && m < 38; i++) {
        const remaster_v3 a = in[i], b = in[(i + 1) % n];
        const int ia = a.z >= nz, ib = b.z >= nz;
        if (ia) out[m++] = (hw_dv){ a.x, a.y, a.z };
        if (ia != ib) { const double k = (nz - a.z) / ((double)b.z - a.z); out[m++] = (hw_dv){ a.x + k * ((double)b.x - a.x), a.y + k * ((double)b.y - a.y), nz }; }
    }
    return m;
}
static int hw_push(vec *v, float x, float y, float d, uint32_t col, uint32_t idx, uint32_t own, int dith)
{
    const size_t n = v->n / sizeof(hw_vtx);
    if (!vec_reserve(v, (n + 1) * sizeof(hw_vtx))) return 0;
    hw_vtx *q = (hw_vtx *)v->p + n;
    q->x = x; q->y = y; q->d = d;
    q->c[0] = (float)((col >> 16) & 255) / 255.0f; q->c[1] = (float)((col >> 8) & 255) / 255.0f; q->c[2] = (float)(col & 255) / 255.0f; q->c[3] = 1.0f;
    q->idx = idx; q->own = own; q->dith = (uint32_t)dith;
    return 1;
}

static int hw_ensure_tex(gpu_compose *g, gset *s, int w, int h)
{
    if (s->hw.w == w && s->hw.h == h && s->hw.pt[0]) return 1;
    for (int k = 0; k < 4; k++) if (s->hw.pt[k]) { SDL_ReleaseGPUTexture(g->dev, s->hw.pt[k]); s->hw.pt[k] = NULL; }
    if (s->hw.tout) { SDL_ReleaseGPUTexture(g->dev, s->hw.tout); s->hw.tout = NULL; }
    if (s->hw.dpt) { SDL_ReleaseGPUTexture(g->dev, s->hw.dpt); s->hw.dpt = NULL; }
    SDL_GPUTextureCreateInfo ti; memset(&ti, 0, sizeof ti);
    ti.type = SDL_GPU_TEXTURETYPE_2D; ti.width = (Uint32)w; ti.height = (Uint32)h; ti.layer_count_or_depth = 1; ti.num_levels = 1; ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM; ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ;
    for (int k = 0; k < 4; k++) if (!(s->hw.pt[k] = SDL_CreateGPUTexture(g->dev, &ti))) return 0;
    if (!(s->hw.tout = SDL_CreateGPUTexture(g->dev, &ti))) return 0;
    ti.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT; ti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    if (!(s->hw.dpt = SDL_CreateGPUTexture(g->dev, &ti))) return 0;
    if (!s->hw.tpal) {
        SDL_GPUTextureCreateInfo pi = ti; pi.width = 256; pi.height = 1; pi.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM; pi.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        if (!(s->hw.tpal = SDL_CreateGPUTexture(g->dev, &pi))) return 0;
    }
    s->hw.w = w; s->hw.h = h;
    return 1;
}

/* the same job as gpu_compose_begin's compute path, for F117_HW_RASTER; 1 on success, 0 when the compute path has to be used */
typedef struct { const gpu_compose_in *in; hw_chunk *ch; double fw, fh; float DS; } hw_build_ctx;
/* Chunk k of the hardware path's vertex build: the polygons and logic fills of the records from..to in order (with the segments of equal kind they make), then their strokes. */
static void hw_build_job(void *vc, int k)
{
    hw_build_ctx *x = (hw_build_ctx *)vc; hw_chunk *ck = &x->ch[k]; const gpu_compose_in *in = x->in;
    const double fw = x->fw, fh = x->fh; const float DS = x->DS;
    vec *pv = &ck->poly, *lv = &ck->line; pv->n = 0; lv->n = 0; ck->nseg = 0; ck->fail = 0;
    hw_seg *S = ck->seg; int nseg = 0;
#define HNDCX(x) ((float)((x) * 2.0 / fw - 1.0))
#define HNDCY(y) ((float)(1.0 - (y) * 2.0 / fh))
#define HSEG(hf, hc, hk) do { if ((hc) && nseg < HW_SEGS) { S[nseg].first = (hf); S[nseg].count = (hc); S[nseg].kind = (hk); nseg++; } } while (0)
    int cur = HK_POLY; size_t curfirst = 0;
    for (uint32_t i = ck->from; i < ck->to; i++) {
        const gpu_trace_rec *r = &in->recs[i];
        const double cx = r->cx, cy = r->cy, sx = r->sx, sy = r->sy, nz = (double)r->near_z, sc = r->inv_depth_scale > 0 ? r->inv_depth_scale : 1.0;
        const size_t nv0 = pv->n / sizeof(hw_vtx);
        if (r->kind == REMASTER_T_CLEAR) {
            const uint32_t own = 16;
            if (!hw_push(pv, -1, 1, 1e-6f, r->color, 0xFFFF, own, 0) || !hw_push(pv, 1, 1, 1e-6f, r->color, 0xFFFF, own, 0) || !hw_push(pv, -1, -1, 1e-6f, r->color, 0xFFFF, own, 0) ||
                !hw_push(pv, 1, 1, 1e-6f, r->color, 0xFFFF, own, 0) || !hw_push(pv, 1, -1, 1e-6f, r->color, 0xFFFF, own, 0) || !hw_push(pv, -1, -1, 1e-6f, r->color, 0xFFFF, own, 0)) { ck->fail = 1; return; }
        } else if (r->kind == REMASTER_T_ZRESET) {
            HSEG(curfirst, nv0 - curfirst, cur); curfirst = nv0; cur = HK_POLY;
            if (nseg < HW_SEGS) { S[nseg].first = 0; S[nseg].count = 0; S[nseg].kind = HK_ZRESET; nseg++; }
        } else if (r->kind == REMASTER_T_POLYGON || r->kind == REMASTER_T_SHADED || r->kind == REMASTER_T_LOGIC) {
            hw_dv c[40]; const int m = hw_clip(r->v, r->n, nz, c); if (m < 3) continue;
            float px_[40], py_[40], pd[40];
            for (int k = 0; k < m; k++) { px_[k] = HNDCX(cx + sx * c[k].x / c[k].z); py_[k] = HNDCY(cy + sy * c[k].y / c[k].z); pd[k] = (float)(sc / c[k].z * DS); }
            int sk = HK_POLY; uint32_t own = r->kind == REMASTER_T_SHADED ? 16u : 0u, idx = r->paint_index, col0 = r->color;
            if (r->kind == REMASTER_T_LOGIC) {
                idx = r->value & 255u;
                if (r->op == REMASTER_STIPPLE) { sk = HK_STIP; own = 2; col0 = in->palette[r->value & 255]; }
                else if (r->op == REMASTER_OR) { sk = HK_OR; own = 1 | (r->value == 0x80 ? 4u : 0u); }
                else { sk = HK_AND; own = 1; }
            }
            if (sk != cur) { HSEG(curfirst, nv0 - curfirst, cur); cur = sk; curfirst = nv0; }
            for (int k = 1; k + 1 < m; k++) {
                const int idxs[3] = { 0, k, k + 1 };
                for (int q = 0; q < 3; q++) {
                    const int vi = idxs[q]; uint32_t col = col0;
                    if (r->kind == REMASTER_T_SHADED && vi < r->n) col = r->colors[vi];
                    if (!hw_push(pv, px_[vi], py_[vi], pd[vi], col, idx, own, r->kind == REMASTER_T_SHADED)) { ck->fail = 1; return; }
                }
            }
        }
    }
    HSEG(curfirst, pv->n / sizeof(hw_vtx) - curfirst, cur);
    {   /* the strokes and dots: quads with square caps, depth scaled by 1 / 0.97 (the decal tolerance) */
        for (uint32_t i = ck->from; i < ck->to; i++) {
            const gpu_trace_rec *r = &in->recs[i]; if (r->kind != REMASTER_T_LINE) continue;
            const double cx = r->cx, cy = r->cy, sx = r->sx, sy = r->sy, nz = (double)r->near_z, sc = r->inv_depth_scale > 0 ? r->inv_depth_scale : 1.0, bias = r->line_bias > 0 ? r->line_bias : 1.5;
            if (r->v[0].z < nz && r->v[1].z < nz) continue;
            hw_dv a = { r->v[0].x, r->v[0].y, r->v[0].z }, b = { r->v[1].x, r->v[1].y, r->v[1].z };
            if (a.z < nz) { const double k = (nz - a.z) / (b.z - a.z); a = (hw_dv){ a.x + k * (b.x - a.x), a.y + k * (b.y - a.y), nz }; }
            else if (b.z < nz) { const double k = (nz - b.z) / (a.z - b.z); b = (hw_dv){ b.x + k * (a.x - b.x), b.y + k * (a.y - b.y), nz }; }
            const double ax = cx + sx * a.x / a.z, ay = cy + sy * a.y / a.z, bx = cx + sx * b.x / b.z, by = cy + sy * b.y / b.z;
            double dx = bx - ax, dy = by - ay, L = sqrt(dx * dx + dy * dy); const double hwid = (r->width > 1.0 ? r->width : 1.0) * 0.5;
            if (L < 1e-6) { dx = 1; dy = 0; L = 1; }
            const double nx = -dy / L * hwid, ny = dx / L * hwid, ex = dx / L * hwid, ey = dy / L * hwid;
            const double ax0 = ax - ex, ay0 = ay - ey, bx0 = bx + ex, by0 = by + ey;
            const float da = (float)(bias * sc / a.z * DS / 0.97), db = (float)(bias * sc / b.z * DS / 0.97);
            const uint32_t pi = r->paint_index, own = 8;
            if (!hw_push(lv, HNDCX(ax0 + nx), HNDCY(ay0 + ny), da, r->color, pi, own, 0) || !hw_push(lv, HNDCX(ax0 - nx), HNDCY(ay0 - ny), da, r->color, pi, own, 0) ||
                !hw_push(lv, HNDCX(bx0 + nx), HNDCY(by0 + ny), db, r->color, pi, own, 0) || !hw_push(lv, HNDCX(ax0 - nx), HNDCY(ay0 - ny), da, r->color, pi, own, 0) ||
                !hw_push(lv, HNDCX(bx0 - nx), HNDCY(by0 - ny), db, r->color, pi, own, 0) || !hw_push(lv, HNDCX(bx0 + nx), HNDCY(by0 + ny), db, r->color, pi, own, 0)) { ck->fail = 1; return; }
        }
    }
    ck->nseg = nseg;
#undef HNDCX
#undef HNDCY
#undef HSEG
}

typedef struct { uint8_t *m; hw_chunk *ch; int nch; size_t pbase[HW_CHUNKS], lbase[HW_CHUNKS], poly_total; } hw_copy_ctx;
static void hw_copy_job(void *vc, int k)
{
    hw_copy_ctx *x = (hw_copy_ctx *)vc;
    if (k < x->nch) { const vec *v = &x->ch[k].poly; if (v->n) memcpy(x->m + x->pbase[k] * sizeof(hw_vtx), v->p, v->n); }
    else { const int j = k - x->nch; const vec *v = &x->ch[j].line; if (v->n) memcpy(x->m + (x->poly_total + x->lbase[j]) * sizeof(hw_vtx), v->p, v->n); }
}

static int hw_begin(gpu_compose *g, const gpu_compose_in *in, Uint64 t_start)
{
    if (!hw_init(g)) return 0;
    gset *s = GS(g);
    const int w = in->w, h = in->h, tw = in->tw;
    const size_t px = (size_t)tw * h;
    s->hw.nseg = 0;
    vec *vv = &s->hw.v; vv->n = 0;
    hw_seg *S = s->hw.seg; int nseg = 0;
    double maxiz = 0;
    for (uint32_t i = 0; i < in->nrec; i++) {
        const gpu_trace_rec *r = &in->recs[i];
        if (r->kind == REMASTER_T_POLYGON || r->kind == REMASTER_T_LINE) { const double sc = r->inv_depth_scale > 0 ? r->inv_depth_scale : 1.0; if (r->near_z > 0 && sc / r->near_z > maxiz) maxiz = sc / r->near_z; }
    }
    const float DS = maxiz > 0 ? (float)(1.0 / maxiz) : 1.0f;
    const double fw = (double)tw, fh = (double)h;
    int nch = 1;
    size_t nvtx = 0;
    hw_copy_ctx cc; memset(&cc, 0, sizeof cc);
    {   /* the vertices on the setup pool, in contiguous chunks of the records: the same vertices in the same order as one serial pass, merged here */
        static int serial = -1; if (serial < 0) serial = getenv("F117_HW_BUILD_SERIAL") != NULL;     /* =1: one chunk, the serial order (the check that the chunks merge to the same vertices) */
        nch = in->nrec >= 4096 && !serial ? HW_CHUNKS : 1;
        hw_build_ctx bx = { in, s->hw.ch, fw, fh, DS };
        for (int k = 0; k < nch; k++) { s->hw.ch[k].from = (uint32_t)((uint64_t)in->nrec * k / nch); s->hw.ch[k].to = (uint32_t)((uint64_t)in->nrec * (k + 1) / nch); }
        pool_run(hw_build_job, &bx, nch);
        for (int k = 0; k < nch; k++) if (s->hw.ch[k].fail) return 0;
        size_t base = 0;
        for (int k = 0; k < nch; k++) {
            const hw_chunk *c = &s->hw.ch[k];
            for (int j = 0; j < c->nseg; j++) {
                hw_seg_t sg = c->seg[j];
                if (sg.kind != HK_ZRESET) sg.first += base;
                if (nseg > 0 && sg.kind != HK_ZRESET && S[nseg - 1].kind == sg.kind && S[nseg - 1].first + S[nseg - 1].count == sg.first) { S[nseg - 1].count += sg.count; continue; }
                if (nseg < HW_SEGS) S[nseg++] = sg;
            }
            cc.pbase[k] = base; base += c->poly.n / sizeof(hw_vtx);
        }
        cc.poly_total = base;
        size_t lbase = 0;
        for (int k = 0; k < nch; k++) { cc.lbase[k] = lbase; lbase += s->hw.ch[k].line.n / sizeof(hw_vtx); }
        if (lbase && nseg < HW_SEGS) { S[nseg].first = base; S[nseg].count = lbase; S[nseg].kind = HK_LINE; nseg++; }
        nvtx = base + lbase;
        cc.ch = s->hw.ch; cc.nch = nch;
    }
    const Uint64 t_built = SDL_GetTicksNS();
    if (!hw_ensure_tex(g, s, tw, h)) return 0;
    const Uint32 vbytes = (Uint32)(nvtx ? nvtx * sizeof(hw_vtx) : sizeof(hw_vtx));
    const Uint32 pbytes = (Uint32)(px * 4), game_bytes = (Uint32)(((size_t)w * h + 7) & ~(size_t)3);
    const SDL_GPUBufferUsageFlags RO = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, RW = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    if (s->hw.vcap < vbytes) {
        if (s->hw.vb) SDL_ReleaseGPUBuffer(g->dev, s->hw.vb);
        Uint32 cap = s->hw.vcap ? s->hw.vcap : (1u << 20); while (cap < vbytes) cap *= 2;
        SDL_GPUBufferCreateInfo bi = { SDL_GPU_BUFFERUSAGE_VERTEX, cap, 0 };
        s->hw.vb = SDL_CreateGPUBuffer(g->dev, &bi); s->hw.vcap = s->hw.vb ? cap : 0;
        if (!s->hw.vb) return 0;
    }
    gbuf *pixbufs[] = { &s->colour, &s->index_, &s->invz, &s->owner, &s->strokez, &s->outpix };
    for (int k = 0; k < 6; k++) if (!gbuf_ensure(g->dev, pixbufs[k], RW, pbytes)) return 0;
    if (!gbuf_ensure(g->dev, &s->tf, RO, 1024) || !gbuf_ensure(g->dev, &s->src, RO, game_bytes) || !gbuf_ensure(g->dev, &s->tag, RO, game_bytes) || !gbuf_ensure(g->dev, &s->dac, RO, 768)) return 0;
    if (s->down_cap < pbytes) {                    /* finish's readback (verification) */
        if (s->down) SDL_ReleaseGPUTransferBuffer(g->dev, s->down);
        SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, pbytes, 0 };
        s->down = SDL_CreateGPUTransferBuffer(g->dev, &ti); s->down_cap = s->down ? pbytes : 0;
        if (!s->down) return 0;
    }
    const Uint32 total = vbytes + 1024 + 1024;
    if (s->hw.upcap < total) {
        if (s->hw.up) SDL_ReleaseGPUTransferBuffer(g->dev, s->hw.up);
        SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, total + total / 2, 0 };
        s->hw.up = SDL_CreateGPUTransferBuffer(g->dev, &ti); s->hw.upcap = s->hw.up ? ti.size : 0;
        if (!s->hw.up) return 0;
    }
    {
        Uint8 *m = (Uint8 *)SDL_MapGPUTransferBuffer(g->dev, s->hw.up, true);
        if (!m) return 0;
        cc.m = m;
        if (nvtx) pool_run(hw_copy_job, &cc, 2 * nch); else memset(m, 0, sizeof(hw_vtx));
        memcpy(m + vbytes, in->palette, 1024);
        uint32_t *fl = (uint32_t *)(m + vbytes + 1024);
        for (int k = 0; k < 256; k++) fl[k] = in->tagflags[k];
        SDL_UnmapGPUTransferBuffer(g->dev, s->hw.up);
    }
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(g->dev);
    if (!cb) return 0;
    profile_mark(g, cb, "hw_upload_begin");
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    { SDL_GPUTransferBufferLocation f = { s->hw.up, 0 }; SDL_GPUBufferRegion t = { s->hw.vb, 0, vbytes }; SDL_UploadToGPUBuffer(cp, &f, &t, false); }
    { SDL_GPUTextureTransferInfo f = { s->hw.up, vbytes, 256, 1 }; SDL_GPUTextureRegion t; memset(&t, 0, sizeof t); t.texture = s->hw.tpal; t.w = 256; t.h = 1; t.d = 1; SDL_UploadToGPUTexture(cp, &f, &t, false); }
    { SDL_GPUTransferBufferLocation f = { s->hw.up, vbytes + 1024 }; SDL_GPUBufferRegion t = { s->tf.buf, 0, 1024 }; SDL_UploadToGPUBuffer(cp, &f, &t, false); }
    SDL_EndGPUCopyPass(cp);
    profile_mark(g, cb, "hw_upload_end");
    int first = 1, clear_depth = 1;
    for (int k = 0; k < nseg; k++) {
        if (S[k].kind == HK_ZRESET) { clear_depth = 1; continue; }
        SDL_GPUColorTargetInfo cti[4]; memset(cti, 0, sizeof cti);
        for (int j = 0; j < 4; j++) { cti[j].texture = s->hw.pt[j]; cti[j].load_op = first ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD; cti[j].store_op = SDL_GPU_STOREOP_STORE; }
        SDL_GPUDepthStencilTargetInfo dti; memset(&dti, 0, sizeof dti); dti.texture = s->hw.dpt; dti.load_op = clear_depth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD; dti.store_op = SDL_GPU_STOREOP_STORE; dti.clear_depth = 0.0f;
        dti.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE; dti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cb, cti, 4, &dti);
        if (!rp) { fprintf(stderr, "[hw-raster] render pass refused: %s\n", SDL_GetError()); SDL_CancelGPUCommandBuffer(cb); return 0; }
        SDL_BindGPUGraphicsPipeline(rp, g->hw.pipe[S[k].kind]); SDL_GPUBufferBinding bb = { s->hw.vb, 0 }; SDL_BindGPUVertexBuffers(rp, 0, &bb, 1);
        SDL_DrawGPUPrimitives(rp, (Uint32)S[k].count, 1, (Uint32)S[k].first, 0);
        SDL_EndGPURenderPass(rp);
        first = 0; clear_depth = 0;
    }
    profile_mark(g, cb, "hw_raster_end");
    {   /* resolve: the palette lookup and the sea's smoothing, into tout */
        SDL_GPUColorTargetInfo cti; memset(&cti, 0, sizeof cti); cti.texture = s->hw.tout; cti.load_op = SDL_GPU_LOADOP_DONT_CARE; cti.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cb, &cti, 1, NULL);
        if (!rp) { fprintf(stderr, "[hw-raster] resolve pass refused: %s\n", SDL_GetError()); SDL_CancelGPUCommandBuffer(cb); return 0; }
        SDL_BindGPUGraphicsPipeline(rp, g->hw.rpipe);
        SDL_GPUTextureSamplerBinding sb[5] = { { s->hw.pt[0], g->hw.smp }, { s->hw.pt[1], g->hw.smp }, { s->hw.pt[2], g->hw.smp }, { s->hw.pt[3], g->hw.smp }, { s->hw.tpal, g->hw.smp } };
        SDL_BindGPUFragmentSamplers(rp, 0, sb, 5);
        SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
        SDL_EndGPURenderPass(rp);
    }
    profile_mark(g, cb, "hw_resolve_end");
    {   /* export into the pixel buffers the replace step reads */
        SDL_GPUStorageBufferReadWriteBinding rw[3] = { { s->colour.buf, false }, { s->invz.buf, false }, { s->strokez.buf, false } };
        SDL_GPUComputePass *c = SDL_BeginGPUComputePass(cb, NULL, 0, rw, 3);
        if (!c) { fprintf(stderr, "[hw-raster] export pass refused: %s\n", SDL_GetError()); SDL_CancelGPUCommandBuffer(cb); return 0; }
        SDL_BindGPUComputePipeline(c, g->hw.pexport);
        SDL_GPUTexture *rt[2] = { s->hw.tout, s->hw.pt[3] };
        SDL_BindGPUComputeStorageTextures(c, 0, rt, 2);
        const Uint32 u[4] = { (Uint32)tw, (Uint32)h, 0, 0 };
        SDL_PushGPUComputeUniformData(cb, 0, u, sizeof u);
        SDL_DispatchGPUCompute(c, ((Uint32)tw + 15) / 16, ((Uint32)h + 15) / 16, 1);
        SDL_EndGPUComputePass(c);
    }
    profile_mark(g, cb, "hw_export_end");
    profile_resolve(g, cb);
    const Uint64 t_prepass = SDL_GetTicksNS();
    SDL_SubmitGPUCommandBuffer(cb);
    s->params = *in; s->params.recs = NULL; s->params.palette = NULL; s->params.src = NULL; s->params.tag = NULL; s->params.dac = NULL; s->params.tagflags = NULL;
    s->pbytes = pbytes; s->game_bytes = game_bytes; s->npix_game = (size_t)w * h;
    s->setup_ms = (double)(t_built - t_start) / 1e6;
    for (int k = 0; k < 9; k++) g_last_phase[k] = 0;
    g_last_phase[0] = (double)(t_built - t_start) / 1e6; g_last_phase[4] = (double)(t_prepass - t_built) / 1e6; g_last_phase[5] = (double)(nvtx / 3);
    {   /* F117_HW_TIMING (diagnostic): the CPU side of a hardware picture, mean of each 120 */
        static int on = -1; static double sb, sg; static unsigned n; static double nvs;
        if (on < 0) on = getenv("F117_HW_TIMING") != NULL;
        if (on) {
            sb += (double)(t_built - t_start) / 1e6; sg += (double)(t_prepass - t_built) / 1e6; nvs += (double)(nvtx / 3);
            if (++n % 120 == 0) fprintf(stderr, "[hw-timing] mean of %u: vertices built in %.2f ms (%.0f triangles), uploaded and submitted in %.2f ms\n", n, sb / n, nvs / n, sg / n);
        }
    }
    s->t_begin = SDL_GetTicksNS();
    s->begun = 1;
    return 1;
}

int gpu_compose_begin(gpu_compose *g, const gpu_compose_in *in)
{
    if (!g || !in) return 0;
#ifdef F117_GPU_TIMESTAMPS
    if (g->profile && g->profile_pending) {
        SDL_WaitForGPUIdle(g->dev);
        f117_timestamp_destroy(g->profile); g->profile = NULL;
        fprintf(stderr, "[gpu-timestamp] incomplete previous frame; profiling disabled\n");
    }
    g->profile_pending = g->profile != NULL;
#endif
    faces_wait(SC(g));       /* a previous call on this set that returned early may have left the face list running */
    GS(g)->begun = 0;
    const Uint64 t_start = SDL_GetTicksNS();
    if (hw_on() && hw_begin(g, in, t_start)) return 1;
    const int w = in->w, h = in->h, tw = in->tw;
    const size_t px = (size_t)tw * h;
    if (px * 4 > 0x7FFFFFFFu) { say(g, "the picture is too large"); return 0; }

    {   const char *sea_env = getenv("F117_SMOOTH_SEA");
        g_smooth_sea = !sea_env || atoi(sea_env) != 0;
        g_square_pen = square_pen_enabled();
        g_q_check = g_plane_check = getenv("F117_BIN_CHECK") != NULL;
        g_plane_full = getenv("F117_PLANE_FULL") != NULL;
        g_cyc_on = getenv("F117_CLASSIFY_COUNTERS") != NULL;      /* the per-record counters (F117_REMASTER_TIMING alone prints the phases without them) */
        exact_on(); decal_faces_enabled();
    }
    /* classify the records: the clear and the bands first, then polygons, then the lines; anything else is not handled */
    int phase = 0;
    SC(g)->polys.n = 0; SC(g)->strokes.n = 0;
    uint32_t nband = 0, nstrokes = 0;
    uint32_t *bw = (uint32_t *)vec_reserve(&SC(g)->band_words, (size_t)4096 * 64 * 4);
    uint32_t *sw = (uint32_t *)vec_reserve(&SC(g)->stroke_words, (size_t)(in->nrec ? in->nrec : 1) * STROKE_WORDS * 4);
    if (!bw || !sw) return 0;
    const size_t band_cap = SC(g)->band_words.cap / (64 * 4);
    const unsigned long long c_loop = CYC();
    /* the order rules first (serial, cheap), then the bands (serial: a handful a picture), then the polygons and lines (several threads) */
    for (uint32_t i = 0; i < in->nrec; i++) {
        const gpu_trace_rec *r = &in->recs[i];
        switch (r->kind) {
        case REMASTER_T_CLEAR: case REMASTER_T_ZRESET: case REMASTER_T_WINDOW: break;
        case REMASTER_T_SHADED: if (phase != 0) { say(g, "a band after a polygon"); return 0; } break;
        case REMASTER_T_POLYGON: case REMASTER_T_LOGIC: if (phase > 1) { say(g, "a polygon after a line"); return 0; } phase = 1; break;
        case REMASTER_T_LINE: if (!r->line_decal || !r->line_order) { say(g, "lines without the decal rules"); return 0; } phase = 2; break;
        default: say(g, "an unknown record"); return 0;
        }
    }
    for (uint32_t i = 0; i < in->nrec; i++) {
        const gpu_trace_rec *r = &in->recs[i];
        if (r->kind != REMASTER_T_SHADED) continue;
        if (r->n < 3 || r->n > 14) continue;
        sv s[MAX_POLY]; double rgb[MAX_POLY][3]; int ok = 1;
        for (int k = 0; k < r->n; k++) {
            if (r->v[k].z < r->near_z) { ok = 0; break; }
            s[k].x = r->cx + r->sx * (double)r->v[k].x / r->v[k].z;
            s[k].y = r->cy + r->sy * (double)r->v[k].y / r->v[k].z;
            s[k].iz = (r->inv_depth_scale > 0.0 ? r->inv_depth_scale : 1.0) / (double)r->v[k].z;
            rgb[k][0] = r->colors[k] >> 16 & 255; rgb[k][1] = r->colors[k] >> 8 & 255; rgb[k][2] = r->colors[k] & 255;
        }
        if (!ok) continue;
        if (exact_on()) for (int k = 0; k < r->n; k++) if (fabs(s[k].x) > EX_GUARD || fabs(s[k].y) > EX_GUARD) { say(g, "a band vertex outside the exact guard band"); return 0; }
        for (int k = 1; k + 1 < r->n; k++) {
            const sv tr[3] = { s[0], s[k], s[k + 1] };
            const double col[3][3] = { { rgb[0][0], rgb[0][1], rgb[0][2] }, { rgb[k][0], rgb[k][1], rgb[k][2] }, { rgb[k + 1][0], rgb[k + 1][1], rgb[k + 1][2] } };
            if (nband >= band_cap) { say(g, "too many band triangles"); return 0; }
            const unsigned long long cb = CYC();
            if (band_triangle(bw + (size_t)nband * 64, r, tr, col, tw, h)) nband++;
            CADD(g_cyc, 0, CYC() - cb); CADD(g_cnt, 0, 1);
        }
    }
    /* Part B (14 November 2026), F117_GPU_BANDS_FIRST (on by default, =0 off): the bands pass (the clear, the sky and ground) needs only the band triangles built above, not the polygons
     * and lines the CPU classifies, bins and packs next. So it is uploaded and submitted now, in a command buffer of its own, and the GPU runs it while the CPU
     * does that setup; the main submission below then leaves the bands out. The passes, their order and their data are the same (command buffers run in
     * submission order), so the result is the same. Not with the per-pass timing or the timestamp profile, which time the passes in one buffer. */
    static int bands_first = -1;
    if (bands_first < 0) { const char *bf = getenv("F117_GPU_BANDS_FIRST"); bands_first = (bf && bf[0] == '1') && !getenv("F117_GPU_PASS_TIMES") && !getenv("F117_GPU_TIMESTAMP_LOG"); }
    int did_bands = 0;
    const Uint64 t_bands_built = SDL_GetTicksNS();
    if (bands_first) {
        const SDL_GPUBufferUsageFlags rb = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, rwb = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
        const Uint32 pb = (Uint32)(px * 4), bb = (Uint32)((nband ? nband : 1) * 256);
        int ok = gbuf_ensure(g->dev, &GS(g)->bt, rb, bb);
        gbuf *early[] = { &GS(g)->colour, &GS(g)->index_, &GS(g)->invz, &GS(g)->owner };
        for (int k = 0; ok && k < 4; k++) ok = gbuf_ensure(g->dev, early[k], rwb, pb);
        if (ok && GS(g)->upb_cap < bb) {
            if (GS(g)->upb) SDL_ReleaseGPUTransferBuffer(g->dev, GS(g)->upb);
            SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bb + bb / 2, 0 };
            GS(g)->upb = SDL_CreateGPUTransferBuffer(g->dev, &ti); GS(g)->upb_cap = GS(g)->upb ? ti.size : 0;
            ok = GS(g)->upb != NULL;
        }
        if (ok) {
            Uint8 *m = (Uint8 *)SDL_MapGPUTransferBuffer(g->dev, GS(g)->upb, true);
            ok = m != NULL;
            if (ok) { memset(m, 0, bb); if (nband) memcpy(m, bw, (size_t)nband * 256); SDL_UnmapGPUTransferBuffer(g->dev, GS(g)->upb); }
        }
        SDL_GPUCommandBuffer *cb0 = ok ? SDL_AcquireGPUCommandBuffer(g->dev) : NULL;
        if (cb0) {
            SDL_GPUCopyPass *cp0 = SDL_BeginGPUCopyPass(cb0);
            SDL_GPUTransferBufferLocation from = { GS(g)->upb, 0 }; SDL_GPUBufferRegion to = { GS(g)->bt.buf, 0, bb };
            SDL_UploadToGPUBuffer(cp0, &from, &to, false);
            SDL_EndGPUCopyPass(cp0);
            SDL_GPUStorageBufferReadWriteBinding rw[4] = { { GS(g)->colour.buf, false }, { GS(g)->index_.buf, false }, { GS(g)->invz.buf, false }, { GS(g)->owner.buf, false } };
            const Uint32 u[4] = { (Uint32)tw, (Uint32)h, nband, exact_on() ? 1u : 0u };
            SDL_GPUComputePass *c = SDL_BeginGPUComputePass(cb0, NULL, 0, rw, 4);
            SDL_BindGPUComputePipeline(c, g->p_bands);
            SDL_BindGPUComputeStorageBuffers(c, 0, &GS(g)->bt.buf, 1);
            SDL_PushGPUComputeUniformData(cb0, 0, u, sizeof u);
            SDL_DispatchGPUCompute(c, ((Uint32)tw + 63) / 64, (Uint32)h, 1);
            SDL_EndGPUComputePass(c);
            SDL_SubmitGPUCommandBuffer(cb0);
            did_bands = 1;
        }
    }
    SC(g)->faces_early = 0;       /* set by classify_records when it started the face list itself */
    const Uint64 t_pre_classify = SDL_GetTicksNS();
    if (!classify_records(g, in, tw, h, sw, &nstrokes)) return 0;
    {   static int on = -1; static unsigned n; static double a, b, c2;
        if (on < 0) on = getenv("F117_CLASSIFY_SPLIT") != NULL;
        if (on) {
            const Uint64 now = SDL_GetTicksNS();
            a += (t_bands_built - t_start) / 1e6; b += (t_pre_classify - t_bands_built) / 1e6; c2 += (now - t_pre_classify) / 1e6;
            if (++n % 60 == 0) fprintf(stderr, "[classify-split] phase: before bands %.2f ms, bands-first upload+submit %.2f, classify_records %.2f\n", a / n, b / n, c2 / n);
        }
    }
    CADD(g_cyc, 5, CYC() - c_loop); CADD(g_cnt, 5, 1);
    const Uint64 t_classified = SDL_GetTicksNS();
    const int faces_async = SC(g)->faces_early || (decal_faces_enabled() && setup_threads() > 1 && faces_start(g));
    const int tilesx = (tw + TILE - 1) / TILE, tilesy = (h + TILE - 1) / TILE;
    size_t npents = 0, nsents = 0;
    if (!bin_tiles(&SC(g)->polys, tilesx, tilesy, &SC(g)->poly_off, &SC(g)->poly_ents, &npents)) return 0;
    if (!bin_tiles(&SC(g)->strokes, tilesx, tilesy, &SC(g)->stroke_off, &SC(g)->stroke_ents, &nsents)) return 0;
    const Uint64 t_binned = SDL_GetTicksNS();
    uint32_t *pw = (uint32_t *)vec_reserve(&SC(g)->poly_words, (SC(g)->polys.n ? SC(g)->polys.n : 1) * 160);
    uint32_t *stw = (uint32_t *)vec_reserve(&SC(g)->stroke_tri_words, (SC(g)->strokes.n ? SC(g)->strokes.n : 1) * 160);
    if (!pw || !stw) return 0;
    if (!SC(g)->polys.n) memset(pw, 0, 160);        /* the one-triangle minimum upload of an empty list stays zero, as before */
    if (!SC(g)->strokes.n) memset(stw, 0, 160);
    const int use_faces = decal_faces_enabled();
    {   const Uint64 pk0 = SDL_GetTicksNS();
        pack_ctx pc = { SC(g), pw, stw, use_faces, faces_async }; pool_run(pack_job, &pc, SETUP_CHUNKS + 1); faces_wait(SC(g));
        static int on = -1; static unsigned n; static double sw_;
        if (on < 0) on = getenv("F117_CLASSIFY_SPLIT") != NULL;
        if (on) { sw_ += (SDL_GetTicksNS() - pk0) / 1e6; if (++n % 60 == 0) fprintf(stderr, "[classify-split] pack pool wall %.2f ms (the face list runs beside the binning, on its own thread)\n", sw_ / n); }
    }
    uint32_t flags[256];
    for (int k = 0; k < 256; k++) flags[k] = in->tagflags[k];

    const Uint32 stroke_words = (nstrokes ? nstrokes : 1) * STROKE_WORDS;
    Uint32 face_offset = 0, sw_bytes = stroke_words * 4;
    if (use_faces) {
        if (SC(g)->faces.failed) return 0;
        const size_t face_words = DF_START + SC(g)->faces.count * DF_STRIDE;
        if (face_words > UINT32_MAX / 4 - stroke_words) return 0;
        sw_bytes += (Uint32)(face_words * 4);
        sw = (uint32_t *)vec_reserve(&SC(g)->stroke_words, sw_bytes);
        if (!sw) return 0;
        face_offset = stroke_words;
        memcpy(sw + face_offset, SC(g)->faces.words, face_words * 4);
    }

    const Uint64 t_packed = SDL_GetTicksNS();
    /* GPU resources */
    const Uint32 pbytes = (Uint32)(px * 4);
    const SDL_GPUBufferUsageFlags RO = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, RW = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    const size_t npix_game = (size_t)w * h;
    const Uint32 game_bytes = (Uint32)((npix_game + 7) & ~(size_t)3);
    const Uint32 nstroke_b = (Uint32)((nstrokes ? nstrokes : 1) * 4);
    seg segs[] = {
        { &GS(g)->bt, bw, (Uint32)((nband ? nband : 1) * 256), RO },
        { &GS(g)->pt, pw, (Uint32)((SC(g)->polys.n ? SC(g)->polys.n : 1) * 160), RO },
        { &GS(g)->pe, SC(g)->poly_ents.p, (Uint32)((npents ? npents : 1) * 36), RO },
        { &GS(g)->po, SC(g)->poly_off.p, (Uint32)(((size_t)tilesx * tilesy + 1) * 4), RO },
        { &GS(g)->pal, in->palette, 1024, RO },
        { &GS(g)->st, stw, (Uint32)((SC(g)->strokes.n ? SC(g)->strokes.n : 1) * 160), RO },
        { &GS(g)->se, SC(g)->stroke_ents.p, (Uint32)((nsents ? nsents : 1) * 36), RO },
        { &GS(g)->so, SC(g)->stroke_off.p, (Uint32)(((size_t)tilesx * tilesy + 1) * 4), RO },
        { &GS(g)->sw, sw, sw_bytes, RO },
        { &GS(g)->tf, flags, 1024, RO },
    };
    const int nseg = (int)(sizeof segs / sizeof *segs);
    Uint32 total = 0;
    for (int k = 0; k < nseg; k++) { if (!gbuf_ensure(g->dev, segs[k].b, segs[k].usage, segs[k].bytes)) return 0; total += (segs[k].bytes + 15u) & ~15u; }
    if (!gbuf_ensure(g->dev, &GS(g)->hid, RW, nstroke_b)) return 0;
    gbuf *pixbufs[] = { &GS(g)->colour, &GS(g)->index_, &GS(g)->invz, &GS(g)->owner, &GS(g)->strokez, &GS(g)->outpix };
    for (int k = 0; k < 6; k++) if (!gbuf_ensure(g->dev, pixbufs[k], RW, pbytes)) return 0;
    if (GS(g)->up_cap < total) {
        if (GS(g)->up) SDL_ReleaseGPUTransferBuffer(g->dev, GS(g)->up);
        SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, total + total / 2, 0 };
        GS(g)->up = SDL_CreateGPUTransferBuffer(g->dev, &ti); GS(g)->up_cap = GS(g)->up ? ti.size : 0;
        if (!GS(g)->up) return 0;
    }
    if (GS(g)->down_cap < pbytes) {
        if (GS(g)->down) SDL_ReleaseGPUTransferBuffer(g->dev, GS(g)->down);
        SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, pbytes, 0 };
        GS(g)->down = SDL_CreateGPUTransferBuffer(g->dev, &ti); GS(g)->down_cap = GS(g)->down ? pbytes : 0;
        if (!GS(g)->down) return 0;
    }
    if (!gbuf_ensure(g->dev, &GS(g)->src, RO, game_bytes) || !gbuf_ensure(g->dev, &GS(g)->tag, RO, game_bytes) || !gbuf_ensure(g->dev, &GS(g)->dac, RO, 768)) return 0;
    const Uint64 t_prepared = SDL_GetTicksNS();
    {
        Uint8 *m = (Uint8 *)SDL_MapGPUTransferBuffer(g->dev, GS(g)->up, true), *p = m;
        if (!m) return 0;
        /* Part B (16 November 2026): each segment's data is copied once, in 1 MB pieces on the setup pool, and only the bytes after the data are zeroed. The upload memory
         * is write-combined, so the earlier zero-then-copy wrote every byte twice from one thread. The bytes uploaded are the same. */
        static up_piece pieces_s[GSETS][512]; up_piece *pieces = pieces_s[g_set_tls]; int np = 0, spill = 0;
        for (int k = 0; k < nseg; k++) {
            const Uint32 bytes = segs[k].bytes;
            if (did_bands && k == 0) { p += (bytes + 15u) & ~15u; continue; }      /* the band triangles went in the early submission */
            size_t n = 0;
            if (segs[k].data) {
                n = bytes;
                if (segs[k].b == &GS(g)->src || segs[k].b == &GS(g)->tag) n = npix_game;
                if (n > bytes) n = bytes;
            }
            if (n < bytes) memset(p + n, 0, bytes - n);
            for (size_t o = 0; o < n; o += UP_PIECE) {
                const size_t c = n - o < UP_PIECE ? n - o : UP_PIECE;
                if (np < 512) { pieces[np].dst = p + o; pieces[np].src = (const Uint8 *)segs[k].data + o; pieces[np].n = c; np++; }
                else { memcpy(p + o, (const Uint8 *)segs[k].data + o, c); spill = 1; }
            }
            p += (bytes + 15u) & ~15u;
        }
        (void)spill;
        { up_ctx uc = { pieces }; pool_run(up_job, &uc, np); }
        SDL_UnmapGPUTransferBuffer(g->dev, GS(g)->up);
    }
    const Uint64 t_setup = SDL_GetTicksNS();

    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(g->dev);
    if (!cb) return 0;
    const Uint64 t_acquired = SDL_GetTicksNS();
    profile_mark(g, cb, "upload_begin");
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    {
        Uint32 at = 0;
        for (int k = 0; k < nseg; k++) {
            SDL_GPUTransferBufferLocation from = { GS(g)->up, at };
            SDL_GPUBufferRegion to = { segs[k].b->buf, 0, segs[k].bytes };
            if (!(did_bands && k == 0)) SDL_UploadToGPUBuffer(cp, &from, &to, false);
            at += (segs[k].bytes + 15u) & ~15u;
        }
    }
    SDL_EndGPUCopyPass(cp);
    profile_mark(g, cb, "upload_end");
    Uint64 pmark = SDL_GetTicksNS();
    if (pass_times_on()) { pmark = t_setup; pass_split(g, &cb, 0, &pmark); if (!cb) return 0; }

    SDL_GPUBuffer *b_colour = GS(g)->colour.buf, *b_index = GS(g)->index_.buf, *b_invz = GS(g)->invz.buf, *b_owner = GS(g)->owner.buf, *b_strokez = GS(g)->strokez.buf, *b_out = GS(g)->outpix.buf;
    if (!did_bands) {   /* bands: the clear, the sky and ground, depth and owner zero (already submitted with F117_GPU_BANDS_FIRST) */
        SDL_GPUStorageBufferReadWriteBinding rw[4] = { { b_colour, false }, { b_index, false }, { b_invz, false }, { b_owner, false } };
        const Uint32 u[4] = { (Uint32)tw, (Uint32)h, nband, exact_on() ? 1u : 0u };
        SDL_GPUComputePass *c = SDL_BeginGPUComputePass(cb, NULL, 0, rw, 4);
        SDL_BindGPUComputePipeline(c, g->p_bands);
        SDL_BindGPUComputeStorageBuffers(c, 0, &GS(g)->bt.buf, 1);
        SDL_PushGPUComputeUniformData(cb, 0, u, sizeof u);
        SDL_DispatchGPUCompute(c, ((Uint32)tw + 63) / 64, (Uint32)h, 1);
        SDL_EndGPUComputePass(c);
        profile_mark(g, cb, "bands_end");
        if (pass_times_on()) pass_split(g, &cb, 1, &pmark);
    }
    if (SC(g)->polys.n) {
        SDL_GPUStorageBufferReadWriteBinding rw[4] = { { b_colour, false }, { b_index, false }, { b_invz, false }, { b_owner, false } };
        SDL_GPUBuffer *ro[4] = { GS(g)->pt.buf, GS(g)->pe.buf, GS(g)->po.buf, GS(g)->pal.buf };
        const Uint32 u[4] = { (Uint32)tw, (Uint32)h, (Uint32)tilesx, exact_on() ? 1u : 0u };
        SDL_GPUComputePass *c = SDL_BeginGPUComputePass(cb, NULL, 0, rw, 4);
        SDL_BindGPUComputePipeline(c, g->p_polys);
        SDL_BindGPUComputeStorageBuffers(c, 0, ro, 4);
        SDL_PushGPUComputeUniformData(cb, 0, u, sizeof u);
        SDL_DispatchGPUCompute(c, (Uint32)tilesx, (Uint32)tilesy, 1);
        SDL_EndGPUComputePass(c);
        profile_mark(g, cb, "polygons_end");
        if (pass_times_on()) pass_split(g, &cb, 2, &pmark);
    }
    for (int mode = nstrokes ? 0 : 1; mode < 2; mode++) {      /* strokes: the points behind their own surface, then the pixels (always: it writes the stroke depth the replace step reads) */
        SDL_GPUStorageBufferReadWriteBinding rw[6] = { { b_colour, false }, { b_index, false }, { b_invz, false }, { b_owner, false }, { GS(g)->hid.buf, false }, { b_strokez, false } };
        SDL_GPUBuffer *ro[4] = { GS(g)->st.buf, GS(g)->se.buf, GS(g)->so.buf, GS(g)->sw.buf };
        const Uint32 u[8] = { (Uint32)mode, (Uint32)tw, (Uint32)h, (Uint32)tilesx, nstrokes, exact_on() ? 1u : 0u, face_offset, decal_simple_on() };
        SDL_GPUComputePass *c = SDL_BeginGPUComputePass(cb, NULL, 0, rw, 6);
        SDL_BindGPUComputePipeline(c, g->p_strokes);
        SDL_BindGPUComputeStorageBuffers(c, 0, ro, 4);
        SDL_PushGPUComputeUniformData(cb, 0, u, sizeof u);
        if (mode == 0) SDL_DispatchGPUCompute(c, 256, (nstrokes / 4096 + 16) / 16, 1);
        else SDL_DispatchGPUCompute(c, (Uint32)tilesx, (Uint32)tilesy, 1);
        SDL_EndGPUComputePass(c);
        profile_mark(g, cb, mode == 0 ? "points_end" : "strokes_end");
        if (pass_times_on()) pass_split(g, &cb, 3 + mode, &pmark);
    }
    GS(g)->params = *in; GS(g)->params.recs = NULL; GS(g)->params.palette = NULL; GS(g)->params.src = NULL; GS(g)->params.tag = NULL; GS(g)->params.dac = NULL; GS(g)->params.tagflags = NULL;
    GS(g)->pbytes = pbytes; GS(g)->game_bytes = game_bytes; GS(g)->npix_game = npix_game;
    profile_resolve(g, cb);
    const Uint64 t_prepass = SDL_GetTicksNS();
    SDL_SubmitGPUCommandBuffer(cb);               /* the raster passes run while the caller makes the game's picture */
    g_last_phase[7] = (double)(t_acquired - t_setup) / 1e6; g_last_phase[8] = (double)(SDL_GetTicksNS() - t_prepass) / 1e6;
    GS(g)->setup_ms = (double)(t_setup - t_start) / 1e6;
    g_last_phase[0] = (double)(t_classified - t_start) / 1e6; g_last_phase[1] = (double)(t_binned - t_classified) / 1e6; g_last_phase[2] = (double)(t_packed - t_binned) / 1e6;
    g_last_phase[3] = (double)(t_prepared - t_packed) / 1e6; g_last_phase[4] = (double)(t_setup - t_prepared) / 1e6;
    g_last_phase[5] = (double)(SC(g)->polys.n + SC(g)->strokes.n); g_last_phase[6] = (double)total / 1048576.0;
    if (getenv("F117_REMASTER_TIMING")) {   /* diagnostic: where the CPU setup goes, every 120th picture (stderr) */
        static double sum[5], mb, ents, trisn; static unsigned count;
        sum[0] += (double)(t_classified - t_start) / 1e6; sum[1] += (double)(t_binned - t_classified) / 1e6; sum[2] += (double)(t_packed - t_binned) / 1e6;
        sum[3] += (double)(t_prepared - t_packed) / 1e6; sum[4] += (double)(t_setup - t_prepared) / 1e6;
        mb += (double)total / 1048576.0; ents += (double)(npents + nsents); trisn += (double)(SC(g)->polys.n + SC(g)->strokes.n);
        if (++count % 120 == 0)
            fprintf(stderr, "[gpu-phases] mean of %u: classify %.2f, bin %.2f, pack %.2f, buffers %.2f, copy %.2f ms; %.0f triangles, %.0f entries, %.1f MB uploaded\n",
                    count, sum[0] / count, sum[1] / count, sum[2] / count, sum[3] / count, sum[4] / count, trisn / count, ents / count, mb / count);
        if (count % 120 == 0 && g_cyc[5]) {     /* the classify loop by record kind, as shares of its cycles scaled to its measured milliseconds */
            const double ms = sum[0] / count, tot = (double)g_cyc[5];
            fprintf(stderr, "[gpu-classify] mean of %u: bands %.2f ms (%.0f a picture), polygons %.2f (%.0f), strokes %.2f (%.0f); of these, depth planes %.2f (%.0f triangles), snapped vertices %.2f; %.0f cycles a triangle for planes, %.0f for vertices\n",
                    count, ms * (double)g_cyc[0] / tot, (double)g_cnt[0] / count, ms * (double)g_cyc[1] / tot, (double)g_cnt[1] / count, ms * (double)g_cyc[2] / tot, (double)g_cnt[2] / count,
                    ms * (double)g_cyc[3] / tot, (double)g_cnt[3] / count, ms * (double)g_cyc[4] / tot, g_cnt[3] ? (double)g_cyc[3] / (double)g_cnt[3] : 0.0, g_cnt[4] ? (double)g_cyc[4] / (double)g_cnt[4] : 0.0);
        }
    }
    GS(g)->t_begin = SDL_GetTicksNS();
    GS(g)->begun = 1;
    return 1;
}

int gpu_compose_finish(gpu_compose *g, const uint8_t *src, const uint8_t *tag, const uint8_t *dac, uint32_t *wide_out)
{
    if (!g || !GS(g)->begun || !src || !tag || !dac) return 0;
    GS(g)->begun = 0;
    const int w = GS(g)->params.w, h = GS(g)->params.h, tw = GS(g)->params.tw;
    const Uint32 pbytes = GS(g)->pbytes, game_bytes = GS(g)->game_bytes;
    const Uint32 off_tag = (game_bytes + 15u) & ~15u, off_dac = off_tag + ((game_bytes + 15u) & ~15u), total2 = off_dac + 768;
    if (GS(g)->up2_cap < total2) {
        if (GS(g)->up2) SDL_ReleaseGPUTransferBuffer(g->dev, GS(g)->up2);
        SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, total2 + total2 / 4, 0 };
        GS(g)->up2 = SDL_CreateGPUTransferBuffer(g->dev, &ti); GS(g)->up2_cap = GS(g)->up2 ? ti.size : 0;
        if (!GS(g)->up2) return 0;
    }
    {
        Uint8 *m = (Uint8 *)SDL_MapGPUTransferBuffer(g->dev, GS(g)->up2, true);
        if (!m) return 0;
        memset(m, 0, total2);
        memcpy(m, src, GS(g)->npix_game); memcpy(m + off_tag, tag, GS(g)->npix_game); memcpy(m + off_dac, dac, 768);
        SDL_UnmapGPUTransferBuffer(g->dev, GS(g)->up2);
    }
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(g->dev);
    if (!cb) return 0;
    profile_mark(g, cb, "picture_upload_begin");
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    {
        SDL_GPUTransferBufferLocation f1 = { GS(g)->up2, 0 }, f2 = { GS(g)->up2, off_tag }, f3 = { GS(g)->up2, off_dac };
        SDL_GPUBufferRegion t1 = { GS(g)->src.buf, 0, game_bytes }, t2 = { GS(g)->tag.buf, 0, game_bytes }, t3 = { GS(g)->dac.buf, 0, 768 };
        SDL_UploadToGPUBuffer(cp, &f1, &t1, false); SDL_UploadToGPUBuffer(cp, &f2, &t2, false); SDL_UploadToGPUBuffer(cp, &f3, &t3, false);
    }
    SDL_EndGPUCopyPass(cp);
    profile_mark(g, cb, "picture_upload_end");
    SDL_GPUBuffer *b_colour = GS(g)->colour.buf, *b_invz = GS(g)->invz.buf, *b_strokez = GS(g)->strokez.buf, *b_out = GS(g)->outpix.buf;
    {   /* the replace step */
        SDL_GPUStorageBufferReadWriteBinding rw[4] = { { b_colour, false }, { b_invz, false }, { b_strokez, false }, { b_out, false } };
        SDL_GPUBuffer *ro[4] = { GS(g)->tf.buf, GS(g)->src.buf, GS(g)->tag.buf, GS(g)->dac.buf };
        const int32_t u[12] = { w, h, tw, GS(g)->params.xoff, GS(g)->params.x0, GS(g)->params.x1, GS(g)->params.y0, GS(g)->params.y1, GS(g)->params.complete, 0, 0, 0 };
        SDL_GPUComputePass *c = SDL_BeginGPUComputePass(cb, NULL, 0, rw, 4);
        SDL_BindGPUComputePipeline(c, g->p_replace);
        SDL_BindGPUComputeStorageBuffers(c, 0, ro, 4);
        SDL_PushGPUComputeUniformData(cb, 0, u, sizeof u);
        SDL_DispatchGPUCompute(c, ((Uint32)tw + 15) / 16, ((Uint32)h + 15) / 16, 1);
        SDL_EndGPUComputePass(c);
    }
    profile_mark(g, cb, "replace_end");
    static int split = -1; if (split < 0) split = getenv("F117_GPU_PRESENT_SPLIT") != NULL;     /* diagnostic: the present pass in a command buffer of its own after the first one finished */
    if (split && g->targets[0] && g->p_present) {
        SDL_GPUFence *f1 = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
        if (!f1) return 0;
        SDL_WaitForGPUFences(g->dev, true, &f1, 1);
        SDL_ReleaseGPUFence(g->dev, f1);
        cb = SDL_AcquireGPUCommandBuffer(g->dev);
        if (!cb) return 0;
    }
    if (g->targets[0] && g->p_present) {        /* piece 3: the picture into the wrapped texture, on the GPU */
        SDL_GPUStorageTextureReadWriteBinding tb = { g->targets[g->cur_write], 0, 0, false };
        const Uint32 u[4] = { (Uint32)tw, (Uint32)h, 0, 0 };
        SDL_GPUComputePass *c = SDL_BeginGPUComputePass(cb, &tb, 1, NULL, 0);
        SDL_BindGPUComputePipeline(c, g->p_present);
        SDL_BindGPUComputeStorageBuffers(c, 0, &b_out, 1);
        SDL_PushGPUComputeUniformData(cb, 0, u, sizeof u);
        SDL_DispatchGPUCompute(c, ((Uint32)tw + 15) / 16, ((Uint32)h + 15) / 16, 1);
        SDL_EndGPUComputePass(c);
        static int twice = -1; if (twice < 0) twice = getenv("F117_GPU_PRESENT_TWICE") != NULL;     /* diagnostic: the same pass again into the other texture, to compare the two */
        if (twice) {
            SDL_GPUStorageTextureReadWriteBinding tb2 = { g->targets[g->cur_write ^ 1], 0, 0, false };
            SDL_GPUComputePass *c2 = SDL_BeginGPUComputePass(cb, &tb2, 1, NULL, 0);
            SDL_BindGPUComputePipeline(c2, g->p_present);
            SDL_BindGPUComputeStorageBuffers(c2, 0, &b_out, 1);
            SDL_PushGPUComputeUniformData(cb, 0, u, sizeof u);
            SDL_DispatchGPUCompute(c2, ((Uint32)tw + 15) / 16, ((Uint32)h + 15) / 16, 1);
            SDL_EndGPUComputePass(c2);
        }
    }
    profile_mark(g, cb, "present_end");
    if (wide_out) {
        cp = SDL_BeginGPUCopyPass(cb);
        SDL_GPUBufferRegion bk = { b_out, 0, pbytes }; SDL_GPUTransferBufferLocation into = { GS(g)->down, 0 }; SDL_DownloadFromGPUBuffer(cp, &bk, &into);
        SDL_EndGPUCopyPass(cp);
    }
    profile_mark(g, cb, "download_end");
    profile_resolve(g, cb);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
    if (!fence) return 0;
    const Uint64 t_wait = SDL_GetTicksNS();
    SDL_WaitForGPUFences(g->dev, true, &fence, 1);
    SDL_ReleaseGPUFence(g->dev, fence);
#ifdef F117_GPU_TIMESTAMPS
    if (g->profile) {
        if (g->profile_failed || !f117_timestamp_read_after_fence(g->profile, g->profile_log)) {
            fprintf(stderr, "[gpu-timestamp] invalid sample; profiling disabled\n");
            f117_timestamp_destroy(g->profile); g->profile = NULL;
        }
        g->profile_pending = 0;
    }
#endif
    const Uint64 t_gpu = SDL_GetTicksNS();
    GS(g)->wait_ms = (double)(t_gpu - t_wait) / 1e6;          /* how long the caller blocked on the GPU: its work not hidden behind the caller's */
    if (g->targets[0]) { g->last_written = g->cur_write; g->cur_write ^= 1; }

    if (wide_out) {
        const void *got = SDL_MapGPUTransferBuffer(g->dev, GS(g)->down, false);
        if (!got) return 0;
        memcpy(wide_out, got, pbytes);
        SDL_UnmapGPUTransferBuffer(g->dev, GS(g)->down);
    }
    GS(g)->gpu_ms = (double)(t_gpu - GS(g)->t_begin) / 1e6;       /* from the end of the first half: the raster passes overlap what the caller did meanwhile */
    GS(g)->copy_ms = (double)(SDL_GetTicksNS() - t_gpu) / 1e6;
    return 1;
}

int gpu_compose_run(gpu_compose *g, const gpu_compose_in *in, uint32_t *wide_out)
{
    return gpu_compose_begin(g, in) && gpu_compose_finish(g, in->src, in->tag, in->dac, wide_out);
}

int gpu_compose_make_targets(gpu_compose *g, int w, int h)
{
    if (!g || !g->p_present || w < 1 || h < 1) return 0;
    for (int k = 0; k < 2; k++) if (g->targets[k]) { SDL_ReleaseGPUTexture(g->dev, g->targets[k]); g->targets[k] = NULL; }
    if (g->tgt_down) { SDL_ReleaseGPUTransferBuffer(g->dev, g->tgt_down); g->tgt_down = NULL; }
    SDL_GPUTextureCreateInfo ti;
    memset(&ti, 0, sizeof ti);
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    ti.width = (Uint32)w; ti.height = (Uint32)h; ti.layer_count_or_depth = 1; ti.num_levels = 1;
    for (int k = 0; k < 2; k++) {
        g->targets[k] = SDL_CreateGPUTexture(g->dev, &ti);
        if (!g->targets[k]) { for (int j = 0; j < 2; j++) if (g->targets[j]) { SDL_ReleaseGPUTexture(g->dev, g->targets[j]); g->targets[j] = NULL; } return 0; }
    }
    g->tgt_w = w; g->tgt_h = h; g->cur_write = 0; g->last_written = 0;
    return 1;
}

SDL_GPUTexture *gpu_compose_target(gpu_compose *g, int i) { return g && i >= 0 && i < 2 ? g->targets[i] : NULL; }
int gpu_compose_last_written(const gpu_compose *g) { return g ? g->last_written : 0; }

int g_read_other;
int gpu_compose_read_target(gpu_compose *g, uint32_t *out)
{
    if (!g || !g->targets[0] || !out) return 0;
    const Uint32 bytes = (Uint32)((size_t)g->tgt_w * g->tgt_h * 4);
    if (!g->tgt_down) {
        SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, bytes, 0 };
        g->tgt_down = SDL_CreateGPUTransferBuffer(g->dev, &ti);
        if (!g->tgt_down) return 0;
    }
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(g->dev);
    if (!cb) return 0;
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTextureRegion tr;
    memset(&tr, 0, sizeof tr);
    tr.texture = g->targets[g_read_other ? g->last_written ^ 1 : g->last_written]; tr.w = (Uint32)g->tgt_w; tr.h = (Uint32)g->tgt_h; tr.d = 1;
    SDL_GPUTextureTransferInfo xi = { g->tgt_down, 0, 0, 0 };
    SDL_DownloadFromGPUTexture(cp, &tr, &xi);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
    if (!fence) return 0;
    SDL_WaitForGPUFences(g->dev, true, &fence, 1);
    SDL_ReleaseGPUFence(g->dev, fence);
    const uint8_t *p = (const uint8_t *)SDL_MapGPUTransferBuffer(g->dev, g->tgt_down, false);
    if (!p) return 0;
    for (size_t i = 0; i < (size_t)g->tgt_w * g->tgt_h; i++) out[i] = (uint32_t)p[4 * i + 3] << 24 | (uint32_t)p[4 * i] << 16 | (uint32_t)p[4 * i + 1] << 8 | p[4 * i + 2];
    SDL_UnmapGPUTransferBuffer(g->dev, g->tgt_down);
    return 1;
}
