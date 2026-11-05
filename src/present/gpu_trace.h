/* gpu_trace.h - a recorded Remastered frame for the GPU compositor's oracle (docs/gpu-compositor.md).
 *
 * File: gpu_trace_header, then nrec gpu_trace_rec (every raster call of one composite, in order, with the target's state at the call),
 * then the CPU compositor's result: color (w * h uint32), index (w * h uint16), owner (w * h uint32). Written and read by the same build:
 * the structs are not a portable format. */
#ifndef F117R_GPU_TRACE_H
#define F117R_GPU_TRACE_H

#include <stdint.h>
#include "remaster_raster.h"

#define GPU_TRACE_MAGIC 0x31555047u      /* "GPU1" */

typedef struct {
    uint32_t magic;
    int32_t w, h;
    uint32_t nrec;
    uint32_t palette[256];
} gpu_trace_header;

typedef struct {
    int32_t kind, n, op;
    uint32_t color, value;
    double width;
    remaster_v3 v[16];
    uint32_t colors[16];
    double cx, cy, sx, sy, inv_depth_scale, line_bias;
    int32_t near_z, line_decal, line_order;
    uint32_t paint_owner;
    uint16_t paint_index, pad;
} gpu_trace_rec;

#endif
