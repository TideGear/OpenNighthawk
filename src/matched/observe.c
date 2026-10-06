/* observe.c - see observe.h. */
#include "observe.h"
#include "matched.h"
#include "recomp_rt.h"
#include "cpu.h"

const f117_observer *g_f117_observer;

void observe_set(const f117_observer *o) { g_f117_observer = o; }

static int32_t ds_dword(cpu_t *c, uint16_t at)
{
    const uint16_t ds = c->seg[S_DS];
    return (int32_t)((uint32_t)seg_read16(c, ds, at) | ((uint32_t)seg_read16(c, ds, (uint16_t)(at + 2)) << 16));
}

void observe_vertex(machine_t *m, uint16_t di, uint16_t bx)
{
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
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return;
    const int32_t v[3] = { slot, (uint16_t)(di + 0xD6B4), (uint16_t)(bx + 0xD6B4) };
    o->prim(o->user, m->cpu.icount, 'G', v, 3);
}

/* The hooks below are told to the observer and then left alone - each
 * returns 0, so the original instruction runs. */

/* VGAME 0x01450, game_draw. */
static int hook_game_draw(machine_t *m)
{
    const f117_observer *o = g_f117_observer;
    if (o && o->frame_phase) o->frame_phase(o->user, m->cpu.icount);
    return 0;
}

/* 130D:004A, one edge of a filled polygon: SI the slot. */
static int hook_poly_edge(machine_t *m)
{
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
    o->prim(o->user, c->icount, 'R', v, n);
    return 0;
}

/* 1377:004C, a fill or an outline polygon begun (the fill entry calls it
 * too): AX the colour word. */
static int hook_outline_begin(machine_t *m)
{
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    const int32_t v[1] = { m->cpu.r[R_AX] };
    o->prim(o->user, m->cpu.icount, 'B', v, 1);
    return 0;
}

/* 1377:0055, an outline edge drawn as a line: SI the slot. */
static int hook_outline_edge(machine_t *m)
{
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    const int32_t v[1] = { m->cpu.r[R_SI] };
    o->prim(o->user, m->cpu.icount, 'L', v, 1);
    return 0;
}

static const recomp_override OBSERVERS[] = {
    { "observe", "VGAME.EXE", VGAME_47304, 0x0000, 0x1450, hook_game_draw, "per-frame draw routine (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x130D, 0x004A, hook_poly_edge, "filled polygon edge (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x130D, 0x0116, hook_poly_fill, "filled polygon fill (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1377, 0x004C, hook_outline_begin, "fill or outline begin (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1377, 0x005E, hook_fill_rows, "fill paints its rows (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1377, 0x0055, hook_outline_edge, "outline polygon edge (observer)", 1 },
};

void observe_register(void)
{
    static int done;
    if (done) return;
    done = 1;
    for (unsigned i = 0; i < sizeof OBSERVERS / sizeof OBSERVERS[0]; i++) recomp_override_add(&OBSERVERS[i]);
}
