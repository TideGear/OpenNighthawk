/* crt.c - the CRT filter (see crt.h). */
#include "crt.h"

#include <stdlib.h>
#include <string.h>

/* the row weight in 1/256: 256 at a line's first row, falling to 256 - 0.5*256 at its last */
static int row_weight(int p, int rows)
{
    const double t = ((double)p + 0.5) / rows;            /* 0..1 down the line */
    return 256 - (int)(0.5 * 256.0 * t * t + 0.5);
}

void crt_scanlines(const uint32_t *src, uint32_t *dst, int w, int h)
{
    const int rows = h / 200;
    if (rows < 2 || h != rows * 200) {
        if (dst != src) memcpy(dst, src, (size_t)w * (size_t)h * 4);
        return;
    }
    int weight[64];
    if (rows > 64) {
        if (dst != src) memcpy(dst, src, (size_t)w * (size_t)h * 4);
        return;
    }
    for (int p = 0; p < rows; p++) weight[p] = row_weight(p, rows);
    for (int y = 0; y < h; y++) {
        const int k = weight[y % rows];
        const uint32_t *s = src + (size_t)y * (size_t)w;
        uint32_t *d = dst + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) {
            const uint32_t c = s[x];
            d[x] = (c & 0xFF000000u) | ((((c >> 16) & 255u) * (unsigned)k >> 8) << 16) |
                   ((((c >> 8) & 255u) * (unsigned)k >> 8) << 8) | (((c & 255u) * (unsigned)k) >> 8);
        }
    }
}

void crt_mask(const uint32_t *src, uint32_t *dst, int w, int h)
{
    for (int y = 0; y < h; y++) {
        const uint32_t *s = src + (size_t)y * (size_t)w;
        uint32_t *d = dst + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) {
            const uint32_t c = s[x];
            unsigned r = (c >> 16) & 255u, g = (c >> 8) & 255u, b = c & 255u;
            switch (x % 3) {
            case 0: g = g * 3 / 4; b = b * 3 / 4; break;      /* the red column */
            case 1: r = r * 3 / 4; b = b * 3 / 4; break;      /* the green column */
            default: r = r * 3 / 4; g = g * 3 / 4; break;     /* the blue column */
            }
            d[x] = (c & 0xFF000000u) | (r << 16) | (g << 8) | b;
        }
    }
}

static uint8_t *g_glow;
static size_t g_glow_cap;

void crt_glow(const uint32_t *src, uint32_t *dst, int w, int h)
{
    const size_t need = (size_t)w * (size_t)h * 3;
    if (need > g_glow_cap) {
        free(g_glow);
        g_glow = (uint8_t *)malloc(need);
        g_glow_cap = g_glow ? need : 0;
    }
    if (!g_glow || w < 8 || h < 8) { if (dst != src) memcpy(dst, src, (size_t)w * (size_t)h * 4); return; }
    if (dst != src) memcpy(dst, src, (size_t)w * (size_t)h * 4);
    int r = w / 320;                                        /* about a game pixel either side */
    if (r < 1) r = 1;
    if (r > 8) r = 8;
    const int span = 2 * r + 1;
    /* horizontal pass: the bright part of each channel, box-blurred along the row */
    for (int y = 0; y < h; y++) {
        const uint32_t *s = src + (size_t)y * (size_t)w;
        uint8_t *o = g_glow + (size_t)y * (size_t)w * 3;
        for (int ch = 0; ch < 3; ch++) {
            const int shift = 16 - 8 * ch;
            unsigned sum = 0;
            for (int x = -r; x <= r; x++) {
                const int xx = x < 0 ? 0 : x >= w ? w - 1 : x;
                const unsigned v = (s[xx] >> shift) & 255u;
                sum += v > 160u ? v - 160u : 0u;
            }
            for (int x = 0; x < w; x++) {
                o[x * 3 + ch] = (uint8_t)(sum / (unsigned)span);
                const int add = x + r + 1 >= w ? w - 1 : x + r + 1, sub = x - r < 0 ? 0 : x - r;
                const unsigned va = (s[add] >> shift) & 255u, vs = (s[sub] >> shift) & 255u;
                sum += (va > 160u ? va - 160u : 0u);
                sum -= (vs > 160u ? vs - 160u : 0u);
            }
        }
    }
    /* vertical pass over the blurred rows, added back at 3/8 */
    for (int x = 0; x < w; x++) {
        for (int ch = 0; ch < 3; ch++) {
            const int shift = 16 - 8 * ch;
            unsigned sum = 0;
            for (int y = -r; y <= r; y++) sum += g_glow[((size_t)(y < 0 ? 0 : y >= h ? h - 1 : y) * (size_t)w + (size_t)x) * 3 + (size_t)ch];
            for (int y = 0; y < h; y++) {
                const uint32_t c = src[(size_t)y * (size_t)w + (size_t)x];
                const unsigned base = (c >> shift) & 255u, add = sum / (unsigned)span * 3u / 8u;
                const unsigned v = base + add > 255u ? 255u : base + add;
                uint32_t *d = dst + (size_t)y * (size_t)w + (size_t)x;
                *d = (*d & ~(255u << shift)) | (v << shift);
                const int add_y = y + r + 1 >= h ? h - 1 : y + r + 1, sub_y = y - r < 0 ? 0 : y - r;
                sum += g_glow[((size_t)add_y * (size_t)w + (size_t)x) * 3 + (size_t)ch];
                sum -= g_glow[((size_t)sub_y * (size_t)w + (size_t)x) * 3 + (size_t)ch];
            }
        }
    }
}
