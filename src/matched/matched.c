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
#include "observe.h"
#include "recomp_rt.h"
#include "x86_sem.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


static int default_runner(machine_t *m) { return machine_run(m, m->run_until); }
matched_runner_fn matched_runner = default_runner;

/* Call original code at CS:target as a near CALL from the routine would,
 * returning to ret_ip (the original instruction after that CALL), and run
 * it until it returns. 1 when it returned; 0 when the run stopped first -
 * the outer run's limit came, or the program ended - in which case the
 * caller must return 1 at once: the machine is inside the callee, exactly
 * as the original would be, and the original code from ret_ip finishes the
 * routine when the callee returns. `pops` is what the callee's RET n removes. The guest state at every call must
 * therefore be the original's, stack frame included. */
static int guest_call_pop(machine_t *m, uint16_t target, uint16_t ret_ip, uint16_t pops)
{
    cpu_t *c = &m->cpu;
    const uint8_t on = m->trap_on;
    const uint16_t tcs = m->trap_cs, tip = m->trap_ip, tsp = m->trap_sp;
    cpu_push16(c, ret_ip);
    c->ip = target;
    c->icount++;                                                  /* the CALL */
    m->trap_on = 1;
    m->trap_cs = c->seg[S_CS];
    m->trap_ip = ret_ip;
    m->trap_sp = (uint16_t)(c->r[R_SP] + 2 + pops);
    const int rc = matched_runner(m);
    m->trap_on = on; m->trap_cs = tcs; m->trap_ip = tip; m->trap_sp = tsp;
    return rc == RUN_TRAP;
}

static int guest_call(machine_t *m, uint16_t target, uint16_t ret_ip) { return guest_call_pop(m, target, ret_ip, 0); }

/* The same for a far CALL to seg:off, returning to ret_ip in the caller's CS. */
static int guest_call_far_to(machine_t *m, uint16_t seg, uint16_t off, uint16_t ret_ip)
{
    cpu_t *c = &m->cpu;
    const uint16_t cs = c->seg[S_CS];
    const uint8_t on = m->trap_on;
    const uint16_t tcs = m->trap_cs, tip = m->trap_ip, tsp = m->trap_sp;
    cpu_push16(c, cs);
    cpu_push16(c, ret_ip);
    c->seg[S_CS] = seg;
    c->ip = off;
    c->icount++;                                                  /* the CALL FAR */
    m->trap_on = 1;
    m->trap_cs = cs;
    m->trap_ip = ret_ip;
    m->trap_sp = (uint16_t)(c->r[R_SP] + 4);
    const int rc = matched_runner(m);
    m->trap_on = on; m->trap_cs = tcs; m->trap_ip = tip; m->trap_sp = tsp;
    return rc == RUN_TRAP;
}

/* The same for a far CALL at CS:ins_ip (9A off seg): the target comes from
 * the loaded code, so its segment is the relocated one. */
static int guest_call_far(machine_t *m, uint16_t ins_ip, uint16_t ret_ip)
{
    cpu_t *c = &m->cpu;
    const uint16_t cs = c->seg[S_CS];
    return guest_call_far_to(m, seg_read16(c, cs, (uint16_t)(ins_ip + 3)), seg_read16(c, cs, (uint16_t)(ins_ip + 1)), ret_ip);
}

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
static int table_sine(machine_t *m, uint16_t table)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 14)) return 0;
    const uint16_t angle = c->r[R_BX];
    uint16_t dx = (uint16_t)((c->r[R_DX] & 0xFF00) | (angle & 0xFF));      /* mov dl, bl */
    dx = (uint16_t)((alu_sub(c, dx >> 8, dx >> 8, 0, 0) << 8) | (dx & 0xFF));   /* sub dh, dh */
    uint16_t bx = (uint16_t)(angle >> 8);                         /* mov bl, bh / mov bh, dh */
    bx = x86_shift(c, 4, bx, 1, 1);                               /* shl bx, 1 */
    const uint16_t ds = c->seg[S_DS];
    uint16_t ax = seg_read16(c, ds, (uint16_t)(bx + table + 2));
    bx = seg_read16(c, ds, (uint16_t)(bx + table));
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
static int vgame_sine(machine_t *m) { return table_sine(m, 0x2084); }
static int start_sine(machine_t *m) { return table_sine(m, 0xABF8); }

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
enum { STR_MOVS, STR_SCAS, STR_STOS };
static unsigned rep_string(cpu_t *c, int op, int w16, uint16_t src_seg, int repne)
{
    if (c->r[R_CX] == 0) return 1;
    unsigned n = 0;
    for (;;) {
        if (op == STR_MOVS) x86_movs(c, w16, src_seg);
        else if (op == STR_STOS) x86_stos(c, w16);
        else x86_scas(c, w16);
        n++;
        c->r[R_CX] = (uint16_t)(c->r[R_CX] - 1);
        if (c->r[R_CX] == 0) break;
        if (op == STR_SCAS && ((c->flags & F_ZF) != 0) == (repne != 0)) break;   /* REPNE stops on ZF=1, REPE on ZF=0 */
    }
    return n;
}

/* VGAME 0x0EE1A, start the mission clock: [929E] = t, [92A0] = 0. */
static int set_word_pair(machine_t *m, uint16_t at)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    const uint16_t t = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = t;
    ds_put(c, at, t);
    ds_put(c, (uint16_t)(at + 2), 0);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 7;
    near_ret(c);
    return 1;
}
static int vgame_set_mission_clock(machine_t *m) { return set_word_pair(m, 0x929E); }
static int start_set_word_pair(machine_t *m) { return set_word_pair(m, 0xAE8C); }

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
    /* The routines that call this push BP before they scan; a string that runs over the
     * two bytes about to be pushed would be read differently, so such a scan is left to the
     * original (a count no room can hold). */
    const uint32_t own0 = phys(c->seg[S_SS], (uint16_t)(c->r[R_SP] - 2)), own1 = phys(c->seg[S_SS], (uint16_t)(c->r[R_SP] - 1));
    unsigned k = 0;
    while (k < 0xFFFF) {
        const uint32_t at = phys(c->seg[S_ES], di);
        if (at == own0 || at == own1) return 0x1000000;
        const uint8_t b = mem_read8(c, at);
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
static int mask_test(machine_t *m, uint16_t at)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    const uint16_t ax = (uint16_t)alu_logic(c, (uint16_t)(~ds_get(c, at) & c->r[R_CX]), 1);
    c->r[R_AX] = ax;
    c->r[R_CX] = (uint16_t)alu_logic(c, 0, 1);                    /* xor cx, cx */
    alu_logic(c, ax & 0x80, 0);                                   /* test al, 80h */
    unsigned n = 7;
    if (c->flags & F_ZF) { set_r8(c, R_CL, (uint8_t)alu_logic(c, 1, 0)); n = 8; }   /* or cl, 1 */
    c->icount += n;
    near_ret(c);
    return 1;
}
static int vgame_mask_test(machine_t *m) { return mask_test(m, 0x9264); }
static int start_mask_test(machine_t *m) { return mask_test(m, 0xAE54); }

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

/* VGAME 0x04ABA, eventlog_add(kind, target): while the log at B9F0 (6-byte
 * records, count [951A]) has fewer than 255, append (mission time [9912],
 * x >> 7, y >> 7, kind, target) and zero the next record's kind byte. */
static int vgame_eventlog_add(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 22)) return 0;
    const uint16_t kind = arg(c, 0), target = arg(c, 1);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    alu_sub(c, ds_get(c, 0x951A), 0xFF, 1, 0);                    /* cmp [951A], 255 */
    unsigned n = 6;
    if (!x86_cond(c, 0xD)) {                                      /* jge not taken */
        const uint16_t ds = c->seg[S_DS];
        c->r[R_AX] = ds_get(c, 0x9912);
        uint16_t bx = x86_imul3(c, ds_get(c, 0x951A), 6);
        ds_put(c, (uint16_t)(bx - 0x45AA), c->r[R_AX]);
        uint16_t ax = x86_shift(c, 5, ds_get(c, 0xC0D0), 7, 1);
        mem_write8(c, phys(ds, (uint16_t)(bx - 0x45A8)), (uint8_t)ax);
        ax = x86_shift(c, 5, ds_get(c, 0xC0DE), 7, 1);
        mem_write8(c, phys(ds, (uint16_t)(bx - 0x45A7)), (uint8_t)ax);
        mem_write8(c, phys(ds, (uint16_t)(bx - 0x45A6)), (uint8_t)kind);
        mem_write8(c, phys(ds, (uint16_t)(bx - 0x45A5)), (uint8_t)target);
        c->r[R_AX] = (uint16_t)((ax & 0xFF00) | (target & 0xFF));
        ds_put(c, 0x951A, (uint16_t)alu_inc(c, ds_get(c, 0x951A), 1));
        bx = x86_imul3(c, ds_get(c, 0x951A), 6);
        mem_write8(c, phys(ds, (uint16_t)(bx - 0x45A6)), 0);
        c->r[R_BX] = bx;
        n = 22;
    }
    x86_leave(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

static int words_overlap(uint32_t a, uint32_t b) { return a + 1 >= b && b + 1 >= a; }

/* VGAME 0x049F9, the altitude-alert chain's reset: [3664], [C5F4] = 0;
 * each of the four entries at 3670 (4 bytes) gets state 9 when its level
 * is below 16, else 1; [3682] = 1000, [3666] = 5000. Run as the original
 * runs it - the loop counter is a stack word - and declined when that word
 * or the saved BP could alias the data it touches (random states only). */
static int vgame_alt_chain_reset(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t ds = c->seg[S_DS];
    const uint32_t bp_word = phys(c->seg[S_SS], (uint16_t)(c->r[R_SP] - 2));
    const uint32_t counter = phys(c->seg[S_SS], (uint16_t)(c->r[R_SP] - 4));
    static const uint16_t touched[] = { 0x3664, 0xC5F4, 0x3682, 0x3666, 0x3670, 0x3672, 0x3674, 0x3676,
                                        0x3678, 0x367A, 0x367C, 0x367E, 0x3680 };
    for (unsigned i = 0; i < sizeof touched / sizeof touched[0]; i++) {
        const uint32_t a = phys(ds, touched[i]);
        if (words_overlap(a, bp_word) || words_overlap(a, counter)) return 0;
    }
    unsigned n = 6 + 2 + 4;
    for (int i = 0; i < 4; i++)
        /* 9 a pass; a level of 16 or more jumps back through the BX
         * reload at 0x4A0A, two instructions more than the jmp it skips */
        n += 9 + ((int16_t)seg_read16(c, ds, (uint16_t)(0x3670 + 4 * i)) >= 0x10 ? 1 : 0);
    if (!room(c, n)) return 0;
    x86_enter(c, 2, 0);
    const uint16_t ss = c->seg[S_SS], local = (uint16_t)(c->r[R_BP] - 2);
    const uint16_t zero = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    c->r[R_AX] = zero;
    ds_put(c, 0x3664, zero);
    ds_put(c, 0xC5F4, zero);
    seg_write16(c, ss, local, zero);
    for (;;) {
        alu_sub(c, seg_read16(c, ss, local), 4, 1, 0);            /* cmp [bp-2], 4 */
        if (x86_cond(c, 0xD)) break;                              /* jge */
        uint16_t bx = x86_shift(c, 4, seg_read16(c, ss, local), 2, 1);
        alu_sub(c, ds_get(c, (uint16_t)(bx + 0x3670)), 0x10, 1, 0);
        if (x86_cond(c, 0xD)) {                                   /* jge 0x4A0A: reload, shift again */
            bx = x86_shift(c, 4, seg_read16(c, ss, local), 2, 1);
            ds_put(c, (uint16_t)(bx + 0x3672), 1);
        } else {
            ds_put(c, (uint16_t)(bx + 0x3672), 9);
        }
        c->r[R_BX] = bx;
        seg_write16(c, ss, local, (uint16_t)alu_inc(c, seg_read16(c, ss, local), 1));
    }
    ds_put(c, 0x3682, 1000);
    ds_put(c, 0x3666, 5000);
    x86_leave(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 0x0C436, decode a scene word: with bit 8 set, the low seven bits
 * index the word table at 0510 and bit 15 is carried over; otherwise the
 * word itself. */
static int vgame_scene_word(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 12)) return 0;
    const uint16_t w = arg(c, 0);
    x86_enter(c, 4, 0);
    alu_logic(c, (w >> 8) & 1, 0);                                /* test byte [bp+5], 1 */
    unsigned n;
    if (c->flags & F_ZF) { c->r[R_AX] = w; n = 6; }
    else {
        uint16_t bx = (uint16_t)((c->r[R_BX] & 0xFF00) | (w & 0xFF));
        bx = (uint16_t)alu_logic(c, bx & 0x7F, 1);
        bx = x86_shift(c, 4, bx, 1, 1);
        c->r[R_BX] = bx;
        const uint16_t ax = ds_get(c, (uint16_t)(bx + 0x0510));
        uint16_t cx = (uint16_t)((c->r[R_CX] & 0x00FF) | (w & 0xFF00));
        cx = (uint16_t)alu_logic(c, cx & 0x8000, 1);
        c->r[R_CX] = cx;
        c->r[R_AX] = (uint16_t)alu_logic(c, ax | cx, 1);
        n = 12;
    }
    x86_leave(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 0x0C845, sign-extend a key byte in place: the argument word
 * becomes its low byte as a signed value, and is returned. */
static int vgame_key_sign_extend(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 12)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    const uint16_t ss = c->seg[S_SS], slot = (uint16_t)(c->r[R_BP] + 4);
    uint16_t ax = mem_read8(c, phys(ss, slot));                   /* mov al, [bp+4] */
    alu_sub(c, (c->r[R_AX] >> 8), (c->r[R_AX] >> 8), 0, 0);       /* sub ah, ah */
    alu_sub(c, ax, 0x80, 1, 0);                                   /* cmp ax, 80h */
    unsigned n;
    if (x86_cond(c, 0xC)) {                                       /* jl */
        mem_write8(c, phys(ss, (uint16_t)(slot + 1)), 0);
        n = 10;
    } else {
        seg_write16(c, ss, slot, (uint16_t)alu_sub(c, ax, 0x100, 1, 0));
        n = 12;
    }
    c->r[R_AX] = seg_read16(c, ss, slot);
    x86_leave(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 0x0B9F6, destroyed_type(object): 1 for object types 0x0C, 0x0D,
 * 9 and 0x0B (as class5_takes_lock reads the type), else 0. */
static int vgame_destroyed_type(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 18)) return 0;
    x86_enter(c, 2, 0);
    const uint16_t object = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 4));
    uint16_t bx = x86_shift(c, 4, object, 4, 1);
    bx = (uint16_t)((bx & 0xFF00) | mem_read8(c, phys(c->seg[S_DS], (uint16_t)(bx - 0x4D24))));
    bx = (uint16_t)alu_logic(c, bx & 0x7F, 1);
    c->r[R_BX] = bx;
    uint16_t ax = (uint16_t)((c->r[R_AX] & 0xFF00) | mem_read8(c, phys(c->seg[S_DS], (uint16_t)(bx - 0x39D0))));
    ax = (uint16_t)alu_logic(c, ax & 0x0F, 1);
    static const uint16_t yes[] = { 0x0C, 0x0D, 0x09, 0x0B };
    unsigned n = 9, hit = 0;
    for (int i = 0; i < 4 && !hit; i++) {
        alu_sub(c, ax, yes[i], 1, 0);
        n += i ? 2 : 0;                                           /* each later cmp/j pair */
        hit = (c->flags & F_ZF) != 0;
    }
    if (hit) { ax = 1; n += 3; }
    else { ax = (uint16_t)alu_sub(c, ax, ax, 1, 0); n += 3; }
    c->r[R_AX] = ax;
    x86_leave(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 130D:00B6 and 130D:00D1: widen a polygon's row span by a side
 * from BX to CX (ordered), into [85E4]/[85E6] or [85E8]/[85EA]. */
static int poly_side(machine_t *m, uint16_t lo_at, uint16_t hi_at)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 10)) return 0;
    unsigned n = 7;
    alu_sub(c, c->r[R_CX], c->r[R_BX], 1, 0);
    if (!x86_cond(c, 0xD)) {                                      /* jge not taken: xchg */
        const uint16_t t = c->r[R_BX]; c->r[R_BX] = c->r[R_CX]; c->r[R_CX] = t;
        n++;
    }
    alu_sub(c, c->r[R_CX], ds_get(c, hi_at), 1, 0);
    if (!x86_cond(c, 0xC)) { ds_put(c, hi_at, c->r[R_CX]); n++; }  /* jl skips */
    alu_sub(c, c->r[R_BX], ds_get(c, lo_at), 1, 0);
    if (!x86_cond(c, 0xD)) { ds_put(c, lo_at, c->r[R_BX]); n++; }  /* jge skips */
    c->icount += n;
    near_ret(c);
    return 1;
}
static int vgame_poly_side_a(machine_t *m) { return poly_side(m, 0x85E4, 0x85E6); }
static int vgame_poly_side_b(machine_t *m) { return poly_side(m, 0x85E8, 0x85EA); }

/* VGAME 1377:0116: the 200 row offsets at DS:861C become 0, 320, 640, ...
 * (STOSW, ES = DS) and [85F2] = 7Ch. */
static int vgame_row_offsets(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 608)) return 0;
    c->r[R_CX] = 0xC8;
    c->seg[S_ES] = c->seg[S_DS];
    c->r[R_DI] = 0x861C;
    uint16_t ax = (uint16_t)alu_sub(c, c->seg[S_DS], c->seg[S_DS], 1, 0);
    for (int i = 0; i < 200; i++) {
        c->r[R_AX] = ax;
        x86_stos(c, 1);
        ax = (uint16_t)alu_add(c, ax, 0x140, 1, 0);
    }
    c->r[R_AX] = ax;
    c->r[R_CX] = 0;
    c->r[R_BX] = 0x7C;
    ds_put(c, 0x85F2, 0x7C);
    c->icount += 608;
    near_ret(c);
    return 1;
}

/* VGAME 0FB2:051F, clear the span tables for the rows [2615]..[2617]
 * used last frame: the left table (at 22A1 in the segment the code loads,
 * 1E42h as relocated) to FFFF and the right (at 2459) to 0, then mark none
 * used ([2615] = FFFF, [2617] = 0). Nothing when [2615] is negative. */
static int vgame_spans_reset(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t first = ds_get(c, 0x2615);
    if (first & 0x8000) {
        if (!room(c, 4)) return 0;
        c->r[R_DI] = (uint16_t)alu_logic(c, first, 1);
        c->icount += 4;
        near_ret(c);
        return 1;
    }
    const uint16_t count = (uint16_t)(ds_get(c, 0x2617) + 1 - first);
    const unsigned r = count ? count : 1;
    if (!room(c, 20 + 2 * r)) return 0;
    alu_logic(c, first, 1);                                       /* or di, di */
    const uint16_t seg = seg_read16(c, c->seg[S_CS], (uint16_t)(c->ip + 0x09));   /* mov ax, imm16 */
    c->seg[S_ES] = seg;
    uint16_t cx = (uint16_t)alu_inc(c, ds_get(c, 0x2617), 1);
    cx = (uint16_t)alu_sub(c, cx, first, 1, 0);
    const uint16_t di2 = x86_shift(c, 4, first, 1, 1);           /* shl di, 1 */
    c->r[R_BX] = cx;
    c->r[R_DX] = di2;
    c->r[R_DI] = (uint16_t)alu_add(c, di2, 0x22A1, 1, 0);
    c->r[R_AX] = 0xFFFF;
    c->r[R_CX] = cx;
    for (unsigned i = 0; i < count; i++) x86_stos(c, 1);
    c->r[R_CX] = 0;
    ds_put(c, 0x2615, 0xFFFF);
    c->r[R_CX] = cx;
    c->r[R_DI] = (uint16_t)alu_add(c, di2, 0x2459, 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0xFFFF, 0xFFFF, 1, 0);
    for (unsigned i = 0; i < count; i++) x86_stos(c, 1);
    c->r[R_CX] = 0;
    ds_put(c, 0x2617, 0);
    c->icount += 20 + 2 * r;
    near_ret(c);
    return 1;
}

/* VGAME 130D:064A, mc32_on_edge: BP = 1 when DX:AX and CX:BX are both
 * 16-bit (high words zero) and the point lies on one of the clip window's
 * edges ([85FA]/[85FE] for x, [85FC]/[8600] for y), else 0. */
static int vgame_mc32_on_edge(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 15)) return 0;
    c->r[R_BP] = (uint16_t)alu_sub(c, c->r[R_BP], c->r[R_BP], 1, 0);
    unsigned n = 1;
    int on = 0;
    alu_logic(c, c->r[R_DX], 1); n += 2;
    if (c->flags & F_ZF) {
        alu_logic(c, c->r[R_CX], 1); n += 2;
        if (c->flags & F_ZF) {
            const uint16_t at[4] = { 0x85FA, 0x85FE, 0x85FC, 0x8600 };
            for (int i = 0; i < 4 && !on; i++) {
                alu_sub(c, i < 2 ? c->r[R_AX] : c->r[R_BX], ds_get(c, at[i]), 1, 0);
                n += 2;
                on = (c->flags & F_ZF) != 0;
            }
        }
    }
    if (on) { c->r[R_BP] = (uint16_t)alu_inc(c, c->r[R_BP], 1); n += 2; }
    else { alu_logic(c, c->r[R_BP], 1); n += 2; }
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 130D:0671, mc32_outcode: the clip outcode of the 32-bit point
 * (DX:AX, CX:BX) against the window [85FA]..[85FE] x [85FC]..[8600], in
 * BP: 4 left, 8 right, 1 above, 2 below. A negative high word is outside
 * low, a positive one outside high; within 16 bits the words compare
 * unsigned. */
static int vgame_mc32_outcode(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 19)) return 0;
    uint16_t bp = (uint16_t)alu_sub(c, c->r[R_BP], c->r[R_BP], 1, 0);
    unsigned n = 1;
    alu_logic(c, c->r[R_DX], 1); n += 2;                          /* or dx, dx / js */
    if (c->flags & F_SF) { bp = (uint16_t)alu_logic(c, bp | 4, 1); n += 2; }
    else if (n++, !(c->flags & F_ZF)) { bp = (uint16_t)alu_logic(c, bp | 8, 1); n += 2; }
    else {
        alu_sub(c, c->r[R_AX], ds_get(c, 0x85FA), 1, 0); n += 2;
        if (c->flags & F_CF) { bp = (uint16_t)alu_logic(c, bp | 4, 1); n += 2; }
        else {
            alu_sub(c, c->r[R_AX], ds_get(c, 0x85FE), 1, 0); n += 2;
            if (x86_cond(c, 0x7)) { bp = (uint16_t)alu_logic(c, bp | 8, 1); n += 2; }
        }
    }
    alu_logic(c, c->r[R_CX], 1); n += 2;                          /* or cx, cx / js */
    if (c->flags & F_SF) { bp = (uint16_t)alu_logic(c, bp | 1, 1); n += 2; }
    else if (n++, !(c->flags & F_ZF)) { bp = (uint16_t)alu_logic(c, bp | 2, 1); n += 2; }
    else {
        alu_sub(c, c->r[R_BX], ds_get(c, 0x85FC), 1, 0); n += 2;
        if (c->flags & F_CF) { bp = (uint16_t)alu_logic(c, bp | 1, 1); n += 2; }
        else {
            alu_sub(c, c->r[R_BX], ds_get(c, 0x8600), 1, 0); n += 2;
            if (x86_cond(c, 0x7)) bp = (uint16_t)alu_logic(c, bp | 2, 1);
            else alu_logic(c, bp, 1);                             /* or bp, bp */
            n += 2;
        }
    }
    c->r[R_BP] = bp;
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 1377:0132 and 1377:0155: the row-offset table at DS:861C for the
 * interleaved planar modes, two or four banks of 2000h with 80 or 160
 * bytes a row ([85F2] = 88h or 94h). */
static int row_banks(machine_t *m, unsigned rows, unsigned banks, uint16_t step, uint16_t tag)
{
    cpu_t *c = &m->cpu;
    const unsigned per = banks * 2 + 2;                          /* stosw/add pairs, sub, add, loop */
    if (!room(c, 8 + rows * per)) return 0;
    c->seg[S_ES] = c->seg[S_DS];
    c->r[R_DI] = 0x861C;
    uint16_t ax = (uint16_t)alu_sub(c, c->seg[S_DS], c->seg[S_DS], 1, 0);
    for (unsigned r = 0; r < rows; r++) {
        for (unsigned b = 0; b < banks; b++) {
            c->r[R_AX] = ax;
            x86_stos(c, 1);
            if (b + 1 < banks) ax = (uint16_t)alu_add(c, ax, 0x2000, 1, 0);
        }
        ax = (uint16_t)alu_sub(c, ax, (uint16_t)(0x2000 * (banks - 1)), 1, 0);
        ax = (uint16_t)alu_add(c, ax, step, 1, 0);
    }
    c->r[R_AX] = ax;
    c->r[R_CX] = 0;
    c->r[R_BX] = tag;
    ds_put(c, 0x85F2, tag);
    c->icount += 8 + rows * per;
    near_ret(c);
    return 1;
}
static int vgame_row_banks2(machine_t *m) { return row_banks(m, 100, 2, 0x50, 0x88); }
static int vgame_row_banks4(machine_t *m) { return row_banks(m, 50, 4, 0xA0, 0x94); }

/* VGAME 1377:01E0, raster_model_fill_begin(AX = colour): keep the colour
 * at [8606]; a shaded colour (AH = FFh, AL >= A0h) is darkened by [48C4]
 * but not below its band's base (AL & F0h). ES = [861A]. */
static int vgame_fill_begin(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 15)) return 0;
    uint16_t ax = c->r[R_AX];
    ds_put(c, 0x8606, ax);
    unsigned n = 6;
    alu_sub(c, ax >> 8, 0xFF, 0, 0);                              /* cmp ah, FFh */
    if (c->flags & F_ZF) {
        alu_sub(c, ax & 0xFF, 0xA0, 0, 0); n += 2;               /* cmp al, A0h */
        if (!(c->flags & F_CF)) {
            const uint8_t base = (uint8_t)alu_logic(c, ax & 0xF0, 0);
            uint8_t al = (uint8_t)alu_sub(c, ax & 0xFF, mem_read8(c, phys(c->seg[S_DS], 0x48C4)), 0, 0);
            alu_sub(c, al, base, 0, 0);
            n += 6;
            if (!x86_cond(c, 0x7)) { al = base; n++; }           /* ja skips */
            mem_write8(c, phys(c->seg[S_DS], 0x8606), al);
        }
    }
    c->r[R_AX] = ds_get(c, 0x861A);
    c->seg[S_ES] = c->r[R_AX];
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 1452:02AC, camera_matrix_multiply: DI's three words = the row at SI
 * times the three columns at BX (stride 6), each a 1.15 fixed-point dot
 * product kept as the high word after one more shift - as shipped, the
 * second carry goes into DX, not BP. BX restored, DI advanced by 6. */
static int vgame_camera_row(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 57)) return 0;
    const uint16_t ds = c->seg[S_DS];
    c->r[R_CX] = 3;
    for (int k = 0; k < 3; k++) {
        const uint16_t si = c->r[R_SI], bx = c->r[R_BX];
        c->r[R_AX] = seg_read16(c, ds, si);
        x86_imul16(c, seg_read16(c, ds, bx));
        uint16_t bp = c->r[R_DX];
        ds_put(c, 0x919A, c->r[R_AX]);
        c->r[R_AX] = seg_read16(c, ds, (uint16_t)(si + 2));
        x86_imul16(c, seg_read16(c, ds, (uint16_t)(bx + 6)));
        ds_put(c, 0x919A, (uint16_t)alu_add(c, ds_get(c, 0x919A), c->r[R_AX], 1, 0));
        bp = (uint16_t)alu_add(c, bp, c->r[R_DX], 1, (c->flags & F_CF) ? 1u : 0u);
        c->r[R_AX] = seg_read16(c, ds, (uint16_t)(si + 4));
        x86_imul16(c, seg_read16(c, ds, (uint16_t)(bx + 0x0C)));
        ds_put(c, 0x919A, (uint16_t)alu_add(c, ds_get(c, 0x919A), c->r[R_AX], 1, 0));
        uint16_t dx = (uint16_t)alu_add(c, c->r[R_DX], bp, 1, (c->flags & F_CF) ? 1u : 0u);
        ds_put(c, 0x919A, x86_shift(c, 4, ds_get(c, 0x919A), 1, 1));   /* shl [919A], 1 */
        dx = x86_shift(c, 2, dx, 1, 1);                           /* rcl dx, 1 */
        c->r[R_DX] = dx;
        c->r[R_BP] = bp;
        ds_put(c, c->r[R_DI], dx);
        c->r[R_DI] = (uint16_t)alu_add(c, c->r[R_DI], 2, 1, 0);
        c->r[R_BX] = (uint16_t)alu_add(c, bx, 2, 1, 0);
        c->r[R_CX]--;
    }
    c->r[R_BX] = (uint16_t)alu_sub(c, c->r[R_BX], 6, 1, 0);
    c->icount += 57;
    near_ret(c);
    return 1;
}

/* VGAME 120A:0654, model_planes_pass setup: clear (|[7D5C]| + 2) / 2 words
 * at DS:7A02. ES preserved; DX the sign of [7D5C]. */
static int vgame_planes_clear(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t v = ds_get(c, 0x7D5C);
    const uint16_t sign = (v & 0x8000) ? 0xFFFF : 0;
    const uint16_t mag = (uint16_t)((v ^ sign) - sign);
    const uint16_t words = (uint16_t)((uint16_t)(mag + 2) >> 1);
    const unsigned r = words ? words : 1;
    if (!room(c, 15 + r)) return 0;
    c->r[R_DX] = sign;
    uint16_t ax = (uint16_t)alu_sub(c, (uint16_t)alu_logic(c, v ^ sign, 1), sign, 1, 0);
    uint16_t cx = ax;
    ax = (uint16_t)alu_sub(c, ax, ax, 1, 0);
    cpu_push16(c, c->seg[S_ES]);
    c->r[R_DI] = c->seg[S_DS];
    c->seg[S_ES] = c->seg[S_DS];
    c->r[R_DI] = 0x7A02;
    c->r[R_AX] = (uint16_t)alu_sub(c, ax, ax, 1, 0);
    cx = (uint16_t)alu_add(c, cx, 2, 1, 0);
    cx = x86_shift(c, 5, cx, 1, 1);                               /* shr cx, 1 */
    c->r[R_CX] = cx;
    rep_string(c, STR_STOS, 1, 0, 0);
    c->seg[S_ES] = cpu_pop16(c);
    c->icount += 15 + r;
    near_ret(c);
    return 1;
}

/* VGAME 0x0BA56, recon_air_ground(unit): the terrain class under the unit
 * (its 36-byte record at C16C: x, y >> 11 index the 16-wide map at B1A0,
 * low two bits) goes to [49F6] for the last four units; for the others
 * [49F6] = 1 only when the class is 0 and [3D90] is 0 too, else 0. */
static int vgame_recon_air_ground(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 23)) return 0;
    const uint16_t ds = c->seg[S_DS];
    x86_enter(c, 2, 0);
    cpu_push16(c, c->r[R_SI]);
    const uint16_t unit = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 4));
    uint16_t bx = x86_imul3(c, unit, 0x24);
    uint16_t si = x86_shift(c, 5, seg_read16(c, ds, (uint16_t)(bx - 0x3E92)), 0x0B, 1);
    si = x86_shift(c, 4, si, 4, 1);
    bx = x86_shift(c, 5, seg_read16(c, ds, (uint16_t)(bx - 0x3E94)), 0x0B, 1);
    c->r[R_BX] = bx;
    uint16_t ax = (uint16_t)((c->r[R_AX] & 0xFF00) | mem_read8(c, phys(ds, (uint16_t)(bx + si - 0x4E60))));
    ax = (uint16_t)alu_logic(c, ax & 3, 1);
    c->r[R_AX] = ax;
    uint16_t cx = (uint16_t)alu_sub(c, ds_get(c, 0xDEFC), 4, 1, 0);
    c->r[R_CX] = cx;
    alu_sub(c, cx, unit, 1, 0);                                   /* cmp cx, [bp+4] */
    unsigned n;
    if (!x86_cond(c, 0xF)) { ds_put(c, 0x49F6, ax); n = 18; }    /* jg not taken */
    else {
        ds_put(c, 0x49F6, 1);
        alu_logic(c, ax, 1);                                      /* or ax, ax */
        if (!(c->flags & F_ZF)) { ds_put(c, 0x49F6, 0); n = 21; }
        else {
            alu_sub(c, ds_get(c, 0x3D90), ax, 1, 0);
            if (c->flags & F_ZF) n = 22;
            else { ds_put(c, 0x49F6, 0); n = 23; }
        }
    }
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 0x0F0F4, translate a key (AL, extended code in AH) through the
 * table at 92A4 into [9262], keeping AL at [926D]; codes past 13h are
 * clamped to 13h, and with [926A] >= 3, 20h-21h map to 5 first. An
 * extended key uses its scan code directly, untranslated. */
static int key_translate(machine_t *m, uint16_t base, uint16_t table)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 17)) return 0;
    const uint16_t ds = c->seg[S_DS];
    uint8_t al = (uint8_t)c->r[R_AX];
    const uint8_t ah = (uint8_t)(c->r[R_AX] >> 8);
    mem_write8(c, phys(ds, (uint16_t)(base + 0x0B)), al);
    alu_logic(c, ah, 0);                                          /* or ah, ah */
    unsigned n = 3;
    int xlat = 1;
    if (!(c->flags & F_ZF)) { al = ah; n += 2; xlat = 0; }        /* mov al, ah / jmp */
    else {
        alu_sub(c, mem_read8(c, phys(ds, (uint16_t)(base + 0x08))), 3, 0, 0); n += 2;
        int low = (c->flags & F_CF) != 0;                         /* jb: straight to the clamp */
        int to13 = 0, five = 0;
        if (!low) {
            alu_sub(c, al, 0x22, 0, 0); n += 2;
            if (!(c->flags & F_CF)) to13 = 1;                     /* jae */
            else {
                alu_sub(c, al, 0x20, 0, 0); n += 2;
                if (c->flags & F_CF) low = 1;                     /* jb */
                else five = 1;
            }
        }
        if (five) { al = 5; n += 2; }                             /* mov al, 5 / jmp */
        else if (low) {
            alu_sub(c, al, 0x13, 0, 0); n += 2;
            if (x86_cond(c, 0x7)) to13 = 1;                       /* jbe not taken */
        }
        if (to13) { al = 0x13; n++; }
    }
    if (xlat) {
        c->r[R_BX] = table;
        al = mem_read8(c, phys(ds, (uint16_t)(table + al)));
        n += 2;                                                   /* mov bx / xlatb */
    }
    c->r[R_AX] = (uint16_t)(int16_t)(int8_t)al;                   /* cbw */
    ds_put(c, base, c->r[R_AX]);
    c->icount += n + 3;                                           /* cbw, mov, ret */
    near_ret(c);
    return 1;
}
static int vgame_key_translate(machine_t *m) { return key_translate(m, 0x9262, 0x92A4); }
static int start_key_translate(machine_t *m) { return key_translate(m, 0xAE52, 0xAE92); }

/* VGAME 120A:0674, plane_shade: the plane's normal (three words at ES:SI-10)
 * dotted with the light vector at [7D7C], as 2.14 fixed point, gives the
 * shade byte at [DI+7802]; SI ends 6 bytes on. BX preserved. */
static int vgame_plane_shade(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 27)) return 0;
    const uint16_t ds = c->seg[S_DS], es = c->seg[S_ES];
    cpu_push16(c, c->r[R_BX]);
    const uint16_t light = ds_get(c, 0x7D7C);
    c->r[R_BX] = light;
    uint16_t si = (uint16_t)alu_sub(c, c->r[R_SI], 0x0A, 1, 0);
    uint16_t cx = 0, bp = 0;
    for (int k = 0; k < 3; k++) {
        c->r[R_AX] = seg_read16(c, es, si);
        si = (uint16_t)alu_add(c, si, 2, 1, 0);
        x86_imul16(c, seg_read16(c, ds, (uint16_t)(light + 2 * k)));
        if (k == 0) { cx = c->r[R_AX]; bp = c->r[R_DX]; }
        else {
            cx = (uint16_t)alu_add(c, cx, c->r[R_AX], 1, 0);
            bp = (uint16_t)alu_add(c, bp, c->r[R_DX], 1, (c->flags & F_CF) ? 1u : 0u);
        }
    }
    for (int k = 0; k < 2; k++) {
        cx = x86_shift(c, 4, cx, 1, 1);                           /* shl cx, 1 */
        bp = x86_shift(c, 2, bp, 1, 1);                           /* rcl bp, 1 */
    }
    c->r[R_CX] = cx;
    c->r[R_BP] = bp;
    c->r[R_AX] = bp;
    c->r[R_SI] = (uint16_t)alu_add(c, si, 4, 1, 0);
    mem_write8(c, phys(ds, (uint16_t)(c->r[R_DI] + 0x7802)), (uint8_t)bp);
    c->r[R_BX] = cpu_pop16(c);
    c->icount += 27;
    near_ret(c);
    return 1;
}

/* VGAME 130D:00EC, keep a polygon for clipping: the eight bytes at SI+10h
 * go to the first slot at 85B8, or the second (85C0) when one is held;
 * [85EE] counts them. */
static int vgame_poly_collect(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 14)) return 0;
    uint16_t bx = 0x85B8;
    unsigned n = 13;
    alu_sub(c, ds_get(c, 0x85EE), 0, 1, 0);
    if (!(c->flags & F_ZF)) { bx = (uint16_t)alu_add(c, bx, 8, 1, 0); n = 14; }
    ds_put(c, 0x85EE, (uint16_t)alu_inc(c, ds_get(c, 0x85EE), 1));
    const uint16_t si = c->r[R_SI];
    for (int k = 0; k < 4; k++) {
        c->r[R_AX] = ds_get(c, (uint16_t)(si + 0x10 + 2 * k));
        ds_put(c, (uint16_t)(bx + 2 * k), c->r[R_AX]);
    }
    c->r[R_BX] = bx;
    c->icount += n;
    near_ret(c);
    return 1;
}

/* START 0x0393D (game_menu_read) and END 0x01C3B (widget_hit): the 1-based
 * index of the first of n 8-byte rectangles (x0, y0, x1, y1, unsigned) at
 * list that holds the pointer ([px], [py]), or 0. The list pointer is the
 * argument slot itself, advanced in place, and the count a stack local. */
static int hit_test(machine_t *m, uint16_t px_at, uint16_t py_at)
{
    cpu_t *c = &m->cpu;
    const uint16_t ds = c->seg[S_DS], ss = c->seg[S_SS];
    /* Count first: the loop reads its rectangles, the pointer and two
     * stack words, and writes only the stack (argument slot and counter),
     * so it can be stepped without effects when those cannot alias. */
    const uint16_t sp = c->r[R_SP];
    const uint32_t frame_lo = phys(ss, (uint16_t)(sp - 4)), frame_hi = phys(ss, (uint16_t)(sp + 4));
    const uint16_t n = seg_read16(c, ss, (uint16_t)(sp + 4));
    uint16_t list = seg_read16(c, ss, (uint16_t)(sp + 2));
    const uint16_t px = ds_get(c, px_at), py = ds_get(c, py_at);
    const uint32_t pxl = phys(ds, px_at), pyl = phys(ds, py_at);
    if (words_overlap(pxl, frame_lo) || words_overlap(pyl, frame_lo) ||
        (pxl >= frame_lo && pxl <= frame_hi + 1) || (pyl >= frame_lo && pyl <= frame_hi + 1)) return 0;
    /* Clocks: 5 to set up; a rectangle missed at its first, second, third
     * or fourth edge costs 9, 11, 14 or 16 (the step to the next included),
     * a hit 14, running out of rectangles 3; then 3 to test the count, 3
     * (found) or 1 to form the answer, and 3 to return. */
    unsigned clocks = 5;
    int found = 0;
    for (uint32_t i = 0; (int32_t)i < (int16_t)n; i++) {
        const uint32_t r = phys(ds, list);
        if (r + 7 >= frame_lo && r <= frame_hi + 1) return 0;     /* a rectangle in the frame */
        const uint16_t x0 = seg_read16(c, ds, list), x1 = seg_read16(c, ds, (uint16_t)(list + 4));
        const uint16_t y0 = seg_read16(c, ds, (uint16_t)(list + 2)), y1 = seg_read16(c, ds, (uint16_t)(list + 6));
        if (x0 > px) clocks += 9;
        else if (x1 < px) clocks += 11;
        else if (y0 > py) clocks += 14;
        else if (y1 < py) clocks += 16;
        else { clocks += 14; found = 1; break; }
        list = (uint16_t)(list + 8);
    }
    if (!found) clocks += 3;
    clocks += 3 + (found ? 3 : 1) + 3;
    if (!room(c, clocks)) return 0;
    /* Now run it, as written. */
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)(c->r[R_SP] - 2);
    const uint16_t bp = c->r[R_BP];
    seg_write16(c, ss, (uint16_t)(bp - 2), 0);
    for (;;) {
        c->r[R_AX] = seg_read16(c, ss, (uint16_t)(bp + 6));
        alu_sub(c, seg_read16(c, ss, (uint16_t)(bp - 2)), c->r[R_AX], 1, 0);
        if (x86_cond(c, 0xD)) break;                              /* jge */
        c->r[R_AX] = px;
        const uint16_t bx = seg_read16(c, ss, (uint16_t)(bp + 4));
        c->r[R_BX] = bx;
        int miss;
        alu_sub(c, seg_read16(c, ds, bx), px, 1, 0);
        miss = x86_cond(c, 0x7);                                  /* ja */
        if (!miss) { alu_sub(c, seg_read16(c, ds, (uint16_t)(bx + 4)), px, 1, 0); miss = (c->flags & F_CF) != 0; }
        if (!miss) {
            c->r[R_AX] = py;
            alu_sub(c, seg_read16(c, ds, (uint16_t)(bx + 2)), py, 1, 0); miss = x86_cond(c, 0x7);
            if (!miss) { alu_sub(c, seg_read16(c, ds, (uint16_t)(bx + 6)), py, 1, 0); miss = (c->flags & F_CF) != 0; }
        }
        if (!miss) break;
        seg_write16(c, ss, (uint16_t)(bp - 2), (uint16_t)alu_inc(c, seg_read16(c, ss, (uint16_t)(bp - 2)), 1));
        seg_write16(c, ss, (uint16_t)(bp + 4), (uint16_t)alu_add(c, seg_read16(c, ss, (uint16_t)(bp + 4)), 8, 1, 0));
    }
    c->r[R_AX] = seg_read16(c, ss, (uint16_t)(bp + 6));
    alu_sub(c, seg_read16(c, ss, (uint16_t)(bp - 2)), c->r[R_AX], 1, 0);
    if (!x86_cond(c, 0xD)) c->r[R_AX] = (uint16_t)alu_inc(c, seg_read16(c, ss, (uint16_t)(bp - 2)), 1);
    else c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    c->r[R_SP] = bp;
    c->r[R_BP] = cpu_pop16(c);
    c->icount += clocks;
    near_ret(c);
    return 1;
}
static int start_menu_hit(machine_t *m) { return hit_test(m, 0xE08A, 0xE08C); }
static int end_widget_hit(machine_t *m) { return hit_test(m, 0x7214, 0x7216); }

/* START 0x059D8, clamp(v, lo, hi): hi when v > hi; v when v >= lo; lo when
 * v is above -16384, else hi. */
static int start_clamp(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 14)) return 0;
    const uint16_t v = arg(c, 0), lo = arg(c, 1), hi = arg(c, 2);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    uint16_t ax = hi;
    alu_sub(c, v, hi, 1, 0);
    unsigned n;
    if (x86_cond(c, 0xF)) n = 8;                                  /* jg: hi */
    else {
        ax = lo;
        alu_sub(c, v, lo, 1, 0);
        if (!x86_cond(c, 0xC)) { ax = v; n = 13; }                /* jl not taken */
        else {
            alu_sub(c, v, 0xC000, 1, 0);
            if (x86_cond(c, 0xF)) n = 13;
            else { ax = hi; n = 14; }
        }
    }
    c->r[R_AX] = ax;
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* START 0x088D4, reset the LZW decoder's table: 9-bit codes, next code
 * 1FFh... [8D74]=9, [8D76]=1FFh, [8D78]=100h; all 2048 three-byte entries'
 * prefix words at 7420 to FFFF, then the 256 literal entries' bytes at
 * 7422 to 0..255. */
static int lzw_reset(machine_t *m, uint16_t bits_at, uint16_t table)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7179)) return 0;
    const uint16_t ds = c->seg[S_DS];
    mem_write8(c, phys(ds, bits_at), 9);
    ds_put(c, (uint16_t)(bits_at + 2), 0x1FF);
    ds_put(c, (uint16_t)(bits_at + 4), 0x100);
    uint16_t bx = (uint16_t)alu_logic(c, 0, 1);
    for (int i = 0; i < 0x800; i++) {
        ds_put(c, (uint16_t)(bx + table), 0xFFFF);
        bx = (uint16_t)alu_add(c, bx, 3, 1, 0);
    }
    uint8_t al = 0;
    bx = (uint16_t)alu_logic(c, 0, 1);
    for (int i = 0; i < 0x100; i++) {
        mem_write8(c, phys(ds, (uint16_t)(bx + table + 2)), al);
        al = (uint8_t)alu_inc(c, al, 0);
        bx = (uint16_t)alu_add(c, bx, 3, 1, 0);
    }
    c->r[R_AX] = (uint16_t)(0xFF00 | al);
    c->r[R_BX] = bx;
    c->r[R_CX] = 0;
    c->r[R_DX] = 0x100;
    c->icount += 7179;
    near_ret(c);
    return 1;
}
static int start_lzw_reset(machine_t *m) { return lzw_reset(m, 0x8D74, 0x7420); }
static int end_lzw_reset(machine_t *m) { return lzw_reset(m, 0x3F8A, 0x2636); }

/* START 0x028CE, startui_cel_start(frame, x, y, count): unless a cel is
 * playing ([B2F4] non-zero, which answers 0), record the position, frame
 * and count and rewind it. */
static int start_cel_start(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 16)) return 0;
    const uint16_t ds = c->seg[S_DS];
    const uint16_t frame = arg(c, 0), x = arg(c, 1), y = arg(c, 2), count = arg(c, 3);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    alu_sub(c, mem_read8(c, phys(ds, 0xB2F4)), 0, 0, 0);
    unsigned n;
    if (!(c->flags & F_ZF)) { c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0); n = 7; }
    else {
        ds_put(c, 0xB300, x);
        ds_put(c, 0xB2FE, y);
        mem_write8(c, phys(ds, 0xB2FA), (uint8_t)frame);
        mem_write8(c, phys(ds, 0xB2F4), (uint8_t)count);
        mem_write8(c, phys(ds, 0xB2EA), 0);
        ds_put(c, 0xB2F0, 0);
        c->r[R_AX] = (uint16_t)((y & 0xFF00) | (count & 0xFF));
        n = 16;
    }
    c->r[R_BP] = cpu_pop16(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* END 0x0185C, widget_queue_dac(a, b, c): append a 6-byte DAC request at
 * 1EE8 + 6 * [1F24] and count it. Kept as written: the slot address goes
 * through a stack local, and AX is the 8-bit MUL's product. */
static int queue_dac(machine_t *m, uint16_t count_at, uint16_t base)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 19)) return 0;
    const uint16_t ds = c->seg[S_DS], ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)(c->r[R_SP] - 2);
    const uint16_t bp = c->r[R_BP];
    set_r8(c, R_AL, 6);
    x86_mul8(c, mem_read8(c, phys(ds, count_at)));
    uint16_t bx = (uint16_t)alu_add(c, c->r[R_AX], base, 1, 0);
    seg_write16(c, ss, (uint16_t)(bp - 2), bx);
    c->r[R_AX] = seg_read16(c, ss, (uint16_t)(bp + 4));
    seg_write16(c, ds, bx, c->r[R_AX]);
    c->r[R_AX] = seg_read16(c, ss, (uint16_t)(bp + 6));
    bx = seg_read16(c, ss, (uint16_t)(bp - 2));
    seg_write16(c, ds, (uint16_t)(bx + 2), c->r[R_AX]);
    c->r[R_AX] = seg_read16(c, ss, (uint16_t)(bp + 8));
    seg_write16(c, ds, (uint16_t)(bx + 4), c->r[R_AX]);
    c->r[R_BX] = bx;
    mem_write8(c, phys(ds, count_at), (uint8_t)alu_inc(c, mem_read8(c, phys(ds, count_at)), 0));
    c->r[R_SP] = bp;
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 19;
    near_ret(c);
    return 1;
}
static int end_queue_dac(machine_t *m) { return queue_dac(m, 0x1F24, 0x1EE8); }
static int start_queue_dac(machine_t *m) { return queue_dac(m, 0x64E2, 0x64A6); }

/* END 0x0452C, widget_distance(x0, y0, x1, y1): the octagonal distance
 * max + min/4 of |dx| and |dy| (NEG on a negative difference). */
static int end_distance(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 17)) return 0;
    const uint16_t x0 = arg(c, 0), y0 = arg(c, 1), x1 = arg(c, 2), y1 = arg(c, 3);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    unsigned n = 15;
    uint16_t ax = (uint16_t)alu_sub(c, x0, x1, 1, 0);
    if (c->flags & F_SF) { ax = (uint16_t)alu_sub(c, 0, ax, 1, 0); n++; }
    uint16_t dx = (uint16_t)alu_sub(c, y0, y1, 1, 0);
    if (c->flags & F_SF) { dx = (uint16_t)alu_sub(c, 0, dx, 1, 0); n++; }
    alu_sub(c, ax, dx, 1, 0);
    if (!x86_cond(c, 0xC)) {                                      /* jl not taken */
        dx = x86_shift(c, 5, dx, 1, 1);
        dx = x86_shift(c, 5, dx, 1, 1);
    } else {
        ax = x86_shift(c, 5, ax, 1, 1);
        ax = x86_shift(c, 5, ax, 1, 1);
    }
    c->r[R_AX] = (uint16_t)alu_add(c, ax, dx, 1, 0);
    c->r[R_DX] = dx;
    c->r[R_BP] = cpu_pop16(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 130D:0217, mclip_publish: write a clipped edge's two y values to
 * the polygon at SI - CX and BP, replaced by the window's top [85FC] or
 * bottom [8600] where the outcodes in AL and AH say the end was clipped -
 * and the pair of side flags (AL bits 3/2, AH bits 3/2) to [SI+2]. */
static int vgame_mclip_publish(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 34)) return 0;
    const uint8_t al = (uint8_t)c->r[R_AX], ah = (uint8_t)(c->r[R_AX] >> 8);
    const uint16_t si = c->r[R_SI];
    unsigned n = 26;
    cpu_push16(c, c->r[R_BX]);
    uint16_t bx = c->r[R_CX];
    alu_logic(c, al & 1, 0); if (al & 1) { bx = ds_get(c, 0x85FC); n++; }
    alu_logic(c, al & 2, 0); if (al & 2) { bx = ds_get(c, 0x8600); n++; }
    ds_put(c, (uint16_t)(si + 6), bx);
    bx = c->r[R_BP];
    alu_logic(c, ah & 1, 0); if (ah & 1) { bx = ds_get(c, 0x85FC); n++; }
    alu_logic(c, ah & 2, 0); if (ah & 2) { bx = ds_get(c, 0x8600); n++; }
    ds_put(c, (uint16_t)(si + 0x0E), bx);
    uint8_t bl = (uint8_t)alu_sub(c, bx & 0xFF, bx & 0xFF, 0, 0);
    alu_logic(c, al & 8, 0); if (al & 8) { bl = (uint8_t)alu_logic(c, bl | 1, 0); n++; }
    alu_logic(c, al & 4, 0); if (al & 4) { bl = (uint8_t)alu_logic(c, bl | 2, 0); n++; }
    alu_logic(c, ah & 8, 0); if (ah & 8) { bl = (uint8_t)alu_logic(c, bl | 4, 0); n++; }
    alu_logic(c, ah & 4, 0); if (ah & 4) { bl = (uint8_t)alu_logic(c, bl | 8, 0); n++; }
    ds_put(c, (uint16_t)(si + 2), (uint16_t)(bl << 8 | bl));
    c->r[R_BX] = cpu_pop16(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 0x0EE9C, the C runtime's signed 32-bit divide: DX:AX = a / b for
 * a = [bp+6]:[bp+4], b = [bp+A]:[bp+8], truncating, the argument slots
 * made positive in place. A 16-bit divisor takes two DIVs; a wider one is
 * shifted down with the dividend until it fits, the quotient estimated with
 * one DIV and corrected by at most one. RET 8. Declines a zero divisor,
 * where the original takes the divide-error interrupt. */
static int vgame_ldiv(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t ss = c->seg[S_SS];
    const uint16_t sp = c->r[R_SP];
    if (!seg_read16(c, ss, (uint16_t)(sp + 6)) && !seg_read16(c, ss, (uint16_t)(sp + 8))) return 0;
    if (!room(c, 64 + 6 * 16)) return 0;
    unsigned n = 0;
    cpu_push16(c, c->r[R_BP]); c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_DI]); cpu_push16(c, c->r[R_SI]); cpu_push16(c, c->r[R_BX]);
    n += 5;
    const uint16_t bp = c->r[R_BP];
#define ARG(o) seg_read16(c, ss, (uint16_t)(bp + (o)))
#define SETARG(o, v) seg_write16(c, ss, (uint16_t)(bp + (o)), (v))
    uint16_t di = (uint16_t)alu_logic(c, 0, 1); n++;              /* xor di, di */
    for (int k = 0; k < 2; k++) {                                 /* make a, then b, positive */
        const int hi = k ? 0x0A : 6, lo = k ? 8 : 4;
        uint16_t ax = ARG(hi);
        alu_logic(c, ax, 1); n += 3;                              /* mov ax / or ax, ax / jge */
        if (c->flags & F_SF) {
            di = (uint16_t)alu_inc(c, di, 1);
            uint16_t dx = ARG(lo);
            ax = (uint16_t)alu_sub(c, 0, ax, 1, 0);               /* neg ax */
            dx = (uint16_t)alu_sub(c, 0, dx, 1, 0);               /* neg dx */
            ax = (uint16_t)alu_sub(c, ax, 0, 1, (c->flags & F_CF) ? 1u : 0u);   /* sbb ax, 0 */
            SETARG(hi, ax);
            SETARG(lo, dx);
            c->r[R_DX] = dx;
            n += 7;
        }
        c->r[R_AX] = ax;
    }
    uint16_t ax = c->r[R_AX];
    alu_logic(c, ax, 1); n += 2;                                  /* or ax, ax / jne */
    uint16_t dx, si;
    if (c->flags & F_ZF) {                                        /* 16-bit divisor */
        const uint16_t cx = ARG(8);
        c->r[R_CX] = cx;
        c->r[R_AX] = ARG(6);
        c->r[R_DX] = (uint16_t)alu_logic(c, 0, 1);
        if (!x86_div16(c, cx)) return 0;                          /* cannot happen: checked above */
        const uint16_t q_hi = c->r[R_AX];
        c->r[R_BX] = q_hi;
        c->r[R_AX] = ARG(4);
        x86_div16(c, cx);
        c->r[R_DX] = q_hi;
        n += 9;                                                   /* mov cx..jmp */
    } else {
        uint16_t bx = ax, cx = ARG(8);
        dx = ARG(6);
        ax = ARG(4);
        n += 4;
        do {
            bx = x86_shift(c, 5, bx, 1, 1);                       /* shr bx, 1 */
            cx = x86_shift(c, 3, cx, 1, 1);                       /* rcr cx, 1 */
            dx = x86_shift(c, 5, dx, 1, 1);
            ax = x86_shift(c, 3, ax, 1, 1);
            alu_logic(c, bx, 1);                                  /* or bx, bx */
            n += 6;
        } while (!(c->flags & F_ZF));
        c->r[R_BX] = bx; c->r[R_CX] = cx; c->r[R_DX] = dx; c->r[R_AX] = ax;
        if (!x86_div16(c, cx)) return 0;                          /* cannot happen after the shifts */
        si = c->r[R_AX];
        x86_mul16(c, ARG(0x0A));                                  /* mul [bp+A] */
        cx = c->r[R_AX];                                          /* xchg cx, ax */
        c->r[R_AX] = ARG(8);
        x86_mul16(c, si);                                         /* mul si */
        c->r[R_DX] = (uint16_t)alu_add(c, c->r[R_DX], cx, 1, 0);
        c->r[R_CX] = cx;
        n += 7 + 1;                                               /* div .. add; jb */
        int dec;
        if (c->flags & F_CF) dec = 1;                             /* jb 0xEF21 */
        else {
            alu_sub(c, c->r[R_DX], ARG(6), 1, 0); n += 2;         /* cmp dx, [bp+6] / ja */
            if (x86_cond(c, 0x7)) dec = 1;
            else {
                n += 1;                                           /* jb 0xEF22 */
                if (c->flags & F_CF) dec = 0;
                else {
                    alu_sub(c, c->r[R_AX], ARG(4), 1, 0); n += 2; /* cmp ax, [bp+4] / jbe */
                    dec = !x86_cond(c, 0x6);
                }
            }
        }
        if (dec) { si = (uint16_t)alu_dec(c, si, 1); n++; }
        c->r[R_DX] = (uint16_t)alu_logic(c, 0, 1);                /* xor dx, dx */
        c->r[R_SI] = c->r[R_AX];                                  /* xchg si, ax */
        c->r[R_AX] = si;
        n += 2;
    }
    di = (uint16_t)alu_dec(c, di, 1); n += 2;                     /* dec di / jne */
    if (c->flags & F_ZF) {                                        /* exactly one negative */
        uint16_t qd = (uint16_t)alu_sub(c, 0, c->r[R_DX], 1, 0);
        const uint16_t qa = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);
        qd = (uint16_t)alu_sub(c, qd, 0, 1, (c->flags & F_CF) ? 1u : 0u);
        c->r[R_DX] = qd;
        c->r[R_AX] = qa;
        n += 3;
    }
    c->r[R_DI] = di;
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    n += 5;
#undef ARG
#undef SETARG
    c->icount += n;
    near_ret(c);
    c->r[R_SP] = (uint16_t)(c->r[R_SP] + 8);
    return 1;
}

/* VGAME 0x048B8, frame_trail: the smoke trail behind the object [3D96]
 * (none when it is -1). Every frame each of the 16 puffs (8 bytes at 3A08:
 * x, y, rise, age) rises: rise += 4, y += rise >> 9. Every eighth frame
 * ([3D8E] & 7 == 0) slot ([3D8E] >> 3) & 15 takes a new puff at the
 * object's position, rise 40h, age 0, [3A88] = that slot, and every other
 * live puff ages by one. Transcribed in the original's order - the loop
 * counters are stack words - and declined when the frame could alias the
 * table (random states only). */
static int vgame_frame_trail(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t ds = c->seg[S_DS], ss = c->seg[S_SS];
    const uint32_t frame_lo = phys(ss, (uint16_t)(c->r[R_SP] - 8)), frame_hi = phys(ss, c->r[R_SP]);
    const uint32_t t_lo = phys(ds, 0x3A08), t_hi = phys(ds, 0x3A8A);
    if (t_lo <= frame_hi + 1 && frame_lo <= t_hi + 1) return 0;
    const uint32_t ctl[] = { phys(ds, 0x3D96), phys(ds, 0x3D8E) };
    for (int i = 0; i < 2; i++) if (ctl[i] + 1 >= frame_lo && ctl[i] <= frame_hi + 1) return 0;
    /* Clocks, counted from the data before anything changes. */
    const uint16_t obj = ds_get(c, 0x3D96);
    const uint8_t tick = mem_read8(c, phys(ds, 0x3D8E));
    unsigned n;
    if (obj == 0xFFFF) n = 8;
    else if (tick & 7) n = 154;
    else {
        const uint16_t slot = (uint16_t)((tick & 0x78) >> 3);
        n = 4 + 1 + 144 + 2 + 20 + 2 + 3;
        for (uint16_t i = 0; i < 16; i++) {
            if (i == slot) n += 6;
            else if (ds_get(c, (uint16_t)(0x3A08 + 8 * i)) == 0) n += 10;   /* x 0: an empty slot */
            else n += 11;
        }
    }
    if (!room(c, n)) return 0;
    x86_enter(c, 4, 0);
    cpu_push16(c, c->r[R_SI]);
    const uint16_t bp = c->r[R_BP];
#define LOCAL(o) seg_read16(c, ss, (uint16_t)(bp - (o)))
#define SETLOCAL(o, v) seg_write16(c, ss, (uint16_t)(bp - (o)), (v))
    alu_sub(c, ds_get(c, 0x3D96), 0xFFFF, 1, 0);                  /* cmp [3D96], -1 */
    if (!(c->flags & F_ZF)) {
        SETLOCAL(2, 0);
        do {                                                      /* every puff rises */
            uint16_t bx = x86_shift(c, 4, LOCAL(2), 3, 1);
            ds_put(c, (uint16_t)(bx + 0x3A0C), (uint16_t)alu_add(c, ds_get(c, (uint16_t)(bx + 0x3A0C)), 4, 1, 0));
            uint16_t ax = x86_shift(c, 7, ds_get(c, (uint16_t)(bx + 0x3A0C)), 9, 1);
            ds_put(c, (uint16_t)(bx + 0x3A0A), (uint16_t)alu_add(c, ds_get(c, (uint16_t)(bx + 0x3A0A)), ax, 1, 0));
            c->r[R_BX] = bx; c->r[R_AX] = ax;
            SETLOCAL(2, (uint16_t)alu_inc(c, LOCAL(2), 1));
            alu_sub(c, LOCAL(2), 0x10, 1, 0);
        } while (x86_cond(c, 0xC));
        alu_logic(c, mem_read8(c, phys(ds, 0x3D8E)) & 7, 0);      /* test byte [3D8E], 7 */
        if (c->flags & F_ZF) {                                    /* a new puff */
            uint16_t bx = x86_shift(c, 4, ds_get(c, 0x3D96), 4, 1);
            uint16_t ax = ds_get(c, (uint16_t)(bx - 0x4D30));
            const uint16_t cx = bx;
            c->r[R_CX] = cx;
            bx = (uint16_t)((bx & 0xFF00) | mem_read8(c, phys(ds, 0x3D8E)));
            bx = (uint16_t)alu_logic(c, bx & 0x78, 1);
            bx = x86_shift(c, 7, bx, 3, 1);
            SETLOCAL(4, bx);
            bx = x86_shift(c, 4, bx, 3, 1);
            ds_put(c, (uint16_t)(bx + 0x3A08), ax);
            c->r[R_SI] = cx;
            ax = ds_get(c, (uint16_t)(cx - 0x4D2E));
            ds_put(c, (uint16_t)(bx + 0x3A0A), ax);
            ds_put(c, (uint16_t)(bx + 0x3A0C), 0x40);
            ax = LOCAL(4);
            ds_put(c, 0x3A88, ax);
            ax = (uint16_t)alu_sub(c, ax, ax, 1, 0);
            ds_put(c, (uint16_t)(bx + 0x3A0E), ax);
            SETLOCAL(2, ax);
            for (;;) {                                            /* the other live puffs age */
                alu_sub(c, LOCAL(2), 0x10, 1, 0);
                if (!x86_cond(c, 0xC)) break;
                ax = LOCAL(2);
                alu_sub(c, LOCAL(4), ax, 1, 0);
                if (!(c->flags & F_ZF)) {
                    bx = x86_shift(c, 4, ax, 3, 1);
                    alu_sub(c, ds_get(c, (uint16_t)(bx + 0x3A08)), 0, 1, 0);
                    if (!(c->flags & F_ZF))
                        ds_put(c, (uint16_t)(bx + 0x3A0E), (uint16_t)alu_inc(c, ds_get(c, (uint16_t)(bx + 0x3A0E)), 1));
                }
                SETLOCAL(2, (uint16_t)alu_inc(c, LOCAL(2), 1));
            }
            c->r[R_AX] = ax; c->r[R_BX] = bx;
        }
    }
#undef LOCAL
#undef SETLOCAL
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* START 0x039B3 / 0x039D5: order the word pair at p and p+4 (unsigned),
 * larger first or smaller first, swapping through a stack local. */
static int order_pair(machine_t *m, int larger_first)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 15)) return 0;
    const uint16_t ds = c->seg[S_DS], ss = c->seg[S_SS];
    const uint16_t p = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)(c->r[R_SP] - 2);
    const uint16_t bp = c->r[R_BP];
    c->r[R_BX] = p;
    uint16_t ax = seg_read16(c, ds, p);
    alu_sub(c, seg_read16(c, ds, (uint16_t)(p + 4)), ax, 1, 0);
    const int keep = larger_first ? x86_cond(c, 0x6) : !(c->flags & F_CF);   /* jbe / jae */
    unsigned n = 10;
    if (!keep) {
        seg_write16(c, ss, (uint16_t)(bp - 2), ax);
        ax = seg_read16(c, ds, (uint16_t)(p + 4));
        seg_write16(c, ds, p, ax);
        ax = seg_read16(c, ss, (uint16_t)(bp - 2));
        seg_write16(c, ds, (uint16_t)(p + 4), ax);
        n = 15;
    }
    c->r[R_AX] = ax;
    c->r[R_SP] = bp;
    c->r[R_BP] = cpu_pop16(c);
    c->icount += n;
    near_ret(c);
    return 1;
}
static int start_order_desc(machine_t *m) { return order_pair(m, 1); }
static int start_order_asc(machine_t *m) { return order_pair(m, 0); }

/* START 0x0851A: copy 20 words from a far pointer to DS:6982. */
static int load_record(machine_t *m, uint16_t dst)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 33)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    const uint16_t bp = c->r[R_BP];
    cpu_push16(c, c->seg[S_ES]);
    cpu_push16(c, c->seg[S_DS]);
    cpu_push16(c, c->seg[S_DS]);                                  /* push ds / pop es */
    c->seg[S_ES] = cpu_pop16(c);
    c->r[R_SI] = seg_read16(c, ss, (uint16_t)(bp + 4));           /* lds si, [bp+4] */
    c->seg[S_DS] = seg_read16(c, ss, (uint16_t)(bp + 6));
    c->r[R_CX] = 0x14;
    c->r[R_DI] = dst;
    rep_string(c, STR_MOVS, 1, c->seg[S_DS], 0);
    c->seg[S_DS] = cpu_pop16(c);
    c->seg[S_ES] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 33;
    near_ret(c);
    return 1;
}
static int start_load_record(machine_t *m) { return load_record(m, 0x6982); }
static int end_load_record(machine_t *m) { return load_record(m, 0x23C4); }

/* START 0x08530: AX = byte [69B6] for a zero argument, else [69B7]. */
static int pick_byte(machine_t *m, uint16_t zero_at)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 9)) return 0;
    const uint16_t v = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    alu_logic(c, v & 0xFF, 0);                                    /* test byte [bp+4], FFh */
    const int zero = (c->flags & F_ZF) != 0;
    c->r[R_AX] = mem_read8(c, phys(c->seg[S_DS], zero ? zero_at : (uint16_t)(zero_at + 1)));
    c->r[R_BP] = cpu_pop16(c);
    c->icount += zero ? 9 : 8;
    near_ret(c);
    return 1;
}
static int start_pick_byte(machine_t *m) { return pick_byte(m, 0x69B6); }
static int end_pick_byte(machine_t *m) { return pick_byte(m, 0x23F8); }

/* START 0x08B3E memset(p, 0, n) and 0x08B6C memcpy(src, dst, n) within DS,
 * by REP STOSB / MOVSB; DI (and SI, ES) preserved. */
static int start_zero_bytes(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t p = arg(c, 0), n = arg(c, 1);
    if (!room(c, 13 + (n ? n : 1u))) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->seg[S_ES]);
    c->seg[S_ES] = c->seg[S_DS];
    c->r[R_AX] = c->seg[S_DS];
    c->r[R_DI] = p;
    c->r[R_CX] = n;
    set_r8(c, R_AL, (uint8_t)alu_logic(c, 0, 0));                 /* xor al, al */
    rep_string(c, STR_STOS, 0, 0, 0);
    c->seg[S_ES] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 13 + (n ? n : 1u);
    near_ret(c);
    return 1;
}

static int start_copy_bytes(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t src = arg(c, 0), dst = arg(c, 1), n = arg(c, 2);
    if (!room(c, 15 + (n ? n : 1u))) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->seg[S_ES]);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_AX] = c->seg[S_DS];
    c->seg[S_ES] = c->seg[S_DS];
    c->r[R_SI] = src;
    c->r[R_DI] = dst;
    c->r[R_CX] = n;
    rep_string(c, STR_MOVS, 0, c->seg[S_DS], 0);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->seg[S_ES] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 15 + (n ? n : 1u);
    near_ret(c);
    return 1;
}

/* START 0x08C15, fixed-point multiply: (a * b) >> 14, rounded by the next
 * bit; BX = SP. */
static int start_fixmul(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 9)) return 0;
    const uint16_t a = arg(c, 0), b = arg(c, 1);
    c->r[R_BX] = c->r[R_SP];
    c->r[R_AX] = a;
    x86_imul16(c, b);
    uint16_t ax = x86_shift(c, 4, c->r[R_AX], 1, 1);
    uint16_t dx = x86_shift(c, 2, c->r[R_DX], 1, 1);              /* rcl dx, 1 */
    ax = x86_shift(c, 4, ax, 1, 1);
    dx = (uint16_t)alu_add(c, dx, 0, 1, (c->flags & F_CF) ? 1u : 0u);   /* adc dx, 0 */
    c->r[R_DX] = dx;
    c->r[R_AX] = dx;
    (void)ax;
    c->icount += 9;
    near_ret(c);
    return 1;
}

/* START 0x0A110 / 0x0A118: the formatter's argument fetchers - the next
 * word (or double word, low in AX) from the list pointer in the caller's
 * frame at [BP+8], which is advanced. */
static int start_next_word(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 4)) return 0;
    const uint16_t ss = c->seg[S_SS], bp = c->r[R_BP];
    c->r[R_SI] = seg_read16(c, ss, (uint16_t)(bp + 8));
    x86_lods(c, 1, c->seg[S_DS]);
    seg_write16(c, ss, (uint16_t)(bp + 8), c->r[R_SI]);
    c->icount += 4;
    near_ret(c);
    return 1;
}

static int start_next_dword(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    const uint16_t ss = c->seg[S_SS], bp = c->r[R_BP];
    c->r[R_SI] = seg_read16(c, ss, (uint16_t)(bp + 8));
    x86_lods(c, 1, c->seg[S_DS]);
    const uint16_t lo = c->r[R_AX];
    x86_lods(c, 1, c->seg[S_DS]);
    c->r[R_DX] = c->r[R_AX];                                      /* mov dx, ax ... xchg */
    c->r[R_AX] = lo;
    seg_write16(c, ss, (uint16_t)(bp + 8), c->r[R_SI]);
    c->icount += 7;
    near_ret(c);
    return 1;
}

/* END 0x00ECD / 0x00EE0: the report's bar scaling, (v << 7) / 146 and
 * (v << 7) / 195, unsigned. */
static int report_scale(machine_t *m, uint16_t divisor)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 10)) return 0;
    const uint16_t v = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    set_r8(c, R_CL, 7);
    c->r[R_AX] = x86_shift(c, 4, v, 7, 1);                        /* shl ax, cl */
    c->r[R_CX] = divisor;
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, divisor);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 10;
    near_ret(c);
    return 1;
}
static int end_scale_146(machine_t *m) { return report_scale(m, 0x92); }
static int end_scale_195(machine_t *m) { return report_scale(m, 0xC3); }

/* END 0x000A1: the terrain class under the replayed aircraft - the far
 * pointer at [7222] holds its record, whose x and y (words 74h and 76h)
 * >> 11 index the 16-wide map at 55EA; low two bits to [6440] and AX. */
static int end_terrain_class(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 16)) return 0;
    const uint16_t ds = c->seg[S_DS];
    cpu_push16(c, c->r[R_SI]);
    set_r8(c, R_CL, 0x0B);
    const uint16_t bx = ds_get(c, 0x7222), es = ds_get(c, 0x7224);
    c->seg[S_ES] = es;
    uint16_t si = x86_shift(c, 5, seg_read16(c, es, (uint16_t)(bx + 0x76)), 0x0B, 1);
    set_r8(c, R_CL, 4);
    si = x86_shift(c, 4, si, 4, 1);
    set_r8(c, R_CL, 0x0B);
    const uint16_t x = x86_shift(c, 5, seg_read16(c, es, (uint16_t)(bx + 0x74)), 0x0B, 1);
    c->r[R_BX] = x;
    uint8_t al = (uint8_t)alu_logic(c, mem_read8(c, phys(ds, (uint16_t)(x + si + 0x55EA))) & 3, 0);
    mem_write8(c, phys(ds, 0x6440), al);
    alu_sub(c, c->r[R_AX] >> 8, c->r[R_AX] >> 8, 0, 0);          /* sub ah, ah */
    c->r[R_AX] = al;
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 16;
    near_ret(c);
    return 1;
}

/* END 0x04137: refill the 512-byte buffer at DS:1C77 from the replay data
 * (far pointer kept in the stack segment at 1ED9/1EDB), advancing it; AX =
 * 200h, the bytes delivered. */
static int end_refill(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 273)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->seg[S_DS]);
    cpu_push16(c, c->seg[S_ES]);
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    c->r[R_AX] = c->seg[S_DS];
    c->seg[S_ES] = c->seg[S_DS];
    c->seg[S_DS] = seg_read16(c, ss, 0x1ED9);
    c->r[R_CX] = 0x100;
    c->r[R_SI] = seg_read16(c, ss, 0x1EDB);
    c->r[R_DI] = 0x1C77;
    rep_string(c, STR_MOVS, 1, c->seg[S_DS], 0);
    seg_write16(c, ss, 0x1EDB, (uint16_t)alu_add(c, seg_read16(c, ss, 0x1EDB), 0x200, 1, 0));
    c->r[R_AX] = 0x200;
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->seg[S_ES] = cpu_pop16(c);
    c->seg[S_DS] = cpu_pop16(c);
    c->icount += 273;
    near_ret(c);
    return 1;
}

/* END 0x04A40: as VGAME's span-table clear, into the ES the caller set,
 * for rows [4198]..[419A] of the tables at 419C (to FFFF) and 4354 (to 0). */
static int spans_reset_es(machine_t *m, uint16_t first_at, uint16_t left, uint16_t right)
{
    cpu_t *c = &m->cpu;
    const uint16_t first = ds_get(c, first_at);
    if (first & 0x8000) {
        if (!room(c, 4)) return 0;
        c->r[R_DI] = (uint16_t)alu_logic(c, first, 1);
        c->icount += 4;
        near_ret(c);
        return 1;
    }
    const uint16_t count = (uint16_t)(ds_get(c, (uint16_t)(first_at + 2)) + 1 - first);
    const unsigned r = count ? count : 1;
    if (!room(c, 18 + 2 * r)) return 0;
    alu_logic(c, first, 1);
    uint16_t cx = (uint16_t)alu_inc(c, ds_get(c, (uint16_t)(first_at + 2)), 1);
    cx = (uint16_t)alu_sub(c, cx, first, 1, 0);
    const uint16_t di2 = x86_shift(c, 4, first, 1, 1);
    c->r[R_BX] = cx;
    c->r[R_DX] = di2;
    c->r[R_DI] = (uint16_t)alu_add(c, di2, left, 1, 0);
    c->r[R_AX] = 0xFFFF;
    c->r[R_CX] = cx;
    for (unsigned i = 0; i < count; i++) x86_stos(c, 1);
    c->r[R_CX] = 0;
    ds_put(c, first_at, 0xFFFF);
    c->r[R_CX] = cx;
    c->r[R_DI] = (uint16_t)alu_add(c, di2, right, 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0xFFFF, 0xFFFF, 1, 0);
    for (unsigned i = 0; i < count; i++) x86_stos(c, 1);
    c->r[R_CX] = 0;
    ds_put(c, (uint16_t)(first_at + 2), 0);
    c->icount += 18 + 2 * r;
    near_ret(c);
    return 1;
}
static int end_spans_reset(machine_t *m) { return spans_reset_es(m, 0x4198, 0x419C, 0x4354); }
static int start_spans_reset(machine_t *m) { return spans_reset_es(m, 0x8F82, 0x8F86, 0x913E); }

static int end_set_word_pair(machine_t *m) { return set_word_pair(m, 0x5158); }

/* START 0x03E20 / END 0x01FE5: start the pointer widget on the screen
 * segment seg: [base] = seg, the two bytes at seg:0 and seg:2 to base+0Ah
 * and +0Bh, 2 to +0Ch and +0Dh, and the four position words at base+2..+8
 * cleared. The far pointer goes through two stack locals, as written. */
static int widget_init(machine_t *m, uint16_t base)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 23)) return 0;
    const uint16_t ds = c->seg[S_DS], ss = c->seg[S_SS];
    const uint16_t seg = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)(c->r[R_SP] - 4);
    const uint16_t bp = c->r[R_BP];
    seg_write16(c, ss, (uint16_t)(bp - 2), seg);
    seg_write16(c, ss, (uint16_t)(bp - 4), 0);
    ds_put(c, base, seg);
    const uint16_t bx = seg_read16(c, ss, (uint16_t)(bp - 4));    /* les bx, [bp-4] */
    c->seg[S_ES] = seg_read16(c, ss, (uint16_t)(bp - 2));
    mem_write8(c, phys(ds, (uint16_t)(base + 0x0A)), mem_read8(c, phys(c->seg[S_ES], bx)));
    mem_write8(c, phys(ds, (uint16_t)(base + 0x0B)), mem_read8(c, phys(c->seg[S_ES], (uint16_t)(bx + 2))));
    mem_write8(c, phys(ds, (uint16_t)(base + 0x0D)), 2);
    mem_write8(c, phys(ds, (uint16_t)(base + 0x0C)), 2);
    c->r[R_AX] = (uint16_t)alu_sub(c, 2, 2, 1, 0);                /* sub ax, ax */
    ds_put(c, (uint16_t)(base + 8), 0);
    ds_put(c, (uint16_t)(base + 4), 0);
    ds_put(c, (uint16_t)(base + 6), 0);
    ds_put(c, (uint16_t)(base + 2), 0);
    c->r[R_BX] = bx;
    c->r[R_SP] = bp;
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 23;
    near_ret(c);
    return 1;
}
static int start_widget_init(machine_t *m) { return widget_init(m, 0xE088); }
static int end_widget_init(machine_t *m) { return widget_init(m, 0x7212); }

/* START 0x0963E, memset(p, value, n) within DS: a byte to word-align, REP
 * STOSW, then the odd byte; returns p (AX), BX the fill word. */
static int start_fill(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (c->flags & F_DF) return 0;
    const uint16_t p = arg(c, 0), v = arg(c, 1), n = arg(c, 2);
    unsigned clocks = 9 + 4;
    if (n) {
        uint16_t k = n;
        clocks += 4;
        if (p & 1) { clocks += 2; k--; }
        clocks += 1 + ((k >> 1) ? (k >> 1) : 1) + 1 + 1;
    }
    if (!room(c, clocks)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    const uint16_t di = c->r[R_DI];
    c->r[R_DX] = di;
    c->seg[S_ES] = c->seg[S_DS];
    c->r[R_AX] = c->seg[S_DS];
    c->r[R_DI] = p;
    c->r[R_BX] = p;
    c->r[R_CX] = n;
    if (n) {
        c->r[R_AX] = (uint16_t)((v & 0xFF) << 8 | (v & 0xFF));
        alu_logic(c, p & 1, 1);                                   /* test di, 1 */
        if (!(c->flags & F_ZF)) {
            x86_stos(c, 0);
            c->r[R_CX] = (uint16_t)alu_dec(c, c->r[R_CX], 1);
        }
        c->r[R_CX] = x86_shift(c, 5, c->r[R_CX], 1, 1);
        rep_string(c, STR_STOS, 1, 0, 0);
        c->r[R_CX] = (uint16_t)alu_add(c, c->r[R_CX], c->r[R_CX], 1, (c->flags & F_CF) ? 1u : 0u);
        rep_string(c, STR_STOS, 0, 0, 0);
    }
    c->r[R_DI] = di;
    const uint16_t t = c->r[R_BX]; c->r[R_BX] = c->r[R_AX]; c->r[R_AX] = t;   /* xchg bx, ax */
    c->r[R_BP] = cpu_pop16(c);
    c->icount += clocks;
    near_ret(c);
    return 1;
}

/* A byte as a routine will see it after its prologue has pushed n bytes
 * starting at stack_lo (the pushed bytes in `over`), for counting ahead. */
static uint8_t peek_over(cpu_t *c, uint16_t seg, uint16_t off, uint32_t stack_lo, const uint8_t *over, unsigned n)
{
    const uint32_t a = phys(seg, off);
    return a - stack_lo < n ? over[a - stack_lo] : mem_read8(c, a);
}

/* VGAME 0x0F06C / START 0x09848: look an id up in a table of (word id,
 * zero-terminated string) entries ending in FFFFh; AX = the string, or 0.
 * Stepped with the interpreter's LODSW/SCASB (DF honoured); its clocks are
 * counted first by walking the same entries. RET 2. */
static int string_lookup(machine_t *m, uint16_t table)
{
    cpu_t *c = &m->cpu;
    const uint16_t ds = c->seg[S_DS];
    const uint16_t id = arg(c, 0);
    const int up = !(c->flags & F_DF);
    /* The prologue pushes BP, SI, DI and DS (8 bytes below SP) before the
     * walk; when the table overlaps them the walk reads the pushed words,
     * so the count reads through them too. */
    uint8_t pushed[8];
    const uint16_t pv[4] = { c->seg[S_DS], c->r[R_DI], c->r[R_SI], c->r[R_BP] };
    for (int i = 0; i < 4; i++) { pushed[2 * i] = (uint8_t)pv[i]; pushed[2 * i + 1] = (uint8_t)(pv[i] >> 8); }
    const uint32_t stack_lo = phys(c->seg[S_SS], (uint16_t)(c->r[R_SP] - 8));
#define PEEK(off) peek_over(c, ds, (uint16_t)(off), stack_lo, pushed, 8)
    uint16_t si = table;
    unsigned clocks = 8, entries = 0;
    for (;;) {
        const uint16_t w = (uint16_t)(PEEK(si) | PEEK((uint16_t)(si + 1)) << 8);
        si = (uint16_t)(si + (up ? 2 : -2));
        clocks += 3;
        if (w == id) break;
        clocks += 3;
        if (w == 0xFFFF) break;
        uint16_t di = si;                                         /* the scan starts after the id */
        unsigned k = 0;
        for (;;) {
            const uint8_t b = PEEK(di);
            di = (uint16_t)(di + (up ? 1 : -1));
            k++;
            if (!b || k == 0xFFFF) break;
        }
        clocks += 3 + k + 2;                                      /* xchg, xor, mov cx; scan; mov si, jmp */
        si = di;
        if (++entries > 4096) return 0;
    }
    clocks += 6;
#undef PEEK
    if (!room(c, clocks)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, ds);
    c->seg[S_ES] = cpu_pop16(c);
    c->r[R_DX] = id;
    c->r[R_SI] = table;
    for (;;) {
        x86_lods(c, 1, ds);
        alu_sub(c, c->r[R_AX], id, 1, 0);
        if (c->flags & F_ZF) break;
        c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
        const uint16_t t = c->r[R_SI]; c->r[R_SI] = c->r[R_AX]; c->r[R_AX] = t;   /* xchg si, ax */
        if (c->flags & F_ZF) break;
        c->r[R_DI] = c->r[R_AX];                                  /* xchg di, ax (AX's old DI is dropped) */
        c->r[R_AX] = (uint16_t)alu_logic(c, 0, 1);
        c->r[R_CX] = 0xFFFF;
        rep_string(c, STR_SCAS, 0, 0, 1);
        c->r[R_SI] = c->r[R_DI];
    }
    { const uint16_t t = c->r[R_SI]; c->r[R_SI] = c->r[R_AX]; c->r[R_AX] = t; }
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
    c->icount += clocks;
    near_ret(c);
    c->r[R_SP] = (uint16_t)(c->r[R_SP] + 2);
    return 1;
}
static int vgame_string_lookup(machine_t *m) { return string_lookup(m, 0x942C); }
static int start_string_lookup(machine_t *m) { return string_lookup(m, 0xB196); }

/* VGAME 0x0FAC1 / START 0x0A913: walk a chain of length-prefixed records
 * (low bit of the length masked) from [BX+8], or [BX+6] when it equals
 * [BX+0Ah], to the FFFEh terminator; SI = the last record before it. */
static int chain_last(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t ds = c->seg[S_DS], bx = c->r[R_BX];
    const int up = !(c->flags & F_DF);
    /* Counted through the DI the routine pushes first: a chain may run
     * over that stack word. */
    const uint32_t stack_lo = phys(c->seg[S_SS], (uint16_t)(c->r[R_SP] - 2));
    const uint8_t pushed[2] = { (uint8_t)c->r[R_DI], (uint8_t)(c->r[R_DI] >> 8) };
#define PEEK16(off) (uint16_t)(peek_over(c, ds, (uint16_t)(off), stack_lo, pushed, 2) | \
                               peek_over(c, ds, (uint16_t)((off) + 1), stack_lo, pushed, 2) << 8)
    uint16_t si = PEEK16(bx + 8);
    unsigned clocks = 4;
    if (si == PEEK16(bx + 0x0A)) { si = PEEK16(bx + 6); clocks++; }
    unsigned steps = 0;
    for (;;) {
        const uint16_t w = PEEK16(si);
        si = (uint16_t)(si + (up ? 2 : -2));
        clocks += 3;
        if (w == 0xFFFE) break;
        si = (uint16_t)(si + (w & 0xFFFE));
        clocks += 4;
        if (++steps > 0x4000) return 0;
    }
#undef PEEK16
    clocks += 5;
    if (!room(c, clocks)) return 0;
    cpu_push16(c, c->r[R_DI]);
    c->r[R_SI] = seg_read16(c, ds, (uint16_t)(bx + 8));
    alu_sub(c, c->r[R_SI], seg_read16(c, ds, (uint16_t)(bx + 0x0A)), 1, 0);
    if (c->flags & F_ZF) c->r[R_SI] = seg_read16(c, ds, (uint16_t)(bx + 6));
    for (;;) {
        x86_lods(c, 1, ds);
        alu_sub(c, c->r[R_AX], 0xFFFE, 1, 0);
        if (c->flags & F_ZF) break;
        c->r[R_DI] = c->r[R_SI];
        set_r8(c, R_AL, (uint8_t)alu_logic(c, c->r[R_AX] & 0xFE, 0));
        c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], c->r[R_AX], 1, 0);
    }
    c->r[R_DI] = (uint16_t)alu_dec(c, c->r[R_DI], 1);
    c->r[R_DI] = (uint16_t)alu_dec(c, c->r[R_DI], 1);
    c->r[R_SI] = c->r[R_DI];
    c->r[R_DI] = cpu_pop16(c);
    c->icount += clocks;
    near_ret(c);
    return 1;
}

/* START 0x0826C: copy palette bank n (0-8, clamped above at 8; below 8 as
 * given) - 768 bytes at the far pointer [D092] + n * 300h - to DS:64E3. */
static int start_palette_bank(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 24 + 768)) return 0;
    const uint16_t ds = c->seg[S_DS];
    uint16_t n = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->seg[S_DS]);
    cpu_push16(c, c->seg[S_ES]);
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, ds);
    c->seg[S_ES] = cpu_pop16(c);
    c->r[R_DI] = 0x64E3;
    c->r[R_SI] = ds_get(c, 0xD092);
    c->seg[S_DS] = ds_get(c, 0xD094);
    unsigned clocks = 13;
    alu_sub(c, n, 8, 1, 0);
    if (!x86_cond(c, 0xC)) { n = 8; clocks++; }                   /* jl keeps n */
    c->r[R_AX] = n;
    c->r[R_BX] = 0x300;
    x86_mul16(c, 0x300);
    c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], c->r[R_AX], 1, 0);
    c->r[R_CX] = 0x300;
    rep_string(c, STR_MOVS, 0, c->seg[S_DS], 0);
    clocks += 4 + 768 + 6;
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->seg[S_ES] = cpu_pop16(c);
    c->seg[S_DS] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += clocks;
    near_ret(c);
    return 1;
}

/* VGAME 0x0EB10 / END 0x05110, strcat(dst, src) within DS: find dst's end
 * and src's length by REPNE SCASB, then the word copy - aligned, unlike the
 * other copies, on the source address. SI and DI restored, AX = dst.
 * Forward only; the scans count through the BP the prologue pushes. */
static int strcat_ds(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (c->flags & F_DF) return 0;
    const uint16_t ds = c->seg[S_DS], dst = arg(c, 0), src = arg(c, 1);
    const uint8_t pushed[2] = { (uint8_t)c->r[R_BP], (uint8_t)(c->r[R_BP] >> 8) };
    const uint32_t stack_lo = phys(c->seg[S_SS], (uint16_t)(c->r[R_SP] - 2));
    unsigned k1 = 0, k2 = 0;
    for (uint16_t d = dst; k1 < 0xFFFF; d++) { k1++; if (!peek_over(c, ds, d, stack_lo, pushed, 2)) break; }
    for (uint16_t d = src; k2 < 0xFFFF; d++) { k2++; if (!peek_over(c, ds, d, stack_lo, pushed, 2)) break; }
    unsigned n = k2, clocks = 9 + k1 + 3 + k2 + 6;
    if (src & 1) { clocks += 2; n--; }
    clocks += 1 + ((n >> 1) ? (n >> 1) : 1) + 1 + 1 + 4;
    if (!room(c, clocks)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    const uint16_t di0 = c->r[R_DI], si0 = c->r[R_SI];
    c->r[R_DX] = di0;
    c->r[R_BX] = si0;
    c->seg[S_ES] = ds;
    c->r[R_DI] = dst;
    c->r[R_AX] = (uint16_t)alu_logic(c, 0, 1);                    /* xor ax, ax */
    c->r[R_CX] = 0xFFFF;
    rep_string(c, STR_SCAS, 0, 0, 1);                             /* repne scasb: dst's end */
    c->r[R_SI] = (uint16_t)(c->r[R_DI] - 1);                      /* lea si, [di-1] */
    c->r[R_DI] = src;
    c->r[R_CX] = 0xFFFF;
    rep_string(c, STR_SCAS, 0, 0, 1);                             /* repne scasb: src's length */
    c->r[R_CX] = (uint16_t)~c->r[R_CX];                           /* not cx */
    c->r[R_DI] = (uint16_t)alu_sub(c, c->r[R_DI], c->r[R_CX], 1, 0);
    { const uint16_t t = c->r[R_SI]; c->r[R_SI] = c->r[R_DI]; c->r[R_DI] = t; }   /* xchg si, di */
    c->r[R_AX] = dst;
    alu_logic(c, c->r[R_SI] & 1, 1);                              /* test si, 1 */
    if (!(c->flags & F_ZF)) {
        x86_movs(c, 0, ds);
        c->r[R_CX] = (uint16_t)alu_dec(c, c->r[R_CX], 1);
    }
    c->r[R_CX] = x86_shift(c, 5, c->r[R_CX], 1, 1);
    rep_string(c, STR_MOVS, 1, ds, 0);
    c->r[R_CX] = (uint16_t)alu_add(c, c->r[R_CX], c->r[R_CX], 1, (c->flags & F_CF) ? 1u : 0u);
    rep_string(c, STR_MOVS, 0, ds, 0);
    c->r[R_SI] = si0;
    c->r[R_DI] = di0;
    c->r[R_BP] = cpu_pop16(c);
    c->icount += clocks;
    near_ret(c);
    return 1;
}

/* VGAME 0x0EFB8, the C runtime's unsigned 32-bit divide: DX:AX = a / b for
 * a = [bp+6]:[bp+4], b = [bp+A]:[bp+8]; two DIVs for a 16-bit divisor, else
 * the divisor (CX:BX here) and dividend shifted down until it fits, one DIV
 * and a correction by at most one. RET 8; BX and SI preserved. A zero
 * divisor is declined. */
static int vgame_uldiv(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t alo = arg(c, 0), ahi = arg(c, 1), blo = arg(c, 2), bhi = arg(c, 3);
    if (!blo && !bhi) return 0;
    if (!room(c, 4 + 3 + 9 + 4 + 6 * 16 + 14 + 4)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]); c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_BX]); cpu_push16(c, c->r[R_SI]);
    const uint16_t bp = c->r[R_BP];
    unsigned n = 4 + 3;
    c->r[R_AX] = bhi;
    alu_logic(c, bhi, 1);                                         /* or ax, ax */
    if (c->flags & F_ZF) {
        c->r[R_CX] = blo;
        c->r[R_AX] = ahi;
        c->r[R_DX] = (uint16_t)alu_logic(c, 0, 1);
        x86_div16(c, blo);
        const uint16_t q_hi = c->r[R_AX];
        c->r[R_BX] = q_hi;
        c->r[R_AX] = alo;
        x86_div16(c, blo);
        c->r[R_DX] = q_hi;
        n += 9;
    } else {
        uint16_t cx = bhi, bx = blo, dx = ahi, ax = alo;
        n += 4;
        do {
            cx = x86_shift(c, 5, cx, 1, 1);
            bx = x86_shift(c, 3, bx, 1, 1);
            dx = x86_shift(c, 5, dx, 1, 1);
            ax = x86_shift(c, 3, ax, 1, 1);
            alu_logic(c, cx, 1);
            n += 6;
        } while (!(c->flags & F_ZF));
        c->r[R_CX] = cx; c->r[R_BX] = bx; c->r[R_DX] = dx; c->r[R_AX] = ax;
        x86_div16(c, bx);
        uint16_t si = c->r[R_AX];
        x86_mul16(c, seg_read16(c, ss, (uint16_t)(bp + 0x0A)));
        cx = c->r[R_AX];                                          /* xchg cx, ax */
        c->r[R_AX] = seg_read16(c, ss, (uint16_t)(bp + 8));
        x86_mul16(c, si);
        c->r[R_DX] = (uint16_t)alu_add(c, c->r[R_DX], cx, 1, 0);
        c->r[R_CX] = cx;
        n += 7 + 1;                                               /* div .. add; jb */
        int dec;
        if (c->flags & F_CF) dec = 1;
        else {
            alu_sub(c, c->r[R_DX], seg_read16(c, ss, (uint16_t)(bp + 6)), 1, 0); n += 2;
            if (x86_cond(c, 0x7)) dec = 1;
            else {
                n += 1;
                if (c->flags & F_CF) dec = 0;
                else {
                    alu_sub(c, c->r[R_AX], seg_read16(c, ss, (uint16_t)(bp + 4)), 1, 0); n += 2;
                    dec = !x86_cond(c, 0x6);
                }
            }
        }
        if (dec) { si = (uint16_t)alu_dec(c, si, 1); n++; }
        c->r[R_DX] = (uint16_t)alu_logic(c, 0, 1);
        c->r[R_SI] = c->r[R_AX];                                  /* xchg si, ax */
        c->r[R_AX] = si;
        n += 2;
    }
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    n += 4;
    c->icount += n;
    near_ret(c);
    c->r[R_SP] = (uint16_t)(c->r[R_SP] + 8);
    return 1;
}

/* VGAME 0x0E530, frame_palette_step(current, target, n): each of n RGB
 * entries of the current palette (far pointer) moves one step toward the
 * target's (far pointer); AX = how many bytes changed. LOOP makes n = 0 mean
 * 65536. Declined when the two palettes or the stack frame overlap, which
 * the game never does, so the clock can be counted from memory first. */
static int vgame_palette_step(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t cur_off = arg(c, 0), cur_seg = arg(c, 1), tgt_off = arg(c, 2), tgt_seg = arg(c, 3), n0 = arg(c, 4);
    const uint32_t count = n0 ? n0 : 0x10000u;
    const uint32_t len = 3 * count;
    if (len > 0x8000) return 0;                                   /* keep the counting simple */
    if ((uint32_t)(cur_off) + len > 0x10000 || (uint32_t)(tgt_off) + len > 0x10000) return 0;
    /* Exact overlap test, wrap at 1 MB included: the current palette's
     * bytes against the target's and the stack frame's. */
    static uint8_t mark[MEM_SIZE / 8];
    memset(mark, 0, sizeof mark);
    for (uint32_t i = 0; i < len; i++) { const uint32_t a = phys(cur_seg, (uint16_t)(cur_off + i)); mark[a >> 3] |= (uint8_t)(1u << (a & 7)); }
    int clash = 0;
    for (uint32_t i = 0; i < len && !clash; i++) { const uint32_t a = phys(tgt_seg, (uint16_t)(tgt_off + i)); clash = (mark[a >> 3] >> (a & 7)) & 1; }
    for (uint32_t i = 0; i < 22 && !clash; i++) { const uint32_t a = phys(c->seg[S_SS], (uint16_t)(c->r[R_SP] - 8 + i)); clash = (mark[a >> 3] >> (a & 7)) & 1; }
    /* The target against the four words pushed before it is read. */
    for (uint32_t i = 0; i < 8 && !clash; i++) {
        const uint32_t s = phys(c->seg[S_SS], (uint16_t)(c->r[R_SP] - 8 + i));
        for (uint32_t j = 0; j < len && !clash; j++) clash = phys(tgt_seg, (uint16_t)(tgt_off + j)) == s;
    }
    if (clash) return 0;
    unsigned clocks = 9 + 6;
    for (uint32_t i = 0; i < len; i++) {
        const uint8_t a = mem_read8(c, phys(cur_seg, (uint16_t)(cur_off + i)));
        const uint8_t b = mem_read8(c, phys(tgt_seg, (uint16_t)(tgt_off + i)));
        clocks += a == b ? 4 : (b > a ? 7 : 8);
        if (i % 3 == 2) clocks += 3;
    }
    if (!room(c, clocks)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->seg[S_DS]);
    c->seg[S_DS] = cur_seg; c->r[R_SI] = cur_off;
    c->seg[S_ES] = tgt_seg; c->r[R_DI] = tgt_off;
    uint16_t bx = (uint16_t)alu_sub(c, c->r[R_BX], c->r[R_BX], 1, 0);
    uint16_t ax = c->r[R_AX];
    for (uint32_t e = 0; e < count; e++) {
        for (int k = 0; k < 3; k++) {
            const uint32_t at = phys(cur_seg, (uint16_t)(c->r[R_SI] + k));
            const uint8_t a = mem_read8(c, at);
            uint8_t step = 1;
            ax = (uint16_t)(1 << 8 | a);
            alu_sub(c, mem_read8(c, phys(tgt_seg, (uint16_t)(c->r[R_DI] + k))), a, 0, 0);
            if (c->flags & F_ZF) continue;
            if (!x86_cond(c, 0x7)) { step = 0xFF; ax = (uint16_t)(0xFF << 8 | a); }
            bx = (uint16_t)alu_inc(c, bx, 1);
            mem_write8(c, at, (uint8_t)alu_add(c, a, step, 0, 0));
        }
        c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], 3, 1, 0);
        c->r[R_DI] = (uint16_t)alu_add(c, c->r[R_DI], 3, 1, 0);
    }
    c->r[R_CX] = 0;
    c->r[R_BX] = bx;
    c->r[R_AX] = bx;
    (void)ax;
    c->seg[S_DS] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += clocks;
    near_ret(c);
    return 1;
}

/* START 0x07387 / END 0x03CA1: point the program's shared-state pointers
 * at the block the shell left in low memory - the segment word at 0000:04F0
 * - with the fixed offsets 04F2/04F4 (START) and 1272. Through two stack
 * locals, as written. */
static int start_shared_pointers(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 19)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)alu_sub(c, c->r[R_SP], 4, 1, 0);   /* sub sp, 4: its flags stand */
    const uint16_t bp = c->r[R_BP];
    ds_put(c, 0xCBDA, 0); ds_put(c, 0xCBD8, 0x4F2);
    ds_put(c, 0xCAC2, 0); ds_put(c, 0xCAC0, 0x4F4);
    seg_write16(c, ss, (uint16_t)(bp - 2), 0);
    seg_write16(c, ss, (uint16_t)(bp - 4), 0x4F0);
    const uint16_t bx = seg_read16(c, ss, (uint16_t)(bp - 4));
    c->seg[S_ES] = seg_read16(c, ss, (uint16_t)(bp - 2));
    c->r[R_BX] = bx;
    ds_put(c, 0xE098, seg_read16(c, c->seg[S_ES], bx)); ds_put(c, 0xE096, 0);
    c->r[R_AX] = seg_read16(c, c->seg[S_ES], bx);
    ds_put(c, 0xCACC, c->r[R_AX]); ds_put(c, 0xCACA, 0x1272);
    c->r[R_SP] = bp;
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 19;
    near_ret(c);
    return 1;
}

static int end_shared_pointers(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 15)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)alu_sub(c, c->r[R_SP], 4, 1, 0);   /* sub sp, 4: its flags stand */
    const uint16_t bp = c->r[R_BP];
    seg_write16(c, ss, (uint16_t)(bp - 2), 0);
    seg_write16(c, ss, (uint16_t)(bp - 4), 0x4F0);
    const uint16_t bx = seg_read16(c, ss, (uint16_t)(bp - 4));
    c->seg[S_ES] = seg_read16(c, ss, (uint16_t)(bp - 2));
    c->r[R_BX] = bx;
    ds_put(c, 0x7224, seg_read16(c, c->seg[S_ES], bx)); ds_put(c, 0x7222, 0);
    c->r[R_AX] = seg_read16(c, c->seg[S_ES], bx);
    ds_put(c, 0x55E0, c->r[R_AX]); ds_put(c, 0x55DE, 0x1272);
    c->r[R_SP] = bp;
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 15;
    near_ret(c);
    return 1;
}

/* START 0x002BA, the home base's risk: the mean of three of the pilot's four
 * skill bytes (the settings structure at far [CACA] points at them through
 * +38h/+3Ch/+3Eh/+40h, bytes +60h/+54h/+58h/+5Ch; the sum of four over 3),
 * less the setting at +42h, plus 2, capped at 10 (unsigned). */
static int start_home_risk(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 33)) return 0;
    const uint16_t ds = c->seg[S_DS], ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)(c->r[R_SP] - 2);
    const uint16_t bp = c->r[R_BP];
    cpu_push16(c, c->r[R_SI]);
    const uint16_t bx = ds_get(c, 0xCACA), es = ds_get(c, 0xCACC);
    c->seg[S_ES] = es;
    c->r[R_BX] = bx;
    static const uint8_t field[4] = { 0x38, 0x3C, 0x3E, 0x40 }, byte[4] = { 0x60, 0x54, 0x58, 0x5C };
    uint16_t si = seg_read16(c, es, (uint16_t)(bx + field[0]));
    uint16_t ax = mem_read8(c, phys(ds, (uint16_t)(si + byte[0])));
    alu_sub(c, c->r[R_AX] >> 8, c->r[R_AX] >> 8, 0, 0);          /* sub ah, ah */
    uint16_t cx = c->r[R_CX];
    for (int k = 1; k < 4; k++) {
        si = seg_read16(c, es, (uint16_t)(bx + field[k]));
        cx = (uint16_t)((cx & 0xFF00) | mem_read8(c, phys(ds, (uint16_t)(si + byte[k]))));
        if (k == 1) cx = (uint16_t)((alu_sub(c, cx >> 8, cx >> 8, 0, 0) << 8) | (cx & 0xFF));   /* sub ch, ch */
        ax = (uint16_t)alu_add(c, ax, cx, 1, 0);
    }
    c->r[R_SI] = si;
    c->r[R_AX] = ax;
    c->r[R_CX] = 3;
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, 3);
    ax = (uint16_t)alu_sub(c, c->r[R_AX], seg_read16(c, es, (uint16_t)(bx + 0x42)), 1, 0);
    ax = (uint16_t)alu_inc(c, ax, 1);
    ax = (uint16_t)alu_inc(c, ax, 1);
    seg_write16(c, ss, (uint16_t)(bp - 2), ax);
    alu_sub(c, ax, 0x0A, 1, 0);
    unsigned n = 32;
    if (!x86_cond(c, 0x6)) { seg_write16(c, ss, (uint16_t)(bp - 2), 0x0A); n = 33; }   /* jbe */
    c->r[R_AX] = seg_read16(c, ss, (uint16_t)(bp - 2));
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_SP] = bp;
    c->r[R_BP] = cpu_pop16(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 1058:0D09, normalise joystick axis SI (0-3): the raw reading
 * [2CCA+2i] against the centre [2CB2+2i] becomes a byte at [2CD2+i]: 7Fh at
 * the centre, below it 0..7Fh scaled by the low range [2CBA+2i], above it
 * 80h..FFh by the high range [2CC2+2i]. A reading past the recorded minimum
 * [2CA2+2i] or maximum [2CAA+2i] widens it (and that range) instead. AX,
 * DX, DS preserved; SI's top bit is lost to SHL/SHR, as shipped. Declined
 * where the original's DIV would fault (random states only). */
static int vgame_axis_normalise(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t ds = c->seg[S_DS];
    const uint16_t i2 = (uint16_t)(c->r[R_SI] << 1);
    const uint16_t raw = ds_get(c, (uint16_t)(i2 + 0x2CCA)), centre = ds_get(c, (uint16_t)(i2 + 0x2CB2));
    const uint16_t diff = (uint16_t)(raw - centre);
    if (raw > centre) {
        if (raw < ds_get(c, (uint16_t)(i2 + 0x2CAA))) {
            const uint16_t d = ds_get(c, (uint16_t)(i2 + 0x2CC2));
            if (!d || diff >= d) return 0;
        }
    } else if (raw < centre && raw > ds_get(c, (uint16_t)(i2 + 0x2CA2))) {
        const uint16_t d = ds_get(c, (uint16_t)(i2 + 0x2CBA));
        const uint16_t neg = (uint16_t)(0 - diff);
        if (!d || neg >= d) return 0;
    }
    if (!room(c, 23)) return 0;
    unsigned n = 0;
    const uint16_t ax0 = c->r[R_AX], dx0 = c->r[R_DX];
    cpu_push16(c, ax0); cpu_push16(c, dx0); cpu_push16(c, ds); n += 3;
    uint16_t si = x86_shift(c, 4, c->r[R_SI], 1, 1); n++;         /* shl si, 1 */
    uint16_t ax = ds_get(c, (uint16_t)(si + 0x2CCA)); n++;
    uint16_t dx = ax; n++;
    ax = (uint16_t)alu_sub(c, ax, ds_get(c, (uint16_t)(si + 0x2CB2)), 1, 0); n++;
    n++;                                                          /* jne */
    if (c->flags & F_ZF) {
        ax = (uint16_t)(0x7F00 | (ax & 0xFF)); n += 2;            /* mov ah, 7Fh / jmp */
    } else if (n++, x86_cond(c, 0x7)) {                           /* ja: above the centre */
        alu_sub(c, dx, ds_get(c, (uint16_t)(si + 0x2CAA)), 1, 0); n += 2;
        if (c->flags & F_CF) {                                    /* jb: within the maximum */
            c->r[R_DX] = ax; c->r[R_AX] = 0;
            alu_sub(c, ax, ax, 1, 0); n += 2;                     /* mov dx, ax / sub ax, ax */
            x86_div16(c, ds_get(c, (uint16_t)(si + 0x2CC2))); n++;
            ax = x86_shift(c, 5, c->r[R_AX], 1, 1); n++;
            ax = (uint16_t)((alu_add(c, ax >> 8, 0x80, 0, 0) << 8) | (ax & 0xFF)); n++;
        } else {
            ds_put(c, (uint16_t)(si + 0x2CAA), dx);
            ds_put(c, (uint16_t)(si + 0x2CC2), ax);
            ax = (uint16_t)~alu_sub(c, ax, ax, 1, 0);
            n += 5;                                               /* mov, mov, sub, not, jmp */
        }
    } else {
        alu_sub(c, dx, ds_get(c, (uint16_t)(si + 0x2CA2)), 1, 0); n += 2;
        if (x86_cond(c, 0x7)) {                                   /* ja: inside the minimum */
            ax = (uint16_t)alu_sub(c, 0, ax, 1, 0);              /* neg ax */
            c->r[R_DX] = ax;
            alu_sub(c, ax, ax, 1, 0);
            c->r[R_AX] = 0;
            n += 3;
            x86_div16(c, ds_get(c, (uint16_t)(si + 0x2CBA))); n++;
            ax = (uint16_t)~c->r[R_AX]; n++;
            ax = x86_shift(c, 5, ax, 1, 1); n++;
        } else {
            ds_put(c, (uint16_t)(si + 0x2CA2), dx);
            ax = (uint16_t)alu_sub(c, 0, ax, 1, 0);
            ds_put(c, (uint16_t)(si + 0x2CBA), ax);
            ax = (uint16_t)alu_sub(c, ax, ax, 1, 0);
            n += 5;                                               /* mov, neg, mov, sub, jmp */
        }
    }
    si = x86_shift(c, 5, si, 1, 1); n++;                          /* shr si, 1 */
    mem_write8(c, phys(ds, (uint16_t)(si + 0x2CD2)), (uint8_t)(ax >> 8)); n++;
    c->r[R_SI] = si;
    c->seg[S_DS] = cpu_pop16(c);
    c->r[R_DX] = cpu_pop16(c);
    c->r[R_AX] = cpu_pop16(c);
    n += 4;                                                       /* pops, ret */
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 120A:02B2, model_setup_matrix's range test (AX = a distance):
 * AX = 0 when the model at [7CD2]:[7CD4] / [7CD6]:[7CD8] (32-bit, |x| and
 * |y| taken) lies within reach - its high words not beyond BP = the high
 * word of [7CDA]:[7CDC] + AX + [7CDC] - and the size test at [7D5E] passes;
 * else AX = FFFFh with ZF clear. */
static int vgame_model_in_range(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 41)) return 0;                                   /* the longest path: 10 + 8 + 2 * 10, then the 3-instruction failure tail */
    unsigned n = 0;
    uint16_t bx = c->r[R_AX]; n++;
    const uint16_t cx = ds_get(c, 0x7D5E); n++;
    uint16_t ax = ds_get(c, 0x7CDC); n++;
    uint16_t dx = (ax & 0x8000) ? 0xFFFF : 0; n++;                /* cwd */
    ax = (uint16_t)alu_logic(c, ax ^ dx, 1); n++;
    ax = (uint16_t)alu_sub(c, ax, dx, 1, 0); n++;
    ax = (uint16_t)alu_inc(c, ax, 1); n++;
    ax = x86_shift(c, 4, ax, 1, 1); n++;
    alu_sub(c, ax, cx, 1, 0); n += 2;                             /* cmp / jae */
    int fail = !(c->flags & F_CF);
    uint16_t bp = c->r[R_BP];
    if (!fail) {
        ax = (uint16_t)alu_add(c, ds_get(c, 0x7CDC), bx, 1, 0); n += 2;
        dx = ax; n++;
        bx = (uint16_t)alu_sub(c, bx, bx, 1, 0); n++;
        bp = dx; n++;
        bx = (uint16_t)alu_add(c, bx, ds_get(c, 0x7CDA), 1, 0); n++;
        bp = (uint16_t)alu_add(c, bp, ds_get(c, 0x7CDC), 1, (c->flags & F_CF) ? 1u : 0u); n++;
        n++;                                                      /* js */
        fail = (c->flags & F_SF) != 0;
        static const uint16_t at[2] = { 0x7CD2, 0x7CD6 };
        for (int k = 0; k < 2 && !fail; k++) {
            ax = ds_get(c, at[k]); dx = ds_get(c, (uint16_t)(at[k] + 2)); n += 2;
            alu_logic(c, dx, 1); n += 2;                          /* or dx, dx / jns */
            if (c->flags & F_SF) {
                ax = (uint16_t)~ax; dx = (uint16_t)~dx;
                ax = (uint16_t)alu_add(c, ax, 1, 1, 0);
                dx = (uint16_t)alu_add(c, dx, 0, 1, (c->flags & F_CF) ? 1u : 0u);
                n += 4;
            }
            alu_sub(c, dx, bp, 1, 0); n += 2;                     /* cmp dx, bp / ja */
            fail = x86_cond(c, 0x7);
        }
    }
    c->r[R_BX] = bx; c->r[R_CX] = cx; c->r[R_DX] = dx; c->r[R_BP] = bp;
    if (fail) { ax = 0xFFFF; alu_logic(c, ax, 1); n += 3; }       /* mov ax, -1 / or / ret */
    else { ax = (uint16_t)alu_sub(c, ax, ax, 1, 0); n += 2; }     /* sub ax, ax / ret */
    c->r[R_AX] = ax;
    c->icount += n;
    near_ret(c);
    return 1;
}

/* ---- Routines that call original code -----------------------------------
 * Pattern: decline at entry if the code up to the first call does not fit;
 * after each call, if what follows does not fit, leave the rest to the
 * original from the instruction after the call (c->ip = that address). */

static void far_ret(cpu_t *c)
{
    c->ip = cpu_pop16(c);
    c->seg[S_CS] = cpu_pop16(c);
}

/* VGAME 104E:0076 / 104E:0066, far sin(a) and cos(a) = sin(a + 4000h): the
 * table sine (104E:008A) on the argument, result in AX (and BX). */
static int far_sine(machine_t *m, uint16_t offset, uint16_t ret_ip)
{
    cpu_t *c = &m->cpu;
    const unsigned pre = offset ? 3 : 2;
    if (!room(c, pre + 1)) return 0;
    c->r[R_BX] = c->r[R_SP];
    uint16_t a = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_SP] + 4));
    if (offset) a = (uint16_t)alu_add(c, a, offset, 1, 0);       /* add bx, 4000h */
    c->r[R_BX] = a;
    c->icount += pre;
    if (!guest_call(m, 0x008A, ret_ip)) return 1;
    if (!room(c, 2)) { c->ip = ret_ip; return 1; }
    c->r[R_AX] = c->r[R_BX];
    c->icount += 2;
    far_ret(c);
    return 1;
}
static int vgame_far_sin(machine_t *m) { return far_sine(m, 0, 0x007F); }
static int vgame_far_cos(machine_t *m) { return far_sine(m, 0x4000, 0x0073); }

/* VGAME 104E:0000, far fixed-point multiply: (a * b) >> 14, rounded by the
 * next bit, from the far frame (a at SP+4, b at SP+6); the same routine as
 * START's near 0x08C15. */
static int vgame_far_fixmul(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 9)) return 0;
    const uint16_t a = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_SP] + 4));
    const uint16_t b = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_SP] + 6));
    c->r[R_BX] = c->r[R_SP];
    c->r[R_AX] = a;
    x86_imul16(c, b);
    uint16_t ax = x86_shift(c, 4, c->r[R_AX], 1, 1);
    uint16_t dx = x86_shift(c, 2, c->r[R_DX], 1, 1);              /* rcl dx, 1 */
    ax = x86_shift(c, 4, ax, 1, 1);
    dx = (uint16_t)alu_add(c, dx, 0, 1, (c->flags & F_CF) ? 1u : 0u);   /* adc dx, 0 */
    c->r[R_DX] = dx;
    c->r[R_AX] = dx;
    (void)ax;
    c->icount += 9;
    far_ret(c);
    return 1;
}

/* VGAME 1452:0316, camera_matrix_copy(src, dst): nine words, src to dst,
 * by REP MOVSW with ES = DS; ES, SI, DI and BP restored. */
static int vgame_camera_matrix_copy(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 24)) return 0;
    const uint16_t ds = c->seg[S_DS], ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->seg[S_ES]);
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    c->r[R_AX] = ds;
    c->seg[S_ES] = ds;
    c->r[R_SI] = seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 6));
    c->r[R_DI] = seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 8));
    c->r[R_CX] = 9;
    const unsigned n = rep_string(c, STR_MOVS, 1, ds, 0);
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->seg[S_ES] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 15 + n;
    far_ret(c);
    return 1;
}

/* VGAME 1452:02E4, camera_matrix_transpose(src, dst): the nine words of a
 * 3x3 matrix, read in order and stored transposed (word k of the source to
 * dst offset 6*(k%3) + 2*(k/3)); AX is left as the last word, SI past the
 * source (LODSW, so it follows DF). */
static int vgame_camera_matrix_transpose(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 28)) return 0;
    const uint16_t ds = c->seg[S_DS], ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    c->r[R_SI] = seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 6));
    const uint16_t di = seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 8));
    static const uint8_t to[9] = { 0, 6, 12, 2, 8, 14, 4, 10, 16 };
    c->r[R_DI] = di;
    for (int k = 0; k < 9; k++) {
        x86_lods(c, 1, ds);                                       /* lodsw: SI steps with DF */
        seg_write16(c, ds, (uint16_t)(di + to[k]), c->r[R_AX]);
    }
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 28;
    far_ret(c);
    return 1;
}

/* START 0x0532A, dist_t(a, b): the distance (0x05413) between two map
 * points kept in 16-byte records at CBE0 (words 0 and 1, x then y) - the
 * difference of x is the second argument pushed, y the first. */
static int start_dist_t(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 15)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    set_r8(c, R_CL, 4);
    uint16_t bx = x86_shift(c, 4, seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 4)), 4, 1);
    uint16_t ax = ds_get(c, (uint16_t)(bx + 0xCBE2));
    uint16_t si = x86_shift(c, 4, seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 6)), 4, 1);
    ax = (uint16_t)alu_sub(c, ax, ds_get(c, (uint16_t)(si + 0xCBE2)), 1, 0);
    cpu_push16(c, ax);
    ax = ds_get(c, (uint16_t)(bx + 0xCBE0));
    ax = (uint16_t)alu_sub(c, ax, ds_get(c, (uint16_t)(si + 0xCBE0)), 1, 0);
    cpu_push16(c, ax);
    c->r[R_BX] = bx; c->r[R_SI] = si; c->r[R_AX] = ax;
    c->icount += 14;
    if (!guest_call(m, 0x5413, 0x534F)) return 1;
    if (!room(c, 6)) { c->ip = 0x534F; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 6;
    near_ret(c);
    return 1;
}

/* START 0x058BE, onc_target(i): 0x058DF on the settings word at far [CACA]+38h
 * and record i's two words (16-byte records at CBE0). */
static int start_onc_target(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 10)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    const uint16_t bx0 = ds_get(c, 0xCACA), es = ds_get(c, 0xCACC);
    c->seg[S_ES] = es;
    c->r[R_BX] = bx0;
    cpu_push16(c, seg_read16(c, es, (uint16_t)(bx0 + 0x38)));
    set_r8(c, R_CL, 4);
    const uint16_t bx = x86_shift(c, 4, seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 4)), 4, 1);
    c->r[R_BX] = bx;
    cpu_push16(c, ds_get(c, (uint16_t)(bx + 0xCBE2)));
    cpu_push16(c, ds_get(c, (uint16_t)(bx + 0xCBE0)));
    c->icount += 9;
    if (!guest_call(m, 0x58DF, 0x58DB)) return 1;
    if (!room(c, 3)) { c->ip = 0x58DB; return 1; }
    x86_leave(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* START 0x03070 / 0x03088, the two ways the fade is started: 0x030A0 with
 * (0x64E3, a, b, arg) where (a, b) is (0, 100h) at 0x03070 and (100h, 0) at
 * 0x03088, the caller's word last. */
static int start_fade_call(machine_t *m, int first_is_100, uint16_t ret)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 10)) return 0;
    const uint16_t arg0 = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, arg0);
    if (first_is_100) {
        c->r[R_AX] = 0x100;
        cpu_push16(c, 0x100);
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
        cpu_push16(c, 0);
    } else {
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
        cpu_push16(c, 0);
        c->r[R_AX] = 0x100;
        cpu_push16(c, 0x100);
    }
    c->r[R_AX] = 0x64E3;
    cpu_push16(c, 0x64E3);
    c->icount += 9;
    if (!guest_call(m, 0x30A0, ret)) return 1;
    if (!room(c, 3)) { c->ip = ret; return 1; }
    x86_leave(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}
static int start_fade_a(machine_t *m) { return start_fade_call(m, 1, 0x3084); }
static int start_fade_b(machine_t *m) { return start_fade_call(m, 0, 0x309C); }

/* START 0x03E5E: when the byte at [73F2] is not zero, 0x076D1 (0). */
static int start_flag_call(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;
    alu_sub(c, mem_read8(c, phys(c->seg[S_DS], 0x73F2)), 0, 0, 0);     /* cmp byte [73F2], 0 */
    if (c->flags & F_ZF) { c->icount += 3; near_ret(c); return 1; }  /* je to ret */
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, 0);
    c->icount += 4;
    if (!guest_call(m, 0x76D1, 0x3E6B)) return 1;
    if (!room(c, 2)) { c->ip = 0x3E6B; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* START 0x03E84, set_pointer(x, y): the pair stored at [E08A]/[E08C]; when
 * bit 1 of byte 72h of the structure at far [E096] is set, 0x085A7 runs. */
static int start_set_pointer(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 13)) return 0;
    const uint16_t x = arg(c, 0), y = arg(c, 1);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = x;
    ds_put(c, 0xE08A, x);
    c->r[R_AX] = y;
    ds_put(c, 0xE08C, y);
    const uint16_t bx = ds_get(c, 0xE096), es = ds_get(c, 0xE098);
    c->r[R_BX] = bx;
    c->seg[S_ES] = es;
    alu_logic(c, mem_read8(c, phys(es, (uint16_t)(bx + 0x72))) & 2, 0);   /* test byte es:[bx+72h], 2 */
    c->icount += 9;
    if (!(c->flags & F_ZF)) {                                     /* je not taken */
        if (!guest_call(m, 0x85A7, 0x3EA1)) return 1;
        if (!room(c, 3)) { c->ip = 0x3EA1; return 1; }
    }
    x86_leave(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* PLAYER 0x00F32, one of two bytes picked by the low byte of the argument: the
 * byte at [19DA] when it is zero, otherwise the one at [19DB]; AH is 0.
 * Flags: the test of the argument byte. */
static int player_pick_byte(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 9)) return 0;
    const uint8_t sel = mem_read8(c, phys(c->seg[S_SS], (uint16_t)(c->r[R_SP] + 2)));   /* [bp+4] */
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);   /* sub ax, ax */
    alu_logic(c, sel & 0xFF, 0);                                  /* test byte [bp+4], 0FFh */
    const int zero = (c->flags & F_ZF) != 0;
    c->r[R_AX] = mem_read8(c, phys(c->seg[S_DS], zero ? 0x19DA : 0x19DB));
    c->r[R_BP] = cpu_pop16(c);
    c->icount += zero ? 9 : 8;
    near_ret(c);
    return 1;
}

/* SETUP 0x01244, copy a zero-terminated string between near pointers: the
 * destination at [bp+4] and the source at [bp+6] are both words on the
 * caller's stack that step one byte a pass, so the caller sees them moved on
 * (past the terminator). BX is the last destination, AL the last byte (0);
 * SI is restored. A pass is 8 instructions. Flags: the OR of the byte. */
static int setup_strcpy_near(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 11)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    c->icount += 3;
    const uint16_t ss = c->seg[S_SS], ds = c->seg[S_DS];
    const uint16_t dst_slot = (uint16_t)(c->r[R_BP] + 4), src_slot = (uint16_t)(c->r[R_BP] + 6);
    for (;;) {
        if (!room(c, 8)) { c->ip = 0x1248; return 1; }
        const uint16_t dst = seg_read16(c, ss, dst_slot);
        c->r[R_BX] = dst;
        seg_write16(c, ss, dst_slot, (uint16_t)alu_inc(c, dst, 1));       /* inc word [bp+4] */
        const uint16_t src = seg_read16(c, ss, src_slot);
        c->r[R_SI] = src;
        seg_write16(c, ss, src_slot, (uint16_t)alu_inc(c, src, 1));       /* inc word [bp+6] */
        const uint8_t b = mem_read8(c, phys(ds, src));
        set_r8(c, R_AL, b);
        mem_write8(c, phys(ds, dst), b);
        alu_logic(c, b, 0);                                       /* or al, al */
        c->icount += 8;
        if (b == 0) break;
    }
    if (!room(c, 3)) { c->ip = 0x125C; return 1; }
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* SETUP 0x0125F, copy a zero-terminated string to a far destination: the
 * destination is the far pointer at [bp+4], the source a near pointer at
 * [bp+8] in DS. SI, DI and ES are restored; AL is the last byte (0). A pass
 * (LODSB, STOSB, OR, JNE) is 4 instructions and goes by the direction flag. */
static int setup_strcpy_far(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 11)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->seg[S_ES]);
    c->r[R_DI] = seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 4));            /* les di, [bp+4] */
    c->seg[S_ES] = seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 6));
    c->r[R_SI] = seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 8));
    c->icount += 7;
    for (;;) {
        if (!room(c, 4)) { c->ip = 0x126B; return 1; }
        x86_lods(c, 0, c->seg[S_DS]);
        x86_stos(c, 0);
        const uint8_t b = get_r8(c, R_AL);
        alu_logic(c, b, 0);                                       /* or al, al */
        c->icount += 4;
        if (b == 0) break;
    }
    if (!room(c, 5)) { c->ip = 0x1271; return 1; }
    c->seg[S_ES] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 5;
    near_ret(c);
    return 1;
}

/* END 0x04B72, copy a zero-terminated string from a far source to ES:DI:
 * DI is the word at [bp+4], the source the far pointer at [bp+6]. SI and DI
 * (and DS, ES) are restored; the last byte copied is the terminator. A pass
 * (MOVSB and a test of the byte just copied) is 3 instructions; the flags
 * are the last test's. */
static int end_strcpy_from_far(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8 + 3)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->seg[S_ES]);
    cpu_push16(c, c->seg[S_DS]);
    c->r[R_SI] = seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 6));            /* lds si, [bp+6] */
    c->seg[S_DS] = seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 8));
    c->r[R_DI] = seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 4));
    c->icount += 8;
    for (;;) {
        if (!room(c, 3)) { c->ip = 0x4B7F; return 1; }
        x86_movs(c, 0, c->seg[S_DS]);
        const uint8_t b = mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_SI] - 1)));
        alu_logic(c, b, 0);                                       /* test byte [si-1], 0FFh */
        c->icount += 3;
        if (b == 0) break;
    }
    if (!room(c, 6)) { c->ip = 0x4B86; return 1; }
    c->seg[S_DS] = cpu_pop16(c);
    c->seg[S_ES] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 6;
    near_ret(c);
    return 1;
}

/* PLAYER 0x00664, the word at a 20-bit linear address given as a long
 * (low word at [bp+4], high at [bp+6]): the segment is the long shifted
 * right four places, the offset its low nibble, and the pair is left in
 * ES:BX. DX is the high word shifted, AX the word read. Flags: the AND of
 * the offset. 22 instructions. */
static int player_linear_word(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 22)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)alu_sub(c, c->r[R_SP], 4, 1, 0);       /* sub sp, 4 */
    const uint16_t bp = c->r[R_BP];
    uint16_t ax = seg_read16(c, ss, (uint16_t)(bp + 4));
    uint16_t dx = seg_read16(c, ss, (uint16_t)(bp + 6));
    for (int i = 0; i < 4; i++) {
        dx = x86_shift(c, 5, dx, 1, 1);                           /* shr dx, 1 */
        ax = x86_shift(c, 3, ax, 1, 1);                           /* rcr ax, 1 */
    }
    c->r[R_DX] = dx;
    seg_write16(c, ss, (uint16_t)(bp - 2), ax);
    ax = (uint16_t)((ax & 0xFF00) | mem_read8(c, phys(ss, (uint16_t)(bp + 4))));   /* mov al, [bp+4] */
    ax = (uint16_t)alu_logic(c, ax & 0xF, 1);                     /* and ax, 0Fh */
    seg_write16(c, ss, (uint16_t)(bp - 4), ax);
    c->r[R_BX] = ax;                                              /* les bx, [bp-4] */
    c->seg[S_ES] = seg_read16(c, ss, (uint16_t)(bp - 2));
    c->r[R_AX] = seg_read16(c, c->seg[S_ES], c->r[R_BX]);
    c->r[R_SP] = bp;
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 22;
    near_ret(c);
    return 1;
}

/* PLAYER 0x0257B, find AL among the six bytes at CS:[24D8..24DD] (searched
 * from the last backwards): found, BX is the matching four-byte record at
 * 1C96 + 4 * index, AX is the index times four and CF is clear; not found,
 * CF is set and BX is left six below its start. CX counts down as the search
 * goes. Flags are the last compare or decrement; a find ends on CLC. */
static int player_find_in_table(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 36)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    uint16_t bx = 0x24DD;
    uint16_t cx = 6;
    const uint8_t al = get_r8(c, R_AL);
    unsigned n = 4;
    int found = 0;
    for (;;) {
        alu_sub(c, mem_read8(c, phys(c->seg[S_CS], bx)), al, 0, 0);   /* cmp cs:[bx], al */
        n += 2;
        if (c->flags & F_ZF) { found = 1; break; }
        bx = (uint16_t)alu_dec(c, bx, 1);
        cx--;
        n += 2;
        if (cx == 0) break;
    }
    if (found) {
        cx = (uint16_t)alu_dec(c, cx, 1);                         /* dec cx */
        uint16_t ax = cx;                                         /* mov ax, cx */
        ax = x86_shift(c, 4, ax, 1, 1);                           /* shl ax, 1 */
        ax = x86_shift(c, 4, ax, 1, 1);
        c->r[R_AX] = ax;
        bx = (uint16_t)alu_add(c, 0x1C96, ax, 1, 0);              /* lea bx, [1C96]; add bx, ax */
        set_flag(c, F_CF, 0);                                     /* clc */
        n += 7;
    } else {
        set_flag(c, F_CF, 1);                                     /* stc */
        n += 2;
    }
    c->r[R_BX] = bx;
    c->r[R_CX] = cx;
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
    c->icount += n + 3;
    near_ret(c);
    return 1;
}

/* The line drawer (START 0x0829A, END 0x04206, DSWAP 0x00790; the three programs carry the
 * same routine): line(surface, x0, y0, x1, y1, colour) draws a Bresenham line
 * of one-byte pixels into the surface whose segment is word `surface` of a
 * table, one row at a time through a table of row offsets, clipping each
 * pixel to 320 columns and `rows` rows. Arguments, near, at [bp+4]..[bp+14]:
 * a pointer to the surface number, then x0, y0, x1, y1 and the colour.
 *
 * The ends are ordered by x (the first compare, signed in START and END,
 * unsigned in DSWAP), the steeper axis is made the major one (AH = 1 when y
 * is), and the pixel loop steps the major axis every pass and the minor one
 * when the running error DX, stepped by the minor length BP and wound back
 * by the major length, crosses zero. Both lengths and the minor step live in
 * three words of the data segment, which stay written. The routine runs
 * pixel by pixel, a pass being at most 27 instructions; when the next pass
 * would not fit before the next event it leaves the loop head as IP with the
 * state of the original there, and the original code carries on. Flags,
 * registers and words are all the original's: BX is the last pixel's offset,
 * CX the count run out (-1), AX the colour with AH the axis flag, DX the
 * error, and SI, DI, ES, BP are restored. */
typedef struct {
    uint16_t segment_table;       /* segment of surface n at [segment_table + 2n] */
    uint16_t row_table;           /* offset of row y at [row_table + 2y] */
    uint16_t step_word;           /* the minor axis's direction, 1 or -1 */
    uint16_t major_word;          /* the major axis's length */
    uint16_t minor_word;          /* the minor axis's length */
    uint16_t rows;                /* rows past which a pixel is clipped */
    int      unsigned_compares;   /* the two ordering compares test unsigned (DSWAP) */
    uint16_t loop_ip;             /* the pixel loop's head */
    uint16_t exit_ip;             /* the first of the five instructions that leave */
} line_variant;

static int line_draw(machine_t *m, const line_variant *v)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 60)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->seg[S_ES]);
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    const uint16_t bp = c->r[R_BP];
    uint16_t bx = seg_read16(c, ss, (uint16_t)(bp + 4));
    bx = ds_get(c, bx);
    bx = x86_shift(c, 4, bx, 1, 1);                               /* shl bx, 1 */
    c->seg[S_ES] = ds_get(c, (uint16_t)(bx + v->segment_table));
    uint16_t ax = seg_read16(c, ss, (uint16_t)(bp + 6));
    bx = seg_read16(c, ss, (uint16_t)(bp + 8));
    uint16_t cx = seg_read16(c, ss, (uint16_t)(bp + 10));
    uint16_t dx = seg_read16(c, ss, (uint16_t)(bp + 12));
    unsigned n = 5 + 4 + 4 + 2;
    alu_sub(c, ax, cx, 1, 0);                                     /* cmp ax, cx */
    if (!x86_cond(c, v->unsigned_compares ? 0x6 : 0xE)) {         /* jle / jbe */
        uint16_t t = cx; cx = ax; ax = t;                         /* xchg cx, ax */
        t = dx; dx = bx; bx = t;                                  /* xchg dx, bx */
        n += 2;
    }
    uint16_t si = ax, di = bx;
    n += 3;                                                       /* mov si; mov di; jne */
    int point = 0;
    if (c->flags & F_ZF) {                                        /* the x ends are equal */
        alu_sub(c, bx, dx, 1, 0);                                 /* cmp bx, dx */
        n += 2;
        if (c->flags & F_ZF) {
            cx = (uint16_t)alu_sub(c, cx, cx, 1, 0);              /* sub cx, cx */
            ax = (uint16_t)((ax & 0xFF00) | mem_read8(c, phys(ss, (uint16_t)(bp + 14))));
            n += 3;
            point = 1;
        }
    }
    uint16_t bpv = bp;                                            /* BP, until the loop */
    if (!point) {
        ds_put(c, v->step_word, 1);
        cx = (uint16_t)alu_sub(c, cx, ax, 1, 0);                  /* sub cx, ax */
        dx = (uint16_t)alu_sub(c, dx, bx, 1, 0);                  /* sub dx, bx */
        n += 4;
        if (x86_cond(c, 0x8)) {                                   /* jns not taken */
            dx = (uint16_t)alu_sub(c, 0, dx, 1, 0);               /* neg dx */
            ds_put(c, v->step_word, (uint16_t)alu_sub(c, 0, ds_get(c, v->step_word), 1, 0));
            n += 2;
        }
        ax = (uint16_t)((ax & 0xFF00) | mem_read8(c, phys(ss, (uint16_t)(bp + 14))));
        ax = (uint16_t)(ax & 0x00FF);                             /* sub ah, ah */
        alu_sub(c, 0, 0, 0, 0);
        alu_sub(c, cx, dx, 1, 0);                                 /* cmp cx, dx */
        n += 4;
        if (!x86_cond(c, v->unsigned_compares ? 0x3 : 0xD)) {     /* jge / jae not taken */
            ax = (uint16_t)(ax | 0x0100);                         /* mov ah, 1 */
            const uint16_t t = dx; dx = cx; cx = t;               /* xchg dx, cx */
            n += 2;
        }
        ds_put(c, v->major_word, cx);
        ds_put(c, v->minor_word, dx);
        bpv = ds_get(c, v->minor_word);
        cx = ds_get(c, v->major_word);
        dx = cx;
        dx = (uint16_t)alu_inc(c, dx, 1);                         /* inc dx */
        dx = x86_shift(c, 5, dx, 1, 1);                           /* shr dx, 1 */
        dx = (uint16_t)alu_sub(c, 0, dx, 1, 0);                   /* neg dx */
        n += 8;
    }
    c->icount += n;
    c->r[R_AX] = ax; c->r[R_BX] = bx; c->r[R_CX] = cx; c->r[R_DX] = dx;
    c->r[R_SI] = si; c->r[R_DI] = di; c->r[R_BP] = bpv;
    for (;;) {
        if (!room(c, 28)) { c->ip = v->loop_ip; return 1; }
        unsigned k = 2;                                           /* cmp si, 0; js */
        alu_sub(c, si, 0, 1, 0);
        int clip = x86_cond(c, 0x8);
        if (!clip) {
            alu_sub(c, si, 0x140, 1, 0); k += 2;                  /* cmp si, 140h; jge */
            clip = x86_cond(c, 0xD);
        }
        if (!clip) {
            alu_sub(c, di, 0, 1, 0); k += 2;                      /* cmp di, 0; js */
            clip = x86_cond(c, 0x8);
        }
        if (!clip) {
            alu_sub(c, di, v->rows, 1, 0); k += 2;                /* cmp di, rows; jge */
            clip = x86_cond(c, 0xD);
        }
        if (!clip) {
            bx = di;
            bx = x86_shift(c, 4, bx, 1, 1);                       /* shl bx, 1 */
            bx = ds_get(c, (uint16_t)(bx + v->row_table));
            bx = (uint16_t)alu_add(c, bx, si, 1, 0);              /* add bx, si */
            mem_write8(c, phys(c->seg[S_ES], bx), (uint8_t)ax);
            k += 5;
        }
        cx = (uint16_t)alu_dec(c, cx, 1);                         /* dec cx */
        k += 2;                                                   /* dec; js */
        if (x86_cond(c, 0x8)) { c->icount += k; break; }
        const int major_y = (ax >> 8) != 0;
        alu_logic(c, (ax >> 8) & 0xFF, 0);                        /* test ah, 0FFh */
        k += 2;                                                   /* test; jne */
        if (!major_y) { si = (uint16_t)alu_inc(c, si, 1); k += 2; }    /* inc si; jmp */
        else          { di = (uint16_t)alu_add(c, di, ds_get(c, v->step_word), 1, 0); k += 1; }
        dx = (uint16_t)alu_add(c, dx, bpv, 1, 0);                 /* add dx, bp */
        k += 2;                                                   /* add; js */
        if (!x86_cond(c, 0x8)) {
            dx = (uint16_t)alu_sub(c, dx, ds_get(c, v->major_word), 1, 0);
            alu_logic(c, (ax >> 8) & 0xFF, 0);                    /* test ah, 0FFh */
            k += 3;                                               /* sub; test; jne */
            if (!major_y) { di = (uint16_t)alu_add(c, di, ds_get(c, v->step_word), 1, 0); k += 2; }   /* add; jmp */
            else          { si = (uint16_t)alu_inc(c, si, 1); k += 1; }
            k += 1;                                               /* jmp to the head */
        }
        c->icount += k;
        c->r[R_AX] = ax; c->r[R_BX] = bx; c->r[R_CX] = cx; c->r[R_DX] = dx;
        c->r[R_SI] = si; c->r[R_DI] = di;
    }
    c->r[R_AX] = ax; c->r[R_BX] = bx; c->r[R_CX] = cx; c->r[R_DX] = dx;
    c->r[R_SI] = si; c->r[R_DI] = di;
    if (!room(c, 5)) { c->ip = v->exit_ip; return 1; }
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->seg[S_ES] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 5;
    near_ret(c);
    return 1;
}
static const line_variant LINE_START = { 0xCAB0, 0x67F2, 0x67EC, 0x67E4, 0x67E6, 0xA8, 0, 0x8305, 0x834D };
static const line_variant LINE_END = { 0x55D0, 0x2234, 0x222E, 0x2226, 0x2228, 0xA8, 0, 0x4271, 0x42B9 };
static const line_variant LINE_DSWAP = { 0x29E0, 0x074C, 0x073E, 0x0736, 0x0738, 0x140, 1, 0x07FB, 0x0843 };
static int start_line(machine_t *m) { return line_draw(m, &LINE_START); }
static int end_line(machine_t *m) { return line_draw(m, &LINE_END); }
static int dswap_line(machine_t *m) { return line_draw(m, &LINE_DSWAP); }

/* The C runtime's stream routines, which every program carries a copy of.
 * A stream (FILE) is 8 bytes - [0] the next byte, [2] the count left, [4] the
 * buffer, [6] flags, [7] the file number - and has a second record in a
 * parallel table `ext` apart from the first by a fixed distance: [ext+0] flags,
 * [ext+2] the buffer size. */
typedef struct {
    uint16_t base;               /* the first stream */
    uint16_t ext;                /* the parallel record of the first stream */
    uint16_t callee;             /* flush a stream (A: the buffer allocator) */
    uint16_t last;               /* B: the word holding the last stream */
    uint16_t flags;              /* C: the table of per-file-number flags */
    uint16_t entry;              /* the routine's address, for the return points */
} crt_stream;

/* 0x022C2 in PLAYER and its copies, getbuf(stream): a buffer of 512 bytes from the allocator
 * (the callee, given 200h), or, if there is none, the one-byte buffer
 * inside the parallel record; the stream gets it, empty. Flags: the OR of the
 * stream flags. */
static int crt_getbuf(machine_t *m, const crt_stream *s)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 6 + 15)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    c->r[R_SI] = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 4));
    c->r[R_AX] = 0x200;
    cpu_push16(c, 0x200);
    c->icount += 6;
    if (!guest_call(m, s->callee, (uint16_t)(s->entry + 0xE))) return 1;
    if (!room(c, 15)) { c->ip = (uint16_t)(s->entry + 0xE); return 1; }
    c->r[R_CX] = cpu_pop16(c);                                    /* pop cx */
    const uint16_t si = c->r[R_SI];                               /* the callee need not keep it */
    uint16_t bx = si;
    bx = (uint16_t)alu_sub(c, bx, s->base, 1, 0);
    bx = (uint16_t)alu_add(c, bx, s->ext, 1, 0);
    c->r[R_BX] = bx;
    uint16_t ax = c->r[R_AX];
    alu_logic(c, ax, 1);                                          /* or ax, ax */
    const uint16_t fl = (uint16_t)(si + 6);
    const uint8_t old = mem_read8(c, phys(c->seg[S_DS], fl));
    if (ax != 0) {
        mem_write8(c, phys(c->seg[S_DS], fl), (uint8_t)alu_logic(c, old | 8, 0));
        ds_put(c, (uint16_t)(bx + 2), 0x200);
    } else {
        mem_write8(c, phys(c->seg[S_DS], fl), (uint8_t)alu_logic(c, old | 4, 0));
        ds_put(c, (uint16_t)(bx + 2), 1);
        ax = (uint16_t)(bx + 1);                                  /* lea ax, [bx+1] */
        c->r[R_AX] = ax;
    }
    ds_put(c, si, ax);
    ds_put(c, (uint16_t)(si + 4), ax);
    ds_put(c, (uint16_t)(si + 2), 0);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 15;
    near_ret(c);
    return 1;
}

/* 0x01D1C in DSWAP and its copies, flush_all(which): every stream that is open
 * for reading or writing (flags & 83h) is flushed through the callee, and a
 * result of -1 marks an error. Returns the number flushed when `which` is 1,
 * otherwise 0 or -1 for the errors. RET 2. */
static int crt_flush_all(machine_t *m, const crt_stream *s)
{
    cpu_t *c = &m->cpu;
    const uint16_t head = (uint16_t)(s->entry + 0x1A), ret_ip = (uint16_t)(s->entry + 0x2A);
    if (!room(c, 9 + 3)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)alu_sub(c, c->r[R_SP], 2, 1, 0);       /* sub sp, 2 */
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_SI] = s->base;
    c->r[R_DI] = (uint16_t)alu_sub(c, c->r[R_DI], c->r[R_DI], 1, 0);   /* sub di, di */
    seg_write16(c, ss, (uint16_t)(c->r[R_BP] - 2), c->r[R_DI]);
    c->icount += 9;                                               /* up to the jump to the head */
    for (;;) {
        if (!room(c, 14)) { c->ip = head; return 1; }
        alu_sub(c, ds_get(c, s->last), c->r[R_SI], 1, 0);         /* cmp [last], si */
        if (c->flags & F_CF) { c->icount += 2; break; }           /* jb: past the last */
        const uint8_t fl = mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_SI] + 6)));
        alu_logic(c, fl & 0x83, 0);                               /* test byte [si+6], 83h */
        unsigned k = 4;                                           /* cmp, jb, test, je */
        if (!(c->flags & F_ZF)) {
            cpu_push16(c, c->r[R_SI]);
            c->icount += k + 1;
            if (!guest_call(m, s->callee, ret_ip)) return 1;
            if (!room(c, 8)) { c->ip = ret_ip; return 1; }
            c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 2, 1, 0);          /* add sp, 2 */
            c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);                /* inc ax */
            k = 2;                                                           /* add, inc */
            if (c->flags & F_ZF) {                                           /* je: it was -1 */
                seg_write16(c, ss, (uint16_t)(c->r[R_BP] - 2), 0xFFFF);
                k += 2;                                                      /* je, mov */
            } else {
                c->r[R_DI] = (uint16_t)alu_inc(c, c->r[R_DI], 1);            /* inc di */
                k += 3;                                                      /* je, inc, jmp */
            }
        }
        c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], 8, 1, 0);   /* add si, 8 */
        c->icount += k + 1;
    }
    if (!room(c, 9)) { c->ip = (uint16_t)(s->entry + 0x34); return 1; }
    alu_sub(c, seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 4)), 1, 1, 0);      /* cmp [bp+4], 1 */
    if (!(c->flags & F_ZF)) {
        c->r[R_AX] = seg_read16(c, ss, (uint16_t)(c->r[R_BP] - 2));
        c->icount += 1;
    } else {
        c->r[R_AX] = c->r[R_DI];
        c->icount += 2;
    }
    c->icount += 2;                                               /* cmp, jne */
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 5;
    c->ip = cpu_pop16(c);
    c->r[R_SP] = (uint16_t)(c->r[R_SP] + 2);                      /* ret 2 */
    return 1;
}

/* 0x0196B in PLAYER and its copies, free_buffer(close, stream): when the stream's parallel
 * record has flag 10h and its file's flag 40h is set, the callee flushes it,
 * and with `close` not zero the record, the stream's count, pointer and buffer
 * are cleared. */
static int crt_free_buffer(machine_t *m, const crt_stream *s)
{
    cpu_t *c = &m->cpu;
    const uint16_t ret_ip = (uint16_t)(s->entry + 0x27);
    if (!room(c, 20)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    uint16_t si = seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 6));
    c->r[R_SI] = si;
    uint16_t di = si;
    di = (uint16_t)alu_sub(c, di, s->base, 1, 0);
    di = (uint16_t)alu_add(c, di, s->ext, 1, 0);
    c->r[R_DI] = di;
    c->icount += 8;
    alu_logic(c, mem_read8(c, phys(c->seg[S_DS], di)) & 0x10, 0); /* test byte [di], 10h */
    c->icount += 2;
    if (!(c->flags & F_ZF)) {
        c->r[R_BX] = (uint16_t)alu_logic(c, 0, 1);                /* xor bx, bx */
        c->r[R_BX] = mem_read8(c, phys(c->seg[S_DS], (uint16_t)(si + 7)));
        alu_logic(c, mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_BX] + s->flags))) & 0x40, 0);
        c->icount += 4;
        if (!(c->flags & F_ZF)) {
            cpu_push16(c, si);
            c->icount += 1;
            if (!guest_call(m, s->callee, ret_ip)) return 1;
            if (!room(c, 3 + 5 + 4)) { c->ip = ret_ip; return 1; }
            c->r[R_AX] = cpu_pop16(c);                            /* pop ax */
            si = c->r[R_SI];                                      /* the callee need not keep SI or DI */
            di = c->r[R_DI];
            alu_sub(c, seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 4)), 0, 1, 0);   /* cmp [bp+4], 0 */
            c->icount += 3;
            if (!(c->flags & F_ZF)) {
                c->r[R_AX] = (uint16_t)alu_logic(c, 0, 1);                /* xor ax, ax */
                mem_write8(c, phys(c->seg[S_DS], di), 0);
                ds_put(c, (uint16_t)(di + 2), 0);
                ds_put(c, si, 0);
                ds_put(c, (uint16_t)(si + 4), 0);
                c->icount += 5;
            }
        }
    }
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}

static const crt_stream vgame_getbuf_S = { 0x92B8, 0x9358, 0xF91A, 0, 0, 0xF5B0 };
static int vgame_getbuf(machine_t *m) { return crt_getbuf(m, &vgame_getbuf_S); }
static const crt_stream start_getbuf_S = { 0xAEA6, 0xAF46, 0xA768, 0, 0, 0xA4DE };
static int start_getbuf(machine_t *m) { return crt_getbuf(m, &start_getbuf_S); }
static const crt_stream end_getbuf_S = { 0x5172, 0x5212, 0x5DAA, 0, 0, 0x5B66 };
static int end_getbuf(machine_t *m) { return crt_getbuf(m, &end_getbuf_S); }
static const crt_stream player_getbuf_S = { 0x1AD2, 0x1B72, 0x222A, 0, 0, 0x22C2 };
static int player_getbuf(machine_t *m) { return crt_getbuf(m, &player_getbuf_S); }
static const crt_stream dswap_getbuf_S = { 0x2710, 0x27B0, 0x2060, 0, 0, 0x1C60 };
static int dswap_getbuf(machine_t *m) { return crt_getbuf(m, &dswap_getbuf_S); }
static const crt_stream dswap_flush_all_S = { 0x2710, 0, 0x1CA2, 0x2850, 0, 0x1D1C };
static int dswap_flush_all(machine_t *m) { return crt_flush_all(m, &dswap_flush_all_S); }
static const crt_stream end_flush_all_S = { 0x5172, 0, 0x5BA8, 0x52B2, 0, 0x5C22 };
static int end_flush_all(machine_t *m) { return crt_flush_all(m, &end_flush_all_S); }
static const crt_stream player_flush_all_S = { 0x1AD2, 0, 0x19AA, 0x1C12, 0, 0x1A24 };
static int player_flush_all(machine_t *m) { return crt_flush_all(m, &player_flush_all_S); }
static const crt_stream start_flush_all_S = { 0xAEA6, 0, 0x9C32, 0xAFE6, 0, 0x9CAC };
static int start_flush_all(machine_t *m) { return crt_flush_all(m, &start_flush_all_S); }
static const crt_stream vgame_flush_all_S = { 0x92B8, 0, 0xF2C6, 0x93F8, 0, 0xF340 };
static int vgame_flush_all(machine_t *m) { return crt_flush_all(m, &vgame_flush_all_S); }
static const crt_stream dswap_free_buffer_S = { 0x2710, 0x27B0, 0x1CA2, 0, 0x26D1, 0x1677 };
static int dswap_free_buffer(machine_t *m) { return crt_free_buffer(m, &dswap_free_buffer_S); }
static const crt_stream end_free_buffer_S = { 0x5172, 0x5212, 0x5BA8, 0, 0x5131, 0x5629 };
static int end_free_buffer(machine_t *m) { return crt_free_buffer(m, &end_free_buffer_S); }
static const crt_stream player_free_buffer_S = { 0x1AD2, 0x1B72, 0x19AA, 0, 0x1A69, 0x196B };
static int player_free_buffer(machine_t *m) { return crt_free_buffer(m, &player_free_buffer_S); }
static const crt_stream start_free_buffer_S = { 0xAEA6, 0xAF46, 0x9C32, 0, 0xAE61, 0x9BF3 };
static int start_free_buffer(machine_t *m) { return crt_free_buffer(m, &start_free_buffer_S); }

/* The runtime start-up check, 0x0115C in DSWAP and its copies in PLAYER and SETUP: the first
 * 66 bytes of the data segment are exclusive-ored together into AH and the
 * result must be 55h. If not, `fail` is called and then `halt`, with 1 pushed (the
 * exit); if the exit returns, AX is 1. On a pass AX is the last byte read
 * (AH is zero). The direction flag is cleared. 198 instructions in the loop. */
typedef struct { uint16_t entry, fail, halt; } crt_check;

static int crt_startup_check(machine_t *m, const crt_check *s)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5 + 3 * 66 + 2 + 2)) return 0;
    cpu_push16(c, c->r[R_SI]);
    c->r[R_SI] = (uint16_t)alu_logic(c, 0, 1);                    /* xor si, si */
    c->r[R_CX] = 0x42;
    set_r8(c, R_AH, (uint8_t)alu_logic(c, 0, 0));                 /* xor ah, ah */
    set_flag(c, F_DF, 0);                                         /* cld */
    for (int i = 0; i < 0x42; i++) {
        x86_lods(c, 0, c->seg[S_DS]);
        set_r8(c, R_AH, (uint8_t)alu_logic(c, get_r8(c, R_AH) ^ get_r8(c, R_AL), 0));   /* xor ah, al */
        c->r[R_CX]--;                                             /* loop: no flags */
    }
    set_r8(c, R_AH, (uint8_t)alu_logic(c, get_r8(c, R_AH) ^ 0x55, 0));   /* xor ah, 55h */
    c->icount += 5 + 3 * 0x42 + 2;
    if (!(c->flags & F_ZF)) {                                     /* je not taken */
        c->icount += 0;
        if (!guest_call(m, s->fail, (uint16_t)(s->entry + 0x16))) return 1;
        if (!room(c, 3)) { c->ip = (uint16_t)(s->entry + 0x16); return 1; }
        c->r[R_AX] = 1;
        cpu_push16(c, 1);
        c->icount += 2;
        if (!guest_call(m, s->halt, (uint16_t)(s->entry + 0x1D))) return 1;
        if (!room(c, 3)) { c->ip = (uint16_t)(s->entry + 0x1D); return 1; }
        c->r[R_AX] = 1;
        c->icount += 1;
    }
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}
static const crt_check DSWAP_CHECK = { 0x115C, 0x1136, 0x13B5 };
static const crt_check PLAYER_CHECK = { 0x15E0, 0x15BA, 0x1839 };
static const crt_check SETUP_CHECK = { 0x1994, 0x196E, 0x1BED };
static int dswap_startup_check(machine_t *m) { return crt_startup_check(m, &DSWAP_CHECK); }
static int player_startup_check(machine_t *m) { return crt_startup_check(m, &PLAYER_CHECK); }
static int setup_startup_check(machine_t *m) { return crt_startup_check(m, &SETUP_CHECK); }

/* END 0x042D0 and START 0x08364: the argument (a word at [bp+4]) goes in DX, with CX
 * 100h, BX 0 and ES made DS, to the callee, which does the work. */
static int crt_es_call(machine_t *m, uint16_t entry, uint16_t callee)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7 + 2)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = c->seg[S_DS];
    c->seg[S_ES] = c->r[R_AX];
    c->r[R_DX] = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 4));
    c->r[R_CX] = 0x100;
    c->r[R_BX] = (uint16_t)alu_logic(c, 0, 1);                    /* xor bx, bx */
    c->icount += 7;
    if (!guest_call(m, callee, (uint16_t)(entry + 0x12))) return 1;
    if (!room(c, 2)) { c->ip = (uint16_t)(entry + 0x12); return 1; }
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}
static int end_es_call(machine_t *m) { return crt_es_call(m, 0x42D0, 0x42E4); }
static int start_es_call(machine_t *m) { return crt_es_call(m, 0x8364, 0x8378); }

/* END 0x011A2 and START 0x02B06, repeat(n): the callee is called n times (n is the
 * word at [bp+4], counted down in place; a count of zero calls nothing). */
static int crt_repeat_call(machine_t *m, uint16_t entry, uint16_t callee)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 3 + 2)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    const uint16_t slot = (uint16_t)(c->r[R_BP] + 4);
    c->icount += 3;                                               /* push, mov, jmp */
    for (;;) {
        if (!room(c, seg_read16(c, c->seg[S_SS], slot) ? 3 : 2)) { c->ip = (uint16_t)(entry + 0xB); return 1; }   /* the CALL too */
        alu_sub(c, seg_read16(c, c->seg[S_SS], slot), 0, 1, 0);   /* cmp word [bp+4], 0 */
        c->icount += 2;                                           /* cmp, jne */
        if (c->flags & F_ZF) break;
        if (!guest_call(m, callee, (uint16_t)(entry + 8))) return 1;
        if (!room(c, 1)) { c->ip = (uint16_t)(entry + 8); return 1; }
        seg_write16(c, c->seg[S_SS], slot, (uint16_t)alu_dec(c, seg_read16(c, c->seg[S_SS], slot), 1));
        c->icount += 1;
    }
    if (!room(c, 3)) { c->ip = (uint16_t)(entry + 0x11); return 1; }
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}
static int end_repeat_call(machine_t *m) { return crt_repeat_call(m, 0x11A2, 0x4393); }
static int start_repeat_call(machine_t *m) { return crt_repeat_call(m, 0x2B06, 0x8427); }

/* END 0x00242 and START 0x0588D, write(buffer, size, count, ...): the product of two arguments
 * (unsigned in END, signed in START) is the byte count; the callee is
 * given the stream's two words in the data segment, the buffer's far
 * pointer and the count, and the first word grows by the count written. The
 * callee's pushes come in a different order in the two programs. */
typedef struct { uint16_t entry, callee, stream, is_signed, pointer_first; } crt_write;

static int crt_write_block(machine_t *m, const crt_write *s)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 14 + 6)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)alu_sub(c, c->r[R_SP], 4, 1, 0);       /* sub sp, 4 */
    const uint16_t bp = c->r[R_BP];
    cpu_push16(c, c->r[R_SI]);
    c->r[R_AX] = seg_read16(c, ss, (uint16_t)(bp + 4));
    seg_write16(c, ss, (uint16_t)(bp - 4), c->r[R_AX]);
    c->r[R_AX] = seg_read16(c, ss, (uint16_t)(bp + 8));
    const uint16_t factor = seg_read16(c, ss, (uint16_t)(bp + 6));
    if (s->is_signed) x86_imul16(c, factor); else x86_mul16(c, factor);
    cpu_push16(c, c->r[R_AX]);
    if (s->pointer_first) {
        cpu_push16(c, ds_get(c, s->stream));
        cpu_push16(c, ds_get(c, (uint16_t)(s->stream + 2)));
        cpu_push16(c, seg_read16(c, ss, (uint16_t)(bp - 4)));
        cpu_push16(c, c->seg[S_DS]);
    } else {
        cpu_push16(c, seg_read16(c, ss, (uint16_t)(bp - 4)));
        cpu_push16(c, c->seg[S_DS]);
        cpu_push16(c, ds_get(c, s->stream));
        cpu_push16(c, ds_get(c, (uint16_t)(s->stream + 2)));
    }
    c->r[R_SI] = c->r[R_AX];
    c->icount += 14;
    if (!guest_call(m, s->callee, (uint16_t)(s->entry + 0x25))) return 1;
    if (!room(c, 6)) { c->ip = (uint16_t)(s->entry + 0x25); return 1; }
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0xA, 1, 0);     /* add sp, 0Ah */
    ds_put(c, s->stream, (uint16_t)alu_add(c, ds_get(c, s->stream), c->r[R_SI], 1, 0));
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 6;
    near_ret(c);
    return 1;
}
static const crt_write END_WRITE = { 0x0242, 0x5272, 0x543C, 0, 0 };
static const crt_write START_WRITE = { 0x588D, 0x95F4, 0xB390, 1, 1 };
static int end_write_block(machine_t *m) { return crt_write_block(m, &END_WRITE); }
static int start_write_block(machine_t *m) { return crt_write_block(m, &START_WRITE); }

/* The C runtime's buffered-output setup for the three standard streams that are
 * not buffered by default (0x09B82 in START and the copies in END, PLAYER and DSWAP),
 * stbuf(stream): for the second, third or fourth stream (BX the word that holds
 * that stream's spare buffer), unless it already has a buffer or flags 0Ch, it is
 * given the spare buffer - or one of 200h bytes from the allocator, kept as the
 * spare - flagged as a temporary buffer (2 and 11h in its second record), and AX is
 * 1; otherwise AX is 0. */
typedef struct {
    uint16_t entry;
    uint16_t stream[3];          /* the three streams this applies to */
    uint16_t spare[3];           /* the word holding each one's spare buffer */
    uint16_t base, ext;          /* the first stream, and its parallel record */
    uint16_t alloc;              /* the allocator */
} crt_stbuf;

static int crt_stbuf_set(machine_t *m, const crt_stbuf *s)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 40)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    c->r[R_SI] = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 4));
    c->icount += 5;
    int ok = 0;
    for (int i = 0; i < 3 && !ok; i++) {
        c->r[R_BX] = s->spare[i];                                 /* mov bx, spare */
        alu_sub(c, c->r[R_SI], s->stream[i], 1, 0);               /* cmp si, stream */
        c->icount += 3;
        if (c->flags & F_ZF) ok = 1;
    }
    uint16_t ax;
    int fail = !ok;
    if (ok) {
        c->r[R_DI] = c->r[R_SI];
        c->r[R_DI] = (uint16_t)alu_sub(c, c->r[R_DI], s->base, 1, 0);
        c->r[R_DI] = (uint16_t)alu_add(c, c->r[R_DI], s->ext, 1, 0);
        alu_logic(c, mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_SI] + 6))) & 0xC, 0);   /* test byte [si+6], 0Ch */
        c->icount += 5;
        if (!(c->flags & F_ZF)) fail = 1;
        else {
            alu_logic(c, mem_read8(c, phys(c->seg[S_DS], c->r[R_DI])) & 1, 0);     /* test byte [di], 1 */
            c->icount += 2;
            if (!(c->flags & F_ZF)) fail = 1;
        }
    }
    int fill = 0;
    if (!fail) {
        ax = ds_get(c, c->r[R_BX]);                               /* mov ax, [bx] */
        c->r[R_AX] = ax;
        alu_logic(c, ax, 1);                                      /* or ax, ax */
        c->icount += 3;
        if (ax != 0) fill = 1;
        else {
            cpu_push16(c, c->r[R_BX]);
            c->r[R_AX] = 0x200;
            cpu_push16(c, 0x200);
            c->icount += 3;
            if (!guest_call(m, s->alloc, (uint16_t)(s->entry + 0x61))) return 1;
            if (!room(c, 25)) { c->ip = (uint16_t)(s->entry + 0x61); return 1; }
            c->r[R_BX] = cpu_pop16(c);                            /* pop bx, pop bx: the first is the 200h */
            c->r[R_BX] = cpu_pop16(c);
            c->icount += 2;
            ax = c->r[R_AX];
            alu_logic(c, ax, 1);                                  /* or ax, ax */
            c->icount += 2;
            if (ax == 0) fail = 1;
            else { ds_put(c, c->r[R_BX], ax); c->icount += 2; fill = 1; }
        }
    }
    if (fill) {
        const uint16_t si = c->r[R_SI], di = c->r[R_DI];
        ax = c->r[R_AX];
        ds_put(c, (uint16_t)(si + 4), ax);
        ds_put(c, si, ax);
        ds_put(c, (uint16_t)(si + 2), 0x200);
        ds_put(c, (uint16_t)(di + 2), 0x200);
        const uint8_t fl = mem_read8(c, phys(c->seg[S_DS], (uint16_t)(si + 6)));
        mem_write8(c, phys(c->seg[S_DS], (uint16_t)(si + 6)), (uint8_t)alu_logic(c, fl | 2, 0));
        mem_write8(c, phys(c->seg[S_DS], di), 0x11);
        c->r[R_AX] = 1;
        c->icount += 8;
    } else {
        c->r[R_AX] = (uint16_t)alu_logic(c, 0, 1);                /* xor ax, ax */
        c->icount += 1;
    }
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}

#define STBUF(P, E, S1, S2, S3, V1, V2, V3, BASE, EXT, ALLOC) \
    static const crt_stbuf P##_STBUF = { E, { S1, S2, S3 }, { V1, V2, V3 }, BASE, EXT, ALLOC }; \
    static int P##_stbuf(machine_t *m) { return crt_stbuf_set(m, &P##_STBUF); }
STBUF(start, 0x9B82, 0xAEAE, 0xAEB6, 0xAEC6, 0xAFE8, 0xAFEA, 0xAFEC, 0xAEA6, 0xAF46, 0xA768)
STBUF(end, 0x55B8, 0x517A, 0x5182, 0x5192, 0x52B4, 0x52B6, 0x52B8, 0x5172, 0x5212, 0x5DAA)
STBUF(player, 0x18FA, 0x1ADA, 0x1AE2, 0x1AF2, 0x1C14, 0x1C16, 0x1C18, 0x1AD2, 0x1B72, 0x222A)
STBUF(dswap, 0x1606, 0x2718, 0x2720, 0x2730, 0x2852, 0x2854, 0x2856, 0x2710, 0x27B0, 0x2060)

/* printf(format, ...): the second stream is given its temporary buffer (stbuf),
 * the formatter is run on it with a pointer to the arguments, and the buffer is
 * released (the flush-and-free routine); the formatter's result comes back. */
typedef struct { uint16_t entry, stbuf, format, release, stream; } crt_printf_t;

static int crt_printf(machine_t *m, const crt_printf_t *s)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7 + 7)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)alu_sub(c, c->r[R_SP], 4, 1, 0);       /* sub sp, 4 */
    const uint16_t bp = c->r[R_BP];
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_SI] = s->stream;
    cpu_push16(c, s->stream);
    c->icount += 7;
    if (!guest_call(m, s->stbuf, (uint16_t)(s->entry + 0xF))) return 1;
    if (!room(c, 7 + 7)) { c->ip = (uint16_t)(s->entry + 0xF); return 1; }
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 2, 1, 0);       /* add sp, 2 */
    c->r[R_DI] = c->r[R_AX];
    c->r[R_AX] = (uint16_t)(bp + 6);                              /* lea ax, [bp+6] */
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, seg_read16(c, ss, (uint16_t)(bp + 4)));
    c->r[R_AX] = s->stream;
    cpu_push16(c, c->r[R_AX]);
    c->icount += 7;
    if (!guest_call(m, s->format, (uint16_t)(s->entry + 0x22))) return 1;
    if (!room(c, 5 + 7)) { c->ip = (uint16_t)(s->entry + 0x22); return 1; }
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);       /* add sp, 6 */
    seg_write16(c, ss, (uint16_t)(bp - 4), c->r[R_AX]);
    c->r[R_AX] = s->stream;
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_DI]);
    c->icount += 5;
    if (!guest_call(m, s->release, (uint16_t)(s->entry + 0x30))) return 1;
    if (!room(c, 7)) { c->ip = (uint16_t)(s->entry + 0x30); return 1; }
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 4, 1, 0);       /* add sp, 4 */
    c->r[R_AX] = seg_read16(c, ss, (uint16_t)(bp - 4));
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 7;
    near_ret(c);
    return 1;
}
#define PRINTF(P, E, STB, FMT, REL, STREAM) \
    static const crt_printf_t P##_PRINTF = { E, STB, FMT, REL, STREAM }; \
    static int P##_printf(machine_t *m) { return crt_printf(m, &P##_PRINTF); }
PRINTF(start, 0x9430, 0x9B82, 0x9D06, 0x9BF3, 0xAEAE)
PRINTF(end, 0x505A, 0x55B8, 0x5678, 0x5629, 0x517A)
PRINTF(player, 0x131E, 0x18FA, 0x1A7E, 0x196B, 0x1ADA)
PRINTF(dswap, 0x0F42, 0x1606, 0x16C6, 0x1677, 0x2718)

/* fflush(stream): with no stream (0) every stream is flushed (flush_all with 0);
 * otherwise, for a stream open for writing only - or one whose second record has bit 1 -
 * the bytes in its buffer (next minus start, if positive) are written to its
 * file with the low-level write; a short write marks the stream (flag 20h) and
 * the result becomes -1, otherwise 0. The stream is then emptied: next back to
 * the start, the count zero. Returns the result in AX. */
typedef struct { uint16_t entry, base, ext, flush_all, write; } crt_fflush;

static int crt_fflush_stream(machine_t *m, const crt_fflush *s)
{
    cpu_t *c = &m->cpu;
    const uint16_t ss = c->seg[S_SS];
    /* Before each stretch of straight code: room for it, or leave at its first instruction. */
#define NEED(n, off) do { if (!room(c, (n))) { c->ip = (uint16_t)(s->entry + (off)); return 1; } } while (0)
    if (!room(c, 8)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)alu_sub(c, c->r[R_SP], 2, 1, 0);       /* sub sp, 2 */
    const uint16_t bp = c->r[R_BP];
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_DI] = (uint16_t)alu_sub(c, c->r[R_DI], c->r[R_DI], 1, 0);   /* sub di, di */
    alu_sub(c, seg_read16(c, ss, (uint16_t)(bp + 4)), c->r[R_DI], 1, 0);   /* cmp [bp+4], di */
    c->icount += 8;                                               /* the eight up to and with JNE */
    if (c->flags & F_ZF) {                                        /* no stream: flush them all */
        NEED(3, 0xF);                                             /* sub, push and the CALL */
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
        cpu_push16(c, c->r[R_AX]);
        c->icount += 2;
        if (!guest_call(m, s->flush_all, (uint16_t)(s->entry + 0x15))) return 1;
        NEED(6, 0x15);
        c->icount += 1;                                           /* jmp to the exit */
        goto leave;
    }
    NEED(6, 0x18);
    c->r[R_SI] = seg_read16(c, ss, (uint16_t)(bp + 4));
    {
        const uint8_t fl = mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_SI] + 6)));
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | fl);      /* mov al, [si+6] */
        c->r[R_CX] = c->r[R_AX];                                  /* mov cx, ax */
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | alu_logic(c, fl & 3, 0));   /* and al, 3 */
        alu_sub(c, get_r8(c, R_AL), 2, 0, 0);                     /* cmp al, 2 */
        c->icount += 6;
        if (c->flags & F_ZF) {                                    /* jne not taken */
            int open = 0;
            NEED(2, 0x26);
            alu_logic(c, get_r8(c, R_CL) & 8, 0);                 /* test cl, 8 */
            c->icount += 2;
            if (!(c->flags & F_ZF)) open = 1;
            else {
                NEED(4, 0x2B);
                c->r[R_BX] = c->r[R_SI];
                c->r[R_BX] = (uint16_t)alu_sub(c, c->r[R_BX], s->base, 1, 0);
                alu_logic(c, mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_BX] + s->ext))) & 1, 0);   /* test byte [bx+ext], 1 */
                c->icount += 4;
                if (!(c->flags & F_ZF)) open = 1;
            }
            if (open) {
                NEED(5, 0x38);
                c->r[R_AX] = ds_get(c, c->r[R_SI]);
                c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], ds_get(c, (uint16_t)(c->r[R_SI] + 4)), 1, 0);
                seg_write16(c, ss, (uint16_t)(bp - 2), c->r[R_AX]);
                alu_logic(c, c->r[R_AX], 1);                      /* or ax, ax */
                c->icount += 5;
                if (x86_cond(c, 0xE) == 0) {                      /* jle not taken: something to write */
                    NEED(6, 0x44);                                /* five and the CALL */
                    cpu_push16(c, c->r[R_AX]);
                    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_SI] + 4)));
                    set_r8(c, R_CL, mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_SI] + 7))));
                    set_r8(c, R_CH, (uint8_t)alu_sub(c, get_r8(c, R_CH), get_r8(c, R_CH), 0, 0));   /* sub ch, ch */
                    cpu_push16(c, c->r[R_CX]);
                    c->icount += 5;
                    if (!guest_call(m, s->write, (uint16_t)(s->entry + 0x51))) return 1;
                    NEED(3, 0x51);
                    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);       /* add sp, 6 */
                    alu_sub(c, seg_read16(c, ss, (uint16_t)(bp - 2)), c->r[R_AX], 1, 0);   /* cmp [bp-2], ax */
                    c->icount += 3;
                    if (!(c->flags & F_ZF)) {                     /* short write */
                        NEED(2, 0x59);
                        const uint16_t fl6 = (uint16_t)(c->r[R_SI] + 6);
                        mem_write8(c, phys(c->seg[S_DS], fl6), (uint8_t)alu_logic(c, mem_read8(c, phys(c->seg[S_DS], fl6)) | 0x20, 0));
                        c->r[R_DI] = 0xFFFF;
                        c->icount += 2;
                    }
                }
            }
        }
    }
    NEED(4, 0x60);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_SI] + 4));
    ds_put(c, c->r[R_SI], c->r[R_AX]);
    ds_put(c, (uint16_t)(c->r[R_SI] + 2), 0);
    c->r[R_AX] = c->r[R_DI];
    c->icount += 4;
leave:
    NEED(5, 0x6C);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 5;
    near_ret(c);
    return 1;
#undef NEED
}
#define FFLUSH(P, E, BASE, EXT, ALL, WRITE) \
    static const crt_fflush P##_FFLUSH = { E, BASE, EXT, ALL, WRITE }; \
    static int P##_fflush(machine_t *m) { return crt_fflush_stream(m, &P##_FFLUSH); }
FFLUSH(player, 0x19AA, 0x1AD2, 0x1B72, 0x1A24, 0x20EC)
FFLUSH(end, 0x5BA8, 0x5172, 0x5212, 0x5C22, 0x5C6C)
FFLUSH(dswap, 0x1CA2, 0x2710, 0x27B0, 0x1D1C, 0x1F22)
/* The rest of the call-free duplicates. */

/* END 0x054AA, PLAYER 0x01896, DSWAP 0x01412, a character-class lookup: AL is stored in a byte; if AH is not
 * zero it replaces AL, otherwise AL is brought into range (a value of 0x22 or
 * more, or from 0x14 up when the mode byte is under 3, becomes 13h, and 20h to
 * 21h become 5), and the byte at TABLE + AL replaces AL; AL is sign-extended
 * into AX and stored in a word. */
typedef struct { uint16_t store_al, mode, table, store_ax; } crt_class;

static int crt_class_lookup(machine_t *m, const crt_class *s)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 17)) return 0;
    uint8_t al = get_r8(c, R_AL);
    unsigned n = 3;
    mem_write8(c, phys(c->seg[S_DS], s->store_al), al);
    alu_logic(c, get_r8(c, R_AH), 0);                             /* or ah, ah */
    if (get_r8(c, R_AH) != 0) {
        al = get_r8(c, R_AH);                                     /* mov al, ah; jmp */
        n += 2;
    } else {
        int to_limit = 0, big = 0;
        alu_sub(c, mem_read8(c, phys(c->seg[S_DS], s->mode)), 3, 0, 0);       /* cmp byte [mode], 3 */
        n += 2;
        if (c->flags & F_CF) to_limit = 1;                         /* jb */
        else {
            alu_sub(c, al, 0x22, 0, 0);                           /* cmp al, 22h */
            n += 2;
            if (!(c->flags & F_CF)) big = 1;                      /* jae */
            else {
                alu_sub(c, al, 0x20, 0, 0);                       /* cmp al, 20h */
                n += 2;
                if (c->flags & F_CF) to_limit = 1;                /* jb */
                else { al = 5; n += 2; }                          /* mov al, 5; jmp */
            }
        }
        if (to_limit) {
            alu_sub(c, al, 0x13, 0, 0);                           /* cmp al, 13h */
            n += 2;
            if (!(c->flags & F_CF) && !(c->flags & F_ZF)) big = 1;   /* jbe not taken */
        }
        if (big) { al = 0x13; n += 1; }
        c->r[R_BX] = s->table;                                    /* mov bx, table */
        al = mem_read8(c, phys(c->seg[S_DS], (uint16_t)(s->table + al)));   /* xlatb */
        n += 2;
    }
    set_r8(c, R_AL, al);
    c->r[R_AX] = (uint16_t)(int16_t)(int8_t)al;                   /* cbw */
    ds_put(c, s->store_ax, c->r[R_AX]);
    c->icount += n + 3;
    near_ret(c);
    return 1;
}
#define CLASS(P, E, A, B, C, D) \
    static const crt_class P##_CLASS = { A, B, C, D }; \
    static int P##_class_lookup(machine_t *m) { return crt_class_lookup(m, &P##_CLASS); }
CLASS(end, 0x54AA, 0x512D, 0x512A, 0x515E, 0x5122)
CLASS(player, 0x1896, 0x1A65, 0x1A62, 0x1ABE, 0x1A5A)
CLASS(dswap, 0x1412, 0x26CD, 0x26CA, 0x26FC, 0x26C2)

/* VGAME 0x0F38A and START 0x0A1DC, find_free_stream(): the first stream, from the first
 * to the last, with neither read, write nor update open (flags & 83h zero) is
 * cleared - count, flags, next and buffer zero, file number 0FFh - and returned
 * (0 if none). */
typedef struct { uint16_t entry, base, last; } crt_freestream;

static int crt_free_stream(machine_t *m, const crt_freestream *s)
{
    cpu_t *c = &m->cpu;
    const uint16_t ds = c->seg[S_DS];
    const uint16_t head = (uint16_t)(s->entry + 0xD), done = (uint16_t)(s->entry + 0x2F);
    if (!room(c, 5 + 2)) return 0;
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_SI] = s->base;
    c->r[R_DI] = (uint16_t)alu_sub(c, c->r[R_DI], c->r[R_DI], 1, 0);   /* sub di, di */
    c->icount += 5;
    for (;;) {
        /* Decide before touching a flag: the stretch's length depends on the data. */
        const uint16_t si = c->r[R_SI];
        const int past = ds_get(c, s->last) < si;
        const int taken = !past && (mem_read8(c, phys(ds, (uint16_t)(si + 6))) & 0x83) == 0;
        if (!room(c, past ? 2 : taken ? 4 + 7 : 5)) { c->ip = head; return 1; }
        alu_sub(c, ds_get(c, s->last), si, 1, 0);                 /* cmp [last], si */
        if (past) { c->icount += 2; break; }                      /* jb */
        alu_logic(c, mem_read8(c, phys(ds, (uint16_t)(si + 6))) & 0x83, 0);   /* test byte [si+6], 83h */
        c->icount += 4;                                           /* cmp, jb, test, jne */
        if (taken) {
            ds_put(c, (uint16_t)(si + 2), 0);
            mem_write8(c, phys(ds, (uint16_t)(si + 6)), 0);
            c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);   /* sub ax, ax */
            ds_put(c, (uint16_t)(si + 4), 0);
            ds_put(c, si, 0);
            mem_write8(c, phys(ds, (uint16_t)(si + 7)), 0xFF);
            c->r[R_DI] = si;
            c->icount += 7;
            break;
        }
        c->r[R_SI] = (uint16_t)alu_add(c, si, 8, 1, 0);           /* add si, 8 */
        c->icount += 1;
    }
    if (!room(c, 4)) { c->ip = done; return 1; }
    c->r[R_AX] = c->r[R_DI];
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}
#define FREESTREAM(P, E, BASE, LAST) \
    static const crt_freestream P##_FREESTREAM = { E, BASE, LAST }; \
    static int P##_free_stream(machine_t *m) { return crt_free_stream(m, &P##_FREESTREAM); }
FREESTREAM(vgame, 0xF38A, 0x92B8, 0x93F8)
FREESTREAM(start, 0xA1DC, 0xAEA6, 0xAFE6)

/* START 0x0860E, PLAYER 0x00F6C, DSWAP 0x0085A, SETUP 0x00BA2, copy_table(segment): from
 * the record at the segment (ES) - a slot number at 1Ch, a count at 22h, a
 * word at 18h and the pairs from 24h on - each of count pairs becomes
 * five-byte entry, from entry (slot*5) of a table: the first word at +1 and the
 * record's word at +3. A flag byte in the data segment is cleared first; if the
 * stores have made it non-zero, the routine returns without restoring anything
 * (RET with the five pushes in place). One pass is 6 instructions. */
typedef struct { uint16_t entry, flag, table; } crt_copy_table;

static int crt_copy_table_run(machine_t *m, const crt_copy_table *s)
{
    cpu_t *c = &m->cpu;
    const uint16_t ss = c->seg[S_SS], ds = c->seg[S_DS];
    const uint16_t loop_ip = (uint16_t)(s->entry + 0x38);
    if (!room(c, 22 + 6)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->seg[S_ES]);
    cpu_push16(c, c->seg[S_DS]);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_DX] = seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 4));
    mem_write8(c, phys(ds, s->flag), 0);
    c->seg[S_ES] = c->r[R_DX];
    c->r[R_BX] = s->table;
    c->r[R_DI] = 0x1C;
    c->r[R_AX] = seg_read16(c, c->seg[S_ES], c->r[R_DI]);
    set_r8(c, R_DL, 5);
    x86_mul8(c, 5);                                               /* mul dl */
    c->r[R_BX] = (uint16_t)alu_add(c, c->r[R_BX], c->r[R_AX], 1, 0);
    c->r[R_DI] = 0x22;
    c->r[R_CX] = seg_read16(c, c->seg[S_ES], c->r[R_DI]);
    c->r[R_SI] = 0x24;
    c->r[R_DI] = 0x18;
    c->r[R_DI] = seg_read16(c, c->seg[S_ES], c->r[R_DI]);
    c->icount += 22;
    for (;;) {
        if (!room(c, 6)) { c->ip = loop_ip; return 1; }
        c->r[R_AX] = seg_read16(c, c->seg[S_ES], c->r[R_SI]);
        ds_put(c, (uint16_t)(c->r[R_BX] + 1), c->r[R_AX]);
        ds_put(c, (uint16_t)(c->r[R_BX] + 3), c->r[R_DI]);
        c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], 2, 1, 0);
        c->r[R_BX] = (uint16_t)alu_add(c, c->r[R_BX], 5, 1, 0);
        c->r[R_CX]--;                                             /* loop */
        c->icount += 6;
        if (c->r[R_CX] == 0) break;
    }
    if (!room(c, 2)) { c->ip = (uint16_t)(s->entry + 0x49); return 1; }
    alu_sub(c, mem_read8(c, phys(ds, s->flag)), 0, 0, 0);         /* cmp byte [flag], 0 */
    c->icount += 2;
    if (c->flags & F_ZF) {
        if (!room(c, 8)) { c->ip = (uint16_t)(s->entry + 0x50); return 1; }
        c->r[R_BP] = cpu_pop16(c);
        c->seg[S_DS] = cpu_pop16(c);
        c->seg[S_ES] = cpu_pop16(c);
        c->r[R_SI] = cpu_pop16(c);
        c->r[R_DI] = cpu_pop16(c);
        c->r[R_SP] = c->r[R_BP];
        c->r[R_BP] = cpu_pop16(c);
        c->icount += 7;
    } else if (!room(c, 1)) { c->ip = (uint16_t)(s->entry + 0x58); return 1; }
    c->icount += 1;                                               /* ret */
    near_ret(c);
    return 1;
}
#define COPYTABLE(P, E, FLAG, TABLE) \
    static const crt_copy_table P##_COPYTABLE = { E, FLAG, TABLE }; \
    static int P##_copy_table(machine_t *m) { return crt_copy_table_run(m, &P##_COPYTABLE); }
COPYTABLE(start, 0x860E, 0x73FC, 0x69B8)
COPYTABLE(player, 0x0F6C, 0x19E0, 0x11C0)
COPYTABLE(dswap, 0x085A, 0x0B02, 0x08DC)
COPYTABLE(setup, 0x0BA2, 0x078E, 0x08A6)
/* sprintf(buffer, format, ...) (START 0x0959E, END 0x0521C, DSWAP 0x0107A): a string
 * stream is set up in the data segment at `stream` - the buffer for its next byte
 * and base, a count of 7FFFh, flags 42h - the formatter runs on it, and a zero
 * is put after the output (through the stream, with a flush through the buffer
 * routine if the count has run out). Returns the formatter's result. */
typedef struct { uint16_t entry, stream, format, overflow; } crt_sprintf_t;

static int crt_sprintf(machine_t *m, const crt_sprintf_t *s)
{
    cpu_t *c = &m->cpu;
    const uint16_t ss = c->seg[S_SS];
    if (!room(c, 17)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)alu_sub(c, c->r[R_SP], 2, 1, 0);       /* sub sp, 2 */
    const uint16_t bp = c->r[R_BP];
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    mem_write8(c, phys(c->seg[S_DS], (uint16_t)(s->stream + 6)), 0x42);
    c->r[R_AX] = seg_read16(c, ss, (uint16_t)(bp + 4));
    ds_put(c, (uint16_t)(s->stream + 4), c->r[R_AX]);
    c->r[R_SI] = s->stream;
    ds_put(c, c->r[R_SI], c->r[R_AX]);
    ds_put(c, (uint16_t)(s->stream + 2), 0x7FFF);
    c->r[R_AX] = (uint16_t)(bp + 8);                              /* lea ax, [bp+8] */
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, seg_read16(c, ss, (uint16_t)(bp + 6)));
    c->r[R_AX] = c->r[R_SI];
    cpu_push16(c, c->r[R_AX]);
    c->icount += 16;
    if (!guest_call(m, s->format, (uint16_t)(s->entry + 0x2B))) return 1;
    if (!room(c, 4 + 4 + 6)) { c->ip = (uint16_t)(s->entry + 0x2B); return 1; }
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);       /* add sp, 6 */
    c->r[R_DI] = c->r[R_AX];
    const uint16_t cnt = (uint16_t)(s->stream + 2);
    ds_put(c, cnt, (uint16_t)alu_dec(c, ds_get(c, cnt), 1));
    c->icount += 4;                                               /* add, mov, dec, js */
    if (!x86_cond(c, 0x8)) {                                      /* js not taken: room in the buffer */
        const uint16_t bx = ds_get(c, s->stream);
        c->r[R_BX] = bx;
        ds_put(c, s->stream, (uint16_t)alu_inc(c, ds_get(c, s->stream), 1));
        mem_write8(c, phys(c->seg[S_DS], bx), 0);
        c->icount += 4;                                           /* mov, inc, mov, jmp */
    } else {
        cpu_push16(c, c->r[R_SI]);
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
        cpu_push16(c, c->r[R_AX]);
        c->icount += 3;
        if (!guest_call(m, s->overflow, (uint16_t)(s->entry + 0x4B))) return 1;
        if (!room(c, 1 + 6)) { c->ip = (uint16_t)(s->entry + 0x4B); return 1; }
        c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 4, 1, 0);   /* add sp, 4 */
        c->icount += 1;
    }
    if (!room(c, 6)) { c->ip = (uint16_t)(s->entry + 0x4E); return 1; }
    c->r[R_AX] = c->r[R_DI];
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 6;
    near_ret(c);
    return 1;
}
#define SPRINTF(P, E, STREAM, FORMAT, OVER) \
    static const crt_sprintf_t P##_SPRINTF = { E, STREAM, FORMAT, OVER }; \
    static int P##_sprintf(machine_t *m) { return crt_sprintf(m, &P##_SPRINTF); }
SPRINTF(start, 0x959E, 0xCAA4, 0x9D06, 0x9992)
SPRINTF(end, 0x521C, 0x55C2, 0x5678, 0x54D8)
SPRINTF(dswap, 0x107A, 0x29D8, 0x16C6, 0x1440)
/* Small routines that call one other and little else. Each checks for room before the
 * stretch, the CALL included, and leaves at the stretch's first instruction otherwise. */

/* flush_all(1) as a routine of its own (END 0x05C1A, PLAYER 0x01A1C, VGAME 0x0F338): the
 * callee takes its argument off the stack (RET 2). */
static int crt_flush_all_one(machine_t *m, uint16_t entry, uint16_t callee)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 3)) return 0;
    c->r[R_AX] = 1;
    cpu_push16(c, 1);
    c->icount += 2;
    if (!guest_call_pop(m, callee, (uint16_t)(entry + 7), 2)) return 1;
    if (!room(c, 1)) { c->ip = (uint16_t)(entry + 7); return 1; }
    c->icount += 1;
    near_ret(c);
    return 1;
}
static int end_flush_all_one(machine_t *m) { return crt_flush_all_one(m, 0x5C1A, 0x5C22); }
static int player_flush_all_one(machine_t *m) { return crt_flush_all_one(m, 0x1A1C, 0x1A24); }
static int vgame_flush_all_one(machine_t *m) { return crt_flush_all_one(m, 0xF338, 0xF340); }

/* END 0x015D7 and 0x015EF: the argument, 100h and 0 (in either order) and 1F25h are
 * handed to 0x01607; the pair differ only in the order of the 100h and the 0. */
static int crt_three_args(machine_t *m, uint16_t entry, int zero_first)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 10)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 4)));
    if (zero_first) {
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
        cpu_push16(c, c->r[R_AX]);
        c->r[R_AX] = 0x100;
        cpu_push16(c, 0x100);
    } else {
        c->r[R_AX] = 0x100;
        cpu_push16(c, 0x100);
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
        cpu_push16(c, c->r[R_AX]);
    }
    c->r[R_AX] = 0x1F25;
    cpu_push16(c, 0x1F25);
    c->icount += 9;
    if (!guest_call(m, 0x1607, (uint16_t)(entry + 0x14))) return 1;
    if (!room(c, 3)) { c->ip = (uint16_t)(entry + 0x14); return 1; }
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}
static int end_three_args_a(machine_t *m) { return crt_three_args(m, 0x15D7, 0); }
static int end_three_args_b(machine_t *m) { return crt_three_args(m, 0x15EF, 1); }

/* SETUP 0x00503 and END 0x02023: when the byte at `flag` is not zero, the callee is
 * called with 0. */
static int crt_call_if_flag(machine_t *m, uint16_t entry, uint16_t flag, uint16_t callee)
{
    cpu_t *c = &m->cpu;
    const int set = mem_read8(c, phys(c->seg[S_DS], flag)) != 0;
    if (!room(c, set ? 5 : 3)) return 0;
    alu_sub(c, mem_read8(c, phys(c->seg[S_DS], flag)), 0, 0, 0);  /* cmp byte [flag], 0 */
    c->icount += 2;
    if (set) {
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
        cpu_push16(c, c->r[R_AX]);
        c->icount += 2;
        if (!guest_call(m, callee, (uint16_t)(entry + 0xD))) return 1;
        if (!room(c, 2)) { c->ip = (uint16_t)(entry + 0xD); return 1; }
        c->r[R_BX] = cpu_pop16(c);
        c->icount += 1;
    }
    c->icount += 1;
    near_ret(c);
    return 1;
}
static int setup_call_if_flag(machine_t *m) { return crt_call_if_flag(m, 0x503, 0xAC8, 0x53B); }
static int end_call_if_flag(machine_t *m) { return crt_call_if_flag(m, 0x2023, 0x2608, 0x3F44); }

/* END 0x052DC, rand(): the seed (a long at [5158]) times 343FDh plus 269EC3h, kept, and
 * its high word less the top bit returned. The multiply is the 32-bit multiply, RET 8. */
static int end_rand(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    c->r[R_AX] = 0x43FD;
    c->r[R_DX] = 3;
    cpu_push16(c, 3);
    cpu_push16(c, 0x43FD);
    cpu_push16(c, ds_get(c, 0x515A));
    cpu_push16(c, ds_get(c, 0x5158));
    c->icount += 6;
    if (!guest_call_pop(m, 0x539C, 0x52EF, 8)) return 1;
    if (!room(c, 7)) { c->ip = 0x52EF; return 1; }
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x9EC3, 1, 0);
    c->r[R_DX] = (uint16_t)alu_add(c, c->r[R_DX], 0x26, 1, (c->flags & F_CF) ? 1 : 0);   /* adc dx, 26h */
    ds_put(c, 0x5158, c->r[R_AX]);
    ds_put(c, 0x515A, c->r[R_DX]);
    c->r[R_AX] = c->r[R_DX];
    set_r8(c, R_AH, (uint8_t)alu_logic(c, get_r8(c, R_AH) & 0x7F, 0));   /* and ah, 7Fh */
    c->icount += 7;
    near_ret(c);
    return 1;
}

/* VGAME 0x0462E, the start of release_hold(index) (the index at [bp+4], the counts at 3668h):
 * the count is stepped down; while it was above zero the rest of the routine, which the
 * census holds as another function from 0x04657, is the original's; once it has run out the
 * count is left at zero, the callee is called with 3DD5h and the routine returns. */
static int vgame_release_count(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 11)) return 0;
    x86_enter(c, 6, 0);
    const uint16_t bp = c->r[R_BP];
    seg_write16(c, c->seg[S_SS], (uint16_t)(bp - 4), 0xFFFF);
    uint16_t bx = seg_read16(c, c->seg[S_SS], (uint16_t)(bp + 4));
    bx = x86_shift(c, 4, bx, 1, 1);                               /* shl bx, 1 */
    c->r[R_BX] = bx;
    const uint16_t slot = (uint16_t)(bx + 0x3668);
    c->r[R_AX] = ds_get(c, slot);
    ds_put(c, slot, (uint16_t)alu_dec(c, c->r[R_AX], 1));         /* dec word [bx+3668h] */
    alu_logic(c, c->r[R_AX], 1);                                  /* or ax, ax */
    c->icount += 8;
    if (x86_cond(c, 0xF)) { c->ip = 0x4657; return 1; }           /* jg: the rest of the routine is the original's */
    ds_put(c, slot, 0);
    cpu_push16(c, 0x3DD5);
    c->icount += 2;
    if (!guest_call(m, 0x8A4F, 0x4654)) return 1;
    if (!room(c, 3)) { c->ip = 0x4654; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* START 0x09A72, release_buffer(stream): when the stream is open for reading or writing
 * (flags & 83h) and owns its buffer (flag 8), the buffer is freed (the callee) and
 * the stream loses the flag and its buffer, pointer and count. */
static int start_release_buffer(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 12)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    c->r[R_SI] = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 4));
    const uint8_t fl = mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_SI] + 6)));
    set_r8(c, R_AL, fl);
    alu_logic(c, fl & 0x83, 0);                                   /* test al, 83h */
    c->icount += 7;
    int release = 0;
    if (!(c->flags & F_ZF)) {
        alu_logic(c, fl & 8, 0);                                  /* test al, 8 */
        c->icount += 2;
        if (!(c->flags & F_ZF)) release = 1;
    }
    if (release) {
        cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_SI] + 4)));
        c->icount += 1;
        if (!guest_call(m, 0xA76C, 0x9A8A)) return 1;
        if (!room(c, 9)) { c->ip = 0x9A8A; return 1; }
        c->r[R_CX] = cpu_pop16(c);
        const uint16_t si = c->r[R_SI];
        const uint16_t f6 = (uint16_t)(si + 6);
        mem_write8(c, phys(c->seg[S_DS], f6), (uint8_t)alu_logic(c, mem_read8(c, phys(c->seg[S_DS], f6)) & 0xF7, 0));
        c->r[R_AX] = (uint16_t)alu_logic(c, 0, 1);                /* xor ax, ax */
        ds_put(c, (uint16_t)(si + 4), 0);
        ds_put(c, si, 0);
        ds_put(c, (uint16_t)(si + 2), 0);
        c->icount += 6;
    }
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* END 0x0206A: the error handler - the word at [6D98] is 3, a message built by the
 * callee at 112Ah from the two argument words and a text, and the exit routine is
 * then called with -1. */
static int end_fatal(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 10)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    ds_put(c, 0x6D98, 3);
    cpu_push16(c, seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 6)));
    cpu_push16(c, seg_read16(c, ss, (uint16_t)(c->r[R_BP] + 4)));
    c->r[R_AX] = 0x2DC;
    cpu_push16(c, 0x2DC);
    c->r[R_AX] = 0x6D8C;
    cpu_push16(c, 0x6D8C);
    c->icount += 9;
    if (!guest_call(m, 0x112A, 0x2084)) return 1;
    if (!room(c, 4)) { c->ip = 0x2084; return 1; }
    c->r[R_SP] = c->r[R_BP];
    c->r[R_AX] = 0xFFFF;
    cpu_push16(c, 0xFFFF);
    c->icount += 3;
    if (!guest_call(m, 0x2092, 0x208D)) return 1;
    if (!room(c, 4)) { c->ip = 0x208D; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}

/* PLAYER 0x005D0: two calls of 0x014CA with 1Bh and 23h and a pair of words each. */
static int player_two_calls(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;                                    /* four and the CALL */
    cpu_push16(c, ds_get(c, 0x1E78));
    cpu_push16(c, ds_get(c, 0x1E76));
    c->r[R_AX] = 0x1B;
    cpu_push16(c, 0x1B);
    c->icount += 4;
    if (!guest_call(m, 0x14CA, 0x05DF)) return 1;
    if (!room(c, 6)) { c->ip = 0x05DF; return 1; }                /* five and the CALL */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);       /* add sp, 6 */
    cpu_push16(c, ds_get(c, 0x2090));
    cpu_push16(c, ds_get(c, 0x208E));
    c->r[R_AX] = 0x23;
    cpu_push16(c, 0x23);
    c->icount += 5;
    if (!guest_call(m, 0x14CA, 0x05F1)) return 1;
    if (!room(c, 2)) { c->ip = 0x05F1; return 1; }
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* PLAYER 0x01FE8, DSWAP 0x01C3C and SETUP 0x01C22, alloc_or_die(size): the allocator is called with
 * the size while the word at `slot` holds 400h (the heap's growth step), which is put back
 * afterwards; with no block the failure routine takes over (a jump, AX the step's old value). */
typedef struct { uint16_t entry, slot, alloc, die; } crt_alloc_die;

static int crt_alloc_or_die(machine_t *m, const crt_alloc_die *s)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    cpu_push16(c, c->r[R_BX]);
    cpu_push16(c, c->seg[S_ES]);
    cpu_push16(c, c->r[R_CX]);
    c->r[R_CX] = 0x400;
    {
        const uint16_t old = ds_get(c, s->slot);                  /* xchg [slot], cx */
        ds_put(c, s->slot, c->r[R_CX]);
        c->r[R_CX] = old;
    }
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 7;
    if (!guest_call(m, s->alloc, (uint16_t)(s->entry + 0xF))) return 1;
    if (!room(c, 9)) { c->ip = (uint16_t)(s->entry + 0xF); return 1; }
    c->r[R_BX] = cpu_pop16(c);
    ds_put(c, s->slot, cpu_pop16(c));                             /* pop word [slot] */
    c->r[R_CX] = cpu_pop16(c);
    c->r[R_DX] = c->seg[S_DS];
    alu_logic(c, c->r[R_AX], 1);                                  /* or ax, ax */
    c->icount += 5;
    if (c->r[R_AX] == 0) {                                        /* je: no block */
        c->r[R_AX] = c->r[R_CX];
        c->icount += 3;                                           /* je, mov, jmp */
        c->ip = s->die;
        return 1;
    }
    c->seg[S_ES] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->icount += 4;                                               /* je, pop es, pop bx, ret */
    near_ret(c);
    return 1;
}
#define ALLOCDIE(P, E, SLOT, ALLOC, DIE) \
    static const crt_alloc_die P##_ALLOCDIE = { E, SLOT, ALLOC, DIE }; \
    static int P##_alloc_or_die(machine_t *m) { return crt_alloc_or_die(m, &P##_ALLOCDIE); }
ALLOCDIE(player, 0x1FE8, 0x1C94, 0x222A, 0x115E)
ALLOCDIE(dswap, 0x1C3C, 0x28D4, 0x2060, 0x0D46)
ALLOCDIE(setup, 0x1C22, 0x0EBC, 0x1C46, 0x15DE)

/* END 0x05422, PLAYER 0x0180E and SETUP 0x01BC2, find_message(code): the table of messages is a
 * run of entries - a code word then a zero-ended text - ended by a code of FFFFh. The
 * text of the entry whose code is the argument is returned (0 at the end of the
 * table). ES becomes DS; RET 2. */
typedef struct { uint16_t entry, table; } crt_message;

static int crt_find_message(machine_t *m, const crt_message *s)
{
    cpu_t *c = &m->cpu;
    if (c->flags & F_DF) return 0;
    if (!room(c, 8)) return 0;
    const uint16_t head = (uint16_t)(s->entry + 0xD);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->seg[S_DS]);                                  /* push ds; pop es: the word stays below SP */
    c->seg[S_ES] = cpu_pop16(c);
    c->r[R_DX] = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 4));
    c->r[R_SI] = s->table;
    c->icount += 8;
    for (;;) {
        const uint16_t code = seg_read16(c, c->seg[S_DS], c->r[R_SI]);
        unsigned cost, k = 0;
        if (code == c->r[R_DX]) cost = 3;
        else if ((uint16_t)(code + 1) == 0) cost = 6;
        else {
            for (uint16_t p = (uint16_t)(c->r[R_SI] + 2); k < 0xFFFF; p++) {
                k++;
                if (mem_read8(c, phys(c->seg[S_DS], p)) == 0) break;
            }
            cost = 9 + k + 2;
        }
        if (!room(c, cost)) { c->ip = head; return 1; }
        x86_lods(c, 1, c->seg[S_DS]);                             /* lodsw */
        alu_sub(c, c->r[R_AX], c->r[R_DX], 1, 0);                 /* cmp ax, dx */
        c->icount += 3;                                           /* lodsw, cmp, je */
        if (c->flags & F_ZF) break;
        c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);         /* inc ax */
        { const uint16_t t = c->r[R_SI]; c->r[R_SI] = c->r[R_AX]; c->r[R_AX] = t; }   /* xchg si, ax */
        c->icount += 3;                                           /* inc, xchg, je */
        if (c->flags & F_ZF) break;
        { const uint16_t t = c->r[R_DI]; c->r[R_DI] = c->r[R_AX]; c->r[R_AX] = t; }   /* xchg di, ax */
        c->r[R_AX] = (uint16_t)alu_logic(c, 0, 1);                /* xor ax, ax */
        c->r[R_CX] = 0xFFFF;
        rep_string(c, STR_SCAS, 0, 0, 1);                         /* repne scasb */
        c->r[R_SI] = c->r[R_DI];
        c->icount += 3 + k + 2;                                   /* xchg, xor, mov, the scan, mov, jmp */
    }
    if (!room(c, 6)) { c->ip = (uint16_t)(s->entry + 0x22); return 1; }
    { const uint16_t t = c->r[R_SI]; c->r[R_SI] = c->r[R_AX]; c->r[R_AX] = t; }   /* xchg si, ax */
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
    c->ip = cpu_pop16(c);
    c->r[R_SP] = (uint16_t)(c->r[R_SP] + 2);                      /* ret 2 */
    c->icount += 6;
    return 1;
}
#define MESSAGE(P, E, TABLE) \
    static const crt_message P##_MESSAGE = { E, TABLE }; \
    static int P##_find_message(machine_t *m) { return crt_find_message(m, &P##_MESSAGE); }
MESSAGE(end, 0x5422, 0x535E)
MESSAGE(player, 0x180E, 0x1CDA)
MESSAGE(setup, 0x1BC2, 0x0EE4)
/* The C runtime's stack check (START 0x0A4C6, END 0x05B4E; VGAME's copy at
 * 0x0F8F0 is never run by any route, so it is left to the original): AX
 * bytes are taken off the stack unless that would wrap or pass the limit
 * word at `limit`, in which case the caller's return address is put back, AX
 * is 0 and the overflow handler at `overflow` runs. The caller's address
 * comes off the stack first and is jumped to (JMP CX), so the stack does not
 * come back to where it was. */
static int rt_stack_check(machine_t *m, uint16_t limit, uint16_t overflow)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 9)) return 0;
    const uint16_t cx = cpu_pop16(c);
    c->r[R_CX] = cx;
    uint16_t bx = c->r[R_SP];
    bx = (uint16_t)alu_sub(c, bx, c->r[R_AX], 1, 0);
    c->r[R_BX] = bx;
    unsigned n = 4;                                               /* pop, mov, sub, jb */
    int over = (c->flags & F_CF) != 0;
    if (!over) {
        alu_sub(c, bx, ds_get(c, limit), 1, 0);
        n += 2;                                                   /* cmp, jb */
        over = (c->flags & F_CF) != 0;
    }
    if (over) {
        cpu_push16(c, cx);
        c->r[R_AX] = (uint16_t)alu_logic(c, 0, 1);               /* xor ax, ax */
        c->ip = overflow;
        c->icount += n + 3;
    } else {
        c->r[R_SP] = bx;
        c->ip = cx;
        c->icount += n + 2;                                       /* mov sp, bx; jmp cx */
    }
    return 1;
}
static int start_stack_check(machine_t *m) { return rt_stack_check(m, 0xB154, 0x8FA4); }
static int end_stack_check(machine_t *m) { return rt_stack_check(m, 0x531E, 0x4E9A); }

/* VGAME 1452:0006 / 1452:0033, vg_sine_direct(a) and vg_cosine_direct(a): a quarter-wave table of words at
 * the segment the code loads into ES (its relocated immediate, at CS:000B and CS:0038), 2,048 entries.
 * The angle's bits 2-13 index it, inverted (NOT) when bit 14 is set for the sine and clear for the cosine;
 * the sign comes from the angle's high byte (plus 40h for the cosine). Result in AX and BX; ES, BP restored. */
static int vg_direct(machine_t *m, int cosine)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 22)) return 0;                                   /* the cosine's longest path: 22 */
    const uint16_t ss = c->seg[S_SS], cs = c->seg[S_CS];
    const uint16_t angle = seg_read16(c, ss, (uint16_t)(c->r[R_SP] + 4));
    const uint16_t old_es = c->seg[S_ES];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, old_es);
    const uint16_t table = seg_read16(c, cs, (uint16_t)(cosine ? 0x0038 : 0x000B));
    c->seg[S_ES] = table;
    unsigned n = 11;                                              /* up to the branch on bit 14 */
    uint16_t bx = (uint16_t)(angle >> 2);
    const int bit14 = (angle >> 8) & 0x40;
    if (cosine ? !bit14 : bit14) { bx = (uint16_t)~bx; n++; }     /* not bx */
    bx = (uint16_t)(bx & 0x0FFE);
    bx = seg_read16(c, table, bx);
    uint8_t ah = (uint8_t)(angle >> 8);
    if (cosine) ah = (uint8_t)alu_add(c, ah, 0x40, 0, 0);         /* add ah, 40h */
    alu_logic(c, ah, 0);                                          /* or ah, ah */
    n += 4;                                                       /* and, load, or, jns */
    if (cosine) n++;
    if (ah & 0x80) { bx = (uint16_t)alu_sub(c, 0, bx, 1, 0); n++; }   /* neg bx */
    c->r[R_BX] = bx;
    c->r[R_AX] = bx;
    c->seg[S_ES] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += n + 4;                                           /* mov ax, pop, pop, retf */
    far_ret(c);
    return 1;
}
static int vgame_vg_sine_direct(machine_t *m) { return vg_direct(m, 0); }
static int vgame_vg_cosine_direct(machine_t *m) { return vg_direct(m, 1); }

/* START 0x096CE, mission_rand(): the 32-bit generator at [AE8C] steps as
 * seed * 343FDh + 269EC3h (by the 32-bit multiply at 0x0978E) and the answer
 * is its high word's low 15 bits; DX is left as the whole high word. */
static int start_rand(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    c->r[R_AX] = 0x43FD;
    c->r[R_DX] = 3;
    cpu_push16(c, 3);
    cpu_push16(c, 0x43FD);
    cpu_push16(c, ds_get(c, 0xAE8E));
    cpu_push16(c, ds_get(c, 0xAE8C));
    c->icount += 6;
    if (!guest_call_pop(m, 0x978E, 0x96E1, 8)) return 1;
    if (!room(c, 7)) { c->ip = 0x96E1; return 1; }
    const uint16_t lo = (uint16_t)alu_add(c, c->r[R_AX], 0x9EC3, 1, 0);
    const uint16_t hi = (uint16_t)alu_add(c, c->r[R_DX], 0x26, 1, (c->flags & F_CF) ? 1 : 0);
    ds_put(c, 0xAE8C, lo);
    ds_put(c, 0xAE8E, hi);
    c->r[R_DX] = hi;
    const uint8_t ah = (uint8_t)alu_logic(c, (hi >> 8) & 0x7F, 0);   /* and ah, 7Fh */
    c->r[R_AX] = (uint16_t)(ah << 8 | (hi & 0xFF));
    c->icount += 7;
    near_ret(c);
    return 1;
}

/* START 0x076C0, rnd(n): mission_rand() modulo n (n of 0 is left to the
 * original, where the DIV faults). */
static int start_rnd(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t n = arg(c, 0);
    if (!n || !room(c, 3)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->icount += 2;
    if (!guest_call(m, 0x96CE, 0x76C6)) return 1;
    if (!room(c, 6)) { c->ip = 0x76C6; return 1; }
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);     /* sub dx, dx */
    x86_div16(c, n);
    c->r[R_AX] = c->r[R_DX];
    x86_leave(c);
    c->icount += 6;
    near_ret(c);
    return 1;
}

/* START 0x08C7C, sine(angle on the stack): the table sine at 0x08C88 of the
 * word at SP+2, returned in AX (and BX). */
static int start_sine_arg(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 3)) return 0;
    c->r[R_BX] = arg(c, 0);
    c->icount += 2;
    if (!guest_call(m, 0x8C88, 0x8C85)) return 1;
    if (!room(c, 2)) { c->ip = 0x8C85; return 1; }
    c->r[R_AX] = c->r[R_BX];
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* START 0x03301, vsin(a, r): sine of a, scaled by r through the fixed-point
 * multiply at 0x08C15. */
static int start_vsin(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;
    const uint16_t a = arg(c, 0), r = arg(c, 1);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, r);
    cpu_push16(c, a);
    c->icount += 4;
    if (!guest_call(m, 0x8C7C, 0x330D)) return 1;
    if (!room(c, 3)) { c->ip = 0x330D; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 2;
    if (!guest_call(m, 0x8C15, 0x3312)) return 1;
    if (!room(c, 5)) { c->ip = 0x3312; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 5;
    near_ret(c);
    return 1;
}

/* START 0x03318, vcos(a, r): vsin of a + 4000h. */
static int start_vcos(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    const uint16_t a = arg(c, 0), r = arg(c, 1);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, r);
    const uint8_t ah = (uint8_t)alu_add(c, (a >> 8) & 0xFF, 0x40, 0, 0);   /* add ah, 40h */
    c->r[R_AX] = (uint16_t)(ah << 8 | (a & 0xFF));
    cpu_push16(c, c->r[R_AX]);
    c->icount += 6;
    if (!guest_call(m, 0x3301, 0x3328)) return 1;
    if (!room(c, 5)) { c->ip = 0x3328; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 5;
    near_ret(c);
    return 1;
}

/* ---- START: the front end's mission generator and screens ---------------
 * Small helpers for START's routines, which are Microsoft C with a BP frame:
 * the frame word at BP+d, CWD, and the epilogue MOV SP, BP / POP BP. */

static uint16_t bp_get(cpu_t *c, int d) { return seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + d)); }
static void bp_put(cpu_t *c, int d, uint16_t v) { seg_write16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + d), v); }
static uint8_t bp_get8(cpu_t *c, int d) { return mem_read8(c, phys(c->seg[S_SS], (uint16_t)(c->r[R_BP] + d))); }
static void bp_put8(cpu_t *c, int d, uint8_t v) { mem_write8(c, phys(c->seg[S_SS], (uint16_t)(c->r[R_BP] + d)), v); }
static uint8_t ds_get8(cpu_t *c, uint16_t off) { return mem_read8(c, phys(c->seg[S_DS], off)); }
static void ds_put8(cpu_t *c, uint16_t off, uint8_t v) { mem_write8(c, phys(c->seg[S_DS], off), v); }
static void cwd(cpu_t *c) { c->r[R_DX] = (c->r[R_AX] & 0x8000) ? 0xFFFF : 0; }

/* PUSH BP / MOV BP, SP / SUB SP, n (the SUB's flags; no SUB when n is 0). */
static void frame_open(cpu_t *c, uint16_t locals)
{
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    if (locals) c->r[R_SP] = (uint16_t)alu_sub(c, c->r[R_SP], locals, 1, 0);
}

/* MOV SP, BP / POP BP / RET. */
static void frame_close_ret(cpu_t *c)
{
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
    near_ret(c);
}

/* START 0x05413, dist(dx, dy): the octagonal distance |larger| + |smaller| / 2
 * of two offsets (each made absolute by 0x096AE, the larger kept by a signed
 * compare), summed in 32 bits and capped at 7FFFh. The absolute values are
 * stored back over the arguments. Flags from the last test of the sum. */
static int start_dist(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;
    frame_open(c, 4);
    cpu_push16(c, bp_get(c, 4));
    c->icount += 4;
    if (!guest_call(m, 0x96AE, 0x541F)) return 1;
    if (!room(c, 4)) { c->ip = 0x541F; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    bp_put(c, 4, c->r[R_AX]);
    cpu_push16(c, bp_get(c, 6));
    c->icount += 3;
    if (!guest_call(m, 0x96AE, 0x5429)) return 1;
    if (!room(c, 25)) { c->ip = 0x5429; return 1; }               /* the longest way to the RET */
    c->r[R_BX] = cpu_pop16(c);
    bp_put(c, 6, c->r[R_AX]);
    const uint16_t ady = c->r[R_AX], adx = bp_get(c, 4);
    alu_sub(c, ady, adx, 1, 0);                                   /* cmp ax, [bp+4] */
    unsigned n = 4;
    uint16_t larger, smaller;
    if (!x86_cond(c, 0xD)) { larger = adx; smaller = ady; n += 5; }   /* jge not taken: |dx| larger */
    else { larger = ady; smaller = adx; n += 3; }
    c->r[R_AX] = larger;
    cwd(c);
    c->r[R_CX] = larger;
    c->r[R_AX] = x86_shift(c, 7, smaller, 1, 1);                  /* sar ax, 1 */
    c->r[R_BX] = c->r[R_DX];                                      /* the larger's sign */
    cwd(c);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], c->r[R_CX], 1, 0);
    c->r[R_DX] = (uint16_t)alu_add(c, c->r[R_DX], c->r[R_BX], 1, (c->flags & F_CF) ? 1u : 0u);
    bp_put(c, -4, c->r[R_AX]);
    alu_logic(c, c->r[R_DX], 1);                                  /* or dx, dx */
    n += 8;
    if (!x86_cond(c, 0xC)) {                                      /* jl not taken */
        n++;
        int cap = x86_cond(c, 0xF);                               /* jg: past 16 bits */
        if (!cap) {
            alu_sub(c, c->r[R_AX], 0x7FFF, 1, 0);
            n += 2;
            cap = !x86_cond(c, 0x6);                              /* jbe not taken */
        }
        if (cap) { bp_put(c, -4, 0x7FFF); n++; }
    }
    c->r[R_AX] = bp_get(c, -4);
    c->icount += n + 4;
    frame_close_ret(c);
    return 1;
}

/* START 0x07C05, level_scale(level, lo, hi): the 32-bit value hi:lo brought
 * to a terrain level's scale - doubled at level 0, unchanged at 1, and
 * shifted right (logically, by 0x097F4) 2, 4 or 6 places at 2, 3 and 4. Any other
 * level returns AX = level - 4 and DX as it came, flags from the last DEC. */
static int start_level_scale(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 19)) return 0;                                   /* level 4: to its CALL */
    frame_open(c, 0);
    uint16_t ax = bp_get(c, 4);
    unsigned n = 4;                                               /* to the switch */
    alu_logic(c, ax, 1);                                          /* or ax, ax */
    n += 2;
    int level = 0;
    while (!(c->flags & F_ZF) && level < 4) {
        ax = (uint16_t)alu_dec(c, ax, 1);
        n += 2;
        level++;
    }
    c->r[R_AX] = ax;
    if (!(c->flags & F_ZF)) {                                     /* no case: the epilogue */
        c->icount += n + 3;
        frame_close_ret(c);
        return 1;
    }
    c->r[R_AX] = bp_get(c, 6);
    c->r[R_DX] = bp_get(c, 8);
    if (level == 0) {
        c->r[R_AX] = x86_shift(c, 4, c->r[R_AX], 1, 1);           /* shl ax, 1 */
        c->r[R_DX] = x86_shift(c, 2, c->r[R_DX], 1, 1);           /* rcl dx, 1 */
        c->icount += n + 5 + 3;
        frame_close_ret(c);
        return 1;
    }
    if (level == 1) {
        c->icount += n + 3 + 3;
        frame_close_ret(c);
        return 1;
    }
    set_r8(c, R_CL, (uint8_t)(2 * (level - 1)));                  /* 2, 4 or 6 */
    c->icount += n + (level == 2 ? 3 : 4);                        /* the loads, and a JMP to the shared call */
    if (!guest_call(m, 0x97F4, 0x7C2C)) return 1;
    if (!room(c, 4)) { c->ip = 0x7C2C; return 1; }
    c->icount += 4;                                               /* jmp, and the epilogue */
    frame_close_ret(c);
    return 1;
}

/* START 0x06A7C, surname(name): a pointer to the last word of the string - the
 * character after the last space found scanning back from its end (strlen by
 * 0x094DE). With no space the scan stops at index 0 and the string itself is
 * returned; a space at index 0 is returned as found. */
static int start_surname(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 6)) return 0;
    frame_open(c, 2);
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, bp_get(c, 4));
    c->icount += 5;
    if (!guest_call(m, 0x94DE, 0x6A89)) return 1;
    if (!room(c, 4)) { c->ip = 0x6A89; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_AX] = (uint16_t)alu_dec(c, c->r[R_AX], 1);             /* the last index */
    bp_put(c, -2, c->r[R_AX]);
    c->icount += 4;
    for (;;) {                                                    /* 0x06A97: index BX, string SI */
        if (!room(c, 15)) { c->ip = 0x6A97; return 1; }           /* a pass, or the last one and the exit */
        c->r[R_BX] = bp_get(c, -2);
        c->r[R_SI] = bp_get(c, 4);
        alu_sub(c, ds_get8(c, (uint16_t)(c->r[R_BX] + c->r[R_SI])), 0x20, 0, 0);
        c->icount += 4;
        if (c->flags & F_ZF) break;                               /* a space */
        alu_logic(c, c->r[R_BX], 1);                              /* or bx, bx */
        c->icount += 2;
        if (x86_cond(c, 0xE)) break;                              /* at the start: jle to the exit */
        bp_put(c, -2, (uint16_t)alu_dec(c, bp_get(c, -2), 1));
        c->icount += 1;
    }
    alu_logic(c, c->r[R_BX], 1);                                  /* or bx, bx */
    c->icount += 2;
    if (!x86_cond(c, 0xE)) { bp_put(c, -2, (uint16_t)alu_inc(c, bp_get(c, -2), 1)); c->icount += 1; }
    c->r[R_AX] = (uint16_t)alu_add(c, bp_get(c, -2), c->r[R_SI], 1, 0);
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 6;
    frame_close_ret(c);
    return 1;
}

/* START 0x071AD, form_hover(): the form's hit test (0x0393D over the list at
 * 6FFE, count at [6FFD]) gives the item under the pointer, 0 for none. When it
 * differs from the one lit before ([D096]), the old item's colour is restored
 * (the DAC request 0x03517 from 11D0h) and the new one lit (from 11C1h), each
 * at slot F5h - item; the new item becomes [D096]. */
static int start_form_hover(machine_t *m)
{
    cpu_t *c = &m->cpu;
#define NEED(n, at) do { if (!room(c, (n))) { c->ip = (at); return 1; } } while (0)
    if (!room(c, 9)) return 0;
    frame_open(c, 2);
    set_r8(c, R_AL, ds_get8(c, 0x6FFD));
    set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x6FFE;
    cpu_push16(c, 0x6FFE);
    c->icount += 8;
    if (!guest_call(m, 0x393D, 0x71C0)) return 1;
    NEED(5, 0x71C0);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    bp_put8(c, -2, get_r8(c, R_AL));
    alu_sub(c, ds_get8(c, 0xD096), 0, 0, 0);                      /* cmp byte [D096], 0 */
    c->icount += 5;
    if (!(c->flags & F_ZF)) {                                     /* something was lit */
        NEED(3, 0x71CC);
        set_r8(c, R_AL, ds_get8(c, 0xD096));
        alu_sub(c, bp_get8(c, -2), get_r8(c, R_AL), 0, 0);
        c->icount += 3;
        if (!(c->flags & F_ZF)) {                                 /* and it is not the new item: restore it */
            NEED(9, 0x71D4);
            c->r[R_CX] = 1;
            cpu_push16(c, 1);
            c->r[R_CX] = 0x11D0;
            cpu_push16(c, 0x11D0);
            set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
            c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], 0xF5, 1, 0);
            c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);   /* neg ax */
            cpu_push16(c, c->r[R_AX]);
            c->icount += 8;
            if (!guest_call(m, 0x3517, 0x71E7)) return 1;
            NEED(1, 0x71E7);
            c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
            c->icount += 1;
        }
    }
    NEED(2, 0x71EA);
    alu_sub(c, bp_get8(c, -2), 0, 0, 0);                          /* cmp byte [bp-2], 0 */
    c->icount += 2;
    if (!(c->flags & F_ZF)) {                                     /* an item is under the pointer */
        NEED(3, 0x71F0);
        set_r8(c, R_AL, ds_get8(c, 0xD096));
        alu_sub(c, bp_get8(c, -2), get_r8(c, R_AL), 0, 0);
        c->icount += 3;
        if (!(c->flags & F_ZF)) {                                 /* newly: light it */
            NEED(10, 0x71F8);
            c->r[R_AX] = 1;
            cpu_push16(c, 1);
            c->r[R_AX] = 0x11C1;
            cpu_push16(c, 0x11C1);
            set_r8(c, R_AL, bp_get8(c, -2));
            set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
            c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], 0xF5, 1, 0);
            c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);   /* neg ax */
            cpu_push16(c, c->r[R_AX]);
            c->icount += 9;
            if (!guest_call(m, 0x3517, 0x720E)) return 1;         /* its arguments stay pushed: MOV SP, BP clears them */
        }
    }
    NEED(5, 0x720E);
    set_r8(c, R_AL, bp_get8(c, -2));
    ds_put8(c, 0xD096, get_r8(c, R_AL));
    c->icount += 5;
    frame_close_ret(c);
    return 1;
#undef NEED
}

/* START 0x03DA2, modal_hover(): the modal box's hit test (0x0393D over the
 * buttons at B30C, count at [B30B]). Over button i (from 1) the three-byte
 * colours at 11BEh go to 679Eh - 3i and the ones at 11C4h to 6795h + 3i;
 * over none the 11C4h colours go to 679Bh and are copied on to 6798h (MOVSW,
 * MOVSB with ES = DS, stepping by DF). When the button differs from [D096] the
 * DAC request 0x03517 (E7h, 6798h, 2) is queued; the button becomes [D096]. */
static int start_modal_hover(machine_t *m)
{
    cpu_t *c = &m->cpu;
#define NEED(n, at) do { if (!room(c, (n))) { c->ip = (at); return 1; } } while (0)
    if (!room(c, 11)) return 0;
    frame_open(c, 4);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    set_r8(c, R_AL, ds_get8(c, 0xB30B));
    set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0xB30C;
    cpu_push16(c, 0xB30C);
    c->icount += 10;
    if (!guest_call(m, 0x393D, 0x3DB7)) return 1;
    NEED(5, 0x3DB7);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    bp_put(c, -2, c->r[R_AX]);
    alu_logic(c, c->r[R_AX], 1);                                  /* or ax, ax */
    c->icount += 5;
    if (!(c->flags & F_ZF)) {                                     /* over a button */
        NEED(24, 0x3DC0);                                         /* to the second copy's end and the test */
        const uint16_t three = (uint16_t)alu_add(c, x86_shift(c, 4, c->r[R_AX], 1, 1), c->r[R_AX], 1, 0);
        c->r[R_CX] = three;
        c->r[R_AX] = (uint16_t)alu_sub(c, 0, (uint16_t)alu_sub(c, three, 0x679E, 1, 0), 1, 0);   /* 679Eh - 3i */
        cpu_push16(c, three);
        c->r[R_DI] = c->r[R_AX];
        c->r[R_SI] = 0x11BE;
        cpu_push16(c, c->seg[S_DS]);
        c->seg[S_ES] = cpu_pop16(c);
        x86_movs(c, 1, c->seg[S_DS]);
        x86_movs(c, 0, c->seg[S_DS]);
        c->r[R_CX] = cpu_pop16(c);
        c->r[R_BX] = c->r[R_CX];
        c->r[R_DI] = (uint16_t)(c->r[R_BX] + 0x6795);
        c->r[R_SI] = 0x11C4;
        c->icount += 18;
    } else {
        NEED(16, 0x3DE3);
        c->r[R_AX] = 0x679B;
        c->r[R_DI] = 0x679B;
        c->r[R_SI] = 0x11C4;
        cpu_push16(c, c->seg[S_DS]);
        c->seg[S_ES] = cpu_pop16(c);
        x86_movs(c, 1, c->seg[S_DS]);
        x86_movs(c, 0, c->seg[S_DS]);
        c->r[R_DX] = 0x6798;
        c->r[R_DI] = 0x6798;
        c->r[R_SI] = 0x679B;
        c->icount += 10;
    }
    x86_movs(c, 1, c->seg[S_DS]);                                 /* 0x03DF6 */
    x86_movs(c, 0, c->seg[S_DS]);
    set_r8(c, R_AL, ds_get8(c, 0xD096));
    set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
    alu_sub(c, c->r[R_AX], bp_get(c, -2), 1, 0);
    c->icount += 6;
    if (!(c->flags & F_ZF)) {                                     /* a change: queue the colours */
        NEED(7, 0x3E02);
        c->r[R_AX] = 2;
        cpu_push16(c, 2);
        c->r[R_AX] = 0x6798;
        cpu_push16(c, 0x6798);
        c->r[R_AX] = 0xE7;
        cpu_push16(c, 0xE7);
        c->icount += 6;
        if (!guest_call(m, 0x3517, 0x3E11)) return 1;
        NEED(1, 0x3E11);
        c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
        c->icount += 1;
    }
    NEED(7, 0x3E14);
    set_r8(c, R_AL, bp_get8(c, -2));
    ds_put8(c, 0xD096, get_r8(c, R_AL));
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->icount += 7;
    frame_close_ret(c);
    return 1;
#undef NEED
}

/* START 0x00E23, weapon_shortage(): clears the 20 flags at D878, then sets
 * rnd(5) of them: a random entry r of the 16-byte table at 01C4 (FFh for no
 * weapon) whose weapon's flag (D878 + the entry) is clear has its own flag
 * D878 + r set; any other draw is made again. Both D878 and D879 then take
 * byte 42h of the settings at far [CACA]. */
static int start_weapon_shortage(machine_t *m)
{
    cpu_t *c = &m->cpu;
#define NEED(n, at) do { if (!room(c, (n))) { c->ip = (at); return 1; } } while (0)
    if (!room(c, 4)) return 0;
    frame_open(c, 6);
    bp_put(c, -2, 0);
    c->icount += 4;
    do {                                                          /* 0x00E2E */
        NEED(5, 0x0E2E);
        c->r[R_BX] = bp_get(c, -2);
        ds_put8(c, (uint16_t)(c->r[R_BX] - 0x2788), 0);
        bp_put(c, -2, (uint16_t)alu_inc(c, bp_get(c, -2), 1));
        alu_sub(c, bp_get(c, -2), 0x14, 1, 0);
        c->icount += 5;
    } while (c->flags & F_CF);                                    /* jb */
    NEED(3, 0x0E3F);
    c->r[R_AX] = 5;
    cpu_push16(c, 5);
    c->icount += 2;
    if (!guest_call(m, 0x76C0, 0x0E46)) return 1;
    NEED(4, 0x0E46);
    c->r[R_BX] = cpu_pop16(c);
    bp_put(c, -4, c->r[R_AX]);
    bp_put(c, -2, 0);
    c->icount += 4;
    for (;;) {                                                    /* 0x00E7C: while marked < wanted */
        NEED(3, 0x0E7C);
        c->r[R_AX] = bp_get(c, -2);
        alu_sub(c, bp_get(c, -4), c->r[R_AX], 1, 0);
        c->icount += 3;
        if (!x86_cond(c, 0x7)) break;                             /* ja */
        NEED(3, 0x0E51);
        c->r[R_AX] = 0x10;
        cpu_push16(c, 0x10);
        c->icount += 2;
        if (!guest_call(m, 0x76C0, 0x0E58)) return 1;
        NEED(5, 0x0E58);
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_BX] = c->r[R_AX];
        bp_put(c, -6, c->r[R_BX]);
        alu_sub(c, ds_get8(c, (uint16_t)(c->r[R_BX] + 0x1C4)), 0xFF, 0, 0);
        c->icount += 5;
        if (c->flags & F_ZF) continue;                            /* no weapon there */
        NEED(4, 0x0E65);
        set_r8(c, R_BL, ds_get8(c, (uint16_t)(c->r[R_BX] + 0x1C4)));
        set_r8(c, R_BH, (uint8_t)alu_sub(c, get_r8(c, R_BH), get_r8(c, R_BH), 0, 0));
        alu_sub(c, ds_get8(c, (uint16_t)(c->r[R_BX] - 0x2788)), get_r8(c, R_BH), 0, 0);
        c->icount += 4;
        if (!(c->flags & F_ZF)) continue;                         /* already short */
        NEED(3, 0x0E71);
        c->r[R_BX] = bp_get(c, -6);
        ds_put8(c, (uint16_t)(c->r[R_BX] - 0x2788), 1);
        bp_put(c, -2, (uint16_t)alu_inc(c, bp_get(c, -2), 1));
        c->icount += 3;
    }
    NEED(7, 0x0E84);
    c->r[R_BX] = ds_get(c, 0xCACA);
    c->seg[S_ES] = ds_get(c, 0xCACC);
    set_r8(c, R_AL, mem_read8(c, phys(c->seg[S_ES], (uint16_t)(c->r[R_BX] + 0x42))));
    ds_put8(c, 0xD879, get_r8(c, R_AL));
    ds_put8(c, 0xD878, get_r8(c, R_AL));
    c->icount += 7;
    frame_close_ret(c);
    return 1;
#undef NEED
}

/* START 0x059FE, time_string(buffer, t): the clock text for a time of day
 * in 2-second units. The template at 0DD2h is copied (0x094AC) and its
 * digits advanced: the hours' tens by byte 33h of the settings at far
 * [E096], the units by t / 1800 % 10 plus 6 (8 when that byte is not zero),
 * carried into the tens past '9'; the minutes' digits from t / 30 % 60
 * rounded down to a multiple of 5. */
static int start_time_string(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    frame_open(c, 4);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_AX] = 0x0DD2;
    cpu_push16(c, 0x0DD2);
    cpu_push16(c, bp_get(c, 4));
    c->icount += 7;
    if (!guest_call(m, 0x94AC, 0x5A0F)) return 1;
    if (!room(c, 53)) { c->ip = 0x5A0F; return 1; }              /* the longest way to the RET */
    const uint16_t ds = c->seg[S_DS];
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    uint16_t es = ds_get(c, 0xE098);
    uint16_t far = ds_get(c, 0xE096);
    c->r[R_BX] = far;
    c->seg[S_ES] = es;
    set_r8(c, R_AL, mem_read8(c, phys(es, (uint16_t)(far + 0x33))));
    uint16_t buf = bp_get(c, 4);
    c->r[R_BX] = buf;
    mem_write8(c, phys(ds, buf), (uint8_t)alu_add(c, mem_read8(c, phys(ds, buf)), get_r8(c, R_AL), 0, 0));
    c->r[R_AX] = bp_get(c, 6);
    c->r[R_CX] = 0x708;
    cwd(c);
    x86_idiv16(c, 0x708, 0);                                      /* t / 1800 */
    c->r[R_CX] = 0x0A;
    cwd(c);
    x86_idiv16(c, 0x0A, 0);                                       /* ... % 10 in DL */
    const uint16_t units = (uint16_t)(buf + 1);
    mem_write8(c, phys(ds, units), (uint8_t)alu_add(c, mem_read8(c, phys(ds, units)), get_r8(c, R_DL), 0, 0));
    far = ds_get(c, 0xE096);
    es = ds_get(c, 0xE098);
    c->r[R_SI] = far;
    c->seg[S_ES] = es;
    alu_sub(c, mem_read8(c, phys(es, (uint16_t)(far + 0x33))), 1, 0, 0);   /* cmp byte es:[si+33h], 1 */
    uint8_t al = (uint8_t)alu_sub(c, get_r8(c, R_AL), get_r8(c, R_AL), 0, (c->flags & F_CF) ? 1u : 0u);   /* sbb al, al */
    al = (uint8_t)alu_logic(c, al & 0xFE, 0);
    al = (uint8_t)alu_add(c, al, 8, 0, 0);
    set_r8(c, R_AL, al);
    mem_write8(c, phys(ds, units), (uint8_t)alu_add(c, mem_read8(c, phys(ds, units)), al, 0, 0));
    alu_sub(c, mem_read8(c, phys(ds, units)), 0x39, 0, 0);
    unsigned n = 22;
    if (!x86_cond(c, 0xE)) {                                      /* past '9': carry into the tens */
        al = (uint8_t)alu_sub(c, mem_read8(c, phys(ds, units)), 0x0A, 0, 0);
        set_r8(c, R_AL, al);
        mem_write8(c, phys(ds, units), al);
        mem_write8(c, phys(ds, buf), (uint8_t)alu_inc(c, mem_read8(c, phys(ds, buf)), 0));
        n += 4;
    }
    c->r[R_CX] = 5;
    c->r[R_DX] = 0x3C;
    c->r[R_AX] = bp_get(c, 6);
    c->r[R_BX] = 0x1E;
    c->r[R_SI] = 0x3C;
    cwd(c);
    x86_idiv16(c, 0x1E, 0);                                       /* t / 30 */
    cwd(c);
    x86_idiv16(c, 0x3C, 0);                                       /* ... % 60 */
    c->r[R_AX] = c->r[R_DX];
    cwd(c);
    x86_idiv16(c, 5, 0);
    x86_imul16(c, 5);                                             /* down to a multiple of 5 */
    bp_put(c, -4, c->r[R_AX]);
    c->r[R_CX] = 0x0A;
    cwd(c);
    x86_idiv16(c, 0x0A, 0);
    buf = bp_get(c, 4);
    c->r[R_BX] = buf;
    mem_write8(c, phys(ds, (uint16_t)(buf + 3)), (uint8_t)alu_add(c, mem_read8(c, phys(ds, (uint16_t)(buf + 3))), get_r8(c, R_AL), 0, 0));
    c->r[R_AX] = bp_get(c, -4);
    cwd(c);
    x86_idiv16(c, 0x0A, 0);
    mem_write8(c, phys(ds, (uint16_t)(buf + 4)), (uint8_t)alu_add(c, mem_read8(c, phys(ds, (uint16_t)(buf + 4))), get_r8(c, R_DL), 0, 0));
    c->r[R_SI] = cpu_pop16(c);
    c->icount += n + 27;
    frame_close_ret(c);
    return 1;
}

/* START's target slots: 16-byte records at CBE0 (x, y at +0/+2, a word at
 * +0Ch), slots 3.. below the count [D33C]. The tail shared by target_near and
 * target_for_type, from the return of the object finder: a found object (its
 * record pointer in AX, 0 for none) has its 32-bit map position (+4 and +8)
 * brought down by 32 (0x097CC) to x and to 8000h - y; when a slot from 3 up
 * already holds that point its index is the answer, otherwise the point is
 * stored in the caller's slot, with the object's first word plus 100h at
 * +0Ch, and that slot is the answer. No object answers FFFFh. */
typedef struct {
    uint16_t found;              /* the instruction after the finder's CALL */
    uint16_t obj;                /* the first instruction for a found object */
    int pops;                    /* 4: the finder's arguments are dropped by ADD SP, 8 (else POP BX) */
    int16_t x, y, idx, slot;     /* frame offsets of the point, the scan index and the slot argument */
} target_tail;

static int target_place(machine_t *m, const target_tail *t)
{
    cpu_t *c = &m->cpu;
    const uint16_t f = t->found, g = t->obj;
#define NEED(n, at) do { if (!room(c, (n))) { c->ip = (uint16_t)(at); return 1; } } while (0)
    NEED(5, f);                                                   /* the test, or the test and the failure */
    if (t->pops) c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    else c->r[R_BX] = cpu_pop16(c);
    bp_put(c, -2, c->r[R_AX]);
    alu_logic(c, c->r[R_AX], 1);                                  /* or ax, ax */
    c->icount += 4;
    if (c->flags & F_ZF) {                                        /* no object */
        NEED(5, g + 0x7B);
        c->r[R_AX] = 0xFFFF;
        c->icount += 1;
        goto out;
    }
    NEED(6, g);
    c->r[R_BX] = c->r[R_AX];
    c->r[R_CX] = c->r[R_AX];
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 4));
    c->r[R_DX] = ds_get(c, (uint16_t)(c->r[R_BX] + 6));
    set_r8(c, R_CL, 5);
    c->icount += 5;
    if (!guest_call(m, 0x97CC, (uint16_t)(g + 0x0F))) return 1;
    NEED(6, g + 0x0F);
    bp_put(c, t->x, c->r[R_AX]);
    c->r[R_BX] = bp_get(c, -2);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 8));
    c->r[R_DX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0A));
    set_r8(c, R_CL, 5);
    c->icount += 5;
    if (!guest_call(m, 0x97CC, (uint16_t)(g + 0x20))) return 1;
    NEED(5, g + 0x20);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], 0x8000, 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);       /* neg ax: 8000h - y */
    bp_put(c, t->y, c->r[R_AX]);
    bp_put(c, t->idx, 3);
    c->icount += 5;
    for (;;) {                                                    /* g + 32h: the scan */
        NEED(20, g + 0x32);                                       /* a pass, or a pass and the store */
        c->r[R_AX] = bp_get(c, t->idx);
        alu_sub(c, ds_get(c, 0xD33C), c->r[R_AX], 1, 0);
        c->icount += 3;
        if (x86_cond(c, 0xE)) break;                              /* no more slots */
        c->r[R_BX] = x86_shift(c, 4, c->r[R_AX], 4, 1);
        set_r8(c, R_CL, 4);
        c->r[R_CX] = bp_get(c, t->x);
        alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] - 0x3420)), c->r[R_CX], 1, 0);
        c->icount += 6;
        if (c->flags & F_ZF) {
            c->r[R_CX] = bp_get(c, t->y);
            alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] - 0x341E)), c->r[R_CX], 1, 0);
            c->icount += 3;
            if (c->flags & F_ZF) { c->icount += 1; goto out; }    /* the point is there: AX its index */
        }
        bp_put(c, t->idx, (uint16_t)alu_inc(c, bp_get(c, t->idx), 1));
        c->icount += 1;
    }
    c->r[R_AX] = bp_get(c, t->x);
    set_r8(c, R_CL, 4);
    c->r[R_BX] = x86_shift(c, 4, bp_get(c, t->slot), 4, 1);
    ds_put(c, (uint16_t)(c->r[R_BX] - 0x3420), c->r[R_AX]);
    c->r[R_AX] = bp_get(c, t->y);
    ds_put(c, (uint16_t)(c->r[R_BX] - 0x341E), c->r[R_AX]);
    c->r[R_SI] = bp_get(c, -2);
    c->r[R_AX] = ds_get(c, c->r[R_SI]);
    set_r8(c, R_AH, (uint8_t)alu_add(c, get_r8(c, R_AH), 1, 0, 0));   /* add ah, 1 */
    ds_put(c, (uint16_t)(c->r[R_BX] - 0x3414), c->r[R_AX]);
    c->r[R_AX] = bp_get(c, t->slot);
    c->icount += 13;
out:
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 4;
    frame_close_ret(c);
    return 1;
#undef NEED
}

/* START 0x0512E, target_near(x, y, slot): the object nearest the map point
 * (0x07744 on x * 32 and (8000h - y) * 32, both 32-bit by 0x097C0), placed in
 * a target slot as target_place says. */
static int start_target_near(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 14)) return 0;
    frame_open(c, 4);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_AX] = bp_get(c, 6);
    cwd(c);
    c->r[R_CX] = c->r[R_AX];
    c->r[R_BX] = c->r[R_DX];
    c->r[R_AX] = 0x8000;
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0x8000, c->r[R_CX], 1, 0);
    c->r[R_DX] = (uint16_t)alu_sub(c, 0, c->r[R_BX], 1, (c->flags & F_CF) ? 1u : 0u);   /* sbb dx, bx */
    set_r8(c, R_CL, 5);
    c->icount += 13;
    if (!guest_call(m, 0x97C0, 0x514B)) return 1;
    if (!room(c, 6)) { c->ip = 0x514B; return 1; }
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = bp_get(c, 4);
    cwd(c);
    set_r8(c, R_CL, 5);
    c->icount += 5;
    if (!guest_call(m, 0x97C0, 0x5156)) return 1;
    if (!room(c, 3)) { c->ip = 0x5156; return 1; }
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 2;
    if (!guest_call(m, 0x7744, 0x515B)) return 1;
    static const target_tail T = { 0x515B, 0x5165, 1, 4, 6, -4, 8 };
    return target_place(m, &T);
}

/* START 0x051E8, target_for_type(slot, type): an object of the given type
 * (0x07904 on the type byte, sign-extended), placed in a target slot as
 * target_place says. */
static int start_target_for_type(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    frame_open(c, 8);
    cpu_push16(c, c->r[R_SI]);
    set_r8(c, R_AL, bp_get8(c, 6));
    c->r[R_AX] = (uint16_t)(int16_t)(int8_t)get_r8(c, R_AL);     /* cbw */
    cpu_push16(c, c->r[R_AX]);
    c->icount += 7;
    if (!guest_call(m, 0x7904, 0x51F7)) return 1;
    static const target_tail T = { 0x51F7, 0x51FF, 0, -4, -6, -8, 4 };
    return target_place(m, &T);
}

/* START 0x05282, pick_weapon(row): over the 16 weapons (from 2 when the
 * setting at far [CACA]+42h is not zero), those whose flag at D878 is clear
 * are scored by the byte at row + 16 * weapon + 1D86h (signed); the best score
 * wins, and a tie among several is broken by rnd(count) (0x076C0). Returns
 * the weapon number. */
static int start_pick_weapon(machine_t *m)
{
    cpu_t *c = &m->cpu;
#define NEED(n, at) do { if (!room(c, (n))) { c->ip = (at); return 1; } } while (0)
    if (!room(c, 11)) return 0;
    frame_open(c, 0x16);
    cpu_push16(c, c->r[R_SI]);
    bp_put(c, -0x16, 0);                                          /* how many share the best score */
    bp_put8(c, -2, 0);                                            /* the best score */
    c->r[R_BX] = ds_get(c, 0xCACA);
    c->seg[S_ES] = ds_get(c, 0xCACC);
    alu_sub(c, seg_read16(c, c->seg[S_ES], (uint16_t)(c->r[R_BX] + 0x42)), 0, 1, 0);
    bp_put(c, -0x14, (c->flags & F_ZF) ? 0 : 2);                  /* the first weapon */
    c->icount += 11;
    for (;;) {                                                    /* 0x052CF */
        NEED(31, 0x52CF);                                         /* the longest pass */
        alu_sub(c, bp_get(c, -0x14), 0x10, 1, 0);
        c->icount += 2;
        if (!(c->flags & F_CF)) break;                            /* jae: all 16 seen */
        c->r[R_BX] = bp_get(c, -0x14);
        alu_sub(c, ds_get8(c, (uint16_t)(c->r[R_BX] - 0x2788)), 0, 0, 0);
        c->icount += 3;
        if (c->flags & F_ZF) {                                    /* available */
            set_r8(c, R_CL, 4);
            c->r[R_SI] = x86_shift(c, 4, c->r[R_BX], 4, 1);
            c->r[R_AX] = c->r[R_BX];
            c->r[R_BX] = bp_get(c, 4);
            set_r8(c, R_AL, ds_get8(c, (uint16_t)(c->r[R_BX] + c->r[R_SI] + 0x1D86)));
            c->r[R_CX] = c->r[R_AX];
            c->r[R_AX] = (uint16_t)(int16_t)(int8_t)get_r8(c, R_AL);   /* cbw */
            set_r8(c, R_DL, bp_get8(c, -2));
            set_r8(c, R_DH, (uint8_t)alu_sub(c, get_r8(c, R_DH), get_r8(c, R_DH), 0, 0));
            alu_sub(c, c->r[R_AX], c->r[R_DX], 1, 0);
            c->icount += 12;
            if (!x86_cond(c, 0xE)) {                              /* a new best: this one alone */
                bp_put8(c, -2, get_r8(c, R_CL));
                set_r8(c, R_AL, bp_get8(c, -0x14));
                bp_put8(c, -0x12, get_r8(c, R_AL));
                bp_put(c, -0x16, 1);
                c->icount += 5;
            } else {                                              /* 0x052AB: as good as the best? */
                set_r8(c, R_CL, 4);
                c->r[R_SI] = x86_shift(c, 4, bp_get(c, -0x14), 4, 1);
                set_r8(c, R_AL, ds_get8(c, (uint16_t)(c->r[R_BX] + c->r[R_SI] + 0x1D86)));
                c->r[R_AX] = (uint16_t)(int16_t)(int8_t)get_r8(c, R_AL);
                set_r8(c, R_CL, bp_get8(c, -2));
                set_r8(c, R_CH, (uint8_t)alu_sub(c, get_r8(c, R_CH), get_r8(c, R_CH), 0, 0));
                alu_sub(c, c->r[R_AX], c->r[R_CX], 1, 0);
                c->icount += 9;
                if (c->flags & F_ZF) {                            /* a tie: one more candidate */
                    set_r8(c, R_AL, bp_get8(c, -0x14));
                    c->r[R_SI] = bp_get(c, -0x16);
                    bp_put8(c, (int)(int16_t)c->r[R_SI] - 0x12, get_r8(c, R_AL));
                    bp_put(c, -0x16, (uint16_t)alu_inc(c, bp_get(c, -0x16), 1));
                    c->icount += 4;
                }
            }
        }
        bp_put(c, -0x14, (uint16_t)alu_inc(c, bp_get(c, -0x14), 1));   /* 0x052CC */
        c->icount += 1;
    }
    NEED(2, 0x530A);
    alu_sub(c, bp_get(c, -0x16), 1, 1, 0);
    c->icount += 2;
    if (c->flags & F_ZF) {                                        /* a single best */
        NEED(7, 0x5310);
        set_r8(c, R_AL, bp_get8(c, -0x12));
        c->icount += 1;
    } else {
        NEED(2, 0x5317);
        cpu_push16(c, bp_get(c, -0x16));
        c->icount += 1;
        if (!guest_call(m, 0x76C0, 0x531D)) return 1;
        NEED(10, 0x531D);
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_SI] = c->r[R_AX];
        set_r8(c, R_AL, bp_get8(c, (int)(int16_t)c->r[R_SI] - 0x12));
        c->icount += 4;
    }
    set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));   /* 0x05313 */
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 6;
    frame_close_ret(c);
    return 1;
#undef NEED
}

/* START 0x05356, slot_fill(slot, target): fills mission slot `slot` (36-byte
 * records at D3BE; its type word at +16h) from target slot `target`: the
 * point, offset by (+9, -12), at +2/+4 and as 32-bit values times 32 at
 * +8/+0Ch; +6 is 8Ch when bit 1 of the target's byte +7 is set, else 0Ch;
 * the type's speed (32-byte records at 171A) at +1Ah and the type's second
 * word times 8192 over that speed (0x096F4) at +1Ch; +10h = FC00h,
 * +12h/+14h = 0, +18h |= 403h, +0 = target. */
static int start_slot_fill(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 24)) return 0;
    frame_open(c, 2);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_AX] = 0x24;
    x86_imul16(c, bp_get(c, 4));
    const uint16_t rec = c->r[R_AX];                              /* slot * 36 */
    c->r[R_BX] = rec;
    c->r[R_AX] = ds_get(c, (uint16_t)(rec - 0x2C2C));
    bp_put(c, -2, c->r[R_AX]);                                    /* the type */
    set_r8(c, R_CL, 4);
    const uint16_t tgt = x86_shift(c, 4, bp_get(c, 6), 4, 1);
    c->r[R_SI] = tgt;
    c->r[R_AX] = (uint16_t)alu_add(c, ds_get(c, (uint16_t)(tgt - 0x3420)), 9, 1, 0);
    ds_put(c, (uint16_t)(rec - 0x2C40), c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, ds_get(c, (uint16_t)(tgt - 0x341E)), 0x0C, 1, 0);
    ds_put(c, (uint16_t)(rec - 0x2C3E), c->r[R_AX]);
    c->r[R_AX] = ds_get(c, (uint16_t)(rec - 0x2C40));
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    set_r8(c, R_CL, 5);
    c->r[R_DI] = rec;
    c->icount += 23;
    if (!guest_call(m, 0x97C0, 0x5397)) return 1;
    if (!room(c, 6)) { c->ip = 0x5397; return 1; }
    uint16_t di = c->r[R_DI];
    ds_put(c, (uint16_t)(di - 0x2C3A), c->r[R_AX]);
    ds_put(c, (uint16_t)(di - 0x2C38), c->r[R_DX]);
    c->r[R_AX] = ds_get(c, (uint16_t)(di - 0x2C3E));
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    set_r8(c, R_CL, 5);
    c->icount += 5;
    if (!guest_call(m, 0x97C0, 0x53AA)) return 1;
    if (!room(c, 29)) { c->ip = 0x53AA; return 1; }
    di = c->r[R_DI];
    ds_put(c, (uint16_t)(di - 0x2C36), c->r[R_AX]);
    ds_put(c, (uint16_t)(di - 0x2C34), c->r[R_DX]);
    set_r8(c, R_AH, ds_get8(c, (uint16_t)(c->r[R_SI] - 0x3419)));
    c->r[R_AX] = (uint16_t)alu_logic(c, c->r[R_AX] & 0x200, 1);
    alu_sub(c, c->r[R_AX], 1, 1, 0);                              /* cmp ax, 1: CF when the bit is clear */
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, (c->flags & F_CF) ? 1u : 0u);   /* sbb ax, ax */
    set_r8(c, R_AL, (uint8_t)alu_logic(c, get_r8(c, R_AL) & 0x80, 0));
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x8C, 1, 0);
    ds_put(c, (uint16_t)(di - 0x2C3C), c->r[R_AX]);
    set_r8(c, R_CL, 5);
    const uint16_t type = x86_shift(c, 4, bp_get(c, -2), 5, 1);  /* type * 32 */
    c->r[R_BX] = type;
    c->r[R_AX] = ds_get(c, (uint16_t)(type + 0x171A));
    ds_put(c, (uint16_t)(di - 0x2C28), c->r[R_AX]);
    ds_put(c, (uint16_t)(di - 0x2C32), 0xFC00);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    ds_put(c, (uint16_t)(di - 0x2C30), 0);
    ds_put(c, (uint16_t)(di - 0x2C2E), 0);
    ds_put(c, (uint16_t)(di - 0x2C2A), (uint16_t)alu_logic(c, ds_get(c, (uint16_t)(di - 0x2C2A)) | 0x403, 1));
    c->r[R_AX] = bp_get(c, 6);
    ds_put(c, (uint16_t)(di - 0x2C42), c->r[R_AX]);
    c->r[R_AX] = ds_get(c, (uint16_t)(di - 0x2C28));
    cwd(c);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = ds_get(c, (uint16_t)(type + 0x171C));
    cwd(c);
    set_r8(c, R_CL, 0x0D);
    c->icount += 28;
    if (!guest_call(m, 0x97C0, 0x5404)) return 1;
    if (!room(c, 3)) { c->ip = 0x5404; return 1; }
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 2;
    if (!guest_call_pop(m, 0x96F4, 0x5409, 8)) return 1;
    if (!room(c, 6)) { c->ip = 0x5409; return 1; }
    ds_put(c, (uint16_t)(c->r[R_DI] - 0x2C26), c->r[R_AX]);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->icount += 6;
    frame_close_ret(c);
    return 1;
}

/* START 0x05743, angle(dx, dy): the bearing of an offset as a 16-bit angle
 * (4000h a quarter turn): 0 straight along +dy, 4000h along +dx. The axes
 * answer at once; otherwise the smaller of |dx|, |dy| over the larger (as
 * 2.14 fixed point, by 0x096F4) goes through the approximation
 * r * (2800h - (|1333h - r| * B00h >> 14)) >> 14 (0x0978E), and the octant
 * (the signs, and which was larger) places it. */
static int start_angle(machine_t *m)
{
    cpu_t *c = &m->cpu;
#define NEED(n, at) do { if (!room(c, (n))) { c->ip = (at); return 1; } } while (0)
    if (!room(c, 16)) return 0;                                   /* the axes, or the first CALL */
    frame_open(c, 0x0E);
    cpu_push16(c, c->r[R_SI]);
    alu_sub(c, bp_get(c, 4), 0, 1, 0);                            /* cmp [bp+4], 0 */
    c->icount += 6;
    if (c->flags & F_ZF) {                                        /* along the dy axis */
        alu_sub(c, bp_get(c, 6), 0, 1, 0);
        if (!x86_cond(c, 0xE)) c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
        else c->r[R_AX] = 0x8000;
        c->icount += 4;
        goto out;
    }
    alu_sub(c, bp_get(c, 6), 0, 1, 0);
    c->icount += 2;
    if (c->flags & F_ZF) {                                        /* along the dx axis */
        alu_sub(c, bp_get(c, 4), 0, 1, 0);
        c->r[R_AX] = x86_cond(c, 0xE) ? 0xC000 : 0x4000;
        c->icount += 4;
        goto out;
    }
    cpu_push16(c, bp_get(c, 6));
    c->icount += 1;
    if (!guest_call(m, 0x96AE, 0x577F)) return 1;
    NEED(4, 0x577F);
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, bp_get(c, 4));
    c->r[R_SI] = c->r[R_AX];                                      /* |dy| */
    c->icount += 3;
    if (!guest_call(m, 0x96AE, 0x5788)) return 1;
    NEED(5, 0x5788);
    c->r[R_BX] = cpu_pop16(c);
    alu_sub(c, c->r[R_AX], c->r[R_SI], 1, 0);
    c->icount += 3;
    /* |dx| > |dy|: dy * 16384 / |dx|, flag 1; else |dx| * 16384 / |dy|, flag 0. */
    const int dx_larger = !x86_cond(c, 0xE);
    const int16_t num = dx_larger ? 6 : 4, den = dx_larger ? 4 : 6;
    const uint16_t r1 = dx_larger ? 0x5793 : 0x57B4, r2 = dx_larger ? 0x579A : 0x57BB, r3 = dx_larger ? 0x57A6 : 0x57C7;
    const uint16_t at = dx_larger ? 0x578D : 0x57AE;
    NEED(2, at);
    cpu_push16(c, bp_get(c, num));
    c->icount += 1;
    if (!guest_call(m, 0x96AE, r1)) return 1;
    NEED(4, r1);
    c->r[R_BX] = cpu_pop16(c);
    cwd(c);
    set_r8(c, R_CL, 0x0E);
    c->icount += 3;
    if (!guest_call(m, 0x97C0, r2)) return 1;
    NEED(4, r2);
    bp_put(c, -8, c->r[R_AX]);
    bp_put(c, -6, c->r[R_DX]);
    cpu_push16(c, bp_get(c, den));
    c->icount += 3;
    if (!guest_call(m, 0x96AE, r3)) return 1;
    NEED(dx_larger ? 9 : 8, r3);                                  /* to the divide's CALL */
    c->r[R_BX] = cpu_pop16(c);
    bp_put(c, -0x0C, dx_larger ? 1 : 0);
    c->icount += dx_larger ? 3 : 2;
    cwd(c);                                                       /* 0x057CD */
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, -6));
    cpu_push16(c, bp_get(c, -8));
    c->icount += 5;
    if (!guest_call_pop(m, 0x96F4, 0x57D9, 8)) return 1;
    NEED(8, 0x57D9);
    bp_put(c, -0x0E, c->r[R_AX]);                                 /* the ratio r */
    cwd(c);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0x1333, bp_get(c, -0x0E), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 7;
    if (!guest_call(m, 0x96AE, 0x57E9)) return 1;
    NEED(5, 0x57E9);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_CX] = 0x0B00;
    x86_imul16(c, 0x0B00);
    set_r8(c, R_CL, 0x0E);
    c->icount += 4;
    if (!guest_call(m, 0x97CC, 0x57F4)) return 1;
    NEED(7, 0x57F4);
    c->r[R_CX] = 0x2800;
    c->r[R_BX] = (uint16_t)alu_sub(c, c->r[R_BX], c->r[R_BX], 1, 0);
    c->r[R_CX] = (uint16_t)alu_sub(c, 0x2800, c->r[R_AX], 1, 0);
    c->r[R_BX] = (uint16_t)alu_sub(c, 0, c->r[R_DX], 1, (c->flags & F_CF) ? 1u : 0u);   /* sbb bx, dx */
    cpu_push16(c, c->r[R_BX]);
    cpu_push16(c, c->r[R_CX]);
    c->icount += 6;
    if (!guest_call_pop(m, 0x978E, 0x5802, 8)) return 1;
    NEED(2, 0x5802);
    set_r8(c, R_CL, 0x0E);
    c->icount += 1;
    if (!guest_call(m, 0x97CC, 0x5807)) return 1;
    NEED(15, 0x5807);                                             /* the octant, the longest way */
    bp_put(c, -2, c->r[R_AX]);
    alu_sub(c, bp_get(c, 4), 0, 1, 0);
    c->icount += 3;
    {
        const int dx_pos = !x86_cond(c, 0xE);
        alu_sub(c, bp_get(c, 6), 0, 1, 0);
        const int dy_pos = !x86_cond(c, 0xE);
        alu_sub(c, bp_get(c, -0x0C), 0, 1, 0);
        const int flag = !(c->flags & F_ZF);
        c->icount += 4;
        uint16_t ax = c->r[R_AX];
        if (dx_pos && dy_pos) {
            if (flag) { ax = (uint16_t)alu_sub(c, 0x4000, bp_get(c, -2), 1, 0); c->icount += 3; }
        } else if (dx_pos) {
            if (flag) { set_r8(c, R_AH, (uint8_t)alu_add(c, get_r8(c, R_AH), 0x40, 0, 0)); ax = c->r[R_AX]; c->icount += 2; }
            else { ax = (uint16_t)alu_sub(c, 0x8000, bp_get(c, -2), 1, 0); c->icount += 4; }
        } else if (dy_pos) {
            if (flag) ax = (uint16_t)alu_sub(c, ax, 0x4000, 1, 0);
            else ax = (uint16_t)alu_sub(c, 0, ax, 1, 0);          /* neg ax */
            c->icount += 2;
        } else {
            if (flag) { ax = (uint16_t)alu_sub(c, 0xC000, bp_get(c, -2), 1, 0); c->icount += 4; }
            else { set_r8(c, R_AH, (uint8_t)alu_add(c, get_r8(c, R_AH), 0x80, 0, 0)); ax = c->r[R_AX]; c->icount += 1; }
        }
        c->r[R_AX] = ax;
    }
out:
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 4;
    frame_close_ret(c);
    return 1;
#undef NEED
}

/* START 0x07C56, terrain_tile(level, x, y): the tile at a terrain level, -1
 * outside it (x or y negative, or not below the level's size [107E + 2 *
 * level]). Levels 0-2 are indexed tiles: the tile of the next level at
 * (x / 4, y / 4) (recursively) picks a 4x4 block, 16 bytes each, in the
 * level's table (B3A0, B5A0, B7A0), and (x & 3, y & 3) the byte in it.
 * Level 3 is a 16-wide byte map at B9A0, level 4 a 4-wide one at BAA0. A
 * level above 4 answers level - 4. */
static int start_terrain_tile(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 39)) return 0;                                   /* the longest path to a CALL or the RET */
    unsigned n = 5;
    frame_open(c, 0);
    cpu_push16(c, c->r[R_SI]);
    int outside = 1;
    alu_sub(c, bp_get(c, 6), 0, 1, 0);
    if (!x86_cond(c, 0xC)) {
        alu_sub(c, bp_get(c, 8), 0, 1, 0);
        n += 2;
        if (!x86_cond(c, 0xC)) {
            c->r[R_AX] = bp_get(c, 6);
            c->r[R_BX] = x86_shift(c, 4, bp_get(c, 4), 1, 1);
            const uint16_t size = ds_get(c, (uint16_t)(c->r[R_BX] + 0x107E));
            alu_sub(c, size, c->r[R_AX], 1, 0);
            n += 5;
            if (!x86_cond(c, 0xE)) {
                c->r[R_AX] = bp_get(c, 8);
                alu_sub(c, size, c->r[R_AX], 1, 0);
                n += 3;
                outside = !x86_cond(c, 0xF);
            }
        }
    }
    if (outside) {
        c->r[R_AX] = 0xFFFF;
        c->icount += n + 2 + 4;
        c->r[R_SI] = cpu_pop16(c);
        frame_close_ret(c);
        return 1;
    }
    uint16_t ax = bp_get(c, 4);
    n += 2;                                                       /* mov ax, [bp+4] / jmp to the switch */
    alu_logic(c, ax, 1);                                          /* or ax, ax */
    n += 2;
    int level = 0;
    if (!(c->flags & F_ZF)) {
        n++;                                                      /* level 0's JMP (a JNE over it otherwise) */
        do { ax = (uint16_t)alu_dec(c, ax, 1); n += 2; level++; } while (!(c->flags & F_ZF) && level < 4);
    } else n++;
    c->r[R_AX] = ax;
    if (!(c->flags & F_ZF)) {                                     /* no case */
        n--;                                                      /* (counted a JMP that is not there) */
        c->icount += n + 4;
        c->r[R_SI] = cpu_pop16(c);
        frame_close_ret(c);
        return 1;
    }
    if (level == 2) n--;                                          /* levels 2-4 are reached by JE, no JMP */
    if (level >= 3) n--;
    if (level <= 2) {
        static const uint16_t table[3] = { 0xB3A0, 0xB5A0, 0xB7A0 };
        static const uint16_t back[3] = { 0x7CA0, 0x7CDB, 0x7D13 };
        c->r[R_AX] = x86_shift(c, 7, x86_shift(c, 7, bp_get(c, 8), 1, 1), 1, 1);
        cpu_push16(c, c->r[R_AX]);
        c->r[R_AX] = x86_shift(c, 7, x86_shift(c, 7, bp_get(c, 6), 1, 1), 1, 1);
        cpu_push16(c, c->r[R_AX]);
        c->r[R_AX] = (uint16_t)(level + 1);
        cpu_push16(c, c->r[R_AX]);
        c->icount += n + 10;
        if (!guest_call(m, 0x7C56, back[level])) return 1;
        if (!room(c, 19)) { c->ip = back[level]; return 1; }
        c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
        set_r8(c, R_CL, 4);
        uint16_t si = x86_shift(c, 4, c->r[R_AX], 4, 1);          /* the block, 16 bytes */
        set_r8(c, R_AL, bp_get8(c, 8));
        ax = (uint16_t)alu_logic(c, c->r[R_AX] & 3, 1);
        ax = x86_shift(c, 4, x86_shift(c, 4, ax, 1, 1), 1, 1);   /* (y & 3) * 4 */
        si = (uint16_t)alu_add(c, si, ax, 1, 0);
        c->r[R_SI] = si;
        set_r8(c, R_BL, bp_get8(c, 6));
        c->r[R_BX] = (uint16_t)alu_logic(c, c->r[R_BX] & 3, 1);
        c->r[R_AX] = ax;
        set_r8(c, R_AL, ds_get8(c, (uint16_t)(c->r[R_BX] + si + table[level])));
        c->icount += level ? 13 : 12;                             /* to the byte read (and its JMP) */
    } else if (level == 3) {
        set_r8(c, R_CL, 4);
        c->r[R_SI] = x86_shift(c, 4, bp_get(c, 8), 4, 1);
        c->r[R_BX] = bp_get(c, 6);
        set_r8(c, R_AL, ds_get8(c, (uint16_t)(c->r[R_BX] + c->r[R_SI] + 0xB9A0)));
        c->icount += n + 6;
    } else {
        c->r[R_SI] = x86_shift(c, 4, x86_shift(c, 4, bp_get(c, 8), 1, 1), 1, 1);
        c->r[R_BX] = bp_get(c, 6);
        set_r8(c, R_AL, ds_get8(c, (uint16_t)(c->r[R_BX] + c->r[R_SI] + 0xBAA0)));
        c->icount += n + 6;
    }
    set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));   /* 0x07CBF */
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 2 + 4;                                           /* sub ah, ah / jmp, and the epilogue */
    frame_close_ret(c);
    return 1;
}

/* Normalise joystick axis SI (0-3), START 0x084B1 and END 0x0441D: the raw
 * reading [raw+2i] against the centre [centre+2i] becomes a byte at
 * [out+i]: 7Fh at the centre; below it (the difference negative, signed)
 * 0..7Fh scaled by the low range [lo+2i], a reading at or under the recorded
 * minimum [min+2i] becoming the new minimum (and range) with 00h; above it
 * 80h..FFh scaled by the high range [hi+2i], a reading at or past the maximum
 * [max+2i] giving FFh - and in END's copy also the new maximum and range
 * (START's leaves them). AX, DX, DS preserved. Where the original's DIV
 * would fault, the routine stops at the DIV and the original takes the
 * fault. */
typedef struct {
    uint16_t raw, centre, min, max, lo, hi, out;   /* the word tables, and the byte results */
    uint16_t div_lo, div_hi;                       /* the two DIVs */
    int widen;                                     /* a reading past the maximum widens it */
} axis_variant;

static int axis_normalise(machine_t *m, const axis_variant *v)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 23)) return 0;                                   /* the longest path */
    const uint16_t ds = c->seg[S_DS];
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, ds);
    uint16_t si = x86_shift(c, 4, c->r[R_SI], 1, 1);             /* shl si, 1 */
    c->r[R_SI] = si;
    uint16_t ax = ds_get(c, (uint16_t)(si + v->raw));
    c->r[R_DX] = ax;
    ax = (uint16_t)alu_sub(c, ax, ds_get(c, (uint16_t)(si + v->centre)), 1, 0);
    c->r[R_AX] = ax;
    unsigned n = 8;
    if (c->flags & F_ZF) {                                        /* at the centre */
        set_r8(c, R_AH, 0x7F);
        n += 2;
    } else if (n++, c->flags & F_SF) {                            /* below the centre */
        alu_sub(c, c->r[R_DX], ds_get(c, (uint16_t)(si + v->min)), 1, 0);
        n += 2;
        if (!x86_cond(c, 0x7)) {                                  /* at or under the minimum: a new one */
            ds_put(c, (uint16_t)(si + v->min), c->r[R_DX]);
            c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);
            ds_put(c, (uint16_t)(si + v->lo), c->r[R_AX]);
            c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
            n += 5;
        } else {
            c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);   /* neg ax */
            c->r[R_DX] = c->r[R_AX];
            c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
            const uint16_t range = ds_get(c, (uint16_t)(si + v->lo));
            if (!range || c->r[R_DX] >= range) {                  /* the DIV faults: leave it to the original */
                c->icount += n + 3;
                c->ip = v->div_lo;
                return 1;
            }
            x86_div16(c, range);
            c->r[R_AX] = (uint16_t)~c->r[R_AX];
            c->r[R_AX] = x86_shift(c, 5, c->r[R_AX], 1, 1);
            n += 6;
        }
    } else {                                                      /* above the centre */
        alu_sub(c, c->r[R_DX], ds_get(c, (uint16_t)(si + v->max)), 1, 0);
        n += 2;
        if (!(c->flags & F_CF)) {                                 /* at or past the maximum */
            if (v->widen) {
                ds_put(c, (uint16_t)(si + v->max), c->r[R_DX]);
                ds_put(c, (uint16_t)(si + v->hi), c->r[R_AX]);
                n += 2;
            }
            c->r[R_AX] = (uint16_t)~alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
            n += 3;
        } else {
            c->r[R_DX] = c->r[R_AX];
            c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
            const uint16_t range = ds_get(c, (uint16_t)(si + v->hi));
            if (!range || c->r[R_DX] >= range) {
                c->icount += n + 2;
                c->ip = v->div_hi;
                return 1;
            }
            x86_div16(c, range);
            c->r[R_AX] = x86_shift(c, 5, c->r[R_AX], 1, 1);
            set_r8(c, R_AH, (uint8_t)alu_add(c, get_r8(c, R_AH), 0x80, 0, 0));
            n += 5;
        }
    }
    si = x86_shift(c, 5, c->r[R_SI], 1, 1);                       /* shr si, 1 */
    c->r[R_SI] = si;
    ds_put8(c, (uint16_t)(si + v->out), get_r8(c, R_AH));
    c->seg[S_DS] = cpu_pop16(c);
    c->r[R_DX] = cpu_pop16(c);
    c->r[R_AX] = cpu_pop16(c);
    c->icount += n + 6;
    near_ret(c);
    return 1;
}
static const axis_variant START_AXIS = { 0x69AA, 0x6992, 0x6982, 0x698A, 0x699A, 0x69A2, 0x69B2, 0x84E4, 0x8507, 0 };
static const axis_variant END_AXIS = { 0x23EC, 0x23D4, 0x23C4, 0x23CC, 0x23DC, 0x23E4, 0x23F4, 0x4450, 0x447B, 1 };
static int start_axis_normalise(machine_t *m) { return axis_normalise(m, &START_AXIS); }
static int end_axis_normalise(machine_t *m) { return axis_normalise(m, &END_AXIS); }

/* END 0x00EF3, time_string(t, buffer): the debriefing's clock text, t in
 * 2-second units as in START's, first moved on by (([71F0] + [7202]) & 0Fh)
 * * 256. The template at 2C5h is copied (0x05150) and its digits advanced:
 * the hours' tens by byte 33h of the settings at far [7222], the units by
 * t / 1800 % 10 plus 6 (8 when that byte is not zero), carried into the tens
 * past '9'; the minutes' digits from t / 30 % 60 and the seconds' from
 * t * 2 % 60 (unsigned divides). Returns the buffer. */
static int end_time_string(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 13)) return 0;
    frame_open(c, 8);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_AX] = (uint16_t)alu_add(c, ds_get(c, 0x71F0), ds_get(c, 0x7202), 1, 0);
    set_r8(c, R_AH, get_r8(c, R_AL));
    c->r[R_AX] = (uint16_t)alu_logic(c, c->r[R_AX] & 0x0F00, 1);
    bp_put(c, 4, (uint16_t)alu_add(c, bp_get(c, 4), c->r[R_AX], 1, 0));
    c->r[R_AX] = 0x2C5;
    cpu_push16(c, 0x2C5);
    cpu_push16(c, bp_get(c, 6));
    c->icount += 12;
    if (!guest_call(m, 0x5150, 0x0F13)) return 1;
    if (!room(c, 62)) { c->ip = 0x0F13; return 1; }               /* the longest way to the RET */
    const uint16_t ds = c->seg[S_DS];
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    uint16_t far = ds_get(c, 0x7222), es = ds_get(c, 0x7224);
    c->r[R_BX] = far;
    c->seg[S_ES] = es;
    set_r8(c, R_AL, mem_read8(c, phys(es, (uint16_t)(far + 0x33))));
    const uint16_t buf = bp_get(c, 6);
    c->r[R_BX] = buf;
    mem_write8(c, phys(ds, buf), (uint8_t)alu_add(c, mem_read8(c, phys(ds, buf)), get_r8(c, R_AL), 0, 0));
    c->r[R_AX] = bp_get(c, 4);
    c->r[R_CX] = 0x708;
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, 0x708);                                          /* t / 1800 */
    c->r[R_CX] = 0x0A;
    cwd(c);
    x86_idiv16(c, 0x0A, 0);                                       /* ... % 10 in DL */
    const uint16_t units = (uint16_t)(buf + 1);
    mem_write8(c, phys(ds, units), (uint8_t)alu_add(c, mem_read8(c, phys(ds, units)), get_r8(c, R_DL), 0, 0));
    far = ds_get(c, 0x7222);
    es = ds_get(c, 0x7224);
    c->r[R_SI] = far;
    c->seg[S_ES] = es;
    alu_sub(c, mem_read8(c, phys(es, (uint16_t)(far + 0x33))), 1, 0, 0);   /* cmp byte es:[si+33h], 1 */
    uint8_t al = (uint8_t)alu_sub(c, get_r8(c, R_AL), get_r8(c, R_AL), 0, (c->flags & F_CF) ? 1u : 0u);   /* sbb al, al */
    al = (uint8_t)alu_logic(c, al & 0xFE, 0);
    al = (uint8_t)alu_add(c, al, 8, 0, 0);
    set_r8(c, R_AL, al);
    mem_write8(c, phys(ds, units), (uint8_t)alu_add(c, mem_read8(c, phys(ds, units)), al, 0, 0));
    alu_sub(c, mem_read8(c, phys(ds, units)), 0x39, 0, 0);
    unsigned n = 22;
    if (!x86_cond(c, 0x6)) {                                      /* past '9': carry into the tens */
        al = (uint8_t)alu_sub(c, mem_read8(c, phys(ds, units)), 0x0A, 0, 0);
        set_r8(c, R_AL, al);
        mem_write8(c, phys(ds, units), al);
        mem_write8(c, phys(ds, buf), (uint8_t)alu_inc(c, mem_read8(c, phys(ds, buf)), 0));
        n += 4;
    }
    c->r[R_CX] = 0x3C;
    c->r[R_AX] = bp_get(c, 4);
    c->r[R_BX] = 0x1E;
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, 0x1E);                                           /* t / 30 */
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, 0x3C);                                           /* ... % 60: the minutes */
    c->r[R_AX] = c->r[R_DX];
    bp_put(c, -6, c->r[R_AX]);
    c->r[R_BX] = 0x0A;
    cwd(c);
    x86_idiv16(c, 0x0A, 0);
    const uint16_t si = bp_get(c, 6);
    c->r[R_SI] = si;
    mem_write8(c, phys(ds, (uint16_t)(si + 3)), (uint8_t)alu_add(c, mem_read8(c, phys(ds, (uint16_t)(si + 3))), get_r8(c, R_AL), 0, 0));
    c->r[R_AX] = bp_get(c, -6);
    cwd(c);
    x86_idiv16(c, 0x0A, 0);
    mem_write8(c, phys(ds, (uint16_t)(si + 4)), (uint8_t)alu_add(c, mem_read8(c, phys(ds, (uint16_t)(si + 4))), get_r8(c, R_DL), 0, 0));
    c->r[R_AX] = x86_shift(c, 4, bp_get(c, 4), 1, 1);             /* t * 2 */
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, 0x3C);                                           /* % 60: the seconds */
    c->r[R_AX] = c->r[R_DX];
    c->r[R_CX] = c->r[R_DX];
    cwd(c);
    x86_idiv16(c, 0x0A, 0);
    mem_write8(c, phys(ds, (uint16_t)(si + 6)), (uint8_t)alu_add(c, mem_read8(c, phys(ds, (uint16_t)(si + 6))), get_r8(c, R_AL), 0, 0));
    c->r[R_AX] = c->r[R_CX];
    cwd(c);
    x86_idiv16(c, 0x0A, 0);
    mem_write8(c, phys(ds, (uint16_t)(si + 7)), (uint8_t)alu_add(c, mem_read8(c, phys(ds, (uint16_t)(si + 7))), get_r8(c, R_DL), 0, 0));
    c->r[R_AX] = si;
    c->r[R_SI] = cpu_pop16(c);
    c->icount += n + 36;
    frame_close_ret(c);
    return 1;
}

/* END 0x02AFA, promote(): unless the mission's result word (far [7222]+30h)
 * is set (AX = 0), the ranks from 5 down are tried against the pilot (far
 * [55DE]): while the pilot's rank (+20h) is not above it, rank r is earned
 * with points (+32h, 32 bits) of at least the word at 5B0h + 2r, missions
 * (+36h) of at least 5C8h + 2r, and points per mission (0x05302) of at least
 * 5BCh + 2r. Earned, the pilot is promoted ([55E6] = 1, the rank goes up)
 * when below rank 5 and [6982] is not 1 (with it 1, [55E7] = 1 instead), or
 * at rank 5 with exactly 99 missions. Returns AX as the last value loaded. */
static int end_promote(machine_t *m)
{
    cpu_t *c = &m->cpu;
#define NEED(n, at) do { if (!room(c, (n))) { c->ip = (at); return 1; } } while (0)
    if (!room(c, 15)) return 0;
    frame_open(c, 2);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_BX] = ds_get(c, 0x7222);
    c->seg[S_ES] = ds_get(c, 0x7224);
    alu_sub(c, seg_read16(c, c->seg[S_ES], (uint16_t)(c->r[R_BX] + 0x30)), 0, 1, 0);
    c->icount += 8;
    if (!(c->flags & F_ZF)) {                                     /* a result: no promotion */
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
        c->icount += 2;
        goto out;
    }
    bp_put(c, -2, 5);                                             /* the rank tried */
    c->icount += 2;
    for (;;) {                                                    /* 0x02B31 */
        NEED(23, 0x2B31);
        c->r[R_AX] = bp_get(c, -2);
        c->r[R_BX] = ds_get(c, 0x55DE);
        c->seg[S_ES] = ds_get(c, 0x55E0);
        const uint16_t es = c->seg[S_ES], bx = c->r[R_BX];
        alu_sub(c, seg_read16(c, es, (uint16_t)(bx + 0x20)), c->r[R_AX], 1, 0);
        c->icount += 4;
        if (x86_cond(c, 0xF)) goto out;                           /* the pilot's rank is above it */
        c->r[R_SI] = x86_shift(c, 4, c->r[R_AX], 1, 1);
        c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_SI] + 0x5B0));   /* the points needed, 0:AX */
        c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
        alu_sub(c, c->r[R_DX], seg_read16(c, es, (uint16_t)(bx + 0x34)), 1, 0);
        c->icount += 6;
        int earned = 0;
        if (!x86_cond(c, 0xF)) {
            c->icount += 1;
            int enough = x86_cond(c, 0xC);                        /* the high word is below the pilot's */
            if (!enough) {
                alu_sub(c, c->r[R_AX], seg_read16(c, es, (uint16_t)(bx + 0x32)), 1, 0);
                c->icount += 2;
                enough = !x86_cond(c, 0x7);
            }
            if (enough) {                                         /* 0x02B56: the missions */
                c->r[R_AX] = seg_read16(c, es, (uint16_t)(bx + 0x36));
                alu_sub(c, ds_get(c, (uint16_t)(c->r[R_SI] + 0x5C8)), c->r[R_AX], 1, 0);
                c->icount += 3;
                earned = !x86_cond(c, 0x7);
            }
        }
        if (!earned) {                                            /* 0x02B2E: the next rank down */
            bp_put(c, -2, (uint16_t)alu_dec(c, bp_get(c, -2), 1));
            c->icount += 1;
            continue;
        }
        c->r[R_CX] = c->r[R_AX];
        cpu_push16(c, c->r[R_DX]);
        cpu_push16(c, c->r[R_AX]);
        cpu_push16(c, seg_read16(c, es, (uint16_t)(bx + 0x34)));
        cpu_push16(c, seg_read16(c, es, (uint16_t)(bx + 0x32)));
        c->r[R_DI] = c->r[R_AX];                                  /* the missions */
        c->icount += 6;
        if (!guest_call_pop(m, 0x5302, 0x2B71, 8)) return 1;      /* points per mission */
        NEED(23, 0x2B71);
        c->r[R_CX] = ds_get(c, (uint16_t)(c->r[R_SI] + 0x5BC));
        c->r[R_BX] = (uint16_t)alu_sub(c, c->r[R_BX], c->r[R_BX], 1, 0);
        alu_sub(c, c->r[R_DX], 0, 1, 0);                          /* cmp dx, bx */
        c->icount += 4;
        int average = 0;
        if (!x86_cond(c, 0xC)) {
            c->icount += 1;
            average = x86_cond(c, 0xF);
            if (!average) {
                alu_sub(c, c->r[R_AX], c->r[R_CX], 1, 0);
                c->icount += 2;
                average = !(c->flags & F_CF);
            }
        }
        if (!average) {
            bp_put(c, -2, (uint16_t)alu_dec(c, bp_get(c, -2), 1));
            c->icount += 1;
            continue;
        }
        c->r[R_BX] = ds_get(c, 0x55DE);                           /* 0x02B81 */
        c->seg[S_ES] = ds_get(c, 0x55E0);
        const uint16_t rank_at = (uint16_t)(c->r[R_BX] + 0x20);
        alu_sub(c, seg_read16(c, c->seg[S_ES], rank_at), 5, 1, 0);
        c->icount += 3;
        int promote = 0;
        if (c->flags & F_ZF) {                                    /* at rank 5: only with 99 missions */
            alu_sub(c, c->r[R_DI], 0x63, 1, 0);
            c->icount += 2;
            promote = (c->flags & F_ZF) != 0;
        }
        if (!promote) {                                           /* 0x02B19 */
            alu_sub(c, seg_read16(c, c->seg[S_ES], rank_at), 5, 1, 0);
            c->icount += 2;
            if (!(c->flags & F_CF)) goto out;                     /* rank 5 or above */
            alu_sub(c, ds_get8(c, 0x6982), 1, 0, 0);
            c->icount += 2;
            if (c->flags & F_ZF) { ds_put8(c, 0x55E7, 1); c->icount += 2; goto out; }
        }
        ds_put8(c, 0x55E6, 1);                                    /* 0x02B91 */
        seg_write16(c, c->seg[S_ES], rank_at, (uint16_t)alu_inc(c, seg_read16(c, c->seg[S_ES], rank_at), 1));
        c->icount += 2;
        goto out;
    }
out:
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->icount += 5;
    frame_close_ret(c);
    return 1;
#undef NEED
}

/* END 0x02476, awards(): the ribbons, the medal and the next rank step after
 * a mission, for the pilot at far [55DE] and the mission's result at far
 * [7222] (+30h, zero for a mission flown home; +34h flags; +36h).
 * - +44h, once: set (and [55DD] = 1) when the result is set and [6D89] or
 *   [720E] is.
 * - With no result, by the pilot's missions (+36h): +46h at 5-9, +48h from
 *   10 (clearing +46h), +4Ah at 30-59, +4Ch from 60 (clearing +4Ah); each new
 *   one is [55E2] = 1-4.
 * - +22h, once: set (and [643A] = 1) when the result's +36h is 7 or more and
 *   its flags +34h have bits 3 and 2.
 * - The point steps: +2Ch past 1200 points (+30h); else +2Ah, +28h, +26h at
 *   (count + 1) * 900, 600, 300; else +24h (below 9) at (count + 1) * 100.
 *   The first step reached is the award 6..2: given at once ([643A], and the
 *   count goes up) when [6982] and the result are zero, else noted in [55DA].
 * SI is preserved; the routine has no frame. */
static int end_awards(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 110)) return 0;                                  /* the longest path */
    unsigned n = 0;
#define LES(reg, at) do { c->r[reg] = ds_get(c, (at)); c->seg[S_ES] = ds_get(c, (uint16_t)((at) + 2)); n++; } while (0)
#define ESW(off) seg_read16(c, c->seg[S_ES], (uint16_t)(c->r[R_BX] + (off)))
#define ESPUT(off, v) seg_write16(c, c->seg[S_ES], (uint16_t)(c->r[R_BX] + (off)), (v))
    cpu_push16(c, c->r[R_SI]);
    n++;
    LES(R_BX, 0x55DE);
    alu_sub(c, ESW(0x44), 0, 1, 0);
    n += 2;
    if (c->flags & F_ZF) {                                        /* the one-time award */
        LES(R_BX, 0x7222);
        alu_sub(c, ESW(0x30), 0, 1, 0);
        n += 2;
        if (!(c->flags & F_ZF)) {
            alu_sub(c, ds_get8(c, 0x6D89), 0, 0, 0);
            n += 2;
            int give = !(c->flags & F_ZF);
            if (!give) { alu_sub(c, ds_get8(c, 0x720E), 0, 0, 0); n += 2; give = !(c->flags & F_ZF); }
            if (give) {
                LES(R_BX, 0x55DE);
                ESPUT(0x44, 1);
                ds_put8(c, 0x55DD, 1);
                n += 2;
            }
        }
    }
    LES(R_BX, 0x7222);                                            /* 0x024AA */
    alu_sub(c, ESW(0x30), 0, 1, 0);
    n += 2;
    if (!(c->flags & F_ZF)) n++;                                  /* jmp past the ribbons */
    else {
        /* The service ribbons by missions flown: (field, from, below, clears, ribbon). */
        static const struct { uint8_t field, from, below, clears, ribbon; } rib[4] = {
            { 0x46, 5, 10, 0, 1 }, { 0x48, 10, 0, 0x46, 2 }, { 0x4A, 30, 60, 0, 3 }, { 0x4C, 60, 0, 0x4A, 4 } };
        for (int k = 0; k < 4; k++) {
            LES(R_BX, 0x55DE);
            alu_sub(c, ESW(rib[k].field), 0, 1, 0);
            n += 2;
            if (!(c->flags & F_ZF)) continue;                     /* had it */
            alu_sub(c, ESW(0x36), rib[k].from, 1, 0);
            n += 2;
            if (c->flags & F_CF) continue;                        /* too few */
            if (rib[k].below) {
                alu_sub(c, ESW(0x36), rib[k].below, 1, 0);
                n += 2;
                if (!(c->flags & F_CF)) continue;                 /* the next one's range */
            }
            if (rib[k].clears) { ESPUT(rib[k].clears, 0); n++; LES(R_BX, 0x55DE); }
            ESPUT(rib[k].field, 1);
            ds_put8(c, 0x55E2, rib[k].ribbon);
            n += 2;
        }
    }
    LES(R_BX, 0x55DE);                                            /* 0x0254E: the medal */
    alu_sub(c, ESW(0x22), 0, 1, 0);
    n += 2;
    if (c->flags & F_ZF) {
        c->r[R_AX] = c->seg[S_ES];
        n++;
        LES(R_SI, 0x7222);
        const uint16_t res = c->r[R_SI];
        alu_sub(c, seg_read16(c, c->seg[S_ES], (uint16_t)(res + 0x36)), 7, 1, 0);
        n += 2;
        if (!(c->flags & F_CF)) {
            c->r[R_CX] = seg_read16(c, c->seg[S_ES], (uint16_t)(res + 0x34));
            c->r[R_DX] = c->r[R_CX];
            alu_logic(c, get_r8(c, R_CL) & 8, 0);
            n += 4;
            if (!(c->flags & F_ZF)) {
                alu_logic(c, get_r8(c, R_DL) & 4, 0);
                n += 2;
                if (!(c->flags & F_ZF)) {
                    c->seg[S_ES] = c->r[R_AX];
                    ESPUT(0x22, 1);
                    ds_put8(c, 0x643A, 1);
                    n += 3;
                }
            }
        }
    }
    LES(R_BX, 0x55DE);                                            /* 0x02583: the point steps */
    alu_sub(c, ESW(0x2C), 0, 1, 0);
    n += 2;
    int award = 0;
    uint8_t field = 0;
    if (c->flags & F_ZF) {
        alu_sub(c, ESW(0x30), 0x4B0, 1, 0);
        n += 2;
        if (x86_cond(c, 0x7)) { award = 6; field = 0x2C; }
    }
    static const struct { uint8_t field; uint16_t step; uint8_t award; } pts[4] = {
        { 0x2A, 0x384, 5 }, { 0x28, 0x258, 4 }, { 0x26, 0x12C, 3 }, { 0x24, 0x64, 2 } };
    for (int k = 0; k < 4 && !award; k++) {
        if (pts[k].field == 0x24) {                               /* the last step stops at 9 */
            alu_sub(c, ESW(0x24), 9, 1, 0);
            n += 2;
            if (!(c->flags & F_CF)) break;
        }
        c->r[R_AX] = (uint16_t)alu_inc(c, ESW(pts[k].field), 1);
        c->r[R_CX] = pts[k].step;
        x86_mul16(c, pts[k].step);
        alu_sub(c, c->r[R_AX], ESW(0x30), 1, 0);
        n += 6;
        if (!x86_cond(c, 0x7)) { award = pts[k].award; field = pts[k].field; }
    }
    if (award) {
        alu_sub(c, ds_get8(c, 0x6982), 0, 0, 0);
        n += 2;
        int now = 0;
        if (c->flags & F_ZF) {
            LES(R_BX, 0x7222);
            alu_sub(c, ESW(0x30), 0, 1, 0);
            n += 2;
            now = (c->flags & F_ZF) != 0;
        }
        if (now) {                                                /* given: the count goes up */
            ds_put8(c, 0x643A, (uint8_t)award);
            LES(R_BX, 0x55DE);
            if (award == 6) ESPUT(0x2C, 1);
            else ESPUT(field, (uint16_t)alu_inc(c, ESW(field), 1));
            n += 2;
        } else {
            ds_put8(c, 0x55DA, (uint8_t)award);
            n += (award == 6) ? 1 : 2;                            /* (rank 6 returns there; the rest JMP to the RET) */
        }
    }
    c->r[R_SI] = cpu_pop16(c);
    c->icount += n + 2;
    near_ret(c);
    return 1;
#undef LES
#undef ESW
#undef ESPUT
}

/* START 0x0819D, the LZW reader's refill: the next 512 bytes of the packed
 * image, at the far pointer [6497]:[6499] (read through SS), are copied to
 * the input buffer DS:6235 (REP MOVSW, stepping by DF) and the pointer
 * advanced; AX = 200h, the bytes now buffered. */
static int start_lzw_refill(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 10 + 256 + 7)) return 0;
    const uint16_t ss = c->seg[S_SS], ds = c->seg[S_DS];
    cpu_push16(c, ds);
    cpu_push16(c, c->seg[S_ES]);
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    c->r[R_AX] = ds;
    c->seg[S_ES] = ds;
    c->seg[S_DS] = seg_read16(c, ss, 0x6497);
    c->r[R_CX] = 0x100;
    c->r[R_SI] = seg_read16(c, ss, 0x6499);
    c->r[R_DI] = 0x6235;
    const unsigned n = rep_string(c, STR_MOVS, 1, c->seg[S_DS], 0);
    seg_write16(c, ss, 0x6499, (uint16_t)alu_add(c, seg_read16(c, ss, 0x6499), 0x200, 1, 0));
    c->r[R_AX] = 0x200;
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->seg[S_ES] = cpu_pop16(c);
    c->seg[S_DS] = cpu_pop16(c);
    c->icount += 10 + n + 7;
    near_ret(c);
    return 1;
}

/* START 0x0A76C, the heap's free(block) for the near heap described at AE1E:
 * a block above the heap's start ([AE24]) has its header word (at block - 2)
 * marked free (bit 0), and the rover [AE26] moves back to that header when it
 * lies above it. BX is left as the header's address. */
static int start_heap_free(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 18)) return 0;
    frame_open(c, 0);
    cpu_push16(c, c->r[R_SI]);
    uint16_t bx = bp_get(c, 4);
    c->r[R_SI] = 0xAE1E;
    alu_sub(c, ds_get(c, 0xAE24), bx, 1, 0);                      /* cmp [si+6], bx */
    unsigned n = 8;                                               /* with the JMP at the entry */
    if (c->flags & F_CF) {                                        /* jae not taken: a heap block */
        bx = (uint16_t)alu_dec(c, alu_dec(c, bx, 1), 1);
        ds_put8(c, bx, (uint8_t)alu_logic(c, ds_get8(c, bx) | 1, 0));
        alu_sub(c, ds_get(c, 0xAE26), bx, 1, 0);                  /* cmp [si+8], bx */
        n += 5;
        if (!x86_cond(c, 0x6)) { ds_put(c, 0xAE26, bx); n++; }    /* jbe not taken */
    }
    c->r[R_BX] = bx;
    c->r[R_SI] = cpu_pop16(c);
    c->icount += n + 4;
    frame_close_ret(c);
    return 1;
}

/* START 0x025D9, date_string(buffer): the mission's date. The theatre (far
 * [CACA]+38h) picks a starting month (byte 882h + theatre), year (word 896h +
 * 2 * theatre) and day (byte 88Ch + theatre, plus the day count at +36h);
 * whole months (lengths at 8C2h + month) are taken off while the day exceeds
 * the month's length, a month past 12 wraps into the next year, and
 * the text is printed by 0x0959E with the format at 878h: the month's name
 * (word 8A8h + 2 * month), the day, the year. Returns the buffer. */
static int start_date_string(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 19)) return 0;
    frame_open(c, 6);
    cpu_push16(c, c->r[R_SI]);
    const uint16_t bx = ds_get(c, 0xCACA);
    c->seg[S_ES] = ds_get(c, 0xCACC);
    c->r[R_BX] = bx;
    const uint16_t theatre = seg_read16(c, c->seg[S_ES], (uint16_t)(bx + 0x38));
    set_r8(c, R_AL, ds_get8(c, (uint16_t)(theatre + 0x882)));
    set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
    bp_put(c, -4, c->r[R_AX]);                                    /* month */
    c->r[R_AX] = theatre;
    c->r[R_SI] = x86_shift(c, 4, theatre, 1, 1);
    c->r[R_CX] = ds_get(c, (uint16_t)(c->r[R_SI] + 0x896));
    bp_put(c, -2, c->r[R_CX]);                                    /* year */
    c->r[R_SI] = theatre;
    set_r8(c, R_AL, ds_get8(c, (uint16_t)(theatre + 0x88C)));
    set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], seg_read16(c, c->seg[S_ES], (uint16_t)(bx + 0x36)), 1, 0);
    bp_put(c, -6, c->r[R_AX]);                                    /* day */
    c->icount += 19;
    for (;;) {                                                    /* 0x02613 */
        if (!room(c, 18)) { c->ip = 0x2613; return 1; }           /* a pass, or the last one to the CALL */
        c->r[R_BX] = bp_get(c, -4);
        set_r8(c, R_AL, ds_get8(c, (uint16_t)(c->r[R_BX] + 0x8C2)));
        set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
        alu_sub(c, c->r[R_AX], bp_get(c, -6), 1, 0);
        c->icount += 5;
        if (!(c->flags & F_CF)) break;                            /* the day lies in this month */
        bp_put(c, -6, (uint16_t)alu_sub(c, bp_get(c, -6), c->r[R_AX], 1, 0));
        bp_put(c, -4, (uint16_t)alu_inc(c, bp_get(c, -4), 1));
        c->icount += 2;
    }
    alu_sub(c, c->r[R_BX], 0x0C, 1, 0);
    c->icount += 2;
    if (!x86_cond(c, 0x6)) {                                      /* past December */
        bp_put(c, -4, (uint16_t)alu_sub(c, bp_get(c, -4), 0x0C, 1, 0));
        bp_put(c, -2, (uint16_t)alu_inc(c, bp_get(c, -2), 1));
        c->icount += 2;
    }
    cpu_push16(c, bp_get(c, -2));
    cpu_push16(c, bp_get(c, -6));
    c->r[R_BX] = x86_shift(c, 4, bp_get(c, -4), 1, 1);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x8A8)));
    c->r[R_AX] = 0x878;
    cpu_push16(c, 0x878);
    cpu_push16(c, bp_get(c, 4));
    c->icount += 8;
    if (!guest_call(m, 0x959E, 0x2646)) return 1;
    if (!room(c, 6)) { c->ip = 0x2646; return 1; }
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0A, 1, 0);
    c->r[R_AX] = bp_get(c, 4);
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 6;
    frame_close_ret(c);
    return 1;
}

/* A target record (16 bytes at CBDE) is shown on the briefing map when it has
 * a type (word +6) or bit 0 of +8 or bit 1 of +9 is set, and bit 3 of +9 is
 * clear. `n` counts the instructions of that test as the original makes it;
 * flags as it leaves them. */
static int site_shown(cpu_t *c, uint16_t rec, unsigned *n)
{
    alu_sub(c, ds_get(c, (uint16_t)(rec + 6)), 0, 1, 0);
    *n += 3;                                                      /* (with the load of the record) */
    if (c->flags & F_ZF) {
        c->r[R_AX] = ds_get(c, (uint16_t)(rec + 8));
        c->r[R_CX] = c->r[R_AX];
        alu_logic(c, get_r8(c, R_AL) & 1, 0);
        *n += 4;
        if (c->flags & F_ZF) {
            alu_logic(c, get_r8(c, R_CH) & 2, 0);
            *n += 2;
            if (c->flags & F_ZF) return 0;
        }
    }
    alu_logic(c, ds_get8(c, (uint16_t)(rec + 9)) & 8, 0);
    *n += 2;
    return (c->flags & F_ZF) != 0;
}

/* The briefing map's site boxes, START 0x01F3F and END's copy at 0x005CA (the
 * debriefing map). For each shown target i (16-byte records, count in a word):
 * unless it is marked a lone site (bit 3 of +8 with bit 2 of +9 clear), it is
 * paired with the first other shown target j at the same place word (+0); a
 * pair is boxed once, at the first of the two, around the midpoint (screen x
 * = sum / 124h, y = sum / 186h), and a site without a partner around its own
 * point (x / 92h, y / C3h). A shown target with a type has bit 6 of +9
 * cleared. START stores each box as four words from 6D62 on - the centre less
 * 7 (clamped at 0), and that plus 14 - with the pair (i, j or 0) in the byte
 * pairs at B286, the count at [B2E6] and [6D21] = count + 8. END stores the
 * centre less (1, 2) in the word tables at 54BA and 5532 and the pair at 5440,
 * the count at [54B8]. END loads the two loop counts the other way round (the
 * count compared with j, AX = j). */
typedef struct {
    int end;                                  /* END's copy */
    uint16_t recs, count, boxes;              /* the records, their count, the boxes made */
    int16_t p, i, q, j, x, y;                 /* frame offsets: i's record, i, j's record, j, the centre */
    uint16_t locals, inner, centre, outer, done;   /* frame size; the loop heads and the exit */
} site_box_variant;

static int site_boxes(machine_t *m, const site_box_variant *v)
{
    cpu_t *c = &m->cpu;
#define NEED(n, at) do { if (!room(c, (n))) { c->ip = (at); return 1; } } while (0)
    if (!room(c, 10)) return 0;
    frame_open(c, v->locals);
    cpu_push16(c, c->r[R_SI]);
    if (!v->end) bp_put(c, -2, 0x6D62);                           /* START: the next box */
    bp_put(c, v->p, v->recs);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    ds_put(c, v->boxes, 0);
    bp_put(c, v->i, 0);
    c->icount += v->end ? 9 : 10;
    for (;;) {                                                    /* each target */
        NEED(28, v->outer);
        c->r[R_AX] = bp_get(c, v->i);
        alu_sub(c, ds_get(c, v->count), c->r[R_AX], 1, 0);
        c->icount += 3;
        if (x86_cond(c, 0x6)) break;                              /* all seen */
        unsigned n = 0;
        c->r[R_BX] = bp_get(c, v->p);
        const int shown = site_shown(c, c->r[R_BX], &n);
        c->icount += n;
        if (shown) {
            alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 6)), 0, 1, 0);
            c->icount += 2;
            if (!(c->flags & F_ZF)) {
                const uint16_t at = (uint16_t)(c->r[R_BX] + 9);
                ds_put8(c, at, (uint8_t)alu_logic(c, ds_get8(c, at) & 0xBF, 0));
                c->icount += 1;
            }
            c->r[R_BX] = bp_get(c, v->p);
            c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 8));
            c->r[R_CX] = c->r[R_AX];
            alu_logic(c, get_r8(c, R_AL) & 8, 0);
            c->icount += 5;
            int lone = 0;
            if (!(c->flags & F_ZF)) {
                alu_logic(c, get_r8(c, R_CH) & 4, 0);
                c->icount += 2;
                lone = (c->flags & F_ZF) != 0;
            }
            if (lone) {                                           /* no partner to look for */
                c->r[R_AX] = ds_get(c, v->count);
                bp_put(c, v->j, c->r[R_AX]);
                c->icount += 3;
            } else {                                              /* find the partner j */
                bp_put(c, v->q, v->recs);
                bp_put(c, v->j, 0);
                c->icount += 4;                                   /* with the JMP to the search */
                for (;;) {
                    NEED(23, v->inner);
                    int more;
                    if (v->end) {
                        c->r[R_AX] = bp_get(c, v->j);
                        alu_sub(c, ds_get(c, v->count), c->r[R_AX], 1, 0);
                        more = !x86_cond(c, 0x6);                 /* jbe */
                    } else {
                        c->r[R_AX] = ds_get(c, v->count);
                        alu_sub(c, bp_get(c, v->j), c->r[R_AX], 1, 0);
                        more = (c->flags & F_CF) != 0;            /* jae */
                    }
                    c->icount += 3;
                    if (!more) break;                             /* none: j = count */
                    unsigned k = 0;
                    c->r[R_BX] = bp_get(c, v->q);
                    if (site_shown(c, c->r[R_BX], &k)) {
                        c->r[R_SI] = bp_get(c, v->p);
                        c->r[R_AX] = ds_get(c, c->r[R_SI]);
                        alu_sub(c, ds_get(c, c->r[R_BX]), c->r[R_AX], 1, 0);
                        k += 4;
                        if (c->flags & F_ZF) {
                            c->r[R_AX] = bp_get(c, v->i);
                            alu_sub(c, bp_get(c, v->j), c->r[R_AX], 1, 0);
                            k += 3;
                            if (!(c->flags & F_ZF)) { c->icount += k; break; }   /* found j */
                        }
                    }
                    bp_put(c, v->j, (uint16_t)alu_inc(c, bp_get(c, v->j), 1));
                    bp_put(c, v->q, (uint16_t)alu_add(c, bp_get(c, v->q), 0x10, 1, 0));
                    c->icount += k + 2;
                }
            }
            NEED(58, v->centre);                                  /* the box's centre */
            if (v->end) {
                c->r[R_AX] = bp_get(c, v->j);
                alu_sub(c, ds_get(c, v->count), c->r[R_AX], 1, 0);
            } else {
                c->r[R_AX] = ds_get(c, v->count);
                alu_sub(c, bp_get(c, v->j), c->r[R_AX], 1, 0);
            }
            c->icount += 3;
            int centre = 1;
            if (c->flags & F_ZF) {                                /* alone: its own point */
                c->r[R_BX] = bp_get(c, v->p);
                c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 2));
                c->r[R_CX] = 0x92;
                c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
                x86_div16(c, 0x92);
                bp_put(c, v->x, c->r[R_AX]);
                c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 4));
                c->r[R_CX] = 0xC3;
                c->icount += 9;
            } else {
                c->r[R_AX] = bp_get(c, v->i);
                alu_sub(c, bp_get(c, v->j), c->r[R_AX], 1, 0);
                c->icount += 3;
                if (x86_cond(c, 0x6)) centre = 0;                 /* the pair was boxed at j */
                else {                                            /* the pair's midpoint */
                    c->r[R_BX] = bp_get(c, v->q);
                    c->r[R_SI] = bp_get(c, v->p);
                    c->r[R_AX] = (uint16_t)alu_add(c, ds_get(c, (uint16_t)(c->r[R_BX] + 2)), ds_get(c, (uint16_t)(c->r[R_SI] + 2)), 1, 0);
                    c->r[R_CX] = 0x124;
                    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
                    x86_div16(c, 0x124);
                    bp_put(c, v->x, c->r[R_AX]);
                    c->r[R_AX] = (uint16_t)alu_add(c, ds_get(c, (uint16_t)(c->r[R_BX] + 4)), ds_get(c, (uint16_t)(c->r[R_SI] + 4)), 1, 0);
                    c->r[R_CX] = 0x186;
                    c->icount += 11;
                }
            }
            if (centre) {
                c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
                x86_div16(c, c->r[R_CX]);
                bp_put(c, v->y, c->r[R_AX]);
                c->icount += 3;
            }
            c->r[R_AX] = bp_get(c, v->i);
            alu_sub(c, bp_get(c, v->j), c->r[R_AX], 1, 0);
            c->icount += 3;
            if (!x86_cond(c, 0x6) && v->end) {                    /* END: the centre and the pair */
                c->r[R_CX] = (uint16_t)alu_dec(c, bp_get(c, v->x), 1);
                c->r[R_BX] = x86_shift(c, 4, ds_get(c, v->boxes), 1, 1);
                ds_put(c, (uint16_t)(c->r[R_BX] + 0x54BA), c->r[R_CX]);
                c->r[R_CX] = (uint16_t)alu_dec(c, alu_dec(c, bp_get(c, v->y), 1), 1);
                ds_put(c, (uint16_t)(c->r[R_BX] + 0x5532), c->r[R_CX]);
                ds_put8(c, (uint16_t)(c->r[R_BX] + 0x5440), get_r8(c, R_AL));
                c->r[R_AX] = bp_get(c, v->j);
                alu_sub(c, ds_get(c, v->count), c->r[R_AX], 1, 0);
                c->icount += 13;
                if (!x86_cond(c, 0x6)) {                          /* a partner */
                    ds_put8(c, (uint16_t)(c->r[R_BX] + 0x5441), get_r8(c, R_AL));
                    c->icount += 2;
                } else {
                    c->r[R_BX] = x86_shift(c, 4, ds_get(c, v->boxes), 1, 1);
                    ds_put8(c, (uint16_t)(c->r[R_BX] + 0x5441), 0);
                    c->icount += 3;
                }
                ds_put(c, v->boxes, (uint16_t)alu_inc(c, ds_get(c, v->boxes), 1));
                c->icount += 1;
            } else if (!x86_cond(c, 0x6)) {                       /* START: the box */
                for (int k = 0; k < 2; k++) {                     /* x0, then y0: the centre less 7, at least 0 */
                    const int16_t from = k ? v->y : v->x;
                    alu_sub(c, bp_get(c, from), 7, 1, 0);
                    c->icount += 2;
                    if (!(c->flags & F_CF)) {
                        c->r[R_AX] = (uint16_t)alu_sub(c, bp_get(c, from), 7, 1, 0);
                        c->r[R_BX] = bp_get(c, -2);
                        ds_put(c, (uint16_t)(c->r[R_BX] + 2 * k), c->r[R_AX]);
                        c->icount += 5;
                    } else {
                        c->r[R_BX] = bp_get(c, -2);
                        ds_put(c, (uint16_t)(c->r[R_BX] + 2 * k), 0);
                        c->icount += 2;
                    }
                }
                c->r[R_BX] = bp_get(c, -2);                       /* x1, y1 = x0, y0 + 14 */
                c->r[R_AX] = (uint16_t)alu_add(c, ds_get(c, c->r[R_BX]), 0x0E, 1, 0);
                ds_put(c, (uint16_t)(c->r[R_BX] + 4), c->r[R_AX]);
                c->r[R_AX] = (uint16_t)alu_add(c, ds_get(c, (uint16_t)(c->r[R_BX] + 2)), 0x0E, 1, 0);
                ds_put(c, (uint16_t)(c->r[R_BX] + 6), c->r[R_AX]);
                set_r8(c, R_AL, bp_get8(c, v->i));
                c->r[R_BX] = x86_shift(c, 4, ds_get(c, v->boxes), 1, 1);
                ds_put8(c, (uint16_t)(c->r[R_BX] - 0x4D7A), get_r8(c, R_AL));
                c->r[R_AX] = ds_get(c, v->count);
                alu_sub(c, bp_get(c, v->j), c->r[R_AX], 1, 0);
                c->icount += 14;
                if (c->flags & F_CF) {                            /* a partner */
                    set_r8(c, R_AL, bp_get8(c, v->j));
                    ds_put8(c, (uint16_t)(c->r[R_BX] - 0x4D79), get_r8(c, R_AL));
                } else {
                    c->r[R_BX] = x86_shift(c, 4, ds_get(c, v->boxes), 1, 1);
                    ds_put8(c, (uint16_t)(c->r[R_BX] - 0x4D79), 0);
                }
                c->icount += 3;
                bp_put(c, -2, (uint16_t)alu_add(c, bp_get(c, -2), 8, 1, 0));
                ds_put(c, v->boxes, (uint16_t)alu_inc(c, ds_get(c, v->boxes), 1));
                c->icount += 2;
            }
        }
        bp_put(c, v->i, (uint16_t)alu_inc(c, bp_get(c, v->i), 1));      /* the next target */
        bp_put(c, v->p, (uint16_t)alu_add(c, bp_get(c, v->p), 0x10, 1, 0));
        c->icount += 2;
    }
    NEED(7, v->done);
    if (!v->end) {
        set_r8(c, R_AL, (uint8_t)alu_add(c, ds_get8(c, v->boxes), 8, 0, 0));
        ds_put8(c, 0x6D21, get_r8(c, R_AL));
        c->icount += 3;
    }
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 4;
    frame_close_ret(c);
    return 1;
#undef NEED
}
static const site_box_variant START_BOXES = { 0, 0xCBDE, 0xD33C, 0xB2E6, -4, -0x0A, -0x0C, -0x0E, -6, -8, 0x0E, 0x1F6E, 0x1FA4, 0x207C, 0x20C9 };
static const site_box_variant END_BOXES = { 1, 0x56EA, 0x6446, 0x54B8, -2, -8, -0x0A, -0x0C, -4, -6, 0x0C, 0x05F4, 0x062B, 0x06C2, 0x070F };
static int start_site_boxes(machine_t *m) { return site_boxes(m, &START_BOXES); }
static int end_site_boxes(machine_t *m) { return site_boxes(m, &END_BOXES); }

/* START 0x02E50, range_ring(x, y, r, colour, solid): a circle on the map
 * screen around the map point (x / 92h, y / C3h) of radius r / 128, in the
 * colour (kept at [DC1C]), stepping the angle by 8/256 of a turn when solid,
 * 16 when dotted (8 from r 3000, 4 from 7000). Each point is (vsin(a, r) *
 * 128 / 92h + x, vcos(a, r) * 128 / -C3h + y); a solid ring joins it to the
 * last point with the line drawer (0x0829A on the page at DC18), a dotted
 * one (and the first point of either) plots it when it lies on the screen
 * (x below 320, y below 200, unsigned). */
static int start_range_ring(machine_t *m)
{
    cpu_t *c = &m->cpu;
#define NEED(n, at) do { if (!room(c, (n))) { c->ip = (at); return 1; } } while (0)
    if (!room(c, 32)) return 0;
    frame_open(c, 0x0E);
    c->r[R_AX] = bp_get(c, 0x0A);
    ds_put(c, 0xDC1C, c->r[R_AX]);
    alu_sub(c, bp_get(c, 0x0C), 0, 1, 0);
    unsigned n = 7;
    if (!(c->flags & F_ZF)) { bp_put(c, -8, 8); n += 2; }         /* the step */
    else { bp_put(c, -8, 0x10); n += 1; }
    static const uint16_t far_r[2] = { 0x0BB8, 0x1B58 }, fine[2] = { 8, 4 };
    for (int k = 0; k < 2; k++) {                                 /* a dotted ring's step by its size */
        alu_sub(c, bp_get(c, 0x0C), 0, 1, 0);
        n += 2;
        if (!(c->flags & F_ZF)) continue;
        alu_sub(c, bp_get(c, 8), far_r[k], 1, 0);
        n += 2;
        if (!x86_cond(c, 0xC)) { bp_put(c, -8, fine[k]); n++; }
    }
    set_r8(c, R_CL, 7);
    bp_put(c, 8, x86_shift(c, 7, bp_get(c, 8), 7, 1));            /* sar [bp+8], 7 */
    c->r[R_CX] = 0x92;
    c->r[R_AX] = bp_get(c, 4);
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, 0x92);
    bp_put(c, 4, c->r[R_AX]);
    c->r[R_CX] = 0xC3;
    c->r[R_AX] = bp_get(c, 6);
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, 0xC3);
    bp_put(c, 6, c->r[R_AX]);
    bp_put(c, -4, 0);                                             /* the angle step count a */
    c->icount += n + 14;
    for (;;) {                                                    /* 0x02EEB */
        NEED(8, 0x2EEB);
        alu_sub(c, bp_get(c, -4), 0x100, 1, 0);
        c->icount += 2;
        if (x86_cond(c, 0xF)) break;                              /* past a whole turn */
        cpu_push16(c, bp_get(c, 8));
        c->r[R_AX] = (uint16_t)(bp_get8(c, -4) << 8 | get_r8(c, R_AL));   /* mov ah, [bp-4] */
        set_r8(c, R_AL, (uint8_t)alu_sub(c, get_r8(c, R_AL), get_r8(c, R_AL), 0, 0));
        bp_put(c, -2, c->r[R_AX]);                                /* the angle */
        cpu_push16(c, c->r[R_AX]);
        c->icount += 5;
        if (!guest_call(m, 0x3301, 0x2F01)) return 1;
        NEED(12, 0x2F01);
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_BX] = cpu_pop16(c);
        set_r8(c, R_CL, 7);
        c->r[R_AX] = x86_shift(c, 4, c->r[R_AX], 7, 1);
        c->r[R_BX] = 0x92;
        cwd(c);
        x86_idiv16(c, 0x92, 0);
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], bp_get(c, 4), 1, 0);
        bp_put(c, -6, c->r[R_AX]);                                /* x */
        cpu_push16(c, bp_get(c, 8));
        cpu_push16(c, bp_get(c, -2));
        c->icount += 11;
        if (!guest_call(m, 0x3318, 0x2F1C)) return 1;
        NEED(26, 0x2F1C);
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_BX] = cpu_pop16(c);
        set_r8(c, R_CL, 7);
        c->r[R_AX] = x86_shift(c, 4, c->r[R_AX], 7, 1);
        c->r[R_CX] = 0xFF3D;
        cwd(c);
        x86_idiv16(c, 0xFF3D, 0);
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], bp_get(c, 6), 1, 0);
        bp_put(c, -0x0C, c->r[R_AX]);                             /* y */
        alu_sub(c, bp_get(c, -4), 0, 1, 0);
        c->icount += 11;
        int join = 0;
        if (!(c->flags & F_ZF)) {
            alu_sub(c, bp_get(c, 0x0C), 0, 1, 0);
            c->icount += 2;
            join = !(c->flags & F_ZF);
        }
        int draw = 1;
        if (join) {                                               /* a line from the last point */
            cpu_push16(c, bp_get(c, 0x0A));
            cpu_push16(c, bp_get(c, -0x0E));
            cpu_push16(c, bp_get(c, -0x0A));
            c->icount += 4;
        } else {                                                  /* 0x02EB8: a point, if on the screen */
            c->icount += 1;
            alu_sub(c, bp_get(c, -6), 0x140, 1, 0);
            c->icount += 2;
            if (!(c->flags & F_CF)) draw = 0;
            else {
                alu_sub(c, c->r[R_AX], 0xC8, 1, 0);
                c->icount += 2;
                if (!(c->flags & F_CF)) draw = 0;
                else {
                    cpu_push16(c, bp_get(c, 0x0A));
                    cpu_push16(c, c->r[R_AX]);
                    cpu_push16(c, bp_get(c, -6));
                    c->icount += 3;
                }
            }
        }
        if (draw) {                                               /* 0x02ECB */
            cpu_push16(c, c->r[R_AX]);
            cpu_push16(c, bp_get(c, -6));
            c->r[R_AX] = 0xDC18;
            cpu_push16(c, 0xDC18);
            c->icount += 4;
            if (!guest_call(m, 0x829A, 0x2ED6)) return 1;
            NEED(7, 0x2ED6);
            c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0C, 1, 0);
            c->icount += 1;
        }
        c->r[R_AX] = bp_get(c, -6);                               /* 0x02ED9: this point is the last */
        bp_put(c, -0x0A, c->r[R_AX]);
        c->r[R_AX] = bp_get(c, -0x0C);
        bp_put(c, -0x0E, c->r[R_AX]);
        c->r[R_AX] = bp_get(c, -8);
        bp_put(c, -4, (uint16_t)alu_add(c, bp_get(c, -4), c->r[R_AX], 1, 0));
        c->icount += 6;
    }
    c->icount += 3;
    frame_close_ret(c);
    return 1;
#undef NEED
}

/* START 0x0183A, site_rings(): for each briefing-map box (count [B2E6]), the
 * site it stands for (its first target, or its partner when the first has no
 * type) and, when that target has a type, its threat ring: the radius is the
 * type's range (14-byte records at 15CE: the range times the factor at +2 /
 * 16 unless [B284] is 1; the 18-byte records at 1AB0 when [B2E8] is 2) times
 * 64; the ring is solid when bit 0 of the type's +4 is set (always for the
 * 1AB0 kind). The box's three palette bytes at 64E3 + 3 * (C0h + box) are
 * set from 11ACh (bit 6 of +8: a known site) or 11BBh, and the ring is drawn
 * by 0x02E50 when bit 6 of +9 is set. Finally the DAC request 0x03517 (C0h,
 * 6723h, count) is queued. */
static int start_site_rings(machine_t *m)
{
    cpu_t *c = &m->cpu;
#define NEED(n, at) do { if (!room(c, (n))) { c->ip = (at); return 1; } } while (0)
    if (!room(c, 8)) return 0;
    frame_open(c, 0x0A);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    bp_put(c, -2, 0xC0);                                          /* the palette slot */
    bp_put(c, -8, 0);                                             /* the box */
    c->icount += 8;
    for (;;) {                                                    /* 0x018FA */
        NEED(73, 0x18FA);                                         /* the longest pass, to the ring's CALL */
        c->r[R_AX] = ds_get(c, 0xB2E6);
        alu_sub(c, bp_get(c, -8), c->r[R_AX], 1, 0);
        c->icount += 3;
        if (!(c->flags & F_CF)) break;
        c->r[R_BX] = x86_shift(c, 4, bp_get(c, -8), 1, 1);
        set_r8(c, R_AL, ds_get8(c, (uint16_t)(c->r[R_BX] - 0x4D7A)));
        c->r[R_AX] = (uint16_t)(int16_t)(int8_t)get_r8(c, R_AL);
        set_r8(c, R_CL, 4);
        c->r[R_SI] = (uint16_t)alu_add(c, x86_shift(c, 4, c->r[R_AX], 4, 1), 0xCBDE, 1, 0);
        bp_put(c, -0x0A, c->r[R_SI]);
        alu_sub(c, ds_get(c, (uint16_t)(c->r[R_SI] + 6)), 0, 1, 0);
        c->icount += 11;
        if (c->flags & F_ZF) {                                    /* no type: the partner */
            set_r8(c, R_AL, ds_get8(c, (uint16_t)(c->r[R_BX] - 0x4D79)));
            c->r[R_AX] = (uint16_t)(int16_t)(int8_t)get_r8(c, R_AL);
            c->r[R_AX] = (uint16_t)alu_add(c, x86_shift(c, 4, c->r[R_AX], 4, 1), 0xCBDE, 1, 0);
            bp_put(c, -0x0A, c->r[R_AX]);
            c->icount += 5;
        }
        c->r[R_BX] = bp_get(c, -0x0A);
        alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 6)), 0, 1, 0);
        c->icount += 3;
        if (!(c->flags & F_ZF)) {                                 /* a typed site: its ring */
            alu_sub(c, ds_get8(c, 0xB2E8), 2, 0, 0);
            c->icount += 2;
            int solid = 1;
            if (!(c->flags & F_ZF)) {                             /* 0x0184F: the 15CE kind */
                alu_sub(c, ds_get8(c, 0xB284), 1, 0, 0);
                const int scaled = !(c->flags & F_ZF);
                c->r[R_AX] = 0x0E;
                x86_imul16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 6)));
                c->r[R_BX] = c->r[R_AX];
                c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x15CE));
                c->icount += 3 + 4;                               /* jmp, cmp, jne; and the load */
                if (scaled) {                                     /* range * factor / 16, toward zero */
                    x86_imul16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x15D0)));
                    cwd(c);
                    c->r[R_AX] = (uint16_t)alu_sub(c, (uint16_t)alu_logic(c, c->r[R_AX] ^ c->r[R_DX], 1), c->r[R_DX], 1, 0);
                    c->r[R_CX] = 4;
                    c->r[R_AX] = x86_shift(c, 7, c->r[R_AX], 4, 1);
                    c->r[R_AX] = (uint16_t)alu_sub(c, (uint16_t)alu_logic(c, c->r[R_AX] ^ c->r[R_DX], 1), c->r[R_DX], 1, 0);
                    c->icount += 8;
                } else c->icount += 1;                            /* jmp */
                set_r8(c, R_CL, 6);                               /* 0x01882 */
                c->r[R_AX] = x86_shift(c, 4, c->r[R_AX], 6, 1);
                bp_put(c, -6, c->r[R_AX]);
                c->r[R_AX] = 0x0E;
                c->r[R_BX] = bp_get(c, -0x0A);
                x86_imul16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 6)));
                c->r[R_BX] = c->r[R_AX];
                alu_logic(c, ds_get8(c, (uint16_t)(c->r[R_BX] + 0x15D2)) & 1, 0);
                c->icount += 9;
                solid = !(c->flags & F_ZF);
            } else {                                              /* 0x0193F: the 1AB0 kind */
                c->r[R_AX] = 0x12;
                x86_imul16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 6)));
                c->r[R_BX] = c->r[R_AX];
                c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x1AB0));
                set_r8(c, R_CL, 6);
                c->r[R_AX] = x86_shift(c, 4, c->r[R_AX], 6, 1);
                bp_put(c, -6, c->r[R_AX]);
                c->icount += 8;
            }
            if (solid) { bp_put(c, -4, 1); c->icount += 2; }
            else { bp_put(c, -4, 0); c->icount += 1; }
            c->r[R_BX] = bp_get(c, -0x0A);                        /* 0x018A7: the box's colours */
            alu_logic(c, ds_get8(c, (uint16_t)(c->r[R_BX] + 8)) & 0x40, 0);
            const int known = !(c->flags & F_ZF);
            const uint16_t slot = bp_get(c, -2);
            c->r[R_AX] = slot;
            c->r[R_BX] = (uint16_t)alu_add(c, x86_shift(c, 4, slot, 1, 1), slot, 1, 0);
            c->r[R_DI] = (uint16_t)(c->r[R_BX] + 0x64E3);
            c->r[R_SI] = known ? 0x11AC : 0x11BB;
            c->icount += 3 + (known ? 7 : 6);
            cpu_push16(c, c->seg[S_DS]);                          /* 0x018D2 */
            c->seg[S_ES] = cpu_pop16(c);
            x86_movs(c, 1, c->seg[S_DS]);
            x86_movs(c, 0, c->seg[S_DS]);
            c->r[R_BX] = bp_get(c, -0x0A);
            alu_logic(c, ds_get8(c, (uint16_t)(c->r[R_BX] + 9)) & 0x40, 0);
            c->icount += 7;
            if (!(c->flags & F_ZF)) {                             /* draw the ring */
                cpu_push16(c, bp_get(c, -4));
                cpu_push16(c, bp_get(c, -2));
                cpu_push16(c, bp_get(c, -6));
                cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 4)));
                cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 2)));
                c->icount += 5;
                if (!guest_call(m, 0x2E50, 0x18F1)) return 1;
                NEED(3, 0x18F1);
                c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0A, 1, 0);
                c->icount += 1;
            }
        }
        bp_put(c, -8, (uint16_t)alu_inc(c, bp_get(c, -8), 1));    /* 0x018F4 */
        bp_put(c, -2, (uint16_t)alu_inc(c, bp_get(c, -2), 1));
        c->icount += 2;
    }
    cpu_push16(c, c->r[R_AX]);                                    /* the count */
    c->r[R_AX] = 0x6723;
    cpu_push16(c, 0x6723);
    c->r[R_AX] = 0xC0;
    cpu_push16(c, 0xC0);
    c->icount += 5;
    if (!guest_call(m, 0x3517, 0x1961)) return 1;
    NEED(6, 0x1961);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->icount += 6;
    frame_close_ret(c);
    return 1;
#undef NEED
}

/* START 0x07744, obj_near(x, y): the object nearest a 32-bit map point.
 * At terrain levels 1 and 2 (the point at each level's scale by 0x07C05),
 * the tile under the point and its eight neighbours (offsets from the tables
 * at 105A/106C through 1054) are looked up (0x07C56); each tile's objects
 * (7-byte records from the list at [C85C + 2 * (32 * level + tile)], count
 * at [0F0A + ...]) whose type byte (+6) is enabled in D2A6 are measured by
 * |dx| + |dy| from the point (quartered at level 1; the offsets kept four
 * times larger at level 2). The nearest so far is recorded: its distance at
 * [CA90] (7FFFh for none), type at [CA8E], record at [CA9A], level, index,
 * tile x and y as bytes at CA9C-CA9F, and its 32-bit position at CA92/CA96.
 * Returns CA8E, or 0 when nothing was found. */
static int start_obj_near(machine_t *m)
{
    cpu_t *c = &m->cpu;
#define NEED(n, at) do { if (!room(c, (n))) { c->ip = (at); return 1; } } while (0)
    if (!room(c, 8)) return 0;
    frame_open(c, 0x24);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    ds_put(c, 0xCA90, 0x7FFF);
    bp_put(c, -0x0E, 1);                                          /* the level */
    c->icount += 8;
    for (;;) {                                                    /* 0x078E1: each level */
        NEED(11, 0x78E1);
        alu_sub(c, bp_get(c, -0x0E), 2, 1, 0);
        c->icount += 2;
        if (x86_cond(c, 0xF)) break;
        bp_put(c, -0x12, 0);                                      /* the neighbour */
        c->icount += 2;
        for (;;) {                                                /* 0x0781F: each of the nine tiles */
            NEED(6, 0x781F);
            alu_sub(c, bp_get(c, -0x12), 9, 1, 0);
            c->icount += 2;
            if (!x86_cond(c, 0xC)) { c->icount += 1; break; }     /* jmp to the level's end */
            cpu_push16(c, bp_get(c, 6));
            cpu_push16(c, bp_get(c, 4));
            cpu_push16(c, bp_get(c, -0x0E));
            c->icount += 3;
            if (!guest_call(m, 0x7C05, 0x7834)) return 1;         /* x at the level's scale */
            NEED(4, 0x7834);
            c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
            bp_put(c, -0x20, c->r[R_AX]);
            set_r8(c, R_CL, 0x0C);
            c->icount += 3;
            if (!guest_call(m, 0x97F4, 0x783F)) return 1;         /* its tile column */
            NEED(8, 0x783F);
            bp_put(c, -0x18, c->r[R_AX]);
            c->r[R_AX] = bp_get(c, -0x20);
            set_r8(c, R_AH, (uint8_t)alu_logic(c, get_r8(c, R_AH) & 0x0F, 0));
            bp_put(c, -0x0C, c->r[R_AX]);                         /* x within the tile */
            cpu_push16(c, bp_get(c, 0x0A));
            cpu_push16(c, bp_get(c, 8));
            cpu_push16(c, bp_get(c, -0x0E));
            c->icount += 7;
            if (!guest_call(m, 0x7C05, 0x7857)) return 1;         /* y at the level's scale */
            NEED(28, 0x7857);
            c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
            bp_put(c, -0x20, c->r[R_AX]);
            bp_put(c, -0x1E, c->r[R_DX]);
            set_r8(c, R_AH, (uint8_t)alu_logic(c, get_r8(c, R_AH) & 0x0F, 0));   /* y within the tile */
            c->r[R_BX] = x86_shift(c, 4, bp_get(c, -0x12), 1, 1);
            const uint16_t ox = ds_get(c, (uint16_t)(c->r[R_BX] + 0x105A)), oy = ds_get(c, (uint16_t)(c->r[R_BX] + 0x106C));
            c->r[R_CX] = ox;
            c->r[R_DX] = oy;
            c->r[R_BX] = x86_shift(c, 4, ox, 1, 1);
            c->r[R_SI] = (uint16_t)alu_add(c, (uint16_t)alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x1054)), bp_get(c, -0x0C), 1, 0), 0x800, 1, 0);
            bp_put(c, -0x24, c->r[R_SI]);                         /* the tile's x origin from the point */
            c->r[R_BX] = x86_shift(c, 4, oy, 1, 1);
            c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], ds_get(c, (uint16_t)(c->r[R_BX] + 0x1054)), 1, 0);
            c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);   /* neg ax */
            set_r8(c, R_AH, (uint8_t)alu_add(c, get_r8(c, R_AH), 8, 0, 0));
            bp_put(c, -2, c->r[R_AX]);                            /* and its y origin */
            c->r[R_AX] = ox;
            set_r8(c, R_CL, 0x0C);
            c->r[R_BX] = ox;
            c->r[R_SI] = oy;
            c->r[R_AX] = bp_get(c, -0x20);
            c->r[R_DX] = bp_get(c, -0x1E);
            c->r[R_DI] = ox;
            c->icount += 27;
            if (!guest_call(m, 0x97F4, 0x78A5)) return 1;         /* the tile row */
            NEED(7, 0x78A5);
            c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], c->r[R_AX], 1, 0);
            bp_put(c, -0x1C, c->r[R_SI]);
            bp_put(c, -0x18, (uint16_t)alu_add(c, bp_get(c, -0x18), c->r[R_DI], 1, 0));
            cpu_push16(c, c->r[R_SI]);
            cpu_push16(c, bp_get(c, -0x18));
            cpu_push16(c, bp_get(c, -0x0E));
            c->icount += 6;
            if (!guest_call(m, 0x7C56, 0x78B7)) return 1;         /* the tile there */
            NEED(13, 0x78B7);
            c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
            bp_put(c, -0x22, c->r[R_AX]);
            c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
            c->icount += 4;
            if (!(c->flags & F_ZF)) {                             /* a tile: its objects */
                set_r8(c, R_CL, 5);
                c->r[R_BX] = x86_shift(c, 4, (uint16_t)alu_add(c, x86_shift(c, 4, bp_get(c, -0x0E), 5, 1), bp_get(c, -0x22), 1, 0), 1, 1);
                c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] - 0x37A4));
                bp_put(c, -0x0A, c->r[R_AX]);                     /* the object's record */
                bp_put(c, -0x14, 0);                              /* its index */
                c->icount += 9;
                for (;;) {                                        /* 0x077C1 */
                    NEED(23, 0x77C1);
                    c->r[R_AX] = bp_get(c, -0x14);
                    set_r8(c, R_CL, 5);
                    c->r[R_BX] = x86_shift(c, 4, (uint16_t)alu_add(c, x86_shift(c, 4, bp_get(c, -0x0E), 5, 1), bp_get(c, -0x22), 1, 0), 1, 1);
                    alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0F0A)), c->r[R_AX], 1, 0);
                    c->icount += 8;
                    if (x86_cond(c, 0x6)) break;                  /* the tile's last */
                    c->r[R_BX] = bp_get(c, -0x0A);
                    set_r8(c, R_BL, ds_get8(c, (uint16_t)(c->r[R_BX] + 6)));
                    set_r8(c, R_BH, (uint8_t)alu_sub(c, get_r8(c, R_BH), get_r8(c, R_BH), 0, 0));
                    alu_sub(c, ds_get8(c, (uint16_t)(c->r[R_BX] - 0x2D5A)), get_r8(c, R_BH), 0, 0);
                    c->icount += 5;
                    if (!(c->flags & F_ZF)) {                     /* an enabled type: measure it */
                        c->r[R_BX] = bp_get(c, -0x0A);
                        c->r[R_AX] = (uint16_t)alu_add(c, ds_get(c, c->r[R_BX]), bp_get(c, -0x24), 1, 0);
                        bp_put(c, -0x16, c->r[R_AX]);             /* dx */
                        c->r[R_CX] = (uint16_t)alu_add(c, ds_get(c, (uint16_t)(c->r[R_BX] + 2)), bp_get(c, -2), 1, 0);
                        bp_put(c, -0x1A, c->r[R_CX]);             /* dy */
                        cpu_push16(c, c->r[R_CX]);
                        c->r[R_SI] = c->r[R_AX];
                        c->icount += 9;
                        if (!guest_call(m, 0x96AE, 0x77FE)) return 1;
                        NEED(4, 0x77FE);
                        c->r[R_BX] = cpu_pop16(c);
                        cpu_push16(c, c->r[R_SI]);
                        c->r[R_SI] = c->r[R_AX];
                        c->icount += 3;
                        if (!guest_call(m, 0x96AE, 0x7805)) return 1;
                        NEED(42, 0x7805);
                        c->r[R_BX] = cpu_pop16(c);
                        c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], c->r[R_AX], 1, 0);
                        bp_put(c, -6, c->r[R_SI]);                /* |dx| + |dy| */
                        alu_sub(c, bp_get(c, -0x0E), 1, 1, 0);
                        c->icount += 5;
                        set_r8(c, R_CL, 2);
                        if (!(c->flags & F_ZF)) {                 /* level 2: the offsets times 4 */
                            bp_put(c, -0x16, x86_shift(c, 4, bp_get(c, -0x16), 2, 1));
                            bp_put(c, -0x1A, x86_shift(c, 4, bp_get(c, -0x1A), 2, 1));
                            c->icount += 4;
                        } else {                                  /* level 1: the distance over 4 */
                            bp_put(c, -6, x86_shift(c, 7, bp_get(c, -6), 2, 1));
                            c->icount += 3;
                        }
                        c->r[R_AX] = ds_get(c, 0xCA90);           /* 0x07762 */
                        alu_sub(c, bp_get(c, -6), c->r[R_AX], 1, 0);
                        c->icount += 3;
                        if (!x86_cond(c, 0xD)) {                  /* the nearest yet: record it */
                            static const struct { int16_t from; uint16_t to; } b[4] = {
                                { -0x0E, 0xCA9C }, { -0x14, 0xCA9D }, { -0x18, 0xCA9E }, { -0x1C, 0xCA9F } };
                            for (int k = 0; k < 4; k++) { set_r8(c, R_AL, bp_get8(c, b[k].from)); ds_put8(c, b[k].to, get_r8(c, R_AL)); }
                            c->r[R_AX] = bp_get(c, -0x0A);
                            ds_put(c, 0xCA9A, c->r[R_AX]);
                            c->r[R_BX] = c->r[R_AX];
                            set_r8(c, R_AL, ds_get8(c, (uint16_t)(c->r[R_BX] + 6)));
                            set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
                            ds_put(c, 0xCA8E, c->r[R_AX]);
                            c->r[R_AX] = bp_get(c, -6);
                            ds_put(c, 0xCA90, c->r[R_AX]);
                            static const struct { int16_t off, lo; uint16_t to; } p[2] = { { -0x16, 4, 0xCA92 }, { -0x1A, 8, 0xCA96 } };
                            for (int k = 0; k < 2; k++) {         /* the object's position: the point plus the offset */
                                c->r[R_AX] = bp_get(c, p[k].off);
                                cwd(c);
                                c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], bp_get(c, p[k].lo), 1, 0);
                                c->r[R_DX] = (uint16_t)alu_add(c, c->r[R_DX], bp_get(c, p[k].lo + 2), 1, (c->flags & F_CF) ? 1u : 0u);
                                ds_put(c, p[k].to, c->r[R_AX]);
                                ds_put(c, (uint16_t)(p[k].to + 2), c->r[R_DX]);
                            }
                            c->icount += 28;
                        }
                    }
                    bp_put(c, -0x0A, (uint16_t)alu_add(c, bp_get(c, -0x0A), 7, 1, 0));   /* 0x077BA */
                    bp_put(c, -0x14, (uint16_t)alu_inc(c, bp_get(c, -0x14), 1));
                    c->icount += 2;
                }
            } else c->icount += 1;                                /* jmp */
            bp_put(c, -0x12, (uint16_t)alu_inc(c, bp_get(c, -0x12), 1));   /* 0x0781C */
            c->icount += 1;
        }
        bp_put(c, -0x0E, (uint16_t)alu_inc(c, bp_get(c, -0x0E), 1));   /* 0x078DE */
        c->icount += 1;
    }
    alu_sub(c, ds_get(c, 0xCA90), 0x7FFF, 1, 0);                  /* 0x078EF */
    if (!(c->flags & F_ZF)) { c->r[R_AX] = 0xCA8E; c->icount += 4; }
    else { c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0); c->icount += 3; }
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->icount += 5;
    frame_close_ret(c);
    return 1;
#undef NEED
}

/* The mission handover between the front end and the debriefing: START
 * 0x05625 writes it into the shared block at far [E096] + 7Ah for VGAME,
 * END 0x00113 reads it back from far [7222] + 7Ah, each through its block
 * copier (START 0x0588D, END 0x00242: buffer, size, count, the far pointer
 * kept in a rover) - the counts, the target records (16 bytes each), the
 * mission slots (36 bytes each), the text and tables, in one fixed order.
 * Each argument is loaded into AX, CX or DX and pushed (or pushed from
 * memory) exactly as the original does, as the registers reach the callee. */
typedef struct { uint8_t kind, reg; uint16_t v; } handover_push;   /* kind 0: MOV reg, v / PUSH reg; 1: PUSH reg; 2: PUSH [v] */
typedef struct { handover_push p[3]; uint16_t ret; } handover_copy;
typedef struct { uint16_t block, rover, copier; unsigned n; const handover_copy *copy; } handover_variant;

#define IMM(r, v) { 0, r, v }
#define REG(r) { 1, r, 0 }
#define MEM(a) { 2, 0, a }
static const handover_copy START_HANDOVER[15] = {
    { { IMM(R_AX, 1), IMM(R_CX, 2), IMM(R_DX, 0xD88C) }, 0x5645 },
    { { IMM(R_AX, 1), IMM(R_CX, 2), IMM(R_DX, 0xD33C) }, 0x5657 },     /* the target count */
    { { IMM(R_AX, 1), IMM(R_CX, 2), IMM(R_DX, 0xCACE) }, 0x5669 },
    { { IMM(R_AX, 1), IMM(R_CX, 2), IMM(R_DX, 0xE084) }, 0x567B },
    { { MEM(0xD33C), IMM(R_AX, 0x10), IMM(R_AX, 0xCBDE) }, 0x568D },    /* the targets */
    { { IMM(R_AX, 1), IMM(R_CX, 2), IMM(R_DX, 0xD890) }, 0x569F },     /* the slot count */
    { { MEM(0xD890), IMM(R_AX, 0x24), IMM(R_AX, 0xD3BE) }, 0x56B1 },    /* the mission slots */
    { { IMM(R_AX, 1), IMM(R_CX, 0x80), IMM(R_DX, 0xD7F6) }, 0x56C3 },
    { { IMM(R_AX, 1), IMM(R_CX, 0x80), IMM(R_CX, 0xD33E) }, 0x56D5 },
    { { IMM(R_AX, 0x2EE), IMM(R_AX, 1), IMM(R_CX, 0xDD70) }, 0x56E7 },  /* the text */
    { { IMM(R_AX, 0x100), IMM(R_AX, 1), IMM(R_CX, 0xCAD8) }, 0x56F9 },
    { { IMM(R_AX, 1), IMM(R_CX, 2), IMM(R_DX, 0xD876) }, 0x570B },
    { { IMM(R_AX, 1), IMM(R_AX, 2), IMM(R_CX, 0xCABC) }, 0x571D },
    { { IMM(R_AX, 4), REG(R_AX), IMM(R_AX, 0x15B2) }, 0x572C },
    { { IMM(R_AX, 2), IMM(R_AX, 0x12), IMM(R_AX, 0xE05E) }, 0x573E },
};
static const handover_copy END_HANDOVER[16] = {
    { { IMM(R_AX, 1), IMM(R_CX, 2), IMM(R_DX, 0x6983) }, 0x0133 },
    { { IMM(R_AX, 1), IMM(R_CX, 2), IMM(R_DX, 0x6446) }, 0x0145 },     /* the target count */
    { { IMM(R_AX, 1), IMM(R_CX, 2), IMM(R_DX, 0x55E4) }, 0x0157 },
    { { IMM(R_AX, 1), IMM(R_CX, 2), IMM(R_DX, 0x7210) }, 0x0169 },
    { { MEM(0x6446), IMM(R_AX, 0x10), IMM(R_AX, 0x56EA) }, 0x017B },    /* the targets */
    { { IMM(R_AX, 1), IMM(R_CX, 2), IMM(R_DX, 0x6D8A) }, 0x018D },     /* the slot count */
    { { MEM(0x6D8A), IMM(R_AX, 0x24), IMM(R_AX, 0x64CA) }, 0x019F },    /* the mission slots */
    { { IMM(R_AX, 1), IMM(R_CX, 0x80), IMM(R_DX, 0x6902) }, 0x01B1 },
    { { IMM(R_AX, 1), IMM(R_CX, 0x80), IMM(R_CX, 0x6448) }, 0x01C3 },
    { { IMM(R_AX, 0x2EE), IMM(R_AX, 1), IMM(R_CX, 0x6EFA) }, 0x01D5 },  /* the text */
    { { IMM(R_AX, 0x100), IMM(R_CX, 1), IMM(R_DX, 0x55EA) }, 0x01E7 },
    { { IMM(R_AX, 1), IMM(R_CX, 2), IMM(R_DX, 0x543A) }, 0x01F9 },
    { { IMM(R_AX, 1), IMM(R_AX, 2), IMM(R_CX, 0x5438) }, 0x020B },
    { { IMM(R_AX, 4), REG(R_AX), IMM(R_AX, 0x5DA4) }, 0x021A },
    { { IMM(R_AX, 2), IMM(R_AX, 0x12), IMM(R_AX, 0x71E8) }, 0x022C },
    { { IMM(R_AX, 0x100), IMM(R_AX, 6), IMM(R_AX, 0x5E38) }, 0x023E },  /* (END's own: 6 x 256 bytes at 5E38) */
};
#undef IMM
#undef REG
#undef MEM
static const handover_variant START_HANDOVER_V = { 0xE096, 0xB390, 0x588D, 15, START_HANDOVER };
static const handover_variant END_HANDOVER_V = { 0x7222, 0x543C, 0x0242, 16, END_HANDOVER };

/* The instructions of a copy before its CALL: two for a load and push, one for a bare push. */
static unsigned handover_len(const handover_copy *w)
{
    unsigned n = 0;
    for (int a = 0; a < 3; a++) n += w->p[a].kind ? 1 : 2;
    return n;
}

static int handover_blocks(machine_t *m, const handover_variant *v)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5 + handover_len(&v->copy[0]) + 1)) return 0;
    c->r[R_AX] = (uint16_t)alu_add(c, ds_get(c, v->block), 0x7A, 1, 0);   /* the block's far pointer + 7Ah */
    c->r[R_DX] = ds_get(c, (uint16_t)(v->block + 2));
    ds_put(c, v->rover, c->r[R_AX]);
    ds_put(c, (uint16_t)(v->rover + 2), c->r[R_DX]);
    c->icount += 5;
    for (unsigned k = 0; k < v->n; k++) {
        const handover_copy *w = &v->copy[k];
        for (int a = 0; a < 3; a++) {
            const handover_push *p = &w->p[a];
            if (p->kind == 0) c->r[p->reg] = p->v;
            cpu_push16(c, p->kind == 2 ? ds_get(c, p->v) : c->r[p->reg]);
        }
        c->icount += handover_len(w);
        if (!guest_call(m, v->copier, w->ret)) return 1;
        const unsigned next = k + 1 < v->n ? handover_len(&v->copy[k + 1]) + 1 : 1;   /* the next copy and its CALL, or the RET */
        if (!room(c, 1 + next)) { c->ip = w->ret; return 1; }
        c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
        c->icount += 1;
    }
    c->icount += 1;
    near_ret(c);
    return 1;
}
static int start_write_handover(machine_t *m) { return handover_blocks(m, &START_HANDOVER_V); }
static int end_load_handover(machine_t *m) { return handover_blocks(m, &END_HANDOVER_V); }

/* END 0x000C9, handover_text(): the handover read (0x00113), the 2EEh bytes
 * of text at 6EFA are indexed: [6DFA] = 6EFA, and each NUL starts the next
 * string, whose address goes to the word table at 6DFA (at most 128). */
static int end_handover_text(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;
    frame_open(c, 4);
    cpu_push16(c, c->r[R_SI]);
    c->icount += 4;
    if (!guest_call(m, 0x0113, 0x00D3)) return 1;
    if (!room(c, 3)) { c->ip = 0x00D3; return 1; }
    ds_put(c, 0x6DFA, 0x6EFA);
    bp_put(c, -2, 1);                                             /* the next string's index */
    bp_put(c, -4, 0);                                             /* the byte */
    c->icount += 3;
    for (;;) {                                                    /* 0x000E3 */
        if (!room(c, 17)) { c->ip = 0x00E3; return 1; }          /* a byte, or the last and the RET */
        c->r[R_BX] = bp_get(c, -4);
        alu_sub(c, ds_get8(c, (uint16_t)(c->r[R_BX] + 0x6EFA)), 0, 0, 0);
        c->icount += 3;
        if (c->flags & F_ZF) {                                    /* a NUL */
            alu_sub(c, bp_get(c, -2), 0x80, 1, 0);
            c->icount += 2;
            if (!x86_cond(c, 0xD)) {                              /* room in the table */
                c->r[R_BX] = (uint16_t)alu_add(c, c->r[R_BX], 0x6EFB, 1, 0);
                c->r[R_SI] = x86_shift(c, 4, bp_get(c, -2), 1, 1);
                ds_put(c, (uint16_t)(c->r[R_SI] + 0x6DFA), c->r[R_BX]);
                bp_put(c, -2, (uint16_t)alu_inc(c, bp_get(c, -2), 1));
                c->icount += 5;
            }
        }
        bp_put(c, -4, (uint16_t)alu_inc(c, bp_get(c, -4), 1));
        alu_sub(c, bp_get(c, -4), 0x2EE, 1, 0);
        c->icount += 3;
        if (!x86_cond(c, 0xC)) break;
    }
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 4;
    frame_close_ret(c);
    return 1;
}

/* Would IDIV r/m16 by d fault on DX:AX (a zero divisor, or a quotient out of
 * the 286's range)? The routine then stops at the IDIV for the original. */
static int idiv16_faults(const cpu_t *c, uint16_t d)
{
    if (!d) return 1;
    const int32_t n = (int32_t)(((uint32_t)c->r[R_DX] << 16) | c->r[R_AX]);
    if ((int16_t)d == -1 && n == INT32_MIN) return 1;
    const int32_t q = n / (int16_t)d;
    return q > 32767 || q < -32768;
}

/* START 0x02F4C, route_leg(x0, y0, x1, y1, stop): the briefing map's route
 * animation along one leg. The longer axis steps by a whole pixel (40h in
 * 10.6 fixed point, signed by the direction), the other by its share
 * (difference * 64 / the longer difference, by IDIV), from the start's centre
 * (* 64 + 32) for the longer difference less one steps. At each step, while
 * stop is 0 it is polled (0x0405F (1), the key check); from the third step
 * the point is plotted in colour 0 by the line drawer 0x0829A on the page at
 * DC18, and while not stopped on the one at DC02 too. The pixel position is
 * left in the x1, y1 arguments. Returns stop (0 for a leg of no length). */
static int start_route_leg(machine_t *m)
{
    cpu_t *c = &m->cpu;
#define NEED(n, at) do { if (!room(c, (n))) { c->ip = (at); return 1; } } while (0)
    if (!room(c, 7)) return 0;
    frame_open(c, 0x0C);
    c->r[R_AX] = (uint16_t)alu_sub(c, bp_get(c, 8), bp_get(c, 4), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 6;
    if (!guest_call(m, 0x96AE, 0x2F5C)) return 1;
    NEED(6, 0x2F5C);
    c->r[R_BX] = cpu_pop16(c);
    bp_put(c, -2, c->r[R_AX]);                                    /* |dx| */
    c->r[R_AX] = (uint16_t)alu_sub(c, bp_get(c, 0x0A), bp_get(c, 6), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 5;
    if (!guest_call(m, 0x96AE, 0x2F6A)) return 1;
    NEED(33, 0x2F6A);
    c->r[R_BX] = cpu_pop16(c);
    bp_put(c, -4, c->r[R_AX]);                                    /* |dy| */
    alu_sub(c, bp_get(c, -2), 0, 1, 0);
    c->icount += 4;
    if (c->flags & F_ZF) {
        alu_logic(c, c->r[R_AX], 1);                              /* or ax, ax */
        c->icount += 2;
        if (c->flags & F_ZF) { c->icount += 1 + 3; frame_close_ret(c); return 1; }   /* no length: AX = 0 */
    }
    c->r[R_AX] = bp_get(c, -2);
    alu_sub(c, bp_get(c, -4), c->r[R_AX], 1, 0);
    c->icount += 3;
    /* Along x when |dy| <= |dx| (signed), else along y: (the long step, the
     * short step, the long axis's start and end, the short axis's). */
    const int along_x = !x86_cond(c, 0xF);
    const int16_t lon = along_x ? -2 : -4, sho = along_x ? -4 : -2;
    const int16_t s0 = along_x ? 6 : 4, s1 = along_x ? 0x0A : 8;  /* the short axis's start, end */
    const int16_t l0 = along_x ? 4 : 6, l1 = along_x ? 8 : 0x0A;  /* the long axis's */
    const uint16_t idiv_ip = along_x ? 0x2F92 : 0x2FC0;
    if (!along_x) { c->r[R_AX] = bp_get(c, -4); c->icount += 1; }
    c->r[R_AX] = (uint16_t)alu_dec(c, c->r[R_AX], 1);
    bp_put(c, -6, c->r[R_AX]);                                    /* the steps */
    c->r[R_AX] = (uint16_t)alu_sub(c, bp_get(c, s1), bp_get(c, s0), 1, 0);
    set_r8(c, R_CL, 6);
    c->r[R_AX] = x86_shift(c, 4, c->r[R_AX], 6, 1);
    cwd(c);
    c->icount += 7;
    const uint16_t d = bp_get(c, lon);
    if (idiv16_faults(c, d)) { c->ip = idiv_ip; return 1; }       /* the original takes the fault */
    x86_idiv16(c, d, 0);
    bp_put(c, sho, c->r[R_AX]);                                   /* the short step */
    c->r[R_AX] = bp_get(c, l0);
    alu_sub(c, bp_get(c, l1), c->r[R_AX], 1, 0);
    c->icount += 5;
    if (!x86_cond(c, 0xE)) { bp_put(c, lon, 0x40); c->icount += 2; }
    else { bp_put(c, lon, 0xFFC0); c->icount += along_x ? 2 : 1; }
    set_r8(c, R_CL, 6);                                           /* 0x02FDA: the centre of the start pixel */
    c->r[R_AX] = (uint16_t)alu_add(c, x86_shift(c, 4, bp_get(c, 4), 6, 1), 0x20, 1, 0);
    bp_put(c, 4, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_add(c, x86_shift(c, 4, bp_get(c, 6), 6, 1), 0x20, 1, 0);
    bp_put(c, 6, c->r[R_AX]);
    bp_put(c, -0x0C, 0);                                          /* the step */
    c->icount += 11;
    for (;;) {                                                    /* 0x03061 */
        NEED(23, 0x3061);
        c->r[R_AX] = bp_get(c, -6);
        alu_sub(c, bp_get(c, -0x0C), c->r[R_AX], 1, 0);
        c->icount += 3;
        if (!x86_cond(c, 0xC)) break;
        alu_sub(c, bp_get(c, 0x0C), 0, 1, 0);
        c->icount += 2;
        if (c->flags & F_ZF) {                                    /* not stopped: poll the keys */
            c->r[R_AX] = 1;
            cpu_push16(c, 1);
            c->icount += 2;
            if (!guest_call(m, 0x405F, 0x3006)) return 1;
            NEED(20, 0x3006);
            c->r[R_BX] = cpu_pop16(c);
            bp_put(c, 0x0C, c->r[R_AX]);
            c->icount += 2;
        }
        set_r8(c, R_CL, 6);                                       /* 0x0300A: the pixel */
        c->r[R_AX] = x86_shift(c, 7, bp_get(c, 4), 6, 1);
        bp_put(c, 8, c->r[R_AX]);
        c->r[R_DX] = x86_shift(c, 7, bp_get(c, 6), 6, 1);
        bp_put(c, 0x0A, c->r[R_DX]);
        alu_sub(c, bp_get(c, -0x0C), 1, 1, 0);
        c->icount += 9;
        if (!x86_cond(c, 0xE)) {                                  /* from the third step: plot it */
            c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], c->r[R_CX], 1, 0);
            cpu_push16(c, 0);
            cpu_push16(c, c->r[R_DX]);
            cpu_push16(c, c->r[R_AX]);
            cpu_push16(c, c->r[R_DX]);
            cpu_push16(c, c->r[R_AX]);
            c->r[R_BX] = 0xDC18;
            cpu_push16(c, 0xDC18);
            c->icount += 8;
            if (!guest_call(m, 0x829A, 0x3030)) return 1;
            NEED(12, 0x3030);
            c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0C, 1, 0);
            alu_sub(c, bp_get(c, 0x0C), 0, 1, 0);
            c->icount += 3;
            if (c->flags & F_ZF) {                                /* and on the other page */
                c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
                cpu_push16(c, 0);
                cpu_push16(c, bp_get(c, 0x0A));
                cpu_push16(c, bp_get(c, 8));
                cpu_push16(c, bp_get(c, 0x0A));
                cpu_push16(c, bp_get(c, 8));
                c->r[R_AX] = 0xDC02;
                cpu_push16(c, 0xDC02);
                c->icount += 8;
                if (!guest_call(m, 0x829A, 0x304F)) return 1;
                NEED(6, 0x304F);
                c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0C, 1, 0);
                c->icount += 1;
            }
        }
        c->r[R_AX] = bp_get(c, -2);                               /* 0x03052: the next step */
        bp_put(c, 4, (uint16_t)alu_add(c, bp_get(c, 4), c->r[R_AX], 1, 0));
        c->r[R_AX] = bp_get(c, -4);
        bp_put(c, 6, (uint16_t)alu_add(c, bp_get(c, 6), c->r[R_AX], 1, 0));
        bp_put(c, -0x0C, (uint16_t)alu_inc(c, bp_get(c, -0x0C), 1));
        c->icount += 5;
    }
    c->r[R_AX] = bp_get(c, 0x0C);
    c->icount += 1 + 3;
    frame_close_ret(c);
    return 1;
#undef NEED
}

/* START 0x0A1A4, the formatter's digits: the 32-bit value DX:AX in base CX,
 * written backwards (STD, STOSB) at ES:DI, at least SI digits; a digit past
 * '9' is moved on by the byte at the caller's [BP-1] (to 'a' or 'A'). On the
 * return DI points at the first digit and CX is the count; DF is cleared.
 * Each digit divides the high word, then the remainder and the low word,
 * the value kept in DX:BX between digits. With CX 0 the routine stops at the
 * first DIV and the original takes the fault. */
static int start_format_digits(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 3)) return 0;
    set_flag(c, F_DF, 1);                                         /* std */
    cpu_push16(c, c->r[R_DI]);
    const uint16_t t = c->r[R_BX];                                /* xchg bx, ax */
    c->r[R_BX] = c->r[R_AX];
    c->r[R_AX] = t;
    c->icount += 3;
    for (;;) {                                                    /* 0x0A1A7 */
        if (!room(c, 21)) { c->ip = 0xA1A7; return 1; }           /* a digit, or the tests and the exit */
        alu_logic(c, c->r[R_SI], 1);                              /* or si, si */
        c->icount += 2;
        if (!x86_cond(c, 0xF)) {                                  /* the minimum is met: anything left? */
            alu_logic(c, c->r[R_BX], 1);
            c->icount += 2;
            if (c->flags & F_ZF) {
                alu_logic(c, c->r[R_DX], 1);
                c->icount += 2;
                if (c->flags & F_ZF) { c->icount += 1; break; }   /* jmp to the exit */
            }
        }
        uint16_t x = c->r[R_DX];                                  /* xchg dx, ax */
        c->r[R_DX] = c->r[R_AX];
        c->r[R_AX] = x;
        c->r[R_DX] = (uint16_t)alu_logic(c, 0, 1);                /* xor dx, dx */
        if (!c->r[R_CX]) { c->icount += 2; c->ip = 0xA1B8; return 1; }   /* the DIV faults */
        x86_div16(c, c->r[R_CX]);                                 /* the high word */
        x = c->r[R_BX];                                           /* xchg bx, ax */
        c->r[R_BX] = c->r[R_AX];
        c->r[R_AX] = x;
        x86_div16(c, c->r[R_CX]);                                 /* the remainder and the low word */
        x = c->r[R_DX];                                           /* xchg dx, ax */
        c->r[R_DX] = c->r[R_AX];
        c->r[R_AX] = x;
        x = c->r[R_BX];                                           /* xchg bx, dx */
        c->r[R_BX] = c->r[R_DX];
        c->r[R_DX] = x;
        set_r8(c, R_AL, (uint8_t)alu_add(c, get_r8(c, R_AL), 0x30, 0, 0));
        alu_sub(c, get_r8(c, R_AL), 0x39, 0, 0);
        c->icount += 10;
        if (!x86_cond(c, 0x6)) {                                  /* a letter */
            set_r8(c, R_AL, (uint8_t)alu_add(c, get_r8(c, R_AL), bp_get8(c, -1), 0, 0));
            c->icount += 1;
        }
        /* STOSB, backwards (DF is set): written out, as MSVC 19.51 /O2 stalls optimising x86_stos here. */
        mem_write8(c, phys(c->seg[S_ES], c->r[R_DI]), get_r8(c, R_AL));
        c->r[R_DI] = (uint16_t)(c->r[R_DI] - 1);
        c->r[R_AX] = c->r[R_DX];
        c->r[R_SI] = (uint16_t)alu_dec(c, c->r[R_SI], 1);
        c->icount += 4;
    }
    c->r[R_CX] = cpu_pop16(c);
    c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], c->r[R_DI], 1, 0);   /* the count */
    c->r[R_DI] = (uint16_t)alu_inc(c, c->r[R_DI], 1);
    set_flag(c, F_DF, 0);                                         /* cld */
    c->icount += 5;
    near_ret(c);
    return 1;
}

/* START 0x0A141, the formatter's put-character into a string stream: AL
 * (made a word by CBW) is stored at the stream's next byte (the stream at the
 * caller's [BP+4]: next pointer +0, room +2) and AX = 0; when the room runs
 * out (the count goes negative) the stream's flush 0x09992 (char, stream) is
 * called instead and AX is 0 unless it answered FFFFh. DI preserved. */
static int start_format_putc(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 11)) return 0;
    c->r[R_AX] = (uint16_t)(int16_t)(int8_t)get_r8(c, R_AL);     /* cbw */
    cpu_push16(c, c->r[R_DI]);
    c->r[R_BX] = bp_get(c, 4);
    const uint16_t room_at = (uint16_t)(c->r[R_BX] + 2);
    ds_put(c, room_at, (uint16_t)alu_dec(c, ds_get(c, room_at), 1));
    c->icount += 5;
    int zero = 1;
    if (!(c->flags & F_SF)) {                                     /* room for it */
        c->r[R_DI] = ds_get(c, c->r[R_BX]);
        ds_put(c, c->r[R_BX], (uint16_t)alu_inc(c, ds_get(c, c->r[R_BX]), 1));
        ds_put8(c, c->r[R_DI], get_r8(c, R_AL));
        c->icount += 3;
    } else {
        cpu_push16(c, c->seg[S_ES]);
        cpu_push16(c, c->r[R_CX]);
        cpu_push16(c, c->r[R_DX]);
        cpu_push16(c, c->r[R_BX]);
        cpu_push16(c, c->r[R_AX]);
        c->icount += 5;
        if (!guest_call(m, 0x9992, 0xA15D)) return 1;
        if (!room(c, 9)) { c->ip = 0xA15D; return 1; }
        c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 4, 1, 0);
        c->r[R_DX] = cpu_pop16(c);
        c->r[R_CX] = cpu_pop16(c);
        c->seg[S_ES] = cpu_pop16(c);
        alu_sub(c, c->r[R_AX], 0xFFFF, 1, 0);
        c->icount += 6;
        zero = !(c->flags & F_ZF);
        if (!zero) c->icount += 1;                                /* jmp over the XOR */
    }
    if (zero) { c->r[R_AX] = (uint16_t)alu_logic(c, 0, 1); c->icount += 1; }
    c->r[R_DI] = cpu_pop16(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x0C831, vcos(a, r): the routine at 0x0C818 with the angle turned a
 * quarter (AH + 40h). */
static int vgame_vcos(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    const uint16_t a = arg(c, 0), r = arg(c, 1);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, r);
    const uint8_t ah = (uint8_t)alu_add(c, a >> 8, 0x40, 0, 0);   /* add ah, 40h */
    c->r[R_AX] = (uint16_t)(ah << 8 | (a & 0xFF));
    cpu_push16(c, c->r[R_AX]);
    c->icount += 6;
    if (!guest_call(m, 0xC818, 0xC841)) return 1;
    if (!room(c, 4)) { c->ip = 0xC841; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x02F4A, rnd_scaled(n): 4000h - rnd(n), from the generator at
 * 0x02ED9. */
static int vgame_rnd_scaled(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 4)) return 0;
    const uint16_t n = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, n);
    c->icount += 3;
    if (!guest_call(m, 0x2ED9, 0x2F53)) return 1;
    if (!room(c, 5)) { c->ip = 0x2F53; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    uint16_t ax = (uint16_t)alu_sub(c, c->r[R_AX], 0x4000, 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, ax, 1, 0);               /* neg ax */
    x86_leave(c);
    c->icount += 5;
    near_ret(c);
    return 1;
}

/* VGAME 0x0C88C: (r * n) >> 15 with r from the generator at 0x0EE2C, by
 * IMUL and the 32-bit arithmetic shift (0x0EF74). */
static int vgame_rnd_times(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 3)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->icount += 2;
    if (!guest_call(m, 0xEE2C, 0xC892)) return 1;
    if (!room(c, 3)) { c->ip = 0xC892; return 1; }
    x86_imul16(c, seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 4)));
    set_r8(c, R_CL, 0x0F);
    c->icount += 2;
    if (!guest_call(m, 0xEF74, 0xC89A)) return 1;
    if (!room(c, 2)) { c->ip = 0xC89A; return 1; }
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x0C880: [9540] = the value from 0x01E72, which is also passed to
 * 0x0EE1A (the clock setter). */
static int vgame_clock_from(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 1)) return 0;
    if (!guest_call(m, 0x1E72, 0xC883)) return 1;
    if (!room(c, 3)) { c->ip = 0xC883; return 1; }
    ds_put(c, 0x9540, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 2;
    if (!guest_call(m, 0xEE1A, 0xC88A)) return 1;
    if (!room(c, 2)) { c->ip = 0xC88A; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x0C6B3, vg_dist(a, b): |a| and |b| (by 0x0EE0C, stored back in the
 * argument slots), then the larger plus half the smaller, as a 32-bit sum
 * clamped to 7FFFh. */
static int vgame_dist(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 3)) return 0;
    const uint16_t ss = c->seg[S_SS];
    x86_enter(c, 4, 0);
    const uint16_t bp = c->r[R_BP];
    cpu_push16(c, seg_read16(c, ss, (uint16_t)(bp + 4)));
    c->icount += 2;
    if (!guest_call(m, 0xEE0C, 0xC6BD)) return 1;
    if (!room(c, 4)) { c->ip = 0xC6BD; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    seg_write16(c, ss, (uint16_t)(bp + 4), c->r[R_AX]);
    cpu_push16(c, seg_read16(c, ss, (uint16_t)(bp + 6)));
    c->icount += 3;
    if (!guest_call(m, 0xEE0C, 0xC6C7)) return 1;
    /* Count the rest from the values it will see. */
    const int16_t b = (int16_t)c->r[R_AX], a = (int16_t)seg_read16(c, ss, (uint16_t)(bp + 4));
    unsigned n = 4 + (b < a ? 5 : 3) + 8;
    {
        const int16_t big = b < a ? a : b, small = b < a ? b : a;
        const int32_t sum = (int32_t)(int16_t)(small >> 1) + big;
        const int16_t hi = (int16_t)(sum >> 16);
        if (hi < 0) n += 3;
        else if (hi > 0) n += 1 + 1 + 3;
        else n += 1 + 2 + (((uint16_t)sum > 0x7FFF) ? 1 : 0) + 3;
    }
    if (!room(c, n)) { c->ip = 0xC6C7; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    seg_write16(c, ss, (uint16_t)(bp + 6), c->r[R_AX]);
    alu_sub(c, c->r[R_AX], seg_read16(c, ss, (uint16_t)(bp + 4)), 1, 0);
    uint16_t ax, cx, dx;
    if (!x86_cond(c, 0xD)) {                                      /* jge not taken: b < a */
        ax = seg_read16(c, ss, (uint16_t)(bp + 4));
        dx = (ax & 0x8000) ? 0xFFFF : 0;
        cx = ax;
        ax = seg_read16(c, ss, (uint16_t)(bp + 6));
    } else {
        ax = c->r[R_AX];
        dx = (ax & 0x8000) ? 0xFFFF : 0;
        cx = ax;
        ax = seg_read16(c, ss, (uint16_t)(bp + 4));
    }
    ax = x86_shift(c, 7, ax, 1, 1);                               /* sar ax, 1 */
    const uint16_t bx = dx;
    dx = (ax & 0x8000) ? 0xFFFF : 0;                              /* cwd */
    ax = (uint16_t)alu_add(c, ax, cx, 1, 0);
    dx = (uint16_t)alu_add(c, dx, bx, 1, (c->flags & F_CF) ? 1u : 0u);
    seg_write16(c, ss, (uint16_t)(bp - 4), ax);
    alu_logic(c, dx, 1);                                          /* or dx, dx */
    if (!x86_cond(c, 0xC)) {                                      /* jl not taken */
        if (x86_cond(c, 0xF)) seg_write16(c, ss, (uint16_t)(bp - 4), 0x7FFF);   /* jg */
        else {
            alu_sub(c, ax, 0x7FFF, 1, 0);
            if (!x86_cond(c, 0x6)) seg_write16(c, ss, (uint16_t)(bp - 4), 0x7FFF);
        }
    }
    c->r[R_AX] = seg_read16(c, ss, (uint16_t)(bp - 4));
    c->r[R_BX] = bx; c->r[R_CX] = cx; c->r[R_DX] = dx;
    x86_leave(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 0x02F5B, fl_isqrt(v): 1 below 4; otherwise Newton's iteration from
 * |v| / 4 - g = (g + |v| / g) / 2 until it moves by at most one - with |x|
 * from 0x0EE0C each pass. At the top of each pass the state is the
 * original's, so a pass that would not fit, or whose IDIV would fault, is
 * left to the original from 0x02F79. */
static int vgame_isqrt(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 3)) return 0;
    const uint16_t ss = c->seg[S_SS];
    x86_enter(c, 4, 0);
    const uint16_t bp = c->r[R_BP];
    cpu_push16(c, seg_read16(c, ss, (uint16_t)(bp + 4)));
    c->icount += 2;
    if (!guest_call(m, 0xEE0C, 0x2F65)) return 1;
    if (!room(c, 7)) { c->ip = 0x2F65; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    seg_write16(c, ss, (uint16_t)(bp + 4), c->r[R_AX]);
    alu_sub(c, c->r[R_AX], 4, 1, 0);
    if (!x86_cond(c, 0xD)) {                                      /* below 4 */
        c->r[R_AX] = 1;
        x86_leave(c);
        c->icount += 7;
        near_ret(c);
        return 1;
    }
    c->r[R_AX] = x86_shift(c, 7, c->r[R_AX], 2, 1);
    seg_write16(c, ss, (uint16_t)(bp - 4), c->r[R_AX]);
    c->icount += 6;
    for (;;) {                                                    /* 0x02F79 */
        const int16_t v = (int16_t)seg_read16(c, ss, (uint16_t)(bp + 4));
        const int16_t g = (int16_t)seg_read16(c, ss, (uint16_t)(bp - 4));
        if (!g || (int32_t)v / g > 32767 || (int32_t)v / g < -32768 || !room(c, 10)) { c->ip = 0x2F79; return 1; }
        c->r[R_AX] = (uint16_t)v;
        c->r[R_DX] = (v < 0) ? 0xFFFF : 0;
        x86_idiv16(c, (uint16_t)g, 0);
        seg_write16(c, ss, (uint16_t)(bp - 2), c->r[R_AX]);
        uint16_t ax = (uint16_t)alu_add(c, c->r[R_AX], (uint16_t)g, 1, 0);
        ax = x86_shift(c, 7, ax, 1, 1);
        seg_write16(c, ss, (uint16_t)(bp - 4), ax);
        ax = (uint16_t)alu_sub(c, ax, seg_read16(c, ss, (uint16_t)(bp - 2)), 1, 0);
        c->r[R_AX] = ax;
        cpu_push16(c, ax);
        c->icount += 9;
        if (!guest_call(m, 0xEE0C, 0x2F92)) return 1;
        if (!room(c, 6)) { c->ip = 0x2F92; return 1; }
        c->r[R_BX] = cpu_pop16(c);
        alu_sub(c, c->r[R_AX], 1, 1, 0);
        c->icount += 3;
        if (!x86_cond(c, 0xF)) break;                             /* jg loops */
    }
    c->r[R_AX] = seg_read16(c, ss, (uint16_t)(bp - 4));
    x86_leave(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x01AE9 / 0x01B2E, compose_draw_sprite(x, y, size, kind): the
 * sprite's two far-pointer words from the table at 920A/9208 (4 bytes a
 * kind), its size less a quarter, and its top-left (centre less half the
 * size) passed with the target page ([0344] or [4070]) to the blitter at
 * 1E42:0188. */
static int draw_sprite(machine_t *m, uint16_t page_at, uint16_t base)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 26)) return 0;
    const uint16_t ss = c->seg[S_SS];
    x86_enter(c, 2, 0);
    const uint16_t bp = c->r[R_BP];
#define A(o) seg_read16(c, ss, (uint16_t)(bp + (o)))
    uint16_t bx = x86_shift(c, 4, A(0x0A), 2, 1);
    c->r[R_BX] = bx;
    cpu_push16(c, ds_get(c, (uint16_t)(bx - 0x6DF6)));
    cpu_push16(c, ds_get(c, (uint16_t)(bx - 0x6DF8)));
    uint16_t ax = x86_shift(c, 7, A(8), 2, 1);
    ax = (uint16_t)alu_sub(c, ax, A(8), 1, 0);
    ax = (uint16_t)alu_sub(c, 0, ax, 1, 0);
    cpu_push16(c, ax);
    cpu_push16(c, A(8));
    for (int k = 0; k < 2; k++) {
        if (k) ax = A(8);                       /* the first CWD takes the NEG's result */
        uint16_t dx = (ax & 0x8000) ? 0xFFFF : 0;
        ax = (uint16_t)alu_sub(c, ax, dx, 1, 0);
        ax = x86_shift(c, 7, ax, 1, 1);
        ax = (uint16_t)alu_sub(c, ax, A(k ? 4 : 6), 1, 0);
        ax = (uint16_t)alu_sub(c, 0, ax, 1, 0);
        cpu_push16(c, ax);
        c->r[R_DX] = dx;
    }
#undef A
    c->r[R_AX] = ax;
    cpu_push16(c, ds_get(c, page_at));
    c->icount += 25;
    if (!guest_call_far(m, (uint16_t)(base + 0x3E), (uint16_t)(base + 0x43))) return 1;
    if (!room(c, 2)) { c->ip = (uint16_t)(base + 0x43); return 1; }
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}
static int vgame_draw_sprite(machine_t *m) { return draw_sprite(m, 0x0344, 0x1AE9); }
static int vgame_draw_sprite_block(machine_t *m) { return draw_sprite(m, 0x4070, 0x1B2E); }

/* VGAME 0x0B792, camera_body_axis(axis, a, c, b): column `axis` of the
 * camera matrix (words at B082/B088/B08E + 2*axis) times the vector (a, b,
 * c), each product scaled by 104E:0000, summed in 32 bits; DX:AX. */
static int vgame_camera_body_axis(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    const uint16_t ss = c->seg[S_SS];
    x86_enter(c, 4, 0);
    const uint16_t bp = c->r[R_BP];
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, seg_read16(c, ss, (uint16_t)(bp + 6)));
    uint16_t bx = x86_shift(c, 4, seg_read16(c, ss, (uint16_t)(bp + 4)), 1, 1);
    c->r[R_BX] = bx;
    cpu_push16(c, ds_get(c, (uint16_t)(bx - 0x4F7E)));
    c->r[R_SI] = bx;
    c->icount += 7;
    static const uint16_t call_at[3] = { 0xB7A5, 0xB7BA, 0xB7CF }, row[3] = { 0, 0x4F78, 0x4F72 }, argo[3] = { 0, 0x0A, 8 };
    for (int k = 0; k < 3; k++) {
        if (k) {
            cpu_push16(c, seg_read16(c, ss, (uint16_t)(bp + argo[k])));
            cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_SI] - row[k])));
            c->icount += 2;
        }
        if (!guest_call_far(m, call_at[k], (uint16_t)(call_at[k] + 5))) return 1;
        const unsigned rest = k < 2 ? 5 + 2 + 1 : 5 + 5;
        if (!room(c, rest)) { c->ip = (uint16_t)(call_at[k] + 5); return 1; }
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_BX] = cpu_pop16(c);
        const uint16_t ax = c->r[R_AX], dx = (ax & 0x8000) ? 0xFFFF : 0;
        c->r[R_DX] = dx;
        if (!k) {
            seg_write16(c, ss, (uint16_t)(bp - 4), ax);
            seg_write16(c, ss, (uint16_t)(bp - 2), dx);
        } else {
            seg_write16(c, ss, (uint16_t)(bp - 4), (uint16_t)alu_add(c, seg_read16(c, ss, (uint16_t)(bp - 4)), ax, 1, 0));
            seg_write16(c, ss, (uint16_t)(bp - 2),
                        (uint16_t)alu_add(c, seg_read16(c, ss, (uint16_t)(bp - 2)), dx, 1, (c->flags & F_CF) ? 1u : 0u));
        }
        c->icount += 5;
    }
    c->r[R_AX] = seg_read16(c, ss, (uint16_t)(bp - 4));
    c->r[R_DX] = seg_read16(c, ss, (uint16_t)(bp - 2));
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 5;
    near_ret(c);
    return 1;
}

/* VGAME 0x0B7E6, camera_relative_depth(a, b, c, d, e, f): the depth of one
 * point from another - the matrix column at 49C2/49C8/49CE times
 * (d - a, (c - f) >> 5, b - e), each product scaled by 104E:0000, summed in
 * 32 bits and shifted right 3 (0x0EF74) - stored at [DEBE] and returned in
 * AX. */
static int vgame_camera_relative_depth(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 15)) return 0;
    const uint16_t ss = c->seg[S_SS];
    x86_enter(c, 0x0A, 0);
    const uint16_t bp = c->r[R_BP];
#define A(o) seg_read16(c, ss, (uint16_t)(bp + (o)))
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    const uint16_t ax = (uint16_t)alu_sub(c, A(6), A(0x0C), 1, 0);
    uint16_t cx = (uint16_t)alu_sub(c, A(8), A(0x0E), 1, 0);
    cx = x86_shift(c, 7, cx, 5, 1);
    const uint16_t dx = (uint16_t)alu_sub(c, A(0x0A), A(4), 1, 0);
#undef A
    c->r[R_AX] = ax; c->r[R_CX] = cx; c->r[R_DX] = dx;
    c->r[R_SI] = ax; c->r[R_DI] = cx;
    static const uint16_t call_at[3] = { 0xB80A, 0xB81D, 0xB830 }, row[3] = { 0x49C2, 0x49C8, 0x49CE };
    cpu_push16(c, dx);
    cpu_push16(c, ds_get(c, row[0]));
    c->icount += 14;
    for (int k = 0; k < 3; k++) {
        if (!guest_call_far(m, call_at[k], (uint16_t)(call_at[k] + 5))) return 1;
        if (!room(c, k < 2 ? 8 : 9)) { c->ip = (uint16_t)(call_at[k] + 5); return 1; }
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_BX] = cpu_pop16(c);
        const uint16_t p = c->r[R_AX], ph = (p & 0x8000) ? 0xFFFF : 0;
        c->r[R_DX] = ph;
        if (!k) {
            seg_write16(c, ss, (uint16_t)(bp - 0x0A), p);
            seg_write16(c, ss, (uint16_t)(bp - 8), ph);
        } else {
            seg_write16(c, ss, (uint16_t)(bp - 0x0A), (uint16_t)alu_add(c, seg_read16(c, ss, (uint16_t)(bp - 0x0A)), p, 1, 0));
            seg_write16(c, ss, (uint16_t)(bp - 8),
                        (uint16_t)alu_add(c, seg_read16(c, ss, (uint16_t)(bp - 8)), ph, 1, (c->flags & F_CF) ? 1u : 0u));
        }
        c->icount += 5;
        if (k < 2) {                                /* the next term: DI, then SI, as pushed */
            cpu_push16(c, k ? c->r[R_SI] : c->r[R_DI]);
            cpu_push16(c, ds_get(c, row[k + 1]));
            c->icount += 2;
        }
    }
    c->r[R_AX] = seg_read16(c, ss, (uint16_t)(bp - 0x0A));
    c->r[R_DX] = seg_read16(c, ss, (uint16_t)(bp - 8));
    set_r8(c, R_CL, 3);
    c->icount += 3;
    if (!guest_call(m, 0xEF74, 0xB849)) return 1;
    if (!room(c, 5)) { c->ip = 0xB849; return 1; }
    ds_put(c, 0xDEBE, c->r[R_AX]);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 5;
    near_ret(c);
    return 1;
}

/* VGAME 1452:021B, camera_matrix_multiply(row, matrix), far: the 3-vector
 * of fixed-point words at DS:row becomes row x matrix (3x3 words at
 * DS:matrix), each element the high words of its three products summed and
 * doubled - written back in place, element by element, as the original
 * does (the first product's high word stands in the element while the
 * other two are added). */
static int vgame_camera_matrix_multiply(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 49)) return 0;
    const uint16_t ds = c->seg[S_DS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    const uint16_t si = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 6));
    const uint16_t di = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 8));
    c->r[R_SI] = si; c->r[R_DI] = di;
#define W(b, o) seg_read16(c, ds, (uint16_t)((b) + (o)))
    const uint16_t x = W(si, 0), y = W(si, 2), z = W(si, 4);
    c->r[R_BX] = x; c->r[R_CX] = y; c->r[R_BP] = z;
    for (int k = 0; k < 3; k++) {
        c->r[R_AX] = x;
        x86_imul16(c, W(di, 2 * k));
        seg_write16(c, ds, (uint16_t)(si + 2 * k), c->r[R_DX]);
    }
    for (int k = 0; k < 3; k++) {
        c->r[R_AX] = y;
        x86_imul16(c, W(di, 6 + 2 * k));
        uint16_t bx = c->r[R_DX];
        c->r[R_AX] = z;
        x86_imul16(c, W(di, 0x0C + 2 * k));
        bx = (uint16_t)alu_add(c, bx, c->r[R_DX], 1, 0);
        bx = (uint16_t)alu_add(c, bx, W(si, 2 * k), 1, 0);
        bx = x86_shift(c, 4, bx, 1, 1);
        seg_write16(c, ds, (uint16_t)(si + 2 * k), bx);
        c->r[R_BX] = bx;
    }
#undef W
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 49;
    far_ret(c);
    return 1;
}

/* VGAME 1452:0330, camera_vec3_by_matrix32(vec, matrix, out), far: the
 * 3-vector of words at DS:vec times the 3x3 matrix of words at DS:matrix,
 * as three 32-bit sums of products at DS:out, built in place in the
 * original's order. */
static int vgame_camera_vec3_by_matrix32(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 51)) return 0;
    const uint16_t ds = c->seg[S_DS], ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    const uint16_t bp = c->r[R_BP];
    const uint16_t vec = seg_read16(c, ss, (uint16_t)(bp + 6));
    const uint16_t di = seg_read16(c, ss, (uint16_t)(bp + 8));
#define W(b, o) seg_read16(c, ds, (uint16_t)((b) + (o)))
    const uint16_t v[3] = { W(vec, 0), W(vec, 2), W(vec, 4) };
    const uint16_t si = seg_read16(c, ss, (uint16_t)(bp + 0x0A));
    c->r[R_SI] = si; c->r[R_DI] = di;
    c->r[R_BX] = v[0]; c->r[R_CX] = v[1]; c->r[R_BP] = v[2];
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 3; k++) {
            c->r[R_AX] = v[r];
            x86_imul16(c, W(di, 6 * r + 2 * k));
            const uint16_t lo = (uint16_t)(si + 4 * k), hi = (uint16_t)(si + 4 * k + 2);
            if (!r) {
                seg_write16(c, ds, lo, c->r[R_AX]);
                seg_write16(c, ds, hi, c->r[R_DX]);
            } else {
                seg_write16(c, ds, lo, (uint16_t)alu_add(c, W(lo, 0), c->r[R_AX], 1, 0));
                seg_write16(c, ds, hi, (uint16_t)alu_add(c, W(hi, 0), c->r[R_DX], 1, (c->flags & F_CF) ? 1u : 0u));
            }
        }
#undef W
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 51;
    far_ret(c);
    return 1;
}

/* Speculation, for a routine whose length depends on memory it also
 * writes: run it once with every byte it writes logged, and if it would not
 * fit before the next event, put the bytes and the CPU back and decline.
 * A write to a code byte is never made (it would invalidate translations);
 * the routine declines instead. */
#define UNDO_MAX 64
typedef struct {
    cpu_t cpu;
    uint32_t at[UNDO_MAX];
    uint8_t old[UNDO_MAX];
    unsigned n;
    int full;
} undo_t;

static void undo_begin(const cpu_t *c, undo_t *u) { u->cpu = *c; u->n = 0; u->full = 0; }

static void undo_put16(cpu_t *c, undo_t *u, uint16_t seg, uint16_t off, uint16_t v)
{
    for (int k = 0; k < 2; k++) {
        const uint32_t a = phys(seg, (uint16_t)(off + k));
        if (u->n == UNDO_MAX || ((cpu_codebits[a >> 3] >> (a & 7)) & 1u)) { u->full = 1; return; }
        u->at[u->n] = a;
        u->old[u->n] = mem_read8(c, a);
        u->n++;
        mem_write8(c, a, (uint8_t)(v >> (8 * k)));
    }
}

static void undo_push16(cpu_t *c, undo_t *u, uint16_t v)
{
    c->r[R_SP] = (uint16_t)(c->r[R_SP] - 2);
    undo_put16(c, u, c->seg[S_SS], c->r[R_SP], v);
}

static int undo_abort(cpu_t *c, undo_t *u)
{
    while (u->n) { u->n--; c->mem[u->at[u->n]] = u->old[u->n]; }
    *c = u->cpu;
    return 0;
}

/* The C runtime's near-heap search (VGAME 0x0F968 and its copies in five
 * other programs): find a free block of at least CX bytes (rounded up to
 * even) in the heap whose descriptor is at DS:BX - block headers are a size
 * word, odd when the block is free; [BX+6] the first block, [BX+8] the
 * rover where the search starts, [BX+0Ah] the end. Adjacent free blocks are
 * merged as they are met. The search runs from the rover to the end, then
 * once from the start to the rover. Found: the block is marked in use,
 * split when larger, the rover set after it, AX = the block, DX = DS, CF
 * clear. Not found: the rover reset to the start, CF set. Its length
 * depends on the heap, so it runs speculatively. */
static int heap_search(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (c->icount >= c->stop_at) return 0;
    const uint64_t budget = c->stop_at - c->icount;
    const uint16_t ds = c->seg[S_DS];
    undo_t u;
    undo_begin(c, &u);
    unsigned long n = 0;
#define R(x) c->r[R_##x]
#define CF_ ((c->flags & F_CF) != 0)
#define ZF_ ((c->flags & F_ZF) != 0)
#define CHECK_ do { if (n > budget || u.full) return undo_abort(c, &u); } while (0)
    R(CX) = (uint16_t)alu_inc(c, R(CX), 1);
    set_r8(c, R_CL, (uint8_t)alu_logic(c, get_r8(c, R_CL) & 0xFE, 0));
    undo_push16(c, &u, R(BX));
    c->flags = (uint16_t)(c->flags & ~F_DF);
    R(SI) = seg_read16(c, ds, (uint16_t)(R(BX) + 8));
    R(BX) = seg_read16(c, ds, (uint16_t)(R(BX) + 0x0A));
    R(DI) = (uint16_t)alu_logic(c, 0, 1);
    n += 8;
    for (;;) {
next:                                                             /* 0x0F99B */
        CHECK_;
        R(AX) = seg_read16(c, ds, R(SI)); R(SI) = (uint16_t)(R(SI) + 2);
        alu_logic(c, R(AX) & 1, 0);
        n += 3;
        if (ZF_) goto used;
        R(DI) = R(SI);
        n++;
        for (;;) {                                                /* 0x0F9A2: a free block at DI */
            CHECK_;
            R(AX) = (uint16_t)alu_dec(c, R(AX), 1);
            alu_sub(c, R(AX), R(CX), 1, 0);
            n += 3;
            if (!CF_) goto found;
            R(SI) = (uint16_t)alu_add(c, R(SI), R(AX), 1, 0);
            n += 2;
            if (CF_) goto none;
            R(DX) = R(AX);
            R(AX) = seg_read16(c, ds, R(SI)); R(SI) = (uint16_t)(R(SI) + 2);
            alu_logic(c, R(AX) & 1, 0);
            n += 4;
            if (ZF_) goto used;
            R(AX) = (uint16_t)alu_add(c, R(AX), R(DX), 1, 0);       /* merge the next free block */
            R(AX) = (uint16_t)alu_add(c, R(AX), 2, 1, 0);
            R(SI) = R(DI);
            undo_put16(c, &u, ds, (uint16_t)(R(SI) - 2), R(AX));
            n += 5;
        }
used:                                                             /* 0x0F990: skip a block in use */
        CHECK_;
        R(DX) = (uint16_t)(R(SI) - 2);
        alu_sub(c, R(DX), R(BX), 1, 0);
        n += 3;
        if (!CF_) {                                               /* 0x0F978: reached the end */
            R(AX) = R(BX);
            R(BX) = cpu_pop16(c);
            alu_logic(c, R(AX) & 1, 0);
            n += 4;
            if (!ZF_) goto give_up;                               /* the second pass is over */
            undo_push16(c, &u, R(BX));
            R(SI) = seg_read16(c, ds, (uint16_t)(R(BX) + 6));
            R(BX) = seg_read16(c, ds, (uint16_t)(R(BX) + 8));
            alu_sub(c, R(BX), R(SI), 1, 0);
            n += 5;
            if (ZF_) goto none_popped;
            R(BX) = (uint16_t)alu_dec(c, R(BX), 1);               /* odd: marks the second pass */
            R(DI) = (uint16_t)alu_logic(c, 0, 1);
            n += 3;
            goto next;
        }
        R(SI) = (uint16_t)alu_add(c, R(SI), R(AX), 1, 0);
        n += 2;
        if (CF_) goto none;
    }
none:                                                             /* 0x0F9BE */
    n++;
none_popped:                                                      /* 0x0F9C0 */
    R(BX) = cpu_pop16(c);
    n++;
give_up:                                                          /* 0x0F9C1 */
    R(AX) = seg_read16(c, ds, (uint16_t)(R(BX) + 6));
    undo_put16(c, &u, ds, (uint16_t)(R(BX) + 8), R(AX));
    c->flags |= F_CF;
    n += 5;
    CHECK_;
    c->icount += n;
    near_ret(c);
    return 1;
found:                                                            /* 0x0F9CA */
    R(BX) = cpu_pop16(c);
    undo_put16(c, &u, ds, (uint16_t)(R(SI) - 2), R(CX));
    n += 3;
    if (!ZF_) {                                                   /* split off the rest */
        R(DI) = (uint16_t)alu_add(c, R(DI), R(CX), 1, 0);
        R(AX) = (uint16_t)alu_sub(c, R(AX), R(CX), 1, 0);
        R(AX) = (uint16_t)alu_dec(c, R(AX), 1);
        undo_put16(c, &u, ds, R(DI), R(AX));
        R(DI) = (uint16_t)alu_sub(c, R(DI), R(CX), 1, 0);
        n += 5;
    }
    R(DI) = (uint16_t)alu_add(c, R(DI), R(CX), 1, 0);
    undo_put16(c, &u, ds, (uint16_t)(R(BX) + 8), R(DI));
    R(AX) = R(SI);
    R(DX) = ds;
    c->flags = (uint16_t)(c->flags & ~F_CF);
    n += 6;
    CHECK_;
    c->icount += n;
    near_ret(c);
    return 1;
#undef R
#undef CF_
#undef ZF_
#undef CHECK_
}

/* VGAME 0x04738, frame_effect_timers: the first of the four effect timers
 * (12-byte records, count at 39E0, kind at 39DE) still running counts down
 * one frame; for kind 3 cockpit lamp 7 (0x0889B) then shows colour 29h
 * while it runs on, 0 when it has just run out. One timer a call. */
static int vgame_effect_timers(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 40)) return 0;                     /* the longest path to the call */
    const uint16_t ss = c->seg[S_SS];
    x86_enter(c, 2, 0);
    const uint16_t bp = c->r[R_BP], local = (uint16_t)(bp - 2);
    seg_write16(c, ss, local, 0);
    unsigned n = 3;
    for (;;) {
        alu_sub(c, seg_read16(c, ss, local), 4, 1, 0);           /* cmp [bp-2], 4 */
        n += 2;
        if (x86_cond(c, 0xD)) break;                              /* jge: none running */
        const uint16_t bx = x86_imul3(c, seg_read16(c, ss, local), 0x0C);
        c->r[R_BX] = bx;
        alu_sub(c, ds_get(c, (uint16_t)(bx + 0x39E0)), 0, 1, 0);
        n += 3;
        if (c->flags & F_ZF) {
            seg_write16(c, ss, local, (uint16_t)alu_inc(c, seg_read16(c, ss, local), 1));
            n++;
            continue;
        }
        ds_put(c, (uint16_t)(bx + 0x39E0), (uint16_t)alu_dec(c, ds_get(c, (uint16_t)(bx + 0x39E0)), 1));
        alu_sub(c, ds_get(c, (uint16_t)(bx + 0x39DE)), 3, 1, 0);
        n += 3;
        if (!(c->flags & F_ZF)) break;
        alu_sub(c, ds_get(c, (uint16_t)(bx + 0x39E0)), 1, 1, 0);  /* cmp [..], 1 */
        c->flags ^= F_CF;                                         /* cmc */
        uint16_t ax = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, (c->flags & F_CF) ? 1u : 0u);
        ax = (uint16_t)alu_logic(c, ax & 0x29, 1);
        c->r[R_AX] = ax;
        cpu_push16(c, ax);
        cpu_push16(c, 7);
        c->icount += n + 6;
        if (!guest_call(m, 0x889B, 0x4773)) return 1;
        if (!room(c, 4)) { c->ip = 0x4773; return 1; }
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_BX] = cpu_pop16(c);
        x86_leave(c);
        c->icount += 4;
        near_ret(c);
        return 1;
    }
    x86_leave(c);
    c->icount += n + 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x02E7F, ratio15(a, b): a / b as a 1.15 fixed-point fraction -
 * |a| * 65536 / |b| by the 32-bit divide at 0x0EFB8, halved, with the sign
 * of a times the sign of b; DX:AX from the last IMUL. */
static int vgame_ratio15(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 26)) return 0;
    const uint16_t ss = c->seg[S_SS];
    x86_enter(c, 0x0C, 0);
    const uint16_t bp = c->r[R_BP];
    cpu_push16(c, c->r[R_SI]);
    set_r8(c, R_AL, 1);
    mem_write8(c, phys(ss, (uint16_t)(bp - 2)), 1);
    mem_write8(c, phys(ss, (uint16_t)(bp - 4)), 1);
    unsigned n = 7 + 2;
    alu_sub(c, seg_read16(c, ss, (uint16_t)(bp + 4)), 0, 1, 0);
    if (!x86_cond(c, 0xD)) { mem_write8(c, phys(ss, (uint16_t)(bp - 2)), 0xFF); n++; }
    alu_sub(c, seg_read16(c, ss, (uint16_t)(bp + 6)), 0, 1, 0);
    if (!x86_cond(c, 0xD)) { mem_write8(c, phys(ss, (uint16_t)(bp - 4)), 0xFF); n++; }
    for (int k = 0; k < 2; k++) {
        uint16_t ax = seg_read16(c, ss, (uint16_t)(bp + (k ? 4 : 6)));
        uint16_t dx = (ax & 0x8000) ? 0xFFFF : 0;
        ax = (uint16_t)alu_logic(c, ax ^ dx, 1);
        ax = (uint16_t)alu_sub(c, ax, dx, 1, 0);
        dx = (ax & 0x8000) ? 0xFFFF : 0;
        c->r[R_AX] = ax; c->r[R_DX] = dx;
        if (!k) { cpu_push16(c, dx); cpu_push16(c, ax); }
        else { cpu_push16(c, ax); cpu_push16(c, 0); }
    }
    c->icount += n + 14;
    if (!guest_call_pop(m, 0xEFB8, 0x2EBA, 8)) return 1;           /* RET 8: the callee takes its arguments */
    if (!room(c, 17)) { c->ip = 0x2EBA; return 1; }
    uint16_t dx = x86_shift(c, 5, c->r[R_DX], 1, 1);              /* shr dx, 1 */
    uint16_t ax = x86_shift(c, 3, c->r[R_AX], 1, 1);              /* rcr ax, 1 */
    const uint16_t cx = ax;
    c->r[R_CX] = cx;
    ax = (uint16_t)((ax & 0xFF00) | mem_read8(c, phys(ss, (uint16_t)(bp - 2))));
    dx = ax;
    ax = (uint16_t)((ax & 0xFF00) | mem_read8(c, phys(ss, (uint16_t)(bp - 4))));
    ax = (uint16_t)(int16_t)(int8_t)(ax & 0xFF);                  /* cbw */
    c->r[R_BX] = ax;
    ax = (uint16_t)((ax & 0xFF00) | (dx & 0xFF));
    ax = (uint16_t)(int16_t)(int8_t)(ax & 0xFF);
    c->r[R_SI] = ax;
    c->r[R_AX] = cx;
    c->r[R_DX] = dx;
    x86_imul16(c, c->r[R_SI]);
    x86_imul16(c, c->r[R_BX]);
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 17;
    near_ret(c);
    return 1;
}

/* VGAME 0x086CA, map_to_screen(x, y, *sx, *sy): 0 when the map is off
 * ([368C] zero); otherwise the moving map's screen x and y (0x085F1,
 * 0x08608) are stored through the pointers, and AX = 1 only when the point
 * is strictly inside the map window: [DEC0] < sx < [E32E] - 1 and
 * [DEC2] < sy < [E470] - 1. */
static int vgame_map_to_screen(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 9)) return 0;
    const uint16_t ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    const uint16_t bp = c->r[R_BP];
#define A(o) seg_read16(c, ss, (uint16_t)(bp + (o)))
    cpu_push16(c, c->r[R_SI]);
    alu_sub(c, ds_get(c, 0x368C), 0, 1, 0);
    unsigned n = 5;
    if (c->flags & F_ZF) goto outside;
    cpu_push16(c, A(4));
    c->icount += n + 1;
    if (!guest_call(m, 0x85F1, 0x86E0)) return 1;
    if (!room(c, 5)) { c->ip = 0x86E0; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = A(8);
    ds_put(c, c->r[R_BX], c->r[R_AX]);
    cpu_push16(c, A(6));
    c->icount += 4;
    if (!guest_call(m, 0x8608, 0x86EC)) return 1;
    if (!room(c, 22)) { c->ip = 0x86EC; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    const uint16_t bx = A(0x0A);
    c->r[R_BX] = bx;
    ds_put(c, bx, c->r[R_AX]);
    const uint16_t si = A(8);
    c->r[R_SI] = si;
    c->r[R_AX] = ds_get(c, 0xDEC0);
    alu_sub(c, ds_get(c, si), c->r[R_AX], 1, 0);
    n = 7;
    if (x86_cond(c, 0xE)) goto outside;                           /* jle */
    c->r[R_AX] = (uint16_t)alu_dec(c, ds_get(c, 0xE32E), 1);
    alu_sub(c, c->r[R_AX], ds_get(c, si), 1, 0);
    n += 4;
    if (x86_cond(c, 0xE)) goto outside;
    c->r[R_AX] = ds_get(c, 0xDEC2);
    alu_sub(c, ds_get(c, bx), c->r[R_AX], 1, 0);
    n += 3;
    if (x86_cond(c, 0xE)) goto outside;
    c->r[R_AX] = (uint16_t)alu_dec(c, ds_get(c, 0xE470), 1);
    alu_sub(c, c->r[R_AX], ds_get(c, bx), 1, 0);
    n += 4;
    if (x86_cond(c, 0xE)) goto outside;
    c->r[R_AX] = 1;
    goto done;
outside:                                                          /* 0x086D5 */
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
done:
#undef A
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += n + 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x01360, map_cell_bounds(*x0, *y0, *x1, *y1): both corners of a
 * map box through the cell transform at 0x013B9 - the first from origin
 * (0, 0), the second from [2293]/[2295] - each corner clamped: the first
 * to at least 0, the second to below [9510]. */
static int vgame_map_cell_bounds(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    /* The original reads its arguments through the live BP and SS, which a
     * misbehaving callee could have changed; so does this. */
#define A(o) seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + (o)))
    cpu_push16(c, A(8));
    cpu_push16(c, A(4));
    cpu_push16(c, 0);
    cpu_push16(c, 0);
    c->icount += 6;
    if (!guest_call(m, 0x13B9, 0x1370)) return 1;
    if (!room(c, 14)) { c->ip = 0x1370; return 1; }
    c->r[R_SP] = c->r[R_BP];
    unsigned n = 0;
    for (int k = 0; k < 2; k++) {                                 /* the first corner: at least 0 */
        const uint16_t bx = A(k ? 8 : 4);
        c->r[R_BX] = bx;
        alu_sub(c, ds_get(c, bx), 0, 1, 0);
        n += 3;
        if (!x86_cond(c, 0xD)) { ds_put(c, bx, 0); n++; }
    }
    cpu_push16(c, A(0x0A));
    cpu_push16(c, A(6));
    cpu_push16(c, ds_get(c, 0x2295));
    cpu_push16(c, ds_get(c, 0x2293));
    c->icount += 1 + n + 4;
    if (!guest_call(m, 0x13B9, 0x139B)) return 1;
    if (!room(c, 15)) { c->ip = 0x139B; return 1; }
    c->r[R_SP] = c->r[R_BP];
    n = 1;
    for (int k = 0; k < 2; k++) {                                 /* the second corner: below [9510] */
        uint16_t ax = ds_get(c, 0x9510);
        const uint16_t bx = A(k ? 0x0A : 6);
        c->r[R_BX] = bx;
        alu_sub(c, ds_get(c, bx), ax, 1, 0);
        n += 4;
        if (!x86_cond(c, 0xC)) {                                  /* jl skips */
            ax = (uint16_t)alu_dec(c, ax, 1);
            ds_put(c, bx, ax);
            n += 2;
        }
        c->r[R_AX] = ax;
    }
#undef A
    x86_leave(c);
    c->icount += n + 2;
    near_ret(c);
    return 1;
}

/* The C runtime's initialiser / terminator walkers (VGAME 0x0E924 and
 * 0x0E933 and their copies): call each non-null entry of the table of
 * near (2-byte) or far (4-byte) function pointers [SI, DI), last first.
 * Between calls the state is the one at the routine's entry (the loop's
 * head is its first instruction), so a call that would not fit leaves the
 * rest to the original from there. */
static int crt_call_table(machine_t *m, int far)
{
    cpu_t *c = &m->cpu;
    const uint16_t at = c->ip, back = (uint16_t)(at + (far ? 0x10 : 0x0C));
    int started = 0;
    for (;;) {
        if (!room(c, 8)) { if (!started) return 0; c->ip = at; return 1; }
        alu_sub(c, c->r[R_SI], c->r[R_DI], 1, 0);
        if (!(c->flags & F_CF)) {                                 /* jae: done */
            c->icount += 3;
            near_ret(c);
            return 1;
        }
        unsigned n;
        int call;
        uint16_t off, seg = 0;
        if (!far) {
            c->r[R_DI] = (uint16_t)alu_dec(c, c->r[R_DI], 1);
            c->r[R_DI] = (uint16_t)alu_dec(c, c->r[R_DI], 1);
            off = c->r[R_CX] = ds_get(c, c->r[R_DI]);
            call = off != 0;                                      /* jcxz */
            n = 6;
        } else {
            c->r[R_DI] = (uint16_t)alu_sub(c, c->r[R_DI], 4, 1, 0);
            off = ds_get(c, c->r[R_DI]);
            seg = ds_get(c, (uint16_t)(c->r[R_DI] + 2));
            c->r[R_AX] = (uint16_t)alu_logic(c, off | seg, 1);
            call = !(c->flags & F_ZF);
            n = 6;
        }
        c->icount += n;
        started = 1;
        if (!call) continue;
        const int ok = far ? guest_call_far_to(m, seg, off, back) : guest_call(m, off, back);
        if (!ok) return 1;
        if (!room(c, 1)) { c->ip = back; return 1; }
        c->icount++;                                              /* jmp to the head */
    }
}
static int crt_call_near_table(machine_t *m) { return crt_call_table(m, 0); }
static int crt_call_far_table(machine_t *m) { return crt_call_table(m, 1); }

/* VGAME 0x0F0EE and copies: AH = 0, then the routine after it (+6). */
static int crt_ah0_call(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 2)) return 0;
    const uint16_t at = c->ip;
    set_r8(c, R_AH, (uint8_t)alu_logic(c, 0, 0));
    c->icount += 1;
    if (!guest_call(m, (uint16_t)(at + 6), (uint16_t)(at + 5))) return 1;
    if (!room(c, 1)) { c->ip = (uint16_t)(at + 5); return 1; }
    c->icount++;
    near_ret(c);
    return 1;
}

/* VGAME 0x0EA1C and copies: f(a, b) = the routine 28h bytes before it
 * called with (a, b, 0). */
static int crt_call_with_zero(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    const uint16_t at = c->ip, ss = c->seg[S_SS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    const uint16_t bp = c->r[R_BP];
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, 0);
    cpu_push16(c, seg_read16(c, ss, (uint16_t)(bp + 6)));
    cpu_push16(c, seg_read16(c, ss, (uint16_t)(bp + 4)));
    c->icount += 6;
    if (!guest_call(m, (uint16_t)(at - 0x28), (uint16_t)(at + 0x0F))) return 1;
    if (!room(c, 3)) { c->ip = (uint16_t)(at + 0x0F); return 1; }
    c->r[R_SP] = c->r[R_BP];                                      /* mov sp, bp: the live BP, as the original */
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x0F04A and copies: the null-pointer check at exit - the XOR of
 * the 42h bytes at DS:0, against 55h; on a mismatch the message (-26h) and
 * the exit path (+4Dh) with 1. AX = 1 then. */
static int crt_null_check(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5 + 3 * 0x42 + 4)) return 0;
    const uint16_t at = c->ip, ds = c->seg[S_DS];
    cpu_push16(c, c->r[R_SI]);
    c->r[R_SI] = (uint16_t)alu_logic(c, 0, 1);
    uint8_t ah = (uint8_t)alu_logic(c, 0, 0);
    c->flags = (uint16_t)(c->flags & ~F_DF);
    uint8_t al = get_r8(c, R_AL);
    for (int k = 0; k < 0x42; k++) {
        al = mem_read8(c, phys(ds, c->r[R_SI]));
        c->r[R_SI]++;
        ah = (uint8_t)alu_logic(c, (uint16_t)(ah ^ al), 0);
    }
    c->r[R_CX] = 0;
    ah = (uint8_t)alu_logic(c, (uint16_t)(ah ^ 0x55), 0);
    c->r[R_AX] = (uint16_t)(ah << 8 | al);
    c->icount += 5 + 3 * 0x42 + 2;
    if (!(c->flags & F_ZF)) {
        if (!guest_call(m, (uint16_t)(at - 0x26), (uint16_t)(at + 0x16))) return 1;
        if (!room(c, 3)) { c->ip = (uint16_t)(at + 0x16); return 1; }
        c->r[R_AX] = 1;
        cpu_push16(c, 1);
        c->icount += 2;
        if (!guest_call(m, (uint16_t)(at + 0x4D), (uint16_t)(at + 0x1D))) return 1;
        if (!room(c, 3)) { c->ip = (uint16_t)(at + 0x1D); return 1; }
        c->r[R_AX] = 1;
        c->icount++;
    }
    if (!room(c, 2)) { c->ip = (uint16_t)(at + 0x20); return 1; }
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* A 32-bit by 16-bit signed divide that does not fault. */
static int idiv_fits(int32_t n, int16_t d)
{
    if (d == 0 || (d == -1 && n == INT32_MIN)) return 0;
    const int32_t q = n / d;
    return q <= 32767 && q >= -32768;
}

/* VGAME 0x129A2 (120A:0902), model_project: DI points at a camera-space
 * vertex of three 32-bit coordinates (x, y, z); BX at the output, two
 * 32-bit pixels. With the orthographic switch [7D8E] clear, by the high word
 * of z - at least 0x100: (x >> 8) / z_high and the same for y, the 24-bit
 * numerators taken from bytes 1..3 and 5..7; from 1 to 0xFF: (x >> 1) /
 * ((z >> 8) >> 1), the low word of z >> 8 halved; below 1: the sentinel
 * 8000h in the x pixel's high word and nothing else. Each quotient is
 * widened to 32 bits (CWD). A divide that would fault, and the orthographic
 * case, are left to the original (which takes the divide-error vector). The
 * fraction the divide throws away is the remainder: the sub-pixel position. */
static int vgame_model_project(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 28)) return 0;
    const uint16_t ds = c->seg[S_DS];
    if (mem_read8(c, phys(ds, 0x7D8E)) != 0) return 0;            /* orthographic: the original */
    undo_t u;
    undo_begin(c, &u);
    const uint16_t di = c->r[R_DI], bx = c->r[R_BX];
    uint16_t cx = ds_get(c, (uint16_t)(di + 0x0A));
    unsigned n = 3;                                               /* cmp, jne, mov cx */
    alu_sub(c, mem_read8(c, phys(ds, 0x7D8E)), 0, 0, 0);
    alu_sub(c, cx, 0x0100, 1, 0);
    n += 2;                                                       /* cmp, jl */
    c->r[R_CX] = cx;
    if (!x86_cond(c, 0xC)) {                                      /* cx >= 0x100 */
        for (int k = 0; k < 2; k++) {
            const uint16_t at = (uint16_t)(di + (k ? 5 : 1));
            const uint16_t w = seg_read16(c, ds, at);
            const int8_t hi = (int8_t)mem_read8(c, phys(ds, (uint16_t)(at + 2)));
            if (!idiv_fits((int32_t)(((uint32_t)(uint16_t)(int16_t)hi << 16) | w), (int16_t)cx)) return undo_abort(c, &u);
            c->r[R_AX] = (uint16_t)(int16_t)hi;
            c->r[R_DX] = w;                                       /* CWDE then XCHG: DX:AX */
            const uint16_t t = c->r[R_AX]; c->r[R_AX] = c->r[R_DX]; c->r[R_DX] = t;
            x86_idiv16(c, cx, 0);
            c->r[R_DX] = (c->r[R_AX] & 0x8000) ? 0xFFFF : 0;
            alu_logic(c, c->r[R_AX], 1);
            undo_put16(c, &u, ds, (uint16_t)(bx + 4 * k), c->r[R_AX]);
            undo_put16(c, &u, ds, (uint16_t)(bx + 4 * k + 2), c->r[R_DX]);
            n += 9;
        }
    } else {
        alu_sub(c, cx, 1, 1, 0);
        n += 2;                                                   /* cmp cx, 1 / jl */
        if (x86_cond(c, 0xC)) {                                   /* behind the eye: the sentinel */
            undo_put16(c, &u, ds, (uint16_t)(bx + 2), 0x8000);
            n += 1;
        } else {
            cx = x86_shift(c, 5, ds_get(c, (uint16_t)(di + 9)), 1, 1);
            c->r[R_CX] = cx;
            n += 2;
            for (int k = 0; k < 2; k++) {
                uint16_t dx = ds_get(c, (uint16_t)(di + 2 + 4 * k)), ax = ds_get(c, (uint16_t)(di + 4 * k));
                dx = x86_shift(c, 7, dx, 1, 1);
                ax = x86_shift(c, 3, ax, 1, 1);
                if (!idiv_fits((int32_t)(((uint32_t)dx << 16) | ax), (int16_t)cx)) return undo_abort(c, &u);
                c->r[R_AX] = ax; c->r[R_DX] = dx;
                x86_idiv16(c, cx, 0);
                c->r[R_DX] = (c->r[R_AX] & 0x8000) ? 0xFFFF : 0;
                alu_logic(c, c->r[R_AX], 1);
                undo_put16(c, &u, ds, (uint16_t)(bx + 4 * k), c->r[R_AX]);
                undo_put16(c, &u, ds, (uint16_t)(bx + 4 * k + 2), c->r[R_DX]);
                n += 9;
            }
        }
    }
    if (u.full || c->icount + n + 1 > c->stop_at) return undo_abort(c, &u);
    c->icount += n + 1;                                           /* the RET */
    if (g_f117_observer && g_f117_observer->vertex) observe_vertex(m, di, bx);
    near_ret(c);
    return 1;
}

/* VGAME 0x128B5 (120A:0815), model_xform_vertex: ES:SI the model vertex
 * (three words, or a part-switch escape first), BX the matrix, DI the
 * camera-space output (three 32-bit coordinates), BP the projection output.
 * A first word in 8000h..800Fh switches to articulated part n (the
 * translation at [7CDE + 12n] copied to [7CD2..7CDC], the matrix pointer
 * advanced by 36n, SI past the escape - all three persist) and reads again.
 * Then out[j] = vx * m[j] + vy * m[3 + j] + vz * m[6 + j] + t[j], every
 * product and sum at 32 bits, then the projection at 0x129A2; on return
 * BP += 8, DI += 16, SI += 6. (Segment 120A: the projection is at IP 0x0902.)
 * Everything up to the call is done speculatively
 * so that it can be undone if it would not fit; after the call the original
 * finishes the routine if the eight instructions left do not. */
static int vgame_model_xform_vertex(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 64)) return 0;
    const uint16_t ds = c->seg[S_DS], es = c->seg[S_ES];
    undo_t u;
    undo_begin(c, &u);
    unsigned n = 0;
    undo_push16(c, &u, c->r[R_CX]);
    undo_push16(c, &u, c->r[R_SI]);
    undo_push16(c, &u, c->r[R_BX]);
    undo_push16(c, &u, c->r[R_BP]);
    n += 4;
    uint16_t ax;
    for (;;) {                                                    /* 0x128B9 */
        if (n > 4000 || u.full) return undo_abort(c, &u);
        ax = seg_read16(c, es, c->r[R_SI]);
        alu_sub(c, ax, 0x8000, 1, 0);
        n += 3;                                                   /* mov, cmp, jb */
        if (c->flags & F_CF) break;
        alu_sub(c, ax, 0x8010, 1, 0);
        n += 2;                                                   /* cmp, jb */
        if (!(c->flags & F_CF)) break;
        /* 0x12868: switch to part n = AL. */
        c->r[R_BP] = cpu_pop16(c);
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_SI] = cpu_pop16(c);
        ax = (uint16_t)(ax & 0x00FF);
        undo_push16(c, &u, c->r[R_DI]);
        uint16_t di = 0x7CDE, bx = ds_get(c, 0x7D50);
        ax = x86_shift(c, 4, ax, 1, 1); ax = x86_shift(c, 4, ax, 1, 1);
        di = (uint16_t)alu_add(c, di, ax, 1, 0);
        bx = (uint16_t)alu_add(c, bx, ax, 1, 0);
        ax = x86_shift(c, 4, ax, 1, 1);
        di = (uint16_t)alu_add(c, di, ax, 1, 0);
        ax = x86_shift(c, 4, ax, 1, 1); ax = x86_shift(c, 4, ax, 1, 1);
        bx = (uint16_t)alu_add(c, bx, ax, 1, 0);
        c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], 2, 1, 0);
        for (int k = 0; k < 6; k++) {
            ax = ds_get(c, (uint16_t)(di + 2 * k));
            undo_put16(c, &u, ds, (uint16_t)(0x7CD2 + 2 * k), ax);
        }
        c->r[R_DI] = cpu_pop16(c);
        c->r[R_BX] = bx;
        undo_push16(c, &u, c->r[R_SI]);
        undo_push16(c, &u, c->r[R_BX]);
        undo_push16(c, &u, c->r[R_BP]);
        n += 34;
        /* the escape's pushes above restore bp/bx/si as the original does */
    }
    {                                                             /* 0x128C6 */
        const uint16_t di = c->r[R_DI], bx = c->r[R_BX];
        const uint16_t vy = seg_read16(c, es, (uint16_t)(c->r[R_SI] + 2));
        const uint16_t vz = seg_read16(c, es, (uint16_t)(c->r[R_SI] + 4));
        const uint16_t vx = ax;
        c->r[R_CX] = vy; c->r[R_SI] = vz; c->r[R_BP] = vx;
#define M(o) ds_get(c, (uint16_t)(bx + (o)))
#define PUT(o, v) undo_put16(c, &u, ds, (uint16_t)(di + (o)), (v))
        /* the three vx products: written */
        for (int j = 0; j < 3; j++) {
            c->r[R_AX] = vx;
            x86_imul16(c, M(2 * j));
            PUT(4 * j, c->r[R_AX]); PUT(4 * j + 2, c->r[R_DX]);
        }
        /* the vy and vz products: added into the outputs */
        for (int pass = 0; pass < 2; pass++)
            for (int j = 0; j < 3; j++) {
                c->r[R_AX] = pass ? vz : vy;
                x86_imul16(c, M((pass ? 12 : 6) + 2 * j));
                if (pass) {                                       /* the part's translation rides along */
                    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], ds_get(c, (uint16_t)(0x7CD2 + 4 * j)), 1, 0);
                    c->r[R_DX] = (uint16_t)alu_add(c, c->r[R_DX], ds_get(c, (uint16_t)(0x7CD4 + 4 * j)), 1, (c->flags & F_CF) ? 1u : 0u);
                }
                PUT(4 * j, (uint16_t)alu_add(c, ds_get(c, (uint16_t)(di + 4 * j)), c->r[R_AX], 1, 0));
                PUT(4 * j + 2, (uint16_t)alu_add(c, ds_get(c, (uint16_t)(di + 4 * j + 2)), c->r[R_DX], 1, (c->flags & F_CF) ? 1u : 0u));
            }
#undef M
#undef PUT
        n += 44;
    }
    c->r[R_BX] = cpu_pop16(c);                                    /* pop bx; push bx: BX = the projection output */
    n += 2;                                                       /* push bx rewrites the same slot */
    if (u.full || c->icount + n + 1 > c->stop_at) return undo_abort(c, &u);
    c->icount += n;                                               /* committed: no undo from here */
    c->r[R_SP] = (uint16_t)(c->r[R_SP] - 2);                      /* push bx: the slot popped above */
    seg_write16(c, c->seg[S_SS], c->r[R_SP], c->r[R_BX]);
    if (!guest_call(m, 0x0902, 0x08AA)) return 1;
    if (!room(c, 8)) { c->ip = 0x08AA; return 1; }
    c->r[R_BP] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_CX] = cpu_pop16(c);
    c->r[R_BP] = (uint16_t)alu_add(c, c->r[R_BP], 8, 1, 0);
    c->r[R_DI] = (uint16_t)alu_add(c, c->r[R_DI], 0x10, 1, 0);
    c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], 6, 1, 0);
    c->icount += 8;
    near_ret(c);
    return 1;
}

/* VGAME 0x12A2C (120A:098C), model_near_clip, far: two 3-D endpoints on the
 * stack - (x0, y0, z0) at [bp+6], [bp+0Ah], [bp+0Eh] and (x1, y1, z1) at
 * [bp+12h], [bp+16h], [bp+1Ah], each a 32-bit coordinate - are bisected until
 * the midpoint's z has a high word of at most 1 (unsigned). A pass takes the
 * midpoint (a + b + 1) >> 1 of each coordinate (x and y are kept in
 * [7D72..7D78]) and replaces an endpoint, in place on the stack, with it: the
 * second when the z high word less one is negative, else the first. 45
 * instructions a pass (46 when it is the first), 33 for the last, 2 to set up
 * the frame. A caller whose
 * endpoints never converge would spin forever; past 40 passes, or if the work
 * would not fit before the next event, the routine declines. */
static int vgame_model_near_clip(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 2 + 33)) return 0;
    const uint16_t ss = c->seg[S_SS], ds = c->seg[S_DS];
    undo_t u;
    undo_begin(c, &u);
    undo_push16(c, &u, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    const uint16_t bp = c->r[R_BP];
    unsigned n = 2;                                               /* push bp, mov bp, sp */
    uint16_t ax = 0, dx = 0;
#define ARG(o) seg_read16(c, ss, (uint16_t)(bp + (o)))
#define SETARG(o, v) undo_put16(c, &u, ss, (uint16_t)(bp + (o)), (v))
    for (int pass = 0;; pass++) {
        if (pass > 40 || u.full) return undo_abort(c, &u);
        static const uint8_t a0[3] = { 6, 0x0A, 0x0E }, a1[3] = { 0x12, 0x16, 0x1A };
        for (int k = 0; k < 3; k++) {
            ax = ARG(a0[k]); dx = ARG(a0[k] + 2);
            ax = (uint16_t)alu_add(c, ax, ARG(a1[k]), 1, 0);
            dx = (uint16_t)alu_add(c, dx, ARG(a1[k] + 2), 1, (c->flags & F_CF) ? 1u : 0u);
            ax = (uint16_t)alu_add(c, ax, 1, 1, 0);
            dx = (uint16_t)alu_add(c, dx, 0, 1, (c->flags & F_CF) ? 1u : 0u);
            dx = x86_shift(c, 7, dx, 1, 1);                       /* sar dx, 1 */
            ax = x86_shift(c, 3, ax, 1, 1);                       /* rcr ax, 1 */
            if (k < 2) {
                undo_put16(c, &u, ds, (uint16_t)(0x7D72 + 4 * k), ax);
                undo_put16(c, &u, ds, (uint16_t)(0x7D74 + 4 * k), dx);
                n += 10;
            } else n += 8;
        }
        const uint16_t cx = dx;
        c->r[R_CX] = cx;
        alu_sub(c, cx, 1, 1, 0);
        n += 3;                                                   /* mov cx, dx / cmp cx, 1 / jbe */
        if (c->flags & (F_CF | F_ZF)) break;                      /* unsigned cx <= 1: done */
        dx = (uint16_t)alu_sub(c, dx, 1, 1, 0);                   /* dec dx (its sign decides) */
        const int second = (c->flags & F_SF) != 0;                /* js */
        dx = (uint16_t)(dx + 1);                                  /* inc dx */
        const unsigned oz = second ? 0x1A : 0x0E, ox = second ? 0x12 : 6, oy = second ? 0x16 : 0x0A;
        SETARG(oz, ax); SETARG(oz + 2, dx);
        SETARG(ox, ds_get(c, 0x7D72)); SETARG(ox + 2, ds_get(c, 0x7D74));
        SETARG(oy, ds_get(c, 0x7D76)); SETARG(oy + 2, ds_get(c, 0x7D78));
        n += 2 + (second ? 12 : 13);                              /* dec, js, inc, ten moves, then jmp (and, for the first endpoint, the jmp over the other arm) */
        if (c->icount + n + 33 > c->stop_at) return undo_abort(c, &u);
    }
#undef ARG
#undef SETARG
    c->r[R_AX] = ax; c->r[R_DX] = dx;
    c->r[R_BP] = cpu_pop16(c);
    n += 2;                                                       /* pop bp, retf */
    if (u.full || c->icount + n > c->stop_at) return undo_abort(c, &u);
    c->icount += n;
    far_ret(c);
    return 1;
}

/* mov ax, [src]; mov dx, [src+2]; add ax, [add]; adc dx, 0; mov [dst], ax;
 * mov [dst+2], dx: one projected coordinate offset by the screen origin. */
static void edge_origin_pair(cpu_t *c, uint16_t src, uint16_t add, uint16_t dst)
{
    uint16_t ax = ds_get(c, src), dx = ds_get(c, (uint16_t)(src + 2));
    ax = (uint16_t)alu_add(c, ax, ds_get(c, add), 1, 0);
    dx = (uint16_t)alu_add(c, dx, 0, 1, (c->flags & F_CF) ? 1u : 0u);
    c->r[R_AX] = ax; c->r[R_DX] = dx;
    ds_put(c, dst, ax);
    ds_put(c, (uint16_t)(dst + 2), dx);
}

/* The clipped crossing from two words at lo_at and hi_at (the near clip's
 * [7D72..7D78]): shifted down a byte, widened, doubled and offset by the
 * origin - 15 instructions - stored at slot + a and again at slot + b. */
static void edge_clip_pair(cpu_t *c, uint16_t lo_at, uint16_t hi_at, uint16_t add, uint16_t si, unsigned a, unsigned b)
{
    uint16_t ax = ds_get(c, hi_at), dx = ds_get(c, lo_at);
    dx = (uint16_t)((dx & 0xFF00u) | (dx >> 8));                  /* mov dl, dh */
    dx = (uint16_t)((dx & 0x00FFu) | ((ax & 0xFFu) << 8));        /* mov dh, al */
    ax = (uint16_t)((ax & 0xFF00u) | (ax >> 8));                  /* mov al, ah */
    ax = (uint16_t)(int16_t)(int8_t)(ax & 0xFF);                  /* cwde: AL to AX */
    { const uint16_t t = ax; ax = dx; dx = t; }                   /* xchg dx, ax */
    ax = x86_shift(c, 4, ax, 1, 1);                               /* shl ax, 1 */
    dx = x86_shift(c, 2, dx, 1, 1);                               /* rcl dx, 1 */
    ax = (uint16_t)alu_add(c, ax, ds_get(c, add), 1, 0);
    dx = (uint16_t)alu_add(c, dx, 0, 1, (c->flags & F_CF) ? 1u : 0u);
    c->r[R_AX] = ax; c->r[R_DX] = dx;
    ds_put(c, (uint16_t)(si + a), ax); ds_put(c, (uint16_t)(si + a + 2), dx);
    ds_put(c, (uint16_t)(si + b), ax); ds_put(c, (uint16_t)(si + b + 2), dx);
}

/* After an edge arm's near clip: add sp, 18h; both clipped coordinates (30
 * instructions); the clipper at 130D:033F; then the marker bit 4000h in
 * [si+2] and RET. The arm that clipped the second endpoint (the code at
 * 0x12B1D) stores the crossing at +8 and +0Ch, the other at +0 and +4. */
static int edge_arm_tail(machine_t *m, int clipped_second)
{
    cpu_t *c = &m->cpu;
    const uint16_t ret_clip = clipped_second ? 0x0ADF : 0x0C1B;
    const uint16_t pub_call = clipped_second ? 0x0B2E : 0x0C69;
    const uint16_t pub_ret = clipped_second ? 0x0B33 : 0x0C6E;
    if (!room(c, 1 + 15 + 15 + 1)) { c->ip = ret_clip; return 1; }   /* ...and the call itself, which must also start before the limit */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x18, 1, 0);
    const uint16_t si = c->r[R_SI];
    edge_clip_pair(c, 0x7D72, 0x7D74, 0x8602, si, clipped_second ? 8 : 0, 0x10);
    edge_clip_pair(c, 0x7D76, 0x7D78, 0x8604, si, clipped_second ? 0x0C : 4, 0x14);
    c->icount += 1 + 15 + 15;
    if (!guest_call_far(m, pub_call, pub_ret)) return 1;
    if (!room(c, 4)) { c->ip = pub_ret; return 1; }
    c->r[R_AX] = 0x4040;
    const uint16_t slot = (uint16_t)(c->r[R_SI] + 2);              /* SI as the clipper left it */
    ds_put(c, slot, (uint16_t)alu_logic(c, ds_get(c, slot) | 0x4040, 1));
    c->icount += 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x12BDB (120A:0B3B), model_prepare_edge: BP the slot (SI = BP), DI
 * and BX the two projected vertices (record offsets less 0xD6B4); a record's
 * x high word of 8000h marks a vertex behind the eye. Both in front: both
 * projections offset by the screen origin ([8602], [8604]) into the slot,
 * then the clipper at 130D:033F. One behind: the visible end's projection
 * into the slot, the twelve camera-space words of each end pushed, the near
 * plane cut (120A:098C), the crossing stored twice, the clipper, and the
 * marker. Both behind: 8080h in [si+2]. */
static int vgame_model_prepare_edge(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 11)) return 0;
    const uint16_t di0 = c->r[R_DI], bx0 = c->r[R_BX];
    const uint16_t p0 = ds_get(c, (uint16_t)(di0 - 0x294A)), p1 = ds_get(c, (uint16_t)(bx0 - 0x294A));
    enum { FRONT, P1_BEHIND, P0_BEHIND, BOTH } arm = p0 == 0x8000 ? (p1 == 0x8000 ? BOTH : P0_BEHIND)
                                                                    : (p1 == 0x8000 ? P1_BEHIND : FRONT);
    const unsigned pre = arm == FRONT || arm == P1_BEHIND ? 34 : arm == P0_BEHIND ? 33 : 11;
    if (!room(c, pre + 1)) return 0;
    if (g_f117_observer) observe_edge_prepared(m, c->r[R_BP], di0, bx0);
    const uint16_t si = c->r[R_BP];
    c->r[R_SI] = si;
    c->r[R_AX] = p0; c->r[R_DX] = p1;
    alu_sub(c, p0, 0x8000, 1, 0);
    alu_sub(c, p1, 0x8000, 1, 0);
    if (arm == BOTH) {
        c->r[R_AX] = 0x8080;
        ds_put(c, (uint16_t)(si + 2), 0x8080);
        c->icount += 11;
        near_ret(c);
        return 1;
    }
    if (arm == FRONT) {
        c->r[R_AX] = 0xD6B4;
        const uint16_t bx = (uint16_t)alu_add(c, bx0, 0xD6B4, 1, 0);
        const uint16_t di = (uint16_t)alu_add(c, di0, 0xD6B4, 1, 0);
        c->r[R_BX] = bx; c->r[R_DI] = di;
        edge_origin_pair(c, di, 0x8602, si);
        edge_origin_pair(c, (uint16_t)(di + 4), 0x8604, (uint16_t)(si + 4));
        edge_origin_pair(c, bx, 0x8602, (uint16_t)(si + 8));
        edge_origin_pair(c, (uint16_t)(bx + 4), 0x8604, (uint16_t)(si + 0x0C));
        c->icount += 34;
        if (!guest_call_far(m, 0x0BA4, 0x0BA9)) return 1;
        if (!room(c, 1)) { c->ip = 0x0BA9; return 1; }
        c->icount += 1;
        near_ret(c);
        return 1;
    }
    const int second = arm == P1_BEHIND;                          /* the second endpoint is the one cut */
    const uint16_t vis = second ? di0 : bx0;                      /* the visible end's record */
    const unsigned at = second ? 0 : 8;
    edge_origin_pair(c, (uint16_t)(vis - 0x294C), 0x8602, (uint16_t)(si + at));
    edge_origin_pair(c, (uint16_t)(vis - 0x2948), 0x8604, (uint16_t)(si + at + 4));
    const uint16_t di = x86_shift(c, 4, di0, 1, 1);               /* shl di, 1 */
    const uint16_t bx = x86_shift(c, 4, bx0, 1, 1);               /* shl bx, 1 */
    c->r[R_DI] = di; c->r[R_BX] = bx;
    const uint16_t base[2] = { second ? bx : di, second ? di : bx };
    for (int e = 0; e < 2; e++)
        for (int k = 0; k < 6; k++) cpu_push16(c, ds_get(c, (uint16_t)(base[e] - 0x3942 - 2 * k)));
    c->icount += pre;
    if (!guest_call_far(m, second ? 0x0ADA : 0x0C16, second ? 0x0ADF : 0x0C1B)) return 1;
    return edge_arm_tail(m, second);
}

/* VGAME 0x12AD3 (120A:0A33), the model edge pass: ES:SI a list - a count
 * word, then one word an edge, its low and high bytes the two vertex
 * numbers - and BP walks the slots from 5EEAh, 18h bytes an edge. With the
 * outcode switch [7D5A] set, an edge whose two endpoints' outcode bytes (at
 * 7A02 + n) have no bit in common is skipped. Otherwise both vertex numbers
 * times eight go to model_prepare_edge, with CX, BP and SI saved around it. */
static int vgame_model_edge_pass(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;
    const uint16_t es = c->seg[S_ES];
    c->r[R_BP] = 0x5EEA;
    c->r[R_CX] = seg_read16(c, es, c->r[R_SI]);
    c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], 2, 1, 0);
    c->icount += 4;                                               /* lea, mov, add, jcxz */
    if (c->r[R_CX] == 0) { c->icount += 1; near_ret(c); return 1; }
    for (;;) {                                                    /* 0x12ADF */
        if (!room(c, 8 + 3 + 6 + 3 + 1)) { c->ip = 0x0A3F; return 1; }
        uint16_t di = seg_read16(c, es, c->r[R_SI]);
        c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], 2, 1, 0);
        uint16_t bx = (uint16_t)((di & 0xFF00u) | (di >> 8));     /* mov bx, di / mov bl, bh */
        di = (uint16_t)alu_logic(c, di & 0xFF, 1);
        bx = (uint16_t)alu_logic(c, bx & 0xFF, 1);
        c->r[R_DI] = di; c->r[R_BX] = bx;
        alu_sub(c, ds_get(c, 0x7D5A), 0, 1, 0);
        unsigned n = 8;                                           /* mov, add, mov, mov, and, and, cmp, je */
        int skip = 0;
        if (!(c->flags & F_ZF)) {                                 /* the outcode test */
            uint8_t al = mem_read8(c, phys(c->seg[S_DS], (uint16_t)(di + 0x7A02)));
            al = (uint8_t)alu_logic(c, al & mem_read8(c, phys(c->seg[S_DS], (uint16_t)(bx + 0x7A02))), 0);
            set_r8(c, R_AL, al);
            n += 3;                                               /* mov al, and al, je */
            skip = (c->flags & F_ZF) != 0;
        }
        if (!skip) {
            for (int k = 0; k < 3; k++) { di = x86_shift(c, 4, di, 1, 1); bx = x86_shift(c, 4, bx, 1, 1); }
            c->r[R_DI] = di; c->r[R_BX] = bx;
            n += 6;
            cpu_push16(c, c->r[R_CX]); cpu_push16(c, c->r[R_BP]); cpu_push16(c, c->r[R_SI]);
            n += 3;
            c->icount += n;
            if (!guest_call(m, 0x0B3B, 0x0A74)) return 1;         /* call 0x12BDB */
            if (!room(c, 3 + 2 + 1)) { c->ip = 0x0A74; return 1; }
            c->r[R_SI] = cpu_pop16(c); c->r[R_BP] = cpu_pop16(c); c->r[R_CX] = cpu_pop16(c);
            c->icount += 3;
        } else c->icount += n;
        c->r[R_BP] = (uint16_t)alu_add(c, c->r[R_BP], 0x18, 1, 0);          /* 0x12B17 */
        c->r[R_CX] = (uint16_t)(c->r[R_CX] - 1);
        c->icount += 2;                                           /* add, loop */
        if (c->r[R_CX] == 0) break;
    }
    c->icount += 1;                                               /* ret */
    near_ret(c);
    return 1;
}

/* VGAME 0x0D4F5, normal time rate: when time is compressed ([3DA8] = 2) the
 * rate returns to 1 and S ([368E]) doubles back, then 0x0D441 clamps it.
 * Flags: the compare when the rate is not 2. */
static int vgame_normal_time_rate(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;
    alu_sub(c, ds_get(c, 0x3DA8), 2, 1, 0);                       /* cmp [3DA8], 2 */
    if (!(c->flags & F_ZF)) { c->icount += 3; near_ret(c); return 1; }   /* jne to the ret */
    ds_put(c, 0x3DA8, 1);
    ds_put(c, 0x368E, x86_shift(c, 4, ds_get(c, 0x368E), 1, 1)); /* shl [368E], 1 */
    c->icount += 4;
    if (!guest_call(m, 0xD441, 0xD509)) return 1;
    if (!room(c, 1)) { c->ip = 0xD509; return 1; }
    c->icount += 1;
    near_ret(c);
    return 1;
}

/* VGAME 0x0B905, target_range(target): 0x0B933 on the target's map position
 * (words 0 and 2 of its 16-byte record at B2D0) with a third argument of 1.
 * BX is left as target*16. */
static int vgame_target_range(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    const uint16_t target = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, 1);
    const uint16_t bx = x86_shift(c, 4, target, 4, 1);           /* shl bx, 4 */
    c->r[R_BX] = bx;
    cpu_push16(c, ds_get(c, (uint16_t)(bx - 0x4D2E)));
    cpu_push16(c, ds_get(c, (uint16_t)(bx - 0x4D30)));
    c->icount += 7;
    if (!guest_call(m, 0xB933, 0xB91B)) return 1;
    if (!room(c, 2)) { c->ip = 0xB91B; return 1; }
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x0EE2C, rnd(): the 32-bit generator at [929E] steps as
 * seed * 343FDh + 269EC3h (by the 32-bit multiply at 0x0EF36) and the answer
 * is its high word's low 15 bits; DX is left as the whole high word. */
static int vgame_rnd(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    c->r[R_AX] = 0x43FD;
    c->r[R_DX] = 3;
    cpu_push16(c, 3);
    cpu_push16(c, 0x43FD);
    cpu_push16(c, ds_get(c, 0x92A0));
    cpu_push16(c, ds_get(c, 0x929E));
    c->icount += 6;
    if (!guest_call_pop(m, 0xEF36, 0xEE3F, 8)) return 1;
    if (!room(c, 7)) { c->ip = 0xEE3F; return 1; }
    const uint16_t lo = (uint16_t)alu_add(c, c->r[R_AX], 0x9EC3, 1, 0);
    const uint16_t hi = (uint16_t)alu_add(c, c->r[R_DX], 0x26, 1, (c->flags & F_CF) ? 1 : 0);
    ds_put(c, 0x929E, lo);
    ds_put(c, 0x92A0, hi);
    c->r[R_DX] = hi;
    const uint8_t ah = (uint8_t)alu_logic(c, (hi >> 8) & 0x7F, 0);   /* and ah, 7Fh */
    c->r[R_AX] = (uint16_t)(ah << 8 | (hi & 0xFF));
    c->icount += 7;
    near_ret(c);
    return 1;
}

/* VGAME 0x0D41F, engine sound: sound 19h unless [43F2] is 0, [2E0A] is not
 * and [C09A] is 0, which asks for 18h; through the driver thunk. Flags: the
 * last compare made. */
static int vgame_engine_sound(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 9)) return 0;
    uint16_t sound = 0x19;
    unsigned n;
    alu_sub(c, ds_get(c, 0x43F2), 0, 1, 0);
    if (!(c->flags & F_ZF)) n = 4;                                /* jne to push 19h */
    else {
        alu_sub(c, ds_get(c, 0x2E0A), 0, 1, 0);
        if (c->flags & F_ZF) n = 6;                               /* je to push 19h */
        else {
            alu_sub(c, ds_get(c, 0xC09A), 0, 1, 0);
            if (c->flags & F_ZF) { sound = 0x18; n = 7; }         /* je to push 18h */
            else n = 8;                                           /* push 19h, jmp */
        }
    }
    cpu_push16(c, sound);
    c->icount += n;
    if (!guest_call_far(m, 0xD43A, 0xD43F)) return 1;
    if (!room(c, 2)) { c->ip = 0xD43F; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x05021, set_element(i, v): while [368C] is set and element i's word
 * (12-byte entries, at 4712) differs from v, 0x0D578 is called with
 * ([4010], i, v). Flags: the last compare, or what 0x0D578 leaves. */
static int vgame_set_element(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 12)) return 0;
    const uint16_t i = arg(c, 0), v = arg(c, 1);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    alu_sub(c, ds_get(c, 0x368C), 0, 1, 0);
    if (c->flags & F_ZF) { x86_leave(c); c->icount += 6; near_ret(c); return 1; }
    c->r[R_AX] = v;
    const uint16_t bx = x86_imul3(c, i, 0x0C);                   /* imul bx, [bp+4], 0Ch */
    c->r[R_BX] = bx;
    alu_sub(c, ds_get(c, (uint16_t)(bx + 0x4712)), v, 1, 0);
    if (c->flags & F_ZF) { x86_leave(c); c->icount += 10; near_ret(c); return 1; }
    cpu_push16(c, v);
    cpu_push16(c, i);
    cpu_push16(c, ds_get(c, 0x4010));
    c->icount += 11;
    if (!guest_call(m, 0xD578, 0x5043)) return 1;
    if (!room(c, 2)) { c->ip = 0x5043; return 1; }
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x0EF80, shl32_at(p, n): the 32-bit value at DS:p shifted left by n
 * in place, by 0x0EF68; DX:AX the result, CX from the shift, BX restored. */
static int shl32_at(machine_t *m, uint16_t shift, uint16_t ret)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_BX]);
    const uint16_t p = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 4));
    c->r[R_BX] = p;
    c->r[R_AX] = ds_get(c, p);
    c->r[R_DX] = ds_get(c, (uint16_t)(p + 2));
    c->r[R_CX] = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 6));
    c->icount += 7;
    if (!guest_call(m, shift, ret)) return 1;
    if (!room(c, 5)) { c->ip = ret; return 1; }
    ds_put(c, p, c->r[R_AX]);
    ds_put(c, (uint16_t)(p + 2), c->r[R_DX]);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->ip = cpu_pop16(c);
    c->r[R_SP] = (uint16_t)(c->r[R_SP] + 4);                     /* ret 4 */
    c->icount += 5;
    return 1;
}
static int vgame_shl32_at(machine_t *m) { return shl32_at(m, 0xEF68, 0xEF92); }
/* START 0x097D8: the same routine, over START's own shift at 0x097C0. */
static int start_shl32_at(machine_t *m) { return shl32_at(m, 0x97C0, 0x97EA); }

/* VGAME 0x056A9, tracked warning: the warning timer [3DC0] is set to two
 * seconds' frames (S * 2) and sound 8 is requested with argument 1 through
 * 0x0D3F9. */
static int vgame_tracked_warning(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 6)) return 0;
    const uint16_t ax = x86_shift(c, 4, ds_get(c, 0x368E), 1, 1); /* shl ax, 1 */
    c->r[R_AX] = ax;
    ds_put(c, 0x3DC0, ax);
    cpu_push16(c, 1);
    cpu_push16(c, 8);
    c->icount += 5;
    if (!guest_call(m, 0xD3F9, 0x56B8)) return 1;
    if (!room(c, 3)) { c->ip = 0x56B8; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x0D557, a near wrapper passing its two word arguments to the far
 * routine at 11B8:0008. */
static int vgame_far_pair(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;
    const uint16_t a = arg(c, 0), b = arg(c, 1);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, b);
    cpu_push16(c, a);
    c->icount += 4;
    if (!guest_call_far(m, 0xD560, 0xD565)) return 1;
    if (!room(c, 4)) { c->ip = 0xD565; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x0B91D, unit_range(i): 0x0B933 on air unit i's map position
 * (words 1 and 2 of its 36-byte record at C16A) with a third argument of 0.
 * BX is left as i*36. */
static int vgame_unit_range(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    const uint16_t i = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, 0);
    const uint16_t bx = x86_imul3(c, i, 0x24);                   /* imul bx, [bp+4], 24h */
    c->r[R_BX] = bx;
    cpu_push16(c, ds_get(c, (uint16_t)(bx - 0x3E92)));
    cpu_push16(c, ds_get(c, (uint16_t)(bx - 0x3E94)));
    c->icount += 6;
    if (!guest_call(m, 0xB933, 0xB931)) return 1;
    if (!room(c, 2)) { c->ip = 0xB931; return 1; }
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x0C818, vsin(a, r): the far sine at 104E:0076 of (a, r), then
 * 104E:0000 on its result with r still on the stack. */
static int vgame_vsin(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;
    const uint16_t a = arg(c, 0), r = arg(c, 1);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, r);
    cpu_push16(c, a);
    c->icount += 4;
    if (!guest_call_far(m, 0xC821, 0xC826)) return 1;
    if (!room(c, 3)) { c->ip = 0xC826; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 2;
    if (!guest_call_far(m, 0xC828, 0xC82D)) return 1;
    if (!room(c, 4)) { c->ip = 0xC82D; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x08A4F, cockpit_message(s): the text is copied to C5A4 (by the
 * string copy at 0x0EB50) and shown for three seconds' frames: [40CC] =
 * S * 3. */
static int vgame_cockpit_message(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;
    const uint16_t s = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, s);
    cpu_push16(c, 0xC5A4);
    c->icount += 4;
    if (!guest_call(m, 0xEB50, 0x8A5B)) return 1;
    if (!room(c, 6)) { c->ip = 0x8A5B; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    const uint16_t ax = x86_imul3(c, ds_get(c, 0x368E), 3);      /* imul ax, [368E], 3 */
    c->r[R_AX] = ax;
    ds_put(c, 0x40CC, ax);
    x86_leave(c);
    c->icount += 6;
    near_ret(c);
    return 1;
}

/* VGAME 0x04D45, mission save: the deadline from the clock (0x04E4B),
 * [DEBA] cleared, 0x04D5D, then 0x04E0B on the 600h-byte block at BA56. */
static int vgame_mission_save(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 1)) return 0;
    if (!guest_call(m, 0x4E4B, 0x4D48)) return 1;
    if (!room(c, 2)) { c->ip = 0x4D48; return 1; }
    ds_put(c, 0xDEBA, 0);
    c->icount += 1;
    if (!guest_call(m, 0x4D5D, 0x4D51)) return 1;
    if (!room(c, 3)) { c->ip = 0x4D51; return 1; }
    cpu_push16(c, 0x0600);
    cpu_push16(c, 0xBA56);
    c->icount += 2;
    if (!guest_call(m, 0x4E0B, 0x4D5A)) return 1;
    if (!room(c, 3)) { c->ip = 0x4D5A; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x0EF9C, sar32_at(p, n): the 32-bit value at DS:p shifted right
 * arithmetically by n in place, by 0x0EF74; DX:AX the result, CX from the
 * shift, BX restored. */
static int vgame_sar32_at(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_BX]);
    const uint16_t p = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 4));
    c->r[R_BX] = p;
    c->r[R_AX] = ds_get(c, p);
    c->r[R_DX] = ds_get(c, (uint16_t)(p + 2));
    c->r[R_CX] = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 6));
    c->icount += 7;
    if (!guest_call(m, 0xEF74, 0xEFAE)) return 1;
    if (!room(c, 5)) { c->ip = 0xEFAE; return 1; }
    ds_put(c, p, c->r[R_AX]);
    ds_put(c, (uint16_t)(p + 2), c->r[R_DX]);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->ip = cpu_pop16(c);
    c->r[R_SP] = (uint16_t)(c->r[R_SP] + 4);                     /* ret 4 */
    c->icount += 5;
    return 1;
}

/* VGAME 0x045C5, seed the cockpit gauges: 0x045E0 for gauges 1, 2 and 3,
 * the counter in a local word. */
static int vgame_seed_gauges(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 4)) return 0;
    x86_enter(c, 2, 0);
    const uint16_t ss = c->seg[S_SS], local = (uint16_t)(c->r[R_BP] - 2);
    seg_write16(c, ss, local, 1);
    c->icount += 2;
    for (;;) {
        cpu_push16(c, seg_read16(c, ss, local));
        c->icount += 1;
        if (!guest_call(m, 0x45E0, 0x45D4)) return 1;
        if (!room(c, 6)) { c->ip = 0x45D4; return 1; }
        c->r[R_BX] = cpu_pop16(c);
        const uint16_t n = (uint16_t)alu_inc(c, seg_read16(c, ss, local), 1);
        seg_write16(c, ss, local, n);
        alu_sub(c, n, 4, 1, 0);                                   /* cmp [bp-2], 4 */
        c->icount += 4;
        if (!x86_cond(c, 0xC)) break;                             /* jl */
    }
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x08880, map_marker(x, y, colour): 0x0886A sets the colour, then
 * 0x087FD draws the point (x, y) to (x, y). */
static int vgame_map_marker(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 4)) return 0;
    const uint16_t x = arg(c, 0), y = arg(c, 1), colour = arg(c, 2);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, colour);
    c->icount += 3;
    if (!guest_call(m, 0x886A, 0x8889)) return 1;
    if (!room(c, 6)) { c->ip = 0x8889; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, y);
    cpu_push16(c, x);
    cpu_push16(c, y);
    cpu_push16(c, x);
    c->icount += 5;
    if (!guest_call(m, 0x87FD, 0x8899)) return 1;
    if (!room(c, 2)) { c->ip = 0x8899; return 1; }
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x02E5E: 0x0DFA9 with (2D94, [2DEE], [2DF0], [2DF2]), then the byte
 * [2DFB] and the word [2DF8] cleared. Flags from the stack release. */
static int vgame_matrix_from_angles(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;
    cpu_push16(c, ds_get(c, 0x2DF2));
    cpu_push16(c, ds_get(c, 0x2DF0));
    cpu_push16(c, ds_get(c, 0x2DEE));
    cpu_push16(c, 0x2D94);
    c->icount += 4;
    if (!guest_call(m, 0xDFA9, 0x2E70)) return 1;
    if (!room(c, 4)) { c->ip = 0x2E70; return 1; }
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);      /* add sp, 8 */
    mem_write8(c, phys(c->seg[S_DS], 0x2DFB), 0);
    ds_put(c, 0x2DF8, 0);
    c->icount += 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x089F7, panel_number(n, x, y, colour): n in decimal into the
 * buffer at C604 (0x0EB9E, radix 10), then drawn there by 0x0895E. */
static int vgame_panel_number(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 6)) return 0;
    const uint16_t n = arg(c, 0), x = arg(c, 1), y = arg(c, 2), colour = arg(c, 3);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, 0x0A);
    cpu_push16(c, 0xC604);
    cpu_push16(c, n);
    c->icount += 5;
    if (!guest_call(m, 0xEB9E, 0x8A05)) return 1;
    if (!room(c, 6)) { c->ip = 0x8A05; return 1; }
    c->r[R_SP] = c->r[R_BP];                                      /* mov sp, bp */
    cpu_push16(c, colour);
    cpu_push16(c, y);
    cpu_push16(c, x);
    cpu_push16(c, 0xC604);
    c->icount += 5;
    if (!guest_call(m, 0x895E, 0x8A16)) return 1;
    if (!room(c, 2)) { c->ip = 0x8A16; return 1; }
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x087FD, map_line(x0, y0, x1, y1): the graphics library's line at
 * 114A:0070 with the full-screen clip (0, 0)-(13Fh, C7h) and page 0. */
static int vgame_map_line(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 12)) return 0;
    const uint16_t a = arg(c, 0), b = arg(c, 1), d = arg(c, 2), e = arg(c, 3);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    static const uint16_t fixed[5] = { 0, 0xC7, 0, 0x13F, 0 };
    for (int i = 0; i < 5; i++) cpu_push16(c, fixed[i]);
    cpu_push16(c, e);
    cpu_push16(c, d);
    cpu_push16(c, b);
    cpu_push16(c, a);
    c->icount += 11;
    if (!guest_call_far(m, 0x8818, 0x881D)) return 1;
    if (!room(c, 2)) { c->ip = 0x881D; return 1; }
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x0E3AC, cull(a, b, n): the register-argument routine at 0x0E3BF
 * with BP = a, BX = b, CX = n; DI and BP restored. */
static int vgame_cull(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    cpu_push16(c, c->r[R_BP]);
    const uint16_t frame = c->r[R_SP];
    c->r[R_BP] = frame;
    cpu_push16(c, c->r[R_DI]);
    const uint16_t ss = c->seg[S_SS];
    c->r[R_CX] = seg_read16(c, ss, (uint16_t)(frame + 8));
    c->r[R_BX] = seg_read16(c, ss, (uint16_t)(frame + 6));
    c->r[R_BP] = seg_read16(c, ss, (uint16_t)(frame + 4));
    c->icount += 6;
    if (!guest_call(m, 0xE3BF, 0xE3BC)) return 1;
    if (!room(c, 3)) { c->ip = 0xE3BC; return 1; }
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x03A3B: while [368C] is set, 0x039C0 with (4, [2E0A]). Flags: the
 * compare, or what 0x039C0 leaves. */
static int vgame_panel_mode_4(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;
    alu_sub(c, ds_get(c, 0x368C), 0, 1, 0);
    if (c->flags & F_ZF) { c->icount += 3; near_ret(c); return 1; }
    cpu_push16(c, ds_get(c, 0x2E0A));
    cpu_push16(c, 4);
    c->icount += 4;
    if (!guest_call(m, 0x39C0, 0x3A4B)) return 1;
    if (!room(c, 3)) { c->ip = 0x3A4B; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x00986: 0x00C38, then 0x009A0 and 0x00B05 each on [008E], then
 * [0CA8] cleared. */
static int vgame_mode_setup(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 1)) return 0;
    if (!guest_call(m, 0x0C38, 0x0989)) return 1;
    if (!room(c, 2)) { c->ip = 0x0989; return 1; }
    cpu_push16(c, ds_get(c, 0x008E));
    c->icount += 1;
    if (!guest_call(m, 0x09A0, 0x0990)) return 1;
    if (!room(c, 3)) { c->ip = 0x0990; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, ds_get(c, 0x008E));
    c->icount += 2;
    if (!guest_call(m, 0x0B05, 0x0998)) return 1;
    if (!room(c, 3)) { c->ip = 0x0998; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    ds_put(c, 0x0CA8, 0);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x00CF3, copy_from_dot(s, d): s advances (in its argument slot) to
 * the first '.' or the terminator, and the rest is copied to d by the string
 * copy at 0x0EB50. Flags: the compare that ended the scan, or the copy's. */
static int vgame_copy_from_dot(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t ds = c->seg[S_DS], ss = c->seg[S_SS];
    /* Count first, touching nothing: push, mov, jmp; then each pass. The routine's own writes
     * (the pushed BP and the stepped argument slot) change what a long scan would read, so a
     * scan that reaches either is left to the original. */
    const uint32_t own[4] = { phys(ss, (uint16_t)(c->r[R_SP] - 2)), phys(ss, (uint16_t)(c->r[R_SP] - 1)),
                              phys(ss, (uint16_t)(c->r[R_SP] + 2)), phys(ss, (uint16_t)(c->r[R_SP] + 3)) };
    uint64_t n = 3;
    for (uint16_t p = arg(c, 0);; p++) {
        const uint32_t at = phys(ds, p);
        if (at == own[0] || at == own[1] || at == own[2] || at == own[3]) return 0;
        const uint8_t ch = mem_read8(c, at);
        n += 3;                                                   /* mov bx, cmp '.', jne */
        if (ch == 0x2E) break;
        n += 2;                                                   /* cmp 0, je */
        if (ch == 0) break;
        n += 1;                                                   /* inc [bp+4] */
        if (n > 0x30000) return 0;                                /* a stray string: let the original run */
    }
    if (!room(c, (unsigned)n + 3)) return 0;
    const uint16_t d = arg(c, 1);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    const uint16_t slot = (uint16_t)(c->r[R_BP] + 4);
    for (;;) {
        const uint16_t bx = seg_read16(c, ss, slot);
        c->r[R_BX] = bx;
        const uint8_t ch = mem_read8(c, phys(ds, bx));
        alu_sub(c, ch, 0x2E, 0, 0);                               /* cmp byte [bx], '.' */
        if (c->flags & F_ZF) break;
        alu_sub(c, ch, 0, 0, 0);                                  /* cmp byte [bx], 0 */
        if (c->flags & F_ZF) break;
        seg_write16(c, ss, slot, (uint16_t)alu_inc(c, bx, 1));    /* inc word [bp+4] */
    }
    cpu_push16(c, d);
    cpu_push16(c, c->r[R_BX]);
    c->icount += n + 2;
    if (!guest_call(m, 0xEB50, 0x0D0F)) return 1;
    if (!room(c, 4)) { c->ip = 0x0D0F; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x0F024, the run-time library's termination messages: 0x0F097 with
 * FCh, the hook at [92A2] when one is set, then 0x0F097 with FFh. */
static int vgame_rt_messages(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = 0xFC;
    cpu_push16(c, 0xFC);
    c->icount += 4;
    if (!guest_call(m, 0xF097, 0xF02E)) return 1;
    if (!room(c, 3)) { c->ip = 0xF02E; return 1; }
    const uint16_t hook = ds_get(c, 0x92A2);
    alu_sub(c, hook, 0, 1, 0);
    c->icount += 2;
    if (!(c->flags & F_ZF)) {
        if (!guest_call(m, hook, 0xF039)) return 1;               /* call word ptr [92A2] */
        if (!room(c, 3)) { c->ip = 0xF039; return 1; }
    }
    c->r[R_AX] = 0xFF;
    cpu_push16(c, 0xFF);
    c->icount += 2;
    if (!guest_call(m, 0xF097, 0xF040)) return 1;
    if (!room(c, 3)) { c->ip = 0xF040; return 1; }
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x0E9F4, open_stream(a, b, c): a stream slot from 0x0F38A; with
 * none, 0; otherwise 0x0F1E0 (a, b, c, slot). SI restored. */
static int vgame_open_stream(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 4)) return 0;
    const uint16_t a = arg(c, 0), b = arg(c, 1), d = arg(c, 2);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    c->icount += 3;
    if (!guest_call(m, 0xF38A, 0xE9FB)) return 1;
    if (!room(c, 9)) { c->ip = 0xE9FB; return 1; }
    const uint16_t si = c->r[R_AX];
    c->r[R_SI] = si;
    alu_logic(c, si, 1);                                          /* or si, si */
    if (c->flags & F_ZF) {
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
        c->icount += 5;
    } else {
        cpu_push16(c, si);
        cpu_push16(c, d);
        cpu_push16(c, b);
        cpu_push16(c, a);
        c->icount += 7;
        if (!guest_call(m, 0xF1E0, 0xEA13)) return 1;
        if (!room(c, 5)) { c->ip = 0xEA13; return 1; }
        c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);  /* add sp, 8 */
        c->icount += 1;
    }
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x0D52E, far_triple(a, b, d): the far pair at 0x0D557 on (a, 0),
 * its answer kept in a local; 11D8:000A on (answer, b, d) - the arguments
 * the pair left on the stack - and 0x0D569 on the answer. */
static int vgame_far_triple(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 6)) return 0;
    const uint16_t a = arg(c, 0), b = arg(c, 1), d = arg(c, 2);
    x86_enter(c, 2, 0);
    const uint16_t ss = c->seg[S_SS], local = (uint16_t)(c->r[R_BP] - 2);
    cpu_push16(c, d);
    cpu_push16(c, b);
    cpu_push16(c, 0);
    cpu_push16(c, a);
    c->icount += 5;
    if (!guest_call(m, 0xD557, 0xD540)) return 1;
    if (!room(c, 5)) { c->ip = 0xD540; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    seg_write16(c, ss, local, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 4;
    if (!guest_call_far(m, 0xD546, 0xD54B)) return 1;
    if (!room(c, 3)) { c->ip = 0xD54B; return 1; }
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);      /* add sp, 6 */
    cpu_push16(c, seg_read16(c, ss, local));
    c->icount += 2;
    if (!guest_call(m, 0xD569, 0xD554)) return 1;
    if (!room(c, 3)) { c->ip = 0xD554; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x0F1B6, release_buffer(stream): a stream (flag byte +6) that is
 * open (83h) and owns its buffer (08h) frees it (0x0F8EC on word +4), drops
 * the flag and zeroes the pointer words +0, +2 and +4. SI restored. */
static int vgame_release_buffer(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 12)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    const uint16_t ds = c->seg[S_DS];
    const uint16_t si = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 4));
    c->r[R_SI] = si;
    const uint8_t fl = mem_read8(c, phys(ds, (uint16_t)(si + 6)));
    set_r8(c, R_AL, fl);
    unsigned n = 7;
    alu_logic(c, fl & 0x83, 0);                                   /* test al, 83h */
    if (!(c->flags & F_ZF)) {
        alu_logic(c, fl & 0x08, 0);                               /* test al, 8 */
        n += 2;
        if (!(c->flags & F_ZF)) {
            cpu_push16(c, ds_get(c, (uint16_t)(si + 4)));
            c->icount += n + 1;
            if (!guest_call(m, 0xF8EC, 0xF1CE)) return 1;
            if (!room(c, 9)) { c->ip = 0xF1CE; return 1; }
            c->r[R_CX] = cpu_pop16(c);
            const uint16_t s2 = c->r[R_SI];                       /* SI and DS as the callee left them */
            const uint32_t p = phys(c->seg[S_DS], (uint16_t)(s2 + 6));
            mem_write8(c, p, (uint8_t)alu_logic(c, mem_read8(c, p) & 0xF7, 0));
            c->r[R_AX] = (uint16_t)alu_logic(c, 0, 1);            /* xor ax, ax */
            ds_put(c, (uint16_t)(s2 + 4), 0);
            ds_put(c, s2, 0);
            ds_put(c, (uint16_t)(s2 + 2), 0);
            n = 6;                                                /* pop cx .. mov [si+2], ax */
        }
    }
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += n + 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x0C89C, gauge_value(i): the base word for gauge i (at 43EE) plus,
 * when the mission record's byte +72h has bit 0, the far reading at
 * 1E42:02EB for i. BX is left as i*2. */
static int vgame_gauge_value(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 13)) return 0;
    const uint16_t i = arg(c, 0);
    x86_enter(c, 4, 0);
    const uint16_t ss = c->seg[S_SS], local = (uint16_t)(c->r[R_BP] - 4);
    const uint16_t off = ds_get(c, 0xE574), seg = ds_get(c, 0xE576);
    c->r[R_BX] = off;
    c->seg[S_ES] = seg;                                           /* les bx, [E574] */
    alu_logic(c, mem_read8(c, phys(seg, (uint16_t)(off + 0x72))) & 1, 0);
    if (!(c->flags & F_ZF)) {
        cpu_push16(c, i);
        c->icount += 5;
        if (!guest_call_far(m, 0xC8AE, 0xC8B3)) return 1;
        if (!room(c, 9)) { c->ip = 0xC8B3; return 1; }
        c->r[R_BX] = cpu_pop16(c);
        c->icount += 2;
    } else {
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
        c->icount += 5;
    }
    seg_write16(c, ss, local, c->r[R_AX]);
    const uint16_t bx = x86_shift(c, 4, i, 1, 1);                /* shl bx, 1 */
    c->r[R_BX] = bx;
    c->r[R_AX] = (uint16_t)alu_add(c, ds_get(c, (uint16_t)(bx + 0x43EE)), seg_read16(c, ss, local), 1, 0);
    x86_leave(c);
    c->icount += 7;
    near_ret(c);
    return 1;
}

/* VGAME 0x03A0D: while [368C] is set, the value [3666], at most 9999, is
 * shown in tens by 0x039C0 (3, value / 10). */
static int vgame_panel_mode_3(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 15)) return 0;
    x86_enter(c, 2, 0);
    const uint16_t ss = c->seg[S_SS], local = (uint16_t)(c->r[R_BP] - 2);
    alu_sub(c, ds_get(c, 0x368C), 0, 1, 0);
    if (c->flags & F_ZF) { x86_leave(c); c->icount += 5; near_ret(c); return 1; }
    const uint16_t v = ds_get(c, 0x3666);
    c->r[R_AX] = v;
    seg_write16(c, ss, local, v);
    unsigned n = 7;
    alu_sub(c, v, 0x270F, 1, 0);                                  /* cmp ax, 9999 */
    if (!x86_cond(c, 0xE)) { seg_write16(c, ss, local, 0x270F); n = 8; }   /* jle not taken */
    c->r[R_CX] = 0x0A;
    c->r[R_AX] = seg_read16(c, ss, local);
    c->r[R_DX] = (c->r[R_AX] & 0x8000) ? 0xFFFF : 0;              /* cwd */
    x86_idiv16(c, 0x0A, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, 3);
    c->icount += n + 6;
    if (!guest_call(m, 0x39C0, 0x3A37)) return 1;
    if (!room(c, 4)) { c->ip = 0x3A37; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x08435: the cockpit layout's fixed positions, then on into 0x08575
 * (a jump, not a call: the original carries on from there). */
static int vgame_cockpit_layout(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    static const uint16_t set[7][2] = { { 0xDF06, 0 }, { 0xDEC0, 0x4A }, { 0xE32E, 0x96 }, { 0xDEC2, 0x6E },
                                        { 0xE470, 0xA9 }, { 0x98FA, 0x4D }, { 0x98FC, 0x3C } };
    for (int i = 0; i < 7; i++) ds_put(c, set[i][0], set[i][1]);
    c->icount += 8;
    c->ip = 0x8575;
    return 1;
}

/* VGAME 0x0895E and its twin 0x0898F, panel_text(s, x, y, colour): the text
 * routine at 0x089C0 on the current page's text record - [4028] while the
 * second page is shown ([40B6] set), else [4010]. Flags: the compare. */
static int panel_text_at(machine_t *m, uint16_t ret_ip)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 11)) return 0;
    const uint16_t a = arg(c, 0), b = arg(c, 1), d = arg(c, 2), e = arg(c, 3);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    alu_sub(c, mem_read8(c, phys(c->seg[S_DS], 0x40B6)), 0, 0, 0);   /* cmp byte [40B6], 0 */
    const int second = !(c->flags & F_ZF);
    cpu_push16(c, e);
    cpu_push16(c, d);
    cpu_push16(c, b);
    cpu_push16(c, a);
    cpu_push16(c, ds_get(c, second ? 0x4028 : 0x4010));
    c->icount += second ? 9 : 10;
    if (!guest_call(m, 0x89C0, ret_ip)) return 1;
    if (!room(c, 2)) { c->ip = ret_ip; return 1; }
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}
static int vgame_panel_text(machine_t *m) { return panel_text_at(m, 0x898D); }
static int vgame_panel_text2(machine_t *m) { return panel_text_at(m, 0x89BE); }

/* VGAME 0x089C0, text_record(rec, s, x, y, colour): the record's +0Ch is
 * cleared, +8/+0Ah take x, y and +4 the colour; the text's length (0x0EB82)
 * and upper-cased form (0x0EDC2) go with the record to 1E42:0133. */
static int vgame_text_record(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 12)) return 0;
    const uint16_t rec = arg(c, 0), s = arg(c, 1), x = arg(c, 2), y = arg(c, 3), colour = arg(c, 4);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_BX] = rec;
    ds_put(c, (uint16_t)(rec + 0x0C), 0);
    ds_put(c, (uint16_t)(rec + 8), x);
    ds_put(c, (uint16_t)(rec + 0x0A), y);
    ds_put(c, (uint16_t)(rec + 4), colour);
    c->r[R_AX] = colour;
    cpu_push16(c, s);
    c->icount += 11;
    if (!guest_call(m, 0xEB82, 0x89E3)) return 1;
    if (!room(c, 4)) { c->ip = 0x89E3; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, s);
    c->icount += 3;
    if (!guest_call(m, 0xEDC2, 0x89EB)) return 1;
    if (!room(c, 4)) { c->ip = 0x89EB; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, rec);
    c->icount += 3;
    if (!guest_call_far(m, 0x89F0, 0x89F5)) return 1;
    if (!room(c, 2)) { c->ip = 0x89F5; return 1; }
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x02C87, attitude_step(a, b): the frame counter [2DF8] advances and
 * every eighth frame sets [2DFB]; the library's 114A:04B4 turns the vector
 * 2DDC by (a, b), and the result is copied over 2D94 (12h bytes, 0x0EDE0). */
static int vgame_attitude_step(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 9)) return 0;
    const uint16_t a = arg(c, 0), b = arg(c, 1);
    x86_enter(c, 4, 0);
    const uint16_t n = (uint16_t)alu_inc(c, ds_get(c, 0x2DF8), 1);
    ds_put(c, 0x2DF8, n);
    alu_logic(c, n & 7, 0);                                       /* test byte [2DF8], 7 */
    unsigned k = 4;
    if (c->flags & F_ZF) { mem_write8(c, phys(c->seg[S_DS], 0x2DFB), 1); k = 5; }
    cpu_push16(c, 0x2DDC);
    cpu_push16(c, b);
    cpu_push16(c, a);
    c->icount += k + 3;
    if (!guest_call_far(m, 0x2CA4, 0x2CA9)) return 1;
    if (!room(c, 6)) { c->ip = 0x2CA9; return 1; }
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);      /* add sp, 6 */
    cpu_push16(c, 0x12);
    cpu_push16(c, 0x2DDC);
    cpu_push16(c, 0x2D94);
    c->icount += 4;
    if (!guest_call(m, 0xEDE0, 0x2CB7)) return 1;
    if (!room(c, 2)) { c->ip = 0x2CB7; return 1; }
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x094AD and 0x094E1, two panel bars: the current page's first word
 * (through [4028] or [4010], kept in a local) and the word at [[4040]] go to
 * 1E42:01EC with the box (4Ah, 6Eh, 4Dh, 3Ch); the two differ only in which
 * of the pair is pushed first. */
static int panel_bar(machine_t *m, int page_first, uint16_t call_ip)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 18)) return 0;
    x86_enter(c, 2, 0);
    alu_sub(c, mem_read8(c, phys(c->seg[S_DS], 0x40B6)), 0, 0, 0);
    const int second = !(c->flags & F_ZF);
    const uint16_t bx = ds_get(c, second ? 0x4028 : 0x4010);
    const uint16_t ax = ds_get(c, bx);
    c->r[R_AX] = ax;
    seg_write16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] - 2), ax);
    static const uint16_t box[4] = { 0x3C, 0x4D, 0x6E, 0x4A };
    for (int i = 0; i < 4; i++) cpu_push16(c, box[i]);
    const uint16_t pb = ds_get(c, 0x4040);
    c->r[R_BX] = pb;
    const uint16_t other = ds_get(c, pb);
    cpu_push16(c, page_first ? ax : other);
    cpu_push16(c, 0x6E);
    cpu_push16(c, 0x4A);
    cpu_push16(c, page_first ? other : ax);
    c->icount += (second ? 5 : 4) + 2 + 4 + 1 + 4;
    if (!guest_call_far(m, call_ip, (uint16_t)(call_ip + 5))) return 1;
    if (!room(c, 2)) { c->ip = (uint16_t)(call_ip + 5); return 1; }
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}
static int vgame_panel_bar_a(machine_t *m) { return panel_bar(m, 0, 0x94DA); }
static int vgame_panel_bar_b(machine_t *m) { return panel_bar(m, 1, 0x950E); }

/* VGAME 0x013B9, map_project(x, y, px, py): the map position to the screen
 * at the current zoom - *px = (x - [0D1E] + [9512]) / [950E] and
 * *py = ((y - [0D20]) * 4 / 3 + [9514]) / [950E]. Declines, before touching
 * anything, when a divide would trap. Flags from the last add. */
static int vgame_map_project(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 22)) return 0;
    const uint16_t x = arg(c, 0), y = arg(c, 1), px = arg(c, 2), py = arg(c, 3);
    const int16_t scale = (int16_t)ds_get(c, 0x950E);
    const int16_t ax1 = (int16_t)(uint16_t)(x - ds_get(c, 0x0D1E) + ds_get(c, 0x9512));
    const int16_t ay1 = (int16_t)(uint16_t)((uint16_t)((uint16_t)(y - ds_get(c, 0x0D20)) << 2));
    const int16_t ay2 = (int16_t)(uint16_t)(ay1 / 3 + ds_get(c, 0x9514));
    if (scale == 0 || (scale == -1 && (ax1 == INT16_MIN || ay2 == INT16_MIN))) return 0;
    /* *px is written before the second half reads 0D20, 9514 and 950E: an
     * alias would change what the checks above assumed. */
    for (int k = -1; k <= 1; k++)
        if ((uint16_t)(px + k) == 0x0D20 || (uint16_t)(px + k) == 0x9514 || (uint16_t)(px + k) == 0x950E) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    uint16_t ax = (uint16_t)alu_sub(c, x, ds_get(c, 0x0D1E), 1, 0);
    ax = (uint16_t)alu_add(c, ax, ds_get(c, 0x9512), 1, 0);
    c->r[R_AX] = ax;
    c->r[R_DX] = (ax & 0x8000) ? 0xFFFF : 0;                      /* cwd */
    x86_idiv16(c, (uint16_t)scale, 0);
    c->r[R_BX] = px;
    ds_put(c, px, c->r[R_AX]);
    ax = (uint16_t)alu_sub(c, y, ds_get(c, 0x0D20), 1, 0);
    ax = x86_shift(c, 4, ax, 2, 1);                               /* shl ax, 2 */
    c->r[R_AX] = ax;
    c->r[R_CX] = 3;
    c->r[R_DX] = (ax & 0x8000) ? 0xFFFF : 0;
    x86_idiv16(c, 3, 0);
    ax = (uint16_t)alu_add(c, c->r[R_AX], ds_get(c, 0x9514), 1, 0);
    c->r[R_AX] = ax;
    c->r[R_DX] = (ax & 0x8000) ? 0xFFFF : 0;
    x86_idiv16(c, (uint16_t)scale, 0);
    c->r[R_BX] = py;
    ds_put(c, py, c->r[R_AX]);
    x86_leave(c);
    c->icount += 22;
    near_ret(c);
    return 1;
}

/* VGAME 0x0B933, point_range(x, y, bearing): the offset from the aircraft
 * ([C0D0], [C0DE]) to (x, y); when asked, its bearing (0x0C702 on (-dx, dy))
 * into [952E]; always its octagonal distance (0x0C6B3) into [952A]. */
static int vgame_point_range(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 13)) return 0;
    const uint16_t x = arg(c, 0), y = arg(c, 1), want = arg(c, 2);
    x86_enter(c, 4, 0);
    const uint16_t ss = c->seg[S_SS], bp = c->r[R_BP];
    const uint16_t dx = (uint16_t)alu_sub(c, ds_get(c, 0xC0D0), x, 1, 0);
    c->r[R_AX] = dx;
    seg_write16(c, ss, (uint16_t)(bp - 2), dx);
    const uint16_t dy = (uint16_t)alu_sub(c, ds_get(c, 0xC0DE), y, 1, 0);
    c->r[R_CX] = dy;
    seg_write16(c, ss, (uint16_t)(bp - 4), dy);
    alu_sub(c, want, 0, 1, 0);                                    /* cmp [bp+8], 0 */
    c->icount += 9;
    if (!(c->flags & F_ZF)) {
        cpu_push16(c, dy);
        c->r[R_AX] = (uint16_t)alu_sub(c, 0, dx, 1, 0);           /* neg ax */
        cpu_push16(c, c->r[R_AX]);
        c->icount += 3;
        if (!guest_call(m, 0xC702, 0xB957)) return 1;
        if (!room(c, 6)) { c->ip = 0xB957; return 1; }
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_BX] = cpu_pop16(c);
        ds_put(c, 0x952E, c->r[R_AX]);
        c->icount += 3;
    }
    cpu_push16(c, seg_read16(c, ss, (uint16_t)(bp - 4)));
    cpu_push16(c, seg_read16(c, ss, (uint16_t)(bp - 2)));
    c->icount += 2;
    if (!guest_call(m, 0xC6B3, 0xB965)) return 1;
    if (!room(c, 5)) { c->ip = 0xB965; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    ds_put(c, 0x952A, c->r[R_AX]);
    x86_leave(c);
    c->icount += 5;
    near_ret(c);
    return 1;
}

/* VGAME 0x087C1, map_line_projected(x0, y0, x1, y1): each end through the
 * moving map's screen x (0x085F1) and y (0x08608), then the library's line
 * at 114A:0070 clipped to the map window ([DEC0], [E32E], [DEC2], [E470])
 * on page 1. */
static int vgame_map_line_projected(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 9)) return 0;
    const uint16_t a = arg(c, 0), b = arg(c, 1), d = arg(c, 2), e = arg(c, 3);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, 1);
    cpu_push16(c, ds_get(c, 0xE470));
    cpu_push16(c, ds_get(c, 0xDEC2));
    cpu_push16(c, ds_get(c, 0xE32E));
    cpu_push16(c, ds_get(c, 0xDEC0));
    cpu_push16(c, e);
    c->icount += 8;
    if (!guest_call(m, 0x8608, 0x87DC)) return 1;
    static const struct { uint16_t target, ret; } step[3] = { { 0x85F1, 0x87E4 }, { 0x8608, 0x87EC }, { 0x85F1, 0x87F4 } };
    const uint16_t next[3] = { d, b, a };
    uint16_t at = 0x87DC;
    for (int i = 0; i < 3; i++) {
        if (!room(c, 4)) { c->ip = at; return 1; }
        c->r[R_BX] = cpu_pop16(c);
        cpu_push16(c, c->r[R_AX]);
        cpu_push16(c, next[i]);
        c->icount += 3;
        if (!guest_call(m, step[i].target, step[i].ret)) return 1;
        at = step[i].ret;
    }
    if (!room(c, 3)) { c->ip = at; return 1; }
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 2;
    if (!guest_call_far(m, 0x87F6, 0x87FB)) return 1;
    if (!room(c, 2)) { c->ip = 0x87FB; return 1; }
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x08575 / 0x085B3, map zoom in and out. In the cockpit's outside view
 * ([C0A6] bit 7) the step [3D9E] moves instead. Otherwise the map scale
 * [40AE] (with [DF06] = 0; 2..9) changes and the map is redrawn around the
 * aircraft (0x08462), and with [DF06] = 1 the second scale [40B0] (0..7). */
static int map_zoom(machine_t *m, int in)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 15)) return 0;
    alu_logic(c, mem_read8(c, phys(c->seg[S_DS], 0xC0A6)) & 0x80, 0);
    if (!(c->flags & F_ZF)) {
        const uint16_t v = ds_get(c, 0x3D9E);
        ds_put(c, 0x3D9E, (uint16_t)(in ? alu_dec(c, v, 1) : alu_inc(c, v, 1)));
        c->icount += 4;
        near_ret(c);
        return 1;
    }
    c->icount += 2;
    alu_sub(c, ds_get(c, 0xDF06), 0, 1, 0);
    c->icount += 2;
    if (c->flags & F_ZF) {
        const uint16_t s = ds_get(c, 0x40AE);
        alu_sub(c, s, in ? 9 : 2, 1, 0);
        c->icount += 2;
        if (in ? !x86_cond(c, 0xD) : !x86_cond(c, 0xE)) {         /* jge / jle not taken */
            ds_put(c, 0x40AE, (uint16_t)(in ? alu_inc(c, s, 1) : alu_dec(c, s, 1)));
            cpu_push16(c, ds_get(c, 0xC0DE));
            cpu_push16(c, ds_get(c, 0xC0D0));
            c->icount += 3;
            const uint16_t ret = in ? 0x859E : 0x85DC;
            if (!guest_call(m, 0x8462, ret)) return 1;
            if (!room(c, 8)) { c->ip = ret; return 1; }
            c->r[R_BX] = cpu_pop16(c);
            c->r[R_BX] = cpu_pop16(c);
            c->icount += 2;
        }
    }
    alu_sub(c, ds_get(c, 0xDF06), 1, 1, 0);
    c->icount += 2;
    if (c->flags & F_ZF) {
        const uint16_t s = ds_get(c, 0x40B0);
        alu_sub(c, s, in ? 7 : 0, 1, 0);
        c->icount += 2;
        if (in ? !x86_cond(c, 0xD) : !(c->flags & F_ZF)) {        /* jge / je not taken */
            ds_put(c, 0x40B0, (uint16_t)(in ? alu_inc(c, s, 1) : alu_dec(c, s, 1)));
            c->icount += 1;
        }
    }
    c->icount += 1;
    near_ret(c);
    return 1;
}
static int vgame_map_zoom_in(machine_t *m) { return map_zoom(m, 1); }
static int vgame_map_zoom_out(machine_t *m) { return map_zoom(m, 0); }

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
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x4ABA, vgame_eventlog_add, "append to the event log", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x49F9, vgame_alt_chain_reset, "reset the altitude-alert chain", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xC436, vgame_scene_word, "decode a scene word", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xC845, vgame_key_sign_extend, "sign-extend a key byte", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xB9F6, vgame_destroyed_type, "destroyed-object type test", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x130D, 0x00B6, vgame_poly_side_a, "polygon row span, first", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x130D, 0x00D1, vgame_poly_side_b, "polygon row span, second", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1377, 0x0116, vgame_row_offsets, "screen row offsets", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0FB2, 0x051F, vgame_spans_reset, "clear the span tables", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x130D, 0x064A, vgame_mc32_on_edge, "point on the clip edge", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x130D, 0x0671, vgame_mc32_outcode, "32-bit clip outcode", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1377, 0x0132, vgame_row_banks2, "row offsets, two banks", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1377, 0x0155, vgame_row_banks4, "row offsets, four banks", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1377, 0x01E0, vgame_fill_begin, "begin a model fill", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1452, 0x02AC, vgame_camera_row, "camera matrix row", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x0654, vgame_planes_clear, "clear the plane table", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xBA56, vgame_recon_air_ground, "terrain under a unit", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xF0F4, vgame_key_translate, "translate a key", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x0674, vgame_plane_shade, "shade a plane", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x130D, 0x00EC, vgame_poly_collect, "keep a polygon for clipping", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x393D, start_menu_hit, "menu hit test", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x59D8, start_clamp, "clamp", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x88D4, start_lzw_reset, "reset the LZW table", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x28CE, start_cel_start, "start a cel animation", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x1C3B, end_widget_hit, "widget hit test", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x185C, end_queue_dac, "queue a DAC request", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x452C, end_distance, "octagonal distance", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x539C, vgame_lmul, "32-bit multiply", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x130D, 0x0217, vgame_mclip_publish, "publish a clipped edge", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEE9C, vgame_ldiv, "32-bit signed divide", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x48B8, vgame_frame_trail, "smoke trail", 1 },
    /* The same bytes in START and END (the C runtime and two shared helpers):
     * identical code with no relocations or fixed data addresses. */
    { "matched", "START.EXE", START_47304, 0x0000, 0x96AE, vgame_abs16, "absolute value of a word", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x52BC, vgame_abs16, "absolute value of a word", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x97C0, vgame_shl32, "32-bit shift left", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x97CC, vgame_sar32, "32-bit arithmetic shift right", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x53CE, vgame_sar32, "32-bit arithmetic shift right", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x97F4, vgame_shr32, "32-bit logical shift right", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x85EC, vgame_read_vector, "read an interrupt vector", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x458B, vgame_read_vector, "read an interrupt vector", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x94DE, vgame_strlen, "string length", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5182, vgame_strlen, "string length", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x94AC, vgame_strcpy, "string copy", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5150, vgame_strcpy, "string copy", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x9612, vgame_memcpy, "block copy", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5290, vgame_memcpy, "block copy", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x95F4, vgame_farcopy, "far block copy", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5272, vgame_farcopy, "far block copy", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x978E, vgame_lmul, "32-bit multiply", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x8BEA, end_distance, "octagonal distance", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x96F4, vgame_ldiv, "32-bit signed divide", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5302, vgame_ldiv, "32-bit signed divide", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0xA745, start_mask_test, "masked sign test", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x96BC, start_set_word_pair, "set a word pair", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x39B3, start_order_desc, "order a pair, larger first", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x39D5, start_order_asc, "order a pair, smaller first", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x851A, start_load_record, "load a 20-word record", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x8530, start_pick_byte, "pick one of two bytes", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x8B3E, start_zero_bytes, "zero bytes", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x8B6C, start_copy_bytes, "copy bytes", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x8C15, start_fixmul, "fixed-point multiply", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0xA110, start_next_word, "next format argument", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0xA118, start_next_dword, "next long format argument", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x1CB1, start_order_desc, "order a pair, larger first", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x1CD3, start_order_asc, "order a pair, smaller first", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x448E, end_load_record, "load a 20-word record", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x44A4, end_pick_byte, "pick one of two bytes", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x4874, end_lzw_reset, "reset the LZW table", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x4ADE, start_zero_bytes, "zero bytes", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x4B0C, start_copy_bytes, "copy bytes", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x52CA, end_set_word_pair, "set a word pair", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x0ECD, end_scale_146, "report bar scale, 146", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x0EE0, end_scale_195, "report bar scale, 195", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x00A1, end_terrain_class, "terrain under the replay", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x4137, end_refill, "refill the replay buffer", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x4A40, end_spans_reset, "clear the span tables", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x0557, vgame_read_vector, "read an interrupt vector", 1 },
    { "matched", "SETUP.EXE", SETUP_47304, 0x0000, 0x0C69, vgame_read_vector, "read an interrupt vector", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5A82, start_next_word, "next format argument", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5A8A, start_next_dword, "next long format argument", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x1E88, start_next_word, "next format argument", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x1E90, start_next_dword, "next long format argument", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x3517, start_queue_dac, "queue a DAC request", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x3E20, start_widget_init, "start the pointer widget", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x1FE5, end_widget_init, "start the pointer widget", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x8AA0, start_spans_reset, "clear the span tables", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x8C88, start_sine, "sine by table", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x98D0, start_key_translate, "translate a key", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x963E, start_fill, "fill bytes", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xF06C, vgame_string_lookup, "look up a string by id", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x9848, start_string_lookup, "look up a string by id", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xFAC1, chain_last, "last record of a chain", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0xA913, chain_last, "last record of a chain", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5F63, chain_last, "last record of a chain", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x24B7, chain_last, "last record of a chain", 1 },
    { "matched", "MPS_LOGO.EXE", MPS_LOGO_47304, 0x0146, 0x1C91, chain_last, "last record of a chain", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x219D, chain_last, "last record of a chain", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x0A33, vgame_model_edge_pass, "model edge pass", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x0B3B, vgame_model_prepare_edge, "prepare one model edge", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x098C, vgame_model_near_clip, "near-plane clip of an edge", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x0815, vgame_model_xform_vertex, "transform a model vertex to camera space", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x0902, vgame_model_project, "perspective projection of a camera-space vertex", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xF968, heap_search, "near-heap free-block search", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x4738, vgame_effect_timers, "effect timers", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x2E7F, vgame_ratio15, "ratio as a 1.15 fraction", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x86CA, vgame_map_to_screen, "map point to screen", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x1360, vgame_map_cell_bounds, "map cell bounds", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xE924, crt_call_near_table, "call the near initialiser table", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x9142, crt_call_near_table, "call the near initialiser table", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5038, crt_call_near_table, "call the near initialiser table", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x12FC, crt_call_near_table, "call the near initialiser table", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x0EE4, crt_call_near_table, "call the near initialiser table", 1 },
    { "matched", "SETUP.EXE", SETUP_47304, 0x0000, 0x177C, crt_call_near_table, "call the near initialiser table", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xE933, crt_call_far_table, "call the far initialiser table", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x9151, crt_call_far_table, "call the far initialiser table", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5047, crt_call_far_table, "call the far initialiser table", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x130B, crt_call_far_table, "call the far initialiser table", 1 },
    { "matched", "MPS_LOGO.EXE", MPS_LOGO_47304, 0x0146, 0x0283, crt_call_far_table, "call the far initialiser table", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x0EF3, crt_call_far_table, "call the far initialiser table", 1 },
    { "matched", "SETUP.EXE", SETUP_47304, 0x0000, 0x178B, crt_call_far_table, "call the far initialiser table", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xF0EE, crt_ah0_call, "clear AH and call on", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x98CA, crt_ah0_call, "clear AH and call on", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x54A4, crt_ah0_call, "clear AH and call on", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x1890, crt_ah0_call, "clear AH and call on", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x140C, crt_ah0_call, "clear AH and call on", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEA1C, crt_call_with_zero, "call with a zero third argument", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x923A, crt_call_with_zero, "call with a zero third argument", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x0F2E, crt_call_with_zero, "call with a zero third argument", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xF04A, crt_null_check, "null-pointer check at exit", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x9826, crt_null_check, "null-pointer check at exit", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5400, crt_null_check, "null-pointer check at exit", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0xA7BA, heap_search, "near-heap free-block search", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x263E, heap_search, "near-heap free-block search", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x0F32, player_pick_byte, "one of two bytes picked by an argument", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x1FE8, player_alloc_or_die, "allocate or die", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x1C3C, dswap_alloc_or_die, "allocate or die", 1 },
    { "matched", "SETUP.EXE", SETUP_47304, 0x0000, 0x1C22, setup_alloc_or_die, "allocate or die", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5422, end_find_message, "find a message text", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x180E, player_find_message, "find a message text", 1 },
    { "matched", "SETUP.EXE", SETUP_47304, 0x0000, 0x1BC2, setup_find_message, "find a message text", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5C1A, end_flush_all_one, "flush all, mode 1", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x1A1C, player_flush_all_one, "flush all, mode 1", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xF338, vgame_flush_all_one, "flush all, mode 1", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x15D7, end_three_args_a, "call with the argument, 100h, 0, 1F25h", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x15EF, end_three_args_b, "call with the argument, 0, 100h, 1F25h", 1 },
    { "matched", "SETUP.EXE", SETUP_47304, 0x0000, 0x0503, setup_call_if_flag, "call when a flag is set", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x2023, end_call_if_flag, "call when a flag is set", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x52DC, end_rand, "random number", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x462E, vgame_release_count, "release a counted hold", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x9A72, start_release_buffer, "release a stream's buffer", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x206A, end_fatal, "fatal error message and exit", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x05D0, player_two_calls, "two calls with word pairs", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x959E, start_sprintf, "format into a string", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x521C, end_sprintf, "format into a string", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x107A, dswap_sprintf, "format into a string", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x54AA, end_class_lookup, "map a character class", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x1896, player_class_lookup, "map a character class", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x1412, dswap_class_lookup, "map a character class", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xF38A, vgame_free_stream, "first free stream", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0xA1DC, start_free_stream, "first free stream", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x860E, start_copy_table, "copy a record into the table", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x0F6C, player_copy_table, "copy a record into the table", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x085A, dswap_copy_table, "copy a record into the table", 1 },
    { "matched", "SETUP.EXE", SETUP_47304, 0x0000, 0x0BA2, setup_copy_table, "copy a record into the table", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x19AA, player_fflush, "flush a stream", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5BA8, end_fflush, "flush a stream", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x1CA2, dswap_fflush, "flush a stream", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x9B82, start_stbuf, "temporary buffer for a stream", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x55B8, end_stbuf, "temporary buffer for a stream", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x18FA, player_stbuf, "temporary buffer for a stream", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x1606, dswap_stbuf, "temporary buffer for a stream", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x9430, start_printf, "formatted output to the second stream", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x505A, end_printf, "formatted output to the second stream", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x131E, player_printf, "formatted output to the second stream", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x0F42, dswap_printf, "formatted output to the second stream", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x115C, dswap_startup_check, "start-up checksum", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x15E0, player_startup_check, "start-up checksum", 1 },
    { "matched", "SETUP.EXE", SETUP_47304, 0x0000, 0x1994, setup_startup_check, "start-up checksum", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x42D0, end_es_call, "call with ES set to DS", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x8364, start_es_call, "call with ES set to DS", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x11A2, end_repeat_call, "call n times", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x2B06, start_repeat_call, "call n times", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x0242, end_write_block, "write a block of records", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x588D, start_write_block, "write a block of records", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xF5B0, vgame_getbuf, "getbuf: a stream's buffer", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0xA4DE, start_getbuf, "getbuf: a stream's buffer", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5B66, end_getbuf, "getbuf: a stream's buffer", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x22C2, player_getbuf, "getbuf: a stream's buffer", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x1C60, dswap_getbuf, "getbuf: a stream's buffer", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x1D1C, dswap_flush_all, "flush every open stream", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5C22, end_flush_all, "flush every open stream", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x1A24, player_flush_all, "flush every open stream", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x9CAC, start_flush_all, "flush every open stream", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xF340, vgame_flush_all, "flush every open stream", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x1677, dswap_free_buffer, "flush and free a stream's buffer", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5629, end_free_buffer, "flush and free a stream's buffer", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x196B, player_free_buffer, "flush and free a stream's buffer", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x9BF3, start_free_buffer, "flush and free a stream's buffer", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x829A, start_line, "line drawer", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x4206, end_line, "line drawer", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x0790, dswap_line, "line drawer", 1 },
    { "matched", "SETUP.EXE", SETUP_47304, 0x0000, 0x1244, setup_strcpy_near, "copy a string, near pointers", 1 },
    { "matched", "SETUP.EXE", SETUP_47304, 0x0000, 0x125F, setup_strcpy_far, "copy a string to a far destination", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x4B72, end_strcpy_from_far, "copy a string from a far source", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x0664, player_linear_word, "word at a linear address", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x257B, player_find_in_table, "find a byte in the six-byte table", 1 },
    { "matched", "MPS_LOGO.EXE", MPS_LOGO_47304, 0x0146, 0x1E2E, heap_search, "near-heap free-block search", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x21BE, heap_search, "near-heap free-block search", 1 },
    { "matched", "SETUP.EXE", SETUP_47304, 0x0000, 0x1D92, heap_search, "near-heap free-block search", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x826C, start_palette_bank, "copy a palette bank", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEB10, strcat_ds, "string concatenate", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5110, strcat_ds, "string concatenate", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEFB8, vgame_uldiv, "32-bit unsigned divide", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xE530, vgame_palette_step, "step a palette toward its target", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x946C, strcat_ds, "string concatenate", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x7387, start_shared_pointers, "point at the shared state", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x3CA1, end_shared_pointers, "point at the shared state", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x02BA, start_home_risk, "home base risk", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1058, 0x0D09, vgame_axis_normalise, "normalise a joystick axis", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x02B2, vgame_model_in_range, "model range test", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x104E, 0x0076, vgame_far_sin, "far sine", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x104E, 0x0066, vgame_far_cos, "far cosine", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xC831, vgame_vcos, "vector cosine", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x2F4A, vgame_rnd_scaled, "scaled random number", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xC88C, vgame_rnd_times, "random times n", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xC880, vgame_clock_from, "mission clock from a reading", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xC6B3, vgame_dist, "octagonal distance", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x2F5B, vgame_isqrt, "integer square root", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x1AE9, vgame_draw_sprite, "draw a sprite on the screen page", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x1B2E, vgame_draw_sprite_block, "draw a sprite on the block page", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xB792, vgame_camera_body_axis, "camera body axis", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xB7E6, vgame_camera_relative_depth, "camera relative depth", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1452, 0x021B, vgame_camera_matrix_multiply, "row times the camera matrix", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1452, 0x0330, vgame_camera_vec3_by_matrix32, "vector times a matrix, 32-bit", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xD4F5, vgame_normal_time_rate, "normal time rate", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xB905, vgame_target_range, "range to a target", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEE2C, vgame_rnd, "random number", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xD41F, vgame_engine_sound, "engine sound", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x5021, vgame_set_element, "set a display element", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEF80, vgame_shl32_at, "32-bit shift left in place", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x56A9, vgame_tracked_warning, "tracked warning", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xD557, vgame_far_pair, "far call with two arguments", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xB91D, vgame_unit_range, "range to an air unit", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xC818, vgame_vsin, "vector sine", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x8A4F, vgame_cockpit_message, "cockpit message", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x4D45, vgame_mission_save, "mission save", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEF9C, vgame_sar32_at, "32-bit arithmetic shift right in place", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x45C5, vgame_seed_gauges, "seed the cockpit gauges", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x8880, vgame_map_marker, "map marker", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x2E5E, vgame_matrix_from_angles, "attitude matrix from the angles", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x89F7, vgame_panel_number, "panel number", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x87FD, vgame_map_line, "map line", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xE3AC, vgame_cull, "cull by register arguments", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x3A3B, vgame_panel_mode_4, "panel mode 4", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x0986, vgame_mode_setup, "graphics mode setup", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x0CF3, vgame_copy_from_dot, "copy from the dot", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xF024, vgame_rt_messages, "run-time termination messages", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xE9F4, vgame_open_stream, "open a stream", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xD52E, vgame_far_triple, "far call with three arguments", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xF1B6, vgame_release_buffer, "release a stream buffer", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xC89C, vgame_gauge_value, "gauge value", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x3A0D, vgame_panel_mode_3, "panel mode 3", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x8435, vgame_cockpit_layout, "cockpit layout", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x895E, vgame_panel_text, "panel text", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x898F, vgame_panel_text2, "panel text, second entry", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x89C0, vgame_text_record, "fill a text record", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x2C87, vgame_attitude_step, "attitude step", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x94AD, vgame_panel_bar_a, "panel bar", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x94E1, vgame_panel_bar_b, "panel bar, second form", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x13B9, vgame_map_project, "map projection to the screen", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xB933, vgame_point_range, "range and bearing to a point", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x87C1, vgame_map_line_projected, "map line, projected", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x8575, vgame_map_zoom_in, "map zoom in", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x85B3, vgame_map_zoom_out, "map zoom out", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x104E, 0x0000, vgame_far_fixmul, "far fixed-point multiply", 2 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x96CE, start_rand, "mission random number", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x76C0, start_rnd, "random number below n", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x8C7C, start_sine_arg, "sine of a stacked angle", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x3301, start_vsin, "scaled sine", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x3318, start_vcos, "scaled cosine", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x532A, start_dist_t, "distance between two map points", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x58BE, start_onc_target, "target text from a map point", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x97D8, start_shl32_at, "32-bit shift left in place", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1452, 0x0006, vgame_vg_sine_direct, "sine from the quarter-wave table", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1452, 0x0033, vgame_vg_cosine_direct, "cosine from the quarter-wave table", 2 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x3070, start_fade_a, "start a fade, first form", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x3088, start_fade_b, "start a fade, second form", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x3E5E, start_flag_call, "call on a flag", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x3E84, start_set_pointer, "store the pointer position", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0xA4C6, start_stack_check, "stack check", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5B4E, end_stack_check, "stack check", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1452, 0x0316, vgame_camera_matrix_copy, "copy a 3x3 camera matrix", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1452, 0x02E4, vgame_camera_matrix_transpose, "transpose a 3x3 camera matrix", 2 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x5413, start_dist, "octagonal distance of two offsets", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x7C05, start_level_scale, "a value at a terrain level's scale", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x6A7C, start_surname, "the last word of a name", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x71AD, start_form_hover, "form hover highlight", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x3DA2, start_modal_hover, "modal button hover highlight", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x0E23, start_weapon_shortage, "draw the weapon shortages", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x59FE, start_time_string, "clock text for a time", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x512E, start_target_near, "target slot for the object near a point", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x51E8, start_target_for_type, "target slot for an object of a type", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x5282, start_pick_weapon, "pick the best-rated weapon", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x5356, start_slot_fill, "fill a mission slot from a target", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x5743, start_angle, "bearing of an offset", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x7C56, start_terrain_tile, "terrain tile at a level", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x84B1, start_axis_normalise, "normalise a joystick axis", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x819D, start_lzw_refill, "refill the LZW input buffer", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0xA76C, start_heap_free, "free a near heap block", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x25D9, start_date_string, "the mission's date text", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x1F3F, start_site_boxes, "briefing map hover boxes", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x2E50, start_range_ring, "draw a range ring", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x183A, start_site_rings, "threat rings for the map's sites", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x7744, start_obj_near, "the object nearest a map point", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x05CA, end_site_boxes, "debriefing map site boxes", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x441D, end_axis_normalise, "normalise a joystick axis", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x0EF3, end_time_string, "clock text for a time", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x2AFA, end_promote, "promotion check", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x2476, end_awards, "ribbons, medal and point steps", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x5625, start_write_handover, "write the mission handover", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x2F4C, start_route_leg, "animate one leg of the route", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0xA1A4, start_format_digits, "the formatter's digits", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0xA141, start_format_putc, "the formatter's put-character", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x0113, end_load_handover, "read the mission handover", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x00C9, end_handover_text, "index the handover's text", 1 },
};

void matched_register(void)
{
    static int done;
    if (done) return;
    done = 1;
    for (unsigned i = 0; i < sizeof MATCHED / sizeof MATCHED[0]; i++) recomp_override_add(&MATCHED[i]);
    observe_register();
}

unsigned matched_count(void) { return (unsigned)(sizeof MATCHED / sizeof MATCHED[0]); }

const recomp_override *matched_entry(unsigned i)
{
    return i < matched_count() ? &MATCHED[i] : 0;
}
