/* test_hud_logs.c - the state-driven HUD's EXACT models against observer logs that carry the HUD's state records.
 *
 *   test_hud_logs LIST
 *
 * LIST names observer logs (f117run --observe with F117R_OBSERVE_HUD=1), one per line, relative to the list's folder.
 * Every state record is checked against the library calls that follow it in the same log:
 *   'h' (the HUD pass, 1058:0016): hud_exact's calls must be the next K, N, T, H and entry-18 records, contiguous,
 *       field for field (as test_hud_state.c does from data-segment snapshots);
 *   'p' with the pass's inputs (the panel pass, 0000:79BE): hud_panel_exact's calls must all come, in order and field
 *       for field, before the HUD pass starts (other calls of the pass come between them);
 *   'm' (the map arrow, 0000:3ADA): the next entry-73 blit must be the arrow hud_map_classify expects, and the record's
 *       16- and 32-bit positions must agree ([C0D0] = ([BA3C] + 16) >> 5, [C0DE] = 8000h - (([C056] + 16) >> 5)).
 * FINE is evaluated at each record's own integer state and compared with the integer positions: the waypoint cue,
 * the flight-path marker, the bars and the map arrow (integer minus float, reported as ranges).
 * The report counts the passes by layout and mode, altitude band, ladder switch and the rungs at |n| >= 9, so it
 * says which cases the logs exercise. Exit 1 on any mismatch, 77 when LIST is absent. */
#include "hud_state.h"

#include <math.h>
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

static int parse(char *p, int32_t *v, int cap)
{
    int n = 0;
    for (;;) {
        char *e;
        const long x = strtol(p, &e, 10);
        if (e == p || n == cap) break;
        v[n++] = (int32_t)x;
        p = e;
    }
    return n;
}

/* a library record as hud_prim fields: T is entry, page, 11 words, count, chars */
static int match(const hud_prim *p, char kind, const int32_t *v, int n)
{
    switch (p->kind) {
    case HP_K: return kind == 'K' && n >= 1 && p->colour == (v[0] & 0xFF);
    case HP_N:
        return kind == 'N' && n >= 7 && p->x0 == (int16_t)v[0] && p->y0 == (int16_t)v[1] && p->x1 == (int16_t)v[2] &&
               p->y1 == (int16_t)v[3] && p->origin == v[6];
    case HP_T:
        if (kind != 'T' || n < 14 || p->entry != v[0] || p->ntext != v[13] || 14 + v[13] > n) return 0;
        for (int k = 0; k < 11; k++) if (p->blk[k] != (uint16_t)v[2 + k]) return 0;
        for (int k = 0; k < p->ntext; k++) if (p->text[k] != v[14 + k]) return 0;
        return 1;
    case HP_H: return kind == 'H' && n >= 4 && p->si == v[0] && p->bx == (int16_t)v[1] && p->dl == v[2] && p->cl == v[3];
    case HP_S18:
        if (kind != 'S' || n < 10 || v[0] != 18) return 0;
        for (int k = 0; k < 8; k++) if (p->blk[k] != (uint16_t)v[2 + k]) return 0;
        return 1;
    case HP_S:
        if (kind != 'S' || n < 10 || v[0] != p->entry) return 0;
        for (int k = 0; k < 8; k++) if (p->blk[k] != (uint16_t)v[2 + k]) return 0;
        return 1;
    }
    return 0;
}

static void describe(const hud_prim *p)
{
    if (p->kind == HP_T) printf("T e%d '%.*s' at %d,%d", p->entry, p->ntext, p->text, (int16_t)p->blk[4], (int16_t)p->blk[5]);
    else if (p->kind == HP_N) printf("N %d %d %d %d o%d", p->x0, p->y0, p->x1, p->y1, p->origin);
    else if (p->kind == HP_K) printf("K %d", p->colour);
    else if (p->kind == HP_H) printf("H %d %d %d %d", p->si, p->bx, p->dl, p->cl);
    else printf("S%d %u,%u -> %d,%d", p->kind == HP_S ? p->entry : 18, p->blk[1], p->blk[2], (int16_t)p->blk[4], (int16_t)p->blk[5]);
}

static void describe_rec(char kind, const int32_t *v, int n)
{
    if (kind == 'T' && n >= 14) {
        printf("T e%d '", v[0]);
        for (int k = 0; k < v[13] && 14 + k < n && k < 40; k++) putchar(v[14 + k]);
        printf("' at %d,%d", v[2 + 4], v[2 + 5]);
    } else if (kind == 'N' && n >= 7) printf("N %d %d %d %d o%d", v[0], v[1], v[2], v[3], v[6]);
    else if (kind == 'K') printf("K %d", v[0]);
    else if (kind == 'H') printf("H %d %d %d %d", v[0], v[1], v[2], v[3]);
    else if (kind == 'S' && n >= 10) printf("S%d %d,%d -> %d,%d", v[0], v[3], v[4], v[6], v[7]);
    else printf("%c", kind);
}

static long fails;
/* the HUD pass */
static hud_state g_st;
static hud_exact_list g_ex;
static int g_hud_on, g_hud_at;
static long hud_passes, hud_ok, hud_bad, hud_calls, hud_items[HI_COUNT];
static long by_layout[2][3], alt_band[3], ladder_off, rungs9, reticle_passes;
/* the panel pass */
static hud_panel g_pn;
static uint8_t g_pds[0x10000];
static hud_exact_list g_pex;
static int g_panel_on, g_panel_at;
static long panel_passes, panel_ok, panel_bad, panel_calls, panel_items[HI_COUNT], panel_v1;
static long panel_by_layout[2][3];
static hud_fine_list g_pfl;
static double cue_lo = 9, cue_hi = -9, fpm_lo = 9, fpm_hi = -9, vs_lo = 9, vs_hi = -9, st_lo = 9, st_hi = -9;
static long n_cue, n_fpm, n_vs, n_st;
/* the map arrow */
static hud_map g_map;
static int g_map_on;
static long map_recs, map_ok, map_bad, map_pos_ok, map_pos_bad, map_sprites[8];
static double mx_lo = 9, mx_hi = -9, my_lo = 9, my_hi = -9;

static void panel_fine_check(void)
{
    /* FINE at the record's own integer state: the positions of the items whose fine places are continuous */
    if (hud_panel_exact(g_pds, &g_pn, 4, NULL, &g_pfl) < 0) return;
    for (int i = 0; i < g_pfl.n; i++) {
        const hud_fprim *f = &g_pfl.p[i];
        for (int k = 0; k < g_pex.n; k++) {
            const hud_prim *p = &g_pex.p[k];
            if (p->item != f->item) continue;
            if (f->item == PI_CUE && f->type == HF_LINE && p->kind == HP_N) {
                /* the cue's last line (the bar) or the mode-2 dot: x0 of the fine stroke against the line's x0 */
                const double fx = f->x0 / 4 - 0.5;
                const double d = p->x0 - fx;
                if (fabs(d - round(d)) < 1e-9 || 1) {
                    if (fabs(f->y0 / 4 - 0.5 - p->y0) < 1e-6 && fabs(f->y1 / 4 - 0.5 - p->y1) < 1e-6) {
                        if (d < cue_lo) cue_lo = d;
                        if (d > cue_hi) cue_hi = d;
                        n_cue++;
                        break;
                    }
                }
            }
            if (f->item == PI_FPM && f->type == HF_SPRITE && p->kind == HP_S) {
                const double d = (int16_t)p->blk[5] - f->y0 / 4;
                if (d < fpm_lo) fpm_lo = d;
                if (d > fpm_hi) fpm_hi = d;
                n_fpm++;
                break;
            }
            if (f->item == PI_VS && p->kind == HP_N) {
                const double d = p->y1 - (f->y1 / 4 - 0.5);
                if (d < vs_lo) vs_lo = d;
                if (d > vs_hi) vs_hi = d;
                n_vs++;
                break;
            }
            if (f->item == PI_STALL && p->kind == HP_N) {
                const double d = p->y0 - (f->y0 / 4 - 0.5);
                if (d < st_lo) st_lo = d;
                if (d > st_hi) st_hi = d;
                n_st++;
                break;
            }
        }
    }
}

static void end_panel(void)
{
    if (!g_panel_on) return;
    g_panel_on = 0;
    if (g_panel_at == g_pex.n) {
        panel_ok++;
        panel_calls += g_pex.n;
        for (int i = 0; i < g_pex.n; i++) if (g_pex.p[i].kind != HP_K) panel_items[g_pex.p[i].item]++;
        panel_fine_check();
        return;
    }
    panel_bad++;
    fails++;
    if (panel_bad < 10) {
        printf("panel pass: item %d of %d not seen: ", g_panel_at, g_pex.n);
        describe(&g_pex.p[g_panel_at]);
        printf(" (layout %d mode %d)\n", g_pn.half, g_pn.mode);
    }
}

static void on_record(char kind, uint64_t t, const int32_t *v, int n)
{
    (void)t;
    if (kind == 'P') { end_panel(); g_hud_on = 0; g_map_on = 0; return; }
    if (kind == 'h') {
        end_panel();
        g_hud_on = 0;
        if (!hud_state_load(v, n, &g_st)) return;
        hud_passes++;
        if (hud_exact(&g_st, &g_ex) < 0) { printf("HUD pass: the model fails (divide error)\n"); fails++; return; }
        g_hud_on = 1; g_hud_at = 0;
        const int half = g_st.ds[0x294B] != 0, mode = g_st.ds[0x294A];
        if (mode <= 2) by_layout[half][mode]++;
        const int alt = g_st.ds[HUD_ALT] | g_st.ds[HUD_ALT + 1] << 8;
        alt_band[alt < 1000 ? 0 : alt < 10000 ? 1 : 2]++;
        if (!g_st.ds[HUD_LADDER_ON]) ladder_off++;
        int nv, first;
        const int *rx, *ry;
        hud_debug_ladder(&nv, &rx, &ry, &first);
        (void)nv; (void)rx; (void)ry;
        if (first <= -9 || first + 4 >= 9) rungs9++;      /* the five rungs are first .. first + 4 */
        return;
    }
    if (kind == 'p') {
        end_panel();
        if (!hud_panel_load_ds(v, n, &g_pn, g_pds)) return;
        if (!g_pn.ext) { panel_v1++; return; }
        panel_passes++;
        if (hud_panel_exact(g_pds, &g_pn, 1, &g_pex, NULL) < 0) { printf("panel pass: the model fails\n"); fails++; return; }
        if (g_pn.half <= 1 && g_pn.mode <= 2) panel_by_layout[g_pn.half][g_pn.mode]++;
        g_panel_on = 1; g_panel_at = 0;
        return;
    }
    if (kind == 'm') {
        if (!hud_map_load(v, n, &g_map)) return;
        map_recs++;
        g_map_on = 1;
        /* the 16-bit positions are the 32-bit ones rounded to whole units: [C0D0] = ([BA3C] + 16) >> 5,
         * [C0DE] = 8000h - (([C056] + 16) >> 5) */
        const int ok = (((g_map.px + 16) >> 5) & 0xFFFF) == g_map.x0;
        const int oky = ((0x8000 - ((g_map.py + 16) >> 5)) & 0xFFFF) == g_map.y0;
        if (ok && oky) map_pos_ok++;
        else { map_pos_bad++; if (map_pos_bad < 5) printf("map: [C0D0] %d [BA3C] %d, [C0DE] %d [C056] %d\n", g_map.x0, g_map.px, g_map.y0, g_map.py); }
        return;
    }
    if (kind != 'K' && kind != 'N' && kind != 'T' && kind != 'H' && kind != 'S') return;
    if (g_hud_on) {
        if (kind == 'S' && n >= 1 && v[0] != 18) goto other;
        if (match(&g_ex.p[g_hud_at], kind, v, n)) {
            g_hud_at++;
            if (g_hud_at == g_ex.n) {
                g_hud_on = 0;
                hud_ok++;
                hud_calls += g_ex.n;
                for (int i = 0; i < g_ex.n; i++) {
                    if (g_ex.p[i].kind != HP_K) hud_items[g_ex.p[i].item]++;
                    if (g_ex.p[i].item == HI_RETICLE) reticle_passes++;
                }
            }
            return;
        }
        g_hud_on = 0;
        hud_bad++;
        fails++;
        if (hud_bad < 10) {
            printf("HUD pass: call %d of %d differs: model ", g_hud_at, g_ex.n);
            describe(&g_ex.p[g_hud_at]);
            printf(", log ");
            describe_rec(kind, v, n);
            printf(" (layout %d mode %d alt %d)\n", g_st.ds[0x294B], g_st.ds[0x294A], g_st.ds[HUD_ALT] | g_st.ds[HUD_ALT + 1] << 8);
        }
        return;
    }
other:
    if (g_panel_on && g_panel_at < g_pex.n && match(&g_pex.p[g_panel_at], kind, v, n)) {
        g_panel_at++;
        return;
    }
    if (g_map_on && kind == 'S' && n >= 30 && v[0] == 73) {
        g_map_on = 0;
        double x, y;
        if (hud_map_classify(&g_map, v, n, &x, &y)) {
            map_ok++;
            map_sprites[((g_map.src_x - 0x100) / 4) & 7]++;
            const double dx = (int16_t)v[2 + 4] - x, dy = (int16_t)v[2 + 5] - y;
            if (dx < mx_lo) mx_lo = dx;
            if (dx > mx_hi) mx_hi = dx;
            if (dy < my_lo) my_lo = dy;
            if (dy > my_hi) my_hi = dy;
            int same = 1;
            for (int k = 0; k < 16; k++) same &= (v[14 + k] & 0xFF) == g_map.art[k];
            if (!same) { map_bad++; fails++; if (map_bad < 5) printf("map: the record's art differs from the blit's\n"); }
        } else {
            map_bad++;
            fails++;
            if (map_bad < 5) printf("map: blit %d,%d -> %d,%d not the arrow of its record (expected sx %d at %d,%d)\n", v[3], v[4], v[6], v[7],
                                    g_map.src_x, g_map.sx_screen - 1, g_map.sy_screen - 1);
        }
    }
}

static int32_t g_v[3 + 3 * 65536 + 16];

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: test_hud_logs LIST\n"); return 2; }
    FILE *lf = fopen(argv[1], "r");
    if (!lf) { printf("SKIP: no list at %s\n", argv[1]); return 77; }
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", argv[1]);
    char *slash = strrchr(dir, '/');
    char *bslash = strrchr(dir, '\\');
    if (bslash > slash) slash = bslash;
    if (slash) slash[1] = 0; else dir[0] = 0;
    char name[512];
    char *buf = NULL;
    size_t cap = 0;
    while (fscanf(lf, "%511s", name) == 1) {
        char path[1600];
        snprintf(path, sizeof path, "%s%s", dir, name);
        FILE *f = fopen(path, "r");
        if (!f) { printf("%s: cannot read\n", path); fails++; continue; }
        const long h0 = hud_passes, p0 = panel_passes, m0 = map_recs, hb = hud_bad, pb = panel_bad, mb = map_bad;
        char *line;
        while ((line = read_line(f, &buf, &cap))) {
            const char k = line[0];
            if (!strchr("PhpmKNTHS", k) || !k) continue;
            char *p = line + 1;
            const uint64_t t = strtoull(p, &p, 10);
            const int n = parse(p, g_v, (int)(sizeof g_v / sizeof g_v[0]));
            on_record(k, t, g_v, n);
        }
        end_panel();
        g_hud_on = 0; g_map_on = 0;
        fclose(f);
        printf("%-40s HUD passes %ld (%ld differ), panel passes %ld (%ld differ), map arrows %ld (%ld differ)\n", name, hud_passes - h0,
               hud_bad - hb, panel_passes - p0, panel_bad - pb, map_recs - m0, map_bad - mb);
    }
    fclose(lf);
    printf("HUD pass EXACT: %ld passes, %ld equal to the log (%ld calls), %ld differ; items:", hud_passes, hud_ok, hud_calls, hud_bad);
    for (int k = 0; k < HI_COUNT; k++) if (hud_items[k]) printf(" %d:%ld", k, hud_items[k]);
    printf("\n  layouts (full mode 0/1/2, small mode 0/1/2): %ld %ld %ld / %ld %ld %ld; altitude below 1,000 / to 10,000 / above: %ld %ld %ld;"
           " ladder off %ld; passes with a rung at |n| >= 9: %ld\n",
           by_layout[0][0], by_layout[0][1], by_layout[0][2], by_layout[1][0], by_layout[1][1], by_layout[1][2], alt_band[0], alt_band[1],
           alt_band[2], ladder_off, rungs9);
    printf("panel pass EXACT: %ld passes with the pass's inputs (%ld first-version records), %ld with every predicted call in order (%ld calls), %ld not; items:",
           panel_passes, panel_v1, panel_ok, panel_calls, panel_bad);
    for (int k = 0; k < HI_COUNT; k++) if (panel_items[k]) printf(" %d:%ld", k, panel_items[k]);
    printf("\n  layouts (full mode 0/1/2, small mode 0/1/2): %ld %ld %ld / %ld %ld %ld\n", panel_by_layout[0][0], panel_by_layout[0][1],
           panel_by_layout[0][2], panel_by_layout[1][0], panel_by_layout[1][1], panel_by_layout[1][2]);
    printf("panel FINE at the integer state, integer minus float: cue x [%.2f, %.2f] (%ld), marker top [%.2f, %.2f] (%ld), vertical-speed bar end [%.2f, %.2f] (%ld), stall bar top [%.2f, %.2f] (%ld)\n",
           cue_lo, cue_hi, n_cue, fpm_lo, fpm_hi, n_fpm, vs_lo, vs_hi, n_vs, st_lo, st_hi, n_st);
    printf("map arrow: %ld records, %ld blits recognised, %ld not; 16-bit position = the 32-bit one rounded in %ld, not in %ld; sprites by heading:",
           map_recs, map_ok, map_bad, map_pos_ok, map_pos_bad);
    for (int k = 0; k < 8; k++) printf(" %ld", map_sprites[k]);
    printf("; blit minus fine top-left: x [%.2f, %.2f], y [%.2f, %.2f]\n", mx_lo, mx_hi, my_lo, my_hi);
    fails += map_pos_bad;
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
