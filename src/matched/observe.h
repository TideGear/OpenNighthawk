/* observe.h - what the original draws, as it draws it, for the presentation
 * work (docs/presentation.md). An observer only reads: it is called from
 * matched routines and from hooks on the original's own entry points, never
 * changes guest memory, registers or the clock, and a run with one installed
 * has the same hashes as a run without. It works in the recompiled engine,
 * where the matched routines are placed. */
#ifndef F117R_OBSERVE_H
#define F117R_OBSERVE_H

#include <stdint.h>
#include "machine.h"

/* The primitives the draw list is made of (observe_prim's `kind`; the
 * graphics library's are described at their hooks in observe.c):
 *   'G' an edge prepared (model_prepare_edge): slot, the two projected
 *       vertex records it was made from (DS offsets) - links edges to vertices
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
     * 2 behind the eye, where px[] is not written), and where both records
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

/* Called by the matched routines placed at 130D:004A and 130D:0116, at
 * their entry: an override placed first at an address shadows any placed
 * after it, so these give the observer's records ('E', 'F') there. */
void observe_poly_edge(machine_t *m);
void observe_poly_fill(machine_t *m);

/* Register the hooks on the original's own entry points (once). */
void observe_register(void);

#endif
