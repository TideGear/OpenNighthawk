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
#include "gpu_compose.h"

typedef struct remaster_live remaster_live;

/* out_path: where frame number `frame` (counted from the first per-frame draw seen) is written as a binary PPM, w x h
 * pixels at the 4:3 scales of remaster_raster.h. NULL out_path: nothing is written. */
remaster_live *remaster_live_new(int w, int h, unsigned frame, const char *out_path);
void remaster_live_free(remaster_live *r);

/* Watch the entries for a VGAME loaded at load_seg: appends to m->exec_hook and returns the index of the first. */
unsigned remaster_live_arm(remaster_live *r, machine_t *m, uint16_t load_seg);

/* A collector that gathers the model requests of a rerun copy (rerun.h): reset before the copy runs. After it the copy's
 * completed frame is the one remaster_live_composite and remaster_live_wide use. */
void remaster_live_copy_begin(remaster_live *r);

/* The machine reached exec_hook[base + k]; k counts from the first entry armed here. Always returns 0 (never takes a
 * call). */
int remaster_live_exec(remaster_live *r, machine_t *m, unsigned k);

/* While set (on the calling thread), remaster_live_compose reads colours from this 768-byte six-bit palette instead of the machine's: the composite can then
 * run on another thread than the machine's, with the palette of the frame it belongs to. NULL: the machine's. */
void remaster_live_set_palette(const uint8_t *dac);
/* The GPU compositor (gpu_compose.h), registered by the app: given a composite's raster calls and the replace step's inputs it writes the wide picture and returns 1, or
 * returns 0 and the CPU compositor is used. NULL: always the CPU. F117_GPU_COMPOSE_CHECK also runs the CPU compositor and compares. */
typedef int (*remaster_gpu_fn)(const gpu_compose_in *in, uint32_t *wide_out);
typedef int (*remaster_gpu_begin_fn)(const gpu_compose_in *in);
typedef int (*remaster_gpu_finish_fn)(const uint8_t *src, const uint8_t *tag, const uint8_t *dac, uint32_t *wide_out);
void remaster_live_set_gpu(remaster_gpu_fn run, remaster_gpu_begin_fn begin, remaster_gpu_finish_fn finish);
/* Starts the GPU compositor on the scene before the game's own picture exists, so the two overlap; the next remaster_live_compose of this scene finishes it. 1 when started. */
int remaster_live_gpu_begin(remaster_live *r, const machine_t *m, int n, int wide_w);
/* Terrain dots: how many coarser levels beyond its own ring an object's dark dots are drawn (0 the original's reach .. 3 the whole map). */
void remaster_live_set_dot_levels(int levels);
/* A static terrain object drawn without the game's placement (rerun.h, rerun_native): 1 when drawn, 0 when no template of its model exists yet
 * for this frame's camera (the caller lets the copy place it, which makes one). F117_NATIVE_CHECK=1 compares each with the game's own. */
struct rerun_native_s;
int remaster_live_native(remaster_live *r, machine_t *m, const void *spec);
void remaster_live_placement_return(remaster_live *r, machine_t *m, int returned);

/* Compose the Enhanced presentation's fine picture (n times finer: 320n x 200n palette indices in src, the origin tag of
 * each pixel in tag when F117_PRESENT_DIAG is on, dac the six-bit palette) with the last completed frame's Remastered
 * 3D view: pixels the Enhanced picture drew for the 3D view are replaced by the nearest palette entry to the
 * Remastered colour; the HUD strokes (state-driven, on the fine grid), text, sprites and the cockpit art stay. dst
 * receives the result (same size; src is untouched). Returns 1 when anything was replaced. */
int remaster_live_composite(remaster_live *r, const machine_t *m, int n, const uint8_t *src, const uint8_t *tag,
                            const uint8_t *dac, uint8_t *dst, uint32_t *rgb, uint8_t *mask);
/* rgb and mask (320n x 200n, may be NULL) receive the true colour of every replaced pixel and a 1 in mask for it; the
 * display shows those instead of the palette entry of dst, which is how a smooth gradient avoids the palette's steps. */

#define REMASTER_PARTS 8

/* Let the composite share a picture's rows among workers: run(fn, arg, parts) must call fn(arg, 0) to fn(arg, parts - 1), in any
 * order or at once, and return when all are done. Without it (or parts 1) everything runs on the calling thread. */
void remaster_live_set_parallel(void (*run)(void (*fn)(void *, int), void *arg, int parts), int parts);

/* The composite, and with wide_w > 320n the widescreen picture too (the fit rule, docs/remastered-design.md): the world is drawn once
 * across a canvas wide_w pixels wide and 200n tall at the game's own projection scale, with its origin shifted to keep the crosshair at
 * the centre; the bands run off every side and nothing is cut at the clip window. The game's own picture is laid in the middle at its own
 * size (Remastered's pixel where it replaced one, the original's elsewhere), so the HUD and cockpit are uncropped. wide_out is wide_w x
 * 200n, 0xAARRGGBB; it needs rgb and mask (the composite's own outputs). wide_w 0: the plain composite. */
int remaster_live_compose(remaster_live *r, const machine_t *m, int n, const uint8_t *src, const uint8_t *tag,
                          const uint8_t *dac, uint8_t *dst, uint32_t *rgb, uint8_t *mask, int wide_w, uint32_t *wide_out);

/* Primitives collected for the frame being drawn, and frames completed. */
size_t remaster_live_prims(const remaster_live *r);
size_t remaster_live_done_prims(const remaster_live *r);      /* the last completed frame's */
unsigned remaster_live_frames(const remaster_live *r);

#endif
