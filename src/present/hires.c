/* hires.c - see hires.h. The functions follow tools/hires_subpixel.py's. */
#include "hires.h"
#include "nearclip.h"

#include "tls.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *const STAT_NAMES[HS_COUNT] = {
    "polygons", "polygons left nearest", "polygons refilled from vertices", "footprint pixels",
    "footprint fine pixels painted", "overshoot 0", "overshoot 0.5", "overshoot 1", "overshoot 1.5", "overshoot 2",
    "overshoot 3", "skipped: near-clipped", "skipped: fewer than three edges", "skipped: edge without vertices",
    "skipped: vertex behind the eye", "skipped: vertex without a divisor",
    "skipped: footprint beyond 3 px of its geometry", "skipped: fill style with no rule",
    "span-rasteriser polygons (strips, regions)", "span polygons refilled from their corners",
    "span polygons: footprint beyond 3 px of the corners", "span polygons: strips too thin to trim" };

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
    if (v->range < 0 || v->range > 1) return 0; /* orthographic needs G mode metadata */
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

static int snapshot_fraction(const hires_snapshot *s, const hires_vertex *v, double *x, double *y)
{
    if (!s->ortho) return vertex_fraction(v, x, y);
    const int32_t camera[3] = { v->x, v->y, v->z };
    int32_t coarse[2]; double fine[2];
    if (!present_ortho_project(camera, s->ortho, coarse, fine) ||
        coarse[0] != v->px || coarse[1] != v->py) return 0;
    *x = fine[0]; *y = fine[1]; return 1;
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

/* Keep the original checkerboard's pixel-sized cells while refining the
 * polygon boundary. Changing its frequency with scale would change the
 * texture rather than just the geometry. Transparent stipple cells retain
 * the fine background already drawn. */
static void paint_run(uint8_t *row, int n, int colour, int x, int y, int scale)
{
    const uint8_t lo = (uint8_t)colour, hi = (uint8_t)(colour >> 8);
    if (hi == 0xFF) memset(row, lo, (size_t)n);
    else if (hi == 0xFE) for (int i = 0; i < n; i++) row[i] &= lo;
    else if (hi == 0xFD) for (int i = 0; i < n; i++) row[i] |= lo;
    else if (hi != 0xFB) for (int i = 0; i < n; i++) {
        const int odd = ((x + i) / scale + y / scale) & 1;
        if (hi == 0xFC) { if (odd) row[i] = lo; }
        else row[i] = odd ? hi : lo;
    }
}

static void model_span(uint8_t *mask,size_t first,size_t last)
{
    for(size_t at=first;at<=last;at++) mask[at>>3]|=(uint8_t)(1u<<(at&7));
}

/* Track unclamped true geometry separately from the polygon's painted fine
 * spans. Their unions distinguish internal footprint seams from outer edges. */
static void model_geometry(drawlist_page *page,const footprint *f,const extents *x,const int32_t vp[4])
{
    const int n=page->n,W=320*n,H=200*n;
    const size_t bits=((size_t)W*H+7)/8;
    if(!page->model) page->model=(uint8_t *)calloc(2*bits,1);
    if(!page->model) return;
    for(int k=0;k<f->n;k++) if(f->has[k] && f->y0+k>=0 && f->y0+k<200) {
        int l=f->l[k]<0?0:f->l[k],r=f->r[k]>319?319:f->r[k];
        if(l<=r) model_span(page->model_foot,(size_t)(f->y0+k)*320+l,(size_t)(f->y0+k)*320+r);
    }
    double top=H,bottom=0;
    for(int k=0;k<x->n;k++) {if(x->e[k].ay<top) top=x->e[k].ay;if(x->e[k].by>bottom) bottom=x->e[k].by;}
    int y0=top<=0?0:top>=H?H:(int)ceil(top-0.5);
    int y1=bottom>=H?H-1:bottom<=0?-1:(int)ceil(bottom-0.5)-1;
    if(vp[1]>0 && vp[1]<200 && y0<vp[1]*n) y0=vp[1]*n;
    if(vp[3]>=0 && vp[3]<199 && y1>=(vp[3]+1)*n) y1=(vp[3]+1)*n-1;
    if(vp[1]>=200 || vp[3]<0 || vp[0]>=320 || vp[2]<0) return;
    const int xmin=vp[0]>0?vp[0]*n:0,xmax=vp[2]<319?(vp[2]+1)*n-1:W-1;
    for(int y=y0;y<=y1;y++) {
        double lo,hi;extents_at(x,y+0.5,&lo,&hi);
        if(lo==INFINITY || hi<=0 || lo>=W) continue;
        int l=lo<=0?0:(int)ceil(lo-0.5),r=hi>=W?W-1:(int)ceil(hi-0.5)-1;
        if(l<xmin) l=xmin;if(r>xmax) r=xmax;
        if(l<=r) model_span(page->model,(size_t)y*W+l,(size_t)y*W+r);
    }
    page->model_active=1;
}

/* The camera cut's two crossings close the visible part of a convex model
 * polygon. Verify each against the crossing retained by the original before
 * using the precision discarded there. Legacy logs retain their old fill. */
static int near_polygon_edges(hires *h, double (*out)[4])
{
    const int ne = h->poly.nedges;
    if (h->poly.nclip != 2 || ne >= HIRES_EDGES) return 0;
    int count = 0, cuts = 0;
    double crossing[2][2];
    int32_t origin[2] = { 0, 0 };
    for (int k = 0; k < ne; k++) {
        const hires_edge *e = &h->poly.edges[k];
        const hires_snapshot *s = &e->snap;
        if (!s->valid || s->ortho) return 0;
        if (!k) { origin[0] = s->origin[0]; origin[1] = s->origin[1]; }
        else if (origin[0] != s->origin[0] || origin[1] != s->origin[1]) return 0;
        if (s->behind == 3) {
            if (!(e->st & 0x80)) return 0;
            continue;
        }
        double a[2], b[2];
        if (s->behind) {
            if (!(e->st & 0x40) || !e->have_crossing || cuts == 2) return 0;
            const hires_vertex *front = s->behind == 1 ? &s->b : &s->a;
            const hires_vertex *behind = s->behind == 1 ? &s->a : &s->b;
            const int32_t f[3] = { front->x, front->y, front->z };
            const int32_t z[3] = { behind->x, behind->y, behind->z };
            int32_t coarse[2];
            double fine[2], visible[2];
            if (!present_near_crossing(f,z,coarse,fine) || !vertex_fraction(front,&visible[0],&visible[1])) return 0;
            for (int c = 0; c < 2; c++) {
                if (coarse[c] + origin[c] != e->crossing[c]) return 0;
                crossing[cuts][c] = fine[c] + origin[c];
                a[c] = (s->behind == 1 ? fine[c] : visible[c]) + origin[c];
                b[c] = (s->behind == 1 ? visible[c] : fine[c]) + origin[c];
            }
            cuts++;
        } else {
            if (e->st & 0x40 || !vertex_fraction(&s->a,&a[0],&a[1]) || !vertex_fraction(&s->b,&b[0],&b[1])) return 0;
            for (int c = 0; c < 2; c++) { a[c] += origin[c]; b[c] += origin[c]; }
        }
        out[count][0] = a[0]; out[count][1] = a[1]; out[count][2] = b[0]; out[count][3] = b[1];
        count++;
    }
    if (cuts != 2 || count < 2) return 0;
    out[count][0] = crossing[0][0]; out[count][1] = crossing[0][1];
    out[count][2] = crossing[1][0]; out[count][3] = crossing[1][1];
    return count + 1;
}

/* True edges in coarse pixels (origin added); zero with the reason counted. */
static int polygon_edges(hires *h, double (*out)[4])
{
    if (h->poly.nclip) {
        const int ne = near_polygon_edges(h,out);
        if (!ne) h->stats[HS_NEAR_CLIPPED]++;
        return ne;
    }
    const int ne = h->poly.nedges;
    if (ne < 3) { h->stats[HS_FEW_EDGES]++; return 0; }
    /* Modern records retain the camera and origin at edge preparation.
     * A polygon clipped on every edge has no unclipped endpoint to vote for
     * its origin; using the previous view's origin loses valid fine geometry.
     * The snapshots also avoid stale V records when vertex storage is reused. */
    int snapshots = 1;
    for (int k = 0; k < ne; k++) {
        const hires_snapshot *s = &h->poly.edges[k].snap;
        if (!s->valid || s->behind || s->origin[0] != h->poly.edges[0].snap.origin[0] ||
            s->origin[1] != h->poly.edges[0].snap.origin[1] ||
            s->ortho != h->poly.edges[0].snap.ortho) { snapshots = 0; break; }
    }
    if (snapshots) {
        for (int k = 0; k < ne; k++) {
            const hires_snapshot *s = &h->poly.edges[k].snap;
            double ax, ay, bx, by;
            if (!snapshot_fraction(s, &s->a, &ax, &ay) || !snapshot_fraction(s, &s->b, &bx, &by)) {
                h->stats[HS_NO_DIVISOR]++; return 0;
            }
            out[k][0] = ax + s->origin[0]; out[k][1] = ay + s->origin[1];
            out[k][2] = bx + s->origin[0]; out[k][3] = by + s->origin[1];
        }
        return ne;
    }
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
    return ne;
}

static void refill(hires *h, drawlist *d, drawlist_page *page)
{
    h->stats[HS_POLYGONS]++;
    static F117_TLS double base[HIRES_EDGES][4], fine[HIRES_EDGES][4];
    const int ne = polygon_edges(h, base);
    if (!ne) { h->stats[HS_LEFT_NEAREST]++; return; }
    static F117_TLS footprint f;
    footprint_rows(h, &f);
    static F117_TLS extents x;
    static F117_TLS int32_t spans[3 * 2 * 256 * 64];
    /* the least overshoot with which the true edges cover the whole footprint at N = 1 */
    int64_t want = 0;
    for (int k = 0; k < f.n; k++) if (f.has[k]) want += f.r[k] - f.l[k] + 1;
    extents_init(&x, (const double (*)[4])base, ne);
    int g;
    for (g = 0; g < 6; g++) {
        const int c = fine_cover(&f, &x, 1, GROWS[g], spans);
        if (cover_total(spans, c) == want) break;
    }
    if (g == 6) { h->stats[HS_BEYOND]++; h->stats[HS_LEFT_NEAREST]++; return; }
    h->stats[HS_GROW0 + g]++;
    const int n = h->n;
    if (n > 64) return;
    for (int k = 0; k < ne; k++)
        for (int c = 0; c < 4; c++) fine[k][c] = base[k][c] * n;
    extents_init(&x, (const double (*)[4])fine, ne);
    const int count = fine_cover(&f, &x, n, GROWS[g], spans);
    const int W = 320 * n;
    const int tracking=n>1 && (h->fill_colour&0xFF00)!=0xFB00;
    if(tracking) model_geometry(page,&f,&x,h->poly.vp);
    for (int k = 0; k < count; k++) {
        const int32_t hy = spans[3 * k];
        int32_t l = spans[3 * k + 1], r = spans[3 * k + 2];
        if (hy < 0 || hy >= 200 * n) continue;
        if (l < 0) l = 0;
        if (r > W - 1) r = W - 1;
        if (r < l) continue;
        paint_run(page->hi + (size_t)hy * W + l, r - l + 1, h->fill_colour, l, hy, n);
        if(tracking && page->model) {
            const size_t bits=((size_t)64000*n*n+7)/8;
            model_span(page->model+bits,(size_t)hy*W+l,(size_t)hy*W+r);
        }
    }
    h->stats[HS_FOOT_PIXELS] += (uint64_t)want;
    h->stats[HS_FOOT_FINE] += (uint64_t)cover_total(spans, count);
    h->stats[HS_REFILLED]++;
    page->suppress = 1;
    h->suppressed = (int)(page - d->page);
}

/* A polygon the game fills through its own span rasteriser (record 'U': the camera's sky and
 * ground gradient strips and the map's regions) arrives as its integer corners, and the span
 * fill ('Q') that paints it follows. The corners are the true geometry to within half a pixel
 * (the game rounded them), so the edges between them are the exact lines the rasteriser walked.
 * Refill the strip on the fine grid the way a model polygon is refilled: the footprint is the
 * span fill's rows, the true shape sits at the corners' pixel centres, and the least overshoot
 * with which it covers the whole footprint at N = 1 is kept at that many fine pixels. */
#ifndef THIN_PERCENT
#define THIN_PERCENT 60
#endif

/* A strip whose true shape is a small part of its footprint is, in the original, a line: a
 * stair of cells one or two thick. Draw it as one fine stroke along the strip's direction, from
 * the footprint's first cell to its last, as thick (in cells) as the footprint is. */
static void thin_strip_stroke(hires *h, drawlist *d, drawlist_page *page, const footprint *f,
                              const double (*base)[4], int ne, int64_t want)
{
    double best = 0, dx = 0, dy = 0;
    for (int k = 0; k < ne; k++) {
        const double ex = base[k][2] - base[k][0], ey = base[k][3] - base[k][1], len = sqrt(ex * ex + ey * ey);
        if (len > best) { best = len; dx = ex / len; dy = ey / len; }
    }
    if (best < 1e-9) { h->stats[HS_SPAN_THIN]++; return; }
    double sx = 0, sy = 0;
    for (int k = 0; k < f->n; k++) {
        if (!f->has[k]) continue;
        const int64_t cells = f->r[k] - f->l[k] + 1;
        sx += (double)cells * (f->l[k] + f->r[k] + 1) / 2.0;
        sy += (double)cells * (f->y0 + k + 0.5);
    }
    const double cx = sx / (double)want, cy = sy / (double)want;
    double tmin = INFINITY, tmax = -INFINITY;
    for (int k = 0; k < f->n; k++) {
        if (!f->has[k]) continue;
        const double y = f->y0 + k + 0.5 - cy;
        const double a = (f->l[k] + 0.5 - cx) * dx + y * dy, b = (f->r[k] + 0.5 - cx) * dx + y * dy;
        if (a < tmin) tmin = a;
        if (b < tmin) tmin = b;
        if (a > tmax) tmax = a;
        if (b > tmax) tmax = b;
    }
    const double major = (tmax - tmin) * (fabs(dx) > fabs(dy) ? fabs(dx) : fabs(dy));
    int thick = (int)floor((double)want / (major + 1.0) + 0.5);
    if (thick < 1) thick = 1;
    const int n = h->n;
    const int ax = (int)floor((cx + dx * tmin) * n + 0.5), ay = (int)floor((cy + dy * tmin) * n + 0.5);
    const int bx = (int)floor((cx + dx * tmax) * n + 0.5), by = (int)floor((cy + dy * tmax) * n + 0.5);
    drawlist_fine_stroke(page, ax, ay, bx, by, thick * n, d->colour & 0xFF);
    page->suppress = 1;
    h->suppressed = (int)(page - d->page);
    h->stats[HS_SPAN_THIN]++;
}
static void refill_span_polygon(hires *h, drawlist *d, const int32_t *v, int n)
{
    static F117_TLS double base[32][4], fine[32][4];
    static F117_TLS footprint f;
    static F117_TLS extents x;
    static F117_TLS int32_t spans[3 * 2 * 256 * 64];
    const int nrows = v[3];
    if (v[2] != 0 || nrows <= 0 || nrows > 256 || h->n <= 1 || h->n > 64) return;   /* N = 1 is the original */
    uint16_t seg = d->seg;
    int32_t org = d->origin;
    if (n >= 6 + 2 * nrows) { seg = (uint16_t)v[4 + 2 * nrows]; org = v[5 + 2 * nrows]; }
    drawlist_page *page = drawlist_page_of(d, seg);
    if (!page || !page->hi || org < 0 || org >= 64000 || d->colour < 0) return;
    h->stats[HS_SPAN_POLYGONS]++;
    const int ox = org % 320, oy = org / 320;
    const int ne = h->span_poly.n;
    for (int k = 0; k < ne; k++) {
        const int j = (k + 1) % ne;
        base[k][0] = h->span_poly.x[k] + ox + 0.5; base[k][1] = h->span_poly.y[k] + oy + 0.5;
        base[k][2] = h->span_poly.x[j] + ox + 0.5; base[k][3] = h->span_poly.y[j] + oy + 0.5;
    }
    f.y0 = v[0] + oy;
    f.n = nrows;
    int64_t want = 0;
    for (int k = 0; k < nrows; k++) {
        int32_t l = v[4 + 2 * k], r = v[5 + 2 * k];
        const int y = f.y0 + k;
        f.has[k] = 0;
        if (y < 0 || y >= 200 || r < l || (r == l && (r == 0 || r == 0x13F))) continue;
        l += ox; r += ox;
        if (l < 0) l = 0;
        if (r > 319) r = 319;
        if (l > r) continue;
        f.l[k] = l; f.r[k] = r; f.has[k] = 1;
        want += r - l + 1;
    }
    if (!want) return;
    extents_init(&x, (const double (*)[4])base, ne);
    int g;
    for (g = 0; g < 6; g++) {
        const int c = fine_cover(&f, &x, 1, GROWS[g], spans);
        if (cover_total(spans, c) == want) break;
    }
    if (g == 6) { h->stats[HS_SPAN_BEYOND]++; return; }
    /* A strip so thin that its true shape is a small part of what the rasteriser painted (the
     * gradient strips near the horizon, whose edges nearly coincide) is a stair-stepped line in
     * the original, not a region: trimming it to the true geometry would leave holes, so it
     * stays a scaled copy. */
    {
        const int c0 = fine_cover(&f, &x, 1, 0, spans);
        if (cover_total(spans, c0) * 100 < want * THIN_PERCENT) {
            thin_strip_stroke(h, d, page, &f, base, ne, want);
            return;
        }
    }
    const int sc = h->n;
    for (int k = 0; k < ne; k++)
        for (int c = 0; c < 4; c++) fine[k][c] = base[k][c] * sc;
    extents_init(&x, (const double (*)[4])fine, ne);
    const int count = fine_cover(&f, &x, sc, GROWS[g], spans);
    const int W = 320 * sc;
    const int word = 0xFF00 | (d->colour & 0xFF);
    for (int k = 0; k < count; k++) {
        const int32_t hy = spans[3 * k];
        int32_t l = spans[3 * k + 1], r = spans[3 * k + 2];
        if (hy < 0 || hy >= 200 * sc) continue;
        if (l < 0) l = 0;
        if (r > W - 1) r = W - 1;
        if (r < l) continue;
        paint_run(page->hi + (size_t)hy * W + l, r - l + 1, word, l, hy, sc);
    }
    page->suppress = 1;
    h->suppressed = (int)(page - d->page);
    h->stats[HS_SPAN_REFILLED]++;
}

int hires_guard(const uint8_t *coarse, uint8_t *fine, int n, uint64_t *flat, uint64_t *agree)
{
    return hires_guard_masked(coarse, fine, n, NULL, flat, agree);
}

int hires_guard_masked(const uint8_t *coarse, uint8_t *fine, int n, const uint8_t *strokes,
                       uint64_t *flat, uint64_t *agree)
{
    const size_t W = (size_t)320 * n;
    int restored = 0;
    for (int y = 1; y < 199; y++)
        for (int x = 1; x < 319; x++) {
            const unsigned a = (unsigned)(y * 320 + x);
            if (strokes && ((strokes[a >> 3] >> (a & 7)) & 1)) continue;
            const uint8_t v = coarse[y * 320 + x];
            int same = 1;
            for (int dy = -1; dy <= 1 && same; dy++)
                for (int dx = -1; dx <= 1 && same; dx++) same = coarse[(y + dy) * 320 + x + dx] == v;
            if (!same) continue;
            (*flat)++;
            int ok = 1;
            uint8_t *q = fine + (size_t)y * n * W + (size_t)x * n;
            /* Read complete groups within this cell only. memcpy permits
             * unaligned rows without aliasing or alignment assumptions. */
            const uint64_t eight = (uint64_t)v * UINT64_C(0x0101010101010101);
            const uint32_t four = (uint32_t)v * UINT32_C(0x01010101);
            for (int j = 0; j < n && ok; j++) {
                const uint8_t *row = q + j * W;
                int i = 0;
                for (; i + 8 <= n && ok; i += 8) {
                    uint64_t got; memcpy(&got, row + i, sizeof got);
                    ok = got == eight;
                }
                if (ok && i + 4 <= n) {
                    uint32_t got; memcpy(&got, row + i, sizeof got);
                    ok = got == four; i += 4;
                }
                for (; i < n && ok; i++) ok = row[i] == v;
            }
            if (ok) { (*agree)++; continue; }
            for (int j = 0; j < n; j++) memset(q + j * W, v, (size_t)n);
            restored++;
        }
    return restored;
}

static void parse_edge(hires *h, const int32_t *v, int n, hires_edge *e)
{
    memset(e,0,sizeof *e);
    e->slot = v[0];
    e->x0 = v[1] & 0xFFFF; e->y0 = v[2] & 0xFFFF; e->x1 = v[3] & 0xFFFF; e->y1 = v[4] & 0xFFFF;
    e->st = v[5] & 0xFFFF;
    const int k = slot_find(h, (uint16_t)v[0], 0);
    e->have_g = k >= 0;
    if (k >= 0) { e->a = h->slot[k].a; e->b = h->slot[k].b; e->snap = h->slot[k].snap; }
    if (n >= 8) { e->have_crossing = 1; e->crossing[0] = v[6]; e->crossing[1] = v[7]; }
}

void hires_record(hires *h, drawlist *d, char kind, const int32_t *v, int n)
{
    if (kind != 'U' && kind != 'K' && kind != 'Q' && kind != 'X' && kind != 'x' && kind != 'n') h->span_poly.have = 0;
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
        hires_snapshot *s = &h->slot[k].snap;
        memset(s,0,sizeof *s);
        if (n >= 17) {
            s->valid = 1; s->origin[0] = v[3]; s->origin[1] = v[4];
            s->ortho = n >= 18 ? v[17] : 0;
            hires_vertex *ends[2] = { &s->a, &s->b };
            for (int end = 0; end < 2; end++) {
                const int at = 5 + 6 * end;
                const int zhi = s16(v[at+2] >> 16);
                *ends[end] = (hires_vertex){ 1,v[at],v[at+1],v[at+2],v[at+3],v[at+4],zhi >= 0x100 ? 0 : zhi >= 1 ? 1 : 2 };
                if (v[at+5]) s->behind |= 1 << end;
            }
        }
        break;
    }
    case 'E':
        if (n < 6) break;
        if (h->poly.active && h->poly.nclip == 2 && !h->poly.njoin) {
            h->poly.njoin = 1;                        /* the join is not one of the polygon's own edges */
            break;
        }
        if (h->npending < HIRES_EDGES) parse_edge(h, v, n, &h->pending[h->npending++]);
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
    case 'U':
        h->span_poly.have = 0;
        if (n >= 8 && v[1] >= 3 && v[1] <= 32 && 2 + 2 * v[1] <= n) {
            h->span_poly.have = 1;
            h->span_poly.colour = v[0] & 0xFF;
            h->span_poly.n = v[1];
            for (int k = 0; k < v[1]; k++) { h->span_poly.x[k] = v[2 + 2 * k]; h->span_poly.y[k] = v[3 + 2 * k]; }
        }
        break;
    case 'Q':
        if (h->span_poly.have && n >= 4 && d->colour == h->span_poly.colour) refill_span_polygon(h, d, v, n);
        h->span_poly.have = 0;
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
