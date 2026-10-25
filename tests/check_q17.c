/* Staged Q17 regression: check_q17 path/to/VGAME.EXE (generated build).
 * Runs the original label-index load of the pitch ladder (1058:08FB, left
 * side, and 1058:095C, right side) in both engines with Q17 off and on, for
 * every index from -6 to 30. Off, DI is the index times four, wrapped to 16
 * bits, whatever the table holds; on, an index outside the 23-entry table
 * mirrors about its ends (4 - i below, 40 - i above) and in-range indices
 * are untouched. */
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
enum { CODE=0x2058 };
static void prepare(const rc_module *mod,const uint8_t *file,size_t len,int idx,int right,int fixed,int engine,unsigned budget)
{
    memset(m.mem,0,MEM_SIZE); cpu_init(&m.cpu,m.mem); m.cpu.user=&m;
    m.engine=engine; m.ips=MACHINE_DEFAULT_IPS;
    m.cpu.seg[S_CS]=CODE; m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=0x5000;
    m.cpu.r[R_SP]=0xF000; m.cpu.r[R_BP]=0xBEEF; m.cpu.r[R_SI]=0xABCD; m.cpu.r[R_DI]=0x4321;
    m.cpu.ip=right?0x095C:0x08FB; m.cpu.icount=1000; m.cpu.stop_at=1000+budget;
    fixes_enable("all",0); fixes_enable("Q17",fixed);
    recomp_module_load(NULL,&m,"VGAME.EXE",file,len,MODLOAD_EXEC,0x1000,0x1000);
    CHECK(test_load_generated_image(m.mem,MEM_SIZE,mod,0x1000));
    seg_write16(&m.cpu,0x5000,0x2C2C,(uint16_t)idx);
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
    if(argc!=2) { fprintf(stderr,"usage: check_q17 path/to/VGAME.EXE\n"); return 2; }
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
    for(int right=0;right<2;right++) for(int idx=-6;idx<=30;idx++) for(int fixed=0;fixed<2;fixed++) {
        cpu_t reference={0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,idx,right,fixed,engine,3);
            run(&m);
            const int in_range=idx>=0&&idx<=22;
            int shown=idx;
            if(fixed&&!in_range) { shown=idx<0?4-idx:40-idx; if(shown<0) shown=0; if(shown>22) shown=22; }
            CHECK(m.cpu.r[R_DI]==(uint16_t)(shown*4));
            CHECK(m.cpu.ip==(right?0x0964:0x0903) && m.cpu.icount==1003);
            CHECK(m.cpu.r[R_SI]==0xABCD && m.cpu.r[R_BP]==0xBEEF);
            if(engine==ENGINE_INTERP) { reference=m.cpu; memcpy(saved,m.mem,MEM_SIZE); }
            else equal(&reference,saved);
            checks++;
        }
    }
    /* With the fix on and a hand-back at every instruction count, the machine finishes the same. */
    for(int fixed=0;fixed<2;fixed++) for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
        prepare(mod,file,(size_t)len,-1,0,fixed,engine,20); run(&m);
        cpu_t whole=m.cpu; memcpy(saved,m.mem,MEM_SIZE);
        for(unsigned limit=1;limit<20;limit++) {
            prepare(mod,file,(size_t)len,-1,0,fixed,engine,limit); run(&m);
            m.cpu.stop_at=1000+20; run(&m);
            equal(&whole,saved);
            checks++;
        }
    }
    recomp_shutdown(&m); free(m.mem); free(file); free(saved);
    printf("Q17: %u staged runs; both states/engines, indices -6..30 on both sides; %d failures\n",checks,failures);
    return failures?1:0;
}
