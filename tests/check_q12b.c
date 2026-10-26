/* Q12b marker-call arguments, original marker-size branch and event boundaries.
 * Only coordinate conversion and individual pixel drawing use observer stubs. */
#include "fixes.h"
#include "matched.h"
#include "reloc_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static int failures, copies;
static unsigned got[4];
static int real_draw;
#undef CHECK
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); failures++; } } while(0)
enum { STACK=0xF000, FRAME=0xF100, DS_SEG=0x5000 };
static int marker_stub(machine_t *mm)
{
    cpu_t *c=&mm->cpu; copies++;
    for(unsigned k=0;k<4;k++) got[k]=seg_read16(c,c->seg[S_SS],(uint16_t)(c->r[R_SP]+2+2*k));
    c->ip=cpu_pop16(c); c->icount++; return 1;
}
static int coordinate_stub(machine_t *mm)
{
    cpu_t *c=&mm->cpu;
    c->r[R_AX]=seg_read16(c,c->seg[S_SS],(uint16_t)(c->r[R_SP]+2));
    c->ip=cpu_pop16(c);c->icount++;return 1;
}
static int pixel_stub(machine_t *mm)
{
    cpu_t *c=&mm->cpu;copies++;
    for(unsigned k=0;k<3;k++)got[k]=seg_read16(c,c->seg[S_SS],(uint16_t)(c->r[R_SP]+2+2*k));
    c->ip=cpu_pop16(c);c->icount++;return 1;
}
static int run(machine_t *mm)
{
    cpu_t *c=&mm->cpu;
    while(c->icount<c->stop_at) {
        if(mm->trap_on && c->seg[S_CS]==mm->trap_cs && c->ip==mm->trap_ip && c->r[R_SP]==mm->trap_sp) return RUN_TRAP;
        if(recomp_override_step(mm)>0) continue;
        if(mm->engine==ENGINE_RECOMP && recomp_run(mm)) continue;
        if(cpu_step(c)) return RUN_FAULT;
    }
    return RUN_SLICE;
}
static int finish(void)
{
    for(unsigned k=0;k<500;k++) {
        if(m.cpu.ip==m.trap_ip && m.cpu.r[R_SP]==m.trap_sp) return RUN_TRAP;
        m.cpu.stop_at=m.cpu.icount+1;
        if(run(&m)==RUN_FAULT) return RUN_FAULT;
    }
    return RUN_SLICE;
}
static void prepare(const rc_module *mod,const uint8_t *file,size_t len,unsigned stale,int fixed,int engine,unsigned budget)
{
    memset(m.mem,0,MEM_SIZE); cpu_init(&m.cpu,m.mem); m.cpu.user=&m;
    m.engine=engine; m.ips=MACHINE_DEFAULT_IPS;
    m.cpu.seg[S_CS]=0x1000; m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=DS_SEG;
    m.cpu.r[R_SP]=STACK; m.cpu.r[R_BP]=FRAME; m.cpu.r[R_SI]=0xABCD;
    m.cpu.ip=0x7071; m.cpu.icount=1000; m.cpu.stop_at=1000+budget;
    fixes_enable("all",0); fixes_enable("Q12b",fixed);
    recomp_override_enable("Q12BTEST",!real_draw);
    recomp_override_enable("Q12DRAW",real_draw);
    recomp_module_load(NULL,&m,"VGAME.EXE",file,len,MODLOAD_EXEC,0x1000,0x1000);
    CHECK(test_load_generated_image(m.mem,MEM_SIZE,mod,0x1000));
    seg_write16(&m.cpu,DS_SEG,STACK,(uint16_t)stale);
    seg_write16(&m.cpu,DS_SEG,0x3C3A,123); seg_write16(&m.cpu,DS_SEG,0x3C3C,134);
    m.trap_on=1; m.trap_cs=0x1000; m.trap_ip=0x7081; m.trap_sp=STACK;
    seg_write16(&m.cpu,DS_SEG,0x368C,1);
    seg_write16(&m.cpu,DS_SEG,0xE32E,320);seg_write16(&m.cpu,DS_SEG,0xE470,200);
    copies=0; memset(got,0,sizeof got);
}
static void equal(const cpu_t *c,const uint8_t *memory)
{
    CHECK(!memcmp(c->r,m.cpu.r,sizeof c->r));
    CHECK(!memcmp(c->seg,m.cpu.seg,sizeof c->seg));
    CHECK(c->flags==m.cpu.flags && c->ip==m.cpu.ip && c->icount==m.cpu.icount);
    CHECK(!memcmp(memory,m.mem,MEM_SIZE));
}
int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    FILE *f=fopen(argv[1],"rb"); if(!f) return 2;
    fseek(f,0,SEEK_END); long len=ftell(f); rewind(f); if(len<=0) return 2;
    uint8_t *file=malloc((size_t)len),*saved=malloc(MEM_SIZE);
    m.mem=calloc(1,MEM_SIZE); if(!file||!saved||!m.mem) return 2;
    if(fread(file,1,(size_t)len,f)!=(size_t)len) return 2; fclose(f);
    const rc_module *mod=NULL;
    for(unsigned k=0;k<RC_NMODULES;k++) if(!strcmp(RC_MODULES[k]->name,"VGAME.EXE")) mod=RC_MODULES[k];
    if(!mod) return 2;
    cpu_init(&m.cpu,m.mem); recomp_init(&m); matched_runner=run;
    const recomp_override stub={"Q12BTEST","VGAME.EXE",VGAME_47304,0,0x8625,marker_stub,"observe marker arguments"};
    CHECK(recomp_override_add(&stub)>=0);
    const recomp_override draw[]={
        {"Q12DRAW","VGAME.EXE",VGAME_47304,0,0x85F1,coordinate_stub,"map x"},
        {"Q12DRAW","VGAME.EXE",VGAME_47304,0,0x8608,coordinate_stub,"map y"},
        {"Q12DRAW","VGAME.EXE",VGAME_47304,0,0x8880,pixel_stub,"map pixel"}};
    for(unsigned k=0;k<3;k++)CHECK(recomp_override_add(&draw[k])>=0);
    const unsigned stale[]={0,1,10,0xFFFF}; unsigned checks=0;
    for(unsigned k=0;k<4;k++) for(int fixed=0;fixed<2;fixed++) {
        cpu_t reference={0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,stale[k],fixed,engine,500);
            CHECK(finish()==RUN_TRAP && copies==1);
            CHECK(got[0]==123 && got[1]==134 && got[2]==0x11 && got[3]==(fixed?1:stale[k]));
            CHECK(m.cpu.r[R_SP]==STACK && m.cpu.r[R_BP]==FRAME && m.cpu.r[R_SI]==0xABCD);
            CHECK(seg_read16(&m.cpu,DS_SEG,STACK)==stale[k]);
            if(engine==ENGINE_INTERP) { reference=m.cpu; memcpy(saved,m.mem,MEM_SIZE); }
            else equal(&reference,saved); checks++;
        }
    }
    /* Run the original marker body; observe only coordinate conversion and
     * the pixel primitive, checking the actual one-point/four-point branch. */
    real_draw=1;
    for(unsigned k=0;k<4;k++)for(int fixed=0;fixed<2;fixed++) {
        cpu_t reference={0};
        const unsigned big=fixed||stale[k]!=0;
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,stale[k],fixed,engine,500);
            CHECK(finish()==RUN_TRAP && copies==(big?4:1));
            CHECK(got[0]==123+big && got[1]==134+big && got[2]==0x11);
            CHECK(seg_read16(&m.cpu,DS_SEG,STACK)==stale[k]);
            if(engine==ENGINE_INTERP){reference=m.cpu;memcpy(saved,m.mem,MEM_SIZE);}else equal(&reference,saved);
            checks++;
        }
    }
    real_draw=0;
    for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
        prepare(mod,file,(size_t)len,0,1,engine,500); CHECK(run(&m)==RUN_TRAP);
        const uint64_t total=m.cpu.icount-1000; cpu_t whole=m.cpu;memcpy(saved,m.mem,MEM_SIZE);
        for(unsigned limit=1;limit<total;limit++) {
            prepare(mod,file,(size_t)len,0,1,engine,limit);run(&m);CHECK(m.cpu.icount<=1000+limit);
            m.cpu.stop_at=1500;CHECK(run(&m)==RUN_TRAP);equal(&whole,saved);checks++;
        }
    }
    recomp_shutdown(&m);free(m.mem);free(file);free(saved);
    printf("Q12b: %u staged runs; both states/engines; explicit size, original coordinates/colour, saved SI and stack, every event boundary; %d failures\n",checks,failures);
    return failures?1:0;
}
