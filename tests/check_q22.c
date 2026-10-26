/* Staged Q22 regression: check_q22 path/to/START.EXE (generated build).
 * Runs START's armoury shortage (0x0E23) in both engines with Q22 off and on.
 * The random-number routine (0x76C0) is a scripted stub: its first call
 * (argument 5) gives the number of weapons to refuse, the following calls
 * (argument 16) give the entries drawn. The original loop, flag tests and
 * stores run; a small model of the loop gives the flags each state must
 * leave in D878h..D88Bh and how many draws it takes. */
#include "fixes.h"
#include "matched.h"
#include "reloc_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static int failures, rnd_calls;
static unsigned wanted;
static const uint8_t *script;
static unsigned script_len;
#undef CHECK
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); failures++; } } while(0)
enum { STACK=0xF000, DS_SEG=0x5000, FLAGS=0xD878 };
static const uint8_t TABLE[16]={7,7,0xFF,9,3,3,12,0xFF,1,9,2,5,5,0xFF,4,4};
static int rnd(machine_t *mm)
{
    cpu_t *c=&mm->cpu;
    const uint16_t limit=seg_read16(c,c->seg[S_SS],(uint16_t)(c->r[R_SP]+2));
    c->r[R_AX]=limit==5?(uint16_t)wanted:script[(rnd_calls-1)%script_len];
    rnd_calls++;
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
/* The loop as the code has it; returns the number of draws, flags in out. */
static unsigned model(unsigned want,const uint8_t *draws,unsigned n,int fixed,uint8_t *out)
{
    memset(out,0,20);
    unsigned count=0,used=0;
    while(count<want) {
        if(used>5000) return 0xFFFFFFFFu;               /* the script cannot reach this many */
        const unsigned r=draws[used%n]; used++;
        const uint8_t s=TABLE[r];
        if(s==0xFF) continue;
        if(out[s]) continue;
        if(fixed && out[r]) continue;
        out[r]=1; count++;
    }
    return used;
}
static void prepare(const rc_module *mod,const uint8_t *file,size_t len,int fixed,int engine,unsigned budget)
{
    memset(m.mem,0,MEM_SIZE); cpu_init(&m.cpu,m.mem); m.cpu.user=&m;
    m.engine=engine; m.ips=MACHINE_DEFAULT_IPS;
    m.cpu.seg[S_CS]=0x1000; m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=DS_SEG;
    m.cpu.r[R_SP]=STACK; m.cpu.r[R_BP]=0xBEEF; m.cpu.r[R_SI]=0xABCD;
    m.cpu.ip=0x0E23; m.cpu.icount=1000; m.cpu.stop_at=1000+budget;
    fixes_enable("all",0); fixes_enable("Q22",fixed);
    recomp_override_enable("Q22TEST",1);
    recomp_module_load(NULL,&m,"START.EXE",file,len,MODLOAD_EXEC,0x1000,0x1000);
    CHECK(test_load_generated_image(m.mem,MEM_SIZE,mod,0x1000));
    memcpy(m.mem+((uint32_t)DS_SEG<<4)+0x1C4,TABLE,16);
    seg_write16(&m.cpu,DS_SEG,0xCACA,0x6000); seg_write16(&m.cpu,DS_SEG,0xCACC,DS_SEG);
    m.mem[((uint32_t)DS_SEG<<4)+0x6042]=0x33;
    seg_write16(&m.cpu,DS_SEG,STACK,0xFF00);
    m.trap_on=1; m.trap_cs=0x1000; m.trap_ip=0xFF00; m.trap_sp=(uint16_t)(STACK+2);
    rnd_calls=0;
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
    if(argc!=2) { fprintf(stderr,"usage: check_q22 path/to/START.EXE\n"); return 2; }
    FILE *f=fopen(argv[1],"rb"); if(!f) return 2;
    fseek(f,0,SEEK_END); long len=ftell(f); rewind(f); if(len<=0) return 2;
    uint8_t *file=malloc((size_t)len),*saved=malloc(MEM_SIZE);
    m.mem=calloc(1,MEM_SIZE); if(!file||!saved||!m.mem) return 2;
    if(fread(file,1,(size_t)len,f)!=(size_t)len) return 2; fclose(f);
    const rc_module *mod=NULL;
    for(unsigned k=0;k<RC_NMODULES;k++) if(!strcmp(RC_MODULES[k]->name,"START.EXE")) mod=RC_MODULES[k];
    if(!mod) return 2;
    cpu_init(&m.cpu,m.mem); recomp_init(&m); matched_runner=run;
    const recomp_override stub={"Q22TEST","START.EXE",START_47304,0,0x76C0,rnd,"scripted test rnd"};
    CHECK(recomp_override_add(&stub)>=0);
    /* Entries 2, 7 and 13 hold FFh (no weapon); 0 and 1 share substitute 7, 3 and 9 share 9 and so on. */
    static const uint8_t scripts[][24]={
        {0,0,1,0,3,3,4,4,5,5,6,8,9,10,11,12,14,15,0,1,3,4,5,6},        /* repeated draws of the same entry */
        {8,3,4,5,6,8,9,10,11,12,14,15,0,1,8,3,4,5,6,9,10,11,12,14},   /* mostly distinct */
        {2,7,13,0,0,0,0,1,1,1,3,3,3,4,4,5,5,6,6,8,8,9,9,10},          /* no-weapon entries between repeats */
        {15,15,15,15,14,14,14,4,4,4,11,11,12,12,10,10,9,9,8,8,6,6,5,5}};
    unsigned checks=0, differing=0;
    for(unsigned s=0;s<4;s++) for(unsigned want=0;want<=8;want++) for(int fixed=0;fixed<2;fixed++) {
        uint8_t flags[20];
        const unsigned draws=want?model(want,scripts[s],24,fixed,flags):0;
        if(!want) memset(flags,0,20);
        if(draws==0xFFFFFFFFu) continue;
        cpu_t reference={0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            wanted=want; script=scripts[s]; script_len=24;
            prepare(mod,file,(size_t)len,fixed,engine,2000000);
            CHECK(run(&m)==RUN_TRAP);
            CHECK(rnd_calls==(int)(1+draws));
            for(unsigned k=0;k<20;k++) CHECK(m.mem[((uint32_t)DS_SEG<<4)+FLAGS+k]==(k<2?0x33:flags[k]));   /* the routine ends by overwriting the first two */
            CHECK(m.mem[((uint32_t)DS_SEG<<4)+0xD879]==0x33 && m.mem[((uint32_t)DS_SEG<<4)+0xD878]==0x33);
            if(engine==ENGINE_INTERP) { reference=m.cpu; memcpy(saved,m.mem,MEM_SIZE); }
            else equal(&reference,saved);
            checks++;
        }
        if(fixed) {
            uint8_t other[20];
            if(want && (model(want,scripts[s],24,0,other)!=draws||memcmp(other+2,flags+2,18))) differing++;
        }
    }
    CHECK(differing>0);                          /* the fix is observable: some scripts end differently */
    /* A stop at every few instruction counts of one run, then resume. */
    for(int fixed=0;fixed<2;fixed++) for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
        wanted=3; script=scripts[0]; script_len=24;
        prepare(mod,file,(size_t)len,fixed,engine,2000000);
        CHECK(run(&m)==RUN_TRAP);
        const uint64_t total=m.cpu.icount-1000; cpu_t whole=m.cpu; memcpy(saved,m.mem,MEM_SIZE);
        for(unsigned limit=1;limit<total;limit+=3) {
            wanted=3; script=scripts[0]; script_len=24;
            prepare(mod,file,(size_t)len,fixed,engine,limit); run(&m);
            m.cpu.stop_at=1000+2000000;
            CHECK(run(&m)==RUN_TRAP);
            equal(&whole,saved);
            checks++;
        }
    }
    recomp_shutdown(&m); free(m.mem); free(file); free(saved);
    printf("Q22: %u staged runs; both states/engines; flags and draw counts match the model; stop and resume; %d failures\n",checks,failures);
    return failures?1:0;
}
