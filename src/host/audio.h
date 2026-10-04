/* audio.h - the AdLib and the PC speaker, rendered on the machine's clock.
 *
 * Every register write and speaker change arrives with the icount it
 * happened at. Rendering advances to that moment before applying it, so a
 * write lands on the sample its emulated time corresponds to - which is
 * what lets the AdLib driver's digitised speech (one OPL write per sample at
 * 7955 Hz, paced by PIT counter 2) come out as it did on the card.
 */
#ifndef F117R_AUDIO_H
#define F117R_AUDIO_H

#include "machine.h"

#include <stddef.h>
#include <stdint.h>

typedef struct audio audio_t;

/* GOG DOSBox's configured OPL/mixer rate. Output is stereo int16. */
#define AUDIO_RATE 44100
typedef enum { AUDIO_OPL_DBOPL, AUDIO_OPL_NUKED } audio_opl_backend;

audio_t *audio_create(uint64_t ips);
audio_t *audio_create_backend(uint64_t ips, audio_opl_backend backend);
void     audio_destroy(audio_t *a);
void     audio_opl_write(audio_t *a, uint64_t icount, uint8_t reg, uint8_t val);
void     audio_speaker(audio_t *a, const machine_t *m, uint64_t icount);
/* Render everything up to icount into the internal buffer. */
void     audio_advance(audio_t *a, uint64_t icount);
/* Take rendered frames (stereo pairs); returns how many. */
size_t   audio_take(audio_t *a, int16_t *out, size_t max_frames);

#endif
