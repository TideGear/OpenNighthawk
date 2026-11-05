/* gpu_replay.c - replays a recorded Remastered composite (gpu_trace.h, F117_GPU_DUMP) through the CPU rasterizer and compares the result with the
 * buffers recorded from the live composite. Zero differences proves the trace holds every input of the frame; it is the CPU oracle the GPU
 * compositor (docs/gpu-compositor.md) is compared with.
 *
 * usage: gpu_replay TRACE      exit 0 when colour, palette index and owner all match */
#include "gpu_trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void apply(remaster_target *t, const gpu_trace_rec *r, int *strokes_cleared)
{
    t->cx = r->cx; t->cy = r->cy; t->sx = r->sx; t->sy = r->sy; t->inv_depth_scale = r->inv_depth_scale; t->line_bias = r->line_bias;
    t->near_z = r->near_z; t->line_decal = r->line_decal; t->line_order = r->line_order; t->paint_owner = r->paint_owner; t->paint_index = r->paint_index;
    if (r->line_order && !*strokes_cleared) {       /* the live composite clears the stroke buffers once, before its line pass */
        const size_t px = (size_t)t->w * t->h;
        memset(t->stroke_z, 0, px * sizeof *t->stroke_z);
        *strokes_cleared = 1;
    }
    switch (r->kind) {
    case REMASTER_T_CLEAR: remaster_clear(t, r->color); break;
    case REMASTER_T_POLYGON: remaster_polygon(t, r->v, r->n, r->color); break;
    case REMASTER_T_SHADED: remaster_polygon_shaded(t, r->v, r->colors, r->n); break;
    case REMASTER_T_LOGIC: remaster_polygon_logic(t, r->v, r->n, r->op, r->value); break;
    case REMASTER_T_LINE: remaster_line(t, r->v[0], r->v[1], r->width, r->color); break;
    case REMASTER_T_ZRESET: memset(t->inv_z, 0, (size_t)t->w * t->h * sizeof *t->inv_z); break;
    case REMASTER_T_WINDOW:
        for (int y = 0; y < t->h; y++)
            for (int x = 0; x < t->w; x++)
                if (x < r->v[0].x || x >= r->v[1].x || y < r->v[0].y || y >= r->v[1].y) t->color[(long)y * t->w + x] = 0xFF000000u;
        break;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: gpu_replay TRACE\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    gpu_trace_header hd;
    if (fread(&hd, sizeof hd, 1, f) != 1 || hd.magic != GPU_TRACE_MAGIC) { fprintf(stderr, "not a trace\n"); return 2; }
    const size_t px = (size_t)hd.w * hd.h;
    gpu_trace_rec *recs = (gpu_trace_rec *)malloc(hd.nrec * sizeof *recs);
    uint32_t *want_c = (uint32_t *)malloc(px * 4), *want_o = (uint32_t *)malloc(px * 4);
    uint16_t *want_i = (uint16_t *)malloc(px * 2);
    if (!recs || !want_c || !want_o || !want_i ||
        fread(recs, sizeof *recs, hd.nrec, f) != hd.nrec || fread(want_c, 4, px, f) != px || fread(want_i, 2, px, f) != px || fread(want_o, 4, px, f) != px) {
        fprintf(stderr, "short trace\n"); return 2;
    }
    fclose(f);

    remaster_target t; memset(&t, 0, sizeof t);
    t.w = hd.w; t.h = hd.h;
    t.color = (uint32_t *)calloc(px, 4); t.inv_z = (float *)calloc(px, 4); t.index = (uint16_t *)calloc(px, 2); t.owner = (uint32_t *)calloc(px, 4);
    t.stroke_z = (float *)calloc(px, 4); t.stroke_aux_x = (float *)calloc(px, 4); t.stroke_aux_y = (float *)calloc(px, 4); t.stroke_owner = (uint32_t *)calloc(px, 4);
    t.palette = hd.palette;
    int cleared = 0;
    for (uint32_t i = 0; i < hd.nrec; i++) apply(&t, &recs[i], &cleared);

    size_t bad_c = 0, bad_i = 0, bad_o = 0;
    for (size_t i = 0; i < px; i++) {
        bad_c += t.color[i] != want_c[i]; bad_i += t.index[i] != want_i[i]; bad_o += t.owner[i] != want_o[i];
        if ((t.color[i] != want_c[i]) && bad_c <= 3) fprintf(stderr, "colour differs at %zu,%zu: %08X, recorded %08X\n", i % hd.w, i / hd.w, t.color[i], want_c[i]);
    }
    printf("%d x %d, %u raster calls: %zu colour, %zu index, %zu owner pixels differ from the recorded composite\n", hd.w, hd.h, hd.nrec, bad_c, bad_i, bad_o);
    return bad_c || bad_i || bad_o;
}
