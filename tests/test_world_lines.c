/* Geometry-backed outlines and lights keep the coarse replay exact while their
 * fine positions retain projection fractions. Stale and clipped slots decline. */
#include "hires.h"
#include "interp.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static drawlist d, coarse;
static hires h;
static const uint16_t SEG=0x1000;
static const uint8_t blank[65536]={0};

static void seed(int n)
{
    drawlist_reset(&d);drawlist_set_scale(&d,n);hires_init(&h,n);
    drawlist_seed(&d,'Z',SEG,0,blank,sizeof blank);
}
static void feed(char kind,const int32_t *v,int n)
{
    hires_record(&h,&d,kind,v,n);drawlist_record(&d,kind,v,n);
}
static void point(int32_t g[18],int32_t l[10],int x)
{
    memset(g,0,18*sizeof *g);memset(l,0,10*sizeof *l);
    g[0]=24;g[1]=g[2]=0xD6B4;g[3]=159;g[4]=52;
    for(int at=5;at<=11;at+=6) {g[at]=x;g[at+1]=0;g[at+2]=0x1000000;g[at+3]=(x>>8)/256;}
    l[0]=24;l[1]=l[3]=159+g[8];l[2]=l[4]=52;l[5]=7;l[6]=SEG;l[8]=320;l[9]=0x046F;
}
static size_t area(const drawlist_page *p)
{
    size_t a=0;for(size_t i=0;i<(size_t)64000*p->n*p->n;i++)a+=p->hi[i]==7;return a;
}
int main(void)
{
    drawlist_init(&d);drawlist_init(&coarse);
    int32_t g[18],l[10];point(g,l,16384);
    double xy[4];CHECK(hires_outline_points(g,18,l,10,xy));CHECK(xy[0]==159.75 && xy[1]==52.5);
    /* Crossing a negative integer must stay continuous, for both sides of
     * the view and both axes. Positive modulo made the middle sample jump. */
    double positions[3];
    for(int k=0;k<3;k++) {
        point(g,l,-65536-256+k*256);
        CHECK(hires_outline_points(g,18,l,10,xy));positions[k]=xy[0];
    }
    CHECK(fabs(positions[2]-2*positions[1]+positions[0])<1e-12);
    CHECK(fabs(positions[2]-positions[0]-2.0/256)<1e-12);
    /* Crossing an intermediate depth-shift boundary must be smooth too. */
    for(int k=0;k<3;k++) {
        point(g,l,0x800000);
        for(int at=5;at<=11;at+=6) {
            g[at+2]=0x1010000-1+k;
            g[at+3]=(g[at]>>8)/(g[at+2]>>16);
        }
        l[1]=l[3]=159+g[8];
        CHECK(hires_outline_points(g,18,l,10,xy));positions[k]=xy[0];
    }
    CHECK(fabs(positions[2]-2*positions[1]+positions[0])<1e-9);
    CHECK(fabs(positions[2]-positions[0])<0.0001);
    point(g,l,16384);
    int32_t wrong[10];memcpy(wrong,l,sizeof wrong);wrong[1]++;
    CHECK(!hires_outline_points(g,18,wrong,10,xy));
    g[10]=1;CHECK(!hires_outline_points(g,18,l,10,xy));g[10]=0;
    CHECK(!hires_outline_points(g,3,l,10,xy));
    for(int n=1;n<=9;n+= n==1?3:5) {
        seed(n);feed('G',g,18);feed('L',l,10);
        drawlist_reset(&coarse);drawlist_seed(&coarse,'Z',SEG,0,blank,sizeof blank);drawlist_record(&coarse,'L',l,10);
        const drawlist_page *p=drawlist_get(&d,SEG);
        CHECK(!memcmp(p->b,drawlist_get(&coarse,SEG)->b,65536));
        CHECK(h.stats[HS_OUTLINES_REFINED]==(n>1));
        if(n==1) CHECK(!memcmp(p->hi,p->b,64000));
        else {
            CHECK(area(p)==(size_t)n*n);
            uint64_t flat=0,agree=0;CHECK(hires_guard_masked(p->b,p->hi,n,p->strokes,&flat,&agree)==0);
        }
    }
    seed(4);feed('G',g,18);feed('L',wrong,10);CHECK(h.stats[HS_OUTLINES_REFINED]==0);
    point(g,l,16384);l[7]=63999;l[8]=64319;seed(4);feed('G',g,18);feed('L',l,10);CHECK(h.stats[HS_OUTLINES_REFINED]==0);

    /* Actual camera interpolation, including a dot whose rounded position is
     * unchanged for the entire pair. Legacy coordinates remain untouched. */
    int32_t ga[18],gb[18],la[10],lb[10];point(ga,la,16384);point(gb,lb,49152);
    int32_t va[8]={16384,0,0x1000000,0,0,0,0xC6B4,0xD6B4};
    int32_t vb[8]={49152,0,0x1000000,0,0,0,0xC6B4,0xD6B4};
    interp_rec ar[]={{'V',0,va,8},{'G',0,ga,18},{'L',0,la,10}};
    interp_rec br[]={{'V',1,vb,8},{'G',1,gb,18},{'L',1,lb,10}};
    interp_frame *a=interp_parse(ar,3),*b=interp_parse(br,3);
    interp_pairing *pair=interp_pair(a,b);interp_list list={0};int sb;
    double last=0;
    for(int step=0;step<=4;step++) {
        const double t=step/4.0;
        interp_inbetween_ex(a,b,pair,t,INTERP_VERTICES,&list,&sb,NULL);
        const interp_rec *lr=&list.rec[2];CHECK(lr->kind=='L');CHECK(!memcmp(lr->v,la,sizeof la));
        if(step==0 || step==4) CHECK(lr->n==10);
        else CHECK(lr->n==28);
        seed(9);
        for(int k=0;k<list.n;k++)feed(list.rec[k].kind,list.rec[k].v,list.rec[k].n);
        CHECK(h.stats[HS_OUTLINES_REFINED]==1);
        const int32_t *gg=step==0?ga:step==4?gb:lr->v+10;
        CHECK(hires_outline_points(gg,18,la,10,xy));
        CHECK(fabs(xy[0]-(159.75+t*0.5))<1e-12);
        if(step)CHECK(fabs(xy[0]-last-0.125)<1e-12);last=xy[0];
    }
    /* Reused edge slots with different vertex identities must not invent motion. */
    gb[1]+=8;
    interp_inbetween_ex(a,b,pair,0.25,INTERP_VERTICES,&list,&sb,NULL);
    CHECK(list.rec[2].n==10);
    gb[1]-=8;
    interp_list_free(&list);interp_pairing_free(pair);interp_frame_free(a);interp_frame_free(b);
    /* Equal-length strokes have equal area regardless of direction, within
     * raster sampling. A square nib would make the diagonal ~40% thicker. */
    seed(4);drawlist_page *p=drawlist_page_of(&d,SEG);
    drawlist_stroke(p,100,100,300,100,9,7,0,0,1280,800);size_t horizontal=area(p);
    seed(4);p=drawlist_page_of(&d,SEG);
    drawlist_stroke(p,100,100,100+200/sqrt(2.0),100+200/sqrt(2.0),9,7,0,0,1280,800);
    CHECK(fabs((double)area(p)/horizontal-1)<0.025);
    drawlist_free(&d);drawlist_free(&coarse);
    puts("projected outlines and dots: exact coarse replay, smooth fine motion, stale-slot rejection and perpendicular widths pass");
    return 0;
}
