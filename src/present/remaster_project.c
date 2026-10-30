/* remaster_project.c - the original's projection (see remaster_project.h). */
#include "remaster_project.h"

int remaster_range(int32_t z)
{
    const int zhi = (int16_t)((uint32_t)z >> 16);
    return zhi >= 0x100 ? REMASTER_NEAR : zhi >= 1 ? REMASTER_MID : REMASTER_BEHIND;
}

/* x86 IDIV truncates towards zero, as C division does */
int remaster_pixel_exact(int32_t v, int32_t z, int *pixel)
{
    const int range = remaster_range(z);
    if (range == REMASTER_BEHIND) return 0;
    int32_t numerator;
    int32_t divisor;
    if (range == REMASTER_NEAR) {
        numerator = v >> 8;                                  /* bytes 1..3 of the 32-bit coordinate */
        divisor = (int32_t)(((uint32_t)z >> 16) & 0xFFFF);   /* the high word of z */
    } else {
        numerator = v >> 1;
        divisor = (int32_t)((((uint32_t)z >> 8) & 0xFFFF) >> 1);   /* the low word of z >> 8, halved */
    }
    if (divisor == 0) return 0;
    *pixel = (int)(numerator / divisor);
    return 1;
}

int remaster_project(int32_t v, int32_t z, double *offset)
{
    if (remaster_range(z) == REMASTER_BEHIND) return 0;
    *offset = 256.0 * (double)v / (double)z;
    return 1;
}
