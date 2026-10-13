#include "nearclip.h"
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int failed;
#define CHECK(v) do { if (!(v)) { fprintf(stderr,"line %d: %s\n",__LINE__,#v);failed++; } } while (0)

int main(int argc, char **argv)
{
    /* Exact integer crossing plus the negative projection's lost low byte. */
    const int32_t front[3] = { 0x12345, -0x12345, 0x20001 };
    const int32_t behind[3] = { 0x12344, -0x12346, -0x10000 };
    int32_t coarse[2] = { 99, 99 };
    double fine[2] = { 99, 99 };
    CHECK(present_near_crossing(front,behind,coarse,fine)==1);
    CHECK(coarse[0]==582 && coarse[1]==-584);
    CHECK(fine[0]==0x12345/128.0 && fine[1]==-0x12345/128.0);
    CHECK(fine[0]>=coarse[0] && fine[0]<coarse[0]+2);
    CHECK(fine[1]>=coarse[1] && fine[1]<coarse[1]+2);
    CHECK(front[0]==0x12345 && behind[1]==-0x12346);

    /* ADD overflow occurs before SAR: this is not a wide analytic average. */
    const int32_t wrapf[3] = { INT32_MAX, INT32_MIN, 0x20000 };
    const int32_t wrapb[3] = { INT32_MAX, INT32_MIN, -0x10000 };
    CHECK(present_near_crossing(wrapf,wrapb,coarse,fine)==1);
    CHECK(coarse[0]==-2 && coarse[1]==0 && fine[0]==-1/128.0 && fine[1]==0);

    /* One halving updates the visible endpoint, then finds the accepted z band. */
    const int32_t deepf[3] = { 1024, 2048, 0x80000 };
    const int32_t deepb[3] = { 0, 0, -0x20000 };
    CHECK(present_near_crossing(deepf,deepb,coarse,fine)==2);
    CHECK(coarse[0]==2 && coarse[1]==4 && fine[0]==2 && fine[1]==4);

    /* A pair which never reaches the accepted band must leave outputs alone. */
    const int32_t invalid[3] = { 0, 0, 0x20000 };
    const int32_t saved[2] = { coarse[0], coarse[1] };
    const double saved_fine[2] = { fine[0], fine[1] };
    CHECK(present_near_crossing(invalid,invalid,coarse,fine)==0);
    CHECK(!memcmp(saved,coarse,sizeof saved) && !memcmp(saved_fine,fine,sizeof saved_fine));
    /* Orthographic icons can have Z=0. SAR rounds from the last high-word
     * bit toward positive infinity, including negative half-way values. */
    const int32_t ortho[3] = { 5*65536+32768, -5*65536+32768, 0 };
    CHECK(present_ortho_project(ortho,17,coarse,fine));
    CHECK(coarse[0]==3 && coarse[1]==-2 && fine[0]==2.75 && fine[1]==-2.25);
    CHECK(present_ortho_project(ortho,16,coarse,fine));
    CHECK(coarse[0]==5 && coarse[1]==-5 && fine[0]==5.5 && fine[1]==-4.5);
    const int32_t icon[3] = { -25*2097152+524288, 25*2097152+524288, 0 };
    CHECK(present_ortho_project(icon,21,coarse,fine));
    CHECK(coarse[0]==-25 && coarse[1]==25 && fine[0]==-24.75 && fine[1]==25.25);
    CHECK(present_ortho_project(icon,31,coarse,fine));
    CHECK(coarse[0]==0 && coarse[1]==0);
    for(int shift=1;shift<16;shift++) {
        CHECK(present_ortho_project(icon,shift,coarse,fine));
        for(int k=0;k<2;k++) {
            const int32_t expected=(int16_t)((uint16_t)((uint32_t)icon[k]>>16)<<(16-shift));
            CHECK(coarse[k]==expected && fine[k]==expected);
        }
    }
    CHECK(!present_ortho_project(icon,0,coarse,fine));
    CHECK(!present_ortho_project(icon,32,coarse,fine));
    if (argc > 1) {
        FILE *cases = fopen(argv[1],"r");
        if (!cases) { fprintf(stderr,"cannot read %s\n",argv[1]);return 2; }
        int count = 0, got;
        int32_t f[3], b[3], original[2];
        while ((got=fscanf(cases,"%"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32,
                    &f[0],&f[1],&f[2],&b[0],&b[1],&b[2],&original[0],&original[1]))==8) {
            CHECK(present_near_crossing(f,b,coarse,fine)>0);
            CHECK(coarse[0]==original[0] && coarse[1]==original[1]);
            count++;
        }
        CHECK(got==EOF && count>0);
        fclose(cases);
        printf("original retained crossings checked: %d\n",count);
    }
    if (!failed) puts("camera-plane crossing: all checks passed");
    return failed ? 1 : 0;
}
