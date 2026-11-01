/* colourblind.h - the optional colour-blind filter (docs/roadmap.md, Phase 3, accessibility).
 *
 * Red and green (or blue and yellow) that look alike to a colour-blind player are moved apart. For protanopia and deuteranopia
 * the part of each colour the vision cannot see is shifted into the channels it can (the standard "daltonization" of Fidaner,
 * Lin and Ozguven: simulate the vision, take what is lost, add it to the other channels); for tritanopia the blue-versus-yellow
 * signal is moved onto the red-green axis (a plain rule: the standard formula has no usable loss to move). It works on the finished picture the window
 * shows, never on the game's own pixels, so the simulation, the draw records and the exactness checks do not see it, and it
 * applies to every presentation and to the HUD with the rest of the picture. Off by default. */
#ifndef F117R_COLOURBLIND_H
#define F117R_COLOURBLIND_H

#include <stdint.h>

enum { CB_OFF, CB_PROTANOPIA, CB_DEUTERANOPIA, CB_TRITANOPIA };

/* "off", "protanopia", "deuteranopia" or "tritanopia" (also "none", "protan", "deutan", "tritan"); -1 for anything else. */
int colourblind_parse(const char *name);

/* The filter as one 3 x 3 matrix on (r, g, b) in 0..1 (row-major), for a mode other than CB_OFF; the identity for CB_OFF. */
void colourblind_matrix(int mode, double m[9]);

/* The picture src (w x h ARGB) filtered into dst (same size; may be src); alpha is kept. CB_OFF copies. */
void colourblind_apply(const uint32_t *src, uint32_t *dst, int w, int h, int mode);

#endif
