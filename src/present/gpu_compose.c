/* gpu_compose.c - the Remastered composite on the GPU (gpu_compose.h, docs/gpu-compositor.md). The setup is the offline tools' (gpu_stage, gpu_polys, gpu_strokes,
 * gpu_replace): the CPU clips and projects the polygons and strokes, splits them into triangles and bins them into 16 x 16 tiles with the weights evaluated in
 * double at each tile's first pixel; the passes are the shaders in src/present/gpu. */
#include "gpu_compose.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TILE 16
#define MAX_POLY 16

typedef struct { double x, y, z; } dv3;
typedef struct { double x, y, iz; } sv;

typedef struct { SDL_GPUBuffer *buf; Uint32 cap; } gbuf;

typedef struct {
    float a1, b1, a2, b2, ciz, diz1, diz2;
    uint32_t w7, paint_index, paint_owner, kind;
    double cx, cy, a1d, b1d, a2d, b2d;
    int bbox[4];
} ctri;
typedef struct { ctri *t; size_t n, cap; } clist;

typedef struct { void *p; size_t n, cap; } vec;     /* a growing array of bytes */

struct gpu_compose {
    SDL_GPUDevice *dev;
    SDL_GPUComputePipeline *p_bands, *p_polys, *p_strokes, *p_replace;
    gbuf bt, pt, pe, po, pal, st, se, so, sw, hid, tf, src, tag, dac;
    gbuf colour, index_, invz, owner, strokez, outpix;
    Uint32 pix_cap;
    SDL_GPUTransferBuffer *up, *down;
    Uint32 up_cap, down_cap;
    clist polys, strokes;
    vec band_words, poly_words, poly_ents, poly_off, stroke_tri_words, stroke_ents, stroke_off, stroke_words;
    double setup_ms, gpu_ms, copy_ms;
    int said;
};

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

static SDL_GPUComputePipeline *load_pipeline(SDL_GPUDevice *dev, const char *dir, const char *name, Uint32 ro, Uint32 rw, Uint32 tx, Uint32 ty)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    size_t size = 0;
    void *code = SDL_LoadFile(path, &size);
    if (!code) return NULL;
    SDL_GPUComputePipelineCreateInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.code = (const Uint8 *)code; ci.code_size = size; ci.entrypoint = "main"; ci.format = SDL_GPU_SHADERFORMAT_DXIL;
    ci.num_readonly_storage_buffers = ro; ci.num_readwrite_storage_buffers = rw; ci.num_uniform_buffers = 1;
    ci.threadcount_x = tx; ci.threadcount_y = ty; ci.threadcount_z = 1;
    SDL_GPUComputePipeline *p = SDL_CreateGPUComputePipeline(dev, &ci);
    SDL_free(code);
    return p;
}

gpu_compose *gpu_compose_create(const char *shader_dir)
{
    if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_InitSubSystem(SDL_INIT_VIDEO)) return NULL;
    gpu_compose *g = (gpu_compose *)calloc(1, sizeof *g);
    if (!g) return NULL;
    g->dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, false, NULL);
    if (!g->dev) { free(g); return NULL; }
    g->p_bands = load_pipeline(g->dev, shader_dir, "gpu_bands_fast.dxil", 1, 4, 64, 1);
    g->p_polys = load_pipeline(g->dev, shader_dir, "gpu_polys.dxil", 4, 4, 16, 16);
    g->p_strokes = load_pipeline(g->dev, shader_dir, "gpu_strokes.dxil", 4, 6, 16, 16);
    g->p_replace = load_pipeline(g->dev, shader_dir, "gpu_replace.dxil", 4, 4, 16, 16);
    if (!g->p_bands || !g->p_polys || !g->p_strokes || !g->p_replace) { gpu_compose_destroy(g); return NULL; }
    return g;
}

void gpu_compose_destroy(gpu_compose *g)
{
    if (!g) return;
    if (g->dev) {
        gbuf *all[] = { &g->bt, &g->pt, &g->pe, &g->po, &g->pal, &g->st, &g->se, &g->so, &g->sw, &g->hid, &g->tf, &g->src, &g->tag, &g->dac,
                        &g->colour, &g->index_, &g->invz, &g->owner, &g->strokez, &g->outpix };
        for (size_t i = 0; i < sizeof all / sizeof *all; i++) if (all[i]->buf) SDL_ReleaseGPUBuffer(g->dev, all[i]->buf);
        if (g->up) SDL_ReleaseGPUTransferBuffer(g->dev, g->up);
        if (g->down) SDL_ReleaseGPUTransferBuffer(g->dev, g->down);
        if (g->p_bands) SDL_ReleaseGPUComputePipeline(g->dev, g->p_bands);
        if (g->p_polys) SDL_ReleaseGPUComputePipeline(g->dev, g->p_polys);
        if (g->p_strokes) SDL_ReleaseGPUComputePipeline(g->dev, g->p_strokes);
        if (g->p_replace) SDL_ReleaseGPUComputePipeline(g->dev, g->p_replace);
        SDL_DestroyGPUDevice(g->dev);
    }
    free(g->polys.t); free(g->strokes.t);
    free(g->band_words.p); free(g->poly_words.p); free(g->poly_ents.p); free(g->poly_off.p);
    free(g->stroke_tri_words.p); free(g->stroke_ents.p); free(g->stroke_off.p); free(g->stroke_words.p);
    free(g);
}

void gpu_compose_times(const gpu_compose *g, double *setup_ms, double *gpu_ms, double *copy_ms)
{
    if (copy_ms) *copy_ms = g ? g->copy_ms : 0.0;
    if (setup_ms) *setup_ms = g ? g->setup_ms : 0.0;
    if (gpu_ms) *gpu_ms = g ? g->gpu_ms : 0.0;
}

/* ---- CPU setup ---- */

static int c_add(clist *l, uint32_t w7, uint32_t paint_index, uint32_t paint_owner, uint32_t kind, sv a, sv b, sv c, int w, int h)
{
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
    const uint32_t kind = r->kind == REMASTER_T_LOGIC ? (uint32_t)r->op + 1u : 0u;
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
    if (!c_add(l, id, 0, 0, 0, q0, q1, q2, w, h) || !c_add(l, id, 0, 0, 0, q0, q2, q3, w, h)) return 0;
    memset(words, 0, 16 * 4);
    words[0] = r->color; words[1] = r->paint_index; words[2] = r->paint_owner; words[3] = (uint32_t)line_point; words[4] = (uint32_t)radius;
    const float f[9] = { (float)sa.x, (float)sa.y, (float)sb.x, (float)sb.y, (float)depth_dx, (float)depth_dy, (float)sa.x, (float)sa.y, (float)sa.iz };
    for (int k = 0; k < 9; k++) memcpy(&words[5 + k], &f[k], 4);
    *drawn = 1;
    return 1;
}

/* Bin the triangles into tiles in their order: offsets (tiles + 1 words) and entries (3 words each: triangle, the two weights at the tile's first pixel centre). */
static int bin_tiles(const clist *l, int tilesx, int tilesy, vec *off_v, vec *ents_v, size_t *nents_out)
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
            ents = (uint32_t *)vec_reserve(ents_v, (nents ? nents : 1) * 12);
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
                    if (m1 < -1e-3 || m2 < -1e-3 || m3 < -1e-3) continue;
                    const int tile = ty * tilesx + tx;
                    if (pass == 0) { off[tile + 1]++; count++; }
                    else {
                        const uint32_t at = cursor[tile]++;
                        const float f1 = (float)l1, f2 = (float)l2;
                        ents[3 * at] = (uint32_t)ti; memcpy(&ents[3 * at + 1], &f1, 4); memcpy(&ents[3 * at + 2], &f2, 4);
                    }
                }
        }
        if (pass == 0) { nents = count; for (int k = 0; k < ntiles; k++) off[k + 1] += off[k]; }
    }
    free(cursor);
    *nents_out = nents;
    return 1;
}

static void tri_words(uint32_t *wd, const ctri *tr)
{
    memcpy(&wd[0], &tr->a1, 4); memcpy(&wd[1], &tr->b1, 4); memcpy(&wd[2], &tr->a2, 4); memcpy(&wd[3], &tr->b2, 4);
    memcpy(&wd[4], &tr->ciz, 4); memcpy(&wd[5], &tr->diz1, 4); memcpy(&wd[6], &tr->diz2, 4);
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
    memset(wd, 0, 24 * 4);
    for (int k = 0; k < 18; k++) memcpy(&wd[k], &f[k], 4);
    wd[18] = r->paint_index;
    wd[19] = (uint32_t)x0; wd[20] = (uint32_t)x1; wd[21] = (uint32_t)y0; wd[22] = (uint32_t)y1;
    return 1;
}

typedef struct { gbuf *b; const void *data; Uint32 bytes; SDL_GPUBufferUsageFlags usage; } seg;

int gpu_compose_run(gpu_compose *g, const gpu_compose_in *in, uint32_t *wide_out)
{
    if (!g || !in || !wide_out) return 0;
    const Uint64 t_start = SDL_GetTicksNS();
    const int w = in->w, h = in->h, tw = in->tw;
    const size_t px = (size_t)tw * h;
    if (px * 4 > 0x7FFFFFFFu) { say(g, "the picture is too large"); return 0; }

    /* classify the records: the clear and the bands first, then polygons, then the lines; anything else is not handled */
    int phase = 0;
    g->polys.n = 0; g->strokes.n = 0;
    uint32_t nband = 0, nstrokes = 0;
    uint32_t *bw = (uint32_t *)vec_reserve(&g->band_words, (size_t)4096 * 24 * 4);
    uint32_t *sw = (uint32_t *)vec_reserve(&g->stroke_words, (size_t)(in->nrec ? in->nrec : 1) * 16 * 4);
    if (!bw || !sw) return 0;
    const size_t band_cap = g->band_words.cap / (24 * 4);
    for (uint32_t i = 0; i < in->nrec; i++) {
        const gpu_trace_rec *r = &in->recs[i];
        switch (r->kind) {
        case REMASTER_T_CLEAR: case REMASTER_T_ZRESET: case REMASTER_T_WINDOW: break;
        case REMASTER_T_SHADED: {
            if (phase != 0) { say(g, "a band after a polygon"); return 0; }
            if (r->n < 3 || r->n > 14) break;
            sv s[MAX_POLY]; double rgb[MAX_POLY][3]; int ok = 1;
            for (int k = 0; k < r->n; k++) {
                if (r->v[k].z < r->near_z) { ok = 0; break; }
                s[k].x = r->cx + r->sx * (double)r->v[k].x / r->v[k].z;
                s[k].y = r->cy + r->sy * (double)r->v[k].y / r->v[k].z;
                s[k].iz = (r->inv_depth_scale > 0.0 ? r->inv_depth_scale : 1.0) / (double)r->v[k].z;
                rgb[k][0] = r->colors[k] >> 16 & 255; rgb[k][1] = r->colors[k] >> 8 & 255; rgb[k][2] = r->colors[k] & 255;
            }
            if (!ok) break;
            for (int k = 1; k + 1 < r->n; k++) {
                const sv tr[3] = { s[0], s[k], s[k + 1] };
                const double col[3][3] = { { rgb[0][0], rgb[0][1], rgb[0][2] }, { rgb[k][0], rgb[k][1], rgb[k][2] }, { rgb[k + 1][0], rgb[k + 1][1], rgb[k + 1][2] } };
                if (nband >= band_cap) { say(g, "too many band triangles"); return 0; }
                if (band_triangle(bw + (size_t)nband * 24, r, tr, col, tw, h)) nband++;
            }
            break;
        }
        case REMASTER_T_POLYGON: case REMASTER_T_LOGIC:
            if (phase > 1) { say(g, "a polygon after a line"); return 0; }
            phase = 1;
            if (!add_polygon(&g->polys, r, tw, h)) return 0;
            break;
        case REMASTER_T_LINE: {
            if (!r->line_decal || !r->line_order) { say(g, "lines without the decal rules"); return 0; }
            phase = 2;
            int drawn = 0;
            if (!add_stroke(&g->strokes, r, tw, h, nstrokes, sw + (size_t)nstrokes * 16, &drawn)) return 0;
            if (drawn) nstrokes++;
            break;
        }
        default: say(g, "an unknown record"); return 0;
        }
    }

    const int tilesx = (tw + TILE - 1) / TILE, tilesy = (h + TILE - 1) / TILE;
    size_t npents = 0, nsents = 0;
    if (!bin_tiles(&g->polys, tilesx, tilesy, &g->poly_off, &g->poly_ents, &npents)) return 0;
    if (!bin_tiles(&g->strokes, tilesx, tilesy, &g->stroke_off, &g->stroke_ents, &nsents)) return 0;
    uint32_t *pw = (uint32_t *)vec_reserve(&g->poly_words, (g->polys.n ? g->polys.n : 1) * 48);
    uint32_t *stw = (uint32_t *)vec_reserve(&g->stroke_tri_words, (g->strokes.n ? g->strokes.n : 1) * 48);
    if (!pw || !stw) return 0;
    memset(pw, 0, (g->polys.n ? g->polys.n : 1) * 48); memset(stw, 0, (g->strokes.n ? g->strokes.n : 1) * 48);
    for (size_t k = 0; k < g->polys.n; k++) {
        const ctri *tr = &g->polys.t[k]; uint32_t *wd = pw + k * 12;
        tri_words(wd, tr); wd[7] = tr->w7; wd[8] = tr->paint_index; wd[9] = tr->paint_owner; wd[10] = tr->kind;
    }
    for (size_t k = 0; k < g->strokes.n; k++) { const ctri *tr = &g->strokes.t[k]; uint32_t *wd = stw + k * 12; tri_words(wd, tr); wd[7] = tr->w7; }
    uint32_t flags[256];
    for (int k = 0; k < 256; k++) flags[k] = in->tagflags[k];

    /* GPU resources */
    const Uint32 pbytes = (Uint32)(px * 4);
    const SDL_GPUBufferUsageFlags RO = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, RW = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    const size_t npix_game = (size_t)w * h;
    const Uint32 game_bytes = (Uint32)((npix_game + 7) & ~(size_t)3);
    const Uint32 nstroke_b = (Uint32)((nstrokes ? nstrokes : 1) * 4);
    seg segs[] = {
        { &g->bt, bw, (Uint32)((nband ? nband : 1) * 96), RO },
        { &g->pt, pw, (Uint32)((g->polys.n ? g->polys.n : 1) * 48), RO },
        { &g->pe, g->poly_ents.p, (Uint32)((npents ? npents : 1) * 12), RO },
        { &g->po, g->poly_off.p, (Uint32)(((size_t)tilesx * tilesy + 1) * 4), RO },
        { &g->pal, in->palette, 1024, RO },
        { &g->st, stw, (Uint32)((g->strokes.n ? g->strokes.n : 1) * 48), RO },
        { &g->se, g->stroke_ents.p, (Uint32)((nsents ? nsents : 1) * 12), RO },
        { &g->so, g->stroke_off.p, (Uint32)(((size_t)tilesx * tilesy + 1) * 4), RO },
        { &g->sw, sw, (Uint32)((nstrokes ? nstrokes : 1) * 64), RO },
        { &g->tf, flags, 1024, RO },
        { &g->src, in->src, game_bytes, RO },
        { &g->tag, in->tag, game_bytes, RO },
        { &g->dac, in->dac, 768, RO },
    };
    const int nseg = (int)(sizeof segs / sizeof *segs);
    Uint32 total = 0;
    for (int k = 0; k < nseg; k++) { if (!gbuf_ensure(g->dev, segs[k].b, segs[k].usage, segs[k].bytes)) return 0; total += (segs[k].bytes + 15u) & ~15u; }
    if (!gbuf_ensure(g->dev, &g->hid, RW, nstroke_b)) return 0;
    gbuf *pixbufs[] = { &g->colour, &g->index_, &g->invz, &g->owner, &g->strokez, &g->outpix };
    for (int k = 0; k < 6; k++) if (!gbuf_ensure(g->dev, pixbufs[k], RW, pbytes)) return 0;
    if (g->up_cap < total) {
        if (g->up) SDL_ReleaseGPUTransferBuffer(g->dev, g->up);
        SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, total + total / 2, 0 };
        g->up = SDL_CreateGPUTransferBuffer(g->dev, &ti); g->up_cap = g->up ? ti.size : 0;
        if (!g->up) return 0;
    }
    if (g->down_cap < pbytes) {
        if (g->down) SDL_ReleaseGPUTransferBuffer(g->dev, g->down);
        SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, pbytes, 0 };
        g->down = SDL_CreateGPUTransferBuffer(g->dev, &ti); g->down_cap = g->down ? pbytes : 0;
        if (!g->down) return 0;
    }
    {
        Uint8 *m = (Uint8 *)SDL_MapGPUTransferBuffer(g->dev, g->up, true), *p = m;
        if (!m) return 0;
        for (int k = 0; k < nseg; k++) {
            const size_t have = segs[k].data ? segs[k].bytes : 0;
            memset(p, 0, segs[k].bytes);
            if (segs[k].data) {
                size_t n = have;
                if (segs[k].b == &g->src || segs[k].b == &g->tag) n = npix_game;
                memcpy(p, segs[k].data, n < segs[k].bytes ? n : segs[k].bytes);
            }
            p += (segs[k].bytes + 15u) & ~15u;
        }
        SDL_UnmapGPUTransferBuffer(g->dev, g->up);
    }
    const Uint64 t_setup = SDL_GetTicksNS();

    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(g->dev);
    if (!cb) return 0;
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    {
        Uint32 at = 0;
        for (int k = 0; k < nseg; k++) {
            SDL_GPUTransferBufferLocation from = { g->up, at };
            SDL_GPUBufferRegion to = { segs[k].b->buf, 0, segs[k].bytes };
            SDL_UploadToGPUBuffer(cp, &from, &to, false);
            at += (segs[k].bytes + 15u) & ~15u;
        }
    }
    SDL_EndGPUCopyPass(cp);

    SDL_GPUBuffer *b_colour = g->colour.buf, *b_index = g->index_.buf, *b_invz = g->invz.buf, *b_owner = g->owner.buf, *b_strokez = g->strokez.buf, *b_out = g->outpix.buf;
    {   /* bands: the clear, the sky and ground, depth and owner zero */
        SDL_GPUStorageBufferReadWriteBinding rw[4] = { { b_colour, false }, { b_index, false }, { b_invz, false }, { b_owner, false } };
        const Uint32 u[4] = { (Uint32)tw, (Uint32)h, nband, 0 };
        SDL_GPUComputePass *c = SDL_BeginGPUComputePass(cb, NULL, 0, rw, 4);
        SDL_BindGPUComputePipeline(c, g->p_bands);
        SDL_BindGPUComputeStorageBuffers(c, 0, &g->bt.buf, 1);
        SDL_PushGPUComputeUniformData(cb, 0, u, sizeof u);
        SDL_DispatchGPUCompute(c, ((Uint32)tw + 63) / 64, (Uint32)h, 1);
        SDL_EndGPUComputePass(c);
    }
    if (g->polys.n) {
        SDL_GPUStorageBufferReadWriteBinding rw[4] = { { b_colour, false }, { b_index, false }, { b_invz, false }, { b_owner, false } };
        SDL_GPUBuffer *ro[4] = { g->pt.buf, g->pe.buf, g->po.buf, g->pal.buf };
        const Uint32 u[4] = { (Uint32)tw, (Uint32)h, (Uint32)tilesx, 0 };
        SDL_GPUComputePass *c = SDL_BeginGPUComputePass(cb, NULL, 0, rw, 4);
        SDL_BindGPUComputePipeline(c, g->p_polys);
        SDL_BindGPUComputeStorageBuffers(c, 0, ro, 4);
        SDL_PushGPUComputeUniformData(cb, 0, u, sizeof u);
        SDL_DispatchGPUCompute(c, (Uint32)tilesx, (Uint32)tilesy, 1);
        SDL_EndGPUComputePass(c);
    }
    for (int mode = nstrokes ? 0 : 1; mode < 2; mode++) {      /* strokes: the points behind their own surface, then the pixels (always: it writes the stroke depth the replace step reads) */
        SDL_GPUStorageBufferReadWriteBinding rw[6] = { { b_colour, false }, { b_index, false }, { b_invz, false }, { b_owner, false }, { g->hid.buf, false }, { b_strokez, false } };
        SDL_GPUBuffer *ro[4] = { g->st.buf, g->se.buf, g->so.buf, g->sw.buf };
        const Uint32 u[8] = { (Uint32)mode, (Uint32)tw, (Uint32)h, (Uint32)tilesx, nstrokes, 0, 0, 0 };
        SDL_GPUComputePass *c = SDL_BeginGPUComputePass(cb, NULL, 0, rw, 6);
        SDL_BindGPUComputePipeline(c, g->p_strokes);
        SDL_BindGPUComputeStorageBuffers(c, 0, ro, 4);
        SDL_PushGPUComputeUniformData(cb, 0, u, sizeof u);
        if (mode == 0) SDL_DispatchGPUCompute(c, 256, (nstrokes / 4096 + 16) / 16, 1);
        else SDL_DispatchGPUCompute(c, (Uint32)tilesx, (Uint32)tilesy, 1);
        SDL_EndGPUComputePass(c);
    }
    {   /* the replace step */
        SDL_GPUStorageBufferReadWriteBinding rw[4] = { { b_colour, false }, { b_invz, false }, { b_strokez, false }, { b_out, false } };
        SDL_GPUBuffer *ro[4] = { g->tf.buf, g->src.buf, g->tag.buf, g->dac.buf };
        const int32_t u[12] = { w, h, tw, in->xoff, in->x0, in->x1, in->y0, in->y1, in->complete, 0, 0, 0 };
        SDL_GPUComputePass *c = SDL_BeginGPUComputePass(cb, NULL, 0, rw, 4);
        SDL_BindGPUComputePipeline(c, g->p_replace);
        SDL_BindGPUComputeStorageBuffers(c, 0, ro, 4);
        SDL_PushGPUComputeUniformData(cb, 0, u, sizeof u);
        SDL_DispatchGPUCompute(c, ((Uint32)tw + 15) / 16, ((Uint32)h + 15) / 16, 1);
        SDL_EndGPUComputePass(c);
    }
    cp = SDL_BeginGPUCopyPass(cb);
    { SDL_GPUBufferRegion bk = { b_out, 0, pbytes }; SDL_GPUTransferBufferLocation into = { g->down, 0 }; SDL_DownloadFromGPUBuffer(cp, &bk, &into); }
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
    if (!fence) return 0;
    SDL_WaitForGPUFences(g->dev, true, &fence, 1);
    SDL_ReleaseGPUFence(g->dev, fence);
    const Uint64 t_gpu = SDL_GetTicksNS();
    const void *got = SDL_MapGPUTransferBuffer(g->dev, g->down, false);
    if (!got) return 0;
    memcpy(wide_out, got, pbytes);
    SDL_UnmapGPUTransferBuffer(g->dev, g->down);
    g->copy_ms = (double)(SDL_GetTicksNS() - t_gpu) / 1e6;
    g->setup_ms = (double)(t_setup - t_start) / 1e6;
    g->gpu_ms = (double)(t_gpu - t_setup) / 1e6;
    return 1;
}
