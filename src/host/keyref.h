/* keyref.h - the on-screen key reference (docs/roadmap.md, Phase 3 "Modern controls").
 *
 * Five pages drawn over the game: four from the manual's Key Controls Summary (the printed appendix, pages 173-174 of
 * the Instruction Manual) and one with this program's own keys. The host pauses the machine while it is open, as for the settings menu. The table is
 * data, so a test can check that every line fits the panel. */
#ifndef F117R_KEYREF_H
#define F117R_KEYREF_H

#include <stdint.h>

typedef struct { const char *what, *keys, *def; } keyref_row;     /* what == NULL: a section heading in keys; def: the SDL name of the game key, or NULL when the row cannot be remapped */

#define KEYREF_PAGES 5
#define KEYREF_WHAT_CHARS 22
#define KEYREF_KEYS_CHARS 33

/* The rows of a page (0 to 4), and how many. */
const keyref_row *keyref_rows(int page, int *count);

/* The default game key's SDL name for a row, NULL when the row is a heading or cannot be remapped. */
const char *keyref_row_default(const keyref_row *row);
/* As keyref_render, with a cursor row (-1: none), the "press the new key" prompt, and per-row binding names (NULL entries keep the manual's text);
 * a changed binding is drawn blue. */
void keyref_render_bind(int page, uint32_t *pixels, int w, int h, int cursor, int waiting, const char *const *bound, const int *changed);

/* Draw page into an ARGB buffer (w*h, w >= 640, h >= 400) over whatever it holds. */
void keyref_render(int page, uint32_t *pixels, int w, int h);
/* The tab strip across the top of the Esc menu: tab 0 Settings, 1 Keys, the active one lit; Tab switches, Esc closes. */
void keyref_tabs(int active, uint32_t *pixels, int w, int h);

#endif
