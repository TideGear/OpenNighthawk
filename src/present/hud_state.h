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
/* HP_S18: a blit of the HUD pass (entry 18; recognised by kind alone, as it always was). HP_S: a sprite of the panel
 * pass (entry 71 or 73), recognised by its entry and its block. */
enum { HP_K, HP_N, HP_T, HP_H, HP_S18, HP_S };
#define HUD_MAX_EXACT 224

/* the items of the two passes (hud_prim.item) */
enum { HI_NONE, HI_ASPD, HI_ALT, HI_MARKS, HI_HDG, HI_STRIP, HI_FRAME, HI_RETICLE, HI_G, HI_LADDER, HI_LADDER_LABEL,
       PI_STATION, PI_GUN, PI_FPM, PI_CROSS, PI_STALL, PI_VS, PI_SINK, PI_SPEED, PI_ALT, PI_HDGNUM, PI_BOX, PI_ACCEL,
       PI_TRAINING, PI_CUE, PI_MESSAGE, HI_COUNT };

#define HUD_TEXT_MAX 47               /* the longest string a pass prints (a panel message) */
typedef struct {
    uint8_t  kind;                    /* HP_ */
    uint8_t  keep;                    /* 1: drawn by the original's own record in the 4K picture, 0: replaced */
    uint8_t  colour;                  /* HP_K */
    uint8_t  entry;                   /* HP_T: the library entry 1..6; HP_S18: 18, 71 or 73 */
    uint8_t  ntext;                   /* HP_T */
    uint8_t  strip;                   /* HP_S18: 1 the heading tick strip, 0 the centre reticle */
    uint8_t  item;                    /* which item of the pass (HI_ / PI_), for reports */
    int16_t  x0, y0, x1, y1;          /* HP_N, after the original's clipper */
    int32_t  origin;                  /* HP_N: the library origin in force */
    uint16_t blk[11];                 /* HP_T: the parameter block as the observer logs it; HP_S18: the blit's
                                         eight words (source segment, x, y, page, destination x, y, width, height) */
    uint8_t  text[HUD_TEXT_MAX + 1];
    int16_t  si, bx, dl, cl;          /* HP_H */
} hud_prim;

typedef struct { int n; hud_prim p[HUD_MAX_EXACT]; } hud_exact_list;

/* The library calls the HUD pass makes on this state, in order (K, N, T, H and the entry-18 blits).
 * 0 on success, -1 when the original would take a divide error (no list). */
int hud_exact(const hud_state *st, hud_exact_list *out);

/* ---- FINE ------------------------------------------------------------------------------------ */
#define HUD_MAX_FINE 320
/* HF_SPRITE: a bitmap the original blits (the reticle, the flight-path marker): drawn when its record arrives, from the
 * record's own art, at x0, y0 (fine pixels, the top-left); `item` says which. */
enum { HF_LINE, HF_TEXT, HF_SPRITE };

typedef struct {
    uint8_t  type;                    /* HF_ */
    uint8_t  colour;
    uint8_t  entry;                   /* HF_TEXT: the clips that apply to it: bit 0 the rows, 1 the left edge, 2 the right (the width) */
    uint8_t  ntext;
    uint8_t  item;                    /* HI_ / PI_ */
    /* HF_LINE: a stroke between two points of the fine picture (pixels, edge-space: a vertex at
     * the centre of cell (x, y) is ((x + .5) N, (y + .5) N)); HF_TEXT: the top-left of the first
     * glyph's cell, in fine pixels */
    double   x0, y0, x1, y1;
    int      cl, ct, cr, cb;          /* the clip rectangle, fine pixels, [cl, cr) x [ct, cb) */
    uint16_t blk[11];                /* HF_TEXT: the block words (mode, colours, font, clips) */
    uint8_t  text[HUD_TEXT_MAX + 1];
} hud_fprim;

typedef struct { int n, overflow; hud_fprim p[HUD_MAX_FINE]; } hud_fine_list;

/* The picture N times finer. Replaces what the exact list marks keep == 0: tape labels and ticks,
 * heading labels and strip ticks, pitch ladder and its labels. Needs the driver's tick tables
 * (st->have_ticks) for the tapes' ticks; without them those are left out. ex, when not NULL,
 * is the exact list of the same state: the heading strip is drawn only when the original draws it. */
int hud_fine(const hud_state *st, int n, const hud_exact_list *ex, hud_fine_list *out);

/* ---- interpolation --------------------------------------------------------------------------- */
/* The record of an in-between frame: skeleton record s with its continuous state moved the fraction
 * w of the way to record o (angles on the shortest arc). The skeleton's own words stay first, so the
 * records of the skeleton's pass are still recognised from them (EXACT).
 *
 * hold says where the discrete state (texts, numbers, modes, switches: everything but the five
 * continuous values) comes from: 0 the skeleton (it switches with the skeleton, at t = 0.5); 1 the
 * other record o, which is then the older frame: it is appended after HUD_HOLD_MAGIC, and the picture is
 * drawn from it (hud_state_load_drawn) until the newer frame itself is shown, at t = 1; -1 the skeleton,
 * which is the older frame. When the two do not share a layout (mode, small flag, layout constants,
 * ladder on/off, readouts on, colours) nothing moves: with hold, the older frame's own state is drawn;
 * without, 0 is returned and *out is untouched. Returns the new record's length (HUD_REC_BASE, the
 * skeleton's own record, when w is under a millionth and nothing is held), or 0. */
#define HUD_HOLD_MAGIC 0x484F4C44
#define HUD_REC_HELD (HUD_REC_FINE + 2 + HUD_REC_BASE)
int hud_blend(const int32_t *s, int ns, const int32_t *o, int no, double w, int hold, int32_t *out, int cap);

/* The state the picture is drawn from: the held (older) frame's words when the record carries them, else its
 * own, with the continuous values of an interpolated record. 0 when v is not a record. */
int hud_state_load_drawn(const int32_t *v, int n, hud_state *st);

/* ---- the panel pass's items: the flight-path marker and the two bars ----------------------------------------
 * The panel pass (0000:79BE) runs before the HUD pass and draws the digital readouts, the stall-margin bar
 * (x = 72), the vertical-speed bar (x = 247), the heading cue, the flight-path marker and the weapon texts. Three of
 * those are a function of one or two state words each, and keep their state in the record 'p' the observer takes at the
 * pass's entry: the flight-path marker (sprite, entry 71, 11 x 7, at x = 154, y = 48 + [98F6] / 64, full HUD), the
 * vertical-speed bar ((247, 52) to (247, 52 - [E586] / 16), clamped to rows 20 to 81) and the stall bar ((72, 81)
 * up to 81 - n, n = ((command speed - speed) * 2 / 5 + 29) clamped to 0..57). A record that arrives after a 'p' is
 * recognised as one of these by its geometry and its value (the integer formulas); the heading cue, the digital numbers
 * and the other items are not covered.
 * v[0] HUD_PANEL_VERSION, then the words [98F6], [E586], [E474], [B07E], [B194], [294A], [294B]; an interpolated
 * record appends HUD_PANEL_FINE_MAGIC and four fixed-point values (flight-path reference, vertical speed, command speed
 * in 1/256, speed in 1/1024 knot). */
#define HUD_PANEL_VERSION 1
#define HUD_PANEL_N 8
#define HUD_PANEL_FINE_MAGIC 0x50414E45
#define HUD_PANEL_REC_FINE (HUD_PANEL_N + 5)

/* The extended record (still version 1, so a reader of the first eight words is unaffected): after them
 * HUD_PANEL_EXT_MAGIC, the two words of the settings record the panel pass reaches through the far pointer [9924]
 * (+40h, the sink marker's divisor, and +42h, which hides the gun text), the number of pieces and the pieces of
 * the data segment the rest of the pass reads (each: offset, length, the bytes; hud_panel_seg below, then the two
 * text blocks [4010] and [4028], the selected station's name, the message line C5A4 and the target record of
 * [E00C]). An interpolated record replaces the first eight words' continuous values after
 * HUD_PANEL_FINE2_MAGIC (flight-path reference, vertical speed, command speed in 1/256, speed in 1/1024 knot,
 * heading in 1/256 of an angle unit, the map position x and y in 1/256), and a held record appends the older
 * frame's own record after HUD_HOLD_MAGIC and its length. */
#define HUD_PANEL_EXT_MAGIC 0x50325850
#define HUD_PANEL_FINE2_MAGIC 0x50464E32
#define HUD_PANEL_NFINE 7
#define HUD_PANEL_MAX 1024
static const uint16_t hud_panel_seg[][2] = {
    { 0x2DF6, 2 }, { 0x368C, 2 }, { 0xC0A6, 2 }, { 0xE32A, 2 }, { 0x2CA0, 2 }, { 0x3680, 2 }, { 0x3682, 2 }, { 0x9B34, 2 },
    { 0xB2B8, 2 }, { 0x368A, 2 }, { 0xE00C, 2 }, { 0x3DA8, 2 }, { 0x3664, 2 }, { 0x2EAE, 2 }, { 0xC0D0, 2 }, { 0xC0DE, 2 },
    { 0x2DEE, 2 }, { 0x40CC, 2 }, { 0x368E, 2 }, { 0x40B6, 1 }, { 0xDF06, 2 }, { 0x294A, 2 }, { 0x3D8E, 2 }, { 0x4010, 2 },
    { 0x4028, 2 }, { 0xE586, 2 }, { 0xE474, 2 }, { 0xB194, 2 }, { 0x98F6, 2 }, { 0xB07E, 2 }, { 0x3670, 16 }, { 0x2E9E, 16 },
    { 0x40CE, 0x2A }, { 0x991E, 2 }, { 0x40CA, 2 }, { 0xE584, 2 }, { 0x3666, 2 }, { 0x40A0, 8 }
};
#define HUD_PANEL_NSEG ((int)(sizeof hud_panel_seg / sizeof hud_panel_seg[0]))

typedef struct {
    int    fpm, vs, cmd, raw, speed, mode, half;   /* the words (fpm and vs signed) */
    double c_fpm, c_vs, c_cmd, c_speed;            /* the continuous values: the words, or interpolated */
    /* the extended record */
    int    ext, far40, far42;
    double c_hdg, c_x, c_y;                        /* heading and map position, continuous */
} hud_panel;

enum { HPI_NONE, HPI_FPM, HPI_VS, HPI_STALL };
typedef struct {
    int    kind;                                   /* HPI_ */
    double x0, y0, x1, y1;                         /* HPI_VS, HPI_STALL: a stroke's vertices in cell index space; HPI_FPM: x0, y0 the sprite's top-left */
} hud_panel_item;

/* 1 when v is a 'p' record. With ds (64 KB, may be NULL) an extended record's pieces are written into it (the rest
 * zero) and p->ext is set. */
int hud_panel_load(const int32_t *v, int n, hud_panel *p);
int hud_panel_load_ds(const int32_t *v, int n, hud_panel *p, uint8_t *ds);
/* The base record of a 'p' (its own words, without an interpolated or held part): its length */
int hud_panel_base_len(const int32_t *v, int n);
/* The drawn state, as hud_state_load_drawn: the held record's words when there is one, with the continuous values */
int hud_panel_load_drawn(const int32_t *v, int n, hud_panel *p, uint8_t *ds);
/* The state of 'p' from the words of a snapshot (tests) */
void hud_panel_from_ds(const uint8_t *ds, hud_panel *p);
/* The interpolated record, as hud_blend does for the HUD's (hold the same). */
int hud_panel_blend(const int32_t *s, int ns, const int32_t *o, int no, double w, int hold, int32_t *out, int cap);
/* Is the record (kind 'S' or 'N', the observer's values) one of the items of this state, and where does the fine picture
 * put it. With the state's integer words the position is the original's floor or ceiling. (The v1 items only.) */
int hud_panel_classify(const hud_panel *p, char kind, const int32_t *v, int n, hud_panel_item *out);

/* The panel pass (0000:79BE) on an extended record's state: EXACT, the library calls it makes for the HUD's items (in
 * order; other calls of the pass, the cockpit lamps, the navigation display, the ILS and the weapon symbology, come
 * between them and are not listed), and with fl, FINE, the same items from the continuous values of p at N. ds is the
 * state (hud_panel_load_ds). 0, or -1 when the state is not one this model covers (no extended record, the front
 * cockpit hidden). */
int hud_panel_exact(const uint8_t *ds, const hud_panel *p, int N, hud_exact_list *ex, hud_fine_list *fl);

/* The original's bearing (0000:C702) and distance (0000:C6B3), for the tests */
int hud_bearing(int x, int y);
double hud_bearing_f(double x, double y);

/* ---- the moving map's aircraft marker ('m') ----------------------------------------------------------------------
 * The per-frame update (0000:3ADA) blits the player's 4 x 4 arrow onto the cockpit map: entry 73 through 114A:03FD,
 * source (100h + 4 ((([2DEE] + 1000h) >> 13) & 7), 4) on page [E584], destination (x - 1, y - 1) with
 * x = (([C0D0] - [40B2]) >> (10 - [40AE])) + 70h and y = (((([C0DE] - [40B4]) >> (10 - [40AE])) * 3) >> 2) + 8Bh, while the
 * cockpit is shown ([368C]) and the map is on ([DF06] = 0). The record, taken just before the blit: HUD_MAP_VERSION,
 * then [C0D0], [C0DE], the 32-bit positions [BA3C] and [C056] (low, high), [40B2], [40B4], [40AE], [2DEE], the x and y
 * the routine computed, the arrow's source x, then its 16 bytes. An interpolated record appends HUD_MAP_FINE_MAGIC and
 * x and y in 1/256 of a map unit; a held one the older record after HUD_HOLD_MAGIC and its length. */
#define HUD_MAP_VERSION 1
#define HUD_MAP_N 30
#define HUD_MAP_FINE_MAGIC 0x4D41504D
#define HUD_MAP_MAX (HUD_MAP_N + 3 + 2 + HUD_MAP_N)
typedef struct {
    int    x0, y0, bx, by, ox, oy, zoom, hdg, sx_screen, sy_screen, src_x;
    int32_t px, py;                                /* [BA3C], [C056] */
    uint8_t art[16];
    double c_x, c_y;                               /* the map position, continuous (units of [C0D0] / [C0DE]) */
} hud_map;
int hud_map_load(const int32_t *v, int n, hud_map *m);
int hud_map_load_drawn(const int32_t *v, int n, hud_map *m);
int hud_map_blend(const int32_t *s, int ns, const int32_t *o, int no, double w, int hold, int32_t *out, int cap);
/* The marker's blit (an 'S' record, observer's values) for this state? Then the top-left the fine picture puts the
 * arrow at, in cells (fractional). */
int hud_map_classify(const hud_map *m, const int32_t *v, int n, double *x, double *y);
/* The continuous position of a state's record words: [BA3C] / 32 and 8000h - [C056] / 32 ([C0D0] and [C0DE] are these rounded) */
double hud_map_fine_x(const hud_map *m);
double hud_map_fine_y(const hud_map *m);

/* The exact pipeline's own helpers, exported for the tests: the clipper, and the rotated, unclipped ladder
 * vertices of the last hud_exact on this thread */
void hud_debug_ladder(int *nv, const int **rx, const int **ry, int *first_rung);
void hud_debug_fine_ladder(int *nv, const double **rx, const double **ry, int *first_rung);
int hud_clip_line(int x0, int y0, int x1, int y1, int xmax, int ymax, int16_t out[4]);

#endif
