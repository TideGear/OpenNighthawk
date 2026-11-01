/* test_colourblind.c - the colour-blind filter: off changes nothing, greys stay grey, the colours the vision confuses move apart,
 * alpha is kept, the table path equals the matrix, and the names parse. */
#include "colourblind.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define R(p) ((int)((p) >> 16 & 255u))
#define G(p) ((int)((p) >> 8 & 255u))
#define B(p) ((int)((p) & 255u))

static int one(uint32_t p, int mode, uint32_t *out)
{
    colourblind_apply(&p, out, 1, 1, mode);
    return 1;
}

int main(void)
{
    CHECK(colourblind_parse("off") == CB_OFF && colourblind_parse("none") == CB_OFF);
    CHECK(colourblind_parse("protanopia") == CB_PROTANOPIA && colourblind_parse("protan") == CB_PROTANOPIA);
    CHECK(colourblind_parse("deuteranopia") == CB_DEUTERANOPIA && colourblind_parse("deutan") == CB_DEUTERANOPIA);
    CHECK(colourblind_parse("tritanopia") == CB_TRITANOPIA && colourblind_parse("tritan") == CB_TRITANOPIA);
    CHECK(colourblind_parse("blue") == -1 && colourblind_parse(NULL) == -1 && colourblind_parse("") == -1);

    /* off is the identity, in place or not */
    {
        double m[9];
        colourblind_matrix(CB_OFF, m);
        CHECK(m[0] == 1 && m[4] == 1 && m[8] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 0);
        uint32_t a[4] = { 0xFF102030u, 0x80FF0000u, 0x0000FF00u, 0xFFFFFFFFu }, b[4];
        colourblind_apply(a, b, 2, 2, CB_OFF);
        for (int i = 0; i < 4; i++) CHECK(a[i] == b[i]);
    }

    for (int mode = CB_PROTANOPIA; mode <= CB_TRITANOPIA; mode++) {
        uint32_t o;
        /* black and white stay put and every grey stays within one level of grey (the three channels move together) */
        one(0xFF000000u, mode, &o);
        CHECK(o == 0xFF000000u);
        one(0xFFFFFFFFu, mode, &o);
        CHECK(R(o) >= 253 && G(o) >= 253 && B(o) >= 253);
        for (int v = 16; v < 256; v += 48) {
            one(0xFF000000u | (uint32_t)v << 16 | (uint32_t)v << 8 | (uint32_t)v, mode, &o);
            CHECK(abs(R(o) - v) <= 3 && abs(G(o) - v) <= 3 && abs(B(o) - v) <= 3);
        }
        /* alpha is kept */
        one(0x40C08040u, mode, &o);
        CHECK((o >> 24) == 0x40);
        /* the matrix path and the table path agree on a sweep */
        double m[9];
        colourblind_matrix(mode, m);
        for (unsigned v = 0; v < 0x1000000u; v += 0x0F2A3Bu) {
            const uint32_t p = 0xFF000000u | v;
            one(p, mode, &o);
            const double r = m[0] * R(p) + m[1] * G(p) + m[2] * B(p), g = m[3] * R(p) + m[4] * G(p) + m[5] * B(p), b = m[6] * R(p) + m[7] * G(p) + m[8] * B(p);
            CHECK(fabs(R(o) - fmin(255, fmax(0, r))) <= 0.51 && fabs(G(o) - fmin(255, fmax(0, g))) <= 0.51 && fabs(B(o) - fmin(255, fmax(0, b))) <= 0.51);
        }
        /* in place gives the same as to a copy */
        uint32_t a[3] = { 0xFFC83020u, 0xFF20C830u, 0xFF3020C8u }, b2[3] = { 0xFFC83020u, 0xFF20C830u, 0xFF3020C8u }, c[3];
        colourblind_apply(a, c, 3, 1, mode);
        colourblind_apply(b2, b2, 3, 1, mode);
        for (int i = 0; i < 3; i++) CHECK(b2[i] == c[i]);
    }

    /* the point of it: a red and a green that the vision cannot tell apart end up farther apart, in the channels it can see */
    {
        const uint32_t red = 0xFFD02020u, green = 0xFF20D020u;
        uint32_t r, g;
        for (int mode = CB_PROTANOPIA; mode <= CB_DEUTERANOPIA; mode++) {
            one(red, mode, &r); one(green, mode, &g);
            /* in the filtered picture the blue channel (which these players see) now differs between them by more than before */
            CHECK(abs(B(r) - B(g)) > abs(B(red) - B(green)) + 20);
        }
        /* tritanopia: a blue and a yellow, which it cannot tell apart, end up on opposite sides of the red-green axis, which it
         * still sees (before, neither had any red-green difference to speak of) */
        const uint32_t blue = 0xFF2020D0u, yellow = 0xFFD0D020u;
        one(blue, CB_TRITANOPIA, &r); one(yellow, CB_TRITANOPIA, &g);
        CHECK(abs((R(r) - G(r)) - (R(g) - G(g))) > abs((R(blue) - G(blue)) - (R(yellow) - G(yellow))) + 100);
    }
    puts("colourblind passes");
    return 0;
}
