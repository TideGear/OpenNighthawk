/* Independent half-space clipping oracle for the proximity sweep. */
#include "weapon_sweep.h"
#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static int clipped(weapon_point a, weapon_point b, int radius)
{
    double lo = 0, hi = 1;
    if (radius <= 0) return 0;
    for (int swap = 0; swap < 2; swap++)
        for (int x = -1; x <= 1; x += 2)
            for (int y = -1; y <= 1; y += 2)
                for (int z = -1; z <= 1; z += 2) {
                    const int cx = x * (swap ? 32 : 64), cy = y * (swap ? 64 : 32);
                    const int64_t start = (int64_t)cx * a.x + (int64_t)cy * a.y + z * 2LL * a.z;
                    const int64_t delta = (int64_t)cx * (b.x - a.x) + (int64_t)cy * (b.y - a.y)
                        + z * 2LL * (b.z - a.z);
                    if (!delta) { if (start >= 64LL * radius) return 0; }
                    else {
                        const double t = (double)(64LL * radius - start) / (double)delta;
                        if (delta > 0 && t < hi) hi = t;
                        if (delta < 0 && t > lo) lo = t;
                    }
                }
    return lo < hi;
}

static uint32_t seed = 0x117A;
static int random_coord(int extent)
{
    seed = seed * 1664525u + 1013904223u;
    return (int)(seed % (uint32_t)(2 * extent + 1)) - extent;
}

int main(void)
{
    CHECK(weapon_delta16(2, 65530) == 8);
    CHECK(weapon_delta16(65530, 2) == -8);
    CHECK(weapon_delta16(32768, 0) == -32768);
    CHECK(weapon_sweep_hits((weapon_point){0, 100, 0}, (weapon_point){0, -100, 0}, 1));
    CHECK(!weapon_sweep_hits((weapon_point){24, 100, 0}, (weapon_point){24, -100, 0}, 24));
    CHECK(weapon_sweep_hits((weapon_point){23, 100, 16}, (weapon_point){23, -100, 16}, 24));
    CHECK(!weapon_sweep_hits((weapon_point){0, 0, 768}, (weapon_point){0, 0, 768}, 24));
    CHECK(weapon_sweep_hits((weapon_point){0, 0, 767}, (weapon_point){0, 0, 767}, 24));
    CHECK(!weapon_sweep_hits((weapon_point){16, 16, 0}, (weapon_point){16, 16, 0}, 24));
    CHECK(weapon_sweep_hits((weapon_point){15, 16, 0}, (weapon_point){15, 16, 0}, 24));
    CHECK(!weapon_sweep_hits((weapon_point){22, 70, 100}, (weapon_point){22, -70, 100}, 24));
    CHECK(weapon_sweep_hits((weapon_point){22, 70, 100}, (weapon_point){22, -70, 100}, 27));
    for (int i = 0; i < 20000; i++) {
        const int extent = i % 7 ? 64 : 49151;
        const weapon_point a = { 2 * random_coord(extent), 2 * random_coord(extent), 2 * random_coord(32768) };
        const weapon_point b = { 2 * random_coord(extent), 2 * random_coord(extent), 2 * random_coord(32768) };
        const weapon_point mid = { (a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2 };
        const int radius = 1 + (int)(seed % 128);
        const int hit = weapon_sweep_hits(a, b, radius);
        CHECK(hit == clipped(a, b, radius));
        CHECK(hit == weapon_sweep_hits(b, a, radius));
        CHECK(hit == (weapon_sweep_hits(a, mid, radius) || weapon_sweep_hits(mid, b, radius)));
    }
    printf("weapon sweep: 20000 paths, independent clipping, reversal and subdivision; %d failures\n", failures);
    return failures != 0;
}
