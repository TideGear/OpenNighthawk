/* remaster_raster.c - the Remastered presentation's triangle fill (see remaster_raster.h). */
#include "remaster_raster.h"
#include "exact.h"
#include "square_pen.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_POLY 16

F117_TLS int (*remaster_trace_hook)(int kind, const remaster_target *t, const remaster_v3 *v, int n, const uint32_t *colors,
                                   uint32_t color, double width, int op, unsigned value);

typedef struct { double x, y, z; } dv3;

static double inverse_depth(const remaster_target *t, double z)
{
    return (t->inv_depth_scale > 0.0 ? t->inv_depth_scale : 1.0) / z;
}

static int decal_hidden(const remaster_target *t, long i, float iz)
{
    static float tol[2] = { -1.0f, 0.0f };
    if (tol[0] < 0.0f) {
        tol[0] = 0.99f; tol[1] = 0.97f;
        const char *e = getenv("F117_DECAL_TOL");
        if (e) sscanf(e, "%f,%f", &tol[0], &tol[1]);
    }
    const uint32_t ow = t->owner[i];
    const int same = ow >> 12 == t->paint_owner >> 12;
    const int on_ground = ow && (ow & 0x800u) && !same;
    if (same && t->line_point) return t->point_self_hidden;
    return !on_ground && iz < t->inv_z[i] * (same ? tol[0] : tol[1]);
}

/* Point decorations are billboards: test their centre against their own
 * surface, rather than clipping their footprint against a steep roof/fin.
 * Inverse depth is planar in screen space, so same-face neighbours recover
 * its value at the sub-pixel centre without increasing the depth bias. */
static int point_behind_own_surface(const remaster_target *t, double cx, double cy, float iz)
{
    if (!t->owner || cx < 0.0 || cy < 0.0 || cx >= t->w || cy >= t->h) return 0;
    const int x = (int)cx, y = (int)cy;
    const long i = (long)y * t->w + x;
    const uint32_t own = t->owner[i];
    if (!own || (own & 0x800u) || own >> 12 != t->paint_owner >> 12) return 0;
    double surface = t->inv_z[i];
    for (int axis = 0; axis < 2; ++axis) {
        double slope = 0.0; int samples = 0;
        for (int sign = -1; sign <= 1; sign += 2) {
            const int nx = x + (axis == 0 ? sign : 0), ny = y + (axis == 1 ? sign : 0);
            if (nx < 0 || ny < 0 || nx >= t->w || ny >= t->h) continue;
            const long ni = (long)ny * t->w + nx;
            if (t->owner[ni] == own) { slope += (t->inv_z[ni] - t->inv_z[i]) * sign; ++samples; }
        }
        if (samples) surface += slope / samples * (axis == 0 ? cx - x - 0.5 : cy - y - 0.5);
    }
    return iz < surface * 0.99;
}

/* A hidden line must not reappear as a fringe outside its own object's
 * raster silhouette. Only the stroke's uncovered pixels need this check. */
static int decal_stroke_hidden(const remaster_target *t, double px, double py, float iz)
{
    const double dx = t->line_screen[2] - t->line_screen[0], dy = t->line_screen[3] - t->line_screen[1];
    const double ll = dx * dx + dy * dy;
    double u = ll > 0.0 ? ((px - t->line_screen[0]) * dx + (py - t->line_screen[1]) * dy) / ll : 0.0;
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;
    const double cx = t->line_screen[0] + u * dx, cy = t->line_screen[1] + u * dy;
    if (cx >= 0.0 && cy >= 0.0 && cx < t->w && cy < t->h) {
        const long i = (long)(int)cy * t->w + (int)cx;
        if (decal_hidden(t, i, iz)) return 1;
        if (t->owner[i] >> 12 == t->paint_owner >> 12) return 0;
    }
    if (ll <= 0.0) return 0;
    for (int y = -t->line_radius; y <= t->line_radius; ++y)
        for (int x = -t->line_radius; x <= t->line_radius; ++x) {
            const double qx = cx + x, qy = cy + y;
            if (qx < 0.0 || qy < 0.0 || qx >= t->w || qy >= t->h) continue;
            const long i = (long)(int)qy * t->w + (int)qx;
            if (t->owner[i] && t->owner[i] >> 12 == t->paint_owner >> 12 && decal_hidden(t, i, iz)) return 1;
        }
    return 0;
}

void remaster_clear(remaster_target *t, uint32_t color)
{
    if (remaster_trace_hook && remaster_trace_hook(REMASTER_T_CLEAR, t, NULL, 0, NULL, color, 0.0, 0, 0)) return;
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

/* Exact coverage (F117R exact rasterising, docs/gpu-compositor.md "Step 2 design"): the triangle's vertices are snapped once to a grid of 1/256 pixel
 * and a pixel is covered when its centre is inside or on the edge of that integer triangle, decided by three int64 edge equations. The decision is a
 * function of the snapped integers alone, so the GPU compositor can make the same decision from the same numbers; it no longer depends on how a
 * compiler or a GPU rounds doubles and floats. Snapped coordinates stay within about 2e8 (8e5 px at 1/256), so every product fits 64 bits.
 * On by default since 6 November 2026 (owner's approval of the changed pixels); F117_RASTER_EXACT=0 restores the earlier double-precision fills. */
typedef ex_tri itri;

static int exact_cov(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("F117_RASTER_EXACT"); on = !(e && *e == '0'); }
    return on;
}

static int itri_setup(itri *r, sv a, sv b, sv c) { return ex_setup(r, a.x, a.y, b.x, b.y, c.x, c.y); }
static int itri_covers(const itri *r, int x, int y) { return ex_covers(r, x, y); }

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
    if (exact_cov() && ex_outside(a.x, a.y, b.x, b.y, c.x, c.y)) {      /* exact mode: clip to the guard band first (exact.h) */
        const ex_vtx in[3] = { { a.x, a.y, { a.iz, 0, 0, 0 } }, { b.x, b.y, { b.iz, 0, 0, 0 } }, { c.x, c.y, { c.iz, 0, 0, 0 } } };
        ex_vtx o[16]; const int m = ex_guard_clip(in, 3, o); long w = 0;
        for (int k = 1; k + 1 < m; k++) {
            const sv p = { o[0].x, o[0].y, o[0].a[0] }, q = { o[k].x, o[k].y, o[k].a[0] }, r = { o[k + 1].x, o[k + 1].y, o[k + 1].a[0] };
            w += fill_triangle(t, p, q, r, color);
        }
        return w;
    }
    const double den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (fabs(den) < 1e-12) return 0;
    itri it; const int exact = exact_cov(); if (exact && !itri_setup(&it, a, b, c)) return 0;
    ex_planes pl;
    if (exact) {
        const double pv[EX_PLANES][3] = { { a.iz, b.iz, c.iz }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
        ex_planes_make(&pl, &it, pv);
    }
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
        if (exact) { xs = x0; xe = x1; }       /* the integer test decides every pixel of the box: row_span's doubles can miss a tip the snapping moved onto a pixel centre */
        else if (!row_span(a, b, c, y + 0.5, &xs, &xe)) continue;
        if (xs < x0) xs = x0;
        if (xe > x1) xe = x1;
        for (int x = xs; x <= xe; x++) {
            const double px = x + 0.5, py = y + 0.5;
            const double l1 = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / den;
            const double l2 = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / den;
            const double l3 = 1.0 - l1 - l2;
            if (exact ? !itri_covers(&it, x, y) : (l1 < 0 || l2 < 0 || l3 < 0)) continue;        /* the pixel's centre is outside */
            const float iz = exact ? (float)((double)(int64_t)ex_eval(&pl, 0, x, y) * EX_INV_2_52) : (float)(l1 * a.iz + l2 * b.iz + l3 * c.iz);
            float *d = &t->inv_z[(long)y * t->w + x];
            /* Something clearly nearer is already there: skip. At the same depth (within a hundredth of a percent) the later
             * polygon wins, so a decal on the ground (the runway strips, the markings) draws over the ground it lies on, as
             * the original's draw order has it; an exact test would let rounding pick between them. */
            if (t->owner && (t->line_decal || !t->line_order)) {
                /* Lines, dots and polygons by what is already at the pixel (remaster_raster.h, owner): a ground object (bit 11:
                 * the runway, an apron) never hides another object's lines or polygons, since the original paints what stands on
                 * the ground over it; a line or dot is drawn over its own face and the rest of its own object unless
                 * clearly nearer (its fixed width spills onto the faces beside); otherwise the depth decides. */
                const uint32_t ow = t->owner[(long)y * t->w + x];
                const int same = ow >> 12 == t->paint_owner >> 12, on_ground = ow && (ow & 0x800u) && !same;
                if (t->line_decal) {
                    if (decal_hidden(t, (long)y * t->w + x, iz)) continue;
                    if (!same && decal_stroke_hidden(t, px, py, iz)) continue;
                } else {
                    const int me_ground = (t->paint_owner & 0x800u) != 0;
                    float k = 0.9999f;                          /* the same object: the depth, later winning a tie */
                    if (on_ground) k = me_ground ? 0.97f : 0.0f;  /* over the ground: always, or (ground on ground) unless clearly nearer */
                    else if (me_ground && ow && !same) k = 1.03f; /* the ground over what stands on it: only where clearly in front */
                    if (iz < *d * k) continue;
                }
            } else if (iz < *d * 0.9999f && !(t->line_slope > 0.0 && t->line_ref_z && iz >= *d - (float)t->line_slope * surface_slope(t, x, y))) continue;
            if (t->line_order && t->stroke_z && t->stroke_owner) {
                const long i = (long)y * t->w + x;
                const float old_z = t->stroke_z[i];
                const uint32_t object = t->paint_owner >> 12, old = old_z > 0.0f ? t->stroke_owner[i] : 0;
                const int old_point = (old & 0x100000u) != 0;
                /* Compare a point at its centre, not at an arbitrary covered pixel.
                 * A thick edge's depth varies across its footprint near a grazing
                 * surface; sampling there made an attached light alternate with it. */
                float point_z = t->line_point ? iz : old_z;
                float edge_z = t->line_point ? old_z : iz;
                if (old_z > 0.0f && old_point != t->line_point && t->stroke_aux_x && t->stroke_aux_y) {
                    if (t->line_point)
                        edge_z += (float)(t->stroke_aux_x[i] * (t->line_screen[0] - px) + t->stroke_aux_y[i] * (t->line_screen[1] - py));
                    else
                        edge_z += (float)(t->line_depth_dx * (t->stroke_aux_x[i] - px) + t->line_depth_dy * (t->stroke_aux_y[i] - py));
                }
                const int point_tie = old_z > 0.0f && object && object == (old & 0xFFFFFu) &&
                    old_point != t->line_point && ((t->paint_owner & 0x800u) || (point_z >= edge_z * 0.99f && edge_z >= point_z * 0.99f));
                const int point_edge_pair = object && (old & 0xFFFFFu) && old_point != t->line_point;
                if (point_edge_pair) {
                    const float new_z = t->line_point ? point_z : edge_z;
                    const float previous_z = t->line_point ? edge_z : point_z;
                    if (point_tie ? !t->line_point : new_z < previous_z * 0.9999f) continue;
                }
                t->stroke_z[i] = iz;
                if (t->stroke_aux_x && t->stroke_aux_y) {
                    t->stroke_aux_x[i] = (float)(t->line_point ? t->line_screen[0] : t->line_depth_dx);
                    t->stroke_aux_y[i] = (float)(t->line_point ? t->line_screen[1] : t->line_depth_dy);
                }
                t->stroke_owner[i] = object | (t->line_point ? 0x100000u : 0);
            }
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
    if (exact_cov() && ex_outside(a.x, a.y, b.x, b.y, c.x, c.y)) {
        const ex_vtx in[3] = { { a.x, a.y, { a.iz, ca[0], ca[1], ca[2] } }, { b.x, b.y, { b.iz, cb[0], cb[1], cb[2] } }, { c.x, c.y, { c.iz, cc[0], cc[1], cc[2] } } };
        ex_vtx o[16]; const int m = ex_guard_clip(in, 3, o); long w = 0;
        for (int k = 1; k + 1 < m; k++) {
            const sv p = { o[0].x, o[0].y, o[0].a[0] }, q = { o[k].x, o[k].y, o[k].a[0] }, r = { o[k + 1].x, o[k + 1].y, o[k + 1].a[0] };
            const double pc[3] = { o[0].a[1], o[0].a[2], o[0].a[3] }, qc[3] = { o[k].a[1], o[k].a[2], o[k].a[3] }, rc[3] = { o[k + 1].a[1], o[k + 1].a[2], o[k + 1].a[3] };
            w += fill_triangle_shaded(t, p, q, r, pc, qc, rc);
        }
        return w;
    }
    const double den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (fabs(den) < 1e-12) return 0;
    itri it; const int exact = exact_cov(); if (exact && !itri_setup(&it, a, b, c)) return 0;
    ex_planes pl;
    if (exact) {
        const double pv[EX_PLANES][3] = { { a.iz, b.iz, c.iz }, { ca[0], cb[0], cc[0] }, { ca[1], cb[1], cc[1] }, { ca[2], cb[2], cc[2] } };
        ex_planes_make(&pl, &it, pv);
    }
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
        if (exact) { xs = x0; xe = x1; }       /* the integer test decides every pixel of the box: row_span's doubles can miss a tip the snapping moved onto a pixel centre */
        else if (!row_span(a, b, c, y + 0.5, &xs, &xe)) continue;
        if (xs < x0) xs = x0;
        if (xe > x1) xe = x1;
        const double py = y + 0.5;
        double l1 = a1 * (xs + 0.5 - c.x) + b1 * (py - c.y), l2 = a2 * (xs + 0.5 - c.x) + b2 * (py - c.y);
        for (int x = xs; x <= xe; x++, l1 += a1, l2 += a2) {
            const double l3 = 1.0 - l1 - l2;
            if (exact ? !itri_covers(&it, x, y) : (l1 < 0 || l2 < 0 || l3 < 0)) continue;
            const float iz = exact ? (float)((double)(int64_t)ex_eval(&pl, 0, x, y) * EX_INV_2_52) : (float)(c.iz + l1 * diz1 + l2 * diz2);
            float *d = &t->inv_z[(long)y * t->w + x];
            if (iz < *d * 0.9999f) continue;
            *d = iz;
            /* a 4 x 4 ordered noise in [-0.5, 0.5) levels, so the rounding to 8 bits does not make steps */
            static const unsigned char bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
            const double noise = (bayer[(y & 3) * 4 + (x & 3)] + 0.5) / 16.0 - 0.5;
            uint32_t out = 0xFF000000u;
            for (int k = 0; k < 3; k++) {
                if (exact) {
                    /* the same rounding in integers: the plane value in 2^-32 units, the ordered noise (2b - 15) / 32 and one half added, floored, limited to 0..255 */
                    const long long vs = (long long)ex_eval(&pl, k + 1, x, y) + ((long long)(2 * (int)bayer[(y & 3) * 4 + (x & 3)] - 15) << 27) + 2147483648LL;
                    long long vi = vs < 0 ? 0 : (vs >> 32);
                    if (vi > 255) vi = 255;
                    out |= (uint32_t)vi << (16 - 8 * k);
                    continue;
                }
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
    if (exact_cov() && ex_outside(a.x, a.y, b.x, b.y, c.x, c.y)) {
        const ex_vtx in[3] = { { a.x, a.y, { a.iz, 0, 0, 0 } }, { b.x, b.y, { b.iz, 0, 0, 0 } }, { c.x, c.y, { c.iz, 0, 0, 0 } } };
        ex_vtx o[16]; const int m = ex_guard_clip(in, 3, o); long w = 0;
        for (int k = 1; k + 1 < m; k++) {
            const sv p = { o[0].x, o[0].y, o[0].a[0] }, q = { o[k].x, o[k].y, o[k].a[0] }, r = { o[k + 1].x, o[k + 1].y, o[k + 1].a[0] };
            w += fill_triangle_logic(t, p, q, r, op, value);
        }
        return w;
    }
    const double den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (fabs(den) < 1e-12) return 0;
    itri it; const int exact = exact_cov(); if (exact && !itri_setup(&it, a, b, c)) return 0;
    ex_planes pl;
    if (exact) {
        const double pv[EX_PLANES][3] = { { a.iz, b.iz, c.iz }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
        ex_planes_make(&pl, &it, pv);
    }
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
        if (exact) { xs = x0; xe = x1; }       /* the integer test decides every pixel of the box: row_span's doubles can miss a tip the snapping moved onto a pixel centre */
        else if (!row_span(a, b, c, y + 0.5, &xs, &xe)) continue;
        if (xs < x0) xs = x0;
        if (xe > x1) xe = x1;
        for (int x = xs; x <= xe; x++) {
            const double px = x + 0.5, py = y + 0.5;
            const double l1 = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / den;
            const double l2 = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / den;
            const double l3 = 1.0 - l1 - l2;
            if (exact ? !itri_covers(&it, x, y) : (l1 < 0 || l2 < 0 || l3 < 0)) continue;
            const float iz = exact ? (float)((double)(int64_t)ex_eval(&pl, 0, x, y) * EX_INV_2_52) : (float)(l1 * a.iz + l2 * b.iz + l3 * c.iz);
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
    if (remaster_trace_hook && remaster_trace_hook(REMASTER_T_LOGIC, t, v, n, NULL, 0, 0.0, op, value)) return 0;
    if (n < 3 || n > MAX_POLY - 2) return 0;
    dv3 c[MAX_POLY];
    const int m = clip_near(v, n, (double)t->near_z, c);
    if (m < 3) return 0;
    sv s[MAX_POLY];
    for (int i = 0; i < m; i++) {
        s[i].x = t->cx + t->sx * c[i].x / c[i].z;
        s[i].y = t->cy + t->sy * c[i].y / c[i].z;
        s[i].iz = inverse_depth(t, c[i].z);
    }
    long written = 0;
    for (int i = 1; i + 1 < m; i++) written += fill_triangle_logic(t, s[0], s[i], s[i + 1], op, value);
    return written;
}

long remaster_polygon_shaded(remaster_target *t, const remaster_v3 *v, const uint32_t *colors, int n)
{
    if (remaster_trace_hook && remaster_trace_hook(REMASTER_T_SHADED, t, v, n, colors, 0, 0.0, 0, 0)) return 0;
    if (n < 3 || n > MAX_POLY - 2) return 0;
    sv s[MAX_POLY];
    double rgb[MAX_POLY][3];
    for (int i = 0; i < n; i++) {
        if (v[i].z < t->near_z) return 0;
        s[i].x = t->cx + t->sx * (double)v[i].x / v[i].z;
        s[i].y = t->cy + t->sy * (double)v[i].y / v[i].z;
        s[i].iz = inverse_depth(t, (double)v[i].z);
        rgb[i][0] = colors[i] >> 16 & 255; rgb[i][1] = colors[i] >> 8 & 255; rgb[i][2] = colors[i] & 255;
    }
    long written = 0;
    for (int i = 1; i + 1 < n; i++) written += fill_triangle_shaded(t, s[0], s[i], s[i + 1], rgb[0], rgb[i], rgb[i + 1]);
    return written;
}

long remaster_line(remaster_target *t, remaster_v3 a, remaster_v3 b, double width_px, uint32_t color)
{
    if (remaster_trace_hook) { const remaster_v3 ab[2] = { a, b }; if (remaster_trace_hook(REMASTER_T_LINE, t, ab, 2, NULL, color, width_px, 0, 0)) return 0; }
    t->line_point = a.x == b.x && a.y == b.y && a.z == b.z;
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
    const sv sa = { t->cx + t->sx * ax / az, t->cy + t->sy * ay / az, bias * inverse_depth(t, az) };
    const sv sb = { t->cx + t->sx * bx / bz, t->cy + t->sy * by / bz, bias * inverse_depth(t, bz) };
    /* The square caps and cross-stroke width extend less than one full
     * width beyond the endpoints on either axis. Reject an off-band
     * stroke before setting up its two triangles. */
    if ((sa.y + width_px < remaster_row_lo(t) && sb.y + width_px < remaster_row_lo(t)) ||
        (sa.y - width_px > remaster_row_hi(t) && sb.y - width_px > remaster_row_hi(t)) ||
        (sa.x + width_px < 0.0 && sb.x + width_px < 0.0) ||
        (sa.x - width_px > t->w && sb.x - width_px > t->w)) return 0;
    t->point_self_hidden = t->line_point && t->line_decal && point_behind_own_surface(t, sa.x, sa.y, (float)sa.iz);
    if (t->point_self_hidden) return 0;
    t->line_screen[0] = sa.x; t->line_screen[1] = sa.y;
    t->line_screen[2] = sb.x; t->line_screen[3] = sb.y;
    t->line_radius = (int)ceil(width_px / 2.0);
    if (t->line_radius > 4) t->line_radius = 4;
    double dx = sb.x - sa.x, dy = sb.y - sa.y;
    const double len = sqrt(dx * dx + dy * dy);
    const double dz = len > 1e-9 ? (sb.iz - sa.iz) / (len * len) : 0.0;
    t->line_depth_dx = dz * dx; t->line_depth_dy = dz * dy;
    if (len < 1e-9) { dx = 1.0; dy = 0.0; } else { dx /= len; dy /= len; }
    const double h = width_px / 2.0;
    const double px = -dy * h, py = dx * h;                  /* half the width, across the line */
    /* The original includes both endpoint pixels. Square caps preserve that
     * footprint at any angle, including a short flag meeting its pole.
     * Continue the inverse-depth plane through the caps so the segment's
     * original vertices retain their depths and attached points still tie. */
    const double ex = dx * h, ey = dy * h;
    const double cap_z = t->line_depth_dx * ex + t->line_depth_dy * ey;
    const sv q0 = { sa.x + px - ex, sa.y + py - ey, sa.iz - cap_z }, q1 = { sa.x - px - ex, sa.y - py - ey, sa.iz - cap_z };
    const sv q2 = { sb.x - px + ex, sb.y - py + ey, sb.iz + cap_z }, q3 = { sb.x + px + ex, sb.y + py + ey, sb.iz + cap_z };
    if (square_pen_enabled()) {
        square_pen_point hull[16]; sv v[16];
        const int count = square_pen_hull(sa.x, sa.y, sb.x, sb.y, h, hull);
        for (int k=0;k<count;k++) {
            v[k].x=hull[k].x; v[k].y=hull[k].y;
            v[k].iz=sa.iz+t->line_depth_dx*(v[k].x-sa.x)+t->line_depth_dy*(v[k].y-sa.y);
        }
        long pixels=0;
        for (int k=1;k<count-1;k++) pixels+=fill_triangle(t,v[0],v[k],v[k+1],color);
        return pixels;
    }
    return fill_triangle(t, q0, q1, q2, color) + fill_triangle(t, q0, q2, q3, color);
}

long remaster_polygon(remaster_target *t, const remaster_v3 *v, int n, uint32_t color)
{
    if (remaster_trace_hook && remaster_trace_hook(REMASTER_T_POLYGON, t, v, n, NULL, color, 0.0, 0, 0)) return 0;
    if (n < 3 || n > MAX_POLY - 2) return 0;
    dv3 c[MAX_POLY];
    const int m = clip_near(v, n, (double)t->near_z, c);
    if (m < 3) return 0;
    sv s[MAX_POLY];
    for (int i = 0; i < m; i++) {
        s[i].x = t->cx + t->sx * c[i].x / c[i].z;
        s[i].y = t->cy + t->sy * c[i].y / c[i].z;
        s[i].iz = inverse_depth(t, c[i].z);
    }
    long written = 0;
    for (int i = 1; i + 1 < m; i++) written += fill_triangle(t, s[0], s[i], s[i + 1], color);
    return written;
}
