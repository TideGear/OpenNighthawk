/* exact.h - the integer definitions shared by the CPU compositor (remaster_raster.c) and the GPU compositor's setup (gpu_compose.c), so that both make the
 * same decisions from the same numbers (docs/gpu-compositor.md, "Step 2 design"). Used when F117_RASTER_EXACT=1.
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

typedef struct { long long q[6]; long long area; } ex_tri;     /* q: ax, ay, bx, by, cx, cy in 1/256 px */

static inline int ex_setup(ex_tri *t, double ax, double ay, double bx, double by, double cx, double cy)
{
    t->q[0] = llround(ax * 256.0); t->q[1] = llround(ay * 256.0);
    t->q[2] = llround(bx * 256.0); t->q[3] = llround(by * 256.0);
    t->q[4] = llround(cx * 256.0); t->q[5] = llround(cy * 256.0);
    t->area = (t->q[2] - t->q[0]) * (t->q[5] - t->q[1]) - (t->q[4] - t->q[0]) * (t->q[3] - t->q[1]);
    return t->area != 0;
}

static inline int ex_covers(const ex_tri *t, int x, int y)
{
    const long long px = (long long)x * 256 + 128, py = (long long)y * 256 + 128;
    const long long e0 = (t->q[2] - t->q[0]) * (py - t->q[1]) - (t->q[3] - t->q[1]) * (px - t->q[0]);
    const long long e1 = (t->q[4] - t->q[2]) * (py - t->q[3]) - (t->q[5] - t->q[3]) * (px - t->q[2]);
    const long long e2 = (t->q[0] - t->q[4]) * (py - t->q[5]) - (t->q[1] - t->q[5]) * (px - t->q[4]);
    return t->area > 0 ? (e0 >= 0 && e1 >= 0 && e2 >= 0) : (e0 <= 0 && e1 <= 0 && e2 <= 0);
}

#define EX_PLANES 4                 /* 0: inverse depth, 1 to 3: the colour channels */
#define EX_SCALE_IZ 52
#define EX_SCALE_RGB 32
#define EX_INV_2_52 2.220446049250313080847263336181640625e-16

typedef struct { long long ox, oy; uint64_t base[EX_PLANES], px[EX_PLANES], py[EX_PLANES]; } ex_planes;

static inline long long ex_floor_div256(long long v) { return v >= 0 ? v / 256 : -((-v + 255) / 256); }

static inline long long ex_round(double v)
{
    if (!(v > -9.0e18)) v = -9.0e18;
    if (!(v < 9.0e18)) v = 9.0e18;
    return llround(v);
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
        const double scale = ldexp(1.0, k == 0 ? EX_SCALE_IZ : EX_SCALE_RGB);
        const double gx = ((v[k][1] - v[k][0]) * (yc - ya) - (v[k][2] - v[k][0]) * (yb - ya)) / d;
        const double gy = ((v[k][2] - v[k][0]) * (xb - xa) - (v[k][1] - v[k][0]) * (xc - xa)) / d;
        const double b0 = v[k][0] + gx * ((double)p->ox + 0.5 - xa) + gy * ((double)p->oy + 0.5 - ya);
        p->base[k] = (uint64_t)ex_round(b0 * scale);
        p->px[k] = (uint64_t)ex_round(gx * scale);
        p->py[k] = (uint64_t)ex_round(gy * scale);
    }
}

static inline uint64_t ex_eval(const ex_planes *p, int k, int x, int y)
{
    return p->base[k] + p->px[k] * (uint64_t)(int64_t)(x - p->ox) + p->py[k] * (uint64_t)(int64_t)(y - p->oy);
}

#endif
