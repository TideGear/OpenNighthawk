#include "hires.h"
#include "interp.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static hires h;
static drawlist d;
static int failed;
#define CHECK(v) do { if (!(v)) { fprintf(stderr,"line %d: %s\n",__LINE__,#v);failed++; } } while (0)

static void record(char kind, const int32_t *v, int n)
{
    hires_record(&h,&d,kind,v,n);
    drawlist_record(&d,kind,v,n);
}

static int g_fat;   /* pixels the footprint's rows are widened by on each side */
static void triangle(int scale, int legacy, int corrupt)
{
    static const uint8_t blank[65536] = {0};
    static const int32_t camera[3][3] = {
        { 0,0,0x20000 }, { 1024,1536,-0x10000 }, { -1024,1024,-0x10000 }
    };
    static const int pair[3][2] = { {0,1},{1,2},{2,0} };
    static const int32_t edges[3][8] = {
        {0,20,20,24,26,0x40,24,26}, {24,0,0,0,0,0x80,0,0}, {48,16,24,20,20,0x40,16,24}
    };
    drawlist_init(&d);drawlist_set_scale(&d,scale);hires_init(&h,scale);
    drawlist_seed(&d,'Z',0xA000,0,blank,sizeof blank);
    /* A capture starts inside a polygon; the first F deliberately primes it. */
    int32_t fill[12] = {0xFF07,0,0,319,199,0,0,0,0,0,0,2};
    record('F',fill,12);
    for (int edge = 0; edge < 3; edge++) {
        int32_t g[17] = {24*edge,0xD6B4+8*pair[edge][0],0xD6B4+8*pair[edge][1],20,20};
        for (int end = 0; end < 2; end++) {
            const int vertex = pair[edge][end], at = 5+6*end;
            memcpy(g+at,camera[vertex],sizeof camera[vertex]);
            g[at+3] = vertex ? INT32_MIN : 0;
            g[at+4] = 0;g[at+5] = vertex != 0;
        }
        record('G',g,legacy?3:17);
        int32_t e[8];memcpy(e,edges[edge],sizeof e);
        if (corrupt && edge==0) e[6]++;
        record('E',e,8);
    }
    record('F',fill,12);
    const int32_t join[8] = {72,24,26,16,24,0,0,0};record('E',join,8);
    /* Inclusive footprint of the triangle with the original's border pixels. */
    int32_t rows[18] = {0xFF07,20,7,20,20,19,21,18,22,17,22,16,23,20,24,24,24,0xFF07};
    for (int k=0;k<7;k++) { rows[3+2*k]-=g_fat; rows[4+2*k]+=g_fat; }
    record('R',rows,18);
    for (int y=20;y<=26;y++) {
        const int l=rows[3+2*(y-20)],r=rows[4+2*(y-20)];
        const int32_t before[5] = {y,l,r-l+1,0xA000,y*320+l};
        record('b',before,5);
    }
    record('B',fill,1);
    const drawlist_page *p=drawlist_page_of(&d,0xA000);
    CHECK(p && p->hi);
    if (!p || !p->hi) return;
    int detailed=0;
    for (int y=0;y<200;y++) for (int x=0;x<320;x++) {
        const int v=p->b[y*320+x];
        for (int j=0;j<scale;j++) for (int i=0;i<scale;i++) {
            const int q=p->hi[(y*scale+j)*320*scale+x*scale+i];
            CHECK(q==0 || q==7);
            /* geometry, not the footprint, bounds a near-clipped polygon; the footprint is its
             * coarse rendering, so no fine pixel lies more than a cell from a painted one */
            if (q) {
                int near_cell = 0;
                for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
                    const int cx=x+dx,cy=y+dy;
                    if (cx>=0 && cx<320 && cy>=0 && cy<200 && p->b[cy*320+cx]) near_cell=1;
                }
                CHECK(near_cell);
            }
            detailed += q!=v;
        }
    }
    if (g_fat) {
        /* A footprint the strict test (3 px) refuses is still drawn from the geometry at N > 1 when it lies
         * within the near-clip tolerance and the geometry spills nowhere; one beyond it keeps the scaled copy. */
        const int tolerated = scale>1 && g_fat<=16;
        CHECK(h.stats[HS_NEAR_TOLERATED]==(uint64_t)tolerated && h.stats[HS_REFILLED]==(uint64_t)tolerated);
        CHECK(h.stats[HS_BEYOND]==(uint64_t)(scale>1 && !tolerated));
        CHECK(tolerated ? detailed>0 : !detailed);
    } else if (legacy || corrupt) {
        CHECK(h.stats[HS_NEAR_CLIPPED]==1 && h.stats[HS_REFILLED]==0 && !detailed);
    } else {
        CHECK(h.stats[HS_NEAR_CLIPPED]==0 && h.stats[HS_REFILLED]==1);
        /* Without the sloped closing edge the least overshoot grows to 1.5. */
        CHECK(h.stats[HS_GROW15]==0 && h.stats[HS_GROW2]==0 && h.stats[HS_GROW3]==0);
        CHECK(scale==1 ? !detailed : detailed>0);
    }
    drawlist_free(&d);
}

typedef struct { interp_rec rec[32]; int32_t values[32][24]; int n; } fixture;
static void append(fixture *f, char kind, const int32_t *v, int n)
{
    memcpy(f->values[f->n],v,(size_t)n*sizeof *v);
    f->rec[f->n]=(interp_rec){kind,0,f->values[f->n],n};f->n++;
}

/* These camera displacements translate the visible triangle two pixels:
 * the front projects at 1/512; each clipping midpoint projects at 1/128. */
static int g_snapshot_fields = 17;
static int g_fill_colour = 0xFF07;
static void moving_triangle(fixture *f, int shift, int reuse)
{
    const int32_t camera[3][3]={{1024*shift,0,0x20000},
        {1024-512*shift,1536,-0x10000},{-1024-512*shift,1024,-0x10000}};
    const int pair[3][2]={{0,1},{1,2},{2,0}};
    const int32_t edges[3][8]={{0,20,20,24,26,0x40,24,26},
        {24,0,0,0,0,0x80,0,0},{48,16,24,20,20,0x40,16,24}};
    const int32_t fill[12]={g_fill_colour,0,0,319,199,0,0,0,0,0,0,2};
    memset(f,0,sizeof *f);
    for(int k=0;k<3;k++) {
        int32_t v[8]={camera[k][0],camera[k][1],camera[k][2],k?0:2*shift,0,
            k?2:1,0xC6B4+16*k,0xD6B4+8*k};append(f,'V',v,8);
    }
    append(f,'F',fill,12);
    for(int k=0;k<3;k++) {
        int32_t g[18]={24*k,0xD6B4+8*pair[k][0],0xD6B4+8*pair[k][1],20,20};
        for(int end=0;end<2;end++) {
            const int vertex=pair[k][end],at=5+6*end;
            memcpy(g+at,camera[vertex],sizeof camera[vertex]);
            g[at+3]=vertex?INT32_MIN:2*shift;g[at+5]=vertex!=0;
        }
        append(f,'G',g,g_snapshot_fields);
        int32_t e[8];memcpy(e,edges[k],sizeof e);
        if(k!=1) {e[1]+=2*shift;e[3]+=2*shift;e[6]+=2*shift;}
        append(f,'E',e,8);
    }
    append(f,'F',fill,12);
    const int32_t join[8]={72,24+2*shift,26,16+2*shift,24,0,0,0};append(f,'E',join,8);
    int32_t rows[19]={0xFF07,20,7,20,20,19,21,18,21,17,22,16,23,18,23,22,24,0xFF07,0xA000};
    rows[0]=rows[17]=g_fill_colour;
    for(int k=3;k<17;k++) rows[k]+=2*shift;
    append(f,'R',rows,19);
    for(int y=20;y<=26;y++) {
        const int l=rows[3+2*(y-20)],r=rows[4+2*(y-20)];
        const int32_t before[5]={y,l,r-l+1,0xA000,y*320+l};append(f,'b',before,5);
    }
    append(f,'B',fill,1);
    if(reuse) for(int k=0;k<3;k++) {
        int32_t g[18]={0};memcpy(g,f->values[4+2*k],(size_t)g_snapshot_fields*sizeof *g);
        g[1]+=32;g[2]+=32;g[5]+=4096;g[11]+=4096;
        append(f,'G',g,g_snapshot_fields);
    }
}

static void picture(const interp_rec *rec, int n, uint8_t *pixels)
{
    static const uint8_t blank[65536]={0};
    drawlist_init(&d);drawlist_set_scale(&d,4);hires_init(&h,4);
    drawlist_seed(&d,'Z',0xA000,0,blank,sizeof blank);
    for(int k=0;k<n;k++) record(rec[k].kind,rec[k].v,rec[k].n);
    const drawlist_page *page=drawlist_page_of(&d,0xA000);
    CHECK(page && page->hi);
    if(page && page->hi) memcpy(pixels,page->hi,64000*16);
    CHECK(h.stats[HS_REFILLED]==1 && h.stats[HS_NEAR_CLIPPED]==0);
    drawlist_free(&d);
}

static void interpolation(int reuse)
{
    fixture a,b;moving_triangle(&a,0,reuse);moving_triangle(&b,1,reuse);
    interp_frame *fa=interp_parse(a.rec,a.n),*fb=interp_parse(b.rec,b.n);
    interp_pairing *pair=interp_pair(fa,fb);
    interp_causes causes;interp_pair_causes(pair,&causes);
    CHECK(causes.polys_paired==2 && causes.batches_paired==2);
    uint8_t *actual=malloc(64000*16),*expected=malloc(64000*16);
    const double times[]={1e-9,0.25,0.5,0.75,1-1e-9};
    for(int t=0;t<5;t++) {
        interp_list out={0};int skeleton=0;
        interp_inbetween_ex(fa,fb,pair,times[t],INTERP_VERTICES,&out,&skeleton,NULL);
        int ng=0,ne=0;
        for(int k=0;k<out.n;k++) {
            const interp_rec *r=&out.rec[k];
            if(r->kind=='G') {
                CHECK(r->n==g_snapshot_fields);ng++;
                if(ng>3) continue; /* later faces reuse slots with other vertices */
                for(int end=0;end<2;end++) {
                    const int vertex=(r->v[1+end]-0xD6B4)/8,at=5+6*end;
                    const int delta=t*(vertex?-128:256);
                    CHECK(r->v[at]==(vertex==0?0:vertex==1?1024:-1024)+delta);
                }
            }
            if(r->kind=='E' && (r->v[5]&0x40)) {
                /* Quantized crossings advance by two at the endpoint;
                 * the fine renderer separately retains the fractional half. */
                CHECK(r->v[6]==(r->v[0]==0?24:16)+(t==4?2:0));ne++;
            }
        }
        CHECK(ng==(reuse?6:3) && ne==2);
        picture(out.rec,out.n,actual);
        if(t==0 || t==4) {
            const fixture *endpoint=t==0?&a:&b;
            picture(endpoint->rec,endpoint->n,expected);
            CHECK(!memcmp(actual,expected,64000*16));
        }
        interp_list_free(&out);
    }
    interp_pairing_free(pair);interp_frame_free(fb);
    /* Unsupported snapshots remain exactly the skeleton's record. This also
     * covers legacy captures and the coarse-only interpolation mode. */
    for(int mode=0;mode<6;mode++) {
        moving_triangle(&b,1,reuse);
        if(mode==0) b.values[4][3]++;
        if(mode==1) b.values[4][10]=1;
        if(mode==2) b.values[4][1]+=8;
        if(mode==3) b.rec[4].n=3;
        if(mode==5) { b.rec[4].n=18;b.values[4][17]=21; }
        fb=interp_parse(b.rec,b.n);pair=interp_pair(fa,fb);
        interp_list out={0};int skeleton=0,found=0;
        interp_inbetween_ex(fa,fb,pair,0.25,mode==4?0:INTERP_VERTICES,&out,&skeleton,NULL);
        for(int k=0;k<out.n;k++) if(out.rec[k].kind=='G' && found<3) {
            CHECK(out.rec[k].n==g_snapshot_fields && !memcmp(out.rec[k].v,a.values[4+2*found],(size_t)g_snapshot_fields*sizeof(int32_t)));
            found++;
        }
        CHECK(found==3);interp_list_free(&out);
        interp_pairing_free(pair);interp_frame_free(fb);
    }
    interp_frame_free(fa);
    free(actual);free(expected);
}

/* Pattern cells keep their 320x200 size while geometry gains subpixels.
 * Compare with an independently rendered solid coverage mask, including
 * transparent stipple cells over an existing background. */
static void pattern_picture(int scale, uint8_t *out)
{
    static uint8_t background[65536];
    memset(background,0x2B,sizeof background);
    fixture f;moving_triangle(&f,0,0);
    drawlist_init(&d);drawlist_set_scale(&d,scale);hires_init(&h,scale);
    drawlist_seed(&d,'Z',0xA000,0,background,sizeof background);
    for(int k=0;k<f.n;k++) record(f.rec[k].kind,f.rec[k].v,f.rec[k].n);
    const drawlist_page *p=drawlist_page_of(&d,0xA000);
    CHECK(h.stats[HS_REFILLED]==1 && !h.stats[HS_STYLE]);
    memcpy(out,p->hi,(size_t)64000*scale*scale);
    drawlist_free(&d);
}

static void patterned_fills(int scale)
{
    const size_t size=(size_t)64000*scale*scale;
    uint8_t *mask=malloc(size),*actual=malloc(size);
    g_fill_colour=0xFF07;pattern_picture(scale,mask);
    const int styles[]={0xFC07,0x1907,0xFA07,0x0007,0x0707,0xFB07};
    for(unsigned k=0;k<sizeof styles/sizeof *styles;k++) {
        g_fill_colour=styles[k];pattern_picture(scale,actual);
        for(int y=0;y<200*scale;y++) for(int x=0;x<320*scale;x++) {
            const size_t at=(size_t)y*320*scale+x;
            const int odd=(x/scale+y/scale)&1,hi=styles[k]>>8;
            const int want=mask[at]!=7 || hi==0xFB ? 0x2B :
                hi==0xFC ? (odd?7:0x2B) : (odd?hi:7);
            CHECK(actual[at]==want);
        }
    }
    g_fill_colour=0xFF07;
    free(mask);free(actual);
}

static const int32_t shared_spans[2][14]={{20,20,19,21,19,21,18,22,17,23,17,23,16,24},
    {20,20,21,22,21,24,22,26,23,28,23,30,24,32}};
static int32_t shared_colours[2]={0xFF07,0xFF09};
static void shared_face(int side, int reverse)
{
    const int32_t camera[3][3]={{0,0,0x20000},
        {side?1024:-1024,1536,-0x10000},{side?3072:1024,1536,-0x10000}};
    const int pair[3][2]={{0,1},{1,2},{2,0}};
    const int xs[3]={20,side?24:16,side?32:24};
    const int32_t colour=shared_colours[side];
    int32_t fill[12]={colour,0,0,319,199,0,0,0,0,0,0,2};
    for(int k=0;k<3;k++) {
        const int a=pair[k][reverse?1:0],b=pair[k][reverse?0:1];
        int32_t g[17]={24*k,0xD6B4+8*a,0xD6B4+8*b,20,20};
        const int vertices[2]={a,b};
        for(int end=0;end<2;end++) {
            const int at=5+6*end,v=vertices[end];
            memcpy(g+at,camera[v],sizeof camera[v]);
            g[at+3]=v?INT32_MIN:0;g[at+5]=v!=0;
        }
        record('G',g,17);
        const int behind=a && b,cut=a?a:b;
        int32_t e[8]={24*k,xs[a],a?26:20,xs[b],b?26:20,
            behind?0x80:0x40,xs[cut],26};
        record('E',e,8);
    }
    record('F',fill,12);
    const int32_t join[8]={72,xs[1],26,xs[2],26,0,0,0};record('E',join,8);
    /* Independent original edge-walk spans for the two adjacent triangles. */
    int32_t rows[19]={colour,20,7};memcpy(rows+3,shared_spans[side],sizeof shared_spans[side]);
    rows[17]=colour;rows[18]=0xA000;record('R',rows,19);
    for(int y=20;y<=26;y++) {
        const int l=rows[3+2*(y-20)],r=rows[4+2*(y-20)];
        const int32_t before[5]={y,l,r-l+1,0xA000,y*320+l};record('b',before,5);
    }
    record('B',fill,1);
    /* Captures retain row/byte diagnostics for supported solid fills;
     * interpolation omits them. Neither is a new painter. */
    const int32_t diagnostic[6]={26,24,1,0xA000,26*320+24,7};
    record('a',diagnostic,6);record('x',diagnostic,6);
}

static void shared_picture(int scale,int side,int reverse,uint8_t *pixels)
{
    static const uint8_t blank[65536]={0};
    drawlist_init(&d);drawlist_set_scale(&d,scale);hires_init(&h,scale);
    drawlist_seed(&d,'Z',0xA000,0,blank,sizeof blank);
    const int32_t fill[12]={0xFF07,0,0,319,199,0,0,0,0,0,0,2};record('F',fill,12);
    if(side<2) shared_face(side,reverse);
    else {shared_face(side==2?0:1,0);shared_face(side==2?1:0,0);}
    const drawlist_page *p=drawlist_page_of(&d,0xA000);
    CHECK(h.stats[HS_REFILLED]==(side<2?1:2) && !h.stats[HS_NEAR_CLIPPED]);
    CHECK(!h.stats[HS_GROW15] && !h.stats[HS_GROW2] && !h.stats[HS_GROW3]);
    memcpy(pixels,p->hi,(size_t)64000*scale*scale);drawlist_free(&d);
}

static void shared_edges(int scale)
{
    const size_t bytes=(size_t)64000*scale*scale;
    uint8_t *left=malloc(bytes),*right=malloc(bytes),*a=malloc(bytes),*b=malloc(bytes);
    shared_picture(scale,0,0,left);shared_picture(scale,0,1,a);CHECK(!memcmp(left,a,bytes));
    shared_picture(scale,1,0,right);shared_picture(scale,1,1,a);CHECK(!memcmp(right,a,bytes));
    shared_picture(scale,2,0,a);shared_picture(scale,3,0,b);
    int interior=0,overlap=0;
    for(int y=0;y<200*scale;y++) for(int x=0;x<320*scale;x++) {
        const size_t at=(size_t)y*320*scale+x;
        const double px=(x+0.5)/scale,py=(y+0.5)/scale;
        CHECK(a[at]==0 || a[at]==7 || a[at]==9);
        if(left[at] || right[at]) CHECK(a[at]!=0);
        CHECK(!!a[at]==!!b[at]);
        if(left[at] && right[at]) {
            overlap++;
            /* Inclusive coarse borders expand by at most one fine pixel in
             * x and y; the shared edge's slope is 2/3. */
            CHECK(fabs(px-(20+(py-20)*2/3))<=1.667/scale+1e-9);
        } else CHECK(a[at]==b[at]);
        const int cy=y/scale,cx=x/scale,row=cy-20;
        int footprint=0;
        if(row>=0 && row<7) for(int side=0;side<2;side++)
            footprint|=cx>=shared_spans[side][2*row] && cx<=shared_spans[side][2*row+1];
        if(!footprint) {      /* the geometry strays at most one cell from the original's footprint */
            int near_cell=0;
            for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) {
                const int r2=cy+dy-20,c2=cx+dx;
                if(r2>=0 && r2<7) for(int side=0;side<2;side++)
                    near_cell|=c2>=shared_spans[side][2*r2] && c2<=shared_spans[side][2*r2+1];
            }
            if(a[at]) CHECK(near_cell);
        }
        CHECK(left[at] || right[at] || !a[at]);   /* two faces paint nothing beyond what each paints alone */
        if(footprint && py>21 && py<25.5 && px>20-(py-20)*2/3 && px<20+(py-20)*2) {
            CHECK(a[at]!=0);interior++;
        }
    }
    CHECK(interior>0 && overlap>0);
    /* Black is actual model paint, not evidence of an uncovered pixel. */
    shared_colours[1]=0xFF00;shared_picture(scale,3,0,b);shared_colours[1]=0xFF09;
    int black_kept=0;
    for(int y=20*scale;y<27*scale;y++) for(int x=15*scale;x<33*scale;x++) {
        const size_t at=(size_t)y*320*scale+x;
        const int row=y/scale-20,cx=x/scale;
        if(!left[at] && right[at] && cx>=shared_spans[0][2*row] && cx<=shared_spans[0][2*row+1]) {
            CHECK(b[at]==0);black_kept++;
        }
    }
    CHECK(black_kept>0);
    free(left);free(right);free(a);free(b);
}

/* Every edge crosses the viewport; no unclipped edge can identify its origin.
 * V deliberately belongs to an older projection, while G retains this view. */
static void clipped_origin(int scale, int legacy, int ortho)
{
    static const uint8_t blank[65536]={0};
    const int xy[4][2]={{-30,0},{0,-30},{30,0},{0,30}};
    const int32_t fill[12]={0xFF07,79,79,121,121,0,0,0,0,0,0,0};
    drawlist_init(&d);drawlist_set_scale(&d,scale);hires_init(&h,scale);
    drawlist_seed(&d,'Z',0xA000,0,blank,sizeof blank);
    record('F',fill,12);
    for(int k=0;k<4;k++) {
        const int32_t stale[8]={0,0,-0x10000,0,0,2,0,0xD6B4+8*k};
        record('V',stale,8);
    }
    for(int k=0;k<4;k++) {
        int32_t g[18]={24*k,0xD6B4+8*k,0xD6B4+8*((k+1)%4),100,100};
        for(int end=0;end<2;end++) {
            const int vertex=(k+end)%4,at=5+6*end;
            g[at]=xy[vertex][0]*(ortho?2097152:512)+(ortho?524288:128);
            g[at+1]=xy[vertex][1]*(ortho?2097152:512)+(ortho?524288:128);
            g[at+2]=ortho?0:0x20000;
            /* Perspective IDIV truncates toward zero, including negative
             * camera coordinates with a positive fractional offset. */
            g[at+3]=ortho?xy[vertex][0]:g[at]/512;
            g[at+4]=ortho?xy[vertex][1]:g[at+1]/512;
        }
        g[17]=ortho==3?32:ortho?21:0;
        if(ortho==2 && k==0)g[8]++;
        record('G',g,legacy?3:ortho?18:17);
        const int32_t e[6]={24*k,79,79,121,121,0x0505};record('E',e,6);
    }
    record('F',fill,12);
    int32_t rows[3+86+1]={0xFF07,79,43};
    for(int k=0;k<43;k++) {
        const int radius=30-abs(k+79-100);
        rows[3+2*k]=100-radius;rows[4+2*k]=100+radius;
    }
    rows[89]=0xFF07;record('R',rows,90);
    for(int y=79;y<=121;y++) {
        int l=rows[3+2*(y-79)],r=rows[4+2*(y-79)];
        if(l<79)l=79;if(r>121)r=121;
        const int32_t b[5]={y,l,r-l+1,0xA000,y*320+l};record('b',b,5);
    }
    const drawlist_page *p=drawlist_page_of(&d,0xA000);
    const int fallback=legacy || ortho>1;
    CHECK(h.stats[HS_REFILLED]==(fallback?0:1));
    int detail=0;
    for(int y=0;y<200;y++)for(int x=0;x<320;x++) {
        const int radius=30-abs(y-100);
        const int painted=y>=79 && y<=121 && x>=79 && x<=121 && abs(x-100)<=radius;
        CHECK(p->b[y*320+x]==(painted?7:0));
        for(int j=0;j<scale;j++)for(int i=0;i<scale;i++) {
            const int v=p->hi[(y*scale+j)*320*scale+x*scale+i];
            if(scale==1 || fallback) { if(!painted)CHECK(v==0); }
            else {
                /* Verified geometry is clipped to the viewport, not to a
                 * coarse staircase. Its diamond can cross that staircase
                 * within one cell, but cannot cross the viewport or its
                 * analytic boundary (half a fine pixel of coverage). */
                if(x<79 || x>121 || y<79 || y>121)CHECK(v==0);
                if(v) {
                    const double centre=100.25+(ortho?0.5:0);
                    const double px=x+(i+0.5)/scale,py=y+(j+0.5)/scale;
                    CHECK(fabs(px-centre)+fabs(py-centre)<=30+1.0/scale+1e-9);
                }
            }
            detail+=v!=p->b[y*320+x];
        }
    }
    CHECK((scale==1 || fallback) ? !detail : detail>0);
    drawlist_free(&d);
}

int main(void)
{
    triangle(1,0,0);triangle(4,0,0);
    triangle(4,1,0);triangle(4,0,1);
    g_fat=4;triangle(4,0,0);g_fat=12;triangle(4,0,0);g_fat=30;triangle(4,0,0);g_fat=0;
    interpolation(0);interpolation(1);
    g_snapshot_fields=18;interpolation(0);interpolation(1);
    patterned_fills(1);patterned_fills(4);patterned_fills(9);
    g_fill_colour=0xFC07;interpolation(0);interpolation(1);
    g_fill_colour=0x1907;interpolation(0);interpolation(1);
    g_fill_colour=0xFF07;
    shared_edges(4);shared_edges(9);
    clipped_origin(1,0,0);clipped_origin(4,0,0);clipped_origin(9,0,0);clipped_origin(4,1,0);
    clipped_origin(1,0,1);clipped_origin(4,0,1);clipped_origin(9,0,1);
    clipped_origin(4,1,1);clipped_origin(4,0,2);clipped_origin(4,0,3);
    if (!failed) puts("camera-plane polygons: all checks passed");
    return failed ? 1 : 0;
}
