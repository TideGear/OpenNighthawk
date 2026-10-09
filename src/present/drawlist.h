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
 * coarse rows, and a blit or page copy between two held pages carries the
 * fine rows across. */
#ifndef F117R_DRAWLIST_H
#define F117R_DRAWLIST_H

#include <stdint.h>

#define DRAWLIST_PAGES 4

typedef struct {
    uint16_t seg;
    unsigned size;                /* 65,536 for a work page, 64,000 for the display (as dumped) */
    uint8_t  b[65536];
    uint8_t *hi;                  /* the finer picture, 320N x 200N, or NULL */
    int      n, suppress;
} drawlist_page;

typedef struct {
    drawlist_page page[DRAWLIST_PAGES];
    int      npages;
    int      n;                   /* the fine picture's scale, 0 for none */
    int      have_seg;            /* seeded with a work page: records before it are skipped */
    uint16_t seg, origin;         /* the work page and the library origin at the phase start */
    int      colour;              /* the library colour (entries 32/33, text), -1 not yet set */
    int      fill_colour;         /* the colour word the model fill uses ([8606]), -1 none */
    int      nfill_rows;          /* the fill's rows in a style with no rule, for their 'a' */
    struct { uint16_t at, n; } fill_rows[256];
    int      prefer_logged;       /* a page copy takes its logged source bytes even when the
                                     source page is held (as drawlist_frame.py does) */
    /* offsets of the work page written from the original's bytes this phase */
    uint8_t  copied[65536 / 8];
    unsigned ncopied;
    /* what had no rule or no data */
    unsigned long long outline_no_rule, blits_no_source, copies_no_source, sprites_no_source, text_no_font,
                       recolour_no_rule;
} drawlist;

void drawlist_init(drawlist *d);

/* Keep a picture n times finer (1 or more; 0 none) on the pages seeded from
 * now on; drawlist_free releases them, drawlist_reset forgets the pages but
 * keeps the scale. A drawlist with fine pages is copied by drawlist_copy. */
void drawlist_set_scale(drawlist *d, int n);
void drawlist_free(drawlist *d);
void drawlist_reset(drawlist *d);
void drawlist_copy(drawlist *dst, const drawlist *src);

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

#endif
