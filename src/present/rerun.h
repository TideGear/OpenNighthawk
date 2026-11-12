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
/* The Far object order (Video settings): 0 exact, the game-order qsort of the far-scenery picks; 1 fast, a radix sort 0.65 ms cheaper that orders objects tied on distance by their listing order. */
void rerun_set_pick_order(int fast);
/* A thread pool for the far scenery's choosing (a pool of its own: the composite's runs at the same time); parts threads including the caller. */
void rerun_set_parallel(void (*run)(void (*fn)(void *, int), void *arg, int parts), int parts);
int rerun_why_model(unsigned model);   /* diagnostic: F117_FAR_WHY lists the model */
int rerun_draw(const rerun_snap *a, const rerun_snap *b, double t, rerun_list *out, int check);

/* An optional collector for the copies (Remastered's, docs/remastered-design.md "Plan: Remastered from the rerun copy"): a
 * copy runs with its host hooks cleared, so its execution hooks (the snapshot keeps machine_t.exec_hook) reach nobody. With a
 * collector set, each copy gets hooks.exec = exec (and hooks.user = user), and begin(user) is called just before the copy
 * runs and end(user) just after, whether or not it finished. The collector sees the copy's machine, never the real one. */
/* One static terrain object drawn natively (docs/scenery-distance.md, "Native far scenery"): the copy's own placement is skipped and the renderer
 * builds the pose from a template, patching only what depends on the object's place. pos is model_draw's position words (x, the height, minus y;
 * camera-relative, in the level's units), bank and model are what E2B6 hands over, darken is DS:48C4 as the game's detail-level routine gives it,
 * level is the ring scale (1..4) it is drawn in, shrink the bits (2 per ring) its own ring's units are finer than that, and eye the three world
 * longs of the viewpoint (for the ring-rounding correction). */
typedef struct {
    uint16_t bank, model;
    int16_t  pos[3];
    uint8_t  darken;
    int      level, shrink;
    uint32_t eye[3];
} rerun_native;

typedef struct {
    void (*begin)(void *user);
    int  (*exec)(void *user, machine_t *m, unsigned index);
    void (*end)(void *user);
    void *user;
    int  (*native)(void *user, machine_t *m, const rerun_native *n);   /* 1: drawn; 0: no template yet, the copy places it itself */
    void (*placement_return)(void *user, machine_t *m, int returned);
    /* A dot the game draws flat at a screen position (x, y in its 320 x 200 pixels, unclamped) that its own cull dropped because it lies outside the
     * 4:3 window, but which the wide picture shows (R41/R42): the renderer draws it in the margins. colour_word as a model's (FFxxh: palette pixel xx). */
    void (*overlay_dot)(void *user, double x, double y, unsigned colour_word);
    /* The same for a w x h block of the game's pixels at (x, y) (a run of a scaled sprite the game drew clipped at the 4:3 window's edge or dropped); clip_rows non-zero keeps
     * only the rows inside the game's 3D clip window, as the sprite blitter's own clip does. */
    void (*overlay_rect)(void *user, double x, double y, double w, double h, unsigned colour_word, int clip_rows, int layer);
    /* A one-game-pixel-wide line from (x0, y0) to (x1, y1) in the game's pixels (a gun tracer the game skipped because an end is off its screen). */
    void (*overlay_line)(void *user, double x0, double y0, double x1, double y1, unsigned colour_word);
    /* The overlays' layers, in the order the original draws the flat things (later on top): 3 far dots (moving-objects pass, 983E), 2 smoke puffs (deferred, drawn by the compose walk D87C
     * after them), 1 tracers and 0 the blast (the display pass 9EFA, the last thing the frame draws; tracers before the blast). Each layer sits at its own depth, 0.2 percent apart, so a
     * tie cannot reorder them. */
} rerun_collector;
void rerun_set_collector(const rerun_collector *c);   /* NULL clears it */
/* The copies' view widened to this percent (100 = the game's own): the cull's side planes only, in the copy only. */
void rerun_set_wide(int percent);
/* The extra terrain walks' reach in world units (0 turns them off; the 16-bit limit of the guest's cell offsets caps every ring, whatever is asked). */
void rerun_set_distance(int units);
/* Nonzero while the copy runs its coarse-scale passes: ring-1 objects drawn at ring 2's scale, whose model vertices the renderer shrinks by 4. */
int rerun_coarse_active(void);
/* Nonzero while the copy runs any extra terrain walk (not the game's own): the renderer takes those models from their geometry, so the copy skips the game's own face drawing for them. */
int rerun_extra_active(void);
/* F117_PROFILE: stamp a named moment of the copy being run (its instruction count and the wall clock); the collector calls it at the frame's own landmarks. */
void rerun_prof_mark(int label);
/* The scale difference, in bits (2 per ring), between the unit the object being placed is drawn in and its own ring's: the renderer shrinks its model by that much. */
int rerun_scale_shift(void);

#endif
