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
    /* the phosphor mask: each column keeps its own colour and dims the other two to three quarters; alpha stays */
    {
        enum { MW = 9, MH = 4 };
        uint32_t m[MW * MH], o[MW * MH];
        for (int i = 0; i < MW * MH; i++) m[i] = 0x80C8C8C8u;
        crt_mask(m, o, MW, MH);
        for (int x = 0; x < MW; x++) {
            const uint32_t c = o[x];
            const unsigned r = (c >> 16) & 255u, g = (c >> 8) & 255u, b = c & 255u;
            CHECK((c >> 24) == 0x80);
            CHECK(x % 3 == 0 ? (r == 200 && g == 150 && b == 150) : x % 3 == 1 ? (g == 200 && r == 150 && b == 150) : (b == 200 && r == 150 && g == 150));
            CHECK(o[x] == o[x + MW * (MH - 1)]);
        }
    }
    /* the glow: a dark picture is unchanged, a bright spot brightens its neighbours (never beyond 255), and it works in place */
    {
        enum { GW = 640, GH = 400 };
        uint32_t *g = (uint32_t *)malloc((size_t)GW * GH * 4), *o = (uint32_t *)malloc((size_t)GW * GH * 4);
        CHECK(g && o);
        for (int i = 0; i < GW * GH; i++) g[i] = 0xFF202020u;
        crt_glow(g, o, GW, GH);
        CHECK(o[0] == 0xFF202020u && o[GW * GH / 2] == 0xFF202020u);
        for (int y = 190; y < 210; y++) for (int x = 310; x < 330; x++) g[y * GW + x] = 0xFFFFFFFFu;
        crt_glow(g, o, GW, GH);
        CHECK(o[200 * GW + 320] == 0xFFFFFFFFu);                      /* the spot stays white */
        CHECK((o[200 * GW + 333] & 255u) > 0x20u);                    /* its neighbourhood brightens */
        CHECK(o[200 * GW + 600] == 0xFF202020u && o[20 * GW + 20] == 0xFF202020u);   /* far away does not */
        uint32_t *copy = (uint32_t *)malloc((size_t)GW * GH * 4);
        CHECK(copy);
        for (int i = 0; i < GW * GH; i++) copy[i] = g[i];
        crt_glow(g, g, GW, GH);                                       /* in place equals into a copy */
        for (int i = 0; i < GW * GH; i++) CHECK(g[i] == o[i]);
        free(copy); free(g); free(o);
    }
    puts("crt filter passes");
    return 0;
}
