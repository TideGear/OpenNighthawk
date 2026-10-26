/* Staged Q13a regression: check_q13a path/to/VGAME.EXE (generated build).
 * Calls the detection routine (0x5582) in both engines with Q13a off and on
 * for a unit with no sensor (sensor word 0 and -1). The routine's own
 * instructions run. Off, the two output words are left alone (stale); on,
 * the bearing is 0 and the range 7FFFh. Strength is 0 either way and the
 * caller's registers come back as they went in. */
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
enum { STACK=0xF000, DS_SEG=0x5000, BEARING=0x7000, RANGE=0x7002 };
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
static void prepare(const rc_module *mod,const uint8_t *file,size_t len,uint16_t sensor,int fixed,int engine,unsigned budget)
{
    memset(m.mem,0,MEM_SIZE); cpu_init(&m.cpu,m.mem); m.cpu.user=&m;
    m.engine=engine; m.ips=MACHINE_DEFAULT_IPS;
    m.cpu.seg[S_CS]=0x1000; m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=DS_SEG;
    m.cpu.r[R_SP]=STACK; m.cpu.r[R_BP]=0xBEEF; m.cpu.r[R_SI]=0xABCD; m.cpu.r[R_DI]=0x4321;
    m.cpu.ip=0x5582; m.cpu.icount=1000; m.cpu.stop_at=1000+budget;
    fixes_enable("all",0); fixes_enable("Q13a",fixed);
    recomp_module_load(NULL,&m,"VGAME.EXE",file,len,MODLOAD_EXEC,0x1000,0x1000);
    CHECK(test_load_generated_image(m.mem,MEM_SIZE,mod,0x1000));
    put(STACK,0xFF00);                   /* return address */
    put(STACK+8,sensor);                 /* [bp+0Ah] */
    put(STACK+0x0A,BEARING);             /* [bp+0Ch] pointer to the bearing */
    put(STACK+0x0C,RANGE);               /* [bp+0Eh] pointer to the range */
    put(BEARING,0xAAAA); put(RANGE,0xBBBB);
    m.trap_on=1; m.trap_cs=0x1000; m.trap_ip=0xFF00; m.trap_sp=STACK+2;
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
    if(argc!=2) { fprintf(stderr,"usage: check_q13a path/to/VGAME.EXE\n"); return 2; }
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
    const uint16_t sensors[]={0,0xFFFF};
    for(unsigned k=0;k<2;k++) for(int fixed=0;fixed<2;fixed++) {
        cpu_t reference={0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,sensors[k],fixed,engine,500);
            CHECK(run(&m)==RUN_TRAP);
            CHECK(m.cpu.r[R_AX]==0);
            CHECK(seg_read16(&m.cpu,DS_SEG,BEARING)==(fixed?0:0xAAAA));
            CHECK(seg_read16(&m.cpu,DS_SEG,RANGE)==(fixed?0x7FFF:0xBBBB));
            CHECK(m.cpu.r[R_SI]==0xABCD && m.cpu.r[R_DI]==0x4321 && m.cpu.r[R_BP]==0xBEEF);
            if(engine==ENGINE_INTERP) { reference=m.cpu; memcpy(saved,m.mem,MEM_SIZE); }
            else equal(&reference,saved);
            checks++;
        }
    }
    /* A stop at every instruction count, then resume. */
    for(int fixed=0;fixed<2;fixed++) for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
        prepare(mod,file,(size_t)len,0,fixed,engine,500);
        CHECK(run(&m)==RUN_TRAP);
        const uint64_t total=m.cpu.icount-1000; cpu_t whole=m.cpu; memcpy(saved,m.mem,MEM_SIZE);
        for(unsigned limit=1;limit<total;limit++) {
            prepare(mod,file,(size_t)len,0,fixed,engine,limit); run(&m);
            m.cpu.stop_at=1000+500;
            CHECK(run(&m)==RUN_TRAP);
            equal(&whole,saved);
            checks++;
        }
    }
    recomp_shutdown(&m); free(m.mem); free(file); free(saved);
    printf("Q13a: %u staged runs; both states/engines; sensorless range and bearing; stop and resume at every instruction; %d failures\n",checks,failures);
    return failures?1:0;
}
