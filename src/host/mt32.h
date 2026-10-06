/* Optional Munt backend. ROMs and libmt32emu are supplied separately. */
#ifndef F117R_MT32_H
#define F117R_MT32_H
#include <stddef.h>
#include <stdint.h>
typedef struct mt32 mt32_t;
mt32_t *mt32_create(const char *control, const char *pcm, unsigned rate,
                    char *error, size_t error_size);
void mt32_destroy(mt32_t *s);
int mt32_byte(mt32_t *s, uint8_t byte);
void mt32_render(mt32_t *s, int16_t *stereo, unsigned frames);
/* Of n files, the best control and PCM ROM pair by content (Munt's own SHA-1
 * table, so names do not matter): a first-generation MT-32 first (1.07 down
 * to 1.04, then BlueRidge - what a 1991 game was written for), then the later
 * MT-32 (2.07 down to 2.03) and the CM-32L, each with its own PCM ROM.
 * Returns 1 with the two paths and a description, else 0. */
int mt32_find_roms(const char *const *files, int n, const char **control, const char **pcm,
                   char *description, size_t description_size);
#endif
