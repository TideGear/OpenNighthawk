/* Staged Q23 regression: check_q23 path/to/START.EXE (generated build).
 * Runs the roster fix-up loop (0x6D12 to 0x6D46) over ten 50h-byte records
 * in both engines with Q23 off and on. Realism words (+42h) above 1 appear in
 * several records. Off, only record 0's word is masked (ten times over);
 * on, every record's is masked to one bit before the original test promotes
 * its status word (+3Ch). A model of the loop gives both results. */
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
enum { STACK=0xF000, FRAME=0xF100, DS_SEG=0x5000, ROSTER=0xD892, SIZE=0x50, COUNT=10 };
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
static const uint16_t REALISM[COUNT]={3,1,2,3,4,5,0x100,0x101,7,0};
static const uint16_t STATUS[COUNT]={0,2,1,0,2,3,0,2,0,2};
static void model(int fixed,uint16_t *realism,uint16_t *status)
{
    for(unsigned i=0;i<COUNT;i++) {
        uint16_t v=REALISM[i], s=STATUS[i];
        if(fixed||i==0) v&=1;
        if(v) { if(s==0||s==2) s++; }
        realism[i]=v; status[i]=s;
    }
}
static void prepare(const rc_module *mod,const uint8_t *file,size_t len,int fixed,int engine)
{
    memset(m.mem,0,MEM_SIZE); cpu_init(&m.cpu,m.mem); m.cpu.user=&m;
    m.engine=engine; m.ips=MACHINE_DEFAULT_IPS;
    m.cpu.seg[S_CS]=0x1000; m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=DS_SEG;
    m.cpu.r[R_SP]=STACK; m.cpu.r[R_BP]=FRAME; m.cpu.r[R_SI]=0xABCD; m.cpu.r[R_DI]=0x4321;
    m.cpu.ip=0x6D12; m.cpu.icount=1000; m.cpu.stop_at=1000+1; m.trap_on=0;
    fixes_enable("all",0); fixes_enable("Q23",fixed);
    recomp_module_load(NULL,&m,"START.EXE",file,len,MODLOAD_EXEC,0x1000,0x1000);
    CHECK(test_load_generated_image(m.mem,MEM_SIZE,mod,0x1000));
    for(unsigned i=0;i<COUNT;i++) {
        put((uint16_t)(ROSTER+SIZE*i+0x42),REALISM[i]);
        put((uint16_t)(ROSTER+SIZE*i+0x3C),STATUS[i]);
    }
}
static void equal(const cpu_t *c,const uint8_t *memory)
{
    CHECK(!memcmp(c->r,m.cpu.r,sizeof c->r));
    CHECK(!memcmp(c->seg,m.cpu.seg,sizeof c->seg));
    CHECK(c->flags==m.cpu.flags && c->ip==m.cpu.ip && c->icount==m.cpu.icount);
    CHECK(!memcmp(memory,m.mem,MEM_SIZE));
}
/* One instruction at a time until the loop has ended at 0x6D46. */
static int finish(void)
{
    for(int k=0;k<500;k++) {
        if(m.cpu.ip==0x6D46) return 1;
        m.cpu.stop_at=m.cpu.icount+1;
        run(&m);
    }
    return 0;
}
int main(int argc,char **argv)
{
    if(argc!=2) { fprintf(stderr,"usage: check_q23 path/to/START.EXE\n"); return 2; }
    FILE *f=fopen(argv[1],"rb"); if(!f) return 2;
    fseek(f,0,SEEK_END); long len=ftell(f); rewind(f); if(len<=0) return 2;
    uint8_t *file=malloc((size_t)len),*saved=malloc(MEM_SIZE);
    m.mem=calloc(1,MEM_SIZE); if(!file||!saved||!m.mem) return 2;
    if(fread(file,1,(size_t)len,f)!=(size_t)len) return 2; fclose(f);
    const rc_module *mod=NULL;
    for(unsigned k=0;k<RC_NMODULES;k++) if(!strcmp(RC_MODULES[k]->name,"START.EXE")) mod=RC_MODULES[k];
    if(!mod) return 2;
    cpu_init(&m.cpu,m.mem); recomp_init(&m); matched_runner=run;
    unsigned checks=0, differing=0;
    for(int fixed=0;fixed<2;fixed++) {
        uint16_t realism[COUNT],status[COUNT];
        model(fixed,realism,status);
        cpu_t reference={0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,fixed,engine);
            CHECK(finish());
            for(unsigned i=0;i<COUNT;i++) {
                CHECK(seg_read16(&m.cpu,DS_SEG,(uint16_t)(ROSTER+SIZE*i+0x42))==realism[i]);
                CHECK(seg_read16(&m.cpu,DS_SEG,(uint16_t)(ROSTER+SIZE*i+0x3C))==status[i]);
            }
            CHECK(m.cpu.r[R_SI]==0xABCD && m.cpu.r[R_DI]==0x4321);
            if(engine==ENGINE_INTERP) { reference=m.cpu; memcpy(saved,m.mem,MEM_SIZE); }
            else equal(&reference,saved);
            checks++;
        }
    }
    { uint16_t r0[COUNT],s0[COUNT],r1[COUNT],s1[COUNT]; model(0,r0,s0); model(1,r1,s1);
      for(unsigned i=0;i<COUNT;i++) if(r0[i]!=r1[i]||s0[i]!=s1[i]) differing++; }
    CHECK(differing>0);                  /* the fix is observable */
    recomp_shutdown(&m); free(m.mem); free(file); free(saved);
    printf("Q23: %u staged runs; both states/engines; records with realism above 1; %u of ten records differ; %d failures\n",checks,differing,failures);
    return failures?1:0;
}
