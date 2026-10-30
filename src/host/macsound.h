/* macsound.h - the Macintosh port's recorded sounds, played over the DOS drivers (hybrid sound; soundpolicy.h).
 *
 * A directory of WAV files named by the Mac resource id (322.wav, 304.wav, ...: 8 or 16 bit PCM, mono or stereo) is
 * loaded once. The game's sound id N is the Mac resource 300 + N; the music ids and a few others have their own
 * resource (macsound_resource). A sound is started by id; macsound_mix adds the playing sounds into an interleaved
 * stereo int16 buffer at AUDIO_RATE. Nothing here touches the machine. */
#ifndef F117R_MACSOUND_H
#define F117R_MACSOUND_H

#include <stddef.h>
#include <stdint.h>

typedef struct macsound macsound;

/* Load every NNN.wav in dir, resampled to `rate`. NULL when the directory has none. */
macsound *macsound_load(const char *dir, int rate);
void macsound_free(macsound *s);

/* The resource that stands for DOS sound id `id`, or 0 for none: 300 + id for the effects the Mac port numbers that
 * way; 322, 321 and 200 for the music ids 36, 37 and 38 (matched by ear, docs/sound-board.md). */
int macsound_resource(int id);

/* Is there a recording for DOS sound id `id`? */
int macsound_has(const macsound *s, int id);

/* Start the recording for `id` (restarting it if it is playing). Returns 0 if there is none. */
int macsound_play(macsound *s, int id);
void macsound_stop_all(macsound *s);

/* Add up to `frames` stereo frames of what is playing into out (which already holds the other sources). */
void macsound_mix(macsound *s, int16_t *out, size_t frames);

#endif
