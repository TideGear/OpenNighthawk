/* hires.c - see hires.h. The functions follow tools/hires_subpixel.py's. */
#include "hires.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *const STAT_NAMES[HS_COUNT] = {
    "polygons", "polygons left nearest", "polygons refilled from vertices", "footprint pixels",
    "footprint fine pixels painted", "overshoot 0", "overshoot 0.5", "overshoot 1", "overshoot 1.5", "overshoot 2",
    "overshoot 3", "skipped: near-clipped", "skipped: fewer than three edges", "skipped: edge without vertices",
    "skipped: vertex behind the eye", "skipped: vertex without a divisor",
    "skipped: footprint beyond 3 px of its geometry", "skipped: fill style with no rule" };

const char *hires_stat_name(int k) { return k >= 0 && k < HS_COUNT ? STAT_NAMES[k] : ""; }

/* the overshoot, in pixels of the grid being drawn, the original's inclusive spans are allowed */
static const double GROWS[6] = { 0, 0.5, 1, 1.5, 2, 3 };

static int s16(int32_t v) { return (int16_t)(uint16_t)v; }

void hires_init(hires *h, int n)
{
    memset(h, 0, sizeof *h);
    h->n = n;
    h->suppressed = -1;
    h->fill_colour = -1;
    h->origin[0] = 159;
    h->origin[1] = 52;
}

/* ---- the remembered vertex records and edge slots (dicts in the Python) ---- */

static unsigned slot_hash(uint16_t key) { return (key * 40503u) & (HIRES_MAP - 1); }

static hires_vertex *vertex_get(hires *h, uint16_t key, int make)
{
    for (unsigned k = slot_hash(key), i = 0; i < HIRES_MAP; i++, k = (k + 1) & (HIRES_MAP - 1)) {
        if (h->vert[k].used && h->vert[k].key == key) return &h->vert[k].v;
        if (!h->vert[k].used) {
            if (!make) return NULL;
            h->vert[k].used = 1;
            h->vert[k].key = key;
            return &h->vert[k].v;
        }
    }
    return NULL;
}

static int slot_find(hires *h, uint16_t key, int make)
{
    for (unsigned k = slot_hash(key), i = 0; i < HIRES_MAP; i++, k = (k + 1) & (HIRES_MAP - 1)) {
        if (h->slot[k].used && h->slot[k].key == key) return (int)k;
        if (!h->slot[k].used) {
            if (!make) return -1;
            h->slot[k].used = 1;
            h->slot[k].key = key;
            return (int)k;
        }
    }
    return -1;
}

/* ---- geometry ---- */

/* The vertex's position in coarse pixels: the pixel the original made plus the
 * share the projection's divide threw away. 0 when the divisor is not positive. */
static int vertex_fraction(const hires_vertex *v, double *x, double *y)
{
    const uint32_t zu = (uint32_t)v->z;
    int64_t den, nx, ny;
    if (v->range == 0) { den = zu >> 16; nx = (int64_t)v->x >> 8; ny = (int64_t)v->y >> 8; }
    else { den = ((zu >> 8) & 0xFFFF) >> 1; nx = (int64_t)v->x >> 1; ny = (int64_t)v->y >> 1; }
    if (den <= 0) return 0;
    const int64_t mx = ((nx % den) + den) % den, my = ((ny % den) + den) % den;
    *x = v->px + (double)mx / (double)den;
    *y = v->py + (double)my / (double)den;
    return 1;
}

typedef struct { double ax, ay, bx, by, k; } xedge;

typedef struct { xedge e[HIRES_EDGES]; int n; } extents;

/* A convex polygon's edges, each put in a fixed order (by y, then x), so two
 * polygons that share an edge get the identical crossing and tile. */
static void extents_init(extents *x, const double (*edges)[4], int n)
{
    x->n = 0;
    for (int k = 0; k < n; k++) {
        double ax = edges[k][0], ay = edges[k][1], bx = edges[k][2], by = edges[k][3];
        if (ay == by) continue;
        if (ay > by || (ay == by && ax > bx)) { double t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
        xedge e = { ax, ay, bx, by, (bx - ax) / (by - ay) };
        x->e[x->n++] = e;
    }
}

/* the extent on the line y = yc, an edge counting from its top end up to but not its bottom */
static void extents_at(const extents *x, double yc, double *lo, double *hi)
{
    *lo = INFINITY; *hi = -INFINITY;
    for (int k = 0; k < x->n; k++) {
        const xedge *e = &x->e[k];
        if (e->ay <= yc && yc < e->by) {
            const double v = e->ax + (yc - e->ay) * e->k;
            if (v < *lo) *lo = v;
            if (v > *hi) *hi = v;
        }
    }
}

/* the extent over the band y0 <= y <= y1 (every edge clipped to the band) */
static void extents_over(const extents *x, double y0, double y1, double *lo, double *hi)
{
    *lo = INFINITY; *hi = -INFINITY;
    for (int k = 0; k < x->n; k++) {
        const xedge *e = &x->e[k];
        const double t = e->ay > y0 ? e->ay : y0, u = e->by < y1 ? e->by : y1;
        if (t > u) continue;
        const double ys[2] = { t, u };
        for (int q = 0; q < 2; q++) {
            const double v = e->ax + (ys[q] - e->ay) * e->k;
            if (v < *lo) *lo = v;
            if (v > *hi) *hi = v;
        }
    }
}

/* the original's footprint: its painted rows clamped to the viewport, by coarse row */
typedef struct { int n, y0; int32_t l[256], r[256]; uint8_t has[256]; } footprint;

static void footprint_rows(const hires *h, footprint *f)
{
    const int32_t xmin = h->poly.vp[0], ymin = h->poly.vp[1], xmax = h->poly.vp[2], ymax = h->poly.vp[3];
    f->y0 = h->poly.top;
    f->n = h->poly.nrows;
    for (int k = 0; k < f->n; k++) {
        const int y = f->y0 + k;
        int32_t l = h->poly.rows[2 * k], r = h->poly.rows[2 * k + 1];
        f->has[k] = 0;
        if (y < ymin || y > ymax) continue;
        if (l < xmin) l = xmin;
        if (r > xmax) r = xmax;
        if (l > r) continue;
        f->l[k] = l; f->r[k] = r; f->has[k] = 1;
    }
}

/* What a polygon paints on the N-times grid: for each fine row up to two spans,
 * written to spans[] as (row, left, right); returns their count. A fine pixel
 * is painted when it lies in the footprint and its centre is within `grow` fine
 * pixels of the true polygon (0: left and top inclusive, right and bottom
 * exclusive), or when its coarse pixel has all eight neighbours in the footprint. */
static int fine_cover(const footprint *f, const extents *x, int n, double grow, int32_t *spans)
{
    int count = 0;
    for (int k = 0; k < f->n; k++) {
        if (!f->has[k]) continue;
        const int y = f->y0 + k;
        const int32_t l = f->l[k], r = f->r[k];
        const int above = k > 0 && f->has[k - 1], below = k + 1 < f->n && f->has[k + 1];
        int32_t in0 = 0, in1 = -1;
        int inner = 0;
        if (above && below) {
            int32_t ix0 = l, ix1 = r;
            if (f->l[k - 1] > ix0) ix0 = f->l[k - 1];
            if (f->l[k + 1] > ix0) ix0 = f->l[k + 1];
            if (f->r[k - 1] < ix1) ix1 = f->r[k - 1];
            if (f->r[k + 1] < ix1) ix1 = f->r[k + 1];
            ix0++; ix1--;
            if (ix0 <= ix1) { inner = 1; in0 = ix0 * n; in1 = ix1 * n + n - 1; }
        }
        for (int j = 0; j < n; j++) {
            const double yc = y * n + j + 0.5;
            double lo, hi;
            int64_t a = 1, b = 0;
            if (grow != 0) {
                extents_over(x, yc - grow, yc + grow, &lo, &hi);
                if (lo != INFINITY) { a = (int64_t)ceil(lo - grow - 0.5); b = (int64_t)floor(hi + grow - 0.5); }
            } else {
                extents_at(x, yc, &lo, &hi);
                if (lo != INFINITY) { a = (int64_t)ceil(lo - 0.5); b = (int64_t)ceil(hi - 0.5) - 1; }
            }
            if (a < (int64_t)l * n) a = (int64_t)l * n;
            if (b > (int64_t)r * n + n - 1) b = (int64_t)r * n + n - 1;
            int32_t s[2][2];
            int ns = 0;
            if (a <= b) { s[0][0] = (int32_t)a; s[0][1] = (int32_t)b; ns = 1; }
            if (inner) {
                if (ns && s[0][1] >= in0 - 1 && s[0][0] <= in1 + 1) {
                    if (in0 < s[0][0]) s[0][0] = in0;
                    if (in1 > s[0][1]) s[0][1] = in1;
                } else if (ns && (s[0][0] > in0 || (s[0][0] == in0 && s[0][1] > in1))) {
                    s[1][0] = s[0][0]; s[1][1] = s[0][1];
                    s[0][0] = in0; s[0][1] = in1;
                    ns = 2;
                } else {
                    s[ns][0] = in0; s[ns][1] = in1;
                    ns++;
                }
            }
            for (int q = 0; q < ns; q++) {
                spans[3 * count] = y * n + j;
                spans[3 * count + 1] = s[q][0];
                spans[3 * count + 2] = s[q][1];
                count++;
            }
        }
    }
    return count;
}

static int64_t cover_total(const int32_t *spans, int count)
{
    int64_t t = 0;
    for (int k = 0; k < count; k++) t += spans[3 * k + 2] - spans[3 * k + 1] + 1;
    return t;
}

/* ---- the builder ---- */

static int paint_ok(int colour)
{
    const int hi = (colour >> 8) & 0xFF;
    return hi == 0xFF || hi == 0xFE || hi == 0xFD || hi == 0xFB;
}

static void paint_run(uint8_t *row, int n, int colour)
{
    const uint8_t lo = (uint8_t)colour, hi = (uint8_t)(colour >> 8);
    if (hi == 0xFF) memset(row, lo, (size_t)n);
    else if (hi == 0xFE) for (int i = 0; i < n; i++) row[i] &= lo;
    else if (hi == 0xFD) for (int i = 0; i < n; i++) row[i] |= lo;
}

/* the true edges in coarse pixels (the origin added); 0 with the reason counted */
static int polygon_edges(hires *h, double (*out)[4])
{
    if (h->poly.nclip) { h->stats[HS_NEAR_CLIPPED]++; return 0; }
    const int ne = h->poly.nedges;
    if (ne < 3) { h->stats[HS_FEW_EDGES]++; return 0; }
    for (int k = 0; k < ne; k++) {
        const hires_edge *e = &h->poly.edges[k];
        if (!e->have_g || !e->a.ok || !e->b.ok) { h->stats[HS_NO_VERTICES]++; return 0; }
        if (e->a.range == 2 || e->b.range == 2) { h->stats[HS_BEHIND]++; return 0; }
    }
    /* the origin ([8602], [8604]) most of the unclipped edges agree on, the first on a tie */
    int32_t vx[HIRES_EDGES], vy[HIRES_EDGES], vc[HIRES_EDGES];
    int nv = 0;
    for (int k = 0; k < ne; k++) {
        const hires_edge *e = &h->poly.edges[k];
        if ((e->st & 0xFF) != 0) continue;
        const int32_t dx = s16(e->x0) - e->a.px, dy = s16(e->y0) - e->a.py;
        int q;
        for (q = 0; q < nv && (vx[q] != dx || vy[q] != dy); q++) {}
        if (q == nv) { vx[nv] = dx; vy[nv] = dy; vc[nv++] = 0; }
        vc[q]++;
    }
    if (nv) {
        int best = 0;
        for (int q = 1; q < nv; q++) if (vc[q] > vc[best]) best = q;
        h->origin[0] = vx[best];
        h->origin[1] = vy[best];
    }
    for (int k = 0; k < ne; k++) {
        const hires_edge *e = &h->poly.edges[k];
        double ax, ay, bx, by;
        if (!vertex_fraction(&e->a, &ax, &ay) || !vertex_fraction(&e->b, &bx, &by)) {
            h->stats[HS_NO_DIVISOR]++;
            return 0;
        }
        out[k][0] = ax + h->origin[0]; out[k][1] = ay + h->origin[1];
        out[k][2] = bx + h->origin[0]; out[k][3] = by + h->origin[1];
    }
    return 1;
}

static void refill(hires *h, drawlist *d, drawlist_page *page)
{
    h->stats[HS_POLYGONS]++;
    static double base[HIRES_EDGES][4], fine[HIRES_EDGES][4];
    if (!polygon_edges(h, base)) { h->stats[HS_LEFT_NEAREST]++; return; }
    static footprint f;
    footprint_rows(h, &f);
    static extents x;
    static int32_t spans[3 * 2 * 256 * 64];
    /* the least overshoot with which the true edges cover the whole footprint at N = 1 */
    int64_t want = 0;
    for (int k = 0; k < f.n; k++) if (f.has[k]) want += f.r[k] - f.l[k] + 1;
    extents_init(&x, (const double (*)[4])base, h->poly.nedges);
    int g;
    for (g = 0; g < 6; g++) {
        const int c = fine_cover(&f, &x, 1, GROWS[g], spans);
        if (cover_total(spans, c) == want) break;
    }
    if (g == 6) { h->stats[HS_BEYOND]++; h->stats[HS_LEFT_NEAREST]++; return; }
    h->stats[HS_GROW0 + g]++;
    const int n = h->n;
    if (n > 64) return;
    for (int k = 0; k < h->poly.nedges; k++)
        for (int c = 0; c < 4; c++) fine[k][c] = base[k][c] * n;
    extents_init(&x, (const double (*)[4])fine, h->poly.nedges);
    const int count = fine_cover(&f, &x, n, GROWS[g], spans);
    if (!paint_ok(h->fill_colour)) { h->stats[HS_STYLE]++; h->stats[HS_LEFT_NEAREST]++; return; }
    const int W = 320 * n;
    for (int k = 0; k < count; k++) {
        const int32_t hy = spans[3 * k];
        int32_t l = spans[3 * k + 1], r = spans[3 * k + 2];
        if (hy < 0 || hy >= 200 * n) continue;
        if (l < 0) l = 0;
        if (r > W - 1) r = W - 1;
        if (r < l) continue;
        paint_run(page->hi + (size_t)hy * W + l, r - l + 1, h->fill_colour);
    }
    h->stats[HS_FOOT_PIXELS] += (uint64_t)want;
    h->stats[HS_FOOT_FINE] += (uint64_t)cover_total(spans, count);
    h->stats[HS_REFILLED]++;
    page->suppress = 1;
    h->suppressed = (int)(page - d->page);
}

int hires_guard(const uint8_t *coarse, uint8_t *fine, int n, uint64_t *flat, uint64_t *agree)
{
    const size_t W = (size_t)320 * n;
    int restored = 0;
    for (int y = 1; y < 199; y++)
        for (int x = 1; x < 319; x++) {
            const uint8_t v = coarse[y * 320 + x];
            int same = 1;
            for (int dy = -1; dy <= 1 && same; dy++)
                for (int dx = -1; dx <= 1 && same; dx++) same = coarse[(y + dy) * 320 + x + dx] == v;
            if (!same) continue;
            (*flat)++;
            int ok = 1;
            uint8_t *q = fine + (size_t)y * n * W + (size_t)x * n;
            for (int j = 0; j < n && ok; j++)
                for (int i = 0; i < n && ok; i++) ok = q[j * W + i] == v;
            if (ok) { (*agree)++; continue; }
            for (int j = 0; j < n; j++) memset(q + j * W, v, (size_t)n);
            restored++;
        }
    return restored;
}

static void parse_edge(hires *h, const int32_t *v, hires_edge *e)
{
    e->slot = v[0];
    e->x0 = v[1] & 0xFFFF; e->y0 = v[2] & 0xFFFF; e->x1 = v[3] & 0xFFFF; e->y1 = v[4] & 0xFFFF;
    e->st = v[5] & 0xFFFF;
    const int k = slot_find(h, (uint16_t)v[0], 0);
    e->have_g = k >= 0;
    if (k >= 0) { e->a = h->slot[k].a; e->b = h->slot[k].b; }
}

void hires_record(hires *h, drawlist *d, char kind, const int32_t *v, int n)
{
    if (h->suppressed >= 0 && kind != 'b' && kind != 'a') {
        d->page[h->suppressed].suppress = 0;
        h->suppressed = -1;
    }
    switch (kind) {
    case 'V': {
        if (n < 8) break;
        hires_vertex *x = vertex_get(h, (uint16_t)v[7], 1);
        if (x) { x->ok = 1; x->x = v[0]; x->y = v[1]; x->z = v[2]; x->px = v[3]; x->py = v[4]; x->range = v[5]; }
        break;
    }
    case 'G': {
        if (n < 3) break;
        const int k = slot_find(h, (uint16_t)v[0], 1);
        if (k < 0) break;
        const hires_vertex *a = vertex_get(h, (uint16_t)v[1], 0), *b = vertex_get(h, (uint16_t)v[2], 0);
        memset(&h->slot[k].a, 0, sizeof h->slot[k].a);
        memset(&h->slot[k].b, 0, sizeof h->slot[k].b);
        if (a) h->slot[k].a = *a;
        if (b) h->slot[k].b = *b;
        break;
    }
    case 'E':
        if (n < 6) break;
        if (h->poly.active && h->poly.nclip == 2 && !h->poly.njoin) {
            h->poly.njoin = 1;                        /* the join is not one of the polygon's own edges */
            break;
        }
        if (h->npending < HIRES_EDGES) parse_edge(h, v, &h->pending[h->npending++]);
        break;
    case 'F':
        if (n < 12) break;
        if (!h->seen_fill) {
            h->seen_fill = 1;
            h->npending = 0;
            h->poly.active = 0;
            break;
        }
        h->poly.active = 1;
        h->poly.done = h->poly.have_rows = 0;
        h->poly.njoin = 0;
        memcpy(h->poly.edges, h->pending, (size_t)h->npending * sizeof h->pending[0]);
        h->poly.nedges = h->npending;
        h->npending = 0;
        for (int k = 0; k < 4; k++) h->poly.vp[k] = v[1 + k];
        h->poly.nclip = v[11];
        break;
    case 'R':
        if (n < 3) break;
        if (3 + 2 * v[2] < n) h->fill_colour = v[3 + 2 * v[2]] & 0xFFFF;
        if (h->poly.active) {
            h->poly.top = v[1];
            h->poly.nrows = v[2] > 256 ? 256 : v[2];
            for (int k = 0; k < 2 * h->poly.nrows && 3 + k < n; k++) h->poly.rows[k] = v[3 + k];
            h->poly.have_rows = 1;
        }
        break;
    case 'b':
        if (n < 5 || !h->poly.active || h->poly.done) break;
        h->poly.done = 1;
        {
            drawlist_page *page = drawlist_page_of(d, (uint16_t)v[3]);
            if (!page || !page->hi || !h->poly.have_rows) break;
            refill(h, d, page);
        }
        break;
    default:
        break;
    }
}
