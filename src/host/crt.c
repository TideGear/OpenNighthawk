/* crt.c - the CRT filter (see crt.h). */
#include "crt.h"

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
