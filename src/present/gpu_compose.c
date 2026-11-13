/* gpu_compose.c - the Remastered composite on the GPU (gpu_compose.h, docs/gpu-compositor.md). The setup is the offline tools' (gpu_stage, gpu_polys, gpu_strokes,
 * gpu_replace): the CPU clips and projects the polygons and strokes, splits them into triangles and bins them into 16 x 16 tiles with the weights evaluated in
 * double at each tile's first pixel; the passes are the shaders in src/present/gpu. */
#include "gpu_compose.h"

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
#define SETUP_CHUNKS 32     /* the classify loop's record ranges (more than threads, for balance) */
#define MAX_POLY 16

typedef struct { double x, y, z; } dv3;
typedef struct { double x, y, iz; } sv;

typedef struct { SDL_GPUBuffer *buf; Uint32 cap; } gbuf;

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

struct gpu_compose {
    SDL_GPUDevice *dev;
    SDL_GPUComputePipeline *p_bands, *p_polys, *p_strokes, *p_replace, *p_present;
    int own_device;
    SDL_GPUTexture *targets[2];
    int cur_write, last_written;
    int tgt_w, tgt_h;
    SDL_GPUTransferBuffer *tgt_down;
    gbuf bt, pt, pe, po, pal, st, se, so, sw, hid, tf, src, tag, dac;
    gbuf colour, index_, invz, owner, strokez, outpix;
    Uint32 pix_cap;
    SDL_GPUTransferBuffer *up, *down;
    Uint32 up_cap, down_cap;
    clist polys, strokes;
    clist cpoly[SETUP_CHUNKS], cstroke[SETUP_CHUNKS];     /* the classify loop's per-chunk lists, merged in order into polys and strokes */
    vec cword[SETUP_CHUNKS];
    decal_face_cache faces;
    vec band_words, poly_words, poly_ents, poly_off, stroke_tri_words, stroke_ents, stroke_off, stroke_words;
    double setup_ms, gpu_ms, copy_ms, wait_ms;
    int said;
    int begun;                              /* gpu_compose_begin has run and gpu_compose_finish has not */
    gpu_compose_in params;                  /* the begun frame's replace parameters (the pointers in it are not kept) */
    Uint32 pbytes, game_bytes;
    size_t npix_game;
    SDL_GPUTransferBuffer *up2;             /* the game's picture, tags and palette, uploaded by the second half */
    Uint32 up2_cap;
    Uint64 t_begin;
    void *profile;
    void *(*profile_list)(SDL_GPUCommandBuffer *);
    FILE *profile_log;
    int profile_pending, profile_failed;
};

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
        gbuf *all[] = { &g->bt, &g->pt, &g->pe, &g->po, &g->pal, &g->st, &g->se, &g->so, &g->sw, &g->hid, &g->tf, &g->src, &g->tag, &g->dac,
                        &g->colour, &g->index_, &g->invz, &g->owner, &g->strokez, &g->outpix };
        for (size_t i = 0; i < sizeof all / sizeof *all; i++) if (all[i]->buf) SDL_ReleaseGPUBuffer(g->dev, all[i]->buf);
        if (g->up) SDL_ReleaseGPUTransferBuffer(g->dev, g->up);
        if (g->down) SDL_ReleaseGPUTransferBuffer(g->dev, g->down);
        if (g->up2) SDL_ReleaseGPUTransferBuffer(g->dev, g->up2);
        if (g->p_bands) SDL_ReleaseGPUComputePipeline(g->dev, g->p_bands);
        if (g->p_polys) SDL_ReleaseGPUComputePipeline(g->dev, g->p_polys);
        if (g->p_strokes) SDL_ReleaseGPUComputePipeline(g->dev, g->p_strokes);
        if (g->p_replace) SDL_ReleaseGPUComputePipeline(g->dev, g->p_replace);
        if (g->p_present) SDL_ReleaseGPUComputePipeline(g->dev, g->p_present);
        for (int k = 0; k < 2; k++) if (g->targets[k]) SDL_ReleaseGPUTexture(g->dev, g->targets[k]);
        if (g->tgt_down) SDL_ReleaseGPUTransferBuffer(g->dev, g->tgt_down);
        if (g->own_device) SDL_DestroyGPUDevice(g->dev);
    }
    free(g->faces.words);
    free(g->polys.t); free(g->strokes.t);
    for (int k = 0; k < SETUP_CHUNKS; k++) { free(g->cpoly[k].t); free(g->cstroke[k].t); free(g->cword[k].p); }
    setup_pool_shutdown();
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

double gpu_compose_wait_ms(const gpu_compose *g)
{
    return g ? g->wait_ms : 0.0;
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
#define CYC() __rdtsc()
#else
#define CYC() 0ull
#endif
static unsigned long long g_cyc[6], g_cnt[6];
#if defined(_MSC_VER)
#define CADD(a, i, v) _InterlockedExchangeAdd64((volatile __int64 *)&(a)[i], (__int64)(v))
#else
#define CADD(a, i, v) ((a)[i] += (v))
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

/* The binning used: the fast form in exact mode, the reference otherwise. F117_BIN_CHECK=1 also runs the reference and reports any difference in the offsets or the entries. */
static int bin_tiles(const clist *l, int tilesx, int tilesy, vec *off_v, vec *ents_v, size_t *nents_out)
{
    static int bin_ref = -1;            /* F117_BIN_REF=1: the reference binning (for paired A/B timing) */
    if (bin_ref < 0) bin_ref = getenv("F117_BIN_REF") != NULL;
    if (!exact_on() || bin_ref) return bin_tiles_ref(l, tilesx, tilesy, off_v, ents_v, nents_out);
    if (!getenv("F117_BIN_CHECK")) return bin_tiles_fast(l, tilesx, tilesy, off_v, ents_v, nents_out);
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
static setup_pool *g_pool;
static int g_setup_threads = -1;

static int pool_worker(void *u)
{
    setup_pool *p = (setup_pool *)u;
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
    setup_pool *p = g_pool;
    if (!p) return;
    g_pool = NULL;
    SDL_SetAtomicInt(&p->quit, 1);
    for (int i = 0; i < p->nworkers; i++) SDL_SignalSemaphore(p->go);
    for (int i = 0; i < p->nworkers; i++) SDL_WaitThread(p->th[i], NULL);
    SDL_DestroySemaphore(p->go); SDL_DestroySemaphore(p->done);
    free(p);
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
    if (nt > 1 && !g_pool) {
        setup_pool *p = (setup_pool *)calloc(1, sizeof *p);
        if (p) {
            p->go = SDL_CreateSemaphore(0); p->done = SDL_CreateSemaphore(0);
            for (int i = 0; i < nt - 1 && p->go && p->done; i++) { p->th[i] = SDL_CreateThread(pool_worker, "setup", p); if (!p->th[i]) break; p->nworkers++; }
            g_pool = p;
        }
    }
    setup_pool *p = g_pool;
    if (!p || p->nworkers == 0) { for (int j = 0; j < njobs; j++) fn(arg, j); return; }
    p->fn = fn; p->arg = arg; p->njobs = njobs;
    SDL_SetAtomicInt(&p->next, 0);
    for (int i = 0; i < p->nworkers; i++) SDL_SignalSemaphore(p->go);
    int j;
    while ((j = SDL_AddAtomicInt(&p->next, 1)) < njobs) fn(arg, j);
    for (int i = 0; i < p->nworkers; i++) SDL_WaitSemaphore(p->done);
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
    gpu_compose *g; const gpu_compose_in *in; int w, h;
    uint32_t per;                       /* records a chunk */
    volatile int fail;
    uint32_t drawn[SETUP_CHUNKS], sofs[SETUP_CHUNKS];       /* strokes drawn in a chunk; strokes drawn before it */
    size_t pofs[SETUP_CHUNKS], tofs[SETUP_CHUNKS];          /* polygon and stroke triangles before the chunk */
    uint32_t *sw;
} cls_ctx;

static void classify_job(void *vc, int k)
{
    cls_ctx *c = (cls_ctx *)vc; gpu_compose *g = c->g; const gpu_compose_in *in = c->in;
    clist *pl = &g->cpoly[k], *sl = &g->cstroke[k];
    pl->n = 0; sl->n = 0; c->drawn[k] = 0;
    const uint64_t a64 = (uint64_t)k * c->per;
    if (a64 >= in->nrec) return;
    const uint32_t a = (uint32_t)a64, b = (uint32_t)(a64 + c->per < in->nrec ? a64 + c->per : in->nrec);
    uint32_t *words = (uint32_t *)vec_reserve(&g->cword[k], (size_t)(b - a) * STROKE_WORDS * 4);
    if (!words) { c->fail = 1; return; }
    for (uint32_t i = a; i < b; i++) {
        const gpu_trace_rec *r = &in->recs[i];
        if (r->kind == REMASTER_T_POLYGON || r->kind == REMASTER_T_LOGIC) {
            const unsigned long long cp = CYC();
            if (!add_polygon(pl, r, c->w, c->h)) { c->fail = 1; return; }
            CADD(g_cyc, 1, CYC() - cp); CADD(g_cnt, 1, 1);
        } else if (r->kind == REMASTER_T_LINE) {
            int drawn = 0;
            const unsigned long long cs = CYC();
            if (!add_stroke(sl, r, c->w, c->h, c->drawn[k], words + (size_t)c->drawn[k] * STROKE_WORDS, &drawn)) { c->fail = 1; return; }
            CADD(g_cyc, 2, CYC() - cs); CADD(g_cnt, 2, 1);
            if (drawn) c->drawn[k]++;
        }
    }
}

static void merge_job(void *vc, int k)
{
    cls_ctx *c = (cls_ctx *)vc; gpu_compose *g = c->g;
    const clist *pl = &g->cpoly[k], *sl = &g->cstroke[k];
    if (pl->n) memcpy(g->polys.t + c->pofs[k], pl->t, pl->n * sizeof(ctri));
    for (size_t j = 0; j < sl->n; j++) { ctri *d = &g->strokes.t[c->tofs[k] + j]; *d = sl->t[j]; d->w7 += c->sofs[k]; }
    if (c->drawn[k]) memcpy(c->sw + (size_t)c->sofs[k] * STROKE_WORDS, g->cword[k].p, (size_t)c->drawn[k] * STROKE_WORDS * 4);
}

/* The classify of polygons and lines, into g->polys, g->strokes and sw (nrec * STROKE_WORDS words). */
static int classify_records(gpu_compose *g, const gpu_compose_in *in, int w, int h, uint32_t *sw, uint32_t *nstrokes_out)
{
    const int nt = setup_threads();
    if (nt <= 1 || in->nrec < 2048) return classify_serial(&g->polys, &g->strokes, sw, nstrokes_out, in, w, h);
    static cls_ctx c;                   /* one render worker calls this; the context is reused */
    c.g = g; c.in = in; c.w = w; c.h = h; c.fail = 0; c.sw = sw;
    c.per = (in->nrec + SETUP_CHUNKS - 1) / SETUP_CHUNKS;
    pool_run(classify_job, &c, SETUP_CHUNKS);
    if (c.fail) return 0;
    size_t np = 0, nsz = 0; uint32_t nd = 0;
    for (int k = 0; k < SETUP_CHUNKS; k++) {
        c.pofs[k] = np; c.tofs[k] = nsz; c.sofs[k] = nd;
        np += g->cpoly[k].n; nsz += g->cstroke[k].n; nd += c.drawn[k];
    }
    if (!clist_reserve(&g->polys, np) || !clist_reserve(&g->strokes, nsz)) return 0;
    g->polys.n = np; g->strokes.n = nsz;
    pool_run(merge_job, &c, SETUP_CHUNKS);
    *nstrokes_out = nd;
    static int check = -1;
    if (check < 0) check = getenv("F117_SETUP_CHECK") != NULL;
    if (check) {                         /* the serial result, compared byte for byte */
        static unsigned n, bad; clist rp = { 0 }, rs = { 0 }; uint32_t rn = 0;
        uint32_t *rw = (uint32_t *)malloc((size_t)(in->nrec ? in->nrec : 1) * STROKE_WORDS * 4);
        int ok = rw && classify_serial(&rp, &rs, rw, &rn, in, w, h);
        if (ok) ok = rp.n == g->polys.n && rs.n == g->strokes.n && rn == nd &&
                     (!rp.n || !memcmp(rp.t, g->polys.t, rp.n * sizeof(ctri))) && (!rs.n || !memcmp(rs.t, g->strokes.t, rs.n * sizeof(ctri))) &&
                     (!rn || !memcmp(rw, sw, (size_t)rn * STROKE_WORDS * 4));
        n++; if (!ok) bad++;
        if (!ok && bad < 4) fprintf(stderr, "[setup-check] differs at picture %u (polys %zu vs %zu, strokes %zu vs %zu, drawn %u vs %u)\n", n, rp.n, g->polys.n, rs.n, g->strokes.n, rn, nd);
        if (n % 120 == 0) fprintf(stderr, "[setup-check] %u pictures checked, %u differ\n", n, bad);
        free(rp.t); free(rs.t); free(rw);
    }
    return 1;
}

typedef struct { const gpu_compose *g; uint32_t *pw, *stw; } pack_ctx;
static void pack_job(void *vc, int k)
{
    const pack_ctx *c = (const pack_ctx *)vc;
    const size_t np = c->g->polys.n, ns = c->g->strokes.n;
    const size_t p0 = np * k / SETUP_CHUNKS, p1 = np * (k + 1) / SETUP_CHUNKS, s0 = ns * k / SETUP_CHUNKS, s1 = ns * (k + 1) / SETUP_CHUNKS;
    if (p1 > p0) memset(c->pw + p0 * 40, 0, (p1 - p0) * 160);
    if (s1 > s0) memset(c->stw + s0 * 40, 0, (s1 - s0) * 160);
    for (size_t j = p0; j < p1; j++) {
        const ctri *tr = &c->g->polys.t[j]; uint32_t *wd = c->pw + j * 40;
        tri_words(wd, tr); wd[7] = tr->w7; wd[8] = tr->paint_index; wd[9] = tr->paint_owner; wd[10] = tr->kind;
    }
    for (size_t j = s0; j < s1; j++) { const ctri *tr = &c->g->strokes.t[j]; uint32_t *wd = c->stw + j * 40; tri_words(wd, tr); wd[7] = tr->w7; }
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
    g->begun = 0;
    const Uint64 t_start = SDL_GetTicksNS();
    const int w = in->w, h = in->h, tw = in->tw;
    const size_t px = (size_t)tw * h;
    if (px * 4 > 0x7FFFFFFFu) { say(g, "the picture is too large"); return 0; }

    {   const char *sea_env = getenv("F117_SMOOTH_SEA");
        g_smooth_sea = !sea_env || atoi(sea_env) != 0;
        g_square_pen = square_pen_enabled();
        g_q_check = g_plane_check = getenv("F117_BIN_CHECK") != NULL;
        g_plane_full = getenv("F117_PLANE_FULL") != NULL;
        exact_on(); decal_faces_enabled();
    }
    /* classify the records: the clear and the bands first, then polygons, then the lines; anything else is not handled */
    int phase = 0;
    g->polys.n = 0; g->strokes.n = 0;
    uint32_t nband = 0, nstrokes = 0;
    uint32_t *bw = (uint32_t *)vec_reserve(&g->band_words, (size_t)4096 * 64 * 4);
    uint32_t *sw = (uint32_t *)vec_reserve(&g->stroke_words, (size_t)(in->nrec ? in->nrec : 1) * STROKE_WORDS * 4);
    if (!bw || !sw) return 0;
    const size_t band_cap = g->band_words.cap / (64 * 4);
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
    if (!classify_records(g, in, tw, h, sw, &nstrokes)) return 0;
    CADD(g_cyc, 5, CYC() - c_loop); CADD(g_cnt, 5, 1);
    const Uint64 t_classified = SDL_GetTicksNS();
    const int tilesx = (tw + TILE - 1) / TILE, tilesy = (h + TILE - 1) / TILE;
    size_t npents = 0, nsents = 0;
    if (!bin_tiles(&g->polys, tilesx, tilesy, &g->poly_off, &g->poly_ents, &npents)) return 0;
    if (!bin_tiles(&g->strokes, tilesx, tilesy, &g->stroke_off, &g->stroke_ents, &nsents)) return 0;
    const Uint64 t_binned = SDL_GetTicksNS();
    uint32_t *pw = (uint32_t *)vec_reserve(&g->poly_words, (g->polys.n ? g->polys.n : 1) * 160);
    uint32_t *stw = (uint32_t *)vec_reserve(&g->stroke_tri_words, (g->strokes.n ? g->strokes.n : 1) * 160);
    if (!pw || !stw) return 0;
    if (!g->polys.n) memset(pw, 0, 160);        /* the one-triangle minimum upload of an empty list stays zero, as before */
    if (!g->strokes.n) memset(stw, 0, 160);
    { pack_ctx pc = { g, pw, stw }; pool_run(pack_job, &pc, SETUP_CHUNKS); }
    uint32_t flags[256];
    for (int k = 0; k < 256; k++) flags[k] = in->tagflags[k];

    const Uint32 stroke_words = (nstrokes ? nstrokes : 1) * STROKE_WORDS;
    Uint32 face_offset = 0, sw_bytes = stroke_words * 4;
    if (decal_faces_enabled()) {
        df_reset(&g->faces);
        for (size_t k = 0; k < g->polys.n; ++k) {
            const ctri *tr = &g->polys.t[k];
            if (tr->kind) continue; /* logic fills do not write surface depth */
            ex_tri tri; memcpy(tri.q, tr->q, sizeof tri.q); tri.area = tr->area;
            ex_planes plane = {0}; plane.ox = tr->dox; plane.oy = tr->doy;
            plane.base[0] = (uint64_t)tr->dbase; plane.px[0] = (uint64_t)tr->dpx; plane.py[0] = (uint64_t)tr->dpy;
            df_add(&g->faces, tr->paint_owner, &tri, &plane);
        }
        if (g->faces.failed) return 0;
        const size_t face_words = DF_START + g->faces.count * DF_STRIDE;
        if (face_words > UINT32_MAX / 4 - stroke_words) return 0;
        sw_bytes += (Uint32)(face_words * 4);
        sw = (uint32_t *)vec_reserve(&g->stroke_words, sw_bytes);
        if (!sw) return 0;
        face_offset = stroke_words;
        memcpy(sw + face_offset, g->faces.words, face_words * 4);
    }

    const Uint64 t_packed = SDL_GetTicksNS();
    /* GPU resources */
    const Uint32 pbytes = (Uint32)(px * 4);
    const SDL_GPUBufferUsageFlags RO = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, RW = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    const size_t npix_game = (size_t)w * h;
    const Uint32 game_bytes = (Uint32)((npix_game + 7) & ~(size_t)3);
    const Uint32 nstroke_b = (Uint32)((nstrokes ? nstrokes : 1) * 4);
    seg segs[] = {
        { &g->bt, bw, (Uint32)((nband ? nband : 1) * 256), RO },
        { &g->pt, pw, (Uint32)((g->polys.n ? g->polys.n : 1) * 160), RO },
        { &g->pe, g->poly_ents.p, (Uint32)((npents ? npents : 1) * 36), RO },
        { &g->po, g->poly_off.p, (Uint32)(((size_t)tilesx * tilesy + 1) * 4), RO },
        { &g->pal, in->palette, 1024, RO },
        { &g->st, stw, (Uint32)((g->strokes.n ? g->strokes.n : 1) * 160), RO },
        { &g->se, g->stroke_ents.p, (Uint32)((nsents ? nsents : 1) * 36), RO },
        { &g->so, g->stroke_off.p, (Uint32)(((size_t)tilesx * tilesy + 1) * 4), RO },
        { &g->sw, sw, sw_bytes, RO },
        { &g->tf, flags, 1024, RO },
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
    if (!gbuf_ensure(g->dev, &g->src, RO, game_bytes) || !gbuf_ensure(g->dev, &g->tag, RO, game_bytes) || !gbuf_ensure(g->dev, &g->dac, RO, 768)) return 0;
    const Uint64 t_prepared = SDL_GetTicksNS();
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
    profile_mark(g, cb, "upload_begin");
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
    profile_mark(g, cb, "upload_end");
    Uint64 pmark = SDL_GetTicksNS();
    if (pass_times_on()) { pmark = t_setup; pass_split(g, &cb, 0, &pmark); if (!cb) return 0; }

    SDL_GPUBuffer *b_colour = g->colour.buf, *b_index = g->index_.buf, *b_invz = g->invz.buf, *b_owner = g->owner.buf, *b_strokez = g->strokez.buf, *b_out = g->outpix.buf;
    {   /* bands: the clear, the sky and ground, depth and owner zero */
        SDL_GPUStorageBufferReadWriteBinding rw[4] = { { b_colour, false }, { b_index, false }, { b_invz, false }, { b_owner, false } };
        const Uint32 u[4] = { (Uint32)tw, (Uint32)h, nband, exact_on() ? 1u : 0u };
        SDL_GPUComputePass *c = SDL_BeginGPUComputePass(cb, NULL, 0, rw, 4);
        SDL_BindGPUComputePipeline(c, g->p_bands);
        SDL_BindGPUComputeStorageBuffers(c, 0, &g->bt.buf, 1);
        SDL_PushGPUComputeUniformData(cb, 0, u, sizeof u);
        SDL_DispatchGPUCompute(c, ((Uint32)tw + 63) / 64, (Uint32)h, 1);
        SDL_EndGPUComputePass(c);
        profile_mark(g, cb, "bands_end");
        if (pass_times_on()) pass_split(g, &cb, 1, &pmark);
    }
    if (g->polys.n) {
        SDL_GPUStorageBufferReadWriteBinding rw[4] = { { b_colour, false }, { b_index, false }, { b_invz, false }, { b_owner, false } };
        SDL_GPUBuffer *ro[4] = { g->pt.buf, g->pe.buf, g->po.buf, g->pal.buf };
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
        SDL_GPUStorageBufferReadWriteBinding rw[6] = { { b_colour, false }, { b_index, false }, { b_invz, false }, { b_owner, false }, { g->hid.buf, false }, { b_strokez, false } };
        SDL_GPUBuffer *ro[4] = { g->st.buf, g->se.buf, g->so.buf, g->sw.buf };
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
    g->params = *in; g->params.recs = NULL; g->params.palette = NULL; g->params.src = NULL; g->params.tag = NULL; g->params.dac = NULL; g->params.tagflags = NULL;
    g->pbytes = pbytes; g->game_bytes = game_bytes; g->npix_game = npix_game;
    profile_resolve(g, cb);
    SDL_SubmitGPUCommandBuffer(cb);               /* the raster passes run while the caller makes the game's picture */
    g->setup_ms = (double)(t_setup - t_start) / 1e6;
    if (getenv("F117_REMASTER_TIMING")) {   /* diagnostic: where the CPU setup goes, every 120th picture (stderr) */
        static double sum[5], mb, ents, trisn; static unsigned count;
        sum[0] += (double)(t_classified - t_start) / 1e6; sum[1] += (double)(t_binned - t_classified) / 1e6; sum[2] += (double)(t_packed - t_binned) / 1e6;
        sum[3] += (double)(t_prepared - t_packed) / 1e6; sum[4] += (double)(t_setup - t_prepared) / 1e6;
        mb += (double)total / 1048576.0; ents += (double)(npents + nsents); trisn += (double)(g->polys.n + g->strokes.n);
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
    g->t_begin = SDL_GetTicksNS();
    g->begun = 1;
    return 1;
}

int gpu_compose_finish(gpu_compose *g, const uint8_t *src, const uint8_t *tag, const uint8_t *dac, uint32_t *wide_out)
{
    if (!g || !g->begun || !src || !tag || !dac) return 0;
    g->begun = 0;
    const int w = g->params.w, h = g->params.h, tw = g->params.tw;
    const Uint32 pbytes = g->pbytes, game_bytes = g->game_bytes;
    const Uint32 off_tag = (game_bytes + 15u) & ~15u, off_dac = off_tag + ((game_bytes + 15u) & ~15u), total2 = off_dac + 768;
    if (g->up2_cap < total2) {
        if (g->up2) SDL_ReleaseGPUTransferBuffer(g->dev, g->up2);
        SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, total2 + total2 / 4, 0 };
        g->up2 = SDL_CreateGPUTransferBuffer(g->dev, &ti); g->up2_cap = g->up2 ? ti.size : 0;
        if (!g->up2) return 0;
    }
    {
        Uint8 *m = (Uint8 *)SDL_MapGPUTransferBuffer(g->dev, g->up2, true);
        if (!m) return 0;
        memset(m, 0, total2);
        memcpy(m, src, g->npix_game); memcpy(m + off_tag, tag, g->npix_game); memcpy(m + off_dac, dac, 768);
        SDL_UnmapGPUTransferBuffer(g->dev, g->up2);
    }
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(g->dev);
    if (!cb) return 0;
    profile_mark(g, cb, "picture_upload_begin");
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    {
        SDL_GPUTransferBufferLocation f1 = { g->up2, 0 }, f2 = { g->up2, off_tag }, f3 = { g->up2, off_dac };
        SDL_GPUBufferRegion t1 = { g->src.buf, 0, game_bytes }, t2 = { g->tag.buf, 0, game_bytes }, t3 = { g->dac.buf, 0, 768 };
        SDL_UploadToGPUBuffer(cp, &f1, &t1, false); SDL_UploadToGPUBuffer(cp, &f2, &t2, false); SDL_UploadToGPUBuffer(cp, &f3, &t3, false);
    }
    SDL_EndGPUCopyPass(cp);
    profile_mark(g, cb, "picture_upload_end");
    SDL_GPUBuffer *b_colour = g->colour.buf, *b_invz = g->invz.buf, *b_strokez = g->strokez.buf, *b_out = g->outpix.buf;
    {   /* the replace step */
        SDL_GPUStorageBufferReadWriteBinding rw[4] = { { b_colour, false }, { b_invz, false }, { b_strokez, false }, { b_out, false } };
        SDL_GPUBuffer *ro[4] = { g->tf.buf, g->src.buf, g->tag.buf, g->dac.buf };
        const int32_t u[12] = { w, h, tw, g->params.xoff, g->params.x0, g->params.x1, g->params.y0, g->params.y1, g->params.complete, 0, 0, 0 };
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
        SDL_GPUBufferRegion bk = { b_out, 0, pbytes }; SDL_GPUTransferBufferLocation into = { g->down, 0 }; SDL_DownloadFromGPUBuffer(cp, &bk, &into);
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
    g->wait_ms = (double)(t_gpu - t_wait) / 1e6;          /* how long the caller blocked on the GPU: its work not hidden behind the caller's */
    if (g->targets[0]) { g->last_written = g->cur_write; g->cur_write ^= 1; }

    if (wide_out) {
        const void *got = SDL_MapGPUTransferBuffer(g->dev, g->down, false);
        if (!got) return 0;
        memcpy(wide_out, got, pbytes);
        SDL_UnmapGPUTransferBuffer(g->dev, g->down);
    }
    g->gpu_ms = (double)(t_gpu - g->t_begin) / 1e6;       /* from the end of the first half: the raster passes overlap what the caller did meanwhile */
    g->copy_ms = (double)(SDL_GetTicksNS() - t_gpu) / 1e6;
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
