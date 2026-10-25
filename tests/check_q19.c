/* Staged Q19 regression: check_q19 path/to/START.EXE (generated build).
 * Runs START's Enemy Troops overlay (0x1AB6) in both engines with Q19 off
 * and on over several troop grids. The original scan and its arithmetic run;
 * the sprite call (0x3693) and the caption routine (0x1B37) are returning
 * test stubs that record what they are given. A small model of the loop
 * gives the markers each state must draw, in order. */
#include "fixes.h"
#include "matched.h"
#include "reloc_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static int failures, draws, captions, caption_kind;
static unsigned xs[512], ys[512];
#undef CHECK
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); failures++; } } while(0)
enum { STACK=0xF000, DS_SEG=0x5000, GRID=0xCAD8 };
static int sprite(machine_t *mm)
{
    cpu_t *c=&mm->cpu;
    if(draws<512) {
        ys[draws]=seg_read16(c,c->seg[S_SS],(uint16_t)(c->r[R_SP]+4));
        xs[draws]=seg_read16(c,c->seg[S_SS],(uint16_t)(c->r[R_SP]+6));
    }
    draws++;
    c->ip=cpu_pop16(c); c->icount++;
    return 1;
}
static int caption(machine_t *mm)
{
    cpu_t *c=&mm->cpu;
    captions++; caption_kind=seg_read16(c,c->seg[S_SS],(uint16_t)(c->r[R_SP]+2));
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
/* The loop as the code has it. Cell (row a, column b) is the byte at GRID + a + 16 * b. */
static unsigned model(const uint8_t *grid,int fixed,unsigned *mx,unsigned *my)
{
    unsigned a=0,b=0,n=0;
    for(;;) {
        if(a>=16) break;
        b=0;
        for(;;) {
            if(b>=16) break;
            if(grid[a+16*b]&0x10) {
                const unsigned x=(b*0x7FFu)/0xC3u, y=(a*0x7FFu)/0x92u;
                if(n<512) { mx[n]=x; my[n]=y; }
                n++;
                if(!fixed) { b=x&0xFFFF; a=y&0xFFFF; }
            }
            b++;
        }
        a++;
    }
    return n;
}
static void prepare(const rc_module *mod,const uint8_t *file,size_t len,const uint8_t *grid,int fixed,int engine,unsigned budget)
{
    memset(m.mem,0,MEM_SIZE); cpu_init(&m.cpu,m.mem); m.cpu.user=&m;
    m.engine=engine; m.ips=MACHINE_DEFAULT_IPS;
    m.cpu.seg[S_CS]=0x1000; m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=DS_SEG;
    m.cpu.r[R_SP]=STACK; m.cpu.r[R_BP]=0xBEEF; m.cpu.r[R_SI]=0xABCD;
    m.cpu.ip=0x1AB6; m.cpu.icount=1000; m.cpu.stop_at=1000+budget;
    fixes_enable("all",0); fixes_enable("Q19",fixed);
    recomp_override_enable("Q19TEST",1);
    recomp_module_load(NULL,&m,"START.EXE",file,len,MODLOAD_EXEC,0x1000,0x1000);
    CHECK(test_load_generated_image(m.mem,MEM_SIZE,mod,0x1000));
    memcpy(m.mem+((uint32_t)DS_SEG<<4)+GRID,grid,256);
    seg_write16(&m.cpu,DS_SEG,0xCAC4,0x1234);
    m.trap_on=1; m.trap_cs=0x1000; seg_write16(&m.cpu,DS_SEG,STACK,0xFF00); m.trap_ip=0xFF00; m.trap_sp=(uint16_t)(STACK+2);
    draws=captions=caption_kind=0;
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
    if(argc!=2) { fprintf(stderr,"usage: check_q19 path/to/START.EXE\n"); return 2; }
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
        {"Q19TEST","START.EXE",START_47304,0,0x3693,sprite,"observed test sprite"},
        {"Q19TEST","START.EXE",START_47304,0,0x1B37,caption,"observed test caption"}};
    for(unsigned k=0;k<sizeof stubs/sizeof stubs[0];k++) CHECK(recomp_override_add(&stubs[k])>=0);
    /* Grids: none; one cell; two; a cell at the origin; a scatter like a theatre's. Other flag bits are noise. */
    static uint8_t grids[6][256];
    memset(grids,0,sizeof grids);
    grids[1][2+16*3]=0x10;
    grids[2][1+16*1]=0x10; grids[2][5+16*9]=0x10;
    grids[3][0]=0x10;
    const unsigned scatter[][2]={{0,1},{1,4},{2,2},{3,7},{4,0},{5,5},{6,3},{7,9},{8,2},{9,8},{10,4},{12,1},{13,13},{15,15}};
    for(unsigned k=0;k<sizeof scatter/sizeof scatter[0];k++) grids[4][scatter[k][0]+16*scatter[k][1]]=0x10;
    for(unsigned k=0;k<256;k++) grids[5][k]=(k%3==0)?0x01:0x0F;      /* no unit bit: nothing to draw */
    grids[5][100]=0x1F;
    unsigned checks=0;
    for(unsigned g=0;g<6;g++) for(int fixed=0;fixed<2;fixed++) {
        unsigned mx[512],my[512];
        const unsigned want=model(grids[g],fixed,mx,my);
        cpu_t reference={0};
        for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
            prepare(mod,file,(size_t)len,grids[g],fixed,engine,400000);
            CHECK(run(&m)==RUN_TRAP);
            if(getenv("Q19_TRACE")) fprintf(stderr,"grid %u fixed %d engine %d: draws %d want %u ic %llu ip %04X sp %04X\n",g,fixed,engine,draws,want,(unsigned long long)(m.cpu.icount-1000),m.cpu.ip,m.cpu.r[R_SP]);
            CHECK((unsigned)draws==want);
            for(unsigned k=0;k<want&&k<512;k++) CHECK(xs[k]==mx[k] && ys[k]==my[k]);
            CHECK(captions==1 && caption_kind==(want?0x65:0x64));
            if(engine==ENGINE_INTERP) { reference=m.cpu; memcpy(saved,m.mem,MEM_SIZE); }
            else equal(&reference,saved);
            checks++;
        }
    }
    /* The fixed overlay draws every troop cell: its count equals the cells in the grid. */
    for(unsigned g=1;g<5;g++) {
        unsigned cells=0; for(unsigned k=0;k<256;k++) if(grids[g][k]&0x10) cells++;
        unsigned mx[512],my[512];
        CHECK(model(grids[g],1,mx,my)==cells);
    }
    /* A stop at every instruction count of a short run, then resume. */
    for(int fixed=0;fixed<2;fixed++) for(int engine=ENGINE_INTERP;engine<=ENGINE_RECOMP;engine++) {
        prepare(mod,file,(size_t)len,grids[2],fixed,engine,400000);
        CHECK(run(&m)==RUN_TRAP);
        const uint64_t total=m.cpu.icount-1000; cpu_t whole=m.cpu; memcpy(saved,m.mem,MEM_SIZE);
        const int whole_draws=draws;
        for(unsigned limit=1;limit<total;limit+=(total>3000?7:1)) {
            prepare(mod,file,(size_t)len,grids[2],fixed,engine,limit); run(&m);
            m.cpu.stop_at=1000+400000;
            CHECK(run(&m)==RUN_TRAP);
            CHECK(draws==whole_draws);
            equal(&whole,saved);
            checks++;
        }
    }
    recomp_shutdown(&m); free(m.mem); free(file); free(saved);
    printf("Q19: %u staged runs; both states/engines; troop markers match the model; stop and resume; %d failures\n",checks,failures);
    return failures?1:0;
}
