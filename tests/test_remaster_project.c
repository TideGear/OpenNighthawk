/* test_remaster_project.c - the original's projection: ranges, exact pixels, the sub-pixel form agrees with them.
 * F117_VERTEX_LOG may name an observer log of V records (f117run --observe, F117R_OBSERVE_KINDS=V): every projected
 * coordinate in it must come out exactly as the game made it. */
#include "remaster_project.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void)
{
    /* The same authored ground point must have the same world-relative
     * position at every terrain detail. In the reported Halfar view the
     * original ring-4 eye is 128 world units high; the real eye is only 24. */
    {
        uint32_t view[3] = { 984882, 800052, 24 };
        double offset[3];
        CHECK(remaster_ring_camera_offset(view, 4, offset));
        CHECK(offset[0] == 0.21875 && offset[1] == 1.625 && offset[2] == -0.1875);
        const unsigned heights[] = { 0, 1, 2, 23, 24, 31, 32, 63, 64, 127, 128, 129, 1024 };
        for (unsigned h = 0; h < sizeof heights / sizeof heights[0]; ++h) {
            view[2] = heights[h];
            for (unsigned ring = 1; ring <= 4; ++ring) {
                const unsigned scale = 1u << (2 * (ring - 1));
                const double old_x = (view[0] + scale / 2) / scale;
                const double old_y = (view[1] + scale / 2) / scale;
                double old_height = (view[2] + scale / 2) / scale;
                if (old_height < 2) old_height = 2;
                CHECK(remaster_ring_camera_offset(view, ring, offset));
                CHECK((985600.0 / scale - old_x + offset[0]) * scale == 718.0);
                CHECK((-old_height + offset[1]) * scale == -(double)(view[2] < 2 ? 2 : view[2]));
                CHECK((old_y - 800768.0 / scale + offset[2]) * scale == -716.0);
            }
        }
        CHECK(!remaster_ring_camera_offset(view, 0, offset));
        CHECK(offset[0] == 0 && offset[1] == 0 && offset[2] == 0);
        CHECK(!remaster_ring_camera_offset(view, 5, offset));
        view[0] = UINT32_MAX;
        CHECK(!remaster_ring_camera_offset(view, 4, offset));
    }
    /* ranges: by the high word of z */
    CHECK(remaster_range(0x01000000) == REMASTER_NEAR);
    CHECK(remaster_range(0x00FF0000) == REMASTER_MID);
    CHECK(remaster_range(0x00010000) == REMASTER_MID);
    CHECK(remaster_range(0x0000FFFF) == REMASTER_BEHIND);
    CHECK(remaster_range(-5) == REMASTER_BEHIND);
    int p;
    /* a vertex at 1/10 of z to the side lands 25.6 pixels out: 256 * 0.1 */
    CHECK(remaster_pixel_exact(0x01000000 / 10, 0x01000000, &p) && p == 25);
    CHECK(remaster_pixel_exact(-0x01000000 / 10, 0x01000000, &p) && p == -25);   /* towards zero */
    CHECK(!remaster_pixel_exact(100, 0x0000FFFF, &p));                           /* behind the eye */
    double d;
    CHECK(remaster_project(0x01000000 / 10, 0x01000000, &d) && fabs(d - 25.6) < 0.01);
    CHECK(!remaster_project(1, -1, &d));
    /* the exact and sub-pixel forms agree to within a pixel across both ranges */
    srand(3);
    for (int i = 0; i < 20000; i++) {
        const int32_t z = (int32_t)(rand() % 0x10000000) + 0x10000;
        const int32_t v = (int32_t)((rand() & 0x3FFFFFF) - 0x1FFFFFF);
        double f;
        if (!remaster_project(v, z, &f)) continue;
        if (fabs(f) > 30000) continue;
        if (!remaster_pixel_exact(v, z, &p)) continue;
        CHECK(fabs((double)p - f) <= 1.5);
    }
    /* a real flight's vertex records, when given */
    const char *log = getenv("F117_VERTEX_LOG");
    if (log) {
        FILE *f = fopen(log, "r");
        CHECK(f);
        char line[256];
        long total = 0, exact = 0;
        while (fgets(line, sizeof line, f)) {
            unsigned long long clock;
            int x, y, zz, px, py, range;
            if (sscanf(line, "V %llu %d %d %d %d %d %d", &clock, &x, &y, &zz, &px, &py, &range) != 7) continue;
            if (range != REMASTER_NEAR && range != REMASTER_MID) continue;
            int qx, qy;
            CHECK(remaster_range(zz) == range);
            CHECK(remaster_pixel_exact(x, zz, &qx) && remaster_pixel_exact(y, zz, &qy));
            total += 2;
            exact += (qx == px) + (qy == py);
        }
        fclose(f);
        printf("vertex log: %ld of %ld projected coordinates exact\n", exact, total);
        CHECK(total > 1000 && exact == total);
    }
    puts("remaster projection passes");
    return 0;
}
