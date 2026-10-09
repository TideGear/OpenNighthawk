/* hires.h - Stage 2 (docs/presentation.md): the model polygons refilled on a
 * picture N times finer from their true geometry, tools/hires_subpixel.py in
 * C and held to it by tests/test_hires.c.
 *
 * It runs beside the Stage 1 replay (drawlist.h) with a scale set: each
 * record goes to hires_record first, then to drawlist_record. A filled
 * polygon's edges are tied through their 'G' records to the camera-space
 * vertices ('V'); each vertex is placed where the projection's divide put it,
 * with the remainder it threw away; a fine pixel takes the fill when its
 * centre lies inside the true polygon (top-left rule) within the polygon's own
 * overshoot (measured at N = 1, so N = 1 is the original's picture) and inside
 * the original's own footprint. Near-clipped polygons, ones with a vertex
 * behind the eye or a footprint more than 3 pixels from their geometry, and
 * fill styles with no rule keep the scaled copy, as do text, sprites, lines,
 * blits and the HUD. The state is plain data: a copy (memcpy) carries on. */
#ifndef F117R_HIRES_H
#define F117R_HIRES_H

#include "drawlist.h"

#include <stdint.h>

#define HIRES_MAP 8192                /* vertex records and edge slots remembered */
#define HIRES_EDGES 512               /* edges a polygon may have */

typedef struct { int ok; int32_t x, y, z, px, py, range; } hires_vertex;

typedef struct {
    int32_t slot, x0, y0, x1, y1, st;
    int have_g;                       /* its slot was prepared ('G') */
    hires_vertex a, b;                /* the two vertices the 'G' record named */
} hires_edge;

/* the counters hires_subpixel.py prints, by name (hires_stat_name) */
enum {
    HS_POLYGONS, HS_LEFT_NEAREST, HS_REFILLED, HS_FOOT_PIXELS, HS_FOOT_FINE,
    HS_GROW0, HS_GROW05, HS_GROW1, HS_GROW15, HS_GROW2, HS_GROW3,
    HS_NEAR_CLIPPED, HS_FEW_EDGES, HS_NO_VERTICES, HS_BEHIND, HS_NO_DIVISOR, HS_BEYOND, HS_STYLE,
    HS_COUNT
};
const char *hires_stat_name(int k);

typedef struct {
    int n;
    struct { uint16_t key; uint8_t used; hires_vertex v; } vert[HIRES_MAP];
    struct { uint16_t key; uint8_t used; hires_vertex a, b; } slot[HIRES_MAP];
    hires_edge pending[HIRES_EDGES];
    int npending;
    struct {
        int active, done, have_rows, nclip, top, nrows;
        int32_t vp[4];
        hires_edge edges[HIRES_EDGES];
        int nedges, njoin;
        int32_t rows[2 * 256];
    } poly;
    int seen_fill;
    int fill_colour;
    int suppressed;                   /* the page index paused while a refilled polygon's rows arrive, or -1 */
    int32_t origin[2];
    uint64_t stats[HS_COUNT];
} hires;

void hires_init(hires *h, int n);

/* One observer record, before drawlist_record gets it. */
void hires_record(hires *h, drawlist *d, char kind, const int32_t *v, int n);

#endif
