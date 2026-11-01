/* colourblind.c - the colour-blind filter (see colourblind.h). */
#include "colourblind.h"

#include <string.h>

int colourblind_parse(const char *name)
{
    if (!name) return -1;
    if (!strcmp(name, "off") || !strcmp(name, "none")) return CB_OFF;
    if (!strcmp(name, "protanopia") || !strcmp(name, "protan")) return CB_PROTANOPIA;
    if (!strcmp(name, "deuteranopia") || !strcmp(name, "deutan")) return CB_DEUTERANOPIA;
    if (!strcmp(name, "tritanopia") || !strcmp(name, "tritan")) return CB_TRITANOPIA;
    return -1;
}

static void mul(const double a[9], const double b[9], double out[9])
{
    double t[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) t[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
    memcpy(out, t, sizeof t);
}

void colourblind_matrix(int mode, double m[9])
{
    static const double identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    /* RGB to LMS cone response, its inverse, and each red-green kind of vision as the LMS it keeps. */
    static const double to_lms[9] = { 17.8824, 43.5161, 4.11935, 3.45565, 27.1554, 3.86714, 0.0299566, 0.184309, 1.46709 };
    static const double from_lms[9] = { 0.0809444479, -0.130504409, 0.116721066, -0.0102485335, 0.0540193266, -0.113614708,
                                        -0.000365296938, -0.00412161469, 0.693511405 };
    static const double sim[2][9] = {
        { 0, 2.02344, -2.52581, 0, 1, 0, 0, 0, 1 },                        /* protanopia: no L cones */
        { 1, 0, 0, 0.494207, 0, 1.24827, 0, 0, 1 },                        /* deuteranopia: no M cones */
    };
    /* What was lost (original minus simulated) is added to the channels the vision still has. */
    static const double shift[2][9] = {
        { 0, 0, 0, 0.7, 1, 0, 0.7, 0, 1 },                                 /* protan and deutan: red-green loss into green and blue */
        { 0, 0, 0, 0.7, 1, 0, 0.7, 0, 1 },
    };
    if (mode < CB_PROTANOPIA || mode > CB_TRITANOPIA) { memcpy(m, identity, sizeof identity); return; }
    if (mode == CB_TRITANOPIA) {
        /* The published formula has nothing to work with here (its simulated tritan loss depends only on red minus green), so this
         * one is a plain rule: the blue-versus-yellow signal, by = b - (r + g) / 2, is moved onto the red-green axis, which a
         * tritanope still sees (blue gains green and loses red, yellow the other way). Greys have by = 0 and do not change. */
        static const double k = 0.35;
        const double t[9] = { 1 + k / 2, k / 2, -k, -k / 2, 1 - k / 2, k, 0, 0, 1 };
        memcpy(m, t, sizeof t);
        return;
    }
    double a[9], s[9], err[9], add[9];
    mul(sim[mode - 1], to_lms, a);
    mul(from_lms, a, s);                                                  /* the simulated colour as a matrix on RGB */
    for (int i = 0; i < 9; i++) err[i] = identity[i] - s[i];             /* what the vision loses */
    mul(shift[mode - 1], err, add);
    for (int i = 0; i < 9; i++) m[i] = identity[i] + add[i];
}

static uint32_t clamp8(double v)
{
    return v <= 0.0 ? 0u : v >= 255.0 ? 255u : (uint32_t)(v + 0.5);
}

void colourblind_apply(const uint32_t *src, uint32_t *dst, int w, int h, int mode)
{
    const long n = (long)w * h;
    if (mode < CB_PROTANOPIA || mode > CB_TRITANOPIA) { if (dst != src) memcpy(dst, src, (size_t)n * 4); return; }
    double m[9];
    colourblind_matrix(mode, m);
    /* The filter is linear: a table per channel and source value makes it three adds a pixel. */
    static double lut[3][3][256];
    static int lut_mode = -1;
    if (lut_mode != mode) {
        for (int c = 0; c < 3; c++)
            for (int ch = 0; ch < 3; ch++)
                for (int v = 0; v < 256; v++) lut[c][ch][v] = m[c * 3 + ch] * v;
        lut_mode = mode;
    }
    for (long i = 0; i < n; i++) {
        const uint32_t p = src[i];
        const unsigned r = p >> 16 & 255u, g = p >> 8 & 255u, b = p & 255u;
        const uint32_t nr = clamp8(lut[0][0][r] + lut[0][1][g] + lut[0][2][b]);
        const uint32_t ng = clamp8(lut[1][0][r] + lut[1][1][g] + lut[1][2][b]);
        const uint32_t nb = clamp8(lut[2][0][r] + lut[2][1][g] + lut[2][2][b]);
        dst[i] = (p & 0xFF000000u) | nr << 16 | ng << 8 | nb;
    }
}
