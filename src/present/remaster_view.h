/* remaster_view.h - the Remastered 3-D view: one picture from one view composition's model requests
 * (docs/remastered-design.md, stage R3-R5).
 *
 * Given the game's data segment (the viewport, the projection centre and the sky and ground band state), the palette and
 * the primitives gathered since the previous composition (remaster_scene.h), this draws the 3-D view at the output
 * size: sky and ground from the band pass's own state, smooth and dithered; then the models with one depth buffer (a
 * tie going to the later draw, in the game's own order), the original's fill styles (one colour, a colour pair, OR and
 * AND into the page), and sub-pixel edges. Pure: it reads the arguments and writes only the picture.
 *
 * Scale: the original's 320 x 200 frame is base_w x h (4x at 1280 x 960). A wider output keeps that scale and adds
 * columns about the projection centre; nothing that was drawn moves. */
#ifndef F117R_REMASTER_VIEW_H
#define F117R_REMASTER_VIEW_H

#include "remaster_scene.h"

#include <stdint.h>

typedef struct { int left, top, right, bottom, centre_x, centre_y; } remaster_view_info;

/* The 3-D window and projection centre the game has published (DS:85FA..8600 and DS:8602/8604), in 320 x 200 pixels. */
void remaster_view_read(const uint8_t *ds, remaster_view_info *v);

/* Draw into rgba (w * h words, 0xAABBGGRR). ds is the data segment's bytes, dac the 256 x 3 six-bit palette. Returns
 * the number of model pixels written. */
long remaster_view_render(const uint8_t *ds, const uint8_t *dac, const remaster_frame *frame, int w, int h, int base_w,
                          uint32_t *rgba);

/* 0xAABBGGRR from a six-bit palette entry. */
uint32_t remaster_dac_rgb(const uint8_t *entry);

#endif
