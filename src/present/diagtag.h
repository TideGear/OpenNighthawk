/* diagtag.h - the presentation diagnostic's pixel origins (docs/presentation.md, "Where the
 * blockiness comes from"). With F117_PRESENT_DIAG=DIR set, each drawlist page keeps a byte per
 * fine pixel naming the path that last wrote it; the plane follows every copy and blit the
 * fine picture follows, and is written beside a shot by f117run. It is an observer of the
 * observer: nothing in the guest, the records or the pictures changes, and with the variable
 * unset no plane exists. */
#ifndef F117R_DIAGTAG_H
#define F117R_DIAGTAG_H

enum {
    DT_SEED,            /* the scaled copy a page was seeded with, never written since */
    /* (a) refined from true geometry on the fine grid */
    DT_MODEL_SOLID,     /* model polygon, solid/AND/OR fill, geometry_cover */
    DT_MODEL_PATTERN,   /* model polygon, dither/stipple fill: boundary fine, pattern cells original-sized */
    DT_MODEL_NEAR,      /* near-clipped model polygon, verified crossings, footprint-bounded */
    DT_SPANPOLY_FINE,   /* span-rasteriser polygon (0FB2:01E6: sky/ground strips, map regions) refined */
    /* (b) a polygon that kept the scaled copy of the original's rows, by reason */
    DT_FB_UNATTEMPTED,  /* 'b' rows with no refill attempted (no rows/page/state before them) */
    DT_FB_NEAR_SHAPE,   /* near-clipped, legacy record / unsupported shape / no snapshot */
    DT_FB_NEAR_CROSSING,/* near-clipped, crossing did not reproduce the original's */
    DT_FB_FEW_EDGES,    /* fewer than three edges */
    DT_FB_NO_VERTICES,  /* an edge without its G/V records */
    DT_FB_BEHIND,       /* a vertex behind the eye */
    DT_FB_NO_DIVISOR,   /* a vertex without a positive divisor */
    DT_FB_BEYOND,       /* footprint more than 3 px from the geometry */
    DT_SEAM,            /* seam repair: inside a refined model, restored to the coarse owner's colour */
    DT_GUARD,           /* flat-cell guard: restored to the coarse colour at presentation */
    /* span fills (library entries 37/40) */
    DT_SPAN_NOT_POLY,   /* a span fill with no fill_polygon corners before it */
    DT_SPAN_REJECT,     /* corners seen but the polygon was not refined (colour/mode/page) */
    DT_SPAN_BEYOND,     /* corners did not reproduce the footprint within 3 px */
    DT_SPAN_THIN,       /* degenerate strip drawn as one fine stroke */
    DT_SPAN_SHADE,      /* OR/AND/shade span fill modifying fine pixels in place */
    /* (c) strokes */
    DT_HUD_FINE,        /* library line (entry 31) on the fine grid */
    DT_OUTLINE_FINE,    /* outline edge (VGAME 1377:046F) on the fine grid */
    DT_TICKS_FINE,      /* tick scale (entry 11) on the fine grid */
    DT_HUD_COARSE,      /* the same, kept as scaled blocks (off-screen or odd addresses) */
    DT_OUTLINE_COARSE,
    DT_TICKS_COARSE,
    /* (d), (e), (f) */
    DT_TEXT,            /* library text (entries 1-6) */
    DT_SPRITE,          /* sprites (73, 18, 71, 19) */
    DT_SPRITE_RLE,      /* scaled RLE sprite (22) */
    DT_BLIT_UNHELD,     /* blit from a page the replay does not hold (cockpit art, map bitmap) */
    DT_BLIT_COARSE,     /* blit from a held page, coarse part */
    DT_PAGECOPY,        /* whole-page copy through logged bytes */
    DT_X_DELTA,         /* byte changes of an entry no rule decodes */
    DT_OTHER,
    DT_COUNT
};

#endif
