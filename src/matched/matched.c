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
static void far_ret(cpu_t *c);
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

/* VGAME 0x0EB50 (and MPS_LOGO 0x01A7E, whose copy is far: the arguments a word higher, RETF), strcpy(dst, src): ES = DS, the length (with its zero) by
 * REPNE SCASB, then the copy tail; SI and DI restored, AX = dst. Declines
 * when DF is set (the tail's alignment arithmetic assumes forward). */
static int crt_strcpy(machine_t *m, int far)
{
    cpu_t *c = &m->cpu;
    if (c->flags & F_DF) return 0;
    const uint16_t dst = arg(c, far), src = arg(c, 1 + far), es = c->seg[S_ES];
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
    if (far) far_ret(c); else near_ret(c);
    return 1;
}
static int vgame_strcpy(machine_t *m) { return crt_strcpy(m, 0); }
static int mps_logo_strcpy(machine_t *m) { return crt_strcpy(m, 1); }

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

/* VGAME 1058:0C9F and SETUP 0x023AA: copy the axis word at [SI+src] into its
 * three derived slots ([SI+d1], [SI+d2], [SI+d3]). AX preserved. */
static int axis_spread(machine_t *m, uint16_t src, uint16_t d1, uint16_t d2, uint16_t d3)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    const uint16_t si = c->r[R_SI];
    cpu_push16(c, c->r[R_AX]);
    const uint16_t v = ds_get(c, (uint16_t)(si + src));
    ds_put(c, (uint16_t)(si + d1), v);
    ds_put(c, (uint16_t)(si + d2), v);
    ds_put(c, (uint16_t)(si + d3), v);
    c->r[R_AX] = cpu_pop16(c);
    c->icount += 7;
    near_ret(c);
    return 1;
}
static int vgame_axis_spread(machine_t *m) { return axis_spread(m, 0x2CCA, 0x2CB2, 0x2CA2, 0x2CAA); }
static int setup_axis_spread(machine_t *m) { return axis_spread(m, 0x0E1C, 0x0E04, 0x0DF4, 0x0DFC); }

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

/* VGAME 0x0EE9C (and MPS_LOGO 0x01BB6, a far copy: RETF 8, the arguments two bytes higher), the C runtime's signed 32-bit divide: DX:AX = a / b for
 * a = [bp+6]:[bp+4], b = [bp+A]:[bp+8], truncating, the argument slots
 * made positive in place. A 16-bit divisor takes two DIVs; a wider one is
 * shifted down with the dividend until it fits, the quotient estimated with
 * one DIV and corrected by at most one. RET 8. Declines a zero divisor,
 * where the original takes the divide-error interrupt. */
static int crt_ldiv(machine_t *m, int far)
{
    const unsigned d = far ? 2 : 0;                               /* the far return address takes a word more */
    cpu_t *c = &m->cpu;
    const uint16_t ss = c->seg[S_SS];
    const uint16_t sp = c->r[R_SP];
    if (!seg_read16(c, ss, (uint16_t)(sp + 6 + d)) && !seg_read16(c, ss, (uint16_t)(sp + 8 + d))) return 0;
    if (!room(c, 64 + 6 * 16)) return 0;
    unsigned n = 0;
    cpu_push16(c, c->r[R_BP]); c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_DI]); cpu_push16(c, c->r[R_SI]); cpu_push16(c, c->r[R_BX]);
    n += 5;
    const uint16_t bp = c->r[R_BP];
#define ARG(o) seg_read16(c, ss, (uint16_t)(bp + (o) + d))
#define SETARG(o, v) seg_write16(c, ss, (uint16_t)(bp + (o) + d), (v))
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
    if (far) far_ret(c); else near_ret(c);
    c->r[R_SP] = (uint16_t)(c->r[R_SP] + 8);
    return 1;
}
static int vgame_ldiv(machine_t *m) { return crt_ldiv(m, 0); }
static int mps_logo_ldiv(machine_t *m) { return crt_ldiv(m, 1); }

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

/* VGAME 0x0EB10 / END 0x05110 (and MPS_LOGO 0x01A3E, a far copy: the arguments a word higher, RETF), strcat(dst, src) within DS: find dst's end
 * and src's length by REPNE SCASB, then the word copy - aligned, unlike the
 * other copies, on the source address. SI and DI restored, AX = dst.
 * Forward only; the scans count through the BP the prologue pushes. */
static int crt_strcat(machine_t *m, int far)
{
    cpu_t *c = &m->cpu;
    if (c->flags & F_DF) return 0;
    const uint16_t ds = c->seg[S_DS], dst = arg(c, far), src = arg(c, 1 + far);
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
    if (far) far_ret(c); else near_ret(c);
    return 1;
}
static int strcat_ds(machine_t *m) { return crt_strcat(m, 0); }
static int mps_logo_strcat(machine_t *m) { return crt_strcat(m, 1); }

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

/* PLAYER 0x0257B (and MPS_LOGO 0x031C6, a far copy), find AL among the six bytes ending at
 * CS:[last] (searched from the last backwards): found, BX is the matching four-byte record at
 * records + 4 * index, AX is the index times four and CF is clear; not found,
 * CF is set and BX is left six below its start. CX counts down as the search
 * goes. Flags are the last compare or decrement; a find ends on CLC. */
static int find_in_table(machine_t *m, uint16_t last, uint16_t records, int far)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 36)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    uint16_t bx = last;
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
        bx = (uint16_t)alu_add(c, records, ax, 1, 0);            /* lea bx, [records]; add bx, ax */
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
    if (far) far_ret(c); else near_ret(c);
    return 1;
}
static int player_find_in_table(machine_t *m) { return find_in_table(m, 0x24DD, 0x1C96, 0); }
static int mps_logo_find_in_table(machine_t *m) { return find_in_table(m, 0x1CB7, 0x044A, 1); }

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
typedef struct { uint16_t entry, base, last; int far; } crt_freestream;   /* far: the copy that ends in RETF */

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
    if (s->far) far_ret(c); else near_ret(c);
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

/* flush_all(1) as a routine of its own (END 0x05C1A, PLAYER 0x01A1C, VGAME 0x0F338, and MPS_LOGO 0x023E0, whose copy ends in RETF): the
 * callee takes its argument off the stack (RET 2). */
static int crt_flush_all_one(machine_t *m, uint16_t entry, uint16_t callee, int far)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 3)) return 0;
    c->r[R_AX] = 1;
    cpu_push16(c, 1);
    c->icount += 2;
    if (!guest_call_pop(m, callee, (uint16_t)(entry + 7), 2)) return 1;
    if (!room(c, 1)) { c->ip = (uint16_t)(entry + 7); return 1; }
    c->icount += 1;
    if (far) far_ret(c); else near_ret(c);
    return 1;
}
static int end_flush_all_one(machine_t *m) { return crt_flush_all_one(m, 0x5C1A, 0x5C22, 0); }
static int player_flush_all_one(machine_t *m) { return crt_flush_all_one(m, 0x1A1C, 0x1A24, 0); }
static int vgame_flush_all_one(machine_t *m) { return crt_flush_all_one(m, 0xF338, 0xF340, 0); }
static int mps_logo_flush_all_one(machine_t *m) { return crt_flush_all_one(m, 0x0F80, 0x0F88, 1); }

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

/* START 0x0A1A4 and PLAYER 0x01F1C, the formatter's digits: the 32-bit value DX:AX in base CX,
 * written backwards (STD, STOSB) at ES:DI, at least SI digits; a digit past
 * '9' is moved on by the byte at the caller's [BP-1] (to 'a' or 'A'). On the
 * return DI points at the first digit and CX is the count; DF is cleared.
 * Each digit divides the high word, then the remainder and the low word,
 * the value kept in DX:BX between digits. With CX 0 the routine stops at the
 * first DIV and the original takes the fault. */
static int format_digits(machine_t *m, uint16_t entry)
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
        if (!room(c, 21)) { c->ip = (uint16_t)(entry + 3); return 1; }   /* a digit, or the tests and the exit */
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
        if (!c->r[R_CX]) { c->icount += 2; c->ip = (uint16_t)(entry + 0x14); return 1; }   /* the DIV faults */
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
static int start_format_digits(machine_t *m) { return format_digits(m, 0xA1A4); }
static int player_format_digits(machine_t *m) { return format_digits(m, 0x1F1C); }

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

/* ---- PLAYER, MPS_LOGO, DSWAP and SETUP: the small programs' copies of routines matched elsewhere ----
 * The same code at other addresses; each entry below has its own data offsets. */
static const crt_freestream MPS_LOGO_FREESTREAM = { 0x14BC, 0x027C, 0x03BC, 1 };
static int mps_logo_free_stream(machine_t *m) { return crt_free_stream(m, &MPS_LOGO_FREESTREAM); }
CLASS(mps_logo, 0x0B66, 0x0203, 0x0200, 0x0268, 0x01F8)
FREESTREAM(dswap, 0x1B9C, 0x2710, 0x2850)
static const axis_variant SETUP_AXIS = { 0x0E1C, 0x0E04, 0x0DF4, 0x0DFC, 0x0E0C, 0x0E14, 0x0E24, 0x2447, 0x2472, 1 };
static int setup_axis_normalise(machine_t *m) { return axis_normalise(m, &SETUP_AXIS); }
static int dswap_string_lookup(machine_t *m) { return string_lookup(m, 0x28FE); }
static int dswap_lzw_reset(machine_t *m) { return lzw_reset(m, 0x247A, 0x0B26); }
static int mps_logo_mask_test(machine_t *m) { return mask_test(m, 0x01FA); }
static int dswap_mask_test(machine_t *m) { return mask_test(m, 0x26C4); }

/* A REP STOS (byte or word) as the interpreter steps it, written out: x86_stos in a loop stalls MSVC 19.51
 * /O2. One clock an iteration; a REP that finds CX 0 takes one. Returns the clocks taken. */
static unsigned sm_rep_stos(cpu_t *c, int w16)
{
    if (c->r[R_CX] == 0) return 1;
    const int delta = x86_str_delta(c, w16);
    unsigned n = 0;
    do {
        if (w16) seg_write16(c, c->seg[S_ES], c->r[R_DI], c->r[R_AX]);
        else     mem_write8(c, phys(c->seg[S_ES], c->r[R_DI]), get_r8(c, R_AL));
        c->r[R_DI] = (uint16_t)(c->r[R_DI] + delta);
        c->r[R_CX] = (uint16_t)(c->r[R_CX] - 1);
        n++;
    } while (c->r[R_CX] != 0);
    return n;
}

/* PLAYER 0x01006, unpack(src, dst): the picture decompressor. src (far, [bp+4]) holds commands, dst (far,
 * [bp+8]) receives the picture. A command byte is a length: positive, that many literal bytes follow and
 * are copied; zero, a fill follows (a count, then the byte to repeat); 81h-FFh, skip (length - 80h) bytes of
 * the destination; 80h begins an escape word that follows: positive, skip that many bytes; zero, the end;
 * negative, a run of words: its high byte less 80h is a count of bytes - below 40h the bytes are copied from
 * the source, from 40h up the count (less 40h) is of a single byte repeated - done as words once the
 * destination (or the source, for a copy) is on an even address. Returns the end of the source as a
 * normalised far pointer, DX:AX; DS, ES, SI, DI, BP are restored. The loops check for room at each command.
 * The source and destination may overlap, so the copies run a byte (or word) at a time. */
#ifdef _MSC_VER
/* MSVC 19.51 /O2 stalls on this command loop even with STOS written out.
 * Keep the workaround local to PLAYER's picture decompressor. */
#pragma optimize("", off)
#endif
static int player_unpack(machine_t *m)
{
    cpu_t *c = &m->cpu;
    enum { CMD = 0x101F, COPY = 0x1028, AFTER_COPY = 0x102A, FILL = 0x1033, AFTER_FILL = 0x1039,
           SKIP = 0x1018, ESCAPE = 0x1048, WORDS = 0x1059, WORD_FILL = 0x106A, FINISH = 0x1081 };
    if (!room(c, 10)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->seg[S_DS]);
    cpu_push16(c, c->seg[S_ES]);
    c->r[R_SI] = bp_get(c, 4);                                    /* lds si, [bp+4] */
    c->seg[S_DS] = bp_get(c, 6);
    c->r[R_DI] = bp_get(c, 8);                                    /* les di, [bp+8] */
    c->seg[S_ES] = bp_get(c, 0x0A);
    set_r8(c, R_CH, (uint8_t)alu_logic(c, 0, 0));                 /* xor ch, ch */
    c->icount += 10;                                              /* ... and the jump to the first command */
#define SRC8() mem_read8(c, phys(c->seg[S_DS], c->r[R_SI]))
#define NEED(n_, at_) do { if (!room(c, (n_))) { c->ip = (at_); return 1; } } while (0)
    unsigned n;
    uint16_t ax;
    goto command;

command:                                                          /* 0x101F: the next command after a skip */
    NEED(5, CMD);
    set_r8(c, R_CL, SRC8());
    c->r[R_SI] = (uint16_t)alu_inc(c, c->r[R_SI], 1);
    if (c->r[R_CX] == 0) { c->icount += 3; goto fill; }           /* jcxz */
    alu_logic(c, get_r8(c, R_CL), 0);                             /* or cl, cl */
    c->icount += 5;
    if (x86_cond(c, 0xC)) goto skip;                              /* jl: a skip */
    goto copy;

after_copy:                                                       /* 0x102A */
    NEED(5, AFTER_COPY);
    set_r8(c, R_CL, SRC8());
    c->r[R_SI] = (uint16_t)alu_inc(c, c->r[R_SI], 1);
    alu_logic(c, get_r8(c, R_CL), 0);
    if (x86_cond(c, 0xC)) { c->icount += 4; goto skip; }
    c->icount += 5;
    if (x86_cond(c, 0xF)) goto copy;                              /* jg */
    goto fill;                                                    /* a zero length: a fill */

after_fill:                                                       /* 0x1039 */
    NEED(6, AFTER_FILL);
    set_r8(c, R_CL, SRC8());
    c->r[R_SI] = (uint16_t)alu_inc(c, c->r[R_SI], 1);
    if (c->r[R_CX] == 0) { c->icount += 3; goto fill; }
    alu_logic(c, get_r8(c, R_CL), 0);
    if (x86_cond(c, 0xC)) { c->icount += 5; goto skip; }
    c->icount += 6;                                               /* ... and the jump to the copy */
    goto copy;

copy:                                                             /* 0x1028: CX literal bytes */
    NEED(c->r[R_CX], COPY);
    c->icount += rep_string(c, STR_MOVS, 0, c->seg[S_DS], 0);
    goto after_copy;

fill:                                                             /* 0x1033: a count, then the byte to repeat */
    NEED(3 + (SRC8() ? SRC8() : 1u), FILL);
    set_r8(c, R_CL, SRC8());
    c->r[R_SI] = (uint16_t)alu_inc(c, c->r[R_SI], 1);
    x86_lods(c, 0, c->seg[S_DS]);
    c->icount += 3 + sm_rep_stos(c, 0);
    goto after_fill;

skip:                                                             /* 0x1018: a skip, or the escape */
    NEED(3, SKIP);
    set_r8(c, R_CL, (uint8_t)alu_sub(c, get_r8(c, R_CL), 0x80, 0, 0));
    if (c->flags & F_ZF) { c->icount += 2; goto escape; }
    c->r[R_DI] = (uint16_t)alu_add(c, c->r[R_DI], c->r[R_CX], 1, 0);
    c->icount += 3;
    goto command;

escape:                                                           /* 0x1048: the escape word */
    NEED(8, ESCAPE);
    x86_lods(c, 1, c->seg[S_DS]);
    ax = c->r[R_AX];
    alu_logic(c, ax, 1);                                          /* or ax, ax */
    if (x86_cond(c, 0xF)) {                                       /* jg: skip ax bytes */
        c->r[R_DI] = (uint16_t)alu_add(c, c->r[R_DI], ax, 1, 0);
        c->icount += 5;
        goto command;
    }
    if (c->flags & F_ZF) { c->icount += 4; goto finish; }         /* je: the end */
    c->r[R_CX] = ax;
    set_r8(c, R_CH, (uint8_t)alu_sub(c, get_r8(c, R_CH), 0x80, 0, 0));
    alu_sub(c, get_r8(c, R_CH), 0x40, 0, 0);
    c->icount += 8;
    if (x86_cond(c, 0xD)) goto word_fill;                         /* jge: a repeated byte */
    goto words;

words:                                                            /* 0x1059: copy CX bytes as words */
    {
        const int odd = c->r[R_SI] & 1;
        const unsigned k = (uint16_t)(odd ? c->r[R_CX] - 1 : c->r[R_CX]) >> 1;
        NEED(2 + (odd ? 2 : 0) + 1 + (k ? k : 1u) + 1 + 2, WORDS);
    }
    alu_logic(c, c->r[R_SI] & 1, 1);                              /* test si, 1 */
    n = 2;
    if (!(c->flags & F_ZF)) {                                     /* an odd source: one byte first */
        x86_movs(c, 0, c->seg[S_DS]);
        c->r[R_CX] = (uint16_t)alu_dec(c, c->r[R_CX], 1);
        n += 2;
    }
    c->r[R_CX] = x86_shift(c, 5, c->r[R_CX], 1, 1);               /* shr cx, 1 */
    n += 1 + rep_string(c, STR_MOVS, 1, c->seg[S_DS], 0) + 1;
    if (!(c->flags & F_CF)) { c->icount += n; goto command; }     /* jae: no odd byte left */
    x86_movs(c, 0, c->seg[S_DS]);
    c->icount += n + 2;                                           /* the last byte, and the jump */
    goto command;

word_fill:                                                        /* 0x106A: CX - 4000h bytes of one value */
    {
        const int odd = c->r[R_DI] & 1;
        const uint16_t count = (uint16_t)(c->r[R_CX] - 0x4000);
        const unsigned k = (uint16_t)(odd ? count - 1 : count) >> 1;
        NEED(5 + (odd ? 2 : 0) + 1 + (k ? k : 1u) + 1 + 2, WORD_FILL);
    }
    set_r8(c, R_CH, (uint8_t)alu_sub(c, get_r8(c, R_CH), 0x40, 0, 0));
    x86_lods(c, 0, c->seg[S_DS]);
    set_r8(c, R_AH, get_r8(c, R_AL));
    alu_logic(c, c->r[R_DI] & 1, 1);                              /* test di, 1 */
    n = 5;
    if (!(c->flags & F_ZF)) {                                     /* an odd destination: one byte first */
        mem_write8(c, phys(c->seg[S_ES], c->r[R_DI]), get_r8(c, R_AL));
        c->r[R_DI] = (uint16_t)(c->r[R_DI] + x86_str_delta(c, 0));
        c->r[R_CX] = (uint16_t)alu_dec(c, c->r[R_CX], 1);
        n += 2;
    }
    c->r[R_CX] = x86_shift(c, 5, c->r[R_CX], 1, 1);
    n += 1 + sm_rep_stos(c, 1) + 1;
    if (!(c->flags & F_CF)) { c->icount += n; goto command; }
    mem_write8(c, phys(c->seg[S_ES], c->r[R_DI]), get_r8(c, R_AL));
    c->r[R_DI] = (uint16_t)(c->r[R_DI] + x86_str_delta(c, 0));
    c->icount += n + 2;
    goto command;

finish:                                                           /* 0x1081: the end of the source, normalised */
    NEED(13, FINISH);
    c->r[R_AX] = c->r[R_SI];
    set_r8(c, R_CL, 4);
    ax = x86_shift(c, 5, c->r[R_AX], 4, 1);                       /* shr ax, cl */
    c->r[R_DX] = c->seg[S_DS];
    c->r[R_DX] = (uint16_t)alu_add(c, c->r[R_DX], ax, 1, 0);
    c->r[R_AX] = (uint16_t)alu_logic(c, c->r[R_SI] & 0x0F, 1);
    c->seg[S_ES] = cpu_pop16(c);
    c->seg[S_DS] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 13;
    near_ret(c);
    return 1;
#undef SRC8
#undef NEED
}

/* PLAYER 0x01790, DSWAP 0x0130C, SETUP 0x01B44 and MPS_LOGO 0x01EBE (a far copy), the C start-up's
 * environment copy: the environment segment (the word at 2Ch of the program prefix, whose segment is in the
 * data word `psp`) holds zero-ended strings ended by an empty one. Their count (SI) and the length of the
 * block (DI) are found with REPNE SCASB; the block is copied by the allocator `alloc` (size in AX, returned
 * in AX; it is called with CX 9) into one allocation and the array of string pointers into another, which
 * is kept in the word `envp`. Strings that begin with the 12-byte marker (the word first, then six words
 * compared) are copied but get no pointer; the array ends in a zero. DS and ES change places for the copy
 * and DS is restored at the end.
 *
 * The stretches with a data-dependent length each check for room first and leave the machine at their
 * first instruction (the REP SCASB, a string's start, a character) so the original can carry on from
 * there. Nothing is read ahead of the pushes: the scan reads memory as the original would find it. */
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
typedef struct { uint16_t entry, psp, alloc, envp, marker; int far; } sm_envp;

static int sm_setenvp(machine_t *m, const sm_envp *s)
{
    cpu_t *c = &m->cpu;
    const uint16_t e = s->entry;
    const uint16_t scan_ip = (uint16_t)(e + 0x24), grow_ip = (uint16_t)(e + 0x2A);
    const uint16_t first_ret = (uint16_t)(e + 0x3A), second_ret = (uint16_t)(e + 0x40);
    const uint16_t string_ip = (uint16_t)(e + 0x51), copy_ip = (uint16_t)(e + 0x6F);
    const uint16_t next_ip = (uint16_t)(e + 0x75), end_ip = (uint16_t)(e + 0x77);
    if (!room(c, 14)) return 0;                                   /* the longest way to the scan */

    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->seg[S_DS]);
    c->seg[S_ES] = ds_get(c, s->psp);                             /* the program prefix */
    const uint16_t env = seg_read16(c, c->seg[S_ES], 0x2C);       /* its environment segment */
    c->r[R_BX] = env;
    c->seg[S_ES] = env;
    c->r[R_AX] = (uint16_t)alu_logic(c, 0, 1);                    /* xor ax, ax */
    c->r[R_SI] = (uint16_t)alu_logic(c, 0, 1);
    c->r[R_DI] = (uint16_t)alu_logic(c, 0, 1);
    c->r[R_CX] = 0xFFFF;
    alu_logic(c, env, 1);                                         /* or bx, bx */
    unsigned n = 12;
    int scanning = 0;
    if (env != 0) {                                               /* a first string to look at */
        alu_sub(c, mem_read8(c, phys(env, 0)), 0, 0, 0);          /* cmp byte es:[0], 0 */
        n += 2;
        scanning = !(c->flags & F_ZF);
    }
    c->icount += n;
    /* The scan: each pass finds a string's end (REPNE SCASB, CX counting down across passes),
     * counts it (SI) and looks at the byte after: another NUL ends the list. */
    while (scanning) {
        const int step = x86_str_delta(c, 0);
        unsigned k = 1;                                           /* a REP with CX 0 takes one clock */
        if (c->r[R_CX] != 0) {
            k = 0;
            uint16_t d = c->r[R_DI];
            for (unsigned left = c->r[R_CX]; left; left--) {
                k++;
                const uint8_t b = mem_read8(c, phys(c->seg[S_ES], d));
                d = (uint16_t)(d + step);
                if (b == 0) break;                                /* AL is 0 */
            }
        }
        if (!room(c, k + 3)) { c->ip = scan_ip; return 1; }
        rep_string(c, STR_SCAS, 0, 0, 1);                         /* repne scasb */
        c->r[R_SI] = (uint16_t)alu_inc(c, c->r[R_SI], 1);
        x86_scas(c, 0);                                           /* scasb: the byte after the NUL */
        c->icount += k + 3;                                       /* the scan, inc, scasb, jne */
        scanning = !(c->flags & F_ZF);
    }
    if (!room(c, 8)) { c->ip = grow_ip; return 1; }
    c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_DI], 1);             /* the block, rounded down to even */
    set_r8(c, R_AL, (uint8_t)alu_logic(c, get_r8(c, R_AL) & 0xFE, 0));
    c->r[R_SI] = (uint16_t)alu_inc(c, c->r[R_SI], 1);
    c->r[R_DI] = c->r[R_SI];
    c->r[R_SI] = x86_shift(c, 4, c->r[R_SI], 1, 1);               /* the pointers: two bytes each */
    c->r[R_CX] = 9;
    c->icount += 7;
    if (!guest_call(m, s->alloc, first_ret)) return 1;
    if (!room(c, 3)) { c->ip = first_ret; return 1; }
    cpu_push16(c, c->r[R_AX]);                                    /* the string block */
    c->r[R_AX] = c->r[R_SI];
    c->icount += 2;
    if (!guest_call(m, s->alloc, second_ret)) return 1;
    if (!room(c, 11)) { c->ip = second_ret; return 1; }
    ds_put(c, s->envp, c->r[R_AX]);                               /* the pointer array */
    {
        const uint16_t es = c->seg[S_ES], ds = c->seg[S_DS];      /* push es, push ds, pop es, pop ds */
        cpu_push16(c, es);
        cpu_push16(c, ds);
        c->seg[S_ES] = cpu_pop16(c);
        c->seg[S_DS] = cpu_pop16(c);
    }
    c->r[R_CX] = c->r[R_DI];
    c->r[R_BX] = c->r[R_AX];
    c->r[R_SI] = (uint16_t)alu_logic(c, 0, 1);
    c->r[R_DI] = cpu_pop16(c);                                    /* the string block */
    c->r[R_CX] = (uint16_t)alu_dec(c, c->r[R_CX], 1);
    c->icount += 11;
    if (c->r[R_CX] != 0) {                                        /* jcxz not taken: copy the strings */
        for (;;) {
            if (!room(c, 3)) { c->ip = string_ip; return 1; }
            const uint16_t word = seg_read16(c, c->seg[S_DS], c->r[R_SI]);
            c->r[R_AX] = word;
            alu_sub(c, word, seg_read16(c, c->seg[S_SS], s->marker), 1, 0);
            c->icount += 3;
            int keep = 1;                                         /* the string gets a pointer */
            if (c->flags & F_ZF) {                                /* might be the marker: compare all of it */
                if (!room(c, 15)) { c->ip = (uint16_t)(e + 0x5A); return 1; }
                cpu_push16(c, c->r[R_CX]);
                cpu_push16(c, c->r[R_SI]);
                cpu_push16(c, c->r[R_DI]);
                c->r[R_DI] = s->marker;
                c->r[R_CX] = 6;
                unsigned k = 0;
                for (;;) {                                        /* repe cmpsw */
                    x86_cmps(c, 1, c->seg[S_DS]);
                    k++;
                    c->r[R_CX] = (uint16_t)(c->r[R_CX] - 1);
                    if (c->r[R_CX] == 0 || !(c->flags & F_ZF)) break;
                }
                c->r[R_DI] = cpu_pop16(c);
                c->r[R_SI] = cpu_pop16(c);
                c->r[R_CX] = cpu_pop16(c);
                c->icount += 5 + k + 3 + 1;                       /* pushes, movs, the scan, pops, je */
                keep = !(c->flags & F_ZF);
            }
            if (keep) {
                if (!room(c, 3)) { c->ip = (uint16_t)(e + 0x6A); return 1; }
                seg_write16(c, c->seg[S_ES], c->r[R_BX], c->r[R_DI]);   /* the pointer */
                c->r[R_BX] = (uint16_t)alu_inc(c, c->r[R_BX], 1);
                c->r[R_BX] = (uint16_t)alu_inc(c, c->r[R_BX], 1);
                c->icount += 3;
            }
            for (;;) {                                            /* copy the string, its NUL last */
                if (!room(c, 4)) { c->ip = copy_ip; return 1; }
                x86_lods(c, 0, c->seg[S_DS]);
                mem_write8(c, phys(c->seg[S_ES], c->r[R_DI]), get_r8(c, R_AL));   /* stosb, by hand (x86_stos stalls MSVC) */
                c->r[R_DI] = (uint16_t)(c->r[R_DI] + x86_str_delta(c, 0));
                alu_logic(c, get_r8(c, R_AL), 0);                 /* or al, al */
                c->icount += 4;
                if (c->flags & F_ZF) break;
            }
            if (!room(c, 1)) { c->ip = next_ip; return 1; }
            c->r[R_CX] = (uint16_t)(c->r[R_CX] - 1);              /* loop */
            c->icount += 1;
            if (c->r[R_CX] == 0) break;
        }
    }
    if (!room(c, 4)) { c->ip = end_ip; return 1; }
    seg_write16(c, c->seg[S_ES], c->r[R_BX], c->r[R_CX]);         /* the array's end */
    c->seg[S_DS] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 4;
    if (s->far) far_ret(c); else near_ret(c);
    return 1;
}
#define SM_SETENVP(P, E, PSP, ALLOC, ENVP, MARKER, FAR) \
    static const sm_envp P##_ENVP = { E, PSP, ALLOC, ENVP, MARKER, FAR }; \
    static int P##_setenvp(machine_t *m) { return sm_setenvp(m, &P##_ENVP); }
SM_SETENVP(player, 0x1790, 0x1A60, 0x1FE8, 0x1A81, 0x1A3E, 0)
SM_SETENVP(dswap, 0x130C, 0x26C8, 0x1C3C, 0x26E9, 0x26A6, 0)
SM_SETENVP(setup, 0x1B44, 0x0E84, 0x1C22, 0x0EA5, 0x0E62, 0)
SM_SETENVP(mps_logo, 0x0A5E, 0x01FE, 0x17B0, 0x021F, 0x01DC, 1)

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

/* VGAME 0x0E9F4 and DSWAP 0x00F06, open_stream(a, b, c): a stream slot from the finder
 * `find` (whose call returns to find_ret); with none, 0; otherwise `open` (a, b, c,
 * slot), returning to open_ret. SI restored. */
static int open_stream(machine_t *m, uint16_t find, uint16_t find_ret, uint16_t open, uint16_t open_ret)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 4)) return 0;
    const uint16_t a = arg(c, 0), b = arg(c, 1), d = arg(c, 2);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    c->icount += 3;
    if (!guest_call(m, find, find_ret)) return 1;
    if (!room(c, 9)) { c->ip = find_ret; return 1; }
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
        if (!guest_call(m, open, open_ret)) return 1;
        if (!room(c, 5)) { c->ip = open_ret; return 1; }
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
static int vgame_open_stream(machine_t *m) { return open_stream(m, 0xF38A, 0xE9FB, 0xF1E0, 0xEA13); }
static int dswap_open_stream(machine_t *m) { return open_stream(m, 0x1B9C, 0x0F0D, 0x1520, 0x0F25); }

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

/* ---- VGAME, second batch ------------------------------------------------ */

/* |v| as `cwd; xor ax, dx; sub ax, dx` computes it (8000h stays 8000h). */
static uint16_t abs_word(uint16_t v) { const uint16_t s = (v & 0x8000) ? 0xFFFF : 0; return (uint16_t)((v ^ s) - s); }
static uint16_t sign_word(uint16_t v) { return (v & 0x8000) ? 0xFFFF : 0; }
static uint16_t sar_word(uint16_t v, unsigned n) { return (uint16_t)((int16_t)v >> n); }

/* The cull half of camera_transform (below), from 0x0E435 with DI the z
 * high word: 1 when the point is kept. Counts its instructions into *n. */
static int camera_cull(cpu_t *c, uint16_t di, unsigned *n)
{
    const uint16_t ds = c->seg[S_DS], ss = c->seg[S_SS];
    if ((int16_t)di > (int16_t)ds_get(c, 0x48E6)) { *n += 3; return 0; }   /* cmp, jg, jmp */
    const uint16_t lod2 = (uint16_t)(ds_get(c, 0x49AA) << 1);
    c->r[R_BX] = lod2;
    if ((int16_t)di < (int16_t)ds_get(c, (uint16_t)(lod2 + 0x48F6))) { *n += 7; return 0; }
    *n += 8;
    uint16_t si, ax;
    if (ds_get(c, 0x438E) != 0) {                                 /* the second frustum */
        const uint16_t t_across = (uint16_t)(0x4966 + lod2), t_up = (uint16_t)(0x4976 + lod2);
        c->r[R_BP] = t_up;
        const uint16_t k = sar_word((uint16_t)(abs_word(di) + ds_get(c, t_across)), 3);
        const uint16_t xh = ds_get(c, 0x498A);
        c->r[R_DX] = sign_word(xh);
        ax = abs_word(xh);
        uint16_t b = sar_word(k, 3);
        si = (uint16_t)(k + b);
        b = sar_word(b, 1);
        si = (uint16_t)(si + b);
        c->r[R_BX] = b;
        c->r[R_AX] = ax;
        *n += 26;
        if ((int16_t)si < (int16_t)ax) { *n += 1; return 0; }     /* jl, jmp */
        const uint16_t v = sar_word((uint16_t)(abs_word(di) + seg_read16(c, ss, t_up)), 2);
        const uint16_t yh = ds_get(c, 0x498E);
        c->r[R_DX] = sign_word(yh);
        ax = abs_word(yh);
        c->r[R_AX] = ax;
        b = sar_word(v, 3);
        c->r[R_BX] = b;
        si = (uint16_t)(v + b);
        *n += 19;
        if (!((int16_t)si > (int16_t)ax)) { *n += 1; return 0; }  /* jg not taken, jmp */
        const uint16_t yh2 = ds_get(c, 0x498E);                   /* |y| read again */
        c->r[R_DX] = sign_word(yh2);
        ax = abs_word(yh2);
        *n += 5;
    } else {                                                      /* the cockpit frustum */
        const uint16_t t_across = (uint16_t)(ds_get(c, 0xE56A) + lod2), t_up = (uint16_t)(ds_get(c, 0xE570) + lod2);
        c->r[R_BP] = t_up;
        const uint8_t cl = (uint8_t)(mem_read8(c, phys(ds, 0x294B)) ^ 1);
        set_r8(c, R_CL, cl);
        di = abs_word(di);
        c->r[R_DI] = di;
        ax = x86_shift(c, 7, (uint16_t)(di + ds_get(c, t_across)), cl, 1);
        si = (uint16_t)(ax + sar_word(ax, 2));
        const uint16_t xh = ds_get(c, 0x498A);
        c->r[R_DX] = sign_word(xh);
        ax = abs_word(xh);
        c->r[R_AX] = ax;
        *n += 23;
        if ((int16_t)ax > (int16_t)si) return 0;
        si = ax;
        ax = x86_shift(c, 7, (uint16_t)(di + seg_read16(c, ss, t_up)), cl, 1);
        uint16_t up = ax;
        *n += 7;
        if (mem_read8(c, phys(ds, 0x368C)) != 0) { up = sar_word((uint16_t)(sar_word(up, 3) + ax), 1); *n += 5; }
        c->r[R_BX] = up;
        const uint16_t yh = ds_get(c, 0x498E);
        c->r[R_DX] = sign_word(yh);
        ax = abs_word(yh);
        c->r[R_AX] = ax;
        *n += 6;
        if ((int16_t)ax > (int16_t)up) return 0;
    }
    si = (uint16_t)(sar_word((uint16_t)(si + ax), 2) + di);      /* 0x0E513: the range metric */
    ds_put(c, 0x49A8, si);
    *n += 7;
    return !((int16_t)si > (int16_t)ds_get(c, 0x48E6));
}

/* VGAME 0x0E3BF, camera_transform: the world offset (BP, BX, CX) into
 * camera space, then the cull. Each camera coordinate is a column of the
 * matrix at 49BE (words; x reads 49CA/49C4/49BE with BX/CX/BP) times the
 * offset, summed and doubled in 32 bits, stored at 4988 (x), 498C (y) and
 * 4990 (z). The point is culled (AX = 1, flags of `or ax, ax`) when the z
 * high word is beyond [48E6], nearer than the detail level's limit
 * [48F6 + 2*[49AA]], or outside the frustum. With [438E] clear the cockpit
 * frustum: half-widths (|z| + table) >> (1 - [294B]), the tables' bases
 * at [E56A] / [E570], the horizontal one times 1.25, the vertical one
 * times 9/16 when [368C] is set; with it set the second frustum's tables
 * at 4966 / 4976 (k = (|z| + t) >> 3, k + k/8 + k/16 across, and
 * v + v/8 with v = (|z| + t) >> 2 up). A point inside gets the range
 * metric (si + |y|) / 4 + di at [49A8] (si and di as each branch leaves
 * them: |x| and |z| in the cockpit, the vertical limit and the signed z in
 * the second frustum) and is kept (AX = 0, flags of `sub ax, ax`) unless
 * the metric is beyond [48E6]. SI is preserved; the second frustum's
 * table entry is read through BP, so from SS. */
static int vgame_camera_transform(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 118)) return 0;
    cpu_push16(c, c->r[R_SI]);
    const uint16_t bx = c->r[R_BX], cx = c->r[R_CX], bp = c->r[R_BP];
    static const uint16_t out_at[3] = { 0x4988, 0x498C, 0x4990 };
    uint32_t acc = 0;
    for (int i = 0; i < 3; i++) {
        acc = (uint32_t)((int32_t)(int16_t)ds_get(c, (uint16_t)(0x49CA + 2 * i)) * (int16_t)bx);
        acc += (uint32_t)((int32_t)(int16_t)ds_get(c, (uint16_t)(0x49C4 + 2 * i)) * (int16_t)cx);
        const uint32_t last = (uint32_t)((int32_t)(int16_t)ds_get(c, (uint16_t)(0x49BE + 2 * i)) * (int16_t)bp);
        acc = (acc + last) << 1;
        c->r[R_AX] = (uint16_t)last;
        c->r[R_DX] = (uint16_t)(last >> 16);
        ds_put(c, out_at[i], (uint16_t)acc);
        ds_put(c, (uint16_t)(out_at[i] + 2), (uint16_t)(acc >> 16));
    }
    c->r[R_DI] = (uint16_t)(acc >> 16);                           /* the z high word */
    unsigned n = 49;
    const int keep = camera_cull(c, c->r[R_DI], &n);
    c->r[R_SI] = cpu_pop16(c);
    if (keep) c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    else c->r[R_AX] = (uint16_t)alu_logic(c, 1, 1);               /* mov ax, 1; or ax, ax */
    c->icount += n + (keep ? 3 : 4);
    near_ret(c);
    return 1;
}

/* VGAME 114A:04B4 (0x11954), mat3_mul(a, b, out), far: out = a x b for 3x3
 * matrices of 1.15 words, each product through the far fixed-point
 * multiply at 104E:0000, each element the sum of its three products in 16
 * bits. Elements go row-major and each is stored before the next is begun,
 * reading a, b and out again from the frame each time, so an output that
 * overlaps an input comes out as the original's. Within an element the
 * products are made in the order k = 1, 0, 2 in the first column and 0,
 * 2, 1 in the others, summed first + (second + third). The first element
 * keeps a in SI and its running sums in DI, SI; the rest keep b in SI and
 * a in DI. Flags from the last element's final add. */
static int vgame_mat3_mul(machine_t *m)
{
    cpu_t *c = &m->cpu;
    static const uint16_t call_at[27] = {
        0x04C5, 0x04D5, 0x04E7, 0x0502, 0x0511, 0x0523, 0x053F, 0x054E, 0x0560,
        0x057D, 0x058B, 0x059D, 0x05BA, 0x05C9, 0x05DB, 0x05F8, 0x0607, 0x0619,
        0x0636, 0x0644, 0x0656, 0x0673, 0x0682, 0x0694, 0x06B1, 0x06C0, 0x06D2 };
    static const int order[2][3] = { { 1, 0, 2 }, { 0, 2, 1 } };
#define FRAME(o) seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + (o)))
#define WORD(p, o) seg_read16(c, c->seg[S_DS], (uint16_t)((p) + (o)))
    if (!room(c, 4 + 4 + 1)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    c->icount += 4;
    for (int e = 0; e < 9; e++) {
        const int i = e / 3, j = e % 3;
        const int *k = order[j != 0];
#define B_AT(n) (uint16_t)(2 * (3 * k[n] + j))
#define A_AT(n) (uint16_t)(2 * (3 * i + k[n]))
        const uint16_t *at = &call_at[3 * e];
        /* the first product */
        if (e == 0) {
            c->r[R_BX] = FRAME(8);
            cpu_push16(c, WORD(c->r[R_BX], B_AT(0)));
            c->r[R_SI] = FRAME(6);
            cpu_push16(c, WORD(c->r[R_SI], A_AT(0)));
        } else {
            c->r[R_SI] = FRAME(8);
            cpu_push16(c, WORD(c->r[R_SI], B_AT(0)));
            c->r[R_DI] = FRAME(6);
            cpu_push16(c, WORD(c->r[R_DI], A_AT(0)));
        }
        c->icount += 4;
        if (!guest_call_far(m, at[0], (uint16_t)(at[0] + 5))) return 1;
        /* the second */
        if (!room(c, (e == 0 ? 6 : 5) + 1)) { c->ip = (uint16_t)(at[0] + 5); return 1; }
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_BX] = cpu_pop16(c);
        if (e == 0) {
            c->r[R_BX] = FRAME(8);
            cpu_push16(c, WORD(c->r[R_BX], B_AT(1)));
            cpu_push16(c, WORD(c->r[R_SI], A_AT(1)));
            c->r[R_DI] = c->r[R_AX];
            c->icount += 6;
        } else {
            cpu_push16(c, WORD(c->r[R_SI], B_AT(1)));
            cpu_push16(c, WORD(c->r[R_DI], A_AT(1)));
            c->r[R_SI] = c->r[R_AX];
            c->icount += 5;
        }
        if (!guest_call_far(m, at[1], (uint16_t)(at[1] + 5))) return 1;
        /* the third */
        if (!room(c, 6 + 1)) { c->ip = (uint16_t)(at[1] + 5); return 1; }
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_BX] = FRAME(8);
        cpu_push16(c, WORD(c->r[R_BX], B_AT(2)));
        if (e == 0) { cpu_push16(c, WORD(c->r[R_SI], A_AT(2))); c->r[R_SI] = c->r[R_AX]; }
        else { cpu_push16(c, WORD(c->r[R_DI], A_AT(2))); c->r[R_DI] = c->r[R_AX]; }
        c->icount += 6;
        if (!guest_call_far(m, at[2], (uint16_t)(at[2] + 5))) return 1;
        /* the sum, stored */
        if (!room(c, 6 + (e < 8 ? 4 + 1 : 4))) { c->ip = (uint16_t)(at[2] + 5); return 1; }
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_BX] = cpu_pop16(c);
        uint16_t sum;
        if (e == 0) {
            c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], c->r[R_AX], 1, 0);
            sum = c->r[R_DI] = (uint16_t)alu_add(c, c->r[R_DI], c->r[R_SI], 1, 0);
        } else {
            c->r[R_DI] = (uint16_t)alu_add(c, c->r[R_DI], c->r[R_AX], 1, 0);
            sum = c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], c->r[R_DI], 1, 0);
        }
        c->r[R_BX] = FRAME(0x0A);
        seg_write16(c, c->seg[S_DS], (uint16_t)(c->r[R_BX] + 2 * e), sum);
        c->icount += 6;
#undef B_AT
#undef A_AT
    }
#undef FRAME
#undef WORD
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 4;
    far_ret(c);
    return 1;
}

/* A far call from the routine at CS:ip_ (9A ...), then room for the next_
 * instructions that follow it (counting the next CALL); otherwise IP is
 * left after the call for the original to carry on. */
#define FAR_THEN(ip_, next_) do {                                                     \
        if (!guest_call_far(m, (ip_), (uint16_t)((ip_) + 5))) return 1;               \
        if (!room(c, (next_))) { c->ip = (uint16_t)((ip_) + 5); return 1; }           \
    } while (0)

/* VGAME 0x0DFA9, matrix_build(out, a, b, g): the 3x3 rotation for three
 * angles, words in 1.15. The sines and cosines are kept at 49B2.. as
 * sa, ca, sb, cb, sg, cg (far sine 104E:0076 and cosine 104E:0066), then,
 * with x*y the far fixed-point multiply 104E:0000:
 *   out[0] = sa*(sb*sg) + ca*cg     out[1] = sa*(sb*cg) - ca*sg     out[2] = sa*cb
 *   out[3] = cb*sg                  out[4] = cb*cg                  out[5] = -sb
 *   out[6] = ca*(sb*sg) - sa*cg     out[7] = ca*(sb*cg) + sa*sg     out[8] = ca*cb
 * each product's operands pushed in the original's order and each element
 * stored as soon as it is made. Flags from the last multiply. */
static int vgame_matrix_build(machine_t *m)
{
    cpu_t *c = &m->cpu;
    enum { SA, CA, SB, CB, SG, CG };
#define TRIG(n) ds_get(c, (uint16_t)(0x49B2 + 2 * (n)))
#define FRAME(o) seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + (o)))
#define POP2() do { c->r[R_BX] = cpu_pop16(c); c->r[R_BX] = cpu_pop16(c); } while (0)
#define PUSH2(x, y) do { cpu_push16(c, TRIG(x)); cpu_push16(c, TRIG(y)); } while (0)
#define STORE(i, v) do { c->r[R_BX] = FRAME(4); seg_write16(c, c->seg[S_DS], (uint16_t)(c->r[R_BX] + 2 * (i)), (v)); } while (0)
    if (!room(c, 5)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, FRAME(6));
    c->icount += 4;
    /* sine then cosine of each angle */
    static const uint16_t trig_call[6] = { 0xDFB0, 0xDFBC, 0xDFC8, 0xDFD4, 0xDFE0, 0xDFEC };
    for (int n = 0; n < 6; n++) {
        FAR_THEN(trig_call[n], (n < 5 ? 3 : 4) + 1);
        c->r[R_BX] = cpu_pop16(c);
        ds_put(c, (uint16_t)(0x49B2 + 2 * n), c->r[R_AX]);
        if (n < 5) { cpu_push16(c, FRAME(6 + 2 * ((n + 1) / 2))); c->icount += 3; }
    }
    PUSH2(SG, SB);
    c->icount += 4;
    /* out[0] */
    FAR_THEN(0xDFFD, 4 + 1);
    POP2(); cpu_push16(c, c->r[R_AX]); cpu_push16(c, TRIG(SA));
    c->icount += 4;
    FAR_THEN(0xE009, 5 + 1);
    POP2(); PUSH2(CG, CA); c->r[R_SI] = c->r[R_AX];
    c->icount += 5;
    FAR_THEN(0xE01A, 7 + 1);
    POP2();
    c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], c->r[R_AX], 1, 0);
    STORE(0, c->r[R_SI]);
    PUSH2(SG, CA);
    c->icount += 7;
    /* out[1] */
    FAR_THEN(0xE030, 5 + 1);
    POP2(); PUSH2(CG, SB); c->r[R_SI] = c->r[R_AX];
    c->icount += 5;
    FAR_THEN(0xE041, 4 + 1);
    POP2(); cpu_push16(c, c->r[R_AX]); cpu_push16(c, TRIG(SA));
    c->icount += 4;
    FAR_THEN(0xE04D, 7 + 1);
    POP2();
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_SI], 1, 0);
    STORE(1, c->r[R_AX]);
    PUSH2(CB, SA);
    c->icount += 7;
    /* out[2], out[3], out[4]: single products */
    FAR_THEN(0xE064, 6 + 1);
    POP2(); STORE(2, c->r[R_AX]); PUSH2(CB, SG);
    c->icount += 6;
    FAR_THEN(0xE079, 6 + 1);
    POP2(); STORE(3, c->r[R_AX]); PUSH2(CB, CG);
    c->icount += 6;
    FAR_THEN(0xE08E, 9 + 1);
    POP2(); STORE(4, c->r[R_AX]);
    /* out[5] = -sb, through the same BX */
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, TRIG(SB), 1, 0);         /* neg ax */
    seg_write16(c, c->seg[S_DS], (uint16_t)(c->r[R_BX] + 0x0A), c->r[R_AX]);
    PUSH2(CG, SA);
    c->icount += 9;
    /* out[6] */
    FAR_THEN(0xE0AB, 5 + 1);
    POP2(); PUSH2(SG, SB); c->r[R_SI] = c->r[R_AX];
    c->icount += 5;
    FAR_THEN(0xE0BC, 4 + 1);
    POP2(); cpu_push16(c, c->r[R_AX]); cpu_push16(c, TRIG(CA));
    c->icount += 4;
    FAR_THEN(0xE0C8, 7 + 1);
    POP2();
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_SI], 1, 0);
    STORE(6, c->r[R_AX]);
    PUSH2(CG, SB);
    c->icount += 7;
    /* out[7] */
    FAR_THEN(0xE0DF, 4 + 1);
    POP2(); cpu_push16(c, c->r[R_AX]); cpu_push16(c, TRIG(CA));
    c->icount += 4;
    FAR_THEN(0xE0EB, 5 + 1);
    POP2(); PUSH2(SA, SG); c->r[R_SI] = c->r[R_AX];
    c->icount += 5;
    FAR_THEN(0xE0FC, 7 + 1);
    POP2();
    c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], c->r[R_AX], 1, 0);
    STORE(7, c->r[R_SI]);
    PUSH2(CB, CA);
    c->icount += 7;
    /* out[8] */
    FAR_THEN(0xE113, 7);
    POP2(); STORE(8, c->r[R_AX]);
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 7;
    near_ret(c);
    return 1;
#undef TRIG
#undef FRAME
#undef POP2
#undef PUSH2
#undef STORE
}

/* VGAME 0x0E123, orientation_build: the orientation matrix at 49D0 (3x3
 * words, 1.15) from the angles at [499A], [499C], [499E] - the same sines
 * and cosines as matrix_build (sa, ca, sb, cb, sg, cg at 49B2..) in a
 * different arrangement, with x*y the far fixed-point multiply:
 *   [49D0] = ca*cg - sa*(sb*sg)    [49D2] = -(cb*sg)    [49D4] = ca*(sg*sb) + sa*cg
 *   [49D6] = sa*(sb*cg) + ca*sg    [49D8] = cb*cg       [49DA] = sa*sg - ca*(sb*cg)
 *   [49DC] = -(sa*cb)              [49DE] = sb          [49E0] = cb*ca
 * made in the order 49D0, 49D6, 49DC, 49D2, 49D8, 49DE, 49D4, 49DA, 49E0.
 * SI preserved; flags from the last multiply. */
static int vgame_orientation_build(machine_t *m)
{
    cpu_t *c = &m->cpu;
    enum { SA, CA, SB, CB, SG, CG };
#define TRIG(n) ds_get(c, (uint16_t)(0x49B2 + 2 * (n)))
#define POP2() do { c->r[R_BX] = cpu_pop16(c); c->r[R_BX] = cpu_pop16(c); } while (0)
#define PUSH2(x, y) do { cpu_push16(c, TRIG(x)); cpu_push16(c, TRIG(y)); } while (0)
    if (!room(c, 3)) return 0;
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, ds_get(c, 0x499A));
    c->icount += 2;
    static const uint16_t trig_call[6] = { 0xE128, 0xE135, 0xE142, 0xE14F, 0xE15C, 0xE169 };
    for (int n = 0; n < 6; n++) {
        FAR_THEN(trig_call[n], (n < 5 ? 3 : 4) + 1);
        c->r[R_BX] = cpu_pop16(c);
        ds_put(c, (uint16_t)(0x49B2 + 2 * n), c->r[R_AX]);
        if (n < 5) { cpu_push16(c, ds_get(c, (uint16_t)(0x499A + 2 * ((n + 1) / 2)))); c->icount += 3; }
    }
    PUSH2(SG, SB);
    c->icount += 4;
    /* [49D0] */
    FAR_THEN(0xE17A, 4 + 1);
    POP2(); cpu_push16(c, c->r[R_AX]); cpu_push16(c, TRIG(SA));
    c->icount += 4;
    FAR_THEN(0xE186, 5 + 1);
    POP2(); PUSH2(CG, CA); c->r[R_SI] = c->r[R_AX];
    c->icount += 5;
    FAR_THEN(0xE197, 6 + 1);
    POP2();
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_SI], 1, 0);
    ds_put(c, 0x49D0, c->r[R_AX]);
    PUSH2(CG, SB);
    c->icount += 6;
    /* [49D6] */
    FAR_THEN(0xE1AB, 4 + 1);
    POP2(); cpu_push16(c, c->r[R_AX]); cpu_push16(c, TRIG(SA));
    c->icount += 4;
    FAR_THEN(0xE1B7, 5 + 1);
    POP2(); PUSH2(SG, CA); c->r[R_SI] = c->r[R_AX];
    c->icount += 5;
    FAR_THEN(0xE1C8, 6 + 1);
    POP2();
    c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], c->r[R_AX], 1, 0);
    ds_put(c, 0x49D6, c->r[R_SI]);
    PUSH2(CB, SA);
    c->icount += 6;
    /* [49DC], [49D2]: negated single products; [49D8] */
    FAR_THEN(0xE1DD, 6 + 1);
    POP2();
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);       /* neg ax */
    ds_put(c, 0x49DC, c->r[R_AX]);
    PUSH2(CB, SG);
    c->icount += 6;
    FAR_THEN(0xE1F1, 6 + 1);
    POP2();
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);
    ds_put(c, 0x49D2, c->r[R_AX]);
    PUSH2(CB, CG);
    c->icount += 6;
    FAR_THEN(0xE205, 7 + 1);
    POP2();
    ds_put(c, 0x49D8, c->r[R_AX]);
    /* [49DE] = sb, which also starts [49D4]'s product */
    c->r[R_AX] = TRIG(SB);
    ds_put(c, 0x49DE, c->r[R_AX]);
    cpu_push16(c, TRIG(SG));
    cpu_push16(c, c->r[R_AX]);
    c->icount += 7;
    /* [49D4] */
    FAR_THEN(0xE21A, 4 + 1);
    POP2(); cpu_push16(c, c->r[R_AX]); cpu_push16(c, TRIG(CA));
    c->icount += 4;
    FAR_THEN(0xE226, 5 + 1);
    POP2(); PUSH2(CG, SA); c->r[R_SI] = c->r[R_AX];
    c->icount += 5;
    FAR_THEN(0xE237, 6 + 1);
    POP2();
    c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], c->r[R_AX], 1, 0);
    ds_put(c, 0x49D4, c->r[R_SI]);
    PUSH2(CG, SB);
    c->icount += 6;
    /* [49DA] */
    FAR_THEN(0xE24C, 4 + 1);
    POP2(); cpu_push16(c, c->r[R_AX]); cpu_push16(c, TRIG(CA));
    c->icount += 4;
    FAR_THEN(0xE258, 5 + 1);
    POP2(); PUSH2(SA, SG); c->r[R_SI] = c->r[R_AX];
    c->icount += 5;
    FAR_THEN(0xE269, 6 + 1);
    POP2();
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_SI], 1, 0);
    ds_put(c, 0x49DA, c->r[R_AX]);
    PUSH2(CB, CA);
    c->icount += 6;
    /* [49E0] */
    FAR_THEN(0xE27D, 5);
    POP2();
    ds_put(c, 0x49E0, c->r[R_AX]);
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 5;
    near_ret(c);
    return 1;
#undef TRIG
#undef POP2
#undef PUSH2
}

/* A near call to target_ returning to ret_ (the callee's RET n removing
 * pops_ bytes), then room for the next_ instructions after it. */
#define NEAR_THEN(target_, ret_, pops_, next_) do {                                   \
        if (!guest_call_pop(m, (target_), (ret_), (pops_))) return 1;                \
        if (!room(c, (next_))) { c->ip = (ret_); return 1; }                          \
    } while (0)
/* The routine's BP frame, read and written where the original does. */
#define FRAME(o) seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + (o)))
#define SETFRAME(o, v) seg_write16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + (o)), (v))

/* VGAME 0x0C702, bearing(x, y): the angle of (x, y) as a word, a full turn
 * 10000h, 0 along +y and 4000h along +x. The axes are answered at once
 * (y = 0: 4000h or C000h by the sign of x; x = 0: 0 or 8000h by y). Off
 * them the ratio t = (min << 14) / max of |x| and |y| (the runtime's shift
 * 0x0EF68 and divide 0x0EE9C; [bp-0Ch] says which was larger) gives the
 * arctangent in 1.14 as t * (2800h - ((|1333h - t| * 0B00h) >> 14)) >> 14
 * (the multiply 0x0EF36 and shift 0x0EF74), and the quadrant places it.
 * Flags: the last compare, or the quadrant's add or subtract. */
static int vgame_bearing(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 12)) return 0;
    const uint16_t x = arg(c, 0), y = arg(c, 1);
    x86_enter(c, 0x0E, 0);
    cpu_push16(c, c->r[R_SI]);
    alu_sub(c, x, 0, 1, 0);
    if (c->flags & F_ZF) {                                        /* on the y axis */
        alu_sub(c, y, 0, 1, 0);
        if (x86_cond(c, 0xE)) c->r[R_AX] = 0x8000;
        else c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
        c->r[R_SI] = cpu_pop16(c);
        x86_leave(c);
        c->icount += 10;
        near_ret(c);
        return 1;
    }
    alu_sub(c, y, 0, 1, 0);
    if (c->flags & F_ZF) {                                        /* on the x axis */
        alu_sub(c, x, 0, 1, 0);
        c->r[R_AX] = x86_cond(c, 0xE) ? 0xC000 : 0x4000;
        c->r[R_SI] = cpu_pop16(c);
        x86_leave(c);
        c->icount += 12;
        near_ret(c);
        return 1;
    }
    /* |y| into SI, |x| into AX: which is wider */
    cpu_push16(c, y);
    c->icount += 7;
    NEAR_THEN(0xEE0C, 0xC73C, 0, 3 + 1);
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, FRAME(4));
    c->r[R_SI] = c->r[R_AX];
    c->icount += 3;
    NEAR_THEN(0xEE0C, 0xC745, 0, 3 + 4 + 1);
    c->r[R_BX] = cpu_pop16(c);
    alu_sub(c, c->r[R_AX], c->r[R_SI], 1, 0);
    const int x_wider = !x86_cond(c, 0xE);
    /* the narrower one, shifted up 14 */
    cpu_push16(c, FRAME(x_wider ? 6 : 4));
    c->icount += 4;
    NEAR_THEN(0xEE0C, x_wider ? 0xC750 : 0xC771, 0, 3 + 1);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_DX] = sign_word(c->r[R_AX]);
    set_r8(c, R_CL, 0x0E);
    c->icount += 3;
    NEAR_THEN(0xEF68, x_wider ? 0xC757 : 0xC778, 0, 3 + 1);
    SETFRAME(-8, c->r[R_AX]);
    SETFRAME(-6, c->r[R_DX]);
    /* over the wider one */
    cpu_push16(c, FRAME(x_wider ? 4 : 6));
    c->icount += 3;
    NEAR_THEN(0xEE0C, x_wider ? 0xC763 : 0xC784, 0, (x_wider ? 8u : 7u) + 1);
    c->r[R_BX] = cpu_pop16(c);
    SETFRAME(-0x0C, x_wider ? 1 : 0);
    c->r[R_DX] = sign_word(c->r[R_AX]);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, FRAME(-6));
    cpu_push16(c, FRAME(-8));
    c->icount += x_wider ? 8 : 7;
    NEAR_THEN(0xEE9C, 0xC796, 8, 7 + 1);
    /* the arctangent's correction term: |1333h - t| * 0B00h >> 14 */
    SETFRAME(-0x0E, c->r[R_AX]);
    c->r[R_DX] = sign_word(c->r[R_AX]);
    cpu_push16(c, c->r[R_DX]);                                    /* t, widened: the multiply's second operand */
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0x1333, FRAME(-0x0E), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 7;
    NEAR_THEN(0xEE0C, 0xC7A6, 0, 4 + 1);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_CX] = 0x0B00;
    x86_imul16(c, 0x0B00);
    set_r8(c, R_CL, 0x0E);
    c->icount += 4;
    NEAR_THEN(0xEF74, 0xC7B1, 0, 6 + 1);
    /* t * (2800h - correction) >> 14 */
    c->r[R_CX] = 0x2800;
    c->r[R_BX] = (uint16_t)alu_sub(c, c->r[R_BX], c->r[R_BX], 1, 0);
    c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], c->r[R_AX], 1, 0);
    c->r[R_BX] = (uint16_t)alu_sub(c, c->r[R_BX], c->r[R_DX], 1, (c->flags & F_CF) ? 1u : 0u);
    cpu_push16(c, c->r[R_BX]);
    cpu_push16(c, c->r[R_CX]);
    c->icount += 6;
    NEAR_THEN(0xEF36, 0xC7BF, 8, 1 + 1);
    set_r8(c, R_CL, 0x0E);
    c->icount += 1;
    NEAR_THEN(0xEF74, 0xC7C4, 0, 14);
    /* the quadrant */
    SETFRAME(-2, c->r[R_AX]);
    unsigned n = 3 + 2 + 2;                                       /* the three tests */
    alu_sub(c, FRAME(4), 0, 1, 0);
    const int x_pos = !x86_cond(c, 0xE);
    alu_sub(c, FRAME(6), 0, 1, 0);
    const int y_pos = !x86_cond(c, 0xE);
    alu_sub(c, FRAME(-0x0C), 0, 1, 0);
    const int flag = !(c->flags & F_ZF);
    if (x_pos && y_pos) {
        if (flag) { c->r[R_AX] = (uint16_t)alu_sub(c, 0x4000, FRAME(-2), 1, 0); n += 3; }
    } else if (x_pos) {
        if (flag) { set_r8(c, R_AH, (uint8_t)alu_add(c, get_r8(c, R_AH), 0x40, 0, 0)); n += 1; }
        else { c->r[R_AX] = (uint16_t)alu_sub(c, 0x8000, FRAME(-2), 1, 0); n += 4; }
    } else if (y_pos) {
        c->r[R_AX] = flag ? (uint16_t)alu_sub(c, c->r[R_AX], 0x4000, 1, 0) : (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);
        n += 2;
    } else {
        if (flag) { c->r[R_AX] = (uint16_t)alu_sub(c, 0xC000, FRAME(-2), 1, 0); n += 4; }
        else { set_r8(c, R_AH, (uint8_t)alu_add(c, get_r8(c, R_AH), 0x80, 0, 0)); n += 1; }
    }
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += n + 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x02ED9, asin(v): the inverse of the quarter-wave sine table at
 * 2084 (65 words, 100h of angle apart), angle 4000h for v = 1.0. v = 8000h
 * answers C000h at once. Otherwise the bracket is searched downward from
 * k = (|v| >> 9) + 1 for the first entry not above |v|, and the angle is
 * (k << 8) + ((|v| - T[k]) << 8) / (T[k+1] - T[k]) (the runtime's 32-bit
 * shift and divide), negated for a negative v. A search that runs off the
 * bottom (k < 0; no entry of the real table does) leaves the local at
 * [bp-2] as it was. Flags: the sign test or the negation. */
static int vgame_asin(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 6)) return 0;
    const uint16_t v = arg(c, 0);
    x86_enter(c, 8, 0);
    alu_sub(c, v, 0x8000, 1, 0);
    if (c->flags & F_ZF) {
        c->r[R_AX] = 0xC000;
        x86_leave(c);
        c->icount += 6;
        near_ret(c);
        return 1;
    }
    cpu_push16(c, v);
    c->icount += 4;
    NEAR_THEN(0xEE0C, 0x2EEF, 0, 6);
    c->r[R_BX] = cpu_pop16(c);
    SETFRAME(-4, c->r[R_AX]);                                     /* |v| */
    c->r[R_AX] = (uint16_t)alu_inc(c, x86_shift(c, 7, c->r[R_AX], 9, 1), 1);   /* sar ax, 9; inc ax */
    SETFRAME(-6, c->r[R_AX]);                                     /* k */
    c->icount += 6;
    int found = 0;
    for (;;) {                                                    /* 0x02EFF */
        if (!room(c, 8)) { c->ip = 0x2EFF; return 1; }
        alu_sub(c, FRAME(-6), 0, 1, 0);
        c->icount += 2;
        if (x86_cond(c, 0xC)) break;                              /* k < 0 */
        c->r[R_AX] = FRAME(-4);
        c->r[R_BX] = (uint16_t)(FRAME(-6) << 1);
        alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x2084)), c->r[R_AX], 1, 0);
        c->icount += 5;
        if (!x86_cond(c, 0xF)) { found = 1; break; }              /* T[k] <= |v| */
        SETFRAME(-6, (uint16_t)alu_dec(c, FRAME(-6), 1));
        c->icount += 1;
    }
    if (found) {
        if (!room(c, 10 + 1)) { c->ip = 0x2F13; return 1; }
        const uint16_t t = (uint16_t)(c->r[R_BX] + 0x2084);
        c->r[R_CX] = (uint16_t)alu_sub(c, ds_get(c, (uint16_t)(t + 2)), ds_get(c, t), 1, 0);
        c->r[R_AX] = c->r[R_CX];
        c->r[R_DX] = sign_word(c->r[R_AX]);
        cpu_push16(c, c->r[R_DX]);                                /* the step, widened: the divisor */
        cpu_push16(c, c->r[R_AX]);
        c->r[R_AX] = (uint16_t)alu_sub(c, FRAME(-4), ds_get(c, t), 1, 0);
        c->r[R_DX] = sign_word(c->r[R_AX]);
        set_r8(c, R_CL, 8);
        c->icount += 10;
        NEAR_THEN(0xEF68, 0x2F2D, 0, 2 + 1);
        cpu_push16(c, c->r[R_DX]);
        cpu_push16(c, c->r[R_AX]);
        c->icount += 2;
        NEAR_THEN(0xEE9C, 0x2F32, 8, 4 + 6);
        set_r8(c, R_CH, mem_read8(c, phys(c->seg[S_SS], (uint16_t)(c->r[R_BP] - 6))));
        set_r8(c, R_CL, (uint8_t)alu_sub(c, get_r8(c, R_CL), get_r8(c, R_CL), 0, 0));
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], c->r[R_CX], 1, 0);
        SETFRAME(-2, c->r[R_AX]);
        c->icount += 4;
    } else if (!room(c, 6)) { c->ip = 0x2F3C; return 1; }
    alu_sub(c, FRAME(4), 0, 1, 0);                                /* 0x02F3C: the sign */
    unsigned n = 5;
    if (x86_cond(c, 0xC)) { SETFRAME(-2, (uint16_t)alu_sub(c, 0, FRAME(-2), 1, 0)); n = 6; }
    c->r[R_AX] = FRAME(-2);
    x86_leave(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* The quadrant of an angle at out_at taken as asin or acos of |s| / cos:
 * with s and k the matrix words at s_at and k_at, a half turn on the high
 * byte when s <= 0 and k < 0, 8000h - angle when s > 0 and k < 0, and the
 * negation when s < 0 and k > 0 (*negated says so). Each test reads its
 * word again, as the original does. Returns the instructions run. */
static unsigned euler_quadrant(cpu_t *c, uint16_t s_at, uint16_t k_at, uint16_t out_at, int *negated)
{
    const uint16_t ds = c->seg[S_DS];
    unsigned n = 2;
    *negated = 0;
    alu_sub(c, ds_get(c, s_at), 0, 1, 0);
    if (!x86_cond(c, 0xF)) {                                      /* s <= 0 */
        alu_sub(c, ds_get(c, k_at), 0, 1, 0);
        n += 2;
        if (x86_cond(c, 0xC)) {
            const uint32_t hi = phys(ds, (uint16_t)(out_at + 1));
            mem_write8(c, hi, (uint8_t)alu_add(c, mem_read8(c, hi), 0x80, 0, 0));
            n += 1;
        }
    }
    alu_sub(c, ds_get(c, s_at), 0, 1, 0);
    n += 2;
    if (!x86_cond(c, 0xE)) {                                      /* s > 0 */
        alu_sub(c, ds_get(c, k_at), 0, 1, 0);
        n += 2;
        if (x86_cond(c, 0xC)) {
            c->r[R_AX] = (uint16_t)alu_sub(c, 0x8000, ds_get(c, out_at), 1, 0);
            ds_put(c, out_at, c->r[R_AX]);
            n += 3;
        }
    }
    alu_sub(c, ds_get(c, s_at), 0, 1, 0);
    n += 2;
    if (x86_cond(c, 0xC)) {                                       /* s < 0 */
        alu_sub(c, ds_get(c, k_at), 0, 1, 0);
        n += 2;
        if (!x86_cond(c, 0xE)) {
            ds_put(c, out_at, (uint16_t)alu_sub(c, 0, ds_get(c, out_at), 1, 0));
            n += 1;
            *negated = 1;
        }
    }
    return n;
}

/* One angle of angles_from_matrix: entered with the word at s_at pushed,
 * its code at 0x02CDB + o. When |s| < 5A81h (sin 45 degrees) the angle is
 * asin(|s / cos|), else acos(|k / cos|) (0x02F4A), with cos the pitch's
 * cosine at [bp-2] and the ratio 0x02E7F; then the quadrant. `after` is
 * the room needed past the quadrant. 0 when it stopped partway (the
 * original carries on), else 1, or 2 when the quadrant negated. */
static int euler_angle(machine_t *m, uint16_t o, uint16_t s_at, uint16_t k_at, uint16_t out_at, unsigned after)
{
    cpu_t *c = &m->cpu;
#define CALL_THEN(t_, r_, next_) do {                                                 \
        if (!guest_call(m, (t_), (uint16_t)((r_) + o))) return 0;                     \
        if (!room(c, (next_))) { c->ip = (uint16_t)((r_) + o); return 0; }            \
    } while (0)
    CALL_THEN(0xEE0C, 0x2CE2, 3 + 2 + 1);
    c->r[R_BX] = cpu_pop16(c);
    alu_sub(c, c->r[R_AX], 0x5A81, 1, 0);
    const int small = x86_cond(c, 0xC);
    cpu_push16(c, FRAME(-2));
    cpu_push16(c, ds_get(c, small ? s_at : k_at));
    c->icount += 5;
    CALL_THEN(0x2E7F, small ? 0x2CF2 : 0x2D09, 3 + 1);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 3;
    CALL_THEN(0xEE0C, small ? 0x2CF8 : 0x2D0F, 2 + 1);
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 2;
    CALL_THEN(small ? 0x2ED9 : 0x2F4A, small ? 0x2CFD : 0x2D14, (small ? 1u : 0u) + 2 + 17 + after);
#undef CALL_THEN
    if (small) c->icount += 1;                                    /* jmp to the common pop */
    c->r[R_BX] = cpu_pop16(c);
    ds_put(c, out_at, c->r[R_AX]);
    c->icount += 2;
    int negated;
    c->icount += euler_quadrant(c, s_at, k_at, out_at, &negated);
    return negated ? 2 : 1;
}

/* VGAME 0x02CB9, angles_from_matrix: the attitude angles from the matrix
 * at 2D94 (m0..m8): pitch [2DF0] = asin(-m5) (0x02ED9), and with its far
 * cosine (104E:0066) not zero, heading [2DEE] from m2 against m8 and roll
 * [2DF2] from m3 against m4 (euler_angle above); with it zero (straight up
 * or down) roll is 0 and heading asin(m1), placed by m3 and m4. Then
 * [2DFB] = 1 when pitch is within 38E3h..4001h or C71Dh..BFFFh
 * (exclusive) or when [2DFA] is set and roll is 0. Flags: the last test. */
static int vgame_angles_from_matrix(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;
    x86_enter(c, 2, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, ds_get(c, 0x2D9E), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 4;
    NEAR_THEN(0x2ED9, 0x2CC6, 0, 3 + 1);
    c->r[R_BX] = cpu_pop16(c);
    ds_put(c, 0x2DF0, c->r[R_AX]);                                /* pitch */
    cpu_push16(c, c->r[R_AX]);
    c->icount += 3;
    FAR_THEN(0x2CCB, 7 + 1);
    c->r[R_BX] = cpu_pop16(c);
    SETFRAME(-2, c->r[R_AX]);                                     /* cos(pitch) */
    alu_logic(c, c->r[R_AX], 1);
    c->icount += 4;
    if (!(c->flags & F_ZF)) {
        cpu_push16(c, ds_get(c, 0x2D98));
        c->icount += 1;
        if (!euler_angle(m, 0, 0x2D98, 0x2DA4, 0x2DEE, 1 + 1)) return 1;          /* heading */
        cpu_push16(c, ds_get(c, 0x2D9A));
        c->icount += 1;
        const int r = euler_angle(m, 0x7A, 0x2D9A, 0x2D9C, 0x2DF2, 1 + 17);       /* roll */
        if (!r) return 1;
        if (r == 2) c->icount += 1;                               /* jmp to the tests */
    } else {
        ds_put(c, 0x2DF2, 0);
        cpu_push16(c, ds_get(c, 0x2D96));
        c->icount += 3;
        NEAR_THEN(0x2ED9, 0x2DDE, 0, 2 + 17 + 17);
        c->r[R_BX] = cpu_pop16(c);
        ds_put(c, 0x2DEE, c->r[R_AX]);
        c->icount += 2;
        int negated;
        c->icount += euler_quadrant(c, 0x2D9A, 0x2D9C, 0x2DEE, &negated);
    }
    /* 0x02E1F: near the vertical, or level with the roll lock */
    const uint32_t lock = phys(c->seg[S_DS], 0x2DFB);
    unsigned n = 2 + 2 + 2 + 2;
    alu_sub(c, ds_get(c, 0x2DF0), 0x38E3, 1, 0);
    if (!x86_cond(c, 0xE)) {
        alu_sub(c, ds_get(c, 0x2DF0), 0x4001, 1, 0);
        n += 2;
        if (x86_cond(c, 0xC)) { mem_write8(c, lock, 1); n += 1; }
    }
    alu_sub(c, ds_get(c, 0x2DF0), 0xC71D, 1, 0);
    if (x86_cond(c, 0xC)) {
        alu_sub(c, ds_get(c, 0x2DF0), 0xBFFF, 1, 0);
        n += 2;
        if (!x86_cond(c, 0xE)) { mem_write8(c, lock, 1); n += 1; }
    }
    alu_sub(c, mem_read8(c, phys(c->seg[S_DS], 0x2DFA)), 0, 0, 0);
    if (!(c->flags & F_ZF)) {
        alu_sub(c, ds_get(c, 0x2DF2), 0, 1, 0);
        n += 2;
        if (c->flags & F_ZF) { mem_write8(c, lock, 1); n += 1; }
    }
    x86_leave(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

#define CF_IN ((c->flags & F_CF) ? 1u : 0u)

/* VGAME 0x0B5DC, camera_effect_point(x, y, alt): an effect's world point
 * on the screen. The offset from the aircraft - ([C0D0] - x, y - [C0DE],
 * (alt - [2DF4]) >> 5), in the outside view ([C0A6] bit 7) less the
 * camera's own offset (the 32-bit [BA3C] - [B2B4], [C056] - [B77E] and
 * [B788] - [2DF4], each >> 5 by 0x0EF74) - goes into the body frame by
 * camera_body_axis (0x0B792) for axes 0, 1 and 2: 32-bit x, y, z in the
 * frame. Behind the eye (z >= 0) or outside -|z| <= x <= |z| (x and y
 * halved first when [294B] is set) it is off: [4A10] = FFFFh. Otherwise
 * [4A10] = (x << 8) / z + 0A0h and [4A18] = 3/4 of (y << 8) / z (as the
 * shifts round it) plus 3Ch (34h with [294B] clear) when [368C] is set,
 * else plus 64h; [DEBE] = z >> 3. A screen x outside 0..13Fh, or a y
 * below 0 or beyond 60h (0C7h when [368C] is clear), keeps the x in
 * [952C] and is off. */
static int vgame_camera_effect_point(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 19)) return 0;
    const uint16_t x = arg(c, 0), y = arg(c, 1), alt = arg(c, 2);
    x86_enter(c, 0x12, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, ds_get(c, 0xC0D0), x, 1, 0);
    SETFRAME(-2, c->r[R_AX]);
    c->r[R_CX] = (uint16_t)alu_sub(c, y, ds_get(c, 0xC0DE), 1, 0);
    SETFRAME(-8, c->r[R_CX]);
    c->r[R_DX] = x86_shift(c, 7, (uint16_t)alu_sub(c, alt, ds_get(c, 0x2DF4), 1, 0), 5, 1);
    SETFRAME(-0x0E, c->r[R_DX]);
    alu_logic(c, mem_read8(c, phys(c->seg[S_DS], 0xC0A6)) & 0x80, 0);
    c->icount += 13;
    if (!(c->flags & F_ZF)) {                                     /* the outside view: less the camera's offset */
        static const uint16_t lo_at[2][2] = { { 0xBA3C, 0xB2B4 }, { 0xC056, 0xB77E } };
        static const uint16_t call_at[3] = { 0xB618, 0xB634, 0xB64C };
        static const int16_t local[3] = { -2, -8, -0x0E };
        for (int k = 0; k < 3; k++) {
            if (k < 2) {
                c->r[R_AX] = ds_get(c, lo_at[k][0]);
                c->r[R_DX] = ds_get(c, (uint16_t)(lo_at[k][0] + 2));
                c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], ds_get(c, lo_at[k][1]), 1, 0);
                c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], ds_get(c, (uint16_t)(lo_at[k][1] + 2)), 1, CF_IN);
            } else {
                c->r[R_AX] = ds_get(c, 0xB788);
                c->r[R_DX] = sign_word(c->r[R_AX]);
                c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], ds_get(c, 0x2DF4), 1, 0);
                c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], 0, 1, CF_IN);
            }
            set_r8(c, R_CL, 5);
            c->icount += 5;
            NEAR_THEN(0xEF74, (uint16_t)(call_at[k] + 3), 0, k < 2 ? 8 + 1 : 3 + 4 + 1);
            c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], FRAME(local[k]), 1, 0);
            c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);   /* neg ax */
            SETFRAME(local[k], c->r[R_AX]);
            c->icount += 3;
        }
    }
    /* the three body axes */
    static const uint16_t axis_call[3] = { 0xB662, 0xB679, 0xB690 };
    static const int16_t axis_lo[3] = { -6, -0x0C, -0x12 };
    for (int k = 0; k < 3; k++) {
        cpu_push16(c, FRAME(-0x0E));
        cpu_push16(c, FRAME(-8));
        cpu_push16(c, FRAME(-2));
        cpu_push16(c, (uint16_t)k);
        c->icount += 4;
        NEAR_THEN(0xB792, (uint16_t)(axis_call[k] + 3), 0, k < 2 ? 3 + 4 + 1 : 30);
        c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
        SETFRAME(axis_lo[k], c->r[R_AX]);
        SETFRAME(axis_lo[k] + 2, c->r[R_DX]);
        c->icount += 3;
    }
    /* in front, and inside the horizontal frustum? */
    unsigned n = 2;
    alu_logic(c, c->r[R_DX], 1);                                  /* or dx, dx */
    int off = !x86_cond(c, 0xC);
    if (!off) {
        alu_sub(c, mem_read8(c, phys(c->seg[S_DS], 0x294B)), 0, 0, 0);
        n += 2;
        if (!(c->flags & F_ZF)) {                                 /* x and y halved */
            SETFRAME(-4, x86_shift(c, 7, FRAME(-4), 1, 1));
            SETFRAME(-6, x86_shift(c, 3, FRAME(-6), 1, 1));
            SETFRAME(-0x0A, x86_shift(c, 7, FRAME(-0x0A), 1, 1));
            SETFRAME(-0x0C, x86_shift(c, 3, FRAME(-0x0C), 1, 1));
            n += 4;
        }
        c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);    /* -z: neg ax; adc dx, 0; neg dx */
        c->r[R_DX] = (uint16_t)alu_add(c, c->r[R_DX], 0, 1, CF_IN);
        c->r[R_DX] = (uint16_t)alu_sub(c, 0, c->r[R_DX], 1, 0);
        alu_sub(c, c->r[R_DX], FRAME(-4), 1, 0);
        n += 5;
        if (!x86_cond(c, 0xD)) off = 1;                           /* -z < x */
        else {
            n += 1;
            if (!x86_cond(c, 0xF)) {
                alu_sub(c, c->r[R_AX], FRAME(-6), 1, 0);
                n += 2;
                if (c->flags & F_CF) off = 1;
            }
        }
        if (!off) {
            c->r[R_AX] = FRAME(-6);
            c->r[R_DX] = FRAME(-4);
            alu_sub(c, FRAME(-0x10), c->r[R_DX], 1, 0);
            n += 4;
            if (!x86_cond(c, 0xC)) {                              /* z > x? */
                n += 1;
                if (!x86_cond(c, 0xE)) off = 1;
                else {
                    alu_sub(c, FRAME(-0x12), c->r[R_AX], 1, 0);
                    n += 2;
                    if (!x86_cond(c, 0x6)) off = 1;
                }
            }
        }
    }
    if (off) {
        ds_put(c, 0x4A10, 0xFFFF);                                /* jmp; mov [4A10], -1 */
        x86_leave(c);
        c->icount += n + 1 + 3;
        near_ret(c);
        return 1;
    }
    /* the screen position */
    cpu_push16(c, FRAME(-0x10));
    cpu_push16(c, FRAME(-0x12));
    set_r8(c, R_CL, 8);
    c->icount += n + 3;
    NEAR_THEN(0xEF68, 0xB6F2, 0, 2 + 1);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 2;
    NEAR_THEN(0xEE9C, 0xB6F7, 8, 7 + 1);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0xA0, 1, 0);
    ds_put(c, 0x4A10, c->r[R_AX]);
    cpu_push16(c, FRAME(-0x10));
    cpu_push16(c, FRAME(-0x12));
    c->r[R_AX] = FRAME(-0x0C);
    c->r[R_DX] = FRAME(-0x0A);
    set_r8(c, R_CL, 8);
    c->icount += 7;
    NEAR_THEN(0xEF68, 0xB70E, 0, 2 + 1);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 2;
    NEAR_THEN(0xEE9C, 0xB713, 8, 7 + 7 + 3 + 1);
    ds_put(c, 0x4A18, c->r[R_AX]);
    c->r[R_CX] = c->r[R_AX];
    c->r[R_AX] = x86_shift(c, 7, c->r[R_AX], 2, 1);
    c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], c->r[R_AX], 1, 0);
    ds_put(c, 0x4A18, c->r[R_CX]);
    alu_sub(c, ds_get(c, 0x368C), 0, 1, 0);
    c->icount += 7;
    if (!(c->flags & F_ZF)) {
        alu_sub(c, mem_read8(c, phys(c->seg[S_DS], 0x294B)), 1, 0, 0);
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, CF_IN);   /* sbb ax, ax */
        set_r8(c, R_AL, (uint8_t)alu_logic(c, get_r8(c, R_AL) & 0xF8, 0));
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x3C, 1, 0);
        c->r[R_CX] = (uint16_t)alu_add(c, c->r[R_CX], c->r[R_AX], 1, 0);
        ds_put(c, 0x4A18, c->r[R_CX]);
        c->icount += 7;
    } else {
        ds_put(c, 0x4A18, (uint16_t)alu_add(c, ds_get(c, 0x4A18), 0x64, 1, 0));
        c->icount += 1;
    }
    c->r[R_AX] = FRAME(-0x12);
    c->r[R_DX] = FRAME(-0x10);
    set_r8(c, R_CL, 3);
    c->icount += 3;
    NEAR_THEN(0xEF74, 0xB74C, 0, 21);
    ds_put(c, 0xDEBE, c->r[R_AX]);
    n = 1 + 2;
    alu_sub(c, ds_get(c, 0x4A10), 0, 1, 0);
    int keep_x = 0;
    if (!x86_cond(c, 0xC)) {
        alu_sub(c, ds_get(c, 0x4A10), 0x13F, 1, 0);
        n += 2;
        keep_x = x86_cond(c, 0xE);
    }
    if (!keep_x) {                                                /* off to the side */
        c->r[R_AX] = ds_get(c, 0x4A10);
        ds_put(c, 0x952C, c->r[R_AX]);
        ds_put(c, 0x4A10, 0xFFFF);
        n += 3;
    }
    alu_sub(c, ds_get(c, 0x4A18), 0, 1, 0);
    n += 2;
    int keep_y = 0;
    if (!x86_cond(c, 0xC)) {
        alu_sub(c, ds_get(c, 0x368C), 1, 1, 0);
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, CF_IN);
        c->r[R_AX] = (uint16_t)alu_logic(c, c->r[R_AX] & 0x67, 1);
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x60, 1, 0);
        alu_sub(c, c->r[R_AX], ds_get(c, 0x4A18), 1, 0);
        n += 6;
        keep_y = x86_cond(c, 0xD);
    }
    if (!keep_y) {                                                /* above or below */
        c->r[R_AX] = ds_get(c, 0x4A10);
        ds_put(c, 0x952C, c->r[R_AX]);
        ds_put(c, 0x4A10, 0xFFFF);
        n += 3;
    }
    x86_leave(c);
    c->icount += n + 2;
    near_ret(c);
    return 1;
}


/* VGAME 120A:0F97 (0x13037), vertex_cache, far: the current model's
 * vertices into camera space. The model stream is at segment [8590]: its
 * first word picks an entry of the offset table after it, which points at
 * a vertex count and then the vertices (three words each). Each vertex
 * (x, y, z) times the 3x3 word matrix at 7B28 becomes three 32-bit
 * coordinates in a 16-byte cache entry from 4EE6: out[j] = x*m[j] +
 * y*m[3+j] + z*m[6+j], the x products stored and the others added in
 * place, as the original does. DI, SI, ES and BP preserved; a loop that
 * would not fit before the next event stops at its head for the original
 * to carry on. Flags from the last pointer step (none when the count is
 * 0: the compare). */
static int vgame_vertex_cache(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 22)) return 0;
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->seg[S_ES]);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_AX] = ds_get(c, 0x8590);
    c->seg[S_ES] = c->r[R_AX];
    const uint16_t es = c->seg[S_ES];
    alu_sub(c, c->r[R_SI], c->r[R_SI], 1, 0);                     /* sub si, si */
    uint16_t si = x86_shift(c, 4, seg_read16(c, es, 0), 1, 1);   /* the model's entry in the table */
    si = (uint16_t)alu_add(c, si, 2, 1, 0);
    si = seg_read16(c, es, si);
    c->r[R_CX] = seg_read16(c, es, si);                           /* the vertex count */
    c->r[R_SI] = (uint16_t)alu_add(c, si, 2, 1, 0);
    c->r[R_DI] = (uint16_t)alu_sub(c, c->r[R_DI], c->r[R_DI], 1, 0);
    c->r[R_BX] = 0x7B28;
    alu_sub(c, c->r[R_CX], 0, 1, 0);
    if (c->flags & F_ZF) {
        c->r[R_BP] = cpu_pop16(c);
        c->seg[S_ES] = cpu_pop16(c);
        c->r[R_SI] = cpu_pop16(c);
        c->r[R_DI] = cpu_pop16(c);
        c->icount += 22;
        far_ret(c);
        return 1;
    }
    c->r[R_DI] = 0x4EE6;
    c->icount += 18;
    const uint16_t ds = c->seg[S_DS];
#define MAT(k) seg_read16(c, ds, (uint16_t)(0x7B28 + 2 * (k)))
#define OUT(o) (uint16_t)(c->r[R_DI] + (o))
    do {                                                          /* 0x0FC2: one vertex */
        if (!room(c, 46 + 5)) { c->ip = 0x0FC2; return 1; }
        cpu_push16(c, c->r[R_CX]);
        cpu_push16(c, c->r[R_SI]);
        const uint16_t v = c->r[R_SI];
        const uint16_t vx = seg_read16(c, es, v);
        c->r[R_CX] = seg_read16(c, es, (uint16_t)(v + 2));        /* vy */
        c->r[R_SI] = seg_read16(c, es, (uint16_t)(v + 4));        /* vz */
        c->r[R_BP] = vx;
        for (int j = 0; j < 3; j++) {                             /* the x products: stored */
            c->r[R_AX] = vx;
            x86_imul16(c, MAT(j));
            seg_write16(c, ds, OUT(4 * j), c->r[R_AX]);
            seg_write16(c, ds, OUT(4 * j + 2), c->r[R_DX]);
        }
        for (int row = 1; row < 3; row++)                         /* the y and z products: added */
            for (int j = 0; j < 3; j++) {
                c->r[R_AX] = row == 1 ? c->r[R_CX] : c->r[R_SI];
                x86_imul16(c, MAT(3 * row + j));
                seg_write16(c, ds, OUT(4 * j), (uint16_t)alu_add(c, seg_read16(c, ds, OUT(4 * j)), c->r[R_AX], 1, 0));
                seg_write16(c, ds, OUT(4 * j + 2),
                            (uint16_t)alu_add(c, seg_read16(c, ds, OUT(4 * j + 2)), c->r[R_DX], 1, CF_IN));
            }
        c->r[R_SI] = cpu_pop16(c);
        c->r[R_CX] = cpu_pop16(c);
        c->r[R_DI] = (uint16_t)alu_add(c, c->r[R_DI], 0x10, 1, 0);
        c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], 6, 1, 0);
        c->r[R_CX] = (uint16_t)(c->r[R_CX] - 1);                  /* loop */
        c->icount += 46;
    } while (c->r[R_CX] != 0);
#undef MAT
#undef OUT
    c->r[R_BP] = cpu_pop16(c);
    c->seg[S_ES] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->icount += 5;
    far_ret(c);
    return 1;
}

/* VGAME 130D:02DD (0x133AD), mclip_outcode: the outcode of the point
 * (BX, CX) against the model clip window - 4 left of [85FA], 8 right of
 * [85FE], 1 above [85FC], 2 below [8600] - in AL (AH kept). A compare
 * with the top-left that overflows sets [85F0] (the overflow note) and is
 * still taken as it came out. Flags from the last OR. */
static int vgame_mclip_outcode(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 23)) return 0;
    const uint16_t ds = c->seg[S_DS];
    const uint16_t x = c->r[R_BX], y = c->r[R_CX];
    uint8_t al = (uint8_t)alu_sub(c, get_r8(c, R_AL), get_r8(c, R_AL), 0, 0);
    unsigned n = 1;
    static const uint16_t lo_at[2] = { 0x85FA, 0x85FC }, hi_at[2] = { 0x85FE, 0x8600 };
    static const uint8_t lo_bit[2] = { 4, 1 }, hi_bit[2] = { 8, 2 };
    for (int axis = 0; axis < 2; axis++) {
        const uint16_t v = axis ? y : x;
        alu_sub(c, v, ds_get(c, lo_at[axis]), 1, 0);
        n += 3;                                                   /* cmp, jo, jl */
        if (c->flags & F_OF) { mem_write8(c, phys(ds, 0x85F0), 1); n += 2; }
        if (x86_cond(c, 0xC)) { al = (uint8_t)alu_logic(c, al | lo_bit[axis], 0); n += 2; }
        alu_sub(c, v, ds_get(c, hi_at[axis]), 1, 0);
        n += 2;                                                   /* cmp, jg */
        if (x86_cond(c, 0xF)) { al = (uint8_t)alu_logic(c, al | hi_bit[axis], 0); n += axis ? 1 : 2; }
        else if (axis) { al = (uint8_t)alu_logic(c, al, 0); n += 1; }   /* or al, al */
    }
    set_r8(c, R_AL, al);
    c->icount += n + 1;
    near_ret(c);
    return 1;
}

/* VGAME 130D:05E5 (0x136B5), mc32_publish: the two clipped edge ends of
 * the record at SI take their y from the window when their outcodes (BP
 * for the first, DI for the second) say they were clipped at the top (1:
 * [85FC]) or bottom (2: [8600]), else their own ([SI+4], [SI+0Ch]), into
 * [SI+6] and [SI+0Eh]. AL = AH = the left and right bits (8 and 4) of
 * both outcodes in one nibble: BP's right 1, left 2, DI's right 4, left 8.
 * BX is the second y; flags from the last test or OR. */
static int vgame_mc32_publish(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7 + 7 + 1 + 8 + 4 + 2)) return 0;
    const uint16_t ds = c->seg[S_DS], si = c->r[R_SI];
    const uint16_t code[2] = { c->r[R_BP], c->r[R_DI] };
    unsigned n = 0;
    for (int e = 0; e < 2; e++) {
        alu_logic(c, code[e] & 1, 1);
        if (!(c->flags & F_ZF)) { c->r[R_BX] = ds_get(c, 0x85FC); n += 5; }
        else {
            alu_logic(c, code[e] & 2, 1);
            if (!(c->flags & F_ZF)) { c->r[R_BX] = ds_get(c, 0x8600); n += 6; }
            else { c->r[R_BX] = ds_get(c, (uint16_t)(si + 4 + 8 * e)); n += 7; }
        }
        seg_write16(c, ds, (uint16_t)(si + 6 + 8 * e), c->r[R_BX]);
    }
    uint8_t al = (uint8_t)alu_sub(c, get_r8(c, R_AL), get_r8(c, R_AL), 0, 0);
    n += 1;
    static const struct { int e; uint16_t mask; uint8_t bit; } side[4] = { { 0, 8, 1 }, { 0, 4, 2 }, { 1, 8, 4 }, { 1, 4, 8 } };
    for (int k = 0; k < 4; k++) {
        alu_logic(c, code[side[k].e] & side[k].mask, 1);
        n += 2;
        if (!(c->flags & F_ZF)) { al = (uint8_t)alu_logic(c, al | side[k].bit, 0); n += 1; }
    }
    c->r[R_AX] = (uint16_t)((al << 8) | al);
    c->icount += n + 2;
    near_ret(c);
    return 1;
}

/* VGAME 130D:0271 (0x13341), mclip_point: the end (BX, CX) of a model
 * edge moved onto the clip window along the edge's slope ([85DE] / [85DC],
 * dy / dx), AL its outcode. Nothing to do (AL = 0): BP:DX = the point,
 * CF clear (the code just before the entry). Clipped left or right (AL &
 * 0Ch): x = [85FA] or [85FE] and y = CX + (x - BX) * dy / dx; inside the
 * window's height that is the answer, CF clear, AL = 1. Otherwise, or
 * when clipped only above or below: y = [85FC] (above, AL & 1) or [8600]
 * and x = BX + (y - CX) * dx / dy; BP:DX = (x, y), CF clear when x is
 * within [85FA]..[85FE], else CF set and AX = 0A0Ah (left) or 0505h
 * (right). It writes no memory, so a divide that would fault declines
 * before anything is changed. */
static int vgame_mclip_point(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 36)) return 0;
    uint16_t saved[8];
    memcpy(saved, c->r, sizeof saved);
    const uint16_t saved_flags = c->flags;
    const uint16_t bx = c->r[R_BX], cx = c->r[R_CX];
    unsigned n = 2;
    uint8_t al = (uint8_t)alu_logic(c, get_r8(c, R_AL), 0);
    if (c->flags & F_ZF) {                                        /* 0x0026B */
        c->r[R_BP] = bx;
        c->r[R_DX] = cx;
        set_flag(c, F_CF, 0);
        c->icount += n + 4;
        near_ret(c);
        return 1;
    }
    alu_logic(c, al & 0x0C, 0);
    n += 2;
    int y_edge_known = 0;                                         /* 1: straight to the bottom edge (0x002AD) */
    if (!(c->flags & F_ZF)) {                                     /* on the left or right edge */
        uint16_t edge = ds_get(c, 0x85FE);
        alu_logic(c, al & 4, 0);
        n += 3;
        if (!(c->flags & F_ZF)) { edge = ds_get(c, 0x85FA); n += 1; }
        c->r[R_BP] = edge;
        c->r[R_AX] = (uint16_t)alu_sub(c, edge, bx, 1, 0);
        x86_imul16(c, ds_get(c, 0x85DE));
        const uint16_t dx_ = ds_get(c, 0x85DC);
        if (!idiv_fits((int32_t)(((uint32_t)c->r[R_DX] << 16) | c->r[R_AX]), (int16_t)dx_)) {
            memcpy(c->r, saved, sizeof saved);
            c->flags = saved_flags;
            return 0;
        }
        x86_idiv16(c, dx_, 0);
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], cx, 1, 0);
        c->r[R_DX] = c->r[R_AX];
        set_r8(c, R_AL, 1);
        alu_sub(c, c->r[R_DX], ds_get(c, 0x85FC), 1, 0);
        n += 9;
        if (!x86_cond(c, 0xC)) {
            alu_sub(c, c->r[R_DX], ds_get(c, 0x8600), 1, 0);
            n += 2;
            if (!x86_cond(c, 0xF)) {                              /* inside the height */
                set_flag(c, F_CF, 0);
                c->icount += n + 2;
                near_ret(c);
                return 1;
            }
            y_edge_known = 1;
        }
    }
    /* 0x002A5: on the top or bottom edge */
    uint16_t edge;
    if (y_edge_known) { edge = ds_get(c, 0x8600); n += 1; }
    else {
        edge = ds_get(c, 0x85FC);
        alu_logic(c, get_r8(c, R_AL) & 1, 0);
        n += 3;
        if (c->flags & F_ZF) { edge = ds_get(c, 0x8600); n += 1; }
    }
    c->r[R_BP] = edge;
    c->r[R_AX] = (uint16_t)alu_sub(c, edge, cx, 1, 0);
    x86_imul16(c, ds_get(c, 0x85DC));
    const uint16_t dy_ = ds_get(c, 0x85DE);
    if (!idiv_fits((int32_t)(((uint32_t)c->r[R_DX] << 16) | c->r[R_AX]), (int16_t)dy_)) {
        memcpy(c->r, saved, sizeof saved);
        c->flags = saved_flags;
        return 0;
    }
    x86_idiv16(c, dy_, 0);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], bx, 1, 0);
    c->r[R_DX] = c->r[R_BP];                                      /* mov dx, ax; xchg bp, dx */
    c->r[R_BP] = c->r[R_AX];
    alu_sub(c, c->r[R_BP], ds_get(c, 0x85FA), 1, 0);
    n += 9;
    if (x86_cond(c, 0xC)) { c->r[R_AX] = 0x0A0A; set_flag(c, F_CF, 1); n += 4; }
    else {
        alu_sub(c, c->r[R_BP], ds_get(c, 0x85FE), 1, 0);
        n += 2;
        if (x86_cond(c, 0xF)) { c->r[R_AX] = 0x0505; set_flag(c, F_CF, 1); n += 4; }
        else { set_flag(c, F_CF, 0); n += 2; }
    }
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 130D:04FB (0x135CB), mc32_bisect_both: the 32-bit edge from the
 * point at DS:DI to the one at DS:SI (x, y as DX:AX, CX:BX) bisected
 * toward the clip window, at most 32 times ([85E0] counts down). Each
 * midpoint ((a + b) >> 1, from DI's end) gets its outcode (0x00671, in
 * BP): none, and it is inside - CF clear; outside on a side the SI end's
 * code [85DA] shares, it replaces the SI end (and the next midpoint starts
 * from DI's end again); on a side DI's code [85D8] shares, the edge is out
 * - CF set; otherwise it replaces the DI end. CF set when the count runs
 * out. */
static int vgame_mc32_bisect_both(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5 + 2 + 8 + 1)) return 0;
    ds_put(c, 0x85E0, 0x20);
#define END(p, k) seg_read16(c, c->seg[S_DS], (uint16_t)((p) + 2 * (k)))
    c->r[R_AX] = END(c->r[R_DI], 0); c->r[R_DX] = END(c->r[R_DI], 1);
    c->r[R_BX] = END(c->r[R_DI], 2); c->r[R_CX] = END(c->r[R_DI], 3);
    c->icount += 5;
    for (;;) {                                                    /* 0x0050C */
        ds_put(c, 0x85E0, (uint16_t)alu_dec(c, ds_get(c, 0x85E0), 1));
        c->icount += 2;
        if (c->flags & F_SF) { set_flag(c, F_CF, 1); c->icount += 2; break; }   /* the count ran out */
        const uint16_t si = c->r[R_SI];
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], END(si, 0), 1, 0);
        c->r[R_DX] = (uint16_t)alu_add(c, c->r[R_DX], END(si, 1), 1, CF_IN);
        c->r[R_BX] = (uint16_t)alu_add(c, c->r[R_BX], END(si, 2), 1, 0);
        c->r[R_CX] = (uint16_t)alu_add(c, c->r[R_CX], END(si, 3), 1, CF_IN);
        c->r[R_DX] = x86_shift(c, 7, c->r[R_DX], 1, 1);
        c->r[R_AX] = x86_shift(c, 3, c->r[R_AX], 1, 1);
        c->r[R_CX] = x86_shift(c, 7, c->r[R_CX], 1, 1);
        c->r[R_BX] = x86_shift(c, 3, c->r[R_BX], 1, 1);
        c->icount += 8;
        NEAR_THEN(0x0671, 0x0528, 0, 1 + 2 + 9 + 2 + 8 + 1);
        c->icount += 1;
        if (c->flags & F_ZF) { set_flag(c, F_CF, 0); c->icount += 2; break; }   /* inside */
        alu_logic(c, ds_get(c, 0x85DA) & c->r[R_BP], 1);
        c->icount += 2;
        const uint16_t si_after = c->r[R_SI], di = c->r[R_DI];
        const int to_si = (c->flags & F_ZF) != 0;
        uint16_t end = si_after;
        if (!to_si) {
            alu_logic(c, ds_get(c, 0x85D8) & c->r[R_BP], 1);
            c->icount += 2;
            if (!(c->flags & F_ZF)) { set_flag(c, F_CF, 1); c->icount += 2; break; }   /* out */
            end = di;
        }
        seg_write16(c, c->seg[S_DS], end, c->r[R_AX]);
        seg_write16(c, c->seg[S_DS], (uint16_t)(end + 2), c->r[R_DX]);
        seg_write16(c, c->seg[S_DS], (uint16_t)(end + 4), c->r[R_BX]);
        seg_write16(c, c->seg[S_DS], (uint16_t)(end + 6), c->r[R_CX]);
        if (to_si) {                                              /* from DI's end again */
            c->r[R_AX] = END(di, 0); c->r[R_DX] = END(di, 1); c->r[R_BX] = END(di, 2); c->r[R_CX] = END(di, 3);
            c->icount += 4;
        }
        c->icount += 5;
    }
#undef END
    near_ret(c);
    return 1;
}

/* The model polygon's span tables: for each screen row y the leftmost x at
 * [89DC + 2y] and the rightmost at [8D9E + 2y] (the original addresses them
 * as [row - 7624h] and [row - 7262h]). Widening one costs 2 instructions,
 * 3 when it moves. */
static unsigned span_widen_left(cpu_t *c, uint16_t row2, uint16_t x)
{
    const uint16_t at = (uint16_t)(row2 - 0x7624);
    alu_sub(c, x, ds_get(c, at), 1, 0);
    if (x86_cond(c, 0xD)) return 2;
    ds_put(c, at, x);
    return 3;
}
static unsigned span_widen_right(cpu_t *c, uint16_t row2, uint16_t x)
{
    const uint16_t at = (uint16_t)(row2 - 0x7262);
    alu_sub(c, x, ds_get(c, at), 1, 0);
    if (x86_cond(c, 0xE)) return 2;
    ds_put(c, at, x);
    return 3;
}

/* VGAME 1377:07E8 (0x13F58), mpoly_run(y0, y1, x), far: a vertical run of
 * the model polygon at column x, rows y0..y1 in either order, clipped to
 * the window's rows [85FC]..[8600] (nothing when it lies wholly outside
 * or is empty after clipping), widens the span tables, and [9160] keeps
 * the topmost row touched. A run longer than the room before the next
 * event stops at the row loop's head for the original to carry on. */
static int vgame_mpoly_run(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 26)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = FRAME(0x0A);
    c->r[R_BX] = FRAME(6);
    c->r[R_CX] = FRAME(8);
    alu_sub(c, c->r[R_CX], c->r[R_BX], 1, 0);
    unsigned n = 7;
    if (!x86_cond(c, 0xD)) {                                      /* top row first */
        const uint16_t t = c->r[R_CX]; c->r[R_CX] = c->r[R_BX]; c->r[R_BX] = t;
        n += 1;
    }
    const uint16_t top = 0x85FC, bottom = 0x8600;
    alu_sub(c, c->r[R_CX], ds_get(c, top), 1, 0);
    n += 2;
    int empty = x86_cond(c, 0xE);
    if (!empty) {
        alu_sub(c, c->r[R_BX], ds_get(c, bottom), 1, 0);
        n += 2;
        empty = x86_cond(c, 0xD);
    }
    if (!empty) {
        alu_sub(c, c->r[R_BX], ds_get(c, top), 1, 0);
        n += 2;
        if (x86_cond(c, 0xC)) { c->r[R_BX] = ds_get(c, top); n += 1; }
        alu_sub(c, c->r[R_CX], ds_get(c, bottom), 1, 0);
        n += 2;
        if (x86_cond(c, 0xF)) { c->r[R_CX] = ds_get(c, bottom); n += 1; }
        c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], c->r[R_BX], 1, 0);
        n += 2;                                                   /* sub, jcxz */
        empty = c->r[R_CX] == 0;
    }
    if (empty) {
        c->r[R_BP] = cpu_pop16(c);
        c->icount += n + 2;
        far_ret(c);
        return 1;
    }
    alu_sub(c, c->r[R_BX], ds_get(c, 0x9160), 1, 0);
    n += 2;
    if (x86_cond(c, 0xC)) { ds_put(c, 0x9160, c->r[R_BX]); n += 1; }
    c->r[R_CX] = (uint16_t)alu_inc(c, c->r[R_CX], 1);
    c->r[R_BX] = x86_shift(c, 4, c->r[R_BX], 1, 1);
    c->icount += n + 2;
    do {                                                          /* 0x0082B: one row */
        if (!room(c, 8 + 2)) { c->ip = 0x082B; return 1; }
        n = span_widen_left(c, c->r[R_BX], c->r[R_AX]);
        n += span_widen_right(c, c->r[R_BX], c->r[R_AX]);
        c->r[R_BX] = (uint16_t)alu_add(c, c->r[R_BX], 2, 1, 0);
        c->r[R_CX] = (uint16_t)(c->r[R_CX] - 1);                  /* loop */
        c->icount += n + 2;
    } while (c->r[R_CX] != 0);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 2;
    far_ret(c);
    return 1;
}

/* VGAME 1377:072B (0x13E9B), mpoly_edge, far: one edge of the model
 * polygon - the record at SI, x and y of its ends at [SI], [SI+4] and
 * [SI+8], [SI+0Ch] - into the span tables, a step per column (shallow) or
 * per row (steep) with the error term starting at -(n + 1) / 2. Taken
 * left to right; the row moves by BP = +2 or -2 a step. An edge with an
 * end above [85FC] or below [8600] is left out whole; [9160] keeps the
 * topmost row. SI and DI preserved. A long edge stops at a loop's head
 * for the original to carry on. */
static int vgame_mpoly_edge(machine_t *m)
{
    cpu_t *c = &m->cpu;
#define R(x) c->r[R_##x]
    if (!room(c, 38)) return 0;
    cpu_push16(c, R(SI));
    cpu_push16(c, R(DI));
    const uint16_t rec = R(SI);
    R(AX) = ds_get(c, rec);                                       /* x0 */
    R(DX) = ds_get(c, (uint16_t)(rec + 8));                       /* x1 */
    R(BX) = ds_get(c, (uint16_t)(rec + 4));                       /* y0 */
    R(SI) = ds_get(c, (uint16_t)(rec + 0x0C));                    /* y1 */
    alu_sub(c, R(AX), R(DX), 1, 0);
    unsigned n = 8;
    if (!x86_cond(c, 0xE)) {                                      /* left end first */
        uint16_t t = R(DX); R(DX) = R(AX); R(AX) = t;
        t = R(SI); R(SI) = R(BX); R(BX) = t;
        n += 2;
    }
    R(DI) = x86_shift(c, 4, R(BX), 1, 1);                         /* the left end's row */
    R(DX) = (uint16_t)alu_sub(c, R(DX), R(AX), 1, 0);             /* the width */
    R(BP) = 2;
    alu_sub(c, R(SI), R(BX), 1, 0);
    n += 6;
    if (!x86_cond(c, 0xD)) {                                      /* going up */
        R(BP) = (uint16_t)alu_sub(c, 0, R(BP), 1, 0);
        const uint16_t t = R(BX); R(BX) = R(SI); R(SI) = t;
        n += 2;
    }
    int out = 0;
    static const struct { int reg; uint16_t at; int cond; } test[4] = {
        { R_BX, 0x85FC, 0xC }, { R_BX, 0x8600, 0xF }, { R_SI, 0x85FC, 0xC }, { R_SI, 0x8600, 0xF } };
    for (int k = 0; k < 4 && !out; k++) {
        alu_sub(c, c->r[test[k].reg], ds_get(c, test[k].at), 1, 0);
        n += 2;
        out = x86_cond(c, test[k].cond);
    }
    if (out) goto done;
    alu_sub(c, R(BX), ds_get(c, 0x9160), 1, 0);
    n += 2;
    if (x86_cond(c, 0xC)) { ds_put(c, 0x9160, R(BX)); n += 1; }
    R(SI) = (uint16_t)alu_sub(c, R(SI), R(BX), 1, 0);             /* the height */
    alu_sub(c, R(SI), R(DX), 1, 0);
    n += 3;
    const int steep = x86_cond(c, 0x7);
    R(CX) = (uint16_t)alu_inc(c, steep ? R(SI) : R(DX), 1);       /* steps */
    R(BX) = x86_shift(c, 5, R(CX), 1, 1);
    R(BX) = (uint16_t)alu_sub(c, 0, R(BX), 1, 0);                 /* the error term */
    c->icount += n + 6;
    n = 0;
    if (!steep) {
        for (;;) {                                                /* 0x00796: a new row */
            if (!room(c, 3)) { c->ip = 0x0796; return 1; }
            c->icount += span_widen_left(c, R(DI), R(AX));
            for (;;) {                                            /* 0x007A0: along the row */
                if (!room(c, 11)) { c->ip = 0x07A0; return 1; }
                R(BX) = (uint16_t)alu_add(c, R(BX), R(SI), 1, 0);
                c->icount += 2;
                if (!(c->flags & F_SF)) break;
                R(AX) = (uint16_t)alu_inc(c, R(AX), 1);
                R(CX) = (uint16_t)(R(CX) - 1);                    /* loop */
                c->icount += 2;
                if (R(CX) == 0) {                                 /* the last column */
                    R(AX) = (uint16_t)alu_dec(c, R(AX), 1);
                    c->icount += 1 + span_widen_right(c, R(DI), R(AX));
                    goto done;
                }
            }
            c->icount += span_widen_right(c, R(DI), R(AX));       /* 0x00784: the row's end */
            R(BX) = (uint16_t)alu_sub(c, R(BX), R(DX), 1, 0);
            R(CX) = (uint16_t)alu_dec(c, R(CX), 1);
            c->icount += 3;
            if (c->flags & F_ZF) goto done;
            R(AX) = (uint16_t)alu_inc(c, R(AX), 1);
            R(DI) = (uint16_t)alu_add(c, R(DI), R(BP), 1, 0);
            c->icount += 2;
        }
    } else {
        for (;;) {                                                /* 0x007C9: a row */
            if (!room(c, 13)) { c->ip = 0x07C9; return 1; }
            c->icount += span_widen_left(c, R(DI), R(AX));
            c->icount += span_widen_right(c, R(DI), R(AX));
            R(BX) = (uint16_t)alu_add(c, R(BX), R(DX), 1, 0);
            c->icount += 2;
            if (c->flags & F_SF) {                                /* same column */
                R(DI) = (uint16_t)alu_add(c, R(DI), R(BP), 1, 0);
                R(CX) = (uint16_t)(R(CX) - 1);                    /* loop */
                c->icount += 2;
                if (R(CX) == 0) goto done;
            } else {                                              /* 0x007C1: the next column */
                R(CX) = (uint16_t)alu_dec(c, R(CX), 1);
                c->icount += 2;
                if (c->flags & F_ZF) goto done;
                R(BX) = (uint16_t)alu_sub(c, R(BX), R(SI), 1, 0);
                R(AX) = (uint16_t)alu_inc(c, R(AX), 1);
                R(DI) = (uint16_t)alu_add(c, R(DI), R(BP), 1, 0);
                c->icount += 3;
            }
        }
    }
done:
    R(DI) = cpu_pop16(c);
    R(SI) = cpu_pop16(c);
    c->icount += n + 3;
    far_ret(c);
    return 1;
#undef R
}

/* LES BX, [DS:at]: the far pointer stored there. */
static void les_bx(cpu_t *c, uint16_t at)
{
    c->r[R_BX] = ds_get(c, at);
    c->seg[S_ES] = ds_get(c, (uint16_t)(at + 2));
}

/* VGAME 0x04A42, flight_end(result): result 7 only raises [E57E], the
 * flag that ends the flight. Otherwise, unless a result other than 0
 * arrives while [C09A] is set, it ends the flight with the sortie record
 * (the far pointer at [E574]) filled in: +28h the result, +26h = 3 for a
 * result of 0 with [C09A] clear, then the map position ([C0D0], [C0DE])
 * at +74h, +76h and [3664], [C5F4] at +34h, +36h - each through the
 * pointer read again - and event 8 is logged (0x04ABA). */
static int vgame_flight_end(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 31)) return 0;
    const uint16_t result = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    alu_sub(c, result, 7, 1, 0);
    if (c->flags & F_ZF) {
        mem_write8(c, phys(c->seg[S_DS], 0xE57E), 1);
        x86_leave(c);
        c->icount += 7;
        near_ret(c);
        return 1;
    }
    alu_sub(c, ds_get(c, 0xC09A), 0, 1, 0);
    unsigned n = 6;
    if (!(c->flags & F_ZF)) {
        alu_sub(c, result, 0, 1, 0);
        n += 2;
        if (!(c->flags & F_ZF)) {                                 /* a later result: kept out */
            x86_leave(c);
            c->icount += n + 2;
            near_ret(c);
            return 1;
        }
    }
    mem_write8(c, phys(c->seg[S_DS], 0xE57E), 1);
    c->r[R_AX] = result;
    les_bx(c, 0xE574);
    seg_write16(c, c->seg[S_ES], (uint16_t)(c->r[R_BX] + 0x28), result);
    alu_logic(c, result, 1);
    n += 6;
    if (c->flags & F_ZF) {
        alu_sub(c, ds_get(c, 0xC09A), result, 1, 0);
        n += 2;
        if (c->flags & F_ZF) {
            les_bx(c, 0xE574);
            seg_write16(c, c->seg[S_ES], (uint16_t)(c->r[R_BX] + 0x26), 3);
            n += 2;
        }
    }
    static const struct { uint16_t from, to; } field[4] = { { 0xC0D0, 0x74 }, { 0xC0DE, 0x76 }, { 0x3664, 0x34 }, { 0xC5F4, 0x36 } };
    for (int k = 0; k < 4; k++) {
        c->r[R_AX] = ds_get(c, field[k].from);
        les_bx(c, 0xE574);
        seg_write16(c, c->seg[S_ES], (uint16_t)(c->r[R_BX] + field[k].to), c->r[R_AX]);
    }
    cpu_push16(c, 0);
    cpu_push16(c, 8);
    c->icount += n + 12 + 2;
    NEAR_THEN(0x4ABA, 0x4AB6, 0, 4);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x056BB, ai_alert_area: the alert's centre and heading taken from
 * the aircraft - [B2B2] into [3D90], the position into [B2CC] / [B786]
 * (the decoy's [39D8] / [39DA] instead while [39E0] is set), [2DF4] into
 * [B79A], [2DEE] into [9920], [C0E2] = 0FFh - and every live object's
 * word at +8 of its 16-byte record at B2D0 (those with +4 set, of [E56C])
 * clamped by 0x0C67A to at most 0FFh and at least
 * ([3688] + [3686] - 1) * 16. A long table stops at the loop's head for
 * the original to carry on. Flags from the count compare. */
static int vgame_ai_alert_area(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 18 + 3)) return 0;
    x86_enter(c, 2, 0);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_AX] = ds_get(c, 0xB2B2);
    ds_put(c, 0x3D90, c->r[R_AX]);
    alu_sub(c, ds_get(c, 0x39E0), 0, 1, 0);
    const int decoy = !(c->flags & F_ZF);
    c->r[R_AX] = ds_get(c, decoy ? 0x39D8 : 0xC0D0);
    ds_put(c, 0xB2CC, c->r[R_AX]);
    c->r[R_AX] = ds_get(c, decoy ? 0x39DA : 0xC0DE);
    ds_put(c, 0xB786, c->r[R_AX]);
    c->r[R_AX] = ds_get(c, 0x2DF4);
    ds_put(c, 0xB79A, c->r[R_AX]);
    c->r[R_AX] = ds_get(c, 0x2DEE);
    ds_put(c, 0x9920, c->r[R_AX]);
    ds_put(c, 0xC0E2, 0x00FF);
    SETFRAME(-2, 0);
    c->icount += decoy ? 18 : 17;
    for (;;) {                                                    /* 0x0572C */
        if (!room(c, 3 + 4 + 8 + 1)) { c->ip = 0x572C; return 1; }
        c->r[R_AX] = ds_get(c, 0xE56C);
        alu_sub(c, FRAME(-2), c->r[R_AX], 1, 0);
        c->icount += 3;
        if (!x86_cond(c, 0xC)) break;
        c->r[R_BX] = x86_shift(c, 4, FRAME(-2), 4, 1);
        alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] - 0x4D2C)), 0, 1, 0);
        c->icount += 4;
        if (!(c->flags & F_ZF)) {
            cpu_push16(c, 0x00FF);
            c->r[R_AX] = (uint16_t)alu_add(c, ds_get(c, 0x3688), ds_get(c, 0x3686), 1, 0);
            c->r[R_AX] = (uint16_t)alu_dec(c, c->r[R_AX], 1);
            c->r[R_AX] = x86_shift(c, 4, c->r[R_AX], 4, 1);
            cpu_push16(c, c->r[R_AX]);
            cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] - 0x4D28)));
            c->r[R_SI] = c->r[R_BX];
            c->icount += 8;
            NEAR_THEN(0xC67A, 0x5722, 0, 3);
            c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
            ds_put(c, (uint16_t)(c->r[R_SI] - 0x4D28), c->r[R_AX]);
            c->icount += 2;
        }
        SETFRAME(-2, (uint16_t)alu_inc(c, FRAME(-2), 1));
        c->icount += 1;
    }
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x04F2B, approach_cue: the glide-slope cue [46F2] for the landing
 * target (object [E00C], its 16-byte record at B2D0). Off (0) while
 * [9B34] bit 0 is set. Otherwise the slope angle is the bearing
 * (0x0C702) of (-(height >> 5), |target y - [C0DE]| less 18h for a
 * carrier, flag 2 in the record's byte +0Bh, else 38h), the height being
 * [2DF4], less 80h for a carrier, and the cue compares it with the pitch
 * [2DF0] - [98F6]: 2 on the slope, 1 more than 5B0h below it, 3 more than
 * 5B0h above; at zero height it stays 2. */
static int vgame_approach_cue(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 35)) return 0;
    x86_enter(c, 0x0A, 0);
    alu_logic(c, mem_read8(c, phys(c->seg[S_DS], 0x9B34)) & 1, 0);
    if (!(c->flags & F_ZF)) {
        ds_put(c, 0x46F2, 0);
        x86_leave(c);
        c->icount += 7;
        near_ret(c);
        return 1;
    }
#define TARGET (uint16_t)x86_shift(c, 4, ds_get(c, 0xE00C), 4, 1)
    c->r[R_AX] = (uint16_t)alu_sub(c, ds_get(c, 0x2DF0), ds_get(c, 0x98F6), 1, 0);
    SETFRAME(-2, c->r[R_AX]);                                     /* the pitch */
    c->r[R_BX] = TARGET;
    c->r[R_AX] = (uint16_t)alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] - 0x4D2E)), ds_get(c, 0xC0DE), 1, 0);
    SETFRAME(-0x0A, c->r[R_AX]);                                  /* the distance */
    alu_logic(c, c->r[R_AX], 1);
    unsigned n = 13;
    if (x86_cond(c, 0xC)) {
        c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);
        SETFRAME(-0x0A, c->r[R_AX]);
        n += 2;
    }
    c->r[R_BX] = TARGET;
    alu_logic(c, mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_BX] - 0x4D29))) & 2, 0);
    const int carrier = !(c->flags & F_ZF);
    SETFRAME(-0x0A, (uint16_t)alu_sub(c, FRAME(-0x0A), carrier ? 0x18 : 0x38, 1, 0));
    n += 4 + (carrier ? 2 : 1);
    c->r[R_AX] = ds_get(c, 0x2DF4);
    SETFRAME(-8, c->r[R_AX]);                                     /* the height */
    c->r[R_BX] = TARGET;
    alu_logic(c, mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_BX] - 0x4D29))) & 2, 0);
    n += 6;
    if (!(c->flags & F_ZF)) {
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], 0x80, 1, 0);
        SETFRAME(-8, c->r[R_AX]);
        n += 2;
    }
#undef TARGET
    cpu_push16(c, FRAME(-0x0A));
    c->r[R_AX] = x86_shift(c, 7, FRAME(-8), 5, 1);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->icount += n + 5;
    NEAR_THEN(0xC702, 0x4F9F, 0, 14);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], FRAME(-2), 1, 0);
    ds_put(c, 0x46F2, 2);
    alu_sub(c, FRAME(-8), 0, 1, 0);
    n = 6;
    if (!(c->flags & F_ZF)) {
        alu_logic(c, c->r[R_AX], 1);
        n += 2;
        const int below = x86_cond(c, 0xC);
        if (below) { c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0); n += 1; }
        alu_sub(c, c->r[R_AX], 0x5B0, 1, 0);
        n += 2;
        if (!x86_cond(c, 0xE)) { ds_put(c, 0x46F2, below ? 1 : 3); n += 1; }
    }
    x86_leave(c);
    c->icount += n + 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x0D441, frame_rates: the rates that follow the frame time [368E]
 * (in timer ticks). The time step [43E8] = clamp3(((-120 / t) + 9) >> 1,
 * 1, 4) for t above 15, else 0; then t itself is clamped to 4 - [3DA8]
 * .. 15, [B198] = clamp3(2t, 3, 10h), [B2B2] = 250t and [9F94] = 200t
 * (clamp3 is 0x0C67A). */
static int vgame_frame_rates(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 11)) return 0;
    alu_sub(c, ds_get(c, 0x368E), 0x0F, 1, 0);
    if (!x86_cond(c, 0xE)) {
        cpu_push16(c, 4);
        cpu_push16(c, 1);
        c->r[R_AX] = 0xFF88;
        c->r[R_DX] = 0xFFFF;
        x86_idiv16(c, ds_get(c, 0x368E), 0);                     /* t > 15: it cannot fault */
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 9, 1, 0);
        c->r[R_AX] = x86_shift(c, 7, c->r[R_AX], 1, 1);
        cpu_push16(c, c->r[R_AX]);
        c->icount += 10;
        NEAR_THEN(0xC67A, 0xD45D, 0, 3 + 5 + 1);
        c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
        ds_put(c, 0x43E8, c->r[R_AX]);
        c->icount += 3;
    } else {
        ds_put(c, 0x43E8, 0);
        c->icount += 3;
    }
    cpu_push16(c, 0x0F);
    c->r[R_AX] = (uint16_t)alu_sub(c, 4, ds_get(c, 0x3DA8), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, ds_get(c, 0x368E));
    c->icount += 5;
    NEAR_THEN(0xC67A, 0xD47C, 0, 6 + 1);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    ds_put(c, 0x368E, c->r[R_AX]);
    cpu_push16(c, 0x10);
    cpu_push16(c, 3);
    c->r[R_AX] = x86_shift(c, 4, c->r[R_AX], 1, 1);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 6;
    NEAR_THEN(0xC67A, 0xD48C, 0, 7);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    ds_put(c, 0xB198, c->r[R_AX]);
    c->r[R_AX] = x86_imul3(c, ds_get(c, 0x368E), 0xFA);
    ds_put(c, 0xB2B2, c->r[R_AX]);
    c->r[R_AX] = x86_imul3(c, ds_get(c, 0x368E), 0xC8);
    ds_put(c, 0x9F94, c->r[R_AX]);
    c->icount += 7;
    near_ret(c);
    return 1;
}

/* VGAME 0x0D4A5, detail_table: the six detail ranges [48D6 + 2k] =
 * 20h << (k + [990E]) (the counter is the local at [bp-2], read where the
 * original reads it), then [48E2] = [48DE] + [48E0], [48E4] =
 * clamp3(2 * [48E0], 1000h, 270Fh) and the far limit [48E6] =
 * ([990E] + 1) * 0D05h. */
static int vgame_detail_table(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 2)) return 0;
    x86_enter(c, 2, 0);
    SETFRAME(-2, 0);
    c->icount += 2;
    for (;;) {                                                    /* 0x0D4AE */
        if (!room(c, 10 + 8 + 1)) { c->ip = 0xD4AE; return 1; }
        const uint8_t cl = (uint8_t)alu_add(c, (uint8_t)FRAME(-2), mem_read8(c, phys(c->seg[S_DS], 0x990E)), 0, 0);
        set_r8(c, R_CL, cl);
        c->r[R_AX] = x86_shift(c, 4, 0x20, cl, 1);
        c->r[R_BX] = x86_shift(c, 4, FRAME(-2), 1, 1);
        ds_put(c, (uint16_t)(c->r[R_BX] + 0x48D6), c->r[R_AX]);
        SETFRAME(-2, (uint16_t)alu_inc(c, FRAME(-2), 1));
        alu_sub(c, FRAME(-2), 6, 1, 0);
        c->icount += 10;
        if (!x86_cond(c, 0xC)) break;
    }
    c->r[R_AX] = (uint16_t)alu_add(c, ds_get(c, 0x48DE), ds_get(c, 0x48E0), 1, 0);
    ds_put(c, 0x48E2, c->r[R_AX]);
    cpu_push16(c, 0x270F);
    cpu_push16(c, 0x1000);
    c->r[R_AX] = x86_shift(c, 4, ds_get(c, 0x48E0), 1, 1);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 8;
    NEAR_THEN(0xC67A, 0xD4E5, 0, 7);
    ds_put(c, 0x48E4, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_inc(c, ds_get(c, 0x990E), 1);
    c->r[R_AX] = x86_imul3(c, c->r[R_AX], 0x0D05);
    ds_put(c, 0x48E6, c->r[R_AX]);
    x86_leave(c);
    c->icount += 7;
    near_ret(c);
    return 1;
}

/* VGAME 0x07243, seeker(weapon, x, y, _, mode): whether the seeker of
 * weapon slot `weapon` (28-byte records at 3C3A: x, y, ..., +6 range, +8
 * heading, +0Eh the lock) sees the point (x, y). The offset's octagonal
 * distance (0x0C6B3) and bearing (0x0C702, kept in [9526]) are taken; the
 * point is seen (AX = 1, the distance into [9524]) when it is within
 * 1000h of the heading or the mode is 3, and - in mode 0 - the heading is
 * also within 2000h of the aircraft's [2DEE]. Not seen, AX = 0; a slot
 * below 8 whose target is more than 6000h off its heading and beyond its
 * range also drops its lock. Flags: the last compare, or the sub. */
static int vgame_seeker(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 13)) return 0;
    const uint16_t ds = c->seg[S_DS];
    x86_enter(c, 8, 0);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_AX] = FRAME(6);
    c->r[R_BX] = x86_imul3(c, FRAME(4), 0x1C);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], seg_read16(c, ds, (uint16_t)(c->r[R_BX] + 0x3C3A)), 1, 0);
    SETFRAME(-6, c->r[R_AX]);                                     /* dx */
    c->r[R_CX] = (uint16_t)alu_sub(c, FRAME(8), seg_read16(c, ds, (uint16_t)(c->r[R_BX] + 0x3C3C)), 1, 0);
    SETFRAME(-8, c->r[R_CX]);                                     /* dy */
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_SI] = c->r[R_BX];
    c->icount += 12;
    NEAR_THEN(0xC6B3, 0x7267, 0, 7 + 1);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    SETFRAME(-4, c->r[R_AX]);                                     /* the distance */
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, FRAME(-8), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, FRAME(-6));
    c->icount += 7;
    NEAR_THEN(0xC702, 0x7278, 0, 5 + 1);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    ds_put(c, 0x9526, c->r[R_AX]);                                /* the bearing */
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], ds_get(c, (uint16_t)(c->r[R_SI] + 0x3C42)), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 5;
    NEAR_THEN(0xEE0C, 0x7285, 0, 17);
    c->r[R_BX] = cpu_pop16(c);
    alu_sub(c, c->r[R_AX], 0x1000, 1, 0);
    unsigned n = 3;
    int in_cone = x86_cond(c, 0xE);
    if (!in_cone) {
        alu_sub(c, FRAME(0x0C), 3, 1, 0);
        n += 2;
        in_cone = (c->flags & F_ZF) != 0;
    }
    int seen = 0;
    if (!in_cone) {                                               /* well off: perhaps lose the lock */
        alu_sub(c, c->r[R_AX], 0x6000, 1, 0);
        n += 2;
        if (!x86_cond(c, 0xE)) {
            alu_sub(c, FRAME(4), 8, 1, 0);
            n += 2;
            if (x86_cond(c, 0xC)) {
                c->r[R_AX] = FRAME(-4);
                alu_sub(c, ds_get(c, (uint16_t)(c->r[R_SI] + 0x3C40)), c->r[R_AX], 1, 0);
                n += 3;
                if (x86_cond(c, 0xC)) { ds_put(c, (uint16_t)(c->r[R_SI] + 0x3C48), 0); n += 1; }
            }
        }
    } else {                                                      /* 0x072B0 */
        alu_sub(c, FRAME(0x0C), 0, 1, 0);
        n += 2;
        seen = 1;
        if (c->flags & F_ZF) {                                    /* mode 0: the heading too */
            c->r[R_BX] = x86_imul3(c, FRAME(4), 0x1C);
            c->r[R_AX] = (uint16_t)alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x3C42)), ds_get(c, 0x2DEE), 1, 0);
            cpu_push16(c, c->r[R_AX]);
            c->icount += n + 4;
            NEAR_THEN(0xEE0C, 0x72C6, 0, 9);
            c->r[R_BX] = cpu_pop16(c);
            alu_sub(c, c->r[R_AX], 0x2000, 1, 0);
            n = 3;
            seen = !x86_cond(c, 0xF);
        }
    }
    if (seen) {
        c->r[R_AX] = FRAME(-4);
        ds_put(c, 0x9524, c->r[R_AX]);
        c->r[R_AX] = 1;
        n += 6;
    } else {
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
        n += 4;
    }
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += n;
    near_ret(c);
    return 1;
}

/* VGAME 0x00FBD, scene_override(a, b, c, d): the override table at B79E
 * (5-byte entries, [9932] of them) searched from the last down for the
 * entry whose first four bytes are a, b, c, d; AL its fifth byte, or 0.
 * The index lives in [950C] (left at the entry found, or -1). AH is what
 * the count's decrement left. A long table stops at the loop's head. */
static int vgame_scene_override(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 6)) return 0;
    const uint16_t ds = c->seg[S_DS];
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = (uint16_t)alu_dec(c, ds_get(c, 0x9932), 1);
    ds_put(c, 0x950C, c->r[R_AX]);
    c->icount += 6;
    for (;;) {                                                    /* 0x00FCD */
        if (!room(c, 16 + 3)) { c->ip = 0x0FCD; return 1; }
        alu_sub(c, ds_get(c, 0x950C), 0, 1, 0);
        c->icount += 2;
        if (x86_cond(c, 0xC)) {                                   /* not found */
            set_r8(c, R_AL, (uint8_t)alu_sub(c, get_r8(c, R_AL), get_r8(c, R_AL), 0, 0));
            break;
        }
        c->r[R_BX] = x86_imul3(c, ds_get(c, 0x950C), 5);
        int k = 0;
        for (; k < 4; k++) {
            const uint8_t want = mem_read8(c, phys(c->seg[S_SS], (uint16_t)(c->r[R_BP] + 4 + 2 * k)));
            set_r8(c, R_AL, want);
            alu_sub(c, mem_read8(c, phys(ds, (uint16_t)(c->r[R_BX] - 0x4862 + k))), want, 0, 0);
            c->icount += k ? 3 : 4;
            if (!(c->flags & F_ZF)) break;
        }
        if (k == 4) {                                             /* found */
            set_r8(c, R_AL, mem_read8(c, phys(ds, (uint16_t)(c->r[R_BX] - 0x485E))));
            break;
        }
        ds_put(c, 0x950C, (uint16_t)alu_dec(c, ds_get(c, 0x950C), 1));
        c->icount += 1;
    }
    x86_leave(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* VGAME 130D:0577 (0x13647), mc32_bisect_one: the 32-bit edge from DS:DI
 * to DS:SI (x, y as DX:AX, CX:BX) bisected until a midpoint lies on the
 * clip window's edge, at most 32 times ([85E0]). A midpoint is
 * (a + b) >> 1 per coordinate, except that a sum whose two words are
 * equal is not halved - and becomes 0 when both are FFFFh (the code just
 * before the entry). Outside the window (0x00671) it replaces the DI
 * end; inside and on an edge (0x0064A) it is the answer, in the
 * registers; inside, off the edges, it replaces the SI end and the next
 * starts from DI's end again. When the count runs out the SI end is the
 * answer. */
static int vgame_mc32_bisect_one(machine_t *m)
{
    cpu_t *c = &m->cpu;
#define R(x) c->r[R_##x]
#define END(p, k) seg_read16(c, c->seg[S_DS], (uint16_t)((p) + 2 * (k)))
#define LOAD(p) do { const uint16_t p_ = (p); R(AX) = END(p_, 0); R(DX) = END(p_, 1); R(BX) = END(p_, 2); R(CX) = END(p_, 3); } while (0)
#define STORE(p) do { const uint16_t p_ = (p), d_ = c->seg[S_DS];                              \
        seg_write16(c, d_, p_, R(AX)); seg_write16(c, d_, (uint16_t)(p_ + 2), R(DX));           \
        seg_write16(c, d_, (uint16_t)(p_ + 4), R(BX)); seg_write16(c, d_, (uint16_t)(p_ + 6), R(CX)); } while (0)
    if (!room(c, 26)) return 0;
    ds_put(c, 0x85E0, 0x20);
    LOAD(R(DI));
    c->icount += 5;
    for (;;) {                                                    /* 0x00588 */
        ds_put(c, 0x85E0, (uint16_t)alu_dec(c, ds_get(c, 0x85E0), 1));
        c->icount += 2;
        if (c->flags & F_SF) { LOAD(R(SI)); c->icount += 5; break; }   /* ran out: the SI end */
        const uint16_t si = R(SI);
        R(AX) = (uint16_t)alu_add(c, R(AX), END(si, 0), 1, 0);
        R(DX) = (uint16_t)alu_add(c, R(DX), END(si, 1), 1, CF_IN);
        R(BX) = (uint16_t)alu_add(c, R(BX), END(si, 2), 1, 0);
        R(CX) = (uint16_t)alu_add(c, R(CX), END(si, 3), 1, CF_IN);
        c->icount += 4;
        for (int k = 0; k < 2; k++) {                             /* x (DX:AX), then y (CX:BX) */
            uint16_t *lo = &c->r[k ? R_BX : R_AX], *hi = &c->r[k ? R_CX : R_DX];
            alu_sub(c, *lo, *hi, 1, 0);
            c->icount += 2;
            if (c->flags & F_ZF) {                                /* 0x0055F / 0x0056B */
                alu_sub(c, *lo, 0xFFFF, 1, 0);
                c->icount += 2;
                if (c->flags & F_ZF) {
                    *lo = (uint16_t)alu_sub(c, *lo, *lo, 1, 0);
                    *hi = (uint16_t)alu_sub(c, *hi, *hi, 1, 0);
                    c->icount += 3;
                }
            } else {
                *hi = x86_shift(c, 7, *hi, 1, 1);
                *lo = x86_shift(c, 3, *lo, 1, 1);
                c->icount += 2;
            }
        }
        NEAR_THEN(0x0671, 0x05AC, 0, 1 + 5 + 21);
        c->icount += 1;
        if (!(c->flags & F_ZF)) { STORE(R(DI)); c->icount += 5; continue; }   /* outside: the DI end */
        NEAR_THEN(0x064A, 0x05B1, 0, 1 + 9 + 21);
        c->icount += 1;
        if (!(c->flags & F_ZF)) { c->icount += 1; break; }       /* on an edge */
        STORE(R(SI));
        LOAD(R(DI));
        c->icount += 9;
    }
#undef R
#undef END
#undef LOAD
#undef STORE
    near_ret(c);
    return 1;
}

/* VGAME 0x00810, scale_by_level(level, v): the 32-bit v at a detail
 * level's scale - level 0 doubles it, 1 keeps it, 2, 3 and 4 divide it by
 * 4, 16 and 64 rounding to nearest (the runtime's unsigned shift
 * 0x0F018); any other level leaves DX:AX as they were but for AX, which
 * keeps what the level's countdown left. */
static int vgame_scale_by_level(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 21)) return 0;
    const uint16_t level = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = level;
    alu_logic(c, level, 1);
    unsigned n = 6;
    int k = 0;
    if (!(c->flags & F_ZF)) {
        for (k = 1; k <= 4; k++) {
            c->r[R_AX] = (uint16_t)alu_dec(c, c->r[R_AX], 1);
            n += 2;
            if (c->flags & F_ZF) break;
        }
    }
    if (k > 4) {                                                  /* no such level */
        x86_leave(c);
        c->icount += n + 2;
        near_ret(c);
        return 1;
    }
    c->r[R_AX] = FRAME(6);
    c->r[R_DX] = FRAME(8);
    if (k <= 1) {
        if (k == 0) {
            c->r[R_AX] = x86_shift(c, 4, c->r[R_AX], 1, 1);
            c->r[R_DX] = x86_shift(c, 2, c->r[R_DX], 1, 1);
            n += 2;
        }
        x86_leave(c);
        c->icount += n + 4;
        near_ret(c);
        return 1;
    }
    static const uint16_t half[5] = { 0, 0, 2, 8, 0x20 };
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], half[k], 1, 0);
    c->r[R_DX] = (uint16_t)alu_add(c, c->r[R_DX], 0, 1, CF_IN);
    set_r8(c, R_CL, (uint8_t)(2 * (k - 1)));
    c->icount += n + (k < 4 ? 6 : 5);                             /* levels 2 and 3 jump to the shared call */
    NEAR_THEN(0xF018, 0x0829, 0, 2);
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x00F3D, scene_set_override(obj, value): an object's override
 * byte (+12h of the scene object at obj, its key the four bytes +0Eh..
 * +11h) becomes value. A key already in the override table (0x00FBD)
 * takes the value in its table entry when value is the theatre's
 * [DED0] or the mission's [C0D8] (signed bytes against the value), else in
 * the object's model record (+6 of the record +0Ch points at); a new key
 * is appended to the table (the 5 bytes from +0Eh, by 0x0EDE0, [9932]
 * counting it). Either way the model record's +6 gets bit 7. */
static int vgame_scene_set_override(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 14)) return 0;
    const uint16_t ds = c->seg[S_DS];
    x86_enter(c, 2, 0);
#define BYTE_AT(o) mem_read8(c, phys(c->seg[S_DS], (uint16_t)(o)))
    set_r8(c, R_AL, mem_read8(c, phys(c->seg[S_SS], (uint16_t)(c->r[R_BP] + 6))));
    c->r[R_BX] = FRAME(4);
    mem_write8(c, phys(ds, (uint16_t)(c->r[R_BX] + 0x12)), get_r8(c, R_AL));
    set_r8(c, R_CH, (uint8_t)alu_sub(c, get_r8(c, R_CH), get_r8(c, R_CH), 0, 0));
    for (int k = 3; k >= 0; k--) {                                /* the key, last byte first */
        set_r8(c, R_CL, BYTE_AT(c->r[R_BX] + 0x0E + k));
        cpu_push16(c, c->r[R_CX]);
    }
    c->icount += 13;
    NEAR_THEN(0x0FBD, 0x0F5F, 0, 23);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    alu_logic(c, get_r8(c, R_AL), 0);
    c->icount += 3;
    if (!(c->flags & F_ZF)) {                                     /* a known key */
        const uint8_t value = mem_read8(c, phys(c->seg[S_SS], (uint16_t)(c->r[R_BP] + 6)));
        c->r[R_AX] = (uint16_t)(int16_t)(int8_t)BYTE_AT(0xDED0);
        set_r8(c, R_CL, value);
        set_r8(c, R_CH, (uint8_t)alu_sub(c, get_r8(c, R_CH), get_r8(c, R_CH), 0, 0));
        alu_sub(c, c->r[R_AX], c->r[R_CX], 1, 0);
        c->icount += 6;
        int in_table = (c->flags & F_ZF) != 0;
        if (!in_table) {
            c->r[R_AX] = (uint16_t)(int16_t)(int8_t)BYTE_AT(0xC0D8);
            alu_sub(c, c->r[R_AX], c->r[R_CX], 1, 0);
            c->icount += 4;
            in_table = (c->flags & F_ZF) != 0;
        }
        set_r8(c, R_AL, mem_read8(c, phys(c->seg[S_SS], (uint16_t)(c->r[R_BP] + 6))));
        if (in_table) {
            c->r[R_BX] = x86_imul3(c, ds_get(c, 0x950C), 5);
            mem_write8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_BX] - 0x485E)), get_r8(c, R_AL));
            c->icount += 4;
        } else {
            c->r[R_BX] = FRAME(4);
            c->r[R_BX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0C));
            mem_write8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_BX] + 6)), get_r8(c, R_AL));
            c->icount += 5;
        }
    } else {                                                      /* 0x00F97: a new key */
        cpu_push16(c, 5);
        c->r[R_AX] = (uint16_t)alu_add(c, FRAME(4), 0x0E, 1, 0);
        cpu_push16(c, c->r[R_AX]);
        c->r[R_AX] = ds_get(c, 0x9932);
        ds_put(c, 0x9932, (uint16_t)alu_inc(c, ds_get(c, 0x9932), 1));
        c->r[R_AX] = x86_imul3(c, c->r[R_AX], 5);
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0xB79E, 1, 0);
        cpu_push16(c, c->r[R_AX]);
        c->icount += 9;
        NEAR_THEN(0xEDE0, 0x0FB1, 0, 5);
    }
    c->r[R_BX] = FRAME(4);                                        /* 0x00FB1 */
    c->r[R_BX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0C));
    const uint32_t flag = phys(c->seg[S_DS], (uint16_t)(c->r[R_BX] + 6));
    mem_write8(c, flag, (uint8_t)alu_logic(c, mem_read8(c, flag) | 0x80, 0));
#undef BYTE_AT
    x86_leave(c);
    c->icount += 5;
    near_ret(c);
    return 1;
}

/* VGAME 0x04E0B, mission_field(at, n): n bytes of the mission record
 * between DS:at and the far cursor [951E]:[951C] - from the cursor into
 * DS when [DEBA] is set (a load), the other way when it is clear (a
 * save) - by the far copy 0x0EDA4, then the cursor moves past them. */
static int vgame_mission_field(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 13)) return 0;
    x86_enter(c, 4, 0);
    c->r[R_AX] = FRAME(4);
    SETFRAME(-4, c->r[R_AX]);
    SETFRAME(-2, c->seg[S_DS]);
    alu_sub(c, ds_get(c, 0xDEBA), 0, 1, 0);
    if (!(c->flags & F_ZF)) {                                     /* load: from the cursor */
        cpu_push16(c, FRAME(6));
        cpu_push16(c, c->r[R_AX]);
        cpu_push16(c, c->seg[S_DS]);
        cpu_push16(c, ds_get(c, 0x951C));
        cpu_push16(c, ds_get(c, 0x951E));
        c->icount += 12;
    } else {                                                      /* save: to the cursor */
        cpu_push16(c, FRAME(6));
        cpu_push16(c, ds_get(c, 0x951C));
        cpu_push16(c, ds_get(c, 0x951E));
        cpu_push16(c, FRAME(-4));
        cpu_push16(c, FRAME(-2));
        c->icount += 11;
    }
    NEAR_THEN(0xEDA4, 0x4E42, 0, 4);
    c->r[R_AX] = FRAME(6);
    ds_put(c, 0x951C, (uint16_t)alu_add(c, ds_get(c, 0x951C), c->r[R_AX], 1, 0));
    x86_leave(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x04D5D, mission_transfer: the mission record's fields, in order,
 * through mission_field (0x04E0B): theatre and mission bytes, the object
 * count [C0E0] and its 16-byte objects from B2CE, [992C], [E56C], the
 * waypoint count [DEFC] and its 36-byte records from C16A, the 80h-byte
 * tables at C630 and C0E6, the 2EEh-byte text at E016, 100h bytes at
 * B1A0, [DEB4], [3D9C], 10h bytes of waypoints at 2E9E and 24h at E304.
 * Each size is read when its field comes up. */
static int vgame_mission_transfer(machine_t *m)
{
    cpu_t *c = &m->cpu;
    enum { OBJECTS = 1, WAYPOINTS = 2 };
    static const struct { uint16_t at, n, call_at; int kind; } field[16] = {
        { 0xDED0, 1, 0x4D62, 0 }, { 0xC0D8, 1, 0x4D6C, 0 }, { 0xC0E0, 2, 0x4D76, 0 }, { 0x992C, 2, 0x4D80, 0 },
        { 0xE56C, 2, 0x4D8A, 0 }, { 0xB2CE, 0, 0x4D99, OBJECTS }, { 0xDEFC, 2, 0x4DA3, 0 },
        { 0xC16A, 0, 0x4DB1, WAYPOINTS }, { 0xC630, 0x80, 0x4DBC, 0 }, { 0xC0E6, 0x80, 0x4DC7, 0 },
        { 0xE016, 0x2EE, 0x4DD2, 0 }, { 0xB1A0, 0x100, 0x4DDD, 0 }, { 0xDEB4, 2, 0x4DE7, 0 },
        { 0x3D9C, 2, 0x4DF1, 0 }, { 0x2E9E, 0x10, 0x4DFB, 0 }, { 0xE304, 0x24, 0x4E05, 0 } };
    static const unsigned before[3] = { 2, 4, 3 };                /* the size's instructions and the two pushes */
    if (!room(c, before[field[0].kind] + 1)) return 0;
    for (int k = 0; k < 16; k++) {
        if (field[k].kind == OBJECTS) {
            c->r[R_AX] = x86_shift(c, 4, ds_get(c, 0xC0E0), 4, 1);
            cpu_push16(c, c->r[R_AX]);
        } else if (field[k].kind == WAYPOINTS) {
            c->r[R_AX] = x86_imul3(c, ds_get(c, 0xDEFC), 0x24);
            cpu_push16(c, c->r[R_AX]);
        } else cpu_push16(c, field[k].n);
        cpu_push16(c, field[k].at);
        c->icount += before[field[k].kind];
        const uint16_t ret = (uint16_t)(field[k].call_at + 3);
        NEAR_THEN(0x4E0B, ret, 0, 2 + (k < 15 ? before[field[k + 1].kind] + 1 : 1));
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_BX] = cpu_pop16(c);
        c->icount += 2;
    }
    c->icount += 1;
    near_ret(c);
    return 1;
}

/* VGAME 0x04BEC, mission_load: the deadline from the clock (0x04E4B), then
 * the mission record read in ([DEBA] = 1, mission_transfer), the index of
 * its text strings rebuilt - [DF08] = E016 and, for each zero byte of the
 * 2EEh-byte text, the address after it in the next of 80h slots from
 * DF0A (the counters are the locals at [bp-2] and [bp-4]) - and the
 * camera's start offset from the home object [E308]: its x << 5 plus 2
 * into [BA3C], (8000h - its y) << 5 into [C056] (the runtime's 32-bit
 * shift 0x0EF68). SI preserved. */
static int vgame_mission_load(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 3)) return 0;
    x86_enter(c, 4, 0);
    cpu_push16(c, c->r[R_SI]);
    c->icount += 2;
    NEAR_THEN(0x4E4B, 0x4BF4, 0, 2);
    ds_put(c, 0xDEBA, 1);
    c->icount += 1;
    NEAR_THEN(0x4D5D, 0x4BFD, 0, 3);
    ds_put(c, 0xDF08, 0xE016);
    SETFRAME(-2, 1);
    SETFRAME(-4, 0);
    c->icount += 3;
    for (;;) {                                                    /* 0x04C0D: one byte of the text */
        if (!room(c, 13 + 6 + 1)) { c->ip = 0x4C0D; return 1; }
        c->r[R_BX] = FRAME(-4);
        alu_sub(c, mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_BX] - 0x1FEA))), 0, 0, 0);
        c->icount += 3;
        if (c->flags & F_ZF) {
            alu_sub(c, FRAME(-2), 0x80, 1, 0);
            c->icount += 2;
            if (x86_cond(c, 0xC)) {                               /* a string starts after it */
                c->r[R_BX] = (uint16_t)alu_add(c, c->r[R_BX], 0xE017, 1, 0);
                c->r[R_SI] = x86_shift(c, 4, FRAME(-2), 1, 1);
                ds_put(c, (uint16_t)(c->r[R_SI] - 0x20F8), c->r[R_BX]);
                SETFRAME(-2, (uint16_t)alu_inc(c, FRAME(-2), 1));
                c->icount += 5;
            }
        }
        SETFRAME(-4, (uint16_t)alu_inc(c, FRAME(-4), 1));
        alu_sub(c, FRAME(-4), 0x2EE, 1, 0);
        c->icount += 3;
        if (!x86_cond(c, 0xC)) break;
    }
    c->r[R_BX] = x86_shift(c, 4, ds_get(c, 0xE308), 4, 1);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] - 0x4D30));
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    set_r8(c, R_CL, 5);
    c->r[R_SI] = c->r[R_BX];
    c->icount += 6;
    NEAR_THEN(0xEF68, 0x4C4C, 0, 9 + 1);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 2, 1, 0);
    c->r[R_DX] = (uint16_t)alu_add(c, c->r[R_DX], 0, 1, CF_IN);
    ds_put(c, 0xBA3C, c->r[R_AX]);
    ds_put(c, 0xBA3E, c->r[R_DX]);
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0x8000, ds_get(c, (uint16_t)(c->r[R_SI] - 0x4D2E)), 1, 0);
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, CF_IN);
    set_r8(c, R_CL, 5);
    c->icount += 9;
    NEAR_THEN(0xEF68, 0x4C69, 0, 5);
    ds_put(c, 0xC056, c->r[R_AX]);
    ds_put(c, 0xC058, c->r[R_DX]);
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 5;
    near_ret(c);
    return 1;
}

/* VGAME 0x08ED6, navgrid_project(x, y): a map point on the navigation
 * display. The offset from the aircraft ((x - [C0D0]) and ([C0DE] - y),
 * each >> (7 - [40B0]), the display's zoom) is turned by the heading
 * [2DEE] with the vector sine and cosine (0x0C818, 0x0C831): [4A10] =
 * cos*dx - sin*dy + 70h, [4A18] = 98h - (sin*dx + cos*dy). [DEBE] = 0,
 * or FFFFh when the point is outside 4Eh..93h across or 71h..0A6h down.
 * SI preserved. */
static int vgame_navgrid_project(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 17)) return 0;
    x86_enter(c, 4, 0);
    cpu_push16(c, c->r[R_SI]);
    ds_put(c, 0xDEBE, 0);
    const uint8_t cl = (uint8_t)alu_sub(c, 7, mem_read8(c, phys(c->seg[S_DS], 0x40B0)), 0, 0);
    set_r8(c, R_CL, cl);
    c->r[R_AX] = x86_shift(c, 7, (uint16_t)alu_sub(c, FRAME(4), ds_get(c, 0xC0D0), 1, 0), cl, 1);
    SETFRAME(-2, c->r[R_AX]);                                     /* dx */
    c->r[R_DX] = x86_shift(c, 7, (uint16_t)alu_sub(c, ds_get(c, 0xC0DE), FRAME(6), 1, 0), cl, 1);
    SETFRAME(-4, c->r[R_DX]);                                     /* dy */
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, ds_get(c, 0x2DEE));
    c->r[R_SI] = c->r[R_AX];
    c->icount += 16;
    /* across: cos*dx - sin*dy */
    NEAR_THEN(0xC818, 0x8F09, 0, 5 + 1);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, ds_get(c, 0x2DEE));
    c->r[R_SI] = c->r[R_AX];
    c->icount += 5;
    NEAR_THEN(0xC831, 0x8F15, 0, 6 + 1);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_SI], 1, 0);
    ds_put(c, 0x4A10, c->r[R_AX]);
    /* down: sin*dx + cos*dy */
    cpu_push16(c, FRAME(-2));
    cpu_push16(c, ds_get(c, 0x2DEE));
    c->icount += 6;
    NEAR_THEN(0xC818, 0x8F26, 0, 5 + 1);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, FRAME(-4));
    cpu_push16(c, ds_get(c, 0x2DEE));
    c->r[R_SI] = c->r[R_AX];
    c->icount += 5;
    NEAR_THEN(0xC831, 0x8F34, 0, 21);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], c->r[R_AX], 1, 0);
    ds_put(c, 0x4A18, c->r[R_SI]);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0x98, c->r[R_SI], 1, 0);
    ds_put(c, 0x4A18, c->r[R_AX]);
    ds_put(c, 0x4A10, (uint16_t)alu_add(c, ds_get(c, 0x4A10), 0x70, 1, 0));
    unsigned n = 10;
    static const struct { int reg; uint16_t lo, hi; } box[2] = { { -1, 0x4E, 0x93 }, { R_AX, 0x71, 0xA6 } };
    for (int k = 0; k < 2; k++) {
#define V (box[k].reg < 0 ? ds_get(c, 0x4A10) : c->r[R_AX])
        alu_sub(c, V, box[k].lo, 1, 0);
        int off = x86_cond(c, 0xC);
        if (k) n += 2;
        if (!off) {
            alu_sub(c, V, box[k].hi, 1, 0);
            n += 2;
            off = x86_cond(c, 0xF);
        }
#undef V
        if (off) { ds_put(c, 0xDEBE, 0xFFFF); n += 1; }
    }
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += n + 3;
    near_ret(c);
    return 1;
}

/* VGAME 120A:0008 (0x120A8), model_matrix(matrix, scale), far: the model
 * renderer's matrix at 7B28 - the camera matrix at `matrix` copied
 * (1452:0316) and transposed into place (1452:02E4, 7B3A as scratch) - set
 * for the perspective: [7D5E] = 7FFF00h / max(|2 * scale|, 100h), the
 * third column times -[7D5E] (the high words of the doubled products),
 * the first two columns halved, and the second negated (with [7D6A] set)
 * or taken as -3/4 of itself. */
static int vgame_model_matrix(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 20)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = x86_shift(c, 4, FRAME(8), 1, 1);
    c->r[R_DX] = sign_word(c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, (uint16_t)alu_logic(c, c->r[R_AX] ^ c->r[R_DX], 1), c->r[R_DX], 1, 0);
    alu_sub(c, c->r[R_AX], 0x100, 1, 0);
    unsigned n = 9;
    if (c->flags & F_CF) { c->r[R_AX] = 0x100; n += 1; }
    c->r[R_CX] = c->r[R_AX];
    c->r[R_DX] = 0x7F;
    c->r[R_AX] = 0xFF00;
    x86_div16(c, c->r[R_CX]);                                     /* CX >= 100h: it cannot fault */
    ds_put(c, 0x7D5E, c->r[R_AX]);
    c->r[R_AX] = 0x7B28;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = FRAME(6);
    cpu_push16(c, c->r[R_AX]);
    c->icount += n + 9;
    FAR_THEN(0x0033, 5 + 1);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 4, 1, 0);
    c->r[R_AX] = 0x7B3A;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], 0x12, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 5;
    FAR_THEN(0x0044, 50);
    const uint16_t ds = c->seg[S_DS];
#define MW(k) seg_read16(c, ds, (uint16_t)(0x7B28 + 2 * (k)))
#define SETMW(k, v) seg_write16(c, ds, (uint16_t)(0x7B28 + 2 * (k)), (v))
    c->r[R_BX] = 0x7B28;
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, ds_get(c, 0x7D5E), 1, 0);
    c->r[R_CX] = c->r[R_AX];
    for (int k = 2; k < 9; k += 3) {                              /* the third column, times -[7D5E] */
        c->r[R_AX] = c->r[R_CX];
        x86_imul16(c, MW(k));
        c->r[R_AX] = x86_shift(c, 4, c->r[R_AX], 1, 1);
        c->r[R_DX] = x86_shift(c, 2, c->r[R_DX], 1, 1);
        SETMW(k, c->r[R_DX]);
    }
    static const int halve[6] = { 0, 3, 6, 1, 4, 7 };
    for (int k = 0; k < 6; k++) SETMW(halve[k], x86_shift(c, 7, MW(halve[k]), 1, 1));
    alu_sub(c, ds_get(c, 0x7D6A), 0, 1, 0);
    n = 4 + 4 + 5 + 5 + 6 + 2;
    if (!(c->flags & F_ZF)) {
        for (int k = 1; k < 9; k += 3) SETMW(k, (uint16_t)alu_sub(c, 0, MW(k), 1, 0));
        n += 4;
    } else {
        for (int k = 1; k < 9; k += 3) {                          /* -3/4 of itself */
            c->r[R_AX] = (uint16_t)alu_sub(c, 0, MW(k), 1, 0);
            c->r[R_CX] = x86_shift(c, 7, x86_shift(c, 7, c->r[R_AX], 1, 1), 1, 1);
            c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_CX], 1, 0);
            SETMW(k, c->r[R_AX]);
        }
        n += 21;
    }
#undef MW
#undef SETMW
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 4, 1, 0);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += n + 3;
    far_ret(c);
    return 1;
}

/* VGAME 0x05582, detect_evaluate(x, y, alt, site, bearing, range): how
 * strongly sensor site `site` (14-byte records at 2EBA: +0 power, +2
 * reach, +4 flags) at (x, y, alt) sees the aircraft; 0 for site 0 or -1.
 * The octagonal distance (0x0C6B3) / 64 is the range r. The strength is
 * (power - r) * ((terrain class bits 2..3 of the map cell under the
 * aircraft, the byte at B1A0 + 16 * (y >> 11) + (x >> 11)) * (2 * [3686]
 * + reach + 1)) / power (the runtime's multiply and divide), then scaled
 * by the aspect: the bearing b from the site to the aircraft (0x0C702),
 * a = the high byte of |b - heading [2DEE]| folded to 0..40h, and the
 * factor (a + 20h) >> 1 - or, for a site with flag 1 (it sees the
 * engines), ((([B07E] >> 5) * (60h - a)) >> 9 + 20h) >> 1 - multiplied
 * with the strength halved and the product >> 4. Out of reach in height
 * (|alt - [2DF4]| >> 10 above r, unsigned) it is 0; past 64h with the
 * awareness [3D8A] added, time returns to normal (0x0D4F5). *bearing = b,
 * *range = r; the strength is halved again while the record at the far
 * pointer [9924] has +42h set. SI, DI preserved. */
static int vgame_detect_evaluate(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 16)) return 0;
    x86_enter(c, 0x0E, 0);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    alu_sub(c, FRAME(0x0A), 0, 1, 0);
    unsigned n = 5;
    int none = (c->flags & F_ZF) != 0;
    if (!none) {
        alu_sub(c, FRAME(0x0A), 0xFFFF, 1, 0);
        n += 2;
        none = (c->flags & F_ZF) != 0;
    }
    if (none) {
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
        c->r[R_SI] = cpu_pop16(c);
        c->r[R_DI] = cpu_pop16(c);
        x86_leave(c);
        c->icount += n + 2 + 4;
        near_ret(c);
        return 1;
    }
    /* the range */
    c->r[R_AX] = (uint16_t)alu_sub(c, ds_get(c, 0xC0DE), FRAME(6), 1, 0);
    SETFRAME(-0x0C, c->r[R_AX]);                                  /* dy */
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, ds_get(c, 0xC0D0), FRAME(4), 1, 0);
    SETFRAME(-0x0A, c->r[R_AX]);                                  /* dx */
    cpu_push16(c, c->r[R_AX]);
    c->icount += n + 8;
    NEAR_THEN(0xC6B3, 0x55B0, 0, 29 + 1);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_AX] = x86_shift(c, 5, c->r[R_AX], 6, 1);
    SETFRAME(-8, c->r[R_AX]);                                     /* r */
    /* the raw strength: (power - r) * terrain * (2 * [3686] + reach + 1) / power */
    c->r[R_BX] = x86_imul3(c, FRAME(0x0A), 0x0E);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x2EBA));
    c->r[R_DX] = sign_word(c->r[R_AX]);
    cpu_push16(c, c->r[R_DX]);                                    /* power, widened: the divisor */
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], FRAME(-8), 1, 0);
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], 0, 1, CF_IN);
    cpu_push16(c, c->r[R_DX]);                                    /* power - r, widened */
    cpu_push16(c, c->r[R_AX]);
    c->r[R_SI] = x86_shift(c, 4, x86_shift(c, 5, ds_get(c, 0xC0DE), 0x0B, 1), 4, 1);
    c->r[R_AX] = c->r[R_BX];
    c->r[R_BX] = ds_get(c, 0xC0D0);
    c->r[R_DI] = c->r[R_AX];
    c->r[R_CX] = x86_shift(c, 4, ds_get(c, 0x3686), 1, 1);
    c->r[R_CX] = (uint16_t)alu_add(c, c->r[R_CX], ds_get(c, (uint16_t)(c->r[R_DI] + 0x2EBC)), 1, 0);
    c->r[R_CX] = (uint16_t)alu_inc(c, c->r[R_CX], 1);
    c->r[R_BX] = x86_shift(c, 5, c->r[R_BX], 0x0B, 1);
    set_r8(c, R_AL, mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_BX] + c->r[R_SI] - 0x4E60))));
    c->r[R_AX] = (uint16_t)alu_logic(c, c->r[R_AX] & 0x0C, 1);
    x86_imul16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 29;
    NEAR_THEN(0xEF36, 0x55F9, 8, 2 + 1);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 2;
    NEAR_THEN(0xEE9C, 0x55FE, 8, 5 + 1);
    SETFRAME(-0x0E, c->r[R_AX]);                                  /* the strength */
    /* the aspect */
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, FRAME(-0x0C), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, FRAME(-0x0A));
    c->icount += 5;
    NEAR_THEN(0xC702, 0x560D, 0, 5 + 1);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    SETFRAME(-6, c->r[R_AX]);                                     /* b */
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], ds_get(c, 0x2DEE), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 5;
    NEAR_THEN(0xEE0C, 0x561A, 0, 29);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_AX] = (uint16_t)(int16_t)(int8_t)get_r8(c, R_AH);     /* mov al, ah; cbw */
    SETFRAME(-2, c->r[R_AX]);                                     /* a */
    alu_sub(c, c->r[R_AX], 0x40, 1, 0);
    n = 6;
    if (!x86_cond(c, 0xE)) {                                      /* fold past a quarter turn */
        c->r[R_AX] = (uint16_t)alu_sub(c, 0, (uint16_t)alu_sub(c, c->r[R_AX], 0x80, 1, 0), 1, 0);
        SETFRAME(-2, c->r[R_AX]);
        n += 3;
    }
    c->r[R_BX] = x86_imul3(c, FRAME(0x0A), 0x0E);
    alu_logic(c, mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_BX] + 0x2EBE))) & 1, 0);
    n += 3;
    if (!(c->flags & F_ZF)) {                                     /* it sees the engines */
        c->r[R_AX] = x86_shift(c, 5, ds_get(c, 0xB07E), 5, 1);
        c->r[R_CX] = (uint16_t)alu_sub(c, 0x60, FRAME(-2), 1, 0);
        x86_mul16(c, c->r[R_CX]);
        c->r[R_AX] = x86_shift(c, 5, c->r[R_AX], 9, 1);
        n += 6;
    }
    c->r[R_AX] = x86_shift(c, 7, (uint16_t)alu_add(c, c->r[R_AX], 0x20, 1, 0), 1, 1);
    c->r[R_CX] = x86_shift(c, 7, FRAME(-0x0E), 1, 1);
    x86_imul16(c, c->r[R_CX]);
    c->r[R_AX] = x86_shift(c, 7, c->r[R_AX], 4, 1);
    SETFRAME(-0x0E, c->r[R_AX]);
    /* out of reach in height? */
    c->r[R_AX] = (uint16_t)alu_sub(c, FRAME(8), ds_get(c, 0x2DF4), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->icount += n + 10;
    NEAR_THEN(0xEE0C, 0x5667, 0, 24);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_AX] = x86_shift(c, 7, c->r[R_AX], 0x0A, 1);
    alu_sub(c, c->r[R_AX], FRAME(-8), 1, 0);
    n = 4;
    if (!x86_cond(c, 0x6)) { SETFRAME(-0x0E, 0); n += 1; }
    c->r[R_AX] = (uint16_t)alu_add(c, FRAME(-0x0E), ds_get(c, 0x3D8A), 1, 0);
    alu_sub(c, c->r[R_AX], 0x64, 1, 0);
    n += 4;
    if (!x86_cond(c, 0xE)) {                                      /* seen: time back to normal */
        c->icount += n;
        NEAR_THEN(0xD4F5, 0x5684, 0, 15);
        n = 0;
    }
    c->r[R_AX] = FRAME(-6);
    c->r[R_BX] = FRAME(0x0C);
    ds_put(c, c->r[R_BX], c->r[R_AX]);
    c->r[R_AX] = FRAME(-8);
    c->r[R_BX] = FRAME(0x0E);
    ds_put(c, c->r[R_BX], c->r[R_AX]);
    les_bx(c, 0x9924);
    alu_sub(c, seg_read16(c, c->seg[S_ES], (uint16_t)(c->r[R_BX] + 0x42)), 0, 1, 0);
    n += 9;
    if (!(c->flags & F_ZF)) { SETFRAME(-0x0E, x86_shift(c, 7, FRAME(-0x0E), 1, 1)); n += 1; }
    c->r[R_AX] = FRAME(-0x0E);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += n + 1 + 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x0B850, scene_hit_lookup(x, y): the scene object under the map
 * point (x, y). The world lookup 0x00D14 takes it as 32-bit world
 * coordinates (x << 5, (8000h - y) << 5, by 0x0EF68) and leaves its record
 * in [9F40]; none answers FFFFh. The record's own position, back in map
 * units (>> 5 by 0x0EF74), replaces x and y in their argument slots, and
 * the mission objects 1 .. [C0E0] - 1 (16-byte records at B2D0) are
 * searched for one at exactly that point: its index is the answer.
 * Otherwise object 0 is put there, its type [B2DC] the record's first
 * word with 100h added, [3D96] set to FFFFh if it was 0, and the answer
 * is 0. A long search stops at its head for the original to carry on. */
static int vgame_scene_hit_lookup(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 11)) return 0;
    x86_enter(c, 2, 0);
    c->r[R_AX] = FRAME(6);
    c->r[R_DX] = sign_word(c->r[R_AX]);
    c->r[R_CX] = c->r[R_AX];
    c->r[R_BX] = c->r[R_DX];
    c->r[R_AX] = 0x8000;
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_CX], 1, 0);
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_BX], 1, CF_IN);
    set_r8(c, R_CL, 5);
    c->icount += 10;
    NEAR_THEN(0xEF68, 0xB86A, 0, 5 + 1);
    cpu_push16(c, c->r[R_DX]);                                    /* (8000h - y) << 5 */
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = FRAME(4);
    c->r[R_DX] = sign_word(c->r[R_AX]);
    set_r8(c, R_CL, 5);
    c->icount += 5;
    NEAR_THEN(0xEF68, 0xB875, 0, 2 + 1);
    cpu_push16(c, c->r[R_DX]);                                    /* x << 5 */
    cpu_push16(c, c->r[R_AX]);
    c->icount += 2;
    NEAR_THEN(0x0D14, 0xB87A, 0, 4 + 4 + 1);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    ds_put(c, 0x9F40, c->r[R_AX]);
    alu_logic(c, c->r[R_AX], 1);
    c->icount += 4;
    if (c->flags & F_ZF) {                                        /* nothing there */
        c->r[R_AX] = 0xFFFF;
        x86_leave(c);
        c->icount += 3;
        near_ret(c);
        return 1;
    }
    c->r[R_BX] = c->r[R_AX];
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 4));
    c->r[R_DX] = ds_get(c, (uint16_t)(c->r[R_BX] + 6));
    set_r8(c, R_CL, 5);
    c->icount += 4;
    NEAR_THEN(0xEF74, 0xB891, 0, 5 + 1);
    SETFRAME(4, c->r[R_AX]);                                      /* the record's x */
    c->r[R_BX] = ds_get(c, 0x9F40);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 8));
    c->r[R_DX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0A));
    set_r8(c, R_CL, 5);
    c->icount += 5;
    NEAR_THEN(0xEF74, 0xB8A3, 0, 5);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, (uint16_t)alu_sub(c, c->r[R_AX], 0x8000, 1, 0), 1, 0);
    SETFRAME(6, c->r[R_AX]);                                      /* and its y */
    SETFRAME(-2, 1);
    c->icount += 5;
    for (;;) {                                                    /* 0x0B8B5 */
        if (!room(c, 17)) { c->ip = 0xB8B5; return 1; }
        c->r[R_AX] = FRAME(-2);
        alu_sub(c, ds_get(c, 0xC0E0), c->r[R_AX], 1, 0);
        c->icount += 3;
        if (x86_cond(c, 0xE)) break;
        c->r[R_BX] = x86_shift(c, 4, c->r[R_AX], 4, 1);
        c->r[R_CX] = FRAME(4);
        alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] - 0x4D30)), c->r[R_CX], 1, 0);
        c->icount += 5;
        if (c->flags & F_ZF) {
            c->r[R_CX] = FRAME(6);
            alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] - 0x4D2E)), c->r[R_CX], 1, 0);
            c->icount += 3;
            if (c->flags & F_ZF) {                                /* object AX is there */
                x86_leave(c);
                c->icount += 2;
                near_ret(c);
                return 1;
            }
        }
        SETFRAME(-2, (uint16_t)alu_inc(c, FRAME(-2), 1));
        c->icount += 1;
    }
    /* 0x0B8D7: object 0 goes there */
    c->r[R_AX] = FRAME(4);
    ds_put(c, 0xB2D0, c->r[R_AX]);
    c->r[R_AX] = FRAME(6);
    ds_put(c, 0xB2D2, c->r[R_AX]);
    c->r[R_BX] = ds_get(c, 0x9F40);
    c->r[R_AX] = ds_get(c, c->r[R_BX]);
    set_r8(c, R_AH, (uint8_t)alu_add(c, get_r8(c, R_AH), 1, 0, 0));
    ds_put(c, 0xB2DC, c->r[R_AX]);
    alu_sub(c, ds_get(c, 0x3D96), 0, 1, 0);
    unsigned n = 10;
    if (c->flags & F_ZF) { ds_put(c, 0x3D96, 0xFFFF); n += 1; }
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    x86_leave(c);
    c->icount += n + 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x0B991, camimage_target_model(obj): the model the camera image
 * shows for mission object obj (16-byte records at B2D0, the type word at
 * +0Ch). Its type unless the record's byte +6 has bit 7 (it is hit): then
 * the decoded scene word (0x0C436) indexes the per-type flag tables (the
 * table for its high byte at [0A60 + 2 * high], the byte at the low byte);
 * flag 20h keeps the type with 100h set, else the wreck - 100h plus the
 * mission's [C0D8] when the object was destroyed outright (0x0B9F6), the
 * theatre's [DED0] when not, sign-extended. SI, DI preserved. */
static int vgame_camimage_target_model(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    x86_enter(c, 0x0A, 0);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_BX] = x86_shift(c, 4, FRAME(4), 4, 1);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] - 0x4D24)));
    c->r[R_SI] = c->r[R_BX];
    c->icount += 7;
    NEAR_THEN(0xC436, 0xB9A6, 0, 20);
    c->r[R_BX] = cpu_pop16(c);
    alu_logic(c, mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_SI] - 0x4D2A))) & 0x80, 0);
    c->icount += 3;
    unsigned n;
    if (c->flags & F_ZF) {                                        /* not hit: its own type */
        c->r[R_BX] = x86_shift(c, 4, FRAME(4), 4, 1);
        c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] - 0x4D24));
        n = 3;
    } else {
        set_r8(c, R_CL, 8);
        c->r[R_BX] = c->r[R_AX];
        c->r[R_DI] = (uint16_t)alu_logic(c, c->r[R_AX] & 0xFF, 1);
        c->r[R_BX] = x86_shift(c, 7, c->r[R_BX], 8, 1);
        c->r[R_BX] = x86_shift(c, 4, c->r[R_BX], 1, 1);
        c->r[R_BX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0A60));
        set_r8(c, R_AL, mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_BX] + c->r[R_DI]))));
        alu_logic(c, get_r8(c, R_AL) & 0x20, 0);
        c->icount += 10;
        if (!(c->flags & F_ZF)) {                                 /* the type, marked */
            c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_SI] - 0x4D24));
            set_r8(c, R_AH, (uint8_t)alu_logic(c, get_r8(c, R_AH) | 1, 0));
            n = 3;
        } else {                                                  /* the wreck */
            cpu_push16(c, FRAME(4));
            c->icount += 1;
            NEAR_THEN(0xB9F6, 0xB9D5, 0, 12);
            c->r[R_BX] = cpu_pop16(c);
            alu_logic(c, c->r[R_AX], 1);
            const int outright = !(c->flags & F_ZF);
            set_r8(c, R_AL, mem_read8(c, phys(c->seg[S_DS], outright ? 0xC0D8 : 0xDED0)));
            c->r[R_AX] = (uint16_t)(int16_t)(int8_t)get_r8(c, R_AL);
            set_r8(c, R_AH, (uint8_t)alu_add(c, get_r8(c, R_AH), 1, 0, 0));
            n = 3 + (outright ? 2 : 1) + 3;
        }
    }
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += n + 4;
    near_ret(c);
    return 1;
}

/* VGAME 114A:0461 (0x11901), camimage_scale(kind, v), far: v scaled by a
 * camera image kind - 0: v/2, 1: v/2 + v/8, 2: v/2 + v/4, 3: v/2 + v/4 +
 * v/8 (each term its own arithmetic shift), others: v - written back to
 * v's argument slot and returned. */
static int vgame_camimage_scale(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 25)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = FRAME(6);
    alu_logic(c, c->r[R_AX], 1);
    unsigned n = 5;
    int kind = 0;
    if (!(c->flags & F_ZF)) {
        for (kind = 1; kind <= 3; kind++) {
            c->r[R_AX] = (uint16_t)alu_dec(c, c->r[R_AX], 1);
            n += 2;
            if (c->flags & F_ZF) break;
        }
    }
    if (kind == 0) {
        SETFRAME(8, x86_shift(c, 7, FRAME(8), 1, 1));
        n += 2;
    } else if (kind <= 3) {
        c->r[R_AX] = x86_shift(c, 7, FRAME(8), 1, 1);
        c->r[R_CX] = x86_shift(c, 7, FRAME(8), kind == 1 ? 3 : 2, 1);
        n += 4;
        if (kind == 3) {
            c->r[R_DX] = x86_shift(c, 7, FRAME(8), 3, 1);
            c->r[R_CX] = (uint16_t)alu_add(c, c->r[R_CX], c->r[R_DX], 1, 0);
            n += 3;
        }
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], c->r[R_CX], 1, 0);
        SETFRAME(8, c->r[R_AX]);
        n += kind == 1 ? 3 : 4;                                   /* add, mov, jmp (and the jump there) */
    } else n += 1;                                                /* jmp */
    c->r[R_AX] = FRAME(8);
    x86_leave(c);
    c->icount += n + 3;
    far_ret(c);
    return 1;
}

/* VGAME 0x00871, scene_cell_index(level, x, y): the terrain cell index at
 * (x, y) in the five-level map hierarchy, AX (AH 0). Level 4 cells are
 * offset by 2 (the slots are moved in place); a cell outside 0 ..
 * [0506 + 2 * level] - 1 on either axis answers 0. Level 4 reads the byte
 * map at B03C (8 wide), level 3 the one at 9F98 (16 wide); levels 2, 1
 * and 0 take their parent (level + 1 at (x >> 2, y >> 2), by recursion)
 * and read its 4x4 block - 16 bytes a parent - at 9D40, 9B38 and 9934,
 * at (y & 3) * 4 + (x & 3). Any other level leaves AX as the switch's
 * countdown left it. SI preserved. */
static int vgame_scene_cell_index(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 40)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    alu_sub(c, FRAME(4), 4, 1, 0);
    unsigned n = 5;
    if (c->flags & F_ZF) {
        SETFRAME(6, (uint16_t)alu_add(c, FRAME(6), 2, 1, 0));
        SETFRAME(8, (uint16_t)alu_add(c, FRAME(8), 2, 1, 0));
        n += 2;
    }
    /* inside the level's map? */
    int inside = 0;
    alu_sub(c, FRAME(6), 0, 1, 0);
    n += 2;
    if (!x86_cond(c, 0xC)) {
        alu_sub(c, FRAME(8), 0, 1, 0);
        n += 2;
        if (!x86_cond(c, 0xC)) {
            c->r[R_AX] = FRAME(6);
            c->r[R_BX] = x86_shift(c, 4, FRAME(4), 1, 1);
            alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x506)), c->r[R_AX], 1, 0);
            n += 5;
            if (!x86_cond(c, 0xE)) {
                c->r[R_AX] = FRAME(8);
                alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x506)), c->r[R_AX], 1, 0);
                n += 3;
                inside = x86_cond(c, 0xF);
            }
        }
    }
    if (!inside) {
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
        c->r[R_SI] = cpu_pop16(c);
        x86_leave(c);
        c->icount += n + 4;
        near_ret(c);
        return 1;
    }
    /* the switch on the level */
    c->r[R_AX] = FRAME(4);
    n += 2;
    alu_logic(c, c->r[R_AX], 1);
    n += 2;
    int level = 0;
    if (!(c->flags & F_ZF)) {
        for (level = 1; level <= 4; level++) {
            c->r[R_AX] = (uint16_t)alu_dec(c, c->r[R_AX], 1);
            n += 2;
            if (c->flags & F_ZF) break;
        }
    }
    if (level > 4) {
        c->r[R_SI] = cpu_pop16(c);
        x86_leave(c);
        c->icount += n + 3;
        near_ret(c);
        return 1;
    }
    if (level >= 2) n += 1;                                       /* jmp to the case */
    if (level == 4) {
        c->r[R_SI] = x86_shift(c, 4, FRAME(8), 3, 1);
        c->r[R_BX] = FRAME(6);
        set_r8(c, R_AL, mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_BX] + c->r[R_SI] - 0x4FC4))));
        n += 4;
    } else if (level == 3) {
        c->r[R_SI] = x86_shift(c, 4, FRAME(8), 4, 1);
        c->r[R_BX] = FRAME(6);
        set_r8(c, R_AL, mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_BX] + c->r[R_SI] - 0x6068))));
        n += 5;
    } else {                                                      /* a 4x4 block of the parent cell */
        static const uint16_t call_at[3] = { 0x0946, 0x0914, 0x08E2 }, block[3] = { 0x9934, 0x9B38, 0x9D40 };
        c->r[R_AX] = x86_shift(c, 7, FRAME(8), 2, 1);
        cpu_push16(c, c->r[R_AX]);
        c->r[R_AX] = x86_shift(c, 7, FRAME(6), 2, 1);
        cpu_push16(c, c->r[R_AX]);
        cpu_push16(c, (uint16_t)(level + 1));
        c->icount += n + 7;
        NEAR_THEN(0x0871, (uint16_t)(call_at[level] + 3), 0, 11 + 4);
        c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
        c->r[R_SI] = x86_shift(c, 4, c->r[R_AX], 4, 1);
        set_r8(c, R_AL, mem_read8(c, phys(c->seg[S_SS], (uint16_t)(c->r[R_BP] + 8))));
        c->r[R_AX] = (uint16_t)alu_logic(c, c->r[R_AX] & 3, 1);
        c->r[R_AX] = x86_shift(c, 4, c->r[R_AX], 2, 1);
        c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], c->r[R_AX], 1, 0);
        set_r8(c, R_BL, mem_read8(c, phys(c->seg[S_SS], (uint16_t)(c->r[R_BP] + 6))));
        c->r[R_BX] = (uint16_t)alu_logic(c, c->r[R_BX] & 3, 1);
        set_r8(c, R_AL, mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_BX] + c->r[R_SI] + block[level]))));
        n = 11;
    }
    set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += n + 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x093B2, clock_field(v): v mod 60 (signed) appended to the text
 * at 98A6 as decimal (the runtime's itoa 0x0EB9E into DED6 and strcat
 * 0x0EB10), with a leading "0" (the string at 4141) below 10. */
static int vgame_clock_field(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 9 + 3 + 1)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_CX] = 0x3C;
    c->r[R_AX] = FRAME(4);
    c->r[R_DX] = sign_word(c->r[R_AX]);
    x86_idiv16(c, 0x3C, 0);                                       /* by 60: it cannot fault */
    SETFRAME(4, c->r[R_DX]);
    alu_sub(c, c->r[R_DX], 0x0A, 1, 0);
    c->icount += 9;
    if (x86_cond(c, 0xC)) {                                       /* one digit: pad it */
        cpu_push16(c, 0x4141);
        cpu_push16(c, 0x98A6);
        c->icount += 2;
        NEAR_THEN(0xEB10, 0x93CF, 0, 2 + 3 + 1);
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_BX] = cpu_pop16(c);
        c->icount += 2;
    }
    cpu_push16(c, 0x0A);
    cpu_push16(c, 0xDED6);
    cpu_push16(c, FRAME(4));
    c->icount += 3;
    NEAR_THEN(0xEB9E, 0x93DC, 0, 3 + 1);
    c->r[R_SP] = c->r[R_BP];                                      /* mov sp, bp */
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, 0x98A6);
    c->icount += 3;
    NEAR_THEN(0xEB10, 0x93E5, 0, 4);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x092CD and 0x09335, the clock texts at 98A6 for a time t in
 * ticks (2 a second) plus the clock offset [98A0], written back to t's
 * slot: the hours (t / 1800, clock_field) after the string at `start`,
 * the hour digits shifted into the theatre's zone - [368A] added to the
 * tens, 8 (6 when [368A] is 0) to the units, carrying past '9' - then
 * `sep` and the minutes (t / 30), and for the deadline a second separator
 * (413F) and the seconds (2t). The two share their code but for 68h
 * bytes. */
static int clock_text(machine_t *m, uint16_t start, uint16_t sep, int seconds)
{
    cpu_t *c = &m->cpu;
    const uint16_t o = seconds ? 0x68 : 0;
    if (!room(c, 7)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = ds_get(c, 0x98A0);
    SETFRAME(4, (uint16_t)alu_add(c, FRAME(4), c->r[R_AX], 1, 0));
    cpu_push16(c, start);
    cpu_push16(c, 0x98A6);
    c->icount += 6;
    NEAR_THEN(0xEB50, (uint16_t)(0x92DF + o), 0, 7 + 1);
    static const uint16_t unit[2] = { 0x708, 0x1E };
    for (int k = 0; k < 2; k++) {                                 /* hours, then minutes */
        c->r[R_BX] = cpu_pop16(c);                                /* the copy's or the append's arguments */
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_AX] = FRAME(4);
        c->r[R_CX] = unit[k];
        c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
        x86_div16(c, unit[k]);                                    /* DX = 0: it cannot fault */
        cpu_push16(c, c->r[R_AX]);
        c->icount += 7;
        const uint16_t ret = (uint16_t)((k ? 0x9332 : 0x92EF) + o);
        if (k == 0) {
            NEAR_THEN(0x93B2, ret, 0, 10 + 4 + 2 + 1);
            c->r[R_BX] = cpu_pop16(c);
            /* the hour digits into the theatre's zone */
            const uint16_t ds = c->seg[S_DS];
            const uint8_t zone = mem_read8(c, phys(ds, 0x368A));
            set_r8(c, R_AL, zone);
            mem_write8(c, phys(ds, 0x98A6), (uint8_t)alu_add(c, mem_read8(c, phys(ds, 0x98A6)), zone, 0, 0));
            alu_sub(c, ds_get(c, 0x368A), 1, 1, 0);
            uint8_t al = (uint8_t)alu_sub(c, get_r8(c, R_AL), get_r8(c, R_AL), 0, CF_IN);   /* sbb al, al */
            al = (uint8_t)alu_logic(c, al & 0xFE, 0);
            al = (uint8_t)alu_add(c, al, 8, 0, 0);
            set_r8(c, R_AL, al);
            mem_write8(c, phys(ds, 0x98A7), (uint8_t)alu_add(c, mem_read8(c, phys(ds, 0x98A7)), al, 0, 0));
            alu_sub(c, mem_read8(c, phys(ds, 0x98A7)), 0x39, 0, 0);
            unsigned n = 10;
            if (!x86_cond(c, 0xE)) {                              /* past '9': carry into the tens */
                set_r8(c, R_AL, (uint8_t)alu_sub(c, mem_read8(c, phys(ds, 0x98A7)), 0x0A, 0, 0));
                mem_write8(c, phys(ds, 0x98A7), get_r8(c, R_AL));
                mem_write8(c, phys(ds, 0x98A6), (uint8_t)alu_inc(c, mem_read8(c, phys(ds, 0x98A6)), 0));
                n += 4;
            }
            cpu_push16(c, sep);
            cpu_push16(c, 0x98A6);
            c->icount += n + 2;
            NEAR_THEN(0xEB10, (uint16_t)(0x9322 + o), 0, 7 + 1);
        } else {
            NEAR_THEN(0x93B2, ret, 0, seconds ? 3 + 1 : 3);
        }
    }
    c->r[R_BX] = cpu_pop16(c);
    if (seconds) {
        cpu_push16(c, 0x413F);
        cpu_push16(c, 0x98A6);
        c->icount += 3;
        NEAR_THEN(0xEB10, 0x93A4, 0, 5 + 1);
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_AX] = x86_shift(c, 4, FRAME(4), 1, 1);
        cpu_push16(c, c->r[R_AX]);
        c->icount += 5;
        NEAR_THEN(0x93B2, 0x93AF, 0, 3);
        c->r[R_BX] = cpu_pop16(c);
        x86_leave(c);
        c->icount += 3;
    } else {
        x86_leave(c);
        c->icount += 3;
    }
    near_ret(c);
    return 1;
}
static int vgame_nav_time(machine_t *m) { return clock_text(m, 0x4139, 0x413A, 0); }
static int vgame_panel_deadline(machine_t *m) { return clock_text(m, 0x413C, 0x413D, 1); }

#undef FAR_THEN
#undef NEAR_THEN
#undef FRAME
#undef SETFRAME
#undef CF_IN

/* ---- START and END, second batch ----------------------------------------
 * The front end's text and box drawing wrappers, and the debriefing's
 * counterparts. Each reaches the graphics driver through its thunk table in
 * the data segment (a far call into the segment the game fills in), so the
 * routines run those calls as original code. */

/* The shared routines below are written for START and take the entry of their copy (END carries the
 * same code at another address): the addresses inside one are the entry plus a fixed distance. */
#define AT(o) ((uint16_t)(entry + (o)))

/* Stop at `at` (the original instruction about to run) when the stretch to
 * the next call or return does not fit before the run loop must look at
 * events: the original code then carries on from there. */
#define ST2_NEED(n, at) do { if (!room(c, (n))) { c->ip = (uint16_t)(at); return 1; } } while (0)

/* START 0x03567 and END 0x0188D, draw_text(win, s, x, y): the window record's pen (+8, +0xA)
 * is set to (x, y) and the driver's text entry draws the string s in the
 * window. BX is left as s (the two POPs take it off the stack). The caller
 * publishes the dirty rectangle with 0x03588. */
static int st2_draw_text(machine_t *m, uint16_t entry)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 10)) return 0;
    frame_open(c, 0);
    const uint16_t win = bp_get(c, 4);
    c->r[R_AX] = bp_get(c, 8);
    c->r[R_BX] = win;
    ds_put(c, (uint16_t)(win + 8), c->r[R_AX]);                   /* the pen's x */
    c->r[R_AX] = bp_get(c, 0x0A);
    ds_put(c, (uint16_t)(win + 0x0A), c->r[R_AX]);                /* the pen's y */
    cpu_push16(c, bp_get(c, 6));
    cpu_push16(c, c->r[R_BX]);
    c->icount += 9;
    if (!guest_call_far(m, AT(0x16), AT(0x1B))) return 1;            /* the driver's text entry */
    ST2_NEED(5, AT(0x1B));
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->icount += 2;
    frame_close_ret(c);
    c->icount += 3;
    return 1;
}

/* START 0x0373C and END 0x01A3B, text_width(win, s): the sum of the driver's width entry
 * for each character of the string s, in the window's font (+0xC). The
 * string pointer is stepped in the caller's frame; flags are the last test
 * for the string's end. */
static int st2_text_width(machine_t *m, uint16_t entry)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    frame_open(c, 6);
    c->r[R_BX] = bp_get(c, 4);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0C));
    bp_put(c, -4, c->r[R_AX]);                                    /* the font */
    bp_put(c, -2, 0);                                             /* the width so far */
    c->icount += 8;                                               /* through the jump to the test */
    for (;;) {                                                    /* 0x03767 */
        ST2_NEED(9, AT(0x2B));
        c->r[R_BX] = bp_get(c, 6);
        alu_sub(c, ds_get8(c, c->r[R_BX]), 0, 0, 0);
        c->icount += 3;
        if (c->flags & F_ZF) break;
        bp_put(c, 6, (uint16_t)alu_inc(c, bp_get(c, 6), 1));      /* 0x03752: the next character */
        cpu_push16(c, bp_get(c, -4));
        set_r8(c, R_AL, ds_get8(c, c->r[R_BX]));
        set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));   /* sub ah, ah */
        cpu_push16(c, c->r[R_AX]);
        c->icount += 5;
        if (!guest_call_far(m, AT(0x21), AT(0x26))) return 1;         /* the driver's character width */
        ST2_NEED(3, AT(0x26));
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_BX] = cpu_pop16(c);
        bp_put(c, -2, (uint16_t)alu_add(c, bp_get(c, -2), c->r[R_AX], 1, 0));
        c->icount += 3;
    }
    c->r[R_AX] = bp_get(c, -2);
    c->icount += 1;
    frame_close_ret(c);
    c->icount += 3;
    return 1;
}

/* START 0x02A84 and END 0x010FA, draw_text_centred(win, s, cx, y): draw_text with
 * x = cx less half the string's width (text_width, halved arithmetically). */
static int st2_draw_text_centred(machine_t *m, uint16_t entry, uint16_t width_fn)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;
    frame_open(c, 0);
    cpu_push16(c, bp_get(c, 6));
    cpu_push16(c, bp_get(c, 4));
    c->icount += 4;
    if (!guest_call(m, width_fn, AT(0x0C))) return 1;
    ST2_NEED(12, AT(0x0C));
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_AX] = x86_shift(c, 7, c->r[R_AX], 1, 1);               /* sar ax, 1 */
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], bp_get(c, 8), 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);       /* neg ax */
    c->r[R_BX] = bp_get(c, 4);
    ds_put(c, (uint16_t)(c->r[R_BX] + 8), c->r[R_AX]);
    c->r[R_AX] = bp_get(c, 0x0A);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x0A), c->r[R_AX]);
    cpu_push16(c, bp_get(c, 6));
    cpu_push16(c, c->r[R_BX]);
    c->icount += 11;
    if (!guest_call_far(m, AT(0x25), AT(0x2A))) return 1;
    ST2_NEED(5, AT(0x2A));
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->icount += 2;
    frame_close_ret(c);
    c->icount += 3;
    return 1;
}

/* START 0x03693 and END 0x01992, draw_sprite(win, x, y, handle): the driver's blit entry
 * with the sprite's first two words (its size) pushed from the start of the
 * segment the handle names. ES:BX is left at that segment, offset 0. */
static int st2_draw_sprite(machine_t *m, uint16_t entry)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 15)) return 0;
    frame_open(c, 4);
    c->r[R_AX] = bp_get(c, 0x0A);
    bp_put(c, -2, c->r[R_AX]);
    bp_put(c, -4, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, -4));
    c->r[R_BX] = bp_get(c, -4);                                   /* les bx, [bp-4] */
    c->seg[S_ES] = bp_get(c, -2);
    cpu_push16(c, seg_read16(c, c->seg[S_ES], (uint16_t)(c->r[R_BX] + 2)));
    cpu_push16(c, seg_read16(c, c->seg[S_ES], c->r[R_BX]));
    cpu_push16(c, bp_get(c, 8));
    cpu_push16(c, bp_get(c, 6));
    cpu_push16(c, bp_get(c, 4));
    c->icount += 14;
    if (!guest_call_far(m, AT(0x28), AT(0x2D))) return 1;
    ST2_NEED(3, AT(0x2D));
    frame_close_ret(c);
    c->icount += 3;
    return 1;
}

/* START 0x03989, restore the backdrop: the driver's page-to-page copy
 * (entry 1) of the pointer's last rectangle, at ([E08E], [E090]) and sized
 * [E092] by [E093], with the origin pushed twice. Flags from the ADD SP that
 * takes the arguments off. */
static int start_restore_backdrop(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 14)) return 0;
    set_r8(c, R_AL, ds_get8(c, 0xE093));
    set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
    cpu_push16(c, c->r[R_AX]);
    set_r8(c, R_AL, ds_get8(c, 0xE092));
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, ds_get(c, 0xE090));
    cpu_push16(c, ds_get(c, 0xE08E));
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, ds_get(c, 0xE090));
    cpu_push16(c, ds_get(c, 0xE08E));
    c->r[R_AX] = 1;
    cpu_push16(c, c->r[R_AX]);
    c->icount += 13;
    if (!guest_call_far(m, 0x39AA, 0x39AF)) return 1;
    ST2_NEED(2, 0x39AF);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x10, 1, 0);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* START 0x02AB4, dismiss_modal(desc, x, y): puts back the backdrop 0x0332E
 * stashed under a dialog, on both pages: the driver's page copy (entry 2)
 * with the descriptor's size (+6, +8; the height 4 more) at (x, y), once
 * with the flag 1 and once with 0. */
static int start_dismiss_modal(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 17)) return 0;
    frame_open(c, 0);
    c->r[R_BX] = bp_get(c, 4);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 8)));
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 6)));
    cpu_push16(c, bp_get(c, 8));
    cpu_push16(c, bp_get(c, 6));
    c->r[R_AX] = 1;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 8));
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 4, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 2;
    cpu_push16(c, c->r[R_CX]);
    c->icount += 16;
    if (!guest_call_far(m, 0x2AD8, 0x2ADD)) return 1;
    ST2_NEED(15, 0x2ADD);
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BX] = bp_get(c, 4);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 8)));
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 6)));
    cpu_push16(c, bp_get(c, 8));
    cpu_push16(c, bp_get(c, 6));
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = ds_get(c, (uint16_t)(c->r[R_BX] + 8));
    c->r[R_CX] = (uint16_t)alu_add(c, c->r[R_CX], 4, 1, 0);
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 2;
    cpu_push16(c, c->r[R_AX]);
    c->icount += 14;
    if (!guest_call_far(m, 0x2AFD, 0x2B02)) return 1;
    ST2_NEED(3, 0x2B02);
    frame_close_ret(c);
    c->icount += 3;
    return 1;
}

/* START 0x03588, publish what the last text draw touched: the driver's page
 * copy (entry 2) twice, flag 1 then 0, over the rectangle the driver left in
 * [B2F6], [B2F8], [B2FC], [B302]. The source origin is (0, y) with y [B2FC]
 * and the height 4 when the rectangle's bottom [B302] is below 63h, else 0
 * with [B302] + 4 as the height. Flags from the ADD SP of the first copy on
 * the way out. */
static int start_publish_text(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 20)) return 0;
    frame_open(c, 4);
    alu_sub(c, ds_get(c, 0xB302), 0x62, 1, 0);
    if (!x86_cond(c, 0xE)) {                                      /* jle not taken: the bottom is past 62h */
        c->r[R_AX] = ds_get(c, 0xB2FC);
        bp_put(c, -2, c->r[R_AX]);
        bp_put(c, -4, 4);
    } else {
        bp_put(c, -2, 0);
        c->r[R_AX] = ds_get(c, 0xB302);
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 4, 1, 0);
        bp_put(c, -4, c->r[R_AX]);
    }
    cpu_push16(c, ds_get(c, 0xB302));
    cpu_push16(c, ds_get(c, 0xB2FC));
    cpu_push16(c, ds_get(c, 0xB2F8));
    cpu_push16(c, ds_get(c, 0xB2F6));
    c->r[R_AX] = 1;
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, -4));
    cpu_push16(c, bp_get(c, -2));
    c->r[R_AX] = 2;
    cpu_push16(c, c->r[R_AX]);
    c->icount += 19;
    if (!guest_call_far(m, 0x35CE, 0x35D3)) return 1;
    ST2_NEED(12, 0x35D3);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x10, 1, 0);
    cpu_push16(c, ds_get(c, 0xB302));
    cpu_push16(c, ds_get(c, 0xB2FC));
    cpu_push16(c, ds_get(c, 0xB2F8));
    cpu_push16(c, ds_get(c, 0xB2F6));
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, -4));
    cpu_push16(c, bp_get(c, -2));
    c->r[R_AX] = 2;
    cpu_push16(c, c->r[R_AX]);
    c->icount += 11;
    if (!guest_call_far(m, 0x35F3, 0x35F8)) return 1;
    ST2_NEED(3, 0x35F8);
    frame_close_ret(c);
    c->icount += 3;
    return 1;
}

/* START 0x036C4 and END 0x019C3 and 0x0112A (the last with the centred draw),
 * draw_text_shadowed(win, s, x, y): when the window record
 * has a shadow colour (+6 not FFFFh) the string is first drawn in that
 * colour (the record's text colour +4 replaced in a local copy) at (x+1, y),
 * (x, y+1) and (x+1, y+1); then the text itself at (x, y). SI and DI are
 * preserved. */
static int st2_draw_text_shadowed(machine_t *m, uint16_t entry, uint16_t draw_fn)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    frame_open(c, 0x16);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_BX] = bp_get(c, 4);
    alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 6)), 0xFFFF, 1, 0);
    c->icount += 8;
    if (!(c->flags & F_ZF)) {
        ST2_NEED(27, AT(0x11));
        c->r[R_DI] = (uint16_t)(c->r[R_BP] - 0x16);               /* a local copy of the record */
        c->r[R_SI] = c->r[R_BX];
        c->seg[S_ES] = c->seg[S_SS];
        c->r[R_CX] = 0x0B;
        c->icount += 5;
        c->icount += rep_string(c, STR_MOVS, 1, c->seg[S_DS], 0);
        c->r[R_AX] = bp_get(c, -0x10);
        bp_put(c, -0x12, c->r[R_AX]);                             /* the shadow colour as its text colour */
        cpu_push16(c, bp_get(c, 0x0A));
        c->r[R_AX] = (uint16_t)alu_inc(c, bp_get(c, 8), 1);
        cpu_push16(c, c->r[R_AX]);
        cpu_push16(c, bp_get(c, 6));
        c->r[R_CX] = (uint16_t)(c->r[R_BP] - 0x16);
        cpu_push16(c, c->r[R_CX]);
        c->r[R_SI] = c->r[R_AX];
        c->icount += 10;
        if (!guest_call(m, draw_fn, AT(0x37))) return 1;             /* (x+1, y) */
        ST2_NEED(10, AT(0x37));
        c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
        c->r[R_AX] = (uint16_t)alu_inc(c, bp_get(c, 0x0A), 1);
        cpu_push16(c, c->r[R_AX]);
        cpu_push16(c, bp_get(c, 8));
        cpu_push16(c, bp_get(c, 6));
        c->r[R_CX] = (uint16_t)(c->r[R_BP] - 0x16);
        cpu_push16(c, c->r[R_CX]);
        c->r[R_DI] = c->r[R_AX];
        c->icount += 9;
        if (!guest_call(m, draw_fn, AT(0x4E))) return 1;             /* (x, y+1) */
        ST2_NEED(7, AT(0x4E));
        c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
        cpu_push16(c, c->r[R_DI]);
        cpu_push16(c, c->r[R_SI]);
        cpu_push16(c, bp_get(c, 6));
        c->r[R_AX] = (uint16_t)(c->r[R_BP] - 0x16);
        cpu_push16(c, c->r[R_AX]);
        c->icount += 6;
        if (!guest_call(m, draw_fn, AT(0x5D))) return 1;             /* (x+1, y+1) */
        ST2_NEED(6, AT(0x5D));
        c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
        c->icount += 1;
    }
    ST2_NEED(5, AT(0x60));                                          /* the text itself */
    cpu_push16(c, bp_get(c, 0x0A));
    cpu_push16(c, bp_get(c, 8));
    cpu_push16(c, bp_get(c, 6));
    cpu_push16(c, bp_get(c, 4));
    c->icount += 4;
    if (!guest_call(m, draw_fn, AT(0x6F))) return 1;
    ST2_NEED(6, AT(0x6F));
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->icount += 3;
    frame_close_ret(c);
    c->icount += 3;
    return 1;
}

/* START 0x060DC, draw_theatre_name(): the selected theatre's name (the
 * string table at [9ABE] indexed by the word at +38h of the mission settings
 * [CACA]) drawn at (D4h, 7Ch) in font 4, colour 8, in the window DC18h; the
 * strip behind it (46h by 0Bh at (D4h, 79h)) is first restored from page 3
 * and afterwards published, by the driver's copy (entry 3 and entry 1). */
static int start_draw_theatre_name(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 25)) return 0;
    frame_open(c, 4);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_BX] = ds_get(c, 0xCACA);                               /* les bx, [CACA] */
    c->seg[S_ES] = ds_get(c, 0xCACC);
    c->r[R_BX] = seg_read16(c, c->seg[S_ES], (uint16_t)(c->r[R_BX] + 0x38));
    c->r[R_BX] = x86_shift(c, 4, c->r[R_BX], 1, 1);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] - 0x6542));
    bp_put(c, -2, c->r[R_AX]);                                    /* the name */
    c->r[R_AX] = 0x0B; cpu_push16(c, c->r[R_AX]);                 /* the strip: 46h by 0Bh at (D4h, 79h) */
    c->r[R_CX] = 0x46; cpu_push16(c, c->r[R_CX]);
    c->r[R_DX] = 0x79; cpu_push16(c, c->r[R_DX]);
    c->r[R_BX] = 0xD4; cpu_push16(c, c->r[R_BX]);
    c->r[R_SI] = 1;    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_BX]);
    c->r[R_DI] = 3;    cpu_push16(c, c->r[R_DI]);
    c->icount += 24;
    if (!guest_call_far(m, 0x610F, 0x6114)) return 1;             /* copy from page 3 */
    ST2_NEED(11, 0x6114);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x10, 1, 0);
    ds_put(c, 0xDC24, 4);                                         /* the window: colour 8, font 4 */
    ds_put(c, 0xDC1C, 8);
    c->r[R_AX] = 0x7C; cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0xD4; cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, -2));
    c->r[R_CX] = 0xDC18; cpu_push16(c, c->r[R_CX]);
    c->icount += 10;
    if (!guest_call(m, 0x3567, 0x6135)) return 1;
    ST2_NEED(15, 0x6135);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    c->r[R_AX] = 0x0B; cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x46; cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x79; cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 0xD4; cpu_push16(c, c->r[R_CX]);
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_SI]);
    c->icount += 14;
    if (!guest_call_far(m, 0x614E, 0x6153)) return 1;             /* publish to page 1 */
    ST2_NEED(6, 0x6153);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x10, 1, 0);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->icount += 3;
    frame_close_ret(c);
    c->icount += 3;
    return 1;
}

/* START 0x02DF2, draw_ok_button(label, cx, y, colour): a 10h-high frame (the
 * routine at 0x02CB4) in the window DC2Eh, as wide as the label (0x0373C)
 * plus 8 and centred on cx, then the label at (cx - width/2 + 4, y + 4) with
 * a shadow (0x036C4). The window's flag [DC3A] is set to 1 and its text
 * colour [DC32] to `colour`. Returns the frame's width. */
static int start_draw_ok_button(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 10)) return 0;
    frame_open(c, 2);
    ds_put(c, 0xDC3A, 1);
    c->r[R_AX] = 0x10; cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, 4));
    c->r[R_AX] = 0xDC2E; cpu_push16(c, c->r[R_AX]);
    c->icount += 9;
    if (!guest_call(m, 0x373C, 0x2E0C)) return 1;
    ST2_NEED(13, 0x2E0C);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 8, 1, 0);
    bp_put(c, -2, c->r[R_AX]);                                    /* the frame's width */
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, 8));
    c->r[R_AX] = x86_shift(c, 7, c->r[R_AX], 1, 1);
    bp_put(c, 6, (uint16_t)alu_sub(c, bp_get(c, 6), c->r[R_AX], 1, 0));
    c->r[R_AX] = bp_get(c, 6);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 0xDC2E; cpu_push16(c, c->r[R_CX]);
    c->icount += 12;
    if (!guest_call(m, 0x2CB4, 0x2E28)) return 1;
    ST2_NEED(13, 0x2E28);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0A, 1, 0);
    c->r[R_AX] = bp_get(c, 0x0A);
    ds_put(c, 0xDC32, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_add(c, bp_get(c, 8), 4, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_add(c, bp_get(c, 6), 4, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, 4));
    c->r[R_AX] = 0xDC2E; cpu_push16(c, c->r[R_AX]);
    c->icount += 12;
    if (!guest_call(m, 0x36C4, 0x2E49)) return 1;
    ST2_NEED(4, 0x2E49);
    c->r[R_AX] = bp_get(c, -2);
    c->icount += 1;
    frame_close_ret(c);
    c->icount += 3;
    return 1;
}

/* START 0x00D65, draw_station(station, weapon): the arming page's frame for one
 * weapon station and the weapon in it. The destination is two words of the
 * tables at 01D6h (x) and 01E6h (y), indexed by the mission settings' layout
 * ([CACA] + 42h, four stations each) and the station. The driver's rectangle
 * copy (0A95:6A8A) takes the 37h by 24h frame from the sheet at (D2h + (station & 1)
 * * 37h, (station & FEh) * 12h), then the 33h by 20h weapon picture at (weapon / 6
 * * 33h, weapon % 6 * 20h) two pixels inside it, and last shows the station's
 * rectangle on the screen page. Neither division can fault (DX is 0). */
static int start_draw_station_weapon(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 35)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)alu_sub(c, c->r[R_SP], 0x0008, 1, 0);
    { const uint16_t off_ = ds_get(c, 0xCACA), seg_ = ds_get(c, 0xCACC); c->r[R_BX] = off_; c->seg[S_ES] = seg_; }
    c->r[R_BX] = seg_read16(c, c->seg[S_ES], (uint16_t)(c->r[R_BX] + 0x0042));
    c->r[R_BX] = x86_shift(c, 4, c->r[R_BX], 1, 1);
    c->r[R_BX] = x86_shift(c, 4, c->r[R_BX], 1, 1);
    c->r[R_BX] = (uint16_t)alu_add(c, c->r[R_BX], bp_get(c, 0x4), 1, 0);
    c->r[R_BX] = x86_shift(c, 4, c->r[R_BX], 1, 1);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x01D6));
    bp_put(c, -0x6, c->r[R_AX]);
    c->r[R_CX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x01E6));
    bp_put(c, -0x8, c->r[R_CX]);
    c->r[R_DX] = 0x0024;
    cpu_push16(c, c->r[R_DX]);
    c->r[R_BX] = 0x0037;
    cpu_push16(c, c->r[R_BX]);
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0001;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = bp_get(c, 0x4);
    set_r8(c, R_AL, (uint8_t)alu_logic(c, (get_r8(c, R_AL)) & (0xFE), 0));
    c->r[R_CX] = 0x0012;
    x86_mul16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_AX]);
    set_r8(c, R_AL, bp_get8(c, 0x4));
    set_r8(c, R_AL, (uint8_t)alu_logic(c, (get_r8(c, R_AL)) & (0x01), 0));
    set_r8(c, R_CL, 0x37);
    x86_mul8(c, get_r8(c, R_CL));
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x00D2, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0002;
    cpu_push16(c, c->r[R_AX]);
    c->icount += 34;
    if (!guest_call_far(m, 0x0DB4, 0x0DB9)) return 1;
    ST2_NEED(32, 0x0DB9);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0010, 1, 0);
    c->r[R_AX] = bp_get(c, 0x6);
    c->r[R_CX] = 0x0006;
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, c->r[R_CX]);                                     /* DX is 0 and the divisor 6: no fault */
    set_r8(c, R_CL, 0x05);
    c->r[R_DX] = x86_shift(c, 4, c->r[R_DX], get_r8(c, R_CL), 1);
    c->r[R_AX] = 0x0020;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0033;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = bp_get(c, -0x8);
    c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
    c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = bp_get(c, -0x6);
    c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
    c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0001;
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_DX]);
    c->r[R_AX] = bp_get(c, 0x6);
    c->r[R_CX] = 0x0006;
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, c->r[R_CX]);
    c->r[R_CX] = 0x0033;
    x86_mul16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0002;
    cpu_push16(c, c->r[R_AX]);
    c->icount += 31;
    if (!guest_call_far(m, 0x0DF7, 0x0DFC)) return 1;
    ST2_NEED(14, 0x0DFC);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0010, 1, 0);
    c->r[R_AX] = 0x0024;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0037;
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, -0x8));
    cpu_push16(c, bp_get(c, -0x6));
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, -0x8));
    cpu_push16(c, bp_get(c, -0x6));
    c->r[R_AX] = 0x0001;
    cpu_push16(c, c->r[R_AX]);
    c->icount += 13;
    if (!guest_call_far(m, 0x0E1A, 0x0E1F)) return 1;
    ST2_NEED(3, 0x0E1F);
    c->icount += 3;
    frame_close_ret(c);
    return 1;
}


/* START 0x02CB4 and END 0x01332, draw_box_frame(win, x, y, w, h): a dialog's frame. The driver's
 * rectangle copy (0A95:6A8A) puts four 2x2 corner tiles from sheet 0 at the box's
 * corners, then the routine at 0x08A32 fills five bars in the colour it writes
 * into the window's +4 before each: E0h across the top, E6h the bottom, E1h the
 * left edge, E5h the right edge and E2h the interior. SI and DI are preserved. */
static int st2_draw_box_frame(machine_t *m, uint16_t entry, uint16_t fill_fn)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 17)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)alu_sub(c, c->r[R_SP], 0x000C, 1, 0);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_AX] = 0x0002;
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, 0x8));
    cpu_push16(c, bp_get(c, 0x6));
    c->r[R_BX] = bp_get(c, 0x4);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX])));
    c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], c->r[R_CX], 1, 0);
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 16;
    if (!guest_call_far(m, AT(0x1D), AT(0x22))) return 1;
    ST2_NEED(19, AT(0x22));
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0010, 1, 0);
    c->r[R_AX] = 0x0002;
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, 0x8));
    c->r[R_CX] = bp_get(c, 0x6);
    c->r[R_CX] = (uint16_t)alu_add(c, c->r[R_CX], bp_get(c, 0xA), 1, 0);
    c->r[R_DX] = c->r[R_CX];
    c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_CX]);
    c->r[R_BX] = bp_get(c, 0x4);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX])));
    c->r[R_SI] = (uint16_t)alu_sub(c, c->r[R_SI], c->r[R_SI], 1, 0);
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_DI] = c->r[R_CX];
    c->r[R_SI] = c->r[R_DX];
    c->icount += 18;
    if (!guest_call_far(m, AT(0x46), AT(0x4B))) return 1;
    ST2_NEED(20, AT(0x4B));
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0010, 1, 0);
    c->r[R_AX] = 0x0002;
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = bp_get(c, 0xC);
    c->r[R_CX] = (uint16_t)alu_add(c, c->r[R_CX], bp_get(c, 0x8), 1, 0);
    c->r[R_DX] = c->r[R_CX];
    c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, bp_get(c, 0x6));
    c->r[R_BX] = bp_get(c, 0x4);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX])));
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0002;
    cpu_push16(c, c->r[R_AX]);
    bp_put(c, -0x2, c->r[R_CX]);
    bp_put(c, -0x4, c->r[R_DX]);
    c->icount += 19;
    if (!guest_call_far(m, AT(0x74), AT(0x79))) return 1;
    ST2_NEED(12, AT(0x79));
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0010, 1, 0);
    c->r[R_AX] = 0x0002;
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, -0x2));
    cpu_push16(c, c->r[R_DI]);
    c->r[R_BX] = bp_get(c, 0x4);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX])));
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 11;
    if (!guest_call_far(m, AT(0x8D), AT(0x92))) return 1;
    ST2_NEED(17, AT(0x92));
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0010, 1, 0);
    c->r[R_BX] = bp_get(c, 0x4);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x0004), 0x00E0);
    c->r[R_AX] = bp_get(c, 0x8);
    c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)(c->r[R_SI] + 0xFFFD);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, 0x8));
    c->r[R_CX] = bp_get(c, 0x6);
    c->r[R_CX] = (uint16_t)alu_inc(c, c->r[R_CX], 1);
    c->r[R_CX] = (uint16_t)alu_inc(c, c->r[R_CX], 1);
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_BX]);
    bp_put(c, -0x6, c->r[R_AX]);
    bp_put(c, -0x8, c->r[R_CX]);
    c->icount += 16;
    if (!guest_call(m, fill_fn, AT(0xB9))) return 1;
    ST2_NEED(11, AT(0xB9));
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x000A, 1, 0);
    c->r[R_BX] = bp_get(c, 0x4);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x0004), 0x00E6);
    c->r[R_AX] = bp_get(c, -0x4);
    c->r[R_AX] = (uint16_t)alu_dec(c, c->r[R_AX], 1);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, -0x6));
    cpu_push16(c, bp_get(c, -0x2));
    cpu_push16(c, bp_get(c, -0x8));
    cpu_push16(c, c->r[R_BX]);
    c->icount += 10;
    if (!guest_call(m, fill_fn, AT(0xD6))) return 1;
    ST2_NEED(18, AT(0xD6));
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x000A, 1, 0);
    c->r[R_BX] = bp_get(c, 0x4);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x0004), 0x00E1);
    c->r[R_AX] = bp_get(c, -0x4);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], 0x0003, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = bp_get(c, 0x6);
    c->r[R_CX] = (uint16_t)alu_inc(c, c->r[R_CX], 1);
    cpu_push16(c, c->r[R_CX]);
    c->r[R_CX] = bp_get(c, 0x8);
    c->r[R_CX] = (uint16_t)alu_inc(c, c->r[R_CX], 1);
    c->r[R_CX] = (uint16_t)alu_inc(c, c->r[R_CX], 1);
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, bp_get(c, 0x6));
    cpu_push16(c, c->r[R_BX]);
    bp_put(c, -0xA, c->r[R_AX]);
    bp_put(c, -0xC, c->r[R_CX]);
    c->icount += 17;
    if (!guest_call(m, fill_fn, AT(0x100))) return 1;
    ST2_NEED(10, AT(0x100));
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x000A, 1, 0);
    c->r[R_BX] = bp_get(c, 0x4);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x0004), 0x00E5);
    cpu_push16(c, bp_get(c, -0xA));
    c->r[R_AX] = (uint16_t)(c->r[R_SI] + 0xFFFF);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, -0xC));
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_BX]);
    c->icount += 9;
    if (!guest_call(m, fill_fn, AT(0x11A))) return 1;
    ST2_NEED(9, AT(0x11A));
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x000A, 1, 0);
    c->r[R_BX] = bp_get(c, 0x4);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x0004), 0x00E2);
    cpu_push16(c, bp_get(c, -0xA));
    cpu_push16(c, bp_get(c, -0x6));
    cpu_push16(c, bp_get(c, -0xC));
    cpu_push16(c, bp_get(c, -0x8));
    cpu_push16(c, c->r[R_BX]);
    c->icount += 8;
    if (!guest_call(m, fill_fn, AT(0x135))) return 1;
    ST2_NEED(6, AT(0x135));
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x000A, 1, 0);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->icount += 6;
    frame_close_ret(c);
    return 1;
}


/* START 0x08A32 and END 0x049D2, fill_rect(win, x0, y0, x1, y1): a filled rectangle, inclusive,
 * by the driver's span entries: after the driver's begin entry (whose answer is
 * kept and handed back at the end), the window's page and colour (+0, +4) are
 * set and the span tables cleared (0x08AA0); the rows y0..y1 get x0 in the left
 * table (8F86h) and x1 in the right (913Eh) with the first and last row at
 * 8F82h and 8F84h, and the driver draws the spans. A row count of 0 fills
 * nothing. */
static int st2_fill_rect(machine_t *m, uint16_t entry, uint16_t clear_fn, uint16_t first, uint16_t last, uint16_t left, uint16_t right)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_BP]);
    cpu_push16(c, c->seg[S_DS]);
    c->seg[S_ES] = cpu_pop16(c);                                  /* ES = DS */
    c->icount += 7;
    if (!guest_call_far(m, AT(0x08), AT(0x0D))) return 1;             /* driver: begin (its answer is kept) */
    ST2_NEED(4, AT(0x0D));
    cpu_push16(c, c->r[R_AX]);
    c->r[R_BX] = bp_get(c, 4);
    c->r[R_AX] = ds_get(c, c->r[R_BX]);                           /* the window's page */
    c->icount += 3;
    if (!guest_call_far(m, AT(0x13), AT(0x18))) return 1;             /* driver: the target page */
    ST2_NEED(2, AT(0x18));
    set_r8(c, R_AH, ds_get8(c, (uint16_t)(c->r[R_BX] + 4)));      /* the window's colour */
    c->icount += 1;
    if (!guest_call_far(m, AT(0x1B), AT(0x20))) return 1;             /* driver: the colour */
    ST2_NEED(1, AT(0x20));
    if (!guest_call(m, clear_fn, AT(0x23))) return 1;                 /* clear the span tables */
    /* The rows y0..y1 (inclusive; a count of 0 is a 64K-row run) take x0 in the left
     * table and x1 in the right, two word fills of the same length. */
    const uint16_t y0 = bp_get(c, 8), y1 = bp_get(c, 0x0C);
    const uint16_t rows = (uint16_t)(y1 - y0 + 1);
    const unsigned r = rows ? rows : 1u;
    ST2_NEED(20 + 2 * r, AT(0x23));
    c->r[R_CX] = y1;
    c->r[R_SI] = y0;
    ds_put(c, last, c->r[R_CX]);                                /* the table's last row */
    ds_put(c, first, c->r[R_SI]);                                /* and first */
    c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], c->r[R_SI], 1, 0);
    c->r[R_CX] = (uint16_t)alu_inc(c, c->r[R_CX], 1);
    c->r[R_DI] = left;                                          /* the left table */
    c->r[R_SI] = x86_shift(c, 4, c->r[R_SI], 1, 1);
    c->r[R_DI] = (uint16_t)alu_add(c, c->r[R_DI], c->r[R_SI], 1, 0);
    c->r[R_AX] = bp_get(c, 6);                                    /* x0 */
    c->r[R_DX] = c->r[R_CX];
    c->icount += 11;
    c->icount += rep_string(c, STR_STOS, 1, 0, 0);
    c->r[R_CX] = c->r[R_DX];
    c->r[R_DI] = right;                                          /* the right table */
    c->r[R_DI] = (uint16_t)alu_add(c, c->r[R_DI], c->r[R_SI], 1, 0);
    c->r[R_AX] = bp_get(c, 0x0A);                                 /* x1 */
    c->icount += 4;
    c->icount += rep_string(c, STR_STOS, 1, 0, 0);
    c->r[R_BX] = left;
    c->r[R_AX] = ds_get(c, first);
    c->r[R_CX] = ds_get(c, last);
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    c->icount += 4;
    if (!guest_call_far(m, AT(0x5C), AT(0x61))) return 1;             /* driver: fill the spans */
    ST2_NEED(2, AT(0x61));
    c->r[R_AX] = cpu_pop16(c);
    c->icount += 1;
    if (!guest_call_far(m, AT(0x62), AT(0x67))) return 1;             /* driver: end */
    ST2_NEED(6, AT(0x67));
    c->r[R_BP] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->icount += 3;
    frame_close_ret(c);
    c->icount += 3;
    return 1;
}

/* START 0x00E96, draw_load_caption(): the fuel figure under the arming page's
 * weapon stations. The 2Ch by 5 box at (76h, 4Dh) is cleared (0x08A32) and a
 * total started at 10000 pounds after an accepted mission ([D2A2] set), else 0;
 * 76Ch pounds more are added for each of the four words at the flight record's
 * +38h ([E096]) equal to 11h (a fuel tank). "%d lbs." is formatted (0x0959E)
 * and drawn at (76h, 4Dh) in colour F, font 3, and the strip published. */
static int start_draw_load_caption(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 16)) return 0;
    frame_open(c, 0x18);
    cpu_push16(c, c->r[R_SI]);
    ds_put(c, 0xDC1C, 0x42);                                      /* the window's colour */
    c->r[R_AX] = 0x51; cpu_push16(c, c->r[R_AX]);                 /* the 2Ch by 5 box at (76h, 4Dh) */
    c->r[R_AX] = 0xA1; cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x4D; cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x76; cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0xDC18; cpu_push16(c, c->r[R_AX]);
    c->icount += 15;
    if (!guest_call(m, 0x8A32, 0x0EBA)) return 1;                 /* clear it */
    ST2_NEED(6, 0x0EBA);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0A, 1, 0);
    alu_sub(c, ds_get8(c, 0xD2A2), 0, 0, 0);                      /* an accepted mission: start at 10000 */
    c->icount += 3;
    if (!(c->flags & F_ZF)) { bp_put(c, -0x18, 0x2710); c->icount += 2; }
    else { bp_put(c, -0x18, 0); c->icount += 1; }
    bp_put(c, -0x16, 0);                                          /* the station */
    c->icount += 1;
    for (;;) {                                                    /* 0x0ED5: +76Ch for each of the four stations at 11h */
        ST2_NEED(10, 0x0ED5);
        c->r[R_SI] = bp_get(c, -0x16);
        c->r[R_SI] = x86_shift(c, 4, c->r[R_SI], 1, 1);
        c->r[R_BX] = ds_get(c, 0xE096);                           /* les bx, [E096] */
        c->seg[S_ES] = ds_get(c, 0xE098);
        c->r[R_BX] = (uint16_t)alu_add(c, c->r[R_BX], 0x38, 1, 0);
        alu_sub(c, seg_read16(c, c->seg[S_ES], (uint16_t)(c->r[R_BX] + c->r[R_SI])), 0x11, 1, 0);
        c->icount += 6;
        if (c->flags & F_ZF) {
            bp_put(c, -0x18, (uint16_t)alu_add(c, bp_get(c, -0x18), 0x76C, 1, 0));
            c->icount += 1;
        }
        bp_put(c, -0x16, (uint16_t)alu_inc(c, bp_get(c, -0x16), 1));
        alu_sub(c, bp_get(c, -0x16), 4, 1, 0);
        c->icount += 3;
        if (!(c->flags & F_CF)) break;                            /* jb loops */
    }
    ST2_NEED(8, 0x0EF5);
    ds_put(c, 0xDC1C, 0x0F);
    ds_put(c, 0xDC24, 0x03);
    cpu_push16(c, bp_get(c, -0x18));
    c->r[R_AX] = 0x025A;                                          /* the format "%d lbs." */
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)(c->r[R_BP] - 0x14);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 7;
    if (!guest_call(m, 0x959E, 0x0F0F)) return 1;                 /* sprintf */
    ST2_NEED(10, 0x0F0F);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    c->r[R_AX] = 0x4D; cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 0x76; cpu_push16(c, c->r[R_CX]);
    c->r[R_DX] = (uint16_t)(c->r[R_BP] - 0x14); cpu_push16(c, c->r[R_DX]);
    c->r[R_DX] = 0xDC18; cpu_push16(c, c->r[R_DX]);
    c->icount += 9;
    if (!guest_call(m, 0x3567, 0x0F25)) return 1;                 /* draw it at (76h, 4Dh) */
    ST2_NEED(16, 0x0F25);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    c->r[R_AX] = 0x05; cpu_push16(c, c->r[R_AX]);                 /* publish the strip */
    c->r[R_AX] = 0x2C; cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x4D; cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 0x76; cpu_push16(c, c->r[R_CX]);
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_CX]);
    c->r[R_AX] = 1; cpu_push16(c, c->r[R_AX]);
    c->icount += 15;
    if (!guest_call_far(m, 0x0F41, 0x0F46)) return 1;
    ST2_NEED(5, 0x0F46);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x10, 1, 0);
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 2;
    frame_close_ret(c);
    c->icount += 3;
    return 1;
}

static int start_draw_text(machine_t *m) { return st2_draw_text(m, 0x3567); }
static int end_draw_text(machine_t *m) { return st2_draw_text(m, 0x188D); }
static int start_text_width(machine_t *m) { return st2_text_width(m, 0x373C); }
static int end_text_width(machine_t *m) { return st2_text_width(m, 0x1A3B); }
static int start_draw_text_centred(machine_t *m) { return st2_draw_text_centred(m, 0x2A84, 0x373C); }
static int end_draw_text_centred(machine_t *m) { return st2_draw_text_centred(m, 0x10FA, 0x1A3B); }
static int start_draw_sprite(machine_t *m) { return st2_draw_sprite(m, 0x3693); }
static int end_draw_sprite(machine_t *m) { return st2_draw_sprite(m, 0x1992); }
static int start_draw_text_shadowed(machine_t *m) { return st2_draw_text_shadowed(m, 0x36C4, 0x3567); }
static int end_draw_text_shadowed(machine_t *m) { return st2_draw_text_shadowed(m, 0x19C3, 0x188D); }
static int end_panel_text_line(machine_t *m) { return st2_draw_text_shadowed(m, 0x112A, 0x10FA); }
static int start_draw_box_frame(machine_t *m) { return st2_draw_box_frame(m, 0x2CB4, 0x8A32); }
static int end_draw_box_frame(machine_t *m) { return st2_draw_box_frame(m, 0x1332, 0x49D2); }
static int start_fill_rect(machine_t *m) { return st2_fill_rect(m, 0x8A32, 0x8AA0, 0x8F82, 0x8F84, 0x8F86, 0x913E); }
static int end_fill_rect(machine_t *m) { return st2_fill_rect(m, 0x49D2, 0x4A40, 0x4198, 0x419A, 0x419C, 0x4354); }

/* START 0x024F7, draw_target_block(win, n): the briefing's block for target n (0 the
 * primary, else the secondary), whose record is 12h bytes at E05Eh + n * 12h. The
 * window gets size 3 (+0Ch) and ink 0 (+4); the block's heading (string 07F0h at
 * (26h, 4Eh) for the primary, 0815h at (26h, 8Ch) for the secondary) is drawn with
 * 0x03567, then the paragraph for the target's kind (the record's word at +8 indexes
 * the string pointers at 1F24h) word-wrapped by 0x03776 (104h wide, lines 7 apart)
 * below it; a label (083Ch, ink 4) follows the paragraph, and the objective text
 * that 0x02651 builds from the record is wrapped under it (lines 6 apart). */
static int start_draw_target_block(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 13)) return 0;
    frame_open(c, 0x104);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_AX] = 0x12;
    x86_mul16(c, bp_get(c, 6));
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0xE05E, 1, 0);
    bp_put(c, -2, c->r[R_AX]);                                    /* the target's record */
    c->r[R_BX] = bp_get(c, 4);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x0C), 3);                  /* the window's size */
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x04), 0);                  /* and ink */
    alu_sub(c, bp_get(c, 6), 0, 1, 0);
    c->icount += 13;
    if (c->flags & F_ZF) {                                        /* the primary target: its heading at (26h, 4Eh) */
        ST2_NEED(10, 0x251E);
        c->r[R_AX] = 0x4E;
        bp_put(c, -0x104, c->r[R_AX]);
        cpu_push16(c, c->r[R_AX]);
        c->r[R_AX] = 0x26; cpu_push16(c, c->r[R_AX]);
        c->r[R_AX] = 0x07F0; cpu_push16(c, c->r[R_AX]);
        cpu_push16(c, c->r[R_BX]);
        c->icount += 9;
    } else {                                                      /* the secondary: at (26h, 8Ch) */
        ST2_NEED(9, 0x2531);
        c->r[R_AX] = 0x8C;
        bp_put(c, -0x104, c->r[R_AX]);
        cpu_push16(c, c->r[R_AX]);
        c->r[R_AX] = 0x26; cpu_push16(c, c->r[R_AX]);
        c->r[R_AX] = 0x0815; cpu_push16(c, c->r[R_AX]);
        cpu_push16(c, bp_get(c, 4));
        c->icount += 8;
    }
    if (!guest_call(m, 0x3567, 0x2547)) return 1;                 /* draw_text */
    ST2_NEED(18, 0x2547);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_AX]);                                    /* the paragraph for the target's kind: wrapped */
    c->r[R_CX] = 7; cpu_push16(c, c->r[R_CX]);                    /* 104h wide, lines 7 apart, not centred */
    c->r[R_CX] = 0x0104; cpu_push16(c, c->r[R_CX]);
    c->r[R_DX] = (uint16_t)alu_add(c, bp_get(c, -0x104), 8, 1, 0);
    cpu_push16(c, c->r[R_DX]);
    c->r[R_DX] = 0x26; cpu_push16(c, c->r[R_DX]);
    c->r[R_BX] = bp_get(c, -2);
    c->r[R_SI] = ds_get(c, (uint16_t)(c->r[R_BX] + 8));           /* the record's kind */
    c->r[R_SI] = x86_shift(c, 4, c->r[R_SI], 1, 1);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_SI] + 0x1F24)));
    cpu_push16(c, bp_get(c, 4));
    c->icount += 17;
    if (!guest_call(m, 0x3776, 0x2573)) return 1;                 /* draw_wrapped */
    ST2_NEED(14, 0x2573);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0E, 1, 0);
    c->r[R_BX] = bp_get(c, 4);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0A));        /* the pen below the paragraph, +2 */
    c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
    c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
    bp_put(c, -0x104, c->r[R_AX]);
    ds_put(c, (uint16_t)(c->r[R_BX] + 4), 4);                     /* ink 4 for the label */
    cpu_push16(c, bp_get(c, -0x104));
    c->r[R_AX] = 0x26; cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 0x083C; cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_BX]);
    c->icount += 13;
    if (!guest_call(m, 0x3567, 0x2597)) return 1;
    ST2_NEED(7, 0x2597);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    c->r[R_BX] = bp_get(c, 4);
    ds_put(c, (uint16_t)(c->r[R_BX] + 4), 0);                     /* ink 0 again */
    c->r[R_AX] = (uint16_t)(c->r[R_BP] - 0x102);                  /* the objective text goes in the local buffer */
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, -2));
    c->icount += 6;
    if (!guest_call(m, 0x2651, 0x25AD)) return 1;                 /* build the objective text */
    ST2_NEED(17, 0x25AD);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 6; cpu_push16(c, c->r[R_AX]);                    /* 104h wide, lines 6 apart */
    c->r[R_AX] = 0x0104; cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_add(c, bp_get(c, -0x104), 8, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x26; cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)(c->r[R_BP] - 0x102);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, 4));
    c->icount += 16;
    if (!guest_call(m, 0x3776, 0x25D1)) return 1;
    ST2_NEED(5, 0x25D1);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0E, 1, 0);
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 2;
    frame_close_ret(c);
    c->icount += 3;
    return 1;
}

/* START 0x03776 and END 0x01A75, draw_wrapped(win, s, x, y, w, dy, centre): word wrap.
 * The string s is cut into lines no wider than w (each character measured by the
 * driver's width entry, in the window's font +0Ch): a line ends at a line feed, at the
 * text's end or, when the next character would not fit, at the last space (or hyphen,
 * which stays on the line) before it. Each line is copied out (0x09612), measured
 * (0x0373C) and, when the window has ink (+0 not FFFFh), drawn with its shadow
 * (0x036C4) at x, or centred in w when `centre` is set, the pen y stepping down by
 * dy after every line (a line feed alone steps too). Returns the widest line's width.
 * END's copy compares the same pairs the other way round (so AX and the flags differ at
 * three tests: `swapped`). */
static int st2_draw_wrapped(machine_t *m, uint16_t entry, uint16_t copy_fn, uint16_t width_fn, uint16_t draw_fn, int swapped)
{
    cpu_t *c = &m->cpu;
#define S(x) AT((x) - 0x3776)
    /* The frame: [bp-2] the font, [bp-4] where the line starts, [bp-6] its width, [bp-8] the widest line so far,
     * [bp-0xA] the scan pointer, [bp-0xC] the end-of-text flag, [bp-0x10] the line's length, [bp-0x112] the last
     * character's address, [bp-0x110...] the line copied out. Arguments: win [bp+4], s [bp+6], x [bp+8], y [bp+0xA],
     * w [bp+0xC], dy [bp+0xE], centre [bp+0x10]. */
    if (!room(c, 21)) return 0;
    frame_open(c, 0x112);
    cpu_push16(c, c->r[R_SI]);
    bp_put(c, -8, 0);
    c->r[R_AX] = bp_get(c, 6);
    bp_put(c, -0x0A, c->r[R_AX]);
    bp_put(c, -4, c->r[R_AX]);
    c->r[R_BX] = bp_get(c, 4);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0C));
    bp_put(c, -2, c->r[R_AX]);
    c->r[R_AX] = bp_get(c, 0x0A);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x0A), c->r[R_AX]);          /* the pen's y */
    bp_put8(c, -0x0C, 0);
    c->icount += 14;
    goto new_line_body;
new_line:                                                         /* 0x0379F: a line from [bp-4] */
    ST2_NEED(7, S(0x379F));
new_line_body:
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    bp_put(c, -0x10, c->r[R_AX]);
    bp_put(c, -6, c->r[R_AX]);
    c->icount += 4;
    goto fits;
take_char:                                                        /* 0x037A9: while the line fits, take a character */
    ST2_NEED(12, S(0x37A9));
    c->r[R_BX] = bp_get(c, -0x0A);
    alu_sub(c, ds_get8(c, c->r[R_BX]), 0, 0, 0);
    c->icount += 3;
    if (c->flags & F_ZF) goto line_end;                           /* the text ends */
    alu_sub(c, ds_get8(c, c->r[R_BX]), 0x0A, 0, 0);
    c->icount += 2;
    if (c->flags & F_ZF) goto line_end;                           /* a line feed */
    bp_put(c, -0x112, c->r[R_BX]);
    bp_put(c, -0x0A, (uint16_t)alu_inc(c, bp_get(c, -0x0A), 1));
    cpu_push16(c, bp_get(c, -2));
    set_r8(c, R_AL, ds_get8(c, c->r[R_BX]));
    set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
    cpu_push16(c, c->r[R_AX]);
    c->icount += 6;
    if (!guest_call_far(m, S(0x37C5), S(0x37CA))) return 1;       /* the driver's character width */
    ST2_NEED(7, S(0x37CA));
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    bp_put(c, -6, (uint16_t)alu_add(c, bp_get(c, -6), c->r[R_AX], 1, 0));
    bp_put(c, -0x10, (uint16_t)alu_inc(c, bp_get(c, -0x10), 1));
    c->icount += 4;
fits:                                                             /* 0x037D2: the width against the limit [bp+0xC] */
    if (!swapped) {                                               /* START: CMP width, limit; JBE */
        c->r[R_AX] = bp_get(c, 0x0C);
        alu_sub(c, bp_get(c, -6), c->r[R_AX], 1, 0);
        c->icount += 3;
        if (x86_cond(c, 0x6)) goto take_char;
    } else {                                                      /* END: AX = the width; CMP limit, AX; JAE */
        c->r[R_AX] = bp_get(c, -6);
        alu_sub(c, bp_get(c, 0x0C), c->r[R_AX], 1, 0);
        c->icount += 3;
        if (x86_cond(c, 0x3)) goto take_char;
    }
line_end:                                                         /* 0x037DA: the text ends, or the line is full */
    ST2_NEED(3, S(0x37DA));
    if (!swapped) alu_sub(c, bp_get(c, -6), c->r[R_AX], 1, 0);
    else alu_sub(c, bp_get(c, 0x0C), c->r[R_AX], 1, 0);
    c->icount += 2;
    if (x86_cond(c, swapped ? 0x3 : 0x6)) goto space_check;      /* it fits */
    c->icount += 1;                                               /* too wide: back up one first */
    goto back_up;
back_scan:                                                        /* 0x037E1: back up to a space, a hyphen or the start */
    ST2_NEED(8, S(0x37E1));
    alu_sub(c, ds_get8(c, c->r[R_BX]), 0, 0, 0);
    c->icount += 2;
    if (c->flags & F_ZF) goto at_break;
    alu_sub(c, ds_get8(c, c->r[R_BX]), 0x0A, 0, 0);
    c->icount += 2;
    if (c->flags & F_ZF) goto at_break;
    alu_sub(c, ds_get8(c, c->r[R_BX]), 0x2D, 0, 0);
    c->icount += 2;
    if (c->flags & F_ZF) goto at_break;
    if (!swapped) alu_sub(c, bp_get(c, 6), c->r[R_BX], 1, 0);    /* the text's start against the scan */
    else alu_sub(c, c->r[R_BX], bp_get(c, 6), 1, 0);
    c->icount += 2;
    if (x86_cond(c, swapped ? 0x6 : 0x3)) goto at_break;
back_up:                                                          /* 0x037F5: one character back */
    ST2_NEED(5, S(0x37F5));
    bp_put(c, -0x0A, (uint16_t)alu_dec(c, bp_get(c, -0x0A), 1));
    bp_put(c, -0x10, (uint16_t)alu_dec(c, bp_get(c, -0x10), 1));
    c->icount += 2;
space_check:                                                      /* 0x037FB: stop on a space */
    ST2_NEED(3, S(0x37FB));
    c->r[R_BX] = bp_get(c, -0x0A);
    alu_sub(c, ds_get8(c, c->r[R_BX]), 0x20, 0, 0);
    c->icount += 3;
    if (!(c->flags & F_ZF)) goto back_scan;
at_break:                                                         /* 0x03803: the line is [bp-4], [bp-0x10] long */
    ST2_NEED(13, S(0x3803));
    alu_sub(c, ds_get8(c, c->r[R_BX]), 0x2D, 0, 0);
    c->icount += 2;
    if (c->flags & F_ZF) {                                        /* a hyphen stays on the line */
        bp_put(c, -0x10, (uint16_t)alu_inc(c, bp_get(c, -0x10), 1));
        c->icount += 1;
    }
    alu_sub(c, ds_get8(c, c->r[R_BX]), 0, 0, 0);
    c->icount += 2;
    if (c->flags & F_ZF) { bp_put8(c, -0x0C, 1); c->icount += 1; }   /* the text's end: this is the last line */
    alu_sub(c, bp_get(c, -0x10), 0, 1, 0);
    c->icount += 2;
    if (c->flags & F_ZF) goto empty_line;
    cpu_push16(c, bp_get(c, -0x10));                              /* copy the line out */
    cpu_push16(c, bp_get(c, -4));
    c->r[R_AX] = (uint16_t)(c->r[R_BP] - 0x110);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 4;
    if (!guest_call(m, copy_fn, S(0x3828))) return 1;
    ST2_NEED(7, S(0x3828));
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    c->r[R_SI] = bp_get(c, -0x10);
    mem_write8(c, phys(c->seg[S_SS], (uint16_t)(c->r[R_BP] + c->r[R_SI] - 0x110)), 0);   /* and end it */
    c->r[R_AX] = (uint16_t)(c->r[R_BP] - 0x110);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, 4));
    c->icount += 6;
    if (!guest_call(m, width_fn, S(0x383E))) return 1;            /* its width */
    ST2_NEED(23, S(0x383E));
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    bp_put(c, -6, c->r[R_AX]);
    alu_sub(c, c->r[R_AX], bp_get(c, -8), 1, 0);
    c->icount += 5;
    if (x86_cond(c, 0x7)) { bp_put(c, -8, c->r[R_AX]); c->icount += 1; }   /* the widest line so far */
    c->r[R_BX] = bp_get(c, 4);
    alu_sub(c, ds_get(c, c->r[R_BX]), 0xFFFF, 1, 0);
    c->icount += 3;
    if (c->flags & F_ZF) goto next_line_y;                        /* the window has no ink: measure only */
    alu_sub(c, bp_get(c, 0x10), 0, 1, 0);
    c->icount += 2;
    if (c->flags & F_ZF) {                                        /* left-aligned at x */
        c->r[R_AX] = bp_get(c, 8);
        c->icount += 2;
    } else {                                                      /* centred in the width */
        c->r[R_AX] = bp_get(c, 0x0C);
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], bp_get(c, -6), 1, 0);
        c->r[R_AX] = x86_shift(c, 5, c->r[R_AX], 1, 1);
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], bp_get(c, 8), 1, 0);
        c->icount += 4;
    }
    ds_put(c, (uint16_t)(c->r[R_BX] + 8), c->r[R_AX]);            /* the pen's x */
    c->r[R_BX] = bp_get(c, 4);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0A)));
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 8)));
    c->r[R_AX] = (uint16_t)(c->r[R_BP] - 0x110);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_BX]);
    c->icount += 7;
    if (!guest_call(m, draw_fn, S(0x387E))) return 1;             /* draw the line (shadowed) */
    ST2_NEED(15, S(0x387E));
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    c->icount += 2;                                               /* and the jump down to the next line */
    goto next_line_y;
empty_line:                                                       /* 0x03883: a line feed only */
    ST2_NEED(15, S(0x3883));
    alu_sub(c, ds_get8(c, c->r[R_BX]), 0x0A, 0, 0);
    c->icount += 2;
    if (!(c->flags & F_ZF)) goto next_start;
next_line_y:                                                      /* 0x03888: step down by dy */
    c->r[R_AX] = bp_get(c, 0x0E);
    c->r[R_BX] = bp_get(c, 4);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x0A), (uint16_t)alu_add(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0A)), c->r[R_AX], 1, 0));
    c->icount += 3;
next_start:                                                       /* 0x03891: the next line starts past the break */
    bp_put(c, -0x0A, (uint16_t)alu_inc(c, bp_get(c, -0x0A), 1));
    c->r[R_AX] = bp_get(c, -0x0A);
    bp_put(c, -4, c->r[R_AX]);
    alu_sub(c, bp_get8(c, -0x0C), 0, 0, 0);
    c->icount += 5;
    if (c->flags & F_ZF) { c->icount += 1; goto new_line; }       /* the text goes on: jmp back */
    c->r[R_AX] = bp_get(c, -8);                                   /* the widest line */
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 2;
    frame_close_ret(c);
    c->icount += 3;
    return 1;
#undef S
}

static int start_draw_wrapped(machine_t *m) { return st2_draw_wrapped(m, 0x3776, 0x9612, 0x373C, 0x36C4, 0); }
static int end_draw_wrapped(machine_t *m) { return st2_draw_wrapped(m, 0x1A75, 0x5290, 0x1A3B, 0x19C3, 1); }

/* START 0x08378 and END 0x042E4, set_dac(): load CX colours into the VGA DAC from the
 * RGB triples at ES:DX, starting at index BL, each with interrupts off (CLI, the index
 * to port 3C8h, three bytes to 3C9h, STI). It first waits for a vertical retrace to
 * begin (bit 3 of the status port 3DAh) and counts the colours written since in BP
 * (the later colours re-test the port, and pause after a retrace ends). The routine
 * goes as far as the first colour's STI: that makes the machine look at pending
 * interrupts at the next boundary (the run loop's limit is then 0), so LOOP, the other
 * colours and the return are left to the original code. When the BIOS data area's
 * gray-scale summing flag ([0489h] bit 1) is set the routine instead goes on at
 * 0x083D1 with a loop of its own, which the original code carries on (it returns
 * without popping BP). */
static int st2_set_dac(machine_t *m, uint16_t entry)
{
    cpu_t *c = &m->cpu;
#define S(x) AT((x) - 0x8378)
    /* Registers in: ES:DX the RGB triples (6 bits each), BL the first DAC index, CX how many. */
    if (!room(c, 12)) return 0;
    cpu_push16(c, c->r[R_BP]);
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->seg[S_DS]);
    c->r[R_SI] = c->seg[S_ES];
    c->seg[S_DS] = c->r[R_SI];                                    /* DS = ES: the table is read through DS:SI */
    c->r[R_SI] = c->r[R_DX];
    cpu_push16(c, c->seg[S_DS]);
    c->r[R_AX] = (uint16_t)alu_logic(c, 0, 1);
    c->seg[S_DS] = c->r[R_AX];                                    /* DS = 0: the BIOS data area */
    alu_logic(c, ds_get8(c, 0x0489) & 2, 0);                      /* test [0489h], 2: the gray-scale summing flag */
    c->seg[S_DS] = cpu_pop16(c);
    c->icount += 12;
    if (!(c->flags & F_ZF)) { c->ip = S(0x83D1); return 1; }      /* gray-scale: the original's other loop (it returns without
                                                                   * popping BP) carries on */
    /* 0x0838E: wait for the retrace. */
    ST2_NEED(1, S(0x838E));
    c->r[R_DX] = 0x03DA;                                          /* the CRT status port */
    c->icount += 1;
    for (;;) {                                                    /* 0x08391: wait for a vertical retrace to begin */
        ST2_NEED(3, S(0x8391));
        x86_in(c, c->r[R_DX], 0);
        c->icount += 1;
        alu_logic(c, (uint32_t)get_r8(c, R_AL) & 0x08, 0);
        c->icount += 2;
        if (!(c->flags & F_ZF)) break;
    }
    ST2_NEED(2, S(0x8396));
    c->r[R_BP] = 0;                                               /* colours written since the retrace began */
    c->icount += 2;
    /* 0x083A8: one colour, interrupts off. */
    ST2_NEED(17, S(0x83A8));
    x86_cli(c);
    c->icount += 1;
    set_r8(c, R_DL, 0xC8);                                        /* the DAC's write index */
    set_r8(c, R_AL, get_r8(c, R_BL));
    c->r[R_BX] = (uint16_t)alu_inc(c, c->r[R_BX], 1);
    c->icount += 3;
    x86_out(c, c->r[R_DX], 0);
    c->icount += 3;                                               /* the OUT, and the two jumps that delay */
    c->r[R_DX] = (uint16_t)alu_inc(c, c->r[R_DX], 1);             /* the data port */
    x86_lods(c, 0, c->seg[S_DS]);
    c->icount += 2;
    x86_out(c, c->r[R_DX], 0);                                    /* red */
    c->icount += 2;
    x86_lods(c, 0, c->seg[S_DS]);
    c->icount += 1;
    x86_out(c, c->r[R_DX], 0);                                    /* green */
    c->icount += 2;
    x86_lods(c, 0, c->seg[S_DS]);
    c->icount += 1;
    x86_out(c, c->r[R_DX], 0);                                    /* blue */
    c->icount += 1;
    x86_sti(c);
    c->icount += 1;
    /* STI makes the machine look at pending interrupts at the next boundary (the run loop's limit
     * is now 0), so the routine ends here: LOOP, the other colours and the return are the original's. */
    c->ip = S(0x83BF);
    return 1;
#undef S
}

static int start_set_dac(machine_t *m) { return st2_set_dac(m, 0x8378); }
static int end_set_dac(machine_t *m) { return st2_set_dac(m, 0x42E4); }

/* A box's position on one axis (START 0x02B68..0x02BD6 do it twice): with the centre flag [bp+0C] set the
 * argument at [bp+arg] is the centre, so the position is size/2 less than it; otherwise the argument is the
 * position. It is stored at out_at, and clamped to 0 below and so the box ends within `limit` above. The
 * result is left in AX; returns the instructions run. */
static unsigned st2_place_axis(cpu_t *c, uint16_t size_at, int arg, uint16_t out_at, uint16_t limit)
{
    unsigned n = 2;
    alu_sub(c, bp_get(c, 0x0C), 0, 1, 0);
    if (c->flags & F_ZF) {
        c->r[R_AX] = bp_get(c, arg);
        n += 1;
    } else {
        c->r[R_AX] = ds_get(c, size_at);
        c->r[R_AX] = x86_shift(c, 7, c->r[R_AX], 1, 1);
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], bp_get(c, arg), 1, 0);
        c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);
        n += 5;
    }
    ds_put(c, out_at, c->r[R_AX]);
    alu_logic(c, c->r[R_AX], 1);
    n += 3;
    if (c->flags & F_SF) {                                        /* off the low edge */
        ds_put(c, out_at, 0);
        n += 2;
        return n;
    }
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], ds_get(c, size_at), 1, 0);
    alu_sub(c, c->r[R_AX], limit, 1, 0);
    n += 3;
    if (x86_cond(c, 0xF)) {                                       /* past the far edge: flush against it */
        c->r[R_AX] = limit;
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], ds_get(c, size_at), 1, 0);
        ds_put(c, out_at, c->r[R_AX]);
        n += 3;
    }
    return n;
}

/* START 0x02B1B, draw_popup(s, x, y, flag, centre, w): a bevelled text box in the
 * window DC2Eh. The text s is first measured by draw_wrapped on a copy of the window
 * with no ink (w wide, lines 8 apart), which gives the widest line and the pen's end: the box is
 * those + 12 each way. It is placed with (x, y) as its corner or, when `centre` is
 * set, its centre, and kept inside the 320 by 200 screen (st2_place_axis). The
 * driver's page copy then saves the backdrop (a strip: the whole width from row 4
 * when the box is deeper than 62h, else from row 0 to its height + 4), the frame is
 * drawn (0x02CB4), the text drawn in ink 15 with shadow 8 (w - 12 wide, at (10, 6)
 * in the box) and the box shown on both pages. The window's flag [DC3A] is set
 * to `flag`. */
static int start_draw_popup(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 36)) return 0;
    frame_open(c, 0x18);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    bp_put(c, -0x18, 0xDC2E);                                     /* the window */
    c->r[R_AX] = bp_get(c, 0x0A);
    ds_put(c, 0xDC3A, c->r[R_AX]);
    c->r[R_DI] = (uint16_t)(c->r[R_BP] - 0x16);                   /* a copy of the window record, to measure with */
    c->r[R_SI] = 0xDC2E;
    c->seg[S_ES] = c->seg[S_SS];
    c->r[R_CX] = 0x0B;
    c->icount += 13;
    c->icount += rep_string(c, STR_MOVS, 1, c->seg[S_DS], 0);
    bp_put(c, -0x16, 0xFFFF);                                     /* no ink: measure only */
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_AX]);                                    /* draw_wrapped(copy, s, 0, 0, w, 8, centre) */
    c->r[R_CX] = 8; cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, bp_get(c, 0x0E));
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, 4));
    c->r[R_AX] = (uint16_t)(c->r[R_BP] - 0x16);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 11;
    if (!guest_call(m, 0x3776, 0x2B56)) return 1;
    ST2_NEED(6 + 2 * 16 + 6, 0x2B56);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0E, 1, 0);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x0C, 1, 0);    /* the widest line + 12: the box's width */
    ds_put(c, 0xB2FC, c->r[R_AX]);
    c->r[R_AX] = bp_get(c, -0x0C);                                /* the pen below the text + 12: its height */
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x0C, 1, 0);
    ds_put(c, 0xB302, c->r[R_AX]);
    c->icount += 6;
    c->icount += st2_place_axis(c, 0xB2FC, 6, 0xB2F6, 0x140);     /* x on the 320-wide screen */
    c->icount += st2_place_axis(c, 0xB302, 8, 0xB2F8, 0xC8);      /* y on the 200 high */
    alu_sub(c, ds_get(c, 0xB302), 0x62, 1, 0);                    /* the saved strip: from the box's own origin ... */
    c->icount += 2;
    if (!x86_cond(c, 0xE)) {                                      /* ... when it is deep: the whole height from (width, 4) */
        c->r[R_AX] = ds_get(c, 0xB2FC);
        bp_put(c, 6, c->r[R_AX]);
        bp_put(c, 8, 4);
        c->icount += 4;
    } else {
        bp_put(c, 6, 0);
        c->r[R_AX] = ds_get(c, 0xB302);
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 4, 1, 0);
        bp_put(c, 8, c->r[R_AX]);
        c->icount += 4;
    }
    ST2_NEED(11, 0x2BF8);
    cpu_push16(c, ds_get(c, 0xB302));
    cpu_push16(c, ds_get(c, 0xB2FC));
    cpu_push16(c, bp_get(c, 8));
    cpu_push16(c, bp_get(c, 6));
    c->r[R_AX] = 2; cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, ds_get(c, 0xB2F8));
    cpu_push16(c, ds_get(c, 0xB2F6));
    c->r[R_CX] = 1; cpu_push16(c, c->r[R_CX]);
    c->icount += 10;
    if (!guest_call_far(m, 0x2C16, 0x2C1B)) return 1;             /* the driver's copy: save the backdrop */
    ST2_NEED(9, 0x2C1B);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x10, 1, 0);
    cpu_push16(c, ds_get(c, 0xB302));
    cpu_push16(c, ds_get(c, 0xB2FC));
    c->r[R_AX] = 4; cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], c->r[R_CX], 1, 0);
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, bp_get(c, -0x18));
    c->icount += 8;
    if (!guest_call(m, 0x2CB4, 0x2C33)) return 1;                 /* draw_box_frame */
    ST2_NEED(17, 0x2C33);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0A, 1, 0);
    c->r[R_BX] = bp_get(c, -0x18);
    ds_put(c, (uint16_t)(c->r[R_BX] + 4), 0x0F);                  /* ink 15, shadow 8 */
    ds_put(c, (uint16_t)(c->r[R_BX] + 6), 0x08);
    cpu_push16(c, bp_get(c, 0x0C));                               /* draw_wrapped(win, s, 10, 6, w - 12, 8, centre) */
    c->r[R_AX] = 8; cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, ds_get(c, 0xB2FC), 0x0C, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0A; cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 6; cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, 4));
    cpu_push16(c, c->r[R_BX]);
    c->icount += 16;
    if (!guest_call(m, 0x3776, 0x2C60)) return 1;
    ST2_NEED(14, 0x2C60);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0E, 1, 0);
    cpu_push16(c, ds_get(c, 0xB302));
    cpu_push16(c, ds_get(c, 0xB2FC));
    cpu_push16(c, ds_get(c, 0xB2F8));
    cpu_push16(c, ds_get(c, 0xB2F6));
    c->r[R_AX] = 1; cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 4; cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], c->r[R_CX], 1, 0);
    cpu_push16(c, c->r[R_CX]);
    c->r[R_DX] = 2; cpu_push16(c, c->r[R_DX]);
    c->icount += 13;
    if (!guest_call_far(m, 0x2C82, 0x2C87)) return 1;             /* show the box on page 1 */
    ST2_NEED(13, 0x2C87);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x10, 1, 0);
    cpu_push16(c, ds_get(c, 0xB302));
    cpu_push16(c, ds_get(c, 0xB2FC));
    cpu_push16(c, ds_get(c, 0xB2F8));
    cpu_push16(c, ds_get(c, 0xB2F6));
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 4; cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 2; cpu_push16(c, c->r[R_AX]);
    c->icount += 12;
    if (!guest_call_far(m, 0x2CA6, 0x2CAB)) return 1;             /* and on page 0 */
    ST2_NEED(6, 0x2CAB);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x10, 1, 0);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->icount += 3;
    frame_close_ret(c);
    c->icount += 3;
    return 1;
}

/* START 0x0332E, open_modal(desc, x, y): lays out and draws a dialog. The descriptor
 * (a five-word record: text, the first and second button's labels at +2 and +4, the box's
 * size written back at +6 and +8) is measured by draw_wrapped on a copy of the window
 * DC2Eh with no ink (12 less than the width given, lines 8 apart); the box becomes
 * that + 12 wide and the text's end + 1Ah high, its frame is drawn (0x02CB4), the text
 * drawn in ink 15 shadow 8, and the buttons (0x02DF2, ink E8h and, for the second, E7h)
 * placed in the descriptor's hit boxes (+0Eh, +16h): one button centred, or two in
 * thirds of the width. The driver's page copy last saves the area on page 1 (and shows
 * it on both pages). */
static int start_open_modal(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 37)) return 0;
    frame_open(c, 0x1C);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    ds_put(c, 0xDC3A, 4);
    c->r[R_DI] = (uint16_t)(c->r[R_BP] - 0x16);                   /* a copy of the window record, to measure with */
    c->r[R_SI] = 0xDC2E;
    c->seg[S_ES] = c->seg[S_SS];
    c->r[R_CX] = 0x0B;
    c->icount += 11;
    c->icount += rep_string(c, STR_MOVS, 1, c->seg[S_DS], 0);
    ST2_NEED(15, 0x3349);
    bp_put(c, -0x16, 0xFFFF);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 0x0008;
    cpu_push16(c, c->r[R_CX]);
    c->r[R_BX] = bp_get(c, 0x4);
    c->r[R_DX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0006));
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], 0x000C, 1, 0);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX])));
    c->r[R_DX] = (uint16_t)(c->r[R_BP] + 0xFFEA);
    cpu_push16(c, c->r[R_DX]);
    c->icount += 14;
    if (!guest_call(m, 0x3776, 0x336A)) return 1;
    ST2_NEED(16, 0x336A);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x000E, 1, 0);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x000C, 1, 0);
    c->r[R_BX] = bp_get(c, 0x4);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x0006), c->r[R_AX]);
    c->r[R_AX] = bp_get(c, -0xC);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x001A, 1, 0);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x0008), c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0006)));
    c->r[R_AX] = 0x0004;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], c->r[R_CX], 1, 0);
    cpu_push16(c, c->r[R_CX]);
    c->r[R_CX] = 0xDC2E;
    cpu_push16(c, c->r[R_CX]);
    c->icount += 15;
    if (!guest_call(m, 0x2CB4, 0x3391)) return 1;
    ST2_NEED(18, 0x3391);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x000A, 1, 0);
    ds_put(c, 0xDC32, 0x000F);
    ds_put(c, 0xDC34, 0x0008);
    c->r[R_AX] = 0x0001;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0008;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_BX] = bp_get(c, 0x4);
    c->r[R_CX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0006));
    c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], 0x000C, 1, 0);
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0006;
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX])));
    c->r[R_CX] = 0xDC2E;
    cpu_push16(c, c->r[R_CX]);
    c->icount += 17;
    if (!guest_call(m, 0x3776, 0x33C0)) return 1;
    ST2_NEED(4, 0x33C0);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x000E, 1, 0);
    c->r[R_BX] = bp_get(c, 0x4);
    c->icount += 2;
    alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 4)), 0, 1, 0);   /* a second button? */
    c->icount += 2;
    if (!(c->flags & F_ZF)) {                                     /* two buttons: thirds of the width */
        ST2_NEED(17, 0x33CC);
        c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 6));
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], 4, 1, 0);
        c->r[R_CX] = 3;
        c->r[R_BX] = c->r[R_AX];
        c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
        x86_div16(c, c->r[R_CX]);                                 /* DX is 0, the divisor 3: no fault */
        c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
        c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
        bp_put(c, -0x1A, c->r[R_AX]);
        c->r[R_AX] = c->r[R_BX];
        c->r[R_AX] = x86_shift(c, 4, c->r[R_AX], 1, 1);
        c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
        x86_div16(c, c->r[R_CX]);
        c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
        c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
        bp_put(c, -0x1C, c->r[R_AX]);
        c->icount += 17;
    } else {                                                      /* one button: half the width */
        ST2_NEED(6, 0x33EF);
        c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 6));
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], 4, 1, 0);
        c->r[R_AX] = x86_shift(c, 5, c->r[R_AX], 1, 1);
        c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
        c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
        bp_put(c, -0x1A, c->r[R_AX]);
        c->icount += 6;
    }
    ST2_NEED(9, 0x33FC);
    c->r[R_AX] = 0x00E8;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = bp_get(c, -0xC);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x000A, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, -0x1A));
    c->r[R_BX] = bp_get(c, 0x4);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0002)));
    c->icount += 8;
    if (!guest_call(m, 0x2DF2, 0x3413)) return 1;
    ST2_NEED(22, 0x3413);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0008, 1, 0);
    bp_put(c, -0x18, c->r[R_AX]);
    c->r[R_AX] = x86_shift(c, 7, c->r[R_AX], 1, 1);
    c->r[R_CX] = c->r[R_AX];
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], bp_get(c, 0x6), 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], bp_get(c, -0x1A), 1, 0);
    c->r[R_BX] = bp_get(c, 0x4);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x000E), c->r[R_AX]);
    c->r[R_CX] = (uint16_t)alu_add(c, c->r[R_CX], bp_get(c, 0x6), 1, 0);
    c->r[R_CX] = (uint16_t)alu_add(c, c->r[R_CX], bp_get(c, -0x1A), 1, 0);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x0012), c->r[R_CX]);
    c->r[R_AX] = bp_get(c, 0x8);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], bp_get(c, -0xC), 1, 0);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x0006, 1, 0);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x0010), c->r[R_AX]);
    c->r[R_AX] = bp_get(c, 0x8);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], bp_get(c, -0xC), 1, 0);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x0016, 1, 0);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x0014), c->r[R_AX]);
    c->icount += 20;
    alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 4)), 0, 1, 0);
    c->icount += 2;
    if (!(c->flags & F_ZF)) {                                     /* the second button, too */
    ST2_NEED(8, 0x3452);
    c->r[R_AX] = 0x00E7;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = bp_get(c, -0xC);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x000A, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, -0x1C));
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0004)));
    c->icount += 7;
    if (!guest_call(m, 0x2DF2, 0x3466)) return 1;
    ST2_NEED(20, 0x3466);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0008, 1, 0);
    bp_put(c, -0x18, c->r[R_AX]);
    c->r[R_AX] = x86_shift(c, 7, c->r[R_AX], 1, 1);
    c->r[R_CX] = c->r[R_AX];
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], bp_get(c, 0x6), 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], bp_get(c, -0x1C), 1, 0);
    c->r[R_BX] = bp_get(c, 0x4);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x0016), c->r[R_AX]);
    c->r[R_CX] = (uint16_t)alu_add(c, c->r[R_CX], bp_get(c, 0x6), 1, 0);
    c->r[R_CX] = (uint16_t)alu_add(c, c->r[R_CX], bp_get(c, -0x1C), 1, 0);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x001A), c->r[R_CX]);
    c->r[R_AX] = bp_get(c, 0x8);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], bp_get(c, -0xC), 1, 0);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x0006, 1, 0);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x0018), c->r[R_AX]);
    c->r[R_AX] = bp_get(c, 0x8);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], bp_get(c, -0xC), 1, 0);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x0016, 1, 0);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x001C), c->r[R_AX]);
    c->icount += 20;
    }
    ST2_NEED(15, 0x349F);
    c->r[R_BX] = bp_get(c, 0x4);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0008)));
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0006)));
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0008));
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x0004, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 0x0002;
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, bp_get(c, 0x8));
    cpu_push16(c, bp_get(c, 0x6));
    c->r[R_DX] = 0x0001;
    cpu_push16(c, c->r[R_DX]);
    c->icount += 14;
    if (!guest_call_far(m, 0x34C0, 0x34C5)) return 1;
    ST2_NEED(15, 0x34C5);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0010, 1, 0);
    c->r[R_BX] = bp_get(c, 0x4);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0008)));
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0006)));
    cpu_push16(c, bp_get(c, 0x8));
    cpu_push16(c, bp_get(c, 0x6));
    c->r[R_AX] = 0x0001;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0004;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], c->r[R_CX], 1, 0);
    cpu_push16(c, c->r[R_CX]);
    c->r[R_DX] = 0x0002;
    cpu_push16(c, c->r[R_DX]);
    c->icount += 14;
    if (!guest_call_far(m, 0x34E6, 0x34EB)) return 1;
    ST2_NEED(14, 0x34EB);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0010, 1, 0);
    c->r[R_BX] = bp_get(c, 0x4);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0008)));
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0006)));
    cpu_push16(c, bp_get(c, 0x8));
    cpu_push16(c, bp_get(c, 0x6));
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 0x0004;
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0002;
    cpu_push16(c, c->r[R_AX]);
    c->icount += 13;
    if (!guest_call_far(m, 0x3509, 0x350E)) return 1;
    ST2_NEED(6, 0x350E);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0010, 1, 0);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->icount += 6;
    frame_close_ret(c);
    return 1;
}

/* START 0x01AB6, draw_troops(): the Enemy Troops overlay of the briefing map. Over the
 * 16 by 16 cells (the table at CAD8h: 10h bytes a row, flag 10h for a unit)
 * a sprite (the handle [CAC4]) is drawn in the window DC18h at each cell with a unit,
 * at its screen position (column * 7FFh / C3h, row * 7FFh / 92h); the caption kind is
 * 65h once any is found, else 64h; the caption (0x01B37) is drawn last. As shipped the
 * scan variables are the ones overwritten with the screen position, so the scan
 * goes on from there. */
static int start_draw_troops(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7 + 2 + 2 + 2 + 4)) return 0;
    frame_open(c, 6);                                             /* [bp-2] the caption, [bp-4] row, [bp-6] column */
    cpu_push16(c, c->r[R_SI]);
    bp_put8(c, -2, 0x64);
    bp_put(c, -4, 0);
    c->icount += 7;
    goto row;
next_column:                                                      /* 0x01AC8 */
    bp_put(c, -6, (uint16_t)alu_inc(c, bp_get(c, -6), 1));
    c->icount += 1;
column:                                                           /* 0x01ACB */
    ST2_NEED(28, 0x1ACB);
    alu_sub(c, bp_get(c, -6), 0x10, 1, 0);
    c->icount += 2;
    if (!(c->flags & F_CF)) goto next_row;                        /* jae */
    set_r8(c, R_CL, 4);
    c->r[R_SI] = bp_get(c, -6);
    c->r[R_SI] = x86_shift(c, 4, c->r[R_SI], get_r8(c, R_CL), 1);
    c->r[R_BX] = bp_get(c, -4);
    alu_logic(c, ds_get8(c, (uint16_t)(c->r[R_BX] + c->r[R_SI] - 0x3528)) & 0x10, 0);   /* the cell's flag: a unit here? */
    c->icount += 6;
    if (c->flags & F_ZF) goto next_column;
    bp_put8(c, -2, 0x65);                                         /* any unit: the caption's kind changes */
    cpu_push16(c, ds_get(c, 0xCAC4));
    c->r[R_AX] = 0x07FF;
    x86_mul16(c, bp_get(c, -6));                                  /* column * 7FFh / C3h: the screen x */
    c->r[R_CX] = 0xC3;
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, c->r[R_CX]);                                     /* the column is below 16: no overflow */
    bp_put(c, -6, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 0x07FF;
    c->r[R_AX] = c->r[R_BX];
    x86_mul16(c, c->r[R_CX]);                                     /* row * 7FFh / 92h: the screen y */
    c->r[R_CX] = 0x92;
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, c->r[R_CX]);
    bp_put(c, -4, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0xDC18;
    cpu_push16(c, c->r[R_AX]);
    c->icount += 19;
    if (!guest_call(m, 0x3693, 0x1B14)) return 1;                 /* draw_sprite(win, x, y, [CAC4]) */
    ST2_NEED(3, 0x1B14);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    c->icount += 2;
    goto next_column;
next_row:                                                         /* 0x01B19 */
    bp_put(c, -4, (uint16_t)alu_inc(c, bp_get(c, -4), 1));
    c->icount += 1;
row:                                                              /* 0x01B1C */
    ST2_NEED(6, 0x1B1C);
    alu_sub(c, bp_get(c, -4), 0x10, 1, 0);
    c->icount += 2;
    if (!(c->flags & F_CF)) {                                     /* all rows: the caption */
        c->r[R_AX] = (uint16_t)(int16_t)(int8_t)bp_get8(c, -2);
        cpu_push16(c, c->r[R_AX]);
        c->icount += 3;
        if (!guest_call(m, 0x1B37, 0x1B31)) return 1;
        ST2_NEED(5, 0x1B31);
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_SI] = cpu_pop16(c);
        c->icount += 2;
        frame_close_ret(c);
        c->icount += 3;
        return 1;
    }
    bp_put(c, -6, 0);
    c->icount += 2;
    goto column;
}

/* START 0x01B37, draw_map_caption(kind): the briefing map's caption strip. The
 * rectangle x 57h..EAh, y B4h..C4h is filled in colour E2h (0x08A32), the window DC18h
 * set to font 4, ink 15, shadow 8, and the text for the kind (a string chosen by a chain
 * of compares: kinds 1 to 8, 64h, 65h, anything else) is word-wrapped into it
 * (0x03776, 92h wide, lines 8 apart, centred); the driver's rectangle copy then
 * publishes the strip (92h by 10h at (57h, B4h)). */
static int start_draw_map_caption(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 16)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)alu_sub(c, c->r[R_SP], 0x0004, 1, 0);
    ds_put(c, 0xDC1C, 0x00E2);
    c->r[R_AX] = 0x00C4;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x00EA;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x00B4;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0057;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0xDC18;
    bp_put(c, -0x2, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 15;
    if (!guest_call(m, 0x8A32, 0x1B5D)) return 1;
    ST2_NEED(4, 0x1B5D);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x000A, 1, 0);
    ds_put(c, 0xDC24, 0x0004);
    ds_put(c, 0xDC1C, 0x000F);
    ds_put(c, 0xDC1E, 0x0008);
    c->icount += 4;
    ST2_NEED(5, 0x1B72);
    c->r[R_AX] = bp_get(c, 4);
    alu_sub(c, c->r[R_AX], 0x65, 1, 0);
    c->icount += 3;
    if (c->flags & F_ZF) { bp_put(c, -4, 0x04E5); c->icount += 2; goto text; }   /* kind 65h */
    c->icount += 1;
    if (!(c->flags & (F_CF | F_ZF))) { bp_put(c, -4, 0x055E); c->icount += 1; goto text; }   /* above: the last string */
    {
        /* kinds 1 to 8 take these strings (a chain of DEC AL / JE, 4 bytes a step from 0x01B7C) */
        static const uint16_t strings[8] = { 0x0327, 0x035E, 0x03C9, 0x03FE, 0x043B, 0x0393, 0x047A, 0x04B6 };
        for (unsigned k = 0; k < 8; k++) {
            ST2_NEED(4, 0x1B7C + 4 * k);
            set_r8(c, R_AL, (uint8_t)alu_dec(c, get_r8(c, R_AL), 0));
            c->icount += 2;
            if (c->flags & F_ZF) { bp_put(c, -4, strings[k]); c->icount += 2; goto text; }
        }
    }
    ST2_NEED(4, 0x1B9C);
    set_r8(c, R_AL, (uint8_t)alu_sub(c, get_r8(c, R_AL), 0x5C, 0, 0));
    c->icount += 2;
    if (c->flags & F_ZF) { bp_put(c, -4, 0x051E); c->icount += 2; goto text; }   /* kind 64h */
    bp_put(c, -4, 0x055E);                                        /* anything else */
    c->icount += 2;
text:
    ST2_NEED(13, 0x1BED);
    c->r[R_AX] = 0x0001;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 0x0008;
    cpu_push16(c, c->r[R_CX]);
    c->r[R_CX] = 0x0092;
    cpu_push16(c, c->r[R_CX]);
    c->r[R_DX] = 0x00B4;
    cpu_push16(c, c->r[R_DX]);
    c->r[R_BX] = 0x0057;
    cpu_push16(c, c->r[R_BX]);
    cpu_push16(c, bp_get(c, -0x4));
    cpu_push16(c, bp_get(c, -0x2));
    c->icount += 12;
    if (!guest_call(m, 0x3776, 0x1C0A)) return 1;
    ST2_NEED(16, 0x1C0A);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x000E, 1, 0);
    c->r[R_AX] = 0x0010;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0092;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x00B4;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 0x0057;
    cpu_push16(c, c->r[R_CX]);
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_CX]);
    c->r[R_AX] = 0x0001;
    cpu_push16(c, c->r[R_AX]);
    c->icount += 15;
    if (!guest_call_far(m, 0x1C26, 0x1C2B)) return 1;
    ST2_NEED(3, 0x1C2B);
    c->icount += 3;
    frame_close_ret(c);
    return 1;
}

/* START 0x031FA, draw_route_label(s, p, q, r): the name s beside a route leg on the
 * briefing map. The three map points p, q and r (records with the x at +2 and the y at
 * +4, scaled to the screen by 92h and C3h) decide on which side of p the label goes:
 * (x, y) = (-5, -3), (+3, -3), (-2, -8), (-2, +3) from p's scaled position, or by the
 * sign of the area of the two legs (-4, +2) or (+3, -7) (the offsets are applied to
 * the scaled coordinates left in the frame); the string is drawn there in the window
 * DC18h (0x03567). */
static int start_draw_route_label(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 49)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_SP] = (uint16_t)alu_sub(c, c->r[R_SP], 0x000E, 1, 0);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_BX] = bp_get(c, 0x6);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0002));
    c->r[R_CX] = 0x0092;
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, c->r[R_CX]);    /* DX is 0: no fault */
    bp_put(c, -0x2, c->r[R_AX]);
    c->r[R_SI] = 0x00C3;
    c->r[R_DX] = c->r[R_AX];
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0004));
    c->r[R_BX] = c->r[R_DX];
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, c->r[R_SI]);    /* DX is 0: no fault */
    bp_put(c, -0x4, c->r[R_AX]);
    c->r[R_DI] = bp_get(c, 0x8);
    c->r[R_DX] = c->r[R_AX];
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_DI] + 0x0002));
    c->r[R_SI] = c->r[R_DX];
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, c->r[R_CX]);    /* DX is 0: no fault */
    c->r[R_CX] = 0x00C3;
    c->r[R_DX] = c->r[R_AX];
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_DI] + 0x0004));
    c->r[R_DI] = c->r[R_DX];
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, c->r[R_CX]);    /* DX is 0: no fault */
    bp_put(c, -0x8, c->r[R_AX]);
    c->r[R_AX] = c->r[R_BX];
    c->r[R_BX] = bp_get(c, 0xA);
    c->r[R_CX] = 0x0092;
    c->r[R_DX] = c->r[R_AX];
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0002));
    c->r[R_BX] = c->r[R_DX];
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, c->r[R_CX]);    /* DX is 0: no fault */
    bp_put(c, -0xA, c->r[R_AX]);
    c->r[R_CX] = 0x00C3;
    c->r[R_DX] = c->r[R_BX];
    c->r[R_BX] = bp_get(c, 0xA);
    bp_put(c, -0xE, c->r[R_AX]);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0004));
    c->r[R_BX] = c->r[R_DX];
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    x86_div16(c, c->r[R_CX]);    /* DX is 0: no fault */
    bp_put(c, -0xC, c->r[R_AX]);
    c->icount += 49;
    ST2_NEED(9, 0x326F);
    alu_sub(c, c->r[R_BX], c->r[R_DI], 1, 0);
    c->icount += 2;
    if (!x86_cond(c, 0xF)) {                                      /* jg not taken */
        alu_sub(c, c->r[R_BX], bp_get(c, -0x0E), 1, 0);
        c->icount += 2;
        if (!x86_cond(c, 0xF)) {
            c->r[R_AX] = (uint16_t)(c->r[R_BX] - 5);
            bp_put(c, -2, c->r[R_AX]);
            c->r[R_AX] = (uint16_t)(c->r[R_SI] - 3);
            bp_put(c, -4, c->r[R_AX]);
            c->icount += 5;
            goto place;
        }
    }
    ST2_NEED(8, 0x3286);
    c->r[R_AX] = c->r[R_BX];
    alu_sub(c, c->r[R_DI], c->r[R_AX], 1, 0);
    c->icount += 3;
    if (!x86_cond(c, 0xF)) {
        alu_sub(c, bp_get(c, -0x0A), c->r[R_AX], 1, 0);
        c->icount += 2;
        if (!x86_cond(c, 0xF)) {
            bp_put(c, -2, (uint16_t)alu_add(c, bp_get(c, -2), 3, 1, 0));
            bp_put(c, -4, (uint16_t)alu_sub(c, bp_get(c, -4), 3, 1, 0));
            c->icount += 3;
            goto place;
        }
    }
    ST2_NEED(8, 0x329B);
    c->r[R_AX] = c->r[R_SI];
    alu_sub(c, bp_get(c, -8), c->r[R_AX], 1, 0);
    c->icount += 3;
    if (!x86_cond(c, 0xC)) {                                      /* jl not taken */
        alu_sub(c, bp_get(c, -0x0C), c->r[R_AX], 1, 0);
        c->icount += 2;
        if (!x86_cond(c, 0xC)) {
            bp_put(c, -2, (uint16_t)alu_sub(c, bp_get(c, -2), 2, 1, 0));
            bp_put(c, -4, (uint16_t)alu_sub(c, bp_get(c, -4), 8, 1, 0));
            c->icount += 3;
            goto place;
        }
    }
    ST2_NEED(7, 0x32B1);
    alu_sub(c, bp_get(c, -8), c->r[R_AX], 1, 0);
    c->icount += 2;
    if (!x86_cond(c, 0xF)) {
        alu_sub(c, bp_get(c, -0x0C), c->r[R_AX], 1, 0);
        c->icount += 2;
        if (!x86_cond(c, 0xF)) {
            bp_put(c, -2, (uint16_t)alu_sub(c, bp_get(c, -2), 2, 1, 0));
            bp_put(c, -4, (uint16_t)alu_add(c, bp_get(c, -4), 3, 1, 0));
            c->icount += 3;
            goto place;
        }
    }
    ST2_NEED(10, 0x32C5);                                         /* the legs cross: by the sign of the area */
    c->r[R_AX] = bp_get(c, -0x0C);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], bp_get(c, -8), 1, 0);
    c->r[R_CX] = bp_get(c, -0x0A);
    c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], c->r[R_DI], 1, 0);
    x86_imul16(c, c->r[R_CX]);
    alu_logic(c, c->r[R_AX], 1);
    c->icount += 7;
    if (x86_cond(c, 0xE)) {                                       /* jle */
        bp_put(c, -2, (uint16_t)alu_add(c, bp_get(c, -2), 3, 1, 0));
        bp_put(c, -4, (uint16_t)alu_sub(c, bp_get(c, -4), 7, 1, 0));
        c->icount += 2;
    } else {
        bp_put(c, -2, (uint16_t)alu_sub(c, bp_get(c, -2), 4, 1, 0));
        bp_put(c, -4, (uint16_t)alu_add(c, bp_get(c, -4), 2, 1, 0));
        c->icount += 3;
    }
place:                                                            /* 0x032E8: draw_text(win DC18h, s, x, y) */
    ST2_NEED(6, 0x32E8);
    cpu_push16(c, bp_get(c, -0x4));
    cpu_push16(c, bp_get(c, -0x2));
    cpu_push16(c, bp_get(c, 0x4));
    c->r[R_AX] = 0xDC18;
    cpu_push16(c, c->r[R_AX]);
    c->icount += 5;
    if (!guest_call(m, 0x3567, 0x32F8)) return 1;
    ST2_NEED(6, 0x32F8);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0008, 1, 0);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->icount += 6;
    frame_close_ret(c);
    return 1;
}

/* START 0x02901, cel_step(): one step of the Transfer Request's cel animation; returns
 * the steps left in AX. With steps left ([B2F4]) only every eighth call (the counter
 * [B2EA]) does anything. A step first puts back the rectangle the last frame covered
 * (the box at [B2EC], [B2EE], [B2F0], [B2F2], by the driver's copy from page 3), then
 * draws the next frame's cells (from the table at [B2FE]: a count, then 12-byte cell
 * records at +[B300]), each by the driver with the colour [B2FA], tracking the
 * bounding box of the cells; the union of that box and the last one is published,
 * the new box is kept, the table pointer moves on by 2 and the steps left drop. */
static int start_cel_step(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 9)) return 0;
    frame_open(c, 0x16);
    alu_sub(c, ds_get8(c, 0xB2F4), 0, 0, 0);
    c->icount += 5;
    if (!(c->flags & F_ZF)) {                                     /* steps left: only every eighth call does one */
        set_r8(c, R_AL, ds_get8(c, 0xB2EA));
        ds_put8(c, 0xB2EA, (uint8_t)alu_inc(c, ds_get8(c, 0xB2EA), 0));
        alu_logic(c, get_r8(c, R_AL) & 7, 0);
        c->icount += 4;
        if (c->flags & F_ZF) goto step;
    }
done:                                                             /* 0x02919: return the steps left */
    ST2_NEED(6, 0x2919);
    set_r8(c, R_AL, ds_get8(c, 0xB2F4));
    set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
    c->icount += 3;
    frame_close_ret(c);
    c->icount += 3;
    return 1;
step:                                                             /* 0x02921: undo the last frame, draw the next */
    ST2_NEED(13, 0x2921);
    alu_sub(c, ds_get(c, 0xB2F0), 0, 1, 0);
    c->icount += 2;
    if (!(c->flags & F_ZF)) {
    ST2_NEED(11, 0x2928);
    cpu_push16(c, ds_get(c, 0xB2F2));
    cpu_push16(c, ds_get(c, 0xB2F0));
    cpu_push16(c, ds_get(c, 0xB2EE));
    cpu_push16(c, ds_get(c, 0xB2EC));
    c->r[R_AX] = 0x0001;
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, ds_get(c, 0xB2EE));
    cpu_push16(c, ds_get(c, 0xB2EC));
    c->r[R_AX] = 0x0003;
    cpu_push16(c, c->r[R_AX]);
    c->icount += 10;
    if (!guest_call_far(m, 0x2948, 0x294D)) return 1;
    ST2_NEED(1, 0x294D);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0010, 1, 0);
    c->icount += 1;
    }
    ST2_NEED(13, 0x2950);
    c->r[R_AX] = 0x0140;
    bp_put(c, -0x6, c->r[R_AX]);
    bp_put(c, -0x4, c->r[R_AX]);
    c->r[R_BX] = ds_get(c, 0xB2FE);
    set_r8(c, R_AL, 0x0C);
    x86_mul8(c, ds_get8(c, (uint16_t)(c->r[R_BX])));
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], ds_get(c, 0xB300), 1, 0);
    bp_put(c, -0x2, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    bp_put(c, -0xC, c->r[R_AX]);
    bp_put(c, -0x8, c->r[R_AX]);
    bp_put(c, -0x10, c->r[R_AX]);
    /* 2973 jmp 0x29dd */
    c->icount += 13;
loop_test:                                                        /* 0x029DD: while cells are left */
    ST2_NEED(18, 0x29DD);
    c->r[R_BX] = ds_get(c, 0xB2FE);
    set_r8(c, R_AL, ds_get8(c, (uint16_t)(c->r[R_BX] + 1)));
    set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
    alu_sub(c, c->r[R_AX], bp_get(c, -0x10), 1, 0);
    c->icount += 5;
    if (!x86_cond(c, 0x7)) goto after_cells;                      /* ja not taken */
    ST2_NEED(13, 0x2975);
    c->r[R_BX] = bp_get(c, -0x2);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x000A)));
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0008)));
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0006)));
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0004)));
    c->r[R_AX] = 0x0001;
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0002)));
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX])));
    set_r8(c, R_AL, ds_get8(c, 0xB2FA));
    set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
    cpu_push16(c, c->r[R_AX]);
    c->icount += 12;
    if (!guest_call_far(m, 0x2993, 0x2998)) return 1;
    ST2_NEED(24, 0x2998);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0010, 1, 0);
    c->icount += 1;
    /* The bounding box of the cells drawn: [bp-4], [bp-6] the least x, y; [bp-8], [bp-0xC] the greatest. */
    c->r[R_AX] = bp_get(c, -4);
    c->r[R_BX] = bp_get(c, -2);
    alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 4)), c->r[R_AX], 1, 0);
    c->icount += 4;
    if (c->flags & F_CF) {
        c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 4));
        bp_put(c, -4, c->r[R_AX]);
        c->icount += 2;
    }
    c->r[R_AX] = bp_get(c, -6);
    alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 6)), c->r[R_AX], 1, 0);
    c->icount += 3;
    if (c->flags & F_CF) {
        c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 6));
        bp_put(c, -6, c->r[R_AX]);
        c->icount += 2;
    }
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 4));
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], ds_get(c, (uint16_t)(c->r[R_BX] + 8)), 1, 0);
    alu_sub(c, c->r[R_AX], bp_get(c, -8), 1, 0);
    c->icount += 4;
    if (!x86_cond(c, 0x6)) { bp_put(c, -8, c->r[R_AX]); c->icount += 1; }
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 6));
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], ds_get(c, (uint16_t)(c->r[R_BX] + 0x0A)), 1, 0);
    alu_sub(c, c->r[R_AX], bp_get(c, -0x0C), 1, 0);
    c->icount += 4;
    if (!x86_cond(c, 0x6)) { bp_put(c, -0x0C, c->r[R_AX]); c->icount += 1; }
    bp_put(c, -0x10, (uint16_t)alu_inc(c, bp_get(c, -0x10), 1));
    bp_put(c, -2, (uint16_t)alu_add(c, bp_get(c, -2), 0x0C, 1, 0));
    c->icount += 2;
    goto loop_test;
after_cells:                                                      /* 0x029EB: the union with the last frame's box */
    ST2_NEED(35, 0x29EB);
    c->r[R_AX] = bp_get(c, -4);
    alu_sub(c, ds_get(c, 0xB2EC), c->r[R_AX], 1, 0);
    c->icount += 3;
    if (c->flags & F_CF) { c->r[R_AX] = ds_get(c, 0xB2EC); c->icount += 1; }
    bp_put(c, -0x0A, c->r[R_AX]);
    c->r[R_AX] = bp_get(c, -6);
    alu_sub(c, ds_get(c, 0xB2EE), c->r[R_AX], 1, 0);
    c->icount += 4;
    if (c->flags & F_CF) { c->r[R_AX] = ds_get(c, 0xB2EE); c->icount += 1; }
    bp_put(c, -0x0E, c->r[R_AX]);
    c->r[R_AX] = ds_get(c, 0xB2EC);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], ds_get(c, 0xB2F0), 1, 0);
    alu_sub(c, c->r[R_AX], bp_get(c, -8), 1, 0);
    c->icount += 5;
    if (x86_cond(c, 0x6)) { c->r[R_AX] = bp_get(c, -8); c->icount += 1; }
    bp_put(c, -0x12, c->r[R_AX]);
    c->r[R_AX] = ds_get(c, 0xB2EE);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], ds_get(c, 0xB2F2), 1, 0);
    alu_sub(c, c->r[R_AX], bp_get(c, -0x0C), 1, 0);
    c->icount += 5;
    if (x86_cond(c, 0x6)) { c->r[R_AX] = bp_get(c, -0x0C); c->icount += 1; }
    ST2_NEED(14, 0x2A2A);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], bp_get(c, -0xE), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = bp_get(c, -0x12);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], bp_get(c, -0xA), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, -0xE));
    cpu_push16(c, bp_get(c, -0xA));
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, -0xE));
    cpu_push16(c, bp_get(c, -0xA));
    c->r[R_AX] = 0x0001;
    cpu_push16(c, c->r[R_AX]);
    c->icount += 13;
    if (!guest_call_far(m, 0x2A48, 0x2A4D)) return 1;
    ST2_NEED(1, 0x2A4D);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0010, 1, 0);
    c->icount += 1;
    ST2_NEED(13, 0x2A50);
    c->r[R_AX] = bp_get(c, -0x6);
    ds_put(c, 0xB2EE, c->r[R_AX]);
    c->r[R_CX] = bp_get(c, -0x4);
    ds_put(c, 0xB2EC, c->r[R_CX]);
    c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], bp_get(c, -0x8), 1, 0);
    c->r[R_CX] = (uint16_t)alu_sub(c, 0, c->r[R_CX], 1, 0);
    ds_put(c, 0xB2F0, c->r[R_CX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], bp_get(c, -0xC), 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);
    ds_put(c, 0xB2F2, c->r[R_AX]);
    ds_put(c, 0xB2FE, (uint16_t)alu_add(c, ds_get(c, 0xB2FE), 0x0002, 1, 0));
    ds_put8(c, 0xB2F4, (uint8_t)alu_dec(c, ds_get8(c, 0xB2F4), 0));
    /* 2A77 jmp 0x2919 */
    c->icount += 13;
    goto done;
}

/* START 0x00BD4, draw_stores_panel(item): the arming page's description panel for the
 * weapon row under the pointer. Item 14h clears the panel (the fill 0x08A32 over
 * (88h, 9) to (DDh, C1h) in colour 3 on the window DC18h and the same on DC02h) and
 * returns 0. Any other item is the one before it (wrapping from 0 to 13h): the panel
 * is cleared in colour 3, the weapon's picture (sheet 3, or 4 from the tenth item on,
 * at the item's cell of the table at 232h/246h) copied from the driver's sheet with
 * its frame, a frame bar (colour index and positions from 0xCC or 0x106) drawn by two
 * driver copies, and the weapon's description (the string pointers at A587h, indexed
 * by the item) word-wrapped under it in font 4 ink 15 shadow 8 (0x03776); the panel
 * is then published. */
static int start_draw_stores_panel(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 6)) return 0;
    frame_open(c, 0x0A);
    cpu_push16(c, c->r[R_SI]);
    alu_sub(c, bp_get(c, 4), 0x14, 1, 0);
    c->icount += 6;
    if (!(c->flags & F_ZF)) goto weapon;
    ST2_NEED(12, 0x0BE1);
    ds_put(c, 0xDC1C, 0x0003);
    c->r[R_AX] = 0x00C1;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 0x00DD;
    cpu_push16(c, c->r[R_CX]);
    c->r[R_DX] = 0x0088;
    cpu_push16(c, c->r[R_DX]);
    c->r[R_BX] = 0x0009;
    cpu_push16(c, c->r[R_BX]);
    c->r[R_SI] = 0xDC18;
    cpu_push16(c, c->r[R_SI]);
    c->icount += 11;
    if (!guest_call(m, 0x8A32, 0x0BFE)) return 1;
    ST2_NEED(13, 0x0BFE);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x000A, 1, 0);
    ds_put(c, 0xDC06, 0x0003);
    c->r[R_AX] = 0x00C1;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x00DD;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0088;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0009;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0xDC02;
    cpu_push16(c, c->r[R_AX]);
    c->icount += 12;
    if (!guest_call(m, 0x8A32, 0x0C1E)) return 1;
    ST2_NEED(3, 0x0C1E);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x000A, 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    /* 0C23 jmp 0xd60 */
    c->icount += 3;
    goto leave;
weapon:                                                           /* 0x00C26: a weapon row: the previous item, wrapping to 14h */
    ST2_NEED(6, 0x0C26);
    alu_sub(c, bp_get(c, 4), 0, 1, 0);
    c->icount += 2;
    if (c->flags & F_ZF) { bp_put(c, 4, 0x14); c->icount += 1; }
    bp_put(c, 4, (uint16_t)alu_dec(c, bp_get(c, 4), 1));
    c->r[R_AX] = bp_get(c, 4);
    bp_put(c, -8, c->r[R_AX]);
    c->icount += 3;
    ST2_NEED(12, 0x0C3A);
    ds_put(c, 0xDC1C, 0x0003);
    c->r[R_CX] = 0x00C1;
    cpu_push16(c, c->r[R_CX]);
    c->r[R_CX] = 0x00DD;
    cpu_push16(c, c->r[R_CX]);
    c->r[R_CX] = 0x00AA;
    cpu_push16(c, c->r[R_CX]);
    c->r[R_CX] = 0x0009;
    cpu_push16(c, c->r[R_CX]);
    c->r[R_CX] = 0xDC18;
    cpu_push16(c, c->r[R_CX]);
    c->icount += 11;
    if (!guest_call(m, 0x8A32, 0x0C57)) return 1;
    ST2_NEED(1, 0x0C57);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x000A, 1, 0);
    c->icount += 1;
    ST2_NEED(7, 0x0C5A);
    bp_put(c, -0x0A, 3);                                          /* the picture sheet 3, or 4 from item 10 on */
    alu_sub(c, bp_get(c, -8), 9, 1, 0);
    c->icount += 3;
    if (x86_cond(c, 0x7)) {                                       /* ja */
        bp_put(c, -0x0A, 4);
        c->r[R_AX] = bp_get(c, -8);
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], 0x0A, 1, 0);
        bp_put(c, -8, c->r[R_AX]);
        c->icount += 4;
    }
    ST2_NEED(16, 0x0C73);
    c->r[R_AX] = 0x0028;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x00A0;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0088;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x003F;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0001;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_BX] = bp_get(c, -0x8);
    c->r[R_BX] = x86_shift(c, 4, c->r[R_BX], 1, 1);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0246)));
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x0232)));
    cpu_push16(c, bp_get(c, -0xA));
    c->icount += 15;
    if (!guest_call_far(m, 0x0C97, 0x0C9C)) return 1;
    ST2_NEED(1, 0x0C9C);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0010, 1, 0);
    c->icount += 1;
    ST2_NEED(4, 0x0C9F);
    alu_sub(c, bp_get(c, -0x0A), 3, 1, 0);
    c->icount += 2;
    if (c->flags & F_ZF) { bp_put(c, -4, 0xCC); c->icount += 2; }
    else { bp_put(c, -4, 0x106); c->icount += 1; }
    ST2_NEED(19, 0x0CB1);
    c->r[R_AX] = 0x0006;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 0x003A;
    cpu_push16(c, c->r[R_CX]);
    c->r[R_DX] = 0x0088;
    cpu_push16(c, c->r[R_DX]);
    c->r[R_BX] = 0x0009;
    cpu_push16(c, c->r[R_BX]);
    c->r[R_SI] = 0x0001;
    cpu_push16(c, c->r[R_SI]);
    c->r[R_AX] = 0x000C;
    x86_mul16(c, bp_get(c, -0x8));
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x0050, 1, 0);
    bp_put(c, -0x6, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, bp_get(c, -0x4));
    c->r[R_DX] = 0x0002;
    cpu_push16(c, c->r[R_DX]);
    c->icount += 18;
    if (!guest_call_far(m, 0x0CD9, 0x0CDE)) return 1;
    ST2_NEED(17, 0x0CDE);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0010, 1, 0);
    c->r[R_AX] = 0x0006;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x003A;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 0x0090;
    cpu_push16(c, c->r[R_CX]);
    c->r[R_CX] = 0x0009;
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_DX] = bp_get(c, -0x6);
    c->r[R_DX] = (uint16_t)alu_add(c, c->r[R_DX], 0x0006, 1, 0);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, bp_get(c, -0x4));
    c->r[R_DX] = 0x0002;
    cpu_push16(c, c->r[R_DX]);
    c->icount += 16;
    if (!guest_call_far(m, 0x0D00, 0x0D05)) return 1;
    ST2_NEED(1, 0x0D05);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0010, 1, 0);
    c->icount += 1;
    ST2_NEED(20, 0x0D08);
    ds_put(c, 0xDC18, c->r[R_SI]);
    ds_put(c, 0xDC24, 0x0004);
    ds_put(c, 0xDC1C, 0x000F);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 0x0008;
    ds_put(c, 0xDC1E, c->r[R_CX]);
    cpu_push16(c, c->r[R_CX]);
    c->r[R_CX] = 0x00D5;
    cpu_push16(c, c->r[R_CX]);
    c->r[R_DX] = 0x00AA;
    cpu_push16(c, c->r[R_DX]);
    c->r[R_DX] = 0x0009;
    cpu_push16(c, c->r[R_DX]);
    c->r[R_BX] = bp_get(c, 0x4);
    c->r[R_BX] = x86_shift(c, 4, c->r[R_BX], 1, 1);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0xA587)));
    c->r[R_BX] = 0xDC18;
    cpu_push16(c, c->r[R_BX]);
    c->icount += 19;
    if (!guest_call(m, 0x3776, 0x0D3F)) return 1;
    ST2_NEED(15, 0x0D3F);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x000E, 1, 0);
    c->r[R_AX] = 0x003A;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x00D5;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x0088;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = 0x0009;
    cpu_push16(c, c->r[R_CX]);
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_SI]);
    c->icount += 14;
    if (!guest_call_far(m, 0x0D58, 0x0D5D)) return 1;
    ST2_NEED(1, 0x0D5D);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0010, 1, 0);
    c->icount += 1;
leave:                                                            /* 0x00D60 */
    ST2_NEED(4, 0x0D60);
    c->r[R_SI] = cpu_pop16(c);
    c->icount += 1;
    frame_close_ret(c);
    c->icount += 3;
    return 1;
}

/* ---- VGAME, second batch of matched routines (Phase 2) -------------------
 *
 * Most of these are the flight program's own glue: a routine that formats a
 * text, lights a lamp or arms a unit, calling the runtime's helpers
 * (0x0EB50 string copy, 0x0EB10 append, 0x0EB82 length, 0x0EB9E number to
 * text) and the display routines. Each is held to the original the same
 * way: the callee runs as original code from the state the original would
 * have reached, and the stretch after it needs room for its instructions
 * (counting the CALL that ends it) or the routine leaves IP at the return
 * address for the original to finish. A room check covers the longest path
 * of its stretch; a shorter path takes fewer clocks, counted exactly. */

/* A near call to target_ returning to ret_, then room for the next_
 * instructions after it (counting the next CALL); otherwise IP is left at
 * the return address for the original to carry on. */
#define VG2_NEAR(target_, ret_, next_) do {                                           \
        if (!guest_call(m, (target_), (ret_))) return 1;                              \
        if (!room(c, (next_))) { c->ip = (ret_); return 1; }                          \
    } while (0)
/* The same for a far call at CS:ip_ (9A off seg; five bytes). */
#define VG2_FAR(ip_, next_) do {                                                      \
        if (!guest_call_far(m, (ip_), (uint16_t)((ip_) + 5))) return 1;               \
        if (!room(c, (next_))) { c->ip = (uint16_t)((ip_) + 5); return 1; }           \
    } while (0)
/* The routine's BP frame (and a byte of the data segment), read and written
 * where the original does, with SS and DS taken from the machine each time. */
#define VG2_FRAME(o) seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + (o)))
#define VG2_SETFRAME(o, v) seg_write16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + (o)), (v))
#define VG2_DS8(off) mem_read8(c, phys(c->seg[S_DS], (uint16_t)(off)))
#define VG2_SETDS8(off, v) mem_write8(c, phys(c->seg[S_DS], (uint16_t)(off)), (uint8_t)(v))
/* POP BX twice: the caller's release of two pushed arguments. */
#define VG2_POP2() do { c->r[R_BX] = cpu_pop16(c); c->r[R_BX] = cpu_pop16(c); } while (0)
/* MOV r8, imm / MOV r8, m8 into one half of AX or BX. */
#define VG2_SET_LOW(reg, v) (c->r[reg] = (uint16_t)((c->r[reg] & 0xFF00) | (uint8_t)(v)))

/* VGAME 0x039C0, cockpit_number(field, n): a gauge's number. n goes to
 * text by 0x0EB9E in B2A0; a number of 100 or more is shown from there,
 * anything smaller from the three-byte field at 2E7C, whose last two bytes
 * take the two digits (a '0' in front of a single one). 0x0D667 draws the
 * text at the gauge field (0, field, text). */
static int vgame_cockpit_number(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;
    x86_enter(c, 2, 0);
    cpu_push16(c, 10);                                            /* radix */
    cpu_push16(c, 0xB2A0);
    cpu_push16(c, VG2_FRAME(6));                                  /* n */
    c->icount += 4;
    VG2_NEAR(0xEB9E, 0x39CF, 15);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    VG2_SETFRAME(-2, 0x2E7C);
    unsigned n = 4;
    alu_sub(c, VG2_FRAME(6), 0x63, 1, 0);
    if (!x86_cond(c, 0xE)) {                                      /* jle not taken: three digits */
        VG2_SETFRAME(-2, 0xB2A0);
        n += 2;
    } else {
        alu_sub(c, VG2_FRAME(6), 9, 1, 0);
        if (!x86_cond(c, 0xE)) {                                  /* two digits */
            VG2_SET_LOW(R_AX, VG2_DS8(0xB2A0));
            VG2_SETDS8(0x2E7D, c->r[R_AX]);
            VG2_SET_LOW(R_AX, VG2_DS8(0xB2A1));
            n += 7;
        } else {                                                  /* one digit, a '0' before it */
            VG2_SETDS8(0x2E7D, 0x30);
            VG2_SET_LOW(R_AX, VG2_DS8(0xB2A0));
            n += 5;
        }
        VG2_SETDS8(0x2E7E, c->r[R_AX]);
    }
    cpu_push16(c, VG2_FRAME(-2));                                 /* text */
    cpu_push16(c, VG2_FRAME(4));                                  /* field */
    cpu_push16(c, 0);
    c->icount += n + 3;
    VG2_NEAR(0xD667, 0x3A0B, 2);
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x045E0, countermeasure_gauge(kind): while the cockpit is shown
 * ([368C] set) the count of countermeasure `kind` (words at 3668) goes to
 * text in DEE0 by 0x0EB9E; a count of ten or more is drawn from there,
 * a single digit is copied into the second byte of the field at 3DD2 so
 * it reads as "0n". 0x0D667 draws it at the kind's gauge field (word table
 * at 3DCA). */
static int vgame_countermeasure_gauge(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 11)) return 0;
    x86_enter(c, 4, 0);
    alu_sub(c, ds_get(c, 0x368C), 0, 1, 0);
    if (c->flags & F_ZF) {                                        /* the cockpit is hidden */
        x86_leave(c);
        c->icount += 5;
        near_ret(c);
        return 1;
    }
    cpu_push16(c, 10);
    cpu_push16(c, 0xDEE0);
    c->r[R_BX] = x86_shift(c, 4, VG2_FRAME(4), 1, 1);             /* shl bx, 1 */
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x3668));
    VG2_SETFRAME(-4, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 10;
    VG2_NEAR(0xEB9E, 0x4600, 12);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    VG2_SETFRAME(-2, 0x3DD2);
    alu_sub(c, VG2_FRAME(-4), 9, 1, 0);
    if (!x86_cond(c, 0xE)) {                                      /* ten or more */
        VG2_SETFRAME(-2, 0xDEE0);
    } else {                                                      /* one digit: the second byte of 3DD2 */
        VG2_SET_LOW(R_AX, VG2_DS8(0xDEE0));
        VG2_SETDS8(0x3DD3, c->r[R_AX]);
    }
    cpu_push16(c, VG2_FRAME(-2));
    c->r[R_BX] = x86_shift(c, 4, VG2_FRAME(4), 1, 1);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x3DCA)));
    cpu_push16(c, 0);
    c->icount += 11;
    VG2_NEAR(0xD667, 0x462C, 2);
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x04B03, cockpit_target_name(t): the description of target t (its
 * 16-byte record at B2CE: the place's name index at +0, the object kind
 * at +14) built in the message buffer 98A6. The kind's name (string
 * pointers at DF08, indexed by the low seven bits of the kind byte) is
 * copied in; when the place has a name, ", " (3E02) follows - only if the
 * kind's name was not empty - and then the place's name. A result longer
 * than 25 characters is cut at 18 with a '.' (98BE). DI and SI are
 * restored; BX, CX and the flags are as the last step leaves them. */
static int vgame_cockpit_target_name(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 17)) return 0;
    const uint16_t target = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    uint16_t bx = x86_shift(c, 4, target, 4, 1);                  /* shl bx, 4 */
    c->r[R_AX] = (uint16_t)(bx - 0x4D24);                         /* lea ax, [bx-4D24]: the kind byte */
    c->r[R_SI] = c->r[R_AX];
    c->r[R_CX] = bx;
    bx = (uint16_t)((bx & 0xFF00) | VG2_DS8(c->r[R_SI]));
    bx = (uint16_t)alu_logic(c, bx & 0x7F, 1);
    bx = x86_shift(c, 4, bx, 1, 1);
    c->r[R_BX] = bx;
    cpu_push16(c, ds_get(c, (uint16_t)(bx - 0x20F8)));            /* the kind's name */
    cpu_push16(c, 0x98A6);
    c->r[R_DI] = c->r[R_SI];
    c->r[R_SI] = c->r[R_CX];
    c->icount += 16;
    VG2_NEAR(0xEB50, 0x4B2B, 6);                                  /* strcpy */
    VG2_POP2();
    c->r[R_BX] = ds_get(c, (uint16_t)(c->r[R_SI] - 0x4D32));      /* the place's name index */
    c->r[R_BX] = x86_shift(c, 4, c->r[R_BX], 1, 1);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] - 0x20F8)));
    c->icount += 5;
    VG2_NEAR(0xEB82, 0x4B3A, 3 + 5);                              /* strlen of the place's name */
    c->r[R_BX] = cpu_pop16(c);
    alu_logic(c, c->r[R_AX], 1);                                  /* or ax, ax */
    c->icount += 3;
    if (!(c->flags & F_ZF)) {                                     /* the place has a name */
        c->r[R_BX] = (uint16_t)((c->r[R_BX] & 0xFF00) | VG2_DS8(c->r[R_DI]));
        c->r[R_BX] = (uint16_t)alu_logic(c, c->r[R_BX] & 0x7F, 1);
        c->r[R_BX] = x86_shift(c, 4, c->r[R_BX], 1, 1);
        cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] - 0x20F8)));
        c->icount += 4;
        VG2_NEAR(0xEB82, 0x4B4D, 3 + 7);                          /* strlen of the kind's name */
        c->r[R_BX] = cpu_pop16(c);
        alu_logic(c, c->r[R_AX], 1);
        c->icount += 3;
        if (!(c->flags & F_ZF)) {                                 /* both: a separator between */
            cpu_push16(c, 0x3E02);
            cpu_push16(c, 0x98A6);
            c->icount += 2;
            VG2_NEAR(0xEB10, 0x4B5B, 2 + 7);                      /* strcat */
            VG2_POP2();
            c->icount += 2;
        }
        c->r[R_BX] = x86_shift(c, 4, VG2_FRAME(4), 4, 1);         /* the place's name, appended */
        c->r[R_BX] = ds_get(c, (uint16_t)(c->r[R_BX] - 0x4D32));
        c->r[R_BX] = x86_shift(c, 4, c->r[R_BX], 1, 1);
        cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] - 0x20F8)));
        cpu_push16(c, 0x98A6);
        c->icount += 6;
        VG2_NEAR(0xEB10, 0x4B73, 2 + 2);
        VG2_POP2();
        c->icount += 2;
    }
    cpu_push16(c, 0x98A6);
    c->icount += 1;
    VG2_NEAR(0xEB82, 0x4B7B, 3 + 2 + 4);                          /* strlen of the result */
    c->r[R_BX] = cpu_pop16(c);
    alu_sub(c, c->r[R_AX], 0x19, 1, 0);
    c->icount += 3;
    if (!x86_cond(c, 0xE)) {                                      /* longer than 25: cut it */
        VG2_SETDS8(0x98BE, 0x2E);
        VG2_SETDS8(0x98BF, 0);
        c->icount += 2;
    }
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x04B8F, objective_place(t): the place of target t in the message
 * buffer 98A6 - the place's own name (index at +0 of the record at B2CE)
 * or, when that name is empty, the name of the kind of object (the low
 * seven bits of its byte at +14); names are the string pointers at DF08.
 * More than 18 characters keep 18, with a '.' after them. SI is restored. */
static int vgame_objective_place(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 11)) return 0;
    const uint16_t target = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    VG2_SETDS8(0x98A6, 0);
    uint16_t bx = x86_shift(c, 4, target, 4, 1);                  /* shl bx, 4 */
    c->r[R_SI] = ds_get(c, (uint16_t)(bx - 0x4D32));
    c->r[R_SI] = x86_shift(c, 4, c->r[R_SI], 1, 1);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_SI] - 0x20F8)));
    c->r[R_SI] = bx;
    c->r[R_BX] = bx;
    c->icount += 10;
    VG2_NEAR(0xEB82, 0x4BAD, 3 + 4 + 4);                          /* strlen of the place's name */
    c->r[R_BX] = cpu_pop16(c);
    alu_logic(c, c->r[R_AX], 1);                                  /* or ax, ax */
    c->icount += 3;
    if (!(c->flags & F_ZF)) {
        c->r[R_BX] = ds_get(c, (uint16_t)(c->r[R_SI] - 0x4D32));  /* the place's own name */
        c->icount += 2;                                           /* mov, jmp */
    } else {                                                      /* the kind of object */
        bx = x86_shift(c, 4, VG2_FRAME(4), 4, 1);
        bx = (uint16_t)((bx & 0xFF00) | VG2_DS8((uint16_t)(bx - 0x4D24)));
        c->r[R_BX] = (uint16_t)alu_logic(c, bx & 0x7F, 1);
        c->icount += 4;
    }
    c->r[R_BX] = x86_shift(c, 4, c->r[R_BX], 1, 1);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] - 0x20F8)));
    cpu_push16(c, 0x98A6);
    c->icount += 3;
    VG2_NEAR(0xEB50, 0x4BD1, 4);                                  /* strcpy */
    VG2_POP2();
    cpu_push16(c, 0x98A6);
    c->icount += 3;
    VG2_NEAR(0xEB82, 0x4BD9, 3 + 2 + 3);                          /* strlen of the result */
    c->r[R_BX] = cpu_pop16(c);
    alu_sub(c, c->r[R_AX], 0x12, 1, 0);
    c->icount += 3;
    if (!x86_cond(c, 0xE)) {                                      /* longer than 18: cut it */
        VG2_SETDS8(0x98B8, 0x2E);
        VG2_SETDS8(0x98B9, 0);
        c->icount += 2;
    }
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x04FD8, frame_lamp_timers: the three timed lamps. Each timer
 * ([3DBC], [3DBE], [3DC0]) that is above zero counts down one frame and
 * keeps its lamp (display elements 0Eh, 0Fh and 18h) lit; at zero the
 * lamp is set off. set_element (0x5021) is called for each with the lamp
 * and 1 or 0. */
static int vgame_frame_lamp_timers(machine_t *m)
{
    cpu_t *c = &m->cpu;
    static const struct { uint16_t timer, element, ret_ip; } lamp[3] = {
        { 0x3DBC, 0x0E, 0x4FEE }, { 0x3DBE, 0x0F, 0x5006 }, { 0x3DC0, 0x18, 0x501E } };
    if (!room(c, 7)) return 0;
    for (int k = 0; k < 3; k++) {
        alu_sub(c, ds_get(c, lamp[k].timer), 0, 1, 0);            /* cmp [timer], 0 */
        unsigned n = 4;                                           /* cmp, je, push, push */
        uint16_t lit = 0;
        if (!(c->flags & F_ZF)) {                                 /* running: dec, push 1, jmp */
            ds_put(c, lamp[k].timer, (uint16_t)alu_dec(c, ds_get(c, lamp[k].timer), 1));
            lit = 1;
            n += 2;
        }
        cpu_push16(c, lit);
        cpu_push16(c, lamp[k].element);
        c->icount += n;
        VG2_NEAR(0x5021, lamp[k].ret_ip, k < 2 ? 2 + 7 : 3);
        VG2_POP2();
        c->icount += 2;
    }
    near_ret(c);
    c->icount += 1;
    return 1;
}

/* VGAME 0x0761A, aircraft_damage: unless the damage is switched off
 * (bit 4 of [9B35]), [3686]+1 hits: each picks one of eight systems with
 * the scaled random number 0x0C88C(8), sets its bit in the damage word
 * [3664] and counts a hit in [C5F4]. The damage lamps are then redrawn
 * (0x083E9 with 16h), [991E] is set and sound request 4 with argument 2
 * goes to 0x0D3F9. */
static int vgame_aircraft_damage(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 10)) return 0;
    x86_enter(c, 2, 0);
    alu_logic(c, VG2_DS8(0x9B35) & 0x10, 0);                      /* test byte [9B35], 10h */
    if (!(c->flags & F_ZF)) {                                     /* damage is off */
        x86_leave(c);
        c->icount += 5;
        near_ret(c);
        return 1;
    }
    VG2_SETFRAME(-2, 0);
    c->r[R_AX] = ds_get(c, 0x3686);
    alu_sub(c, VG2_FRAME(-2), c->r[R_AX], 1, 0);
    unsigned n = 8;                                               /* up to the loop test's jle */
    while (x86_cond(c, 0xE)) {                                    /* jle: another hit */
        cpu_push16(c, 8);
        c->icount += n + 1;
        VG2_NEAR(0xC88C, 0x7631, 12);
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_CX] = c->r[R_AX];
        c->r[R_AX] = x86_shift(c, 4, 1, (uint8_t)c->r[R_CX], 1);  /* shl ax, cl */
        ds_put(c, 0x3664, (uint16_t)alu_logic(c, ds_get(c, 0x3664) | c->r[R_AX], 1));
        ds_put(c, 0xC5F4, (uint16_t)alu_inc(c, ds_get(c, 0xC5F4), 1));
        VG2_SETFRAME(-2, (uint16_t)alu_inc(c, VG2_FRAME(-2), 1));
        c->r[R_AX] = ds_get(c, 0x3686);
        alu_sub(c, VG2_FRAME(-2), c->r[R_AX], 1, 0);
        n = 10;                                                   /* pop .. jle */
    }
    cpu_push16(c, 0x16);
    c->icount += n + 1;
    VG2_NEAR(0x83E9, 0x7651, 5);
    c->r[R_BX] = cpu_pop16(c);
    ds_put(c, 0x991E, 1);
    cpu_push16(c, 2);
    cpu_push16(c, 4);
    c->icount += 4;
    VG2_NEAR(0xD3F9, 0x765F, 4);
    VG2_POP2();
    x86_leave(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x083E9, keys_display_refresh(page): for the damage display (page
 * 16h) the seven system lamps (cockpit_lamp, 0x0889B, for lamps 0Ah to
 * 10h) are redrawn: colour 29h for a system whose bit is set in the damage
 * word [3664], 0Ah otherwise. Then, when page is the display now shown
 * ([E008]), 0x0825D is called on it. */
static int vgame_keys_display_refresh(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t page = arg(c, 0);
    if (!room(c, page == 0x16 ? 17 : 8)) return 0;
    x86_enter(c, 2, 0);
    alu_sub(c, page, 0x16, 1, 0);
    unsigned n = 3;                                               /* enter, cmp, jne */
    if (c->flags & F_ZF) {
        VG2_SETFRAME(-2, 0);
        n += 1;
        do {
            VG2_SET_LOW(R_CX, VG2_FRAME(-2));                     /* mov cl, [bp-2] */
            c->r[R_AX] = x86_shift(c, 4, 1, (uint8_t)c->r[R_CX], 1);
            c->r[R_AX] = (uint16_t)alu_logic(c, c->r[R_AX] & ds_get(c, 0x3664), 1);
            alu_sub(c, c->r[R_AX], 1, 1, 0);                      /* cmp ax, 1: CF if the bit is clear */
            c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, (c->flags & F_CF) ? 1u : 0u);   /* sbb ax, ax */
            VG2_SET_LOW(R_AX, alu_logic(c, c->r[R_AX] & 0xE1, 0));
            c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x29, 1, 0);
            cpu_push16(c, c->r[R_AX]);                            /* the colour */
            c->r[R_AX] = (uint16_t)alu_add(c, VG2_FRAME(-2), 0x0A, 1, 0);
            cpu_push16(c, c->r[R_AX]);                            /* the lamp */
            c->icount += n + 12;
            VG2_NEAR(0x889B, 0x8419, 5 + 13);
            VG2_POP2();
            VG2_SETFRAME(-2, (uint16_t)alu_inc(c, VG2_FRAME(-2), 1));
            alu_sub(c, VG2_FRAME(-2), 7, 1, 0);
            n = 5;                                                /* pop, pop, inc, cmp, jl */
        } while (x86_cond(c, 0xC));
    }
    c->r[R_AX] = ds_get(c, 0xE008);
    alu_sub(c, VG2_FRAME(4), c->r[R_AX], 1, 0);
    n += 3;
    if (!(c->flags & F_ZF)) {                                     /* not the display shown */
        x86_leave(c);
        c->icount += n + 2;
        near_ret(c);
        return 1;
    }
    cpu_push16(c, VG2_FRAME(4));
    c->icount += n + 1;
    VG2_NEAR(0x825D, 0x8432, 3);
    c->r[R_BX] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* The five pushes of a display_line block: text, then the column and row
 * as screen coordinates (x0 + 4 * column, 6E + 6 * row), then the colour. */
static void vg2_display_line_args(cpu_t *c, uint16_t x0)
{
    cpu_push16(c, VG2_FRAME(0x0C));                               /* colour */
    c->r[R_AX] = x86_imul3(c, VG2_FRAME(8), 6);                   /* imul ax, [bp+8], 6 */
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x6E, 1, 0);
    cpu_push16(c, c->r[R_AX]);                                    /* y */
    c->r[R_AX] = x86_shift(c, 4, VG2_FRAME(6), 2, 1);             /* mov ax, [bp+6] / shl ax, 2 */
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], x0, 1, 0);
    cpu_push16(c, c->r[R_AX]);                                    /* x */
    cpu_push16(c, VG2_FRAME(0x0A));                               /* text */
}

/* VGAME 0x0890E, display_line(side, column, row, text, colour): while the
 * cockpit is shown ([368C]), text drawn by 0x0895E on one of the two side
 * displays: side 1 from x = 4A + 4 * column, side 2 from A9 + 4 * column,
 * at y = 6E + 6 * row. Any other side draws nothing. */
static int vgame_display_line(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 18)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    alu_sub(c, ds_get(c, 0x368C), 0, 1, 0);
    if (c->flags & F_ZF) {                                        /* the cockpit is hidden */
        x86_leave(c);
        c->icount += 6;
        near_ret(c);
        return 1;
    }
    unsigned n = 4;                                               /* push bp, mov, cmp, je */
    alu_sub(c, VG2_FRAME(4), 1, 1, 0);
    n += 2;
    if (c->flags & F_ZF) {                                        /* side 1 */
        vg2_display_line_args(c, 0x4A);
        c->icount += n + 9;
        VG2_NEAR(0x895E, 0x8939, 1 + 2 + 10);
        c->r[R_SP] = c->r[R_BP];                                  /* mov sp, bp */
        n = 1;
    }
    alu_sub(c, VG2_FRAME(4), 2, 1, 0);
    n += 2;
    if (c->flags & F_ZF) {                                        /* side 2 */
        vg2_display_line_args(c, 0xA9);
        c->icount += n + 9;
        VG2_NEAR(0x895E, 0x895C, 2);
        n = 0;
    }
    x86_leave(c);
    c->icount += n + 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x0927F, nav_bar(value, colour): the fuel bar on the navigation
 * display. The value is clamped (0x0C67A, to 0..10000); a bar longer than
 * the 86h stub is drawn in `colour` (pen 0x0886A) as a box from x = AA to
 * AA + value / 87h, y = A4 to A7, on the current page ([4028] when the
 * flag [40B6] is set, otherwise [4010]) by the library's 0FB2:0246. */
static int vgame_nav_bar(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5)) return 0;
    x86_enter(c, 2, 0);
    cpu_push16(c, 0x2710);
    cpu_push16(c, 0);
    cpu_push16(c, VG2_FRAME(4));
    c->icount += 4;
    VG2_NEAR(0xC67A, 0x928E, 6);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    VG2_SETFRAME(4, c->r[R_AX]);
    alu_sub(c, c->r[R_AX], 0x86, 1, 0);
    c->icount += 4;
    if (x86_cond(c, 0xE)) {                                       /* jle: nothing to draw */
        x86_leave(c);
        c->icount += 2;
        near_ret(c);
        return 1;
    }
    cpu_push16(c, VG2_FRAME(6));
    c->icount += 1;
    VG2_NEAR(0x886A, 0x929F, 16);
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, 0xA7);
    c->r[R_AX] = VG2_FRAME(4);
    c->r[R_CX] = 0x87;
    c->r[R_DX] = (c->r[R_AX] & 0x8000) ? 0xFFFF : 0;              /* cdq */
    x86_idiv16(c, 0x87, 0);                                       /* by 135: it cannot fault */
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0xAA, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, 0xA4);
    cpu_push16(c, 0xAA);
    alu_sub(c, VG2_DS8(0x40B6), 0, 0, 0);                         /* cmp byte [40B6], 0 */
    unsigned n = 11;                                              /* pop .. cmp */
    if (c->flags & F_ZF) {
        c->r[R_AX] = ds_get(c, 0x4010);
        n += 2;                                                   /* je, mov */
    } else {
        c->r[R_AX] = ds_get(c, 0x4028);
        n += 3;                                                   /* je, mov, jmp */
    }
    cpu_push16(c, c->r[R_AX]);                                    /* the page */
    c->icount += n + 1;
    VG2_FAR(0x92C6, 2);
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x0B577, recon_range_text(range): "<prefix><whole><point><tenths><unit>"
 * built in the message buffer 98A6 for a range in 64ths: the prefix
 * (4338), range / 64 as decimal (0x0EB9E into DED6), the point (433F),
 * (range & 3F) * 2 / 13 - the 64ths as tenths - and the unit (4341); the
 * numbers are appended by the runtime's strcat (0x0EB10). */
static int vgame_recon_range_text(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 4)) return 0;
    x86_enter(c, 0x0A, 0);
    cpu_push16(c, 0x4338);
    cpu_push16(c, 0x98A6);
    c->icount += 3;
    VG2_NEAR(0xEB50, 0x0B584, 8);                                 /* the prefix */
    VG2_POP2();
    cpu_push16(c, 0x0A);
    cpu_push16(c, 0xDED6);
    c->r[R_AX] = x86_shift(c, 7, VG2_FRAME(4), 6, 1);             /* sar ax, 6 */
    cpu_push16(c, c->r[R_AX]);
    c->icount += 7;
    VG2_NEAR(0xEB9E, 0xB595, 4);                                  /* the whole part as decimal */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, 0x98A6);
    c->icount += 3;
    VG2_NEAR(0xEB10, 0xB59F, 5);                                  /* appended */
    VG2_POP2();
    cpu_push16(c, 0x433F);
    cpu_push16(c, 0x98A6);
    c->icount += 4;
    VG2_NEAR(0xEB10, 0xB5AA, 12);                                 /* the point */
    VG2_POP2();
    cpu_push16(c, 0x0A);
    cpu_push16(c, 0xDED6);
    VG2_SET_LOW(R_AX, VG2_FRAME(4));                              /* mov al, [bp+4] */
    c->r[R_AX] = (uint16_t)alu_logic(c, c->r[R_AX] & 0x3F, 1);    /* and ax, 3Fh */
    c->r[R_AX] = x86_shift(c, 4, c->r[R_AX], 1, 1);               /* shl ax, 1 */
    c->r[R_CX] = 0x0D;
    c->r[R_DX] = (c->r[R_AX] & 0x8000) ? 0xFFFF : 0;              /* cdq */
    x86_idiv16(c, 0x0D, 0);                                       /* by 13: it cannot fault */
    cpu_push16(c, c->r[R_AX]);
    c->icount += 11;
    VG2_NEAR(0xEB9E, 0xB5C3, 4);                                  /* the tenths as decimal */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, 0x98A6);
    c->icount += 3;
    VG2_NEAR(0xEB10, 0xB5CD, 5);
    VG2_POP2();
    cpu_push16(c, 0x4341);
    cpu_push16(c, 0x98A6);
    c->icount += 4;
    VG2_NEAR(0xEB10, 0xB5D8, 4);                                  /* the unit */
    VG2_POP2();
    x86_leave(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}

/* At a point where the original may be stopped, the pending instructions
 * (n_) are counted and the next stretch needs `need_` of them: otherwise IP
 * is left at ip_, with the machine as the original has it there. */
#define VG2_ROOM_OR_STOP(need_, ip_) do {                                             \
        c->icount += n; n = 0;                                                        \
        if (!room(c, (need_))) { c->ip = (uint16_t)(ip_); return 1; }                 \
    } while (0)

/* VGAME 0x0792E, ground_impact_eligible(target): 0 when the world object a
 * ground impact would destroy is already destroyed, 1 when it may be. The
 * object class (0x0B9F6) picks the destroyed-object byte, [C0D8] or
 * [DED0], sign-extended; the world cell found at [9F40] is already
 * destroyed if its first word equals that, or if its model record (word at
 * +0C) carries a destroyed mark - bit 7 of its byte at +6 with the low
 * seven bits equal to that word. SI is restored. */
static int vgame_ground_impact_eligible(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 4)) return 0;
    x86_enter(c, 2, 0);
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, VG2_FRAME(4));
    c->icount += 3;
    VG2_NEAR(0xB9F6, 0x7939, 20);
    c->r[R_BX] = cpu_pop16(c);
    alu_logic(c, c->r[R_AX], 1);                                  /* or ax, ax */
    unsigned n = 3;
    if (!(c->flags & F_ZF)) { VG2_SET_LOW(R_AX, VG2_DS8(0xC0D8)); n += 2; }   /* mov, jmp */
    else { VG2_SET_LOW(R_AX, VG2_DS8(0xDED0)); n += 1; }
    c->r[R_AX] = (uint16_t)(int16_t)(int8_t)c->r[R_AX];           /* cwde */
    c->r[R_BX] = ds_get(c, 0x9F40);
    alu_sub(c, ds_get(c, c->r[R_BX]), c->r[R_AX], 1, 0);          /* cmp [bx], ax */
    n += 4;
    int destroyed = (c->flags & F_ZF) != 0;
    if (!destroyed) {
        c->r[R_SI] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0C));
        alu_logic(c, VG2_DS8(c->r[R_SI] + 6) & 0x80, 0);          /* test byte [si+6], 80h */
        n += 3;
        if (!(c->flags & F_ZF)) {
            VG2_SET_LOW(R_AX, VG2_DS8(c->r[R_SI] + 6));
            c->r[R_AX] = (uint16_t)alu_logic(c, c->r[R_AX] & 0x7F, 1);
            alu_sub(c, c->r[R_AX], ds_get(c, c->r[R_BX]), 1, 0);
            n += 4;
            destroyed = (c->flags & F_ZF) != 0;
        }
    }
    if (destroyed) c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);   /* sub ax, ax */
    else c->r[R_AX] = 1;
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += n + 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x07594, frame_objective_mark(i): objective i (0 primary, 1
 * secondary) is marked done in [9B34]'s bit 4000h >> i, once; it returns 1
 * the first time, 0 after. A first-time kind 3 or 4 objective (word at +0
 * of its 18-byte record at E304) logs an event (0x04ABA, 8Bh for the
 * primary, 4Bh for the secondary, argument 0). The message is copied to
 * the buffer 98A6 (3EEB primary, 3EDC secondary), [2EAE] is set to 2 or 1
 * and bit 40h or 20h of [9B35] set; both bits set make [2EAE] 3. */
static int vgame_frame_objective_mark(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 20)) return 0;
    const uint16_t objective = arg(c, 0);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    VG2_SET_LOW(R_CX, objective);                                 /* mov cl, [bp+4] */
    c->r[R_AX] = x86_shift(c, 7, 0x4000, (uint8_t)c->r[R_CX], 1); /* sar ax, cl */
    alu_logic(c, ds_get(c, 0x9B34) & c->r[R_AX], 1);              /* test [9B34], ax */
    unsigned n = 7;
    if (!(c->flags & F_ZF)) {                                     /* already marked */
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
        x86_leave(c);
        c->icount += n + 3;
        near_ret(c);
        return 1;
    }
    c->r[R_BX] = x86_imul3(c, VG2_FRAME(4), 0x12);
    alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] - 0x1CFC)), 4, 1, 0);
    n += 3;
    int logged = (c->flags & F_ZF) != 0;
    if (!logged) {
        alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] - 0x1CFC)), 3, 1, 0);
        n += 2;
        logged = (c->flags & F_ZF) != 0;
    }
    if (logged) {
        cpu_push16(c, 0);
        alu_sub(c, VG2_FRAME(4), 1, 1, 0);                        /* cmp [bp+4], 1: CF for the primary */
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, (c->flags & F_CF) ? 1u : 0u);   /* sbb ax, ax */
        c->r[R_AX] = (uint16_t)alu_logic(c, c->r[R_AX] & 0x40, 1);
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x40, 1, 0);
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x0B, 1, 0);
        cpu_push16(c, c->r[R_AX]);
        c->icount += n + 7;
        VG2_NEAR(0x4ABA, 0x75D0, 7);
        VG2_POP2();
        n = 2;
    }
    alu_sub(c, VG2_FRAME(4), 0, 1, 0);
    n += 2;
    uint16_t ret_ip;
    if (!(c->flags & F_ZF)) { cpu_push16(c, 0x3EDC); ret_ip = 0x75E1; }
    else { cpu_push16(c, 0x3EEB); ret_ip = 0x75F9; }
    cpu_push16(c, 0x98A6);
    c->icount += n + 2;
    VG2_NEAR(0xEB50, ret_ip, 13);
    VG2_POP2();
    if (ret_ip == 0x75E1) {                                       /* secondary */
        ds_put(c, 0x2EAE, 1);
        VG2_SETDS8(0x9B35, alu_logic(c, VG2_DS8(0x9B35) | 0x20, 0));
        n = 5;                                                    /* pop, pop, mov, or, jmp */
    } else {                                                      /* primary */
        ds_put(c, 0x2EAE, 2);
        VG2_SETDS8(0x9B35, alu_logic(c, VG2_DS8(0x9B35) | 0x40, 0));
        n = 4;
    }
    VG2_SET_LOW(R_AX, VG2_DS8(0x9B35));
    VG2_SET_LOW(R_AX, alu_logic(c, c->r[R_AX] & 0x60, 0));
    alu_sub(c, c->r[R_AX] & 0xFF, 0x60, 0, 0);                    /* cmp al, 60h */
    n += 4;
    if (c->flags & F_ZF) { ds_put(c, 0x2EAE, 3); n += 1; }
    c->r[R_AX] = 1;
    x86_leave(c);
    c->icount += n + 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x08625, map_plot(x, y, colour, big): a marker at the world
 * position (x, y) on the moving map. The position goes to screen
 * coordinates (0x085F1, 0x08608) and must lie inside the map window
 * (x from [DEC0] to [E32E] - 1, y from [DEC2] to [E470] - 1): outside,
 * AX is 1 and nothing is drawn. Inside, AX is 0 and, unless the colour is
 * -1, the marker routine 0x08880 plots the point - and, when `big` is
 * set, the three points beside and below it for a 2x2 block. Nothing is
 * done while the map is off ([DF06] set) or the cockpit hidden ([368C]
 * clear). SI and DI are restored. */
static int vgame_map_plot(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 13)) return 0;
    x86_enter(c, 4, 0);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    unsigned n = 5;                                               /* enter, push, push, cmp, jne */
    alu_sub(c, ds_get(c, 0xDF06), 0, 1, 0);
    if (!(c->flags & F_ZF)) goto nothing;
    alu_sub(c, ds_get(c, 0x368C), 0, 1, 0);
    n += 2;
    if (!(c->flags & F_ZF)) goto shown;
nothing:                                                          /* 08639: AX = 0 */
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    n += 2;
    goto done;
shown:
    cpu_push16(c, VG2_FRAME(4));
    c->icount += n + 1;
    VG2_NEAR(0x85F1, 0x8644, 4);                                  /* screen x */
    c->r[R_BX] = cpu_pop16(c);
    VG2_SETFRAME(-2, c->r[R_AX]);
    cpu_push16(c, VG2_FRAME(6));
    c->icount += 3;
    VG2_NEAR(0x8608, 0x864E, 24);                                 /* screen y */
    c->r[R_BX] = cpu_pop16(c);
    VG2_SETFRAME(-4, c->r[R_AX]);
    n = 2;
    c->r[R_AX] = ds_get(c, 0xDEC0);
    alu_sub(c, VG2_FRAME(-2), c->r[R_AX], 1, 0);
    n += 3;
    if (x86_cond(c, 0xC)) goto outside;                           /* jl */
    c->r[R_AX] = (uint16_t)alu_dec(c, ds_get(c, 0xE32E), 1);
    alu_sub(c, c->r[R_AX], VG2_FRAME(-2), 1, 0);
    n += 4;
    if (x86_cond(c, 0xE)) goto outside;                           /* jle */
    c->r[R_AX] = ds_get(c, 0xDEC2);
    alu_sub(c, VG2_FRAME(-4), c->r[R_AX], 1, 0);
    n += 3;
    if (x86_cond(c, 0xC)) goto outside;
    c->r[R_AX] = (uint16_t)alu_dec(c, ds_get(c, 0xE470), 1);
    alu_sub(c, c->r[R_AX], VG2_FRAME(-4), 1, 0);
    n += 4;
    if (x86_cond(c, 0xE)) goto outside;
    alu_sub(c, VG2_FRAME(8), 0xFFFF, 1, 0);                       /* cmp [bp+8], -1 */
    n += 2;
    if (c->flags & F_ZF) goto nothing;
    cpu_push16(c, VG2_FRAME(8));
    cpu_push16(c, VG2_FRAME(-4));
    cpu_push16(c, VG2_FRAME(-2));
    c->icount += n + 3;
    VG2_NEAR(0x8880, 0x8686, 10);                                 /* the point */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    alu_sub(c, VG2_FRAME(0x0A), 0, 1, 0);
    n = 3;
    if (c->flags & F_ZF) goto nothing;
    cpu_push16(c, VG2_FRAME(8));
    cpu_push16(c, VG2_FRAME(-4));
    c->r[R_AX] = (uint16_t)alu_inc(c, VG2_FRAME(-2), 1);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_SI] = c->r[R_AX];
    c->icount += n + 6;
    VG2_NEAR(0x8880, 0x869F, 8);                                  /* the point to the right */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    cpu_push16(c, VG2_FRAME(8));
    c->r[R_AX] = (uint16_t)alu_inc(c, VG2_FRAME(-4), 1);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, VG2_FRAME(-2));
    c->r[R_DI] = c->r[R_AX];
    c->icount += 7;
    VG2_NEAR(0x8880, 0x86B2, 5);                                  /* the point below */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    cpu_push16(c, VG2_FRAME(8));
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    c->icount += 4;
    VG2_NEAR(0x8880, 0x86BD, 8);                                  /* the point diagonally */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    n = 2;                                                        /* add, jmp */
    goto nothing;
outside:                                                          /* 086C3 */
    c->r[R_AX] = 1;
    n += 1;
done:                                                             /* 086C6 */
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += n + 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x09216, map_overlay_route: the route on the navigation display,
 * from the aircraft ([C0D0], [C0DE]) through the waypoints that remain:
 * four (x, y) word pairs at 2E9E, four bytes apart, from the one at index
 * [2EAE] on; an x of zero is an empty slot and is skipped. Each leg is drawn
 * by 0x087C1; the pen is 0Fh for the first leg and 0Ah after it. Nothing
 * when the map is off ([DF06] set). SI is restored. */
static int vgame_map_overlay_route(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 10)) return 0;
    x86_enter(c, 6, 0);
    cpu_push16(c, c->r[R_SI]);
    alu_sub(c, ds_get(c, 0xDF06), 0, 1, 0);
    if (!(c->flags & F_ZF)) {
        c->r[R_SI] = cpu_pop16(c);
        x86_leave(c);
        c->icount += 7;
        near_ret(c);
        return 1;
    }
    c->r[R_AX] = ds_get(c, 0xC0D0);
    VG2_SETFRAME(-4, c->r[R_AX]);
    c->r[R_AX] = ds_get(c, 0xC0DE);
    VG2_SETFRAME(-6, c->r[R_AX]);
    cpu_push16(c, 0x0F);
    c->icount += 9;
    VG2_NEAR(0x886A, 0x9233, 6);                                  /* the pen */
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_AX] = ds_get(c, 0x2EAE);
    VG2_SETFRAME(-2, c->r[R_AX]);
    unsigned n = 4;                                               /* pop, mov, mov, jmp */
    for (;;) {
        alu_sub(c, VG2_FRAME(-2), 4, 1, 0);                       /* 09276: cmp [bp-2], 4 */
        n += 2;
        if (!x86_cond(c, 0xC)) break;                             /* jl */
        const int empty = ds_get(c, (uint16_t)((uint16_t)(VG2_FRAME(-2) << 2) + 0x2E9E)) == 0;
        VG2_ROOM_OR_STOP(empty ? 7 : 10, 0x923C);
        c->r[R_BX] = x86_shift(c, 4, VG2_FRAME(-2), 2, 1);        /* mov bx, [bp-2] / shl bx, 2 */
        alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x2E9E)), 0, 1, 0);
        n += 4;
        if (c->flags & F_ZF) {                                    /* an empty slot */
            VG2_SETFRAME(-2, (uint16_t)alu_inc(c, VG2_FRAME(-2), 1));
            n += 1;
            continue;
        }
        cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x2EA0)));
        cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x2E9E)));
        cpu_push16(c, VG2_FRAME(-6));
        cpu_push16(c, VG2_FRAME(-4));
        c->r[R_SI] = c->r[R_BX];
        c->icount += n + 5;
        n = 0;
        VG2_NEAR(0x87C1, 0x925C, 7);                              /* the leg */
        c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
        c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_SI] + 0x2E9E));
        VG2_SETFRAME(-4, c->r[R_AX]);
        c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_SI] + 0x2EA0));
        VG2_SETFRAME(-6, c->r[R_AX]);
        cpu_push16(c, 0x0A);
        c->icount += 6;
        VG2_NEAR(0x886A, 0x9272, 4);
        c->r[R_BX] = cpu_pop16(c);
        VG2_SETFRAME(-2, (uint16_t)alu_inc(c, VG2_FRAME(-2), 1));
        n = 2;                                                    /* pop, inc */
    }
    VG2_ROOM_OR_STOP(3, 0x927C);
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x08719, map_overlay_arc(x, y, radius, colour, join, a0, a1): the
 * warning arc round (x, y) on the map. The angles are bytes (a high byte
 * added to a0 makes a range that wraps, when a1 < a0), taken a step of 10h
 * at a time from a0 to a1 inclusive. For each, the point on the circle -
 * the far sine and cosine (0x0C818, 0x0C831) of the angle scaled to the
 * radius, added to x and subtracted from y - is clamped to 0 when above
 * C000h and then either plotted (0x08625, big 0), for the first point or
 * when `join` is 0, or joined to the previous one by a line (0x087C1).
 * The pen is the colour (0x0886A). */
static int vgame_map_overlay_arc(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7)) return 0;
    x86_enter(c, 0x0E, 0);
    c->r[R_AX] = VG2_FRAME(0x0E);
    alu_sub(c, VG2_FRAME(0x10), c->r[R_AX], 1, 0);                /* cmp [bp+10h], ax */
    unsigned n = 4;                                               /* enter, mov, cmp, jge */
    if (!x86_cond(c, 0xD)) {                                      /* a1 < a0: wrap the range */
        const uint32_t hi = phys(c->seg[S_SS], (uint16_t)(c->r[R_BP] + 0x0F));
        mem_write8(c, hi, (uint8_t)alu_add(c, mem_read8(c, hi), 1, 0, 0));
        n += 1;
    }
    cpu_push16(c, VG2_FRAME(0x0A));
    c->icount += n + 1;
    VG2_NEAR(0x886A, 0x872F, 13);                                 /* the pen */
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_AX] = VG2_FRAME(0x0E);
    VG2_SETFRAME(-6, c->r[R_AX]);
    n = 4;                                                        /* pop, mov, mov, jmp */
    goto test;
draw:                                                             /* 08738: plot the point */
    cpu_push16(c, 0);
    cpu_push16(c, VG2_FRAME(0x0A));
    cpu_push16(c, VG2_FRAME(-0x0A));
    cpu_push16(c, VG2_FRAME(-4));
    c->icount += n + 4;
    n = 0;
    VG2_NEAR(0x8625, 0x8746, 15);
tail:                                                             /* 08746: remember the point */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    c->r[R_AX] = VG2_FRAME(-4);
    VG2_SETFRAME(-8, c->r[R_AX]);
    c->r[R_AX] = VG2_FRAME(-0x0A);
    VG2_SETFRAME(-0x0E, c->r[R_AX]);
    {
        const uint16_t a = (uint16_t)(c->r[R_BP] - 6);
        seg_write16(c, c->seg[S_SS], a, (uint16_t)alu_add(c, seg_read16(c, c->seg[S_SS], a), 0x10, 1, 0));
    }
    n += 6;                                                       /* add sp .. add [bp-6] */
test:                                                             /* 08759 */
    c->r[R_AX] = VG2_FRAME(-6);
    alu_sub(c, VG2_FRAME(0x10), c->r[R_AX], 1, 0);
    n += 3;
    if (x86_cond(c, 0xC)) {                                       /* jl: past a1 */
        VG2_ROOM_OR_STOP(2, 0x87BF);
        x86_leave(c);
        c->icount += 2;
        near_ret(c);
        return 1;
    }
    VG2_ROOM_OR_STOP(6, 0x8761);
    cpu_push16(c, VG2_FRAME(8));                                  /* the radius */
    c->r[R_AX] = (uint16_t)((c->r[R_AX] << 8) | (c->r[R_AX] & 0xFF));   /* mov ah, al */
    VG2_SET_LOW(R_AX, alu_sub(c, c->r[R_AX] & 0xFF, c->r[R_AX] & 0xFF, 0, 0));   /* sub al, al */
    VG2_SETFRAME(-2, c->r[R_AX]);                                 /* the angle in the high byte */
    cpu_push16(c, c->r[R_AX]);
    c->icount += 5;
    VG2_NEAR(0xC818, 0x876F, 7);
    VG2_POP2();
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], VG2_FRAME(4), 1, 0);
    VG2_SETFRAME(-4, c->r[R_AX]);                                 /* x */
    cpu_push16(c, VG2_FRAME(8));
    cpu_push16(c, VG2_FRAME(-2));
    c->icount += 6;
    VG2_NEAR(0xC831, 0x8780, 21);
    VG2_POP2();
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], VG2_FRAME(6), 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);       /* neg ax */
    VG2_SETFRAME(-0x0A, c->r[R_AX]);                              /* y */
    alu_sub(c, VG2_FRAME(-4), 0xC000, 1, 0);
    n = 7;
    if (x86_cond(c, 0x7)) { VG2_SETFRAME(-4, 0); n += 1; }        /* ja: off the map */
    alu_sub(c, c->r[R_AX], 0xC000, 1, 0);
    n += 2;
    if (x86_cond(c, 0x7)) { VG2_SETFRAME(-0x0A, 0); n += 1; }
    c->r[R_AX] = VG2_FRAME(-6);
    alu_sub(c, VG2_FRAME(0x0E), c->r[R_AX], 1, 0);
    n += 3;
    if (c->flags & F_ZF) goto draw;                               /* the first point */
    alu_sub(c, VG2_FRAME(0x0C), 0, 1, 0);
    n += 2;
    if (c->flags & F_ZF) goto draw;                               /* join is 0 */
    cpu_push16(c, VG2_FRAME(-0x0E));
    cpu_push16(c, VG2_FRAME(-8));
    cpu_push16(c, VG2_FRAME(-0x0A));
    cpu_push16(c, VG2_FRAME(-4));
    c->icount += n + 4;
    VG2_NEAR(0x87C1, 0x87BD, 1 + 15);                             /* the line to the previous point */
    n = 1;                                                        /* jmp 08746 */
    goto tail;
}

/* VGAME 0x0971A, panel_ils: the instrument landing needles, drawn while the
 * cockpit is shown ([368C]), in the large layout ([294B] clear) and the
 * target ([E00C], 16-byte records at B2D0) has bit 3 of its flags byte
 * clear. With dx, dy the target's offset from the aircraft ([C0D0],
 * [C0DE]):
 *   range  = |dy|, which must be 40h to A00h or nothing is drawn;
 *   course = bearing(dx, sign(dy) * |dx| - dy)  (0x0C702, 0x0C863, 0x0EE0C);
 *   the localizer: a vertical line at x = clamp3((course - [2DEE]) >> 8
 *   as a signed byte + 9F, 8Bh, B5h) from y 27h to 49h, with the label at
 *   421C six pixels left of it, the pen being the colour [2CA0] (0x0886A);
 *   the glide slope: a horizontal line from x 8Ch to B4h at
 *   y = clamp3([2DF4] / 128 - drop, -16, 16) + 38h, where for a target
 *   with bit 1 of its flags the height is [2DF4] - 80h and drop =
 *   (range - 18h) >> 5, otherwise the height is [2DF4] and drop =
 *   (range - 38h) >> 6.
 * [4A10] and [4A18] hold the needle's x and y (and y before its clamp).
 * Lines are drawn by 0x087FD, the label by 0x0898F. SI and DI restored. */
static int vgame_panel_ils(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 22)) return 0;
    x86_enter(c, 8, 0);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    unsigned n = 5;                                               /* enter, push, push, cmp, je */
    alu_sub(c, ds_get(c, 0x368C), 0, 1, 0);
    if (c->flags & F_ZF) goto skip;                               /* the cockpit is hidden */
    alu_sub(c, VG2_DS8(0x294B), 0, 0, 0);
    n += 2;
    if (!(c->flags & F_ZF)) goto skip;                            /* the small layout */
    c->r[R_BX] = x86_shift(c, 4, ds_get(c, 0xE00C), 4, 1);
    alu_logic(c, VG2_DS8(c->r[R_BX] - 0x4D29) & 8, 0);            /* test byte [bx-4D29], 8 */
    n += 4;
    if (!(c->flags & F_ZF)) goto skip;                            /* the target takes no ILS */
    c->r[R_BX] = x86_shift(c, 4, ds_get(c, 0xE00C), 4, 1);
    c->r[R_AX] = (uint16_t)alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] - 0x4D30)), ds_get(c, 0xC0D0), 1, 0);   /* dx */
    c->r[R_CX] = (uint16_t)alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] - 0x4D2E)), ds_get(c, 0xC0DE), 1, 0);   /* dy */
    VG2_SETFRAME(-6, c->r[R_CX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_SI] = c->r[R_AX];
    c->r[R_DI] = c->r[R_CX];
    c->icount += n + 10;
    VG2_NEAR(0xEE0C, 0x9761, 4);                                  /* |dx| */
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, c->r[R_DI]);
    VG2_SETFRAME(-8, c->r[R_AX]);
    c->icount += 3;
    VG2_NEAR(0xC863, 0x9769, 6);                                  /* sign of dy */
    c->r[R_BX] = cpu_pop16(c);
    x86_imul16(c, VG2_FRAME(-8));                                 /* sign * |dx| */
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_DI], 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_SI]);
    c->icount += 5;
    VG2_NEAR(0xC702, 0x9774, 15);                                 /* the course */
    VG2_POP2();
    VG2_SETFRAME(-2, c->r[R_AX]);
    alu_sub(c, VG2_FRAME(-6), 0, 1, 0);
    n = 5;                                                        /* pop, pop, mov, cmp, jge */
    if (!x86_cond(c, 0xD)) {                                      /* neg [bp-6]: |dy| */
        VG2_SETFRAME(-6, (uint16_t)alu_sub(c, 0, VG2_FRAME(-6), 1, 0));
        n += 1;
    }
    alu_sub(c, VG2_FRAME(-6), 0x40, 1, 0);
    n += 2;
    if (x86_cond(c, 0xC)) goto skip;                              /* range below 40h */
    alu_sub(c, VG2_FRAME(-6), 0x0A00, 1, 0);
    n += 2;
    if (!x86_cond(c, 0xE)) goto skip;                             /* above A00h */
    VG2_SET_LOW(R_AX, VG2_DS8(0x2CA0));
    c->r[R_AX] = (uint16_t)(int16_t)(int8_t)c->r[R_AX];           /* cwde */
    cpu_push16(c, c->r[R_AX]);
    c->icount += n + 3;
    VG2_NEAR(0x886A, 0x979A, 10);                                 /* the pen */
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, 0xB5);
    cpu_push16(c, 0x8B);
    c->r[R_AX] = (uint16_t)alu_sub(c, VG2_FRAME(-2), ds_get(c, 0x2DEE), 1, 0);
    VG2_SET_LOW(R_AX, c->r[R_AX] >> 8);                           /* mov al, ah */
    c->r[R_AX] = (uint16_t)(int16_t)(int8_t)c->r[R_AX];           /* cwde */
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x9F, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 9;
    VG2_NEAR(0xC67A, 0x97B2, 7);                                  /* the needle's x */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    ds_put(c, 0x4A10, c->r[R_AX]);
    cpu_push16(c, 0x49);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, 0x27);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 6;
    VG2_NEAR(0x87FD, 0x97C1, 10);                                 /* the localizer */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    VG2_SET_LOW(R_AX, VG2_DS8(0x2CA0));
    c->r[R_AX] = (uint16_t)(int16_t)(int8_t)c->r[R_AX];
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, 0x21);
    c->r[R_AX] = (uint16_t)alu_sub(c, ds_get(c, 0x4A10), 6, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, 0x421C);
    c->icount += 9;
    VG2_NEAR(0x898F, 0x97D8, 7);                                  /* the label */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    c->r[R_BX] = x86_shift(c, 4, ds_get(c, 0xE00C), 4, 1);
    alu_logic(c, VG2_DS8(c->r[R_BX] - 0x4D29) & 2, 0);            /* test byte [bx-4D29], 2 */
    n = 5;                                                        /* add, mov, shl, test, je */
    uint16_t ret_ip;
    cpu_push16(c, VG2_FRAME(-6));
    c->icount += n + 1;
    if (!(c->flags & F_ZF)) ret_ip = 0x97EF; else ret_ip = 0x9806;
    VG2_NEAR(0xEE0C, ret_ip, 13);                                 /* |dy| again */
    c->r[R_BX] = cpu_pop16(c);
    if (ret_ip == 0x97EF) {
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], 0x18, 1, 0);
        c->r[R_AX] = x86_shift(c, 7, c->r[R_AX], 5, 1);
        c->r[R_CX] = (uint16_t)alu_sub(c, ds_get(c, 0x2DF4), 0x80, 1, 0);
        n = 6;                                                    /* pop .. jmp */
    } else {
        c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], 0x38, 1, 0);
        c->r[R_AX] = x86_shift(c, 7, c->r[R_AX], 6, 1);
        c->r[R_CX] = ds_get(c, 0x2DF4);
        n = 4;
    }
    c->r[R_CX] = x86_shift(c, 5, c->r[R_CX], 7, 1);               /* shr cx, 7 */
    c->r[R_CX] = (uint16_t)alu_sub(c, c->r[R_CX], c->r[R_AX], 1, 0);
    ds_put(c, 0x4A18, c->r[R_CX]);
    cpu_push16(c, 0x10);
    cpu_push16(c, 0xFFF0);
    cpu_push16(c, c->r[R_CX]);
    c->icount += n + 6;
    VG2_NEAR(0xC67A, 0x9822, 8);                                  /* clamp to +-16 */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x38, 1, 0);
    ds_put(c, 0x4A18, c->r[R_AX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, 0xB4);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, 0x8C);
    c->icount += 7;
    VG2_NEAR(0x87FD, 0x9836, 5);                                  /* the glide slope */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    n = 1;
    goto quit;
skip:                                                             /* 0973C: jmp 09839 */
    n += 1;
quit:                                                             /* 09839 */
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += n + 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x0B4F5, panel_marker_label(text, pen, margin): the label of the
 * marker at the projected point ([4A10], [4A18]; [4A10] = -1 is none).
 * When the point is further than `margin` from the edges of the 320 x 92
 * window, the weapon-lock marker (0x0B171, locked, size `margin`) is drawn
 * in `pen`. Inside x 15h..117h and y 1..4Bh the text (0x0898F, in the
 * colour [98A2]) is also written, centred under the point: its x is
 * [4A10] minus twice the text's length (0x0EB82), its y [4A18] + 5. */
static int vgame_panel_marker_label(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 28)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    alu_sub(c, ds_get(c, 0x4A10), 0xFFFF, 1, 0);
    unsigned n = 4;                                               /* push bp, mov, cmp, je */
    if (c->flags & F_ZF) goto quit;                               /* no marker */
    c->r[R_AX] = VG2_FRAME(8);
    alu_sub(c, ds_get(c, 0x4A10), c->r[R_AX], 1, 0);
    n += 3;
    if (x86_cond(c, 0xE)) goto no_box;                            /* x <= margin */
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], 0x013F, 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);       /* neg ax: 13Fh - margin */
    alu_sub(c, c->r[R_AX], ds_get(c, 0x4A10), 1, 0);
    n += 4;
    if (x86_cond(c, 0xE)) goto no_box;
    c->r[R_AX] = ds_get(c, 0x4A18);
    alu_sub(c, VG2_FRAME(8), c->r[R_AX], 1, 0);
    n += 3;
    if (x86_cond(c, 0xD)) goto no_box;                            /* margin >= y */
    c->r[R_CX] = (uint16_t)alu_sub(c, 0x5C, VG2_FRAME(8), 1, 0);
    alu_sub(c, c->r[R_CX], c->r[R_AX], 1, 0);
    n += 4;
    if (x86_cond(c, 0xE)) goto no_box;
    cpu_push16(c, VG2_FRAME(6));                                  /* pen */
    cpu_push16(c, 1);                                             /* locked */
    cpu_push16(c, VG2_FRAME(8));                                  /* size */
    cpu_push16(c, c->r[R_AX]);                                    /* y */
    cpu_push16(c, ds_get(c, 0x4A10));                             /* x */
    c->icount += n + 5;
    VG2_NEAR(0xB171, 0xB535, 1 + 14);
    c->r[R_SP] = c->r[R_BP];                                      /* mov sp, bp */
    n = 1;
no_box:                                                           /* 0B537 */
    alu_sub(c, ds_get(c, 0x4A10), 0x14, 1, 0);
    n += 2;
    if (x86_cond(c, 0xE)) goto quit;
    alu_sub(c, ds_get(c, 0x4A10), 0x118, 1, 0);
    n += 2;
    if (x86_cond(c, 0xD)) goto quit;
    alu_sub(c, ds_get(c, 0x4A18), 0, 1, 0);
    n += 2;
    if (x86_cond(c, 0xE)) goto quit;
    alu_sub(c, ds_get(c, 0x4A18), 0x4C, 1, 0);
    n += 2;
    if (x86_cond(c, 0xD)) goto quit;
    cpu_push16(c, ds_get(c, 0x98A2));
    c->r[R_AX] = (uint16_t)alu_add(c, ds_get(c, 0x4A18), 5, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, VG2_FRAME(4));
    c->icount += n + 5;
    VG2_NEAR(0xEB82, 0xB565, 7);                                  /* the text's length */
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_AX] = x86_shift(c, 4, c->r[R_AX], 1, 1);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], ds_get(c, 0x4A10), 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, c->r[R_AX], 1, 0);       /* neg ax */
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, VG2_FRAME(4));
    c->icount += 6;
    VG2_NEAR(0x898F, 0xB575, 2);                                  /* the text */
    n = 0;
quit:
    x86_leave(c);
    c->icount += n + 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x0D9A2, compose_camera(a1, a2, a3, scale): the matrices a frame is
 * drawn with. The view matrix at 49BE is built from the negated angles
 * (-a1, -a2, -a3) by 0x0DFA9; a second one at 49D0 from (-a1, -a2, a3) by
 * the far routine 1452:03AB; and 120A:0008 scales that one by `scale`
 * (the model renderer's matrix set-up). SI and DI are restored. */
static int vgame_compose_camera(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 17)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, VG2_FRAME(8), 1, 0);     /* neg ax */
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, VG2_FRAME(6), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_CX] = (uint16_t)alu_sub(c, 0, VG2_FRAME(4), 1, 0);
    cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, 0x49BE);
    c->r[R_SI] = c->r[R_AX];
    c->r[R_DI] = c->r[R_CX];
    c->icount += 16;
    VG2_NEAR(0xDFA9, 0xD9C3, 6);                                  /* the view matrix */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    cpu_push16(c, 0x49D0);
    cpu_push16(c, VG2_FRAME(8));
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    c->icount += 5;
    VG2_FAR(0xD9CE, 4);                                           /* the second matrix */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    cpu_push16(c, VG2_FRAME(0x0A));
    cpu_push16(c, 0x49D0);
    c->icount += 3;
    VG2_FAR(0xD9DC, 6);                                           /* scaled, and its reciprocal */
    VG2_POP2();
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 6;
    near_ret(c);
    return 1;
}

/* VGAME 0x0889B, cockpit_lamp(lamp, colour): lamp number `lamp` (6-word
 * entries at 3F30: its element at +0, the colour it shows at +2, and the
 * state it last drew at +4) is set to `colour`. Nothing while the cockpit
 * is hidden ([368C] clear) or when it already shows that colour. Otherwise
 * the element is drawn by 0x0D578 into the page [4010] - lit or off by
 * whether the lamp's colour word at +2 equals the new colour - and, for
 * lamp 2, also into the page [4040] by whether that word (read again)
 * equals it. The new colour is then stored as the state. */
static int vgame_cockpit_lamp(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 20)) return 0;
    x86_enter(c, 4, 0);
    alu_sub(c, ds_get(c, 0x368C), 0, 1, 0);
    unsigned n = 3;                                               /* enter, cmp, je */
    if (c->flags & F_ZF) goto quit;
    c->r[R_AX] = VG2_FRAME(6);
    c->r[R_BX] = x86_imul3(c, VG2_FRAME(4), 6);
    alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x3F34)), c->r[R_AX], 1, 0);
    n += 4;
    if (c->flags & F_ZF) goto quit;                               /* already that colour */
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)(c->r[R_BX] + 0x3F32);                 /* lea ax, [bx+3F32] */
    VG2_SETFRAME(-2, c->r[R_AX]);
    c->r[R_AX] = cpu_pop16(c);
    VG2_SETFRAME(-4, c->r[R_BX]);
    alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x3F32)), c->r[R_AX], 1, 0);
    n += 7;
    if (c->flags & F_ZF) { c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0); n += 2; }
    else { c->r[R_AX] = 1; n += 1; }
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x3F30)));
    cpu_push16(c, ds_get(c, 0x4010));
    c->icount += n + 3;
    VG2_NEAR(0xD578, 0x88D8, 14);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    alu_sub(c, VG2_FRAME(4), 2, 1, 0);
    n = 3;                                                        /* add, cmp, jne */
    if (c->flags & F_ZF) {                                        /* lamp 2: the second page too */
        c->r[R_AX] = VG2_FRAME(6);
        c->r[R_BX] = VG2_FRAME(-2);
        alu_sub(c, ds_get(c, c->r[R_BX]), c->r[R_AX], 1, 0);
        n += 4;
        if (c->flags & F_ZF) { c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0); n += 2; }
        else { c->r[R_AX] = 1; n += 1; }
        cpu_push16(c, c->r[R_AX]);
        c->r[R_BX] = VG2_FRAME(-4);
        cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x3F30)));
        cpu_push16(c, ds_get(c, 0x4040));
        c->icount += n + 4;
        VG2_NEAR(0xD578, 0x8901, 5);
        n = 0;
    }
    c->r[R_AX] = VG2_FRAME(6);
    c->r[R_BX] = x86_imul3(c, VG2_FRAME(4), 6);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x3F34), c->r[R_AX]);
    n += 3;
quit:
    x86_leave(c);
    c->icount += n + 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x0BC5F, camimage_place_near_effect(model, x, y, z, a1, a2, a3): a
 * model placed relative to the eye for the camera view. x and y are 32-bit
 * (the words at +6/+8 and +A/+C); the eye is at [B792] (x), [B796] (y) and
 * [B834] (z). rel_x = x - eye x, rel_y = y + eye y - 100h:0000h (low words
 * kept), rel_z = z - eye z. A relative position whose low word has |v| >=
 * 7FFFh is out of range and nothing is drawn; otherwise the camera origin
 * is set to (0, 0, -rel_z) (0x0D9E7), [E328] to 1, and the model is placed
 * by 0x0E2B6 as (model, -a1, a2, a3, rel_x, -rel_y, z != 0). */
static int vgame_camimage_place_near_effect(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 20)) return 0;
    x86_enter(c, 0x0A, 0);
    c->r[R_AX] = ds_get(c, 0xB796);
    c->r[R_DX] = ds_get(c, 0xB798);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], VG2_FRAME(0x0A), 1, 0);
    c->r[R_DX] = (uint16_t)alu_add(c, c->r[R_DX], VG2_FRAME(0x0C), 1, (c->flags & F_CF) ? 1u : 0u);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], 0, 1, 0);
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], 0x0100, 1, (c->flags & F_CF) ? 1u : 0u);
    VG2_SETFRAME(-8, c->r[R_AX]);
    VG2_SETFRAME(-6, c->r[R_DX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, VG2_FRAME(0x0E), ds_get(c, 0xB834), 1, 0);
    VG2_SETFRAME(-0x0A, c->r[R_AX]);
    c->r[R_AX] = VG2_FRAME(6);
    c->r[R_DX] = VG2_FRAME(8);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], ds_get(c, 0xB792), 1, 0);
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], ds_get(c, 0xB794), 1, (c->flags & F_CF) ? 1u : 0u);
    VG2_SETFRAME(-4, c->r[R_AX]);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 19;
    VG2_NEAR(0xEE0C, 0xBC9D, 7);                                  /* |rel_x| */
    VG2_POP2();
    alu_sub(c, c->r[R_AX], 0x7FFF, 1, 0);
    unsigned n = 4;                                               /* pop, pop, cmp, jge */
    if (x86_cond(c, 0xD)) goto quit;
    cpu_push16(c, VG2_FRAME(-6));
    cpu_push16(c, VG2_FRAME(-8));
    c->icount += n + 2;
    VG2_NEAR(0xEE0C, 0xBCAD, 10);                                 /* |rel_y| */
    VG2_POP2();
    alu_sub(c, c->r[R_AX], 0x7FFF, 1, 0);
    n = 4;
    if (x86_cond(c, 0xD)) goto quit;
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, VG2_FRAME(-0x0A), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, 0);
    cpu_push16(c, 0);
    c->icount += n + 5;
    VG2_NEAR(0xD9E7, 0xBCC1, 17);                                 /* the camera origin */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    ds_put(c, 0xE328, 1);
    alu_sub(c, VG2_FRAME(0x0E), 1, 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, (c->flags & F_CF) ? 1u : 0u);   /* sbb ax, ax */
    c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
    cpu_push16(c, c->r[R_AX]);                                    /* z != 0 */
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, VG2_FRAME(-8), 1, 0);
    cpu_push16(c, c->r[R_AX]);                                    /* -rel_y */
    cpu_push16(c, VG2_FRAME(-4));                                 /* rel_x */
    cpu_push16(c, VG2_FRAME(0x14));
    cpu_push16(c, VG2_FRAME(0x12));
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, VG2_FRAME(0x10), 1, 0);
    cpu_push16(c, c->r[R_AX]);                                    /* -a1 */
    cpu_push16(c, VG2_FRAME(4));                                  /* model */
    c->icount += 16;
    VG2_NEAR(0xE2B6, 0xBCED, 2);
    n = 0;
quit:
    x86_leave(c);
    c->icount += n + 2;
    near_ret(c);
    return 1;
}

/* The five wrappers of the palette set-up in the 1039 segment: ES = DS,
 * BX, CX and DX loaded with a first colour, a count and a table, and the
 * driver's far entry 14C2:0000 (or 14C2:00B7 for the two-argument form)
 * called with the table's far pointer (ES:DX) and, for the four-argument
 * form, the count and first colour. All return far, the pushed arguments
 * removed with ADD SP. */
typedef struct { uint16_t bx, cx, dx; int args; uint16_t lcall_ip; int zero_bx; } vg2_palette_call;
static int vg2_palette_set(machine_t *m, const vg2_palette_call *p)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 10)) return 0;
    c->r[R_AX] = c->seg[S_DS];
    c->seg[S_ES] = c->r[R_AX];
    if (p->zero_bx) c->r[R_BX] = (uint16_t)alu_sub(c, c->r[R_BX], c->r[R_BX], 1, 0);   /* sub bx, bx */
    else c->r[R_BX] = p->bx;
    c->r[R_CX] = p->cx;
    c->r[R_DX] = p->dx;
    cpu_push16(c, c->seg[S_ES]);
    cpu_push16(c, c->r[R_DX]);
    if (p->args == 4) { cpu_push16(c, c->r[R_CX]); cpu_push16(c, c->r[R_BX]); }
    c->icount += p->args == 4 ? 9 : 7;
    if (!guest_call_far(m, p->lcall_ip, (uint16_t)(p->lcall_ip + 5))) return 1;
    if (!room(c, 2)) { c->ip = (uint16_t)(p->lcall_ip + 5); return 1; }
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], (uint16_t)(p->args * 2), 1, 0);
    c->icount += 2;
    far_ret(c);
    return 1;
}
/* 1039:009D and 1039:00B7, eight colours from index 98h: the tables at 206A and 1212. */
static const vg2_palette_call VG2_PAL_206A = { 0x98, 8, 0x206A, 4, 0x00AE, 0 };
static const vg2_palette_call VG2_PAL_1212 = { 0x98, 8, 0x1212, 4, 0x00C8, 0 };
/* 1039:00D1 (the 14C2:00B7 form), 1039:00E8 and 1039:0101: all 256 colours from index 0: the table at D4A, D4A and 134A. */
static const vg2_palette_call VG2_PAL_D4A_READ = { 0, 0x100, 0x0D4A, 2, 0x00DF, 1 };
static const vg2_palette_call VG2_PAL_D4A = { 0, 0x100, 0x0D4A, 4, 0x00F8, 1 };
static const vg2_palette_call VG2_PAL_134A = { 0, 0x100, 0x134A, 4, 0x0111, 1 };
static int vgame_palette_206a(machine_t *m) { return vg2_palette_set(m, &VG2_PAL_206A); }
static int vgame_palette_1212(machine_t *m) { return vg2_palette_set(m, &VG2_PAL_1212); }
static int vgame_palette_d4a_read(machine_t *m) { return vg2_palette_set(m, &VG2_PAL_D4A_READ); }
static int vgame_palette_d4a(machine_t *m) { return vg2_palette_set(m, &VG2_PAL_D4A); }
static int vgame_palette_134a(machine_t *m) { return vg2_palette_set(m, &VG2_PAL_134A); }

/* VGAME 1452:0288, three rows through 1452:02AC: the routine takes a
 * pointer to three 6-byte rows (SI, from the first argument) and two more
 * words (BX and DI) and calls its row routine on each, SI advancing by 6
 * between; SI and DI are restored. */
static int vgame_matrix_rows3(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    c->r[R_SI] = VG2_FRAME(6);
    c->r[R_BX] = VG2_FRAME(8);
    c->r[R_DI] = VG2_FRAME(0x0A);
    c->icount += 7;
    VG2_NEAR(0x02AC, 0x0299, 2);
    c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], 6, 1, 0);
    c->icount += 1;
    VG2_NEAR(0x02AC, 0x029F, 2);
    c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], 6, 1, 0);
    c->icount += 1;
    VG2_NEAR(0x02AC, 0x02A5, 5);
    c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], 6, 1, 0);
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 5;
    far_ret(c);
    return 1;
}

/* VGAME 1058:0D8C, copy_settings(p): the 20 words at the far pointer p
 * (argument, offset then segment) are copied to DS:2CA2. DS and ES are
 * restored. */
static int vgame_copy_settings(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 13 + 20)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->seg[S_ES]);
    cpu_push16(c, c->seg[S_DS]);
    cpu_push16(c, c->seg[S_DS]);
    c->seg[S_ES] = cpu_pop16(c);
    c->r[R_SI] = VG2_FRAME(6);
    c->seg[S_DS] = VG2_FRAME(8);
    c->r[R_CX] = 0x14;
    c->r[R_DI] = 0x2CA2;
    const unsigned copied = rep_string(c, STR_MOVS, 1, c->seg[S_DS], 0);
    c->seg[S_DS] = cpu_pop16(c);
    c->seg[S_ES] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += 13 + copied;
    far_ret(c);
    return 1;
}

/* VGAME 11ED:0078, lzw_reset: the picture decoder's string table starts
 * again - the code length 9 bits, the mask 1FFh, the next code 100h; the
 * 800h three-byte entries at C6B4 get the prefix FFFFh and the first 100h
 * of them the suffix byte equal to their index. */
static int vgame_lzw_table_reset(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 7 + 3 * 0x800 + 3 + 4 * 0x100 + 1)) return 0;
    VG2_SETDS8(0x968E, 9);
    ds_put(c, 0x9690, 0x01FF);
    c->r[R_DX] = 0x0100;
    ds_put(c, 0x9692, c->r[R_DX]);
    c->r[R_AX] = 0xFFFF;
    c->r[R_BX] = (uint16_t)alu_logic(c, 0, 1);                    /* xor bx, bx */
    c->r[R_CX] = 0x0800;
    do {
        ds_put(c, (uint16_t)(c->r[R_BX] - 0x394C), c->r[R_AX]);
        c->r[R_BX] = (uint16_t)alu_add(c, c->r[R_BX], 3, 1, 0);
        c->r[R_CX] = (uint16_t)(c->r[R_CX] - 1);
    } while (c->r[R_CX] != 0);
    VG2_SET_LOW(R_AX, 0);
    c->r[R_BX] = (uint16_t)alu_logic(c, 0, 1);
    c->r[R_CX] = 0x0100;
    do {
        VG2_SETDS8(c->r[R_BX] - 0x394A, c->r[R_AX]);
        VG2_SET_LOW(R_AX, alu_inc(c, c->r[R_AX] & 0xFF, 0));
        c->r[R_BX] = (uint16_t)alu_add(c, c->r[R_BX], 3, 1, 0);
        c->r[R_CX] = (uint16_t)(c->r[R_CX] - 1);
    } while (c->r[R_CX] != 0);
    c->icount += 7 + 3 * 0x800 + 3 + 4 * 0x100 + 1;
    near_ret(c);
    return 1;
}

/* VGAME 11ED:00AE and END 0x48AA, the picture decoder's RLE and pixel loop
 * (the Reimp's rle_byte fused with pic_decode_pixels' count loop,
 * src/core/pic.c): in nibble mode the count is halved first, since each
 * turn stores two pixels. The private stack at [spsave] is swapped in and
 * DX (the next table code) loaded from [nextsave]; each turn takes one RLE
 * byte through the LZW step below - a run left in [run] replays [last],
 * else a fresh byte with the 90h escape (90h 00 reads as a plain 90h, any
 * other second byte sets the run) - then stores it to ES:DI, a byte or low
 * nibble first with the high nibble in AH, until the count runs out. DX is
 * written back and the caller's stack restored. The step runs as a C call
 * with its return pushed: it pops that return and comes back with the
 * stack dirty, so a machine-run call would never reach its stop trap. The
 * stores are written out by hand: MSVC at /O2 never finishes a loop using
 * x86_stos. */
typedef struct pic_lzw pic_lzw;  /* below: the loop calls the step as C, not through the machine */
typedef struct {
    uint16_t nibble, count, spsave, nextsave, run, last;
    uint16_t entry, loop, call1, ret1, store, call2, ret2;
} pic_rle;

static int pic_lzw_step(machine_t *m, const pic_lzw *s);
static int pic_rle_row(machine_t *m, const pic_rle *s, const pic_lzw *lzw)
{
    cpu_t *c = &m->cpu;
    uint16_t ds = c->seg[S_DS];
    int word = 0;                                                 /* the nibble path jumps to the exit */
    if (!room(c, 7)) return 0;
    alu_sub(c, mem_read8(c, phys(ds, s->nibble)), 0, 0, 0);       /* cmp [nibble], 0 */
    if (!(c->flags & F_ZF)) {                                     /* nibble mode: halve the count */
        const uint16_t count = s->count;
        seg_write16(c, ds, count, x86_shift(c, 5, seg_read16(c, ds, count), 1, 1));
        c->icount += 3;                                           /* cmp, je, shr */
    } else {
        c->icount += 2;                                           /* cmp, je */
    }
    c->r[R_AX] = seg_read16(c, ds, s->spsave);                    /* swap in the private stack */
    seg_write16(c, ds, s->spsave, c->r[R_SP]);
    c->r[R_SP] = c->r[R_AX];
    c->r[R_DX] = seg_read16(c, ds, s->nextsave);
    c->icount += 4;
    for (;;) {
        uint16_t es;
        if (!room(c, 4)) { c->ip = s->loop; return 1; }           /* cmp, jne, mov/dec/call */
        alu_sub(c, mem_read8(c, phys(ds, s->run)), 0, 0, 0);      /* cmp [run], 0 */
        if (!(c->flags & F_ZF)) {                                 /* jne taken: replay [last] */
            c->icount += 2;                                       /* cmp, jne */
            goto emit;
        }
        c->icount += 2;                                           /* cmp, jne */
        /* The step pops its return address and returns with the stack dirty,
         * so a machine-run call would never reach its stop trap; it runs
         * here as C, with its return pushed as a call would push it. By the
         * room check the step cannot decline; a stop inside it propagates. */
        if (!room(c, 6)) { c->ip = s->call1; return 1; }          /* the call, and the step's entry */
        cpu_push16(c, s->ret1);
        c->icount += 1;
        if (!pic_lzw_step(m, lzw)) {                              /* declined: nothing ran */
            c->r[R_SP] = (uint16_t)(c->r[R_SP] + 2);
            c->icount -= 1;
            c->ip = s->call1;
            return 1;
        }
        ds = c->seg[S_DS];
        if (c->ip != s->ret1) return 1;                           /* stopped inside the step */
        if (!room(c, 4)) { c->ip = s->ret1; return 1; }           /* cmp, je, mov/call */
        alu_sub(c, get_r8(c, R_AL), 0x90, 0, 0);                  /* cmp al, 90h */
        if (!(c->flags & F_ZF)) {                                 /* je not taken: plain byte */
            mem_write8(c, phys(ds, s->last), get_r8(c, R_AL));    /* [last] = al */
            c->icount += 2 + 2;                                   /* cmp, je, mov, jmp */
            goto store;
        }
        c->icount += 2;                                           /* cmp, je */
        if (!room(c, 6)) { c->ip = s->call2; return 1; }          /* the call, and the step's entry */
        cpu_push16(c, s->ret2);
        c->icount += 1;
        if (!pic_lzw_step(m, lzw)) {                              /* declined: nothing ran */
            c->r[R_SP] = (uint16_t)(c->r[R_SP] + 2);
            c->icount -= 1;
            c->ip = s->call2;
            return 1;
        }
        ds = c->seg[S_DS];
        if (c->ip != s->ret2) return 1;                           /* stopped inside the step */
        if (!room(c, 6)) { c->ip = s->ret2; return 1; }           /* or, jne, then movs to emit/store */
        alu_op(c, 1, get_r8(c, R_AL), get_r8(c, R_AL), 0);        /* or al, al */
        if (!(c->flags & F_ZF)) {                                 /* jne taken: a run count */
            uint8_t al = get_r8(c, R_AL);
            set_r8(c, R_AL, (uint8_t)alu_dec(c, al, 0));          /* dec al */
            mem_write8(c, phys(ds, s->run), get_r8(c, R_AL));     /* [run] = al - 1 */
            c->icount += 2 + 2;                                   /* or, jne, dec, mov */
        } else {
            set_r8(c, R_AL, 0x90);                                /* mov al, 90h */
            mem_write8(c, phys(ds, s->last), get_r8(c, R_AL));
            c->icount += 2 + 3;                                   /* or, jne, mov, mov, jmp */
            goto store;
        }
emit:
        set_r8(c, R_AL, mem_read8(c, phys(ds, s->last)));         /* mov al, [last] */
        { const uint16_t run = s->run; const uint16_t v = mem_read8(c, phys(ds, run));
          mem_write8(c, phys(ds, run), (uint8_t)alu_dec(c, v, 0)); }   /* dec [run] */
        c->icount += 2;                                           /* mov, dec */
store:
        if (!room(c, 17)) { c->ip = s->store; return 1; }
        alu_sub(c, mem_read8(c, phys(ds, s->nibble)), 0, 0, 0);   /* cmp [nibble], 0 */
        if (!(c->flags & F_ZF)) {                                 /* je not taken: two nibbles */
            set_r8(c, R_AH, get_r8(c, R_AL));                     /* mov ah, al */
            set_r8(c, R_AL, (uint8_t)alu_op(c, 4, get_r8(c, R_AL), 0x0F, 0));
            set_r8(c, R_AH, (uint8_t)x86_shift(c, 5, get_r8(c, R_AH), 1, 0));
            set_r8(c, R_AH, (uint8_t)x86_shift(c, 5, get_r8(c, R_AH), 1, 0));
            set_r8(c, R_AH, (uint8_t)x86_shift(c, 5, get_r8(c, R_AH), 1, 0));
            set_r8(c, R_AH, (uint8_t)x86_shift(c, 5, get_r8(c, R_AH), 1, 0));
            es = c->seg[S_ES];                                    /* stosw, by hand */
            seg_write16(c, es, c->r[R_DI], c->r[R_AX]);
            c->r[R_DI] = (uint16_t)(c->r[R_DI] + ((c->flags & F_DF) ? -2 : 2));
            c->icount += 2 + 6 + 1;                               /* cmp, je, mov, and, 4 shr, stos */
            word = 1;
        } else {
            es = c->seg[S_ES];                                    /* stosb, by hand */
            mem_write8(c, phys(es, c->r[R_DI]), get_r8(c, R_AL));
            c->r[R_DI] = (uint16_t)(c->r[R_DI] + ((c->flags & F_DF) ? -1 : 1));
            c->icount += 2 + 1;                                   /* cmp, je, stos */
            word = 0;
        }
        { const uint16_t count = s->count;
          const uint16_t v = seg_read16(c, ds, count);            /* dec [count] */
          seg_write16(c, ds, count, (uint16_t)alu_dec(c, v, 1));
          c->icount += 2;                                         /* dec, jne */
          if (c->flags & F_ZF) break; }                           /* jne not taken: out of bytes */
    }
    ds_put(c, s->nextsave, c->r[R_DX]);                           /* save the next table code */
    c->r[R_AX] = seg_read16(c, ds, s->spsave);                    /* the caller's stack back */
    seg_write16(c, ds, s->spsave, c->r[R_SP]);
    c->r[R_SP] = c->r[R_AX];
    c->icount += (unsigned)word + 5;                              /* the jmp on the nibble path, then mov, mov, mov, mov, ret */
    near_ret(c);
    return 1;
}

/* VGAME 11ED:0127 and END 0x4923, the picture decoder's code and table step
 * (the Reimp's next_code fused with lzw_byte, src/core/pic.c): when the
 * private stack is not at its top a stacked string byte is popped and
 * returned (the string stack lives on the private stack itself, pushed
 * words below the caller's return address). Otherwise width bits are
 * assembled from [buf] past [remaining], refilling whole words through the
 * file reader - a far call in VGAME's overlay, the [reader] vector in END -
 * and masked to the next code. A code already in the table walks its
 * prefix chain pushing suffixes; a new one (the KwKwK case) is the previous
 * string plus its first byte. The table at [table] gains the entry (prefix
 * word, suffix byte at +2), the width and mask grow past the mask, and the
 * table resets through [reset] past the maximum width; [prev] takes the
 * code. Returns by popping the pushed root byte and jumping to the caller.
 * The lodsw is written out with the stores, as above. */
struct pic_lzw {
    uint16_t len, width, maxw, mask, buf, rem, prev, first;
    uint16_t table;              /* prefix words; suffix bytes at +2 */
    uint16_t bufaddr;            /* the refill buffer (mov si, ...) */
    uint16_t stacktop;           /* cmp sp, ...: the empty private stack */
    uint16_t reader_at;          /* END: [reader_at] holds the near reader */
    uint16_t reset, reset_ret;
    uint16_t entry, main, refill, reader_ret, walk;
    int far_reader;              /* VGAME: the reader is a far call */
};

static int pic_lzw_step(machine_t *m, const pic_lzw *s)
{
    cpu_t *c = &m->cpu;
    uint16_t ds = c->seg[S_DS];
    if (!room(c, 5)) return 0;
    c->r[R_BP] = cpu_pop16(c);                                    /* pop bp */
    alu_sub(c, c->r[R_SP], s->stacktop, 1, 0);                    /* cmp sp, top */
    if (!x86_cond(c, 4)) {                                        /* je not taken: stacked byte */
        c->r[R_AX] = cpu_pop16(c);                                /* pop ax */
        c->ip = c->r[R_BP];                                       /* jmp bp */
        c->icount += 5;                                           /* pop, cmp, je, pop, jmp */
        return 1;
    }
    c->icount += 3;                                               /* pop, cmp, je */
    if (!room(c, 6)) { c->ip = s->main; return 1; }
    c->r[R_BX] = seg_read16(c, ds, s->buf);                       /* mov bx, [buf] */
    set_r8(c, R_CL, 0x10);                                        /* mov cl, 10h */
    set_r8(c, R_CH, mem_read8(c, phys(ds, s->rem)));              /* mov ch, [remaining] */
    set_r8(c, R_CL, (uint8_t)alu_sub(c, get_r8(c, R_CL), get_r8(c, R_CH), 0, 0));
    c->r[R_BX] = x86_shift(c, 5, c->r[R_BX], get_r8(c, R_CL), 1); /* shr bx, cl */
    set_r8(c, R_CL, get_r8(c, R_CH));                             /* mov cl, ch */
    c->icount += 6;
refill:
    if (!room(c, 13)) { c->ip = s->refill; return 1; }
    alu_sub(c, get_r8(c, R_CL), mem_read8(c, phys(ds, s->width)), 0, 0);
    if (x86_cond(c, 13)) { c->icount += 2; goto buffered; }       /* jge: enough bits */
    alu_sub(c, c->r[R_SI], seg_read16(c, ds, s->len), 1, 0);      /* cmp si, [len] */
    if (!x86_cond(c, 2)) {                                        /* jb not taken: refill */
        cpu_push16(c, c->r[R_BX]);
        cpu_push16(c, c->r[R_CX]);
        cpu_push16(c, c->r[R_DX]);
        c->icount += 2 + 2 + 3;                                   /* cmp, jge, cmp, jb, pushes */
        int rrc;
        if (s->far_reader) {
            rrc = guest_call_far(m, (uint16_t)(s->entry + 0x29), s->reader_ret);
            if (!rrc) return 1;
        } else {
            rrc = guest_call(m, ds_get(c, s->reader_at), s->reader_ret);
            if (!rrc) return 1;
        }
        ds = c->seg[S_DS];
        if (!room(c, 10)) { c->ip = s->reader_ret; return 1; }
        c->r[R_DX] = cpu_pop16(c);
        c->r[R_CX] = cpu_pop16(c);
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_SI] = s->bufaddr;                                  /* mov si, buffer */
        c->icount += 4;
    } else {
        c->icount += 2 + 2;                                       /* cmp, jge, cmp, jb */
    }
    c->r[R_AX] = seg_read16(c, ds, c->r[R_SI]);                   /* lodsw, by hand */
    c->r[R_SI] = (uint16_t)(c->r[R_SI] + ((c->flags & F_DF) ? -2 : 2));
    seg_write16(c, ds, s->buf, c->r[R_AX]);                       /* mov [buf], ax */
    c->r[R_AX] = x86_shift(c, 4, c->r[R_AX], get_r8(c, R_CL), 1);/* shl ax, cl */
    c->r[R_BX] = (uint16_t)alu_op(c, 1, c->r[R_BX], c->r[R_AX], 1);   /* or bx, ax */
    set_r8(c, R_CL, (uint8_t)alu_add(c, get_r8(c, R_CL), 0x10, 0, 0)); /* add cl, 10h */
    c->icount += 6;                                               /* lods, mov, shl, or, add, jmp */
    goto refill;
buffered:
    set_r8(c, R_CL, (uint8_t)alu_sub(c, get_r8(c, R_CL), mem_read8(c, phys(ds, s->width)), 0, 0));
    mem_write8(c, phys(ds, s->rem), get_r8(c, R_CL));             /* [remaining] = cl - width */
    c->r[R_AX] = c->r[R_BX];                                      /* mov ax, bx */
    c->r[R_AX] = (uint16_t)alu_op(c, 4, c->r[R_AX], seg_read16(c, ds, s->mask), 1);
    c->r[R_CX] = c->r[R_AX];                                      /* mov cx, ax: the code */
    alu_sub(c, c->r[R_AX], c->r[R_DX], 1, 0);                     /* cmp ax, dx */
    c->icount += 6;                                               /* sub, mov, mov, and, mov, cmp */
    if (!x86_cond(c, 12)) {                                       /* jl not taken: KwKwK */
        c->r[R_CX] = c->r[R_DX];                                  /* mov cx, dx */
        c->r[R_AX] = seg_read16(c, ds, s->prev);                 /* mov ax, [prev] */
        set_r8(c, R_BL, mem_read8(c, phys(ds, s->first)));       /* mov bl, [first] */
        cpu_push16(c, c->r[R_BX]);                                /* push bx */
        c->icount += 1 + 4;                                       /* jl, mov, mov, mov, push */
    } else {
        c->icount += 1;                                           /* jl taken */
    }
walk:
    if (!room(c, 28)) { c->ip = s->walk; return 1; }
    for (;;) {
        c->r[R_BX] = c->r[R_AX];                                  /* mov bx, ax */
        c->r[R_BX] = (uint16_t)alu_add(c, c->r[R_BX], c->r[R_AX], 1, 0);
        c->r[R_BX] = (uint16_t)alu_add(c, c->r[R_BX], c->r[R_AX], 1, 0);
        c->r[R_AX] = seg_read16(c, ds, (uint16_t)(c->r[R_BX] + s->table));
        c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);         /* inc ax */
        c->icount += 6;                                           /* mov, add, add, mov, inc, je */
        if (x86_cond(c, 4)) break;                                /* je: the prefix is FFFFh */
        c->r[R_AX] = (uint16_t)alu_dec(c, c->r[R_AX], 1);         /* dec ax */
        set_r8(c, R_BL, mem_read8(c, phys(ds, (uint16_t)(c->r[R_BX] + s->table + 2))));
        cpu_push16(c, c->r[R_BX]);                                /* push bx */
        c->icount += 3 + 1;                                       /* dec, mov, push, jmp */
    }
    set_r8(c, R_AL, mem_read8(c, phys(ds, (uint16_t)(c->r[R_BX] + s->table + 2))));
    mem_write8(c, phys(ds, s->first), get_r8(c, R_AL));           /* [first] = the root */
    cpu_push16(c, c->r[R_AX]);                                    /* push ax */
    c->r[R_BX] = c->r[R_DX];                                      /* the new entry at [next] */
    c->r[R_BX] = (uint16_t)alu_add(c, c->r[R_BX], c->r[R_DX], 1, 0);
    c->r[R_BX] = (uint16_t)alu_add(c, c->r[R_BX], c->r[R_DX], 1, 0);
    mem_write8(c, phys(ds, (uint16_t)(c->r[R_BX] + s->table + 2)), get_r8(c, R_AL));
    c->r[R_AX] = seg_read16(c, ds, s->prev);
    seg_write16(c, ds, (uint16_t)(c->r[R_BX] + s->table), c->r[R_AX]);
    c->r[R_DX] = (uint16_t)alu_inc(c, c->r[R_DX], 1);             /* inc dx */
    alu_sub(c, c->r[R_DX], seg_read16(c, ds, s->mask), 1, 0);    /* cmp dx, [mask] */
    c->icount += 11;                                              /* mov, mov, push, mov, add, add, mov, mov, mov, inc, cmp */
    if (!x86_cond(c, 14)) {                                       /* jle not taken: widen */
        const uint16_t mask = s->mask;
        const uint16_t v = mem_read8(c, phys(ds, s->width));
        mem_write8(c, phys(ds, s->width), (uint8_t)alu_inc(c, v, 0));   /* inc [width] */
        set_flag(c, F_CF, 1);                                     /* stc */
        seg_write16(c, ds, mask, x86_shift(c, 2, seg_read16(c, ds, mask), 1, 1));
        c->icount += 1 + 3;                                       /* jle, inc, stc, rcl */
    } else {
        c->icount += 1;                                           /* jle taken */
    }
    set_r8(c, R_AL, mem_read8(c, phys(ds, s->width)));           /* mov al, [width] */
    alu_sub(c, get_r8(c, R_AL), mem_read8(c, phys(ds, s->maxw)), 0, 0);
    if (!x86_cond(c, 14)) {                                       /* jle not taken: reset */
        c->icount += 3;                                           /* mov, cmp, jle */
        if (!guest_call(m, s->reset, s->reset_ret)) return 1;
        ds = c->seg[S_DS];
        if (!room(c, 4)) { c->ip = s->reset_ret; return 1; }
    } else {
        c->icount += 3;                                           /* mov, cmp, jle */
    }
    seg_write16(c, ds, s->prev, c->r[R_CX]);                      /* mov [prev], cx */
    c->r[R_AX] = cpu_pop16(c);                                    /* pop ax: the root byte */
    c->ip = c->r[R_BP];                                           /* jmp bp */
    c->icount += 4;                                               /* mov, jmp, pop, jmp */
    return 1;
}

static const pic_lzw PIC_LZW_VGAME = { 0x9686, 0x968E, 0x968F, 0x9690, 0x9694, 0x9696,
    0x9698, 0x969A, 0xC6B4, 0x43F8, 0x989B, 0, 0x0078, 0x01D0,
    0x0127, 0x0131, 0x0141, 0x0155, 0x0186, 1 };
static const pic_lzw PIC_LZW_END = { 0x3F82, 0x3F8A, 0x3F8B, 0x3F8C, 0x3F90, 0x3F92,
    0x3F94, 0x3F96, 0x2636, 0x1C77, 0x4197, 0x3F80, 0x4874, 0x49CB,
    0x4923, 0x492D, 0x493D, 0x4950, 0x4981, 0 };

static const pic_rle PIC_RLE_VGAME = { 0x9697, 0x968A, 0x9688, 0x9692, 0x968C, 0x968D,
    0x00AE, 0x00C6, 0x00CD, 0x00D0, 0x00F5, 0x00DA, 0x00DD };
static const pic_rle PIC_RLE_END = { 0x3F93, 0x3F86, 0x3F84, 0x3F8E, 0x3F88, 0x3F89,
    0x48AA, 0x48C2, 0x48C9, 0x48CC, 0x48F1, 0x48D6, 0x48D9 };
static int vgame_pic_rle(machine_t *m) { return pic_rle_row(m, &PIC_RLE_VGAME, &PIC_LZW_VGAME); }
static int end_pic_rle(machine_t *m) { return pic_rle_row(m, &PIC_RLE_END, &PIC_LZW_END); }
static int vgame_pic_lzw(machine_t *m) { return pic_lzw_step(m, &PIC_LZW_VGAME); }
static int end_pic_lzw(machine_t *m) { return pic_lzw_step(m, &PIC_LZW_END); }

/* VGAME 0x078FD, scene_obstacle_probe(x, y, z): the world object at a map
 * position, by 0x01007 (which looks it up and replaces it), with the
 * coordinates widened to 32 bits and scaled by 32 through the runtime's
 * shift 0x0EF68: (x << 5, (8000h - y) << 5, z). */
static int vgame_scene_obstacle_probe(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 16)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = VG2_FRAME(8);
    c->r[R_DX] = (c->r[R_AX] & 0x8000) ? 0xFFFF : 0;              /* cwd */
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = VG2_FRAME(6);
    c->r[R_DX] = (c->r[R_AX] & 0x8000) ? 0xFFFF : 0;
    c->r[R_CX] = c->r[R_AX];
    c->r[R_BX] = c->r[R_DX];
    c->r[R_AX] = 0x8000;
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_CX], 1, 0);
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_BX], 1, (c->flags & F_CF) ? 1u : 0u);
    VG2_SET_LOW(R_CX, 5);
    c->icount += 15;
    VG2_NEAR(0xEF68, 0x791C, 6);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = VG2_FRAME(4);
    c->r[R_DX] = (c->r[R_AX] & 0x8000) ? 0xFFFF : 0;
    VG2_SET_LOW(R_CX, 5);
    c->icount += 5;
    VG2_NEAR(0xEF68, 0x7927, 3);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 2;
    VG2_NEAR(0x1007, 0x792C, 2);
    x86_leave(c);
    c->icount += 2;
    near_ret(c);
    return 1;
}

/* VGAME 1039:011A, scene_detail_level, far: the detail level 0 to 15 of
 * the view. 0 while [7D8E] is set. Otherwise the signed byte [4993] (a
 * negative one as 0) is halved, [49AA] taken off and the result clamped
 * to 0..31; a final halving gives the level, and 0 when [43E6] is clear. */
static int vgame_scene_detail_level(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 25)) return 0;
    set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));   /* sub ah, ah */
    set_r8(c, R_AL, VG2_DS8(0x7D8E));
    alu_logic(c, get_r8(c, R_AL), 0);                             /* or al, al */
    unsigned n = 4;
    if (get_r8(c, R_AL) == 0) {
        set_r8(c, R_AL, VG2_DS8(0x4993));
        c->r[R_AX] = (uint16_t)(int16_t)(int8_t)get_r8(c, R_AL);  /* cbw */
        set_r8(c, R_AH, (uint8_t)~get_r8(c, R_AH));               /* not ah */
        set_r8(c, R_AL, (uint8_t)alu_logic(c, get_r8(c, R_AL) & get_r8(c, R_AH), 0));
        set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
        c->r[R_AX] = x86_shift(c, 5, c->r[R_AX], 1, 1);           /* shr ax, 1 */
        set_r8(c, R_AH, get_r8(c, R_AL));
        set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), VG2_DS8(0x49AA), 0, 0));
        n += 9;                                                   /* mov .. jns */
        if (c->flags & F_SF) {
            set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
            n += 1;
        }
        alu_sub(c, get_r8(c, R_AH), 0x1F, 0, 0);
        n += 2;
        if (!x86_cond(c, 0xE)) { set_r8(c, R_AH, 0x1F); n += 1; }
        c->r[R_AX] = x86_shift(c, 5, c->r[R_AX], 1, 1);
        set_r8(c, R_AL, VG2_DS8(0x43E6));
        alu_logic(c, get_r8(c, R_AL), 0);
        n += 4;
        if (get_r8(c, R_AL) == 0) {
            set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
            n += 1;
        }
    }
    set_r8(c, R_AL, get_r8(c, R_AH));
    set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
    c->icount += n + 3;
    far_ret(c);
    return 1;
}

/* VGAME 120A:04C9, a vertex through the camera: the three words at 7D84
 * are copied to the vertex slot at the pointer [7D7C] (7C9C to begin
 * with), transformed by the camera matrix (1452:021B) and the pointer
 * advanced by 6. BX and DI are saved and restored. */
static int vgame_model_vertex_camera(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 12)) return 0;
    cpu_push16(c, c->r[R_BX]);
    c->r[R_BX] = 0x7C9C;
    ds_put(c, 0x7D7C, c->r[R_BX]);
    c->r[R_AX] = ds_get(c, 0x7D84);
    ds_put(c, c->r[R_BX], c->r[R_AX]);
    c->r[R_AX] = ds_get(c, 0x7D86);
    ds_put(c, (uint16_t)(c->r[R_BX] + 2), c->r[R_AX]);
    c->r[R_AX] = ds_get(c, 0x7D88);
    ds_put(c, (uint16_t)(c->r[R_BX] + 4), c->r[R_AX]);
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_BX]);
    c->icount += 11;
    VG2_FAR(0x04E5, 5);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 2, 1, 0);
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    ds_put(c, 0x7D7C, (uint16_t)alu_add(c, ds_get(c, 0x7D7C), 6, 1, 0));
    c->icount += 5;
    near_ret(c);
    return 1;
}

/* VGAME 0x04E5F, frame_palette_cycle: the day and night palette moves one
 * step toward its target. With t = [9912] & 3F the game clock's phase, only
 * every 16th tick does anything (and not while [43DC] is set); the four
 * phases each step one band of the live palette at 104A (3 bytes a colour)
 * toward the target - the table at 164A, or at 1C4A when [368A] is set -
 * by 0x0E530:
 *   t = 00h  colours 10h..4Fh, only when bit 6 of [9912] is set;
 *   t = 10h  colours 60h..9Fh;   t = 20h  colours A0h..CFh;   t = 30h  colours D0h..FFh.
 * The band is then sent to the video driver (14C2:0000, colour, count,
 * pointer), the count at phase 10h being cut to 38h when the cockpit is
 * shown ([368C]). */
static int vgame_frame_palette_cycle(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 40)) return 0;
    x86_enter(c, 0x10, 0);
    set_r8(c, R_AL, VG2_DS8(0x9912));
    c->r[R_AX] = (uint16_t)alu_logic(c, c->r[R_AX] & 0x3F, 1);
    VG2_SETFRAME(-0x0C, c->r[R_AX]);
    alu_logic(c, get_r8(c, R_AL) & 0x0F, 0);                      /* test al, 0Fh */
    unsigned n = 6;                                               /* enter .. jne */
    if (!(c->flags & F_ZF)) goto idle;
    alu_sub(c, ds_get(c, 0x43DC), 0, 1, 0);
    n += 2;
    if (!(c->flags & F_ZF)) goto idle;
    VG2_SETFRAME(-6, 0);
    n += 1;
    alu_sub(c, c->r[R_AX], 0x30, 1, 0);
    if (c->flags & F_ZF) {                                        /* phase 30h */
        VG2_SETFRAME(-2, 0xD0);
        VG2_SETFRAME(-6, 0x30);
        n += 6;
        goto band;
    }
    n += 3;
    if (x86_cond(c, 0x7)) goto band;                              /* above: nothing set */
    alu_logic(c, get_r8(c, R_AL), 0);                             /* or al, al */
    n += 2;
    if (get_r8(c, R_AL) == 0) {                                   /* phase 0 */
        alu_logic(c, VG2_DS8(0x9912) & 0x40, 0);
        n += 2;
        if (!(c->flags & F_ZF)) {
            VG2_SETFRAME(-2, 0x10);
            VG2_SETFRAME(-6, 0x40);
            n += 2;
        }
        goto band;
    }
    set_r8(c, R_AL, (uint8_t)alu_sub(c, get_r8(c, R_AL), 0x10, 0, 0));
    n += 2;
    if (c->flags & F_ZF) {                                        /* phase 10h */
        VG2_SETFRAME(-2, 0x60);
        VG2_SETFRAME(-6, 0x40);
        n += 3;
        goto band;
    }
    set_r8(c, R_AL, (uint8_t)alu_sub(c, get_r8(c, R_AL), 0x10, 0, 0));
    n += 2;
    if (c->flags & F_ZF) {                                        /* phase 20h */
        VG2_SETFRAME(-2, 0xA0);
        VG2_SETFRAME(-6, 0x30);
        n += 3;
        goto band;
    }
    n += 1;                                                       /* jmp */
band:                                                             /* 04EA5 */
    alu_sub(c, VG2_FRAME(-6), 0, 1, 0);
    n += 2;
    if (c->flags & F_ZF) goto quit;                               /* no band this tick */
    VG2_SETFRAME(-0x0A, 0x104A);
    VG2_SETFRAME(-8, c->seg[S_DS]);
    VG2_SETFRAME(-0x0E, 0x164A);
    n += 3;
    alu_sub(c, ds_get(c, 0x368A), 0, 1, 0);
    n += 2;
    if (!(c->flags & F_ZF)) {                                     /* the night target */
        c->r[R_AX] = (uint16_t)alu_add(c, x86_imul3(c, VG2_FRAME(-2), 3), 0x1C4A, 1, 0);
        n += 3;
    } else {
        c->r[R_AX] = x86_imul3(c, VG2_FRAME(-2), 3);
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], VG2_FRAME(-0x0E), 1, 0);
        n += 2;
    }
    cpu_push16(c, VG2_FRAME(-6));                                 /* the count */
    cpu_push16(c, c->seg[S_DS]);
    cpu_push16(c, c->r[R_AX]);                                    /* the target band */
    c->r[R_AX] = x86_imul3(c, VG2_FRAME(-2), 3);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], VG2_FRAME(-0x0A), 1, 0);
    c->r[R_DX] = VG2_FRAME(-8);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);                                    /* the live band */
    c->icount += n + 8;
    VG2_NEAR(0xE530, 0x4EFD, 14);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0A, 1, 0);
    alu_sub(c, VG2_FRAME(-0x0C), 0x10, 1, 0);
    n = 3;                                                        /* add, cmp, jne */
    if (c->flags & F_ZF) {
        alu_sub(c, ds_get(c, 0x368C), 0, 1, 0);
        n += 2;
        if (!(c->flags & F_ZF)) {
            VG2_SETFRAME(-6, 0x38);
            n += 1;
        }
    }
    c->r[R_AX] = x86_imul3(c, VG2_FRAME(-2), 3);
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], VG2_FRAME(-0x0A), 1, 0);
    c->r[R_DX] = VG2_FRAME(-8);
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, VG2_FRAME(-6));
    cpu_push16(c, VG2_FRAME(-2));
    c->icount += n + 7;
    VG2_FAR(0x4F24, 2);
    n = 0;
    goto quit;
idle:                                                             /* 04E77: jmp 04F29 */
    n += 1;
quit:
    x86_leave(c);
    c->icount += n + 2;
    near_ret(c);
    return 1;
}

/* VGAME 0x0C5B2, camimage_near_effects(a1, a2, a3): the camera's own
 * near-object markers, while a target is tracked ([3D96] not -1). Of the 16
 * slots at 3A08 (8 bytes: x, y, ...), each one in use (x not 0) is placed
 * relative to the eye by 0x0B7E6 with the three words given; one in front
 * of the eye ([DEBE] between -255 and -1) is then drawn: its size is
 * 0x114A:0461 of the slot's kind (word +6) and 200h / [DEBE] (|v|, limited
 * 0..FF), clamped by 0x0C67A, becomes the high byte of the model word
 * [49F6] (with the kind's bits 1 to 3 below it), and the marker is placed
 * with x and y widened by 32 (0x0EF68) by 0x0BC5F as model 25h. SI is
 * restored. */
static int vgame_camimage_near_effects(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 8)) return 0;
    x86_enter(c, 4, 0);
    cpu_push16(c, c->r[R_SI]);
    alu_sub(c, ds_get(c, 0x3D96), 0xFFFF, 1, 0);
    unsigned n = 4;                                               /* enter, push, cmp, jne */
    if (c->flags & F_ZF) { n += 1; goto leave_; }                 /* no target: jmp 0C677 */
    VG2_SETFRAME(-2, 0);
    n += 1;
head:                                                             /* 0C5C6 */
    {
        const uint16_t slot = (uint16_t)((uint16_t)(VG2_FRAME(-2) << 3));
        VG2_ROOM_OR_STOP(ds_get(c, (uint16_t)(slot + 0x3A08)) ? 4 + 8 : 9, 0xC5C6);
    }
    c->r[R_BX] = x86_shift(c, 4, VG2_FRAME(-2), 3, 1);
    alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x3A08)), 0, 1, 0);
    n += 4;                                                       /* mov, shl, cmp, jne */
    if (c->flags & F_ZF) { n += 1; goto next; }                   /* an empty slot: jmp 0C66B */
    cpu_push16(c, VG2_FRAME(8));
    cpu_push16(c, VG2_FRAME(6));
    cpu_push16(c, VG2_FRAME(4));
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x3A0C)));
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x3A0A)));
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x3A08)));
    c->r[R_SI] = c->r[R_BX];
    c->icount += n + 7;
    n = 0;
    VG2_NEAR(0xB7E6, 0xC5F0, 12);                                 /* the slot relative to the eye */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0C, 1, 0);
    alu_sub(c, ds_get(c, 0xDEBE), 0, 1, 0);
    n = 3;                                                        /* add, cmp, jge */
    if (x86_cond(c, 0xD)) goto next;                              /* not in front */
    alu_sub(c, ds_get(c, 0xDEBE), 0xFF00, 1, 0);
    n += 2;
    if (x86_cond(c, 0xE)) goto next;                              /* too far */
    cpu_push16(c, 0xFF);
    cpu_push16(c, 4);
    c->r[R_AX] = 0x0200;
    c->r[R_DX] = 0;                                               /* cwd */
    x86_idiv16(c, ds_get(c, 0xDEBE), 0);                          /* [DEBE] is -255..-1: it cannot fault */
    cpu_push16(c, c->r[R_AX]);
    c->icount += n + 6;
    VG2_NEAR(0xEE0C, 0xC613, 4);                                  /* |200h / [DEBE]| */
    c->r[R_BX] = cpu_pop16(c);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_SI] + 0x3A0E)));
    c->icount += 3;
    VG2_FAR(0xC619, 4);                                           /* the size, by 114A:0461 */
    VG2_POP2();
    cpu_push16(c, c->r[R_AX]);
    c->icount += 3;
    VG2_NEAR(0xC67A, 0xC624, 18);                                 /* clamped */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 6, 1, 0);
    VG2_SETFRAME(-4, c->r[R_AX]);
    VG2_SET_LOW(R_AX, VG2_DS8(c->r[R_SI] + 0x3A0E));
    c->r[R_AX] = (uint16_t)alu_logic(c, c->r[R_AX] & 0x0E, 1);
    c->r[R_AX] = x86_shift(c, 7, c->r[R_AX], 1, 1);               /* sar ax, 1 */
    c->r[R_CX] = (uint16_t)((c->r[R_CX] & 0x00FF) | (VG2_FRAME(-4) & 0xFF) << 8);   /* mov ch, [bp-4] */
    VG2_SET_LOW(R_CX, alu_sub(c, c->r[R_CX] & 0xFF, c->r[R_CX] & 0xFF, 0, 0));      /* sub cl, cl */
    c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], c->r[R_CX], 1, 0);
    ds_put(c, 0x49F6, c->r[R_AX]);
    cpu_push16(c, 1);
    cpu_push16(c, 0);
    cpu_push16(c, 0);
    cpu_push16(c, 0);
    cpu_push16(c, ds_get(c, (uint16_t)(c->r[R_SI] + 0x3A0C)));
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_SI] + 0x3A0A));
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    VG2_SET_LOW(R_CX, 5);
    c->icount += 17;
    VG2_NEAR(0xEF68, 0xC654, 6);                                  /* y << 5 */
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_SI] + 0x3A08));
    c->r[R_DX] = (uint16_t)alu_sub(c, c->r[R_DX], c->r[R_DX], 1, 0);
    VG2_SET_LOW(R_CX, 5);
    c->icount += 5;
    VG2_NEAR(0xEF68, 0xC661, 4);                                  /* x << 5 */
    cpu_push16(c, c->r[R_DX]);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, 0x25);
    c->icount += 3;
    VG2_NEAR(0xBC5F, 0xC668, 5);                                  /* the marker */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x14, 1, 0);
    n = 1;
next:                                                             /* 0C66B */
    VG2_SETFRAME(-2, (uint16_t)alu_inc(c, VG2_FRAME(-2), 1));
    alu_sub(c, VG2_FRAME(-2), 0x10, 1, 0);
    n += 3;                                                       /* inc, cmp, jge */
    if (x86_cond(c, 0xD)) goto leave_;
    n += 1;                                                       /* jmp 0C5C6 */
    goto head;
leave_:                                                           /* 0C677 */
    VG2_ROOM_OR_STOP(3, 0xC677);
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x0C4BA, camimage_aircraft_glint(a1, a2, a3, x, y, z): the glint
 * on a flagged aircraft in the camera view. Six slots at 3A8A (8 bytes: two
 * jitter offsets to the angles, a frame counter, an active flag) each add
 * their jitter to a1 and a2, fold a2 back when it passes a quarter turn
 * (|a2| > 4000h or = 8000h: a2 = 8000h - a2 and a1 += 8000h in its high
 * byte), and place a model by 0x0E2B6: slots 0..3 the frame model 25h +
 * counter, slots 4 and 5 model 24h (the counter then also saved at
 * [C06C]); the counter advances at each use. A slot that is idle or has
 * reached 6 gets new jitter from the random numbers (0x0C88C: 7 twice,
 * the offsets from the table at 4398) and a counter of 1 or 2, and is
 * marked active. SI is restored. */
static int vgame_camimage_aircraft_glint(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 4)) return 0;
    x86_enter(c, 8, 0);
    cpu_push16(c, c->r[R_SI]);
    VG2_SETFRAME(-4, 0);
    unsigned n = 4;                                               /* enter, push, mov, jmp */
head:                                                             /* 0C533 */
    {
        const int16_t i = (int16_t)VG2_FRAME(-4);
        const uint16_t slot = (uint16_t)((uint16_t)i << 3);
        unsigned need = 16;
        if (i >= 6) need = 5;
        else if (ds_get(c, (uint16_t)(slot + 0x3A90)) == 0) need = 8;
        else if ((int16_t)ds_get(c, (uint16_t)(slot + 0x3A8E)) >= 6) need = 10;
        VG2_ROOM_OR_STOP(need, 0xC533);
    }
    alu_sub(c, VG2_FRAME(-4), 6, 1, 0);
    n += 2;
    if (x86_cond(c, 0xD)) goto leave_;                            /* all six done */
    c->r[R_BX] = x86_shift(c, 4, VG2_FRAME(-4), 3, 1);
    alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x3A90)), 0, 1, 0);
    n += 4;
    if (c->flags & F_ZF) goto respawn;                            /* idle */
    alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x3A8E)), 6, 1, 0);
    n += 2;
    if (x86_cond(c, 0xD)) goto respawn;                           /* the counter ran out */
    c->r[R_AX] = (uint16_t)alu_add(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x3A8A)), VG2_FRAME(4), 1, 0);
    VG2_SETFRAME(-2, c->r[R_AX]);                                 /* a1 + jitter */
    c->r[R_AX] = (uint16_t)alu_add(c, ds_get(c, (uint16_t)(c->r[R_BX] + 0x3A8C)), VG2_FRAME(6), 1, 0);
    VG2_SETFRAME(-6, c->r[R_AX]);                                 /* a2 + jitter */
    cpu_push16(c, c->r[R_AX]);
    c->icount += n + 7;
    VG2_NEAR(0xEE0C, 0xC565, 23);                                 /* |a2| */
    c->r[R_BX] = cpu_pop16(c);
    alu_sub(c, c->r[R_AX], 0x4000, 1, 0);
    n = 3;                                                        /* pop, cmp, jg */
    int fold = x86_cond(c, 0xF);
    if (!fold) {
        alu_sub(c, VG2_FRAME(-6), 0x8000, 1, 0);
        n += 2;                                                   /* cmp, jne */
        fold = (c->flags & F_ZF) != 0;
    }
    if (fold) {
        c->r[R_AX] = (uint16_t)alu_sub(c, 0x8000, VG2_FRAME(-6), 1, 0);
        VG2_SETFRAME(-6, c->r[R_AX]);
        const uint32_t hi = phys(c->seg[S_SS], (uint16_t)(c->r[R_BP] - 1));
        mem_write8(c, hi, (uint8_t)alu_add(c, mem_read8(c, hi), 0x80, 0, 0));
        n += 4;
    }
    alu_sub(c, VG2_FRAME(-4), 4, 1, 0);
    n += 2;                                                       /* cmp, jl */
    if (!x86_cond(c, 0xC)) {                                      /* slots 4 and 5: model 24h */
        n += 1;                                                   /* jmp 0C4C6 */
        c->r[R_BX] = x86_shift(c, 4, VG2_FRAME(-4), 3, 1);
        c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x3A8E));
        ds_put(c, (uint16_t)(c->r[R_BX] + 0x3A8E), (uint16_t)alu_inc(c, c->r[R_AX], 1));
        ds_put(c, 0xC06C, c->r[R_AX]);
        cpu_push16(c, VG2_FRAME(0x0E));
        cpu_push16(c, VG2_FRAME(0x0C));
        cpu_push16(c, VG2_FRAME(0x0A));
        cpu_push16(c, VG2_FRAME(8));
        cpu_push16(c, VG2_FRAME(-6));
        cpu_push16(c, VG2_FRAME(-2));
        cpu_push16(c, 0x24);
        c->icount += n + 12;
    } else {                                                      /* slots 0..3: model 25h + counter */
        cpu_push16(c, VG2_FRAME(0x0E));
        cpu_push16(c, VG2_FRAME(0x0C));
        cpu_push16(c, VG2_FRAME(0x0A));
        cpu_push16(c, VG2_FRAME(8));
        cpu_push16(c, VG2_FRAME(-6));
        cpu_push16(c, VG2_FRAME(-2));
        c->r[R_BX] = x86_shift(c, 4, VG2_FRAME(-4), 3, 1);
        c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x3A8E));
        ds_put(c, (uint16_t)(c->r[R_BX] + 0x3A8E), (uint16_t)alu_inc(c, c->r[R_AX], 1));
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x25, 1, 0);
        cpu_push16(c, c->r[R_AX]);
        c->icount += n + 13;
    }
    n = 0;
    VG2_NEAR(0xE2B6, 0xC4EE, 3);                                  /* the glint model */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0E, 1, 0);
    n = 2;                                                        /* add, jmp */
    goto advance;
respawn:                                                          /* 0C4F3: new jitter and lifetime */
    cpu_push16(c, 7);
    c->icount += n + 1;
    n = 0;
    VG2_NEAR(0xC88C, 0xC4F8, 10);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = x86_shift(c, 4, c->r[R_AX], 1, 1);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x4398));
    c->r[R_BX] = x86_shift(c, 4, VG2_FRAME(-4), 3, 1);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x3A8A), c->r[R_AX]);
    cpu_push16(c, 7);
    c->r[R_SI] = c->r[R_BX];
    c->icount += 9;
    VG2_NEAR(0xC88C, 0xC512, 7);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_BX] = x86_shift(c, 4, c->r[R_AX], 1, 1);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x4398));
    ds_put(c, (uint16_t)(c->r[R_SI] + 0x3A8C), c->r[R_AX]);
    cpu_push16(c, 2);
    c->icount += 6;
    VG2_NEAR(0xC88C, 0xC524, 5);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_AX] = (uint16_t)alu_inc(c, c->r[R_AX], 1);
    ds_put(c, (uint16_t)(c->r[R_SI] + 0x3A8E), c->r[R_AX]);
    ds_put(c, (uint16_t)(c->r[R_SI] + 0x3A90), 1);
    n = 4;                                                        /* pop, inc, mov, mov */
advance:                                                          /* 0C530 */
    VG2_SETFRAME(-4, (uint16_t)alu_inc(c, VG2_FRAME(-4), 1));
    n += 1;
    goto head;
leave_:                                                           /* 0C5AF */
    VG2_ROOM_OR_STOP(3, 0xC5AF);
    c->r[R_SI] = cpu_pop16(c);
    x86_leave(c);
    c->icount += 3;
    near_ret(c);
    return 1;
}

/* VGAME 120A:043C, model_vertex_world: a model vertex (three words at ES:SI)
 * is rotated by the camera matrix (1452:0330, row vector by matrix to
 * 32 bits) twice - first the vertex at the slot BX by the matrix at 7D3E
 * and then, negated, the offset at 7D56 (the second multiply is given
 * DI - 12h) - and the six 32-bit sums go to the three dwords at [7D56]
 * (offsets 0, 4, 8): each is the camera position dword ([7CD2], [7CD6],
 * [7CDA] with the word after it) plus the rotated vertex ([7D3E], [7D42],
 * [7D46] likewise). BX is restored. */
static int vgame_model_vertex_world(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 11)) return 0;
    const uint16_t es = c->seg[S_ES];
    for (int k = 0; k < 3; k++)
        seg_write16(c, c->seg[S_DS], (uint16_t)(c->r[R_BX] + 2 * k), seg_read16(c, es, (uint16_t)(c->r[R_SI] + 2 * k)));
    c->r[R_AX] = 0x7D3E;                                          /* lea ax, [7D3E] */
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, ds_get(c, 0x7D50));
    cpu_push16(c, c->r[R_BX]);
    c->icount += 10;
    VG2_FAR(0x0459, 11);                                          /* the vertex by the matrix */
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 4, 1, 0);
    for (int k = 0; k < 3; k++) {                                 /* neg [bx], [bx+2], [bx+4] */
        const uint16_t a = (uint16_t)(c->r[R_BX] + 2 * k);
        ds_put(c, a, (uint16_t)alu_sub(c, 0, ds_get(c, a), 1, 0));
    }
    cpu_push16(c, ds_get(c, 0x7D56));
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_DI], 0x12, 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, c->r[R_BX]);
    c->icount += 10;
    VG2_FAR(0x0475, 28);                                          /* the offset by the matrix */
    c->r[R_BX] = ds_get(c, 0x7D56);
    static const uint16_t sum[3][5] = {
        { 0, 0x7CD2, 0x7CD4, 0x7D3E, 0x7D40 },
        { 4, 0x7CD6, 0x7CD8, 0x7D42, 0x7D44 },
        { 8, 0x7CDA, 0x7CDC, 0x7D46, 0x7D48 } };
    for (int k = 0; k < 3; k++) {
        const uint16_t lo = (uint16_t)(c->r[R_BX] + sum[k][0]), hi = (uint16_t)(lo + 2);
        for (int s = 0; s < 2; s++) {
            c->r[R_AX] = ds_get(c, sum[k][1 + 2 * s]);
            ds_put(c, lo, (uint16_t)alu_add(c, ds_get(c, lo), c->r[R_AX], 1, 0));
            c->r[R_AX] = ds_get(c, sum[k][2 + 2 * s]);
            ds_put(c, hi, (uint16_t)alu_add(c, ds_get(c, hi), c->r[R_AX], 1, (c->flags & F_CF) ? 1u : 0u));
        }
    }
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 4, 1, 0);
    c->icount += 1 + 24 + 3;
    near_ret(c);
    return 1;
}

/* VGAME 0x0E2B6, compose_emit_object(model, a1, a2, a3, x, y, z): an object
 * placed in the scene. The arguments are stored (x, y, z at 499A..499E, the
 * model word at 4986); the model word's bit 15 is kept on the stack, its
 * high byte (below bit 15) selects a table (words at 0A60, indexing with
 * the model number's low byte) whose byte gives the detail class: its low
 * three bits go to [49AA], and the model page word for the high byte
 * ([B78A + 2 * high byte]) to [8590]. The position is taken relative to
 * the camera ([49AC..49B0] subtracted, to 4994..4998) and culled by
 * 0x0E3BF. A culled object ends the routine. A visible one with bit 7 of
 * its class goes to the deferred list (0x0DB3C). Otherwise the detail level
 * (1039:011A) goes to [48C4], the second matrix (49D0) is built by
 * 1452:03AB from the negated first two angles and the third, its off-
 * diagonal pairs are swapped (a transpose), and the model is drawn by
 * 120A:00CF with the position, y negated. SI, DI and BP are restored. */
static int vgame_compose_emit_object(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 42)) return 0;
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    c->r[R_AX] = VG2_FRAME(6);
    ds_put(c, 0x499A, c->r[R_AX]);
    c->r[R_AX] = VG2_FRAME(8);
    ds_put(c, 0x499C, c->r[R_AX]);
    c->r[R_AX] = VG2_FRAME(0x0A);
    ds_put(c, 0x499E, c->r[R_AX]);
    c->r[R_AX] = VG2_FRAME(4);
    ds_put(c, 0x4986, c->r[R_AX]);
    c->r[R_BX] = (uint16_t)alu_sub(c, c->r[R_BX], c->r[R_BX], 1, 0);
    alu_logic(c, c->r[R_AX] & 0x8000, 1);                         /* test ax, 8000h */
    unsigned pre = 40;                                            /* the instructions up to the call */
    if (!(c->flags & F_ZF)) { c->r[R_BX] = (uint16_t)alu_inc(c, c->r[R_BX], 1); pre++; }
    cpu_push16(c, c->r[R_BX]);                                    /* bit 15 */
    c->r[R_AX] = (uint16_t)alu_logic(c, c->r[R_AX] & 0x7FFF, 1);
    VG2_SET_LOW(R_BX, c->r[R_AX] >> 8);                           /* mov bl, ah */
    c->r[R_AX] = (uint16_t)alu_logic(c, c->r[R_AX] & 0x7F, 1);
    VG2_SETFRAME(4, c->r[R_AX]);
    c->r[R_BX] = (uint16_t)alu_add(c, c->r[R_BX], c->r[R_BX], 1, 0);
    c->r[R_SI] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x0A60));
    c->r[R_AX] = (uint16_t)alu_logic(c, c->r[R_AX] & 0xFF, 1);
    c->r[R_SI] = (uint16_t)alu_add(c, c->r[R_SI], c->r[R_AX], 1, 0);
    x86_lods(c, 0, c->seg[S_DS]);                                 /* the class byte */
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_logic(c, c->r[R_AX] & 7, 1);
    ds_put(c, 0x49AA, c->r[R_AX]);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] - 0x4876));
    ds_put(c, 0x8590, c->r[R_AX]);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BX] = (uint16_t)alu_sub(c, VG2_FRAME(0x0E), ds_get(c, 0x49AE), 1, 0);
    ds_put(c, 0x4996, c->r[R_BX]);
    c->r[R_CX] = (uint16_t)alu_sub(c, VG2_FRAME(0x10), ds_get(c, 0x49B0), 1, 0);
    ds_put(c, 0x4998, c->r[R_CX]);
    c->r[R_BP] = (uint16_t)alu_sub(c, VG2_FRAME(0x0C), ds_get(c, 0x49AC), 1, 0);
    ds_put(c, 0x4994, c->r[R_BP]);
    c->icount += pre;
    VG2_NEAR(0xE3BF, 0xE326, 8);                                  /* project and cull */
    c->r[R_BP] = cpu_pop16(c);
    c->r[R_AX] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    unsigned n = 4;                                               /* pop, pop, pop, jne */
    if (!(c->flags & F_ZF)) goto done;                            /* culled */
    alu_logic(c, c->r[R_AX] & 0x80, 1);
    n += 2;
    if (!(c->flags & F_ZF)) {                                     /* a deferred draw */
        c->icount += n;
        VG2_NEAR(0xDB3C, 0xE333, 5);
        n = 1;                                                    /* jmp */
        goto done;
    }
    ds_put(c, 0xC05C, c->r[R_BX]);
    c->icount += n + 1;
    VG2_FAR(0xE33A, 11);                                          /* the detail level */
    VG2_SETDS8(0x48C4, c->r[R_AX]);
    c->r[R_AX] = 0x49D0;
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, VG2_FRAME(0x0A));
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, VG2_FRAME(8), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, VG2_FRAME(6), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 10;
    VG2_FAR(0xE355, 24);                                          /* the second matrix */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 8, 1, 0);
    {
        static const uint16_t swap[3][2] = { { 0x49D2, 0x49D6 }, { 0x49D4, 0x49DC }, { 0x49DA, 0x49DE } };
        for (int k = 0; k < 3; k++) {
            c->r[R_AX] = ds_get(c, swap[k][0]);
            c->r[R_DX] = ds_get(c, swap[k][1]);
            ds_put(c, swap[k][0], c->r[R_DX]);
            ds_put(c, swap[k][1], c->r[R_AX]);
        }
    }
    c->r[R_AX] = 0xC05A;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = 0x49D0;
    cpu_push16(c, c->r[R_AX]);
    c->r[R_AX] = (uint16_t)alu_sub(c, 0, ds_get(c, 0x4996), 1, 0);
    cpu_push16(c, c->r[R_AX]);
    cpu_push16(c, ds_get(c, 0x4998));
    cpu_push16(c, ds_get(c, 0x4994));
    cpu_push16(c, VG2_FRAME(4));
    c->icount += 23;
    VG2_FAR(0xE3A0, 5);                                           /* the model */
    c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], 0x0C, 1, 0);
    n = 1;
done:                                                             /* 0E3A8 */
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += n + 4;
    near_ret(c);
    return 1;
}

/* VGAME 0x0DB3C, scene_defer_object: the object being placed (its fields
 * at 4986..499E and 49F6, set by 0x0E2B6) goes on the list of deferred
 * objects, which is kept in order of distance. The list is [49F4] pointers
 * at 4E6C to 22-byte records at 4A20; a full list (50 entries) drops its
 * first and reuses that record. The record's key is the camera distance
 * dword [4990] arithmetically shifted right by (4 - [E328]) * 2 (0x0EF74),
 * 2000h added to its high word for the detail class 5 in view mode 2; then
 * the detail level (1039:011A) and the eight words follow. The new record
 * is inserted after the last entry whose key is not above it, the later
 * pointers moved up one place, and the count incremented. */
static int vgame_scene_defer_object(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t count = ds_get(c, 0x49F4);
    if (!room(c, count == 0x32 ? 357 : 12)) return 0;
    x86_enter(c, 6, 0);
    alu_sub(c, count, 0x32, 1, 0);
    unsigned n = 3;                                               /* enter, cmp, jne */
    if (c->flags & F_ZF) {                                        /* full: drop the first entry */
        c->r[R_AX] = ds_get(c, 0x4E6C);
        VG2_SETFRAME(-2, c->r[R_AX]);
        VG2_SETFRAME(-4, 1);
        n += 3;
        do {
            c->r[R_BX] = x86_shift(c, 4, VG2_FRAME(-4), 1, 1);
            c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x4E6C));
            ds_put(c, (uint16_t)(c->r[R_BX] + 0x4E6A), c->r[R_AX]);
            VG2_SETFRAME(-4, (uint16_t)alu_inc(c, VG2_FRAME(-4), 1));
            alu_sub(c, VG2_FRAME(-4), 0x32, 1, 0);
            n += 7;
        } while (x86_cond(c, 0xC));
        ds_put(c, 0x49F4, (uint16_t)alu_dec(c, ds_get(c, 0x49F4), 1));
        n += 2;                                                   /* dec, jmp */
    } else {                                                      /* the next free record */
        c->r[R_AX] = x86_imul3(c, ds_get(c, 0x49F4), 0x16);
        c->r[R_AX] = (uint16_t)alu_add(c, c->r[R_AX], 0x4A20, 1, 0);
        VG2_SETFRAME(-2, c->r[R_AX]);
        n += 3;
    }
    c->r[R_AX] = ds_get(c, 0x4990);
    c->r[R_DX] = ds_get(c, 0x4992);
    VG2_SET_LOW(R_CX, 4);
    VG2_SET_LOW(R_CX, alu_sub(c, c->r[R_CX] & 0xFF, VG2_DS8(0xE328), 0, 0));
    VG2_SET_LOW(R_CX, x86_shift(c, 4, c->r[R_CX] & 0xFF, 1, 0));  /* shl cl, 1 */
    c->icount += n + 5;
    VG2_NEAR(0xEF74, 0xDB8B, 10);                                 /* the key: distance >> CL */
    c->r[R_BX] = VG2_FRAME(-2);
    ds_put(c, c->r[R_BX], c->r[R_AX]);
    ds_put(c, (uint16_t)(c->r[R_BX] + 2), c->r[R_DX]);
    alu_sub(c, ds_get(c, 0xE328), 2, 1, 0);
    n = 5;                                                        /* mov, mov, mov, cmp, jne */
    if (c->flags & F_ZF) {
        alu_sub(c, ds_get(c, 0x49AA), 5, 1, 0);
        n += 2;
        if (c->flags & F_ZF) {
            ds_put(c, c->r[R_BX], (uint16_t)alu_add(c, ds_get(c, c->r[R_BX]), 0, 1, 0));
            ds_put(c, (uint16_t)(c->r[R_BX] + 2),
                   (uint16_t)alu_add(c, ds_get(c, (uint16_t)(c->r[R_BX] + 2)), 0x20, 1, (c->flags & F_CF) ? 1u : 0u));
            n += 2;
        }
    }
    c->icount += n;
    VG2_FAR(0xDBA8, 23);                                          /* the detail level */
    set_r8(c, R_AH, (uint8_t)alu_sub(c, get_r8(c, R_AH), get_r8(c, R_AH), 0, 0));
    c->r[R_BX] = VG2_FRAME(-2);
    ds_put(c, (uint16_t)(c->r[R_BX] + 4), c->r[R_AX]);
    {
        static const uint16_t fields[8][2] = {
            { 0x49F6, 6 }, { 0x4986, 8 }, { 0x4994, 0x0A }, { 0x4996, 0x0C },
            { 0x4998, 0x0E }, { 0x499A, 0x10 }, { 0x499C, 0x12 }, { 0x499E, 0x14 } };
        for (int k = 0; k < 8; k++) {
            c->r[R_AX] = ds_get(c, fields[k][0]);
            ds_put(c, (uint16_t)(c->r[R_BX] + fields[k][1]), c->r[R_AX]);
        }
    }
    c->r[R_AX] = (uint16_t)alu_dec(c, ds_get(c, 0x49F4), 1);
    VG2_SETFRAME(-4, c->r[R_AX]);
    n = 3 + 16 + 4;                                               /* sub .. jmp */
search:                                                           /* 0DBF1: from the end, back to the place */
    VG2_ROOM_OR_STOP(17, 0xDBF1);
    alu_sub(c, VG2_FRAME(-4), 0xFFFF, 1, 0);
    n += 2;
    if (x86_cond(c, 0xE)) goto place;                             /* jle: the front of the list */
    c->r[R_BX] = VG2_FRAME(-2);
    c->r[R_AX] = ds_get(c, c->r[R_BX]);
    c->r[R_DX] = ds_get(c, (uint16_t)(c->r[R_BX] + 2));
    c->r[R_BX] = x86_shift(c, 4, VG2_FRAME(-4), 1, 1);
    c->r[R_BX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x4E6C));
    alu_sub(c, ds_get(c, (uint16_t)(c->r[R_BX] + 2)), c->r[R_DX], 1, 0);
    n += 7;                                                       /* mov .. cmp */
    n += 1;                                                       /* jl */
    if (x86_cond(c, 0xC)) goto earlier;                           /* that entry's key is lower: look further */
    n += 1;                                                       /* jg */
    if (x86_cond(c, 0xF)) goto place;
    alu_sub(c, ds_get(c, c->r[R_BX]), c->r[R_AX], 1, 0);
    n += 2;                                                       /* cmp, jbe */
    if (!x86_cond(c, 0x6)) goto place;
earlier:                                                          /* 0DBEE */
    VG2_SETFRAME(-4, (uint16_t)alu_dec(c, VG2_FRAME(-4), 1));
    n += 1;
    goto search;
place:                                                            /* 0DC13: move the later pointers up */
    c->r[R_AX] = (uint16_t)alu_dec(c, ds_get(c, 0x49F4), 1);
    VG2_SETFRAME(-6, c->r[R_AX]);
    n += 4;                                                       /* mov, dec, mov, jmp */
shift:                                                            /* 0DC2C */
    VG2_ROOM_OR_STOP(10, 0xDC2C);
    c->r[R_AX] = VG2_FRAME(-4);
    alu_sub(c, VG2_FRAME(-6), c->r[R_AX], 1, 0);
    n += 3;
    if (!x86_cond(c, 0xF)) goto finish;                           /* jg not taken */
    c->r[R_BX] = x86_shift(c, 4, VG2_FRAME(-6), 1, 1);
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_BX] + 0x4E6C));
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x4E6E), c->r[R_AX]);
    VG2_SETFRAME(-6, (uint16_t)alu_dec(c, VG2_FRAME(-6), 1));
    n += 5;
    goto shift;
finish:                                                           /* 0DC34 */
    c->r[R_AX] = VG2_FRAME(-2);
    c->r[R_BX] = x86_shift(c, 4, VG2_FRAME(-4), 1, 1);
    ds_put(c, (uint16_t)(c->r[R_BX] + 0x4E6E), c->r[R_AX]);
    ds_put(c, 0x49F4, (uint16_t)alu_inc(c, ds_get(c, 0x49F4), 1));
    x86_leave(c);
    c->icount += n + 7;
    near_ret(c);
    return 1;
}

#undef VG2_NEAR
#undef VG2_FAR
#undef VG2_FRAME
#undef VG2_SETFRAME
#undef VG2_DS8
#undef VG2_SETDS8
#undef VG2_POP2
#undef VG2_SET_LOW
#undef VG2_ROOM_OR_STOP

/* ---- VGAME, third batch of matched routines (Phase 2) --------------------
 *
 * Text and panel glue, the raster wrappers and the runtime's integer to
 * text. Held the same way as the second batch: a callee runs as original
 * code, the stretch after it needs room for all of its instructions
 * (counting the CALL that ends it) or IP is left at the return address, and
 * a loop whose length depends on data looks at the room on every turn and
 * stops at its head when there is none. */

/* A near call to target_ returning to ret_, then room for the next_
 * instructions after it; otherwise IP is left at the return address. */
#define VG3_NEAR(target_, ret_, next_) do {                                           \
        if (!guest_call(m, (target_), (ret_))) return 1;                              \
        if (!room(c, (next_))) { c->ip = (ret_); return 1; }                          \
    } while (0)
/* The same for a far call at CS:ip_ (9A off seg; five bytes). */
#define VG3_FAR(ip_, next_) do {                                                      \
        if (!guest_call_far(m, (ip_), (uint16_t)((ip_) + 5))) return 1;               \
        if (!room(c, (next_))) { c->ip = (uint16_t)((ip_) + 5); return 1; }           \
    } while (0)
/* Count the n instructions run so far; stop at ip_ (the original carries on
 * from there) unless need_ more fit. */
#define VG3_ROOM_OR_STOP(need_, ip_) do {                                             \
        c->icount += n; n = 0;                                                        \
        if (!room(c, (need_))) { c->ip = (uint16_t)(ip_); return 1; }                 \
    } while (0)
#define VG3_FRAME(o) seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + (o)))
#define VG3_SETFRAME(o, v) seg_write16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + (o)), (v))
#define VG3_DS8(off) mem_read8(c, phys(c->seg[S_DS], (uint16_t)(off)))
#define VG3_SETDS8(off, v) mem_write8(c, phys(c->seg[S_DS], (uint16_t)(off)), (uint8_t)(v))
/* ADD SP, k after a call: the caller's release of its arguments. */
#define VG3_DROP(k) (c->r[R_SP] = (uint16_t)alu_add(c, c->r[R_SP], (k), 1, 0))

/* VGAME 130D:0191 and 130D:01A7, near: close a clipped model polygon along
 * the window's right or left edge - a vertical run (mpoly_run, 1377:07E8)
 * one column outside the window ([85FE]+1, or [85FA]-1) over the rows the
 * side recorded ([85EA]..[85E8] for the right, [85E6]..[85E4] for the
 * left). */
static int vg3_poly_edge_run(machine_t *m, uint16_t edge_at, int step, uint16_t rows_at, uint16_t call_ip)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5 + 1)) return 0;                              /* counting the CALL */
    c->r[R_AX] = (uint16_t)(step > 0 ? alu_inc(c, ds_get(c, edge_at), 1) : alu_dec(c, ds_get(c, edge_at), 1));
    cpu_push16(c, c->r[R_AX]);                                    /* x */
    cpu_push16(c, ds_get(c, rows_at));                            /* y1 */
    cpu_push16(c, ds_get(c, (uint16_t)(rows_at + 2)));            /* y0 */
    c->icount += 5;
    VG3_FAR(call_ip, 2);
    VG3_DROP(6);
    c->icount += 2;
    near_ret(c);
    return 1;
}
static int vgame_poly_edge_right(machine_t *m) { return vg3_poly_edge_run(m, 0x85FE, 1, 0x85E8, 0x019E); }
static int vgame_poly_edge_left(machine_t *m) { return vg3_poly_edge_run(m, 0x85FA, -1, 0x85E4, 0x01B4); }

/* VGAME 0x08342, view_caption: while the cockpit is shown ([368C]) the
 * view's direction caption goes to the message buffer 98A6 - " AHEAD"
 * (40F8), the right (4108), behind (4100) or the left (4110) for the view
 * quadrant [E582] of 0, 4000h, 8000h or C000h, the buffer as it was for
 * anything else - and is drawn by the display text routine 0x089C0 at
 * (C5h, C1h) on the element [4010] in the colour pair that follows the
 * element's state cache [4856] (2Fh/2Eh when it is 1, else 2Dh/2Ch).
 * The element's words +2 and +6 are set to 1 and the background colour
 * for the draw and put back after. */
static int vgame_view_caption(machine_t *m)
{
    cpu_t *c = &m->cpu;
    static const uint16_t caption[4] = { 0x40F8, 0x4108, 0x4100, 0x4110 };
    if (!room(c, 34)) return 0;
    x86_enter(c, 8, 0);
    alu_sub(c, ds_get(c, 0x368C), 0, 1, 0);
    if (c->flags & F_ZF) {                                        /* the cockpit is hidden */
        x86_leave(c);
        c->icount += 6;
        near_ret(c);
        return 1;
    }
    alu_sub(c, ds_get(c, 0x4856), 1, 1, 0);
    unsigned n = 3 + 4 + 2;                                       /* enter .. jne; cmp .. mov; mov ax, jmp */
    if (c->flags & F_ZF) { VG3_SETFRAME(-2, 0x2F); VG3_SETFRAME(-4, 0x2E); n++; }
    else { VG3_SETFRAME(-2, 0x2D); VG3_SETFRAME(-4, 0x2C); }
    uint16_t ax = ds_get(c, 0xE582);
    alu_logic(c, ax, 1);                                          /* or ax, ax */
    n += 2;
    int k = 0;
    while (!(c->flags & F_ZF) && k < 3) {                         /* sub ax, 4000h; je */
        ax = (uint16_t)alu_sub(c, ax, 0x4000, 1, 0);
        k++;
        n += 2;
    }
    c->r[R_AX] = ax;
    if (c->flags & F_ZF) {                                        /* a quadrant: its caption */
        cpu_push16(c, caption[k]);
        cpu_push16(c, 0x98A6);
        c->icount += n + (k ? 2 : 1) + 1;
        VG3_NEAR(0xEB50, 0x837B, 3 + 15 + 1);                     /* strcpy */
        c->r[R_BX] = cpu_pop16(c);
        c->r[R_BX] = cpu_pop16(c);
        n = 3;
    }
    c->r[R_BX] = ds_get(c, 0x4010);                               /* 083A1: draw it */
    VG3_SETFRAME(-6, ds_get(c, (uint16_t)(c->r[R_BX] + 2)));
    VG3_SETFRAME(-8, ds_get(c, (uint16_t)(c->r[R_BX] + 6)));
    c->r[R_AX] = VG3_FRAME(-4);
    ds_put(c, (uint16_t)(c->r[R_BX] + 6), c->r[R_AX]);
    c->r[R_BX] = ds_get(c, 0x4010);
    ds_put(c, (uint16_t)(c->r[R_BX] + 2), 1);
    cpu_push16(c, VG3_FRAME(-2));
    cpu_push16(c, 0xC1);
    cpu_push16(c, 0xC5);
    cpu_push16(c, 0x98A6);
    cpu_push16(c, ds_get(c, 0x4010));
    c->icount += n + 15;
    VG3_NEAR(0x89C0, 0x83D3, 8);
    c->r[R_AX] = VG3_FRAME(-8);
    c->r[R_BX] = ds_get(c, 0x4010);
    ds_put(c, (uint16_t)(c->r[R_BX] + 6), c->r[R_AX]);
    c->r[R_AX] = VG3_FRAME(-6);
    c->r[R_BX] = ds_get(c, 0x4010);
    ds_put(c, (uint16_t)(c->r[R_BX] + 2), c->r[R_AX]);
    x86_leave(c);
    c->icount += 8;
    near_ret(c);
    return 1;
}

/* VGAME 0x0B13C, camera_view_caption: the camera's direction relative to
 * the aircraft - the bearing [2DEE] less the heading [DF00], rounded to a
 * quadrant (+2000h, top two bits) with the two side quadrants swapped
 * (+8000h on the high byte) - and, when it differs from the quadrant the
 * caption shows ([E582]), the new quadrant is kept there and the caption
 * redrawn (view_caption, 0x08342). */
static int vgame_camera_view_caption(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 17)) return 0;
    x86_enter(c, 2, 0);
    uint16_t ax = (uint16_t)alu_sub(c, ds_get(c, 0x2DEE), ds_get(c, 0xDF00), 1, 0);
    ax = (uint16_t)((ax & 0x00FF) | (alu_add(c, ax >> 8, 0x20, 0, 0) << 8));
    ax = (uint16_t)alu_logic(c, ax & 0xC000, 1);
    c->r[R_AX] = ax;
    VG3_SETFRAME(-2, ax);
    alu_sub(c, ax, 0x4000, 1, 0);
    unsigned n = 8;
    int side = (c->flags & F_ZF) != 0;
    if (!side) {
        alu_sub(c, ax, 0xC000, 1, 0);
        side = (c->flags & F_ZF) != 0;
        n += 2;
    }
    if (side) {                                                   /* add byte [bp-1], 80h */
        const uint16_t at = (uint16_t)(c->r[R_BP] - 1);
        mem_write8(c, phys(c->seg[S_SS], at), (uint8_t)alu_add(c, mem_read8(c, phys(c->seg[S_SS], at)), 0x80, 0, 0));
        n++;
    }
    c->r[R_AX] = ds_get(c, 0xE582);
    alu_sub(c, VG3_FRAME(-2), c->r[R_AX], 1, 0);
    n += 3;
    if (!(c->flags & F_ZF)) {                                     /* a new quadrant */
        c->r[R_AX] = VG3_FRAME(-2);
        ds_put(c, 0xE582, c->r[R_AX]);
        c->icount += n + 2;
        VG3_NEAR(0x8342, 0xB16F, 2);
        n = 0;
    }
    x86_leave(c);
    c->icount += n + 2;
    near_ret(c);
    return 1;
}

/* VGAME 0FB2:0324 and 0FB2:069A, far: the far entries of two near raster
 * routines (0FB2:0330, the sky bands, and 0FB2:06A6, a boxed line into
 * the span tables), keeping BP, SI, DI and ES around the call; they push
 * them in different orders. */
static int vg3_raster_far_entry(machine_t *m, int bp_first, uint16_t target, uint16_t ret_ip)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 4 + 1)) return 0;
    if (bp_first) {
        cpu_push16(c, c->r[R_BP]); cpu_push16(c, c->r[R_SI]); cpu_push16(c, c->r[R_DI]); cpu_push16(c, c->seg[S_ES]);
    } else {
        cpu_push16(c, c->seg[S_ES]); cpu_push16(c, c->r[R_SI]); cpu_push16(c, c->r[R_DI]); cpu_push16(c, c->r[R_BP]);
    }
    c->icount += 4;
    VG3_NEAR(target, ret_ip, 5);
    if (bp_first) {
        c->seg[S_ES] = cpu_pop16(c); c->r[R_DI] = cpu_pop16(c); c->r[R_SI] = cpu_pop16(c); c->r[R_BP] = cpu_pop16(c);
    } else {
        c->r[R_BP] = cpu_pop16(c); c->r[R_DI] = cpu_pop16(c); c->r[R_SI] = cpu_pop16(c); c->seg[S_ES] = cpu_pop16(c);
    }
    c->icount += 5;
    far_ret(c);
    return 1;
}
static int vgame_sky_bands_far(machine_t *m) { return vg3_raster_far_entry(m, 1, 0x0330, 0x032B); }
static int vgame_boxed_line_far(machine_t *m) { return vg3_raster_far_entry(m, 0, 0x06A6, 0x06A1); }

/* VGAME 1058:0C91 and 1058:0CB2, far: read the joystick port (1058:0CC1,
 * both axes' counts into [2CCA]/[2CCC]) and then, for axis 0 and axis 1
 * (SI 0 and 2, or 0 and 1), either centre the calibration on the reading
 * (axis_spread, 1058:0C9F) or turn the reading into the axis byte
 * (axis_normalise, 1058:0D09). SI is left at the second axis. */
static int vgame_stick_centre(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 1)) return 0;
    VG3_NEAR(0x0CC1, 0x0C94, 2);                                  /* read the port */
    c->r[R_SI] = (uint16_t)alu_sub(c, c->r[R_SI], c->r[R_SI], 1, 0);
    c->icount += 1;
    VG3_NEAR(0x0C9F, 0x0C99, 3);
    c->r[R_SI] = (uint16_t)alu_inc(c, c->r[R_SI], 1);
    c->r[R_SI] = (uint16_t)alu_inc(c, c->r[R_SI], 1);
    c->icount += 2;
    VG3_NEAR(0x0C9F, 0x0C9E, 1);
    c->icount += 1;
    far_ret(c);
    return 1;
}

static int vgame_stick_read(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 1)) return 0;
    VG3_NEAR(0x0CC1, 0x0CB5, 2);                                  /* read the port */
    c->r[R_SI] = (uint16_t)alu_sub(c, c->r[R_SI], c->r[R_SI], 1, 0);
    c->icount += 1;
    VG3_NEAR(0x0D09, 0x0CBA, 2);
    c->r[R_SI] = 1;
    c->icount += 1;
    VG3_NEAR(0x0D09, 0x0CC0, 1);
    c->icount += 1;
    far_ret(c);
    return 1;
}

/* VGAME 120A:04F5, model_part_light (DI = the part's inverse matrix): the
 * base-local light direction (7C9C..7CA0) copied into the next 6-byte
 * light slot [7D7C], turned into the part's frame there by the camera
 * matrix product (1452:021B) and the slot pointer moved on. BX and DI
 * are kept. */
static int vgame_model_part_light(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 10 + 1)) return 0;
    cpu_push16(c, c->r[R_BX]);
    c->r[R_BX] = ds_get(c, 0x7D7C);
    for (int k = 0; k < 3; k++) {
        c->r[R_AX] = ds_get(c, (uint16_t)(0x7C9C + 2 * k));
        ds_put(c, (uint16_t)(c->r[R_BX] + 2 * k), c->r[R_AX]);
    }
    cpu_push16(c, c->r[R_DI]);                                    /* the matrix */
    cpu_push16(c, c->r[R_BX]);                                    /* the vector */
    c->icount += 10;
    VG3_FAR(0x050D, 5);
    VG3_DROP(2);
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    ds_put(c, 0x7D7C, (uint16_t)alu_add(c, ds_get(c, 0x7D7C), 6, 1, 0));
    c->icount += 5;
    near_ret(c);
    return 1;
}

/* VGAME 120A:0CA8, model_face (BX = face): a face whose byte at 7702 is
 * above 7Fh is drawn - its colour (7802) becomes the fill colour [7D8A]
 * and its command stream (word BX of the table at ES:SI) goes to the
 * fill (120A:0CE9). DI, BX and SI are kept; AL is the colour. */
static int vgame_model_face(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 9 + 1)) return 0;
    alu_sub(c, VG3_DS8(c->r[R_BX] + 0x7702), 0x7F, 0, 0);
    if (x86_cond(c, 6)) {                                         /* jbe: hidden */
        c->icount += 3;
        near_ret(c);
        return 1;
    }
    set_r8(c, R_AL, VG3_DS8(c->r[R_BX] + 0x7802));
    VG3_SETDS8(0x7D8A, get_r8(c, R_AL));
    cpu_push16(c, c->r[R_DI]);
    cpu_push16(c, c->r[R_BX]);
    cpu_push16(c, c->r[R_SI]);
    c->r[R_BX] = x86_shift(c, 4, c->r[R_BX], 1, 1);               /* shl bx, 1 */
    c->r[R_SI] = seg_read16(c, c->seg[S_ES], (uint16_t)(c->r[R_BX] + c->r[R_SI]));
    c->icount += 9;
    VG3_NEAR(0x0CE9, 0x0CC1, 4);
    c->r[R_SI] = cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c);
    c->r[R_DI] = cpu_pop16(c);
    c->icount += 4;
    near_ret(c);
    return 1;
}

/* VGAME 120A:0C76, model_faces: the faces of a model by the count [7D5A] -
 * below zero, the faces listed as bytes at 7902 up to an FFh; zero, the
 * one stream at ES:SI straight to the fill (120A:0CE9); above zero, faces
 * 0 to count-1 - each through model_face (120A:0CA8). Every turn looks at
 * the room, and stops at its loop's head when there is none. */
static int vgame_model_faces(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 5 + 1)) return 0;
    c->r[R_CX] = ds_get(c, 0x7D5A);
    alu_logic(c, c->r[R_CX], 1);                                  /* or cx, cx */
    unsigned n = 3;                                               /* mov, or, js */
    if (c->flags & F_SF) {                                        /* a list */
        c->r[R_DI] = 0x7902;
        n += 1;
        for (;;) {                                                /* 0C98 */
            VG3_ROOM_OR_STOP(6, 0x0C98);
            set_r8(c, R_BL, VG3_DS8(c->r[R_DI]));
            c->r[R_DI]++;
            alu_sub(c, get_r8(c, R_BL), 0xFF, 0, 0);
            if (c->flags & F_ZF) { c->icount += 4 + 1; near_ret(c); return 1; }
            set_r8(c, R_BH, (uint8_t)alu_sub(c, get_r8(c, R_BH), get_r8(c, R_BH), 0, 0));
            c->icount += 5;
            VG3_NEAR(0x0CA8, 0x0CA5, 1);
            n = 1;                                                /* jmp */
        }
    }
    if (c->flags & F_ZF) {                                        /* no faces: the stream itself */
        c->r[R_CX] = (uint16_t)alu_inc(c, c->r[R_CX], 1);
        n += 2;                                                   /* jne, inc cx */
        do {                                                      /* 0C81: once, unless the count on */
            VG3_ROOM_OR_STOP(2, 0x0C81);                          /* the stack was written over */
            cpu_push16(c, c->r[R_CX]);
            c->icount += 1;
            VG3_NEAR(0x0CE9, 0x0C85, 2 + 2);
            c->r[R_CX] = (uint16_t)(cpu_pop16(c) - 1);            /* pop cx, loop */
            n = 2;
        } while (c->r[R_CX]);
        c->icount += n + 1;
        near_ret(c);
        return 1;
    }
    c->r[R_BX] = (uint16_t)alu_sub(c, c->r[R_BX], c->r[R_BX], 1, 0);
    n += 2;                                                       /* jne, sub bx */
    for (;;) {                                                    /* 0C8B */
        VG3_ROOM_OR_STOP(2, 0x0C8B);
        cpu_push16(c, c->r[R_CX]);
        c->icount += 1;
        VG3_NEAR(0x0CA8, 0x0C8F, 3 + 2);
        c->r[R_CX] = cpu_pop16(c);
        c->r[R_BX] = (uint16_t)alu_inc(c, c->r[R_BX], 1);
        c->r[R_CX]--;
        n = 3;                                                    /* pop, inc, loop */
        if (!c->r[R_CX]) break;
    }
    c->icount += n + 1;
    near_ret(c);
    return 1;
}

/* VGAME 130D:004A, model_edge_spans (SI = a clipped edge's slot), far: the
 * edge's flag word [si+2] gathers into [85E2]; bit 40h keeps the polygon
 * for clipping (130D:00EC). An edge in view (bit 80h clear) marks the
 * polygon drawn ([85EC] |= 4), goes into the span tables (mpoly_edge,
 * 1377:072B) and, where an end left the window (bits 0-1 for the first
 * end's rows at +4/+6, bits 2-3 for the second's at +0Ch/+0Eh), widens the
 * left (bit 1 or 3: 130D:00B6) or right (130D:00D1) side's rows. A culled
 * edge (bit 80h) widens one side by the rows +6/+0Eh when bits 0-1 say so.
 * AX is the flag word the side routines see. */
static int vgame_model_edge_spans(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 15)) return 0;                                  /* a culled edge's side: 5 + 3 + 2 + 4 and the CALL */
    const uint16_t si = c->r[R_SI];
    c->r[R_AX] = ds_get(c, (uint16_t)(si + 2));
    ds_put(c, 0x85E2, (uint16_t)alu_logic(c, ds_get(c, 0x85E2) | c->r[R_AX], 1));
    c->r[R_AX] = ds_get(c, (uint16_t)(si + 2));
    alu_logic(c, c->r[R_AX] & 0x40, 0);
    c->icount += 5;
    if (!(c->flags & F_ZF)) VG3_NEAR(0x00EC, 0x005B, 10);         /* keep it for clipping */
    c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_SI] + 2));
    alu_logic(c, c->r[R_AX] & 0x80, 0);
    unsigned n = 3;
    if (c->flags & F_ZF) {                                        /* in view */
        ds_put(c, 0x85EC, (uint16_t)alu_logic(c, ds_get(c, 0x85EC) | 4, 1));
        c->icount += n + 1;
        VG3_FAR(0x0067, 10);                                      /* into the span tables */
        n = 0;
        static const struct { uint8_t any, left; uint8_t rows; uint16_t call_left, call_right, next; } end[2] = {
            { 3, 2, 4, 0x0083, 0x007D, 0x0086 }, { 0x0C, 8, 0x0C, 0x009A, 0x0094, 0x009D } };
        c->r[R_AX] = ds_get(c, (uint16_t)(c->r[R_SI] + 2));
        n += 1;
        for (int k = 0; k < 2; k++) {
            alu_logic(c, get_r8(c, R_AL) & end[k].any, 0);
            n += 2;                                               /* test, je */
            if (c->flags & F_ZF) continue;
            c->r[R_BX] = ds_get(c, (uint16_t)(c->r[R_SI] + end[k].rows));
            c->r[R_CX] = ds_get(c, (uint16_t)(c->r[R_SI] + end[k].rows + 2));
            alu_logic(c, get_r8(c, R_AL) & end[k].left, 0);
            c->icount += n + 4;
            n = 0;
            if (!(c->flags & F_ZF)) {
                VG3_NEAR(0x00B6, (uint16_t)(end[k].call_left + 3), k ? 1 : 7);
            } else {
                VG3_NEAR(0x00D1, (uint16_t)(end[k].call_right + 3), k ? 2 : 8);
                n = 1;                                            /* jmp */
            }
        }
        c->icount += n + 1;
        far_ret(c);
        return 1;
    }
    alu_logic(c, get_r8(c, R_AL) & 3, 0);                         /* culled */
    n += 2;
    if (!(c->flags & F_ZF)) {
        c->r[R_BX] = ds_get(c, (uint16_t)(c->r[R_SI] + 6));
        c->r[R_CX] = ds_get(c, (uint16_t)(c->r[R_SI] + 0x0E));
        alu_logic(c, get_r8(c, R_AL) & 2, 0);
        c->icount += n + 4;
        if (!(c->flags & F_ZF)) VG3_NEAR(0x00B6, 0x00B4, 2);
        else VG3_NEAR(0x00D1, 0x00AF, 2);
        n = 1;                                                    /* jmp */
    }
    c->icount += n + 1;
    far_ret(c);
    return 1;
}

/* VGAME 130D:0116, model_poly_finish(style), far: the end of a model
 * polygon. The fill style's begin entry (1377:004C) is called; a polygon
 * kept for clipping twice ([85EE] = 2) is clipped (130D:033F on the slot
 * at 85B8) and its edge added (130D:004A). When the polygon was drawn
 * ([85EC]) its sides that left the window ([85E2] bits 0/2 right, 1/3
 * left) are closed along the window's edges (130D:0191, 130D:01A7) and it
 * is filled (1377:005E with the style in AX); a polygon wholly outside
 * is filled only when it crossed both sides. The side rows go back to
 * empty (7FFFh / 8000h) and [85EC], [85E2] and [85EE] to zero. */
static int vgame_model_poly_finish(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 4 + 1)) return 0;
    x86_push_reg(c, R_BP);
    c->r[R_BP] = c->r[R_SP];
    c->r[R_AX] = VG3_FRAME(6);
    cpu_push16(c, c->r[R_AX]);
    c->icount += 4;
    VG3_FAR(0x011D, 21);                                          /* the style's begin */
    VG3_DROP(2);
    alu_sub(c, ds_get(c, 0x85EE), 2, 1, 0);
    unsigned n = 3;
    if (c->flags & F_ZF) {                                        /* clip the kept polygon */
        cpu_push16(c, c->r[R_SI]);
        c->r[R_SI] = 0x85B8;
        c->icount += n + 2;
        VG3_FAR(0x0131, 1);
        VG3_FAR(0x0136, 19);
        c->r[R_SI] = cpu_pop16(c);
        n = 1;
    }
    c->r[R_DX] = ds_get(c, 0x85EC);                               /* 013C */
    alu_logic(c, c->r[R_DX], 1);
    n += 3;
    int fill = 1;
    if (!(c->flags & F_ZF)) {                                     /* drawn: close each side that left */
        c->r[R_AX] = ds_get(c, 0x85E2);
        alu_logic(c, get_r8(c, R_AL) & 5, 0);
        n += 3;
        if (!(c->flags & F_ZF)) {
            c->icount += n;
            VG3_NEAR(0x0191, 0x014E, 5);
            n = 0;
        }
        c->r[R_AX] = ds_get(c, 0x85E2);
        alu_logic(c, get_r8(c, R_AL) & 0x0A, 0);
        n += 3;
        if (!(c->flags & F_ZF)) {
            c->icount += n;
            VG3_NEAR(0x01A7, 0x0158, 2);
            n = 0;
        }
    } else {                                                      /* 017E: outside; both sides or nothing */
        c->r[R_AX] = ds_get(c, 0x85E2);
        alu_logic(c, get_r8(c, R_AL) & 5, 0);
        n += 3;
        if (!(c->flags & F_ZF)) {
            alu_logic(c, get_r8(c, R_AL) & 0x0A, 0);
            n += 2;
        }
        if (c->flags & F_ZF) fill = 0;
        else {
            c->icount += n;
            VG3_NEAR(0x0191, 0x018C, 1);
            VG3_NEAR(0x01A7, 0x018F, 3);
            n = 1;                                                /* jmp */
        }
    }
    if (fill) {                                                   /* 0158 */
        c->r[R_AX] = VG3_FRAME(6);
        c->icount += n + 1;
        VG3_FAR(0x015B, 12);                                      /* fill it */
        n = 0;
    }
    c->r[R_AX] = 0x7FFF;                                          /* 0160: empty the sides */
    ds_put(c, 0x85E8, 0x7FFF);
    ds_put(c, 0x85E4, 0x7FFF);
    c->r[R_AX] = 0x8000;                                          /* not ax */
    ds_put(c, 0x85EA, 0x8000);
    ds_put(c, 0x85E6, 0x8000);
    c->r[R_AX] = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    ds_put(c, 0x85EC, 0);
    ds_put(c, 0x85E2, 0);
    ds_put(c, 0x85EE, 0);
    c->r[R_BP] = cpu_pop16(c);
    c->icount += n + 12;
    far_ret(c);
    return 1;
}

/* VGAME 1377:00F3, row_offsets_planar: the row-offset table at DS:861C
 * for the planar modes, 200 rows of 40 bytes - or, when [916E] is set (the
 * 640-wide mode, from 1377:00C2), 480 rows of 80 bytes, with the span
 * limits [9172] = 79 and [9174] = -81 for 80-byte rows. ES = DS; the
 * words are stored by STOSW (so by the direction flag); the style table
 * [85F2] = 70h. */
static int vgame_row_offsets_planar(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const int wide = ds_get(c, 0x916E) != 0;
    const unsigned rows = wide ? 480 : 200, step = wide ? 0x50 : 0x28;
    const unsigned total = 2 + 5 + 3 * rows + (wide ? 12 : 3);
    if (!room(c, total)) return 0;
    alu_sub(c, ds_get(c, 0x916E), 0, 1, 0);
    c->r[R_CX] = (uint16_t)rows;
    c->r[R_AX] = c->seg[S_DS];
    c->seg[S_ES] = c->seg[S_DS];
    c->r[R_DI] = 0x861C;
    uint16_t ax = (uint16_t)alu_sub(c, c->r[R_AX], c->r[R_AX], 1, 0);
    for (unsigned r = 0; r < rows; r++) {                         /* stosw; add ax, step; loop */
        seg_write16(c, c->seg[S_ES], c->r[R_DI], ax);
        c->r[R_DI] = (uint16_t)(c->r[R_DI] + ((c->flags & F_DF) ? -2 : 2));
        ax = (uint16_t)alu_add(c, ax, step, 1, 0);
    }
    c->r[R_CX] = 0;
    c->r[R_BX] = 0x70;
    ds_put(c, 0x85F2, 0x70);
    if (wide) {
        ax = x86_shift(c, 4, 0x28, 1, 1);                         /* mov ax, 28h; shl ax, 1 */
        c->r[R_BX] = ax;
        ax = (uint16_t)alu_sub(c, ax, 1, 1, 0);
        ds_put(c, 0x9172, ax);
        ax = (uint16_t)alu_inc(c, c->r[R_BX], 1);
        ax = (uint16_t)alu_sub(c, 0, ax, 1, 0);                   /* neg ax */
        ds_put(c, 0x9174, ax);
    }
    c->r[R_AX] = ax;
    c->icount += total;
    near_ret(c);
    return 1;
}

/* VGAME 1377:046F, model_line (SI = the edge's slot: x0 +0, y0 +4, x1 +8,
 * y1 +0Ch): a line in the colour [8606] on the page ES = [861A], rows from
 * the offset table at 861C. A shallow line runs along x from the left end,
 * one byte a column (STOSB, so by the direction flag), stepping a row
 * (+-140h) when the error crosses zero; a steep one runs down y from the
 * top end, a row a step, stepping a column. BP is left as the minor step;
 * SI and DI are kept. Each pixel looks at the room and stops at its loop's
 * head when there is none. */
static int vgame_model_line(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 22 + 19)) return 0;
    cpu_push16(c, c->r[R_SI]);
    cpu_push16(c, c->r[R_DI]);
    c->r[R_AX] = ds_get(c, 0x861A);
    c->seg[S_ES] = c->r[R_AX];
    const uint16_t s = c->r[R_SI];
    uint16_t y1 = ds_get(c, (uint16_t)(s + 0x0C)), y0 = ds_get(c, (uint16_t)(s + 4));
    uint16_t x1 = ds_get(c, (uint16_t)(s + 8)), x0 = ds_get(c, s);
    /* |y1 - y0| and |x1 - x0| by CWD, XOR, SUB */
    uint16_t a = (uint16_t)alu_sub(c, y1, y0, 1, 0);
    uint16_t sx = (a & 0x8000) ? 0xFFFF : 0;
    a = (uint16_t)alu_logic(c, a ^ sx, 1);
    const uint16_t ady = (uint16_t)alu_sub(c, a, sx, 1, 0);
    a = (uint16_t)alu_sub(c, x1, x0, 1, 0);
    sx = (a & 0x8000) ? 0xFFFF : 0;
    a = (uint16_t)alu_logic(c, a ^ sx, 1);
    const uint16_t adx = (uint16_t)alu_sub(c, a, sx, 1, 0);
    alu_sub(c, ady, adx, 1, 0);
    unsigned n = 22;
    uint16_t minor, major;                                        /* the error's two steps (SI, DX) */
    uint16_t cnt, row, col, step;
    int steep = x86_cond(c, 7);                                   /* ja */
    if (!steep) {
        minor = ady; major = adx;
        alu_sub(c, x1, x0, 1, 0);
        n += 2;
        if (!x86_cond(c, 0xD)) {                                  /* jge not taken: from the left end */
            uint16_t t = x0; x0 = x1; x1 = t;
            t = y0; y0 = y1; y1 = t;
            n += 2;
        }
        step = 0x140;
        alu_sub(c, y1, y0, 1, 0);
        n += 3;
        if (!x86_cond(c, 0xD)) { step = (uint16_t)alu_sub(c, 0, step, 1, 0); n++; }
        row = y0; col = x0;
        cnt = (uint16_t)alu_inc(c, (uint16_t)alu_sub(c, x1, x0, 1, 0), 1);
    } else {
        minor = adx; major = ady;                                 /* xchg si, dx */
        alu_sub(c, y1, y0, 1, 0);
        n += 3;
        if (!x86_cond(c, 0xD)) {                                  /* from the top end */
            uint16_t t = x0; x0 = x1; x1 = t;
            t = y0; y0 = y1; y1 = t;
            n += 2;
        }
        step = 1;
        alu_sub(c, x1, x0, 1, 0);
        n += 3;
        if (!x86_cond(c, 0xD)) { step = (uint16_t)alu_sub(c, 0, step, 1, 0); n++; }
        row = y0; col = x0;
        cnt = (uint16_t)alu_inc(c, (uint16_t)alu_sub(c, y1, y0, 1, 0), 1);
    }
    c->r[R_SI] = minor;
    c->r[R_DX] = major;
    c->r[R_BP] = step;
    c->r[R_DI] = (uint16_t)(ds_get(c, (uint16_t)(x86_shift(c, 4, row, 1, 1) + 0x861C)) + col);
    c->r[R_CX] = cnt;
    uint16_t err = x86_shift(c, 5, cnt, 1, 1);                    /* shr bx, 1 */
    c->r[R_BX] = (uint16_t)alu_sub(c, 0, err, 1, 0);              /* neg bx */
    c->r[R_AX] = ds_get(c, 0x8606);
    n += steep ? 10 : 11;
    const uint16_t head = steep ? 0x0500 : 0x04C4;
    for (;;) {
        VG3_ROOM_OR_STOP(7 + 4, head);
        const uint8_t colour = get_r8(c, R_AL);
        mem_write8(c, phys(c->seg[S_ES], c->r[R_DI]), colour);
        if (steep) {
            c->r[R_DI] = (uint16_t)alu_add(c, c->r[R_DI], 0x140, 1, 0);
            n += 1;
        } else {
            c->r[R_DI] = (uint16_t)(c->r[R_DI] + ((c->flags & F_DF) ? -1 : 1));
        }
        c->r[R_BX] = (uint16_t)alu_add(c, c->r[R_BX], c->r[R_SI], 1, 0);
        n += 3;                                                   /* store, add, jns */
        const int stepped = !(c->flags & F_SF);
        if (stepped) {                                            /* the minor step */
            c->r[R_DI] = (uint16_t)alu_add(c, c->r[R_DI], c->r[R_BP], 1, 0);
            c->r[R_BX] = (uint16_t)alu_sub(c, c->r[R_BX], c->r[R_DX], 1, 0);
            n += 2;
        }
        c->r[R_CX]--;
        n += 1;                                                   /* loop */
        if (!c->r[R_CX]) {
            if (!stepped || steep) n += 1;                        /* jmp to the end */
            break;
        }
    }
    c->r[R_DI] = cpu_pop16(c);
    c->r[R_SI] = cpu_pop16(c);
    c->icount += n + 3;
    near_ret(c);
    return 1;
}

/* VGAME 1377:0A89, model_fill_or (AL = colour): the polygon's spans ORed
 * into the page at ES, row by row from [9160] while a row's left end
 * (table at 89DC) is not 7FFFh. Each span is clipped to the window's
 * columns [85FA]..[85FE] (one wholly outside is skipped), its odd first
 * byte ORed alone, then whole words (AH = AL), then an odd last byte; the
 * row's ends go back to empty (7FFFh, 8001h). [9160] ends as 7FFFh. Rows
 * and words look at the room and stop at their loop's head. */
static int vgame_model_fill_or(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!room(c, 6)) return 0;
    set_r8(c, R_AH, get_r8(c, R_AL));
    c->r[R_SI] = ds_get(c, 0x9160);
    alu_sub(c, c->r[R_SI], 0x7FFF, 1, 0);
    unsigned n = 4;                                               /* mov, mov, cmp, jne */
    if (!(c->flags & F_ZF)) {
        c->r[R_SI] = x86_shift(c, 4, c->r[R_SI], 1, 1);
        n += 1;
        for (;;) {                                                /* 0A9E: a row */
            VG3_ROOM_OR_STOP(34, 0x0A9E);
            const uint16_t si = c->r[R_SI];
            c->r[R_BX] = ds_get(c, (uint16_t)(si + 0x89DC));
            c->r[R_CX] = ds_get(c, (uint16_t)(si + 0x8D9E));
            alu_sub(c, c->r[R_BX], 0x7FFF, 1, 0);
            n += 4;
            if (c->flags & F_ZF) break;                           /* the last row */
            alu_sub(c, c->r[R_BX], ds_get(c, 0x85FE), 1, 0);
            n += 2;
            int inside = !x86_cond(c, 0xF);                       /* jg: right of the window */
            if (inside) {
                alu_sub(c, c->r[R_CX], ds_get(c, 0x85FA), 1, 0);
                n += 2;
                inside = !x86_cond(c, 0xC);                       /* jl: left of it */
            }
            if (inside) {
                alu_sub(c, c->r[R_BX], ds_get(c, 0x85FA), 1, 0);
                n += 2;
                if (!x86_cond(c, 0xD)) { c->r[R_BX] = ds_get(c, 0x85FA); n++; }
                alu_sub(c, c->r[R_CX], ds_get(c, 0x85FE), 1, 0);
                n += 2;
                if (!x86_cond(c, 0xE)) { c->r[R_CX] = ds_get(c, 0x85FE); n++; }
                c->r[R_CX] = (uint16_t)alu_inc(c, (uint16_t)alu_sub(c, c->r[R_CX], c->r[R_BX], 1, 0), 1);
                c->r[R_DI] = (uint16_t)alu_add(c, ds_get(c, (uint16_t)(si + 0x861C)), c->r[R_BX], 1, 0);
                alu_logic(c, c->r[R_DI] & 1, 1);
                n += 6;
                int more = 1;
                if (!(c->flags & F_ZF)) {                         /* an odd first byte */
                    const uint32_t at = phys(c->seg[S_ES], c->r[R_DI]);
                    mem_write8(c, at, (uint8_t)alu_logic(c, mem_read8(c, at) | get_r8(c, R_AL), 0));
                    c->r[R_DI] = (uint16_t)alu_inc(c, c->r[R_DI], 1);
                    c->r[R_CX] = (uint16_t)alu_dec(c, c->r[R_CX], 1);
                    n += 4;
                    more = !(c->flags & F_ZF);
                }
                if (more) {
                    c->r[R_BX] = c->r[R_CX];
                    c->r[R_CX] = x86_shift(c, 5, c->r[R_CX], 1, 1);   /* shr cx, 1 */
                    n += 3;
                    if (!(c->flags & F_ZF)) {
                        do {                                      /* 0AE8: the words */
                            VG3_ROOM_OR_STOP(3 + 7, 0x0AE8);
                            const uint16_t w = seg_read16(c, c->seg[S_ES], c->r[R_DI]);
                            seg_write16(c, c->seg[S_ES], c->r[R_DI], (uint16_t)alu_logic(c, w | c->r[R_AX], 1));
                            c->r[R_DI] = (uint16_t)alu_add(c, c->r[R_DI], 2, 1, 0);
                            c->r[R_CX]--;
                            n += 3;
                        } while (c->r[R_CX]);
                    }
                    c->r[R_BX] = (uint16_t)alu_logic(c, c->r[R_BX] & 1, 1);
                    n += 2;
                    if (!(c->flags & F_ZF)) {                     /* an odd last byte */
                        const uint32_t at = phys(c->seg[S_ES], c->r[R_DI]);
                        mem_write8(c, at, (uint8_t)alu_logic(c, mem_read8(c, at) | get_r8(c, R_AL), 0));
                        n++;
                    }
                }
            }
            ds_put(c, (uint16_t)(si + 0x89DC), 0x7FFF);           /* 0AF8: the row is empty again */
            ds_put(c, (uint16_t)(si + 0x8D9E), 0x8001);
            c->r[R_SI] = (uint16_t)alu_add(c, si, 2, 1, 0);
            n += 4;                                               /* mov, mov, add, jmp */
        }
    }
    ds_put(c, 0x9160, 0x7FFF);                                    /* 0A95 */
    c->icount += n + 2;
    near_ret(c);
    return 1;
}
/* VG3-END */

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
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xE3BF, vgame_camera_transform, "camera transform and cull", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x114A, 0x04B4, vgame_mat3_mul, "3x3 matrix product", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xDFA9, vgame_matrix_build, "rotation matrix from three angles", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xE123, vgame_orientation_build, "orientation matrix from the angles", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xC702, vgame_bearing, "bearing of a vector", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x2ED9, vgame_asin, "arcsine by table", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x2CB9, vgame_angles_from_matrix, "attitude angles from the matrix", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xB5DC, vgame_camera_effect_point, "effect point on the screen", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x0F97, vgame_vertex_cache, "model vertices into camera space", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x130D, 0x02DD, vgame_mclip_outcode, "model clip outcode", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x130D, 0x05E5, vgame_mc32_publish, "publish a 32-bit clipped edge", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x130D, 0x0271, vgame_mclip_point, "clip an edge end to the window", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x130D, 0x04FB, vgame_mc32_bisect_both, "bisect a 32-bit edge to the window", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1377, 0x07E8, vgame_mpoly_run, "vertical run of the model polygon", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1377, 0x072B, vgame_mpoly_edge, "model polygon edge into the spans", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x4A42, vgame_flight_end, "end the flight", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x56BB, vgame_ai_alert_area, "alert area from the aircraft", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x4F2B, vgame_approach_cue, "glide-slope cue", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xD441, vgame_frame_rates, "rates from the frame time", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xD4A5, vgame_detail_table, "detail ranges", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x7243, vgame_seeker, "weapon seeker sees a point", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x0FBD, vgame_scene_override, "scene override lookup", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x130D, 0x0577, vgame_mc32_bisect_one, "bisect a 32-bit edge onto the window", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x0810, vgame_scale_by_level, "scale by detail level", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x0F3D, vgame_scene_set_override, "set a scene override", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x4E0B, vgame_mission_field, "move one mission field", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x4D5D, vgame_mission_transfer, "move the mission record", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x4BEC, vgame_mission_load, "load the mission", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x8ED6, vgame_navgrid_project, "point on the navigation display", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x0008, vgame_model_matrix, "model renderer matrix", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x5582, vgame_detect_evaluate, "sensor detection strength", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xB850, vgame_scene_hit_lookup, "scene object under a map point", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xB991, vgame_camimage_target_model, "camera image model of a target", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x114A, 0x0461, vgame_camimage_scale, "camera image scale by kind", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x0871, vgame_scene_cell_index, "terrain cell index by level", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x93B2, vgame_clock_field, "clock field text", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x92CD, vgame_nav_time, "navigation clock text", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x9335, vgame_panel_deadline, "deadline clock text", 1 },
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
    { "matched", "START.EXE", START_47304, 0x0000, 0x3567, start_draw_text, "draw a string in a window", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x373C, start_text_width, "width of a string in a window's font", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x2A84, start_draw_text_centred, "draw a string centred on x", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x3693, start_draw_sprite, "draw a sprite in a window", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x3989, start_restore_backdrop, "restore the pointer's backdrop", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x2AB4, start_dismiss_modal, "put back the backdrop under a dialog", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x3588, start_publish_text, "publish the last text draw", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x36C4, start_draw_text_shadowed, "draw a string with a shadow", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x60DC, start_draw_theatre_name, "draw the theatre's name", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x2DF2, start_draw_ok_button, "draw a labelled button", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x0D65, start_draw_station_weapon, "draw a weapon station and its weapon", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x2CB4, start_draw_box_frame, "draw a dialog's frame", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x8A32, start_fill_rect, "fill a rectangle by spans", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x0E96, start_draw_load_caption, "draw the fuel figure", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x188D, end_draw_text, "draw a string in a window", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x1A3B, end_text_width, "width of a string in a window's font", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x10FA, end_draw_text_centred, "draw a string centred on x", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x1992, end_draw_sprite, "draw a sprite in a window", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x19C3, end_draw_text_shadowed, "draw a string with a shadow", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x112A, end_panel_text_line, "draw a centred string with a shadow", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x1332, end_draw_box_frame, "draw a dialog's frame", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x49D2, end_fill_rect, "fill a rectangle by spans", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x24F7, start_draw_target_block, "draw a briefing target block", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x3776, start_draw_wrapped, "draw word-wrapped text", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x1A75, end_draw_wrapped, "draw word-wrapped text", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x8378, start_set_dac, "write colours to the DAC in the retrace", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x42E4, end_set_dac, "write colours to the DAC in the retrace", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x2B1B, start_draw_popup, "draw a text box", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x332E, start_open_modal, "lay out and draw a dialog", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x1AB6, start_draw_troops, "draw the enemy troops overlay", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x1B37, start_draw_map_caption, "draw the briefing map's caption", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x31FA, start_draw_route_label, "place a route label", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x2901, start_cel_step, "step the transfer request animation", 1 },
    { "matched", "START.EXE", START_47304, 0x0000, 0x0BD4, start_draw_stores_panel, "draw the stores description panel", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x39C0, vgame_cockpit_number, "gauge number text", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x45E0, vgame_countermeasure_gauge, "countermeasure count gauge", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x4B03, vgame_cockpit_target_name, "describe a target", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x4B8F, vgame_objective_place, "place of an objective", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x4FD8, vgame_frame_lamp_timers, "timed lamps", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x761A, vgame_aircraft_damage, "damage the aircraft's systems", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x83E9, vgame_keys_display_refresh, "refresh a display page", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x890E, vgame_display_line, "text line on a side display", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x927F, vgame_nav_bar, "fuel bar on the navigation display", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xB577, vgame_recon_range_text, "recon range text", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x792E, vgame_ground_impact_eligible, "ground impact may destroy the object", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x7594, vgame_frame_objective_mark, "mark an objective done", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x8625, vgame_map_plot, "plot a marker on the map", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x9216, vgame_map_overlay_route, "route on the navigation display", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x8719, vgame_map_overlay_arc, "warning arc on the map", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x971A, vgame_panel_ils, "instrument landing needles", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xB4F5, vgame_panel_marker_label, "label of a projected marker", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xD9A2, vgame_compose_camera, "the frame's camera matrices", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x889B, vgame_cockpit_lamp, "set a cockpit lamp", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xBC5F, vgame_camimage_place_near_effect, "camera view: model placed near the eye", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1039, 0x009D, vgame_palette_206a, "set 8 colours from the table at 206A", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1039, 0x00B7, vgame_palette_1212, "set 8 colours from the table at 1212", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1039, 0x00D1, vgame_palette_d4a_read, "palette call, two-argument form", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1039, 0x00E8, vgame_palette_d4a, "set 256 colours from the table at 0D4A", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1039, 0x0101, vgame_palette_134a, "set 256 colours from the table at 134A", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1452, 0x0288, vgame_matrix_rows3, "three matrix rows", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1058, 0x0D8C, vgame_copy_settings, "copy the 20-word settings block", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x11ED, 0x0078, vgame_lzw_table_reset, "reset the picture decoder's string table", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x78FD, vgame_scene_obstacle_probe, "probe an obstacle in the world", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1039, 0x011A, vgame_scene_detail_level, "detail level of the view", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x04C9, vgame_model_vertex_camera, "a vertex through the camera", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x4E5F, vgame_frame_palette_cycle, "day and night palette step", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xC5B2, vgame_camimage_near_effects, "camera view: near-object markers", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xC4BA, vgame_camimage_aircraft_glint, "camera view: aircraft glint", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x043C, vgame_model_vertex_world, "a model vertex into world space", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xE2B6, vgame_compose_emit_object, "place an object in the scene", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xDB3C, vgame_scene_defer_object, "defer an object in distance order", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x130D, 0x0191, vgame_poly_edge_right, "close a polygon on the window's right", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x130D, 0x01A7, vgame_poly_edge_left, "close a polygon on the window's left", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0x8342, vgame_view_caption, "the view's direction caption", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xB13C, vgame_camera_view_caption, "the camera's direction caption", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0FB2, 0x0324, vgame_sky_bands_far, "far entry of the sky bands", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0FB2, 0x069A, vgame_boxed_line_far, "far entry of a boxed line", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1058, 0x0C91, vgame_stick_centre, "centre the joystick calibration", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1058, 0x0CB2, vgame_stick_read, "read both joystick axes", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x04F5, vgame_model_part_light, "the light in a part's frame", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x0CA8, vgame_model_face, "draw a model face", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x120A, 0x0C76, vgame_model_faces, "draw a model's faces", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x130D, 0x004A, vgame_model_edge_spans, "a clipped edge into the polygon", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x130D, 0x0116, vgame_model_poly_finish, "finish and fill a model polygon", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1377, 0x00F3, vgame_row_offsets_planar, "row offsets for the planar modes", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1377, 0x046F, vgame_model_line, "draw a model line", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x1377, 0x0A89, vgame_model_fill_or, "OR the polygon spans into the page", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x0B20, dswap_lzw_reset, "reset the LZW table", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x1F1C, player_format_digits, "the formatter's digits", 1 },
    { "matched", "MPS_LOGO.EXE", MPS_LOGO_47304, 0x0146, 0x14BC, mps_logo_free_stream, "first free stream", 2 },
    { "matched", "MPS_LOGO.EXE", MPS_LOGO_47304, 0x0146, 0x0B66, mps_logo_class_lookup, "map a character class", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x0F06, dswap_open_stream, "open a stream", 1 },
    { "matched", "MPS_LOGO.EXE", MPS_LOGO_47304, 0x0146, 0x1A3F, mps_logo_mask_test, "masked sign test", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x1F11, dswap_mask_test, "masked sign test", 1 },
    { "matched", "SETUP.EXE", SETUP_47304, 0x0000, 0x23AA, setup_axis_spread, "spread an axis value", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x1B9C, dswap_free_stream, "first free stream", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x138A, dswap_string_lookup, "look up a string by id", 1 },
    { "matched", "MPS_LOGO.EXE", MPS_LOGO_47304, 0x0146, 0x0F80, mps_logo_flush_all_one, "flush all, mode 1", 2 },
    { "matched", "MPS_LOGO.EXE", MPS_LOGO_47304, 0x0146, 0x1D66, mps_logo_find_in_table, "find a byte in the six-byte table", 2 },
    { "matched", "MPS_LOGO.EXE", MPS_LOGO_47304, 0x0146, 0x0756, mps_logo_ldiv, "32-bit signed divide", 2 },
    { "matched", "SETUP.EXE", SETUP_47304, 0x0000, 0x2414, setup_axis_normalise, "normalise a joystick axis", 1 },
    { "matched", "MPS_LOGO.EXE", MPS_LOGO_47304, 0x0146, 0x05DE, mps_logo_strcat, "string concatenate", 2 },
    { "matched", "MPS_LOGO.EXE", MPS_LOGO_47304, 0x0146, 0x061E, mps_logo_strcpy, "string copy", 2 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x1006, player_unpack, "unpack a compressed picture", 1 },
    { "matched", "PLAYER.EXE", PLAYER_47304, 0x0000, 0x1790, player_setenvp, "copy the environment", 1 },
    { "matched", "DSWAP.EXE", DSWAP_47304, 0x0000, 0x130C, dswap_setenvp, "copy the environment", 1 },
    { "matched", "SETUP.EXE", SETUP_47304, 0x0000, 0x1B44, setup_setenvp, "copy the environment", 1 },
    { "matched", "MPS_LOGO.EXE", MPS_LOGO_47304, 0x0146, 0x0A5E, mps_logo_setenvp, "copy the environment", 2 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x11ED, 0x00AE, vgame_pic_rle, "the picture decoder's row (RLE) step", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x11ED, 0x0127, vgame_pic_lzw, "the picture decoder's code and table step", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x48AA, end_pic_rle, "the picture decoder's row (RLE) step", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x4923, end_pic_lzw, "the picture decoder's code and table step", 1 },
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
