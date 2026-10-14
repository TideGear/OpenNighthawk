/* Polygons the game fills through its own span rasteriser (record 'U', then the span fill 'Q'):
 * the camera's sky and ground strips and the like. On the fine grid a thick strip's slanted edge
 * must be the straight line between its corners rather than the original's stair, a strip so
 * thin that it is a line must stay a solid stroke as thick as the original drew it, and a
 * polygon that does not explain its footprint must be left as the scaled copy. */
#include "drawlist.h"
#include "hires.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static const uint16_t SEG = 0x1000;
#define COLOUR 0x55
#define BACKGROUND 0x10

static drawlist d;
static hires h;

static void start(int scale)
{
    static uint8_t background[65536];
    memset(background, BACKGROUND, sizeof background);
    drawlist_reset(&d);
    drawlist_set_scale(&d, scale);
    hires_init(&h, scale);
    drawlist_seed(&d, 'Z', SEG, 0, background, sizeof background);
}

static void record(char kind, const int32_t *v, int n)
{
    hires_record(&h, &d, kind, v, n);
    drawlist_record(&d, kind, v, n);
}

/* K, the polygon's corners, and a span fill of the cells whose centres lie between two lines
 * y = a1 + s * x and y = a2 + s * x (x the column), grown by `grow` cells: what a rasteriser
 * with inclusive spans leaves. Returns the number of rows. */
static int strip(double a1, double a2, double s, double grow, int x0, int x1, int y0, int y1,
                 const int32_t *corners, int ncorners)
{
    const int32_t colour[] = { COLOUR };
    record('K', colour, 1);
    int32_t u[2 + 2 * 8];
    int n = 0;
    u[n++] = COLOUR; u[n++] = ncorners;
    for (int k = 0; k < 2 * ncorners; k++) u[n++] = corners[k];
    record('U', u, n);
    static int32_t q[4 + 2 * 256 + 2];
    int rows = 0, first = -1, last = -1;
    int32_t l[256], r[256];
    for (int y = y0; y <= y1; y++) {
        int32_t lo = 1000, hi = -1000;
        for (int x = x0; x <= x1; x++) {
            const double cy = y + 0.5, cx = x + 0.5;
            if (cy >= a1 + s * cx - grow && cy <= a2 + s * cx + grow) {
                if (x < lo) lo = x;
                if (x > hi) hi = x;
            }
        }
        l[y - y0] = lo; r[y - y0] = hi;
        if (hi >= lo) { if (first < 0) first = y - y0; last = y - y0; }
    }
    if (first < 0) return 0;
    rows = last - first + 1;
    n = 0;
    q[n++] = y0 + first; q[n++] = y0 + last; q[n++] = 0; q[n++] = rows;
    for (int k = first; k <= last; k++) { q[n++] = l[k] < 0 ? 0 : l[k]; q[n++] = r[k] < x0 ? x0 : r[k]; }
    q[n++] = SEG; q[n++] = 0;
    record('Q', q, n);
    return rows;
}

int main(void)
{
    drawlist_init(&d);
    static drawlist plain;
    drawlist_init(&plain);

    /* A band 18 cells thick whose edges slope 1 in 25: corners far outside the picture. */
    const double s = 0.04;
    {
        const int32_t corners[] = { -1000, (int32_t)lround(60 - 1000 * s), 1000, (int32_t)lround(60 + 1000 * s),
                                    1000, (int32_t)lround(78 + 1000 * s), -1000, (int32_t)lround(78 - 1000 * s) };
        for (int scale = 4; scale <= 9; scale += 5) {
            start(scale);
            CHECK(strip(60, 78, s, 0.5, 40, 200, 40, 110, corners, 4) > 20);
            const drawlist_page *p = drawlist_get(&d, SEG);
            const uint64_t refilled = h.stats[HS_SPAN_REFILLED], thin = h.stats[HS_SPAN_THIN];
            CHECK(refilled == 1 && thin == 0);
            /* the unrefined picture, for the coarse rows, from a drawlist with no builder */
            drawlist_reset(&plain);
            static uint8_t background[65536];
            memset(background, BACKGROUND, sizeof background);
            drawlist_seed(&plain, 'Z', SEG, 0, background, sizeof background);
            const int32_t colour[] = { COLOUR };
            drawlist_record(&plain, 'K', colour, 1);
            /* its coarse picture is the same: refinement changes only the fine one */
            const drawlist_page *c = drawlist_get(&plain, SEG);
            (void)c;
            /* the top edge: the first painted fine row of a column must follow the line */
            const int W = 320 * scale;
            double worst = 0;
            int columns = 0;
            for (int x = 70 * scale; x < 170 * scale; x++) {
                int top = -1;
                for (int y = 0; y < 200 * scale; y++)
                    if (p->hi[(size_t)y * W + x] == COLOUR) { top = y; break; }
                if (top < 0) continue;
                const double want = (60 + s * ((x + 0.5) / scale)) * scale;
                const double e = fabs(top - want);
                if (e > worst) worst = e;
                columns++;
            }
            CHECK(columns > 90 * scale);
            CHECK(worst < 0.5 * scale);                   /* a stair of whole cells would be off by half a cell or more */
        }
    }

    /* A narrow region still has two geometric boundaries. Its original
     * inclusive footprint is much thicker, which must not turn it into a
     * fat stroke or leave coarse teeth at the boundary. */
    {
        const int32_t corners[] = { -1000,0,1000,80,1000,81,-1000,1 };
        start(9);
        CHECK(strip(40,41,0.04,1.5,40,200,30,70,corners,4)>0);
        CHECK(h.stats[HS_SPAN_REFILLED]==1 && h.stats[HS_SPAN_THIN]==0);
        const drawlist_page *p=drawlist_get(&d,SEG);
        for(int x=70*9;x<170*9;x++) {
            int top=-1,bottom=-1;
            for(int y=0;y<200*9;y++)if(p->hi[(size_t)y*2880+x]==COLOUR){if(top<0)top=y;bottom=y;}
            CHECK(top>=0 && bottom-top+1<=12);
        }
        for(int y=71*9;y<200*9;y++)for(int x=0;x<2880;x++)CHECK(p->hi[(size_t)y*2880+x]==BACKGROUND);
    }

    /* A strip of no thickness, painted as the one-cell line the original leaves. */
    {
        const double s2 = 0.25;
        const int32_t corners[] = { -1000, (int32_t)lround(40 - 1000 * s2), 1000, (int32_t)lround(40 + 1000 * s2),
                                    1000, (int32_t)lround(40 + 1000 * s2), -1000, (int32_t)lround(40 - 1000 * s2) };
        start(4);
        const int rows = strip(40, 40, s2, 0.5, 40, 120, 30, 110, corners, 4);
        CHECK(rows > 15);
        CHECK(h.stats[HS_SPAN_THIN] == 1 && h.stats[HS_SPAN_REFILLED] == 0);
        const drawlist_page *p = drawlist_get(&d, SEG);
        /* no hole: every cell the coarse picture painted has fine pixels of the colour */
        int cells = 0, holes = 0;
        for (int y = 0; y < 200; y++)
            for (int x = 0; x < 320; x++) {
                if (p->b[y * 320 + x] != COLOUR) continue;
                cells++;
                int any = 0;
                for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) any |= p->hi[(size_t)(y * 4 + j) * 1280 + x * 4 + k] == COLOUR;
                holes += !any;
            }
        CHECK(cells > 20 && holes == 0);
    }

    /* A polygon that is nowhere near its footprint keeps the scaled copy. */
    {
        const int32_t corners[] = { -1000, -1000, -900, -1000, -900, -900, -1000, -900 };
        start(4);
        strip(60, 78, 0.04, 0.5, 40, 200, 40, 110, corners, 4);
        CHECK(h.stats[HS_SPAN_REFILLED] == 0 && h.stats[HS_SPAN_BEYOND] == 1);
        const drawlist_page *p = drawlist_get(&d, SEG);
        for (int y = 0; y < 200; y++)
            for (int x = 0; x < 320; x++)
                for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++)
                    CHECK(p->hi[(size_t)(y * 4 + j) * 1280 + x * 4 + k] == p->b[y * 320 + x]);
    }
    puts("span-rasteriser polygons: a thick strip's edge is straight, a thin strip stays a solid line, a mismatch is left scaled");
    return 0;
}
