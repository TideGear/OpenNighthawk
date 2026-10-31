/* test_remaster_raster.c - the Remastered triangle fill: depth order does not depend on draw order, the near plane clips,
 * a wider frame adds columns without moving what was drawn, and positions are sub-pixel. */
#include "remaster_raster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    puts("remaster raster passes");
    return 0;
}
