/* drawfeed.c - see drawfeed.h. */
#include "drawfeed.h"

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
    f->want_seed = 1;
    begin(f, 0);
}

void drawfeed_free(drawfeed *f)
{
    for (int k = 0; k < DRAWFEED_FRAMES; k++) free(f->frame[k].rec);
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

/* Draw the in-between list on a copy of the replay at the skeleton's start. */
static const uint8_t *draw_list(drawlive *r, const drawlive_step *skel)
{
    drawlist_copy(&r->scratch, &skel->start);
    for (int i = 0; i < r->ilist.n; i++)
        drawlist_record(&r->scratch, r->ilist.rec[i].kind, r->ilist.rec[i].v, r->ilist.rec[i].n);
    const drawlist_page *disp = drawlist_get(&r->scratch, 0xA000);
    return disp && disp->size >= 64000 ? disp->b : NULL;
}

static void pair_steps(drawlive *r)
{
    interp_pairing_free(r->pair);
    r->pair = NULL;
    drawlive_step *a = &r->step[r->last ^ 1], *b = &r->step[r->last];
    if (!a->ok || !b->ok || b->seq != a->seq + 1) return;
    r->pair = interp_pair(a->parsed, b->parsed);
    r->pairs++;
    if (!r->check) return;
    int sb, ok = 1;
    interp_inbetween(a->parsed, b->parsed, r->pair, 1e-6, &r->ilist, &sb, NULL);
    const uint8_t *p = draw_list(r, sb ? b : a);
    ok &= p && !memcmp(p, a->picture, 64000);
    interp_inbetween(a->parsed, b->parsed, r->pair, 1 - 1e-6, &r->ilist, &sb, NULL);
    p = draw_list(r, sb ? b : a);
    ok &= p && !memcmp(p, b->picture, 64000);
    if (ok) r->pairs_exact++; else r->pairs_inexact++;
}

int drawlive_update(drawlive *r, drawfeed *f)
{
    int taken = 0;
    const drawfeed_frame *fr;
    while ((fr = drawfeed_oldest(f))) {
        if (fr->seq != r->next_seq) drawlist_reset(&r->list);   /* a frame was dropped: wait for a seed */
        r->next_seq = fr->seq + 1;
        drawlive_step *step = r->interp ? &r->step[r->last ^ 1] : NULL;
        if (step) drawlist_copy(&step->start, &r->list);
        size_t at = 0;
        char kind;
        uint64_t icount;
        const int32_t *v;
        int n;
        while (drawfeed_next(fr, &at, &kind, &icount, &v, &n)) {
            if (kind == 'Z' || kind == 'Y') {
                static uint8_t bytes[65536];
                for (int a = 0; a < n - 2; a++) bytes[a] = (uint8_t)v[2 + a];
                drawlist_seed(&r->list, kind, (uint16_t)v[0], (uint16_t)v[1], bytes, (unsigned)(n - 2));
            } else drawlist_record(&r->list, kind, v, n);
        }
        r->frames++;
        const drawlist_page *disp = drawlist_get(&r->list, 0xA000);
        if (!r->list.have_seg || !disp || disp->size < 64000) r->unseeded++;
        else if (!memcmp(disp->b, fr->display, 64000)) {
            r->exact++;
            memcpy(r->picture, disp->b, 64000);
            r->shown = 1;
            r->picture_seq = fr->seq;
            r->picture_end = fr->end;
            if (step) {
                keep_step(step, fr);
                memcpy(step->picture, disp->b, 64000);
                step->seq = fr->seq;
                step->end = fr->end;
                step->ok = !fr->seeded && step->start.have_seg;
                r->last ^= 1;
                pair_steps(r);
            }
        } else {
            r->inexact++;
            r->shown = 0;
            drawlist_reset(&r->list);
            drawfeed_reseed(f);
        }
        drawfeed_pop(f);
        taken++;
    }
    return taken;
}

int drawlive_current(const drawlive *r, const machine_t *m)
{
    /* the replay's display is A000 from offset 0, and holds only what the
     * game_draw frames draw: a frame a quarter second old has been left */
    return r->shown && m->video_mode == 0x13 && m->scan_start == 0 && !strcmp(dos_current_program(m), "VGAME.EXE") &&
           m->cpu.icount - r->picture_end < m->ips / 4;
}

int drawlive_present(const drawlive *r, const machine_t *m, present_frame *f)
{
    if (f->text || !drawlive_current(r, m)) return 0;
    memcpy(f->vram, r->picture, sizeof r->picture);
    return 1;
}

int drawlive_present_interp(drawlive *r, const machine_t *m, present_frame *f)
{
    const drawlive_step *a = &r->step[r->last ^ 1], *b = &r->step[r->last];
    if (!r->pair || f->text || !drawlive_current(r, m) || r->picture_seq != b->seq || b->end <= a->end)
        return drawlive_present(r, m, f);
    const double t = (double)(m->cpu.icount - b->end) / (double)(b->end - a->end);
    if (t <= 0) { memcpy(f->vram, a->picture, 64000); return 1; }
    if (t >= 1) { memcpy(f->vram, b->picture, 64000); return 1; }
    int sb;
    interp_inbetween(a->parsed, b->parsed, r->pair, t, &r->ilist, &sb, NULL);
    const uint8_t *p = draw_list(r, sb ? b : a);
    if (!p) return drawlive_present(r, m, f);
    memcpy(f->vram, p, 64000);
    r->inbetweens++;
    return 1;
}
