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
#endif
