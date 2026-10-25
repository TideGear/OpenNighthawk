/* Staged Q11 regression: check_q11 path/to/VGAME.EXE (generated build).
 * The original model-placement routine and abs/shift helpers execute. Only
 * descriptor lookup and the two drawing callees are returning test stubs;
 * draw calls are observed, so an admitted wrapped object is measurable. */
#include "fixes.h"
#include "matched.h"
#include "reloc_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static int failures, draws;
#undef CHECK
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); failures++; } } while(0)
static int descriptor(machine_t *mm) { mm->cpu.r[R_AX]=0x1234; mm->cpu.ip=cpu_pop16(&mm->cpu); mm->cpu.icount++; return 1; }
static int origin(machine_t *mm) { mm->cpu.ip=cpu_pop16(&mm->cpu); mm->cpu.icount++; return 1; }
static int draw(machine_t *mm) { draws++; return origin(mm); }
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
static void put(uint16_t at,uint16_t v) { seg_write16(&m.cpu,0x5000,at,v); }
typedef struct { uint32_t x,y; int scale, original, fixed; } sample;
static void prepare(const rc_module *mod,const uint8_t *file,size_t len,const sample *s,int fixed,int engine,unsigned budget)
{
    memset(m.mem,0,MEM_SIZE); cpu_init(&m.cpu,m.mem); m.cpu.user=&m;
    m.engine=engine; m.ips=MACHINE_DEFAULT_IPS;
    m.cpu.seg[S_CS]=0x1000; m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=0x5000;
    m.cpu.r[R_SP]=0xF000; m.cpu.r[R_BP]=0xBEEF; m.cpu.r[R_SI]=0xABCD;
    m.cpu.ip=0xBB0D; m.cpu.icount=1000; m.cpu.stop_at=1000+budget;
    fixes_enable("all",0); fixes_enable("Q11",fixed);
    recomp_override_enable("Q11TEST",1);
    recomp_module_load(NULL,&m,"VGAME.EXE",file,len,MODLOAD_EXEC,0x1000,0x1000);
    CHECK(test_load_generated_image(m.mem,MEM_SIZE,mod,0x1000));
    put(0xC058,0x100); /* cancel the routine's fixed y-origin subtraction */
    put(0xF000,0xFF00); put(0xF002,1);
    put(0xF004,(uint16_t)s->x); put(0xF006,(uint16_t)(s->x>>16));
    put(0xF008,(uint16_t)s->y); put(0xF00A,(uint16_t)(s->y>>16));
    put(0xF00C,100); put(0xF014,(uint16_t)s->scale);
    m.trap_on=1; m.trap_cs=0x1000; m.trap_ip=0xFF00; m.trap_sp=0xF002;
    draws=0;
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
    if(argc!=2) { fprintf(stderr,"usage: check_q11 path/to/VGAME.EXE\n"); return 2; }
    FILE *f=fopen(argv[1],"rb"); if(!f) return 2;
    fseek(f,0,SEEK_END); long len=ftell(f); rewind(f); if(len<=0) return 2;
    uint8_t *file=malloc((size_t)len),*saved=malloc(MEM_SIZE);
    m.mem=calloc(1,MEM_SIZE); if(!file||!saved||!m.mem) return 2;
    if(fread(file,1,(size_t)len,f)!=(size_t)len) return 2; fclose(f);
    const rc_module *mod=NULL;
    for(unsigned k=0;k<RC_NMODULES;k++) if(!strcmp(RC_MODULES[k]->name,"VGAME.EXE")) mod=RC_MODULES[k];
    if(!mod) return 2;
    cpu_init(&m.cpu,m.mem); recomp_init(&m); matched_runner=run;
    const recomp_override stubs[]={
        {"Q11TEST","VGAME.EXE",VGAME_47304,0,0xC436,descriptor,"test descriptor"},
        {"Q11TEST","VGAME.EXE",VGAME_47304,0,0xD9E7,origin,"test drawing origin"},
        {"Q11TEST","VGAME.EXE",VGAME_47304,0,0xE2B6,draw,"observed test draw"}};
    for(unsigned k=0;k<sizeof stubs/sizeof stubs[0];k++) CHECK(recomp_override_add(&stubs[k])>=0);
    const sample cases[]={
        {0,0,3,1,1},{1,0xFFFFFFFFu,3,1,1},
        {32766,0,3,1,1},{0xFFFF8002u,0,3,1,1},
        {32767,0,3,0,0},{0xFFFF8001u,0,3,0,0},
        {32768,0,3,1,0},{0xFFFF8000u,0,3,1,0},
        {65536,0,3,1,0},{0xFFFF0000u,0,3,1,0},
        {0x7FFFFFFFu,0,3,1,0},{0x80000000u,0,3,1,0},
        {0x17FFFu,0,3,0,0},
        {0,32766,3,1,1},{0,0xFFFF8002u,3,1,1},
        {0,32767,3,0,0},{0,0xFFFF8001u,3,0,0},
        {0,32768,3,1,0},{0,0xFFFF8000u,3,1,0},
        {0,65536,3,1,0},{0,0xFFFF0000u,3,1,0},
        {0,0x7FFFFFFFu,3,1,0},{0,0x80000000u,3,1,0},
        {20000,0,4,1,0},{0,20000,4,1,0},
        {65536,0,2,1,0},{0,65536,1,1,1}};
    unsigned checks=0;
    for(unsigned k=0;k<sizeof cases/sizeof cases[0];k++) for(int fixed=0;fixed<2;fixed++) {
        cpu_t reference={0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,&cases[k],fixed,engine,2000);
            CHECK(run(&m)==RUN_TRAP);
            CHECK(draws==(fixed?cases[k].fixed:cases[k].original));
            CHECK(seg_read16(&m.cpu,0x5000,0xE328)==draws);
            CHECK(m.cpu.r[R_SI]==0xABCD && m.cpu.r[R_BP]==0xBEEF);
            if(engine==ENGINE_INTERP) { reference=m.cpu; memcpy(saved,m.mem,MEM_SIZE); }
            else equal(&reference,saved);
            checks++;
        }
    }
    /* Both branches: changing the decision preserves all registers and flags.
     * When the original already rejects, the override declines untouched. */
    const uint32_t branch_values[]={65536,32768,0xFFFF8000u,32767};
    for(int axis=0;axis<2;axis++) for(unsigned j=0;j<4;j++) {
        const int same=j==3;
        sample s={0,0,3,0,0}; if(axis) s.y=branch_values[j]; else s.x=branch_values[j];
        prepare(mod,file,(size_t)len,&s,1,ENGINE_INTERP,2000);
        m.trap_ip=axis?0xBC1E:0xBC0E; m.trap_sp=0xEFEC;
        CHECK(run(&m)==RUN_TRAP); cpu_t before=m.cpu; memcpy(saved,m.mem,MEM_SIZE);
        CHECK(recomp_override_step(&m)==(same?-1:1));
        CHECK(!memcmp(before.r,m.cpu.r,sizeof before.r)); CHECK(before.flags==m.cpu.flags);
        CHECK(!memcmp(saved,m.mem,MEM_SIZE)); CHECK(m.cpu.icount==before.icount+(same?0:1));
        CHECK(m.cpu.ip==(same?before.ip:0xBC5C));
    }
    for(int axis=0;axis<2;axis++) for(unsigned limit=1;limit<180;limit++) {
        sample s={0,0,3,1,0}; if(axis) s.y=65536; else s.x=65536;
        cpu_t before={0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,&s,1,engine,limit); run(&m);
            CHECK(m.cpu.icount<=1000+limit);
            if(engine==ENGINE_INTERP) { before=m.cpu; memcpy(saved,m.mem,MEM_SIZE); }
            else equal(&before,saved);
        }
    }
    recomp_shutdown(&m); free(m.mem); free(file); free(saved);
    printf("Q11: %u staged runs; both states/engines; eight branch-preservation checks; 358 paired event limits; %d failures\n",checks,failures);
    return failures?1:0;
}
