/* Keep the original bitmap, its transparent cells and coarse replay while
 * moving its top-left on the fine grid. Heading changes at the logic frame. */
#include "drawlist.h"
#include "hud_overlay.h"
#include "hud_state.h"
#include "interp.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static drawlist d;
static int failed;
#define CHECK(x) do { if (!(x)) { if(failed<10)fprintf(stderr,"line %d: %s\n",__LINE__,#x);failed++; } } while(0)
static void state(int32_t m[HUD_MAP_N],int heading,int dx)
{
    memset(m,0,HUD_MAP_N*sizeof *m);
    m[0]=HUD_MAP_VERSION;m[1]=40+dx;m[2]=80;
    m[3]=(40+dx)*32+8;m[5]=((32768-80)*32-8)&0xFFFF;m[6]=((32768-80)*32-8)>>16;
    m[9]=6;m[10]=8192*heading;m[11]=112+((40+dx)>>4);m[12]=142;
    m[13]=256+4*heading;
    /* Every heading has different art; zero cells must remain transparent. */
    for(int k=0;k<16;k++)m[14+k]=k%3 ? 1+heading*16+k : 0;
}
static void sprite(const int32_t *m,int32_t s[30])
{
    memset(s,0,30*sizeof *s);s[0]=73;s[1]=0x1000;
    s[2]=0x2000;s[3]=m[13];s[4]=4;s[5]=1;
    s[6]=m[11]-1;s[7]=m[12]-1;s[8]=s[9]=4;
    for(int k=0;k<16;k++)s[14+k]=m[14+k];
}
static void picture(int N,const int32_t *m,int nm,const int32_t *s,const uint8_t art[16],double x,double y)
{
    static uint8_t blank[65536];memset(blank,200,sizeof blank);
    drawlist_init(&d);drawlist_set_scale(&d,N);drawlist_seed(&d,'Z',0x1000,0,blank,sizeof blank);
    drawlist_record(&d,'m',m,nm);drawlist_record(&d,'S',s,30);
    drawlist_page *p=drawlist_page_of(&d,0x1000);
    const int X=(int)floor(x*N+0.5),Y=(int)floor(y*N+0.5),W=320*N;
    for(int yy=Y-1;yy<Y+4*N+1;yy++)for(int xx=X-1;xx<X+4*N+1;xx++) {
        const int rx=xx-X,ry=yy-Y;
        const int a=rx>=0&&ry>=0&&rx<4*N&&ry<4*N ? art[(ry/N)*4+rx/N] : 0;
        CHECK(p->hi[yy*W+xx]==(a ? a : 200));
    }
    /* Coarse bytes still describe the original blit, even for a held sprite. */
    for(int k=0;k<16;k++)CHECK(p->b[(s[7]+k/4)*320+s[6]+k%4]==(s[14+k] ? s[14+k] : 200));
    drawlist_free(&d);
}
int main(void)
{
    int32_t a[HUD_MAP_N],b[HUD_MAP_N],sa[30],sb[30];
    for(int heading=0;heading<8;heading++) {
        state(a,heading,0);sprite(a,sa);
        uint8_t art[16];for(int k=0;k<16;k++)art[k]=(uint8_t)a[14+k];
        picture(1,a,HUD_MAP_N,sa,art,113,141);
        picture(4,a,HUD_MAP_N,sa,art,113.515625,141.76171875);
        picture(9,a,HUD_MAP_N,sa,art,113.515625,141.76171875);
        a[7]=65500;a[11]=116;sprite(a,sa);
        picture(9,a,HUD_MAP_N,sa,art,115.765625,141.76171875);
        state(a,heading,0);sprite(a,sa);
        state(b,(heading+1)%8,16);sprite(b,sb);
        interp_rec ar[2]={{'m',0,a,HUD_MAP_N},{'S',0,sa,30}};
        interp_rec br[2]={{'m',1,b,HUD_MAP_N},{'S',1,sb,30}};
        interp_frame *A=interp_parse(ar,2),*B=interp_parse(br,2);interp_pairing *P=interp_pair(A,B);
        interp_list out={0};int skeleton;
        const double t[3]={0.25,0.5,0.75};
        for(int k=0;k<3;k++) {
            interp_inbetween_ex(A,B,P,t[k],INTERP_HUD|INTERP_HUD_HOLD|INTERP_BANDS,&out,&skeleton,NULL);
            CHECK(out.n==2);
            picture(9,out.rec[0].v,out.rec[0].n,out.rec[1].v,art,113.515625+t[k],141.76171875);
        }
        interp_inbetween_ex(A,B,P,1,INTERP_HUD|INTERP_HUD_HOLD,&out,&skeleton,NULL);
        for(int k=0;k<16;k++)art[k]=(uint8_t)b[14+k];
        picture(9,out.rec[0].v,out.rec[0].n,out.rec[1].v,art,114.515625,141.76171875);
        interp_list_free(&out);interp_pairing_free(P);interp_frame_free(A);interp_frame_free(B);
    }
    if(!failed)puts("map marker: eight original bitmaps, transparency, exact coarse replay, fine movement and held heading pass");
    return failed!=0;
}
