/* test_crt.c - the CRT filter: only brightness changes, bright first row, a gap towards the next line. */
#include "crt.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static unsigned grey(uint32_t c) { return c & 255u; }

int main(void)
{
    enum { W = 8 };
    for (int rows = 2; rows <= 6; rows++) {
        const int h = 200 * rows;
        uint32_t *a = (uint32_t *)malloc((size_t)W * h * 4), *b = (uint32_t *)malloc((size_t)W * h * 4);
        CHECK(a && b);
        for (int i = 0; i < W * h; i++) a[i] = 0x80FFFFFFu;          /* white, half alpha */
        crt_scanlines(a, b, W, h);
        int ok = 1;
        for (int y = 0; y < h; y++) {
            const uint32_t c = b[y * W];
            const unsigned g = grey(c);
            ok &= (c >> 24) == 0x80 && ((c >> 16) & 255u) == g && ((c >> 8) & 255u) == g;   /* alpha, hue kept */
            ok &= g <= 255u;
            if (y % rows == 0) ok &= g >= 240u;                      /* the line's first row stays bright */
            if (y % rows == rows - 1) ok &= g < 200u && g >= 120u;   /* its last row is dimmer, not black */
            if (y % rows) ok &= g <= grey(b[(y - 1) * W]);           /* never brighter than the row above */
            ok &= b[y * W] == b[y * W + W - 1];                      /* same across the row */
        }
        CHECK(ok);
        crt_scanlines(a, a, W, h);                                    /* in place gives the same picture */
        for (int i = 0; i < W * h; i++) CHECK(a[i] == b[i]);
        free(a);
        free(b);
    }
    /* one row per line: nothing to draw, a straight copy */
    uint32_t src[W * 200], dst[W * 200];
    for (int i = 0; i < W * 200; i++) src[i] = 0xFF000000u | (uint32_t)i;
    crt_scanlines(src, dst, W, 200);
    for (int i = 0; i < W * 200; i++) CHECK(dst[i] == src[i]);
    puts("crt filter passes");
    return 0;
}
