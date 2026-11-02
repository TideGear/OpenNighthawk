/* remaster_raster.c - the Remastered presentation's triangle fill (see remaster_raster.h). */
#include "remaster_raster.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_POLY 16

typedef struct { double x, y, z; } dv3;

void remaster_clear(remaster_target *t, uint32_t color)
{
    const long from = (long)remaster_row_lo(t) * t->w, to = (long)remaster_row_hi(t) * t->w;
    for (long i = from; i < to; i++) { t->color[i] = color; t->inv_z[i] = 0.0f; }
    if (t->index) for (long i = from; i < to; i++) t->index[i] = REMASTER_NO_INDEX;
    if (t->owner) for (long i = from; i < to; i++) t->owner[i] = 0;
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

/* The columns of row y (pixel centres at y + 0.5) that can lie inside triangle abc: where the row meets its three edges, one pixel
 * wider each side. A pixel outside it is certainly outside the triangle, so the per-pixel test (which stays) gives the same
 * result as testing every pixel of the bounding box, at a cost that follows the area instead of the box: a long thin diagonal
 * line no longer tests the whole rectangle around it. Returns 0 when the row misses the triangle. */
static int row_span(sv a, sv b, sv c, double py, int *xs, int *xe)
{
    const sv v[3] = { a, b, c };
    double lo = 1e300, hi = -1e300;
    for (int i = 0; i < 3; i++) {
        const sv p = v[i], q = v[(i + 1) % 3];
        if (!((p.y <= py && py <= q.y) || (q.y <= py && py <= p.y))) continue;
        if (p.y == q.y) { lo = fmin(lo, fmin(p.x, q.x)); hi = fmax(hi, fmax(p.x, q.x)); continue; }
        const double x = p.x + (py - p.y) * (q.x - p.x) / (q.y - p.y);
        lo = fmin(lo, x); hi = fmax(hi, x);
    }
    if (lo > hi) return 0;
    *xs = (int)floor(lo - 0.5) - 1;
    *xe = (int)ceil(hi - 0.5) + 1;
    return 1;
}

/* How much the polygons' inverse depth changes across one pixel at (x, y): per axis the smaller step to a covered neighbour, so a
 * surface's edge, where one neighbour is another surface, reads the slope of the surface itself. */
static float surface_slope(const remaster_target *t, int x, int y)
{
    const float *z = t->line_ref_z;
    const long i = (long)y * t->w + x;
    const float c = z[i];
    float g[2] = { 1e30f, 1e30f };
    for (int axis = 0; axis < 2; axis++)
        for (int s = -1; s <= 1; s += 2) {
            const int nx = x + (axis == 0 ? s : 0), ny = y + (axis == 1 ? s : 0);
            if (nx < 0 || ny < 0 || nx >= t->w || ny >= t->h) continue;
            const float n = z[(long)ny * t->w + nx];
            if (n <= 0.0f) continue;
            const float d = fabsf(n - c);
            if (d < g[axis]) g[axis] = d;
        }
    return (g[0] < 1e29f ? g[0] : 0.0f) + (g[1] < 1e29f ? g[1] : 0.0f);
}

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
    if (y0 < remaster_row_lo(t)) y0 = remaster_row_lo(t);
    if (y1 > remaster_row_hi(t) - 1) y1 = remaster_row_hi(t) - 1;
    long written = 0;
    for (int y = y0; y <= y1; y++) {
        int xs, xe;
        if (!row_span(a, b, c, y + 0.5, &xs, &xe)) continue;
        if (xs < x0) xs = x0;
        if (xe > x1) xe = x1;
        for (int x = xs; x <= xe; x++) {
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
            if (t->owner && (t->line_decal || !t->line_order)) {
                /* Lines, dots and polygons by what is already at the pixel (remaster_raster.h, owner): a ground object (bit 11:
                 * the runway, an apron) never hides another object's lines or polygons, since the original paints what stands on
                 * the ground over it; a line or dot is drawn over its own face always and over the rest of its own object unless
                 * clearly nearer (its fixed width spills onto the faces beside); otherwise the depth decides. */
                static float tol[2] = { -1.0f, 0.0f };     /* F117_DECAL_TOL=SAME,OTHER (diagnostic): the line margins */
                if (tol[0] < 0.0f) {
                    tol[0] = 0.99f; tol[1] = 0.97f;
                    const char *e = getenv("F117_DECAL_TOL");
                    if (e) sscanf(e, "%f,%f", &tol[0], &tol[1]);
                }
                const uint32_t ow = t->owner[(long)y * t->w + x];
                const int same = ow >> 12 == t->paint_owner >> 12, on_ground = ow && (ow & 0x800u) && !same;
                if (t->line_decal) {
                    if (ow != t->paint_owner && !on_ground && iz < *d * (same ? tol[0] : tol[1])) continue;
                } else {
                    const int me_ground = (t->paint_owner & 0x800u) != 0;
                    float k = 0.9999f;                          /* the same object: the depth, later winning a tie */
                    if (on_ground) k = me_ground ? 0.97f : 0.0f;  /* over the ground: always, or (ground on ground) unless clearly nearer */
                    else if (me_ground && ow && !same) k = 1.03f; /* the ground over what stands on it: only where clearly in front */
                    if (iz < *d * k) continue;
                }
            } else if (iz < *d * 0.9999f && !(t->line_slope > 0.0 && t->line_ref_z && iz >= *d - (float)t->line_slope * surface_slope(t, x, y))) continue;
            if (!t->line_order) *d = iz;
            if (t->owner && !t->line_order) t->owner[(long)y * t->w + x] = t->paint_owner;
            t->color[(long)y * t->w + x] = color;
            if (t->index) t->index[(long)y * t->w + x] = t->paint_index;
            written++;
        }
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
    if (y0 < remaster_row_lo(t)) y0 = remaster_row_lo(t);
    if (y1 > remaster_row_hi(t) - 1) y1 = remaster_row_hi(t) - 1;
    /* The weights l1 and l2 (l3 = 1 - l1 - l2) are linear in the pixel's position, so a row steps them by a constant instead of
     * dividing at every pixel; the depth and the three colour channels are linear in them. */
    const double a1 = (b.y - c.y) / den, b1 = (c.x - b.x) / den, a2 = (c.y - a.y) / den, b2 = (a.x - c.x) / den;
    const double diz1 = a.iz - c.iz, diz2 = b.iz - c.iz;
    double dc1[3], dc2[3];
    for (int k = 0; k < 3; k++) { dc1[k] = ca[k] - cc[k]; dc2[k] = cb[k] - cc[k]; }
    long written = 0;
    for (int y = y0; y <= y1; y++) {
        int xs, xe;
        if (!row_span(a, b, c, y + 0.5, &xs, &xe)) continue;
        if (xs < x0) xs = x0;
        if (xe > x1) xe = x1;
        const double py = y + 0.5;
        double l1 = a1 * (xs + 0.5 - c.x) + b1 * (py - c.y), l2 = a2 * (xs + 0.5 - c.x) + b2 * (py - c.y);
        for (int x = xs; x <= xe; x++, l1 += a1, l2 += a2) {
            const double l3 = 1.0 - l1 - l2;
            if (l1 < 0 || l2 < 0 || l3 < 0) continue;
            const float iz = (float)(c.iz + l1 * diz1 + l2 * diz2);
            float *d = &t->inv_z[(long)y * t->w + x];
            if (iz < *d * 0.9999f) continue;
            *d = iz;
            /* a 4 x 4 ordered noise in [-0.5, 0.5) levels, so the rounding to 8 bits does not make steps */
            static const unsigned char bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
            const double noise = (bayer[(y & 3) * 4 + (x & 3)] + 0.5) / 16.0 - 0.5;
            uint32_t out = 0xFF000000u;
            for (int k = 0; k < 3; k++) {
                double v = cc[k] + l1 * dc1[k] + l2 * dc2[k] + noise + 0.5;
                if (v < 0) v = 0;
                if (v > 255) v = 255;
                out |= (uint32_t)v << (16 - 8 * k);
            }
            t->color[(long)y * t->w + x] = out;
            if (t->index) t->index[(long)y * t->w + x] = t->paint_index;
            written++;
        }
    }
    return written;
}

static long fill_triangle_logic(remaster_target *t, sv a, sv b, sv c, int op, unsigned value)
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
    if (y0 < remaster_row_lo(t)) y0 = remaster_row_lo(t);
    if (y1 > remaster_row_hi(t) - 1) y1 = remaster_row_hi(t) - 1;
    long written = 0;
    for (int y = y0; y <= y1; y++) {
        int xs, xe;
        if (!row_span(a, b, c, y + 0.5, &xs, &xe)) continue;
        if (xs < x0) xs = x0;
        if (xe > x1) xe = x1;
        for (int x = xs; x <= xe; x++) {
            const double px = x + 0.5, py = y + 0.5;
            const double l1 = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / den;
            const double l2 = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / den;
            const double l3 = 1.0 - l1 - l2;
            if (l1 < 0 || l2 < 0 || l3 < 0) continue;
            const float iz = (float)(l1 * a.iz + l2 * b.iz + l3 * c.iz);
            const long i = (long)y * t->w + x;
            if (iz < t->inv_z[i] * 0.9999f) continue;          /* something clearly nearer covers it */
            if (op == REMASTER_STIPPLE) {
                const uint32_t o = t->color[i], p = t->palette ? t->palette[value & 255] : o;
                t->color[i] = 0xFF000000u | ((((o >> 16 & 255) + (p >> 16 & 255)) / 2) << 16) | ((((o >> 8 & 255) + (p >> 8 & 255)) / 2) << 8) | (((o & 255) + (p & 255)) / 2);
                if (t->index) t->index[i] = REMASTER_NO_INDEX;
                if (t->inv_z[i] <= 0.0f) t->inv_z[i] = 1e-15f;     /* drawn: a hair of depth, so a composite sees something here (the bands sit at none) */
                written++;
            } else if (t->index && t->palette && t->index[i] != REMASTER_NO_INDEX) {
                const unsigned ni = op == REMASTER_AND ? (t->index[i] & value) : (t->index[i] | value);
                t->index[i] = (uint16_t)(ni & 255);
                t->color[i] = t->palette[ni & 255];
                if (t->inv_z[i] <= 0.0f) t->inv_z[i] = 1e-15f;     /* as above */
                written++;
            }
        }
    }
    return written;
}

long remaster_polygon_logic(remaster_target *t, const remaster_v3 *v, int n, int op, unsigned value)
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
    for (int i = 1; i + 1 < m; i++) written += fill_triangle_logic(t, s[0], s[i], s[i + 1], op, value);
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
    /* A line lies on the surface it marks (a hangar's door markings, a runway's edge), at the same depth as the polygon under
     * it: a small bias toward the eye keeps it on top, as the original's draw order does, instead of z-fighting with it. */
    const double bias = t->line_bias > 0.0 ? t->line_bias : 1.5;     /* near the horizon one pixel of height is a big step in depth: the ground just below a line is "nearer" than the line */
    const sv sa = { t->cx + t->sx * ax / az, t->cy + t->sy * ay / az, bias / az };
    const sv sb = { t->cx + t->sx * bx / bz, t->cy + t->sy * by / bz, bias / bz };
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
