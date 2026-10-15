/* ROM-free band geometry, discrete-state policy and wrapped-corner regression.
 * The captured-corpus test separately compares all exact corners with DOS. */
#include "band_state.h"
#include "interp.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static void state(int32_t v[BAND_REC_BASE])
{
    memset(v,0,BAND_REC_BASE*sizeof *v);
    v[0]=BAND_REC_VERSION;v[1]=0x1234;v[2]=0x7C;v[3]=3;
    v[6]=512;v[8]=v[10]=32767;v[13]=159;v[14]=52;
    for(int k=0;k<16;k++)v[15+k]=64*k;
    v[31]=319;v[32]=90;
}
static void quad_record(const band_quad *q,int32_t u[10])
{
    u[0]=q->colour;u[1]=4;
    for(int k=0;k<4;k++){u[2+2*k]=q->x[k];u[3+2*k]=q->y[k];}
}
int main(void)
{
    int32_t v[BAND_REC_BASE],other[BAND_REC_BASE],blend[BAND_REC_FINE];
    band_state st;band_exact_list ex;band_fine_list fine;band_cover cover={0};
    state(v);CHECK(band_state_load(v,BAND_REC_BASE,&st));
    CHECK(band_exact(&st,&ex)==0 && ex.n==32);
    CHECK(ex.q[0].colour==0x80 && ex.q[16].colour==0x70);
    CHECK(ex.q[0].x[0]==-22440 && ex.q[0].x[1]==22758);
    CHECK(ex.q[0].y[0]==52 && ex.q[0].y[1]==52 && ex.q[0].y[2]==4);
    CHECK(band_fine(&st,&ex,&fine)==0 && fine.n==32);
    CHECK(fabs(fine.q[0].x[0]-(159-22600*32767.0/32768))<1e-9);
    CHECK(fine.q[0].y[0]==52 && fine.q[0].y[1]==52);
    /* Sky and ground start at exactly the same horizon. */
    CHECK(fine.q[0].x[0]==fine.q[16].x[0] && fine.q[0].y[0]==fine.q[16].y[0]);
    CHECK(fine.q[0].x[1]==fine.q[16].x[1] && fine.q[0].y[1]==fine.q[16].y[1]);
    st.flat_a=st.flat_b=1;CHECK(band_exact(&st,&ex)==0);
    for(int k=0;k<32;k++)CHECK(ex.q[k].colour==(k<16?0x88:0x7C));
    st.detail=2;CHECK(band_exact(&st,&ex)==1 && ex.n==0);CHECK(band_fine(&st,NULL,&fine)==1 && fine.n==0);
    v[0]++;CHECK(!band_state_load(v,BAND_REC_BASE,&st));
    CHECK(!band_cover_begin(&cover,v,BAND_REC_BASE,1) && !cover.active);state(v);

    /* Pitch crosses zero on the short arc, without turning through 180 degrees. */
    v[7]=(uint16_t)-572;v[8]=32763;memcpy(other,v,sizeof v);other[7]=572;
    CHECK(band_blend(v,BAND_REC_BASE,other,BAND_REC_BASE,0.5,blend,BAND_REC_FINE)==BAND_REC_FINE);
    CHECK(band_state_load(blend,BAND_REC_FINE,&st));CHECK(fabs(st.f_sp)<0.01 && st.f_cp>32760);
    other[3]=2;CHECK(!band_blend(v,BAND_REC_BASE,other,BAND_REC_BASE,0.5,blend,BAND_REC_FINE));
    state(v);v[6]=32767;v[7]=32767;v[8]=512;v[9]=v[10]=23170;
    for(int k=0;k<16;k++)v[15+k]=1024*k;
    CHECK(band_state_load(v,BAND_REC_BASE,&st));CHECK(band_exact(&st,&ex)==0);CHECK(band_fine(&st,&ex,&fine)==0);
    int wrapped=0;for(int k=0;k<32;k++)if(ex.q[k].wrapped){wrapped++;CHECK(!fine.q[k].valid);}
    CHECK(wrapped>0);
    CHECK(band_cover_begin(&cover,v,BAND_REC_BASE,1));
    int32_t u[10];
    for(int k=0;k<32;k++) {
        quad_record(&ex.q[k],u);
        CHECK(band_cover_step(&cover,u,10,NULL)==(ex.q[k].wrapped?BC_KEPT:BC_REPLACED));
    }
    CHECK(!cover.active && !cover.mismatched && cover.kept==(uint64_t)wrapped);

    /* Fine interpolation changes the state, while all exact U corners stay
     * put, including wrapped bands. Otherwise a kept quad invalidates the
     * cover and later valid quads silently lose their state-driven geometry. */
    band_state_enable(1);
    memcpy(other,v,sizeof v);other[6]=30000;other[7]=32000;
    band_state os;band_exact_list oe;CHECK(band_state_load(other,BAND_REC_BASE,&os));CHECK(!band_exact(&os,&oe));
    interp_rec ar[33],br[33];int32_t au[32][10],bu[32][10];
    ar[0]=(interp_rec){'o',0,v,BAND_REC_BASE};br[0]=(interp_rec){'o',1,other,BAND_REC_BASE};
    for(int k=0;k<32;k++) {
        quad_record(&ex.q[k],au[k]);quad_record(&oe.q[k],bu[k]);
        ar[k+1]=(interp_rec){'U',0,au[k],10};br[k+1]=(interp_rec){'U',1,bu[k],10};
    }
    interp_frame *a=interp_parse(ar,33),*b=interp_parse(br,33);interp_pairing *pair=interp_pair(a,b);
    interp_list list={0};int sb;
    CHECK(interp_inbetween_ex(a,b,pair,0.25,INTERP_BANDS,&list,&sb,NULL)==33 && !sb);
    CHECK(list.rec[0].kind=='o' && list.rec[0].n==BAND_REC_FINE);
    for(int k=0;k<32;k++)CHECK(list.rec[k+1].kind=='U' && !memcmp(list.rec[k+1].v,au[k],sizeof au[k]));
    interp_list_free(&list);interp_pairing_free(pair);interp_frame_free(a);interp_frame_free(b);
    /* The old-renderer switch must retain its interpolated U corners. */
    band_state_enable(0);
    a=interp_parse(ar,33);b=interp_parse(br,33);pair=interp_pair(a,b);
    CHECK(interp_inbetween_ex(a,b,pair,0.25,INTERP_BANDS,&list,&sb,NULL)==33 && !sb);
    int moved=0;
    for(int k=0;k<32;k++)moved+=memcmp(list.rec[k+1].v,au[k],sizeof au[k])!=0;
    CHECK(moved>0);
    interp_list_free(&list);interp_pairing_free(pair);interp_frame_free(a);interp_frame_free(b);
    band_state_enable(1);
    puts("band rules: analytic horizon, original colours, low detail, shortest arc, wrapped bands and old-renderer switch pass");
    return 0;
}
