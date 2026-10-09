/* interp.h - Stage 3 (docs/presentation.md): in-between frames of two
 * consecutive logic frames, tools/interp_frame.py in C and held to it by
 * tests/test_interp.c.
 *
 * A logic frame's draw list is parsed into batches (the vertices one model
 * chunk projects), the painted polygons in them, the span fills, outline
 * edges and HUD lines. Two frames are paired: batches by a longest common
 * subsequence of compatible batches, polygons inside a paired batch by their
 * edge slots, span fills by colour, mode and order, outline edges by slot
 * and colour, HUD lines by colour, page and order. An in-between frame at t
 * is the nearer frame's list (the skeleton) with every paired primitive moved
 * the fraction of the way to its pair, polygons re-walked into span rows by
 * the original's edge rules; everything else stays as the skeleton has it.
 * Drawn by the Stage 1 replay from the pages at the skeleton's start, t = 0
 * and t = 1 are the two frames themselves. */
#ifndef F117R_INTERP_H
#define F117R_INTERP_H

#include <stdint.h>

/* One record of a draw list: its kind, clock and values (not owned). */
typedef struct {
    char kind;
    uint64_t icount;
    const int32_t *v;
    int n;
} interp_rec;

typedef struct interp_frame interp_frame;
typedef struct interp_pairing interp_pairing;

/* Counters the study reports (interp_frame.py's names), indexed by
 * INTERP_xxx; interp_stat_name gives each name. */
enum {
    IS_POLYS_MOVED, IS_SPANS, IS_SPANS_MOVED, IS_HUD, IS_HUD_MOVED, IS_OUTLINE, IS_OUTLINE_MOVED,
    IS_EDGES_CAMERA, IS_EDGES_FELL_BACK, IS_EDGES_SCREEN, IS_REGENERATED,
    IS_HELD_UNPAIRED, IS_HELD_STYLE, IS_HELD_CLIP, IS_HELD_EDGE_MOTION, IS_HELD_NOTHING,
    IS_HELD_SPAN_NO_PAIR, IS_HELD_SPAN, IS_HELD_HUD_GROUP, IS_HELD_HUD_MOTION,
    IS_HELD_OUTLINE_BATCH, IS_HELD_OUTLINE_NO_PAIR,
    IS_COUNT
};
const char *interp_stat_name(int k);

/* Parse a frame's records (P, Z, Y and J records are skipped). The records
 * must outlive the frame. */
interp_frame *interp_parse(const interp_rec *rec, int n);
void interp_frame_free(interp_frame *f);

interp_pairing *interp_pair(const interp_frame *a, const interp_frame *b);
void interp_pairing_free(interp_pairing *p);

/* The in-between list at t (0 <= t <= 1) into *out (grown as needed; the
 * values of regenerated records live in *pool). Returns its length; *skel_b
 * says whether the skeleton is b (draw from b's start) or a. stats, if not
 * NULL, is added to. */
typedef struct {
    interp_rec *rec;
    int *off;                         /* where a regenerated record's values are in pool, else -1 */
    int n, cap;
    int32_t *pool;
    int pool_n, pool_cap;
} interp_list;
int interp_inbetween(const interp_frame *a, const interp_frame *b, const interp_pairing *p, double t,
                     interp_list *out, int *skel_b, uint64_t *stats);
void interp_list_free(interp_list *l);

/* For reports: polygons in both frames and how many paired, batches and how
 * many paired, the causes of unpaired polygons (5 counters, interp_cause_name),
 * the vertex motion between the frames (max of |dx|, |dy| per vertex of the
 * paired batches; *motion is malloc'd), and whether the pair is a cut. */
typedef struct {
    uint64_t polys, polys_paired, batches, batches_paired, cause[5];
} interp_causes;
const char *interp_cause_name(int k);
void interp_pair_causes(const interp_pairing *p, interp_causes *c);
int interp_pair_motion(const interp_pairing *p, int **motion);
int interp_is_cut(const interp_pairing *p);

#endif
