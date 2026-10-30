/* remaster_raster.c - the Remastered presentation's triangle fill (see remaster_raster.h). */
#include "remaster_raster.h"

#include <math.h>
#include <string.h>

#define MAX_POLY 16

typedef struct { double x, y, z; } dv3;

void remaster_clear(remaster_target *t, uint32_t color)
{
    for (long i = 0; i < (long)t->w * t->h; i++) { t->color[i] = color; t->inv_z[i] = 0.0f; }
}

/* clip the polygon against z >= near (Sutherland-Hodgman on the one plane) */
static int clip_near(const remaster_v3 *in, int n, double near_z, dv3 *out)
{
    int m = 0;
    for (int i = 0; i < n && m < MAX_POLY - 1; i++) {
        const remaster_v3 a = in[i], b = in[(i + 1) % n];
        const int ina = a.z >= near_z, inb = b.z >= near_z;
        if (ina) out[m++] = (dv3){ a.x, a.y, a.z };
        if (ina != inb) {
            const double k = (near_z - a.z) / ((double)b.z - a.z);
            out[m++] = (dv3){ a.x + k * ((double)b.x - a.x), a.y + k * ((double)b.y - a.y), near_z };
        }
    }
    return m;
}

typedef struct { double x, y, iz; } sv;      /* a screen vertex: pixel position and 1/z */

static long fill_triangle(remaster_target *t, sv a, sv b, sv c, uint32_t color)
{
    const double den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (fabs(den) < 1e-12) return 0;
    const double minx = fmin(a.x, fmin(b.x, c.x)), maxx = fmax(a.x, fmax(b.x, c.x));
    const double miny = fmin(a.y, fmin(b.y, c.y)), maxy = fmax(a.y, fmax(b.y, c.y));
    int x0 = (int)floor(minx - 0.5), x1 = (int)ceil(maxx - 0.5), y0 = (int)floor(miny - 0.5), y1 = (int)ceil(maxy - 0.5);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > t->w - 1) x1 = t->w - 1;
    if (y1 > t->h - 1) y1 = t->h - 1;
    long written = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            const double px = x + 0.5, py = y + 0.5;
            const double l1 = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / den;
            const double l2 = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / den;
            const double l3 = 1.0 - l1 - l2;
            if (l1 < 0 || l2 < 0 || l3 < 0) continue;        /* the pixel's centre is outside */
            const float iz = (float)(l1 * a.iz + l2 * b.iz + l3 * c.iz);
            float *d = &t->inv_z[(long)y * t->w + x];
            if (iz <= *d) continue;                          /* something nearer is already there */
            *d = iz;
            t->color[(long)y * t->w + x] = color;
            written++;
        }
    return written;
}

long remaster_polygon(remaster_target *t, const remaster_v3 *v, int n, uint32_t color)
{
    if (n < 3 || n > MAX_POLY - 2) return 0;
    dv3 c[MAX_POLY];
    const int m = clip_near(v, n, (double)t->near_z, c);
    if (m < 3) return 0;
    sv s[MAX_POLY];
    for (int i = 0; i < m; i++) {
        s[i].x = t->cx + t->sx * c[i].x / c[i].z;
        s[i].y = t->cy + t->sy * c[i].y / c[i].z;
        s[i].iz = 1.0 / c[i].z;
    }
    long written = 0;
    for (int i = 1; i + 1 < m; i++) written += fill_triangle(t, s[0], s[i], s[i + 1], color);
    return written;
}
