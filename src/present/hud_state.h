/* hud_state.h - the flight HUD as a function of the game state, for the 4K presentation
 * (docs/presentation.md, "The state-driven HUD").
 *
 * VGAME's HUD pass (1058:0016) is a pure function of its data segment at entry. A snapshot of the
 * words it reads (the observer's 'h' record, observe.c) therefore holds everything the pass will
 * draw, and this module turns one into drawing primitives in two ways:
 *
 *   EXACT  the original's integer pipeline, ported from the disassembly (work/hudmodel.py): the
 *          library calls the pass makes, in order, after the original's own clipper. Used to
 *          recognise the pass's records in a draw list (hud_cover.h) and checked against the
 *          observer's log by tests/test_hud_state.c.
 *   FINE   the same formulas carried in floating point with no final rounding, from the
 *          continuous state (sub-knot airspeed, fractional altitude, angles), mapped to a picture
 *          N times finer. Text is returned as (fine) top-left positions of bitmap text, lines as
 *          fine strokes; drawing them is hud_draw.c's job.
 *
 * The state that moves between logic steps is five numbers (hud_state.speed .. .roll); everything
 * else in a snapshot is layout or a switch and is taken from the nearer logic frame. hud_blend
 * interpolates the five for an in-between frame. Nothing here reads or writes the machine. */
#ifndef F117R_HUD_STATE_H
#define F117R_HUD_STATE_H

#include <stdint.h>

/* ---- the 'h' record -------------------------------------------------------------------------
 * v[0]            HUD_REC_VERSION
 * v[1 .. 1560]    the data segment's words 0x2080 .. 0x2CAE (tables, layout constants, text blocks)
 * then            HUD_NEXTRA words, one at each of hud_extra_off[] (the scattered words the pass reads)
 * then            one word "have driver tick table", then 4 x (x, low row, high row): the graphics
 *                 driver's tick tables at SI + 4*CL = 0, 2, 4, 6 (observe.c hook_lib_ticks)
 * then, only in an interpolated record, HUD_FINE_MAGIC and the five continuous values in fixed point
 * (speed in 1/1024 kt, altitude in 1/256 ft, heading, pitch and roll in 1/256 of an angle unit) */
#define HUD_REC_VERSION 1
#define HUD_DS_LO 0x2080
#define HUD_DS_HI 0x2CB0
#define HUD_NWORDS ((HUD_DS_HI - HUD_DS_LO) / 2)
#define HUD_NEXTRA 19
#define HUD_REC_BASE (1 + HUD_NWORDS + HUD_NEXTRA + 13)
#define HUD_FINE_MAGIC 0x46494E45
#define HUD_REC_FINE (HUD_REC_BASE + 6)

/* the scattered words of the record, in record order: heading, pitch, roll, altitude (AGL scale), altitude,
 * raw speed, speed, ladder flag, vertical speed, command speed, flight-path reference, frame counter, then the
 * G meter's text and word (9900 .. 990C). In the header so the observer needs no library. */
static const uint16_t hud_extra_off[HUD_NEXTRA] = {
    0x2DEE, 0x2DF0, 0x2DF2, 0x2DF4, 0x2DF6, 0xB07E, 0xB194, 0xE56E, 0xE586, 0xE474, 0x98F6, 0x3D8E,
    0x9900, 0x9902, 0x9904, 0x9906, 0x9908, 0x990A, 0x990C
};

/* data segment offsets the module and its users name */
enum { HUD_HDG = 0x2DEE, HUD_PITCH = 0x2DF0, HUD_ROLL = 0x2DF2, HUD_ALT = 0x2DF6, HUD_SPEED_RAW = 0xB07E,
       HUD_SPEED = 0xB194, HUD_LADDER_ON = 0xE56E };

typedef struct {
    uint8_t ds[0x10000];              /* only the snapshot's words are meaningful */
    int     have_ticks;
    struct { int x, lo, hi; } tick[4];/* by (SI + 4*CL) / 2 */
    /* the continuous state: speed in knots (B07E / 27), altitude in feet, and the three angles in
     * the game's units (65536 = 360 degrees; pitch signed). Fractional in an interpolated state. */
    double  speed, alt, hdg, pitch, roll;
    int     interpolated;
} hud_state;

/* Expand a record into *st. 0 when it is not a version-1 record. */
int hud_state_load(const int32_t *v, int n, hud_state *st);
/* Expand a snapshot straight from 64 KB of data segment (the tests' corpus), tick tables absent. */
void hud_state_from_ds(const uint8_t *ds, hud_state *st);

/* ---- EXACT ----------------------------------------------------------------------------------- */
enum { HP_K, HP_N, HP_T, HP_H, HP_S18 };
#define HUD_MAX_EXACT 224

typedef struct {
    uint8_t  kind;                    /* HP_ */
    uint8_t  keep;                    /* 1: drawn by the original's own record in the 4K picture
                                         (constants, the reticle, the G meter), 0: replaced */
    uint8_t  colour;                  /* HP_K */
    uint8_t  entry;                   /* HP_T: the library entry 1..6 */
    uint8_t  ntext;                   /* HP_T */
    uint8_t  strip;                   /* HP_S18: 1 the heading tick strip, 0 the centre reticle */
    int16_t  x0, y0, x1, y1;          /* HP_N, after the original's clipper */
    int32_t  origin;                  /* HP_N: the library origin in force */
    uint16_t blk[11];                 /* HP_T: the parameter block as the observer logs it */
    uint8_t  text[12];
    int16_t  si, bx, dl, cl;          /* HP_H */
} hud_prim;

typedef struct { int n; hud_prim p[HUD_MAX_EXACT]; } hud_exact_list;

/* The library calls the HUD pass makes on this state, in order (K, N, T, H and the entry-18 blits).
 * 0 on success, -1 when the original would take a divide error (no list). */
int hud_exact(const hud_state *st, hud_exact_list *out);

/* ---- FINE ------------------------------------------------------------------------------------ */
#define HUD_MAX_FINE 320
enum { HF_LINE, HF_TEXT };

typedef struct {
    uint8_t  type;                    /* HF_ */
    uint8_t  colour;
    uint8_t  entry;                   /* HF_TEXT: the clips that apply to it: bit 0 the rows, 1 the left edge, 2 the right (the width) */
    uint8_t  ntext;
    /* HF_LINE: a stroke between two points of the fine picture (pixels, edge-space: a vertex at
     * the centre of cell (x, y) is ((x + .5) N, (y + .5) N)); HF_TEXT: the top-left of the first
     * glyph's cell, in fine pixels */
    double   x0, y0, x1, y1;
    int      cl, ct, cr, cb;          /* the clip rectangle, fine pixels, [cl, cr) x [ct, cb) */
    uint16_t blk[11];                /* HF_TEXT: the block words (mode, colours, font, clips) */
    uint8_t  text[12];
} hud_fprim;

typedef struct { int n, overflow; hud_fprim p[HUD_MAX_FINE]; } hud_fine_list;

/* The picture N times finer. Replaces what the exact list marks keep == 0: tape labels and ticks,
 * heading labels and strip ticks, pitch ladder and its labels. Needs the driver's tick tables
 * (st->have_ticks) for the tapes' ticks; without them those are left out. ex, when not NULL,
 * is the exact list of the same state: the heading strip is drawn only when the original draws it. */
int hud_fine(const hud_state *st, int n, const hud_exact_list *ex, hud_fine_list *out);

/* ---- interpolation --------------------------------------------------------------------------- */
/* The record of an in-between frame: skeleton record s with its continuous state moved the fraction
 * w of the way to record o (angles on the shortest arc). 0 and *out untouched when the two do not
 * share a layout (mode, small flag, layout constants, ladder on/off, readouts on, colours) or are
 * not records. Returns the new record's length (HUD_REC_FINE; HUD_REC_BASE, the skeleton's own record, when w is
 * under a millionth), or 0. */
int hud_blend(const int32_t *s, int ns, const int32_t *o, int no, double w, int32_t *out, int cap);

/* The exact pipeline's own helpers, exported for the tests: the clipper, and the rotated, unclipped ladder
 * vertices of the last hud_exact on this thread */
void hud_debug_ladder(int *nv, const int **rx, const int **ry, int *first_rung);
void hud_debug_fine_ladder(int *nv, const double **rx, const double **ry, int *first_rung);
int hud_clip_line(int x0, int y0, int x1, int y1, int xmax, int ymax, int16_t out[4]);

#endif
