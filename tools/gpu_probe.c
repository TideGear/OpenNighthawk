/* gpu_probe.c - GPU compositor stage 1 (docs/gpu-compositor.md): create an SDL GPU device with no window, run a compute
 * shader over a 3840 x 2160 grid, read the result back and compare every value with the CPU's. Proves the toolchain
 * (dxc to DXIL, SDL's binding rules, readback) before any compositor rule is ported.
 *
 * usage: gpu_probe SHADER.dxil      exit 0 when every value matches */
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: gpu_probe SHADER.dxil\n"); return 2; }
    size_t code_size = 0;
    void *code = SDL_LoadFile(argv[1], &code_size);
    if (!code) { fprintf(stderr, "cannot read %s: %s\n", argv[1], SDL_GetError()); return 2; }
    if (!SDL_Init(SDL_INIT_VIDEO)) {     /* SDL 3.2 creates GPU devices through the video subsystem; no window is made */ fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GPUDevice *dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, false, NULL);
    if (!dev) { fprintf(stderr, "SDL_CreateGPUDevice: %s\n", SDL_GetError()); return 1; }
    printf("device: %s\n", SDL_GetGPUDeviceDriver(dev));

    const Uint32 w = 3840, h = 2160, n = w * h, bytes = n * 4;
    SDL_GPUComputePipelineCreateInfo pci = { 0 };
    pci.code = (const Uint8 *)code; pci.code_size = code_size; pci.entrypoint = "main";
    pci.format = SDL_GPU_SHADERFORMAT_DXIL;
    pci.num_readonly_storage_buffers = 1; pci.num_readwrite_storage_buffers = 1; pci.num_uniform_buffers = 1;
    pci.threadcount_x = 8; pci.threadcount_y = 8; pci.threadcount_z = 1;
    SDL_GPUComputePipeline *pipe = SDL_CreateGPUComputePipeline(dev, &pci);
    if (!pipe) { fprintf(stderr, "SDL_CreateGPUComputePipeline: %s\n", SDL_GetError()); return 1; }

    SDL_GPUBufferCreateInfo bi = { SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, bytes, 0 };
    SDL_GPUBuffer *src = SDL_CreateGPUBuffer(dev, &bi);
    bi.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    SDL_GPUBuffer *dst = SDL_CreateGPUBuffer(dev, &bi);
    SDL_GPUTransferBufferCreateInfo ti = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes, 0 };
    SDL_GPUTransferBuffer *up = SDL_CreateGPUTransferBuffer(dev, &ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    SDL_GPUTransferBuffer *down = SDL_CreateGPUTransferBuffer(dev, &ti);
    if (!src || !dst || !up || !down) { fprintf(stderr, "buffers: %s\n", SDL_GetError()); return 1; }

    uint32_t *in = (uint32_t *)SDL_MapGPUTransferBuffer(dev, up, false);
    for (Uint32 i = 0; i < n; i++) in[i] = i ^ 0x5A5A5A5Au;
    SDL_UnmapGPUTransferBuffer(dev, up);

    const Uint64 t0 = SDL_GetTicksNS();
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTransferBufferLocation from = { up, 0 };
    SDL_GPUBufferRegion to = { src, 0, bytes };
    SDL_UploadToGPUBuffer(cp, &from, &to, false);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUStorageBufferReadWriteBinding rw = { dst, false };
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(cb, NULL, 0, &rw, 1);
    SDL_BindGPUComputePipeline(pass, pipe);
    SDL_BindGPUComputeStorageBuffers(pass, 0, &src, 1);
    const Uint32 params[4] = { w, h, 0, 0 };
    SDL_PushGPUComputeUniformData(cb, 0, params, sizeof params);
    SDL_DispatchGPUCompute(pass, (w + 7) / 8, (h + 7) / 8, 1);
    SDL_EndGPUComputePass(pass);
    cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUBufferRegion back = { dst, 0, bytes };
    SDL_GPUTransferBufferLocation into = { down, 0 };
    SDL_DownloadFromGPUBuffer(cp, &back, &into);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
    SDL_WaitForGPUFences(dev, true, &fence, 1);
    SDL_ReleaseGPUFence(dev, fence);
    const Uint64 t1 = SDL_GetTicksNS();

    const uint32_t *out = (const uint32_t *)SDL_MapGPUTransferBuffer(dev, down, false);
    Uint32 bad = 0;
    for (Uint32 y = 0; y < h; y++)
        for (Uint32 x = 0; x < w; x++) {
            const Uint32 i = y * w + x;
            const uint32_t want = (i ^ 0x5A5A5A5Au) * 2654435761u + x * 31u + y;
            if (out[i] != want && bad++ < 4) fprintf(stderr, "differs at %u,%u: %08X, want %08X\n", x, y, out[i], want);
        }
    SDL_UnmapGPUTransferBuffer(dev, down);
    printf("%u of %u values differ; upload, dispatch and readback %.2f ms\n", bad, n, (t1 - t0) / 1e6);
    SDL_ReleaseGPUComputePipeline(dev, pipe);
    SDL_ReleaseGPUBuffer(dev, src); SDL_ReleaseGPUBuffer(dev, dst);
    SDL_ReleaseGPUTransferBuffer(dev, up); SDL_ReleaseGPUTransferBuffer(dev, down);
    SDL_DestroyGPUDevice(dev);
    SDL_free(code);
    SDL_Quit();
    return bad ? 1 : 0;
}
