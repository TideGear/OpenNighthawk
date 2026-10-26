/* Staged Q18 regression: check_q18 path/to/START.EXE (generated build).
 * Runs START's date loop from its head (0x2613) in both engines with Q18 off
 * and on. The original loop and the text call's argument pushes execute; only
 * the printing routine (0x959E) is a returning test stub that records the
 * month name, day and year it was given. The month-length table has twelve
 * real entries and, as in the shipped file, 80 in the 13th place. */
#include "fixes.h"
#include "matched.h"
#include "reloc_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static int failures, calls;
static unsigned got_name, got_day, got_year;
#undef CHECK
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); failures++; } } while(0)
enum { STACK=0xF000, FRAME=0xF100, DS_SEG=0x5000 };
static int printer(machine_t *mm)
{
    cpu_t *c=&mm->cpu;
    calls++;
    got_name=seg_read16(c,c->seg[S_SS],(uint16_t)(c->r[R_SP]+6));
    got_day=seg_read16(c,c->seg[S_SS],(uint16_t)(c->r[R_SP]+8));
    got_year=seg_read16(c,c->seg[S_SS],(uint16_t)(c->r[R_SP]+10));
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
static void put8(uint16_t at,uint8_t v) { m.mem[((uint32_t)DS_SEG<<4)+at]=v; }
typedef struct { uint16_t month, year, day; int fm; unsigned fy, fd; int om; unsigned oy, od; } sample;
static void prepare(const rc_module *mod,const uint8_t *file,size_t len,const sample *s,int fixed,int engine,unsigned budget)
{
    static const uint8_t days[13]={0,31,28,31,30,31,30,31,31,30,31,30,31};
    memset(m.mem,0,MEM_SIZE); cpu_init(&m.cpu,m.mem); m.cpu.user=&m;
    m.engine=engine; m.ips=MACHINE_DEFAULT_IPS;
    m.cpu.seg[S_CS]=0x1000; m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=DS_SEG;
    m.cpu.r[R_SP]=STACK; m.cpu.r[R_BP]=FRAME; m.cpu.r[R_SI]=0xABCD; m.cpu.r[R_DI]=0x4321;
    m.cpu.ip=0x2613; m.cpu.icount=1000; m.cpu.stop_at=1000+budget;
    fixes_enable("all",0); fixes_enable("Q18",fixed);
    recomp_override_enable("Q18TEST",1);
    recomp_module_load(NULL,&m,"START.EXE",file,len,MODLOAD_EXEC,0x1000,0x1000);
    CHECK(test_load_generated_image(m.mem,MEM_SIZE,mod,0x1000));
    for(unsigned k=0;k<13;k++) put8((uint16_t)(0x8C2+k),days[k]);
    put8(0x8C2+13,80);
    for(unsigned k=0;k<13;k++) put((uint16_t)(0x8A8+2*k),(uint16_t)(0x1100+k));
    put(FRAME-4,s->month); put(FRAME-2,s->year); put(FRAME-6,s->day); put(FRAME+4,0x7777);
    m.trap_on=1; m.trap_cs=0x1000; m.trap_ip=0x2646; m.trap_sp=(uint16_t)(STACK-10);
    calls=0; got_name=got_day=got_year=0;
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
    if(argc!=2) { fprintf(stderr,"usage: check_q18 path/to/START.EXE\n"); return 2; }
    FILE *f=fopen(argv[1],"rb"); if(!f) return 2;
    fseek(f,0,SEEK_END); long len=ftell(f); rewind(f); if(len<=0) return 2;
    uint8_t *file=malloc((size_t)len),*saved=malloc(MEM_SIZE);
    m.mem=calloc(1,MEM_SIZE); if(!file||!saved||!m.mem) return 2;
    if(fread(file,1,(size_t)len,f)!=(size_t)len) return 2; fclose(f);
    const rc_module *mod=NULL;
    for(unsigned k=0;k<RC_NMODULES;k++) if(!strcmp(RC_MODULES[k]->name,"START.EXE")) mod=RC_MODULES[k];
    if(!mod) return 2;
    cpu_init(&m.cpu,m.mem); recomp_init(&m); matched_runner=run;
    const recomp_override stub={"Q18TEST","START.EXE",START_47304,0,0x959E,printer,"observed test printer"};
    CHECK(recomp_override_add(&stub)>=0);
    /* Month names are 1100h + month. Original: a month past 12 reads the 80 in the table's 13th place,
     * so a day count of up to 80 stays in "month 13", printed as month 1 of the next year with the
     * leftover day (Jan 32..59 here). Fixed: it wraps to month 1 and goes on through February. */
    const sample cases[]={
        /* month year day   fixed(name,year,day)      original(name,year,day) */
        {1,1990,10,   1,1990,10,   1,1990,10},          /* inside the first month */
        {1,1990,45,   2,1990,14,   2,1990,14},          /* one month on */
        {11,1990,75,  1,1991,14,   1,1991,14},          /* across the year end, day fits January */
        {11,1990,120, 2,1991,28,   1,1991,59},          /* the defect: January 59 */
        {12,1990,100, 3,1991,10,   1,1991,69},          /* the table's 80 holds the rest of the 13th month */
        {12,1990,45,  1,1991,14,   1,1991,14},
        {11,1990,105, 2,1991,13,   1,1991,44},
        {10,1990,150, 2,1991,27,   1,1991,58}};
    unsigned checks=0;
    for(unsigned k=0;k<sizeof cases/sizeof cases[0];k++) for(int fixed=0;fixed<2;fixed++) {
        cpu_t reference={0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,&cases[k],fixed,engine,2000);
            CHECK(run(&m)==RUN_TRAP);
            CHECK(calls==1);
            if(getenv("Q18_TRACE")) fprintf(stderr,"case %u fixed %d engine %d: name %X year %u day %u\n",k,fixed,engine,got_name,got_year,got_day);
            CHECK(got_name==(unsigned)(0x1100+(fixed?cases[k].fm:cases[k].om)));
            CHECK(got_year==(fixed?cases[k].fy:cases[k].oy));
            CHECK(got_day==(fixed?cases[k].fd:cases[k].od));
            if(engine==ENGINE_INTERP) { reference=m.cpu; memcpy(saved,m.mem,MEM_SIZE); }
            else equal(&reference,saved);
            checks++;
        }
    }
    /* A stop at every instruction count, then resume, ends in the same state as an unbroken run. */
    for(int fixed=0;fixed<2;fixed++) for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
        prepare(mod,file,(size_t)len,&cases[3],fixed,engine,2000);
        CHECK(run(&m)==RUN_TRAP);
        const uint64_t total=m.cpu.icount-1000; cpu_t whole=m.cpu; memcpy(saved,m.mem,MEM_SIZE);
        for(unsigned limit=1;limit<total;limit++) {
            prepare(mod,file,(size_t)len,&cases[3],fixed,engine,limit); run(&m);
            m.cpu.stop_at=1000+2000;
            CHECK(run(&m)==RUN_TRAP);
            equal(&whole,saved);
            checks++;
        }
    }
    recomp_shutdown(&m); free(m.mem); free(file); free(saved);
    printf("Q18: %u staged runs; both states/engines; dates past December; stop and resume at every instruction; %d failures\n",checks,failures);
    return failures?1:0;
}
