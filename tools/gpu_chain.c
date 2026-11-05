/* gpu_chain.c - all the GPU compositor's stages chained (gpu_compose.h) on a recorded wide composite (gpu_trace.h with its replace section), compared with the final
 * picture the CPU recorded.
 *
 * usage: gpu_chain TRACE SHADER_DIR      GPU_STAGE_REPEAT=N times N runs after the first; 77 without a GPU */
#include "gpu_compose.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: gpu_chain TRACE SHADER_DIR\n"); return 2; }
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
    if (fread(&rp, sizeof rp, 1, f) != 1) { fprintf(stderr, "the trace has no replace section\n"); return 2; }
    const size_t npix = (size_t)rp.w * rp.h;
    uint8_t *src = (uint8_t *)malloc(npix + 4), *tag = (uint8_t *)malloc(npix + 4), dac[768];
    uint32_t *want = (uint32_t *)malloc(px * 4), *got = (uint32_t *)malloc(px * 4);
    if (!src || !tag || fread(src, 1, npix, f) != npix || fread(tag, 1, npix, f) != npix || fread(dac, 1, 768, f) != 768 || fread(want, 4, px, f) != px) { fprintf(stderr, "short replace section\n"); return 2; }
    fclose(f);

    gpu_compose *g = gpu_compose_create(argv[2]);
    if (!g) { fprintf(stderr, "no GPU compositor (skipped)\n"); return 77; }
    gpu_compose_in in;
    memset(&in, 0, sizeof in);
    in.recs = recs; in.nrec = hd.nrec; in.palette = hd.palette;
    in.w = rp.w; in.h = rp.h; in.tw = rp.tw; in.xoff = rp.xoff; in.x0 = rp.x0; in.x1 = rp.x1; in.y0 = rp.y0; in.y1 = rp.y1; in.complete = rp.complete;
    in.src = src; in.tag = tag; in.dac = dac; in.tagflags = rp.tagflags;
    if (!gpu_compose_run(g, &in, got)) { fprintf(stderr, "the GPU compositor declined this frame\n"); return 1; }
    size_t bad = 0, big = 0; int maxd = 0;
    for (size_t i = 0; i < px; i++)
        if (got[i] != want[i]) {
            bad++;
            int m = 0;
            for (int sh = 0; sh < 24; sh += 8) { int d = (int)(got[i] >> sh & 255) - (int)(want[i] >> sh & 255); if (d < 0) d = -d; if (d > m) m = d; }
            if (m > maxd) maxd = m;
            if (m > 8) big++;
        }
    printf("chain, %d x %d canvas: %zu of %zu pixels differ from the CPU's wide picture (%.4f%%), %zu by more than 8 levels (largest %d)\n", rp.tw, rp.h, bad, px, 100.0 * (double)bad / (double)px, big, maxd);
    const char *e = getenv("GPU_STAGE_REPEAT");
    const int reps = e ? atoi(e) : 0;
    if (reps > 0) {
        double setup = 0, gpu = 0, mx_s = 0, mx_g = 0;
        const Uint64 t0 = SDL_GetTicksNS();
        for (int k = 0; k < reps; k++) {
            gpu_compose_run(g, &in, got);
            double s, gg, cc; gpu_compose_times(g, &s, &gg, &cc);
            setup += s; gpu += gg; if (s > mx_s) mx_s = s; if (gg > mx_g) mx_g = gg;
        }
        printf("%d runs, each synchronous: %.3f ms (CPU setup and upload %.3f, GPU compute, readback and copy %.3f; worst %.3f and %.3f)\n", reps,
               (double)(SDL_GetTicksNS() - t0) / 1e6 / reps, setup / reps, gpu / reps, mx_s, mx_g);
    }
    gpu_compose_destroy(g);
    return bad != 0;
}
