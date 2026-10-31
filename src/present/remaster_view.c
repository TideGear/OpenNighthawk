/* remaster_view.c - the Remastered 3-D view (see remaster_view.h). */
#include "remaster_view.h"

#include "band_state.h"
#include "remaster_raster.h"
#include "scene_mesh.h"

#include <stdlib.h>
#include <string.h>

uint32_t remaster_dac_rgb(const uint8_t *c)
{
    return 0xFF000000u | ((uint32_t)((c[2] << 2) | (c[2] >> 4)) << 16) | ((uint32_t)((c[1] << 2) | (c[1] >> 4)) << 8) |
           (uint32_t)((c[0] << 2) | (c[0] >> 4));
}

static int word(const uint8_t *ds, unsigned o) { return (int)(int16_t)(ds[o] | ds[o + 1] << 8); }

void remaster_view_read(const uint8_t *ds, remaster_view_info *v)
{
    v->left = word(ds, 0x85FA); v->top = word(ds, 0x85FC); v->right = word(ds, 0x85FE); v->bottom = word(ds, 0x8600);
    v->centre_x = word(ds, 0x8602); v->centre_y = word(ds, 0x8604);
}

/* Sky and ground from the band pass's own state (band_state.h), unrounded and smooth: each class (16 sky bands, 16 ground
 * bands) is painted flat into float colour, then blurred across the bands (normalised, so the horizon stays sharp) and
 * dithered on the way out, so a gradient shows no steps. index receives each covered pixel's band palette index. */
static void paint_bands(const uint8_t *ds, const uint8_t *dac, int w, int h, double kx, double ky, int xoff, uint32_t *rgba,
                        uint8_t *index)
{
    int32_t rec[BAND_REC_BASE] = { BAND_REC_VERSION, 0, 0 };
    for (int k = 0; k < BAND_NWORDS; k++)
        rec[3 + k] = band_rec_byte[k] ? ds[band_rec_off[k]] : (uint16_t)(ds[band_rec_off[k]] | ds[band_rec_off[k] + 1] << 8);
    band_state st;
    band_fine_list *fl = (band_fine_list *)malloc(sizeof *fl);
    if (!fl) return;
    if (band_state_load(rec, BAND_REC_BASE, &st) && band_fine(&st, NULL, fl) == 0) {
        const size_t np = (size_t)w * h;
        float *acc = (float *)calloc(np * 4, sizeof(float)), *tmp = (float *)malloc(np * 4 * sizeof(float));
        uint8_t *mask = (uint8_t *)calloc(np, 1);
        if (acc && tmp && mask) {
            for (int cls = 0; cls < 2; cls++) {
                memset(acc, 0, np * 4 * sizeof(float));
                memset(mask, 0, np);
                for (int q = cls * 16; q < (cls + 1) * 16 && q < fl->n; q++) {
                    const band_fquad *b = &fl->q[q];
                    if (!b->valid) continue;
                    const uint8_t *c = &dac[3 * b->colour];
                    const float cr = (float)((c[0] << 2) | (c[0] >> 4)), cg = (float)((c[1] << 2) | (c[1] >> 4)),
                                cb = (float)((c[2] << 2) | (c[2] >> 4));
                    double X[4], Y[4];
                    for (int j = 0; j < 4; j++) { X[j] = (b->x[j] + 0.5) * kx + xoff; Y[j] = (b->y[j] + 0.5) * ky; }
                    double mnx = X[0], mxx = X[0], mny = Y[0], mxy = Y[0];
                    for (int j = 1; j < 4; j++) {
                        if (X[j] < mnx) mnx = X[j];
                        if (X[j] > mxx) mxx = X[j];
                        if (Y[j] < mny) mny = Y[j];
                        if (Y[j] > mxy) mxy = Y[j];
                    }
                    const int bx0 = mnx < 0 ? 0 : (int)mnx, bx1 = mxx > w - 1 ? w - 1 : (int)mxx;
                    const int by0 = mny < 0 ? 0 : (int)mny, by1 = mxy > h - 1 ? h - 1 : (int)mxy;
                    for (int y = by0; y <= by1; y++)
                        for (int x = bx0; x <= bx1; x++) {
                            int pos = 0, neg = 0;
                            for (int j = 0; j < 4; j++) {
                                const int j2 = (j + 1) & 3;
                                const double e = (X[j2] - X[j]) * (y + 0.5 - Y[j]) - (Y[j2] - Y[j]) * (x + 0.5 - X[j]);
                                if (e > 0) pos = 1; else if (e < 0) neg = 1;
                            }
                            if (pos && neg) continue;
                            float *a4 = &acc[((size_t)y * w + x) * 4];
                            a4[0] = cr; a4[1] = cg; a4[2] = cb; a4[3] = 1.0f;
                            mask[(size_t)y * w + x] = 1;
                            index[(size_t)y * w + x] = b->colour;
                        }
                }
                /* two vertical box blurs of about a band's height (a triangle kernel), weights carried in channel 3 */
                const int rad = (int)(ky * 3.0);
                for (int pass = 0; pass < 2; pass++) {
                    for (int x = 0; x < w; x++) {
                        double sum[4] = { 0, 0, 0, 0 };
                        for (int y = 0; y < rad && y < h; y++) for (int k = 0; k < 4; k++) sum[k] += acc[((size_t)y * w + x) * 4 + k];
                        for (int y = 0; y < h; y++) {
                            if (y + rad < h) for (int k = 0; k < 4; k++) sum[k] += acc[((size_t)(y + rad) * w + x) * 4 + k];
                            if (y - rad - 1 >= 0) for (int k = 0; k < 4; k++) sum[k] -= acc[((size_t)(y - rad - 1) * w + x) * 4 + k];
                            for (int k = 0; k < 4; k++) tmp[((size_t)y * w + x) * 4 + k] = (float)sum[k];
                        }
                    }
                    memcpy(acc, tmp, np * 4 * sizeof(float));
                }
                unsigned noise = 12345u;
                for (size_t i = 0; i < np; i++) {
                    if (!mask[i] || acc[i * 4 + 3] <= 0) continue;
                    noise = noise * 1664525u + 1013904223u;
                    const float d = ((noise >> 16) & 255) / 255.0f - 0.5f;
                    int v[3];
                    for (int k = 0; k < 3; k++) {
                        v[k] = (int)(acc[i * 4 + k] / acc[i * 4 + 3] + 0.5f + d);
                        if (v[k] < 0) v[k] = 0;
                        if (v[k] > 255) v[k] = 255;
                    }
                    rgba[i] = 0xFF000000u | ((uint32_t)v[2] << 16) | ((uint32_t)v[1] << 8) | (uint32_t)v[0];
                }
            }
        }
        free(acc); free(tmp); free(mask);
    }
    free(fl);
}

static const remaster_prim *g_sort;
static int by_distance(const void *a, const void *b)
{
    const remaster_prim *p = &g_sort[*(const size_t *)a], *q = &g_sort[*(const size_t *)b];
    if (p->zkey != q->zkey) return p->zkey > q->zkey ? -1 : 1;                 /* far first */
    if (p->request != q->request) return p->request < q->request ? -1 : 1;
    return *(const size_t *)a < *(const size_t *)b ? -1 : 1;
}

long remaster_view_render(const uint8_t *ds, const uint8_t *dac, const remaster_frame *frame, int w, int h, int base_w,
                          uint32_t *rgba)
{
    remaster_view_info v;
    remaster_view_read(ds, &v);
    const int xoff = (w - base_w) / 2;
    const double kx = base_w / 320.0, ky = h / 200.0;
    remaster_target t;
    memset(&t, 0, sizeof t);
    t.w = w; t.h = h; t.color = rgba;
    t.inv_z = (float *)malloc((size_t)w * h * sizeof(float));
    t.index = (uint8_t *)calloc((size_t)w * h, 1);
    if (!t.inv_z || !t.index) { free(t.inv_z); free(t.index); return 0; }
    t.cx = kx * v.centre_x + xoff; t.cy = ky * v.centre_y;
    t.sx = 256.0 * kx; t.sy = 256.0 * ky;
    t.near_z = 0x10000;
    for (long i = 0; i < (long)w * h; i++) t.inv_z[i] = 0.0f;
    for (int i = 0; i < 256; i++) t.pal[i] = remaster_dac_rgb(&dac[3 * i]);
    const int hy = (int)t.cy;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) rgba[(size_t)y * w + x] = y < hy ? 0xFFC08040u : 0xFF305030u;
    paint_bands(ds, dac, w, h, kx, ky, xoff, rgba, t.index);

    long px = 0;
    const size_t n = frame->n;
    size_t *order = (size_t *)malloc((n ? n : 1) * sizeof *order);
    if (order) {
        for (size_t k = 0; k < n; k++) order[k] = k;
        /* Depth-correct drawing is the point of Remastered: one depth buffer across every model, a tie (coplanar ground, a
         * marking on it) going to the later draw in the game's own order. F117_RM_PAINTER=1 reproduces the original's
         * whole-object sort instead (far to near, depth only inside a model), for comparison. */
        const int painter = getenv("F117_RM_PAINTER") != NULL;
        uint32_t *gen = NULL;
        if (painter) {
            g_sort = frame->prim;
            qsort(order, n, sizeof *order, by_distance);
            gen = (uint32_t *)calloc((size_t)w * h, sizeof(uint32_t));
            t.gen = gen;
        }
        const char *only_e = getenv("F117_RM_ONLY"), *skip_e = getenv("F117_RM_SKIP");
        const int only = only_e ? atoi(only_e) : -1, skip = skip_e ? atoi(skip_e) : -1;   /* debugging: one model, or all but one */
        uint32_t group = 0, last_request = 0;
        for (size_t i = 0; i < n; i++) {
            const remaster_prim *p = &frame->prim[order[i]];
            if ((only >= 0 && p->model != only) || (skip >= 0 && p->model == skip)) continue;
            if (!i || p->request != last_request) { group++; last_request = p->request; }
            t.cur_gen = group;
            if (p->style == 0xFB) continue;                                   /* "clear without drawing": the original paints nothing */
            if (p->style == 0xFC) continue;                                   /* stipple: not modelled yet */
            t.op = p->style == 0xFD ? 1 : p->style == 0xFE ? 2 : 0;           /* OR / AND into the page (VGAME 1377:0A89) */
            t.opval = (uint8_t)p->colour_word;
            t.cur_index = p->pixel;
            uint32_t col = remaster_dac_rgb(&dac[3 * p->pixel]);
            if (p->style < 0xFB) {                                            /* a colour pair: the original dithers; the average is smooth */
                const uint32_t b = remaster_dac_rgb(&dac[3 * p->pixel2]);
                col = 0xFF000000u | ((((col & 255) + (b & 255)) / 2) & 255) | (((((col >> 8) & 255) + ((b >> 8) & 255)) / 2) << 8) |
                      (((((col >> 16) & 255) + ((b >> 16) & 255)) / 2) << 16);
            }
            if (p->kind == SCENE_POLYGON && p->n >= 3) px += remaster_polygon(&t, p->v, p->n, col);
            else if (p->kind == SCENE_LINES) for (int j = 0; j + 1 < p->n; j++) px += remaster_line(&t, p->v[j], p->v[j + 1], 2.0, col);
        }
        free(gen);
        t.gen = NULL;
    }
    free(order);
    free(t.inv_z);
    free(t.index);
    return px;
}
