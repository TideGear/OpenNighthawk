/* Orthographic map geometry and its round trip through an unseeded art page. */
#include "drawlist.h"
#include "hires.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static drawlist d, clone;
static hires h;
static int failed;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);failed++; } } while(0)
static void record(char kind,const int32_t *v,int n)
{
    hires_record(&h,&d,kind,v,n);drawlist_record(&d,kind,v,n);
}

static void triangle(int scale)
{
    static const uint8_t blank[65536]={0};
    const int xy[3][2]={{20,20},{80,40},{20,60}};
    const int32_t fill[12]={0xFF07,10,10,90,70,0,0,0,0,0,0,0};
    drawlist_init(&d);drawlist_init(&clone);drawlist_set_scale(&d,scale);hires_init(&h,scale);
    drawlist_seed(&d,'Z',0x1000,0,blank,sizeof blank);
    record('F',fill,12);
    for(int k=0;k<3;k++) {
        int32_t g[18]={24*k,8*k,8*((k+1)%3),0,0};
        for(int end=0;end<2;end++) {
            const int vertex=(k+end)%3,at=5+6*end;
            g[at]=xy[vertex][0]*2097152-524288;
            g[at+1]=xy[vertex][1]*2097152-524288;
            g[at+3]=xy[vertex][0];g[at+4]=xy[vertex][1];
        }
        g[17]=21;record('G',g,18);
        const int32_t e[6]={24*k,xy[k][0],xy[k][1],xy[(k+1)%3][0],xy[(k+1)%3][1],0};
        record('E',e,6);
    }
    record('F',fill,12);
    int32_t rows[3+82+1]={0xFF07,20,41};
    for(int k=0;k<41;k++){rows[3+2*k]=20;rows[4+2*k]=80-3*abs(k-20);}
    rows[85]=0xFF07;record('R',rows,86);
    for(int y=20;y<=60;y++) {
        const int32_t b[5]={y,20,rows[4+2*(y-20)]-19,0x1000,y*320+20};record('b',b,5);
    }
    CHECK(h.stats[HS_REFILLED]==1);
    drawlist_page *p=drawlist_page_of(&d,0x1000);
    if(scale==1)CHECK(!memcmp(p->hi,p->b,64000));
    else {
        /* The upper right edge is x=20.25+3*(y-20.25). Every fine row
         * follows that line: clipping to the coarse footprint leaves teeth. */
        for(int y=25*scale;y<35*scale;y++) {
            int last=-1;for(int x=0;x<100*scale;x++)if(p->hi[y*320*scale+x]==7)last=x;
            const double edge=(20.25+3*((y+0.5)/scale-20.25))*scale;
            CHECK(last>=0 && fabs(last+0.5-edge)<=2.01);
        }
        /* The lower vertex belongs to cell (20,60), even though the
         * orthographic projection rounded it up from 19.75,59.75. */
        int vertex=0;
        for(int j=0;j<scale;j++)for(int i=0;i<scale;i++)
            vertex+=p->hi[(60*scale+j)*320*scale+20*scale+i]==7;
        CHECK(vertex>0);
    }
    const int w=61,ht=41;
    const size_t bytes=(size_t)w*ht*scale*scale;
    uint8_t *expected=malloc(bytes);
    int32_t back[10+61*41]={1,100,100,2,120,80,61,41,0x900,0x1000};
    for(int y=0;y<ht;y++) {
        for(int x=0;x<w;x++)back[10+y*w+x]=p->b[(20+y)*320+20+x];
        for(int j=0;j<scale;j++)memcpy(expected+((size_t)y*scale+j)*w*scale,
            p->hi+((size_t)(20+y)*scale+j)*320*scale+20*scale,(size_t)w*scale);
    }
    const int32_t out[10]={1,20,20,2,100,100,61,41,0x1000,0x900};
    record('C',out,10);
    drawlist_copy(&clone,&d);
    if(scale>1)CHECK(clone.overlay[0].hi!=d.overlay[0].hi);
    drawlist_free(&d); /* the saved fine rows are owned by the clone */
    drawlist_record(&clone,'C',back,10+w*ht);
    p=drawlist_page_of(&clone,0x1000);
    for(int y=0;y<ht*scale;y++)CHECK(!memcmp(p->hi+((size_t)80*scale+y)*320*scale+120*scale,
        expected+(size_t)y*w*scale,(size_t)w*scale));
    /* A duplicate display byte must preserve the carried fine block;
     * a different byte is still an ordinary coarse write. */
    drawlist_seed(&clone,'Y',0xA000,0,blank,64000);
    back[9]=0xA000;drawlist_record(&clone,'C',back,10+w*ht);
    p=drawlist_page_of(&clone,0xA000);
    const unsigned a=80*320+120;
    int32_t change[6]={42,0xA000,1,(int32_t)a,p->b[a],0};
    uint8_t *before=malloc((size_t)64000*scale*scale);memcpy(before,p->hi,(size_t)64000*scale*scale);
    drawlist_record(&clone,'x',change,6);CHECK(!memcmp(before,p->hi,(size_t)64000*scale*scale));
    change[4]=9;drawlist_record(&clone,'x',change,6);
    for(int j=0;j<scale;j++)for(int i=0;i<scale;i++)CHECK(p->hi[(80*scale+j)*320*scale+120*scale+i]==9);
    /* Stale coarse bytes invalidate a cached rectangle. */
    back[10]=11;drawlist_record(&clone,'C',back,10+w*ht);
    for(int j=0;j<scale;j++)for(int i=0;i<scale;i++)CHECK(p->hi[(80*scale+j)*320*scale+120*scale+i]==11);
    free(before);free(expected);drawlist_free(&clone);
}
int main(void)
{
    triangle(1);triangle(4);triangle(9);
    if(!failed)puts("map: centred orthographic vertices, straight triangle edges, fine page round trip and stale-byte fallback");
    return failed?1:0;
}
