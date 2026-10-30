/* keyref.h - the on-screen key reference (docs/roadmap.md, Phase 3 "Modern controls").
 *
 * Two pages drawn over the game, from the manual's Key Controls Summary (the printed appendix, pages 173-174 of
 * the Instruction Manual). The host pauses the machine while it is open, as for the settings menu. The table is
 * data, so a test can check that every line fits the panel. */
#ifndef F117R_KEYREF_H
#define F117R_KEYREF_H

#include <stdint.h>

typedef struct { const char *what, *keys; } keyref_row;     /* what == NULL: a section heading in keys */

#define KEYREF_PAGES 4
#define KEYREF_WHAT_CHARS 22
#define KEYREF_KEYS_CHARS 33

/* The rows of a page (0 to 3), and how many. */
const keyref_row *keyref_rows(int page, int *count);

/* Draw page into an ARGB buffer (w*h, w >= 640, h >= 400) over whatever it holds. */
void keyref_render(int page, uint32_t *pixels, int w, int h);

#endif
