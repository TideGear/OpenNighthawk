/* R36: carry a quantised RGB position between band endpoints into another
 * palette ramp. Integer arithmetic shared with the compute shader. */
#ifndef F117_BAND_PALETTE_H
#define F117_BAND_PALETTE_H
#ifdef F117_BAND_HLSL
#define BP_UINT uint
#define BP_FN
#else
#include <stdint.h>
#define BP_UINT uint32_t
#define BP_FN static inline
#endif
BP_FN BP_UINT band_palette_mean(BP_UINT a, BP_UINT b)
{
    return 0xFF000000u | (((((a >> 16) & 255u) + ((b >> 16) & 255u)) / 2u) << 16)
        | (((((a >> 8) & 255u) + ((b >> 8) & 255u)) / 2u) << 8)
        | (((a & 255u) + (b & 255u)) / 2u);
}
BP_FN BP_UINT band_palette_map(BP_UINT colour, BP_UINT lo, BP_UINT hi,
                              BP_UINT mapped_lo, BP_UINT mapped_hi)
{
    int numerator = 0, denominator = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        int a = (int)((lo >> shift) & 255u);
        int delta = (int)((hi >> shift) & 255u) - a;
        numerator += ((int)((colour >> shift) & 255u) - a) * delta;
        denominator += delta * delta;
    }
    if (denominator == 0) return mapped_lo;
    if (numerator < 0) numerator = 0;
    if (numerator > denominator) numerator = denominator;
    BP_UINT result = 0xFF000000u;
    for (int shift = 0; shift <= 16; shift += 8) {
        int a = (int)((mapped_lo >> shift) & 255u);
        int b = (int)((mapped_hi >> shift) & 255u);
        int channel = (a * (denominator - numerator) + b * numerator + denominator / 2) / denominator;
        result |= (BP_UINT)channel << shift;
    }
    return result;
}
BP_FN BP_UINT band_palette_sea(BP_UINT colour, BP_UINT index,
    BP_UINT prev, BP_UINT own, BP_UINT next,
    BP_UINT sea_prev, BP_UINT sea_own, BP_UINT sea_next)
{
    BP_UINT lo = index == 0x70u ? own : band_palette_mean(prev, own);
    BP_UINT hi = index == 0x7Fu ? own : band_palette_mean(own, next);
    BP_UINT sea_lo = index == 0x70u ? sea_own : band_palette_mean(sea_prev, sea_own);
    BP_UINT sea_hi = index == 0x7Fu ? sea_own : band_palette_mean(sea_own, sea_next);
    return band_palette_map(colour, lo, hi, sea_lo, sea_hi);
}
#undef BP_UINT
#undef BP_FN
#endif
