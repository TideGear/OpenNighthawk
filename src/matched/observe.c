/* observe.c - see observe.h. */
#include "observe.h"
#include "matched.h"
#include "recomp_rt.h"
#include "cpu.h"
#include <stdlib.h>

const f117_observer *g_f117_observer;

void observe_set(const f117_observer *o) { g_f117_observer = o; }

static int32_t ds_dword(cpu_t *c, uint16_t at)
{
    const uint16_t ds = c->seg[S_DS];
    return (int32_t)((uint32_t)seg_read16(c, ds, at) | ((uint32_t)seg_read16(c, ds, (uint16_t)(at + 2)) << 16));
}

/* (flush, below) */
static void flush(machine_t *m);

void observe_vertex(machine_t *m, uint16_t di, uint16_t bx)
{
    flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->vertex) return;
    cpu_t *c = &m->cpu;
    int32_t xf[3], px[2] = { 0, 0 };
    for (int k = 0; k < 3; k++) xf[k] = ds_dword(c, (uint16_t)(di + 4 * k));
    const int16_t zhi = (int16_t)seg_read16(c, c->seg[S_DS], (uint16_t)(di + 0x0A));
    const int range = zhi >= 0x100 ? 0 : zhi >= 1 ? 1 : 2;
    if (range != 2)
        for (int k = 0; k < 2; k++) px[k] = ds_dword(c, (uint16_t)(bx + 4 * k));
    o->vertex(o->user, c->icount, xf, px, range, di, bx);
}

void observe_edge_prepared(machine_t *m, uint16_t slot, uint16_t di, uint16_t bx)
{
    flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return;
    const int32_t v[3] = { slot, (uint16_t)(di + 0xD6B4), (uint16_t)(bx + 0xD6B4) };
    o->prim(o->user, m->cpu.icount, 'G', v, 3);
}

/* The pixels a fill is about to paint: the R hook records each row's span,
 * clamped to the viewport, and the page bytes there ('b'); the next event the
 * observer sees - nothing draws in between - records the same bytes again
 * ('a'). Replaying the fill style on the 'b' bytes must give the 'a' bytes. */
static struct {
    int n;
    uint16_t es;
    struct { int16_t y, x0; uint16_t at, len; } row[256];
} g_pend;

static void emit_row_bytes(machine_t *m, char kind)
{
    const f117_observer *o = g_f117_observer;
    cpu_t *c = &m->cpu;
    for (int k = 0; k < g_pend.n; k++) {
        int32_t v[5 + 330];
        int n = 0;
        v[n++] = g_pend.row[k].y; v[n++] = g_pend.row[k].x0; v[n++] = g_pend.row[k].len;
        v[n++] = g_pend.es; v[n++] = g_pend.row[k].at;
        for (unsigned i = 0; i < g_pend.row[k].len && n < 5 + 330; i++)
            v[n++] = mem_read8(c, phys(g_pend.es, (uint16_t)(g_pend.row[k].at + i)));
        o->prim(o->user, c->icount, kind, v, n);
    }
}

/* Called first by every observer entry: closes a pending fill. */
static void line_flush(machine_t *m);
static void flush(machine_t *m)
{
    line_flush(m);
    if (!g_pend.n) return;
    const f117_observer *o = g_f117_observer;
    if (o && o->prim) emit_row_bytes(m, 'a');
    g_pend.n = 0;
}

/* The hooks below are told to the observer and then left alone - each
 * returns 0, so the original instruction runs. */

/* VGAME 0x01450, game_draw. */
static int hook_game_draw(machine_t *m)
{
    flush(m);
    const f117_observer *o = g_f117_observer;
    if (o && o->frame_phase) o->frame_phase(o->user, m->cpu.icount);
    /* The whole page the library draws to ('Z': its segment, then 64,000
     * bytes), so a frame can be rebuilt from the previous one plus every
     * primitive captured in between, and what is left counted. */
    if (o && o->prim && getenv("F117R_OBSERVE_PAGES")) {
        cpu_t *c = &m->cpu;
        const uint16_t drv = seg_read16(c, (uint16_t)(c->seg[S_DS]), 0x01B5 + 3);
        if (drv) {
            static int32_t v[2 + 65536];
            const uint16_t page = seg_read16(c, drv, 0x0194), origin = seg_read16(c, drv, 0x0196);
            v[0] = page; v[1] = origin;
            for (uint32_t a = 0; a < 65536; a++) v[2 + a] = mem_read8(c, phys(page, (uint16_t)a));
            o->prim(o->user, c->icount, 'Z', v, 2 + 65536);
            /* and the display ('Y': A000, 64,000 bytes), which the HUD phase
             * reaches by blits from the work page and by drawing on it */
            v[0] = 0xA000; v[1] = 0;
            for (uint32_t a = 0; a < 64000; a++) v[2 + a] = mem_read8(c, phys(0xA000, (uint16_t)a));
            o->prim(o->user, c->icount, 'Y', v, 2 + 64000);
        }
    }
    return 0;
}

/* 130D:004A, one edge of a filled polygon: SI the slot. */
static int hook_poly_edge(machine_t *m)
{
    flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    cpu_t *c = &m->cpu;
    const uint16_t si = c->r[R_SI];
    const int32_t v[8] = { si, ds_dword(c, si), ds_dword(c, (uint16_t)(si + 4)), ds_dword(c, (uint16_t)(si + 8)),
                           ds_dword(c, (uint16_t)(si + 0x0C)), (int32_t)seg_read16(c, c->seg[S_DS], (uint16_t)(si + 2)),
                           ds_dword(c, (uint16_t)(si + 0x10)), ds_dword(c, (uint16_t)(si + 0x14)) };
    o->prim(o->user, c->icount, 'E', v, 8);
    return 0;
}

/* 130D:0116, the filled polygon closed and filled: the colour word above
 * the far return address, then what the edges left for the fill - the
 * viewport (85FA..8600), the polygon accumulator (85E2..85EE) and the span
 * rows from the top row (9160) down while a row holds a bound (left at
 * 89DC + 2y, right at 8D9E + 2y): the ground truth for replaying the edges. */
static int hook_poly_fill(machine_t *m)
{
    flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    cpu_t *c = &m->cpu;
    const uint16_t ds = c->seg[S_DS];
    int32_t v[14 + 2 * 256];
    int n = 0;
    v[n++] = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_SP] + 4));
    for (uint16_t a = 0x85FA; a <= 0x8600; a = (uint16_t)(a + 2)) v[n++] = (int16_t)seg_read16(c, ds, a);
    for (uint16_t a = 0x85E2; a <= 0x85EE; a = (uint16_t)(a + 2)) v[n++] = (int16_t)seg_read16(c, ds, a);
    const int16_t top = (int16_t)seg_read16(c, ds, 0x9160);
    v[n++] = top;
    const int rows_at = n++;
    int rows = 0;
    if (top >= 0 && top < 256)
        for (int y = top; y < 256; y++) {
            const int16_t l = (int16_t)seg_read16(c, ds, (uint16_t)(0x89DC + 2 * y));
            const int16_t r = (int16_t)seg_read16(c, ds, (uint16_t)(0x8D9E + 2 * y));
            if (l == 0x7FFF && (uint16_t)r == 0x8001u) break;
            v[n++] = l; v[n++] = r; rows++;
        }
    v[rows_at] = rows;
    o->prim(o->user, c->icount, 'F', v, n);
    return 0;
}

/* 1377:005E, the fill paints: the span rows as they stand once the fill
 * entry has added the near-clip join and the border runs - the ground truth
 * for the whole polygon - and the colour word in AX. */
static int hook_fill_rows(machine_t *m)
{
    flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    cpu_t *c = &m->cpu;
    const uint16_t ds = c->seg[S_DS];
    int32_t v[3 + 2 * 256];
    int n = 0;
    v[n++] = c->r[R_AX];
    const int16_t top = (int16_t)seg_read16(c, ds, 0x9160);
    v[n++] = top;
    const int rows_at = n++;
    int rows = 0;
    if (top >= 0 && top < 256)
        for (int y = top; y < 256; y++) {
            const int16_t l = (int16_t)seg_read16(c, ds, (uint16_t)(0x89DC + 2 * y));
            const int16_t r = (int16_t)seg_read16(c, ds, (uint16_t)(0x8D9E + 2 * y));
            if (l == 0x7FFF && (uint16_t)r == 0x8001u) break;
            v[n++] = l; v[n++] = r; rows++;
        }
    v[rows_at] = rows;
    v[n++] = seg_read16(c, ds, 0x8606);                           /* the colour the fill uses, after the fade */
    v[n++] = c->seg[S_ES];
    o->prim(o->user, c->icount, 'R', v, n);
    /* the page bytes the rows cover, clamped to the viewport */
    const int16_t xmin = (int16_t)seg_read16(c, ds, 0x85FA), xmax = (int16_t)seg_read16(c, ds, 0x85FE);
    g_pend.n = 0;
    g_pend.es = c->seg[S_ES];
    for (int k = 0; k < rows && g_pend.n < 256; k++) {
        const int16_t l = (int16_t)v[3 + 2 * k], r = (int16_t)v[4 + 2 * k];
        const int16_t x0 = l < xmin ? xmin : l, x1 = r > xmax ? xmax : r;
        if (x0 > x1) continue;
        const int y = top + k;
        g_pend.row[g_pend.n].y = (int16_t)y;
        g_pend.row[g_pend.n].x0 = x0;
        g_pend.row[g_pend.n].at = (uint16_t)(seg_read16(c, ds, (uint16_t)(0x861C + 2 * y)) + x0);
        g_pend.row[g_pend.n].len = (uint16_t)(x1 - x0 + 1);
        g_pend.n++;
    }
    emit_row_bytes(m, 'b');
    return 0;
}

/* 1377:004C, a fill or an outline polygon begun (the fill entry calls it
 * too): AX the colour word. */
static int hook_outline_begin(machine_t *m)
{
    flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    const int32_t v[1] = { m->cpu.r[R_AX] };
    o->prim(o->user, m->cpu.icount, 'B', v, 1);
    return 0;
}

/* 1377:0055, an outline edge drawn as a line: SI the slot. */
static int hook_outline_edge(machine_t *m)
{
    flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    /* the slot, its endpoints (x0 [si], y0 [si+4], x1 [si+8], y1 [si+0Ch],
     * the low words of 32-bit fields), the colour [8606], the page [861A],
     * the first two rows of the row table [861C], and the handler the style
     * table [85F2] sends it to (1377:046F, the plain line, so far) */
    cpu_t *c = &m->cpu;
    const uint16_t ds = c->seg[S_DS], si = c->r[R_SI];
    const int32_t v[10] = { si,
        (int16_t)seg_read16(c, ds, si), (int16_t)seg_read16(c, ds, (uint16_t)(si + 4)),
        (int16_t)seg_read16(c, ds, (uint16_t)(si + 8)), (int16_t)seg_read16(c, ds, (uint16_t)(si + 12)),
        seg_read16(c, ds, 0x8606), seg_read16(c, ds, 0x861A),
        seg_read16(c, ds, 0x861C), seg_read16(c, ds, 0x861E),
        seg_read16(c, c->seg[S_CS], (uint16_t)(seg_read16(c, ds, 0x85F2) + 6)) };
    o->prim(o->user, c->icount, 'L', v, 10);
    return 0;
}

/* ---- the resident graphics library's lines ---------------------------
 * VGAME reaches the library through jump slots at 1E42:011A + 5n (graphics
 * entry n), each a JMP FAR patched at load. The driver keeps, in its own code
 * segment, the page segment (cs:[0194]), the origin (cs:[0196]) and the row
 * table (cs:[0004 + 2y]); a pixel is page:rowtab[y] + origin + x. */
static uint16_t slot_target_seg(machine_t *m, uint16_t slot)
{
    cpu_t *c = &m->cpu;
    return seg_read16(c, c->seg[S_CS], (uint16_t)(slot + 3));
}

static struct {
    int on;
    uint16_t page, x0, y0, w, h;
    uint16_t at[200];
    uint8_t before[200 * 320];
} g_line;

/* With F117R_OBSERVE_PAGES, an entry no hook decodes keeps the whole active
 * page and, when different, the VGA display page. The next event logs every
 * changed byte ('x': entry, page segment, count, then offset, now and before
 * for each). A nested entry can split a caller's drawing, so each boundary
 * starts a fresh capture. */
static struct {
    int on, entry;
    uint16_t page;
    uint8_t before[65536];
} g_any;
/* A library entry can name a work page in its driver state and still draw
 * straight to VGA memory. Keep the display alongside the active page while
 * an otherwise uncaptured entry runs. */
static struct {
    int on, entry;
    uint8_t before[65536];
} g_any_display;

static void any_emit(machine_t *m, int entry, uint16_t page,
                     const uint8_t *before, int emit_empty)
{
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return;
    cpu_t *c = &m->cpu;
    static int32_t v[3 + 3 * 65536];
    int n = 0;
    v[n++] = entry; v[n++] = page;
    const int count_at = n++;
    for (uint32_t a = 0; a < 65536; a++) {
        const uint8_t now = mem_read8(c, phys(page, (uint16_t)a));
        if (now != before[a]) { v[n++] = (int32_t)a; v[n++] = now; v[n++] = before[a]; }
    }
    v[count_at] = (n - 3) / 3;
    if (emit_empty || n > 3) o->prim(o->user, c->icount, 'x', v, n);
}

static void any_flush(machine_t *m)
{
    if (g_any.on) {
        g_any.on = 0;
        any_emit(m, g_any.entry, g_any.page, g_any.before, 1);
    }
    if (g_any_display.on) {
        g_any_display.on = 0;
        any_emit(m, g_any_display.entry, 0xA000, g_any_display.before, 0);
    }
}

static void line_flush(machine_t *m)
{
    any_flush(m);
    if (!g_line.on) return;
    g_line.on = 0;
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return;
    cpu_t *c = &m->cpu;
    int32_t v[4 + 4 * 2000];
    int n = 0;
    v[n++] = g_line.page; v[n++] = 0;
    const int count_at = n++;
    int changed = 0;
    for (unsigned y = 0; y < g_line.h; y++)
        for (unsigned x = 0; x < g_line.w; x++) {
            const uint8_t now = mem_read8(c, phys(g_line.page, (uint16_t)(g_line.at[y] + x)));
            if (now != g_line.before[y * g_line.w + x] && n + 4 <= (int)(sizeof v / sizeof v[0])) {
                v[n++] = (int32_t)(uint16_t)(g_line.at[y] + x); v[n++] = (int32_t)(g_line.x0 + x) | ((int32_t)(g_line.y0 + y) << 16);
                v[n++] = now; v[n++] = g_line.before[y * g_line.w + x]; changed++;
            }
        }
    v[count_at] = changed;
    o->prim(o->user, c->icount, 'n', v, n);
}

/* Graphics entry 31 (1E42:01B5), the line drawer: (AX, BX) to (CX, DX). The
 * bounding box's page bytes are kept, and the next event logs the pixels the
 * line changed ('n'). */
static int hook_lib_line(machine_t *m)
{
    flush(m);
    line_flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    cpu_t *c = &m->cpu;
    const int16_t x0 = (int16_t)c->r[R_AX], y0 = (int16_t)c->r[R_BX], x1 = (int16_t)c->r[R_CX], y1 = (int16_t)c->r[R_DX];
    const uint16_t drv = slot_target_seg(m, 0x01B5);
    /* with the page and the origin it draws at (entries 12-16 and 24/26
     * move them: the HUD draws some lines straight to the display) */
    const int32_t v[7] = { x0, y0, x1, y1, drv, seg_read16(c, drv, 0x0194), seg_read16(c, drv, 0x0196) };
    o->prim(o->user, c->icount, 'N', v, 7);
    int16_t xa = x0 < x1 ? x0 : x1, xb = x0 < x1 ? x1 : x0, ya = y0 < y1 ? y0 : y1, yb = y0 < y1 ? y1 : y0;
    if (xa < 0) xa = 0;
    if (ya < 0) ya = 0;
    if (xb > 319) xb = 319;
    if (yb > 199) yb = 199;
    if (xa > xb || ya > yb) return 0;
    g_line.page = seg_read16(c, drv, 0x0194);
    const uint16_t origin = seg_read16(c, drv, 0x0196);
    g_line.x0 = (uint16_t)xa; g_line.y0 = (uint16_t)ya;
    g_line.w = (uint16_t)(xb - xa + 1); g_line.h = (uint16_t)(yb - ya + 1);
    for (unsigned y = 0; y < g_line.h; y++) {
        g_line.at[y] = (uint16_t)(seg_read16(c, drv, (uint16_t)(4 + 2 * (g_line.y0 + y))) + origin + g_line.x0);
        for (unsigned x = 0; x < g_line.w; x++)
            g_line.before[y * g_line.w + x] = mem_read8(c, phys(g_line.page, (uint16_t)(g_line.at[y] + x)));
    }
    g_line.on = 1;
    return 0;
}

/* Graphics entries 32 (1E42:01BA, colour in AH) and 33 (1E42:01BF, colour
 * on the stack): the line colour. */
static int hook_lib_colour_ah(machine_t *m)
{
    flush(m); line_flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    const int32_t v[1] = { m->cpu.r[R_AX] >> 8 };
    o->prim(o->user, m->cpu.icount, 'K', v, 1);
    return 0;
}

static int hook_lib_colour_stack(machine_t *m)
{
    flush(m); line_flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    cpu_t *c = &m->cpu;
    const int32_t v[1] = { seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_SP] + 4)) & 0xFF };
    o->prim(o->user, c->icount, 'K', v, 1);
    return 0;
}

/* Graphics entries 37 (1E42:01D3) and 40 (1E42:01E2, the same with mode 0),
 * the library's span fill: rows AX..CX of
 * the table at SS:BX (left words, then right words 1B8h bytes on), combine
 * mode DX (0 set, 1 OR, 2 AND, 3 darken), the colour from entry 32/33. Logged
 * with its rows, then the page and the origin ('Q'); the rows' page bytes are kept like a line's bounding
 * box, so the next event logs the pixels it changed ('n'). */
static int hook_lib_spans(machine_t *m)
{
    flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    cpu_t *c = &m->cpu;
    const int16_t ya = (int16_t)c->r[R_AX], yb = (int16_t)c->r[R_CX];
    const uint16_t bx = c->r[R_BX], ss = c->seg[S_SS];
    int32_t v[4 + 2 * 220];
    int n = 0;
    v[n++] = ya; v[n++] = yb; v[n++] = c->ip == 0x01E2 ? 0 : c->r[R_DX];   /* entry 40 is mode 0 */
    const int rows_at = n++;
    int rows = 0;
    if (ya >= 0 && yb >= ya && yb < 220)
        for (int y = ya; y <= yb; y++) {
            v[n++] = seg_read16(c, ss, (uint16_t)(bx + 2 * y));
            v[n++] = seg_read16(c, ss, (uint16_t)(bx + 0x1B8 + 2 * y));
            rows++;
        }
    v[rows_at] = rows;
    const uint16_t drv = slot_target_seg(m, 0x01D3);
    v[n++] = seg_read16(c, drv, 0x0194);        /* then the page and the origin it fills at */
    v[n++] = seg_read16(c, drv, 0x0196);
    o->prim(o->user, c->icount, 'Q', v, n);
    if (!rows) return 0;
    g_line.page = seg_read16(c, drv, 0x0194);
    const uint16_t origin = seg_read16(c, drv, 0x0196);
    int16_t y0 = ya < 0 ? 0 : ya, y1 = yb > 199 ? 199 : yb;
    if (y0 > y1) return 0;
    g_line.x0 = 0; g_line.y0 = (uint16_t)y0; g_line.w = 320; g_line.h = (uint16_t)(y1 - y0 + 1);
    for (unsigned y = 0; y < g_line.h; y++) {
        g_line.at[y] = (uint16_t)(seg_read16(c, drv, (uint16_t)(4 + 2 * (g_line.y0 + y))) + origin);
        for (unsigned x = 0; x < 320; x++)
            g_line.before[y * 320 + x] = mem_read8(c, phys(g_line.page, (uint16_t)(g_line.at[y] + x)));
    }
    g_line.on = 1;
    return 0;
}

/* Graphics entry 42 (1E42:01EC), the blit: a far call with eight words,
 * (src_page, sx, sy, dst_page, dx, dy, w, h), each page an index into the
 * driver's page table (cs:[0787 + 2 page]); a row is cs:[0004 + 2y] with no
 * origin added. Logged ('C') with the two segments, and - for the frame
 * accounting - the source bytes and the destination page's changes like
 * any other entry. */
static void any_begin(machine_t *m, int entry, uint16_t page);
static void any_begin_display(machine_t *m, int entry, uint16_t page);
static int hook_lib_blit(machine_t *m)
{
    flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    cpu_t *c = &m->cpu;
    const uint16_t drv = slot_target_seg(m, 0x01EC), ss = c->seg[S_SS], sp = c->r[R_SP];
    static int32_t v[10 + 64000];
    int n = 10;
    for (int k = 0; k < 8; k++) v[k] = (int16_t)seg_read16(c, ss, (uint16_t)(sp + 4 + 2 * k));
    v[8] = seg_read16(c, drv, (uint16_t)(0x0787 + 2 * (v[0] & 0xFF)));
    v[9] = seg_read16(c, drv, (uint16_t)(0x0787 + 2 * (v[3] & 0xFF)));
    /* with the frame accounting on, the source rectangle's bytes follow, so
     * a copy can be checked without a dump of every page */
    if (getenv("F117R_OBSERVE_PAGES") && v[6] > 0 && v[7] > 0 && v[6] * v[7] <= 64000)
        for (int y = 0; y < v[7]; y++) {
            const uint16_t row = (uint16_t)(seg_read16(c, drv, (uint16_t)(4 + 2 * (v[2] + y))) + v[1]);
            for (int x = 0; x < v[6]; x++) v[n++] = mem_read8(c, phys((uint16_t)v[8], (uint16_t)(row + x)));
        }
    o->prim(o->user, c->icount, 'C', v, n);
    any_begin(m, 42, (uint16_t)v[9]);
    return 0;
}

/* Graphics entries 6, 5, 4, 3, 2 and 1 (1E42:0138, 0133, 012E, 0129, 0124, 011F): text. A
 * parameter block in SS - page, mode (1 opaque, else transparent),
 * foreground, background, x, y, font, the row clip (+0Eh, +10h) and the width
 * clip (+14h) - and a NUL-terminated string in SS; entry 5 is handed both
 * as arguments, the others take the block at BP and the string at BX. 4
 * draws, 3 first cuts the string at the width clip, 1 first clips the rows.
 * Logged ('T') with the string and the font it uses, read from the driver's
 * data segment (the immediate of its MOV AX at cs:03EB): the entry, the
 * page's segment, the block's eleven words, the string's length and bytes, then the font's
 * first and last character, shift, fixed width, height, spacing and extra
 * height, its width table, and its glyph rows. */
static int hook_lib_text(machine_t *m)
{
    flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    cpu_t *c = &m->cpu;
    const int entry = (c->ip - 0x011A) / 5;
    const uint16_t ss = c->seg[S_SS], sp = c->r[R_SP];
    const uint16_t blk = entry == 5 ? seg_read16(c, ss, (uint16_t)(sp + 4)) : c->r[R_BP];
    const uint16_t str = entry == 5 ? seg_read16(c, ss, (uint16_t)(sp + 6)) : c->r[R_BX];
    const uint16_t drv = slot_target_seg(m, c->ip);
    static int32_t v[64 + 256 + 256 + 32768];
    int n = 0;
    v[n++] = entry;
    v[n++] = seg_read16(c, drv, (uint16_t)(0x0787 + 2 * (seg_read16(c, ss, blk) & 0xFF)));   /* the page's segment */
    for (int k = 0; k < 11; k++) v[n++] = seg_read16(c, ss, (uint16_t)(blk + 2 * k));
    const int len_at = n++;
    int len = 0;
    while (len < 255) {
        const uint8_t ch = mem_read8(c, phys(ss, (uint16_t)(str + len)));
        if (!ch) break;
        v[n++] = ch; len++;
    }
    v[len_at] = len;
    /* the font: the driver's data segment, its table at 00D0 */
    uint16_t dds = 0;
    if (mem_read8(c, phys(drv, 0x03EB)) == 0xB8) dds = seg_read16(c, drv, 0x03EC);
    const uint16_t font = (uint16_t)v[2 + 6];
    int have = dds && font <= seg_read16(c, dds, 0x00D0);
    uint16_t di = have ? seg_read16(c, dds, (uint16_t)(0x00D0 + 2 * (font + 1))) : 0;
    if (!di) have = 0;
    v[n++] = have;
    if (have) {
        const uint8_t first = mem_read8(c, phys(dds, (uint16_t)(di - 8))), last = mem_read8(c, phys(dds, (uint16_t)(di - 7)));
        const uint8_t shift = mem_read8(c, phys(dds, (uint16_t)(di - 6))), fixed = mem_read8(c, phys(dds, (uint16_t)(di - 5)));
        const uint8_t height = mem_read8(c, phys(dds, (uint16_t)(di - 4))), spacing = mem_read8(c, phys(dds, (uint16_t)(di - 3)));
        const uint8_t extra = mem_read8(c, phys(dds, (uint16_t)(di - 2)));
        v[n++] = first; v[n++] = last; v[n++] = shift; v[n++] = fixed; v[n++] = height; v[n++] = spacing; v[n++] = extra;
        const int count = last >= first ? last - first + 1 : 0;
        for (int k = 0; k < count; k++) v[n++] = mem_read8(c, phys(dds, (uint16_t)(di - 9 - (count - 1) + k)));
        const unsigned sh = shift ? shift - 1u : 0u;
        const long bytes = ((long)count << sh) * (height + extra) + 2;
        v[n++] = (int32_t)bytes;
        for (long k = 0; k < bytes && n < (int)(sizeof v / sizeof v[0]); k++)
            v[n++] = mem_read8(c, phys(dds, (uint16_t)(di + k)));
    }
    o->prim(o->user, c->icount, 'T', v, n);
    any_begin(m, entry, seg_read16(c, drv, 0x0194));
    return 0;
}

/* Graphics entries 73, 18, 71 and 19 (1E42:0287, 0174, 027D, 0179): the
 * sprite. A block in SS - the source segment, sx, sy, the destination page,
 * dx, dy, w, h, and for 71/19 the clip (y low and high at +10h, +12h, x
 * low and high at +14h, +16h) - copied with colour 0 transparent (driver
 * 0EE6); 71 and 19 clip first (0E41). 73 and 71 take the block as an
 * argument, 18 and 19 at BP. Logged ('S'): the entry, the destination's
 * segment, the block's twelve words, then the source rectangle's bytes
 * before any clip, each row read through the driver's row table. */
static int hook_lib_sprite(machine_t *m)
{
    flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    cpu_t *c = &m->cpu;
    const int entry = (c->ip - 0x011A) / 5;
    const uint16_t ss = c->seg[S_SS];
    const uint16_t blk = entry == 73 || entry == 71 ? seg_read16(c, ss, (uint16_t)(c->r[R_SP] + 4)) : c->r[R_BP];
    const uint16_t drv = slot_target_seg(m, c->ip);
    static int32_t v[16 + 64000];
    int n = 0;
    v[n++] = entry;
    v[n++] = seg_read16(c, drv, (uint16_t)(0x0787 + 2 * (seg_read16(c, ss, (uint16_t)(blk + 6)) & 0xFF)));
    for (int k = 0; k < 12; k++) v[n++] = seg_read16(c, ss, (uint16_t)(blk + 2 * k));
    const uint16_t src = (uint16_t)v[2], sx = (uint16_t)v[3], sy = (uint16_t)v[4];
    const int w = (int16_t)v[8], h = (int16_t)v[9];
    if (w > 0 && h > 0 && w * h <= 64000 && sy + h <= 256)
        for (int y = 0; y < h; y++) {
            const uint16_t row = (uint16_t)(seg_read16(c, drv, (uint16_t)(4 + 2 * (sy + y))) + sx);
            for (int x = 0; x < w; x++) v[n++] = mem_read8(c, phys(src, (uint16_t)(row + x)));
        }
    o->prim(o->user, c->icount, 'S', v, n);
    any_begin(m, entry, seg_read16(c, drv, 0x0194));
    return 0;
}

/* Graphics entry 22 (1E42:0188, driver 0B70): the scaled, flippable RLE
 * sprite. Arguments after the return address: the block (SS offset; word 0
 * the page, +0Eh..+14h the clip's y range and x range), x, y, a signed width
 * and a signed height (the sign flips the axis), the source's offset and
 * segment. The source is a word width, a word height, then per row a data
 * length A, a leading transparent count B and A bytes. Logged ('W'): the
 * entry, the page's segment, the block's eleven words, x, y, width, height,
 * the source segment and offset, its width and height, the length of the rows
 * that follow and those bytes (each row as stored, its two words included). */
static int hook_lib_scaled(machine_t *m)
{
    flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    cpu_t *c = &m->cpu;
    const uint16_t ss = c->seg[S_SS], sp = c->r[R_SP];
    const uint16_t blk = seg_read16(c, ss, (uint16_t)(sp + 4));
    const uint16_t drv = slot_target_seg(m, c->ip);
    const uint16_t soff = seg_read16(c, ss, (uint16_t)(sp + 0x0E)), sseg = seg_read16(c, ss, (uint16_t)(sp + 0x10));
    static int32_t v[32 + 64000];
    int n = 0;
    v[n++] = 22;
    v[n++] = seg_read16(c, drv, (uint16_t)(0x0787 + 2 * (seg_read16(c, ss, blk) & 0xFF)));
    for (int k = 0; k < 11; k++) v[n++] = seg_read16(c, ss, (uint16_t)(blk + 2 * k));
    v[n++] = (int16_t)seg_read16(c, ss, (uint16_t)(sp + 6));
    v[n++] = (int16_t)seg_read16(c, ss, (uint16_t)(sp + 8));
    v[n++] = (int16_t)seg_read16(c, ss, (uint16_t)(sp + 0x0A));
    v[n++] = (int16_t)seg_read16(c, ss, (uint16_t)(sp + 0x0C));
    v[n++] = sseg;
    v[n++] = soff;
    const uint16_t sw = sseg ? seg_read16(c, sseg, soff) : 0, sh = sseg ? seg_read16(c, sseg, (uint16_t)(soff + 2)) : 0;
    v[n++] = sw;
    v[n++] = sh;
    const int len_at = n++;
    int total = 0;
    if (sw && sh && sh <= 256) {
        uint16_t at = (uint16_t)(soff + 4);
        for (int r = 0; r < sh; r++) {
            const int len = seg_read16(c, sseg, at) + 4;
            if (total + len > 60000) { total = -1; break; }
            for (int k = 0; k < len; k++) v[n++] = mem_read8(c, phys(sseg, (uint16_t)(at + k)));
            total += len;
            at = (uint16_t)(at + len);
        }
    }
    v[len_at] = total;
    if (total < 0) { v[len_at] = 0; n = len_at + 1; }
    o->prim(o->user, c->icount, 'W', v, n);
    any_begin(m, 22, seg_read16(c, drv, 0x0194));
    return 0;
}

/* Graphics entry 11 (1E42:0151): a tick scale (driver 071A). From row BX - 1
 * (+20 when DL >= 1, +1 more when CL is set) upward in steps of two rows,
 * while the row is not below the table's low limit: a tick at the table's x,
 * drawn leftward when SI is 0 and rightward otherwise, 3 pixels every tenth
 * (2 when CL is set), 2 every fifth, else 1, skipped above the high limit.
 * The table (in the driver's data segment, at 1966h / 196Eh / 1976h, indexed
 * by SI + 4 when CL is set) and the colour [1866] are read here. Logged
 * ('H'): SI, BX, DL, CL, the colour, x, the low and the high limit, the page. */
static int hook_lib_ticks(machine_t *m)
{
    flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    cpu_t *c = &m->cpu;
    const uint16_t drv = slot_target_seg(m, c->ip);
    if (mem_read8(c, phys(drv, 0x071C)) != 0xB8) return 0;
    const uint16_t dds = seg_read16(c, drv, 0x071D);
    const uint16_t si0 = c->r[R_SI], cl = c->r[R_CX] & 0xFF;
    const uint16_t si = (uint16_t)(si0 + (cl ? 4 : 0));
    const int32_t v[9] = { si0, c->r[R_BX], (int8_t)(c->r[R_DX] & 0xFF), cl, mem_read8(c, phys(dds, 0x1866)),
                           seg_read16(c, dds, (uint16_t)(si + 0x1966)), seg_read16(c, dds, (uint16_t)(si + 0x196E)),
                           seg_read16(c, dds, (uint16_t)(si + 0x1976)), seg_read16(c, drv, 0x0194) };
    o->prim(o->user, c->icount, 'H', v, 9);
    any_begin(m, 11, seg_read16(c, drv, 0x0194));
    return 0;
}

/* Graphics entries 44, 48 and 79 (1E42:01F6, 020A, 02D6): whole-page copies.
 * 44 presents: unless the driver flips pages (cs:[11DC] set), it copies AX
 * words from offset 0 of page 1 (cs:[0789]) to page 0 (cs:[0787], the
 * display) - the 3-D window. 48 copies 32,000 words from the segment given
 * as its argument to the current page, and 79 does the same as a dissolve
 * (every offset below FA00h, in a shift-register order). Logged ('D'): the
 * entry, the byte count, the source and destination segments, the flip flag,
 * and with F117R_OBSERVE_PAGES for 48/79 the source bytes. */
static int hook_lib_copy(machine_t *m)
{
    flush(m);
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    cpu_t *c = &m->cpu;
    const int entry = (c->ip - 0x011A) / 5;
    const uint16_t drv = slot_target_seg(m, c->ip);
    static int32_t v[8 + 64000];
    int n = 0;
    v[n++] = entry;
    if (entry == 44) {
        const uint16_t flip = seg_read16(c, drv, 0x11DC);
        v[n++] = flip ? 0 : 2 * c->r[R_AX];
        v[n++] = seg_read16(c, drv, 0x0789);
        v[n++] = seg_read16(c, drv, 0x0787);
        v[n++] = flip;
        /* A work-page dump at game_draw can miss a transient change that
         * entry 44 presents and the next phase then overwrites. Keep the
         * source as it exists at the instant of the present, just like the
         * full-page copies below. */
        if (getenv("F117R_OBSERVE_PAGES") && !flip) {
            const uint16_t src = (uint16_t)v[2];
            const uint32_t count = (uint32_t)v[1];
            for (uint32_t a = 0; a < count && n < (int)(sizeof v / sizeof v[0]); a++)
                v[n++] = mem_read8(c, phys(src, (uint16_t)a));
        }
    } else {
        const uint16_t src = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_SP] + 4));
        v[n++] = 64000;
        v[n++] = src;
        v[n++] = seg_read16(c, drv, 0x0194);
        v[n++] = 0;
        if (getenv("F117R_OBSERVE_PAGES"))
            for (uint32_t a = 0; a < 64000; a++) v[n++] = mem_read8(c, phys(src, (uint16_t)a));
    }
    o->prim(o->user, c->icount, 'D', v, n);
    /* Entry 44 is a present, so the original copy runs after this hook.
     * Snapshot the destination now; the next observer entry can record what
     * the present actually changed there, including any row/window rules the
     * source-side log cannot describe. */
    if (entry == 44 && v[4] == 0)
        any_begin(m, entry, (uint16_t)v[3]);
    return 0;
}

/* Every other graphics entry: logged ('X', the entry number) so what draws
 * between two captured primitives is known, and so a pending capture is
 * closed before an uncaptured primitive draws over its pixels (a library
 * capture only: VGAME's fill calls entry 46 while it paints). */
static int hook_lib_other(machine_t *m)
{
    line_flush(m);                         /* not the fill's rows: the fill itself calls entries mid-paint */
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    cpu_t *c = &m->cpu;
    /* the entry, then the first two argument words (far call: SS:SP+4, +6) -
     * entry 26 sets the origin from the first, so a replay can follow it */
    const int32_t v[3] = { (int32_t)((c->ip - 0x011A) / 5),
                           seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_SP] + 4)),
                           seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_SP] + 6)) };
    o->prim(o->user, c->icount, 'X', v, 3);
    /* Keep both pages through every entry. Some setters (24/26/46/62/65)
     * draw nothing themselves, but can be called inside a drawing entry;
     * the outer entry may resume writing after this hook flushes it. */
    const uint16_t page = seg_read16(c, slot_target_seg(m, c->ip), 0x0194);
    any_begin(m, v[0], page);
    any_begin_display(m, v[0], page);
    return 0;
}

static void any_begin(machine_t *m, int entry, uint16_t page)
{
    if (!getenv("F117R_OBSERVE_PAGES")) return;
    cpu_t *c = &m->cpu;
    g_any.entry = entry;
    g_any.page = page;
    for (uint32_t a = 0; a < 65536; a++) g_any.before[a] = mem_read8(c, phys(page, (uint16_t)a));
    g_any.on = 1;
}

/* Unknown entries can target VGA memory directly even when the driver's
 * active page points elsewhere. */
static void any_begin_display(machine_t *m, int entry, uint16_t page)
{
    if (!getenv("F117R_OBSERVE_PAGES") || page == 0xA000) return;
    cpu_t *c = &m->cpu;
    g_any_display.entry = entry;
    for (uint32_t a = 0; a < 65536; a++)
        g_any_display.before[a] = mem_read8(c, phys(0xA000, (uint16_t)a));
    g_any_display.on = 1;
}

static const recomp_override OBSERVERS[] = {
    { "observe", "VGAME.EXE", VGAME_47304, 0x0000, 0x1450, hook_game_draw, "per-frame draw routine (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x130D, 0x004A, hook_poly_edge, "filled polygon edge (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x130D, 0x0116, hook_poly_fill, "filled polygon fill (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1377, 0x004C, hook_outline_begin, "fill or outline begin (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x01D3, hook_lib_spans, "library span fill (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x01EC, hook_lib_blit, "library blit (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x01E2, hook_lib_spans, "library span fill, mode 0 (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x01F6, hook_lib_copy, "library present (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x020A, hook_lib_copy, "library page copy (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x02D6, hook_lib_copy, "library page dissolve (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x0151, hook_lib_ticks, "library tick scale (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x0287, hook_lib_sprite, "library sprite, block argument (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x0188, hook_lib_scaled, "library scaled sprite (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x0174, hook_lib_sprite, "library sprite (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x027D, hook_lib_sprite, "library clipped sprite, block argument (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x0179, hook_lib_sprite, "library clipped sprite (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x011F, hook_lib_text, "library text, rows clipped (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x0124, hook_lib_text, "library text, left clipped (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x0138, hook_lib_text, "library text, clipped on all sides (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x0129, hook_lib_text, "library text, width clipped (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x012E, hook_lib_text, "library text (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x0133, hook_lib_text, "library text, block argument (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x01B5, hook_lib_line, "library line (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x01BA, hook_lib_colour_ah, "library line colour (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1E42, 0x01BF, hook_lib_colour_stack, "library line colour (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1377, 0x005E, hook_fill_rows, "fill paints its rows (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1377, 0x0055, hook_outline_edge, "outline polygon edge (observer)", 1 },
};

void observe_register(void)
{
    static int done;
    if (done) return;
    done = 1;
    for (unsigned i = 0; i < sizeof OBSERVERS / sizeof OBSERVERS[0]; i++) recomp_override_add(&OBSERVERS[i]);
    /* the graphics entries not hooked above: 0..83 at 1E42:011A + 5n */
    static recomp_override others[84];
    unsigned n = 0;
    for (unsigned e = 0; e < 84; e++) {
        const uint16_t ip = (uint16_t)(0x011A + 5 * e);
        int taken = 0;
        for (unsigned i = 0; i < sizeof OBSERVERS / sizeof OBSERVERS[0]; i++)
            if (OBSERVERS[i].seg == 0x1E42 && OBSERVERS[i].ip == ip) taken = 1;
        if (taken) continue;
        others[n] = (recomp_override){ "observe", "VGAME.EXE", VGAME_47304, 0x1E42, ip, hook_lib_other,
                                       "graphics entry (observer)", 1 };
        recomp_override_add(&others[n]);
        n++;
    }
}
