/* cpu.c - 8086/80186/80286 real-mode interpreter.
 *
 * The decode loop of the Reimp oracle (tools/x86oracle/cpu.c at cfb8cec9)
 * with every instruction's semantics moved into x86_sem.h, which the
 * recompiled code calls too. This file is the reference engine: whatever the
 * recompiled code does, it must do what cpu_step does.
 *
 * Correctness targets, in priority order:
 *   1. Flag semantics, including the quirks each CPU model has.
 *   2. Exact arithmetic, including divide-error behaviour.
 *   3. Speed, a distant third.
 */
#include "x86_sem.h"
#include "timing386.h"

#include <string.h>

const uint8_t x86_parity_tab[256] = {
    1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1, 0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
    0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
    0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
    1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1, 0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
    0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
    1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1, 0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
    1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1, 0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
    0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0, 1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1
};

/* ===================================================================== */
/* Out-of-line semantics                                                 */
/* ===================================================================== */

uint16_t x86_shift(cpu_t *c, int op, uint16_t val, uint8_t cnt, int w16)
{
    uint32_t bits = w16 ? 16u : 8u;
    uint32_t mask = w16 ? 0xFFFFu : 0xFFu;
    uint32_t sign = w16 ? 0x8000u : 0x80u;
    uint32_t v = val & mask;

    /* The 80186 and later mask the count to five bits and make group-2 /6 a
     * plain SHL; the 8086 does neither (see SETMO below). */
    if (c->model != CPU_8086) {
        cnt &= 0x1F;
        if (op == 6) op = 4;
    }

    if (cnt == 0) return (uint16_t)v;   /* no flags touched at all */

    switch (op) {
    case 0: {   /* ROL */
        uint32_t n = cnt % bits;
        if (n) v = ((v << n) | (v >> (bits - n))) & mask;
        set_flag(c, F_CF, v & 1u);
        if (cnt == 1) set_flag(c, F_OF, ((v & sign) != 0) ^ ((v & 1u) != 0));
        return (uint16_t)v;
    }
    case 1: {   /* ROR */
        uint32_t n = cnt % bits;
        if (n) v = ((v >> n) | (v << (bits - n))) & mask;
        set_flag(c, F_CF, (v & sign) != 0);
        if (cnt == 1)
            set_flag(c, F_OF, ((v & sign) != 0) ^ ((v & (sign >> 1)) != 0));
        return (uint16_t)v;
    }
    case 2: {   /* RCL */
        uint32_t n = cnt % (bits + 1);
        for (uint32_t i = 0; i < n; i++) {
            uint32_t nc = (v & sign) != 0;
            v = ((v << 1) | ((c->flags & F_CF) ? 1u : 0u)) & mask;
            set_flag(c, F_CF, nc);
        }
        if (cnt == 1)
            set_flag(c, F_OF, ((v & sign) != 0) ^ ((c->flags & F_CF) != 0));
        return (uint16_t)v;
    }
    case 3: {   /* RCR */
        uint32_t n = cnt % (bits + 1);
        if (cnt == 1)
            set_flag(c, F_OF, ((v & sign) != 0) ^ ((c->flags & F_CF) != 0));
        for (uint32_t i = 0; i < n; i++) {
            uint32_t nc = v & 1u;
            v = (v >> 1) | ((c->flags & F_CF) ? sign : 0u);
            set_flag(c, F_CF, nc);
        }
        return (uint16_t)v;
    }
    case 4: {   /* SHL / SAL */
        uint32_t last = 0;
        for (uint32_t i = 0; i < cnt; i++) {
            last = (v & sign) != 0;
            v = (v << 1) & mask;
        }
        set_flag(c, F_CF, (cnt <= bits) ? last : 0);
        set_flag(c, F_OF, ((v & sign) != 0) ^ ((c->flags & F_CF) != 0));
        set_flag(c, F_AF, 0);
        flags_szp(c, v, w16);
        return (uint16_t)v;
    }
    case 6: {   /* SETMO / SETMOC - undocumented, 8086/8088 only: sets the
                 * operand to all ones. A zero CL count does nothing, which
                 * the cnt == 0 early-out above already handles. */
        v = mask;
        set_flag(c, F_CF, 0);
        set_flag(c, F_OF, 0);
        set_flag(c, F_AF, 0);
        flags_szp(c, v, w16);
        return (uint16_t)v;
    }
    case 5: {   /* SHR */
        uint32_t last = 0, orig = v;
        for (uint32_t i = 0; i < cnt; i++) { last = v & 1u; v >>= 1; }
        set_flag(c, F_CF, (cnt <= bits) ? last : 0);
        set_flag(c, F_OF, (cnt == 1) ? ((orig & sign) != 0) : 0);
        set_flag(c, F_AF, 0);
        flags_szp(c, v, w16);
        return (uint16_t)v;
    }
    default: {  /* SAR */
        uint32_t last = 0;
        for (uint32_t i = 0; i < cnt; i++) {
            last = v & 1u;
            v = (v >> 1) | (v & sign);
        }
        set_flag(c, F_CF, last);
        set_flag(c, F_OF, 0);
        set_flag(c, F_AF, 0);
        flags_szp(c, v, w16);
        return (uint16_t)v;
    }
    }
}

void cpu_hw_interrupt(cpu_t *c, uint8_t vec)
{
    cpu_push16(c, c->flags);
    cpu_push16(c, c->seg[S_CS]);
    cpu_push16(c, c->ip);
    c->flags = (uint16_t)(c->flags & (uint16_t)(0xFFFFu ^ (F_IF | F_TF)));
    uint32_t v = (uint32_t)vec * 4u;
    c->ip = mem_read16(c, v);
    c->seg[S_CS] = mem_read16(c, v + 2);
    c->int_depth++;
}

void cpu_interrupt(cpu_t *c, uint8_t vec)
{
    if (c->int_hook && c->int_hook(c, vec))
        return;                       /* the host emulated it */
    cpu_hw_interrupt(c, vec);
}

void x86_divide_error(cpu_t *c)
{
    c->t386_fault = 1;
    if (c->model >= CPU_80286) { c->ip = c->op_ip; c->seg[S_CS] = c->op_cs; }
    cpu_interrupt(c, 0);
}

void x86_daa(cpu_t *c)
{
    /* The high correction's threshold was measured against real 8088
     * silicon (SingleStepTests vectors): it is 0x99 as Intel documents,
     * but 0x9F when AF was already set on entry. The 80286 uses 0x99 either
     * way. */
    uint8_t al = get_r8(c, R_AL);
    uint8_t old_al = al;
    int old_cf = (c->flags & F_CF) != 0;
    int old_af = (c->flags & F_AF) != 0;
    int cf = old_cf;
    if ((al & 0x0F) > 9 || old_af) {
        cf = old_cf || ((unsigned)al + 6u > 0xFFu);
        al = (uint8_t)(al + 6);
        set_flag(c, F_AF, 1);
    } else {
        set_flag(c, F_AF, 0);
    }
    if (old_cf || old_al > ((c->model == CPU_8086 && old_af) ? 0x9F : 0x99)) {
        al = (uint8_t)(al + 0x60);
        cf = 1;
    }
    set_flag(c, F_CF, cf);
    set_r8(c, R_AL, al);
    flags_szp(c, al, 0);
}

void x86_das(cpu_t *c)
{
    /* Same measured threshold as DAA. A second measured divergence from the
     * manual: on the 8088 the low-nibble borrow does NOT propagate into CF;
     * on the 80286 it does. */
    uint8_t al = get_r8(c, R_AL);
    uint8_t old_al = al;
    int old_cf = (c->flags & F_CF) != 0;
    int old_af = (c->flags & F_AF) != 0;
    int cf = old_cf;
    if ((al & 0x0F) > 9 || old_af) {
        if (c->model >= CPU_80286 && (al < 6 || old_cf))
            cf = 1;
        al = (uint8_t)(al - 6);
        set_flag(c, F_AF, 1);
    } else {
        set_flag(c, F_AF, 0);
    }
    if (old_cf || old_al > ((c->model == CPU_8086 && old_af) ? 0x9F : 0x99)) {
        al = (uint8_t)(al - 0x60);
        cf = 1;
    }
    set_flag(c, F_CF, cf);
    set_r8(c, R_AL, al);
    flags_szp(c, al, 0);
}

void x86_aaa(cpu_t *c)
{
    uint8_t al = get_r8(c, R_AL);
    if ((al & 0x0F) > 9 || (c->flags & F_AF)) {
        if (c->model >= CPU_80286) {
            /* a 16-bit add: AL=FC carries into AH, so AH rises by TWO */
            c->r[0] = (uint16_t)(c->r[0] + 0x106);
            set_r8(c, R_AL, (uint8_t)(get_r8(c, R_AL) & 0x0F));
        } else {
            set_r8(c, R_AL, (uint8_t)((al + 6) & 0x0F));
            set_r8(c, R_AH, (uint8_t)(get_r8(c, R_AH) + 1));
        }
        set_flag(c, F_AF, 1); set_flag(c, F_CF, 1);
    } else {
        set_r8(c, R_AL, (uint8_t)(al & 0x0F));
        set_flag(c, F_AF, 0); set_flag(c, F_CF, 0);
    }
}

void x86_aas(cpu_t *c)
{
    uint8_t al = get_r8(c, R_AL);
    if ((al & 0x0F) > 9 || (c->flags & F_AF)) {
        if (c->model >= CPU_80286) {
            c->r[0] = (uint16_t)(c->r[0] - 0x106);       /* the 16-bit subtract */
            set_r8(c, R_AL, (uint8_t)(get_r8(c, R_AL) & 0x0F));
        } else {
            set_r8(c, R_AL, (uint8_t)((al - 6) & 0x0F));
            set_r8(c, R_AH, (uint8_t)(get_r8(c, R_AH) - 1));
        }
        set_flag(c, F_AF, 1); set_flag(c, F_CF, 1);
    } else {
        set_r8(c, R_AL, (uint8_t)(al & 0x0F));
        set_flag(c, F_AF, 0); set_flag(c, F_CF, 0);
    }
}

int x86_aam(cpu_t *c, uint8_t base)
{
    if (base == 0) {
        /* Measured on 8088: AX is left alone, SZP are set as though the
         * result were zero, AF and CF are cleared, then INT 0 is taken. */
        set_flag(c, F_AF, 0);
        set_flag(c, F_CF, 0);
        flags_szp(c, 0, 0);
        x86_divide_error(c);
        return 0;
    }
    uint8_t al = get_r8(c, R_AL);
    set_r8(c, R_AH, (uint8_t)(al / base));
    set_r8(c, R_AL, (uint8_t)(al % base));
    flags_szp(c, get_r8(c, R_AL), 0);
    return 1;
}

void x86_aad(cpu_t *c, uint8_t base)
{
    uint8_t al = (uint8_t)(get_r8(c, R_AL) + get_r8(c, R_AH) * base);
    set_r8(c, R_AL, al);
    set_r8(c, R_AH, 0);
    flags_szp(c, al, 0);
}

/* ===================================================================== */
/* Instruction fetch and ModR/M                                          */
/* ===================================================================== */

static inline uint8_t fetch8(cpu_t *c)
{
    uint8_t v = c->mem[phys(c->seg[S_CS], c->ip)];
    c->ip = (uint16_t)(c->ip + 1);
    return v;
}

static inline uint16_t fetch16(cpu_t *c)
{
    uint16_t lo = c->mem[phys(c->seg[S_CS], c->ip)];
    uint16_t hi = c->mem[phys(c->seg[S_CS], (uint16_t)(c->ip + 1))];
    c->ip = (uint16_t)(c->ip + 2);
    return (uint16_t)(lo | (hi << 8));
}

typedef struct {
    int      mod, reg, rm;
    int      is_reg;
    uint16_t seg;
    uint16_t off;
} modrm_t;

static void decode_modrm(cpu_t *c, modrm_t *m)
{
    uint8_t b = fetch8(c);
    c->t386_modrm = b;
    m->mod = (b >> 6) & 3;
    m->reg = (b >> 3) & 7;
    m->rm  = b & 7;
    m->is_reg = (m->mod == 3);
    if (m->is_reg) { m->seg = 0; m->off = 0; return; }

    int def_seg = S_DS;
    uint16_t off = 0;
    switch (m->rm) {
    case 0: off = (uint16_t)(c->r[R_BX] + c->r[R_SI]); break;
    case 1: off = (uint16_t)(c->r[R_BX] + c->r[R_DI]); break;
    case 2: off = (uint16_t)(c->r[R_BP] + c->r[R_SI]); def_seg = S_SS; break;
    case 3: off = (uint16_t)(c->r[R_BP] + c->r[R_DI]); def_seg = S_SS; break;
    case 4: off = c->r[R_SI]; break;
    case 5: off = c->r[R_DI]; break;
    case 6:
        if (m->mod == 0) off = fetch16(c);
        else { off = c->r[R_BP]; def_seg = S_SS; }
        break;
    case 7: off = c->r[R_BX]; break;
    }
    if (m->mod == 1)      off = (uint16_t)(off + (int8_t)fetch8(c));
    else if (m->mod == 2) off = (uint16_t)(off + fetch16(c));

    m->off = off;
    m->seg = c->seg[(c->seg_override >= 0) ? c->seg_override : def_seg];
}

static inline uint32_t ea(const modrm_t *m) { return phys(m->seg, m->off); }

static uint16_t rm_read(cpu_t *c, const modrm_t *m, int w16)
{
    if (m->is_reg) return w16 ? c->r[m->rm] : get_r8(c, m->rm);
    return w16 ? seg_read16(c, m->seg, m->off) : mem_read8(c, ea(m));
}

static void rm_write(cpu_t *c, const modrm_t *m, int w16, uint16_t v)
{
    if (m->is_reg) {
        if (w16) c->r[m->rm] = v; else set_r8(c, m->rm, (uint8_t)v);
    } else {
        if (w16) seg_write16(c, m->seg, m->off, v);
        else     mem_write8(c, ea(m), (uint8_t)v);
    }
}

static inline uint16_t reg_read(cpu_t *c, const modrm_t *m, int w16)
{
    return w16 ? c->r[m->reg] : get_r8(c, m->reg);
}

static inline void reg_write(cpu_t *c, const modrm_t *m, int w16, uint16_t v)
{
    if (w16) c->r[m->reg] = v; else set_r8(c, m->reg, (uint8_t)v);
}

/* ===================================================================== */
/* Lifecycle                                                             */
/* ===================================================================== */

void cpu_init(cpu_t *c, uint8_t *mem)
{
    memset(c, 0, sizeof(*c));
    c->mem = mem;
    c->model = CPU_80286;
    cpu_reset(c);
}

void cpu_reset(cpu_t *c)
{
    memset(c->r, 0, sizeof(c->r));
    c->seg[S_CS] = 0xFFFF;
    c->seg[S_DS] = c->seg[S_ES] = c->seg[S_SS] = 0;
    c->ip = 0;
    c->flags = cpu_flags_fixed(c);
    c->halted = 0;
    c->stop_reason = STOP_NONE;
    c->seg_override = -1;
    c->rep_prefix = 0;
    c->stop_at = 0;
    c->inhibit_at = ~0ull;
}

const char *cpu_stop_name(int reason)
{
    switch (reason) {
    case STOP_NONE:       return "none";
    case STOP_STEPS:      return "step-budget";
    case STOP_BREAKPOINT: return "breakpoint";
    case STOP_HLT:        return "hlt";
    case STOP_EXIT:       return "exit";
    case STOP_FAULT:      return "fault";
    case STOP_WATCHDOG:   return "watchdog";
    default:              return "?";
    }
}

void cpu_dump(const cpu_t *c, FILE *f)
{
    fprintf(f,
        "AX=%04X BX=%04X CX=%04X DX=%04X SP=%04X BP=%04X SI=%04X DI=%04X\n"
        "DS=%04X ES=%04X SS=%04X CS=%04X IP=%04X FL=%04X [%c%c%c%c%c%c%c%c%c]\n",
        c->r[R_AX], c->r[R_BX], c->r[R_CX], c->r[R_DX],
        c->r[R_SP], c->r[R_BP], c->r[R_SI], c->r[R_DI],
        c->seg[S_DS], c->seg[S_ES], c->seg[S_SS], c->seg[S_CS], c->ip, c->flags,
        (c->flags & F_OF) ? 'O' : '-', (c->flags & F_DF) ? 'D' : '-',
        (c->flags & F_IF) ? 'I' : '-', (c->flags & F_TF) ? 'T' : '-',
        (c->flags & F_SF) ? 'S' : '-', (c->flags & F_ZF) ? 'Z' : '-',
        (c->flags & F_AF) ? 'A' : '-', (c->flags & F_PF) ? 'P' : '-',
        (c->flags & F_CF) ? 'C' : '-');
}

/* ===================================================================== */
/* Execution                                                             */
/* ===================================================================== */

/* The REP control shared by every string instruction: run one iteration,
 * count CX down and, if the loop continues, point IP back at the prefix so
 * the next iteration is the next instruction. `cmp` adds the ZF test of
 * CMPS/SCAS. Returns with the iteration done (or skipped when CX was 0). */
#define REP_ITER(body, cmp)                                                   \
    do {                                                                      \
        if (c->rep_prefix) {                                                  \
            if (c->r[R_CX] == 0) break;                                       \
            c->t386_elem = 1;                                                 \
            body;                                                             \
            c->r[R_CX] = (uint16_t)(c->r[R_CX] - 1);                          \
            int again_ = c->r[R_CX] != 0;                                     \
            if (cmp) again_ = again_ &&                                       \
                ((c->flags & F_ZF) != 0) == (c->rep_prefix == 0xF3);          \
            if (again_) { c->ip = c->op_ip; c->seg[S_CS] = c->op_cs; }        \
        } else {                                                              \
            body;                                                             \
        }                                                                     \
    } while (0)

int cpu_step(cpu_t *c)
{
    c->op_cs = c->seg[S_CS];
    c->op_ip = c->ip;

    if (c->cover) {
        const uint32_t a = phys(c->op_cs, c->op_ip);
        if (a >= c->cover_lo && a <= c->cover_hi) c->cover[a - c->cover_lo] |= 1;
    }
    c->seg_override = -1;
    c->rep_prefix = 0;
    c->t386_seg_pfx = c->t386_rep_pfx = c->t386_lock_pfx = 0;
    c->t386_modrm = -1;
    c->t386_elem = c->t386_fault = 0;
    c->t386_dev = 0;

    uint8_t op;
    int more_prefixes = 1;

    /* --- prefixes ---------------------------------------------------- */
    do {
        op = fetch8(c);
        switch (op) {
        case 0x26: c->seg_override = S_ES; c->t386_seg_pfx++; break;
        case 0x2E: c->seg_override = S_CS; c->t386_seg_pfx++; break;
        case 0x36: c->seg_override = S_SS; c->t386_seg_pfx++; break;
        case 0x3E: c->seg_override = S_DS; c->t386_seg_pfx++; break;
        case 0xF0: case 0xF1: c->t386_lock_pfx++; break;   /* LOCK (0xF1 aliases it) */
        case 0xF2: c->rep_prefix = 0xF2; c->t386_rep_pfx++; break;  /* REPNE */
        case 0xF3: c->rep_prefix = 0xF3; c->t386_rep_pfx++; break;  /* REP / REPE */
        default:   more_prefixes = 0; break;
        }
    } while (more_prefixes);

    modrm_t m;
    int w16 = op & 1;
    const uint16_t dseg = c->seg[(c->seg_override >= 0) ? c->seg_override : S_DS];

    switch (op) {

    /* --- ALU r/m,reg and reg,r/m ------------------------------------- */
    case 0x00: case 0x01: case 0x08: case 0x09:
    case 0x10: case 0x11: case 0x18: case 0x19:
    case 0x20: case 0x21: case 0x28: case 0x29:
    case 0x30: case 0x31: case 0x38: case 0x39: {
        int aop = (op >> 3) & 7;
        decode_modrm(c, &m);
        uint16_t res = (uint16_t)alu_op(c, aop, rm_read(c, &m, w16),
                                        reg_read(c, &m, w16), w16);
        if (aop != 7) rm_write(c, &m, w16, res);
        break;
    }
    case 0x02: case 0x03: case 0x0A: case 0x0B:
    case 0x12: case 0x13: case 0x1A: case 0x1B:
    case 0x22: case 0x23: case 0x2A: case 0x2B:
    case 0x32: case 0x33: case 0x3A: case 0x3B: {
        int aop = (op >> 3) & 7;
        decode_modrm(c, &m);
        uint16_t res = (uint16_t)alu_op(c, aop, reg_read(c, &m, w16),
                                        rm_read(c, &m, w16), w16);
        if (aop != 7) reg_write(c, &m, w16, res);
        break;
    }
    /* --- ALU acc,imm -------------------------------------------------- */
    case 0x04: case 0x05: case 0x0C: case 0x0D:
    case 0x14: case 0x15: case 0x1C: case 0x1D:
    case 0x24: case 0x25: case 0x2C: case 0x2D:
    case 0x34: case 0x35: case 0x3C: case 0x3D: {
        int aop = (op >> 3) & 7;
        uint16_t imm = w16 ? fetch16(c) : fetch8(c);
        uint16_t a = w16 ? c->r[R_AX] : get_r8(c, R_AL);
        uint16_t res = (uint16_t)alu_op(c, aop, a, imm, w16);
        if (aop != 7) { if (w16) c->r[R_AX] = res; else set_r8(c, R_AL, (uint8_t)res); }
        break;
    }

    /* --- segment push/pop -------------------------------------------- */
    case 0x06: cpu_push16(c, c->seg[S_ES]); break;
    case 0x07: c->seg[S_ES] = cpu_pop16(c); break;
    case 0x0E: cpu_push16(c, c->seg[S_CS]); break;
    case 0x0F:
        if (c->model == CPU_8086) { c->seg[S_CS] = cpu_pop16(c); break; }   /* POP CS */
        /* 286 system instructions have no place in real-mode game code. */
        c->ip = c->op_ip;
        c->stop_reason = STOP_FAULT;
        return STOP_FAULT;
    case 0x16: cpu_push16(c, c->seg[S_SS]); break;
    case 0x17: x86_load_ss(c, cpu_pop16(c)); break;
    case 0x1E: cpu_push16(c, c->seg[S_DS]); break;
    case 0x1F: c->seg[S_DS] = cpu_pop16(c); break;

    /* --- BCD adjusts -------------------------------------------------- */
    case 0x27: x86_daa(c); break;
    case 0x2F: x86_das(c); break;
    case 0x37: x86_aaa(c); break;
    case 0x3F: x86_aas(c); break;

    /* --- INC/DEC reg16 ------------------------------------------------ */
    case 0x40: case 0x41: case 0x42: case 0x43:
    case 0x44: case 0x45: case 0x46: case 0x47:
        c->r[op & 7] = (uint16_t)alu_inc(c, c->r[op & 7], 1);
        break;
    case 0x48: case 0x49: case 0x4A: case 0x4B:
    case 0x4C: case 0x4D: case 0x4E: case 0x4F:
        c->r[op & 7] = (uint16_t)alu_dec(c, c->r[op & 7], 1);
        break;

    /* --- PUSH/POP reg16 ----------------------------------------------- */
    case 0x50: case 0x51: case 0x52: case 0x53:
    case 0x54: case 0x55: case 0x56: case 0x57:
        x86_push_reg(c, op & 7);
        break;
    case 0x58: case 0x59: case 0x5A: case 0x5B:
    case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        c->r[op & 7] = cpu_pop16(c);
        break;

    /* --- Jcc ------------------------------------------------------------ */
    case 0x70: case 0x71: case 0x72: case 0x73:
    case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B:
    case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        int8_t rel = (int8_t)fetch8(c);
        if (x86_cond(c, op & 0x0F)) c->ip = (uint16_t)(c->ip + rel);
        break;
    }

    /* --- 0x60..0x6F: aliases of Jcc on the 8086; new instructions from the
     *     80186 on. ------------------------------------------------------ */
    case 0x60: case 0x61: case 0x62: case 0x63:
    case 0x64: case 0x65: case 0x66: case 0x67:
    case 0x68: case 0x69: case 0x6A: case 0x6B:
    case 0x6C: case 0x6D: case 0x6E: case 0x6F:
        if (c->model == CPU_8086) {
            int8_t rel = (int8_t)fetch8(c);
            if (x86_cond(c, op & 0x0F)) c->ip = (uint16_t)(c->ip + rel);
            break;
        }
        switch (op) {
        case 0x60: x86_pusha(c); break;
        case 0x61: x86_popa(c); break;
        case 0x62: {   /* BOUND r16, m16&16 -> INT 5 when out of range */
            decode_modrm(c, &m);
            int16_t idx = (int16_t)c->r[m.reg];
            int16_t lo = (int16_t)seg_read16(c, m.seg, m.off);
            int16_t hi = (int16_t)seg_read16(c, m.seg, (uint16_t)(m.off + 2));
            if (idx < lo || idx > hi) {
                c->ip = c->op_ip; c->seg[S_CS] = c->op_cs;
                cpu_interrupt(c, 5);
            }
            break;
        }
        case 0x68: cpu_push16(c, fetch16(c)); break;                       /* PUSH imm16 */
        case 0x6A: cpu_push16(c, (uint16_t)(int16_t)(int8_t)fetch8(c)); break;
        case 0x69: case 0x6B: {   /* IMUL r16, r/m16, imm16 / imm8 */
            decode_modrm(c, &m);
            uint16_t src = rm_read(c, &m, 1);
            uint16_t imm = (op == 0x69) ? fetch16(c)
                                        : (uint16_t)(int16_t)(int8_t)fetch8(c);
            c->r[m.reg] = x86_imul3(c, src, imm);
            break;
        }
        case 0x6C: case 0x6D: REP_ITER(x86_ins(c, op & 1), 0); break;
        case 0x6E: case 0x6F: REP_ITER(x86_outs(c, op & 1, dseg), 0); break;
        default:       /* 0x63..0x67 mean nothing in real mode on these parts */
            c->ip = c->op_ip;
            c->stop_reason = STOP_FAULT;
            return STOP_FAULT;
        }
        break;

    /* --- group 1: ALU r/m,imm ----------------------------------------- */
    case 0x80: case 0x81: case 0x82: case 0x83: {
        decode_modrm(c, &m);
        uint16_t imm;
        if (op == 0x81)      imm = fetch16(c);
        else if (op == 0x83) imm = (uint16_t)(int16_t)(int8_t)fetch8(c);
        else                 imm = fetch8(c);          /* 0x80 and 0x82 */
        int wide = (op == 0x81 || op == 0x83);
        uint16_t res = (uint16_t)alu_op(c, m.reg, rm_read(c, &m, wide), imm, wide);
        if (m.reg != 7) rm_write(c, &m, wide, res);
        break;
    }

    /* --- TEST / XCHG / MOV -------------------------------------------- */
    case 0x84: case 0x85:
        decode_modrm(c, &m);
        alu_logic(c, (uint32_t)rm_read(c, &m, w16) & reg_read(c, &m, w16), w16);
        break;
    case 0x86: case 0x87: {
        decode_modrm(c, &m);
        uint16_t a = rm_read(c, &m, w16), b = reg_read(c, &m, w16);
        rm_write(c, &m, w16, b);
        reg_write(c, &m, w16, a);
        break;
    }
    case 0x88: case 0x89:
        decode_modrm(c, &m);
        rm_write(c, &m, w16, reg_read(c, &m, w16));
        break;
    case 0x8A: case 0x8B:
        decode_modrm(c, &m);
        reg_write(c, &m, w16, rm_read(c, &m, w16));
        break;
    case 0x8C:   /* MOV r/m16, sreg */
        decode_modrm(c, &m);
        rm_write(c, &m, 1, c->seg[m.reg & 3]);
        break;
    case 0x8D:   /* LEA */
        decode_modrm(c, &m);
        c->r[m.reg] = m.off;
        break;
    case 0x8E: { /* MOV sreg, r/m16 */
        decode_modrm(c, &m);
        uint16_t v = rm_read(c, &m, 1);
        if ((m.reg & 3) == S_SS) x86_load_ss(c, v);
        else c->seg[m.reg & 3] = v;
        break;
    }
    case 0x8F:   /* POP r/m16 */
        decode_modrm(c, &m);
        rm_write(c, &m, 1, cpu_pop16(c));
        break;

    /* --- XCHG AX,reg / NOP -------------------------------------------- */
    case 0x90: break;   /* NOP = XCHG AX,AX */
    case 0x91: case 0x92: case 0x93:
    case 0x94: case 0x95: case 0x96: case 0x97: {
        uint16_t t = c->r[R_AX];
        c->r[R_AX] = c->r[op & 7];
        c->r[op & 7] = t;
        break;
    }

    case 0x98: c->r[R_AX] = (uint16_t)(int16_t)(int8_t)get_r8(c, R_AL); break;   /* CBW */
    case 0x99: c->r[R_DX] = (c->r[R_AX] & 0x8000) ? 0xFFFF : 0x0000; break;     /* CWD */
    case 0x9A: { /* CALL far imm */
        uint16_t noff = fetch16(c), nseg = fetch16(c);
        cpu_push16(c, c->seg[S_CS]);
        cpu_push16(c, c->ip);
        c->seg[S_CS] = nseg; c->ip = noff;
        break;
    }
    case 0x9B: break;   /* WAIT */
    case 0x9C: x86_pushf(c); break;
    case 0x9D: x86_popf(c); break;
    case 0x9E: x86_sahf(c); break;
    case 0x9F: x86_lahf(c); break;

    /* --- MOV acc,[disp] ------------------------------------------------ */
    case 0xA0: case 0xA1: {
        uint16_t d = fetch16(c);
        if (w16) c->r[R_AX] = seg_read16(c, dseg, d);
        else     set_r8(c, R_AL, mem_read8(c, phys(dseg, d)));
        break;
    }
    case 0xA2: case 0xA3: {
        uint16_t d = fetch16(c);
        if (w16) seg_write16(c, dseg, d, c->r[R_AX]);
        else     mem_write8(c, phys(dseg, d), get_r8(c, R_AL));
        break;
    }

    /* --- string ops ----------------------------------------------------- */
    case 0xA4: case 0xA5: REP_ITER(x86_movs(c, w16, dseg), 0); break;
    case 0xAA: case 0xAB: REP_ITER(x86_stos(c, w16), 0); break;
    case 0xAC: case 0xAD: REP_ITER(x86_lods(c, w16, dseg), 0); break;
    case 0xA6: case 0xA7: REP_ITER(x86_cmps(c, w16, dseg), 1); break;
    case 0xAE: case 0xAF: REP_ITER(x86_scas(c, w16), 1); break;

    /* --- TEST acc,imm --------------------------------------------------- */
    case 0xA8: case 0xA9: {
        uint16_t imm = w16 ? fetch16(c) : fetch8(c);
        alu_logic(c, (uint32_t)(w16 ? c->r[R_AX] : get_r8(c, R_AL)) & imm, w16);
        break;
    }

    /* --- MOV reg,imm ---------------------------------------------------- */
    case 0xB0: case 0xB1: case 0xB2: case 0xB3:
    case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        set_r8(c, op & 7, fetch8(c));
        break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB:
    case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        c->r[op & 7] = fetch16(c);
        break;

    /* --- returns -------------------------------------------------------- */
    case 0xC2: {   /* RET imm16 */
        uint16_t n = fetch16(c);
        c->ip = cpu_pop16(c);
        c->r[R_SP] = (uint16_t)(c->r[R_SP] + n);
        break;
    }
    case 0xC3:
        c->ip = cpu_pop16(c);
        break;
    case 0xC0: case 0xC1:     /* 8086: RET aliases. 186+: group 2 by imm8 */
        if (c->model == CPU_8086) {
            if (op == 0xC0) {
                uint16_t n = fetch16(c);
                c->ip = cpu_pop16(c);
                c->r[R_SP] = (uint16_t)(c->r[R_SP] + n);
            } else {
                c->ip = cpu_pop16(c);
            }
            break;
        }
        decode_modrm(c, &m);
        {
            uint8_t cnt = fetch8(c);
            rm_write(c, &m, w16, x86_shift(c, m.reg, rm_read(c, &m, w16), cnt, w16));
        }
        break;
    case 0xC4: case 0xC5: {   /* LES / LDS */
        decode_modrm(c, &m);
        c->r[m.reg] = seg_read16(c, m.seg, m.off);
        c->seg[(op == 0xC4) ? S_ES : S_DS] =
            seg_read16(c, m.seg, (uint16_t)(m.off + 2));
        break;
    }
    case 0xC6: case 0xC7: {   /* MOV r/m, imm */
        decode_modrm(c, &m);
        uint16_t imm = w16 ? fetch16(c) : fetch8(c);
        rm_write(c, &m, w16, imm);
        break;
    }
    case 0xCA: {   /* RETF imm16 */
        uint16_t n = fetch16(c);
        c->ip = cpu_pop16(c);
        c->seg[S_CS] = cpu_pop16(c);
        c->r[R_SP] = (uint16_t)(c->r[R_SP] + n);
        break;
    }
    case 0xCB:     /* RETF */
        c->ip = cpu_pop16(c);
        c->seg[S_CS] = cpu_pop16(c);
        break;
    case 0xC8: case 0xC9:     /* 8086: RETF aliases. 186+: ENTER / LEAVE */
        if (c->model == CPU_8086) {
            if (op == 0xC8) {
                uint16_t n = fetch16(c);
                c->ip = cpu_pop16(c);
                c->seg[S_CS] = cpu_pop16(c);
                c->r[R_SP] = (uint16_t)(c->r[R_SP] + n);
            } else {
                c->ip = cpu_pop16(c);
                c->seg[S_CS] = cpu_pop16(c);
            }
            break;
        }
        if (op == 0xC8) {
            uint16_t size = fetch16(c);
            uint8_t level = fetch8(c);
            x86_enter(c, size, level);
        } else {
            x86_leave(c);
        }
        break;

    case 0xCC: cpu_interrupt(c, 3); break;
    case 0xCD: { uint8_t v = fetch8(c); cpu_interrupt(c, v); break; }
    case 0xCE: if (c->flags & F_OF) cpu_interrupt(c, 4); break;
    case 0xCF: x86_iret(c); break;

    /* --- group 2: shifts ------------------------------------------------ */
    case 0xD0: case 0xD1:
        decode_modrm(c, &m);
        rm_write(c, &m, w16, x86_shift(c, m.reg, rm_read(c, &m, w16), 1, w16));
        break;
    case 0xD2: case 0xD3:
        decode_modrm(c, &m);
        rm_write(c, &m, w16,
                 x86_shift(c, m.reg, rm_read(c, &m, w16), get_r8(c, R_CL), w16));
        break;

    case 0xD4: { uint8_t base = fetch8(c); x86_aam(c, base); break; }
    case 0xD5: { uint8_t base = fetch8(c); x86_aad(c, base); break; }
    case 0xD6:     /* SALC (undocumented) */
        set_r8(c, R_AL, (c->flags & F_CF) ? 0xFF : 0x00);
        break;
    case 0xD7:     /* XLAT */
        set_r8(c, R_AL,
               mem_read8(c, phys(dseg, (uint16_t)(c->r[R_BX] + get_r8(c, R_AL)))));
        break;

    /* --- ESC (8087 coprocessor) ----------------------------------------- */
    case 0xD8: case 0xD9: case 0xDA: case 0xDB:
    case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        /* No 8087 is present. The instruction is decoded so that IP advances
         * correctly, and the operand is read for its side effects only. */
        decode_modrm(c, &m);
        if (!m.is_reg) (void)mem_read16(c, ea(&m));
        break;

    /* --- loops and jumps ------------------------------------------------ */
    case 0xE0: {   /* LOOPNZ */
        int8_t rel = (int8_t)fetch8(c);
        c->r[R_CX] = (uint16_t)(c->r[R_CX] - 1);
        if (c->r[R_CX] != 0 && !(c->flags & F_ZF)) c->ip = (uint16_t)(c->ip + rel);
        break;
    }
    case 0xE1: {   /* LOOPZ */
        int8_t rel = (int8_t)fetch8(c);
        c->r[R_CX] = (uint16_t)(c->r[R_CX] - 1);
        if (c->r[R_CX] != 0 && (c->flags & F_ZF)) c->ip = (uint16_t)(c->ip + rel);
        break;
    }
    case 0xE2: {   /* LOOP */
        int8_t rel = (int8_t)fetch8(c);
        c->r[R_CX] = (uint16_t)(c->r[R_CX] - 1);
        if (c->r[R_CX] != 0) c->ip = (uint16_t)(c->ip + rel);
        break;
    }
    case 0xE3: {   /* JCXZ */
        int8_t rel = (int8_t)fetch8(c);
        if (c->r[R_CX] == 0) c->ip = (uint16_t)(c->ip + rel);
        break;
    }
    case 0xE4: case 0xE5: { uint8_t p = fetch8(c); x86_in(c, p, w16); break; }
    case 0xE6: case 0xE7: { uint8_t p = fetch8(c); x86_out(c, p, w16); break; }
    case 0xE8: {   /* CALL rel16 */
        int16_t rel = (int16_t)fetch16(c);
        cpu_push16(c, c->ip);
        c->ip = (uint16_t)(c->ip + rel);
        break;
    }
    case 0xE9: {   /* JMP rel16 */
        int16_t rel = (int16_t)fetch16(c);
        c->ip = (uint16_t)(c->ip + rel);
        break;
    }
    case 0xEA: {   /* JMP far imm */
        uint16_t noff = fetch16(c), nseg = fetch16(c);
        c->ip = noff; c->seg[S_CS] = nseg;
        break;
    }
    case 0xEB: {   /* JMP rel8 */
        int8_t rel = (int8_t)fetch8(c);
        c->ip = (uint16_t)(c->ip + rel);
        break;
    }
    case 0xEC: case 0xED: x86_in(c, c->r[R_DX], w16); break;
    case 0xEE: case 0xEF: x86_out(c, c->r[R_DX], w16); break;

    case 0xF4:                /* HLT: wait for an interrupt */
        c->halted = 1;
        break;
    case 0xF5: c->flags ^= F_CF; break;   /* CMC */

    /* --- group 3 -------------------------------------------------------- */
    case 0xF6: case 0xF7: {
        decode_modrm(c, &m);
        uint16_t v = rm_read(c, &m, w16);
        switch (m.reg) {
        case 0: case 1: {   /* TEST r/m, imm */
            uint16_t imm = w16 ? fetch16(c) : fetch8(c);
            alu_logic(c, (uint32_t)v & imm, w16);
            break;
        }
        case 2: rm_write(c, &m, w16, (uint16_t)~v); break;                         /* NOT */
        case 3: rm_write(c, &m, w16, (uint16_t)alu_sub(c, 0, v, w16, 0)); break;   /* NEG */
        case 4: if (w16) x86_mul16(c, v); else x86_mul8(c, (uint8_t)v); break;
        case 5: if (w16) x86_imul16(c, v); else x86_imul8(c, (uint8_t)v); break;
        case 6: if (w16) x86_div16(c, v); else x86_div8(c, (uint8_t)v); break;
        default:
            if (w16) x86_idiv16(c, v, c->rep_prefix);
            else     x86_idiv8(c, (uint8_t)v, c->rep_prefix);
            break;
        }
        break;
    }

    case 0xF8: set_flag(c, F_CF, 0); break;   /* CLC */
    case 0xF9: set_flag(c, F_CF, 1); break;   /* STC */
    case 0xFA: x86_cli(c); break;             /* CLI */
    case 0xFB: x86_sti(c); break;             /* STI */
    case 0xFC: set_flag(c, F_DF, 0); break;   /* CLD */
    case 0xFD: set_flag(c, F_DF, 1); break;   /* STD */

    /* --- groups 4 and 5 -------------------------------------------------- */
    case 0xFE: {
        decode_modrm(c, &m);
        uint16_t v = rm_read(c, &m, 0);
        rm_write(c, &m, 0,
                 (uint16_t)((m.reg == 0) ? alu_inc(c, v, 0) : alu_dec(c, v, 0)));
        break;
    }
    case 0xFF: {
        decode_modrm(c, &m);
        switch (m.reg) {
        case 0: rm_write(c, &m, 1, (uint16_t)alu_inc(c, rm_read(c, &m, 1), 1)); break;
        case 1: rm_write(c, &m, 1, (uint16_t)alu_dec(c, rm_read(c, &m, 1), 1)); break;
        case 2:            /* CALL near r/m */
            { uint16_t t = rm_read(c, &m, 1); cpu_push16(c, c->ip); c->ip = t; }
            break;
        case 3: {          /* CALL far [mem] */
            uint16_t noff = seg_read16(c, m.seg, m.off);
            uint16_t nseg = seg_read16(c, m.seg, (uint16_t)(m.off + 2));
            cpu_push16(c, c->seg[S_CS]);
            cpu_push16(c, c->ip);
            c->seg[S_CS] = nseg; c->ip = noff;
            break;
        }
        case 4: c->ip = rm_read(c, &m, 1); break;   /* JMP near r/m */
        case 5: {          /* JMP far [mem] */
            c->ip = seg_read16(c, m.seg, m.off);
            c->seg[S_CS] = seg_read16(c, m.seg, (uint16_t)(m.off + 2));
            break;
        }
        default:           /* PUSH r/m16 (reg field 7 aliases 6) */
            /* Same 8086 quirk as opcode 0x54: SP is decremented before the
             * source operand is read. The 286 reads the operand first. */
            {
                uint16_t before = rm_read(c, &m, 1);
                c->r[R_SP] = (uint16_t)(c->r[R_SP] - 2);
                seg_write16(c, c->seg[S_SS], c->r[R_SP],
                            (c->model >= CPU_80286) ? before : rm_read(c, &m, 1));
            }
            break;
        }
        break;
    }

    default:
        c->ip = c->op_ip;
        c->stop_reason = STOP_FAULT;
        return STOP_FAULT;
    }

    c->icount += c->t386 ? t386_step(c, op, c->t386_fault) : 1;
    return STOP_NONE;
}
