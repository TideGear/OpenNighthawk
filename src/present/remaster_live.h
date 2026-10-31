/* remaster_live.h - Remastered's view of a running game through the machine's execution hooks
 * (docs/remastered-design.md, stage R3; docs/sound-board.md explains why the hooks live in the machine loop).
 *
 * Five entries of VGAME are watched (machine_t.exec_hook): the model request (120A:00CF, the model, position and the
 * bank at DS:8590 noted), the five places a projection returns to (120A:08D5, 0901, 0937, 096A, 0970: the first one a
 * request reaches finds the game's pose prepared, and remaster_scene_add reads it), and the per-frame draw routine
 * (0000:1450), where the previous frame's primitives are complete. Nothing here changes the guest.
 *
 * A frame can be written as a picture (a first approximation: colours come from the game's current palette by the
 * model's colour word, shades are not applied) so that the geometry can be looked at against the original's frame. */
#ifndef F117R_REMASTER_LIVE_H
#define F117R_REMASTER_LIVE_H

#include "machine.h"

#include <stdint.h>

typedef struct remaster_live remaster_live;

/* out_path: where frame number `frame` (counted from the first per-frame draw seen) is written as a binary PPM, w x h
 * pixels at the 4:3 scales of remaster_raster.h. NULL out_path: nothing is written. */
remaster_live *remaster_live_new(int w, int h, unsigned frame, const char *out_path);
void remaster_live_free(remaster_live *r);

/* Watch the entries for a VGAME loaded at load_seg: appends to m->exec_hook and returns the index of the first. */
unsigned remaster_live_arm(remaster_live *r, machine_t *m, uint16_t load_seg);

/* The machine reached exec_hook[base + k]; k counts from the first entry armed here. Always returns 0 (never takes a
 * call). */
int remaster_live_exec(remaster_live *r, machine_t *m, unsigned k);

/* Compose the Enhanced presentation's fine picture (n times finer: 320n x 200n palette indices in src, the origin tag of
 * each pixel in tag when F117_PRESENT_DIAG is on, dac the six-bit palette) with the last completed frame's Remastered
 * 3D view: pixels the Enhanced picture drew for the 3D view are replaced by the nearest palette entry to the
 * Remastered colour; the HUD strokes (state-driven, on the fine grid), text, sprites and the cockpit art stay. dst
 * receives the result (same size; src is untouched). Returns 1 when anything was replaced. */
int remaster_live_composite(remaster_live *r, const machine_t *m, int n, const uint8_t *src, const uint8_t *tag,
                            const uint8_t *dac, uint8_t *dst, uint32_t *rgb, uint8_t *mask);
/* rgb and mask (320n x 200n, may be NULL) receive the true colour of every replaced pixel and a 1 in mask for it; the
 * display shows those instead of the palette entry of dst, which is how a smooth gradient avoids the palette's steps. */

/* Primitives collected for the frame being drawn, and frames completed. */
size_t remaster_live_prims(const remaster_live *r);
unsigned remaster_live_frames(const remaster_live *r);

#endif
