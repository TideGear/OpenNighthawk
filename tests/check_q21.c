/* Staged Q21 regression: check_q21 path/to/VGAME.EXE (generated build).
 * The display pass's steering bracket reaches 0xAE9F, where a weapon of type
 * 1Eh gets its half-width [DEBE] worked out from a corner that is only
 * written when the aircraft is banked less than 45 degrees ([2DF2] below
 * 2000h in magnitude). Q21 off and on, both engines:
 *  - banked 45 degrees or more with weapon 1Eh, fix on: the code goes on to
 *    0xAEDE with [DEBE] untouched (stepped to 0xAEDE and compared between
 *    engines);
 *  - every other combination: the hook declines, leaving the original
 *    instruction, registers, flags, memory and clock exactly as they were. */
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
enum { STACK=0xF000, FRAME=0xF100, DS_SEG=0x5000 };
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
static void prepare(const rc_module *mod,const uint8_t *file,size_t len,uint16_t weapon,uint16_t roll,int fixed,int engine)
{
    memset(m.mem,0,MEM_SIZE); cpu_init(&m.cpu,m.mem); m.cpu.user=&m;
    m.engine=engine; m.ips=MACHINE_DEFAULT_IPS;
    m.cpu.seg[S_CS]=0x1000; m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=DS_SEG;
    m.cpu.r[R_SP]=STACK; m.cpu.r[R_BP]=FRAME; m.cpu.r[R_SI]=0xABCD; m.cpu.r[R_DI]=0x4321;
    m.cpu.ip=0xAE9F; m.cpu.icount=1000; m.cpu.stop_at=1000+100; m.trap_on=0;
    fixes_enable("all",0); fixes_enable("Q21",fixed);
    recomp_module_load(NULL,&m,"VGAME.EXE",file,len,MODLOAD_EXEC,0x1000,0x1000);
    CHECK(test_load_generated_image(m.mem,MEM_SIZE,mod,0x1000));
    put(FRAME-0x12,weapon); put(0x2DF2,roll); put(0xDEBE,0x1234);
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
    if(argc!=2) { fprintf(stderr,"usage: check_q21 path/to/VGAME.EXE\n"); return 2; }
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
    /* [2DF2] is the signed roll; the original's test is |roll| >= 2000h with the 16-bit abs. */
    const uint16_t rolls[]={0,0x1FFF,0xE001,0x2000,0xE000,0x4000,0x7FFF,0xC000,0x8000};
    const int skips[]={0,0,0,1,1,1,1,1,0};      /* 8000h: abs stays negative, so it passes the original's signed test */
    for(unsigned k=0;k<sizeof rolls/sizeof rolls[0];k++) for(unsigned w=0;w<3;w++) {
        const uint16_t weapon=w==0?0x1E:w==1?0x1D:0x1F;
        const int expect_skip=weapon==0x1E&&skips[k];
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            /* decision: the hook alone */
            prepare(mod,file,(size_t)len,weapon,rolls[k],1,engine);
            cpu_t before=m.cpu; memcpy(saved,m.mem,MEM_SIZE);
            const int r=recomp_override_step(&m);
            CHECK(r==(expect_skip?1:-1));
            if(expect_skip) { CHECK(m.cpu.ip==0xAEDE && m.cpu.icount==before.icount+1); CHECK(!memcmp(saved,m.mem,MEM_SIZE)); }
            else { CHECK(before.ip==m.cpu.ip&&before.icount==m.cpu.icount&&!memcmp(before.r,m.cpu.r,sizeof before.r)&&before.flags==m.cpu.flags); CHECK(!memcmp(saved,m.mem,MEM_SIZE)); }
            checks++;
            /* off: nothing applies */
            prepare(mod,file,(size_t)len,weapon,rolls[k],0,engine);
            CHECK(recomp_override_step(&m)!=1 || m.cpu.ip!=0xAEDE);
            checks++;
        }
    }
    /* The skip, run through by single steps in both engines, leaves [DEBE] alone and ends the same way. */
    cpu_t reference={0};
    for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
        prepare(mod,file,(size_t)len,0x1E,0x4000,1,engine);
        for(int k=0;k<20&&m.cpu.ip==0xAE9F;k++) { m.cpu.stop_at=m.cpu.icount+1; run(&m); }
        CHECK(m.cpu.ip==0xAEDE);
        CHECK(seg_read16(&m.cpu,DS_SEG,0xDEBE)==0x1234);
        if(engine==ENGINE_INTERP) { reference=m.cpu; memcpy(saved,m.mem,MEM_SIZE); }
        else {
            if(getenv("Q21_TRACE")) fprintf(stderr,"ref ip %04X ic %llu fl %04X ax %04X sp %04X | now ip %04X ic %llu fl %04X ax %04X sp %04X\n",reference.ip,(unsigned long long)reference.icount,reference.flags,reference.r[R_AX],reference.r[R_SP],m.cpu.ip,(unsigned long long)m.cpu.icount,m.cpu.flags,m.cpu.r[R_AX],m.cpu.r[R_SP]);
            equal(&reference,saved);
        }
        checks++;
    }
    recomp_shutdown(&m); free(m.mem); free(file); free(saved);
    printf("Q21: %u staged runs; both engines; the bracket keeps its width when banked 45 degrees or more with weapon 1Eh, and declines otherwise; %d failures\n",checks,failures);
    return failures?1:0;
}
