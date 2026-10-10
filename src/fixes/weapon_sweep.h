/* Continuous version of VGAME's octagonal horizontal distance plus |z|/32.
 * Endpoints are relative weapon/target coordinates in the original units.
 * Sweeping separates a physical hit band from the distance moved per frame.
 */
#ifndef F117R_WEAPON_SWEEP_H
#define F117R_WEAPON_SWEEP_H

#include <stdint.h>

typedef struct { int32_t x, y, z; } weapon_point;

static inline int32_t weapon_delta16(uint16_t a, uint16_t b)
{
    const uint16_t d = (uint16_t)(a - b);
    return d >= 32768 ? (int32_t)d - 65536 : d;
}

static inline int64_t weapon_abs64(int64_t v) { return v < 0 ? -v : v; }

static inline int weapon_sweep_at(weapon_point a, weapon_point v, int radius,
                                  int64_t n, int64_t d)
{
    if (d < 0) { d = -d; n = -n; }
    if (!d || n < 0 || n > d) return 0;
    const int64_t x = weapon_abs64((int64_t)a.x * d + (int64_t)v.x * n);
    const int64_t y = weapon_abs64((int64_t)a.y * d + (int64_t)v.y * n);
    const int64_t z = weapon_abs64((int64_t)a.z * d + (int64_t)v.z * n);
    const int64_t big = x > y ? x : y, small = x > y ? y : x;
    return 64 * big + 32 * small + 2 * z < (int64_t)64 * radius * d;
}

static inline int weapon_sweep_hits(weapon_point a, weapon_point b, int radius)
{
    if (radius <= 0) return 0;
    const weapon_point v = { b.x - a.x, b.y - a.y, b.z - a.z };
    /* The norm is linear between x=0, y=0, z=0 and |x|=|y|.
     * Its minimum is at an endpoint or one of those exact rational roots.
     * With wrapped 16-bit positions and displacements these products fit
     * int64_t; no floating point, square root or sampling tolerance is used.
     */
    return weapon_sweep_at(a, v, radius, 0, 1)
        || weapon_sweep_at(a, v, radius, 1, 1)
        || weapon_sweep_at(a, v, radius, -(int64_t)a.x, v.x)
        || weapon_sweep_at(a, v, radius, -(int64_t)a.y, v.y)
        || weapon_sweep_at(a, v, radius, -(int64_t)a.z, v.z)
        || weapon_sweep_at(a, v, radius, (int64_t)a.y - a.x, (int64_t)v.x - v.y)
        || weapon_sweep_at(a, v, radius, -(int64_t)a.x - a.y, (int64_t)v.x + v.y);
}

#endif
