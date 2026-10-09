/* test_interp.c - Stage 3 in C (src/present/interp.c) against an observer
 * log, as tools/interp_frame.py LOG --pairing --check --primitives reports
 * it: the same lines, so the two outputs diff.
 *
 *   test_interp LOG [--extrapolate]
 *
 * LOG is f117run --observe output taken with F117R_OBSERVE_PAGES=1. Its
 * phases are grouped into logic frames (split after each phase that
 * presents; the first group is dropped). For each pair of consecutive frames:
 * the pairing; the Stage 1 replay of frame n and the in-between frames at
 * t = 1e-6 and 1 - 1e-6 (every paired primitive regenerated and moved by
 * almost nothing) against the page dumps; and the in-between frames at 1/4,
 * 1/2 and 3/4: what moved and what was held, the provenance rule (every
 * record is one of the two frames' or a regenerated one) and how the picture
 * differs from the skeleton frame's. Exit 1 when an exactness check fails.
 *
 * --extrapolate measures the picture-age setting instead: for three frames
 * A, B and C, the frame predicted from A and B a whole step past B (t = 2,
 * drawn from B's start) against C as the original drew it, beside B held
 * (what interpolation shows then, a step late), over the whole display and
 * the 3-D window (its first 107 rows). */
#include "interp.h"
#include "drawlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_line(FILE *f, char **buf, size_t *cap)
{
    size_t len = 0;
    if (!*buf) { *cap = 1 << 16; *buf = (char *)malloc(*cap); }
    for (;;) {
        if (!fgets(*buf + len, (int)(*cap - len), f)) return len ? *buf : NULL;
        len += strlen(*buf + len);
        if (len && (*buf)[len - 1] == '\n') return *buf;
        *cap *= 2;
        *buf = (char *)realloc(*buf, *cap);
    }
}

typedef struct {
    uint64_t icount;
    const int32_t *z, *y;             /* the dumps' values (segment, origin, bytes) */
    int nz, ny;
    int r0, nr;                       /* records in the log's list */
    int presents;
} phase_t;

typedef struct {
    interp_rec *rec;
    int nrec;
    int p0, np;                       /* its phases */
    interp_frame *f;
} frame_t;

static interp_rec *g_rec;
static int g_nrec, g_crec;
static phase_t *g_ph;
static int g_nph, g_cph;

static void render(const frame_t *fr, const interp_rec *rec, int n, uint8_t *work, uint8_t *disp)
{
    static drawlist d;
    static uint8_t bytes[65536];
    drawlist_init(&d);
    d.prefer_logged = 1;
    const phase_t *ph = &g_ph[fr->p0];
    for (int a = 0; a < ph->nz - 2; a++) bytes[a] = (uint8_t)ph->z[2 + a];
    drawlist_seed(&d, 'Z', (uint16_t)ph->z[0], (uint16_t)ph->z[1], bytes, (unsigned)(ph->nz - 2));
    for (int a = 0; a < ph->ny - 2; a++) bytes[a] = (uint8_t)ph->y[2 + a];
    drawlist_seed(&d, 'Y', (uint16_t)ph->y[0], (uint16_t)ph->y[1], bytes, (unsigned)(ph->ny - 2));
    for (int i = 0; i < n; i++) drawlist_record(&d, rec[i].kind, rec[i].v, rec[i].n);
    const drawlist_page *w = drawlist_get(&d, (uint16_t)ph->z[0]), *y = drawlist_get(&d, 0xA000);
    memcpy(work, w->b, 65536);
    memcpy(disp, y->b, 64000);
}

/* the dumps that open frame k + 1: the pages after frame k */
static void end_state(const frame_t *frames, int k, uint8_t *work, uint8_t *disp)
{
    const phase_t *ph = &g_ph[frames[k + 1].p0];
    for (int a = 0; a < 65536; a++) work[a] = a < ph->nz - 2 ? (uint8_t)ph->z[2 + a] : 0;
    for (int a = 0; a < 64000; a++) disp[a] = (uint8_t)ph->y[2 + a];
}

/* ---- a set of records, for the provenance rule ---- */
typedef struct { const interp_rec **slot; size_t cap, n; } recset;

static uint64_t rec_hash(const interp_rec *r)
{
    uint64_t h = 1469598103934665603ull ^ (uint64_t)(unsigned char)r->kind;
    h = (h ^ r->icount) * 1099511628211ull;
    h = (h ^ (uint64_t)r->n) * 1099511628211ull;
    for (int k = 0; k < r->n; k++) h = (h ^ (uint32_t)r->v[k]) * 1099511628211ull;
    return h;
}

static int rec_eq(const interp_rec *a, const interp_rec *b)
{
    return a->kind == b->kind && a->icount == b->icount && a->n == b->n && !memcmp(a->v, b->v, (size_t)a->n * sizeof *a->v);
}

static void set_add(recset *s, const interp_rec *r)
{
    if (2 * (s->n + 1) > s->cap) {
        recset t = { NULL, s->cap ? 2 * s->cap : 1 << 16, 0 };
        t.slot = calloc(t.cap, sizeof *t.slot);
        for (size_t k = 0; k < s->cap; k++) if (s->slot[k]) set_add(&t, s->slot[k]);
        free(s->slot);
        *s = t;
    }
    size_t k = (size_t)(rec_hash(r) & (s->cap - 1));
    while (s->slot[k]) { if (rec_eq(s->slot[k], r)) return; k = (k + 1) & (s->cap - 1); }
    s->slot[k] = r;
    s->n++;
}

static int set_has(const recset *s, const interp_rec *r)
{
    size_t k = (size_t)(rec_hash(r) & (s->cap - 1));
    while (s->slot[k]) { if (rec_eq(s->slot[k], r)) return 1; k = (k + 1) & (s->cap - 1); }
    return 0;
}

/* ---- reports as interp_frame.py prints them: "  key: value", sorted ---- */
typedef struct { char name[128]; uint64_t v; } kv;
static kv g_res[128];
static int g_nres;

static void res_add(const char *name, uint64_t v)
{
    for (int k = 0; k < g_nres; k++)
        if (!strcmp(g_res[k].name, name)) { g_res[k].v += v; return; }
    snprintf(g_res[g_nres].name, sizeof g_res[0].name, "%s", name);
    g_res[g_nres++].v = v;
}

static int kv_cmp(const void *a, const void *b) { return strcmp(((const kv *)a)->name, ((const kv *)b)->name); }

static void res_print(void)
{
    qsort(g_res, (size_t)g_nres, sizeof g_res[0], kv_cmp);
    for (int k = 0; k < g_nres; k++) printf("  %s: %llu\n", g_res[k].name, (unsigned long long)g_res[k].v);
    g_nres = 0;
}

static void stats_add(const uint64_t *st)
{
    for (int k = 0; k < IS_COUNT; k++)
        if (st[k]) res_add(interp_stat_name(k), st[k]);
}

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static int pct(const int *xs, int n, double q) { int at = (int)(q * n); return n ? xs[at < n - 1 ? at : n - 1] : 0; }

static int diff_count(const uint8_t *a, const uint8_t *b, int n)
{
    int d = 0;
    for (int k = 0; k < n; k++) d += a[k] != b[k];
    return d;
}

static int extrapolate(const frame_t *frames, int nfr);

int main(int argc, char **argv)
{
    const int extrap = argc == 3 && !strcmp(argv[2], "--extrapolate");
    if (argc != 2 && !extrap) { fprintf(stderr, "usage: test_interp LOG [--extrapolate]\n"); return 2; }
    FILE *f = fopen(argv[1], "r");
    if (!f) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    char *buf = NULL;
    size_t cap = 0;
    char *line;
    int nph_all = 0;
    while ((line = read_line(f, &buf, &cap))) {
        const char k = line[0];
        if (k == '\n' || k == ' ' || !k) continue;
        char *p = line + 1;
        const uint64_t icount = strtoull(p, &p, 10);
        if (k == 'P') {
            if (g_nph == g_cph) { g_cph = g_cph ? 2 * g_cph : 256; g_ph = realloc(g_ph, (size_t)g_cph * sizeof *g_ph); }
            phase_t ph = { icount, NULL, NULL, 0, 0, g_nrec, 0, 0 };
            g_ph[g_nph++] = ph;
            nph_all++;
            continue;
        }
        if (!g_nph) continue;
        static int32_t v[3 + 3 * 65536 + 16];
        int n = 0;
        if (k == 'V') {                               /* the last two fields are hex */
            for (int q = 0; q < 8; q++) {
                char *e;
                v[n++] = (int32_t)strtol(p, &e, q < 6 ? 10 : 16);
                p = e;
            }
        } else
            for (;;) {
                char *e;
                const long x = strtol(p, &e, 10);
                if (e == p || n == (int)(sizeof v / sizeof v[0])) break;
                v[n++] = (int32_t)x;
                p = e;
            }
        int32_t *keep = malloc((size_t)(n ? n : 1) * sizeof *keep);
        memcpy(keep, v, (size_t)n * sizeof *v);
        phase_t *ph = &g_ph[g_nph - 1];
        if (k == 'Z') { ph->z = keep; ph->nz = n; continue; }
        if (k == 'Y') { ph->y = keep; ph->ny = n; continue; }
        if (k == 'J') continue;
        if (g_nrec == g_crec) { g_crec = g_crec ? 2 * g_crec : 1 << 16; g_rec = realloc(g_rec, (size_t)g_crec * sizeof *g_rec); }
        interp_rec r = { k, icount, keep, n };
        g_rec[g_nrec++] = r;
        ph->nr++;
        if (k == 'D' && n >= 1 && v[0] == 44) ph->presents = 1;
    }
    fclose(f);
    /* the phases with both dumps */
    int m = 0;
    for (int k = 0; k < g_nph; k++) if (g_ph[k].z && g_ph[k].y) g_ph[m++] = g_ph[k];
    g_nph = m;
    /* logic frames: split after each phase that presents; the first group is dropped */
    frame_t *frames = calloc((size_t)g_nph + 1, sizeof *frames);
    int nfr = 0, start = 0, group = 0;
    for (int k = 0; k < g_nph; k++) {
        if (!g_ph[k].presents) continue;
        if (group++) {
            frame_t *fr = &frames[nfr++];
            fr->p0 = start; fr->np = k - start + 1;
            int total = 0;
            for (int q = start; q <= k; q++) total += g_ph[q].nr + 1;
            fr->rec = malloc((size_t)total * sizeof *fr->rec);
            for (int q = start; q <= k; q++) {
                if (q > start) {                      /* a later phase opens with its origin, as the Python Frame does */
                    int32_t *x = malloc(2 * sizeof *x);
                    x[0] = 26; x[1] = g_ph[q].z[1];
                    interp_rec r = { 'X', g_ph[q].icount, x, 2 };
                    fr->rec[fr->nrec++] = r;
                }
                for (int i = 0; i < g_ph[q].nr; i++) fr->rec[fr->nrec++] = g_rec[g_ph[q].r0 + i];
            }
            fr->f = interp_parse(fr->rec, fr->nrec);
        }
        start = k + 1;
    }
    printf("%d phases, %d logic frames\n", g_nph, nfr);
    if (extrap) return extrapolate(frames, nfr);

    /* --pairing */
    {
        interp_causes tot;
        memset(&tot, 0, sizeof tot);
        int *motion = NULL, nmotion = 0, cmot = 0, ncuts = 0;
        char cuts[512] = "";
        for (int n = 0; n + 1 < nfr; n++) {
            interp_pairing *P = interp_pair(frames[n].f, frames[n + 1].f);
            interp_causes c;
            interp_pair_causes(P, &c);
            tot.polys += c.polys; tot.polys_paired += c.polys_paired; tot.batches += c.batches; tot.batches_paired += c.batches_paired;
            for (int q = 0; q < 5; q++) tot.cause[q] += c.cause[q];
            int *mv, nm = interp_pair_motion(P, &mv);
            if (nmotion + nm > cmot) { cmot = 2 * (nmotion + nm); motion = realloc(motion, (size_t)cmot * sizeof *motion); }
            memcpy(motion + nmotion, mv, (size_t)nm * sizeof *mv);
            nmotion += nm;
            const int cut = interp_is_cut(P);
            if (cut) {
                char what[64];
                if (cut == 1) snprintf(what, sizeof what, "polygons paired %.0f%%", 100.0 * (double)c.polys_paired / (double)c.polys);
                else if (cut == 2) { qsort(mv, (size_t)nm, sizeof *mv, cmp_int); snprintf(what, sizeof what, "median motion %d px", pct(mv, nm, 0.5)); }
                else snprintf(what, sizeof what, "no batch pairs");
                if (ncuts < 6) {
                    const size_t l = strlen(cuts);
                    snprintf(cuts + l, sizeof cuts - l, "%s(%d, '%s')", ncuts ? ", " : "", n, what);
                }
                ncuts++;
            }
            free(mv);
            interp_pairing_free(P);
        }
        printf("%d frame pairs; %d fall back to hold (cuts): [%s]\n", nfr - 1, ncuts, cuts);
        printf("polygons: %llu, paired %llu (%.1f%%)\n", (unsigned long long)tot.polys, (unsigned long long)tot.polys_paired,
               100.0 * (double)tot.polys_paired / (double)(tot.polys ? tot.polys : 1));
        res_add("batches", tot.batches);
        if (tot.batches_paired) res_add("batches paired", tot.batches_paired);
        if (tot.batches - tot.batches_paired) res_add("batches unpaired", tot.batches - tot.batches_paired);
        for (int q = 0; q < 5; q++) if (tot.cause[q]) res_add(interp_cause_name(q), tot.cause[q]);
        res_print();
        if (nmotion) {
            qsort(motion, (size_t)nmotion, sizeof *motion, cmp_int);
            double sum = 0;
            for (int q = 0; q < nmotion; q++) sum += motion[q];
            printf("vertex motion between steps (px, max of |dx|,|dy|): mean %.1f, median %d, p95 %d, p99 %d, max %d, over %d vertices\n",
                   sum / nmotion, pct(motion, nmotion, .5), pct(motion, nmotion, .95), pct(motion, nmotion, .99),
                   motion[nmotion - 1], nmotion);
        }
        free(motion);
    }

    static uint8_t za[65536], ya[64000], zb[65536], yb[64000], z[65536], y[64000];
    interp_list list;
    memset(&list, 0, sizeof list);
    int failed = 0;
    /* --check */
    for (int n = 0; n + 2 < nfr; n++) {
        const frame_t *A = &frames[n], *B = &frames[n + 1];
        interp_pairing *P = interp_pair(A->f, B->f);
        end_state(frames, n, za, ya);
        end_state(frames, n + 1, zb, yb);
        res_add("frames", 1);
        render(A, A->rec, A->nrec, z, y);
        res_add("stage 1 replay of frame n: display exact", !memcmp(y, ya, 64000));
        res_add("stage 1 replay of frame n: work page exact", !memcmp(z, za, 65536));
        uint64_t st[IS_COUNT] = { 0 }, st2[IS_COUNT] = { 0 };
        int sb;
        interp_inbetween(A->f, B->f, P, 1e-6, &list, &sb, st);
        render(A, list.rec, list.n, z, y);
        int ok = !memcmp(y, ya, 64000);
        res_add("t=0+: display exact", ok);
        res_add("t=0+: work page exact", !memcmp(z, za, 65536));
        if (!ok) { res_add("t=0+: display bytes off", (uint64_t)diff_count(y, ya, 64000)); failed = 1; }
        interp_inbetween(A->f, B->f, P, 1 - 1e-6, &list, &sb, st2);
        render(B, list.rec, list.n, z, y);
        ok = !memcmp(y, yb, 64000);
        res_add("t=1-: display exact", ok);
        res_add("t=1-: work page exact", !memcmp(z, zb, 65536));
        if (!ok) { res_add("t=1-: display bytes off", (uint64_t)diff_count(y, yb, 64000)); failed = 1; }
        res_add("regenerated polygons moved", st[IS_POLYS_MOVED] + st2[IS_POLYS_MOVED]);
        res_add("regenerated span fills moved", st[IS_SPANS_MOVED] + st2[IS_SPANS_MOVED]);
        res_add("regenerated outline edges moved", st[IS_OUTLINE_MOVED] + st2[IS_OUTLINE_MOVED]);
        interp_pairing_free(P);
    }
    res_print();
    /* --primitives */
    for (int n = 0; n + 2 < nfr; n++) {
        const frame_t *A = &frames[n], *B = &frames[n + 1];
        interp_pairing *P = interp_pair(A->f, B->f);
        res_add("frame pairs", 1);
        res_add("frame pairs that are cuts (hold)", interp_is_cut(P) != 0);
        recset known = { NULL, 0, 0 };
        for (int i = 0; i < A->nrec; i++) set_add(&known, &A->rec[i]);
        for (int i = 0; i < B->nrec; i++) set_add(&known, &B->rec[i]);
        static uint8_t da[64000], db[64000], wk[65536];
        end_state(frames, n, wk, da);
        end_state(frames, n + 1, wk, db);
        char in_a[256] = { 0 }, in_b[256] = { 0 };
        for (int k = 0; k < 64000; k++) { in_a[da[k]] = 1; in_b[db[k]] = 1; }
        static const double ts[3] = { 0.25, 0.5, 0.75 };
        for (int q = 0; q < 3; q++) {
            uint64_t st[IS_COUNT] = { 0 };
            int sb;
            interp_inbetween(A->f, B->f, P, ts[q], &list, &sb, st);
            res_add("in-between frames", 1);
            int novel = 0, kinds_ok = 1;
            for (int i = 0; i < list.n; i++) {
                if (set_has(&known, &list.rec[i])) continue;
                novel++;
                if (!strchr("RbQLDN", list.rec[i].kind)) kinds_ok = 0;
            }
            res_add("records in the list", (uint64_t)list.n);
            res_add("records neither frame has", (uint64_t)novel);
            res_add("provenance violations", (uint64_t)novel > st[IS_REGENERATED] || !kinds_ok);
            stats_add(st);
            render(sb ? B : A, list.rec, list.n, z, y);
            int extra = 0;
            for (int k = 0; k < 64000 && !extra; k++) extra = !in_a[y[k]] && !in_b[y[k]];
            res_add("pixels compared", 64000);
            res_add("pixels that differ from the skeleton frame's own picture", (uint64_t)diff_count(y, sb ? db : da, 64000));
            res_add("in-between displays with a colour index in neither neighbour", (uint64_t)extra);
        }
        free(known.slot);
        interp_pairing_free(P);
    }
    res_print();
    interp_list_free(&list);
    return failed;
}

static int extrapolate(const frame_t *frames, int nfr)
{
    static uint8_t zc[65536], yc[64000], zb[65536], yb[64000], z[65536], y[64000];
    interp_list list;
    memset(&list, 0, sizeof list);
    uint64_t st[IS_COUNT] = { 0 };
    double sum_e[2] = { 0, 0 }, sum_h[2] = { 0, 0 }, worst_e[2] = { 0, 0 }, worst_h[2] = { 0, 0 };
    int count = 0, better = 0, same = 0;
    for (int n = 0; n + 3 < nfr; n++) {
        const frame_t *A = &frames[n], *B = &frames[n + 1];
        interp_pairing *P = interp_pair(A->f, B->f);
        int sb;
        interp_inbetween_ex(A->f, B->f, P, 2.0, INTERP_EXTRAPOLATE, &list, &sb, st);
        render(sb ? B : A, list.rec, list.n, z, y);
        end_state(frames, n + 2, zc, yc);              /* C as the original drew it */
        end_state(frames, n + 1, zb, yb);              /* B held */
        const int rows[2] = { 107, 200 };
        for (int k = 0; k < 2; k++) {
            const int px = 320 * rows[k];
            const double e = (double)diff_count(y, yc, px) / px, h = (double)diff_count(yb, yc, px) / px;
            sum_e[k] += e; sum_h[k] += h;
            if (e > worst_e[k]) worst_e[k] = e;
            if (h > worst_h[k]) worst_h[k] = h;
        }
        const int de = diff_count(y, yc, 64000), dh = diff_count(yb, yc, 64000);
        better += de < dh;
        same += de == dh;
        count++;
        interp_pairing_free(P);
    }
    printf("%d predictions (a step past the newer frame) against the next real frame\n", count);
    if (count) {
        for (int k = 0; k < 2; k++)
            printf("  %s: extrapolated %.3f%% of pixels differ (worst %.3f%%); B held %.3f%% (worst %.3f%%)\n",
                   k ? "whole display" : "3-D window", 100.0 * sum_e[k] / count, 100.0 * worst_e[k],
                   100.0 * sum_h[k] / count, 100.0 * worst_h[k]);
        printf("  the prediction is nearer than B held in %d, as near in %d, further in %d\n", better, same, count - better - same);
        printf("  moved: %llu polygons, %llu span fills, %llu outline edges, %llu HUD lines\n",
               (unsigned long long)st[IS_POLYS_MOVED], (unsigned long long)st[IS_SPANS_MOVED],
               (unsigned long long)st[IS_OUTLINE_MOVED], (unsigned long long)st[IS_HUD_MOVED]);
    }
    interp_list_free(&list);
    return 0;
}
