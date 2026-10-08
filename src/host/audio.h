/* audio.h - AdLib, PC speaker and optional Munt on the machine's clock.
 *
 * Every register write and speaker change arrives with the icount it
 * happened at. Rendering advances to that moment before applying it, so a
 * write lands on the sample its emulated time corresponds to - which is
 * what lets the AdLib driver's digitised speech (one OPL write per sample at
 * 7955 Hz, paced by PIT counter 2) come out as it did on the card. The PC
 * speaker is run from counter 2's writes by speaker.c.
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
/* One change to counter 2 or port 61h as the machine reports it: the state
 * after it (port 61h bits 0-1, counter 2's count, mode and null-count flag). */
void     audio_speaker_event(audio_t *a, uint64_t icount, uint8_t port61,
                             uint16_t reload, uint8_t mode, uint8_t null_count);
/* The speaker model (speaker.h: 0 realsound, the default; 1 pwm). Choose it
 * before the first speaker change; returns zero otherwise. */
int      audio_set_speaker_model(audio_t *a, int model);
/* Enable before rendering starts. Returns zero with a diagnostic on failure. */
int      audio_enable_mt32(audio_t *a, const char *control, const char *pcm,
                           char *error, size_t error_size);
/* Returns zero if the enabled backend rejects a message. */
int      audio_midi_byte(audio_t *a, uint64_t icount, uint8_t byte);
/* Render everything up to icount into the internal buffer. */
void     audio_advance(audio_t *a, uint64_t icount);
/* Take rendered frames (stereo pairs); returns how many. */
size_t   audio_take(audio_t *a, int16_t *out, size_t max_frames);

#endif
