/* Consuming an open frame must equal consuming it only at its close, and
 * collecting frames long before they are published must equal publishing each
 * at once (the draw worker runs while the main thread collects). */
#include "drawfeed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
#define LIVES 3
static uint8_t memory[MEM_SIZE];
static drawfeed feeds[LIVES];
static drawlive lives[LIVES];
static drawlist oracle;
static int32_t seed[65538];
static uint64_t clock_now;

static void record(char kind, const int32_t *values, int n)
{
    for (int k = 0; k < LIVES; k++)
        feeds[k].obs.prim(feeds[k].obs.user, clock_now, kind, values, n);
    if (kind == 'Z' || kind == 'Y') {
        static uint8_t bytes[65536];
        for (int k = 2; k < n; k++) bytes[k-2] = (uint8_t)values[k];
        drawlist_seed(&oracle,kind,(uint16_t)values[0],(uint16_t)values[1],bytes,(unsigned)n-2);
    } else drawlist_record(&oracle,kind,values,n);
}

static int same_step(const drawlive_step *x, const drawlive_step *y)
{
    CHECK((x != NULL) == (y != NULL));
    if (!x) return 0;
    CHECK(x->seq == y->seq && x->end == y->end && x->ok == y->ok);
    CHECK((x->pair != NULL) == (y->pair != NULL));
    CHECK(!memcmp(x->picture,y->picture,64000));
    CHECK(!memcmp(x->picture_hi,y->picture_hi,64000u*4*4));
    for (int p = 0; p < x->start.npages; p++) {
        const drawlist_page *u = &x->start.page[p], *v = &y->start.page[p];
        CHECK(u->seg == v->seg && !memcmp(u->b,v->b,65536));
        CHECK(!memcmp(u->hi,v->hi,64000u*4*4));
    }
    return 0;
}

static int same(int i, int j)
{
    const drawlive *a = &lives[i], *b = &lives[j];
    if (!(a->frames == b->frames && a->exact == b->exact && a->inexact == b->inexact && a->unseeded == b->unseeded)) fprintf(stderr,"counters %d,%d: frames %llu/%llu exact %llu/%llu inexact %llu/%llu unseeded %llu/%llu\n",i,j,(unsigned long long)a->frames,(unsigned long long)b->frames,(unsigned long long)a->exact,(unsigned long long)b->exact,(unsigned long long)a->inexact,(unsigned long long)b->inexact,(unsigned long long)a->unseeded,(unsigned long long)b->unseeded);
    CHECK(a->frames == b->frames && a->exact == b->exact && a->inexact == b->inexact && a->unseeded == b->unseeded);
    CHECK(a->pairs == b->pairs && a->pairs_exact == b->pairs_exact && a->pairs_inexact == b->pairs_inexact);
    CHECK(a->shown == b->shown && a->picture_seq == b->picture_seq && a->picture_end == b->picture_end);
    CHECK(!memcmp(a->picture,b->picture,64000));
    if (a->picture_hi || b->picture_hi) CHECK(!memcmp(a->picture_hi,b->picture_hi,64000u*4*4));
    const drawlive_step *ao, *an, *bo, *bn;
    drawlive_steps(a,&ao,&an);
    drawlive_steps(b,&bo,&bn);
    CHECK(!same_step(ao,bo) && !same_step(an,bn));
    return 0;
}

int main(void)
{
    drawlist_init(&oracle); drawlist_set_scale(&oracle,4);
    for (int k = 0; k < LIVES; k++) {
        drawfeed_init(&feeds[k],memory); drawlive_init(&lives[k]);
        drawlive_set_scale(&lives[k],4); lives[k].interp = lives[k].check = 1;
    }
    for (int frame = 0; frame < 60; frame++) {
        clock_now += 100000;
        for (int k = 0; k < LIVES; k++) feeds[k].obs.frame_phase(feeds[k].obs.user,clock_now);
        /* Leave enough closed frames queued to overflow the ring, while
         * the streaming consumer still holds a partially consumed start. */
        const int paused = frame >= 15 && frame < 25;
        if (!paused) {
            drawlive_update(&lives[0],&feeds[0]);
            drawlive_update(&lives[1],&feeds[1]);
            CHECK(!same(0,1));
            /* collected now, published every third frame: three steps wait at most */
            drawlive_collect(&lives[2],&feeds[2]);
            if (frame % 3 == 2) {                  /* frames that waited for a free step follow the publish */
                int more;
                do { drawlive_publish(&lives[2]); more = drawlive_collect(&lives[2],&feeds[2]); } while (more);
                drawlive_publish(&lives[2]);
                CHECK(!same(0,2));
            }
        }
        if (feeds[0].want_seed) {
            const drawlist_page *page = drawlist_get(&oracle,0x1000);
            seed[0] = 0x1000; seed[1] = 0;
            for (int i = 0; i < 65536; i++) seed[i+2] = page ? page->b[i] : 7;
            record('Z',seed,65538);
            page = drawlist_get(&oracle,0xA000);
            seed[0] = 0xA000;
            for (int i = 0; i < 64000; i++) seed[i+2] = page ? page->b[i] : 7;
            record('Y',seed,64002);
            if (!paused) {
                drawlive_update(&lives[1],&feeds[1]);
                drawlive_collect(&lives[2],&feeds[2]);
            }
        }
        const int32_t colour[] = {frame+1}, line[] = {10+frame,20,100+frame,80,0,0x1000,0};
        record('K',colour,1);
        if (!paused) { drawlive_update(&lives[1],&feeds[1]); drawlive_collect(&lives[2],&feeds[2]); }
        record('N',line,7);
        if (!paused) {
            const uint64_t before = lives[1].frames;
            drawlive_update(&lives[1],&feeds[1]);
            drawlive_collect(&lives[2],&feeds[2]);
            CHECK(lives[1].frames == before); /* partial work never closes/shows a frame */
            CHECK(!same(0,1));                /* both completed starts remain intact */
        }
        const int32_t copy[] = {44,64000,0x1000,0xA000};
        record('D',copy,4);
        memcpy(memory+0xA0000,drawlist_get(&oracle,0xA000)->b,64000);
        if (frame == 30) memory[0xA0000] ^= 1; /* identical mismatch/reseed recovery */
        if (!paused) { drawlive_update(&lives[1],&feeds[1]); drawlive_collect(&lives[2],&feeds[2]); }
    }
    drawlive_publish(&lives[2]);
    drawlive_update(&lives[0],&feeds[0]);
    CHECK(lives[0].exact > 20 && lives[0].inexact == 1 && lives[0].pairs_exact > 10);
    CHECK(feeds[0].dropped && feeds[0].dropped == feeds[1].dropped && feeds[0].dropped == feeds[2].dropped);
    CHECK(lives[2].exact == lives[0].exact && lives[2].inexact == lives[0].inexact && lives[2].pairs == lives[0].pairs);
    puts("streamed replay: closed pictures, cached starts, endpoints, dropped frames, reseeds and deferred publication agree");
    return 0;
}
