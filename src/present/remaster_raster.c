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
            /* Something clearly nearer is already there: skip. At the same depth (within a hundredth of a percent) the later
             * polygon wins, so a decal on the ground (the runway strips, the markings) draws over the ground it lies on, as
             * the original's draw order has it; an exact test would let rounding pick between them. */
            if (iz < *d * 0.9999f) continue;
            *d = iz;
            t->color[(long)y * t->w + x] = color;
            written++;
        }
    return written;
}

static long fill_triangle_shaded(remaster_target *t, sv a, sv b, sv c, const double ca[3], const double cb[3], const double cc[3])
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
            if (l1 < 0 || l2 < 0 || l3 < 0) continue;
            const float iz = (float)(l1 * a.iz + l2 * b.iz + l3 * c.iz);
            float *d = &t->inv_z[(long)y * t->w + x];
            if (iz < *d * 0.9999f) continue;
            *d = iz;
            /* a 4 x 4 ordered noise in [-0.5, 0.5) levels, so the rounding to 8 bits does not make steps */
            static const unsigned char bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
            const double noise = (bayer[(y & 3) * 4 + (x & 3)] + 0.5) / 16.0 - 0.5;
            uint32_t out = 0xFF000000u;
            for (int k = 0; k < 3; k++) {
                double v = l1 * ca[k] + l2 * cb[k] + l3 * cc[k] + noise + 0.5;
                if (v < 0) v = 0;
                if (v > 255) v = 255;
                out |= (uint32_t)v << (16 - 8 * k);
            }
            t->color[(long)y * t->w + x] = out;
            written++;
        }
    return written;
}

long remaster_polygon_shaded(remaster_target *t, const remaster_v3 *v, const uint32_t *colors, int n)
{
    if (n < 3 || n > MAX_POLY - 2) return 0;
    sv s[MAX_POLY];
    double rgb[MAX_POLY][3];
    for (int i = 0; i < n; i++) {
        if (v[i].z < t->near_z) return 0;
        s[i].x = t->cx + t->sx * (double)v[i].x / v[i].z;
        s[i].y = t->cy + t->sy * (double)v[i].y / v[i].z;
        s[i].iz = 1.0 / (double)v[i].z;
        rgb[i][0] = colors[i] >> 16 & 255; rgb[i][1] = colors[i] >> 8 & 255; rgb[i][2] = colors[i] & 255;
    }
    long written = 0;
    for (int i = 1; i + 1 < n; i++) written += fill_triangle_shaded(t, s[0], s[i], s[i + 1], rgb[0], rgb[i], rgb[i + 1]);
    return written;
}

long remaster_line(remaster_target *t, remaster_v3 a, remaster_v3 b, double width_px, uint32_t color)
{
    const double near_z = (double)t->near_z;
    double ax = a.x, ay = a.y, az = a.z, bx = b.x, by = b.y, bz = b.z;
    if (az < near_z && bz < near_z) return 0;
    if (az < near_z) {                                       /* clip the end behind the near plane */
        const double k = (near_z - az) / (bz - az);
        ax += k * (bx - ax); ay += k * (by - ay); az = near_z;
    } else if (bz < near_z) {
        const double k = (near_z - bz) / (az - bz);
        bx += k * (ax - bx); by += k * (ay - by); bz = near_z;
    }
    const sv sa = { t->cx + t->sx * ax / az, t->cy + t->sy * ay / az, 1.0 / az };
    const sv sb = { t->cx + t->sx * bx / bz, t->cy + t->sy * by / bz, 1.0 / bz };
    double dx = sb.x - sa.x, dy = sb.y - sa.y;
    const double len = sqrt(dx * dx + dy * dy);
    if (len < 1e-9) { dx = 1.0; dy = 0.0; } else { dx /= len; dy /= len; }
    const double h = width_px / 2.0;
    const double px = -dy * h, py = dx * h;                  /* half the width, across the line */
    const double ex = (len < 1e-9) ? dx * h : 0.0, ey = (len < 1e-9) ? dy * h : 0.0;   /* a point gets a square */
    const sv q0 = { sa.x + px - ex, sa.y + py - ey, sa.iz }, q1 = { sa.x - px - ex, sa.y - py - ey, sa.iz };
    const sv q2 = { sb.x - px + ex, sb.y - py + ey, sb.iz }, q3 = { sb.x + px + ex, sb.y + py + ey, sb.iz };
    return fill_triangle(t, q0, q1, q2, color) + fill_triangle(t, q0, q2, q3, color);
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
