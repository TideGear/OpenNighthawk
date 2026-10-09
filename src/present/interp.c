/* interp.c - see interp.h. The functions follow tools/interp_frame.py's
 * one for one, under the same names where there is one. */
#include "interp.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NOLEFT 0x7FFF                /* 7FFFh and 8001h as signed words */
#define NORIGHT (-0x7FFF)

#define MAX_MOTION 60                /* a batch whose vertices moved further than this (pixels, mean) is not paired */
#define MAX_HUD_MOTION 30            /* a HUD line that moved further than this between steps is held */
#define MAX_EDGE_MOTION 80           /* a paired polygon with an edge end that moved further than this is held */
#define MAX_COUNT_DIFF 0.1           /* batches of 3 or more vertices may differ in count by this share (at least 1) */
#define CUT_PAIRED 0.5               /* fewer than this share of the polygons paired: a cut */
#define CUT_MOTION 40                /* the median vertex moved further than this (pixels): a cut */

static const char *const STAT_NAMES[IS_COUNT] = {
    "polygons moved", "span fills", "span fills moved", "HUD lines", "HUD lines moved", "outline edges",
    "outline edges moved", "edges camera-space", "edges camera mode fell back", "edges screen-space",
    "regenerated lines", "held: unpaired polygon", "held: fill style with no rule", "held: clip status differs",
    "held: an edge end moved over 80 px (not the same vertex?)", "held: nothing painted",
    "held: span fill with no pair", "held: span fill", "held: HUD lines whose group changed in size",
    "held: HUD line that moved over 30 px", "held: outline edge in an unpaired batch",
    "held: outline edge with no pair" };

const char *interp_stat_name(int k) { return k >= 0 && k < IS_COUNT ? STAT_NAMES[k] : ""; }

static const char *const CAUSE_NAMES[5] = {
    "polygons unpaired: no batch of its size (1 vertex)",
    "polygons unpaired: no batch of its size (model split or merged, or count changed too much)",
    "polygons unpaired: every batch with its vertex count moved over 60 px",
    "polygons unpaired: equal batch exists but in another order or taken by a nearer one",
    "polygons unpaired: no polygon with its edges in the paired batch" };

const char *interp_cause_name(int k) { return k >= 0 && k < 5 ? CAUSE_NAMES[k] : ""; }

static int s16(int64_t v) { return (int16_t)(uint16_t)v; }

static int64_t floordiv(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b && (a < 0) != (b < 0)) ? q - 1 : q;
}

/* Python's int(x + 0.5) for x >= 0, else -int(-x + 0.5) */
static int64_t rnd(double x) { return x >= 0 ? (int64_t)(x + 0.5) : -(int64_t)(-x + 0.5); }

static double lerp(int64_t a, int64_t b, double w) { return (double)a + (double)(b - a) * w; }

#define GROW(arr, n, cap) do { if ((n) == (cap)) { (cap) = (cap) ? 2 * (cap) : 64; \
    (arr) = realloc((arr), (size_t)(cap) * sizeof *(arr)); } } while (0)

/* ---- a frame: batches and polygons ------------------------------------- */

typedef struct { int32_t slot, x0, y0, x1, y1, st, y0hi, y1hi; } edge_t;
typedef struct { int32_t slot, k0, k1; } gedge_t;

typedef struct {
    int e0, ne, j0, nj;               /* the edges before the fill, the near-clip join */
    const int32_t *f;                 /* 'F' values */
    const int32_t *r;                 /* 'R' values */
    int ri;                           /* the 'R' record's index */
    int b0, nb, a0, na;               /* its 'b' and 'a' records (indexes into the frame's lists) */
    int colour;                       /* the colour word the fill uses ([8606]) */
} poly_t;

/* a batch's vertices, G edges, polygons (indexes into pl) and outline edges (into li) */
typedef struct { int v0, nv, g0, ng, p0, np, l0, nl; } batch_t;

typedef struct { int colour, mode, ord, ri; } span_t;
typedef struct { int colour, page, origin, ri; } hud_t;

struct interp_frame {
    const interp_rec *rec;
    int nrec;
    batch_t *b; int nb, cb;
    const int32_t **vert; int nvert, cvert;
    int *vrec;                        /* each vertex's record index */
    gedge_t *g; int ng, cg;
    edge_t *e; int ne, ce;
    edge_t *j; int nj, cj;
    poly_t *p; int np, cp;
    int *bi; int nbi, cbi;
    int *ai; int nai, cai;
    int *li; int nli, cli;
    int *pl; int npl, cpl;
    span_t *s; int ns, cs;
    hud_t *h; int nh, ch;
};

static edge_t parse_edge(const int32_t *v)
{
    edge_t e = { v[0], v[1] & 0xFFFF, v[2] & 0xFFFF, v[3] & 0xFFFF, v[4] & 0xFFFF, v[5] & 0xFFFF,
                 s16((int64_t)v[2] >> 16), s16((int64_t)v[4] >> 16) };
    return e;
}

interp_frame *interp_parse(const interp_rec *rec, int n)
{
    interp_frame *f = calloc(1, sizeof *f);
    f->rec = rec;
    f->nrec = n;
    int cur = -1;                     /* the batch being collected */
    int pend0 = 0;                    /* edges since the last fill */
    int poly = -1, poly_nclip = 0;    /* the polygon between its fill entry and its painting */
    int fill_poly = -1;
    int seen_fill = 0;                /* the first polygon may have edges from before the frame */
    int colour = -1;
    for (int i = 0; i < n; i++) {
        const interp_rec *r = &rec[i];
        const int32_t *v = r->v;
        switch (r->kind) {
        case 'V':
            if (r->n < 8) break;
            if (v[6] == 0xC6B4 || cur < 0) {
                GROW(f->b, f->nb, f->cb);
                batch_t nb = { f->nvert, 0, f->ng, 0, f->npl, 0, f->nli, 0 };
                f->b[f->nb++] = nb;
                cur = f->nb - 1;
            }
            if (f->nvert == f->cvert) f->vrec = realloc(f->vrec, sizeof(int) * (size_t)(f->cvert ? 2 * f->cvert : 64));
            GROW(f->vert, f->nvert, f->cvert);
            f->vrec[f->nvert] = i;
            f->vert[f->nvert++] = v;
            f->b[cur].nv++;
            break;
        case 'G':
            if (cur >= 0 && r->n >= 3) {
                GROW(f->g, f->ng, f->cg);
                gedge_t g = { v[0], (int32_t)floordiv(v[1] - 0xD6B4, 8), (int32_t)floordiv(v[2] - 0xD6B4, 8) };
                f->g[f->ng++] = g;
                f->b[cur].ng++;
            }
            break;
        case 'L':
            if (cur >= 0) {
                GROW(f->li, f->nli, f->cli);
                f->li[f->nli++] = i;
                f->b[cur].nl++;
            }
            break;
        case 'E':
            if (r->n < 6) break;
            if (poly >= 0 && poly_nclip == 2 && !f->p[poly].nj) {
                GROW(f->j, f->nj, f->cj);
                f->p[poly].j0 = f->nj;
                f->j[f->nj++] = parse_edge(v);
                f->p[poly].nj = 1;
            } else {
                GROW(f->e, f->ne, f->ce);
                f->e[f->ne++] = parse_edge(v);
            }
            break;
        case 'F':
            if (r->n < 12) break;
            if (!seen_fill) {
                seen_fill = 1;
                pend0 = f->ne;
                poly = -1;
                break;
            }
            GROW(f->p, f->np, f->cp);
            {
                poly_t p;
                memset(&p, 0, sizeof p);
                p.e0 = pend0; p.ne = f->ne - pend0;
                p.f = v;
                p.ri = -1;
                f->p[f->np++] = p;
            }
            pend0 = f->ne;
            poly = f->np - 1;
            poly_nclip = v[11];
            break;
        case 'R':
            if (poly < 0) break;
            {
                poly_t *p = &f->p[poly];
                p->r = v;
                p->ri = i;
                p->colour = v[3 + 2 * v[2]] & 0xFFFF;
                p->b0 = f->nbi; p->a0 = f->nai;
                fill_poly = poly;
                if (cur >= 0) {                       /* batches only advance: each one's polygons are a run of pl */
                    GROW(f->pl, f->npl, f->cpl);
                    f->pl[f->npl++] = poly;
                    f->b[cur].np++;
                }
                poly = -1;
            }
            break;
        case 'b':
            if (fill_poly >= 0) {
                GROW(f->bi, f->nbi, f->cbi);
                f->bi[f->nbi++] = i;
                f->p[fill_poly].nb++;
            }
            break;
        case 'a':
            if (fill_poly >= 0) {
                GROW(f->ai, f->nai, f->cai);
                f->ai[f->nai++] = i;
                f->p[fill_poly].na++;
            }
            break;
        case 'K':
            if (r->n >= 1) colour = v[0];
            break;
        case 'Q':
            if (r->n >= 4 && v[3] > 0) {
                int ord = 0;
                for (int k = 0; k < f->ns; k++) ord += f->s[k].colour == colour && f->s[k].mode == v[2];
                GROW(f->s, f->ns, f->cs);
                span_t s = { colour, v[2], ord, i };
                f->s[f->ns++] = s;
            }
            break;
        case 'N':
            if (r->n >= 7) {
                GROW(f->h, f->nh, f->ch);
                hud_t h = { colour, v[5], v[6], i };
                f->h[f->nh++] = h;
            }
            break;
        default:
            break;
        }
    }
    return f;
}

void interp_frame_free(interp_frame *f)
{
    if (!f) return;
    free(f->b); free(f->vert); free(f->vrec); free(f->g); free(f->e); free(f->j); free(f->p);
    free(f->bi); free(f->ai); free(f->li); free(f->pl); free(f->s); free(f->h);
    free(f);
}

/* polygon k of a batch, and its index in the frame */
#define BATCH_PI(f, bt, k) ((f)->pl[(bt)->p0 + (k)])
#define BATCH_POLY(f, bt, k) (&(f)->p[BATCH_PI(f, bt, k)])

static const int32_t *vertex_of(const interp_frame *f, const batch_t *b, int32_t slot, int end)
{
    for (int k = b->ng - 1; k >= 0; k--) {          /* the last G record of a slot counts */
        const gedge_t *g = &f->g[b->g0 + k];
        if (g->slot != slot) continue;
        int idx = end ? g->k1 : g->k0;
        if (idx >= b->nv) return NULL;
        if (idx < 0) idx += b->nv;                  /* Python's index from the end */
        return idx < 0 ? NULL : f->vert[b->v0 + idx];
    }
    return NULL;
}

static const gedge_t *gedge_of(const interp_frame *f, const batch_t *b, int32_t slot)
{
    for (int k = b->ng - 1; k >= 0; k--)
        if (f->g[b->g0 + k].slot == slot) return &f->g[b->g0 + k];
    return NULL;
}

/* ---- the original's edge walk (drawlist_spans.Spans) ------------------- */

#define SPAN_OFF 2048
#define SPAN_N 4096

typedef struct {
    int left[SPAN_N], right[SPAN_N];
    int lo, hi;                       /* the rows touched */
    int top, ymin, ymax;
    int flags, lmin, lmax, rmin, rmax, drew, nclip;
} spans_t;

static void spans_init(spans_t *s, int ymin, int ymax)
{
    static int ready;
    if (!ready) {
        for (int k = 0; k < SPAN_N; k++) { s->left[k] = NOLEFT; s->right[k] = NORIGHT; }
        ready = 1;
    } else
        for (int k = s->lo; k <= s->hi; k++) { s->left[k] = NOLEFT; s->right[k] = NORIGHT; }
    s->lo = SPAN_N; s->hi = -1;
    s->top = NOLEFT;
    s->ymin = ymin; s->ymax = ymax;
    s->flags = 0;
    s->lmin = NOLEFT; s->lmax = -0x8000; s->rmin = NOLEFT; s->rmax = -0x8000;
    s->drew = s->nclip = 0;
}

static int row_ok(spans_t *s, int y)
{
    const int k = y + SPAN_OFF;
    if (k < 0 || k >= SPAN_N) return -1;
    if (k < s->lo) s->lo = k;
    if (k > s->hi) s->hi = k;
    return k;
}

static void widen_left(spans_t *s, int y, int x)
{
    const int k = row_ok(s, y);
    if (k >= 0 && x < s->left[k]) s->left[k] = x;
}

static void widen_right(spans_t *s, int y, int x)
{
    const int k = row_ok(s, y);
    if (k >= 0 && x > s->right[k]) s->right[k] = x;
}

static void span_edge(spans_t *s, int x0, int y0, int x1, int y1)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    int y = y0;
    const int dx = s16(x1 - x0);
    int step, ylo, yhi;
    if (y1 >= y0) { step = 1; ylo = y0; yhi = y1; } else { step = -1; ylo = y1; yhi = y0; }
    /* the whole edge is dropped, not clipped */
    if (ylo < s->ymin || ylo > s->ymax || yhi < s->ymin || yhi > s->ymax) return;
    if (ylo < s->top) s->top = ylo;
    const int dy = s16(yhi - ylo);
    int x = x0;
    if (dy > dx) {                                    /* steep: one row a step */
        int cx = dy + 1;
        int err = s16(-(int64_t)floordiv(cx, 2));
        for (;;) {
            widen_left(s, y, x);
            widen_right(s, y, x);
            err = s16(err + dx);
            if (err >= 0) {
                if (--cx == 0) return;
                err = s16(err - dy);
                x++;
                y += step;
            } else {
                y += step;
                if (--cx == 0) return;
            }
        }
    }
    int cx = dx + 1;                                  /* shallow: several x a row */
    int err = s16(-(int64_t)floordiv(cx, 2));
    for (;;) {
        widen_left(s, y, x);
        for (;;) {
            err = s16(err + dy);
            if (err >= 0) break;
            x++;
            if (--cx == 0) { widen_right(s, y, x - 1); return; }
        }
        widen_right(s, y, x);
        err = s16(err - dx);
        if (--cx == 0) return;
        x++;
        y += step;
    }
}

static void side(spans_t *s, int left, int a, int b)
{
    if (b < a) { int t = a; a = b; b = t; }
    if (left) {
        if (b > s->lmax) s->lmax = b;
        if (a < s->lmin) s->lmin = a;
    } else {
        if (b > s->rmax) s->rmax = b;
        if (a < s->rmin) s->rmin = a;
    }
}

/* 1377:07E8: one vertical run at column x over rows a..b (borders) */
static void run(spans_t *s, int x, int a, int b)
{
    if (b < a) { int t = a; a = b; b = t; }
    if (b <= s->ymin || a >= s->ymax) return;
    if (a < s->ymin) a = s->ymin;
    if (b > s->ymax) b = s->ymax;
    const int n = b - a;
    if (!n) return;
    if (a < s->top) s->top = a;
    for (int y = a; y <= a + n; y++) { widen_left(s, y, x); widen_right(s, y, x); }
}

static void poly_edge(spans_t *s, const edge_t *e)
{
    s->flags |= e->st;
    if (e->st & 0x40) s->nclip++;
    const int lo = e->st & 0xFF;
    if (lo & 0x80) {                                  /* rejected: borders only */
        if (lo & 3) side(s, lo & 2, e->y0hi, e->y1hi);
        return;
    }
    s->drew |= 4;
    span_edge(s, s16(e->x0), s16(e->y0), s16(e->x1), s16(e->y1));
    if (lo & 3) side(s, lo & 2, s16(e->y0), e->y0hi);
    if (lo & 0x0C) side(s, lo & 8, s16(e->y1), e->y1hi);
}

/* The span rows of a polygon from edges by the original's rules: the top
 * row and the rows (left, right) into rows[] (at most 2 * SPAN_N values). */
static int poly_rows(spans_t *s, const poly_t *p, const edge_t *edges, int ne, const edge_t *join, int nj,
                     int *top, int *rows)
{
    const int xmin = p->f[1], ymin = p->f[2], xmax = p->f[3], ymax = p->f[4];
    spans_init(s, ymin, ymax);
    for (int k = 0; k < ne; k++) poly_edge(s, &edges[k]);
    for (int k = 0; k < nj; k++) poly_edge(s, &join[k]);
    const int fl = s->flags;
    if (s->drew) {
        if (fl & 5) run(s, xmax + 1, s->rmax, s->rmin);
        if (fl & 10) run(s, xmin - 1, s->lmax, s->lmin);
    } else if ((fl & 5) && (fl & 10)) {
        run(s, xmax + 1, s->rmax, s->rmin);
        run(s, xmin - 1, s->lmax, s->lmin);
    }
    *top = s->top;
    int n = 0;
    if (s->top != NOLEFT)
        for (int y = s->top;; y++) {
            const int k = y + SPAN_OFF;
            if (k < 0 || k >= SPAN_N || (s->left[k] == NOLEFT && s->right[k] == NORIGHT)) break;
            rows[2 * n] = s->left[k];
            rows[2 * n + 1] = s->right[k];
            n++;
        }
    return n;
}

/* ---- pairing ------------------------------------------------------------ */

/* The original's model_project (VGAME 0x129A2) for a camera-space vertex:
 * 0 behind the eye or when the divide would fault. */
static int idiv(int64_t n, int64_t d, int64_t *q)
{
    if (!d) return 0;
    int64_t a = llabs(n) / llabs(d);
    a = (n < 0) == (d < 0) ? a : -a;
    if (a < -32768 || a > 32767) return 0;
    *q = a;
    return 1;
}

static int project(const int64_t xf[3], int64_t out[2])
{
    const int64_t x = xf[0], y = xf[1], z = xf[2];
    const int zhi = s16(z >> 16);
    if (zhi >= 0x100) return idiv(x >> 8, zhi, &out[0]) && idiv(y >> 8, zhi, &out[1]);
    if (zhi >= 1) {
        const int64_t d = ((z >> 8) & 0xFFFF) >> 1;
        return idiv(x >> 1, d, &out[0]) && idiv(y >> 1, d, &out[1]);
    }
    return 0;
}

static int vertex_distance(const int32_t *va, const int32_t *vb)
{
    if (va[5] == 2 || vb[5] == 2) return -1;
    const int dx = abs(va[3] - vb[3]), dy = abs(va[4] - vb[4]);
    return dx > dy ? dx : dy;
}

/* mean screen motion of the first min(n) vertices; 0 when nothing compares */
static int batch_distance(const interp_frame *fa, const batch_t *a, const interp_frame *fb, const batch_t *b, double *d)
{
    int64_t sum = 0;
    int n = 0;
    for (int k = 0; k < a->nv && k < b->nv; k++) {
        const int x = vertex_distance(fa->vert[a->v0 + k], fb->vert[b->v0 + k]);
        if (x >= 0) { sum += x; n++; }
    }
    if (!n) return 0;
    *d = (double)sum / (double)n;
    return 1;
}

static int compatible(const interp_frame *fa, const batch_t *a, const interp_frame *fb, const batch_t *b)
{
    if (a->nv == b->nv) return 1;
    const int lo = a->nv < b->nv ? a->nv : b->nv, hi = a->nv < b->nv ? b->nv : a->nv;
    int lim = (int)(MAX_COUNT_DIFF * hi);
    if (lim < 1) lim = 1;
    if (lo < 3 || abs(a->nv - b->nv) > lim) return 0;
    int near = 0;
    for (int k = 0; k < lo; k++) {
        const int32_t *va = fa->vert[a->v0 + k], *vb = fb->vert[b->v0 + k];
        int64_t z = llabs((int64_t)va[2]);
        if (llabs((int64_t)vb[2]) > z) z = llabs((int64_t)vb[2]);
        if (z < (1 << 16)) z = 1 << 16;
        int64_t m = 0;
        for (int c = 0; c < 3; c++) {
            const int64_t d = llabs((int64_t)va[c] - vb[c]);
            if (d > m) m = d;
        }
        near += m <= z / 4;
    }
    return near >= 0.7 * lo;
}

struct interp_pairing {
    const interp_frame *A, *B;
    int *bpair;  int nbpair;          /* (i, j) */
    int *ppair;  int nppair;          /* (polygon of A, polygon of B, batch of A, batch of B) */
    int *pa_of, *pb_of;               /* polygon -> its pair's index in ppair, or -1 */
    char *ba_paired, *bb_paired;
    int *bmap_ab, *bmap_ba;           /* batch -> paired batch, or -1 */
};

static int score(const interp_frame *A, int i, const interp_frame *B, int j, double *s)
{
    if (!compatible(A, &A->b[i], B, &B->b[j])) return 0;
    double d = 0;
    const int have = batch_distance(A, &A->b[i], B, &B->b[j], &d);
    if (have && d > MAX_MOTION) return 0;
    *s = 1000.0 - 100.0 * abs(A->b[i].nv - B->b[j].nv) - (have ? d : 0.0);
    return 1;
}

static int poly_key_equal(const interp_frame *A, const poly_t *pa, const interp_frame *B, const poly_t *pb)
{
    if (pa->ne != pb->ne) return 0;
    for (int k = 0; k < pa->ne; k++)
        if (A->e[pa->e0 + k].slot != B->e[pb->e0 + k].slot) return 0;
    return 1;
}

interp_pairing *interp_pair(const interp_frame *A, const interp_frame *B)
{
    interp_pairing *P = calloc(1, sizeof *P);
    P->A = A; P->B = B;
    const int na = A->nb, nb = B->nb;
    /* batches: the longest common subsequence of compatible batches */
    double *best = malloc((size_t)(na + 1) * (nb + 1) * sizeof *best);
#define BEST(i, j) best[(size_t)(i) * (nb + 1) + (j)]
    for (int i = 0; i <= na; i++) BEST(i, nb) = 0.0;
    for (int j = 0; j <= nb; j++) BEST(na, j) = 0.0;
    for (int i = na - 1; i >= 0; i--)
        for (int j = nb - 1; j >= 0; j--) {
            double s, v = -1;
            if (score(A, i, B, j, &s)) v = BEST(i + 1, j + 1) + s;
            double m = BEST(i + 1, j);
            if (BEST(i, j + 1) > m) m = BEST(i, j + 1);
            if (v > m) m = v;
            BEST(i, j) = m;
        }
    P->bpair = malloc(sizeof(int) * 2 * (size_t)(na < nb ? na + 1 : nb + 1));
    for (int i = 0, j = 0; i < na && j < nb;) {
        double s;
        if (score(A, i, B, j, &s) && fabs(BEST(i, j) - (BEST(i + 1, j + 1) + s)) < 1e-9) {
            P->bpair[2 * P->nbpair] = i;
            P->bpair[2 * P->nbpair + 1] = j;
            P->nbpair++;
            i++; j++;
        } else if (BEST(i + 1, j) >= BEST(i, j + 1)) i++;
        else j++;
    }
#undef BEST
    free(best);
    P->pa_of = malloc(sizeof(int) * (size_t)(A->np + 1));
    P->pb_of = malloc(sizeof(int) * (size_t)(B->np + 1));
    for (int k = 0; k < A->np; k++) P->pa_of[k] = -1;
    for (int k = 0; k < B->np; k++) P->pb_of[k] = -1;
    P->ba_paired = calloc((size_t)na + 1, 1);
    P->bb_paired = calloc((size_t)nb + 1, 1);
    P->bmap_ab = malloc(sizeof(int) * (size_t)(na + 1));
    P->bmap_ba = malloc(sizeof(int) * (size_t)(nb + 1));
    for (int k = 0; k < na; k++) P->bmap_ab[k] = -1;
    for (int k = 0; k < nb; k++) P->bmap_ba[k] = -1;
    int cap = 0;
    for (int q = 0; q < P->nbpair; q++) {
        const int i = P->bpair[2 * q], j = P->bpair[2 * q + 1];
        P->ba_paired[i] = P->bb_paired[j] = 1;
        P->bmap_ab[i] = j; P->bmap_ba[j] = i;
        const batch_t *ba = &A->b[i], *bb = &B->b[j];
        /* polygons inside: by their edge slots (LCS, in order) */
        const int pa = ba->np, pb = bb->np;
        int *lcs = malloc(sizeof(int) * (size_t)(pa + 1) * (pb + 1));
#define L(x, y) lcs[(size_t)(x) * (pb + 1) + (y)]
        for (int x = 0; x <= pa; x++) L(x, pb) = 0;
        for (int y = 0; y <= pb; y++) L(pa, y) = 0;
        for (int x = pa - 1; x >= 0; x--)
            for (int y = pb - 1; y >= 0; y--) {
                int m = L(x + 1, y) > L(x, y + 1) ? L(x + 1, y) : L(x, y + 1);
                const int v = poly_key_equal(A, BATCH_POLY(A, ba, x), B, BATCH_POLY(B, bb, y)) ? L(x + 1, y + 1) + 1 : -1;
                L(x, y) = v > m ? v : m;
            }
        for (int x = 0, y = 0; x < pa && y < pb;) {
            if (poly_key_equal(A, BATCH_POLY(A, ba, x), B, BATCH_POLY(B, bb, y)) && L(x, y) == L(x + 1, y + 1) + 1) {
                if (P->nppair == cap) {           /* four ints a pair */
                    cap = cap ? 2 * cap : 256;
                    P->ppair = realloc(P->ppair, sizeof(int) * 4 * (size_t)cap);
                }
                const int ia = BATCH_PI(A, ba, x), ib = BATCH_PI(B, bb, y);
                P->ppair[4 * P->nppair] = ia; P->ppair[4 * P->nppair + 1] = ib;
                P->ppair[4 * P->nppair + 2] = i; P->ppair[4 * P->nppair + 3] = j;
                P->pa_of[ia] = P->nppair; P->pb_of[ib] = P->nppair;
                P->nppair++;
                x++; y++;
            } else if (L(x + 1, y) >= L(x, y + 1)) x++;
            else y++;
        }
#undef L
        free(lcs);
    }
    return P;
}

void interp_pairing_free(interp_pairing *p)
{
    if (!p) return;
    free(p->bpair); free(p->ppair); free(p->pa_of); free(p->pb_of);
    free(p->ba_paired); free(p->bb_paired); free(p->bmap_ab); free(p->bmap_ba);
    free(p);
}

/* why batch b of frame fr has no pair in the other (a cause index) */
static int why(const interp_pairing *P, int in_a, int bi)
{
    const interp_frame *fr = in_a ? P->A : P->B, *other = in_a ? P->B : P->A;
    const batch_t *b = &fr->b[bi];
    int same = 0, near = 0;
    for (int k = 0; k < other->nb; k++) {
        if (!compatible(fr, b, other, &other->b[k])) continue;
        same++;
        double d;
        if (!batch_distance(fr, b, other, &other->b[k], &d) || d <= MAX_MOTION) near++;
    }
    if (!same) return b->nv == 1 ? 0 : 1;
    if (!near) return 2;
    return 3;
}

void interp_pair_causes(const interp_pairing *P, interp_causes *c)
{
    memset(c, 0, sizeof *c);
    for (int side_a = 1; side_a >= 0; side_a--) {
        const interp_frame *fr = side_a ? P->A : P->B;
        const char *bp = side_a ? P->ba_paired : P->bb_paired;
        const int *of = side_a ? P->pa_of : P->pb_of;
        for (int k = 0; k < fr->nb; k++) {
            const batch_t *b = &fr->b[k];
            c->batches++;
            c->batches_paired += bp[k] != 0;
            for (int q = 0; q < b->np; q++) {
                c->polys++;
                if (of[BATCH_PI(fr, b, q)] >= 0) c->polys_paired++;
                else if (!bp[k]) c->cause[why(P, side_a, k)]++;
                else c->cause[4]++;
            }
        }
    }
}

int interp_pair_motion(const interp_pairing *P, int **motion)
{
    int n = 0, cap = 0;
    *motion = NULL;
    for (int q = 0; q < P->nbpair; q++) {
        const batch_t *a = &P->A->b[P->bpair[2 * q]], *b = &P->B->b[P->bpair[2 * q + 1]];
        for (int k = 0; k < a->nv && k < b->nv; k++) {
            const int d = vertex_distance(P->A->vert[a->v0 + k], P->B->vert[b->v0 + k]);
            if (d < 0) continue;
            GROW(*motion, n, cap);
            (*motion)[n++] = d;
        }
    }
    return n;
}

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

int interp_is_cut(const interp_pairing *P)
{
    interp_causes c;
    interp_pair_causes(P, &c);
    if (c.polys && (double)c.polys_paired / (double)c.polys < CUT_PAIRED) return 1;
    int *m, n = interp_pair_motion(P, &m);
    int cut = 0;
    if (n) {
        qsort(m, (size_t)n, sizeof *m, cmp_int);
        const int at = (int)(0.5 * n);
        cut = m[at < n - 1 ? at : n - 1] > CUT_MOTION;
    }
    free(m);
    if (cut) return 2;
    if (!P->nbpair && (P->A->nb || P->B->nb)) return 3;
    return 0;
}

/* ---- in-between frames -------------------------------------------------- */

/* A record of the list: its values are the skeleton's (off -1) or a
 * regenerated record's, copied into the pool at off; the pool may move while
 * the list grows, so the pointers are set when it is complete. */
static void out_rec(interp_list *l, char kind, uint64_t icount, const int32_t *v, int n, int off)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? 2 * l->cap : 1024;
        l->rec = realloc(l->rec, (size_t)l->cap * sizeof *l->rec);
        l->off = realloc(l->off, (size_t)l->cap * sizeof *l->off);
    }
    interp_rec r = { kind, icount, v, n };
    l->off[l->n] = off;
    l->rec[l->n++] = r;
}

static void out_pooled(interp_list *l, char kind, uint64_t icount, const int32_t *v, int n)
{
    while (l->pool_n + n > l->pool_cap) {
        l->pool_cap = l->pool_cap ? 2 * l->pool_cap : 1 << 16;
        l->pool = realloc(l->pool, (size_t)l->pool_cap * sizeof *l->pool);
    }
    memcpy(l->pool + l->pool_n, v, (size_t)n * sizeof *v);
    out_rec(l, kind, icount, NULL, n, l->pool_n);
    l->pool_n += n;
}

static void out_resolve(interp_list *l)
{
    for (int i = 0; i < l->n; i++)
        if (l->off[i] >= 0) l->rec[i].v = l->pool + l->off[i];
}

void interp_list_free(interp_list *l)
{
    free(l->rec); free(l->off); free(l->pool);
    memset(l, 0, sizeof *l);
}

static edge_t interp_edge(const edge_t *ea, const edge_t *eb, double w, const interp_frame *fa, const batch_t *ba,
                          const interp_frame *fb, const batch_t *bb, const int32_t *vp, int camera, uint64_t *st)
{
    edge_t e = *ea;
    const int xmin = vp[0], ymin = vp[1], xmax = vp[2], ymax = vp[3];
    const int lo = ea->st & 0xFF;
    if (camera && lo == 0) {
        const int32_t *va0 = vertex_of(fa, ba, ea->slot, 0), *va1 = vertex_of(fa, ba, ea->slot, 1);
        const int32_t *vb0 = vertex_of(fb, bb, eb->slot, 0), *vb1 = vertex_of(fb, bb, eb->slot, 1);
        const gedge_t *ga = gedge_of(fa, ba, ea->slot), *gb = gedge_of(fb, bb, eb->slot);
        if (va0 && va1 && vb0 && vb1 && va0[5] != 2 && va1[5] != 2 && vb0[5] != 2 && vb1[5] != 2 &&
            ga->k0 == gb->k0 && ga->k1 == gb->k1) {
            const int ox = s16(ea->x0) - s16(va0[3]), oy = s16(ea->y0) - s16(va0[4]);
            const int ox2 = s16(eb->x0) - s16(vb0[3]), oy2 = s16(eb->y0) - s16(vb0[4]);
            if (ox == ox2 && oy == oy2 && s16(ea->x1) - s16(va1[3]) == ox && s16(ea->y1) - s16(va1[4]) == oy &&
                s16(eb->x1) - s16(vb1[3]) == ox && s16(eb->y1) - s16(vb1[4]) == oy) {
                int64_t got[2][2];
                int ok = 1;
                const int32_t *ends[2][2] = { { va0, vb0 }, { va1, vb1 } };
                for (int k = 0; k < 2 && ok; k++) {
                    int64_t xf[3];
                    for (int c = 0; c < 3; c++) xf[c] = rnd(lerp(ends[k][0][c], ends[k][1][c], w));
                    ok = project(xf, got[k]);
                }
                if (ok) {
                    const int64_t xs[2] = { got[0][0] + ox, got[1][0] + ox }, ys[2] = { got[0][1] + oy, got[1][1] + oy };
                    if (xs[0] >= xmin && xs[0] <= xmax && xs[1] >= xmin && xs[1] <= xmax &&
                        ys[0] >= ymin && ys[0] <= ymax && ys[1] >= ymin && ys[1] <= ymax) {
                        e.x0 = (int32_t)(xs[0] & 0xFFFF); e.y0 = (int32_t)(ys[0] & 0xFFFF);
                        e.x1 = (int32_t)(xs[1] & 0xFFFF); e.y1 = (int32_t)(ys[1] & 0xFFFF);
                        st[IS_EDGES_CAMERA]++;
                        return e;
                    }
                }
            }
        }
        st[IS_EDGES_FELL_BACK]++;
    }
    e.x0 = (int32_t)(rnd(lerp(s16(ea->x0), s16(eb->x0), w)) & 0xFFFF);
    e.y0 = (int32_t)(rnd(lerp(s16(ea->y0), s16(eb->y0), w)) & 0xFFFF);
    e.x1 = (int32_t)(rnd(lerp(s16(ea->x1), s16(eb->x1), w)) & 0xFFFF);
    e.y1 = (int32_t)(rnd(lerp(s16(ea->y1), s16(eb->y1), w)) & 0xFFFF);
    e.y0hi = (int32_t)rnd(lerp(ea->y0hi, eb->y0hi, w));
    e.y1hi = (int32_t)rnd(lerp(ea->y1hi, eb->y1hi, w));
    st[IS_EDGES_SCREEN]++;
    return e;
}

/* polygon pa (the skeleton's) a fraction w of the way to pb: its rows, or -1 held */
static int interp_poly(const interp_frame *fa, const poly_t *pa, const batch_t *ba, const interp_frame *fb,
                       const poly_t *pb, const batch_t *bb, double w, spans_t *sp, int *top, int *rows, uint64_t *st)
{
    const int style = pa->colour & 0xFF00;
    if (style != 0xFF00 && style != 0xFE00 && style != 0xFD00 && style != 0xFB00) { st[IS_HELD_STYLE]++; return -1; }
    int same = pa->ne == pb->ne && pa->nj == pb->nj;
    for (int k = 0; same && k < pa->ne; k++) same = fa->e[pa->e0 + k].st == fb->e[pb->e0 + k].st;
    for (int k = 0; same && k < pa->nj; k++)
        same = fa->j[pa->j0 + k].slot == fb->j[pb->j0 + k].slot && fa->j[pa->j0 + k].st == fb->j[pb->j0 + k].st;
    if (!same) { st[IS_HELD_CLIP]++; return -1; }
    for (int k = 0; k < pa->ne; k++) {
        const edge_t *ea = &fa->e[pa->e0 + k], *eb = &fb->e[pb->e0 + k];
        if (ea->st & 0x80) continue;
        int m = abs(s16(ea->x0) - s16(eb->x0));
        const int d[3] = { abs(s16(ea->y0) - s16(eb->y0)), abs(s16(ea->x1) - s16(eb->x1)), abs(s16(ea->y1) - s16(eb->y1)) };
        for (int c = 0; c < 3; c++) if (d[c] > m) m = d[c];
        if (m > MAX_EDGE_MOTION) { st[IS_HELD_EDGE_MOTION]++; return -1; }
    }
    static edge_t edges[1024], join[16];
    if (pa->ne > 1024 || pa->nj > 16) { st[IS_HELD_CLIP]++; return -1; }
    for (int k = 0; k < pa->ne; k++)
        edges[k] = interp_edge(&fa->e[pa->e0 + k], &fb->e[pb->e0 + k], w, fa, ba, fb, bb, pa->f + 1, 1, st);
    for (int k = 0; k < pa->nj; k++)
        join[k] = interp_edge(&fa->j[pa->j0 + k], &fb->j[pb->j0 + k], w, fa, ba, fb, bb, pa->f + 1, 0, st);
    return poly_rows(sp, pa, edges, pa->ne, join, pa->nj, top, rows);
}

/* the most common row base of the frame's fill rows (the first on a tie) */
static int rowbase_of(const interp_frame *f)
{
    int best = 0, best_n = 0;
    for (int k = 0; k < f->nbi; k++) {
        const int32_t *v = f->rec[f->bi[k]].v;
        const int base = v[4] - v[1] - 320 * v[0];
        int seen = 0, n = 0;
        for (int q = 0; q < f->nbi; q++) {
            const int32_t *u = f->rec[f->bi[q]].v;
            if (u[4] - u[1] - 320 * u[0] != base) continue;
            if (q < k) { seen = 1; break; }
            n++;
        }
        if (!seen && n > best_n) { best = base; best_n = n; }
    }
    return best;
}

int interp_inbetween(const interp_frame *A, const interp_frame *B, const interp_pairing *P, double t,
                     interp_list *out, int *skel_b, uint64_t *stats)
{
    return interp_inbetween_ex(A, B, P, t, 0, out, skel_b, stats);
}

int interp_inbetween_ex(const interp_frame *A, const interp_frame *B, const interp_pairing *P, double t, int flags,
                        interp_list *out, int *skel_b, uint64_t *stats)
{
    static uint64_t scratch[IS_COUNT];
    uint64_t *st = stats ? stats : scratch;
    out->n = 0;
    out->pool_n = 0;
    const int use_b = !(t < 0.5);
    const interp_frame *S = use_b ? B : A, *O = use_b ? A : B;
    const double w = use_b ? 1 - t : t;
    *skel_b = use_b;
    if (t <= 0 || (t >= 1 && !(flags & INTERP_EXTRAPOLATE)) || t == 1) {
        for (int i = 0; i < S->nrec; i++) out_rec(out, S->rec[i].kind, S->rec[i].icount, S->rec[i].v, S->rec[i].n, -1);
        return out->n;
    }
    /* what replaces each record of the skeleton: -1 keep, -2 drop, else the first of the records written for it */
    int *rep = malloc(sizeof(int) * (size_t)(S->nrec + 1));
    int *repn = malloc(sizeof(int) * (size_t)(S->nrec + 1));
    for (int i = 0; i < S->nrec; i++) rep[i] = -1;
    interp_list made;                 /* the regenerated records, in the order made */
    memset(&made, 0, sizeof made);
    const int *of = use_b ? P->pb_of : P->pa_of;
    const int base = rowbase_of(S);
    static spans_t sp;
    static int rows[2 * SPAN_N];
    static int32_t vals[8 + 2 * SPAN_N];
    for (int bi = 0; bi < S->nb; bi++) {
        const batch_t *b = &S->b[bi];
        for (int q = 0; q < b->np; q++) {
            const int pi = BATCH_PI(S, b, q);
            const poly_t *p = &S->p[pi];
            const int pr = of[pi];
            if (pr < 0) { st[IS_HELD_UNPAIRED]++; continue; }
            const int *pp = &P->ppair[4 * pr];
            const int oi = use_b ? pp[0] : pp[1];
            const batch_t *ob = &O->b[use_b ? pp[2] : pp[3]];
            int top;
            const int nrows = interp_poly(S, p, b, O, &O->p[oi], ob, w, &sp, &top, rows, st);
            if (nrows < 0) continue;
            if (!nrows || p->r[2] == 0) { st[IS_HELD_NOTHING]++; continue; }
            int mine = base;
            if (p->nb) {
                const int32_t *v = S->rec[S->bi[p->b0]].v;
                mine = v[4] - v[1] - 320 * v[0];
            }
            /* the 'R' record and the 'b' records, in the original's order */
            const int32_t *r = p->r;
            const int n0 = r[2];
            const uint64_t ic = S->rec[p->ri].icount;
            const int first = made.n;
            int n = 0;
            vals[n++] = r[0]; vals[n++] = top; vals[n++] = nrows;
            for (int k = 0; k < 2 * nrows; k++) vals[n++] = rows[k];
            vals[n++] = r[3 + 2 * n0]; vals[n++] = r[4 + 2 * n0];
            out_pooled(&made, 'R', ic, vals, n);
            const int xmin = p->f[1], xmax = p->f[3], es = r[4 + 2 * n0];
            for (int k = 0; k < nrows; k++) {
                const int l = rows[2 * k], rt = rows[2 * k + 1];
                const int x0 = l > xmin ? l : xmin, x1 = rt < xmax ? rt : xmax;
                if (x0 > x1 || l == NOLEFT) continue;
                const int y = top + k;
                const int32_t bv[5] = { y, x0, x1 - x0 + 1, es, mine + 320 * y + x0 };
                out_pooled(&made, 'b', ic, bv, 5);
            }
            st[IS_REGENERATED] += (uint64_t)(made.n - first);
            rep[p->ri] = first;
            repn[p->ri] = made.n - first;
            for (int k = 0; k < p->nb; k++) rep[S->bi[p->b0 + k]] = -2;
            for (int k = 0; k < p->na; k++) rep[S->ai[p->a0 + k]] = -2;
            st[IS_POLYS_MOVED]++;
        }
    }
    for (int k = 0; k < S->ns; k++) {
        const span_t *s = &S->s[k];
        st[IS_SPANS]++;
        int j = -1;
        for (int q = 0; q < O->ns; q++)
            if (O->s[q].colour == s->colour && O->s[q].mode == s->mode && O->s[q].ord == s->ord) { j = O->s[q].ri; break; }
        if (j < 0) { st[IS_HELD_SPAN_NO_PAIR]++; continue; }
        const int32_t *va = S->rec[s->ri].v, *vb = O->rec[j].v;
        const int na = va[3], nb = vb[3];
        int64_t ya = rnd(lerp(va[0], vb[0], w)), yb = rnd(lerp(va[1], vb[1], w));
        /* Between two fills every row lies between theirs; carried past them it
         * can leave the screen by any amount, so a prediction keeps to it. */
        const int clamp = (flags & INTERP_EXTRAPOLATE) && w < 0;
        if (clamp) { if (ya < 0) ya = 0; if (yb > 199) yb = 199; }
        if (yb < ya || yb - ya + 1 > SPAN_N) { st[IS_HELD_SPAN]++; continue; }
        int n = 0;
        vals[n++] = (int32_t)ya; vals[n++] = (int32_t)yb; vals[n++] = va[2]; vals[n++] = (int32_t)(yb - ya + 1);
        for (int64_t y = ya; y <= yb; y++) {
            int64_t ra = y - va[0], rb = y - vb[0];
            if (ra < 0) ra = 0;
            if (ra > na - 1) ra = na - 1;
            if (rb < 0) rb = 0;
            if (rb > nb - 1) rb = nb - 1;
            int64_t l = rnd(lerp(va[4 + 2 * ra], vb[4 + 2 * rb], w)), r = rnd(lerp(va[5 + 2 * ra], vb[5 + 2 * rb], w));
            if (clamp) { l = l < 0 ? 0 : l > 319 ? 319 : l; r = r < 0 ? 0 : r > 319 ? 319 : r; }
            vals[n++] = (int32_t)l;
            vals[n++] = (int32_t)r;
        }
        for (int q = 4 + 2 * na; q < S->rec[s->ri].n; q++) vals[n++] = va[q];
        rep[s->ri] = made.n;
        repn[s->ri] = 1;
        out_pooled(&made, 'Q', S->rec[s->ri].icount, vals, n);
        st[IS_SPANS_MOVED]++;
        st[IS_REGENERATED]++;
    }
    /* HUD lines: groups by colour, page and origin */
    static int ia[4096], ib[4096];
    for (int k = 0; k < S->nh; k++) {
        const hud_t *h = &S->h[k];
        int first = 1;
        for (int q = 0; q < k; q++)
            if (S->h[q].colour == h->colour && S->h[q].page == h->page && S->h[q].origin == h->origin) { first = 0; break; }
        if (!first) continue;
        int na = 0, nb = 0;
        for (int q = k; q < S->nh && na < 4096; q++)
            if (S->h[q].colour == h->colour && S->h[q].page == h->page && S->h[q].origin == h->origin) ia[na++] = S->h[q].ri;
        for (int q = 0; q < O->nh && nb < 4096; q++)
            if (O->h[q].colour == h->colour && O->h[q].page == h->page && O->h[q].origin == h->origin) ib[nb++] = O->h[q].ri;
        st[IS_HUD] += (uint64_t)na;
        if (na != nb) { st[IS_HELD_HUD_GROUP] += (uint64_t)na; continue; }
        for (int q = 0; q < na; q++) {
            const interp_rec *ra = &S->rec[ia[q]];
            const int32_t *a = ra->v, *bv = O->rec[ib[q]].v;
            int m = 0;
            for (int c = 0; c < 4; c++) if (abs(a[c] - bv[c]) > m) m = abs(a[c] - bv[c]);
            if (m > MAX_HUD_MOTION) { st[IS_HELD_HUD_MOTION]++; continue; }
            int n = 0;
            for (int c = 0; c < 4; c++) vals[n++] = (int32_t)rnd(lerp(a[c], bv[c], w));
            for (int c = 4; c < ra->n; c++) vals[n++] = a[c];
            rep[ia[q]] = made.n;
            repn[ia[q]] = 1;
            out_pooled(&made, 'N', ra->icount, vals, n);
            st[IS_HUD_MOVED]++;
            st[IS_REGENERATED]++;
        }
    }
    /* outline edges: by batch, slot and colour */
    const int *bmap = use_b ? P->bmap_ba : P->bmap_ab;
    for (int bi = 0; bi < S->nb; bi++) {
        const batch_t *b = &S->b[bi];
        const batch_t *ob = bmap[bi] >= 0 ? &O->b[bmap[bi]] : NULL;
        for (int q = 0; q < b->nl; q++) {
            const int i = S->li[b->l0 + q];
            const int32_t *v = S->rec[i].v;
            st[IS_OUTLINE]++;
            if (!ob) { st[IS_HELD_OUTLINE_BATCH]++; continue; }
            int seen = 0;
            for (int x = 0; x < q; x++) {
                const int32_t *u = S->rec[S->li[b->l0 + x]].v;
                seen += u[0] == v[0] && u[5] == v[5];
            }
            int match = -1, c = 0;
            for (int x = 0; x < ob->nl; x++) {
                const int32_t *u = O->rec[O->li[ob->l0 + x]].v;
                if (u[0] == v[0] && u[5] == v[5] && c++ == seen) { match = O->li[ob->l0 + x]; break; }
            }
            if (match < 0) { st[IS_HELD_OUTLINE_NO_PAIR]++; continue; }
            const int32_t *u = O->rec[match].v;
            int n = 0;
            for (int k = 0; k < S->rec[i].n; k++) vals[n++] = k >= 1 && k <= 4 ? (int32_t)rnd(lerp(v[k], u[k], w)) : v[k];
            rep[i] = made.n;
            repn[i] = 1;
            out_pooled(&made, 'L', S->rec[i].icount, vals, n);
            st[IS_OUTLINE_MOVED]++;
            st[IS_REGENERATED]++;
        }
    }
    /* the vertices of paired batches of equal size, for a finer grid */
    if (flags & INTERP_VERTICES)
        for (int bi = 0; bi < S->nb; bi++) {
            const batch_t *b = &S->b[bi];
            if (bmap[bi] < 0 || O->b[bmap[bi]].nv != b->nv) continue;
            const batch_t *ob = &O->b[bmap[bi]];
            for (int k = 0; k < b->nv; k++) {
                const int i = S->vrec[b->v0 + k];
                const int32_t *va = S->vert[b->v0 + k], *vb = O->vert[ob->v0 + k];
                int64_t xf[3], px[2] = { 0, 0 };
                for (int c = 0; c < 3; c++) xf[c] = rnd(lerp(va[c], vb[c], w));
                if (xf[0] != (int32_t)xf[0] || xf[1] != (int32_t)xf[1] || xf[2] != (int32_t)xf[2]) continue;
                const int zhi = s16(xf[2] >> 16);
                const int range = zhi >= 0x100 ? 0 : zhi >= 1 ? 1 : 2;
                if (range != 2 && !project(xf, px)) continue;
                const int32_t nv[8] = { (int32_t)xf[0], (int32_t)xf[1], (int32_t)xf[2], (int32_t)px[0], (int32_t)px[1],
                                        range, va[6], va[7] };
                rep[i] = made.n;
                repn[i] = 1;
                out_pooled(&made, 'V', S->rec[i].icount, nv, 8);
            }
        }
    /* the present: the replay's own work page is the source, not bytes the original logged */
    for (int i = 0; i < S->nrec; i++) {
        const interp_rec *r = &S->rec[i];
        if (r->kind == 'D' && r->n > 5 && r->v[0] == 44 && rep[i] == -1) {
            rep[i] = made.n;
            repn[i] = 1;
            out_pooled(&made, 'D', r->icount, r->v, 5);
        }
    }
    /* the list: the skeleton with its replacements */
    for (int i = 0; i < S->nrec; i++) {
        if (rep[i] == -2) continue;
        if (rep[i] == -1) { out_rec(out, S->rec[i].kind, S->rec[i].icount, S->rec[i].v, S->rec[i].n, -1); continue; }
        for (int k = 0; k < repn[i]; k++) {
            const int m = rep[i] + k;
            out_pooled(out, made.rec[m].kind, made.rec[m].icount, made.pool + made.off[m], made.rec[m].n);
        }
    }
    out_resolve(out);
    free(rep);
    free(repn);
    interp_list_free(&made);
    return out->n;
}
