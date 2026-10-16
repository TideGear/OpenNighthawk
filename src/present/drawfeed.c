/* drawfeed.c - see drawfeed.h. */
#include "drawfeed.h"

#include "diagtag.h"
#include "tls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* a frame with no present closes after this many phases all the same */
#define MAX_PHASES 8

static drawfeed_frame *open_frame(drawfeed *f)
{
    return &f->frame[(f->head + f->count) % DRAWFEED_FRAMES];
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
        f->closed++;
        if (f->count == DRAWFEED_FRAMES - 1) {         /* nobody took the oldest: drop it */
            f->head = (f->head + 1) % DRAWFEED_FRAMES;
            f->count--;
            f->dropped++;
            f->want_seed = 1;                           /* the replay must start again */
        }
        f->count++;
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
    if (!f->count) return;
    f->head = (f->head + 1) % DRAWFEED_FRAMES;
    f->count--;
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
    hires_init(r->hr, n);
    r->inter_hi = (uint8_t *)calloc(bytes, 1);
    const char *diag = getenv("F117_PRESENT_DIAG");
    if (diag && *diag) { drawlist_diag_enable(1); r->inter_tag = (uint8_t *)calloc(bytes, 1); }
    for (int k = 0; k < DRAWLIVE_POOL; k++) {
        r->step[k].start_hires = (hires *)malloc(sizeof *r->hr);
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
    r->ema_alpha = 0.2; r->pull_in = 0.05;
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
static const uint8_t *draw_recs(drawlive *r, const drawlive_step *skel, const interp_rec *rec, int nrec)
{
    drawlist_copy(&r->scratch, &skel->start);
    hires *h = NULL;
    if (r->scale) { memcpy(r->scratch_hr, skel->start_hires, sizeof *r->scratch_hr); h = r->scratch_hr; }
    for (int i = 0; i < nrec; i++) feed(&r->scratch, h, rec[i].kind, rec[i].v, rec[i].n);
    const drawlist_page *disp = drawlist_get(&r->scratch, 0xA000);
    if (!disp || disp->size < 64000) return NULL;
    if (r->scale) {
        memcpy(r->inter_hi, disp->hi, (size_t)320 * r->scale * 200 * r->scale);
        if (r->inter_tag && disp->tag) memcpy(r->inter_tag, disp->tag, (size_t)320 * r->scale * 200 * r->scale);
        uint64_t flat = 0, agree = 0;
        hires_guard_masked_tag(disp->b, r->inter_hi, r->scale, disp->strokes, &flat, &agree,
                               r->inter_tag && disp->tag ? r->inter_tag : NULL);
    }
    return disp->b;
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
    if (!rerun_draw(&a->snap, b && b->have_compose ? &b->snap : NULL, t, &r->rlist, r->check) || r->rlist.first < 0) return 0;
    r->rerun_insn += r->rlist.insn;
    return 1;
}

/* the step's own start state, then the copy's drawing from where the step begins (its first phase) */
static const uint8_t *draw_copy(drawlive *r, const drawlive_step *a)
{
    return draw_recs(r, a, r->rlist.rec + r->rlist.first, r->rlist.nrec - r->rlist.first);
}

static const uint8_t *draw_rerun(drawlive *r, const drawlive_step *a, const drawlive_step *b, double t)
{
    return run_copy(r, a, b, t) ? draw_copy(r, a) : NULL;
}

void drawlive_prepare_rerun(drawlive *r, drawlive_plan *plan)
{
    plan->rerun_ready = 0;
    if (!r->rerun || plan->kind != PLAN_BETWEEN || plan->extrapolate || r->ib < 0) return;
    const int ia = plan->sa >= 0 ? plan->sa : (r->smooth ? r->im : r->ia), ib = plan->sb >= 0 ? plan->sb : r->ib;
    if (ia < 0 || ib < 0) return;
    plan->rerun_ready = run_copy(r, &r->step[ia], &r->step[ib], plan->t);
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
        if (fr->seq != r->next_seq || r->pending_active) drawlist_reset(&r->list);
        if (r->interp) {
            drawlist_copy(&r->pending_start, &r->list);
            if (r->scale) memcpy(r->pending_hires, r->hr, sizeof *r->hr);
        }
        r->pending_seq = fr->seq;
        r->pending_at = 0;
        r->pending_active = 1;
    }
    char kind;
    uint64_t icount;
    const int32_t *v;
    int n;
    while (drawfeed_next(fr, &r->pending_at, &kind, &icount, &v, &n))
        feed(&r->list, r->scale ? r->hr : NULL, kind, v, n);
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

/* A frame closes into a step when the replay equals its display; the step is
 * queued for drawlive_publish, with its pairing against the step kept before. */
static void close_frame(drawlive *r, drawfeed *f, const drawfeed_frame *fr)
{
    r->frames++;
    const drawlist_page *disp = drawlist_get(&r->list, 0xA000);
    if (!r->list.have_seg || !disp || disp->size < 64000) { r->unseeded++; return; }
    if (memcmp(disp->b, fr->display, 64000)) {
        r->inexact++;
        r->queue[r->nqueue++] = -1;                    /* nothing is shown until the next exact frame */
        drawlist_reset(&r->list);
        if (r->scale) hires_init(r->hr, r->scale);
        drawfeed_reseed(f);
        return;
    }
    r->exact++;
    const int k = free_step(r);
    drawlive_step *s = &r->step[k];
    if (r->interp) {
        swap_lists(&s->start, &r->pending_start);
        if (r->scale) {
            hires *old = s->start_hires;
            s->start_hires = r->pending_hires;
            r->pending_hires = old;
        }
    }
    memcpy(s->picture, disp->b, 64000);
    if (r->scale) take_fine(r, disp, s->picture_hi, s->picture_tag);
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
    if (r->interp) {
        keep_step(s, fr);
        s->ok = !fr->seeded && s->start.have_seg;
        const drawlive_step *prev = r->newest >= 0 ? &r->step[r->newest] : NULL;
        if (prev && prev->ok && s->ok && s->seq == prev->seq + 1) {
            s->pair = interp_pair(prev->parsed, s->parsed);
            r->pairs++;
        }
    }
    s->state = DRAWLIVE_STAGED;
    r->newest = k;
    r->queue[r->nqueue++] = k;
}

int drawlive_collect(drawlive *r, drawfeed *f)
{
    int taken = 0;
    const drawfeed_frame *fr;
    while ((fr = drawfeed_oldest(f))) {
        if (free_step(r) < 0 || r->nqueue >= DRAWLIVE_QUEUE - 1) return taken;   /* wait for a publish */
        replay_available(r, fr);
        close_frame(r, f, fr);
        drawfeed_pop(f);
        r->next_seq = fr->seq + 1;
        r->pending_active = 0;
        taken++;
    }
    const drawfeed_frame *current = open_frame(f);
    if (current->len) replay_available(r, current);
    return taken;
}

static void release_step(drawlive *r, int k)
{
    interp_pairing_free(r->step[k].pair);
    r->step[k].pair = NULL;
    r->step[k].state = DRAWLIVE_FREE;
}

void drawlive_publish(drawlive *r)
{
    for (int q = 0; q < r->nqueue; q++) {
        const int k = r->queue[q];
        if (k < 0) { r->shown = 0; continue; }
        drawlive_step *s = &r->step[k];
        if (r->ia >= 0) release_step(r, r->ia);
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
    if (!r->play_icount) { r->play_pos = N - 1.0; r->play_icount = icount; }
    if (icount > r->play_icount) {
        const double rate = 1.0 + r->pull_in * ((N - 1.0) - r->play_pos);
        r->play_pos += (rate > 0.2 ? rate : 0.2) * (double)(icount - r->play_icount) / r->ema;
    }
    r->play_icount = icount;
    if (r->play_pos > N) r->play_pos = N;
    if (r->play_pos < (double)oldest_seq) r->play_pos = (double)oldest_seq;
    int lo = -1;
    for (int k = 0; k < 2; k++)
        if (idx[k] >= 0 && idx[k + 1] >= 0 && r->step[idx[k]].seq + 1 == r->step[idx[k + 1]].seq &&
            r->step[idx[k + 1]].pair && r->step[idx[k]].have_compose && r->step[idx[k + 1]].have_compose &&   /* a pair that can be drawn, or interpolated if the copy fails */
            r->play_pos >= (double)r->step[idx[k]].seq && r->play_pos <= (double)r->step[idx[k + 1]].seq) { lo = k; break; }
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
        if (r->rerun && !plan->extrapolate) {
            const uint8_t *p = plan->rerun_ready ? draw_copy(r, a) : plan->rerun_inline ? draw_rerun(r, a, b, plan->t) : NULL;
            if (p) {
                memcpy(f->vram, p, 64000);
                if (r->scale) { r->shown_hi = r->inter_hi; r->shown_tag = r->inter_tag; }
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
    memcpy(f->vram, r->picture, 64000);                /* the replayed picture */
    if (r->scale) { r->shown_hi = r->picture_hi; r->shown_tag = r->picture_tag; }
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
