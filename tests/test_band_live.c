/* test_band_live.c - the state-driven sky and ground bands through the live path (drawfeed.c) from an observer log.
 *
 *   test_band_live LOG [N] [--old] [--dump PREFIX PAIR T]
 *
 * LOG is f117run --observe output taken with F117R_OBSERVE_PAGES=1 and F117R_OBSERVE_BANDS=1 (and, for the HUD,
 * F117R_OBSERVE_HUD=1), so it carries the page dumps and each band pass's starting state ('o'). Its records go to a
 * drawfeed exactly as the machine's observer would give them, and a drawlive replays, pairs and interpolates them
 * with the picture N times finer (default 4), as test_hud_live.c does. Reported:
 *   - the replayed frames equal the display at every close (the coarse replay is exact, bands or not);
 *   - every pair exact at both ends, coarse and fine (drawlive's own check);
 *   - what the bands did (the logic frames' replay, hires.h's band cover): passes, passes at low detail, band 'U'
 *     records drawn from the state, kept the original's, records off the prediction, band polygons painted and left
 *     scaled;
 *   - in-between pictures drawn at t = 1/4, 1/2, 3/4 of every pair (none failed), and the band pixels drawn from
 *     the state in them (found by their origin tag).
 * --old turns the state-driven bands off for comparison. --dump writes the fine display of in-between picture T of
 * pair PAIR (a PPM, and a PPM of the origin plane); PAIR:LAST takes a run of pairs and a negative T every picture of
 * each (PREFIX-PAIR-K-fine.ppm, K = 1..3 for t = 1/4, 1/2, 3/4). Exit 1 on a failed check, 77 when LOG is absent. */
#include "drawfeed.h"
#include "diagtag.h"
#include "hud_overlay.h"
#include "band_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t memory[MEM_SIZE];
static drawfeed feed;
static drawlive live;

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

#define MAXPH 4096
static uint8_t *g_y[MAXPH];
static uint8_t g_pal[MAXPH][768];
static int g_havepal[MAXPH];

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

static int32_t g_v[3 + 3 * 65536 + 16];

static int g_dump_pair = -1, g_dump_last = -1;   /* pairs PAIR (or PAIR:LAST) */
static double g_dump_t;                          /* T, or every T when negative */
static const char *g_dump;
static int g_n = 4;

static void write_ppm(const char *path, const uint8_t *pix, int w, int h, const uint8_t *pal)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (size_t i = 0; i < (size_t)w * h; i++) fwrite(pal + 3 * pix[i], 1, 3, f);
    fclose(f);
}

static int g_phase_pal;
static uint64_t g_between, g_between_bad, g_pairs_seen;
static uint64_t g_ours_px;
static const uint8_t *g_tag;

static uint64_t ours_pixels(const uint8_t *tag)
{
    if (!tag) return 0;
    uint64_t n = 0;
    const size_t total = (size_t)320 * g_n * 200 * g_n;
    for (size_t i = 0; i < total; i++) n += tag[i] == DT_BAND_STATE;
    return n;
}

static void inbetweens(void)
{
    const drawlive_step *a, *b;
    drawlive_steps(&live, &a, &b);
    if (!a || !b || !b->pair || live.picture_seq != b->seq) return;
    static uint64_t last = ~0ull;
    if (last == b->seq) return;
    last = b->seq;
    g_pairs_seen++;
    static present_frame fr;
    const double ts[3] = { 0.25, 0.5, 0.75 };
    for (int k = 0; k < 3; k++) {
        drawlive_plan plan;
        memset(&plan, 0, sizeof plan);
        plan.kind = PLAN_BETWEEN;
        plan.t = ts[k];
        plan.older = a->seq; plan.newer = b->seq;
        const int ok = drawlive_render(&live, &plan, &fr);
        g_between++;
        if (!ok || !live.shown_hi) { g_between_bad++; continue; }
        const uint8_t *tag = live.shown_tag;
        if (tag) g_ours_px += ours_pixels(tag);
        const int pair = (int)g_pairs_seen - 1;
        if (g_dump && pair >= g_dump_pair && pair <= (g_dump_last >= 0 ? g_dump_last : g_dump_pair) && (g_dump_t < 0 || ts[k] == g_dump_t)) {
            char path[1024];
            if (g_dump_last >= 0 || g_dump_t < 0) snprintf(path, sizeof path, "%s-%04d-%d-fine.ppm", g_dump, pair, k + 1);
            else snprintf(path, sizeof path, "%s-fine.ppm", g_dump);
            write_ppm(path, live.shown_hi, 320 * g_n, 200 * g_n, g_pal[g_phase_pal]);
            if (tag) {
                static uint8_t colours[768];
                for (int i = 0; i < 256; i++) {
                    colours[3 * i] = (uint8_t)(i * 37); colours[3 * i + 1] = (uint8_t)(i * 91); colours[3 * i + 2] = (uint8_t)(i * 53);
                }
                if (g_dump_last >= 0 || g_dump_t < 0) snprintf(path, sizeof path, "%s-%04d-%d-tag.ppm", g_dump, pair, k + 1);
                else snprintf(path, sizeof path, "%s-tag.ppm", g_dump);
                write_ppm(path, tag, 320 * g_n, 200 * g_n, colours);
            }
        }
    }
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    int old = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--old")) old = 1;
        else if (!strcmp(argv[i], "--dump") && i + 3 < argc) {
            g_dump = argv[i + 1]; g_dump_pair = atoi(argv[i + 2]); g_dump_t = atof(argv[i + 3]); i += 3;
            const char *colon = strchr(argv[i - 1], ':');
            if (colon) g_dump_last = atoi(colon + 1);
        }
        else if (!path) path = argv[i];
        else g_n = atoi(argv[i]);
    }
    if (!path || g_n < 2 || g_n > 12) { fprintf(stderr, "usage: test_band_live LOG [N 2..12] [--old] [--dump PREFIX PAIR T]\n"); return 2; }
    if (old) band_state_enable(0);
    _putenv_s("F117_PRESENT_DIAG", "x");                   /* the origin plane, to see what drew where */
    FILE *f = fopen(path, "r");
    if (!f) { printf("SKIP: cannot read %s\n", path); return 77; }
    char *buf = NULL;
    size_t cap = 0;
    char *line;
    /* pass 1: each phase's display and palette */
    int phases = 0;
    while ((line = read_line(f, &buf, &cap))) {
        const char k = line[0];
        if (k == 'P') { phases++; continue; }
        if ((k != 'Y' && k != 'J') || phases < 1 || phases >= MAXPH) continue;
        char *p = line + 1;
        strtoull(p, &p, 10);
        const int n = parse_values(k, p, g_v, (int)(sizeof g_v / sizeof g_v[0]));
        if (k == 'Y' && n >= 2 + 64000) {
            g_y[phases - 1] = (uint8_t *)malloc(64000);
            for (int a = 0; a < 64000; a++) g_y[phases - 1][a] = (uint8_t)g_v[2 + a];
        }
        if (k == 'J' && n >= 768) {
            for (int i = 0; i < 768; i++) g_pal[phases - 1][i] = (uint8_t)((g_v[i] * 255 + 31) / 63);
            g_havepal[phases - 1] = 1;
        }
    }
    rewind(f);
    drawfeed_init(&feed, memory);
    drawlive_init(&live);
    drawlive_set_scale(&live, g_n);
    live.interp = live.check = 1;
    int phase = -1, deliver = 0;
    uint64_t records = 0;
    while ((line = read_line(f, &buf, &cap))) {
        const char k = line[0];
        if (k == '\n' || k == ' ' || !k) continue;
        char *p = line + 1;
        const uint64_t icount = strtoull(p, &p, 10);
        if (k == 'P') {
            phase++;
            if (phase < MAXPH && g_y[phase]) memcpy(memory + 0xA0000, g_y[phase], 64000);
            if (phase < MAXPH && g_havepal[phase]) g_phase_pal = phase;
            feed.obs.frame_phase(feed.obs.user, icount);
            deliver = 0;
            drawlive_update(&live, &feed);
            inbetweens();
            continue;
        }
        if (phase < 0) continue;
        if (k == 'Z') deliver = feed.want_seed;
        if ((k == 'Z' || k == 'Y' || k == 'J') && !deliver) continue;
        const int n = parse_values(k, p, g_v, (int)(sizeof g_v / sizeof g_v[0]));
        if (k == 'V') feed.obs.vertex(feed.obs.user, icount, g_v, g_v + 3, g_v[5], (uint16_t)g_v[6], (uint16_t)g_v[7]);
        else feed.obs.prim(feed.obs.user, icount, k, g_v, n);
        records++;
    }
    fclose(f);
    free(buf);
    drawlive_update(&live, &feed);
    inbetweens();
    const band_cover *bc = &live.hr->band;
    printf("%llu records, %d phases; %llu logic frames closed, %llu exact, %llu inexact, %llu unseeded; %llu pairs, %llu exact at both ends, %llu inexact\n",
           (unsigned long long)records, phases, (unsigned long long)live.frames, (unsigned long long)live.exact,
           (unsigned long long)live.inexact, (unsigned long long)live.unseeded, (unsigned long long)live.pairs,
           (unsigned long long)live.pairs_exact, (unsigned long long)live.pairs_inexact);
    printf("bands, last replay (%s): passes %llu, at low detail %llu, drawn from the state %llu, kept the original's %llu, "
           "records off the prediction %llu, band polygons painted %llu, left scaled %llu\n", old ? "state-driven bands off" : "state-driven bands on",
           (unsigned long long)bc->passes, (unsigned long long)bc->low_detail, (unsigned long long)bc->replaced, (unsigned long long)bc->kept,
           (unsigned long long)bc->mismatched, (unsigned long long)bc->painted, (unsigned long long)bc->scaled);
    printf("%llu in-between pictures drawn (%llu failed); band pixels drawn from the state in them %llu\n",
           (unsigned long long)g_between, (unsigned long long)g_between_bad, (unsigned long long)g_ours_px);
    int fail = 0;
    if (live.inexact || live.pairs_inexact || g_between_bad || !live.exact) fail = 1;
    if (!old && (bc->mismatched || (bc->passes > bc->low_detail && (!bc->replaced || !g_ours_px)))) fail = 1;
    printf(fail ? "FAIL\n" : "PASS\n");
    return fail;
}
