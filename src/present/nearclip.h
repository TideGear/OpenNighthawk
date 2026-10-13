/* The original model's camera-plane crossing, with its discarded precision. */
#ifndef F117R_NEARCLIP_H
#define F117R_NEARCLIP_H
#include <stdint.h>

/* front/behind are camera xyz, chosen by the projected x-high-word marker.
 * Return the number of midpoint turns, or zero for a nonconvergent pair.
 * coarse is the original projection before the screen origin is added;
 * fine retains the low byte discarded by that projection. Inputs are read
 * only and outputs are written only when a crossing has been found. */
int present_near_crossing(const int32_t front[3], const int32_t behind[3],
                          int32_t coarse[2], double fine[2]);
/* Orthographic 120A:08E1: shift the signed high word, round from the last
 * shifted bit, and sign-extend. Retain full fixed-point precision for fine
 * drawing for shifts 16..31. Shifts 1..15 retain the original wrapped,
 * integer high-word projection; their edges can still draw on a finer grid. */
int present_ortho_project(const int32_t camera[3], int shift,
                          int32_t coarse[2], double fine[2]);
#endif
