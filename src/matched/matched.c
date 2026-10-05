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
#define START_47304 0xC65ECC83823E4907ULL
#define END_47304   0xFA7167EE4E377EC1ULL
#define PLAYER_47304 0xF61A7BE2C4607B24ULL
#define DSWAP_47304 0xF947E1BD62AA2812ULL
#define SETUP_47304 0xEDD5021CF28E7FC4ULL

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
    uint16_t si = seg_read16(c, ds, (uint16_t)(bx + 8));
    unsigned clocks = 4;
    if (si == seg_read16(c, ds, (uint16_t)(bx + 0x0A))) { si = seg_read16(c, ds, (uint16_t)(bx + 6)); clocks++; }
    unsigned steps = 0;
    for (;;) {
        const uint16_t w = seg_read16(c, ds, si);
        si = (uint16_t)(si + (up ? 2 : -2));
        clocks += 3;
        if (w == 0xFFFE) break;
        si = (uint16_t)(si + (w & 0xFFFE));
        clocks += 4;
        if (++steps > 0x4000) return 0;
    }
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
    { "matched", "START.EXE", START_47304, 0x0000, 0x826C, start_palette_bank, "copy a palette bank", 1 },
    { "matched", "VGAME.EXE", VGAME_47304, 0x0000, 0xEB10, strcat_ds, "string concatenate", 1 },
    { "matched", "END.EXE", END_47304, 0x0000, 0x5110, strcat_ds, "string concatenate", 1 },
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
