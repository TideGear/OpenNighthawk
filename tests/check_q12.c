/* Staged Q12 regression: check_q12 path/to/VGAME.EXE (generated build).
 * Starts at the display pass's call to the ground-impact eligibility test
 * (0xA153) with the original routines 0x792E and 0xB9F6 executing in both
 * engines. The unit hit and the stale word on top of the stack name objects
 * of different classes, so the fix changes the answer; a run is also stopped
 * at every instruction count and resumed, to show a stop inside the callee
 * leaves a machine that finishes with the same result. */
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
static void put(uint16_t at,uint16_t v) { seg_write16(&m.cpu,0x5000,at,v); }
static void put8(uint16_t at,uint8_t v) { m.mem[((uint32_t)0x5000<<4)+at]=v; }
enum { STACK=0xF000, FRAME=0xF100, OBJECT=0x7000, MODEL=0x7100, WRECK_A=0x10, WRECK_B=0x20 };
/* Unit 3 is class 9 (wreck byte [C0D8]); unit 5 is class 0 (wreck byte [DED0]). */
typedef struct { uint16_t unit, stale, word, mark; int original, fixed; } sample;
static void prepare(const rc_module *mod,const uint8_t *file,size_t len,const sample *s,int fixed,int engine,unsigned budget)
{
    memset(m.mem,0,MEM_SIZE); cpu_init(&m.cpu,m.mem); m.cpu.user=&m;
    m.engine=engine; m.ips=MACHINE_DEFAULT_IPS;
    m.cpu.seg[S_CS]=0x1000; m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=0x5000;
    m.cpu.r[R_SP]=STACK; m.cpu.r[R_BP]=FRAME; m.cpu.r[R_SI]=0xABCD; m.cpu.r[R_DI]=0x4321;
    m.cpu.ip=0xA153; m.cpu.icount=1000; m.cpu.stop_at=1000+budget;
    fixes_enable("all",0); fixes_enable("Q12",fixed);
    recomp_module_load(NULL,&m,"VGAME.EXE",file,len,MODLOAD_EXEC,0x1000,0x1000);
    CHECK(test_load_generated_image(m.mem,MEM_SIZE,mod,0x1000));
    put(FRAME-0x0A,s->unit); put(STACK,s->stale);
    put8(0xB2DC+3*16,3); put8(0xB2DC+4*16,4); put8(0xB2DC+5*16,5);          /* the unit's kind is its own number */
    put8(0xC630+3,0x09); put8(0xC630+4,0x09); put8(0xC630+5,0x00);          /* class 9 takes [C0D8]; class 0 takes [DED0] */
    put8(0xC0D8,WRECK_A); put8(0xDED0,WRECK_B);
    put(0x9F40,OBJECT); put(OBJECT,s->word);
    put(OBJECT+0x0C,MODEL); m.mem[((uint32_t)0x5000<<4)+MODEL+6]=(uint8_t)s->mark;
    m.trap_on=1; m.trap_cs=0x1000; m.trap_ip=0xA156; m.trap_sp=STACK;
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
    if(argc!=2) { fprintf(stderr,"usage: check_q12 path/to/VGAME.EXE\n"); return 2; }
    FILE *f=fopen(argv[1],"rb"); if(!f) return 2;
    fseek(f,0,SEEK_END); long len=ftell(f); rewind(f); if(len<=0) return 2;
    uint8_t *file=malloc((size_t)len),*saved=malloc(MEM_SIZE);
    m.mem=calloc(1,MEM_SIZE); if(!file||!saved||!m.mem) return 2;
    if(fread(file,1,(size_t)len,f)!=(size_t)len) return 2; fclose(f);
    const rc_module *mod=NULL;
    for(unsigned k=0;k<RC_NMODULES;k++) if(!strcmp(RC_MODULES[k]->name,"VGAME.EXE")) mod=RC_MODULES[k];
    if(!mod) return 2;
    cpu_init(&m.cpu,m.mem); recomp_init(&m); matched_runner=run;
    /* AX is 1 when the object may still be destroyed, 0 when it is already a wreck.
     * original: the class of the stale unit decides; fixed: the unit hit decides. */
    const sample cases[]={
        {3,5,WRECK_A,0,1,0},       /* the hit unit's wreck byte matches */
        {3,5,WRECK_B,0,0,1},       /* only the stale unit's wreck byte matches */
        {3,5,0x30,0,1,1},          /* matches neither */
        {3,5,0x30,0x80|0x30,0,0},  /* marked destroyed through the model record: both refuse */
        {3,5,0x30,0x30,1,1},       /* an unmarked model record */
        {5,3,WRECK_A,0,0,1},       /* the same, with the roles swapped */
        {5,3,WRECK_B,0,1,0},
        {3,3,WRECK_A,0,0,0},       /* equal unit and stale word: the fix declines */
        {5,5,WRECK_B,0,0,0},
        {3,4,0x30,0,1,1}};         /* two units of the same class: the clock check */
    unsigned checks=0;
    for(unsigned k=0;k<sizeof cases/sizeof cases[0];k++) for(int fixed=0;fixed<2;fixed++) {
        cpu_t reference={0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,&cases[k],fixed,engine,500);
            CHECK(run(&m)==RUN_TRAP);
            CHECK(m.cpu.r[R_AX]==(uint16_t)(fixed?cases[k].fixed:cases[k].original));
            CHECK(m.cpu.r[R_SP]==STACK && m.cpu.r[R_BP]==FRAME && m.cpu.r[R_SI]==0xABCD && m.cpu.r[R_DI]==0x4321);
            CHECK(seg_read16(&m.cpu,0x5000,STACK)==cases[k].stale);
            if(engine==ENGINE_INTERP) { reference=m.cpu; memcpy(saved,m.mem,MEM_SIZE); }
            else equal(&reference,saved);
            checks++;
        }
    }
    /* A decline leaves the original instruction to run: same clocks as the unfixed run. */
    for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
        uint64_t spent[2];
        for(int fixed=0;fixed<2;fixed++) {
            prepare(mod,file,(size_t)len,&cases[7],fixed,engine,500);
            CHECK(run(&m)==RUN_TRAP); spent[fixed]=m.cpu.icount;
        }
        CHECK(spent[0]==spent[1]);
        uint64_t differing[2];
        for(int fixed=0;fixed<2;fixed++) {
            prepare(mod,file,(size_t)len,&cases[9],fixed,engine,500);
            CHECK(run(&m)==RUN_TRAP); differing[fixed]=m.cpu.icount;
        }
        CHECK(differing[1]==differing[0]+1);   /* the dropped argument's POP; both units are class 9, so the callee runs the same arm */
    }
    /* A stop at every instruction count, then resume: the same result and memory as an unbroken run. */
    for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) for(int k=0;k<2;k++) {
        prepare(mod,file,(size_t)len,&cases[k],1,engine,500);
        CHECK(run(&m)==RUN_TRAP);
        const uint64_t total=m.cpu.icount-1000;
        const uint16_t ax=m.cpu.r[R_AX]; memcpy(saved,m.mem,MEM_SIZE);
        cpu_t whole=m.cpu;
        for(unsigned limit=1;limit<total;limit++) {
            prepare(mod,file,(size_t)len,&cases[k],1,engine,limit);
            run(&m);
            CHECK(m.cpu.icount<=1000+limit);
            m.cpu.stop_at=1000+500;
            CHECK(run(&m)==RUN_TRAP);
            CHECK(m.cpu.r[R_AX]==ax);
            equal(&whole,saved);
            checks++;
        }
    }
    recomp_shutdown(&m); free(m.mem); free(file); free(saved);
    printf("Q12: %u staged runs; both states/engines; declines and clocks; stop and resume at every instruction; %d failures\n",checks,failures);
    return failures?1:0;
}
