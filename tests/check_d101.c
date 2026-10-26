/* D101 staged regression. Original erase argument pushes and clean-up run;
 * the two far copy calls are observed stubs. Real-driver staged pixel evidence
 * is recorded separately. Check initialization and every stop/resume boundary. */
#include "fixes.h"
#include "matched.h"
#include "reloc_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static int failures, copies;
#undef CHECK
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); failures++; } } while(0)
enum { STACK=0xF000, FRAME=0xF100, DS_SEG=0x5000 };
static int copy_stub(machine_t *mm)
{
    cpu_t *c=&mm->cpu;
    copies++;
    c->ip=(uint16_t)(c->ip+5); c->icount++;
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
static void prepare(const rc_module *mod,const uint8_t *file,size_t len,unsigned slot,int fixed,int engine,unsigned budget)
{
    memset(m.mem,0,MEM_SIZE); cpu_init(&m.cpu,m.mem); m.cpu.user=&m;
    m.engine=engine; m.ips=MACHINE_DEFAULT_IPS;
    m.cpu.seg[S_CS]=0x1000; m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=DS_SEG;
    m.cpu.r[R_SP]=STACK; m.cpu.r[R_BP]=FRAME; m.cpu.r[R_SI]=0xABCD;
    m.cpu.ip=0x5CAA; m.cpu.icount=1000; m.cpu.stop_at=1000+budget;
    fixes_enable("all",0); fixes_enable("D101",fixed);
    recomp_override_enable("D101TEST",1);
    recomp_module_load(NULL,&m,"START.EXE",file,len,MODLOAD_EXEC,0x1000,0x1000);
    CHECK(test_load_generated_image(m.mem,MEM_SIZE,mod,0x1000));
    seg_write16(&m.cpu,DS_SEG,FRAME-10,(uint16_t)slot);
    for(unsigned k=0;k<32;k++) seg_write16(&m.cpu,DS_SEG,(uint16_t)(0xDEC+2*k),(uint16_t)(100+k));
    m.trap_on=1; m.trap_cs=0x1000; m.trap_ip=0x5CFC; m.trap_sp=STACK;
    copies=0;
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
    for(unsigned k=0;k<RC_NMODULES;k++) if(!strcmp(RC_MODULES[k]->name,"START.EXE")) mod=RC_MODULES[k];
    if(!mod) return 2;
    cpu_init(&m.cpu,m.mem); recomp_init(&m); matched_runner=run;
    const recomp_override stubs[]={
        {"D101TEST","START.EXE",START_47304,0,0x5CD1,copy_stub,"observe first erase copy"},
        {"D101TEST","START.EXE",START_47304,0,0x5CF4,copy_stub,"observe second erase copy"}};
    for(unsigned k=0;k<2;k++) CHECK(recomp_override_add(&stubs[k])>=0);
    const unsigned slots[]={0,1,2,3,4,5,6,7,11,0x7FFF,0x8000,0xFFFF};
    unsigned checks=0;
    for(unsigned k=0;k<sizeof slots/sizeof slots[0];k++) for(int fixed=0;fixed<2;fixed++) {
        cpu_t reference={0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,slots[k],fixed,engine,500);
            CHECK(run(&m)==RUN_TRAP);
            CHECK(copies==((fixed&&slots[k]>=7)?0:2));
            CHECK(m.cpu.r[R_SP]==STACK && m.cpu.r[R_BP]==FRAME);
            if(engine==ENGINE_INTERP) { reference=m.cpu; memcpy(saved,m.mem,MEM_SIZE); }
            else equal(&reference,saved);
            checks++;
        }
    }
    for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) for(int fixed=0;fixed<2;fixed++) {
        prepare(mod,file,(size_t)len,11,fixed,engine,500);
        m.cpu.ip=0x5BF7; m.trap_ip=0x5BFA;
        const uint16_t flags=m.cpu.flags;
        CHECK(run(&m)==RUN_TRAP);
        CHECK(seg_read16(&m.cpu,DS_SEG,FRAME-10)==(fixed?0xFFFF:11));
        CHECK(m.cpu.r[R_AX]==0x5A8A && m.cpu.flags==flags);
        CHECK(m.cpu.icount==1000+(fixed?2:1));
        cpu_t whole=m.cpu; memcpy(saved,m.mem,MEM_SIZE);
        if(fixed) {
            prepare(mod,file,(size_t)len,11,1,engine,1);
            m.cpu.ip=0x5BF7; m.trap_ip=0x5BFA;
            CHECK(run(&m)==RUN_SLICE && m.cpu.icount==1001 && m.cpu.ip==0x5BF8);
            m.cpu.stop_at=1500; CHECK(run(&m)==RUN_TRAP); equal(&whole,saved);
        }
        checks++;
    }
    /* Both the bypass and a remembered slot resume at every clock boundary. */
    for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) for(unsigned k=0;k<2;k++) {
        unsigned slot=k?11:2;
        prepare(mod,file,(size_t)len,slot,1,engine,500); CHECK(run(&m)==RUN_TRAP);
        const uint64_t total=m.cpu.icount-1000; cpu_t whole=m.cpu; memcpy(saved,m.mem,MEM_SIZE);
        for(unsigned limit=1;limit<total;limit++) {
            prepare(mod,file,(size_t)len,slot,1,engine,limit); run(&m);
            CHECK(m.cpu.icount<=1000+limit);
            m.cpu.stop_at=1500; CHECK(run(&m)==RUN_TRAP); equal(&whole,saved);checks++;
        }
    }
    recomp_shutdown(&m); free(m.mem);free(file);free(saved);
    printf("D101: %u staged runs; both states/engines; initialized sentinel, seven valid slots, invalid slots and event boundaries; %d failures\n",checks,failures);
    return failures?1:0;
}
