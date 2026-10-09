/* test_hires.c - Stage 2 in C (src/present/hires.c) against an observer log,
 * as tools/hires_subpixel.py LOG N reports it (its lines after the Stage 1
 * replay's, so the two diff).
 *
 *   test_hires LOG [N] [--carry]
 *
 * LOG is f117run --observe output taken with F117R_OBSERVE_PAGES=1. Each
 * record goes to the sub-pixel builder and then to the replay, with the fine
 * picture N times finer (default 2). At every display dump the stage's checks
 * are counted on the replayed display: the flat coarse pixels (all eight
 * neighbours equal) whose N x N fine pixels keep their value, before a guard
 * that restores those that do not, and the share of the fine picture that
 * differs from the plain scaled copy; at N = 1, whether the fine picture is
 * the replay's own. Pages are seeded from each phase's dumps, as the Python
 * replay does, or with --carry once, as the live replay is. Exit 1 when N = 1
 * and a page differs from the replay's. */
#include "drawlist.h"
#include "hires.h"

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

static unsigned long long g_flat, g_agree, g_bad, g_bad_fine, g_fine, g_unlike, g_phases, g_n1_equal;

static void phase_check(const drawlist_page *p, int n)
{
    const uint8_t *c = p->b, *f = p->hi;
    const size_t W = (size_t)320 * n;
    unsigned long long unlike = 0;
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++) {
            const uint8_t v = c[y * 320 + x];
            unsigned block = 0;
            for (int j = 0; j < n; j++)
                for (int i = 0; i < n; i++) block += f[((size_t)y * n + j) * W + (size_t)x * n + i] != v;
            int flat = y > 0 && y < 199 && x > 0 && x < 319;
            for (int dy = -1; dy <= 1 && flat; dy++)
                for (int dx = -1; dx <= 1 && flat; dx++) flat = c[(y + dy) * 320 + x + dx] == v;
            if (flat) {
                g_flat++;
                if (!block) g_agree++;
                else { g_bad++; g_bad_fine += block; block = 0; }   /* the guard restores the block */
            }
            unlike += block;
        }
    g_fine += (unsigned long long)W * 200 * n;
    g_unlike += unlike;
    g_phases++;
    if (n == 1) g_n1_equal += !unlike;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    int n = 2, carry = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--carry")) carry = 1;
        else if (!path) path = argv[i];
        else n = atoi(argv[i]);
    }
    if (!path || n < 1 || n > 16) { fprintf(stderr, "usage: test_hires LOG [N] [--carry]\n"); return 2; }
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot read %s\n", path); return 2; }
    static drawlist d;
    drawlist_init(&d);
    drawlist_set_scale(&d, n);
    d.prefer_logged = 1;                              /* the page copies as drawlist_frame.py makes them */
    static hires h;
    hires_init(&h, n);
    static int32_t v[3 + 3 * 65536 + 16];
    static uint8_t z[65536];
    char *buf = NULL;
    size_t cap = 0;
    char *line;
    while ((line = read_line(f, &buf, &cap))) {
        const char k = line[0];
        if (k == '\n' || k == ' ' || !k) continue;
        char *p = line + 1;
        strtoull(p, &p, 10);
        int nv = 0;
        if (k == 'V') {                               /* the last two fields are hex */
            for (int q = 0; q < 8; q++) { char *e; v[nv++] = (int32_t)strtol(p, &e, q < 6 ? 10 : 16); p = e; }
        } else
            for (;;) {
                char *e;
                const long x = strtol(p, &e, 10);
                if (e == p || nv == (int)(sizeof v / sizeof v[0])) break;
                v[nv++] = (int32_t)x;
                p = e;
            }
        hires_record(&h, &d, k, v, nv);
        if (k == 'Z' || k == 'Y') {
            const uint16_t seg = (uint16_t)v[0];
            const drawlist_page *rep = drawlist_get(&d, seg);
            if (k == 'Y' && rep && rep->hi) phase_check(rep, n);
            for (int a = 0; a < nv - 2; a++) z[a] = (uint8_t)v[2 + a];
            if (!carry || !rep) drawlist_seed(&d, k, seg, (uint16_t)v[1], z, (unsigned)(nv - 2));
            else if (k == 'Z') drawlist_phase(&d, seg, (uint16_t)v[1]);
            continue;
        }
        if (k != 'P') drawlist_record(&d, k, v, nv);
    }
    fclose(f);
    free(buf);
    /* the builder's counters, sorted by name as the Python prints them */
    int order[HS_COUNT];
    for (int k = 0; k < HS_COUNT; k++) order[k] = k;
    for (int a = 0; a < HS_COUNT; a++)
        for (int b = a + 1; b < HS_COUNT; b++)
            if (strcmp(hires_stat_name(order[b]), hires_stat_name(order[a])) < 0) { int t = order[a]; order[a] = order[b]; order[b] = t; }
    for (int k = 0; k < HS_COUNT; k++)
        if (h.stats[order[k]]) printf("%-46s %llu\n", hires_stat_name(order[k]), (unsigned long long)h.stats[order[k]]);
    printf("%-46s %llu\n%-46s %llu\n%-46s %llu\n%-46s %llu\n%-46s %llu\n%-46s %llu\n%-46s %llu\n",
           "display phases", g_phases, "flat coarse pixels", g_flat, "flat pixels that agree", g_agree,
           "flat pixels disagreeing", g_bad, "fine pixels in disagreeing flat pixels", g_bad_fine, "fine pixels", g_fine,
           "fine pixels unlike the scaled copy", g_unlike);
    if (n == 1) printf("%-46s %llu\n", "N = 1 pages equal to the replay's", g_n1_equal);
    if (g_flat)
        printf("N = %d, overshoot per polygon: rule alone %.3f%% of flat coarse pixels keep their value on all %d fine pixels "
               "(the guard restores %llu); %.3f%% of the fine picture differs from the scaled copy\n",
               n, 100.0 * (double)g_agree / (double)g_flat, n * n, g_bad,
               100.0 * (double)g_unlike / (double)(g_fine ? g_fine : 1));
    drawlist_free(&d);
    return n == 1 && g_n1_equal != g_phases ? 1 : 0;
}
