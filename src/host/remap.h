/* remap.h - keyboard remapping (docs/roadmap.md, Phase 3 "Modern controls" and accessibility).
 *
 * `remap = Q:W` (f117a.ini) or `--remap Q:W` makes the physical Q key act as W in the game: pairs are
 * SDL key names ("A", "Left Shift", "F5", "Keypad 4"), several separated by commas or spaces. Only the
 * key the game is sent changes; the host's own chords (Alt+Enter, Ctrl+Alt+P, Ctrl+Alt+S) keep the
 * physical keys. */
#ifndef F117R_REMAP_H
#define F117R_REMAP_H

#include <SDL3/SDL_scancode.h>

/* Add the pairs in spec ("Q:W, A:S"). Returns the number added, or -1 if a name is not a key
 * (nothing from that spec is kept). */
int remap_add(const char *spec);

/* The key the game should see for a physical key. */
SDL_Scancode remap_apply(SDL_Scancode physical);

/* Forget every pair. */
void remap_clear(void);

#endif
