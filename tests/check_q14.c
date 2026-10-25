/* Staged Q14 regression: check_q14 path/to/VGAME.EXE (generated build).
 * Starts at the objectives panel's overdue branch (0x9699) with the original
 * string copy executing in both engines. The local deadline line [bp-18h]
 * already holds "Before ..." and the shared buffer 0x98A6 the same text; the
 * original copies "Time expired" into the buffer, the fix into the line. */
#include "fixes.h"
#include "matched.h"
#include "reloc_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static int failures;
#undef CHECK
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); failures++; } } while(0)
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
static void put8(uint16_t seg,uint16_t at,uint8_t v) { m.mem[((uint32_t)seg<<4)+at]=v; }
static void text(uint16_t at,const char *t) { do put8(0x5000,at++,(uint8_t)*t); while(*t++); }
static int is(uint16_t at,const char *t) { return !strcmp((const char *)m.mem+((uint32_t)0x5000<<4)+at,t); }
enum { STACK=0xF000, FRAME=0xF100, LINE_OFF=0x18 };
static void prepare(const rc_module *mod,const uint8_t *file,size_t len,int fixed,int engine,unsigned budget)
{
    memset(m.mem,0,MEM_SIZE); cpu_init(&m.cpu,m.mem); m.cpu.user=&m;
    m.engine=engine; m.ips=MACHINE_DEFAULT_IPS;
    m.cpu.seg[S_CS]=0x1000; m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=0x5000;
    m.cpu.r[R_SP]=STACK; m.cpu.r[R_BP]=FRAME; m.cpu.r[R_SI]=0xABCD; m.cpu.r[R_DI]=0x4321;
    m.cpu.ip=0x9699; m.cpu.icount=1000; m.cpu.stop_at=1000+budget;
    fixes_enable("all",0); fixes_enable("Q14",fixed);
    recomp_module_load(NULL,&m,"VGAME.EXE",file,len,MODLOAD_EXEC,0x1000,0x1000);
    CHECK(test_load_generated_image(m.mem,MEM_SIZE,mod,0x1000));
    text(0x4205,"Time expired");
    text(0x98A6,"Before 12:00"); text(FRAME-LINE_OFF,"Before 12:00");
    m.trap_on=1; m.trap_cs=0x1000; m.trap_ip=0x96A2; m.trap_sp=STACK-4;
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
    if(argc!=2) { fprintf(stderr,"usage: check_q14 path/to/VGAME.EXE\n"); return 2; }
    FILE *f=fopen(argv[1],"rb"); if(!f) return 2;
    fseek(f,0,SEEK_END); long len=ftell(f); rewind(f); if(len<=0) return 2;
    uint8_t *file=malloc((size_t)len),*saved=malloc(MEM_SIZE);
    m.mem=calloc(1,MEM_SIZE); if(!file||!saved||!m.mem) return 2;
    if(fread(file,1,(size_t)len,f)!=(size_t)len) return 2; fclose(f);
    const rc_module *mod=NULL;
    for(unsigned k=0;k<RC_NMODULES;k++) if(!strcmp(RC_MODULES[k]->name,"VGAME.EXE")) mod=RC_MODULES[k];
    if(!mod) return 2;
    cpu_init(&m.cpu,m.mem); recomp_init(&m); matched_runner=run;
    unsigned checks=0;
    uint64_t clock[2]={0,0};
    for(int fixed=0;fixed<2;fixed++) {
        cpu_t reference={0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,fixed,engine,500);
            CHECK(run(&m)==RUN_TRAP);
            CHECK(is(FRAME-LINE_OFF,fixed?"Time expired":"Before 12:00"));
            CHECK(is(0x98A6,fixed?"Before 12:00":"Time expired"));
            CHECK(m.cpu.r[R_SP]==STACK-4 && m.cpu.r[R_BP]==FRAME && m.cpu.r[R_SI]==0xABCD && m.cpu.r[R_DI]==0x4321);
            if(engine==ENGINE_INTERP) { reference=m.cpu; memcpy(saved,m.mem,MEM_SIZE); clock[fixed]=m.cpu.icount; }
            else equal(&reference,saved);
            checks++;
        }
    }
    CHECK(clock[0]==clock[1]);       /* the push is replaced, not added */
    /* A stop at every instruction count, then resume. */
    for(int fixed=0;fixed<2;fixed++) for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
        prepare(mod,file,(size_t)len,fixed,engine,500);
        CHECK(run(&m)==RUN_TRAP);
        const uint64_t total=m.cpu.icount-1000; cpu_t whole=m.cpu; memcpy(saved,m.mem,MEM_SIZE);
        for(unsigned limit=1;limit<total;limit++) {
            prepare(mod,file,(size_t)len,fixed,engine,limit);
            run(&m);
            CHECK(m.cpu.icount<=1000+limit);
            m.cpu.stop_at=1000+500;
            CHECK(run(&m)==RUN_TRAP);
            equal(&whole,saved);
            checks++;
        }
    }
    recomp_shutdown(&m); free(m.mem); free(file); free(saved);
    printf("Q14: %u staged runs; both states/engines; text lands in the displayed line; stop and resume at every instruction; %d failures\n",checks,failures);
    return failures?1:0;
}
