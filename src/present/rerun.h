/* rerun.h - in-between pictures drawn by the original itself (docs/presentation.md, "The current plan for 60+ fps").
 *
 * At each view composition (VGAME 0000:2F9D) the presentation keeps a snapshot of the machine. A picture between two
 * logic frames is made by running a throwaway copy of the earlier snapshot, with the inputs the composition reads -
 * the aircraft's position and attitude, the computer-flown aircraft and the weapons in flight - moved the fraction t
 * of the way to the later snapshot's, and collecting the draw records the original makes from there to its present.
 * The copy never reaches the real machine: its CPU calls back into the copy, its hooks, log, input recorder and OPL
 * schedule are cleared, and the observer's pending state is saved and restored around it. It runs on the machine's
 * own thread (the observer and the recompiled-code runtime are process globals). */
#ifndef F117R_RERUN_H
#define F117R_RERUN_H

#include "machine.h"
#include "interp.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    machine_t m;
    uint8_t  *mem;           /* MEM_SIZE bytes, or NULL */
    void     *fix_pending;   /* the fixes' host state at the same moment */
    int       valid;
} rerun_snap;

void rerun_snap_take(rerun_snap *s, const machine_t *m);
void rerun_snap_free(rerun_snap *s);
void rerun_snap_swap(rerun_snap *a, rerun_snap *b);

/* A copy's records, from the composition's entry to the phase after the original's present (the frame's close). The
 * presentation's frames begin at a phase (VGAME 1450, inside the composition), so a frame's drawing is the records
 * from `first` on. */
typedef struct {
    int32_t    *vals;
    size_t      nvals, cvals;
    interp_rec *rec;
    int         nrec, crec;
    int         presented;    /* the copy reached graphics entry 44 */
    int         first;        /* the first record after the copy's first frame phase (VGAME 1450), or -1 */
    uint64_t    insn;         /* instructions the copy ran */
    uint64_t    checks, changed, blocked; /* cumulative isolation diagnostics */
} rerun_list;

void rerun_list_free(rerun_list *l);

/* Run a copy of a with its inputs moved the fraction t (0..1) towards b (b NULL or t 0: unchanged), and collect its
 * records into out. Returns 1 when the copy presented and its frame closed. Call only on the thread that runs the machine. */
int rerun_draw(const rerun_snap *a, const rerun_snap *b, double t, rerun_list *out, int check);

#endif
