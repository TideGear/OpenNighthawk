/* speaker.h - the PC speaker as the hardware drives it, rendered on the machine's clock.
 *
 * The cone is driven by PIT counter 2's OUT through the AND gate of port 61h
 * bit 1; bit 0 is the counter's GATE. This module runs counter 2 itself, one
 * PIT clock at a time, from the writes the machine reports (control words,
 * counts, port 61h), following the 8254 data sheet (86Box's pit.c is the
 * executable reference it was checked against): a count written in modes 2
 * and 3 while counting is taken at the end of the current period or
 * half-cycle, not at once; a mode 0 count drives OUT low at the write and
 * high N+1 clocks later. The level is integrated over each output sample, so
 * nothing is point-sampled.
 *
 * Digitised sound ("realsound", F-117A's radio calls under the speaker and
 * Roland drivers) is a pulse-width-modulated train: counter 2 in mode 0,
 * rewritten at a fixed carrier rate. SPEAKER_REALSOUND (the default) passes
 * that train through a moving average one carrier period long, measured from
 * the writes, which removes the carrier and its harmonics and leaves the
 * modulation: the level GOG DOSBox 0.74 and 86Box derive from the count
 * (with the hardware's polarity and scale). SPEAKER_PWM renders the train
 * itself, carrier included, as DOSBox-X does (less its 10 kHz filter).
 *
 * The speaker's output lags the machine by one PIT clock (0.84 us), so that
 * every clock is complete before its sample is taken; the moving average adds
 * half a carrier period (33 us for F-117A).
 */
#ifndef F117R_SPEAKER_H
#define F117R_SPEAKER_H

#include <stdint.h>

typedef enum { SPEAKER_REALSOUND, SPEAKER_PWM } speaker_model;

#define SPEAKER_RING 256        /* the longest carrier period averaged, in PIT clocks */
#define SPEAKER_ACC 8           /* output samples held open */

typedef struct {
    speaker_model model;
    uint64_t ips, rate;
    /* counter 2 (8254) */
    uint8_t  mode;              /* 0..5 */
    uint8_t  out, gate, data;   /* OUT pin, port 61h bit 0, bit 1 */
    uint8_t  null;              /* control word written, no count yet */
    uint8_t  pending;           /* a load on the next clock */
    uint8_t  low_half;          /* mode 3: in the low half-cycle */
    uint8_t  odd;               /* mode 3: the first step after an odd load */
    uint8_t  done_tc;           /* modes 0, 1: terminal count reached */
    uint8_t  strobe;            /* modes 4, 5: OUT low for this clock */
    uint32_t latch;             /* the count written, 1..65536 */
    int32_t  count;
    /* time: inside PIT clock interval `tick`, `pos` of it accounted for */
    uint64_t tick;
    double   pos, cov;
    /* the carrier-period average */
    float    ring[SPEAKER_RING];
    unsigned ring_at, avg_len;
    unsigned live_zero;         /* clocks since the last one that was not silent */
    double   ring_sum;
    uint64_t last_pwm_tick;
    double   period_est;
    /* output samples: acc[s % SPEAKER_ACC], in units of 1/rate PIT clocks */
    double   acc[SPEAKER_ACC];
    uint64_t place;             /* the PIT clock the next value lands on */
    uint64_t sample;            /* the sample `place` falls in */
    uint64_t bound;             /* (sample + 1) * PIT_HZ, the sample's end in 1/rate clocks */
    uint64_t events;
} speaker_t;

void   speaker_init(speaker_t *s, uint64_t ips, uint64_t rate, speaker_model model);
/* Each applies at icount (which never decreases). */
void   speaker_control(speaker_t *s, uint64_t icount, unsigned mode);
void   speaker_count(speaker_t *s, uint64_t icount, unsigned reload);
void   speaker_port61(speaker_t *s, uint64_t icount, unsigned bits);
/* Bring the model up to icount (so every sample before it is complete). */
void   speaker_advance(speaker_t *s, uint64_t icount);
/* The cone's input averaged over output sample `n` (0..1), once. */
float  speaker_take(speaker_t *s, uint64_t n);

#endif
