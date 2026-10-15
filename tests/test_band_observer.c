/* An observer must not charge VGA bus cycles or mutate guest state, in either
 * timing profile. Exercise byte fields and wrapped guest stack/view offsets. */
#include "observe.h"
#include "band_state.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static machine_t m,before;
static uint8_t memory[MEM_SIZE],saved[MEM_SIZE];
static int32_t record[BAND_REC_BASE],reference[BAND_REC_BASE];
static int calls,nrecord;
static uint64_t clock_seen;
static void capture(void *user,uint64_t clock,char kind,const int32_t *v,int n)
{
    (void)user;if(kind!='o')return;
    calls++;clock_seen=clock;nrecord=n;
    if(n==BAND_REC_BASE)memcpy(record,v,sizeof record);
}
static void put16(uint16_t seg,uint16_t off,uint16_t value)
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
        m.cpu.seg[S_DS]=0xA000;m.cpu.seg[S_SS]=0x9000;m.cpu.r[R_SP]=0xFFFE;
        put16(0x9000,0,0xFFFE);put16(0xA000,2,0xABCD);put16(0xA000,0x990E,0x1203);
        put16(0xA000,0x438E,0x5678);
        memcpy(&before,&m,sizeof m);memcpy(saved,memory,sizeof memory);
        calls=0;obs.bands=0;observe_set(&obs);observe_game_draw(&m);
        CHECK(calls==0 && !memcmp(&m,&before,sizeof m) && !memcmp(memory,saved,sizeof memory));
        obs.bands=1;observe_game_draw(&m);
        CHECK(calls==1 && nrecord==BAND_REC_BASE && clock_seen==123456);
        CHECK(record[0]==BAND_REC_VERSION && record[1]==0xFFFE && record[2]==0xABCD);
        CHECK(record[3]==3 && record[4]==0x5678);
        CHECK(!memcmp(&m,&before,sizeof m) && !memcmp(memory,saved,sizeof memory));
        if(profile)CHECK(!memcmp(record,reference,sizeof reference));else memcpy(reference,record,sizeof record);
    }
    observe_set(NULL);
    puts("band observer: identical records, no guest writes or clock/bus changes in default and 386 profiles pass");
    return 0;
}
