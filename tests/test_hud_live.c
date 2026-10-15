/* test_hud_live.c - the state-driven HUD through the live path (drawfeed.c) from an observer log.
 *
 *   test_hud_live LOG [N] [--old] [--leak-check] [--dump PREFIX PAIR T]
 *
 * LOG is f117run --observe output taken with F117R_OBSERVE_PAGES=1 and F117R_OBSERVE_HUD=1, so it carries the
 * page dumps and the HUD pass's starting state ('h'). Its records go to a drawfeed exactly as the machine's
 * observer would give them (the pages only when the feed asks for them, the display of each phase in the
 * memory the feed copies at a frame's close), and a drawlive replays, pairs and interpolates them with the
 * picture N times finer (default 4). Reported:
 *   - the replayed frames equal the display at every close (the coarse replay is exact, HUD or not);
 *   - every pair exact at both ends, coarse and fine (drawlive's own check, which draws the pair a hair
 *     from each end and compares with the two frames' fine pictures);
 *   - what the HUD did (hud_overlay_stats): passes drawn from the state, records replaced and kept, none off the
 *     prediction, and with --leak-check none of the replaced records changing the fine picture (the original's
 *     strokes, ticks and text are not drawn a second time);
 *   - in-between pictures drawn at t = 1/4, 1/2, 3/4 of every pair (none empty), and the state-driven HUD's
 *     ink in them (found by its origin tag).
 * --leak-check also runs every record the fine picture replaces between two copies of the fine page and counts
 * the ones that changed it (slow: a whole page copied twice per record). --old turns the state-driven HUD off (the
 * earlier HUD) for comparison. --dump writes the fine display of in-between picture T of pair PAIR (a PPM, and a
 * PPM of the origin plane). Exit 1 on a failed check, 77 when LOG is absent. */
#include "drawfeed.h"
#include "diagtag.h"
#include "hud_overlay.h"
#include "hud_state.h"

#include <math.h>
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

static int g_dump_pair = -1;
static double g_dump_t;
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
    for (size_t i = 0; i < total; i++) n += tag[i] == DT_HUD_STATE_LINE || tag[i] == DT_HUD_STATE_TEXT;
    return n;
}

/* ---- the discrete items' hold: in every in-between picture the texts and numbers (G meter, readouts, station and gun,
 * ACCEL, TRAINING, the message, the mode-2 heading) and the map arrow's sprite are the older frame's ------------------ */
static uint64_t g_hold_pairs, g_hold_differ, g_hold_samples, g_hold_bad;

typedef struct { int n; struct { uint8_t item; uint8_t text[HUD_TEXT_MAX + 1]; int16_t x, y; } t[64]; int map_src; } discrete_t;

static void discrete_add(discrete_t *d, const hud_fine_list *fl)
{
    for (int i = 0; i < fl->n && d->n < 64; i++) {
        const hud_fprim *f = &fl->p[i];
        if (f->type != HF_TEXT) continue;
        if (f->item != HI_G && (f->item < PI_STATION || f->item == PI_FPM || f->item == PI_CROSS || f->item == PI_STALL || f->item == PI_VS ||
                                f->item == PI_SINK || f->item == PI_BOX || f->item == PI_CUE)) continue;
        d->t[d->n].item = f->item;
        memset(d->t[d->n].text, 0, sizeof d->t[d->n].text);
        memcpy(d->t[d->n].text, f->text, f->ntext);
        d->t[d->n].x = (int16_t)floor(f->x0 / g_n + 0.5);
        d->t[d->n].y = (int16_t)floor(f->y0 / g_n + 0.5);
        d->n++;
    }
}

/* the discrete items of a list's state records, as drawn (hud_state_load_drawn and friends) */
static void discrete_of(const interp_rec *rec, int nrec, discrete_t *d)
{
    static hud_state st;
    static hud_exact_list ex;
    static hud_fine_list fl;
    static hud_panel pn;
    static uint8_t ds[0x10000];
    static hud_map mp;
    memset(d, 0, sizeof *d);
    d->map_src = -1;
    for (int i = 0; i < nrec; i++) {
        const interp_rec *r = &rec[i];
        if (r->kind == 'h' && hud_state_load_drawn(r->v, r->n, &st) && hud_exact(&st, &ex) == 0 && hud_fine(&st, g_n, &ex, &fl) == 0)
            discrete_add(d, &fl);
        if (r->kind == 'p' && hud_panel_load_drawn(r->v, r->n, &pn, ds) && pn.ext && hud_panel_exact(ds, &pn, g_n, NULL, &fl) == 0)
            discrete_add(d, &fl);
        if (r->kind == 'm' && hud_map_load_drawn(r->v, r->n, &mp)) d->map_src = mp.src_x * 1000 + mp.art[5] + mp.art[6] * 7 + mp.art[9] * 13;
    }
}

static int discrete_same(const discrete_t *a, const discrete_t *b)
{
    if (a->n != b->n || a->map_src != b->map_src) return 0;
    for (int i = 0; i < a->n; i++)
        if (a->t[i].item != b->t[i].item || memcmp(a->t[i].text, b->t[i].text, sizeof a->t[i].text) || a->t[i].x != b->t[i].x ||
            a->t[i].y != b->t[i].y) return 0;
    return 1;
}

static void hold_check(const drawlive_step *a, const drawlive_step *b)
{
    static discrete_t da, db, dt;
    static interp_list l;
    discrete_of(a->rec, a->nrec, &da);
    discrete_of(b->rec, b->nrec, &db);
    g_hold_pairs++;
    if (!discrete_same(&da, &db)) g_hold_differ++;
    const double ts[4] = { 0.25, 0.5, 0.75, 1 - 1e-9 };
    for (int k = 0; k < 4; k++) {
        int sb;
        interp_inbetween_ex(a->parsed, b->parsed, b->pair, ts[k], INTERP_VERTICES | INTERP_HUD | INTERP_HUD_HOLD, &l, &sb, NULL);
        discrete_of(l.rec, l.n, &dt);
        g_hold_samples++;
        if (!discrete_same(&dt, &da)) {
            g_hold_bad++;
            if (g_hold_bad < 5) {
                printf("hold: pair %llu t %.3f: the discrete items are not the older frame's (%d items, map %d; older %d, %d):",
                       (unsigned long long)g_hold_pairs, ts[k], dt.n, dt.map_src, da.n, da.map_src);
                for (int i = 0; i < dt.n && i < da.n; i++)
                    if (memcmp(dt.t[i].text, da.t[i].text, sizeof dt.t[i].text) || dt.t[i].x != da.t[i].x || dt.t[i].y != da.t[i].y)
                        printf(" [%d '%s' %d,%d vs '%s' %d,%d]", dt.t[i].item, dt.t[i].text, dt.t[i].x, dt.t[i].y, da.t[i].text, da.t[i].x, da.t[i].y);
                printf("\n");
            }
        }
    }
    /* at t = 1 the newer frame itself */
    int sb;
    interp_inbetween_ex(a->parsed, b->parsed, b->pair, 1.0, INTERP_VERTICES | INTERP_HUD | INTERP_HUD_HOLD, &l, &sb, NULL);
    discrete_of(l.rec, l.n, &dt);
    g_hold_samples++;
    if (!discrete_same(&dt, &db)) { g_hold_bad++; if (g_hold_bad < 5) printf("hold: pair %llu t 1: not the newer frame's\n", (unsigned long long)g_hold_pairs); }
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
    hold_check(a, b);
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
        if (g_dump && (int)g_pairs_seen - 1 == g_dump_pair && ts[k] == g_dump_t) {
            char path[1024];
            snprintf(path, sizeof path, "%s-fine.ppm", g_dump);
            write_ppm(path, live.shown_hi, 320 * g_n, 200 * g_n, g_pal[g_phase_pal]);
            if (tag) {
                static uint8_t colours[768];
                for (int i = 0; i < 256; i++) {
                    colours[3 * i] = (uint8_t)(i * 37); colours[3 * i + 1] = (uint8_t)(i * 91); colours[3 * i + 2] = (uint8_t)(i * 53);
                }
                snprintf(path, sizeof path, "%s-tag.ppm", g_dump);
                write_ppm(path, tag, 320 * g_n, 200 * g_n, colours);
            }
        }
    }
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    int old = 0, leak_check = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--old")) old = 1;
        else if (!strcmp(argv[i], "--leak-check")) leak_check = 1;
        else if (!strcmp(argv[i], "--dump") && i + 3 < argc) { g_dump = argv[i + 1]; g_dump_pair = atoi(argv[i + 2]); g_dump_t = atof(argv[i + 3]); i += 3; }
        else if (!path) path = argv[i];
        else g_n = atoi(argv[i]);
    }
    if (!path || g_n < 2 || g_n > 12) { fprintf(stderr, "usage: test_hud_live LOG [N 2..12] [--old] [--leak-check] [--dump PREFIX PAIR T]\n"); return 2; }
    if (old) hud_overlay_enable(0);
    hud_overlay_debug(leak_check);
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
    uint64_t st[HUD_STATS];
    hud_overlay_stats(&live.list, st);
    printf("%llu records, %d phases; %llu logic frames closed, %llu exact, %llu inexact, %llu unseeded; %llu pairs, %llu exact at both ends, %llu inexact\n",
           (unsigned long long)records, phases, (unsigned long long)live.frames, (unsigned long long)live.exact,
           (unsigned long long)live.inexact, (unsigned long long)live.unseeded, (unsigned long long)live.pairs,
           (unsigned long long)live.pairs_exact, (unsigned long long)live.pairs_inexact);
    printf("HUD, last replay (%s): ", old ? "state-driven HUD off" : "state-driven HUD on");
    for (int k = 0; k < HUD_STATS; k++) printf("%s%s %llu", k ? ", " : "", hud_overlay_stat_name(k), (unsigned long long)st[k]);
    printf("\n%llu in-between pictures drawn (%llu failed); state-driven HUD pixels in them %llu\n",
           (unsigned long long)g_between, (unsigned long long)g_between_bad, (unsigned long long)g_ours_px);
    printf("hold: %llu pairs (%llu whose two frames differ in a discrete item), %llu samples at t = 1/4, 1/2, 3/4, 1 - 1e-9 and 1, %llu with the wrong frame's items\n",
           (unsigned long long)g_hold_pairs, (unsigned long long)g_hold_differ, (unsigned long long)g_hold_samples, (unsigned long long)g_hold_bad);
    int fail = 0;
    if (live.inexact || live.pairs_inexact || g_between_bad || !live.exact) fail = 1;
    if (!old && (st[5] || !st[1] || st[9] || !g_ours_px || st[13] || g_hold_bad)) fail = 1;   /* st[9] counts only with --leak-check */
    printf(fail ? "FAIL\n" : "PASS\n");
    return fail;
}
