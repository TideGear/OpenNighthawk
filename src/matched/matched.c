/* matched.c - Phase 2: hand-written equivalents of the original's routines.
 *
 * Each entry replaces the code at one routine's entry point with C that
 * does what the original does there, written to be read. "Matched" means
 * equal, not similar: every register, flag, memory byte and the instruction
 * clock come out as the original's, on every path, which is what lets the
 * game's chaotic flight and its shared random generators stay on course.
 * The semantics helpers are the interpreter's own (x86_sem.h), so the
 * arithmetic and its flags cannot drift from it.
 *
 * Matched entries are code overrides with `matched` set (recomp_rt.h):
 * always on, placed only for the recompiled engine. The interpreter keeps
 * running the original instructions, so every route and the
 * random-state check (tests/func_lockstep.c) compare the two.
 * F117R_NO_MATCHED=1 leaves them all out.
 *
 * An entry runs the whole routine at once, so it declines (returns 0, and
 * the original instructions run) when the routine would cross the run
 * loop's next look at events: an interrupt or I/O event due inside it then
 * lands exactly where it would have.
 *
 * Names and readings come from the Reimp's notes (tools/reimp_names.py
 * finds the leads); each routine here was read again in the original.
 */
#include "matched.h"
#include "recomp_rt.h"
#include "x86_sem.h"

#define VGAME_47304 0x8287450CCA85106FULL

/* Room for n instructions before the run loop must look at events. */
static int room(const cpu_t *c, unsigned n)
{
    return c->icount + n <= c->stop_at;
}

static void near_ret(cpu_t *c)
{
    c->ip = cpu_pop16(c);
}

static uint16_t ds_get(cpu_t *c, uint16_t off) { return seg_read16(c, c->seg[S_DS], off); }
static void ds_put(cpu_t *c, uint16_t off, uint16_t v) { seg_write16(c, c->seg[S_DS], off, v); }

/* VGAME 0x04958, free fall. While the height at [C62E] is above zero the
 * fall rate [B782] steepens by 12 a frame, down to no less than -16 before
 * the step, and the height moves by it. Flags: the last compare when the
 * height is not above zero, otherwise the add. */
static int vgame_free_fall(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    const uint16_t height = ds_get(c, 0xC62E);
    alu_sub(c, height, 0, 1, 0);                                  /* cmp [C62E], 0 */
    if (x86_cond(c, 0xE)) { c->icount += 3; near_ret(c); return 1; }   /* jle */
    uint16_t rate = ds_get(c, 0xB782);
    alu_sub(c, rate, 0xFFF0, 1, 0);                               /* cmp [B782], -16 */
    unsigned n = 7;
    if (!x86_cond(c, 0xE)) {                                      /* jle skips the sub */
        rate = (uint16_t)alu_sub(c, rate, 12, 1, 0);
        ds_put(c, 0xB782, rate);
        n = 8;
    }
    c->r[R_AX] = rate;                                            /* mov ax, [B782] */
    ds_put(c, 0xC62E, (uint16_t)alu_add(c, height, rate, 1, 0));  /* add [C62E], ax */
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 0x0D50A, waypoint_from_target(slot, target): the target's map
 * position (its 16-byte record at B2D0, words 0 and 2) becomes waypoint
 * slot's (4-byte entries at 2E9E). BX is left as target*16, SI restored,
 * AX the y word; flags from the slot's shift. */
static int vgame_waypoint_from_target(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 14)) return 0;
    const uint16_t ss = c->seg[S_SS], sp = c->r[R_SP];
    const uint16_t slot = seg_read16(c, ss, (uint16_t)(sp + 2));
    const uint16_t target = seg_read16(c, ss, (uint16_t)(sp + 4));
    cpu_push16(c, c->r[R_BP]);                                    /* push bp */
    c->r[R_BP] = c->r[R_SP];                                      /* mov bp, sp */
    cpu_push16(c, c->r[R_SI]);                                    /* push si */
    const uint16_t bx = x86_shift(c, 4, target, 4, 1);           /* shl bx, 4 */
    c->r[R_BX] = bx;
    const uint16_t x = ds_get(c, (uint16_t)(bx - 0x4D30));
    const uint16_t si = x86_shift(c, 4, slot, 2, 1);             /* shl si, 2 */
    ds_put(c, (uint16_t)(si + 0x2E9E), x);
    const uint16_t y = ds_get(c, (uint16_t)(bx - 0x4D2E));
    ds_put(c, (uint16_t)(si + 0x2EA0), y);
    c->r[R_AX] = y;
    c->r[R_SI] = cpu_pop16(c);                                    /* pop si */
    x86_leave(c);                                                 /* leave */
    c->icount += 14;
    near_ret(c);
    return 1;
}

/* VGAME 0x0E289, transpose the 3x3 orientation matrix at 49D2 in place:
 * swap (0,1)<->(1,0), (0,2)<->(2,0), (1,2)<->(2,1). No flag changes; AX is
 * left as the last element moved, SI as it was. */
static int vgame_transpose_matrix(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 15)) return 0;
    static const uint16_t pair[3][2] = { { 0x49D2, 0x49D6 }, { 0x49D4, 0x49DC }, { 0x49DA, 0x49DE } };
    cpu_push16(c, c->r[R_SI]);
    for (int i = 0; i < 3; i++) {
        const uint16_t a = ds_get(c, pair[i][0]), b = ds_get(c, pair[i][1]);
        ds_put(c, pair[i][0], b);
        ds_put(c, pair[i][1], a);
        c->r[R_AX] = b;
    }
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 15;
    near_ret(c);
    return 1;
}

static const recomp_override MATCHED[] = {
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x4958, vgame_free_fall, "free fall", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xD50A, vgame_waypoint_from_target, "waypoint from target", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xE289, vgame_transpose_matrix, "transpose the orientation matrix", 1 },
};

void matched_register(void)
{
    static int done;
    if (done) return;
    done = 1;
    for (unsigned i = 0; i < sizeof MATCHED / sizeof MATCHED[0]; i++) recomp_override_add(&MATCHED[i]);
}

unsigned matched_count(void) { return (unsigned)(sizeof MATCHED / sizeof MATCHED[0]); }

const recomp_override *matched_entry(unsigned i)
{
    return i < matched_count() ? &MATCHED[i] : 0;
}
