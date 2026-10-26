/* Staged D18 regression: check_d18 path/to/START.EXE (generated build).
 * Runs the objective sentences' target-name lookup (0x2657 onward) in both
 * engines with D18 off and on, for a target record whose first word is zero
 * (an unnamed airbase) and one that has a name. The original instructions
 * run up to the switch at 0x2840; the name pointer kept in [bp-4] is then
 * read. Off, an unnamed target leaves NULL (which sprintf prints as
 * "(null)"); on, it points at an empty string (a zero byte at the start of
 * the data segment). A named target is unchanged. */
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
enum { STACK=0xF000, FRAME=0xF100, DS_SEG=0x5000, OBJECTIVE=0x7000, TARGET=3, KIND=2 };
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
static void prepare(const rc_module *mod,const uint8_t *file,size_t len,unsigned first_word,int fixed,int engine)
{
    memset(m.mem,0,MEM_SIZE); cpu_init(&m.cpu,m.mem); m.cpu.user=&m;
    m.engine=engine; m.ips=MACHINE_DEFAULT_IPS;
    m.cpu.seg[S_CS]=0x1000; m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=DS_SEG;
    m.cpu.r[R_SP]=STACK; m.cpu.r[R_BP]=FRAME; m.cpu.r[R_SI]=0xABCD;
    m.cpu.ip=0x2657; m.cpu.icount=1000; m.cpu.stop_at=1000+1000; m.trap_on=0;
    fixes_enable("all",0); fixes_enable("D18",fixed);
    recomp_module_load(NULL,&m,"START.EXE",file,len,MODLOAD_EXEC,0x1000,0x1000);
    CHECK(test_load_generated_image(m.mem,MEM_SIZE,mod,0x1000));
    put(FRAME+4,OBJECTIVE);                       /* the objective record */
    put(OBJECTIVE+2,TARGET); put(OBJECTIVE+8,KIND);
    put((uint16_t)(0xCBDE+16*TARGET),(uint16_t)first_word);
    for(unsigned k=0;k<16;k++) put((uint16_t)(0xDC70+2*k),(uint16_t)(0x3000+k));
}
static void equal(const cpu_t *c,const uint8_t *memory)
{
    CHECK(!memcmp(c->r,m.cpu.r,sizeof c->r));
    CHECK(!memcmp(c->seg,m.cpu.seg,sizeof c->seg));
    CHECK(c->flags==m.cpu.flags && c->ip==m.cpu.ip && c->icount==m.cpu.icount);
    CHECK(!memcmp(memory,m.mem,MEM_SIZE));
}
/* One instruction at a time until the switch at 0x2840. */
static int finish(void)
{
    for(int k=0;k<200;k++) {
        if(m.cpu.ip==0x2840) return 1;
        m.cpu.stop_at=m.cpu.icount+1;
        run(&m);
    }
    return 0;
}
int main(int argc,char **argv)
{
    if(argc!=2) { fprintf(stderr,"usage: check_d18 path/to/START.EXE\n"); return 2; }
    FILE *f=fopen(argv[1],"rb"); if(!f) return 2;
    fseek(f,0,SEEK_END); long len=ftell(f); rewind(f); if(len<=0) return 2;
    uint8_t *file=malloc((size_t)len),*saved=malloc(MEM_SIZE);
    m.mem=calloc(1,MEM_SIZE); if(!file||!saved||!m.mem) return 2;
    if(fread(file,1,(size_t)len,f)!=(size_t)len) return 2; fclose(f);
    const rc_module *mod=NULL;
    for(unsigned k=0;k<RC_NMODULES;k++) if(!strcmp(RC_MODULES[k]->name,"START.EXE")) mod=RC_MODULES[k];
    if(!mod) return 2;
    cpu_init(&m.cpu,m.mem); recomp_init(&m); matched_runner=run;
    unsigned checks=0;
    const unsigned words[]={0,5,1};
    for(unsigned k=0;k<3;k++) for(int fixed=0;fixed<2;fixed++) {
        cpu_t reference={0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,words[k],fixed,engine);
            CHECK(finish());
            const uint16_t name=seg_read16(&m.cpu,DS_SEG,(uint16_t)(FRAME-4));
            const uint16_t want=words[k]?(uint16_t)(0x3000+words[k]):(fixed?0x0004:0x0000);
            CHECK(name==want);
            if(!words[k]&&fixed) CHECK(m.mem[((uint32_t)DS_SEG<<4)+name]==0);   /* an empty string */
            if(engine==ENGINE_INTERP) { reference=m.cpu; memcpy(saved,m.mem,MEM_SIZE); }
            else equal(&reference,saved);
            checks++;
        }
    }
    recomp_shutdown(&m); free(m.mem); free(file); free(saved);
    printf("D18: %u staged runs; both states/engines; unnamed and named targets; %d failures\n",checks,failures);
    return failures?1:0;
}
