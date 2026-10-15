/* test_band_jitter.c - how smoothly the horizon moves between logic frames, before and after the state-driven sky
 * and ground bands.
 *
 *   test_band_jitter LOG [N] [--series]
 *
 * LOG is f117run --observe output taken with F117R_OBSERVE_PAGES=1 and F117R_OBSERVE_BANDS=1. Its records go through
 * a drawfeed and a drawlive as in test_hud_live.c; for every pair of consecutive logic frames the in-between list is
 * built at t = 0, 1/4, 1/2 and 3/4 twice: as the presentation did before (the bands' 'U' corners interpolated as
 * integers: flags INTERP_VERTICES | INTERP_HUD) and with the band state (adding INTERP_BANDS, whose band corners
 * band_fine makes from the interpolated 'o' record).
 *
 * The edges measured are the main view's band edges: each band's first edge (its corners 0 and 1), the horizon being
 * the first sky band's (the line the sky and the ground share). From each sample an edge's height is taken where it
 * crosses three columns of the picture (x = 40, 160 and 280, cell centres), in fine pixels at scale N (default 4), as
 * each path draws it: before, the line through the integer corners' cell centres; after, the line through the fine
 * corners (the fine builder draws both edges from exactly these lines). A sample counts while the line crosses the
 * column inside the window and is not steeper than 3:1. For each series the second difference per sample
 * (y[i+1] - 2 y[i] + y[i-1], samples a quarter of a logic step apart) is summarised, the horizon alone and every band
 * edge together: a path that moves smoothly has small second differences, one that holds and jumps has large ones.
 * --series prints the horizon's series. */
#include "drawfeed.h"
#include "band_state.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t memory[MEM_SIZE];
static drawfeed feed;
static drawlive live;
static int g_n = 4, g_series;

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

static int parse_values(char kind, char *p, int32_t *v, int cap)
{
    int n = 0;
    if (kind == 'V') {
        for (int q = 0; q < 8; q++) { char *e; v[n++] = (int32_t)strtol(p, &e, q < 6 ? 10 : 16); p = e; }
        return n;
    }
    for (;;) {
        char *e;
        const long x = strtol(p, &e, 10);
        if (e == p || n == cap) break;
        v[n++] = (int32_t)x;
        p = e;
    }
    return n;
}

#define MAIN_VIEW 814            /* the 3-D view's block (the camera window's are others) */
#define NCOL 3
static const int g_col[NCOL] = { 40, 160, 280 };
#define MAXS 8192
typedef struct { int n; double y[MAXS]; uint8_t ok[MAXS]; } series_t;
static series_t g_before[BAND_QUADS][NCOL], g_after[BAND_QUADS][NCOL];

/* the height (fine pixels) at which the line through (ax, ay), (bx, by) (coarse units, edge space) crosses column c */
static int cross(double ax, double ay, double bx, double by, int c, double ymax, double *y)
{
    const double dx = bx - ax, dy = by - ay;
    if (fabs(dx) < 1e-9 || fabs(dy) > 3 * fabs(dx)) return 0;
    const double yc = ay + (c + 0.5 - ax) * dy / dx;
    if (yc < 0 || yc > ymax + 1) return 0;
    *y = yc * g_n;
    return 1;
}

static void put(series_t *s, int ok, double y)
{
    if (s->n >= MAXS) return;
    s->ok[s->n] = (uint8_t)ok;
    s->y[s->n] = y;
    s->n++;
}

/* after: the main view's 'o' record in the list, its fine horizon; *ordinal the index among the list's 'U' records of
 * the first one after it (the pass's first band) */
static int sample_after(const interp_list *l, double y[BAND_QUADS][NCOL], int ok[BAND_QUADS][NCOL], int *ordinal)
{
    band_state st;
    band_fine_list fl;
    int u = 0;
    for (int i = 0; i < l->n; i++) {
        if (l->rec[i].kind == 'U') { u++; continue; }
        if (l->rec[i].kind != 'o' || !band_state_load(l->rec[i].v, l->rec[i].n, &st) || st.view != MAIN_VIEW) continue;
        if (band_fine(&st, NULL, &fl)) return 0;
        *ordinal = u;
        for (int b = 0; b < BAND_QUADS; b++) {
            const band_fquad *q = &fl.q[b];
            for (int c = 0; c < NCOL; c++)
                ok[b][c] = cross(q->x[0] + 0.5, q->y[0] + 0.5, q->x[1] + 0.5, q->y[1] + 0.5, g_col[c], st.ymax, &y[b][c]);
        }
        return 1;
    }
    return 0;
}

/* before: the 'U' record with that ordinal, its integer corners */
static int sample_before(const interp_list *l, int ordinal, double ymax, double y[BAND_QUADS][NCOL], int ok[BAND_QUADS][NCOL])
{
    int u = 0, b = 0;
    for (int i = 0; i < l->n && b < BAND_QUADS; i++) {
        if (l->rec[i].kind != 'U') continue;
        if (u++ < ordinal) continue;
        const int32_t *v = l->rec[i].v;
        if (l->rec[i].n < 10 || (b == 0 && (v[0] & 0xFF) != 0x80)) return 0;
        for (int c = 0; c < NCOL; c++)
            ok[b][c] = cross((int16_t)v[2] + 0.5, (int16_t)v[3] + 0.5, (int16_t)v[4] + 0.5, (int16_t)v[5] + 0.5, g_col[c], ymax, &y[b][c]);
        b++;
    }
    return b == BAND_QUADS;
}

static void sample_pair(const drawlive_step *a, const drawlive_step *b)
{
    static interp_list lo, ln;
    const double ts[4] = { 0, 0.25, 0.5, 0.75 };
    for (int k = 0; k < 4; k++) {
        int sb;
        interp_inbetween_ex(a->parsed, b->parsed, b->pair, ts[k], INTERP_VERTICES | INTERP_HUD, &lo, &sb, NULL);
        interp_inbetween_ex(a->parsed, b->parsed, b->pair, ts[k], INTERP_VERTICES | INTERP_HUD | INTERP_BANDS, &ln, &sb, NULL);
        static double ya[BAND_QUADS][NCOL], yb[BAND_QUADS][NCOL];
        static int oka[BAND_QUADS][NCOL], okb[BAND_QUADS][NCOL];
        int ordinal = -1;
        band_state st;
        memset(&st, 0, sizeof st);
        int have = 0;
        for (int i = 0; i < ln.n && !have; i++)
            if (ln.rec[i].kind == 'o' && band_state_load(ln.rec[i].v, ln.rec[i].n, &st) && st.view == MAIN_VIEW) have = 1;
        if (!have || !sample_after(&ln, ya, oka, &ordinal) || !sample_before(&lo, ordinal, st.ymax, yb, okb)) {
            for (int q = 0; q < BAND_QUADS; q++)
                for (int c = 0; c < NCOL; c++) { put(&g_after[q][c], 0, 0); put(&g_before[q][c], 0, 0); }
            continue;
        }
        for (int q = 0; q < BAND_QUADS; q++)
            for (int c = 0; c < NCOL; c++) { put(&g_after[q][c], oka[q][c], ya[q][c]); put(&g_before[q][c], okb[q][c], yb[q][c]); }
    }
}

typedef struct { int n; double sum, sq, max; } acc_t;
static void acc_add(acc_t *a, double v) { a->n++; a->sum += fabs(v); a->sq += v * v; if (fabs(v) > a->max) a->max = fabs(v); }

int main(int argc, char **argv)
{
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--series")) g_series = 1;
        else if (!path) path = argv[i];
        else g_n = atoi(argv[i]);
    }
    if (!path || g_n < 2 || g_n > 12) { fprintf(stderr, "usage: test_band_jitter LOG [N 2..12] [--series]\n"); return 2; }
    FILE *f = fopen(path, "r");
    if (!f) { printf("SKIP: cannot read %s\n", path); return 77; }
    static uint8_t *ydump[8192];
    char *buf = NULL;
    size_t cap = 0;
    char *line;
    static int32_t v[3 + 3 * 65536 + 16];
    int phases = 0;
    while ((line = read_line(f, &buf, &cap))) {
        const char k = line[0];
        if (k == 'P') { phases++; continue; }
        if (k != 'Y' || phases < 1 || phases >= 8192) continue;
        char *p = line + 1;
        strtoull(p, &p, 10);
        const int n = parse_values(k, p, v, (int)(sizeof v / sizeof v[0]));
        if (n >= 2 + 64000) {
            ydump[phases - 1] = (uint8_t *)malloc(64000);
            for (int a = 0; a < 64000; a++) ydump[phases - 1][a] = (uint8_t)v[2 + a];
        }
    }
    rewind(f);
    drawfeed_init(&feed, memory);
    drawlive_init(&live);
    drawlive_set_scale(&live, g_n);
    live.interp = 1;
    int phase = -1, deliver = 0, pairs = 0;
    uint64_t last_seq = ~0ull;
    while ((line = read_line(f, &buf, &cap))) {
        const char k = line[0];
        if (k == '\n' || k == ' ' || !k) continue;
        char *p = line + 1;
        const uint64_t icount = strtoull(p, &p, 10);
        if (k == 'P') {
            phase++;
            if (phase < 8192 && ydump[phase]) memcpy(memory + 0xA0000, ydump[phase], 64000);
            feed.obs.frame_phase(feed.obs.user, icount);
            deliver = 0;
            drawlive_update(&live, &feed);
            const drawlive_step *a, *b;
            drawlive_steps(&live, &a, &b);
            if (a && b && b->pair && live.picture_seq != last_seq) { last_seq = live.picture_seq; sample_pair(a, b); pairs++; }
            continue;
        }
        if (phase < 0) continue;
        if (k == 'Z') deliver = feed.want_seed;
        if ((k == 'Z' || k == 'Y' || k == 'J') && !deliver) continue;
        const int n = parse_values(k, p, v, (int)(sizeof v / sizeof v[0]));
        if (k == 'V') feed.obs.vertex(feed.obs.user, icount, v, v + 3, v[5], (uint16_t)v[6], (uint16_t)v[7]);
        else feed.obs.prim(feed.obs.user, icount, k, v, n);
    }
    fclose(f);
    printf("%d logic-frame pairs, %d samples (four per logic step); band edges' heights where they cross a column, second "
           "differences in fine pixels, N=%d\n", pairs, g_after[0][0].n, g_n);
    acc_t all[2];
    memset(all, 0, sizeof all);
    for (int b = 0; b < BAND_QUADS; b++)
        for (int c = 0; c < NCOL; c++) {
            acc_t acc[2];
            memset(acc, 0, sizeof acc);
            const series_t *B = &g_before[b][c], *A = &g_after[b][c];
            for (int i = 1; i + 1 < A->n; i++) {
                if (!A->ok[i - 1] || !A->ok[i] || !A->ok[i + 1] || !B->ok[i - 1] || !B->ok[i] || !B->ok[i + 1]) continue;
                const double db = B->y[i + 1] - 2 * B->y[i] + B->y[i - 1], da = A->y[i + 1] - 2 * A->y[i] + A->y[i - 1];
                acc_add(&acc[0], db); acc_add(&acc[1], da);
                acc_add(&all[0], db); acc_add(&all[1], da);
            }
            if (b != 0) continue;
            if (!acc[0].n) { printf("horizon, column %3d: no run of three samples in the window in both\n", g_col[c]); continue; }
            printf("horizon, column %3d: %5d second differences; before: mean %.2f rms %.2f max %.2f; after: mean %.2f rms %.2f max %.2f\n",
                   g_col[c], acc[0].n, acc[0].sum / acc[0].n, sqrt(acc[0].sq / acc[0].n), acc[0].max,
                   acc[1].sum / acc[1].n, sqrt(acc[1].sq / acc[1].n), acc[1].max);
        }
    if (all[0].n)
        printf("every band edge, three columns: %5d second differences; before: mean %.2f rms %.2f max %.2f; after: mean %.2f rms %.2f max %.2f\n",
               all[0].n, all[0].sum / all[0].n, sqrt(all[0].sq / all[0].n), all[0].max, all[1].sum / all[1].n, sqrt(all[1].sq / all[1].n), all[1].max);
    if (g_series)
        for (int c = 0; c < NCOL; c++) {
            printf("series horizon column %d (sample, before y, after y)\n", g_col[c]);
            for (int i = 0; i < g_after[0][c].n; i++)
                printf("%d %s %.3f %s %.3f\n", i, g_before[0][c].ok[i] ? "ok" : "--", g_before[0][c].y[i], g_after[0][c].ok[i] ? "ok" : "--", g_after[0][c].y[i]);
        }
    return 0;
}
