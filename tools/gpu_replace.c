/* gpu_replace.c - GPU compositor stage 5 (docs/gpu-compositor.md): the replace step of a recorded wide composite (gpu_trace.h, with its replace section) on a hidden D3D12
 * device, compared with the final picture the CPU recorded. The composite's depth and stroke buffers come from a CPU replay of the trace's raster calls, so this stage is
 * checked on its own, as the others are.
 *
 * usage: gpu_replace TRACE REPLACE.dxil      GPU_STAGE_REPEAT=N times N runs; 77 without a GPU */
#include "gpu_trace.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_strokes_cleared;

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

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: gpu_replace TRACE REPLACE.dxil\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    gpu_trace_header hd;
    if (fread(&hd, sizeof hd, 1, f) != 1 || hd.magic != GPU_TRACE_MAGIC) { fprintf(stderr, "not a trace\n"); return 2; }
    const size_t px = (size_t)hd.w * hd.h;
    gpu_trace_rec *recs = (gpu_trace_rec *)malloc(hd.nrec * sizeof *recs);
    uint32_t *rec_col = (uint32_t *)malloc(px * 4), *rec_own = (uint32_t *)malloc(px * 4);
    uint16_t *rec_idx = (uint16_t *)malloc(px * 2);
    if (!recs || fread(recs, sizeof *recs, hd.nrec, f) != hd.nrec || fread(rec_col, 4, px, f) != px || fread(rec_idx, 2, px, f) != px || fread(rec_own, 4, px, f) != px) { fprintf(stderr, "short trace\n"); return 2; }
    gpu_trace_replace rp;
    if (fread(&rp, sizeof rp, 1, f) != 1) { fprintf(stderr, "the trace has no replace section (a wide picture is needed)\n"); return 2; }
    const size_t npix = (size_t)rp.w * rp.h;
    uint8_t *src = (uint8_t *)malloc(npix + 4), *tag = (uint8_t *)malloc(npix + 4), dac[768];
    uint32_t *want = (uint32_t *)malloc(px * 4);
    if (!src || !tag || fread(src, 1, npix, f) != npix || fread(tag, 1, npix, f) != npix || fread(dac, 1, 768, f) != 768 || fread(want, 4, px, f) != px) { fprintf(stderr, "short replace section\n"); return 2; }
    fclose(f);
    memset(src + npix, 0, 4); memset(tag + npix, 0, 4);
    const int w = rp.w, h = rp.h, tw = rp.tw;

    /* the composite's buffers by a CPU replay of every raster call */
    remaster_target t; memset(&t, 0, sizeof t);
    t.w = tw; t.h = h;
    t.color = (uint32_t *)calloc(px, 4); t.inv_z = (float *)calloc(px, 4); t.index = (uint16_t *)calloc(px, 2); t.owner = (uint32_t *)calloc(px, 4);
    t.stroke_z = (float *)calloc(px, 4); t.stroke_aux_x = (float *)calloc(px, 4); t.stroke_aux_y = (float *)calloc(px, 4); t.stroke_owner = (uint32_t *)calloc(px, 4);
    t.palette = hd.palette;
    for (uint32_t i = 0; i < hd.nrec; i++) cpu_apply(&t, &recs[i]);
    size_t pre = 0;
    for (size_t i = 0; i < px; i++) pre += t.color[i] != rec_col[i];
    if (pre) fprintf(stderr, "warning: the replayed colour differs from the recorded one in %zu pixels\n", pre);

    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL_Init (skipped): %s\n", SDL_GetError()); return 77; }
    size_t csz = 0; void *code = SDL_LoadFile(argv[2], &csz);
    if (!code) { fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
    SDL_GPUDevice *dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, false, NULL);
    if (!dev) { fprintf(stderr, "no GPU device (skipped): %s\n", SDL_GetError()); return 77; }
    SDL_GPUComputePipelineCreateInfo pci = { 0 };
    pci.code = (const Uint8 *)code; pci.code_size = csz; pci.entrypoint = "main"; pci.format = SDL_GPU_SHADERFORMAT_DXIL;
    pci.num_readonly_storage_buffers = 4; pci.num_readwrite_storage_buffers = 4; pci.num_uniform_buffers = 1;
    pci.threadcount_x = 16; pci.threadcount_y = 16; pci.threadcount_z = 1;
    SDL_GPUComputePipeline *pipe = SDL_CreateGPUComputePipeline(dev, &pci);
    if (!pipe) { fprintf(stderr, "pipeline: %s\n", SDL_GetError()); return 2; }

    const Uint32 pbytes = (Uint32)(px * 4);
    uint32_t flags[256]; for (int k = 0; k < 256; k++) flags[k] = rp.tagflags[k];
    const Uint32 ro_size[4] = { 1024, (Uint32)((npix + 7) & ~3u), (Uint32)((npix + 7) & ~3u), 768 };
    SDL_GPUBufferCreateInfo bi = { SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, 0, 0 };
    SDL_GPUBuffer *ro[4];
    for (int k = 0; k < 4; k++) { bi.size = ro_size[k]; ro[k] = SDL_CreateGPUBuffer(dev, &bi); }
    bi.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE; bi.size = pbytes;
    SDL_GPUBuffer *rw[4];
    for (int k = 0; k < 4; k++) rw[k] = SDL_CreateGPUBuffer(dev, &bi);
    const Uint32 total_up = ro_size[0] + ro_size[1] + ro_size[2] + ro_size[3] + 3 * pbytes;
    SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, total_up, 0 };
    SDL_GPUTransferBuffer *up = SDL_CreateGPUTransferBuffer(dev, &ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; ti.size = pbytes;
    SDL_GPUTransferBuffer *down = SDL_CreateGPUTransferBuffer(dev, &ti);
    {
        Uint8 *p = (Uint8 *)SDL_MapGPUTransferBuffer(dev, up, false);
        memcpy(p, flags, 1024); p += ro_size[0];
        memset(p, 0, ro_size[1]); memcpy(p, src, npix); p += ro_size[1];
        memset(p, 0, ro_size[2]); memcpy(p, tag, npix); p += ro_size[2];
        memcpy(p, dac, 768); p += ro_size[3];
        memcpy(p, t.color, pbytes); p += pbytes; memcpy(p, t.inv_z, pbytes); p += pbytes; memcpy(p, t.stroke_z, pbytes);
        SDL_UnmapGPUTransferBuffer(dev, up);
    }
    const char *e = getenv("GPU_STAGE_REPEAT"); const int reps = e ? atoi(e) : 1;
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    {
        Uint32 at = 0;
        for (int k = 0; k < 4; k++) { SDL_GPUTransferBufferLocation from = { up, at }; SDL_GPUBufferRegion to = { ro[k], 0, ro_size[k] }; SDL_UploadToGPUBuffer(cp, &from, &to, false); at += ro_size[k]; }
        for (int k = 0; k < 3; k++) { SDL_GPUTransferBufferLocation from = { up, at }; SDL_GPUBufferRegion to = { rw[k], 0, pbytes }; SDL_UploadToGPUBuffer(cp, &from, &to, false); at += pbytes; }
    }
    SDL_EndGPUCopyPass(cp);
    SDL_GPUStorageBufferReadWriteBinding rwb[4];
    for (int k = 0; k < 4; k++) { rwb[k].buffer = rw[k]; rwb[k].cycle = false; }
    typedef struct { Uint32 w, h, tw, xoff; int x0, x1, y0, y1; Uint32 complete, p0, p1, p2; } params;
    const params q = { (Uint32)w, (Uint32)h, (Uint32)tw, (Uint32)rp.xoff, rp.x0, rp.x1, rp.y0, rp.y1, (Uint32)rp.complete, 0, 0, 0 };
    for (int rep = 0; rep < reps; rep++) {
        SDL_GPUComputePass *cps = SDL_BeginGPUComputePass(cb, NULL, 0, rwb, 4);
        SDL_BindGPUComputePipeline(cps, pipe);
        SDL_BindGPUComputeStorageBuffers(cps, 0, ro, 4);
        SDL_PushGPUComputeUniformData(cb, 0, &q, sizeof q);
        SDL_DispatchGPUCompute(cps, ((Uint32)tw + 15) / 16, ((Uint32)h + 15) / 16, 1);
        SDL_EndGPUComputePass(cps);
    }
    cp = SDL_BeginGPUCopyPass(cb);
    { SDL_GPUBufferRegion bk = { rw[3], 0, pbytes }; SDL_GPUTransferBufferLocation in2 = { down, 0 }; SDL_DownloadFromGPUBuffer(cp, &bk, &in2); }
    SDL_EndGPUCopyPass(cp);
    const Uint64 t0 = SDL_GetTicksNS();
    SDL_GPUFence *fe = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
    SDL_WaitForGPUFences(dev, true, &fe, 1);
    SDL_ReleaseGPUFence(dev, fe);
    const double ms = (SDL_GetTicksNS() - t0) / 1e6;
    const uint32_t *got = (const uint32_t *)SDL_MapGPUTransferBuffer(dev, down, false);
    size_t bad = 0;
    for (size_t i = 0; i < px; i++) {
        if (got[i] != want[i]) { if (bad < 3) fprintf(stderr, "differs at %zu,%zu: gpu %08X, cpu %08X\n", i % tw, i / tw, got[i], want[i]); bad++; }
    }
    printf("replace, %d x %d canvas over a %d x %d picture: %zu of %zu pixels differ from the CPU's wide picture; %d run(s) in %.3f ms (%.3f ms a run, with the upload and one readback once)\n",
           tw, h, w, h, bad, px, reps, ms, ms / reps);
    return bad != 0;
}
