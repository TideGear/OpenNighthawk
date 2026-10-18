/* ROM-free integration tests for the registered library-line/span hooks.
 * Capture their real records without loading proprietary game bytes. */
#include "observe.h"
#include "recomp_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static recomp_override hooks[192];
static int nhooks, failures, collecting = 1, nrecords;
static machine_t m, saved_machine;
static uint8_t memory[MEM_SIZE], saved_memory[MEM_SIZE], before_pixels[64000];
static struct { char kind; int n; int32_t v[8004]; } records[4];
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d: %s\n",__LINE__,#c); failures++; } } while (0)

/* This target compiles observe.c itself; registration hands its hooks to
 * this fixture, rather than a DOS-loaded module and the real dispatcher. */
int recomp_override_add(const recomp_override *o)
{
    if (nhooks >= (int)(sizeof hooks / sizeof hooks[0])) abort();
    hooks[nhooks] = *o;
    return nhooks++;
}

static recomp_override_fn hook(uint16_t ip)
{
    for (int k = 0; k < nhooks; k++)
        if (hooks[k].seg == 0x1E42 && hooks[k].ip == ip) return hooks[k].fn;
    abort();
}

static void capture(void *u, uint64_t clock, char kind, const int32_t *v, int n)
{
    (void)u;
    if (!collecting) return;
    CHECK(clock == m.cpu.icount);
    CHECK(nrecords < 4 && n <= 8004);
    if (nrecords >= 4 || n > 8004) return;
    records[nrecords].kind = kind; records[nrecords].n = n;
    memcpy(records[nrecords++].v, v, (size_t)n * sizeof *v);
}

static void word(uint16_t seg, uint16_t off, uint16_t v)
{
    memory[phys(seg,off)] = (uint8_t)v;
    memory[phys(seg,(uint16_t)(off+1))] = (uint8_t)(v>>8);
}

static void setup(uint16_t page, uint16_t origin, int profile)
{
    static const f117_observer obs = { .prim = capture };
    memset(&m,0,sizeof m);
    for (unsigned i=0;i<MEM_SIZE;i++) memory[i]=(uint8_t)((i*37)^(i>>8));
    m.mem=m.cpu.mem=memory; m.cpu.seg[S_CS]=0x2000; m.cpu.seg[S_SS]=0x6000;
    m.cpu.icount=123456; m.cpu.stop_at=123500; m.cpu.charged=17;
    m.cpu.io_cross=23; m.cpu.t386=profile; m.cpu.t386_vga_lo=0xA0000;
    m.cpu.t386_vga_size=0x20000; m.cpu.t386_vga_byte=32;
    word(0x2000,0x01B8,0x5000); word(0x2000,0x01D6,0x5000);
    word(0x5000,0x0194,page); word(0x5000,0x0196,origin);
    for (unsigned y=0;y<220;y++) {
        if (y<200) word(0x5000,(uint16_t)(4+2*y),(uint16_t)(320*y));
        word(0x6000,(uint16_t)(0x200+2*y),10);
        word(0x6000,(uint16_t)(0x3B8+2*y),110);
    }
    observe_pending_clear(); observe_set(&obs); nrecords=0;
}

static void invoke(uint16_t ip)
{
    m.cpu.ip=ip;
    saved_machine=m; memcpy(saved_memory,memory,MEM_SIZE);
    CHECK(hook(ip)(&m)==0);
    CHECK(!memcmp(&m,&saved_machine,sizeof m));
    CHECK(!memcmp(memory,saved_memory,MEM_SIZE));
}

static void exercise(uint16_t page, uint16_t origin, int profile, int span_rows, int all)
{
    setup(page,origin,profile);
    const int spans=span_rows!=0, x0=spans?0:3, y0=spans?0:2;
    const int w=spans?320:99, h=spans?(span_rows>200?200:span_rows):3;
    m.cpu.r[R_AX]=(uint16_t)(spans?0:x0);
    m.cpu.r[R_BX]=(uint16_t)(spans?0x200:y0);
    m.cpu.r[R_CX]=(uint16_t)(spans?span_rows-1:x0+w-1);
    m.cpu.r[R_DX]=(uint16_t)(spans?2:y0+h-1);
    for (int y=0;y<h;y++) for (int x=0;x<w;x++)
        before_pixels[y*w+x]=memory[phys(page,(uint16_t)(origin+320*(y+y0)+x+x0))];
    invoke(spans?0x01D3:0x01B5);
    CHECK(nrecords==1 && records[0].kind==(spans?'Q':'N'));
    if (spans) {
        CHECK(records[0].n==6+2*span_rows && records[0].v[3]==span_rows);
        CHECK(records[0].v[4+2*span_rows]==page && records[0].v[5+2*span_rows]==origin);
    }
    for (int y=0;y<h;y++) for (int x=0;x<w;x++)
        if (all>=0 && (all || x==0 || x==31 || x==63 || x==64 || x==w-1))
            memory[phys(page,(uint16_t)(origin+320*(y+y0)+x+x0))]^=0xFF;
    m.cpu.r[R_AX]=0x7F00;
    invoke(0x01BA);
    CHECK(nrecords==3 && records[1].kind=='n' && records[2].kind=='K');
    CHECK(records[2].n==1 && records[2].v[0]==0x7F);
    CHECK(records[1].v[0]==page && records[1].v[1]==0);
    int count=0;
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        const uint16_t at=(uint16_t)(origin+320*(y+y0)+x+x0);
        const uint8_t now=memory[phys(page,at)], old=before_pixels[y*w+x];
        if (now==old || count>=2000) continue;
        const int k=3+4*count++;
        CHECK(records[1].v[k]==at);
        CHECK(records[1].v[k+1]==((x+x0)|((y+y0)<<16)));
        CHECK(records[1].v[k+2]==now && records[1].v[k+3]==old);
    }
    CHECK(records[1].v[2]==count && records[1].n==3+4*count);
}

int main(int argc,char **argv)
{
    observe_register();
    if (argc==2 && !strcmp(argv[1],"--bench")) {
        collecting=0; setup(0xA000,0,1); collecting=0;
        m.cpu.r[R_AX]=0; m.cpu.r[R_BX]=0x200; m.cpu.r[R_CX]=199;
        const clock_t began=clock();
        for (int k=0;k<1000;k++) { m.cpu.ip=0x01D3; hook(0x01D3)(&m); hook(0x01BA)(&m); }
        printf("span snapshot/unchanged flush: 1000 pairs, %.6f seconds\n",(double)(clock()-began)/CLOCKS_PER_SEC);
        return 0;
    }
    for (int profile=0;profile<2;profile++) {
        exercise(0xA000,0,profile,0,0);
        exercise(0x4000,0xFFF8,profile,0,0);
        exercise(0xFFFF,8,profile,200,0);
        exercise(0xFFFF,0xFFF8,profile,200,1);
        exercise(0xA000,0,profile,200,-1);
        exercise(0xA000,0,profile,220,0);
    }
    if (failures) return 1;
    puts("observer rows: ordered deltas, truncation, segment/physical wrap and both timing profiles pass");
    return 0;
}
