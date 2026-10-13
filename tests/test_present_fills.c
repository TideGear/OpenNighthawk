/* Staged presentation fixtures against the original VGAME fill selector.
 * The executable bytes come only from the owner's generated build. This
 * checks drawing rules, not reachability during normal play. */
#include "drawlist.h"
#include "recomp_gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DATA = 0x4000, PAGE = 0x6000, CODE = 0x1377, RETURN_IP = 0xFFF0 };
static uint8_t mem[MEM_SIZE], before[65536];
static drawlist replay;
static unsigned cases;
static FILE *rows_out;

static void word(unsigned off, uint16_t value)
{
    mem[16 * DATA + off] = (uint8_t)value;
    mem[16 * DATA + off + 1] = (uint8_t)(value >> 8);
}

static int check(const rc_module *mod, unsigned colour, int y, int left,
                 int width, int origin, int scale)
{
    cpu_t c;
    memset(mem, 0, sizeof mem);
    memcpy(mem + mod->origin, mod->image, mod->size);
    for (unsigned i = 0; i < sizeof before; i++) before[i] = (uint8_t)(i * 37 + (i >> 8));
    memcpy(mem + 16 * PAGE, before, sizeof before);
    cpu_init(&c, mem);
    c.model = CPU_80286;
    c.seg[S_CS] = CODE; c.seg[S_DS] = DATA; c.seg[S_ES] = PAGE; c.seg[S_SS] = 0x5000;
    c.r[R_SP] = 0xFF00; c.flags = 2; c.ip = 0x0B09;
    mem[0x5FF00] = RETURN_IP & 0xFF; mem[0x5FF01] = RETURN_IP >> 8;
    word(0x8606, (uint16_t)colour);
    word(0x9160, (uint16_t)y);
    word(0x85FA, 3); word(0x85FE, 316);
    word(0x861C + 2 * y, (uint16_t)(origin + 320 * y));
    word(0x89DC + 2 * y, (uint16_t)left);
    word(0x8D9E + 2 * y, (uint16_t)(left + width - 1));
    word(0x89DC + 2 * (y + 1), 0x7FFF);
    word(0x8D9E + 2 * (y + 1), 0x8001);
    unsigned steps = 0;
    while (c.ip != RETURN_IP && steps++ < 10000) cpu_step(&c);
    if (c.ip != RETURN_IP || c.seg[S_CS] != CODE) {
        fprintf(stderr, "fill did not return: %04X:%04X\n", c.seg[S_CS], c.ip);
        return 0;
    }
    drawlist_reset(&replay);
    drawlist_set_scale(&replay, scale);
    drawlist_seed(&replay, 'Z', PAGE, (uint16_t)origin, before, sizeof before);
    const int32_t fill[] = { colour, y, 1, left, left + width - 1, colour, PAGE };
    drawlist_record(&replay, 'R', fill, 7);
    int x0 = left < 3 ? 3 : left, x1 = left + width - 1;
    if (x1 > 316) x1 = 316;
    if (x0 <= x1) {
        const int32_t row[] = { y, x0, x1 - x0 + 1, PAGE, (origin + 320 * y + x0) & 0xFFFF };
        drawlist_record(&replay, 'b', row, 5);
        /* Deliberately false post-fill bytes must never overwrite the rule. */
        int32_t after[5 + 320] = { y, x0, x1 - x0 + 1, PAGE, row[4] };
        for (int i = 0; i < x1 - x0 + 1; i++) after[5 + i] = 0xA5;
        drawlist_record(&replay, 'a', after, 5 + x1 - x0 + 1);
    }
    const drawlist_page *p = drawlist_get(&replay, PAGE);
    if (!p || memcmp(p->b, mem + 16 * PAGE, sizeof before) || replay.ncopied) {
        unsigned at = 0;
        if (p) while (at < sizeof before && p->b[at] == mem[16 * PAGE + at]) at++;
        fprintf(stderr, "fill %04X row %d left %d width %d origin %d scale %d differs at %u\n",
                colour, y, left, width, origin, scale, at);
        return 0;
    }
    if (scale) for (int py = 0; py < 200; py++) for (int px = 0; px < 320; px++)
        for (int sy = 0; sy < scale; sy++) for (int sx = 0; sx < scale; sx++)
            if (p->hi[(py * scale + sy) * 320 * scale + px * scale + sx] != p->b[py * 320 + px]) return 0;
    if (rows_out && x0 <= x1) {
        const unsigned at=(unsigned)(origin+320*y+x0)&0xFFFF;
        fprintf(rows_out,"%u %d %d %d",colour,y,x0,x1-x0+1);
        for(int i=0;i<x1-x0+1;i++)fprintf(rows_out," %u",before[(at+i)&0xFFFF]);
        for(int i=0;i<x1-x0+1;i++)fprintf(rows_out," %u",mem[16*PAGE+((at+i)&0xFFFF)]);
        fputc('\n',rows_out);
    }
    cases++;
    return 1;
}

int main(int argc,char **argv)
{
    if(argc==3 && !strcmp(argv[1],"--rows")) {
        rows_out=fopen(argv[2],"w");
        if(!rows_out){perror(argv[2]);return 2;}
    } else if(argc!=1) {fputs("usage: test_present_fills [--rows FILE]\n",stderr);return 2;}
    const rc_module *mod = NULL;
    for (unsigned i = 0; i < RC_NMODULES; i++)
        if (!strcmp(RC_MODULES[i]->name, "VGAME.EXE")) mod = RC_MODULES[i];
    if (!mod) { fputs("VGAME translation required\n", stderr); return 2; }
    drawlist_init(&replay);
    const unsigned colours[] = { 0xFF37, 0xFE53, 0xFD81, 0xFB29, 0xFC3B, 0x1937, 0x3737, 0x0037, 0xFA37 };
    const int widths[] = { 1, 2, 3, 8, 17, 320 };
    for (unsigned col = 0; col < sizeof colours / sizeof *colours; col++)
        for (int y = 0; y < 4; y++) for (int x = 0; x < 6; x++)
            for (unsigned w = 0; w < sizeof widths / sizeof *widths; w++)
                for (int origin = 0; origin < 2; origin++)
                    if (!check(mod, colours[col], y, x, widths[w], origin, 0)) return 1;
    for (unsigned col = 0; col < sizeof colours / sizeof *colours; col++)
        for (int scale = 1; scale <= 9; scale += scale == 1 ? 3 : 5)
            if (!check(mod, colours[col], 3, 1, 320, 1, scale)) return 1;
    drawlist_free(&replay);
    if(rows_out && fclose(rows_out))return 1;
    printf("presentation fills: %u staged cases equal to original bytes; no copied output\n", cases);
    return 0;
}
