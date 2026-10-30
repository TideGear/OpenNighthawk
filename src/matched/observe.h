/* observe.h - what the original draws, as it draws it, for the presentation
 * work (docs/presentation.md). An observer only reads: it is called from
 * matched routines and from hooks on the original's own entry points, never
 * changes guest memory, registers or the clock, and a run with one installed
 * has the same hashes as a run without. It works in both engines and timing profiles; matched bodies emit the
 * same records as the hooks on the original paths. */
#ifndef F117R_OBSERVE_H
#define F117R_OBSERVE_H

#include <stddef.h>
#include <stdint.h>
#include "machine.h"

/* The primitives the draw list is made of (observe_prim's `kind`; the
 * graphics library's are described at their hooks in observe.c):
 *   'G' an edge prepared (model_prepare_edge): slot, the two projected
 *       vertex records it was made from (DS offsets) - links edges to vertices;
 *       then raw screen-origin words x/y, and for each endpoint camera x/y/z,
 *       projected x/y and the exact behind-eye marker (x high word 8000h).
 *       Field 18 is the projection mode (0 perspective, otherwise shift).
 *       Older logs have only the first three or 17 fields.
 *   'E' an edge of a filled polygon handed to the rasteriser (130D:004A):
 *       slot, x0, y0, x1, y1 (screen, 32-bit), the slot's status word, and
 *       the near-plane crossing kept at slot + 10h (x, y)
 *   'F' a filled polygon closed and filled (130D:0116): the colour word, the
 *       viewport (xmin, ymin, xmax, ymax), the accumulator (flags, lmin,
 *       lmax, rmin, rmax, drew, nclip), the top span row, the row count and
 *       each row's left and right bound
 *   'B' a fill or an outline polygon begun (1377:004C; the fill entry calls
 *       it, so it follows each 'F'): the colour word
 *   'R' the fill paints (1377:005E): the colour word, the top row, the row
 *       count and each row's bounds, after the near-clip join (an 'E' between
 *       'F' and 'R') and the border runs
 *   'U' a polygon handed to the span rasteriser (0FB2:01E6, fill_polygon): the
 *       colour byte, the corner count and each corner's x and y (screen words
 *       before the rasteriser's clip); the span fills it makes follow as 'Q'
 *   'L' an edge of an outline polygon drawn as a line (1377:0055): slot,
 *       x0, y0, x1, y1, the colour word, the page segment, rows 0 and 1 of
 *       the row table, and the line handler's offset in 1377 */
typedef struct f117_observer {
    void *user;
    /* The original's per-frame routine (game_draw, VGAME 0x01450) was
     * entered: one call per phase of a picture, twice a logic step. */
    void (*frame_phase)(void *user, uint64_t icount);
    /* A model vertex was projected: the camera-space coordinates (three
     * 32-bit values, the only place sub-pixel precision exists), the pixels
     * the original made of them, which depth range it took (0 near, 1 mid,
     * 2 behind the eye, where px[] is not written; 3 orthographic), and where both records
     * are in DS (the projected record is what an edge refers to). */
    void (*vertex)(void *user, uint64_t icount, const int32_t xf[3], const int32_t px[2], int range,
                   uint16_t xf_at, uint16_t px_at);
    /* A draw-list primitive (see above): n values in v. */
    void (*prim)(void *user, uint64_t icount, char kind, const int32_t *v, int n);
    /* Nonzero: the records carry what a replay needs beyond the primitives,
     * a blit's and a whole-page copy's source bytes, as F117R_OBSERVE_PAGES
     * gives a log. (A log also has the byte changes of each graphics entry,
     * 'x', from a page snapshot at every entry, to check the rules with.) */
    int sources;
    /* Asked at each game_draw (may be NULL): nonzero to have the pages at
     * this phase ('Z' the work page, 'Y' the display, 'J' the palette), as
     * F117R_OBSERVE_PAGES has them at every phase. A live replay asks until
     * it has its first 'Z'. */
    int (*want_pages)(void *user);
    /* Nonzero: the HUD pass (VGAME 1058:0016) hands over the state it starts from as a record 'h' (hud_state.h
     * describes it: the data segment's words the pass reads and the driver's tick tables), once per pass, just
     * before the pass's own library calls. Off by default so that existing logs stay as they were. */
    int hud;
    /* Nonzero: the sky and ground band pass (VGAME 0000:1450) hands over the state it starts from as a record 'o'
     * (band_state.h: the data segment's words the pass reads), once per call, after the phase's pages. Off by
     * default so that existing logs stay as they were. */
    int bands;
    /* Opt-in model-input research capture (scene_capture.h), before model
     * selection/culling. want_scene may bound requests to a clock window
     * and count; NULL accepts every request when scene is enabled. */
    int scene;
    int (*want_scene)(void *user, uint64_t icount);
    /* Nonzero: every model request (VGAME 120A:00CF at entry) logs a compact record 'I' (scene_capture.h) with the
     * model index, caller chain and the world-cell words that name the object (research for stable identity; the
     * words are raw and unproven). Off by default so that existing logs stay as they were. */
    int ident;
    /* The view composition (VGAME 0000:2F9D) was entered: the machine as it stands there, before any of it runs, for a
     * presentation that re-runs the original's own drawing on a copy (docs/presentation.md, "The current plan for
     * 60+ fps"). It may read and copy the machine; it must not change it. NULL: not called. */
    void (*compose)(void *user, machine_t *m);
    /* A sound request the game's priority gate (VGAME 0D3F9) admits: the sound id and its priority. A nonzero return
     * means the host plays it itself (hybrid sound: a recording), so the request is not passed to the DOS driver. NULL:
     * not called, and every admitted request goes to the driver as the original does. */
    int (*sound)(void *user, uint16_t id, int16_t priority);
} f117_observer;

extern const f117_observer *g_f117_observer;

/* Install (or clear, with NULL) the observer. */
void observe_set(const f117_observer *o);

/* Called by the projection (matched.c) after it has run: DI the camera-space
 * vertex, BX the output, both in DS. */
void observe_vertex(machine_t *m, uint16_t di, uint16_t bx);

/* Called by model_prepare_edge (matched.c) at its entry: BP the slot, DI and
 * BX the two projected records less 0xD6B4. */
void observe_edge_prepared(machine_t *m, uint16_t slot, uint16_t di, uint16_t bx);

/* Called by the matched routines placed at 1450, 130D:004A and 130D:0116, at
 * their entry: an override placed first at an address shadows any placed
 * after it, so these give the observer's records ('E', 'F') there. */
void observe_game_draw(machine_t *m);   /* VGAME 1450: the frame phase and the pages the frame is rebuilt from */
void observe_poly_edge(machine_t *m);
void observe_poly_fill(machine_t *m);
void observe_span_polygon(machine_t *m);   /* fill_polygon's entry: the corners handed to the span rasteriser ('U') */

/* Called by the matched HUD pass (matched.c) at its entry: the state the pass starts from, as an 'h' record
 * when the observer asks for one. Reads guest memory directly; changes nothing. */
void observe_hud_pass(machine_t *m);

/* Called by the matched panel pass (matched.c, VGAME 0000:79BE) at its entry: the state the pass's HUD items are
 * made from, as a record 'p' when the observer asks for the HUD state. */
void observe_panel_pass(machine_t *m);

/* Called by the matched per-frame update (VGAME 0000:3ADA) just before it blits the player's arrow onto the moving
 * map: the arrow's position words and sprite, as a record 'm' when the observer asks for the HUD state. */
void observe_map_marker(machine_t *m);
void observe_scene_model(machine_t *m); /* VGAME 120A:00CF at entry, before its prologue */
void observe_frame_compose(machine_t *m); /* VGAME 0000:2F9D at entry (the matched body calls it: its placement shadows ours) */

/* The observer's private state (pending draw records and the prepared-pose callback plus its pending request): a copy
 * of the machine run with another observer must start without it and must not disturb the real run's. Save it, clear it,
 * run the copy, restore it. */
size_t observe_pending_size(void);
void observe_pending_save(void *dst);
void observe_pending_restore(const void *src);
void observe_pending_clear(void);

/* Register the hooks on the original's own entry points (once). */
void observe_register(void);

#endif
