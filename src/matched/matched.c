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

/* The word argument n (0 = the first) of a near routine whose BP frame is
 * not yet built: SS:[SP + 2 + 2n]. */
static uint16_t arg(cpu_t *c, int n)
{
    return seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_SP] + 2 + 2 * n));
}

/* VGAME 0x0C863, sign16(v): -1, 0 or 1. Flags: the zero test's sub when v
 * is 0, otherwise the second compare with zero. */
static int vgame_sign16(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 9)) return 0;
    const uint16_t v = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    alu_sub(c, v, 0, 1, 0);                                       /* cmp [bp+4], 0 */
    unsigned n;
    if (!(c->flags & F_ZF)) {                                     /* jne */
        alu_sub(c, v, 0, 1, 0);
        c->r[R_AX] = x86_cond(c, 0xE) ? 0xFFFF : 1;               /* jle */
        n = 9;
    } else {
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);   /* sub ax, ax */
        n = 7;
    }
    x86_leave(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 0x0C699, clamp(v, hi, lo): lo when lo < v, else hi when hi > v,
 * else v (word compares, signed). Flags from the last compare made. */
static int vgame_clamp(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 10)) return 0;
    const uint16_t v = arg(c, 0), hi = arg(c, 1), lo = arg(c, 2);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = v;
    alu_sub(c, lo, v, 1, 0);                                      /* cmp [bp+8], ax */
    unsigned n;
    if (!x86_cond(c, 0xD)) {                                      /* jge not taken */
        c->r[R_AX] = lo;
        n = 8;
    } else {
        alu_sub(c, hi, v, 1, 0);                                  /* cmp [bp+6], ax */
        if (!x86_cond(c, 0xE)) { c->r[R_AX] = hi; n = 10; }       /* jle not taken */
        else n = 9;
    }
    x86_leave(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 0x0BA2B, class5_takes_lock(object): the object's class byte (its
 * 16-byte record at B2D0, byte 0x0C, low seven bits) selects a type byte at
 * C630; types 0x0C and 0x0D answer 1, the rest 0. BX is left as the class,
 * AL's type byte read leaves AH as it was before the mask. */
static int vgame_class5_takes_lock(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 14)) return 0;
    x86_enter(c, 2, 0);                                           /* enter 2, 0 */
    const uint16_t object = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 4));
    uint16_t bx = x86_shift(c, 4, object, 4, 1);                  /* shl bx, 4 */
    bx = (uint16_t)((bx & 0xFF00) | mem_read8(c, phys(c->seg[S_DS], (uint16_t)(bx - 0x4D24))));
    bx = (uint16_t)alu_logic(c, bx & 0x7F, 1);                    /* and bx, 7Fh */
    c->r[R_BX] = bx;
    uint16_t ax = (uint16_t)((c->r[R_AX] & 0xFF00) | mem_read8(c, phys(c->seg[S_DS], (uint16_t)(bx - 0x39D0))));
    ax = (uint16_t)alu_logic(c, ax & 0x0F, 1);                    /* and ax, 0Fh */
    unsigned n;
    alu_sub(c, ax, 0x0C, 1, 0);                                   /* cmp ax, 0Ch */
    if (c->flags & F_ZF) { ax = 1; n = 12; }
    else {
        alu_sub(c, ax, 0x0D, 1, 0);                               /* cmp ax, 0Dh */
        if (c->flags & F_ZF) { ax = 1; n = 14; }
        else { ax = (uint16_t)alu_sub(c, ax, ax, 1, 0); n = 14; } /* sub ax, ax */
    }
    c->r[R_AX] = ax;
    x86_leave(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 0x0EE0C, abs16(v): AX = |v| by CWD, XOR, SUB; DX the sign. */
static int vgame_abs16(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    const uint16_t v = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);                                    /* push bp (its word stays below SP) */
    c->r[R_BP] = cpu_pop16(c);                                    /* ... pop bp */
    const uint16_t sign = (v & 0x8000) ? 0xFFFF : 0;              /* cwd */
    c->r[R_DX] = sign;
    c->r[R_AX] = (uint16_t)alu_sub(c, (uint16_t)alu_logic(c, v ^ sign, 1), sign, 1, 0);
    c->icount += 8;
    near_ret(c);
    return 1;
}

/* VGAME 0x0EF68 / 0x0EF74 / 0x0F018: the 32-bit shifts of DX:AX by CL -
 * left, arithmetic right and logical right - one bit a pass through a LOOP,
 * exactly as the original steps (flags from the last pass; CX ends 0). */
static int shift32(machine_t *m, int hi_op, int lo_op, int left)
{
    cpu_t *c = &m->cpu;
    const unsigned n = c->r[R_CX] & 0xFF;
    if (!room(c, 3 + 3 * n)) return 0;
    set_r8(c, 5, (uint8_t)alu_logic(c, 0, 0));                   /* xor ch, ch (CH is r8 index 5) */
    c->r[R_CX] = (uint16_t)n;
    for (unsigned i = 0; i < n; i++) {
        if (left) {
            c->r[R_AX] = x86_shift(c, lo_op, c->r[R_AX], 1, 1);
            c->r[R_DX] = x86_shift(c, hi_op, c->r[R_DX], 1, 1);
        } else {
            c->r[R_DX] = x86_shift(c, hi_op, c->r[R_DX], 1, 1);
            c->r[R_AX] = x86_shift(c, lo_op, c->r[R_AX], 1, 1);
        }
    }
    c->r[R_CX] = 0;
    c->icount += 3 + 3 * n;
    near_ret(c);
    return 1;
}
static int vgame_shl32(machine_t *m) { return shift32(m, 2, 4, 1); }   /* shl ax / rcl dx */
static int vgame_sar32(machine_t *m) { return shift32(m, 7, 3, 0); }   /* sar dx / rcr ax */
static int vgame_shr32(machine_t *m) { return shift32(m, 5, 3, 0); }   /* shr dx / rcr ax */

/* VGAME 0x0C67A, clamp3(v, hi, lo): lo when lo < v; else hi when hi > v and
 * v is above -16384 (C000h); a v at or below that takes lo instead. */
static int vgame_clamp3(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 12)) return 0;
    const uint16_t v = arg(c, 0), hi = arg(c, 1), lo = arg(c, 2);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = v;
    alu_sub(c, lo, v, 1, 0);                                      /* cmp [bp+8], ax */
    unsigned n;
    if (!x86_cond(c, 0xD)) { c->r[R_AX] = lo; n = 8; }            /* jge not taken */
    else {
        alu_sub(c, hi, v, 1, 0);                                  /* cmp [bp+6], ax */
        if (x86_cond(c, 0xE)) n = 9;                              /* jle: keep v */
        else {
            alu_sub(c, v, 0xC000, 1, 0);                          /* cmp ax, C000h */
            c->r[R_AX] = x86_cond(c, 0xE) ? lo : hi;
            n = 12;
        }
    }
    x86_leave(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 0x1056A, sine(BX = angle): the word table at DS:2084 is indexed by
 * the angle's high byte, and the low byte interpolates toward the next
 * entry: BX = t[i] + (t[i+1] - t[i]) * frac / 256, rounded by the bit
 * shifted out of AL. Result in BX; AX and DX as the original leaves them. */
static int vgame_sine(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 14)) return 0;
    const uint16_t angle = c->r[R_BX];
    uint16_t dx = (uint16_t)((c->r[R_DX] & 0xFF00) | (angle & 0xFF));      /* mov dl, bl */
    dx = (uint16_t)((alu_sub(c, dx >> 8, dx >> 8, 0, 0) << 8) | (dx & 0xFF));   /* sub dh, dh */
    uint16_t bx = (uint16_t)(angle >> 8);                         /* mov bl, bh / mov bh, dh */
    bx = x86_shift(c, 4, bx, 1, 1);                               /* shl bx, 1 */
    const uint16_t ds = c->seg[S_DS];
    uint16_t ax = seg_read16(c, ds, (uint16_t)(bx + 0x2086));
    bx = seg_read16(c, ds, (uint16_t)(bx + 0x2084));
    ax = (uint16_t)alu_sub(c, ax, bx, 1, 0);
    c->r[R_AX] = ax;
    c->r[R_DX] = dx;
    x86_imul16(c, dx);                                            /* imul dx: DX:AX */
    ax = c->r[R_AX];
    dx = c->r[R_DX];
    dx = (uint16_t)(((dx & 0xFF) << 8) | (ax >> 8));              /* mov dh, dl / mov dl, ah */
    const uint8_t al = (uint8_t)x86_shift(c, 4, ax & 0xFF, 1, 0); /* shl al, 1 */
    c->r[R_AX] = (uint16_t)((ax & 0xFF00) | al);
    c->r[R_DX] = dx;
    c->r[R_BX] = (uint16_t)alu_add(c, bx, dx, 1, (c->flags & F_CF) ? 1u : 0u);   /* adc bx, dx */
    c->icount += 14;
    near_ret(c);
    return 1;
}

/* VGAME 0x0FFDC, outcode(BX = x, BP = y): the clipping outcode in AL, bit
 * 3 left (x < 0), 0 right (x > [2293]), 2 above (y < 0), 1 below
 * (y > [2295]), signed; flags from the final OR AL, AL. */
static int vgame_outcode(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 15)) return 0;
    const uint16_t x = c->r[R_BX], y = c->r[R_BP], ds = c->seg[S_DS];
    uint8_t al = 0x0F;
    unsigned n = 11;
    alu_logic(c, x, 1);                                           /* or bx, bx */
    if (!(c->flags & F_SF)) { al &= 0xF7; n++; }
    alu_sub(c, x, seg_read16(c, ds, 0x2293), 1, 0);
    if (!x86_cond(c, 0xF)) { al &= 0xFE; n++; }                   /* jg */
    alu_logic(c, y, 1);                                           /* or bp, bp */
    if (!(c->flags & F_SF)) { al &= 0xFB; n++; }
    alu_sub(c, y, seg_read16(c, ds, 0x2295), 1, 0);
    if (!x86_cond(c, 0xF)) { al &= 0xFD; n++; }
    alu_logic(c, al, 0);                                          /* or al, al */
    c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | al);
    c->icount += n;
    near_ret(c);
    return 1;
}

static const recomp_override MATCHED[] = {
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x4958, vgame_free_fall, "free fall", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xD50A, vgame_waypoint_from_target, "waypoint from target", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xE289, vgame_transpose_matrix, "transpose the orientation matrix", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xC863, vgame_sign16, "sign of a word", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xC699, vgame_clamp, "clamp a word", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xBA2B, vgame_class5_takes_lock, "object class takes a lock", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEE0C, vgame_abs16, "absolute value of a word", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEF68, vgame_shl32, "32-bit shift left", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEF74, vgame_sar32, "32-bit arithmetic shift right", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xF018, vgame_shr32, "32-bit logical shift right", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xC67A, vgame_clamp3, "clamp a bar value", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x104E, 0x008A, vgame_sine, "sine by table", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xFFDC, vgame_outcode, "clipping outcode", 1 },
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
