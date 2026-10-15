/* The optional fine HUD keeps the original replay exact, draws real fine
 * strokes, and carries them through the library's colour and copy rules. */
#include "drawlist.h"
#include "hires.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static const uint16_t SEG = 0x1000;

static void start(drawlist *d, int scale, int width)
{
    static uint8_t background[65536];
    memset(background, 0x10, sizeof background);
    drawlist_reset(d);
    drawlist_set_scale(d, scale);
    drawlist_set_line_width(d, width);
    drawlist_seed(d, 'Z', SEG, 0, background, sizeof background);
    const int32_t colour[] = { 0x1F };
    drawlist_record(d, 'K', colour, 1);
}

static void line(drawlist *d, int x0, int y0, int x1, int y1, int origin)
{
    const int32_t values[] = { x0, y0, x1, y1, 0, SEG, origin };
    drawlist_record(d, 'N', values, 7);
}

static size_t coloured(const drawlist_page *p, int col)
{
    size_t count = 0, bytes = (size_t)320 * p->n * 200 * p->n;
    for (size_t i = 0; i < bytes; i++) count += p->hi[i] == col;
    return count;
}

int main(void)
{
    static drawlist original, one, fine, reverse, copy;
    drawlist_init(&original); drawlist_init(&one); drawlist_init(&fine);
    drawlist_init(&reverse); drawlist_init(&copy);
    const int ends[][4] = {
        {10,10,30,15}, {10,10,15,30}, {30,10,10,15}, {15,10,10,30},
        {30,30,10,25}, {30,30,25,10}, {10,30,30,25}, {10,30,15,10},
        {10,10,30,10}, {10,10,10,30}, {10,10,10,10}, {0,0,319,199}
    };
    for (unsigned i = 0; i < sizeof ends / sizeof ends[0]; i++) {
        start(&original, 0, 0); start(&one, 1, 20);
        start(&fine, 4, 0); start(&reverse, 4, 0);
        const int *e = ends[i];
        line(&original, e[0],e[1],e[2],e[3],0);
        line(&one, e[0],e[1],e[2],e[3],0);
        line(&fine, e[0],e[1],e[2],e[3],0);
        line(&reverse, e[2],e[3],e[0],e[1],0);
        const drawlist_page *a = drawlist_get(&original,SEG), *b = drawlist_get(&one,SEG);
        const drawlist_page *f = drawlist_get(&fine,SEG), *r = drawlist_get(&reverse,SEG);
        CHECK(!memcmp(a->b,b->b,65536) && !memcmp(b->b,b->hi,64000));
        CHECK(!memcmp(a->b,f->b,65536));
        CHECK(!memcmp(f->hi,r->hi,1280 * 800));
    }
    start(&fine,4,0); line(&fine,10,10,20,10,0);
    const drawlist_page *f = drawlist_get(&fine,SEG);
    CHECK(coloured(f,0x1F) == 44 * 4);              /* cells 10..20 of row 10: exactly the original's footprint */
    CHECK(f->hi[40 * 1280 + 40] == 0x1F && f->hi[43 * 1280 + 83] == 0x1F);
    CHECK(f->hi[40 * 1280 + 39] == 0x10 && f->hi[40 * 1280 + 84] == 0x10);
    CHECK(f->hi[39 * 1280 + 40] == 0x10 && f->hi[44 * 1280 + 40] == 0x10);
    start(&fine,4,1); line(&fine,10,10,20,10,0);
    f = drawlist_get(&fine,SEG);
    CHECK(coloured(f,0x1F) == 41);
    start(&fine,4,0); line(&fine,10,10,10,10,0);
    CHECK(coloured(drawlist_get(&fine,SEG),0x1F) == 16);
    start(&fine,2,20); line(&fine,10,10,20,10,0);
    f = drawlist_get(&fine,SEG);
    CHECK(coloured(f,0x1F) == 40 * 20);              /* whole end caps: half a nib beyond each end centre */
    uint64_t flat_wide = 0, agree_wide = 0;
    f->hi[100 * 640 + 100] = 7;                    /* unrelated flat corruption still repaired */
    CHECK(hires_guard_masked(f->b,f->hi,2,f->strokes,&flat_wide,&agree_wide) == 1);
    CHECK(coloured(f,0x1F) == 40 * 20 && f->hi[100 * 640 + 100] == 0x10);
    start(&fine,4,1); line(&fine,0,0,10,5,0x8A29);
    f = drawlist_get(&fine,SEG);
    CHECK(f->hi[442 * 1280 + 678] == 0x1F);         /* page origin: (169,110), the cell's centre */
    CHECK(f->hi[462 * 1280 + 718] == 0x1F);

    /* two strokes sharing an end meet without a notch: the V's tip is solid on its outer side */
    start(&fine,4,8); line(&fine,10,10,20,20,0); line(&fine,20,20,30,10,0);
    f = drawlist_get(&fine,SEG);
    CHECK(f->hi[85 * 1280 + 82] == 0x1F && f->hi[82 * 1280 + 85] == 0x1F && f->hi[82 * 1280 + 79] == 0x1F);
    start(&fine,4,1); line(&fine,10,10,20,15,0);
    f = drawlist_get(&fine,SEG);
    size_t mixed = 0;
    for (int y = 0; y < 200; y++) for (int x = 0; x < 320; x++) {
        int on = 0;
        for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++)
            on += f->hi[(y * 4 + j) * 1280 + x * 4 + k] == 0x1F;
        mixed += on > 0 && on < 16;
    }
    CHECK(mixed > 10);                             /* finer than scaled original pixels */
    uint64_t flat = 0, agree = 0;
    CHECK(hires_guard(f->b, (uint8_t *)f->hi, 4, &flat, &agree) == 0);

    const size_t bytes = 1280 * 800;
    uint8_t *before = (uint8_t *)malloc(bytes);
    CHECK(before != NULL);
    memcpy(before,f->hi,bytes);
    const int32_t recolour[] = {SEG,0,0,319,199,0x1F,7,0,320};
    drawlist_record(&fine,'M',recolour,9);
    f = drawlist_get(&fine,SEG);
    for (size_t i = 0; i < bytes; i++) CHECK(f->hi[i] == (before[i] == 0x1F ? 7 : before[i]));
    memcpy(before,f->hi,bytes);
    int32_t spans[406] = {0,0,1,200};               /* OR 8 over the entire page */
    for (int y = 0; y < 200; y++) { spans[4 + 2*y] = 0; spans[5 + 2*y] = 319; }
    spans[404] = SEG; spans[405] = 0;
    const int32_t colour[] = {8};
    drawlist_record(&fine,'K',colour,1);
    drawlist_record(&fine,'Q',spans,406);
    f = drawlist_get(&fine,SEG);
    for (size_t i = 0; i < bytes; i++) CHECK(f->hi[i] == (uint8_t)(before[i] | 8));
    memcpy(before,f->hi,bytes);
    spans[2] = 2;                                 /* AND on existing fine colours */
    drawlist_record(&fine,'Q',spans,406);
    for (size_t i = 0; i < bytes; i++) CHECK(f->hi[i] == (uint8_t)(before[i] & 8));
    memcpy(before,f->hi,bytes);
    spans[2] = 3;                                 /* the driver's shade transform */
    drawlist_record(&fine,'Q',spans,406);
    for (size_t i = 0; i < bytes; i++) CHECK(f->hi[i] == (uint8_t)(((before[i] & 14) >> 1) | 0x98));
    drawlist_copy(&copy,&fine);
    CHECK(copy.line_width == 1 && !memcmp(drawlist_get(&copy,SEG)->hi,f->hi,bytes));
    memcpy(before,f->hi,bytes);
    const int32_t self_present[] = {44,64000,SEG,SEG};
    drawlist_record(&fine,'D',self_present,4);
    CHECK(!memcmp(before,f->hi,bytes));
    int32_t *logged_self = (int32_t *)calloc(64005,sizeof *logged_self);
    CHECK(logged_self != NULL);
    memcpy(logged_self,self_present,sizeof self_present);
    for (int i = 0; i < 64000; i++) logged_self[5+i] = f->b[i];
    fine.prefer_logged = 1;
    drawlist_record(&fine,'D',logged_self,64005);
    CHECK(!memcmp(before,f->hi,bytes));
    fine.prefer_logged = 0;
    free(logged_self);
    static uint8_t blank[64000];
    drawlist_seed(&fine,'Y',0xA000,0,blank,sizeof blank);
    const int32_t present[] = {44,64000,SEG,0xA000};
    drawlist_record(&fine,'D',present,4);
    CHECK(!memcmp(drawlist_get(&fine,0xA000)->hi,f->hi,bytes));
    /* Duplicate copy deltas preserve fine detail. A genuine byte change
     * in a raw frame still takes the ordinary coarse fallback. */
    start(&fine,4,1);line(&fine,10,10,20,15,0);
    drawlist_seed(&fine,'Y',0xA000,0,blank,sizeof blank);
    drawlist_record(&fine,'D',present,4);
    const drawlist_page *delta_display=drawlist_get(&fine,0xA000);
    memcpy(before,delta_display->hi,bytes);
    const int entries[3]={42,44,46};
    for(int k=0;k<3;k++) {
        int32_t delta[6]={entries[k],0xA000,1,10*320+10,delta_display->b[10*320+10],0};
        drawlist_record(&fine,'x',delta,6);
        CHECK(!memcmp(before,delta_display->hi,bytes));
    }
    const int32_t changed_delta[6]={44,0xA000,1,10*320+10,0x77,0};
    drawlist_record(&fine,'x',changed_delta,6);
    CHECK(delta_display->b[10*320+10]==0x77);
    for(int y=40;y<44;y++)for(int x=40;x<44;x++)CHECK(delta_display->hi[y*1280+x]==0x77);
    start(&fine,4,1); line(&fine,10,10,20,15,0);
    f = drawlist_get(&fine,SEG);
    drawlist_seed(&fine,'Y',0xA000,0,blank,sizeof blank);
    const int32_t rectangle[] = {42,0,0,0,100,80,32,24,SEG,0xA000};
    drawlist_record(&fine,'C',rectangle,10);
    const drawlist_page *display = drawlist_get(&fine,0xA000);
    for (int y = 0; y < 24 * 4; y++)
        CHECK(!memcmp(display->hi+(320+y)*1280+400,f->hi+y*1280,32*4));
    memcpy(before,f->hi,bytes);
    const int32_t overlap[] = {42,0,0,0,2,1,32,24,SEG,SEG};
    drawlist_record(&fine,'C',overlap,10);
    for (int y = 0; y < 24 * 4; y++)
        CHECK(!memcmp(f->hi+(4+y)*1280+8,before+y*1280,32*4));

    /* Sprites and text retain N x N pixels, including after fine HUD work. */
    start(&fine,4,1);
    int32_t sprite[23] = {73,SEG};
    sprite[6] = 50; sprite[7] = 50; sprite[8] = 3; sprite[9] = 3;
    for (int i = 0; i < 9; i++) sprite[14+i] = i + 1;
    drawlist_record(&fine,'S',sprite,23);
    int32_t text[28] = {4,SEG};
    text[4] = 5; text[6] = 60; text[7] = 60; text[13] = 2; text[14] = 65;
    text[16] = 1; text[17] = text[18] = 65; text[19] = 1; text[20] = 1;
    text[21] = 1; text[24] = 1; text[25] = 2; text[26] = 0x80;
    drawlist_record(&fine,'T',text,28);
    f = drawlist_get(&fine,SEG);
    for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) {
        CHECK(f->hi[(200+j)*1280+200+k] == 1);
        CHECK(f->hi[(204+j)*1280+204+k] == 5);
        CHECK(f->hi[(240+j)*1280+240+k] == 5);
    }
    start(&fine,4,1);
    const int32_t tick[] = {1,14,0,0,0x1F,10,13,13,SEG};
    drawlist_record(&fine,'H',tick,9);
    CHECK(coloured(drawlist_get(&fine,SEG),0x1F) == 9);
    drawlist_reset(&fine);
    CHECK(fine.line_width == 1 && fine.n == 4);
    free(before);
    drawlist_free(&original); drawlist_free(&one); drawlist_free(&fine);
    drawlist_free(&reverse); drawlist_free(&copy);
    puts("fine HUD strokes, original replay, artwork, colour operations and page copies pass");
    return 0;
}
