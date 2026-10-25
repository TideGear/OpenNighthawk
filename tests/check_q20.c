/* Staged Q20 regression: check_q20 path/to/START.EXE (generated build).
 * Starts at the briefing key loop's click on a map site (0x11C8: the item
 * under the cursor is turned into a site index and passed to the popup
 * routine 0x1C2F) in both engines with Q20 off and on, for every remembered
 * hover item [D096h] from 0 to 14. The popup routine is a returning test stub
 * that records its argument; the original instructions around it run. Off,
 * the popup is always drawn; on, it is left to the hover hook while the
 * remembered item is not a site (<= 8), and drawn as before otherwise. */
#include "fixes.h"
#include "matched.h"
#include "reloc_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static int failures, popups, popup_arg;
#undef CHECK
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); failures++; } } while(0)
enum { STACK=0xF000, FRAME=0xF100, DS_SEG=0x5000 };
static int popup(machine_t *mm)
{
    cpu_t *c=&mm->cpu;
    popups++; popup_arg=seg_read16(c,c->seg[S_SS],(uint16_t)(c->r[R_SP]+2));
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
static void prepare(const rc_module *mod,const uint8_t *file,size_t len,int hover,int item,int fixed,int engine,unsigned budget)
{
    memset(m.mem,0,MEM_SIZE); cpu_init(&m.cpu,m.mem); m.cpu.user=&m;
    m.engine=engine; m.ips=MACHINE_DEFAULT_IPS;
    m.cpu.seg[S_CS]=0x1000; m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=DS_SEG;
    m.cpu.r[R_SP]=STACK; m.cpu.r[R_BP]=FRAME; m.cpu.r[R_SI]=0xABCD;
    m.cpu.ip=0x11C8; m.cpu.icount=1000; m.cpu.stop_at=1000+budget;
    fixes_enable("all",0); fixes_enable("Q20",fixed);
    recomp_override_enable("Q20TEST",1);
    recomp_module_load(NULL,&m,"START.EXE",file,len,MODLOAD_EXEC,0x1000,0x1000);
    CHECK(test_load_generated_image(m.mem,MEM_SIZE,mod,0x1000));
    m.mem[((uint32_t)DS_SEG<<4)+0xD096]=(uint8_t)hover;
    m.mem[((uint32_t)DS_SEG<<4)+(uint16_t)(FRAME-2)]=(uint8_t)item;
    m.trap_on=0;
    popups=0; popup_arg=-1;
}
static void equal(const cpu_t *c,const uint8_t *memory)
{
    CHECK(!memcmp(c->r,m.cpu.r,sizeof c->r));
    CHECK(!memcmp(c->seg,m.cpu.seg,sizeof c->seg));
    CHECK(c->flags==m.cpu.flags && c->ip==m.cpu.ip && c->icount==m.cpu.icount);
    CHECK(!memcmp(memory,m.mem,MEM_SIZE));
}
/* One instruction at a time until the loop has returned to 0x111B. */
static int finish(void)
{
    for(int k=0;k<200;k++) {
        if(m.cpu.ip==0x111B) return 1;
        m.cpu.stop_at=m.cpu.icount+1;
        run(&m);
    }
    return 0;
}
int main(int argc,char **argv)
{
    if(argc!=2) { fprintf(stderr,"usage: check_q20 path/to/START.EXE\n"); return 2; }
    FILE *f=fopen(argv[1],"rb"); if(!f) return 2;
    fseek(f,0,SEEK_END); long len=ftell(f); rewind(f); if(len<=0) return 2;
    uint8_t *file=malloc((size_t)len),*saved=malloc(MEM_SIZE);
    m.mem=calloc(1,MEM_SIZE); if(!file||!saved||!m.mem) return 2;
    if(fread(file,1,(size_t)len,f)!=(size_t)len) return 2; fclose(f);
    const rc_module *mod=NULL;
    for(unsigned k=0;k<RC_NMODULES;k++) if(!strcmp(RC_MODULES[k]->name,"START.EXE")) mod=RC_MODULES[k];
    if(!mod) return 2;
    cpu_init(&m.cpu,m.mem); recomp_init(&m); matched_runner=run;
    const recomp_override stub={"Q20TEST","START.EXE",START_47304,0,0x1C2F,popup,"observed test popup"};
    CHECK(recomp_override_add(&stub)>=0);
    unsigned checks=0;
    for(int hover=0;hover<=14;hover++) for(int item=9;item<=12;item+=3) for(int fixed=0;fixed<2;fixed++) {
        cpu_t reference={0};
        uint64_t ticks[2]={0,0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,hover,item,fixed,engine,100);
            CHECK(finish());
            const int skipped=fixed&&hover<=8;
            CHECK(popups==(skipped?0:1));
            if(!skipped) CHECK(popup_arg==item-9);
            /* the pushed argument stays where the original's call leaves it, on both paths */
            CHECK(m.cpu.r[R_SP]==STACK-2);
            CHECK(seg_read16(&m.cpu,DS_SEG,(uint16_t)(STACK-2))==(uint16_t)(item-9));
            ticks[engine]=m.cpu.icount;
            if(engine==ENGINE_INTERP) { reference=m.cpu; memcpy(saved,m.mem,MEM_SIZE); }
            else equal(&reference,saved);
            checks++;
        }
        CHECK(ticks[0]==ticks[1]);
    }
    recomp_shutdown(&m); free(m.mem); free(file); free(saved);
    printf("Q20: %u staged runs; both states/engines; popup left to the hover hook only while no site is remembered; %d failures\n",checks,failures);
    return failures?1:0;
}
