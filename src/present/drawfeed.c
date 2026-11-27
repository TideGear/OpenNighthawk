/* drawfeed.c - see drawfeed.h. */
#include "drawfeed.h"

#include "diagtag.h"
#include "tls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* F117_COLLECT_PROBE (diagnostic): where drawlive_collect's time goes, printed every 600 closed frames. */
static double probe_ms(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq; LARGE_INTEGER now;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    return 0.0;
#endif
}
static double g_pc_memcmp, g_pc_start, g_pc_picture, g_pc_fine, g_pc_snap, g_pc_keep, g_pc_pair, g_pc_n;
static double g_probe_replay, g_probe_close, g_probe_open; static unsigned g_probe_n, g_probe_calls;

static const drawlive_sync_ops *g_ops;   /* the host's lock, condition and thread calls (drawlive_collector_start) */

/* a frame with no present closes after this many phases all the same */
#define MAX_PHASES 8

static drawfeed_frame *open_frame(drawfeed *f)
{
    return &f->frame[f->open];
}

static void append(drawfeed_frame *fr, char kind, uint64_t icount, const int32_t *v, int n)
{
    const size_t need = fr->len + 4 + (size_t)n;
    if (need > fr->cap) {
        size_t cap = fr->cap ? fr->cap : 1 << 16;
        while (cap < need) cap *= 2;
        int32_t *rec = (int32_t *)realloc(fr->rec, cap * sizeof *rec);
        if (!rec) return;
        fr->rec = rec;
        fr->cap = cap;
    }
    int32_t *p = fr->rec + fr->len;
    p[0] = kind; p[1] = n; p[2] = (int32_t)(uint32_t)icount; p[3] = (int32_t)(uint32_t)(icount >> 32);
    if (n) memcpy(p + 4, v, (size_t)n * sizeof *v);
    fr->len = need;
}

static void begin(drawfeed *f, uint64_t icount)
{
    drawfeed_frame *fr = open_frame(f);
    fr->seq = f->closed;
    fr->start = icount;
    fr->end = 0;
    fr->phases = 0;
    fr->seeded = 0;
    fr->len = 0;
    fr->have_compose = 0;
    f->presented = 0;
}

static void on_phase(void *user, uint64_t icount)
{
    drawfeed *f = (drawfeed *)user;
    drawfeed_frame *fr = open_frame(f);
    if (fr->phases && (f->presented || fr->phases >= MAX_PHASES)) {
        fr->end = icount;
        memcpy(fr->display, f->mem + 0xA0000, sizeof fr->display);
        if (f->lk) g_ops->lock(f->lk);
        f->closed++;
        if (f->count == DRAWFEED_FRAMES - 1) {         /* nobody took the oldest: drop it */
            f->head = (f->head + 1) % DRAWFEED_FRAMES;
            f->count--;
            f->dropped++;
            f->want_seed = 1;                           /* the replay must start again */
        }
        f->count++;
        f->open = (f->open + 1) % DRAWFEED_FRAMES;
        if (f->lk) g_ops->unlock(f->lk);
        begin(f, icount);
        fr = open_frame(f);
        if (f->have_next_snap) {
            rerun_snap_swap(&fr->snap, &f->next_snap);
            fr->have_compose = 1;
            f->have_next_snap = 0;
        }
    }
    if (!fr->phases) fr->start = icount;
    fr->phases++;
    append(fr, 'P', icount, NULL, 0);
}

static void on_vertex(void *user, uint64_t icount, const int32_t xf[3], const int32_t px[2], int range,
                      uint16_t xf_at, uint16_t px_at)
{
    drawfeed *f = (drawfeed *)user;
    const int32_t v[8] = { xf[0], xf[1], xf[2], px[0], px[1], range, xf_at, px_at };
    append(open_frame(f), 'V', icount, v, 8);
}

static void on_prim(void *user, uint64_t icount, char kind, const int32_t *v, int n)
{
    drawfeed *f = (drawfeed *)user;
    drawfeed_frame *fr = open_frame(f);
    if (kind == 'Z') { f->want_seed = 0; fr->seeded = 1; }
    if (kind == 'D' && n >= 1 && v[0] == 44) f->presented = 1;
    append(fr, kind, icount, v, n);
}

static void on_compose(void *user, machine_t *m)
{
    drawfeed *f = (drawfeed *)user;
    if (!f->presented) return;                        /* only the composition that will draw the next frame */
    rerun_snap_take(&f->next_snap, m);
    f->have_next_snap = f->next_snap.valid;
}

void drawfeed_set_rerun(drawfeed *f)
{
    f->rerun = 1;
    f->obs.compose = on_compose;
}

static int on_want_pages(void *user)
{
    return ((drawfeed *)user)->want_seed;
}

void drawfeed_init(drawfeed *f, const uint8_t *mem)
{
    memset(f, 0, sizeof *f);
    f->mem = mem;
    f->obs.user = f;
    f->obs.frame_phase = on_phase;
    f->obs.vertex = on_vertex;
    f->obs.prim = on_prim;
    f->obs.sources = 1;
    f->obs.want_pages = on_want_pages;
    f->obs.hud = 1;                                   /* the HUD pass's state, for the state-driven HUD */
    f->obs.ident = interp_ident_enabled();           /* each model request's identity (the identity-pairing experiment) */
    f->obs.bands = 1;                                 /* the band pass's state, for the state-driven sky and ground */
    f->want_seed = 1;
    begin(f, 0);
}

void drawfeed_free(drawfeed *f)
{
    for (int k = 0; k < DRAWFEED_FRAMES; k++) { free(f->frame[k].rec); rerun_snap_free(&f->frame[k].snap); }
    rerun_snap_free(&f->next_snap);
    memset(f, 0, sizeof *f);
}

const drawfeed_frame *drawfeed_oldest(const drawfeed *f)
{
    return f->count ? &f->frame[f->head] : NULL;
}

void drawfeed_pop(drawfeed *f)
{
    if (f->lk) g_ops->lock(f->lk);
    if (f->count) {
        f->head = (f->head + 1) % DRAWFEED_FRAMES;
        f->count--;
    }
    if (f->lk) g_ops->unlock(f->lk);
}

void drawfeed_reseed(drawfeed *f)
{
    f->want_seed = 1;
}

int drawfeed_next(const drawfeed_frame *fr, size_t *at, char *kind, uint64_t *icount, const int32_t **v, int *n)
{
    if (*at + 4 > fr->len) return 0;
    const int32_t *p = fr->rec + *at;
    *kind = (char)p[0];
    *n = p[1];
    *icount = (uint64_t)(uint32_t)p[2] | (uint64_t)(uint32_t)p[3] << 32;
    *v = p + 4;
    *at += 4 + (size_t)p[1];
    return 1;
}

void drawlive_init(drawlive *r)
{
    memset(r, 0, sizeof *r);
    drawlist_init(&r->list);
    drawlist_init(&r->pending_start);
    drawlist_init(&r->pending_prehud);
    { const char *e = getenv("F117_PREHUD"); r->prehud_on = e && atoi(e) != 0; }
    { const char *e = getenv("F117_STEPA"); r->stepa = e && atoi(e) != 0; r->gpu_hud = r->stepa && getenv("F117_GPU_HUD") && atoi(getenv("F117_GPU_HUD")) != 0; if (r->stepa) r->prehud_on = 1; r->stepa_check = getenv("F117_STEPA_CHECK") != NULL; }   /* opt-in: pixel-identical and keeps the long flight's hit at 60, but on the late flight it moves the cost into compose (p99 14 ms) and misses more frames (26 Nov, handoff) */
    r->ia = r->ib = r->newest = r->im = -1;
    r->picture = r->blank;
}

void drawlive_set_scale(drawlive *r, int n)
{
    const size_t bytes = (size_t)320 * n * 200 * n;
    r->scale = n;
    drawlist_set_scale(&r->list, n);
    r->hr = (hires *)malloc(sizeof *r->hr);
    r->scratch_hr = (hires *)malloc(sizeof *r->scratch_hr);
    r->pending_hires = (hires *)malloc(sizeof *r->pending_hires);
    r->pending_prehud_hires = (hires *)malloc(sizeof *r->pending_hires);
    hires_init(r->hr, n);
    r->inter_hi = (uint8_t *)calloc(bytes, 1);
    const char *diag = getenv("F117_PRESENT_DIAG");
    if (diag && *diag) { drawlist_diag_enable(1); r->inter_tag = (uint8_t *)calloc(bytes, 1); }
    for (int k = 0; k < DRAWLIVE_POOL; k++) {
        r->step[k].start_hires = (hires *)malloc(sizeof *r->hr);
        r->step[k].prehud_hires = (hires *)malloc(sizeof *r->hr);
        r->step[k].picture_hi = (uint8_t *)calloc(bytes, 1);
        if (r->inter_tag) r->step[k].picture_tag = (uint8_t *)calloc(bytes, 1);
    }
}

void drawlive_set_line_width(drawlive *r, int width)
{
    drawlist_set_line_width(&r->list, width);
}

void drawlive_set_rerun(drawlive *r)
{
    r->rerun = 1;
    const char *clock = getenv("F117_PRESENT_CLOCK");
    r->smooth = !(clock && !strcmp(clock, "raw"));      /* the smoothed clock, unless F117_PRESENT_CLOCK=raw */
    r->ema_alpha = 0.4; r->pull_in = 0.025;
    if (getenv("F117_STAGE2")) { const char *dl = getenv("F117_STAGE2_DELAY"); r->extra_delay = dl && *dl ? atof(dl) : 0.75; if (r->extra_delay < 0) r->extra_delay = 0; if (r->extra_delay > 1) r->extra_delay = 1; }
    const char *a = getenv("F117_PRESENT_CLOCK_ALPHA"), *p = getenv("F117_PRESENT_CLOCK_PULL");   /* tuning only */
    if (a && atof(a) > 0 && atof(a) <= 1) r->ema_alpha = atof(a);
    if (p && atof(p) > 0 && atof(p) <= 1) r->pull_in = atof(p);
}

/* One record into the replay, through the sub-pixel builder first when there is one. */
static void feed(drawlist *d, hires *h, char kind, const int32_t *v, int n)
{
    if (h) hires_record(h, d, kind, v, n);
    if (kind == 'Z' || kind == 'Y') {
        static F117_TLS uint8_t bytes[65536];
        for (int a = 0; a < n - 2; a++) bytes[a] = (uint8_t)v[2 + a];
        drawlist_seed(d, kind, (uint16_t)v[0], (uint16_t)v[1], bytes, (unsigned)(n - 2));
    } else drawlist_record(d, kind, v, n);
}

/* the fine picture of a display page, guarded, into out */
static void take_fine(drawlive *r, const drawlist_page *disp, uint8_t *out, uint8_t *out_tag)
{
    memcpy(out, disp->hi, (size_t)320 * r->scale * 200 * r->scale);
    if (out_tag && disp->tag) memcpy(out_tag, disp->tag, (size_t)320 * r->scale * 200 * r->scale);
    r->fine_restored += (uint64_t)hires_guard_masked_tag(disp->b, out, r->scale, disp->strokes, &r->fine_flat, &r->fine_agree,
                                                         out_tag && disp->tag ? out_tag : NULL);
}

/* Keep a replayed frame's records for interpolation (P, Z, Y and J left out). */
static void keep_step(drawlive_step *s, const drawfeed_frame *fr)
{
    interp_frame_free(s->parsed);
    s->parsed = NULL;
    if (fr->len > s->cvals) {
        s->cvals = fr->len;
        s->vals = (int32_t *)realloc(s->vals, s->cvals * sizeof *s->vals);
    }
    memcpy(s->vals, fr->rec, fr->len * sizeof *s->vals);
    s->nvals = fr->len;
    s->nrec = 0;
    size_t at = 0;
    char kind;
    uint64_t icount;
    const int32_t *v;
    int n;
    while (drawfeed_next(fr, &at, &kind, &icount, &v, &n)) {
        if (kind == 'P' || kind == 'Z' || kind == 'Y' || kind == 'J') continue;
        if (s->nrec == s->crec) {
            s->crec = s->crec ? 2 * s->crec : 4096;
            s->rec = (interp_rec *)realloc(s->rec, (size_t)s->crec * sizeof *s->rec);
        }
        interp_rec r = { kind, icount, s->vals + (v - fr->rec), n };
        s->rec[s->nrec++] = r;
    }
    s->parsed = interp_parse(s->rec, s->nrec);
}

/* Draw the in-between list on a copy of the replay at the skeleton's start
 * (with a scale, its fine picture goes to inter_hi). */
static const uint8_t *draw_recs_ex(drawlive *r, const drawlive_step *skel, const interp_rec *rec, int nrec, int use_pre)
{
    static double t_copy, t_feed, t_hi, t_guard; static unsigned n; static int prb = -1;
    if (prb < 0) prb = getenv("F117_DRAW_PROBE") != NULL;
    const double d0 = prb ? probe_ms() : 0;
    if (prb) { static unsigned calls, wantpre, havepre; calls++; if (r->prehud_on) wantpre++; if (skel->have_prehud) havepre++; if (calls % 600 == 0) fprintf(stderr, "[prehud] calls %u, prehud_on %u, step has snapshot %u, use_pre %d\n", calls, wantpre, havepre, use_pre); }
    int from = 0;
    if (use_pre) { for (int i = 0; i < nrec; i++) if (rec[i].kind == 'h') { from = i; break; } if (!rec || nrec <= 0 || rec[from].kind != 'h') use_pre = 0; }
    drawlist_copy(&r->scratch, use_pre ? &skel->prehud : &skel->start);
    hires *h = NULL;
    if (r->scale) { memcpy(r->scratch_hr, use_pre ? skel->prehud_hires : skel->start_hires, sizeof *r->scratch_hr); h = r->scratch_hr; }
    if (!use_pre) from = 0;
    const double d1 = prb ? probe_ms() : 0;
    if (prb) { static double kt[128], seg_t[3]; static unsigned kn[128], seg_n[3], np; int seg = 0; for (int i = from; i < nrec; i++) { const double q0 = probe_ms(); feed(&r->scratch, h, rec[i].kind, rec[i].v, rec[i].n); const int k = rec[i].kind & 127; const double dq = probe_ms() - q0; kt[k] += dq; kn[k]++; if (rec[i].kind == 'h') seg = 1; else if (rec[i].kind == 'p') seg = 2; seg_t[seg] += dq; seg_n[seg]++; }
        if (np % 300 == 299) fprintf(stderr, "[segprobe] per picture: before h %.3f ms (%.0f recs), h to p %.3f ms (%.0f recs), p to end %.3f ms (%.0f recs)\n",seg_t[0] / (np + 1), (double)seg_n[0] / (np + 1), seg_t[1] / (np + 1), (double)seg_n[1] / (np + 1), seg_t[2] / (np + 1), (double)seg_n[2] / (np + 1));
        if (++np % 300 == 0) { for (int k = 0; k < 128; k++) if (kn[k] && kt[k] / np > 0.02) fprintf(stderr, "[kindprobe] '%c' %.3f ms/picture, %.1f recs/picture, %.2f us each\n", k, kt[k] / np, (double)kn[k] / np, 1000.0 * kt[k] / kn[k]); } }
    else for (int i = from; i < nrec; i++) feed(&r->scratch, h, rec[i].kind, rec[i].v, rec[i].n);
    if (prb) {   /* where the HUD's state record ('h') sits in the list: the records before it and after it */
        static unsigned nh, sb4, saft; static double fb;
        int first_h = -1; for (int i = 0; i < nrec; i++) if (rec[i].kind == 'h') { first_h = i; break; }
        if (first_h >= 0) { nh++; sb4 += (unsigned)first_h; saft += (unsigned)(nrec - first_h); fb += (double)first_h / nrec; }
        if (nh && nh % 300 == 0) fprintf(stderr, "[hudpos] 'h' at record %.0f on average of %.0f (%.0f%% in): %.0f records before it, %.0f after\n",(double)sb4 / nh, (double)(sb4 + saft) / nh, 100.0 * fb / nh, (double)sb4 / nh, (double)saft / nh);
    }
    const double d2 = prb ? probe_ms() : 0;
    const drawlist_page *disp = drawlist_get(&r->scratch, 0xA000);
    if (!disp || disp->size < 64000) return NULL;
    double d3 = d2, d4 = d2;
    if (r->scale) {
        memcpy(r->inter_hi, disp->hi, (size_t)320 * r->scale * 200 * r->scale);
        if (r->inter_tag && disp->tag) memcpy(r->inter_tag, disp->tag, (size_t)320 * r->scale * 200 * r->scale);
        d3 = prb ? probe_ms() : 0;
        uint64_t flat = 0, agree = 0;
        hires_guard_masked_tag(disp->b, r->inter_hi, r->scale, disp->strokes, &flat, &agree,
                               r->inter_tag && disp->tag ? r->inter_tag : NULL);
        d4 = prb ? probe_ms() : 0;
    }
    if (prb) {
        t_copy += d1 - d0; t_feed += d2 - d1; t_hi += d3 - d2; t_guard += d4 - d3;
        if (++n % 300 == 0) fprintf(stderr, "[drawprobe] per picture: start copy %.3f, feed %.3f, hi copy %.3f, guard %.3f ms (scale %d, %d recs)\n", t_copy / n, t_feed / n, t_hi / n, t_guard / n, r->scale, nrec);
    }
    return disp->b;
}

static const uint8_t *draw_recs(drawlive *r, const drawlive_step *skel, const interp_rec *rec, int nrec) { return draw_recs_ex(r, skel, rec, nrec, 0); }

/* The in-between picture's HUD pass only, on the step's replay as it stood when the pass began (F117_PREHUD): the records before the HUD are the step's own, whose pixels outside the HUD the scene or the static cockpit decide. */
/* The coarse rows [*y0, *y1) where page a differs from page b in its bytes, stroke flags, fine pixels or tags (exact: the fine rows are compared), widened by one row each way; empty (0, 0) when none */
static void changed_rows(const drawlist_page *a, const drawlist_page *b, int *y0, int *y1, int fine_only)
{
    int lo = 200, hi = -1;
    for (int y = 0; y < 200; y++) {
        const size_t fr = a->hi && b->hi && a->n == b->n ? (size_t)320 * a->n * a->n : 0, fo = (size_t)y * fr;   /* the row's n fine rows, contiguous */
        if ((!fine_only && (memcmp(a->b + y * 320, b->b + y * 320, 320) || memcmp(a->strokes + y * 40, b->strokes + y * 40, 40) ||
             memcmp(a->hud_strokes + y * 40, b->hud_strokes + y * 40, 40))) ||
            (fr && memcmp(a->hi + fo, b->hi + fo, fr)) || (fr && a->tag && b->tag && memcmp(a->tag + fo, b->tag + fo, fr))) { if (lo > y) lo = y; hi = y; }
    }
    if (hi < 0) { *y0 = *y1 = 0; return; }
    *y0 = lo > 0 ? lo - 1 : 0; *y1 = hi + 2 < 200 ? hi + 2 : 200;
}

static void join_rows(int *y0, int *y1, int a0, int a1)
{
    if (a1 <= a0) return;
    if (*y1 <= *y0) { *y0 = a0; *y1 = a1; return; }
    if (a0 < *y0) *y0 = a0;
    if (a1 > *y1) *y1 = a1;
}

/* Step A: the HUD records on the step's pre-HUD snapshot, touching only the rows the last picture changed and this one changes.
 * The changed rows are found by comparing with the snapshot, fine planes included; F117_STEPA_CHECK holds every picture to the full path. */
static const uint8_t *draw_hud_stepa(drawlive *r, const drawlive_step *skel, const interp_rec *rec, int nrec)
{
    int from = -1;
    for (int i = 0; i < nrec; i++) if (rec[i].kind == 'h') { from = i; break; }
    if (from < 0) return NULL;
    static double t_restore, t_feed, t_diff, t_inter; const double q0 = probe_ms();
    const uint64_t key = skel->gen;
    int rows_restored = 0;
    if (r->sa_key_in == key && drawlist_copy_rows(&r->scratch, &skel->prehud, r->sa_y0, r->sa_y1)) {
        for (int k = 0; k < DRAWLIST_PAGES; k++) rows_restored += r->sa_y1[k] > r->sa_y0[k] ? r->sa_y1[k] - r->sa_y0[k] : 0;
    } else {
        drawlist_copy(&r->scratch, &skel->prehud);
        r->sa_full++;
        rows_restored = 200 * r->scratch.npages;
    }
    memcpy(r->scratch_hr, skel->prehud_hires, sizeof *r->scratch_hr);
    const double q1 = probe_ms();
    if (r->gpu_hud) { r->sink.page = drawlist_get(&r->scratch, 0xA000); r->sink.page2 = r->scratch.seg != 0xA000 ? drawlist_get(&r->scratch, r->scratch.seg) : NULL; r->sink.n = r->sink.n2 = 0; drawlist_set_stroke_sink(&r->sink); }
    static int who = -1; if (who < 0) who = getenv("F117_STEPA_WHO") != NULL;   /* diagnostic: which record kinds still change the display page's coarse rows, and where */
    if (who) {
        static unsigned cnt[128], lo[128], hi[128], np; static uint8_t before[64000];
        drawlist_page *dp = drawlist_page_of(&r->scratch, 0xA000);
        for (int i = from; i < nrec; i++) {
            if (dp) memcpy(before, dp->b, 64000);
            feed(&r->scratch, r->scratch_hr, rec[i].kind, rec[i].v, rec[i].n);
            if (dp && memcmp(before, dp->b, 64000)) {
                int a = 0; while (before[a] == dp->b[a]) a++;
                int b = 63999; while (before[b] == dp->b[b]) b--;
                const int k = rec[i].kind & 127; if (!cnt[k]) { lo[k] = 200; hi[k] = 0; }
                cnt[k]++; if ((unsigned)(a / 320) < lo[k]) lo[k] = (unsigned)(a / 320); if ((unsigned)(b / 320) > hi[k]) hi[k] = (unsigned)(b / 320);
            }
        }
        if (++np % 300 == 0) for (int k = 0; k < 128; k++) if (cnt[k]) fprintf(stderr, "[stepa-who] '%c' changed the display %.1f times a picture, rows %u-%u\n", k, (double)cnt[k] / np, lo[k], hi[k]);
    } else
    for (int i = from; i < nrec; i++) feed(&r->scratch, r->scratch_hr, rec[i].kind, rec[i].v, rec[i].n);
    if (r->gpu_hud) { drawlist_set_stroke_sink(NULL); r->shown_strokes = r->sink.n; }
    const double q2 = probe_ms();
    int ok = r->scratch.npages == skel->prehud.npages;
    for (int k = 0; k < DRAWLIST_PAGES; k++) {
        r->sa_y0[k] = 0; r->sa_y1[k] = 200;
        if (ok && k < r->scratch.npages) changed_rows(&r->scratch.page[k], &skel->prehud.page[k], &r->sa_y0[k], &r->sa_y1[k], r->gpu_hud);   /* GPU HUD: only what the GPU reads (fine pixels and tags); coarse bytes are restored whole anyway */
    }
    const drawlist_page *disp = drawlist_get(&r->scratch, 0xA000), *base = drawlist_get(&skel->prehud, 0xA000);
    if (!ok || !disp || disp->size < 64000 || !base || !disp->hi) return NULL;
    r->sa_key = key;
    const double q3 = probe_ms();
    const int k_disp = (int)(disp - r->scratch.page);
    /* inter_hi: the guarded picture. Outside the rows this picture and the last one changed it already holds the guarded base (same key). */
    int iy0 = r->sa_y0[k_disp], iy1 = r->sa_y1[k_disp];
    const int full = r->sa_ikey_in != key;
    if (full) { iy0 = 0; iy1 = 200; }
    else join_rows(&iy0, &iy1, r->sa_iy0, r->sa_iy1);
    if (iy1 > iy0) { if (iy0 > 0) iy0--; if (iy1 < 200) iy1++; }   /* the guard reads a row's neighbours */
    const int n = r->scale;
    const size_t W = (size_t)320 * n;
    uint8_t *itag = r->inter_tag && disp->tag ? r->inter_tag : NULL;
    if (iy1 > iy0) {
        const size_t off = (size_t)iy0 * n * W, len = (size_t)(iy1 - iy0) * n * W;
        memcpy(r->inter_hi + off, disp->hi + off, len);
        if (itag) memcpy(itag + off, disp->tag + off, len);
        uint64_t flat = 0, agree = 0;
        hires_guard_masked_tag_rows(disp->b, r->inter_hi, n, disp->strokes, &flat, &agree, itag, iy0, iy1);
    }
    r->sa_iy0 = r->sa_y0[k_disp]; r->sa_iy1 = r->sa_y1[k_disp];
    r->sa_ikey = itag || !r->inter_tag ? key : 0;          /* a tag plane not kept in step: next picture copies all */
    r->shown_key = full ? 0 : key;
    {   /* the fine rows the shown picture may differ from the step's guarded base in, for the upload (gpu_compose_set_rows) */
        int y0 = r->sa_y0[k_disp], y1 = r->sa_y1[k_disp];
        if (y1 > y0) { if (y0 > 0) y0--; if (y1 < 200) y1++; }
        r->shown_y0 = y0 * n; r->shown_y1 = y1 * n;
    }
    if (full) r->shown_key = 0;
    { const double q4 = probe_ms(); t_restore += q1 - q0; t_feed += q2 - q1; t_diff += q3 - q2; t_inter += q4 - q3; }
    r->sa_pictures++; r->sa_rows += (uint64_t)rows_restored;
    if (r->stepa_check && !r->gpu_hud) {   /* the full path's picture, pixel for pixel */
        static drawlist chk; static hires *chr; static uint8_t *chi; static int init;
        if (!init) { drawlist_init(&chk); chr = (hires *)malloc(sizeof *chr); chi = (uint8_t *)malloc(W * 200 * n); init = 1; }
        drawlist_copy(&chk, &skel->prehud);
        memcpy(chr, skel->prehud_hires, sizeof *chr);
        for (int i = from; i < nrec; i++) feed(&chk, chr, rec[i].kind, rec[i].v, rec[i].n);
        const drawlist_page *cd = drawlist_get(&chk, 0xA000);
        r->sa_checked++;
        if (cd && chi) {
            memcpy(chi, cd->hi, W * 200 * n);
            uint64_t flat = 0, agree = 0;
            hires_guard_masked_tag(cd->b, chi, n, cd->strokes, &flat, &agree, NULL);
            size_t bad = 0, first = (size_t)-1;
            for (size_t q = 0; q < W * 200 * n; q++) if (chi[q] != r->inter_hi[q]) { if (first == (size_t)-1) first = q; bad++; }
            if (memcmp(cd->b, disp->b, 64000)) bad++;
            if (bad) { r->sa_mismatch++; fprintf(stderr, "[stepa-check] MISMATCH picture %llu: %zu fine pixels differ, first at row %zu (band %d-%d)\n", (unsigned long long)r->sa_checked, bad, first == (size_t)-1 ? 0 : first / W, r->sa_iy0 * n, r->sa_iy1 * n); }
        }
    }
    if (r->sa_pictures % 300 == 0) fprintf(stderr, "[stepa-ms] per picture: restore %.3f, HUD records %.3f, row compare %.3f, picture copy and guard %.3f\n",
        t_restore / r->sa_pictures, t_feed / r->sa_pictures, t_diff / r->sa_pictures, t_inter / r->sa_pictures);
    if (r->sa_pictures % 300 == 0) fprintf(stderr, "[stepa] %llu pictures, %.1f coarse rows restored each (all pages), %llu full copies, shown band %d-%d of %d rows; checked %llu, mismatched %llu\n",
        (unsigned long long)r->sa_pictures, (double)r->sa_rows / r->sa_pictures, (unsigned long long)r->sa_full, r->shown_y0, r->shown_y1, 200 * n, (unsigned long long)r->sa_checked, (unsigned long long)r->sa_mismatch);
    return disp->b;
}

static const uint8_t *draw_list_hud(drawlive *r, const drawlive_step *skel)
{
    if (r->stepa && r->scale && r->inter_hi && skel->have_prehud) {
        const uint8_t *p = draw_hud_stepa(r, skel, r->ilist.rec, r->ilist.n);
        if (p) return p;
        r->sa_key = r->sa_ikey = 0; r->shown_key = 0; r->shown_strokes = 0;
    }
    return draw_recs_ex(r, skel, r->ilist.rec, r->ilist.n, r->prehud_on && skel->have_prehud);
}

static const uint8_t *draw_list(drawlive *r, const drawlive_step *skel)
{
    return draw_recs(r, skel, r->ilist.rec, r->ilist.n);
}

/* rerun: the original's own drawing at t between steps a and b on a copy of a's machine, spliced after a's records
 * from before its composition, then replayed from a's start. NULL when it could not be made. */
static int run_copy(drawlive *r, const drawlive_step *a, const drawlive_step *b, double t)
{
    if (!a->have_compose || !a->snap.valid) return 0;
    {   /* F117_SKIP_COMPOSE_BEFORE=CLOCK (test tooling, host main.c compose_skipped): before that clock no Remastered picture is made, so the render copy is not run either: it would be thrown away */
        static uint64_t until; static int init;
        if (!init) { const char *e = getenv("F117_SKIP_COMPOSE_BEFORE"); until = e ? strtoull(e, NULL, 0) : 0; init = 1; }
        if (until && a->snap.m.cpu.icount < until) return 0;
    }
    if (!rerun_draw(&a->snap, b && b->have_compose ? &b->snap : NULL, t, &r->rlist, r->check) || r->rlist.first < 0) return 0;
    r->rerun_insn += r->rlist.insn;
    return 1;
}

/* the step's own start state, then the copy's drawing from where the step begins (its first phase) */
static const uint8_t *draw_copy(drawlive *r, const drawlive_step *a, const rerun_list *l)
{
    return draw_recs(r, a, l->rec + l->first, l->nrec - l->first);
}

static const uint8_t *draw_rerun(drawlive *r, const drawlive_step *a, const drawlive_step *b, double t)
{
    return run_copy(r, a, b, t) ? draw_copy(r, a, &r->rlist) : NULL;
}

void drawlive_prepare_rerun(drawlive *r, drawlive_plan *plan)
{
    plan->rerun_ready = 0;
    if (!r->rerun || plan->kind != PLAN_BETWEEN || plan->extrapolate || r->ib < 0) return;
    const int ia = plan->sa >= 0 ? plan->sa : (r->smooth ? r->im : r->ia), ib = plan->sb >= 0 ? plan->sb : r->ib;
    if (ia < 0 || ib < 0) return;
    plan->rerun_ready = run_copy(r, &r->step[ia], &r->step[ib], plan->t);
}

int drawlive_stage2_view(const drawlive *r, const drawlive_plan *plan, rerun_forward_view *view)
{
    if (plan->sa < 0 || plan->sb < 0) return 0;
    const drawlive_step *a = &r->step[plan->sa], *b = &r->step[plan->sb];
    return a->seq == plan->older && b->seq == plan->newer &&
           rerun_forward_view_at(&a->snap, &b->snap, plan->t, view);
}

int drawlive_scene_begin(drawlive *r)
{
    if (!r->rerun || r->ib < 0) return 0;
    const int ia = r->smooth ? r->im : r->ia, ib = r->ib;
    if (ia < 0 || ib < 0) return 0;
    const drawlive_step *a = &r->step[ia], *b = &r->step[ib];
    if (!a->have_compose || !a->snap.valid) return 0;
    {   /* F117_SKIP_COMPOSE_BEFORE, as run_copy */
        static uint64_t until; static int init;
        if (!init) { const char *e = getenv("F117_SKIP_COMPOSE_BEFORE"); until = e ? strtoull(e, NULL, 0) : 0; init = 1; }
        if (until && a->snap.m.cpu.icount < until) return 0;
    }
    return rerun_begin(&a->snap, b->have_compose ? &b->snap : NULL, 1.0, &r->rlist_scene, 0);
}
int drawlive_scene_step(drawlive *r, uint64_t budget_ns) { (void)r; return rerun_step_scene(budget_ns); }
int drawlive_scene_ok(void) { return rerun_job_ok(); }

/* F117_COPY_FP (diagnostic): a fingerprint of the job's copy records (kinds, clocks, values). */
unsigned long long drawlive_job_fingerprint(const drawlive *r)
{
    unsigned long long h = 1469598103934665603ull;
    const rerun_list *l = &r->rlist_job;
    for (int i = l->first < 0 ? 0 : l->first; i < l->nrec; i++) {
        const interp_rec *x = &l->rec[i];
        h ^= (unsigned char)x->kind; h *= 1099511628211ull;
        for (int k = 0; k < x->n; k++) { h ^= (unsigned)x->v[k]; h *= 1099511628211ull; }
    }
    return h;
}

/* Hand the copy's records to the job: swap them into rlist_job (the next copy fills the other list). Call it when no job is drawing from rlist_job. */
void drawlive_commit_rerun(drawlive *r, const drawlive_plan *plan)
{
    if (plan->rerun_ready) { const rerun_list tmp = r->rlist; r->rlist = r->rlist_job; r->rlist_job = tmp; }
}

/* rerun: at t = 0 the copy, unchanged, must draw a's own picture exactly. */
static void check_rerun(drawlive *r)
{
    const drawlive_step *a = &r->step[r->smooth ? r->im : r->ia];
    const uint8_t *p = draw_rerun(r, a, NULL, 0);
    if (!p) return;
    static int reported;
    if (getenv("F117_RERUN_DEBUG") && reported < 12) {
        /* the copy's records against the step's own from its composition */
        int i = 0;
        const int base = r->rlist.first;
        for (; base + i < r->rlist.nrec && i < a->nrec; i++) {
            const interp_rec *x = &r->rlist.rec[base + i], *y = &a->rec[i];
            if (x->kind != y->kind || x->n != y->n || memcmp(x->v, y->v, (size_t)x->n * sizeof *x->v)) break;
        }
        fprintf(stderr, "[rerun] step %llu: copy %d records from its first phase, step %d; first difference at %d: copy '%c' n %d, step '%c' n %d\n",
                (unsigned long long)a->seq, r->rlist.nrec - base, a->nrec, i,
                base + i < r->rlist.nrec ? r->rlist.rec[base + i].kind : '-', base + i < r->rlist.nrec ? r->rlist.rec[base + i].n : -1,
                i < a->nrec ? a->rec[i].kind : '-', i < a->nrec ? a->rec[i].n : -1);
        reported++;
    }
    const size_t fine = (size_t)320 * r->scale * 200 * r->scale;
    if (!memcmp(p, a->picture, 64000) && (!r->scale || !memcmp(r->inter_hi, a->picture_hi, fine))) r->rerun_t0_exact++;
    else r->rerun_t0_inexact++;
}

/* A published pair a hair from each end must draw as the two frames it joins. */
static void check_pair(drawlive *r)
{
    const drawlive_step *a = &r->step[r->smooth ? r->im : r->ia], *b = &r->step[r->ib];
    /* t a hair from each end: every paired primitive regenerated and moved by
     * almost nothing. With a scale the vertices move too, and 1e-6 of a step
     * can still move a camera-space coordinate by a rounding unit, which shows
     * on a finer grid: those ends are checked at 1e-9. */
    int sb, ok = 1;
    const int flags = (r->scale ? INTERP_VERTICES : 0) | (r->scale > 1 ? INTERP_HUD | INTERP_BANDS | INTERP_HUD_HOLD : 0);
    const double eps = r->scale ? 1e-9 : 1e-6;
    const size_t fine = (size_t)320 * r->scale * 200 * r->scale;
    interp_inbetween_ex(a->parsed, b->parsed, b->pair, eps, flags, &r->ilist, &sb, NULL);
    const uint8_t *p = draw_list(r, sb ? b : a);
    ok &= p && !memcmp(p, a->picture, 64000) && (!r->scale || !memcmp(r->inter_hi, a->picture_hi, fine));
    /* the newer end without the HUD's hold: everything that moves arrives at b (the held discrete items change at
     * t = 1 itself, where b's own picture is shown) */
    interp_inbetween_ex(a->parsed, b->parsed, b->pair, 1 - eps, flags & ~INTERP_HUD_HOLD, &r->ilist, &sb, NULL);
    p = draw_list(r, sb ? b : a);
    ok &= p && !memcmp(p, b->picture, 64000) && (!r->scale || !memcmp(r->inter_hi, b->picture_hi, fine));
    if (ok) r->pairs_exact++; else r->pairs_inexact++;
}

/* Consume only records added since the last host slice. Keep the new
 * frame's start separate: the published starts still serve interpolation. */
static void replay_available(drawlive *r, const drawfeed_frame *fr)
{
    if (!r->pending_active || r->pending_seq != fr->seq) {
        if (fr->seq != r->next_seq || r->pending_active) drawlist_reset_keep_overlays(&r->list);
        if (r->interp) {
            drawlist_copy(&r->pending_start, &r->list);
            if (r->scale) memcpy(r->pending_hires, r->hr, sizeof *r->hr);
        }
        r->pending_seq = fr->seq;
        r->pending_at = 0;
        r->pending_active = 1;
        r->pending_prehud_have = 0;
    }
    char kind;
    uint64_t icount;
    const int32_t *v;
    int n;
    while (drawfeed_next(fr, &r->pending_at, &kind, &icount, &v, &n)) {
        if (kind == 'h' && r->prehud_on && r->interp && r->scale && !r->pending_prehud_have) {   /* the HUD pass begins: keep the replay as it stands */
            drawlist_copy(&r->pending_prehud, &r->list);
            memcpy(r->pending_prehud_hires, r->hr, sizeof *r->hr);
            r->pending_prehud_have = 1;
        }
        feed(&r->list, r->scale ? r->hr : NULL, kind, v, n);
    }
}

/* Transfer ownership of the cached fine pages without copying them again.
 * A small stack buffer avoids placing a whole drawlist on the stack. */
static void swap_lists(drawlist *a, drawlist *b)
{
    unsigned char temp[256];
    for (size_t at = 0; at < sizeof *a; at += sizeof temp) {
        const size_t left = sizeof *a - at;
        const size_t n = left < sizeof temp ? left : sizeof temp;
        unsigned char *pa = (unsigned char *)a + at, *pb = (unsigned char *)b + at;
        memcpy(temp, pa, n); memcpy(pa, pb, n); memcpy(pb, temp, n);
    }
}

static int free_step(const drawlive *r)
{
    for (int k = 0; k < DRAWLIVE_POOL; k++)
        if (r->step[k].state == DRAWLIVE_FREE) return k;
    return -1;
}

/* The collector thread's lock (drawlive_collector_start): no-ops when there is none. */
static void qlock(const drawlive *r) { if (r->lk) g_ops->lock(r->lk); }
static void qunlock(const drawlive *r) { if (r->lk) g_ops->unlock(r->lk); }
static void qwake(const drawlive *r) { if (r->cv) g_ops->broadcast(r->cv); }

/* A frame closes into a step when the replay equals its display; the step is
 * queued for drawlive_publish, with its pairing against the step kept before. */
static void close_frame(drawlive *r, drawfeed *f, const drawfeed_frame *fr)
{
    r->frames++;
    const drawlist_page *disp = drawlist_get(&r->list, 0xA000);
    if (!r->list.have_seg || !disp || disp->size < 64000) { r->unseeded++; return; }
    if (memcmp(disp->b, fr->display, 64000)) {
        r->inexact++;
        if (getenv("F117_MAP_DIAG")) fprintf(stderr, "[mapdiag] frame %llu not equal to the display: list reset, kept rectangles lost (%d kept)\n", (unsigned long long)fr->seq, r->list.noverlay);
        qlock(r); r->queue[r->nqueue++] = -1; qunlock(r);   /* nothing is shown until the next exact frame */
        drawlist_reset_keep_overlays(&r->list);
        if (r->scale) hires_init(r->hr, r->scale);
        drawfeed_reseed(f);
        return;
    }
    const double c0 = probe_ms();
    r->exact++;
    qlock(r);
    const int k = free_step(r);
    qunlock(r);
    drawlive_step *s = &r->step[k];
    if (r->interp) {
        swap_lists(&s->start, &r->pending_start);
        if (r->scale) {
            hires *old = s->start_hires;
            s->start_hires = r->pending_hires;
            r->pending_hires = old;
        }
    }
    if (r->interp && r->prehud_on && r->scale) {
        swap_lists(&s->prehud, &r->pending_prehud);
        hires *oh = s->prehud_hires; s->prehud_hires = r->pending_prehud_hires; r->pending_prehud_hires = oh;
        s->have_prehud = r->pending_prehud_have;
        s->gen = ++r->gen_next;
    }
    const double c1 = probe_ms();
    memcpy(s->picture, disp->b, 64000);
    const double c2 = probe_ms();
    if (r->scale) take_fine(r, disp, s->picture_hi, s->picture_tag);
    const double c3 = probe_ms();
    s->seq = fr->seq;
    s->end = fr->end;
    s->ok = 1;
    interp_pairing_free(s->pair);
    s->pair = NULL;
    s->have_compose = 0;
    if (r->rerun && fr->have_compose) {
        rerun_snap_swap(&s->snap, &((drawfeed_frame *)fr)->snap);
        s->have_compose = 1;
    }
    const double c4 = probe_ms();
    if (r->interp) {
        keep_step(s, fr);
        s->ok = !fr->seeded && s->start.have_seg;
        const drawlive_step *prev = r->newest >= 0 ? &r->step[r->newest] : NULL;
        if (prev && prev->ok && s->ok && s->seq == prev->seq + 1) {
            s->pair = interp_pair(prev->parsed, s->parsed);
            r->pairs++;
        }
    }
    { const double c5 = probe_ms(); g_pc_start += c1 - c0; g_pc_picture += c2 - c1; g_pc_fine += c3 - c2; g_pc_snap += c4 - c3; g_pc_keep += c5 - c4; g_pc_n++; }
    qlock(r);
    s->state = DRAWLIVE_STAGED;
    r->newest = k;
    r->queue[r->nqueue++] = k;
    qunlock(r);
}

int drawlive_collect(drawlive *r, drawfeed *f)
{
    int taken = 0;
    const drawfeed_frame *fr;
    while ((fr = drawfeed_oldest(f))) {
        if (free_step(r) < 0 || r->nqueue >= DRAWLIVE_QUEUE - 1) return taken;   /* wait for a publish */
        const double pr0 = probe_ms();
        replay_available(r, fr);
        const double pr1 = probe_ms();
        close_frame(r, f, fr);
        const double pr2 = probe_ms();
        g_probe_replay += pr1 - pr0; g_probe_close += pr2 - pr1;
        if (++g_probe_n % 600 == 0 && getenv("F117_COLLECT_PROBE")) {
            fprintf(stderr, "[collect2] close_frame parts per frame: pending-start swap %.3f, picture copy %.3f, fine copy+guard %.3f, snap swap %.3f, keep+pair %.3f\n", g_pc_start / g_pc_n, g_pc_picture / g_pc_n, g_pc_fine / g_pc_n, g_pc_snap / g_pc_n, g_pc_keep / g_pc_n); fprintf(stderr, "[collect] per closed frame: replay of its last records %.3f ms, close_frame %.3f ms; open-frame replays (every slice) %.3f ms a call over %u calls\n", g_probe_replay / g_probe_n, g_probe_close / g_probe_n, g_probe_open / (g_probe_calls ? g_probe_calls : 1), g_probe_calls); }
        drawfeed_pop(f);
        r->next_seq = fr->seq + 1;
        r->pending_active = 0;
        taken++;
    }
    const drawfeed_frame *current = open_frame(f);
    if (current->len) { const double po0 = probe_ms(); replay_available(r, current); g_probe_open += probe_ms() - po0; g_probe_calls++; }
    return taken;
}

/* The collector thread: takes each closed frame out of the ring (so the observer never waits on it), replays it and closes it into a step. */
static void take_frame(drawfeed *f, drawfeed_frame *w)
{
    drawfeed_frame *s = &f->frame[f->head];
    int32_t *rec = w->rec; const size_t cap = w->cap;
    w->seq = s->seq; w->start = s->start; w->end = s->end; w->phases = s->phases; w->seeded = s->seeded;
    w->rec = s->rec; w->len = s->len; w->cap = s->cap;
    s->rec = rec; s->len = 0; s->cap = cap;
    memcpy(w->display, s->display, sizeof w->display);
    rerun_snap_swap(&w->snap, &s->snap);
    w->have_compose = s->have_compose; s->have_compose = 0;
    f->head = (f->head + 1) % DRAWFEED_FRAMES;
    f->count--;
}

static int collector_main(void *p)
{
    drawlive *r = (drawlive *)p;
    drawfeed *f = r->hfeed;
    void *lk = r->lk, *cv = r->cv;
    g_ops->lock(lk);
    while (r->hrun) {
        if (!(f->count > 0 && free_step(r) >= 0 && r->nqueue < DRAWLIVE_QUEUE - 1)) {   /* nothing closed, or waiting for a publish to free a step */
            r->hblocked = f->count > 0;
            r->hbusy = 0;
            g_ops->broadcast(cv);
            g_ops->wait(cv, lk, 2);
            continue;
        }
        r->hblocked = 0;
        r->hbusy = 1;
        take_frame(f, &r->hwork);
        g_ops->unlock(lk);
        replay_available(r, &r->hwork);
        close_frame(r, f, &r->hwork);
        r->next_seq = r->hwork.seq + 1;
        r->pending_active = 0;
        g_ops->lock(lk);
        r->hbusy = 0;
        g_ops->broadcast(cv);
    }
    g_ops->unlock(lk);
    return 0;
}

int drawlive_collector_start(drawlive *r, drawfeed *f, int sync, const drawlive_sync_ops *ops)
{
    if (r->thread || r->check || !ops) return 0;
    g_ops = ops;
    r->lk = ops->mutex_new();
    r->cv = ops->cond_new();
    if (!r->lk || !r->cv) return 0;
    f->lk = r->lk;
    r->hfeed = f; r->hrun = 1; r->hsync = sync;
    r->thread = ops->spawn(collector_main, r);
    if (!r->thread) { r->hrun = 0; f->lk = NULL; return 0; }
    return 1;
}

/* Wake the thread; with sync, wait until it has taken every closed frame (or is waiting for a free step). */
void drawlive_collector_kick(drawlive *r, int wait)
{
    if (!r->thread) return;
    void *lk = r->lk;
    g_ops->lock(lk);
    r->hblocked = 0;
    g_ops->broadcast(r->cv);
    if (r->hsync && wait)
        while (r->hrun && (r->hbusy || (r->hfeed->count > 0 && !r->hblocked))) g_ops->wait(r->cv, lk, 5);
    g_ops->unlock(lk);
}

void drawlive_collector_stop(drawlive *r)
{
    if (!r->thread) return;
    g_ops->lock(r->lk);
    r->hrun = 0;
    g_ops->broadcast(r->cv);
    g_ops->unlock(r->lk);
    g_ops->join(r->thread);
    r->thread = NULL;
    r->hfeed->lk = NULL;
}

static void release_step(drawlive *r, int k)
{
    interp_pairing_free(r->step[k].pair);
    r->step[k].pair = NULL;
    r->step[k].state = DRAWLIVE_FREE;
}

/* The steps drawlive_publish queued for release (defer_release): free them when no job can draw from them any more (the loop calls this after the wait). */
void drawlive_release_pending(drawlive *r)
{
    qlock(r);
    for (int q = 0; q < r->nrelease; q++) release_step(r, r->release_q[q]);
    r->nrelease = 0;
    qwake(r);
    qunlock(r);
}

/* Frees the first n queued steps only (the pipeline: a picture planned later may still draw from the others) and keeps the rest queued. */
void drawlive_release_pending_n(drawlive *r, int n)
{
    qlock(r);
    if (n > r->nrelease) n = r->nrelease;
    for (int q = 0; q < n; q++) release_step(r, r->release_q[q]);
    for (int q = n; q < r->nrelease; q++) r->release_q[q - n] = r->release_q[q];
    r->nrelease -= n;
    qwake(r);
    qunlock(r);
}

void drawlive_publish(drawlive *r)
{
    qlock(r);
    for (int q = 0; q < r->nqueue; q++) {
        const int k = r->queue[q];
        if (k < 0) { r->shown = 0; continue; }
        drawlive_step *s = &r->step[k];
        if (r->ia >= 0) { if (r->defer_release && r->nrelease < DRAWLIVE_POOL) r->release_q[r->nrelease++] = r->ia; else release_step(r, r->ia); }
        if (r->smooth) {                               /* three published: the playback clock sits between them */
            if (r->last_end && s->end > r->last_end) {
                const double d = (double)(s->end - r->last_end);
                r->ema = r->ema > 0 ? (1 - r->ema_alpha) * r->ema + r->ema_alpha * d : d;
            }
            r->last_end = s->end;
            r->ia = r->im;
            r->im = r->ib;
        } else r->ia = r->ib;
        r->ib = k;
        s->state = DRAWLIVE_PUBLISHED;
        r->shown = 1;
        r->picture = s->picture;
        r->picture_hi = r->scale ? s->picture_hi : NULL;
        r->picture_tag = r->scale ? s->picture_tag : NULL;
        r->picture_seq = s->seq;
        r->picture_end = s->end;
        if (r->check && s->pair && (r->smooth ? r->im : r->ia) >= 0) check_pair(r);
        if (r->check && r->rerun && (r->smooth ? r->im : r->ia) >= 0) check_rerun(r);
    }
    r->nqueue = 0;
    qwake(r);
    qunlock(r);
}

int drawlive_update(drawlive *r, drawfeed *f)
{
    int taken = 0, more;
    do {                               /* a closed frame may have waited for a free step */
        more = drawlive_collect(r, f);
        drawlive_publish(r);
        taken += more;
    } while (more && drawfeed_oldest(f));
    return taken;
}

void drawlive_steps(const drawlive *r, const drawlive_step **older, const drawlive_step **newer)
{
    const int prev = r->smooth ? r->im : r->ia;
    *older = prev >= 0 ? &r->step[prev] : NULL;
    *newer = r->ib >= 0 ? &r->step[r->ib] : NULL;
}

int drawlive_current(const drawlive *r, const machine_t *m)
{
    /* the replay's display is A000 from offset 0, and holds only what the
     * game_draw frames draw: a frame a quarter second old has been left */
    return r->shown && m->video_mode == 0x13 && m->scan_start == 0 && !strcmp(dos_current_program(m), "VGAME.EXE") &&
           m->cpu.icount - r->picture_end < m->ips / 4;
}

int drawlive_present(drawlive *r, const machine_t *m, present_frame *f)
{
    r->shown_hi = r->shown_tag = NULL;
    if (f->text || !drawlive_current(r, m)) return 0;
    memcpy(f->vram, r->picture, 64000);
    if (r->scale) { r->shown_hi = r->picture_hi; r->shown_tag = r->picture_tag; }
    r->diag_kind = PLAN_REPLAY; r->diag_t = 0; r->diag_older = r->diag_newer = r->picture_seq;
    return 1;
}

/* The smoothed playback clock: the position advances with the machine clock over the smoothed step duration, is
 * pulled towards one step behind the newest closed step, never passes it, and names the two consecutive published
 * steps it lies between. Returns 0 when it cannot (fewer than two consecutive steps): the caller plans as before. */
static int plan_smooth(drawlive *r, uint64_t icount, drawlive_plan *plan)
{
    const int idx[3] = { r->ia, r->im, r->ib };
    if (r->ib < 0 || r->im < 0 || r->ema <= 0) return 0;
    const drawlive_step *newest = &r->step[r->ib];
    const uint64_t oldest_seq = r->ia >= 0 ? r->step[r->ia].seq : r->step[r->im].seq;
    const double N = (double)newest->seq;
    if (!r->play_icount) { r->play_pos = N - 1.0 - r->extra_delay; r->play_icount = icount; }
    if (icount > r->play_icount) {
        const double rate = 1.0 + r->pull_in * ((N - 1.0 - r->extra_delay) - r->play_pos);
        r->play_pos += (rate > 0.2 ? rate : 0.2) * (double)(icount - r->play_icount) / r->ema;
    }
    r->play_icount = icount;
    if (r->play_pos > N) r->play_pos = N;
    if (r->play_pos < (double)oldest_seq) r->play_pos = (double)oldest_seq;
    int lo = -1;
    for (int k = 0; k < 2; k++) {
        if (idx[k] >= 0 && idx[k + 1] >= 0 && r->step[idx[k]].seq + 1 == r->step[idx[k + 1]].seq &&
            r->step[idx[k + 1]].pair) {
            /* The first seeded pair may start ahead of the playback clock.
             * Start at that pair instead of falling back to the raw clock
             * and rewinding when smoothing catches up. A missing machine
             * snapshot changes how we draw, never the playback position. */
            if (r->play_pos < (double)r->step[idx[k]].seq) r->play_pos = (double)r->step[idx[k]].seq;
            if (r->play_pos <= (double)r->step[idx[k + 1]].seq) { lo = k; break; }
        }
    }
    if (getenv("F117_RERUN_DEBUG")) fprintf(stderr, "[clock] idx %d %d %d pos %.3f N %.0f ema %.0f lo %d\n", idx[0], idx[1], idx[2], r->play_pos, N, r->ema, lo);
    if (lo < 0) return 0;
    const drawlive_step *a = &r->step[idx[lo]], *b = &r->step[idx[lo + 1]];
    plan->sa = idx[lo]; plan->sb = idx[lo + 1];
    plan->older = a->seq; plan->newer = b->seq;
    plan->t = r->play_pos - (double)a->seq;
    plan->t_shown = plan->t;
    plan->kind = PLAN_BETWEEN;
    return 1;
}

int drawlive_plan_interp(drawlive *r, const machine_t *m, const present_frame *f, uint64_t icount,
                         drawlive_plan *plan)
{
    memset(plan, 0, sizeof *plan);
    plan->sa = plan->sb = -1;
    const drawlive_step *a, *b;
    drawlive_steps(r, &a, &b);
    /* what the worker would otherwise read from the live ring when it runs: the ring's older and newer step and the replayed picture (plan_smooth overrides the
     * indices with its own pair). With these in the plan the worker needs nothing drawlive_publish changes. */
    plan->sa = r->smooth ? r->im : r->ia; plan->sb = r->ib;
    plan->picture = r->picture; plan->picture_hi = r->scale ? r->picture_hi : NULL; plan->picture_tag = r->scale ? r->picture_tag : NULL;
    if (a && b) {
        plan->older = a->seq;
        plan->newer = b->seq;
        const double span = b->end > a->end ? (double)(b->end - a->end) : 0;
        plan->t_shown = span > 0 ? ((double)icount - (double)b->end) / span : 0.0;
    }
    if (f->text || !drawlive_current(r, m)) { plan->kind = PLAN_NONE; return 0; }
    if (r->rerun && r->smooth && plan_smooth(r, icount, plan)) return 1;
    if (!a || !b || !b->pair || r->picture_seq != b->seq || b->end <= a->end) { plan->kind = PLAN_REPLAY; return 1; }
    double t = (double)(icount - b->end) / (double)(b->end - a->end);
    if (r->extrapolate) t = t + 1 > 2 ? 2 : t + 1;
    plan->t = t;
    plan->extrapolate = r->extrapolate;
    if (t <= 0) plan->kind = PLAN_OLDER;
    else if (t == 1 || (t > 1 && !r->extrapolate)) plan->kind = PLAN_NEWER;
    else plan->kind = PLAN_BETWEEN;
    return 1;
}

int drawlive_render(drawlive *r, const drawlive_plan *plan, present_frame *f)
{
    r->shown_hi = r->shown_tag = NULL;
    r->shown_copy = 0;
    r->sa_key_in = r->sa_key; r->sa_ikey_in = r->sa_ikey; r->sa_key = r->sa_ikey = 0; r->shown_key = 0; r->shown_strokes = 0;
    if (plan->kind == PLAN_NONE) return 0;
    r->diag_kind = plan->kind; r->diag_t = plan->t; r->diag_older = plan->older; r->diag_newer = plan->newer;
    if (plan->kind == PLAN_OLDER || plan->kind == PLAN_NEWER || plan->kind == PLAN_BETWEEN) {
        const int older_ix = plan->sa >= 0 ? plan->sa : (r->smooth ? r->im : r->ia);   /* as drawlive_steps names it */
        const drawlive_step *a = &r->step[older_ix], *b = &r->step[plan->sb >= 0 ? plan->sb : r->ib];
        if (plan->kind == PLAN_OLDER) {
            memcpy(f->vram, a->picture, 64000);
            if (r->scale) { r->shown_hi = a->picture_hi; r->shown_tag = a->picture_tag; }
            return 1;
        }
        if (plan->kind == PLAN_NEWER) {
            memcpy(f->vram, b->picture, 64000);
            if (r->scale) { r->shown_hi = b->picture_hi; r->shown_tag = b->picture_tag; }
            return 1;
        }
        if (plan->stage2 && !plan->extrapolate) {        /* the picture layer (the HUD), then the scene: the blend the host staged */
            {   /* The picture layer at this picture's own time (17 November 2026): the two steps' draw lists interpolated and drawn, as the Enhanced path does, with no machine copy.
                 * The nearer step's finished picture moved the HUD's horizon bar and pitch ladder at the game's 15 steps a second while the world under them moved every picture. */
                static int hud_step = -1;
                if (hud_step < 0) hud_step = getenv("F117_STAGE2_HUD_STEP") != NULL;      /* =1: the nearer step's own picture, as before */
                if (!hud_step && b->pair && a->parsed && b->parsed) {
                    int sb;
                    /* F117_PREHUD replays only from the HUD pass on, on the skeleton's own snapshot: interpolating the records before it is wasted */
                    const int from_hud = r->prehud_on && r->scale && a->have_prehud && b->have_prehud ? INTERP_FROM_HUD : 0;
                    interp_inbetween_ex(a->parsed, b->parsed, b->pair, plan->t,
                                        (r->scale ? INTERP_VERTICES : 0) | (r->scale > 1 ? INTERP_HUD | INTERP_BANDS | INTERP_HUD_HOLD : 0) | from_hud,
                                        &r->ilist, &sb, r->istats);
                    const uint8_t *hp = draw_list_hud(r, sb ? b : a);
                    if (hp) {
                        memcpy(f->vram, hp, 64000);
                        if (r->scale) { r->shown_hi = r->inter_hi; r->shown_tag = r->inter_tag; }
                        r->shown_copy = 1;
                        r->inbetweens++;
                        return 1;
                    }
                }
            }
            const drawlive_step *nr = plan->t >= 0.5 ? b : a;
            memcpy(f->vram, nr->picture, 64000);
            if (r->scale) { r->shown_hi = nr->picture_hi; r->shown_tag = nr->picture_tag; }
            r->shown_copy = 1;
            r->inbetweens++;
            return 1;
        }
        if (r->rerun && !plan->extrapolate) {
            const uint8_t *p = plan->rerun_ready ? draw_copy(r, a, &r->rlist_job) : plan->rerun_inline ? draw_rerun(r, a, b, plan->t) : NULL;
            {   /* F117_RERUN_FALLBACK_AT=N (diagnostic): the Nth picture made on a copy is treated as late, as a slow machine would */
                static long forced = -2, n;
                if (forced == -2) { const char *e = getenv("F117_RERUN_FALLBACK_AT"); forced = e ? atol(e) : -1; }
                if (p && forced >= 0 && ++n == forced) p = NULL;
            }
            if (p) {
                memcpy(f->vram, p, 64000);
                if (r->scale) { r->shown_hi = r->inter_hi; r->shown_tag = r->inter_tag; }
                r->shown_copy = plan->rerun_ready || plan->rerun_inline;
                r->inbetweens++;
                r->rerun_drawn++;
                return 1;
            }
            r->rerun_fell_back++;
        }
        int sb;
        interp_inbetween_ex(a->parsed, b->parsed, b->pair, plan->t,
                            (r->scale ? INTERP_VERTICES : 0) | (r->scale > 1 ? INTERP_HUD | INTERP_BANDS | INTERP_HUD_HOLD : 0) |
                            (plan->extrapolate ? INTERP_EXTRAPOLATE : 0),
                            &r->ilist, &sb, r->istats);
        const uint8_t *p = draw_list(r, sb ? b : a);
        if (p) {
            memcpy(f->vram, p, 64000);
            if (r->scale) { r->shown_hi = r->inter_hi; r->shown_tag = r->inter_tag; }
            r->inbetweens++;
            return 1;
        }
    }
    memcpy(f->vram, plan->picture ? plan->picture : r->picture, 64000);                /* the replayed picture */
    if (r->scale) { r->shown_hi = plan->picture ? plan->picture_hi : r->picture_hi; r->shown_tag = plan->picture ? plan->picture_tag : r->picture_tag; }
    r->diag_kind = PLAN_REPLAY;
    return 1;
}

int drawlive_present_interp(drawlive *r, const machine_t *m, present_frame *f)
{
    drawlive_plan plan;
    drawlive_plan_interp(r, m, f, m->cpu.icount, &plan);
    plan.rerun_inline = 1;                                      /* single-threaded: this is the machine's thread */
    return drawlive_render(r, &plan, f);
}
