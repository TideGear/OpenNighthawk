/* remaster_live.c - Remastered's view of a running game through the machine's execution hooks (see remaster_live.h). */
#include "remaster_live.h"

#include "remaster_raster.h"
#include "remaster_scene.h"
#include "scene_mesh.h"
#include "band_state.h"
#include "diagtag.h"
#include "remaster_project.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* the watched entries, as {segment relative to the load segment, offset}: k = 0 the model request, 1 model_faces (VGAME
 * 120A:0C76, the faces of a model that is visible: run after the pose is fully prepared, so the part matrices, planes and
 * vertices are where remaster_scene_add reads them), 2 the per-frame draw routine */
static const struct { uint16_t seg, ip; } WATCH[3] = {
    { 0x120A, 0x00CF },
    { 0x120A, 0x0C76 },
    { 0x0000, 0x1450 },
};

struct remaster_live {
    remaster_scene *scene;
    remaster_frame frame;
    int pending;
    uint16_t model, bank;
    int16_t position[3];
    unsigned frames, want_frame;
    int w, h;
    char *out_path;
    int written;
    unsigned requests, added, refused, overwritten, ortho_skipped;
    /* F117_REMASTER_VALIDATE: per model, how many vertices' projections match the game's own (the 8-byte records at
     * DS:D6B4 + 8 * id, signed pixel offsets from the origin) and how many do not */
    struct { uint16_t model; unsigned seen, match, differ, behind_mine, behind_game; } check[256];
    unsigned nchecks;
    int32_t band_rec[BAND_REC_BASE];  /* the sky and ground band pass's starting state (band_state.h), for the frame in progress */
    int have_band;
    remaster_frame done;                /* the last completed frame, with its band state, origin and window, for compositing */
    int32_t done_band[BAND_REC_BASE];
    int done_have_band, have_done, done_origin[2], done_window[4];
    int origin[2], window[4];       /* the game's screen origin [8602],[8604] and clip window [85FA],[85FC],[85FE],[8600] */
};

remaster_live *remaster_live_new(int w, int h, unsigned frame, const char *out_path)
{
    remaster_live *r = (remaster_live *)calloc(1, sizeof *r);
    if (!r) return NULL;
    r->scene = remaster_scene_new();
    r->w = w; r->h = h; r->want_frame = frame;
    if (out_path) {
        r->out_path = (char *)malloc(strlen(out_path) + 1);
        if (r->out_path) strcpy(r->out_path, out_path);
    }
    if (!r->scene) { remaster_live_free(r); return NULL; }
    return r;
}

void remaster_live_free(remaster_live *r)
{
    if (!r) return;
    remaster_scene_free(r->scene);
    remaster_frame_free(&r->frame);
    remaster_frame_free(&r->done);
    free(r->out_path);
    free(r);
}

unsigned remaster_live_arm(remaster_live *r, machine_t *m, uint16_t load_seg)
{
    (void)r;
    const unsigned base = m->exec_hook_n;
    for (unsigned k = 0; k < 3 && m->exec_hook_n < EXEC_HOOKS_MAX; k++) {
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
static uint32_t dac_colour(const machine_t *m, unsigned index)
{
    const uint8_t *d = &m->dac[(index & 255) * 3];
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
static void draw_scene(const machine_t *m, remaster_target *t, const remaster_frame *fr, const int32_t band_rec[],
                       int have_band, const int origin[2], const int window[4], double ux, double uy)
{
    t->cx = origin[0] * ux; t->cy = origin[1] * uy;           /* the game's origin, in its 320 x 200 pixels */
    t->sx = 256.0 * ux; t->sy = 256.0 * uy;
    t->near_z = 0x10000;
    uint32_t palette[256];
    for (unsigned i = 0; i < 256; i++) palette[i] = dac_colour(m, i);
    t->palette = palette;
    t->paint_index = REMASTER_NO_INDEX;
    remaster_clear(t, 0xFF000000u);
    if (have_band) {                            /* the sky and ground bands, as far as anything: the game's own state model */
        band_state st;
        band_exact_list ex;
        band_fine_list fine;
        const int loaded = band_state_load(band_rec, BAND_REC_BASE, &st);
        const int rx = loaded ? band_exact(&st, &ex) : -1, rf = loaded && !rx ? band_fine(&st, &ex, &fine) : -1;
        if (loaded && !rx && !rf) {
            const double far_z = 10000000.0;
            for (int q = 0; q < fine.n; q++) {
                if (!fine.q[q].valid) continue;
                remaster_v3 v[4];
                for (int k = 0; k < 4; k++) {   /* a screen point as a camera-space point at a very large depth */
                    v[k].x = (int32_t)((fine.q[q].x[k] * ux - t->cx) * far_z / t->sx);
                    v[k].y = (int32_t)((fine.q[q].y[k] * uy - t->cy) * far_z / t->sy);
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
            for (long i = 0; i < (long)t->w * t->h; i++) t->inv_z[i] = 0.0f;   /* the bands are behind everything */
        }
    }
    for (size_t i = 0; i < fr->n; i++) {
        const remaster_prim *p = &fr->prim[i];
        uint32_t colour;
        if (!word_colour(m, p->colour_word, p->darken, &colour)) continue;
        unsigned idx = 0;
        const int style = word_style(p->colour_word, p->darken, &idx);
        const double line_w = ux > 1.0 ? ux : 1.0;                    /* one of the original's pixels, at least one of ours */
        if (p->kind == SCENE_POLYGON && p->n >= 3 && style >= 1) {     /* AND, OR, stipple: combine with what is under it */
            remaster_polygon_logic(t, p->v, p->n, style == 1 ? REMASTER_AND : style == 2 ? REMASTER_OR : REMASTER_STIPPLE, idx);
            continue;
        }
        t->paint_index = (uint16_t)idx;
        if (p->kind == SCENE_POLYGON && p->n >= 3) remaster_polygon(t, p->v, p->n, colour);
        else if (p->kind == SCENE_LINES)
            for (int k = 0; k + 1 < p->n; k++) remaster_line(t, p->v[k], p->v[k + 1], line_w, colour);
        else if (p->kind == SCENE_DOTS)
            for (int k = 0; k < p->n; k++) remaster_line(t, p->v[k], p->v[k], line_w * 1.5, colour);
    }
    {                                           /* nothing outside the game's clip window */
        const int x0 = (int)(window[0] * ux), x1 = (int)((window[2] + 1) * ux);
        const int y0 = (int)(window[1] * uy), y1 = (int)((window[3] + 1) * uy);
        for (int y = 0; y < t->h; y++)
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
static uint8_t nearest_index(const uint8_t *dac, uint32_t c)
{
    static struct { uint32_t colour; uint8_t index; uint8_t valid; const uint8_t *dac; } cache[4096];
    const unsigned slot = (c * 2654435761u) >> 20 & 4095;
    if (cache[slot].valid && cache[slot].colour == c && cache[slot].dac == dac) return cache[slot].index;
    const int cr = (int)(c >> 16 & 255), cg = (int)(c >> 8 & 255), cb = (int)(c & 255);
    unsigned best = 0;
    long bestd = -1;
    for (unsigned i = 0; i < 256; i++) {
        const int r = dac[i * 3] << 2 | dac[i * 3] >> 4, g = dac[i * 3 + 1] << 2 | dac[i * 3 + 1] >> 4, b = dac[i * 3 + 2] << 2 | dac[i * 3 + 2] >> 4;
        const long d = (long)(r - cr) * (r - cr) + (long)(g - cg) * (g - cg) + (long)(b - cb) * (b - cb);
        if (bestd < 0 || d < bestd) { bestd = d; best = i; }
    }
    cache[slot].colour = c; cache[slot].index = (uint8_t)best; cache[slot].valid = 1; cache[slot].dac = dac;
    return (uint8_t)best;
}

int remaster_live_composite(remaster_live *r, const machine_t *m, int n, const uint8_t *src, const uint8_t *tag,
                            const uint8_t *dac, uint8_t *dst, uint32_t *rgb, uint8_t *mask)
{
    if (!r || !r->have_done || n < 1 || !src || !tag || !dac || !dst) return 0;
    const int w = 320 * n, h = 200 * n;
    remaster_target t;
    t.w = w; t.h = h;
    t.color = (uint32_t *)malloc((size_t)w * h * 4);
    t.inv_z = (float *)malloc((size_t)w * h * 4);
    t.index = (uint16_t *)malloc((size_t)w * h * 2);
    if (!t.color || !t.inv_z || !t.index) { free(t.color); free(t.inv_z); free(t.index); return 0; }
    draw_scene(m, &t, &r->done, r->done_band, r->done_have_band, r->done_origin, r->done_window, n, n);
    {                                           /* F117_REMASTER_DUMP=FILE.ppm: the render itself, once, for a look */
        static int dumped; const char *path = getenv("F117_REMASTER_DUMP");
        if (path && !dumped && r->done.n > 50) {
            FILE *f = fopen(path, "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", w, h);
                for (long i = 0; i < (long)w * h; i++) { const uint32_t c = t.color[i]; const unsigned char px[3] = { (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c }; fwrite(px, 1, 3, f); }
                fclose(f); dumped = 1;
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
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < h; y++)
        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < w; x++) {
            const long i = (long)y * w + x;
            /* Where the Enhanced picture drew a model polygon (not a sky or ground strip) and Remastered has no model
             * geometry (the terrain polygons, which are not model requests yet), the original pixel stays. */
            if (is_scene_tag(tag[i]) && (t.inv_z[i] > 0.0f || is_span_tag(tag[i]))) {
                dst[i] = nearest_index(dac, t.color[i]);       /* the palette index, for anything that wants one */
                if (rgb && mask) { rgb[i] = t.color[i]; mask[i] = 1; }   /* and the true colour, which is what is shown */
                replaced++;
            }
        }
    free(t.color);
    free(t.inv_z);
    free(t.index);
    return replaced > 0;
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
        if (mine_y && (int32_t)gx == px && (int32_t)gy == py) r->check[slot].match++; else r->check[slot].differ++;
    }
}

static void report_validation(const remaster_live *r)
{
    fprintf(stderr, "[validate] model: vertices matching the game's own projection / differing (behind the eye here / there)\n");
    for (unsigned i = 0; i < r->nchecks; i++)
        if (r->check[i].differ)
            fprintf(stderr, "[validate] model %u: %u match, %u differ (%u behind here, %u behind there) of %u\n", (unsigned)r->check[i].model, r->check[i].match, r->check[i].differ, r->check[i].behind_mine,
                    r->check[i].behind_game, r->check[i].seen);
    unsigned total = 0, bad = 0;
    for (unsigned i = 0; i < r->nchecks; i++) { total += r->check[i].seen; bad += r->check[i].differ; }
    fprintf(stderr, "[validate] %u models, %u vertices, %u differ\n", r->nchecks, total, bad);
}

int remaster_live_exec(remaster_live *r, machine_t *m, unsigned k)
{
    if (k == 0) {                                           /* the model request: return IP, CS, then the model */
        const uint16_t ss = m->cpu.seg[S_SS], sp = m->cpu.r[R_SP];
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
    } else if (k == 1) {                                    /* the model's faces are about to be drawn: the pose is prepared */
        if (r->pending && m->mem[(((uint32_t)m->cpu.seg[S_DS] << 4) + 0x7D8E) & 0xFFFFF]) {
            r->pending = 0;                                 /* orthographic ([7D8E] not zero): a map tile, not part of the 3D view */
            r->ortho_skipped++;
        } else if (r->pending) {
            r->pending = 0;
            const int n = remaster_scene_add(r->scene, m->mem, 0x100000, m->cpu.seg[S_DS], r->bank, r->model, r->position, &r->frame);
            if (n < 0) r->refused++; else r->added++;
            if (n >= 0 && validate_on()) validate_model(r, m);
        }
    } else if (k == 2) {                                    /* the per-frame draw routine: the last frame is complete */
        if (validate_on() && r->frames == 40) report_validation(r);
        if (r->out_path && !r->written && r->frames == r->want_frame && r->frame.n) write_frame(r, m);
        {                                       /* the completed frame becomes the one a composite uses */
            remaster_frame tmp = r->done; r->done = r->frame; r->frame = tmp;
            memcpy(r->done_band, r->band_rec, sizeof r->done_band);
            r->done_have_band = r->have_band; r->have_done = r->done.n > 0;
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
        remaster_frame_clear(&r->frame);
        r->pending = 0;
    }
    return 0;
}

size_t remaster_live_prims(const remaster_live *r) { return r->frame.n; }
unsigned remaster_live_frames(const remaster_live *r) { return r->frames; }
