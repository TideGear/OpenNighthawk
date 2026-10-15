/* Complete model inputs must be passive even when DS is VGA memory and
 * model/stack reads cross physical or logical segment boundaries. */
#include "observe.h"
#include "scene_capture.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;} } while(0)
static machine_t m,before;
static uint8_t memory[MEM_SIZE],saved[MEM_SIZE],pages[3][65536],reference[3][65536];
static int32_t request[SCENE_REQUEST_N],refrequest[SCENE_REQUEST_N];
static unsigned bytes[3],calls,wants;
static int accept,bad;
static int want(void *u,uint64_t clock){(void)u;wants++;if(clock!=123456)bad=1;return accept;}
static void capture(void *u,uint64_t clock,char kind,const int32_t *v,int n)
{
    (void)u;calls++;if(clock!=123456)bad=1;
    if(kind==SCENE_REQUEST_KIND&&n==SCENE_REQUEST_N){memcpy(request,v,sizeof request);return;}
    if(kind!=SCENE_MEMORY_KIND||n<SCENE_MEMORY_HEAD||v[0]!=SCENE_CAPTURE_VERSION||v[1]<0||v[1]>2||v[4]<=0||v[4]>SCENE_CHUNK_BYTES||n!=SCENE_MEMORY_HEAD+v[4]/2){bad=1;return;}
    const int role=v[1];
    for(int k=0;k<v[4]/2;k++) {
        const uint16_t at=(uint16_t)(v[3]+2*k),word=(uint16_t)v[SCENE_MEMORY_HEAD+k];
        pages[role][at]=(uint8_t)word;pages[role][(uint16_t)(at+1)]=(uint8_t)(word>>8);
    }
    bytes[role]+=v[4];
}
static void word(uint16_t seg,uint16_t at,uint16_t v)
{
    memory[phys(seg,at)]=(uint8_t)v;memory[phys(seg,(uint16_t)(at+1))]=(uint8_t)(v>>8);
}
int main(void)
{
    f117_observer obs={0};obs.prim=capture;obs.want_scene=want;
    for(int profile=0;profile<2;profile++) {
        memset(&m,0,sizeof m);for(unsigned i=0;i<MEM_SIZE;i++)memory[i]=(uint8_t)((i*37)^(i>>8));
        m.cpu.mem=memory;m.cpu.t386=profile;m.cpu.icount=123456;m.cpu.stop_at=123500;
        m.cpu.seg[S_DS]=0xA000;m.cpu.seg[S_SS]=0x9000;m.cpu.seg[S_CS]=0x1234;m.cpu.seg[S_ES]=0x5678;m.cpu.ip=0xCF;
        m.cpu.r[R_SP]=0xFFFC;m.cpu.r[R_BP]=0x123;m.cpu.flags=0xF202;
        word(0xA000,0x8590,0xFFFF);word(0xFFFF,0,3);
        const uint16_t stack[]={0x1111,0x2222,1,0xFFFE,0x7FFF,0x8000,0xABCD,0xFFFC};
        for(int k=0;k<8;k++)word(0x9000,(uint16_t)(0xFFFC+2*k),stack[k]);
        word(0xA000,0x7D8E,0xAB17);
        memcpy(&before,&m,sizeof m);memcpy(saved,memory,sizeof memory);calls=wants=bad=0;
        observe_set(&obs);obs.scene=0;accept=1;observe_scene_model(&m);CHECK(!calls&&!wants);
        obs.scene=1;accept=0;observe_scene_model(&m);CHECK(!calls&&wants==1);
        accept=1;memset(bytes,0,sizeof bytes);observe_scene_model(&m);
        CHECK(calls==258&&wants==2&&!bad&&bytes[0]==65536&&bytes[1]==65536&&bytes[2]==SCENE_STACK_BYTES);
        CHECK(request[0]==SCENE_CAPTURE_VERSION&&request[3]==0xFFFC&&request[6]==0x1111&&request[7]==0x2222);
        CHECK(request[8]==0xFFFF&&request[9]==3&&request[10]==1);
        for(int k=0;k<5;k++)CHECK(request[11+k]==stack[3+k]);
        CHECK(request[16]==0x17&&request[23]==0x5678&&request[24+R_BP]==0x123&&request[32]==0xF202);
        for(unsigned at=0;at<65536;at++) {
            CHECK(pages[0][at]==saved[phys(0xA000,(uint16_t)at)]);
            CHECK(pages[1][at]==saved[phys(0xFFFF,(uint16_t)at)]);
        }
        for(unsigned k=0;k<SCENE_STACK_BYTES;k++)CHECK(pages[2][(uint16_t)(0xFFFC+k)]==saved[phys(0x9000,(uint16_t)(0xFFFC+k))]);
        CHECK(!memcmp(&m,&before,sizeof m)&&!memcmp(memory,saved,sizeof memory));
        if(profile)CHECK(!memcmp(request,refrequest,sizeof request)&&!memcmp(pages,reference,sizeof pages));
        else {memcpy(refrequest,request,sizeof request);memcpy(reference,pages,sizeof pages);}
    }
    observe_set(NULL);puts("scene observer: complete bytes, original arguments, bounded opt-in and unchanged guest in both profiles pass");return 0;
}
