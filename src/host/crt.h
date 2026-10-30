/* crt.h - the optional CRT filter for the 4:3 presentations (docs/roadmap.md, Phase 3).
 *
 * Scanlines: each of the game's 200 lines is shown over h/200 picture rows, brightest at the top and
 * dimmer towards the gap below it, as on a monitor. It works on the finished picture the window
 * shows, never on the game's own pixels, so the simulation, the draw records and the exactness checks
 * do not see it. Off by default. */
#ifndef F117R_CRT_H
#define F117R_CRT_H

#include <stdint.h>

/* The picture src (w x h ARGB, h a multiple of 200) with scanlines into dst (same size; may be src).
 * A picture with a single row per game line is copied unchanged (there is no gap to draw). */
void crt_scanlines(const uint32_t *src, uint32_t *dst, int w, int h);

#endif
