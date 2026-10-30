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

/* Primitives collected for the frame being drawn, and frames completed. */
size_t remaster_live_prims(const remaster_live *r);
unsigned remaster_live_frames(const remaster_live *r);

#endif
