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

/* A REP-prefixed string instruction as the interpreter steps it: one clock
 * an iteration, one for a REP that finds CX already 0; REPNE/REPE stop on
 * ZF as well (cmp). Returns the clocks taken. */
enum { STR_MOVS, STR_SCAS };
static unsigned rep_string(cpu_t *c, int op, int w16, uint16_t src_seg, int repne)
{
    if (c->r[R_CX] == 0) return 1;
    unsigned n = 0;
    for (;;) {
        if (op == STR_MOVS) x86_movs(c, w16, src_seg);
        else x86_scas(c, w16);
        n++;
        c->r[R_CX] = (uint16_t)(c->r[R_CX] - 1);
        if (c->r[R_CX] == 0) break;
        if (op == STR_SCAS && ((c->flags & F_ZF) != 0) == (repne != 0)) break;   /* REPNE stops on ZF=1, REPE on ZF=0 */
    }
    return n;
}

/* VGAME 0x0EE1A, start the mission clock: [929E] = t, [92A0] = 0. */
static int vgame_set_mission_clock(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    const uint16_t t = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = t;
    ds_put(c, 0x929E, t);
    ds_put(c, 0x92A0, 0);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 7;
    near_ret(c);
    return 1;
}

/* VGAME 0x0D9E7, set the scene walk's origin: [49AC..49B0] = x, y, z. */
static int vgame_set_scene_origin(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 10)) return 0;
    const uint16_t x = arg(c, 0), y = arg(c, 1), z = arg(c, 2);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    ds_put(c, 0x49AC, x);
    ds_put(c, 0x49AE, y);
    ds_put(c, 0x49B0, z);
    c->r[R_AX] = z;
    x86_leave(c);
    c->icount += 10;
    near_ret(c);
    return 1;
}

/* VGAME 0x04E4B: [951C]:[951E] = [E574] + 7Ah : [E576] - a 32-bit value
 * offset in its low word only (no carry into the high word, as shipped).
 * Returns AX = 0, DX the high word; flags from SUB AX, AX. */
static int vgame_deadline_from_clock(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    const uint16_t lo = ds_get(c, 0xE574), hi = ds_get(c, 0xE576);
    const uint16_t sum = (uint16_t)alu_add(c, lo, 0x7A, 1, 0);
    ds_put(c, 0x951C, sum);
    ds_put(c, 0x951E, hi);
    c->r[R_DX] = hi;
    c->r[R_AX] = (uint16_t)alu_sub(c, sum, sum, 1, 0);
    c->icount += 7;
    near_ret(c);
    return 1;
}

/* VGAME 0x01BA7, read the far pointer at 0000:SI (an interrupt vector):
 * BX = offset, AX = segment. */
static int vgame_read_vector(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    cpu_push16(c, c->seg[S_DS]);                                  /* push ds */
    c->r[R_AX] = (uint16_t)alu_logic(c, 0, 1);                    /* xor ax, ax */
    c->r[R_BX] = seg_read16(c, 0, c->r[R_SI]);
    c->r[R_AX] = seg_read16(c, 0, (uint16_t)(c->r[R_SI] + 2));
    c->seg[S_DS] = cpu_pop16(c);                                  /* pop ds */
    c->icount += 7;
    near_ret(c);
    return 1;
}

/* VGAME 0x0EDC2, strupr(s): a-z become A-Z in place, up to the zero byte;
 * returns s. Declines a string with no zero byte in its segment. */
static int vgame_strupr(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t s = arg(c, 0), ds = c->seg[S_DS];
    unsigned n = 5 + 3 + 3;                                       /* frame + jmp; last test; xchg, pop, ret */
    uint32_t len = 0;
    for (; len < 0x10000; len++) {
        const uint8_t ch = mem_read8(c, phys(ds, (uint16_t)(s + len)));
        if (!ch) break;
        n += 3 + ((uint8_t)(ch - 0x61) < 0x1A ? 6 : 4);
    }
    if (len >= 0x10000 || !room(c, n)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    uint16_t ax = c->r[R_AX];
    for (uint32_t i = 0; i < len; i++) {
        const uint16_t at = (uint16_t)(s + i);
        uint8_t al = mem_read8(c, phys(ds, at));
        alu_logic(c, al, 0);                                      /* or al, al */
        al = (uint8_t)alu_sub(c, al, 0x61, 0, 0);                 /* sub al, 'a' */
        alu_sub(c, al, 0x1A, 0, 0);                               /* cmp al, 26 */
        if (!(c->flags & F_CF)) { ax = (uint16_t)((ax & 0xFF00) | al); continue; }
        al = (uint8_t)alu_add(c, al, 0x41, 0, 0);                 /* add al, 'A' */
        mem_write8(c, phys(ds, at), al);
        ax = (uint16_t)((ax & 0xFF00) | al);
    }
    alu_logic(c, 0, 0);                                           /* or al, al on the zero byte */
    c->r[R_BX] = (uint16_t)(s + len);
    c->r[R_DX] = (uint16_t)(ax & 0xFF00);                         /* xchg dx, ax */
    c->r[R_AX] = s;
    c->r[R_BP] = cpu_pop16(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* REPNE SCASB for AL = 0 from ES:DI with CX = FFFF, counted without
 * running it: the iterations it will take (DF honoured). */
static unsigned scan_zero_count(cpu_t *c, uint16_t di)
{
    const int delta = (c->flags & F_DF) ? -1 : 1;
    unsigned k = 0;
    while (k < 0xFFFF) {
        const uint8_t b = mem_read8(c, phys(c->seg[S_ES], di));
        k++;
        if (!b) break;
        di = (uint16_t)(di + delta);
    }
    return k;
}

/* VGAME 0x0EB82, strlen(s) by REPNE SCASB; DI kept, ES = DS. */
static int vgame_strlen(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t s = arg(c, 0), es = c->seg[S_ES];
    c->seg[S_ES] = c->seg[S_DS];
    const unsigned k = scan_zero_count(c, s);
    c->seg[S_ES] = es;
    if (!room(c, 14 + k)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    const uint16_t di = c->r[R_DI];
    c->seg[S_ES] = c->seg[S_DS];                                  /* mov ax, ds / mov es, ax */
    c->r[R_DI] = s;
    c->r[R_AX] = (uint16_t)alu_logic(c, 0, 1);                    /* xor ax, ax */
    c->r[R_CX] = 0xFFFF;
    rep_string(c, STR_SCAS, 0, 0, 1);                             /* repne scasb */
    uint16_t cx = (uint16_t)~c->r[R_CX];                          /* not cx */
    cx = (uint16_t)alu_dec(c, cx, 1);                             /* dec cx */
    c->r[R_CX] = c->r[R_AX];                                      /* xchg cx, ax */
    c->r[R_AX] = cx;
    c->r[R_DX] = di;
    c->r[R_DI] = di;
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 14 + k;
    near_ret(c);
    return 1;
}

/* Clocks the shared copy tail will take for CX = n bytes to DI = dst. */
static unsigned copy_tail_clocks(uint16_t dst, uint16_t n)
{
    unsigned t = 2;                                               /* test al, 1 / je */
    if (dst & 1) { t += 2; n = (uint16_t)(n - 1); }
    t += 1;                                                       /* shr cx, 1 */
    t += (n >> 1) ? (n >> 1) : 1;                                 /* rep movsw */
    t += 1;                                                       /* adc cx, cx */
    t += (n & 1) ? 1 : 1;                                         /* rep movsb: one byte, or CX=0 */
    return t;
}

/* The copy tail VGAME's string routines share, with AX = DI = dst and
 * CX bytes: a byte to word-align DI, REP MOVSW, then the odd byte through
 * ADC CX, CX and REP MOVSB. Returns the clocks taken. */
static unsigned copy_tail(cpu_t *c, uint16_t src_seg)
{
    unsigned n = 2;
    alu_logic(c, c->r[R_AX] & 1, 0);                              /* test al, 1 */
    if (!(c->flags & F_ZF)) {                                     /* je not taken */
        x86_movs(c, 0, src_seg);
        c->r[R_CX] = (uint16_t)alu_dec(c, c->r[R_CX], 1);
        n += 2;
    }
    c->r[R_CX] = x86_shift(c, 5, c->r[R_CX], 1, 1);               /* shr cx, 1 */
    n++;
    n += rep_string(c, STR_MOVS, 1, src_seg, 0);                  /* rep movsw */
    c->r[R_CX] = (uint16_t)alu_add(c, c->r[R_CX], c->r[R_CX], 1, (c->flags & F_CF) ? 1u : 0u);
    n++;                                                          /* adc cx, cx */
    n += rep_string(c, STR_MOVS, 0, src_seg, 0);                  /* rep movsb */
    return n;
}

/* VGAME 0x0EB50, strcpy(dst, src): ES = DS, the length (with its zero) by
 * REPNE SCASB, then the copy tail; SI and DI restored, AX = dst. Declines
 * when DF is set (the tail's alignment arithmetic assumes forward). */
static int vgame_strcpy(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (c->flags & F_DF) return 0;
    const uint16_t dst = arg(c, 0), src = arg(c, 1), es = c->seg[S_ES];
    c->seg[S_ES] = c->seg[S_DS];
    const unsigned k = scan_zero_count(c, src);
    c->seg[S_ES] = es;
    const unsigned total = 10 + k + 3 + copy_tail_clocks(dst, (uint16_t)k) + 4;
    if (!room(c, total)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    const uint16_t di = c->r[R_DI], si = c->r[R_SI];
    c->r[R_DX] = di;
    c->r[R_BX] = si;
    c->r[R_SI] = src;
    c->r[R_DI] = src;
    c->seg[S_ES] = c->seg[S_DS];
    c->r[R_AX] = (uint16_t)alu_logic(c, 0, 1);                    /* xor ax, ax */
    c->r[R_CX] = 0xFFFF;
    rep_string(c, STR_SCAS, 0, 0, 1);
    c->r[R_CX] = (uint16_t)~c->r[R_CX];                           /* not cx */
    c->r[R_DI] = dst;
    c->r[R_AX] = dst;
    copy_tail(c, c->seg[S_DS]);
    c->r[R_SI] = si;
    c->r[R_DI] = di;
    c->r[R_BP] = cpu_pop16(c);
    c->icount += total;
    near_ret(c);
    return 1;
}

/* VGAME 0x0EDE0, memcpy(dst, src, n) within DS (ES = DS), the copy tail
 * when n is non-zero; SI and DI restored, AX = dst. Forward only. */
static int vgame_memcpy(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (c->flags & F_DF) return 0;
    const uint16_t dst = arg(c, 0), src = arg(c, 1), n = arg(c, 2);
    const unsigned total = 11 + (n ? copy_tail_clocks(dst, n) : 0) + 4;
    if (!room(c, total)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    const uint16_t di = c->r[R_DI], si = c->r[R_SI];
    c->r[R_DX] = di;
    c->r[R_BX] = si;
    c->seg[S_ES] = c->seg[S_DS];
    c->r[R_SI] = src;
    c->r[R_DI] = dst;
    c->r[R_AX] = dst;
    c->r[R_CX] = n;
    if (n) copy_tail(c, c->seg[S_DS]);                            /* jcxz skips it */
    c->r[R_SI] = si;
    c->r[R_DI] = di;
    c->r[R_BP] = cpu_pop16(c);
    c->icount += total;
    near_ret(c);
    return 1;
}

/* VGAME 0x0EDA4, farcopy(src_seg, src, dst_seg, dst, n): REP MOVSB between
 * segments; DS, SI, DI restored, ES left as dst_seg, CX 0. */
static int vgame_farcopy(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t sseg = arg(c, 0), src = arg(c, 1), dseg = arg(c, 2), dst = arg(c, 3), n = arg(c, 4);
    const unsigned total = 16 + (n ? n : 1);
    if (!room(c, total)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->seg[S_DS]);
    c->seg[S_DS] = sseg;
    c->r[R_SI] = src;
    c->seg[S_ES] = dseg;
    c->r[R_DI] = dst;
    c->r[R_CX] = n;
    rep_string(c, STR_MOVS, 0, c->seg[S_DS], 0);
    c->seg[S_DS] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
    c->icount += total;
    near_ret(c);
    return 1;
}

/* VGAME 0x085F1, the moving map's screen x of world x: (x - [40B2]) >>
 * (10 - zoom [40AE]) + 112, arithmetic shift. CL is left as the shift. */
static int vgame_map_screen_x(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 10)) return 0;
    const uint16_t x = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    uint16_t ax = (uint16_t)alu_sub(c, x, ds_get(c, 0x40B2), 1, 0);
    const uint8_t cl = (uint8_t)alu_sub(c, 10, mem_read8(c, phys(c->seg[S_DS], 0x40AE)), 0, 0);
    set_r8(c, R_CL, cl);
    ax = x86_shift(c, 7, ax, cl, 1);                              /* sar ax, cl */
    c->r[R_AX] = (uint16_t)alu_add(c, ax, 0x70, 1, 0);
    x86_leave(c);
    c->icount += 10;
    near_ret(c);
    return 1;
}

/* VGAME 0x08608, the moving map's screen y: ((y - [40B4]) >> (10 - zoom))
 * * 3 >> 2 + 139 - the map's 4:3 aspect. */
static int vgame_map_screen_y(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 12)) return 0;
    const uint16_t y = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    const uint8_t cl = (uint8_t)alu_sub(c, 10, mem_read8(c, phys(c->seg[S_DS], 0x40AE)), 0, 0);
    set_r8(c, R_CL, cl);
    uint16_t ax = (uint16_t)alu_sub(c, y, ds_get(c, 0x40B4), 1, 0);
    ax = x86_shift(c, 7, ax, cl, 1);                              /* sar ax, cl */
    ax = x86_imul3(c, ax, 3);                                     /* imul ax, ax, 3 */
    ax = x86_shift(c, 7, ax, 2, 1);                               /* sar ax, 2 */
    c->r[R_AX] = (uint16_t)alu_add(c, ax, 0x8B, 1, 0);
    x86_leave(c);
    c->icount += 12;
    near_ret(c);
    return 1;
}

/* VGAME 0x0886A, pen(colour): both text contexts ([4010], [4028]) take the
 * colour at their offset 4. */
static int vgame_pen(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 9)) return 0;
    const uint16_t colour = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = colour;
    uint16_t bx = ds_get(c, 0x4010);
    ds_put(c, (uint16_t)(bx + 4), colour);
    bx = ds_get(c, 0x4028);
    ds_put(c, (uint16_t)(bx + 4), colour);
    c->r[R_BX] = bx;
    x86_leave(c);
    c->icount += 9;
    near_ret(c);
    return 1;
}

/* VGAME 0x08A67, effectiveness(weapon, object): the object's type (as in
 * class5_takes_lock) indexes the weapon's 16-byte row at 3898; the signed
 * byte comes back in AX. */
static int vgame_weapon_effectiveness(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 17)) return 0;
    const uint16_t weapon = arg(c, 0), object = arg(c, 1), ds = c->seg[S_DS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    uint16_t bx = x86_shift(c, 4, object, 4, 1);
    bx = (uint16_t)((bx & 0xFF00) | mem_read8(c, phys(ds, (uint16_t)(bx - 0x4D24))));
    bx = (uint16_t)alu_logic(c, bx & 0x7F, 1);
    uint16_t ax = (uint16_t)((c->r[R_AX] & 0xFF00) | mem_read8(c, phys(ds, (uint16_t)(bx - 0x39D0))));
    const uint16_t type = (uint16_t)alu_logic(c, ax & 0x0F, 1);
    bx = x86_shift(c, 4, weapon, 4, 1);
    const uint8_t e = mem_read8(c, phys(ds, (uint16_t)(bx + type + 0x3898)));
    c->r[R_AX] = (uint16_t)(int16_t)(int8_t)e;                    /* cbw */
    c->r[R_BX] = bx;
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 17;
    near_ret(c);
    return 1;
}

/* VGAME 0x0EF36, the C runtime's 32-bit multiply: DX:AX = (a * b) mod 2^32
 * for a = [bp+6]:[bp+4], b = [bp+A]:[bp+8]; one MUL when both high words
 * are 0. RET 8 pops the arguments; BX is preserved, CX = b's low word. */
static int vgame_lmul(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 19)) return 0;
    const uint16_t alo = arg(c, 0), ahi = arg(c, 1), blo = arg(c, 2), bhi = arg(c, 3);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    alu_logic(c, (uint16_t)(bhi | ahi), 1);                       /* or cx, ax */
    c->r[R_CX] = blo;
    unsigned n;
    if (!(c->flags & F_ZF)) {
        const uint16_t bx = c->r[R_BX];
        cpu_push16(c, bx);
        c->r[R_AX] = ahi;
        x86_mul16(c, blo);
        uint16_t t = c->r[R_AX];
        c->r[R_AX] = alo;
        x86_mul16(c, bhi);
        t = (uint16_t)alu_add(c, t, c->r[R_AX], 1, 0);
        c->r[R_AX] = alo;
        x86_mul16(c, blo);
        c->r[R_DX] = (uint16_t)alu_add(c, c->r[R_DX], t, 1, 0);
        c->r[R_BX] = cpu_pop16(c);
        n = 19;
    } else {
        c->r[R_AX] = alo;
        x86_mul16(c, blo);
        n = 11;
    }
    c->r[R_BP] = cpu_pop16(c);
    c->icount += n;
    near_ret(c);
    c->r[R_SP] = (uint16_t)(c->r[R_SP] + 8);                      /* ret 8 */
    return 1;
}

/* VGAME 120A:027D / 120A:029E: install the renderer's divide-error handler
 * (CS:0971) on INT 0, keeping the old vector at [5EE6]/[5EE8]; and put the
 * old one back. ES preserved, BX = 0. */
static int vgame_hook_int0(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 13)) return 0;
    cpu_push16(c, c->seg[S_ES]);
    c->r[R_BX] = (uint16_t)alu_sub(c, c->r[R_BX], c->r[R_BX], 1, 0);
    ds_put(c, 0x5EE6, seg_read16(c, 0, 0));
    ds_put(c, 0x5EE8, seg_read16(c, 0, 2));
    seg_write16(c, 0, 2, c->seg[S_CS]);
    seg_write16(c, 0, 0, 0x0971);
    c->r[R_AX] = 0x0971;
    c->seg[S_ES] = cpu_pop16(c);
    c->icount += 13;
    near_ret(c);
    return 1;
}

static int vgame_unhook_int0(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 9)) return 0;
    cpu_push16(c, c->seg[S_ES]);
    c->r[R_BX] = (uint16_t)alu_sub(c, c->r[R_BX], c->r[R_BX], 1, 0);
    seg_write16(c, 0, 0, ds_get(c, 0x5EE6));
    const uint16_t seg = ds_get(c, 0x5EE8);
    seg_write16(c, 0, 2, seg);
    c->r[R_AX] = seg;
    c->seg[S_ES] = cpu_pop16(c);
    c->icount += 9;
    near_ret(c);
    return 1;
}

/* VGAME 0x0F79D: CL = 1 when bit 7 of (CX & ~[9264]) is clear, else 0;
 * AX that masked value, CH 0. */
static int vgame_mask_test(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    const uint16_t ax = (uint16_t)alu_logic(c, (uint16_t)(~ds_get(c, 0x9264) & c->r[R_CX]), 1);
    c->r[R_AX] = ax;
    c->r[R_CX] = (uint16_t)alu_logic(c, 0, 1);                    /* xor cx, cx */
    alu_logic(c, ax & 0x80, 0);                                   /* test al, 80h */
    unsigned n = 7;
    if (c->flags & F_ZF) { set_r8(c, R_CL, (uint8_t)alu_logic(c, 1, 0)); n = 8; }   /* or cl, 1 */
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 1058:0C9F: copy the axis word at [SI+2CCA] into its three
 * derived slots ([SI+2CB2], [SI+2CA2], [SI+2CAA]). AX preserved. */
static int vgame_axis_spread(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    const uint16_t si = c->r[R_SI];
    cpu_push16(c, c->r[R_AX]);
    const uint16_t v = ds_get(c, (uint16_t)(si + 0x2CCA));
    ds_put(c, (uint16_t)(si + 0x2CB2), v);
    ds_put(c, (uint16_t)(si + 0x2CA2), v);
    ds_put(c, (uint16_t)(si + 0x2CAA), v);
    c->r[R_AX] = cpu_pop16(c);
    c->icount += 7;
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
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEE1A, vgame_set_mission_clock, "start the mission clock", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xD9E7, vgame_set_scene_origin, "set the scene origin", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x4E4B, vgame_deadline_from_clock, "deadline from the clock", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x1BA7, vgame_read_vector, "read an interrupt vector", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEDC2, vgame_strupr, "upper-case a string", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEB82, vgame_strlen, "string length", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEB50, vgame_strcpy, "string copy", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEDE0, vgame_memcpy, "block copy", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEDA4, vgame_farcopy, "far block copy", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x85F1, vgame_map_screen_x, "moving map screen x", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x8608, vgame_map_screen_y, "moving map screen y", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x886A, vgame_pen, "text pen colour", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x8A67, vgame_weapon_effectiveness, "weapon effectiveness", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEF36, vgame_lmul, "32-bit multiply", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x027D, vgame_hook_int0, "hook the divide-error vector", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x029E, vgame_unhook_int0, "restore the divide-error vector", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xF79D, vgame_mask_test, "masked sign test", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1058, 0x0C9F, vgame_axis_spread, "spread an axis value", 1 },
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
