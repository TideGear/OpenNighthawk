/* Staged Q4 regression: check_q4 path/to/VGAME.EXE (generated build).
 * Runs the joystick axis scaling (1058:0D09) for axis 0 and 3 with Q4 off and
 * on, both engines, on each side of the centre. With a real calibrated span
 * the hook declines and the result is the original's (fix off and on give the
 * same state). With a zero span the original's DIV raises the divide error;
 * with Q4 the quotient is taken as 0 and the axis byte is its side's centre
 * (7Fh below, 80h above). */
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
enum { STACK=0xF000, DS_SEG=0x5000, OUT=0x2CD2 };
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
/* Per axis a (word offset 2a): value [2CCA], centre [2CB2], minimum [2CA2], maximum [2CAA],
 * span below [2CBA], span above [2CC2]. */
static void prepare(const rc_module *mod,const uint8_t *file,size_t len,unsigned axis,unsigned value,unsigned lo_span,unsigned hi_span,int fixed,int engine,unsigned budget)
{
    memset(m.mem,0,MEM_SIZE); cpu_init(&m.cpu,m.mem); m.cpu.user=&m;
    m.engine=engine; m.ips=MACHINE_DEFAULT_IPS;
    m.cpu.seg[S_CS]=0x2058; m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=DS_SEG;
    m.cpu.r[R_SP]=STACK; m.cpu.r[R_BP]=0xBEEF; m.cpu.r[R_SI]=(uint16_t)axis; m.cpu.r[R_AX]=0x1111; m.cpu.r[R_DX]=0x2222;
    m.cpu.ip=0x0D09; m.cpu.icount=1000; m.cpu.stop_at=1000+budget;
    fixes_enable("all",0); fixes_enable("Q4",fixed);
    recomp_module_load(NULL,&m,"VGAME.EXE",file,len,MODLOAD_EXEC,0x1000,0x1000);
    CHECK(test_load_generated_image(m.mem,MEM_SIZE,mod,0x1000));
    const uint16_t w=(uint16_t)(2*axis);
    put((uint16_t)(0x2CCA+w),(uint16_t)value); put((uint16_t)(0x2CB2+w),100);
    put((uint16_t)(0x2CA2+w),50); put((uint16_t)(0x2CAA+w),150);
    put((uint16_t)(0x2CBA+w),(uint16_t)lo_span); put((uint16_t)(0x2CC2+w),(uint16_t)hi_span);
    put(STACK,0xFF00);
    m.trap_on=1; m.trap_cs=0x2058; m.trap_ip=0xFF00; m.trap_sp=STACK+2;
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
    if(argc!=2) { fprintf(stderr,"usage: check_q4 path/to/VGAME.EXE\n"); return 2; }
    FILE *f=fopen(argv[1],"rb"); if(!f) return 2;
    fseek(f,0,SEEK_END); long len=ftell(f); rewind(f); if(len<=0) return 2;
    uint8_t *file=malloc((size_t)len),*saved=malloc(MEM_SIZE),*other=malloc(MEM_SIZE);
    m.mem=calloc(1,MEM_SIZE); if(!file||!saved||!other||!m.mem) return 2;
    if(fread(file,1,(size_t)len,f)!=(size_t)len) return 2; fclose(f);
    const rc_module *mod=NULL;
    for(unsigned k=0;k<RC_NMODULES;k++) if(!strcmp(RC_MODULES[k]->name,"VGAME.EXE")) mod=RC_MODULES[k];
    if(!mod) return 2;
    cpu_init(&m.cpu,m.mem); recomp_init(&m); matched_runner=run;
    unsigned checks=0;
    const unsigned axes[]={0,3};
    /* value 90 is below the centre (100) and above the minimum (50): the low span divides; 120 is above the centre
     * and below the maximum (150): the high span divides. */
    const struct { unsigned value; int low; } sides[]={{90,1},{120,0}};
    for(unsigned a=0;a<2;a++) for(unsigned s=0;s<2;s++) {
        const unsigned axis=axes[a], value=sides[s].value;
        /* real spans: the fix changes nothing */
        cpu_t keep={0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,axis,value,50,50,0,engine,500);
            CHECK(run(&m)==RUN_TRAP); keep=m.cpu; memcpy(other,m.mem,MEM_SIZE);
            prepare(mod,file,(size_t)len,axis,value,50,50,1,engine,500);
            CHECK(run(&m)==RUN_TRAP); equal(&keep,other);
            checks+=2;
        }
        /* zero span of the side in use */
        cpu_t reference={0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,axis,value,sides[s].low?0:50,sides[s].low?50:0,0,engine,500);
            CHECK(run(&m)!=RUN_TRAP);                      /* the original: the divide error */
            prepare(mod,file,(size_t)len,axis,value,sides[s].low?0:50,sides[s].low?50:0,1,engine,500);
            CHECK(run(&m)==RUN_TRAP);
            CHECK(m.mem[((uint32_t)DS_SEG<<4)+OUT+axis]==(sides[s].low?0x7F:0x80));
            CHECK(m.cpu.r[R_SI]==axis && m.cpu.r[R_SP]==STACK+2);
            if(engine==ENGINE_INTERP) { reference=m.cpu; memcpy(saved,m.mem,MEM_SIZE); }
            else equal(&reference,saved);
            checks+=2;
        }
    }
    /* A stop at every instruction count of the guarded run, then resume. */
    for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
        prepare(mod,file,(size_t)len,0,90,0,50,1,engine,500);
        CHECK(run(&m)==RUN_TRAP);
        const uint64_t total=m.cpu.icount-1000; cpu_t whole=m.cpu; memcpy(saved,m.mem,MEM_SIZE);
        for(unsigned limit=1;limit<total;limit++) {
            prepare(mod,file,(size_t)len,0,90,0,50,1,engine,limit); run(&m);
            m.cpu.stop_at=1000+500;
            CHECK(run(&m)==RUN_TRAP);
            equal(&whole,saved);
            checks++;
        }
    }
    recomp_shutdown(&m); free(m.mem); free(file); free(saved); free(other);
    printf("Q4: %u staged runs; both states/engines; zero spans on both sides; stop and resume; %d failures\n",checks,failures);
    return failures?1:0;
}
