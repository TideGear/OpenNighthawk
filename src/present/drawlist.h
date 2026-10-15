/* drawlist.h - the Stage 1 replay (docs/presentation.md): the original's
 * work page and display rebuilt at 320x200 from the draw records the
 * observer gives (src/matched/observe.h), by the original's own rasteriser
 * rules. A port of tools/drawlist_frame.py, held to it and to the page dumps
 * by tests/test_drawlist.c.
 *
 * The replay keeps its own copy of each page it was seeded with (the work
 * page 'Z' and the display 'Y', as the observer dumps them) and draws every
 * record on it in order. A record that names a page it does not hold draws
 * nothing. It never reads the machine: what it needs comes in the records.
 *
 * With a scale N (drawlist_set_scale), each page also keeps its first 320x200
 * bytes N times finer in each direction (tools/hires_frame.py's HiPage):
 * every write is mirrored there as an N x N block, unless the page is
 * suppressed while a polygon refilled on the finer grid (hires.h) paints its
 * coarse rows. HUD lines and ticks are redrawn on the fine grid with
 * scale-proportional or chosen stroke width; colour operations preserve
 * those fine pixels. A blit or page copy between two held pages carries
 * the fine rows across. Blits through unseeded pages retain a bounded cache
 * of fine rectangles, restored only while their logged coarse bytes match. */
#ifndef F117R_DRAWLIST_H
#define F117R_DRAWLIST_H

#include <stdint.h>

#define DRAWLIST_PAGES 4
#define DRAWLIST_OVERLAYS 8       /* fine rectangles kept for pages the replay does not hold */

typedef struct {
    uint16_t seg;
    unsigned size;                /* 65,536 for a work page, 64,000 for the display (as dumped) */
    uint8_t  b[65536];
    uint8_t *hi;                  /* the finer picture, 320N x 200N, or NULL */
    int      n, suppress;
    uint8_t  strokes[64000 / 8];  /* coarse cells with authoritative fine strokes/geometry, for the flat guard */
    uint8_t  hud_strokes[64000 / 8]; /* actual HUD strokes, which also take priority over seam repair */
    uint8_t *model;               /* two fine bit planes: true model geometry and actual model paint */
    uint8_t *tag;                 /* diagnostic only (diagtag.h): the path that wrote each fine pixel, or NULL */
    uint8_t  model_foot[64000 / 8]; /* union of the tracked models' original footprints */
    int      model_active;
} drawlist_page;

/* A rectangle blitted to a page the replay does not hold (the cockpit-art page also keeps the
 * map's terrain between frames): its coarse bytes and fine rows, so that the blit that brings it
 * back, whose source the log carries only as coarse bytes, can bring the fine rows too. They are
 * used only while the coarse bytes the log carries still equal the ones kept. */
typedef struct {
    uint16_t seg;
    int32_t x, y, w, h, n;
    uint8_t *tag;                 /* diagnostic origin plane, same size as hi, or NULL */
    uint8_t *b, *hi, *strokes;    /* w*h coarse bytes, (w*n)*(h*n) fine bytes, w*h flags (bit 0 guard protection, bit 1 HUD stroke) */
} drawlist_overlay;

typedef struct {
    drawlist_page page[DRAWLIST_PAGES];
    drawlist_overlay overlay[DRAWLIST_OVERLAYS];
    int noverlay, overlay_next;
    int      npages;
    int      n;                   /* the fine picture's scale, 0 for none */
    int      line_width;          /* fine HUD strokes: 0 uses the scale, else output pixels */
    int      have_seg;            /* seeded with a work page: records before it are skipped */
    uint16_t seg, origin;         /* the work page and the library origin at the phase start */
    int      colour;              /* the library colour (entries 32/33, text), -1 not yet set */
    int      fill_colour;         /* the colour word the model fill uses ([8606]), -1 none */
    int      prefer_logged;       /* a page copy takes its logged source bytes even when the
                                     source page is held (as drawlist_frame.py does) */
    /* offsets of the work page written from the original's bytes this phase */
    uint8_t  copied[65536 / 8];
    unsigned ncopied;
    uint64_t seam_restored;
    uint8_t  diag_b, diag_q;      /* diagnostic: the origin tag the coarse rows of the current polygon / span fill take */
    /* what had no rule or no data */
    unsigned long long outline_no_rule, blits_no_source, copies_no_source, sprites_no_source, text_no_font,
                       recolour_no_rule;
} drawlist;

void drawlist_init(drawlist *d);

/* Diagnostic origin planes (diagtag.h): off unless enabled before the pages are seeded. With
 * them on, every page keeps a byte per fine pixel naming what wrote it. The tag set by
 * drawlist_diag_stroke_tag is what the next fine stroke takes (hires.c, for a thin strip). */
void drawlist_diag_enable(int on);
int  drawlist_diag_enabled(void);
void drawlist_diag_stroke_tag(uint8_t tag);

/* Keep a picture n times finer (1 or more; 0 none) on the pages seeded from
 * now on; drawlist_free releases them, drawlist_reset forgets the pages but
 * keeps the scale. A drawlist with fine pages is copied by drawlist_copy. */
void drawlist_set_scale(drawlist *d, int n);
void drawlist_set_line_width(drawlist *d, int width);
void drawlist_free(drawlist *d);
void drawlist_reset(drawlist *d);
void drawlist_copy(drawlist *dst, const drawlist *src);
/* Complete internal model seams before presenting or painting another layer.
 * Returns the fine pixels restored to their original coarse owner's colour. */
uint64_t drawlist_finish_models(drawlist *d);

/* Give the replay a page as it stands: 'Z' the work page at a phase's start
 * (its segment and the library origin then; it starts a phase), 'Y' the
 * display. size is the bytes given. */
void drawlist_seed(drawlist *d, char kind, uint16_t seg, uint16_t origin, const uint8_t *bytes, unsigned size);

/* A phase begins on the work page seg with the library origin as given,
 * without new bytes ('Z' does this as well as seeding). */
void drawlist_phase(drawlist *d, uint16_t seg, uint16_t origin);

/* One observer record: its kind and values (the log line without the clock). */
void drawlist_record(drawlist *d, char kind, const int32_t *v, int n);

/* The replay of a page, or NULL when it holds none at seg. */
const drawlist_page *drawlist_get(const drawlist *d, uint16_t seg);
drawlist_page *drawlist_page_of(drawlist *d, uint16_t seg);

/* A fine stroke between two points on the fine grid (pixels of the N-times picture): Bresenham
 * between them with a square nib of `width` fine pixels and whole end caps. The cells it touches
 * are marked so the flat-pixel guard leaves them alone. */
void drawlist_fine_stroke(drawlist_page *p, int ax, int ay, int bx, int by, int width, int colour);

#endif
