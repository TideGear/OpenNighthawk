/* Staged Q13b regression: check_q13b path/to/START.EXE (generated build).
 * Runs the written orders' landing-base name choice (0x22E5) in both engines
 * with Q13b off and on. The original instructions run; the text routine
 * (0x959E) is a returning test stub that records the name pointer it is
 * given. The departure base (E062h) and landing base (E074h) records have
 * their first words set or zero in all four combinations; name pointers are
 * 3000h plus the index they were read at. */
#include "fixes.h"
#include "matched.h"
#include "reloc_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static int failures, calls;
static unsigned got_name;
#undef CHECK
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); failures++; } } while(0)
enum { STACK=0xF000, FRAME=0xF100, DS_SEG=0x5000, DEPART=1, LAND=2, BYTE14=9 };
static int printer(machine_t *mm)
{
    cpu_t *c=&mm->cpu;
    calls++;
    got_name=seg_read16(c,c->seg[S_SS],(uint16_t)(c->r[R_SP]+6));
    c->ip=cpu_pop16(c); c->icount++;
    return 1;
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
static void put(uint16_t at,uint16_t v) { seg_write16(&m.cpu,DS_SEG,at,v); }
static void prepare(const rc_module *mod,const uint8_t *file,size_t len,unsigned depart_word,unsigned land_word,int fixed,int engine,unsigned budget)
{
    memset(m.mem,0,MEM_SIZE); cpu_init(&m.cpu,m.mem); m.cpu.user=&m;
    m.engine=engine; m.ips=MACHINE_DEFAULT_IPS;
    m.cpu.seg[S_CS]=0x1000; m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=DS_SEG;
    m.cpu.r[R_SP]=STACK; m.cpu.r[R_BP]=FRAME; m.cpu.r[R_SI]=0xABCD;
    m.cpu.ip=0x22E5; m.cpu.icount=1000; m.cpu.stop_at=1000+budget;
    fixes_enable("all",0); fixes_enable("Q13b",fixed);
    recomp_override_enable("Q13BTEST",1);
    recomp_module_load(NULL,&m,"START.EXE",file,len,MODLOAD_EXEC,0x1000,0x1000);
    CHECK(test_load_generated_image(m.mem,MEM_SIZE,mod,0x1000));
    put(0xE062,DEPART); put(0xE074,LAND);
    put((uint16_t)(0xCBDE+16*DEPART),(uint16_t)depart_word);
    put((uint16_t)(0xCBDE+16*LAND),(uint16_t)land_word);
    m.mem[((uint32_t)DS_SEG<<4)+0xCBDE+16*LAND+0x0E]=BYTE14;
    for(unsigned k=0;k<16;k++) put((uint16_t)(0xDC70+2*k),(uint16_t)(0x3000+k));
    m.trap_on=1; m.trap_cs=0x1000; m.trap_ip=0x233B; m.trap_sp=(uint16_t)(STACK-6);
    calls=0; got_name=0;
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
    if(argc!=2) { fprintf(stderr,"usage: check_q13b path/to/START.EXE\n"); return 2; }
    FILE *f=fopen(argv[1],"rb"); if(!f) return 2;
    fseek(f,0,SEEK_END); long len=ftell(f); rewind(f); if(len<=0) return 2;
    uint8_t *file=malloc((size_t)len),*saved=malloc(MEM_SIZE);
    m.mem=calloc(1,MEM_SIZE); if(!file||!saved||!m.mem) return 2;
    if(fread(file,1,(size_t)len,f)!=(size_t)len) return 2; fclose(f);
    const rc_module *mod=NULL;
    for(unsigned k=0;k<RC_NMODULES;k++) if(!strcmp(RC_MODULES[k]->name,"START.EXE")) mod=RC_MODULES[k];
    if(!mod) return 2;
    cpu_init(&m.cpu,m.mem); recomp_init(&m); matched_runner=run;
    const recomp_override stub={"Q13BTEST","START.EXE",START_47304,0,0x959E,printer,"observed test printer"};
    CHECK(recomp_override_add(&stub)>=0);
    /* The name is read from the landing record's first word when the tested record's first word is set,
     * else by its byte at +0Eh (BYTE14). Tested record: the departure base's (original), the landing base's (fixed). */
    const struct { unsigned depart, land; } cases[]={{7,5},{7,0},{0,5},{0,0}};
    unsigned checks=0;
    for(unsigned k=0;k<4;k++) for(int fixed=0;fixed<2;fixed++) {
        cpu_t reference={0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,cases[k].depart,cases[k].land,fixed,engine,500);
            CHECK(run(&m)==RUN_TRAP);
            CHECK(calls==1);
            const unsigned tested=fixed?cases[k].land:cases[k].depart;
            const unsigned index=tested?cases[k].land:BYTE14;
            CHECK(got_name==0x3000+index);
            if(engine==ENGINE_INTERP) { reference=m.cpu; memcpy(saved,m.mem,MEM_SIZE); }
            else equal(&reference,saved);
            checks++;
        }
    }
    /* The defect case is where the two states differ; the others agree. */
    for(unsigned k=0;k<4;k++) {
        unsigned seen[2];
        for(int fixed=0;fixed<2;fixed++) { prepare(mod,file,(size_t)len,cases[k].depart,cases[k].land,fixed,ENGINE_INTERP,500); run(&m); seen[fixed]=got_name; }
        CHECK((seen[0]!=seen[1])==(k==1||k==2));
    }
    /* A stop at every instruction count, then resume. */
    for(int fixed=0;fixed<2;fixed++) for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
        prepare(mod,file,(size_t)len,7,0,fixed,engine,500);
        CHECK(run(&m)==RUN_TRAP);
        const uint64_t total=m.cpu.icount-1000; cpu_t whole=m.cpu; memcpy(saved,m.mem,MEM_SIZE);
        for(unsigned limit=1;limit<total;limit++) {
            prepare(mod,file,(size_t)len,7,0,fixed,engine,limit); run(&m);
            m.cpu.stop_at=1000+500;
            CHECK(run(&m)==RUN_TRAP);
            equal(&whole,saved);
            checks++;
        }
    }
    recomp_shutdown(&m); free(m.mem); free(file); free(saved);
    printf("Q13b: %u staged runs; both states/engines; four record combinations; stop and resume; %d failures\n",checks,failures);
    return failures?1:0;
}
