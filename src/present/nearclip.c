#include "nearclip.h"
#include <string.h>

int present_ortho_project(const int32_t camera[3], int shift,
                          int32_t coarse[2], double fine[2])
{
    if (shift < 1 || shift > 31) return 0;
    for (int k = 0; k < 2; k++) {
        const int hi = (int16_t)((uint32_t)camera[k] >> 16);
        /* The low-shift arm scales only the high word and wraps at 16 bits.
         * Keep those integer vertices rather than inventing low-word detail. */
        if (shift < 16) {
            coarse[k] = (int16_t)((uint16_t)hi << (16 - shift));
            fine[k] = coarse[k];
            continue;
        }
        const int bits = shift - 16, den = 1 << bits;
        int q = hi >= 0 ? hi / den : -((-hi + den - 1) / den);
        if (bits && ((uint16_t)hi & (1u << (bits - 1)))) q++;
        coarse[k] = q;
        fine[k] = camera[k] / (double)(1u << shift);
    }
    return 1;
}

/* Wrap the guest's ADD/ADC plus one, then arithmetic-shift the 32-bit sum. */
static int32_t midpoint(int32_t a, int32_t b)
{
    const uint32_t sum = (uint32_t)a + (uint32_t)b + 1u;
    const int64_t signed_sum = sum & 0x80000000u ? (int64_t)sum - 0x100000000LL : sum;
    return (int32_t)(signed_sum >= 0 ? signed_sum / 2 : -((-signed_sum + 1) / 2));
}

int present_near_crossing(const int32_t front[3], const int32_t behind[3],
                          int32_t coarse[2], double fine[2])
{
    int32_t f[3], b[3];
    memcpy(f, front, sizeof f); memcpy(b, behind, sizeof b);
    for (int turn = 1; turn <= 64; turn++) {
        int32_t mid[3];
        for (int k = 0; k < 3; k++) mid[k] = midpoint(f[k], b[k]);
        const uint32_t hi = (uint32_t)mid[2] >> 16;
        if (hi <= 1) {
            for (int k = 0; k < 2; k++) {
                /* SAR by eight rounds toward negative infinity. */
                const int64_t v = mid[k];
                const int32_t shifted = (int32_t)(v >= 0 ? v / 256 : -((-v + 255) / 256));
                coarse[k] = shifted * 2;
                fine[k] = v / 128.0;
            }
            return turn;
        }
        /* The original tests DEC DX's sign, including the 8000h boundary. */
        memcpy(((hi - 1u) & 0x8000u) ? b : f, mid, sizeof mid);
    }
    return 0;
}
