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

typedef struct f117_observer {
    void *user;
    /* The original's per-frame routine (game_draw, VGAME 0x01450) was
     * entered: one call per phase of a picture, twice a logic step. */
    void (*frame_phase)(void *user, uint64_t icount);
    /* A model vertex was projected: the camera-space coordinates (three
     * 32-bit values, the only place sub-pixel precision exists), the pixels
     * the original made of them, and which depth range it took
     * (0 near, 1 mid, 2 behind the eye, where px[] is not written). */
    void (*vertex)(void *user, uint64_t icount, const int32_t xf[3], const int32_t px[2], int range);
} f117_observer;

extern const f117_observer *g_f117_observer;

/* Install (or clear, with NULL) the observer. */
void observe_set(const f117_observer *o);

/* Called by the projection (matched.c) after it has run: DI the camera-space
 * vertex, BX the output, both in DS. */
void observe_vertex(machine_t *m, uint16_t di, uint16_t bx);

/* Register the hooks on the original's own entry points (once). */
void observe_register(void);

#endif
