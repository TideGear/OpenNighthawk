/* band_state.h - the sky and ground bands as a function of the game state, for the 4K presentation
 * (docs/presentation.md, "The state-driven sky and ground bands").
 *
 * VGAME 0000:1450 (matched.c vgame_horizon_bands) draws the 3-D view's background: at detail 3 and above
 * it projects 17 pairs of band edge points about the horizon and hands 16 sky and 16 ground quadrilaterals
 * to the span rasteriser (fill_polygon, 0FB2:01E6), whose corners the observer logs as 'U' records. Its
 * whole input is a few words of the data segment at entry, which the observer hands over as a record 'o'
 * (observe.c, hook_game_draw), so the pass can be modelled two ways:
 *
 *   EXACT  the original's 16-bit pipeline (the rounding multiply 104E:0000, the long divide, the
 *          arithmetic shifts, the wraps): the 32 quadrilaterals' corners and colours, as the 'U' records
 *          carry them. Checked against captured logs by tests/test_band_state.c.
 *   FINE   the same formulas in floating point with none of the integer rounding, in the same units (a
 *          coarse pixel; an integer value names the cell whose centre the original's corner stands for).
 *
 * The state that moves between logic steps is the camera's height above the ground and two sine/cosine
 * pairs (pitch and roll); band_blend interpolates them for an in-between frame, the angles on the shortest
 * arc. Nothing here reads or writes the machine. Low detail ([990E] < 3) takes another path (0FB2:0000,
 * a two-colour split through the library's span fill, no 'U' records) and is not modelled. */
#ifndef F117R_BAND_STATE_H
#define F117R_BAND_STATE_H

#include <stdint.h>

/* ---- the 'o' record -------------------------------------------------------------------------
 * v[0]  BAND_REC_VERSION
 * v[1]  the view block (the routine's argument, a DS offset); v[2] its word +4 (low detail's colour)
 * v[3]  [990E] the detail level (signed byte)
 * v[4]  [438E], v[5] [43E0]: both nonzero, the bands are flat (sky 88h, ground 7Ch)
 * v[6]  [49B0] the camera's height
 * v[7]  [49B6], v[8] [49B8]: sine and cosine of the negated pitch (1.15)
 * v[9]  [49BA], v[10] [49BC]: sine and cosine of the negated roll (1.15)
 * v[11] [4368] a left shift of the horizon offset (byte), v[12] [294B] halve it (byte)
 * v[13] [0D1E], v[14] [0D20]: the view's centre
 * v[15 .. 30] [0D22 .. 0D40]: the sixteen sky band offsets
 * v[31] [2293], v[32] [2295]: the span rasteriser's window, x 0..[2293] and y 0..[2295]
 * then, only in an interpolated record, BAND_FINE_MAGIC and the five continuous values in 1/256:
 * height, pitch sine, pitch cosine, roll sine, roll cosine. Words are logged as the observer reads them
 * (unsigned 16-bit); band_state_load gives them their signs. */
#define BAND_REC_VERSION 1
#define BAND_REC_BASE 33
#define BAND_FINE_MAGIC 0x42414E44
#define BAND_REC_FINE (BAND_REC_BASE + 6)

/* the data segment words the record holds, after the version, the view block and its word +4 (the observer's
 * list; in the header so the observer needs no library) */
#define BAND_NWORDS 30
static const uint16_t band_rec_off[BAND_NWORDS] = {
    0x990E, 0x438E, 0x43E0, 0x49B0, 0x49B6, 0x49B8, 0x49BA, 0x49BC, 0x4368, 0x294B, 0x0D1E, 0x0D20,
    0x0D22, 0x0D24, 0x0D26, 0x0D28, 0x0D2A, 0x0D2C, 0x0D2E, 0x0D30, 0x0D32, 0x0D34, 0x0D36, 0x0D38,
    0x0D3A, 0x0D3C, 0x0D3E, 0x0D40, 0x2293, 0x2295
};
/* which of them are bytes */
static const uint8_t band_rec_byte[BAND_NWORDS] = { 1, 0, 0, 0, 0, 0, 0, 0, 1, 1 };

typedef struct {
    int view, low_colour, detail, flat_a, flat_b;
    int height, sp, cp, sr, cr;       /* the words, signed */
    int shift, halve, cx, cy;
    int table[16];
    int xmax, ymax;                   /* the window, signed words */
    /* the continuous state: the words, or interpolated values */
    double f_height, f_sp, f_cp, f_sr, f_cr;
    int interpolated;
} band_state;

/* Expand a record. 0 when it is not a version-1 record. */
int band_state_load(const int32_t *v, int n, band_state *st);

/* ---- EXACT ----------------------------------------------------------------------------------- */
#define BAND_QUADS 32

typedef struct {
    uint8_t colour;
    uint8_t wrapped;                  /* a corner left the 16-bit range: the original drew a wrapped value */
    int16_t x[4], y[4];               /* the corners as fill_polygon gets them (the 'U' record's order) */
} band_quad;

typedef struct { int n; band_quad q[BAND_QUADS]; } band_exact_list;

/* The quadrilaterals the pass hands to fill_polygon, in order (16 sky, then 16 ground). 0 on success,
 * 1 for low detail (no quadrilaterals: n = 0). */
int band_exact(const band_state *st, band_exact_list *out);

/* ---- FINE ------------------------------------------------------------------------------------ */
typedef struct {
    uint8_t colour, valid;            /* valid: finite and drawn by the state (not wrapped in the exact list) */
    double x[4], y[4];                /* coarse pixel units, unrounded: the integer corner k is cell k's centre */
} band_fquad;

typedef struct { int n; band_fquad q[BAND_QUADS]; } band_fine_list;

/* The same quadrilaterals without rounding. ex, when not NULL, is the exact list of the same state: a quad it
 * marks wrapped is not valid. Returns 0 on success, 1 for low detail. */
int band_fine(const band_state *st, const band_exact_list *ex, band_fine_list *out);

/* ---- interpolation --------------------------------------------------------------------------- */
/* The record of an in-between frame: skeleton record s with its continuous state moved the fraction w of the
 * way to record o (the pitch and roll pairs by angle on the shortest arc, their lengths and the height
 * linearly). 0 when the two do not share the discrete words (detail, flat flags, shift, halve, centre,
 * table, window, view). Returns the new record's length (BAND_REC_FINE; BAND_REC_BASE, the skeleton's own
 * record, when w is under a millionth), or 0. */
int band_blend(const int32_t *s, int ns, const int32_t *o, int no, double w, int32_t *out, int cap);

/* ---- following a pass's records -------------------------------------------------------------- */
enum { BC_NONE, BC_REPLACED, BC_KEPT, BC_MISMATCH };

typedef struct {
    int active, at, n, state_ok;
    band_exact_list ex;
    band_fine_list fine;
    int xmax, ymax;
    /* counters, for reports: passes seen, at low detail, 'U' records drawn from the state, kept the original's,
     * off the prediction; band polygons painted from the state, and those left to the scaled copy (geometry
     * outside the window while the original painted rows) */
    uint64_t passes, low_detail, replaced, kept, mismatched, painted, scaled;
} band_cover;

/* Begin following the pass an 'o' record starts. with_fine: also compute the fine list (N > 1). 0 when the
 * record is not one (the cover stays off). */
int band_cover_begin(band_cover *c, const int32_t *v, int n, int with_fine);
/* The next 'U' record. BC_NONE when no pass is followed; BC_REPLACED when it is the next quad the state
 * predicts and the fine list draws it (*fq the fine quad); BC_KEPT when it is predicted but stays the
 * original's (wrapped); BC_MISMATCH once, when it is not the one expected (the cover ends). */
int band_cover_step(band_cover *c, const int32_t *v, int n, const band_fquad **fq);
void band_cover_end(band_cover *c);

/* On by default; F117_PRESENT_BANDS=old turns the state-driven bands off. */
int band_state_enabled(void);
void band_state_enable(int on);

/* The diagnostic plane's tag for band pixels drawn from the state (diagtag.h's own enum is left alone). */
enum { DT_BAND_STATE = 242 };

#endif
