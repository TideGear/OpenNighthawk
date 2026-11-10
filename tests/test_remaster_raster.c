/* test_remaster_raster.c - the Remastered triangle fill: depth order does not depend on draw order, the near plane clips,
 * a wider frame adds columns without moving what was drawn, and positions are sub-pixel. */
#include "remaster_raster.h"
#include "exact.h"
#include "decal_faces.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define SETENV(k, v) _putenv_s(k, v)
#else
#define SETENV(k, v) setenv(k, v, 1)
#endif

#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#define RED 0xFFFF0000u
#define GREEN 0xFF00FF00u
#define BLUE 0xFF0000FFu
#define BG 0xFF101010u

static remaster_target make(int w, int h)
{
    remaster_target t = { 0 };
    t.w = w; t.h = h;
    t.color = (uint32_t *)malloc((size_t)w * h * 4);
    t.inv_z = (float *)malloc((size_t)w * h * 4);
    t.cx = w / 2.0; t.cy = h / 2.0;
    t.sx = 256.0; t.sy = 256.0;
    t.near_z = 0x10000;
    remaster_clear(&t, BG);
    return t;
}

/* a square facing the eye at depth z, half-width `half` pixels on screen, centred `dx` pixels right of the view's centre */
static void quad(remaster_v3 q[4], int32_t z, double half, double dx)
{
    const double k = (double)z / 256.0;               /* x per pixel at this depth */
    q[0] = (remaster_v3){ (int32_t)((dx - half) * k), (int32_t)(-half * k), z };
    q[1] = (remaster_v3){ (int32_t)((dx + half) * k), (int32_t)(-half * k), z };
    q[2] = (remaster_v3){ (int32_t)((dx + half) * k), (int32_t)(half * k), z };
    q[3] = (remaster_v3){ (int32_t)((dx - half) * k), (int32_t)(half * k), z };
}

int main(void)
{
    /* The same ground directly under a two-unit-high camera must cover
     * the bottom corners at every terrain detail, including wide margins.
     * A fixed native near plane leaves holes at the coarser levels. */
    for (int ring = 1; ring <= 4; ++ring) {
      for (int bank = -1; bank <= 1; ++bank) {
        const int scale = 1 << (2 * (4 - ring)), unit = 256 * scale;
        remaster_target t = make(128, 48);
        t.sx = t.sy = 32;
        t.inv_depth_scale = scale;
        t.near_z = remaster_ground_near_z(scale);
        remaster_v3 floor[4] = {
            {-1024*unit, 2*unit, -1024*unit}, {1024*unit, 2*unit, -1024*unit},
            {1024*unit, 2*unit, 4096*unit}, {-1024*unit, 2*unit, 4096*unit}
        };
        for (int k = 0; k < 4; ++k) {
            const double x = floor[k].x, y = floor[k].y, a = bank * 0.15;
            floor[k].x = (int32_t)llround(x*cos(a)-y*sin(a));
            floor[k].y = (int32_t)llround(x*sin(a)+y*cos(a));
        }
        CHECK(remaster_polygon(&t, floor, 4, GREEN) > 0);
        CHECK(t.color[47*128] == GREEN && t.color[48*128-1] == GREEN);
        CHECK(t.color[64] == BG);
        free(t.color); free(t.inv_z);
      }
    }
    /* 1. the nearer face wins in either order: the original's whole-object sort cannot promise this */
    for (int order = 0; order < 2; order++) {
        remaster_target t = make(64, 48);
        remaster_v3 far_q[4], near_q[4];
        quad(far_q, 0x08000000, 14, 0);
        quad(near_q, 0x02000000, 6, 0);
        if (order == 0) { remaster_polygon(&t, far_q, 4, RED); remaster_polygon(&t, near_q, 4, GREEN); }
        else { remaster_polygon(&t, near_q, 4, GREEN); remaster_polygon(&t, far_q, 4, RED); }
        CHECK(t.color[24 * 64 + 32] == GREEN);                 /* the middle: the near square */
        CHECK(t.color[24 * 64 + 32 + 10] == RED);              /* beyond the near square's edge: the far one */
        CHECK(t.color[2 * 64 + 2] == BG);                      /* outside both */
        free(t.color); free(t.inv_z);
    }
    /* a near face crossing a far one: depth decides each pixel, not whole objects (a far wall's edge over a near wall) */
    {
        remaster_target t = make(64, 48);
        remaster_v3 a[4], b[4];
        quad(a, 0x04000000, 12, -6);                           /* nearer, left */
        quad(b, 0x03000000, 12, 6);                            /* even nearer, right, overlapping a */
        remaster_polygon(&t, b, 4, BLUE);
        remaster_polygon(&t, a, 4, RED);
        CHECK(t.color[24 * 64 + 32 - 8] == RED);               /* only a covers here */
        CHECK(t.color[24 * 64 + 32 + 2] == BLUE);              /* both cover: b is nearer, though drawn first */
        free(t.color); free(t.inv_z);
    }
    /* 2. behind the eye: nothing; across the near plane: clipped, only the part in front is drawn */
    {
        remaster_target t = make(64, 48);
        const remaster_v3 behind[3] = { { 0, 0, -0x1000000 }, { 100, 0, -0x1000000 }, { 0, 100, -0x1000000 } };
        CHECK(remaster_polygon(&t, behind, 3, RED) == 0);
        const remaster_v3 across[3] = { { -0x100000, 0, 0x100000 }, { 0x100000, 0, 0x100000 }, { 0, 0x100000, -0x1000000 } };
        const long n = remaster_polygon(&t, across, 3, RED);
        CHECK(n > 0 && n < 64 * 48);
        free(t.color); free(t.inv_z);
    }
    /* 3. a wider frame adds columns and moves nothing: the same scene at 64 and 80 columns, the view's centre in the
     *    middle of each, agree on the columns the narrow one has (shifted by the 8 added on each side) */
    {
        remaster_target n = make(64, 48), w = make(80, 48);
        remaster_v3 a[4], b[4], c[4];
        quad(a, 0x06000000, 9, -7);
        quad(b, 0x03000000, 5, 4);
        quad(c, 0x0A000000, 30, 20);                           /* runs off the narrow frame, into the added columns */
        remaster_target *both[2] = { &n, &w };
        for (int i = 0; i < 2; i++) {
            remaster_polygon(both[i], c, 4, BLUE);
            remaster_polygon(both[i], a, 4, RED);
            remaster_polygon(both[i], b, 4, GREEN);
        }
        int same = 1, widened = 0;
        for (int y = 0; y < 48; y++) {
            for (int x = 0; x < 64; x++) same &= n.color[y * 64 + x] == w.color[y * 80 + x + 8];
            for (int x = 72; x < 80; x++) widened |= w.color[y * 80 + x] == BLUE;   /* the far square shows in the added columns */
        }
        CHECK(same);
        CHECK(widened);
        free(n.color); free(n.inv_z); free(w.color); free(w.inv_z);
    }
    /* 4. sub-pixel positions: a whole-pixel move shifts the picture by exactly that, a half-pixel move changes the
     *    coverage of the edge pixels (the original's whole-pixel positions cannot show either) */
    {
        const remaster_v3 tri[3] = { { 0, 0, 0x01000000 }, { 0x180000, 0, 0x01000000 }, { 0, 0x180000, 0x01000000 } };
        long count[3] = { 0, 0, 0 };
        uint32_t keep[2][48 * 64];
        const double shift[3] = { 0.0, 1.0, 0.5 };
        for (int s = 0; s < 3; s++) {
            remaster_target t = make(64, 48);
            t.cx += shift[s];
            count[s] = remaster_polygon(&t, tri, 3, RED);
            if (s < 2) memcpy(keep[s], t.color, sizeof keep[s]);
            if (s == 2) {
                int differs_from_both = 0;
                for (int i = 0; i < 64 * 48; i++) differs_from_both |= t.color[i] != keep[0][i] || t.color[i] != keep[1][i];
                CHECK(differs_from_both);
            }
            free(t.color); free(t.inv_z);
        }
        CHECK(count[0] == count[1]);                           /* a whole pixel over: the same pixels, moved */
        int moved = 1;
        for (int y = 0; y < 48; y++)
            for (int x = 1; x < 64; x++) moved &= keep[1][y * 64 + x] == keep[0][y * 64 + x - 1];
        CHECK(moved);
    }
    /* 5. lines: drawn with a width, hidden behind a nearer face, clipped at the near plane, a point is a square */
    {
        remaster_target t = make(64, 48);
        const remaster_v3 a = { -0x1000000, 0, 0x04000000 }, b = { 0x1000000, 0, 0x04000000 };    /* across the middle */
        const long n = remaster_line(&t, a, b, 3.0, RED);
        CHECK(n > 3 * 20);                                       /* a few pixels wide, some tens long */
        CHECK(t.color[24 * 64 + 32] == RED && t.color[10 * 64 + 32] == BG);
        remaster_v3 q[4];
        quad(q, 0x02000000, 8, 0);                               /* a nearer square covers the middle of the line */
        remaster_polygon(&t, q, 4, GREEN);
        CHECK(t.color[24 * 64 + 32] == GREEN && t.color[24 * 64 + 32 + 20] == RED);
        const remaster_v3 behind = { 0, 0, -0x1000000 };
        CHECK(remaster_line(&t, behind, behind, 3.0, BLUE) == 0);
        const remaster_v3 e = { 0x00A00000, -0x00500000, 0x08000000 };      /* the pixel 20 right of centre, 10 up: nothing nearer there */
        CHECK(remaster_line(&t, e, e, 4.0, BLUE) >= 12);         /* a point: a square of about 4 x 4 */
        free(t.color); free(t.inv_z);
    }
    /* 6. combining fills: AND turns the ground's palette index into the water's, OR sets bits, stipple blends, none moves depth */
    {
        uint32_t pal[256];
        for (int i = 0; i < 256; i++) pal[i] = 0xFF000000u | (uint32_t)i << 16 | (uint32_t)(255 - i) << 8;   /* colour from the index */
        remaster_target t = make(64, 48);
        t.index = (uint16_t *)malloc(64 * 48 * 2);
        t.palette = pal;
        remaster_clear(&t, BG);
        remaster_v3 q[4], r2[4];
        quad(q, 0x04000000, 14, 0);
        t.paint_index = 0x7C;                                    /* a ground-band colour */
        remaster_polygon(&t, q, 4, pal[0x7C]);
        const float z0 = t.inv_z[24 * 64 + 32];
        quad(r2, 0x03000000, 6, 0);                              /* a nearer square that ANDs with 6Fh */
        CHECK(remaster_polygon_logic(&t, r2, 4, REMASTER_AND, 0x6F) > 0);
        CHECK(t.index[24 * 64 + 32] == (0x7C & 0x6F) && t.color[24 * 64 + 32] == pal[0x7C & 0x6F]);   /* water: 6Ch */
        CHECK(t.color[24 * 64 + 32 + 10] == pal[0x7C] && t.index[24 * 64 + 32 + 10] == 0x7C);        /* outside the square */
        CHECK(t.inv_z[24 * 64 + 32] == z0);                      /* depth is left alone */
        CHECK(remaster_polygon_logic(&t, r2, 4, REMASTER_OR, 0x01) > 0 && t.index[24 * 64 + 32] == ((0x7C & 0x6F) | 0x01));
        CHECK(remaster_polygon_logic(&t, r2, 4, REMASTER_STIPPLE, 0x10) > 0 && t.index[24 * 64 + 32] == REMASTER_NO_INDEX);
        remaster_v3 far_q[4];
        quad(far_q, 0x08000000, 30, 0);                          /* behind the first square: an AND there does nothing */
        const uint32_t before = t.color[24 * 64 + 32 + 20];
        remaster_polygon_logic(&t, far_q, 4, REMASTER_AND, 0x0F);
        CHECK(t.color[24 * 64 + 32 + 20] == before);
        free(t.color); free(t.inv_z); free(t.index);
    }
    {   /* the fill draws exactly the pixels a brute-force test of every pixel of the bounding box would: random triangles,
         * thin diagonal slivers and partly off-screen ones included (the row spans only skip pixels that are certainly outside) */
        unsigned seed = 12345u;
        #define RND() (seed = seed * 1664525u + 1013904223u, (double)(seed >> 8) / 16777216.0)
        long compared = 0, bad = 0;
        for (int trial = 0; trial < 4000; trial++) {
            remaster_target t = make(96, 72);
            const int z = 0x00800000;                            /* a constant depth, so depth plays no part */
            double sx[3], sy[3];
            for (int k = 0; k < 3; k++) { sx[k] = RND() * 160.0 - 32.0; sy[k] = RND() * 120.0 - 24.0; }
            if (trial % 3 == 0) {                                /* a sliver: two vertices close together, the third far off */
                sx[1] = sx[0] + RND() * 2.0; sy[1] = sy[0] + RND() * 2.0;
            }
            remaster_v3 v[3];
            for (int k = 0; k < 3; k++) {
                const double kx = (double)z / 256.0;
                v[k] = (remaster_v3){ (int32_t)((sx[k] - t.cx) * kx), (int32_t)((sy[k] - t.cy) * kx), z };
            }
            remaster_polygon(&t, v, 3, RED);
            /* the reference, from the vertices as the fill sees them (truncated to the integers it was given) */
            double px[3], py[3];
            for (int k = 0; k < 3; k++) { px[k] = t.cx + t.sx * (double)v[k].x / z; py[k] = t.cy + t.sy * (double)v[k].y / z; }
            const double den = (py[1] - py[2]) * (px[0] - px[2]) + (px[2] - px[1]) * (py[0] - py[2]);
            /* exact mode (the default; F117_RASTER_EXACT=0 turns it off): the rule is the snapped-integer one of exact.h, so the reference is too */
            const char *ee = getenv("F117_RASTER_EXACT");
            const int exact = !(ee && *ee == '0');
            ex_tri et;
            const int et_ok = exact && ex_setup(&et, px[0], py[0], px[1], py[1], px[2], py[2]);
            for (int y = 0; y < t.h; y++)
                for (int x = 0; x < t.w; x++) {
                    int inside = 0;
                    if (exact) inside = et_ok && ex_covers(&et, x, y);
                    else if (fabs(den) >= 1e-12) {
                        const double cx = x + 0.5, cy = y + 0.5;
                        const double l1 = ((py[1] - py[2]) * (cx - px[2]) + (px[2] - px[1]) * (cy - py[2])) / den;
                        const double l2 = ((py[2] - py[0]) * (cx - px[2]) + (px[0] - px[2]) * (cy - py[2])) / den;
                        inside = !(l1 < 0 || l2 < 0 || 1.0 - l1 - l2 < 0);
                    }
                    const int drawn = t.color[(long)y * t.w + x] == RED;
                    compared++;
                    bad += inside != drawn;
                }
            free(t.color); free(t.inv_z);
        }
        #undef RND
        printf("fill vs brute force: %ld pixels compared, %ld differ\n", compared, bad);
        CHECK(bad == 0);
    }
    /* 3. the whole-line pass (line_slope): a light behind a flat wall stays hidden, a marking on a ground seen at a grazing angle shows */
    {
        remaster_target t = make(64, 48);
        remaster_v3 wall[4];
        quad(wall, 0x02000000, 14, 0);
        remaster_polygon(&t, wall, 4, RED);
        const remaster_v3 ground[4] = { { -0x300000, 0x40000, 0x300000 }, { 0x300000, 0x40000, 0x300000 }, { 0x300000, 0x40000, 0x4000000 }, { -0x300000, 0x40000, 0x4000000 } };
        remaster_polygon(&t, ground, 4, BLUE);
        const size_t px = (size_t)t.w * t.h;
        float *ref = (float *)malloc(px * 4);
        memcpy(ref, t.inv_z, px * 4);
        {   /* the old whole-line pass (a bias of 50, no slope) draws the same light through the wall: the defect, kept as the proof */
            uint32_t *c0 = (uint32_t *)malloc(px * 4); float *z0 = (float *)malloc(px * 4);
            memcpy(c0, t.color, px * 4); memcpy(z0, t.inv_z, px * 4);
            t.line_bias = 50.0; t.line_slope = 0.0;
            remaster_line(&t, (remaster_v3){ 0, 0, 0x03000000 }, (remaster_v3){ 0, 0, 0x03000000 }, 3.0, GREEN);
            CHECK(t.color[24 * 64 + 32] == GREEN);
            memcpy(t.color, c0, px * 4); memcpy(t.inv_z, z0, px * 4);
            free(c0); free(z0);
        }
        t.line_bias = 1.01; t.line_slope = 2.0; t.line_ref_z = ref;
        /* a light 1.5 times farther than the wall, behind its middle */
        remaster_line(&t, (remaster_v3){ 0, 0, 0x03000000 }, (remaster_v3){ 0, 0, 0x03000000 }, 3.0, GREEN);
        CHECK(t.color[24 * 64 + 32] == RED);
        /* a marking lying in the ground's plane, 1 pixel wide, across it at one depth */
        remaster_line(&t, (remaster_v3){ -0x100000, 0x40000, 0x800000 }, (remaster_v3){ 0x100000, 0x40000, 0x800000 }, 1.0, GREEN);
        int green = 0;
        for (int y = 0; y < t.h; y++) for (int x = 0; x < t.w; x++) green += t.color[y * 64 + x] == GREEN;
        CHECK(green >= 6);
        free(ref); free(t.color); free(t.inv_z);
    }
    /* 4. rows shared among workers: drawing the same scene in row bands (y_lo, y_hi) gives exactly the picture drawn whole */
    {
        remaster_target whole = make(64, 48), banded = make(64, 48);
        remaster_v3 far_q[4], near_q[4];
        quad(far_q, 0x08000000, 20, 3);
        quad(near_q, 0x03000000, 9, -6);
        const remaster_v3 slanted[4] = { { -0x300000, 0x40000, 0x300000 }, { 0x300000, 0x40000, 0x300000 }, { 0x300000, 0x40000, 0x4000000 }, { -0x300000, 0x40000, 0x4000000 } };
        const uint32_t cols[4] = { 0xFF204060u, 0xFF4060A0u, 0xFFA0C0E0u, 0xFF102030u };
        remaster_target *both[2] = { &whole, &banded };
        for (int k = 0; k < 2; k++) {
            const int bands = k == 0 ? 1 : 4;
            for (int b = 0; b < bands; b++) {
                remaster_target *t = both[k];
                t->y_lo = t->h * b / bands; t->y_hi = t->h * (b + 1) / bands;
                if (bands == 1) t->y_lo = t->y_hi = 0;
                remaster_clear(t, 0xFF000000u);
                remaster_polygon(t, far_q, 4, RED);
                remaster_polygon(t, near_q, 4, GREEN);
                remaster_polygon_shaded(t, far_q, cols, 4);
                remaster_polygon(t, slanted, 4, BLUE);
                remaster_line(t, (remaster_v3){ -0x400000, 0x20000, 0x1000000 }, (remaster_v3){ 0x400000, -0x10000, 0x2000000 }, 3.0, 0xFFFFFF00u);
            }
        }
        CHECK(!memcmp(whole.color, banded.color, (size_t)64 * 48 * 4) && !memcmp(whole.inv_z, banded.inv_z, (size_t)64 * 48 * 4));
        free(whole.color); free(whole.inv_z); free(banded.color); free(banded.inv_z);
    }
    /* Native coordinates from two terrain rings (or an aircraft's model scale)
     * cannot be compared directly. Keep their projection, compare world depth. */
    {
        remaster_target t = make(64, 48);
        remaster_v3 far_q[4], near_q[4];
        quad(far_q, 0x02000000, 14, 0);
        quad(near_q, 0x03000000, 8, 0);
        t.inv_depth_scale = 1.0;
        remaster_polygon(&t, far_q, 4, RED);
        t.inv_depth_scale = 2.0;
        remaster_polygon(&t, near_q, 4, GREEN);
        CHECK(t.color[24 * 64 + 32] == GREEN);
        t.inv_depth_scale = 1.0;
        remaster_polygon(&t, far_q, 4, RED);
        CHECK(t.color[24 * 64 + 32] == GREEN);
        /* A line at a smaller native z is still behind the nearer object. */
        t.line_bias = 1.0;
        remaster_line(&t, (remaster_v3){-0x100000, 0, 0x02000000},
                          (remaster_v3){0x100000, 0, 0x02000000}, 3.0, BLUE);
        CHECK(t.color[24 * 64 + 32] == GREEN);
        const uint32_t colors[4] = { RED, RED, RED, RED };
        remaster_polygon_shaded(&t, far_q, colors, 4);
        CHECK(t.color[24 * 64 + 32] == GREEN);
        t.inv_depth_scale = 4.0;
        remaster_line(&t, (remaster_v3){0, 0, 0x02000000},
                          (remaster_v3){0, 0, 0x02000000}, 3.0, BLUE);
        CHECK(t.color[24 * 64 + 32] == BLUE);
        free(t.color); free(t.inv_z);
    }
    /* A material face can contain both front and rear door edges. Sharing its
     * owner is not permission for a rear marking to shine through a hangar. */
    {
        remaster_target t = make(64, 48);
        t.owner = (uint32_t *)calloc(64 * 48, sizeof *t.owner);
        t.paint_owner = 0x1001;
        remaster_v3 wall[4];
        quad(wall, 0x02000000, 14, 0);
        remaster_polygon(&t, wall, 4, RED);
        t.line_decal = 1; t.line_order = 1; t.line_bias = 1.0;
        remaster_line(&t, (remaster_v3){-0x100000, 0, 0x04000000},
                          (remaster_v3){0x100000, 0, 0x04000000}, 3.0, GREEN);
        CHECK(t.color[24 * 64 + 32] == RED);
        remaster_line(&t, (remaster_v3){-0x100000, 0, 0x02000000},
                          (remaster_v3){0x100000, 0, 0x02000000}, 3.0, GREEN);
        CHECK(t.color[24 * 64 + 32] == GREEN);
        /* The rear stroke also stays hidden where its width extends below the
         * wall's raster coverage. A front stroke retains that same footprint. */
        t.line_decal = 0; t.line_order = 0;
        remaster_clear(&t, BG);
        remaster_polygon(&t, wall, 4, RED);
        t.line_decal = 1; t.line_order = 1;
        const int32_t far_y = (int32_t)(13.25 * 0x04000000 / 256.0);
        remaster_line(&t, (remaster_v3){-0x100000, far_y, 0x04000000},
                          (remaster_v3){0x100000, far_y, 0x04000000}, 4.0, GREEN);
        CHECK(t.color[38 * 64 + 32] == BG);
        remaster_line(&t, (remaster_v3){-0x100000, far_y / 2, 0x02000000},
                          (remaster_v3){0x100000, far_y / 2, 0x02000000}, 4.0, GREEN);
        CHECK(t.color[38 * 64 + 32] == GREEN);
        free(t.color); free(t.inv_z); free(t.owner);
    }
    /* Point decorations on a stroke stay visible independent of material order.
     * Depth still hides a light behind another object or a clearly nearer stroke. */
    {
        remaster_target t = make(64, 48);
        t.owner = (uint32_t *)calloc(64 * 48, sizeof *t.owner);
        t.stroke_z = (float *)calloc(64 * 48, sizeof *t.stroke_z);
        t.stroke_aux_x = (float *)calloc(64 * 48, sizeof *t.stroke_aux_x);
        t.stroke_aux_y = (float *)calloc(64 * 48, sizeof *t.stroke_aux_y);
        t.stroke_owner = (uint32_t *)calloc(64 * 48, sizeof *t.stroke_owner);
        const remaster_v3 light = { 0, 0, 0x02000000 };
        const remaster_v3 a = { -0x100000, 0, 0x02000000 }, b = { 0x100000, 0, 0x02000000 };
        const uint32_t cream = 0xFFFFFFAAu;
        /* A steep edge shares the light's vertex. Its finite-width sample can
         * be over 1% nearer than that endpoint; ownership plus the shared
         * endpoint, rather than a larger depth bias, preserves the light. */
        for (int reverse = 0; reverse < 2; ++reverse) {
            remaster_clear(&t, BG);
            memset(t.stroke_z, 0, 64 * 48 * sizeof *t.stroke_z);
            t.line_decal = 1; t.line_order = 1; t.line_bias = 1.0; t.paint_owner = 0x1001;
            if (!reverse) remaster_line(&t, light, light, 4.0, cream);
            remaster_line(&t, light, (remaster_v3){0x20000, 0, 0x01000000}, 2.0, RED);
            if (reverse) remaster_line(&t, light, light, 4.0, cream);
            CHECK(t.color[24 * 64 + 32] == cream);
        }
        for (int reverse = 0; reverse < 2; ++reverse) {
            remaster_clear(&t, BG);
            memset(t.stroke_z, 0, 64 * 48 * sizeof *t.stroke_z);
            t.line_decal = 1; t.line_order = 1; t.line_bias = 1.0; t.paint_owner = 0x1001;
            if (!reverse) remaster_line(&t, light, light, 4.0, cream);
            remaster_line(&t, a, b, 2.0, RED);
            if (reverse) remaster_line(&t, light, light, 4.0, cream);
            CHECK(t.color[24 * 64 + 32] == cream);
        }
        /* A crossing stroke from another object still compares at the
         * light centre, regardless of which object was painted first. */
        for (int reverse = 0; reverse < 2; ++reverse) {
            remaster_clear(&t, BG);
            memset(t.stroke_z, 0, 64 * 48 * sizeof *t.stroke_z);
            t.paint_owner = 0x1001;
            if (!reverse) remaster_line(&t, light, light, 4.0, cream);
            t.paint_owner = 0x2001;
            remaster_line(&t, (remaster_v3){a.x, 0, 0x03000000},
                          (remaster_v3){b.x, 0, 0x03000000}, 2.0, RED);
            t.paint_owner = 0x1001;
            if (reverse) remaster_line(&t, light, light, 4.0, cream);
            CHECK(t.color[24 * 64 + 32] == cream);
        }
        /* A same-object stroke sufficiently in front also wins. */
        memset(t.stroke_z, 0, 64 * 48 * sizeof *t.stroke_z);
        remaster_line(&t, light, light, 4.0, cream);
        remaster_line(&t, (remaster_v3){a.x, 0, 0x01000000}, (remaster_v3){b.x, 0, 0x01000000}, 2.0, GREEN);
        CHECK(t.color[24 * 64 + 32] == GREEN);
        /* Ordinary strokes retain their existing order at equal depth. */
        remaster_line(&t, (remaster_v3){a.x, 0, 0x01000000}, (remaster_v3){b.x, 0, 0x01000000}, 2.0, RED);
        CHECK(t.color[24 * 64 + 32] == RED);
        /* Opaque occluders still hide lights, including another face of the
         * same object. The point priority applies only after polygon culling. */
        for (int self = 0; self < 2; ++self) {
            t.line_decal = 0; t.line_order = 0; t.paint_owner = self ? 0x1001 : 0x2001;
            remaster_v3 wall[4]; quad(wall, 0x01000000, 12, 0);
            remaster_polygon(&t, wall, 4, BLUE);
            t.line_decal = 1; t.line_order = 1; t.paint_owner = 0x1001;
            remaster_line(&t, light, light, 4.0, cream);
            CHECK(t.color[24 * 64 + 32] == BLUE);
        }
        free(t.color); free(t.inv_z); free(t.owner); free(t.stroke_z); free(t.stroke_aux_x); free(t.stroke_aux_y); free(t.stroke_owner);
    }
    /* An attached light remains a whole billboard on a steep roof/fin through
     * sub-pixel camera motion. Per-pixel depth used to carve a moving stripe. */
    {
        remaster_target t = make(64, 48), reference = make(64, 48);
        t.owner = (uint32_t *)calloc(64 * 48, sizeof *t.owner);
        const remaster_v3 roof[4] = { {-0x300000,0x40000,0x300000}, {0x300000,0x40000,0x300000},
                                      {0x300000,0x40000,0x4000000}, {-0x300000,0x40000,0x4000000} };
        const remaster_v3 light = {0,0x40000,0x2000000};
        for (int phase = 0; phase < 16; ++phase) {
            t.cy = 24.0 + phase / 16.0;
            t.line_order = 0; t.line_decal = 0; t.paint_owner = 0x1001;
            remaster_clear(&t, BG);
            remaster_polygon(&t, roof, 4, RED);
            t.line_order = 1; t.line_decal = 1; t.line_bias = 1.0;
            remaster_line(&t, light, light, 4.0, GREEN);
            reference.cy = t.cy;
            remaster_clear(&reference, BG);
            remaster_line(&reference, light, light, 4.0, GREEN);
            for (int i = 0; i < 64 * 48; ++i)
                CHECK((t.color[i] == GREEN) == (reference.color[i] == GREEN));
            /* The same projection, but genuinely behind that roof. */
            remaster_clear(&t, BG);
            t.line_order = 0; t.line_decal = 0;
            remaster_polygon(&t, roof, 4, RED);
            t.line_order = 1; t.line_decal = 1;
            remaster_line(&t, (remaster_v3){0,0x80000,0x4000000}, (remaster_v3){0,0x80000,0x4000000}, 4.0, BLUE);
            for (int i = 0; i < 64 * 48; ++i) CHECK(t.color[i] != BLUE);
        }
        free(t.color); free(t.inv_z); free(t.owner); free(reference.color); free(reference.inv_z);
    }
    /* Finite strokes include their endpoint footprint, just like point
     * decorations. A short cross-stroke covers the shared pole endpoint
     * through sub-pixel motion rather than exposing a grey sliver there. */
    {
        remaster_target t = make(64, 48);
        t.line_order = 1; t.line_bias = 1.0;
        const remaster_v3 join = {0, 0, 0x02000000};
        const remaster_v3 pole = {0, 0x100000, 0x02000000};
        const remaster_v3 flag = {0x20000, 0, 0x02000000};
        for (int phase = 0; phase < 16; ++phase) {
            t.cx = 32.0 + phase / 16.0;
            t.cy = 24.0 + phase / 16.0;
            remaster_clear(&t, BG);
            remaster_line(&t, join, pole, 4.0, GREEN);
            remaster_line(&t, join, flag, 4.0, RED);
            for (int y = 0; y < 48; ++y)
                for (int x = 0; x < 64; ++x)
                    if (fabs(x + 0.5 - t.cx) < 1.9 && fabs(y + 0.5 - t.cy) < 1.9)
                        CHECK(t.color[y * 64 + x] == RED);
        }
        free(t.color); free(t.inv_z);
    }
    /* A ground plane under a low camera must reach the bottom of a tall
     * side margin. The model near plane used to cut it off inside the view. */
    {
        remaster_target t = make(64, 48);
        const remaster_v3 floor[4] = {{-0x100000,0x1000,-0x100000}, {0x100000,0x1000,-0x100000},
                                     {0x100000,0x1000,0x100000}, {-0x100000,0x1000,0x100000}};
        remaster_polygon(&t, floor, 4, BLUE);
        CHECK(t.color[47 * 64 + 32] == BG);
        t.near_z = 0x1000;
        remaster_polygon(&t, floor, 4, BLUE);
        CHECK(t.color[47 * 64 + 32] == BLUE);
        /* Coast tiles use a palette OR fill; it needs the same clipping. */
        uint32_t palette[256] = {0}; palette[128] = BLUE;
        t.palette = palette;
        t.index = (uint16_t *)calloc(64 * 48, sizeof *t.index);
        for (int low = 0; low < 2; ++low) {
            remaster_clear(&t, BG);
            memset(t.index, 0, 64 * 48 * sizeof *t.index);
            t.near_z = low ? 0x1000 : 0x10000;
            remaster_polygon_logic(&t, floor, 4, REMASTER_OR, 128);
            CHECK(t.color[47 * 64 + 32] == (low ? BLUE : BG));
        }
        free(t.index);
        free(t.color); free(t.inv_z);
    }
    puts("remaster raster passes");
    /* R38 face cache: dedupe, coverage lookup, size/allocation failure, and a recording pass that must not change the picture */
    {
        decal_face_cache c = { 0 };
        df_reset(&c);
        CHECK(!c.failed && c.words);
        ex_tri tri = { { 0, 0, 40 * 256, 0, 0, 40 * 256 }, 40LL * 40 * 256 * 256 };
        ex_planes pl = { 0 };
        df_add(&c, 0x1234u, &tri, &pl);
        df_add(&c, 0x1234u, &tri, &pl);                          /* tiled row stages meet the same face again */
        CHECK(c.count == 1);
        CHECK(df_contains(&c, 0x1234u, 5.0, 5.0));              /* inside the face */
        CHECK(!df_contains(&c, 0x1234u, 38.0, 38.0));           /* outside its triangle: the face cannot hide a line there */
        CHECK(df_contains(&c, 0x9999u, 5.0, 5.0));              /* an unknown owner never rejects */
        df_reset(&c);
        CHECK(c.count == 0 && !c.failed);                        /* reuse across frames */
        CHECK(!df_reserve(&c, (size_t)-1 / 8) && c.failed);      /* an impossible size fails cleanly and disables the rule */
        df_add(&c, 0x1234u, &tri, &pl);                          /* a failed cache ignores further faces */
        CHECK(c.count == 0);
        df_reset(&c);
        CHECK(!c.failed && c.count == 0);                        /* the next frame starts afresh */
        free(c.words);
    }
    {
        static uint32_t ref_px[64 * 48];
        decal_face_cache ctx = { 0 }, bad = { 0 };
        for (int pass = 0; pass < 3; pass++) {                   /* 0 off, 1 on with a caller context, 2 on with a failed cache */
            SETENV("F117_DECAL_FACE", pass ? "1" : "0");
            remaster_target t = make(64, 48);
            void *previous = pass ? remaster_faces_use(pass == 1 ? &ctx : &bad) : NULL;
            t.paint_owner = 0x3000;
            remaster_clear(&t, BG);
            if (pass == 2) bad.failed = 1;
            remaster_v3 q[4]; quad(q, 0x04000000, 12, 0);
            remaster_polygon(&t, q, 4, RED);
            if (pass == 0) memcpy(ref_px, t.color, sizeof ref_px);
            else CHECK(memcmp(ref_px, t.color, sizeof ref_px) == 0);
            if (pass == 1) CHECK(ctx.count > 0);                 /* the caller's context received the faces */
            if (pass) remaster_faces_use(previous);
            free(t.color); free(t.inv_z);
        }
        free(ctx.words); free(bad.words);
        SETENV("F117_DECAL_FACE", "1"); SETENV("F117_RASTER_EXACT", "0");
        CHECK(!decal_faces_enabled());                           /* the float path never uses the new rule */
        SETENV("F117_RASTER_EXACT", "1"); CHECK(decal_faces_enabled());
        SETENV("F117_RASTER_EXACT", ""); SETENV("F117_DECAL_FACE", "0");
        CHECK(!decal_faces_enabled());                           /* 0 turns it off */
        SETENV("F117_DECAL_FACE", ""); CHECK(decal_faces_enabled());   /* unset or empty: on by default */
    }
    return 0;
}
