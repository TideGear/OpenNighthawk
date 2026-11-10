/* exact.h - the integer definitions shared by the CPU compositor (remaster_raster.c) and the GPU compositor's setup (gpu_compose.c), so that both make the
 * same decisions from the same numbers (docs/gpu-compositor.md, "Step 2 design"). Used by default; F117_RASTER_EXACT=0 turns it off.
 *
 * ex_tri: a triangle's three vertices snapped to 1/256 pixel and twice its signed area, all integers. A pixel is covered when its centre is inside or on the
 * edge of that integer triangle (three int64 edge values). The values stay within about 2e8, so every product fits 64 bits.
 *
 * ex_planes: the quantities that vary linearly over the triangle (inverse depth, three colour channels) as integer planes: a value at one origin pixel and a
 * step per pixel in x and in y, in fixed point (inverse depth scaled by 2^52, colour by 2^32). The CPU makes them once from doubles and rounds them to
 * integers; both compositors then evaluate base + step_x * dx + step_y * dy in unsigned 64-bit arithmetic, which wraps, so the result is exact whenever the
 * true value fits, whatever the size of the intermediate products. */
#ifndef F117R_EXACT_H
#define F117R_EXACT_H

#include <math.h>
#include <stdint.h>
#include <string.h>
#if defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#endif

/* Multiplication in unsigned 64-bit (wraps, defined) so no signed overflow is ever undefined; after the guard clip below nothing wraps anyway. */
#define EX_MUL(a, b) ((long long)((unsigned long long)(a) * (unsigned long long)(b)))
#define EX_SUB(a, b) ((long long)((unsigned long long)(a) - (unsigned long long)(b)))

typedef struct { long long q[6]; long long area; } ex_tri;     /* q: ax, ay, bx, by, cx, cy in 1/256 px */

static inline int ex_setup(ex_tri *t, double ax, double ay, double bx, double by, double cx, double cy)
{
    t->q[0] = llround(ax * 256.0); t->q[1] = llround(ay * 256.0);
    t->q[2] = llround(bx * 256.0); t->q[3] = llround(by * 256.0);
    t->q[4] = llround(cx * 256.0); t->q[5] = llround(cy * 256.0);
    t->area = EX_SUB(EX_MUL(t->q[2] - t->q[0], t->q[5] - t->q[1]), EX_MUL(t->q[4] - t->q[0], t->q[3] - t->q[1]));
    return t->area != 0;
}

static inline int ex_covers(const ex_tri *t, int x, int y)
{
    const long long px = (long long)x * 256 + 128, py = (long long)y * 256 + 128;
    const long long e0 = EX_SUB(EX_MUL(t->q[2] - t->q[0], py - t->q[1]), EX_MUL(t->q[3] - t->q[1], px - t->q[0]));
    const long long e1 = EX_SUB(EX_MUL(t->q[4] - t->q[2], py - t->q[3]), EX_MUL(t->q[5] - t->q[3], px - t->q[2]));
    const long long e2 = EX_SUB(EX_MUL(t->q[0] - t->q[4], py - t->q[5]), EX_MUL(t->q[1] - t->q[5], px - t->q[4]));
    return t->area > 0 ? (e0 >= 0 && e1 >= 0 && e2 >= 0) : (e0 <= 0 && e1 <= 0 && e2 <= 0);
}

#define EX_PLANES_ATTR 4
#define EX_PLANES 4                 /* 0: inverse depth, 1 to 3: the colour channels */
#define EX_SCALE_IZ 52
#define EX_SCALE_RGB 32
#define EX_INV_2_52 2.220446049250313080847263336181640625e-16


/* The guard band: every screen vertex is clipped to |x|, |y| <= EX_GUARD pixels before it is snapped, on both sides, by this one function. A vertex near the
 * camera's near plane can project to 10^9 pixels, where the edge products (about 10^21) leave 64 bits. Inside the band the snapped coordinates are at most
 * 2.7e8, so every product is under 3e17 and nothing wraps. Attributes (inverse depth, three colour channels) are interpolated linearly in screen space,
 * which is exact for the planes they are evaluated from. Clipped coordinates are clamped onto the band so a clipped triangle never needs clipping again. */
#define EX_GUARD 1048576.0

typedef struct { double x, y, a[4]; } ex_vtx;

static inline int ex_outside(double ax, double ay, double bx, double by, double cx, double cy)
{
    return fabs(ax) > EX_GUARD || fabs(ay) > EX_GUARD || fabs(bx) > EX_GUARD || fabs(by) > EX_GUARD || fabs(cx) > EX_GUARD || fabs(cy) > EX_GUARD;
}

/* Sutherland-Hodgman against the four sides of the band. in: n vertices; out: room for n + 4. Returns the vertex count (0 when nothing is inside). */
static inline int ex_guard_clip(const ex_vtx *in, int n, ex_vtx *out)
{
    ex_vtx buf[2][24];
    int cnt = n, cur = 0;
    for (int i = 0; i < n; i++) buf[0][i] = in[i];
    for (int side = 0; side < 4; side++) {
        const int axis = side >> 1;                       /* 0: x, 1: y */
        const double bound = (side & 1) ? EX_GUARD : -EX_GUARD, sign = (side & 1) ? 1.0 : -1.0;     /* inside: sign * (v - bound) <= 0 */
        int m = 0;
        const ex_vtx *src = buf[cur];
        ex_vtx *dst = buf[cur ^ 1];
        for (int i = 0; i < cnt; i++) {
            const ex_vtx p = src[i], q = src[(i + 1) % cnt];
            const double pv = axis ? p.y : p.x, qv = axis ? q.y : q.x;
            const int pin = sign * (pv - bound) <= 0.0, qin = sign * (qv - bound) <= 0.0;
            if (pin && m < 22) dst[m++] = p;
            if (pin != qin && m < 22) {
                const double k = (bound - pv) / (qv - pv);
                ex_vtx r;
                r.x = p.x + k * (q.x - p.x); r.y = p.y + k * (q.y - p.y);
                for (int j = 0; j < EX_PLANES_ATTR; j++) r.a[j] = p.a[j] + k * (q.a[j] - p.a[j]);
                if (axis) r.y = bound; else r.x = bound;
                dst[m++] = r;
            }
        }
        cnt = m; cur ^= 1;
        if (cnt == 0) return 0;
    }
    for (int i = 0; i < cnt; i++) {
        out[i] = buf[cur][i];
        if (out[i].x > EX_GUARD) out[i].x = EX_GUARD;
        if (out[i].x < -EX_GUARD) out[i].x = -EX_GUARD;
        if (out[i].y > EX_GUARD) out[i].y = EX_GUARD;
        if (out[i].y < -EX_GUARD) out[i].y = -EX_GUARD;
    }
    return cnt;
}

typedef struct { long long ox, oy; uint64_t base[EX_PLANES], px[EX_PLANES], py[EX_PLANES]; } ex_planes;

static inline long long ex_floor_div256(long long v) { return v >= 0 ? v / 256 : -((-v + 255) / 256); }

/* llround without the library call: the processor's convert (nearest even in the default rounding mode), then the exact ties (the fraction is exactly 1/2, which the
 * convert sends to the even neighbour) corrected to llround's rule, away from zero. Identical to llround for every double: equal on 4e8 random, tie and edge inputs
 * (private/step6/llround_bench2.c) and checked in the app against llround for every snapped vertex (F117_BIN_CHECK=1); 3.4 times faster (0.96 ns against 3.25 ns).
 * |x| below 2^52 only; anything else, and NaN, goes to llround as before. */
static inline long long ex_llround(double x)
{
#if defined(_M_X64) || defined(__x86_64__)
    if (fabs(x) < 4503599627370496.0) {
        const long long r = _mm_cvtsd_si64(_mm_set_sd(x));
        const double d = x - (double)r;                 /* exact */
        if (d == 0.5) return x > 0.0 ? r + 1 : r;
        if (d == -0.5) return x < 0.0 ? r - 1 : r;
        return r;
    }
#endif
    return llround(x);
}

/* 2^k for the exponents used here, as the double ldexp(1.0, k) returns (a power of two is exactly its biased exponent in the top bits) */
static inline double ex_pow2(int k)
{
    const uint64_t b = (uint64_t)(k + 1023) << 52;
    double d; memcpy(&d, &b, 8);
    return d;
}

static inline long long ex_round(double v)
{
    if (!(v > -9.0e18)) v = -9.0e18;
    if (!(v < 9.0e18)) v = 9.0e18;
    return ex_llround(v);
}

/* v[k][0..2]: plane k's value at vertices a, b and c */
static inline void ex_planes_make(ex_planes *p, const ex_tri *t, const double v[EX_PLANES][3])
{
    const double xa = (double)t->q[0] / 256.0, ya = (double)t->q[1] / 256.0, xb = (double)t->q[2] / 256.0, yb = (double)t->q[3] / 256.0;
    const double xc = (double)t->q[4] / 256.0, yc = (double)t->q[5] / 256.0;
    const double d = (xb - xa) * (yc - ya) - (xc - xa) * (yb - ya);
    long long mx = t->q[0], my = t->q[1];
    if (t->q[2] < mx) mx = t->q[2];
    if (t->q[4] < mx) mx = t->q[4];
    if (t->q[3] < my) my = t->q[3];
    if (t->q[5] < my) my = t->q[5];
    p->ox = ex_floor_div256(mx); p->oy = ex_floor_div256(my);
    for (int k = 0; k < EX_PLANES; k++) {
        const double scale = ex_pow2(k == 0 ? EX_SCALE_IZ : EX_SCALE_RGB);
        const double gx = ((v[k][1] - v[k][0]) * (yc - ya) - (v[k][2] - v[k][0]) * (yb - ya)) / d;
        const double gy = ((v[k][2] - v[k][0]) * (xb - xa) - (v[k][1] - v[k][0]) * (xc - xa)) / d;
        const double b0 = v[k][0] + gx * ((double)p->ox + 0.5 - xa) + gy * ((double)p->oy + 0.5 - ya);
        p->base[k] = (uint64_t)ex_round(b0 * scale);
        p->px[k] = (uint64_t)ex_round(gx * scale);
        p->py[k] = (uint64_t)ex_round(gy * scale);
    }
}

/* ex_planes_make for a triangle whose colour planes are all zero (the GPU setup's polygons and strokes use plane 0 only): the same arithmetic in the same order for
 * plane 0, so base[0], px[0] and py[0] are bit-identical; the three colour planes of zero inputs are exactly zero (0 / d = 0, round(0) = 0), so they are set to 0
 * instead of being computed. About a quarter of the cost: 2 divisions, 1 ldexp and 3 llround calls instead of 8, 4 and 12. */
static inline void ex_plane0_make(ex_planes *p, const ex_tri *t, double a, double b, double c)
{
    const double xa = (double)t->q[0] / 256.0, ya = (double)t->q[1] / 256.0, xb = (double)t->q[2] / 256.0, yb = (double)t->q[3] / 256.0;
    const double xc = (double)t->q[4] / 256.0, yc = (double)t->q[5] / 256.0;
    const double d = (xb - xa) * (yc - ya) - (xc - xa) * (yb - ya);
    long long mx = t->q[0], my = t->q[1];
    if (t->q[2] < mx) mx = t->q[2];
    if (t->q[4] < mx) mx = t->q[4];
    if (t->q[3] < my) my = t->q[3];
    if (t->q[5] < my) my = t->q[5];
    p->ox = ex_floor_div256(mx); p->oy = ex_floor_div256(my);
    const double scale = ex_pow2(EX_SCALE_IZ);
    const double gx = ((b - a) * (yc - ya) - (c - a) * (yb - ya)) / d;
    const double gy = ((c - a) * (xb - xa) - (b - a) * (xc - xa)) / d;
    const double b0 = a + gx * ((double)p->ox + 0.5 - xa) + gy * ((double)p->oy + 0.5 - ya);
    p->base[0] = (uint64_t)ex_round(b0 * scale);
    p->px[0] = (uint64_t)ex_round(gx * scale);
    p->py[0] = (uint64_t)ex_round(gy * scale);
    for (int k = 1; k < EX_PLANES; k++) p->base[k] = p->px[k] = p->py[k] = 0;
}

static inline uint64_t ex_eval(const ex_planes *p, int k, int x, int y)
{
    return p->base[k] + p->px[k] * (uint64_t)(int64_t)(x - p->ox) + p->py[k] * (uint64_t)(int64_t)(y - p->oy);
}

#endif
