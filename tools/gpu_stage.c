/* gpu_stage.c - GPU compositor stage 2 (docs/gpu-compositor.md): the clear, the sky and ground band triangles and the depth reset of a recorded
 * composite (gpu_trace.h) drawn on a hidden D3D12 device and compared, pixel for pixel, with the CPU rasterizer's drawing of the same records.
 *
 * usage: gpu_stage TRACE BANDS.dxil      exit 0 when colour, index, depth and owner all match; 77 when there is no GPU device (or no double support) */
#include "gpu_trace.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRI_DOUBLES 22

typedef struct { double x, y, iz; } sv;

/* The CPU oracle: the same calls the live composite made. */
static void cpu_apply(remaster_target *t, const gpu_trace_rec *r)
{
    t->cx = r->cx; t->cy = r->cy; t->sx = r->sx; t->sy = r->sy; t->inv_depth_scale = r->inv_depth_scale; t->line_bias = r->line_bias;
    t->near_z = r->near_z; t->line_decal = r->line_decal; t->line_order = r->line_order; t->paint_owner = r->paint_owner; t->paint_index = r->paint_index;
    switch (r->kind) {
    case REMASTER_T_CLEAR: remaster_clear(t, r->color); break;
    case REMASTER_T_SHADED: remaster_polygon_shaded(t, r->v, r->colors, r->n); break;
    case REMASTER_T_ZRESET: memset(t->inv_z, 0, (size_t)t->w * t->h * sizeof *t->inv_z); break;
    }
}

/* One triangle's setup, as fill_triangle_shaded computes it before its pixel loops; 0 when the CPU would draw nothing. */
static int triangle_setup(const gpu_trace_rec *r, int w, int h, const sv s[3], const double rgb[3][3], double *d, int *box)
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
    (void)r;
    box[0] = x0; box[1] = x1; box[2] = y0; box[3] = y1;
    d[0] = a.x; d[1] = a.y; d[2] = b.x; d[3] = b.y; d[4] = c.x; d[5] = c.y;
    d[6] = (b.y - c.y) / den; d[7] = (c.x - b.x) / den; d[8] = (c.y - a.y) / den; d[9] = (a.x - c.x) / den;
    d[10] = c.iz; d[11] = a.iz - c.iz; d[12] = b.iz - c.iz;
    for (int k = 0; k < 3; k++) { d[13 + k] = rgb[0][k] - rgb[2][k]; d[16 + k] = rgb[1][k] - rgb[2][k]; d[19 + k] = rgb[2][k]; }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: gpu_stage TRACE BANDS.dxil\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    gpu_trace_header hd;
    if (fread(&hd, sizeof hd, 1, f) != 1 || hd.magic != GPU_TRACE_MAGIC) { fprintf(stderr, "not a trace\n"); return 2; }
    gpu_trace_rec *recs = (gpu_trace_rec *)malloc(hd.nrec * sizeof *recs);
    if (!recs || fread(recs, sizeof *recs, hd.nrec, f) != hd.nrec) { fprintf(stderr, "short trace\n"); return 2; }
    fclose(f);
    const int w = hd.w, h = hd.h;
    const size_t px = (size_t)w * h;

    /* the stage's records: from the start to the first depth reset */
    uint32_t last = 0; int found = 0;
    for (uint32_t i = 0; i < hd.nrec; i++) {
        if (recs[i].kind == REMASTER_T_ZRESET) { last = i; found = 1; break; }
        if (recs[i].kind != REMASTER_T_CLEAR && recs[i].kind != REMASTER_T_SHADED) break;
    }
    if (!found) { fprintf(stderr, "no band records before the first other call\n"); return 2; }

    /* CPU oracle */
    remaster_target t; memset(&t, 0, sizeof t);
    t.w = w; t.h = h;
    t.color = (uint32_t *)calloc(px, 4); t.inv_z = (float *)calloc(px, 4); t.index = (uint16_t *)calloc(px, 2); t.owner = (uint32_t *)calloc(px, 4);
    t.palette = hd.palette;
    for (uint32_t i = 0; i <= last; i++) cpu_apply(&t, &recs[i]);

    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init (skipped): %s\n", SDL_GetError()); return 77; }
    size_t code_size = 0;
    void *code = SDL_LoadFile(argv[2], &code_size);
    if (!code) { fprintf(stderr, "cannot read %s: %s\n", argv[2], SDL_GetError()); return 2; }
    SDL_GPUDevice *dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, false, NULL);
    if (!dev) { fprintf(stderr, "no GPU device (skipped): %s\n", SDL_GetError()); return 77; }
    SDL_GPUComputePipelineCreateInfo pci = { 0 };
    pci.code = (const Uint8 *)code; pci.code_size = code_size; pci.entrypoint = "main"; pci.format = SDL_GPU_SHADERFORMAT_DXIL;
    pci.num_readonly_storage_buffers = 1; pci.num_readwrite_storage_buffers = 4; pci.num_uniform_buffers = 1;
    pci.threadcount_x = 256; pci.threadcount_y = 1; pci.threadcount_z = 1;
    SDL_GPUComputePipeline *pipe = SDL_CreateGPUComputePipeline(dev, &pci);
    if (!pipe) { fprintf(stderr, "no compute pipeline (skipped; doubles unsupported?): %s\n", SDL_GetError()); return 77; }

    /* triangles: one record of TRI_DOUBLES doubles and a box each, in the trace's order */
    size_t ntri = 0, cap = 256;
    double *tri = (double *)malloc(cap * TRI_DOUBLES * sizeof(double));
    int (*boxes)[4] = (int (*)[4])malloc(cap * sizeof *boxes);
    uint16_t *paint = (uint16_t *)malloc(cap * sizeof *paint);
    for (uint32_t i = 0; i <= last; i++) {
        const gpu_trace_rec *r = &recs[i];
        if (r->kind != REMASTER_T_SHADED || r->n < 3) continue;
        sv s[16]; double rgb[16][3]; int ok = 1;
        for (int k = 0; k < r->n && ok; k++) {
            if (r->v[k].z < r->near_z) { ok = 0; break; }
            s[k].x = r->cx + r->sx * (double)r->v[k].x / r->v[k].z;
            s[k].y = r->cy + r->sy * (double)r->v[k].y / r->v[k].z;
            s[k].iz = (r->inv_depth_scale > 0.0 ? r->inv_depth_scale : 1.0) / (double)r->v[k].z;
            rgb[k][0] = r->colors[k] >> 16 & 255; rgb[k][1] = r->colors[k] >> 8 & 255; rgb[k][2] = r->colors[k] & 255;
        }
        if (!ok) continue;
        for (int k = 1; k + 1 < r->n; k++) {
            if (ntri == cap) { cap *= 2; tri = (double *)realloc(tri, cap * TRI_DOUBLES * sizeof(double)); boxes = (int (*)[4])realloc(boxes, cap * sizeof *boxes); paint = (uint16_t *)realloc(paint, cap * sizeof *paint); }
            const sv tr[3] = { s[0], s[k], s[k + 1] };
            const double col[3][3] = { { rgb[0][0], rgb[0][1], rgb[0][2] }, { rgb[k][0], rgb[k][1], rgb[k][2] }, { rgb[k + 1][0], rgb[k + 1][1], rgb[k + 1][2] } };
            if (triangle_setup(r, w, h, tr, col, tri + ntri * TRI_DOUBLES, boxes[ntri])) { paint[ntri] = r->paint_index; ntri++; }
        }
    }

    if (argc >= 4) {        /* the fast form: gpu_stage TRACE BANDS.dxil BANDS_FAST.dxil: float, one thread per pixel, one dispatch; a tolerance report, not an equality test */
        size_t fsz = 0; void *fcode = SDL_LoadFile(argv[3], &fsz);
        if (!fcode) { fprintf(stderr, "cannot read %s\n", argv[3]); return 2; }
        SDL_GPUComputePipelineCreateInfo fci = { 0 };
        fci.code = (const Uint8 *)fcode; fci.code_size = fsz; fci.entrypoint = "main"; fci.format = SDL_GPU_SHADERFORMAT_DXIL;
        fci.num_readonly_storage_buffers = 1; fci.num_readwrite_storage_buffers = 2; fci.num_uniform_buffers = 1;
        fci.threadcount_x = 64; fci.threadcount_y = 1; fci.threadcount_z = 1;
        SDL_GPUComputePipeline *fp = SDL_CreateGPUComputePipeline(dev, &fci);
        if (!fp) { fprintf(stderr, "fast pipeline: %s\n", SDL_GetError()); return 2; }
        uint32_t *words = (uint32_t *)calloc(ntri ? ntri : 1, 24 * 4);
        for (size_t k = 0; k < ntri; k++) {
            const double *d = tri + k * TRI_DOUBLES; float fl[24]; uint32_t *wd = words + k * 24; memset(fl, 0, sizeof fl);
            const float fv[19] = { (float)d[6], (float)d[7], (float)d[8], (float)d[9], (float)d[4], (float)d[5], (float)d[10], (float)d[11], (float)d[12],
                                   (float)d[13], (float)d[14], (float)d[15], (float)d[16], (float)d[17], (float)d[18], (float)d[19], (float)d[20], (float)d[21], 0.0f };
            for (int j = 0; j < 18; j++) memcpy(&wd[j], &fv[j], 4);
            wd[18] = paint[k];
            wd[19] = (uint32_t)boxes[k][0]; wd[20] = (uint32_t)boxes[k][1]; wd[21] = (uint32_t)boxes[k][2]; wd[22] = (uint32_t)boxes[k][3];
        }
        const Uint32 tbytes = (Uint32)((ntri ? ntri : 1) * 24 * 4), fb = (Uint32)(px * 4);
        SDL_GPUBufferCreateInfo b1 = { SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, tbytes, 0 };
        SDL_GPUBuffer *tb = SDL_CreateGPUBuffer(dev, &b1);
        b1.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE; b1.size = fb;
        SDL_GPUBuffer *ob[2] = { SDL_CreateGPUBuffer(dev, &b1), SDL_CreateGPUBuffer(dev, &b1) };
        SDL_GPUTransferBufferCreateInfo t1 = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, tbytes, 0 };
        SDL_GPUTransferBuffer *tu = SDL_CreateGPUTransferBuffer(dev, &t1);
        t1.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; t1.size = fb;
        SDL_GPUTransferBuffer *td[2] = { SDL_CreateGPUTransferBuffer(dev, &t1), SDL_CreateGPUTransferBuffer(dev, &t1) };
        void *m1 = SDL_MapGPUTransferBuffer(dev, tu, false); memcpy(m1, words, tbytes); SDL_UnmapGPUTransferBuffer(dev, tu);
        const Uint32 fparams[4] = { (Uint32)w, (Uint32)h, (Uint32)ntri, 0 };
        SDL_GPUStorageBufferReadWriteBinding frw[2] = { { ob[0], false }, { ob[1], false } };
        const char *e = getenv("GPU_STAGE_REPEAT"); const int reps = e ? atoi(e) : 1;
        SDL_GPUCommandBuffer *cbf = SDL_AcquireGPUCommandBuffer(dev);
        SDL_GPUCopyPass *cpf = SDL_BeginGPUCopyPass(cbf);
        SDL_GPUTransferBufferLocation fl = { tu, 0 }; SDL_GPUBufferRegion fr = { tb, 0, tbytes };
        SDL_UploadToGPUBuffer(cpf, &fl, &fr, false); SDL_EndGPUCopyPass(cpf);
        for (int rep = 0; rep < reps; rep++) {
            SDL_GPUComputePass *cps = SDL_BeginGPUComputePass(cbf, NULL, 0, frw, 2);
            SDL_BindGPUComputePipeline(cps, fp); SDL_BindGPUComputeStorageBuffers(cps, 0, &tb, 1);
            SDL_PushGPUComputeUniformData(cbf, 0, fparams, sizeof fparams);
            SDL_DispatchGPUCompute(cps, ((Uint32)w + 63) / 64, (Uint32)h, 1); SDL_EndGPUComputePass(cps);
        }
        cpf = SDL_BeginGPUCopyPass(cbf);
        for (int k = 0; k < 2; k++) { SDL_GPUBufferRegion bk = { ob[k], 0, fb }; SDL_GPUTransferBufferLocation in2 = { td[k], 0 }; SDL_DownloadFromGPUBuffer(cpf, &bk, &in2); }
        SDL_EndGPUCopyPass(cpf);
        const Uint64 f0 = SDL_GetTicksNS();
        SDL_GPUFence *ff = SDL_SubmitGPUCommandBufferAndAcquireFence(cbf);
        SDL_WaitForGPUFences(dev, true, &ff, 1); SDL_ReleaseGPUFence(dev, ff);
        const double fms = (SDL_GetTicksNS() - f0) / 1e6;
        const uint32_t *gc = (const uint32_t *)SDL_MapGPUTransferBuffer(dev, td[0], false), *gi = (const uint32_t *)SDL_MapGPUTransferBuffer(dev, td[1], false);
        size_t nd = 0, ni = 0, big = 0; int maxd = 0;
        for (size_t i = 0; i < px; i++) {
            if (gi[i] != t.index[i]) ni++;
            if (gc[i] != t.color[i]) {
                nd++;
                int m = 0;
                for (int sh = 0; sh < 24; sh += 8) { int dd = (int)(gc[i] >> sh & 255) - (int)(t.color[i] >> sh & 255); if (dd < 0) dd = -dd; if (dd > m) m = dd; }
                if (m > maxd) maxd = m;
                if (m > 1) big++;
            }
        }
        printf("fast form, %d x %d: %zu of %zu pixels differ in colour (%.4f%%), the largest channel difference %d level(s), %zu differ by more than 1; %zu index differ; %d run(s) in %.3f ms (%.3f ms each, with upload and readback once)\n",
               w, h, nd, px, 100.0 * (double)nd / (double)px, maxd, big, ni, reps, fms, fms / reps);
        return 0;
    }
    const Uint32 pbytes = (Uint32)(px * 4);
    SDL_GPUBufferCreateInfo bi = { SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, (Uint32)(ntri * TRI_DOUBLES * sizeof(double) + 16), 0 };
    SDL_GPUBuffer *tribuf = SDL_CreateGPUBuffer(dev, &bi);
    bi.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE; bi.size = pbytes;
    SDL_GPUBuffer *buf[4];
    for (int k = 0; k < 4; k++) buf[k] = SDL_CreateGPUBuffer(dev, &bi);
    SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, (Uint32)(ntri * TRI_DOUBLES * sizeof(double) + 16), 0 };
    SDL_GPUTransferBuffer *up = SDL_CreateGPUTransferBuffer(dev, &ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; ti.size = pbytes;
    SDL_GPUTransferBuffer *down[4];
    for (int k = 0; k < 4; k++) down[k] = SDL_CreateGPUTransferBuffer(dev, &ti);
    void *map = SDL_MapGPUTransferBuffer(dev, up, false);
    memcpy(map, tri, ntri * TRI_DOUBLES * sizeof(double));
    SDL_UnmapGPUTransferBuffer(dev, up);

    const Uint64 t0 = SDL_GetTicksNS();
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTransferBufferLocation from = { up, 0 };
    SDL_GPUBufferRegion to = { tribuf, 0, (Uint32)(ntri * TRI_DOUBLES * sizeof(double)) };
    if (to.size) SDL_UploadToGPUBuffer(cp, &from, &to, false);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUStorageBufferReadWriteBinding rw[4];
    for (int k = 0; k < 4; k++) { rw[k].buffer = buf[k]; rw[k].cycle = false; }
    typedef struct { Uint32 mode, tri_at, w, h; int y0, y1, x0, x1; Uint32 paint, pad0, pad1, pad2; } params;
    #define PASS(p, groups) do { SDL_GPUComputePass *cps = SDL_BeginGPUComputePass(cb, NULL, 0, rw, 4); SDL_BindGPUComputePipeline(cps, pipe); \
        SDL_BindGPUComputeStorageBuffers(cps, 0, &tribuf, 1); SDL_PushGPUComputeUniformData(cb, 0, &(p), sizeof(p)); \
        SDL_DispatchGPUCompute(cps, (groups), 1, 1); SDL_EndGPUComputePass(cps); } while (0)
    params p = { 0, 0, (Uint32)w, (Uint32)h, 0, 0, 0, 0, 0, 0, 0, 0 };
    PASS(p, ((Uint32)px + 255) / 256);
    for (size_t k = 0; k < ntri; k++) {
        params q = { 1, (Uint32)(k * TRI_DOUBLES * 2), (Uint32)w, (Uint32)h, boxes[k][2], boxes[k][3], boxes[k][0], boxes[k][1], paint[k], 0, 0, 0 };
        const int rows = boxes[k][3] - boxes[k][2] + 1;
        if (rows > 0) PASS(q, ((Uint32)rows + 255) / 256);
    }
    params z = { 2, 0, (Uint32)w, (Uint32)h, 0, 0, 0, 0, 0, 0, 0, 0 };
    PASS(z, ((Uint32)px + 255) / 256);
    cp = SDL_BeginGPUCopyPass(cb);
    for (int k = 0; k < 4; k++) {
        SDL_GPUBufferRegion back = { buf[k], 0, pbytes };
        SDL_GPUTransferBufferLocation into = { down[k], 0 };
        SDL_DownloadFromGPUBuffer(cp, &back, &into);
    }
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
    SDL_WaitForGPUFences(dev, true, &fence, 1);
    SDL_ReleaseGPUFence(dev, fence);
    const Uint64 t1 = SDL_GetTicksNS();

    {   /* GPU_STAGE_REPEAT=N: the compute passes alone (no upload, no readback), N times in one submission */
        const char *e = getenv("GPU_STAGE_REPEAT");
        const int reps = e ? atoi(e) : 0;
        if (reps > 0) {
            SDL_GPUCommandBuffer *c2 = SDL_AcquireGPUCommandBuffer(dev);
            #define PASS2(p, groups) do { SDL_GPUComputePass *cps = SDL_BeginGPUComputePass(c2, NULL, 0, rw, 4); SDL_BindGPUComputePipeline(cps, pipe);                 SDL_BindGPUComputeStorageBuffers(cps, 0, &tribuf, 1); SDL_PushGPUComputeUniformData(c2, 0, &(p), sizeof(p));                 SDL_DispatchGPUCompute(cps, (groups), 1, 1); SDL_EndGPUComputePass(cps); } while (0)
            for (int rep = 0; rep < reps; rep++) {
                PASS2(p, ((Uint32)px + 255) / 256);
                for (size_t k = 0; k < ntri; k++) {
                    params q = { 1, (Uint32)(k * TRI_DOUBLES * 2), (Uint32)w, (Uint32)h, boxes[k][2], boxes[k][3], boxes[k][0], boxes[k][1], paint[k], 0, 0, 0 };
                    const int rows = boxes[k][3] - boxes[k][2] + 1;
                    if (rows > 0) PASS2(q, ((Uint32)rows + 255) / 256);
                }
                PASS2(z, ((Uint32)px + 255) / 256);
            }
            const Uint64 r0 = SDL_GetTicksNS();
            SDL_GPUFence *fe = SDL_SubmitGPUCommandBufferAndAcquireFence(c2);
            SDL_WaitForGPUFences(dev, true, &fe, 1);
            SDL_ReleaseGPUFence(dev, fe);
            printf("compute only: %.3f ms per run of clear + %zu band triangles + depth reset (%d runs)\n", (SDL_GetTicksNS() - r0) / 1e6 / reps, ntri, reps);
        }
    }
    const uint32_t *g_col = (const uint32_t *)SDL_MapGPUTransferBuffer(dev, down[0], false);
    const uint32_t *g_idx = (const uint32_t *)SDL_MapGPUTransferBuffer(dev, down[1], false);
    const uint32_t *g_z = (const uint32_t *)SDL_MapGPUTransferBuffer(dev, down[2], false);
    const uint32_t *g_own = (const uint32_t *)SDL_MapGPUTransferBuffer(dev, down[3], false);
    size_t bad_c = 0, bad_i = 0, bad_z = 0, bad_o = 0;
    for (size_t i = 0; i < px; i++) {
        uint32_t cz; memcpy(&cz, &t.inv_z[i], 4);
        if (g_col[i] != t.color[i]) { if (bad_c < 3) fprintf(stderr, "colour differs at %zu,%zu: gpu %08X, cpu %08X\n", i % w, i / w, g_col[i], t.color[i]); bad_c++; }
        bad_i += g_idx[i] != t.index[i]; bad_z += g_z[i] != cz; bad_o += g_own[i] != t.owner[i];
    }
    printf("%d x %d, %zu band triangles of %u records: %zu colour, %zu index, %zu depth, %zu owner pixels differ (GPU %.2f ms including upload and readback)\n",
           w, h, ntri, last + 1, bad_c, bad_i, bad_z, bad_o, (t1 - t0) / 1e6);
    return bad_c || bad_i || bad_z || bad_o;
}
