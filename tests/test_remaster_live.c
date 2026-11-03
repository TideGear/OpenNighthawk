/* Complete frames own cleared terrain footprints, but never HUD/artwork.
 * Invalid model captures and unsupported backgrounds retain fallback pixels. */
#include "remaster_live.h"
#include "diagtag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static void put(uint8_t *p, unsigned at, uint32_t value, int bytes)
{ for (int k=0;k<bytes;k++) p[(uint16_t)(at+k)] = (uint8_t)(value>>(8*k)); }
static void word(uint8_t *p, unsigned *at, int v)
{ put(p,*at,(uint16_t)v,2); *at+=2; }

int main(void)
{
    machine_t *m=calloc(1,sizeof *m);
    CHECK(m); m->mem=calloc(1,1048576); CHECK(m->mem);
    m->cpu.seg[S_DS]=0x4000; m->cpu.seg[S_SS]=0x5000; m->cpu.r[R_SP]=0x100;
    uint8_t *ds=m->mem+0x40000, *bank=m->mem+0x20000, *stack=m->mem+0x50000;
    put(bank,0,1,2);put(bank,2,0x100,2);
    unsigned at=0x100; word(bank,&at,5);word(bank,&at,0x200);word(bank,&at,32769);word(bank,&at,0x300);
    at=0x300;word(bank,&at,-1);for(int k=0;k<4;k++)word(bank,&at,0);bank[at]=255;
    at=0x200;word(bank,&at,-1);word(bank,&at,0);word(bank,&at,-2);word(bank,&at,0);
    bank[at++]=5;bank[at++]=7;word(bank,&at,1);bank[at++]=5;bank[at++]=7;
    const uint8_t material[]={0x41,1,0,0x21,2,7,255};memcpy(bank+at,material,sizeof material);
    put(ds,0x7D50,0x1000,2);put(ds,0x7D4E,0x9000,2);put(ds,0x7D80,0x9100,2);
    /* Shared vertices behind the eye: successful capture, no visible surface. */
    for(int id=5;id<=7;id+=2)put(ds,0x4EE6+16*id+8,(uint32_t)-65536,4);
    for(int i=0;i<256;i++)put(ds,0x9100+2*i,0xFF00+i,2);
    put(ds,0x8590,0x2000,2);put(ds,0x8602,160,2);put(ds,0x8604,100,2);
    put(ds,0x85FE,319,2);put(ds,0x8600,199,2);
    ds[0x990E]=3;put(ds,0x438E,1,2);put(ds,0x43E0,1,2);
    put(ds,0x49B8,32767,2);put(ds,0x49BC,32767,2);
    put(ds,0x0D1E,160,2);put(ds,0x0D20,100,2);put(ds,0x2293,319,2);put(ds,0x2295,199,2);
    for(int i=0;i<256;i++) {m->dac[i*3]=17;m->dac[i*3+1]=16;m->dac[i*3+2]=13;}
    uint8_t *src=malloc(64000), *tags=malloc(64000), *dst=malloc(64000), *mask=malloc(64000);
    uint32_t *rgb=malloc(64000*4), *wide=malloc(400*200*4);
    CHECK(src && tags && dst && mask && rgb && wide);
    memset(src,7,64000);memset(tags,DT_MODEL_SOLID,64000);
    const int terrain=150*320+120, hud=150*320+121, art=150*320+122;
    tags[hud]=DT_HUD_FINE;tags[art]=DT_SPRITE;
    for(int scenario=0;scenario<3;scenario++) {
        remaster_live *r=remaster_live_new(320,200,~0u,NULL); CHECK(r);
        ds[0x990E]=(uint8_t)(scenario==2 ? 2 : 3);
        remaster_live_exec(r,m,2); /* start a complete frame */
        put(stack,0x104,0,2);remaster_live_exec(r,m,0);remaster_live_exec(r,m,1);
        CHECK(remaster_live_prims(r)==2);
        if(scenario==1) { /* one unreadable request poisons completeness */
            put(stack,0x104,9,2);remaster_live_exec(r,m,0);remaster_live_exec(r,m,1);
        }
        remaster_live_exec(r,m,2);
        CHECK(remaster_live_compose(r,m,1,src,tags,m->dac,dst,rgb,mask,400,wide)==(scenario==0));
        CHECK(mask[terrain]==(scenario==0));
        CHECK(!mask[hud] && !mask[art] && dst[hud]==7 && dst[art]==7);
        if(scenario==0) {
            CHECK(rgb[terrain]==0xFF454134u);
            CHECK(wide[150*400+40+120]==wide[150*400+20]);
        }
        remaster_live_free(r);
    }
    free(src);free(tags);free(dst);free(mask);free(rgb);free(wide);free(m->mem);free(m);
    puts("remaster live background ownership passes"); return 0;
}
