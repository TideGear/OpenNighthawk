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
    const int32_t v[6] = { si, ds_dword(c, si), ds_dword(c, (uint16_t)(si + 4)), ds_dword(c, (uint16_t)(si + 8)),
                           ds_dword(c, (uint16_t)(si + 0x0C)), (int32_t)seg_read16(c, c->seg[S_DS], (uint16_t)(si + 2)) };
    o->prim(o->user, c->icount, 'E', v, 6);
    return 0;
}

/* 130D:0116, the filled polygon closed and filled: the colour word above
 * the far return address. */
static int hook_poly_fill(machine_t *m)
{
    const f117_observer *o = g_f117_observer;
    if (!o || !o->prim) return 0;
    cpu_t *c = &m->cpu;
    const int32_t v[1] = { seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_SP] + 4)) };
    o->prim(o->user, c->icount, 'F', v, 1);
    return 0;
}

/* 1377:004C, an outline polygon begun: AX the colour word. */
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
    { "observe", "VGAME.EXE", VGAME_47304, 0x1377, 0x004C, hook_outline_begin, "outline polygon begin (observer)", 1 },
    { "observe", "VGAME.EXE", VGAME_47304, 0x1377, 0x0055, hook_outline_edge, "outline polygon edge (observer)", 1 },
};

void observe_register(void)
{
    static int done;
    if (done) return;
    done = 1;
    for (unsigned i = 0; i < sizeof OBSERVERS / sizeof OBSERVERS[0]; i++) recomp_override_add(&OBSERVERS[i]);
}
