/* observe.c - see observe.h. */
#include "observe.h"
#include "matched.h"
#include "recomp_rt.h"
#include "cpu.h"

const f117_observer *g_f117_observer;

void observe_set(const f117_observer *o) { g_f117_observer = o; }

void observe_vertex(machine_t *m, uint16_t di, uint16_t bx)
{
    const f117_observer *o = g_f117_observer;
    if (!o || !o->vertex) return;
    cpu_t *c = &m->cpu;
    const uint16_t ds = c->seg[S_DS];
    int32_t xf[3], px[2] = { 0, 0 };
    for (int k = 0; k < 3; k++)
        xf[k] = (int32_t)((uint32_t)seg_read16(c, ds, (uint16_t)(di + 4 * k)) |
                          ((uint32_t)seg_read16(c, ds, (uint16_t)(di + 4 * k + 2)) << 16));
    const int16_t zhi = (int16_t)seg_read16(c, ds, (uint16_t)(di + 0x0A));
    const int range = zhi >= 0x100 ? 0 : zhi >= 1 ? 1 : 2;
    if (range != 2)
        for (int k = 0; k < 2; k++)
            px[k] = (int32_t)((uint32_t)seg_read16(c, ds, (uint16_t)(bx + 4 * k)) |
                              ((uint32_t)seg_read16(c, ds, (uint16_t)(bx + 4 * k + 2)) << 16));
    o->vertex(o->user, c->icount, xf, px, range);
}

/* VGAME 0x01450, game_draw: told to the observer and then left alone - the
 * hook returns 0, so the original instruction runs. */
static int hook_game_draw(machine_t *m)
{
    const f117_observer *o = g_f117_observer;
    if (o && o->frame_phase) o->frame_phase(o->user, m->cpu.icount);
    return 0;
}

static const recomp_override OBSERVERS[] = {
    { "observe", "VGAME.EXE", VGAME_47304, 0x0000, 0x1450, hook_game_draw, "per-frame draw routine (observer)", 1 },
};

void observe_register(void)
{
    static int done;
    if (done) return;
    done = 1;
    for (unsigned i = 0; i < sizeof OBSERVERS / sizeof OBSERVERS[0]; i++) recomp_override_add(&OBSERVERS[i]);
}
