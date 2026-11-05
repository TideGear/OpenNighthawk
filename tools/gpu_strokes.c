/* gpu_strokes.c - GPU compositor stage 4 (docs/gpu-compositor.md): the lines and dots of a recorded composite (gpu_trace.h) drawn on a hidden D3D12 device from
 * the state the polygons leave, and compared with the CPU rasterizer's drawing of the same records (single precision on the GPU: counts and sizes of differences).
 *
 * usage: gpu_strokes TRACE STROKES.dxil      GPU_STAGE_REPEAT=N times N runs of the two compute passes; 77 without a GPU */
#include "gpu_trace.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TILE 16

typedef struct { double x, y, iz; } sv;

static int g_strokes_cleared;

/* The CPU oracle: the calls the live composite made; the stroke buffers are cleared once before the first line, as it does. */
static void cpu_apply(remaster_target *t, const gpu_trace_rec *r)
{
    if (r->line_order && !g_strokes_cleared) {
        const size_t n = (size_t)t->w * t->h;
        memset(t->stroke_z, 0, n * 4); memset(t->stroke_aux_x, 0, n * 4); memset(t->stroke_aux_y, 0, n * 4); memset(t->stroke_owner, 0, n * 4);
        g_strokes_cleared = 1;
    }
    t->cx = r->cx; t->cy = r->cy; t->sx = r->sx; t->sy = r->sy; t->inv_depth_scale = r->inv_depth_scale; t->line_bias = r->line_bias;
    t->near_z = r->near_z; t->line_decal = r->line_decal; t->line_order = r->line_order; t->paint_owner = r->paint_owner; t->paint_index = r->paint_index;
    switch (r->kind) {
    case REMASTER_T_CLEAR: remaster_clear(t, r->color); break;
    case REMASTER_T_POLYGON: remaster_polygon(t, r->v, r->n, r->color); break;
    case REMASTER_T_SHADED: remaster_polygon_shaded(t, r->v, r->colors, r->n); break;
    case REMASTER_T_LOGIC: remaster_polygon_logic(t, r->v, r->n, r->op, r->value); break;
    case REMASTER_T_LINE: remaster_line(t, r->v[0], r->v[1], r->width, r->color); break;
    case REMASTER_T_ZRESET: memset(t->inv_z, 0, (size_t)t->w * t->h * sizeof *t->inv_z); break;
    }
}

typedef struct { float a1, b1, a2, b2, ciz, diz1, diz2; uint32_t stroke; double cx, cy; int bbox[4]; double a1d, b1d, a2d, b2d; } tri_t;
typedef struct { tri_t *t; size_t n, cap; } tri_list;

static void add_triangle(tri_list *l, uint32_t stroke, sv a, sv b, sv c, int w, int h)
{
    const double den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (fabs(den) < 1e-12) return;
    const double minx = fmin(a.x, fmin(b.x, c.x)), maxx = fmax(a.x, fmax(b.x, c.x));
    const double miny = fmin(a.y, fmin(b.y, c.y)), maxy = fmax(a.y, fmax(b.y, c.y));
    int x0 = (int)floor(minx - 0.5), x1 = (int)ceil(maxx - 0.5), y0 = (int)floor(miny - 0.5), y1 = (int)ceil(maxy - 0.5);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w - 1) x1 = w - 1;
    if (y1 > h - 1) y1 = h - 1;
    if (x0 > x1 || y0 > y1) return;
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 1024; l->t = (tri_t *)realloc(l->t, l->cap * sizeof *l->t); }
    tri_t *t = &l->t[l->n++];
    memset(t, 0, sizeof *t);
    t->a1d = (b.y - c.y) / den; t->b1d = (c.x - b.x) / den; t->a2d = (c.y - a.y) / den; t->b2d = (a.x - c.x) / den;
    t->a1 = (float)t->a1d; t->b1 = (float)t->b1d; t->a2 = (float)t->a2d; t->b2 = (float)t->b2d;
    t->ciz = (float)c.iz; t->diz1 = (float)(a.iz - c.iz); t->diz2 = (float)(b.iz - c.iz);
    t->stroke = stroke; t->cx = c.x; t->cy = c.y;
    t->bbox[0] = x0; t->bbox[1] = x1; t->bbox[2] = y0; t->bbox[3] = y1;
}

/* remaster_line's setup up to its two triangles; 0 when the CPU draws nothing. words: 16 per stroke (strokes.hlsl). */
static int stroke_setup(const gpu_trace_rec *r, int w, int h, uint32_t id, tri_list *l, uint32_t *words)
{
    remaster_v3 a = r->v[0], b = r->v[1];
    const double width_px = r->width;
    const int line_point = a.x == b.x && a.y == b.y && a.z == b.z;
    const double near_z = (double)r->near_z;
    double ax = a.x, ay = a.y, az = a.z, bx = b.x, by = b.y, bz = b.z;
    if (az < near_z && bz < near_z) return 0;
    if (az < near_z) { const double k = (near_z - az) / (bz - az); ax += k * (bx - ax); ay += k * (by - ay); az = near_z; }
    else if (bz < near_z) { const double k = (near_z - bz) / (az - bz); bx += k * (ax - bx); by += k * (ay - by); bz = near_z; }
    const double bias = r->line_bias > 0.0 ? r->line_bias : 1.5;
    const double scale = r->inv_depth_scale > 0.0 ? r->inv_depth_scale : 1.0;
    const sv sa = { r->cx + r->sx * ax / az, r->cy + r->sy * ay / az, bias * (scale / az) };
    const sv sb = { r->cx + r->sx * bx / bz, r->cy + r->sy * by / bz, bias * (scale / bz) };
    if ((sa.y + width_px < 0 && sb.y + width_px < 0) || (sa.y - width_px > h && sb.y - width_px > h) ||
        (sa.x + width_px < 0.0 && sb.x + width_px < 0.0) || (sa.x - width_px > w && sb.x - width_px > w)) return 0;
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
    add_triangle(l, id, q0, q1, q2, w, h);
    add_triangle(l, id, q0, q2, q3, w, h);
    memset(words, 0, 16 * 4);
    words[0] = r->color; words[1] = r->paint_index; words[2] = r->paint_owner; words[3] = (uint32_t)line_point; words[4] = (uint32_t)radius;
    const float f[9] = { (float)sa.x, (float)sa.y, (float)sb.x, (float)sb.y, (float)depth_dx, (float)depth_dy, (float)sa.x, (float)sa.y, (float)sa.iz };
    for (int k = 0; k < 9; k++) memcpy(&words[5 + k], &f[k], 4);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: gpu_strokes TRACE STROKES.dxil\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    gpu_trace_header hd;
    if (fread(&hd, sizeof hd, 1, f) != 1 || hd.magic != GPU_TRACE_MAGIC) { fprintf(stderr, "not a trace\n"); return 2; }
    gpu_trace_rec *recs = (gpu_trace_rec *)malloc(hd.nrec * sizeof *recs);
    if (!recs || fread(recs, sizeof *recs, hd.nrec, f) != hd.nrec) { fprintf(stderr, "short trace\n"); return 2; }
    fclose(f);
    const int w = hd.w, h = hd.h;
    const size_t px = (size_t)w * h;

    /* [0, zr] the clear and bands, (zr, pe) the polygons, [pe, le) the lines and dots */
    uint32_t zr = hd.nrec, pe = hd.nrec, le = hd.nrec;
    for (uint32_t i = 0; i < hd.nrec; i++) if (recs[i].kind == REMASTER_T_ZRESET) { zr = i; break; }
    if (zr == hd.nrec) { fprintf(stderr, "no depth reset in the trace\n"); return 2; }
    for (uint32_t i = zr + 1; i < hd.nrec; i++) if (recs[i].kind == REMASTER_T_LINE || recs[i].kind == REMASTER_T_WINDOW) { pe = i; break; }
    if (pe == hd.nrec) { fprintf(stderr, "no lines in the trace\n"); return 2; }
    for (uint32_t i = pe; i < hd.nrec; i++) if (recs[i].kind != REMASTER_T_LINE) { le = i; break; }

    remaster_target t; memset(&t, 0, sizeof t);
    t.w = w; t.h = h;
    t.color = (uint32_t *)calloc(px, 4); t.inv_z = (float *)calloc(px, 4); t.index = (uint16_t *)calloc(px, 2); t.owner = (uint32_t *)calloc(px, 4);
    t.stroke_z = (float *)calloc(px, 4); t.stroke_aux_x = (float *)calloc(px, 4); t.stroke_aux_y = (float *)calloc(px, 4); t.stroke_owner = (uint32_t *)calloc(px, 4);
    t.palette = hd.palette;
    for (uint32_t i = 0; i < pe; i++) cpu_apply(&t, &recs[i]);
    uint32_t *s1_col = (uint32_t *)malloc(px * 4), *s1_idx = (uint32_t *)malloc(px * 4), *s1_z = (uint32_t *)malloc(px * 4), *s1_own = (uint32_t *)malloc(px * 4);
    for (size_t i = 0; i < px; i++) { s1_col[i] = t.color[i]; s1_idx[i] = t.index[i]; memcpy(&s1_z[i], &t.inv_z[i], 4); s1_own[i] = t.owner[i]; }
    for (uint32_t i = pe; i < le; i++) cpu_apply(&t, &recs[i]);                /* the oracle: the state after the lines */

    tri_list tl = { 0 };
    uint32_t nstrokes = 0;
    uint32_t *swords = (uint32_t *)malloc((size_t)(le - pe + 1) * 16 * 4);
    for (uint32_t i = pe; i < le; i++) if (stroke_setup(&recs[i], w, h, nstrokes, &tl, swords + (size_t)nstrokes * 16)) nstrokes++;

    const int tilesx = (w + TILE - 1) / TILE, tilesy = (h + TILE - 1) / TILE, ntiles = tilesx * tilesy;
    uint32_t *off = (uint32_t *)calloc((size_t)ntiles + 1, 4);
    size_t nents = 0;
    uint32_t *ents = NULL;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t *cursor = (uint32_t *)calloc((size_t)ntiles + 1, 4);
        if (pass == 1) { for (int k = 0; k < ntiles; k++) cursor[k] = off[k]; ents = (uint32_t *)malloc((nents ? nents : 1) * 3 * 4); }
        size_t count = 0;
        for (size_t ti = 0; ti < tl.n; ti++) {
            const tri_t *tr = &tl.t[ti];
            const int tx0 = tr->bbox[0] / TILE, tx1 = tr->bbox[1] / TILE, ty0 = tr->bbox[2] / TILE, ty1 = tr->bbox[3] / TILE;
            for (int ty = ty0; ty <= ty1; ty++)
                for (int tx = tx0; tx <= tx1; tx++) {
                    const double ox = tx * TILE + 0.5, oy = ty * TILE + 0.5;
                    const double l1 = tr->a1d * (ox - tr->cx) + tr->b1d * (oy - tr->cy), l2 = tr->a2d * (ox - tr->cx) + tr->b2d * (oy - tr->cy);
                    double m1 = -1e300, m2 = -1e300, m3 = -1e300;
                    for (int cy2 = 0; cy2 < 2; cy2++) for (int cx2 = 0; cx2 < 2; cx2++) {
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
        free(cursor);
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init (skipped): %s\n", SDL_GetError()); return 77; }
    size_t csz = 0; void *code = SDL_LoadFile(argv[2], &csz);
    if (!code) { fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
    SDL_GPUDevice *dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, false, NULL);
    if (!dev) { fprintf(stderr, "no GPU device (skipped): %s\n", SDL_GetError()); return 77; }
    SDL_GPUComputePipelineCreateInfo pci = { 0 };
    pci.code = (const Uint8 *)code; pci.code_size = csz; pci.entrypoint = "main"; pci.format = SDL_GPU_SHADERFORMAT_DXIL;
    pci.num_readonly_storage_buffers = 4; pci.num_readwrite_storage_buffers = 6; pci.num_uniform_buffers = 1;
    pci.threadcount_x = 16; pci.threadcount_y = 16; pci.threadcount_z = 1;
    SDL_GPUComputePipeline *pipe = SDL_CreateGPUComputePipeline(dev, &pci);
    if (!pipe) { fprintf(stderr, "pipeline: %s\n", SDL_GetError()); return 2; }

    const Uint32 pbytes = (Uint32)(px * 4);
    uint32_t *twords = (uint32_t *)calloc(tl.n ? tl.n : 1, 12 * 4);
    for (size_t k = 0; k < tl.n; k++) {
        const tri_t *tr = &tl.t[k]; uint32_t *wd = twords + k * 12;
        memcpy(&wd[0], &tr->a1, 4); memcpy(&wd[1], &tr->b1, 4); memcpy(&wd[2], &tr->a2, 4); memcpy(&wd[3], &tr->b2, 4);
        memcpy(&wd[4], &tr->ciz, 4); memcpy(&wd[5], &tr->diz1, 4); memcpy(&wd[6], &tr->diz2, 4); wd[7] = tr->stroke;
    }
    const Uint32 sz_t = (Uint32)((tl.n ? tl.n : 1) * 48), sz_e = (Uint32)((nents ? nents : 1) * 12), sz_o = (Uint32)((ntiles + 1) * 4), sz_s = (Uint32)((nstrokes ? nstrokes : 1) * 64);
    const Uint32 ro_size[4] = { sz_t, sz_e, sz_o, sz_s };
    SDL_GPUBufferCreateInfo bi = { SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, 0, 0 };
    SDL_GPUBuffer *ro[4];
    for (int k = 0; k < 4; k++) { bi.size = ro_size[k]; ro[k] = SDL_CreateGPUBuffer(dev, &bi); }
    bi.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    SDL_GPUBuffer *rw[6];
    for (int k = 0; k < 4; k++) { bi.size = pbytes; rw[k] = SDL_CreateGPUBuffer(dev, &bi); }
    bi.size = (Uint32)((nstrokes ? nstrokes : 1) * 4); rw[4] = SDL_CreateGPUBuffer(dev, &bi);
    bi.size = pbytes; rw[5] = SDL_CreateGPUBuffer(dev, &bi);
    const Uint32 total_up = sz_t + sz_e + sz_o + sz_s + 4 * pbytes;
    SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, total_up, 0 };
    SDL_GPUTransferBuffer *up = SDL_CreateGPUTransferBuffer(dev, &ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; ti.size = pbytes;
    SDL_GPUTransferBuffer *down[2] = { SDL_CreateGPUTransferBuffer(dev, &ti), SDL_CreateGPUTransferBuffer(dev, &ti) };
    {
        Uint8 *m = (Uint8 *)SDL_MapGPUTransferBuffer(dev, up, false), *p = m;
        memcpy(p, twords, sz_t); p += sz_t;
        memcpy(p, ents, sz_e); p += sz_e;
        memcpy(p, off, sz_o); p += sz_o;
        memset(p, 0, sz_s); memcpy(p, swords, (size_t)nstrokes * 64); p += sz_s;
        memcpy(p, s1_col, pbytes); p += pbytes; memcpy(p, s1_idx, pbytes); p += pbytes; memcpy(p, s1_z, pbytes); p += pbytes; memcpy(p, s1_own, pbytes);
        SDL_UnmapGPUTransferBuffer(dev, up);
    }
    const char *e = getenv("GPU_STAGE_REPEAT"); const int reps = e ? atoi(e) : 1;
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    {
        Uint32 at = 0;
        for (int k = 0; k < 4; k++) { SDL_GPUTransferBufferLocation from = { up, at }; SDL_GPUBufferRegion to = { ro[k], 0, ro_size[k] }; SDL_UploadToGPUBuffer(cp, &from, &to, false); at += ro_size[k]; }
        for (int k = 0; k < 4; k++) { SDL_GPUTransferBufferLocation from = { up, at }; SDL_GPUBufferRegion to = { rw[k], 0, pbytes }; SDL_UploadToGPUBuffer(cp, &from, &to, false); at += pbytes; }
    }
    SDL_EndGPUCopyPass(cp);
    SDL_GPUStorageBufferReadWriteBinding rwb[6];
    for (int k = 0; k < 6; k++) { rwb[k].buffer = rw[k]; rwb[k].cycle = false; }
    typedef struct { Uint32 mode, w, h, tilesx, nstrokes, p0, p1, p2; } params;
    for (int rep = 0; rep < reps; rep++) {
        for (int mode = 0; mode < 2; mode++) {
            params q = { (Uint32)mode, (Uint32)w, (Uint32)h, (Uint32)tilesx, nstrokes, 0, 0, 0 };
            SDL_GPUComputePass *cps = SDL_BeginGPUComputePass(cb, NULL, 0, rwb, 6);
            SDL_BindGPUComputePipeline(cps, pipe);
            SDL_BindGPUComputeStorageBuffers(cps, 0, ro, 4);
            SDL_PushGPUComputeUniformData(cb, 0, &q, sizeof q);
            if (mode == 0) SDL_DispatchGPUCompute(cps, 256, (nstrokes / 4096 + 16) / 16, 1);
            else SDL_DispatchGPUCompute(cps, (Uint32)tilesx, (Uint32)tilesy, 1);
            SDL_EndGPUComputePass(cps);
        }
        if (rep == 0) {
            SDL_GPUCopyPass *c2 = SDL_BeginGPUCopyPass(cb);
            for (int k = 0; k < 2; k++) { SDL_GPUBufferRegion bk = { rw[k], 0, pbytes }; SDL_GPUTransferBufferLocation in2 = { down[k], 0 }; SDL_DownloadFromGPUBuffer(c2, &bk, &in2); }
            SDL_EndGPUCopyPass(c2);
        }
    }
    const Uint64 t0 = SDL_GetTicksNS();
    SDL_GPUFence *fe = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
    SDL_WaitForGPUFences(dev, true, &fe, 1);
    SDL_ReleaseGPUFence(dev, fe);
    const double ms = (SDL_GetTicksNS() - t0) / 1e6;

    const uint32_t *g_col = (const uint32_t *)SDL_MapGPUTransferBuffer(dev, down[0], false), *g_idx = (const uint32_t *)SDL_MapGPUTransferBuffer(dev, down[1], false);
    size_t bad_c = 0, bad_i = 0, big = 0; int maxd = 0;
    for (size_t i = 0; i < px; i++) {
        bad_i += g_idx[i] != t.index[i];
        if (g_col[i] != t.color[i]) {
            bad_c++;
            int m = 0;
            for (int sh = 0; sh < 24; sh += 8) { int dd = (int)(g_col[i] >> sh & 255) - (int)(t.color[i] >> sh & 255); if (dd < 0) dd = -dd; if (dd > m) m = dd; }
            if (m > maxd) maxd = m;
            if (m > 8) big++;
        }
    }
    printf("strokes, %d x %d: %u line records, %u drawn as %zu triangles in %zu tile entries; %zu of %zu pixels differ in colour (%.4f%%), %zu by more than 8 levels (largest %d); %zu index differ; %d run(s) in %.3f ms (%.3f ms a run, with the upload and one readback once)\n",
           w, h, le - pe, nstrokes, tl.n, nents, bad_c, px, 100.0 * (double)bad_c / (double)px, big, maxd, bad_i, reps, ms, ms / reps);
    return 0;
}
