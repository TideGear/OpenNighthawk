/* gpu_compose.h - the Remastered composite on the GPU (docs/gpu-compositor.md).
 *
 * Takes the raster calls of one composite (the records gpu_trace.h describes) and the replace step's inputs, runs the clear and bands, the polygons, the lines
 * and dots and the replace step as compute passes on its own SDL GPU device (no window), and returns the wide picture as the CPU compositor would have made it
 * (to within the float differences docs/gpu-compositor.md lists). The records come from the raster functions' trace hook, set to skip the CPU's drawing.
 * When the device, the shaders or the records cannot be handled it returns 0 and the caller uses the CPU compositor. */
#ifndef F117R_GPU_COMPOSE_H
#define F117R_GPU_COMPOSE_H

#include <stdint.h>
#include "gpu_trace.h"

typedef struct gpu_compose gpu_compose;

typedef struct {
    const gpu_trace_rec *recs;
    uint32_t nrec;
    const uint32_t *palette;        /* 256 colours (0xAARRGGBB): the AND / OR / stipple fills */
    int w, h;                       /* the game's picture, in fine pixels */
    int tw;                         /* the canvas width (the wide picture) */
    int xoff;                       /* where the game's picture starts on the canvas */
    int x0, x1, y0, y1;             /* the game's 3D window in the picture's pixels */
    int complete;                   /* the scene owns its background */
    const uint8_t *src, *tag;       /* the game's picture (palette indices) and its origin tags, w * h */
    const uint8_t *dac;             /* 768 six-bit palette bytes */
    const uint8_t *tagflags;        /* 256: bit 0 scene tag, bit 1 outline tag, bit 2 span tag */
} gpu_compose_in;

/* shader_dir holds gpu_bands_fast.dxil, gpu_polys.dxil, gpu_strokes.dxil, gpu_replace.dxil and gpu_present.dxil. NULL when there is no usable GPU device. */
gpu_compose *gpu_compose_create(const char *shader_dir);
/* The same on a device the caller owns (the app's GPU renderer's, SDL_GetGPURendererDevice): it is not destroyed with the compositor. */
struct SDL_GPUDevice;
struct SDL_GPUTexture;
gpu_compose *gpu_compose_create_on(struct SDL_GPUDevice *device, const char *shader_dir);
void gpu_compose_destroy(gpu_compose *g);

/* In two halves, so the CPU can make the game's own picture while the GPU draws: begin takes everything but src, tag and dac (which it ignores), uploads and runs the bands,
 * polygons and strokes and returns without waiting; finish takes the game's picture, its tags and its palette, runs the replace step and writes the wide picture. 1 on success; 0 when
 * the CPU compositor has to be used (the reason is on stderr once). Finish without a successful begin returns 0. */
int gpu_compose_begin(gpu_compose *g, const gpu_compose_in *in);
int gpu_compose_finish(gpu_compose *g, const uint8_t *src, const uint8_t *tag, const uint8_t *dac, uint32_t *wide_out);

/* Piece 3 (no readback): two w x h RGBA textures (double-buffered: finish writes one while the caller draws the other) that the compositor writes the finished picture into itself
 * (a compute pass after the replace step). The caller wraps them as textures of its own renderer (SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, SDL_PIXELFORMAT_RGBA32) and draws the one
 * gpu_compose_last_written names. gpu_compose_make_targets returns 1 on success; gpu_compose_target(g, i) the i-th texture. With targets set, finish may be given a NULL wide_out
 * to skip the readback; with both, the readback still happens (verification). gpu_compose_read_target downloads the last written one as 0xAARRGGBB words (verification only). */
int gpu_compose_make_targets(gpu_compose *g, int w, int h);
struct SDL_GPUTexture *gpu_compose_target(gpu_compose *g, int i);
int gpu_compose_last_written(const gpu_compose *g);
int gpu_compose_read_target(gpu_compose *g, uint32_t *out);

/* Both, one after the other: the wide picture, tw * h pixels, into wide_out. */
int gpu_compose_run(gpu_compose *g, const gpu_compose_in *in, uint32_t *wide_out);

/* Milliseconds of the last run: CPU setup, upload and compute (to the end of the submission), readback. For diagnostics. */
void gpu_compose_times(const gpu_compose *g, double *setup_ms, double *gpu_ms, double *copy_ms);

#endif
