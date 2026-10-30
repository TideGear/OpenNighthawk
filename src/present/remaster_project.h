/* remaster_project.h - the original's perspective projection, exact and with its sub-pixel part kept
 * (docs/remastered-design.md, stage R3).
 *
 * The game projects a camera-space vertex (three 32-bit coordinates; VGAME 0x129A2, matched.c) with integer
 * divides: by the high word of z when that is at least 0x100, (x >> 8) / z_high; for a nearer vertex, from 1 to
 * 0xFF, (x >> 1) / ((z >> 8) >> 1); below 1 the vertex is behind the eye. The quotient is the pixel offset from the
 * view's centre; the remainder the divide throws away is the sub-pixel position. Checked against the game: all
 * 7,359 projected coordinates of a flight (both ranges, both axes) are reproduced exactly by
 * remaster_pixel_exact().
 *
 * A renderer that wants the sub-pixel part uses remaster_project(): the same projection without the truncation,
 * 256 x / z in the same units, so truncating it towards zero gives the original's pixel to within the one
 * the divisor's own truncation can move. Widescreen only needs a wider range of x for the same scale. */
#ifndef F117R_REMASTER_PROJECT_H
#define F117R_REMASTER_PROJECT_H

#include <stdint.h>

enum { REMASTER_NEAR = 0, REMASTER_MID = 1, REMASTER_BEHIND = 2 };

/* Which range a vertex's z falls in (the game's own test on the high word of z). */
int remaster_range(int32_t z);

/* The original's pixel offset for one coordinate: the exact integer rule. Returns 1 and sets *pixel for the near
 * and mid ranges; 0 for a vertex behind the eye (the game writes a sentinel) or a divisor of zero. */
int remaster_pixel_exact(int32_t v, int32_t z, int *pixel);

/* The projected offset in pixels with its fraction, 256 v / z; 0 if z is not in front of the eye. */
int remaster_project(int32_t v, int32_t z, double *offset);

#endif
