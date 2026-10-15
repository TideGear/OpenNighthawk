/* Observer reads must leave the entire guest unchanged, including VGA bus
 * timing. Check the panel extension, wrapped stack and every marker byte. */
#include "observe.h"
#include "hud_state.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1; } } while(0)
static machine_t m,before;
static uint8_t memory[MEM_SIZE],saved[MEM_SIZE];
static int32_t panel[HUD_PANEL_MAX],marker[HUD_MAP_N],refp[HUD_PANEL_MAX],refm[HUD_MAP_N];
static int calls,np,nm;
static uint64_t clock_seen;
static void capture(void *user,uint64_t clock,char kind,const int32_t *v,int n)
{
    (void)user;calls++;clock_seen=clock;
    if(kind=='p'&&n<=HUD_PANEL_MAX) { np=n;memcpy(panel,v,n*sizeof *v); }
    if(kind=='m'&&n==HUD_MAP_N) { nm=n;memcpy(marker,v,n*sizeof *v); }
}
static void word(uint16_t seg,uint16_t off,uint16_t value)
{
    memory[phys(seg,off)]=(uint8_t)value;
    memory[phys(seg,(uint16_t)(off+1))]=(uint8_t)(value>>8);
}
int main(void)
{
    f117_observer obs={0};obs.prim=capture;
    for(int profile=0;profile<2;profile++) {
        memset(&m,0,sizeof m);
        for(unsigned i=0;i<MEM_SIZE;i++)memory[i]=(uint8_t)((i*37)^(i>>8));
        m.cpu.mem=memory;m.cpu.t386=profile;m.cpu.icount=123456;m.cpu.stop_at=123500;
        m.cpu.seg[S_DS]=m.cpu.seg[S_SS]=0xA000;m.cpu.r[R_BP]=2;
        word(0xA000,0,123);word(0xA000,0xFFFE,145);
        word(0xA000,0x2DEE,0x6000);word(0xA000,0x028A,0x8000);word(0xA000,0xE584,0x7000);
        word(0xA000,0x9924,0xFFF0);word(0xA000,0x9926,0x9000);
        word(0x9000,0x30,321);word(0x9000,0x32,654);
        for(int y=0;y<4;y++) {
            word(0x8000,(uint16_t)(12+2*y),(uint16_t)(320*(4+y)));
            for(int x=0;x<4;x++)memory[phys(0x7000,(uint16_t)(320*(4+y)+268+x))]=(uint8_t)(y*4+x);
        }
        memcpy(&before,&m,sizeof m);memcpy(saved,memory,sizeof memory);
        calls=np=nm=0;observe_set(&obs);observe_panel_pass(&m);observe_map_marker(&m);
        CHECK(!calls&&!memcmp(&m,&before,sizeof m)&&!memcmp(memory,saved,sizeof memory));
        obs.hud=1;observe_panel_pass(&m);observe_map_marker(&m);
        CHECK(calls==2&&np>HUD_PANEL_N&&nm==HUD_MAP_N&&clock_seen==123456);
        CHECK(panel[HUD_PANEL_N]==HUD_PANEL_EXT_MAGIC&&panel[HUD_PANEL_N+1]==321&&panel[HUD_PANEL_N+2]==654);
        CHECK(marker[11]==123&&marker[12]==145&&marker[13]==268);
        for(int k=0;k<16;k++)CHECK(marker[14+k]==k);
        CHECK(!memcmp(&m,&before,sizeof m)&&!memcmp(memory,saved,sizeof memory));
        if(profile)CHECK(!memcmp(panel,refp,np*sizeof *panel)&&!memcmp(marker,refm,sizeof marker));
        else { memcpy(refp,panel,np*sizeof *panel);memcpy(refm,marker,sizeof marker); }
        obs.hud=0;
    }
    observe_set(NULL);
    puts("HUD/map observers: original sprite bytes, wrapped pointers, identical default/386 records and unchanged guest pass");
    return 0;
}
