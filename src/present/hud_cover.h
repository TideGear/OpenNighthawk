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
    int      missed;                  /* (a panel cover) items passed over */
    int      last;                    /* the item (HI_ / PI_) of the record last recognised */
    struct { uint8_t kind, keep, item; uint64_t sig; } item[HUD_MAX_EXACT];
} hud_cover;

/* Follow the pass hud_exact lists. */
void hud_cover_begin(hud_cover *c, const hud_exact_list *ex);

/* The next library record (kind 'K', 'N', 'T', 'H' or 'S', the observer's values). HC_NONE when no pass is
 * followed or the record is not one a pass makes (an entry-73 sprite, say), HC_REPLACED for a record whose
 * drawing the fine picture does itself, HC_KEPT for one that stays the original's, HC_MISMATCH once, when
 * the record is not the one expected (the cover ends). */
int hud_cover_step(hud_cover *c, char kind, const int32_t *v, int n);

/* The panel pass's cover (hud_panel_exact): other routines' records come between its items, so a record that is not
 * the next item is passed by (HC_NONE) instead of ending the cover; a line, a text or a sprite that is a later item
 * is taken as that one, the items before it counted as missed (they stay the original's). hud_pcover_end closes the
 * cover at the end of the pass and returns the items it never saw (lines, texts, sprites). */
void hud_pcover_begin(hud_cover *c, const hud_exact_list *ex);
int hud_pcover_step(hud_cover *c, char kind, const int32_t *v, int n);
int hud_pcover_end(hud_cover *c);

/* ---- the bitmap fonts the fine text uses, kept from the 'T' records ------------------------------ */
#define HUD_FONTS 4
typedef struct {
    int     valid, first, last, shift, fixed, height, spacing, extra, count, nglyph;
    uint8_t widths[256];
    uint8_t glyph[4096];
} hud_font;

/* ---- the art of the blits the fine picture draws itself (the reticle, the flight-path marker), kept from their 'S'
 * records: the pass's fine items are drawn before its records arrive, in the original's order ---------------------- */
#define HUD_ARTS 6
#define HUD_ART_MAX 400
typedef struct {
    int     valid, entry, seg, sx, sy, w, h;
    int16_t clip[4];                  /* entry 71's clip: rows low, high, columns low, high */
    uint8_t px[HUD_ART_MAX];
} hud_art;

typedef struct {
    hud_cover cover;
    hud_font  font[HUD_FONTS];
    hud_art   art[HUD_ARTS];
    hud_panel panel;              /* the panel pass's state ('p', a first-version record), while panel_valid */
    int       panel_valid;
    hud_cover pcover;             /* the panel pass's items (an extended 'p'), while pcover.active */
    uint16_t  pcover_seg;         /* the page the panel's fine items were drawn on */
    hud_map   map;                /* the map arrow's state ('m'), while map_valid */
    int       map_valid;
    /* what the HUD did, for reports */
    uint64_t  passes, drawn, no_ticks, no_font, no_page, mismatched, replaced, kept, unsupported, leaked, panel_replaced;
    uint64_t  panel_passes, panel_drawn, panel_missed, panel_no_art, art_changed, map_drawn, map_kept;
} hud_ov;

/* Remember the font a 'T' record carries (font index = its block's word 6). */
void hud_fonts_note(hud_ov *o, const int32_t *v, int n);
/* Remember a blit's art ('S' record, entry 18, 71 or 73); a changed art is counted. */
void hud_art_note(hud_ov *o, const int32_t *v, int n);
const hud_art *hud_art_find(const hud_ov *o, int entry, int seg, int sx, int sy, int w, int h);

#endif
