/* test_hud_jitter.c - how smoothly the HUD moves between logic frames, before and after the state-driven HUD.
 *
 *   test_hud_jitter LOG [N] [--series]
 *
 * LOG is f117run --observe output taken with F117R_OBSERVE_PAGES=1 and F117R_OBSERVE_HUD=1. Its records go through
 * a drawfeed and a drawlive as in test_hud_live.c; for every pair of consecutive logic frames the in-between list is
 * built at t = 0, 1/4, 1/2 and 3/4 twice: as the presentation did (the HUD lines interpolated as integers, the text
 * and tick marks held: flags INTERP_VERTICES) and with the state-driven HUD (INTERP_VERTICES | INTERP_HUD), whose fine
 * primitives are then made from the interpolated state (hud_fine at the picture's scale N, default 4).
 *
 * From each sample the positions of the HUD's moving parts are taken in fine pixels, the same in both: every
 * airspeed and altitude tape label and heading label (by its text), every ladder label (by its text and end), each
 * counted only while it is wholly in the part of the picture its clip shows, and the ladder endpoint nearest the
 * pivot, followed from sample to sample by nearest neighbour. For each series the second difference per sample
 * (x[i+1] - 2 x[i] + x[i-1], samples a quarter of a logic step apart) is summarised: a path that moves smoothly has
 * small second differences, one that holds and jumps has large ones. Labels are compared over runs of three
 * samples where both modes show them. --series prints the series. */
#include "drawfeed.h"
#include "hud_cover.h"
#include "hud_state.h"

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

/* ---- one sample's features ----------------------------------------------------------------------------------- */
enum { C_ASPD, C_ALT, C_HDG, C_LAD };
typedef struct { int cls, parity; char text[12]; double x, y; } label_t;
typedef struct { double x, y; } pt_t;
typedef struct { label_t lab[64]; int nlab; pt_t pt[96]; int npt; double pivot_x, pivot_y; } sample_t;

static void add_label(sample_t *s, int cls, const uint8_t *text, int n, double x, double y)
{
    if (s->nlab >= 64) return;
    label_t *l = &s->lab[s->nlab];
    memset(l, 0, sizeof *l);
    l->cls = cls;
    memcpy(l->text, text, n > 11 ? 11 : n);
    l->x = x; l->y = y;
    int par = 0;
    for (int k = 0; k < s->nlab; k++) if (s->lab[k].cls == cls && !strcmp(s->lab[k].text, l->text)) par++;
    l->parity = par;
    s->nlab++;
}

/* after: the primitives hud_fine makes from the list's (interpolated) 'h' record */
static int sample_after(const interp_list *l, sample_t *s)
{
    static hud_state st;
    static hud_exact_list ex;
    static hud_fine_list fl;
    memset(s, 0, sizeof *s);
    for (int i = 0; i < l->n; i++) {
        if (l->rec[i].kind != 'h') continue;
        if (!hud_state_load_drawn(l->rec[i].v, l->rec[i].n, &st) || !st.have_ticks || hud_exact(&st, &ex) < 0) return 0;
        if (hud_fine(&st, g_n, &ex, &fl) < 0) return 0;
        const int origin = st.ds[0x2942] | (st.ds[0x2943] << 8);
        const double ox = origin % 320, oy = origin / 320;
        s->pivot_x = (159 + 0.5) * g_n; s->pivot_y = (52 + 0.5) * g_n;
        for (int k = 0; k < fl.n; k++) {
            const hud_fprim *f = &fl.p[k];
            if (f->type == HF_TEXT && f->item == HI_G) continue;     /* a fixed text: not one of the moving labels */
            if (f->type == HF_SPRITE) continue;
            if (f->type == HF_TEXT) {
                const int x = f->blk[4];
                (void)x;
                const int cls = f->entry == 1 ? (fabs(f->x0 / g_n - (st.ds[0x2923] | st.ds[0x2924] << 8)) < 0.01 ? C_ASPD : C_ALT)
                                : f->entry == 6 ? C_HDG : C_LAD;
                add_label(s, cls, f->text, f->ntext, f->x0, f->y0);
            } else if (f->item == HI_LADDER && f->cl == (int)ox * g_n && f->ct == (int)oy * g_n && s->npt + 2 <= 96) {
                s->pt[s->npt++] = (pt_t){ f->x0, f->y0 };
                s->pt[s->npt++] = (pt_t){ f->x1, f->y1 };
            }
        }
        return 1;
    }
    return 0;
}

/* before: the HUD pass's records as the list holds them, found with the cover */
static int sample_before(const interp_list *l, sample_t *s)
{
    static hud_state st;
    static hud_exact_list ex;
    hud_cover cov;
    cov.active = 0;
    memset(s, 0, sizeof *s);
    int found = 0, aspd_x = 0;
    for (int i = 0; i < l->n; i++) {
        const interp_rec *r = &l->rec[i];
        if (r->kind == 'h') {
            if (!hud_state_load(r->v, r->n, &st) || !st.have_ticks || hud_exact(&st, &ex) < 0) return 0;
            hud_cover_begin(&cov, &ex);
            aspd_x = st.ds[0x2923] | (st.ds[0x2924] << 8);
            s->pivot_x = (159 + 0.5) * g_n; s->pivot_y = (52 + 0.5) * g_n;
            found = 1;
            continue;
        }
        if (!cov.active || !strchr("KNTHS", r->kind)) continue;
        if (hud_cover_step(&cov, r->kind, r->v, r->n) != HC_REPLACED) continue;
        if (r->kind == 'T') {
            const int entry = r->v[0], x = (int16_t)r->v[2 + 4], yw = r->v[2 + 5] & 0xFFFF;
            uint8_t text[12];
            const int nt = r->v[13] > 11 ? 11 : r->v[13];
            for (int k = 0; k < nt; k++) text[k] = (uint8_t)r->v[14 + k];
            if (entry == 1) {
                int y = yw & 0xFF; if (y >= 200) y -= 256;
                add_label(s, x == aspd_x ? C_ASPD : C_ALT, text, nt, (double)x * g_n, (double)y * g_n);
            } else if (entry == 6) add_label(s, C_LAD, text, nt, (double)x * g_n, (double)(int16_t)yw * g_n);
            else add_label(s, C_HDG, text, nt, (double)x * g_n, (double)(int16_t)yw * g_n);
        } else if (r->kind == 'N' && r->v[6] != 0 && s->npt + 2 <= 96) {
            const int ox = r->v[6] % 320, oy = r->v[6] / 320;
            s->pt[s->npt++] = (pt_t){ (r->v[0] + ox + 0.5) * g_n, (r->v[1] + oy + 0.5) * g_n };
            s->pt[s->npt++] = (pt_t){ (r->v[2] + ox + 0.5) * g_n, (r->v[3] + oy + 0.5) * g_n };
        }
    }
    return found;
}

/* ---- series ---------------------------------------------------------------------------------------------------- */
#define MAXS 1024
#define MAXK 96
typedef struct { int n; double x[MAXS], y[MAXS]; unsigned char ok[MAXS]; } series_t;
typedef struct { int cls, parity; char text[12]; } key_t;

static key_t g_key[MAXK];
static int g_nkey;
typedef struct { series_t s[MAXK]; series_t pt; series_t pan[6]; int pt_alive; pt_t pt_last; int nsamples; } track_t;
static track_t g_new, g_old;

static int key_of(int cls, const char *text, int parity)
{
    for (int k = 0; k < g_nkey; k++)
        if (g_key[k].cls == cls && g_key[k].parity == parity && !strcmp(g_key[k].text, text)) return k;
    if (g_nkey >= MAXK) return -1;
    g_key[g_nkey].cls = cls; g_key[g_nkey].parity = parity; strcpy(g_key[g_nkey].text, text);
    return g_nkey++;
}

/* a label counts only while it is wholly in the part of the picture its clip shows, as cells */
static int visible(const label_t *l)
{
    const double cx = l->x / g_n, cy = l->y / g_n;
    switch (l->cls) {
    case C_ASPD: case C_ALT: return cy >= 22 && cy + 5 <= 82;
    case C_HDG: return cx >= 90 && cx + 11 <= 230;
    default: return cx >= 78 && cx + 11 <= 241 && cy >= 20 && cy + 5 <= 89;
    }
}

static void put_series(series_t *sr, int n, int ok, double x, double y)
{
    if (n >= MAXS) return;
    sr->ok[n] = (unsigned char)ok; sr->x[n] = x; sr->y[n] = y;
    if (sr->n < n + 1) sr->n = n + 1;
}

static void record(track_t *t, const sample_t *s)
{
    const int at = t->nsamples++;
    for (int i = 0; i < s->nlab; i++) {
        const int k = key_of(s->lab[i].cls, s->lab[i].text, s->lab[i].parity);
        if (k >= 0) put_series(&t->s[k], at, visible(&s->lab[i]), s->lab[i].x, s->lab[i].y);
    }
    /* the ladder endpoint nearest the pivot, followed by nearest neighbour; one on the window's edge is the
     * clipper's, not the rung's */
    const pt_t *best = NULL;
    double bd = 1e18;
    const double ref_x = t->pt_alive ? t->pt_last.x : s->pivot_x, ref_y = t->pt_alive ? t->pt_last.y : s->pivot_y;
    for (int i = 0; i < s->npt; i++) {
        const double cx = s->pt[i].x / g_n, cy = s->pt[i].y / g_n;
        if (cx < 82 || cx > 237 || cy < 18 || cy > 82) continue;
        const double d = hypot(s->pt[i].x - ref_x, s->pt[i].y - ref_y);
        if (d < bd) { bd = d; best = &s->pt[i]; }
    }
    if (best && t->pt_alive && bd >= 3.0 * g_n) best = NULL;
    if (best) { t->pt_alive = 1; t->pt_last = *best; } else t->pt_alive = 0;
    put_series(&t->pt, at, best != NULL, best ? best->x : 0, best ? best->y : 0);
}

typedef struct { int n; double sum, sq, max; } acc_t;
static void acc_add(acc_t *a, double d2) { a->n++; a->sum += fabs(d2); a->sq += d2 * d2; if (fabs(d2) > a->max) a->max = fabs(d2); }

/* second differences over runs of three consecutive samples where the feature is shown in both modes */
static void d2_series(const series_t *a, const series_t *b, acc_t out[2][2])
{
    const int n = a->n < b->n ? a->n : b->n;
    for (int i = 1; i + 1 < n; i++) {
        if (!a->ok[i - 1] || !a->ok[i] || !a->ok[i + 1] || !b->ok[i - 1] || !b->ok[i] || !b->ok[i + 1]) continue;
        for (int axis = 0; axis < 2; axis++) {
            const double *pa = axis ? a->y : a->x, *pb = axis ? b->y : b->x;
            acc_add(&out[0][axis], pa[i + 1] - 2 * pa[i] + pa[i - 1]);
            acc_add(&out[1][axis], pb[i + 1] - 2 * pb[i] + pb[i - 1]);
        }
    }
}

static void print_acc(const char *label, const acc_t o[2][2], int nseries)
{
    for (int axis = 0; axis < 2; axis++) {
        if (!o[0][axis].n) { printf("%-30s %c: no run of three samples shown in both\n", label, axis ? 'y' : 'x'); continue; }
        printf("%-30s %c: %4d second differences (%d series); before: mean %.2f rms %.2f max %.2f; after: mean %.2f rms %.2f max %.2f\n",
               label, axis ? 'y' : 'x', o[0][axis].n, nseries, o[0][axis].sum / o[0][axis].n, sqrt(o[0][axis].sq / o[0][axis].n),
               o[0][axis].max, o[1][axis].sum / o[1][axis].n, sqrt(o[1][axis].sq / o[1][axis].n), o[1][axis].max);
    }
}

static void print_acc_one(const char *label, const acc_t o[2][2], int axis)
{
    printf("%-30s %c: %4d second differences; before: mean %.2f rms %.2f max %.2f; after: mean %.2f rms %.2f max %.2f\n",
           label, axis ? 'y' : 'x', o[0][axis].n, o[0][axis].sum / o[0][axis].n, sqrt(o[0][axis].sq / o[0][axis].n), o[0][axis].max,
           o[1][axis].sum / o[1][axis].n, sqrt(o[1][axis].sq / o[1][axis].n), o[1][axis].max);
}

static void summarise(void)
{
    static const char *const names[4] = { "airspeed tape labels", "altitude tape labels", "heading labels", "ladder labels" };
    for (int cls = 0; cls < 4; cls++) {
        acc_t acc[2][2];
        memset(acc, 0, sizeof acc);
        int ns = 0;
        for (int k = 0; k < g_nkey; k++) {
            if (g_key[k].cls != cls) continue;
            const int before = acc[0][0].n;
            d2_series(&g_old.s[k], &g_new.s[k], acc);
            ns += acc[0][0].n > before;
        }
        print_acc(names[cls], acc, ns);
    }
    static const char *const pnames[6] = { "flight-path marker top", "vertical-speed bar end", "stall bar end", "waypoint cue centre x",
                                           "map arrow left x", "map arrow top y" };
    for (int q = 0; q < 6; q++) {
        acc_t acc[2][2];
        memset(acc, 0, sizeof acc);
        d2_series(&g_old.pan[q], &g_new.pan[q], acc);
        if (!acc[0][1].n) printf("%-30s y: no run of three samples shown in both\n", pnames[q]);
        else print_acc_one(pnames[q], acc, 1);
    }
    /* the ladder endpoint: followed separately in each mode, so its runs are counted separately */
    acc_t ep[2][2];
    memset(ep, 0, sizeof ep);
    const series_t *sm[2] = { &g_old.pt, &g_new.pt };
    for (int m = 0; m < 2; m++)
        for (int i = 1; i + 1 < sm[m]->n; i++) {
            if (!sm[m]->ok[i - 1] || !sm[m]->ok[i] || !sm[m]->ok[i + 1]) continue;
            for (int axis = 0; axis < 2; axis++) {
                const double *p = axis ? sm[m]->y : sm[m]->x;
                acc_add(&ep[m][axis], p[i + 1] - 2 * p[i] + p[i - 1]);
            }
        }
    for (int axis = 0; axis < 2; axis++)
        printf("%-30s %c: before %d second differences: mean %.2f rms %.2f max %.2f; after %d: mean %.2f rms %.2f max %.2f\n",
               "ladder endpoint nearest pivot", axis ? 'y' : 'x', ep[0][axis].n, ep[0][axis].n ? ep[0][axis].sum / ep[0][axis].n : 0,
               ep[0][axis].n ? sqrt(ep[0][axis].sq / ep[0][axis].n) : 0, ep[0][axis].max, ep[1][axis].n,
               ep[1][axis].n ? ep[1][axis].sum / ep[1][axis].n : 0, ep[1][axis].n ? sqrt(ep[1][axis].sq / ep[1][axis].n) : 0, ep[1][axis].max);
}

/* the panel pass's items in a list: the flight-path marker's top and the two bars' free ends, in fine pixels, from the
 * records themselves (before: whatever the interpolation left in them) or, after, from the state's continuous values */
static void panel_sample(const interp_list *l, int after, double y[6], int has[6])
{
    static hud_panel pst, pdr;
    static hud_map mp;
    static uint8_t ds[0x10000];
    static hud_fine_list fl;
    int pvalid = 0, mvalid = 0;
    for (int q = 0; q < 6; q++) { y[q] = 0; has[q] = 0; }
    for (int i = 0; i < l->n; i++) {
        const interp_rec *r = &l->rec[i];
        if (r->kind == 'p') {
            pvalid = hud_panel_load(r->v, r->n, &pst);
            /* the waypoint cue: after, its fine centre from the drawn state (the bar of the full HUD's mark) */
            if (after && pvalid && hud_panel_load_drawn(r->v, r->n, &pdr, ds) && pdr.ext && hud_panel_exact(ds, &pdr, g_n, NULL, &fl) == 0)
                for (int k = 0; k < fl.n; k++) {
                    const hud_fprim *f = &fl.p[k];
                    if (f->item == PI_CUE && f->type == HF_LINE && fabs(f->y0 - f->y1) < 1e-9 && fabs(f->y0 - 11.5 * g_n) < 1e-6 &&
                        fabs(fabs(f->x1 - f->x0) - 4.0 * g_n) < 1e-6) { y[3] = (f->x0 + f->x1) / 2; has[3] = 1; }
                }
            continue;
        }
        if (r->kind == 'm') { mvalid = after ? hud_map_load_drawn(r->v, r->n, &mp) : 1; continue; }
        if (r->kind == 'S' && r->n >= 10 && r->v[0] == 73 && (int16_t)r->v[2 + 6] == 4 && (int16_t)r->v[2 + 7] == 4 &&
            (int16_t)r->v[2 + 2] == 4 && (r->v[2 + 1] & 0xFF00) == 0x100 && mvalid) {
            mvalid = 0;
            if (after) {
                double x, yy;
                static hud_map sk;
                memcpy(&sk, &mp, sizeof sk);
                sk.src_x = r->v[2 + 1];
                if (hud_map_classify(&sk, r->v, r->n, &x, &yy)) { y[4] = x * g_n; y[5] = yy * g_n; has[4] = has[5] = 1; }
            } else { y[4] = (int16_t)r->v[2 + 4] * (double)g_n; y[5] = (int16_t)r->v[2 + 5] * (double)g_n; has[4] = has[5] = 1; }
            continue;
        }
        /* before: the cue's bar as the list holds it (a library line at row 11, four cells wide) */
        if (!after && r->kind == 'N' && r->n >= 7 && r->v[6] == 0 && r->v[1] == 11 && r->v[3] == 11 && r->v[2] - r->v[0] == 4) {
            y[3] = (r->v[0] + 2 + 0.5) * g_n; has[3] = 1;
        }
        if (!pvalid) continue;
        hud_panel_item it;
        if (r->kind == 'S' && r->n >= 14 && r->v[0] == 71 && (int16_t)r->v[2 + 6] == 11 && (int16_t)r->v[2 + 7] == 7 && (int16_t)r->v[2 + 4] == 154) {
            if (after) { if (hud_panel_classify(&pst, 'S', r->v, r->n, &it) == HPI_FPM) { y[0] = it.y0 * g_n; has[0] = 1; } }
            else { y[0] = (int16_t)r->v[2 + 5] * (double)g_n; has[0] = 1; }
        } else if (r->kind == 'N' && r->n >= 7 && r->v[6] == 0 && (int16_t)r->v[0] == 247 && (int16_t)r->v[2] == 247 && (int16_t)r->v[1] == 52) {
            if (after) { if (hud_panel_classify(&pst, 'N', r->v, r->n, &it) == HPI_VS) { y[1] = (it.y1 + 0.5) * g_n; has[1] = 1; } }
            else { y[1] = ((int16_t)r->v[3] + 0.5) * g_n; has[1] = 1; }
        } else if (r->kind == 'N' && r->n >= 7 && r->v[6] == 0 && (int16_t)r->v[0] == 72 && (int16_t)r->v[2] == 72 && (int16_t)r->v[3] == 81) {
            if (after) { if (hud_panel_classify(&pst, 'N', r->v, r->n, &it) == HPI_STALL) { y[2] = (it.y0 + 0.5) * g_n; has[2] = 1; } }
            else { y[2] = ((int16_t)r->v[1] + 0.5) * g_n; has[2] = 1; }
        }
    }
}


static void sample_pair(const drawlive_step *a, const drawlive_step *b)
{
    static interp_list lo, ln;
    static sample_t so, sn;
    const double ts[4] = { 0, 0.25, 0.5, 0.75 };
    for (int k = 0; k < 4; k++) {
        int sb;
        interp_inbetween_ex(a->parsed, b->parsed, b->pair, ts[k], INTERP_VERTICES, &lo, &sb, NULL);
        interp_inbetween_ex(a->parsed, b->parsed, b->pair, ts[k], INTERP_VERTICES | INTERP_HUD | INTERP_HUD_HOLD, &ln, &sb, NULL);
        const int okn = sample_after(&ln, &sn), oko = sample_before(&lo, &so);
        if (!okn || !oko) continue;
        record(&g_new, &sn);
        record(&g_old, &so);
        double yn[6], yo[6];
        int hn[6], ho[6];
        panel_sample(&ln, 1, yn, hn);
        panel_sample(&lo, 0, yo, ho);
        for (int q = 0; q < 6; q++) {
            put_series(&g_new.pan[q], g_new.nsamples - 1, hn[q], 0, yn[q]);
            put_series(&g_old.pan[q], g_old.nsamples - 1, ho[q], 0, yo[q]);
        }
    }
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--series")) g_series = 1;
        else if (!path) path = argv[i];
        else g_n = atoi(argv[i]);
    }
    if (!path || g_n < 2 || g_n > 12) { fprintf(stderr, "usage: test_hud_jitter LOG [N 2..12] [--series]\n"); return 2; }
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot read %s\n", path); return 2; }
    static uint8_t *ydump[4096];
    char *buf = NULL;
    size_t cap = 0;
    char *line;
    static int32_t v[3 + 3 * 65536 + 16];
    int phases = 0;
    while ((line = read_line(f, &buf, &cap))) {
        const char k = line[0];
        if (k == 'P') { phases++; continue; }
        if (k != 'Y' || phases < 1 || phases >= 4096) continue;
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
    int phase = -1, deliver = 0;
    uint64_t last_seq = ~0ull;
    int pairs = 0;
    while ((line = read_line(f, &buf, &cap))) {
        const char k = line[0];
        if (k == '\n' || k == ' ' || !k) continue;
        char *p = line + 1;
        const uint64_t icount = strtoull(p, &p, 10);
        if (k == 'P') {
            phase++;
            if (phase < 4096 && ydump[phase]) memcpy(memory + 0xA0000, ydump[phase], 64000);
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
    printf("%d logic-frame pairs, %d samples per mode (four per logic step); second differences in fine pixels, N=%d\n", pairs, g_new.nsamples, g_n);
    summarise();
    if (g_series) {
        for (int k = 0; k < g_nkey; k++) {
            printf("series class %d '%s' end %d (sample, before x y, after x y)\n", g_key[k].cls, g_key[k].text, g_key[k].parity);
            for (int i = 0; i < g_old.s[k].n || i < g_new.s[k].n; i++)
                printf("%d %s %.2f %.2f %s %.2f %.2f\n", i, g_old.s[k].ok[i] ? "ok" : "--", g_old.s[k].x[i], g_old.s[k].y[i],
                       g_new.s[k].ok[i] ? "ok" : "--", g_new.s[k].x[i], g_new.s[k].y[i]);
        }
        printf("series ladder endpoint (sample, before x y, after x y)\n");
        for (int i = 0; i < g_old.pt.n || i < g_new.pt.n; i++)
            printf("%d %s %.2f %.2f %s %.2f %.2f\n", i, g_old.pt.ok[i] ? "ok" : "--", g_old.pt.x[i], g_old.pt.y[i],
                   g_new.pt.ok[i] ? "ok" : "--", g_new.pt.x[i], g_new.pt.y[i]);
    }
    return 0;
}
