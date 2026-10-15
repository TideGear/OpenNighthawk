/* hud_cover.h - which of a draw list's records are the HUD pass's, and which the state-driven HUD
 * replaces (docs/presentation.md, "The state-driven HUD").
 *
 * The observer's 'h' record is the state the HUD pass starts from. hud_exact (hud_state.h) predicts
 * the library calls the pass makes from it; a cover follows the records that arrive after the 'h' and
 * recognises each of the predicted ones in order (kind and every value that matters), so the replay
 * knows which K, N, T, H and entry-18 records belong to the pass and which of those the fine picture
 * draws itself instead of taking the original's. A record that does not match ends the cover (the rest
 * of the pass keeps the original path) and is counted. */
#ifndef F117R_HUD_COVER_H
#define F117R_HUD_COVER_H

#include "hud_state.h"

#include <stdint.h>

enum { HC_NONE, HC_REPLACED, HC_KEPT, HC_MISMATCH };

typedef struct {
    int      active, n, at;           /* a pass is being followed: its length and the next record expected */
    struct { uint8_t kind, keep; uint64_t sig; } item[HUD_MAX_EXACT];
} hud_cover;

/* Follow the pass hud_exact lists. */
void hud_cover_begin(hud_cover *c, const hud_exact_list *ex);

/* The next library record (kind 'K', 'N', 'T', 'H' or 'S', the observer's values). HC_NONE when no pass is
 * followed or the record is not one a pass makes (an entry-73 sprite, say), HC_REPLACED for a record whose
 * drawing the fine picture does itself, HC_KEPT for one that stays the original's, HC_MISMATCH once, when
 * the record is not the one expected (the cover ends). */
int hud_cover_step(hud_cover *c, char kind, const int32_t *v, int n);

/* ---- the bitmap fonts the fine text uses, kept from the 'T' records ------------------------------ */
#define HUD_FONTS 4
typedef struct {
    int     valid, first, last, shift, fixed, height, spacing, extra, count, nglyph;
    uint8_t widths[256];
    uint8_t glyph[4096];
} hud_font;

typedef struct {
    hud_cover cover;
    hud_font  font[HUD_FONTS];
    /* what the HUD did, for reports */
    uint64_t  passes, drawn, no_ticks, no_font, no_page, mismatched, replaced, kept, unsupported, leaked;
} hud_ov;

/* Remember the font a 'T' record carries (font index = its block's word 6). */
void hud_fonts_note(hud_ov *o, const int32_t *v, int n);

#endif
