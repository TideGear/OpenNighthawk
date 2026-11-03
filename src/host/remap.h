/* remap.h - keyboard remapping (docs/roadmap.md, Phase 3 "Modern controls" and accessibility).
 *
 * `remap = Q:W` (f117a.ini) or `--remap Q:W` makes the physical Q key act as W in the game: pairs are
 * SDL key names ("A", "Left Shift", "F5", "Keypad 4"), separated by commas or semicolons. Only the
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
void remap_remove(SDL_Scancode physical);
int remap_set(SDL_Scancode physical, SDL_Scancode target);
/* The physical key that now sends target to the game: target itself when it is not remapped, the first key remapped to it otherwise, and
 * SDL_SCANCODE_UNKNOWN when none does. */
SDL_Scancode remap_source(SDL_Scancode target);
/* Make physical send the game key target and target send what physical sent (a swap), first freeing the key that sent target before.
 * physical == target restores target's default. Returns 0 if a pair could not be stored. */
int remap_assign(SDL_Scancode physical, SDL_Scancode target);
/* Canonical current nondefault pairs; caller frees the returned string. */
char *remap_export(void);

#endif
