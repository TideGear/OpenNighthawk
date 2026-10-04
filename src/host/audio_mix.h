/* DOSBox 0.74 mixer interpolation at equal input/output sample rates. */
#ifndef F117R_AUDIO_MIX_H
#define F117R_AUDIO_MIX_H
#include <stdint.h>
static inline int32_t opl_mixer_sample(int32_t sample, int32_t *last)
{
    /* Enable initializes freq_index to (1 << 14) - 1. With equal rates,
     * every sample retains that fraction. AddSamples interpolates from the
     * preceding sample with an arithmetic shift, then applies mixer gain.
     * Write as sample - ceil(delta / 16384) to avoid signed right shifts. */
    int64_t delta = (int64_t)sample - *last;
    int64_t correction = delta > 0 ? (delta + 16383) / 16384 : delta / 16384;
    *last = sample;
    return (int32_t)((int64_t)sample - correction);
}
#endif
