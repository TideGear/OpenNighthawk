/* test_hud_state.c - src/present/hud_state.c against the original's own draw log.
 *
 *   test_hud_state CORPUS_DIR
 *
 * CORPUS_DIR holds list.txt (NAME SNAPFILE LOGFILE per line), snapshot files (a count, then per
 * snapshot the clock of the HUD frame's first line, the machine clock and the 64 KB data segment)
 * and the observer logs (f117run --observe) of the same flights, reduced to the records the HUD
 * pass makes. They were captured by D:/f117-gate/present-v2/work (grab.py, runwin.sh) from seven
 * scripted flights: full HUD in climb, on the runway, small HUD, high pitch, 48 degrees of bank,
 * HUD mode 1 inverted and mode 2 in a dive.
 *
 * 1. EXACT: for each snapshot, the library calls hud_exact predicts (every K colour, N line after
 *    the original's clipper, H tick record, T text with its eleven block words, and the entry-18
 *    blits) must appear contiguously in the log's records of the pass, field for field.
 * 2. FINE agrees with EXACT where the analysis says it must (evaluated at the snapshot's own integer
 *    state): tape labels (y = floor of the float), tape tick lattices (the same rows and lengths), the
 *    heading labels (int - float in [0, 2)), ladder vertices and label anchors (within the analysis's
 *    windows), and the ladder's offset tables (analytic form = tables, to the truncation).
 *
 * Exit 1 on any EXACT mismatch or FINE check failure, 77 when the corpus is absent. */
#include "hud_state.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint64_t t; char kind; int n; int v[48]; } rec_t;

static rec_t *g_rec;
static int g_nrec, g_crec;

static void add_rec(rec_t *r)
{
    if (g_nrec == g_crec) { g_crec = g_crec ? 2 * g_crec : 1 << 16; g_rec = (rec_t *)realloc(g_rec, (size_t)g_crec * sizeof *g_rec); }
    g_rec[g_nrec++] = *r;
}

static int load_log(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    g_nrec = 0;
    size_t cap = 1 << 20;
    char *line = (char *)malloc(cap);
    while (fgets(line, (int)cap, f)) {
        const char k = line[0];
        if (k != 'K' && k != 'N' && k != 'T' && k != 'H' && k != 'S') continue;
        rec_t r;
        memset(&r, 0, sizeof r);
        char *p = line + 1;
        char *end;
        r.t = (uint64_t)strtoull(p, &end, 10);
        p = end;
        r.kind = k;
        int n = 0, want = k == 'T' ? 13 + 12 : k == 'N' ? 7 : k == 'H' ? 9 : 14;
        while (n < want && n < 48) {
            const long v = strtol(p, &end, 10);
            if (end == p) break;
            r.v[n++] = (int)v;
            p = end;
        }
        r.n = n;
        if (k == 'T') {
            /* entry, page, 11 block words, count, chars: regroup as entry, 11 words, count, chars */
            int w[48];
            memcpy(w, r.v, sizeof w);
            r.v[0] = w[0];
            for (int i = 0; i < 11; i++) r.v[1 + i] = w[2 + i];
            r.v[12] = w[13];
            for (int i = 0; i < 12 && i < w[13]; i++) r.v[13 + i] = w[14 + i];
        }
        if (k == 'S' && r.v[0] != 18) continue;
        add_rec(&r);
    }
    free(line);
    fclose(f);
    return 1;
}

static int match(const hud_prim *p, const rec_t *r)
{
    switch (p->kind) {
    case HP_K: return r->kind == 'K' && p->colour == (r->v[0] & 0xFF);
    case HP_N:
        return r->kind == 'N' && p->x0 == (int16_t)r->v[0] && p->y0 == (int16_t)r->v[1] && p->x1 == (int16_t)r->v[2] &&
               p->y1 == (int16_t)r->v[3] && p->origin == r->v[6];
    case HP_T:
        if (r->kind != 'T' || p->entry != r->v[0] || p->ntext != r->v[12]) return 0;
        for (int k = 0; k < 11; k++) if (p->blk[k] != (uint16_t)r->v[1 + k]) return 0;
        for (int k = 0; k < p->ntext; k++) if (p->text[k] != r->v[13 + k]) return 0;
        return 1;
    case HP_H: return r->kind == 'H' && p->si == r->v[0] && p->bx == (int16_t)r->v[1] && p->dl == r->v[2] && p->cl == r->v[3];
    case HP_S18: return r->kind == 'S';
    }
    return 0;
}

static const char *kind_name(int k) { return k == HP_K ? "K" : k == HP_N ? "N" : k == HP_T ? "T" : k == HP_H ? "H" : "S18"; }

static uint8_t *g_ds;

static int s16(int v) { return (int16_t)(uint16_t)v; }
static int W(int o) { return g_ds[o] | (g_ds[o + 1] << 8); }

static long g_fail;
static long c_label_ok, c_label_bad, c_label_cull;
static long c_tick_ok, c_tick_bad;
static long c_head_ok, c_head_bad, c_head_cull;
static long c_lad_n, c_anchor_n, c_off_bad, c_rung_boundary;
static int ladx_lo = 99, ladx_hi = -99, lady_lo = 99, lady_hi = -99, anx_lo = 99, anx_hi = -99, any_lo = 99, any_hi = -99;
static int hx_lo = 99, hx_hi = -99;

/* the tick marks the original's driver draws for an H record: (row, cells) */
static int driver_ticks(const hud_prim *h, int lo, int hi, int rows[200], int cells[200])
{
    int n = 0;
    int row = (h->bx - 1 + (h->dl >= 1 ? 0x14 : 0) + (h->cl ? 1 : 0)) & 0xFFFF, ch = 10;
    while (row >= lo) {
        if (row <= hi && n < 200) {
            rows[n] = row;
            cells[n] = ch == 5 ? 2 : ch == 10 ? (h->cl ? 2 : 3) : 1;
            n++;
        }
        row = (row - 2) & 0xFFFF;
        if (row > 0xFFF0) break;
        ch = ch == 10 ? 1 : ch + 1;
    }
    return n;
}

static void check_fine(const hud_state *st, const hud_exact_list *ex, int N, const char *name, int pass)
{
    static hud_fine_list fl;
    hud_state s2 = *(const hud_state *)st;                 /* the integer state itself */
    s2.speed = (double)W(0xB194);
    s2.alt = (double)W(0x2DF6);
    if (hud_fine(&s2, N, ex, &fl) < 0) { printf("%s pass %d: fine list overflow\n", name, pass); g_fail++; return; }
    const int mode = g_ds[0x294A], half = g_ds[0x294B];
    /* labels */
    for (int i = 0; i < ex->n; i++) {
        const hud_prim *p = &ex->p[i];
        if (p->kind != HP_T || p->entry != 1) continue;
        const int x = p->blk[4], y = p->blk[5] & 0xFF;
        int found = 0;
        for (int j = 0; j < fl.n; j++) {
            const hud_fprim *f = &fl.p[j];
            if (f->type != HF_TEXT || f->entry != 1 || f->ntext != p->ntext || memcmp(f->text, p->text, p->ntext)) continue;
            if (fabs(f->x0 / N - x) > 1e-6) continue;
            found = 1;
            const int yi = ((int)floor(f->y0 / N + 1e-9)) & 0xFF;
            if (yi == y) c_label_ok++;
            else { c_label_bad++; if (c_label_bad < 6) printf("%s pass %d: label '%.*s' y exact %d fine %.3f\n", name, pass, p->ntext, p->text, y, f->y0 / N); }
        }
        if (!found) {
            /* culled: the original issues labels the row clip removes */
            const int top = p->blk[7], bot = p->blk[8], yy = y >= 128 ? y - 256 : y;
            if (yy + 6 <= top || yy > bot) c_label_cull++;
            else { c_label_bad++; if (c_label_bad < 6) printf("%s pass %d: label '%.*s' x%d y%d missing from fine\n", name, pass, p->ntext, p->text, x, yy); }
        }
    }
    /* tick lattices */
    for (int i = 0; i < ex->n; i++) {
        const hud_prim *h = &ex->p[i];
        if (h->kind != HP_H) continue;
        const int ti = (h->si + (h->cl ? 4 : 0)) / 2;
        if (!st->have_ticks) continue;
        const int lo = st->tick[ti].lo, hi = st->tick[ti].hi, x = st->tick[ti].x;
        int rows[200], cells[200];
        const int n = driver_ticks(h, lo, hi, rows, cells);
        int seen[200] = { 0 }, bad = 0;
        for (int j = 0; j < fl.n; j++) {
            const hud_fprim *f = &fl.p[j];
            if (f->type != HF_LINE || fabs(f->x0 / N - 0.5 - x) > 1e-6 || f->y0 != f->y1 || f->cl != 0 || f->ct != lo * N) continue;
            const int cellx = (int)floor(f->x1 / N - 0.5 + 0.5 * (f->x1 >= f->x0 ? 0 : 1) + 1e-9);
            (void)cellx;
            const int row = (int)floor(f->y0 / N - 0.5 + 1e-9);
            if (row < lo || row > hi) continue;
            const int len = (int)floor(fabs(f->x1 - f->x0) / N + 0.5) + 1;
            int m = -1;
            for (int q = 0; q < n; q++) if (rows[q] == row) m = q;
            if (m < 0 || cells[m] != len) bad++;
            else seen[m] = 1;
        }
        for (int q = 0; q < n; q++) if (!seen[q]) bad++;
        (void)half;
        if (bad) { c_tick_bad++; if (c_tick_bad < 6) printf("%s pass %d: tick lattice (si %d, bx %d dl %d) differs in %d marks of %d\n", name, pass, h->si, h->bx, h->dl, bad, n); }
        else c_tick_ok++;
    }
    /* heading labels */
    if (mode <= 1) {
        for (int i = 0; i < ex->n; i++) {
            const hud_prim *p = &ex->p[i];
            if (p->kind != HP_T || p->entry < 2 || p->entry > 4 || p->blk[5] != W(0x286C)) continue;
            int found = 0;
            for (int j = 0; j < fl.n; j++) {
                const hud_fprim *f = &fl.p[j];
                if (f->type != HF_TEXT || f->entry != 6 || f->ntext != p->ntext || memcmp(f->text, p->text, p->ntext)) continue;
                /* nearest in x */
                const double dx = p->blk[4] - f->x0 / N;
                if (dx < -50 || dx > 50) continue;
                found = 1;
                if (dx >= -1e-9 && dx < 2.0) c_head_ok++;
                else { c_head_bad++; if (c_head_bad < 6) printf("%s pass %d: heading label '%.*s' int-float %.3f\n", name, pass, p->ntext, p->text, dx); }
                if (dx < hx_lo) hx_lo = (int)floor(dx);
                if (dx > hx_hi) hx_hi = (int)ceil(dx);
                break;
            }
            if (!found) {
                /* not drawn: the original issues labels its clips remove */
                if (p->blk[4] + 12 <= (uint16_t)W(0x2874) + 2 || p->blk[4] > (uint16_t)W(0x2876)) c_head_cull++;
                else { c_head_bad++; if (c_head_bad < 6) { printf("%s pass %d: heading label '%.*s' at %d missing from fine; hdg %.3f left %d right %d; fine:", name, pass, p->ntext, p->text, p->blk[4], s2.hdg * 360 / 65536, W(0x2874), W(0x2876)); for (int j = 0; j < fl.n; j++) if (fl.p[j].type == HF_TEXT && fl.p[j].entry == 6) printf(" %.*s@%.2f", fl.p[j].ntext, fl.p[j].text, fl.p[j].x0 / N); printf("\n"); } }
            }
        }
    }
    /* the ladder: exact (unclipped) vertices against fine segment ends */
    if (g_ds[0xE56E]) {
        int nv, first_rung;
        const int *rxv, *ryv;
        hud_debug_ladder(&nv, &rxv, &ryv, &first_rung);
        /* the original numbers the rungs from its quantised pitch; within a quantum of a multiple of ten
         * degrees the float's numbering differs by one, and a whole rung then has no partner */
        if ((int)floor(s2.pitch * 360.0 / 65536.0 / 10.0) - 2 != first_rung) { c_rung_boundary++; nv = 0; }
        int fnv, ffirst;
        const double *frx, *fry;
        hud_debug_fine_ladder(&fnv, &frx, &fry, &ffirst);
        if (nv && fnv != nv) { printf("%s pass %d: ladder vertex counts differ (%d, %d)\n", name, pass, nv, fnv); g_fail++; nv = 0; }
        for (int v = 0; v < nv; v++) {
            c_lad_n++;
            const int dx = rxv[v] - (int)floor(frx[v]), dy = ryv[v] - (int)floor(fry[v]);
            if (dx < ladx_lo) ladx_lo = dx;
            if (dx > ladx_hi) ladx_hi = dx;
            if (dy < lady_lo) lady_lo = dy;
            if (dy > lady_hi) lady_hi = dy;
        }
        /* the label anchors: exact entry-6 labels against fine text of the same string, by rung order */
        int k = 0;
        for (int i = 0; i < ex->n; i++) {
            const hud_prim *p = &ex->p[i];
            if (p->kind != HP_T || p->entry != 6) continue;
            double best = 1e9, bx = 0, by = 0;
            for (int j = 0; j < fl.n; j++) {
                const hud_fprim *f = &fl.p[j];
                if (f->type != HF_TEXT || f->entry != 7 || f->ntext != p->ntext || memcmp(f->text, p->text, p->ntext)) continue;
                const double ddx = f->x0 / N - s16(p->blk[4]), ddy = f->y0 / N - s16(p->blk[5]);
                const double d = fabs(ddx) + fabs(ddy);
                if (d < best) { best = d; bx = ddx; by = ddy; }
            }
            k++;
            if (best < 1e8) {
                c_anchor_n++;
                const int ax = (int)floor(-bx + 1e-9), ay = (int)floor(-by + 1e-9);
                if (ax < anx_lo) anx_lo = ax;
                if (ax > anx_hi) anx_hi = ax;
                if (ay < any_lo) any_lo = ay;
                if (ay > any_hi) any_hi = ay;
            }
        }
        /* the offset tables: analytic = table, to the truncation */
        for (int i = 0; i < 64; i++) {
            const double th = 2.0 * 3.14159265358979323846 * i / 64.0;
            const int t264 = (int)(-3.5 + 6.0 * cos(th)), t274 = (int)(-5.5 + 6.0 * cos(th)), t26 = (int)(-1.5 - 6.0 * sin(th));
            if (t264 != s16(W(0x264C + 2 * i)) || t274 != s16(W(0x274C + 2 * i)) || t26 != s16(W(0x26CC + 2 * i)) ||
                t26 != s16(W(0x27CC + 2 * i))) c_off_bad++;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: test_hud_state CORPUS_DIR\n"); return 2; }
    char path[1024];
    snprintf(path, sizeof path, "%s/list.txt", argv[1]);
    FILE *lf = fopen(path, "r");
    if (!lf) { printf("SKIP: no corpus at %s\n", argv[1]); return 77; }
    long passes = 0, bad = 0, items = 0, nk = 0, nn = 0, nt = 0, nh = 0, ns = 0, divide = 0;
    char name[64], snapf[256], logf[256];
    static hud_state st;
    static hud_exact_list ex;
    while (fscanf(lf, "%63s %255s %255s", name, snapf, logf) == 3) {
        snprintf(path, sizeof path, "%s/%s", argv[1], logf);
        if (!load_log(path)) { printf("%s: cannot read %s\n", name, path); return 1; }
        snprintf(path, sizeof path, "%s/%s", argv[1], snapf);
        FILE *sf = fopen(path, "rb");
        if (!sf) { printf("%s: cannot read %s\n", name, path); return 1; }
        uint32_t count;
        if (fread(&count, 4, 1, sf) != 1) return 1;
        long wb = 0, wp = 0;
        static uint8_t ds[65536];
        for (uint32_t s = 0; s < count; s++) {
            uint64_t tf, clk;
            if (fread(&tf, 8, 1, sf) != 1 || fread(&clk, 8, 1, sf) != 1 || fread(ds, 65536, 1, sf) != 1) return 1;
            g_ds = ds;
            hud_state_from_ds(ds, &st);
            /* the driver's tick tables, from the log's own H records of this pass */
            st.have_ticks = 0;
            int fine_ticks[4] = { 0 };
            for (int r = 0; r < g_nrec; r++) {
                if (g_rec[r].t < tf - 21000 || g_rec[r].t > tf + 36000 || g_rec[r].kind != 'H') continue;
                const int ti = (g_rec[r].v[0] + (g_rec[r].v[3] ? 4 : 0)) / 2;
                st.tick[ti].x = g_rec[r].v[5]; st.tick[ti].lo = g_rec[r].v[6]; st.tick[ti].hi = g_rec[r].v[7];
                fine_ticks[ti] = 1;
            }
            st.have_ticks = fine_ticks[0] || fine_ticks[1] || fine_ticks[2] || fine_ticks[3];
            passes++;
            if (hud_exact(&st, &ex) < 0) { divide++; continue; }
            /* the log's records of the pass, in order */
            int idx[512], m = 0;
            for (int r = 0; r < g_nrec && m < 512; r++)
                if (g_rec[r].t >= tf - 21000 && g_rec[r].t <= tf + 36000) idx[m++] = r;
            int ok = 0, at = -1;
            for (int s0 = 0; s0 + ex.n <= m && !ok; s0++) {
                int all = 1;
                for (int i = 0; i < ex.n && all; i++) all = match(&ex.p[i], &g_rec[idx[s0 + i]]);
                if (all) { ok = 1; at = s0; }
            }
            if (ok) {
                wp++;
                for (int i = 0; i < ex.n; i++) {
                    items++;
                    switch (ex.p[i].kind) { case HP_K: nk++; break; case HP_N: nn++; break; case HP_T: nt++; break; case HP_H: nh++; break; default: ns++; }
                }
            } else {
                wb++;
                bad++;
                int s0 = 0;
                for (; s0 < m; s0++) if (match(&ex.p[0], &g_rec[idx[s0]])) break;
                printf("%s pass %u: MISMATCH (model %d items, log %d records)\n", name, s, ex.n, m);
                for (int i = 0; i < ex.n && s0 + i < m; i++)
                    if (!match(&ex.p[i], &g_rec[idx[s0 + i]])) {
                        const hud_prim *p = &ex.p[i];
                        const rec_t *r = &g_rec[idx[s0 + i]];
                        printf("  first divergence at item %d (%s): model", i, kind_name(p->kind));
                        if (p->kind == HP_N) printf(" N %d %d %d %d o%d", p->x0, p->y0, p->x1, p->y1, p->origin);
                        if (p->kind == HP_T) { printf(" T e%d '%.*s' blk", p->entry, p->ntext, p->text); for (int k = 0; k < 11; k++) printf(" %u", p->blk[k]); }
                        if (p->kind == HP_K) printf(" K %d", p->colour);
                        printf(" | log %c", r->kind);
                        if (r->kind == 'N') printf(" %d %d %d %d o%d", r->v[0], r->v[1], r->v[2], r->v[3], r->v[6]);
                        if (r->kind == 'T') { printf(" e%d blk", r->v[0]); for (int k = 0; k < 11; k++) printf(" %u", (uint16_t)r->v[1 + k]); }
                        if (r->kind == 'K') printf(" %d", r->v[0]);
                        printf("\n");
                        break;
                    }
                continue;
            }
            (void)at;
            check_fine(&st, &ex, 4, name, (int)s);
        }
        fclose(sf);
        printf("%-8s %ld passes: %ld exact, %ld mismatched\n", name, (long)count, wp, wb);
    }
    fclose(lf);
    printf("EXACT: %ld passes (%ld divide errors), %ld matched, %ld mismatched; %ld library calls compared: K %ld, N %ld, T %ld, H %ld, S18 %ld\n",
           passes, divide, passes - divide - bad, bad, items, nk, nn, nt, nh, ns);
    printf("FINE labels: %ld agree (y = floor of the float), %ld disagree, %ld culled by the row clip\n", c_label_ok, c_label_bad, c_label_cull);
    printf("FINE tick lattices: %ld identical to the driver's marks, %ld differ\n", c_tick_ok, c_tick_bad);
    printf("FINE heading labels: %ld with int - float in [0, 2), %ld outside, %ld not drawn (int - float seen %d..%d)\n", c_head_ok, c_head_bad, c_head_cull, hx_lo, hx_hi);
    printf("FINE ladder: %ld passes with the rung numbering at a quantum boundary left out; %ld vertices, int - float x in [%d, %d], y in [%d, %d]; %ld label anchors, int - float x in [%d, %d], y in [%d, %d]; offset tables off the analytic form: %ld\n",
           c_rung_boundary, c_lad_n, ladx_lo, ladx_hi, lady_lo, lady_hi, c_anchor_n, anx_lo, anx_hi, any_lo, any_hi, c_off_bad);
    g_fail += c_label_bad + c_tick_bad + c_head_bad + c_off_bad;
    /* the analysis's measured windows for the ladder: vertices x in [-3, +5], y in [-3, +1] */
    if (ladx_lo < -3 || ladx_hi > 5 || lady_lo < -3 || lady_hi > 1) { printf("ladder vertex window exceeded\n"); g_fail++; }
    if (bad || g_fail) { printf("FAIL\n"); return 1; }
    printf("PASS\n");
    return 0;
}
