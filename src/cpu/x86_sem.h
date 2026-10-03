/* x86_sem.h - instruction semantics shared by the interpreter and the
 * recompiled code.
 *
 * Every helper here is the oracle interpreter's own code (Reimp
 * tools/x86oracle/cpu.c at cfb8cec9), moved out of cpu_step so that the
 * generated C calls exactly what the interpreter calls. Nothing in this file
 * decodes: operands arrive already decoded, from cpu_step at run time or
 * from the recompiler at build time. The quirk comments are the oracle's,
 * because each records a measurement against real silicon.
 *
 * The SingleStepTests vectors exercise these through the interpreter
 * (tests/sstest.py, tests/sst286.py) and through generated code
 * (tests/sst_recomp.py), so a semantic slip shows up in both places.
 */
#ifndef F117R_X86_SEM_H
#define F117R_X86_SEM_H

#include "cpu.h"

#ifdef _MSC_VER
#define X86_INLINE static __forceinline
#else
#define X86_INLINE static inline __attribute__((always_inline))
#endif

extern const uint8_t x86_parity_tab[256];

/* ===================================================================== */
/* Flags                                                                 */
/* ===================================================================== */

X86_INLINE void set_flag(cpu_t *c, uint16_t m, int on)
{
    if (on) c->flags |= m; else c->flags = (uint16_t)(c->flags & (uint16_t)(m ^ 0xFFFFu));
}

X86_INLINE void flags_szp(cpu_t *c, uint32_t res, int w16)
{
    uint32_t m = w16 ? 0xFFFFu : 0xFFu;
    set_flag(c, F_ZF, (res & m) == 0);
    set_flag(c, F_SF, (res & (w16 ? 0x8000u : 0x80u)) != 0);
    set_flag(c, F_PF, x86_parity_tab[res & 0xFF]);
}

/* Condition codes for Jcc / SETcc, indexed by the low nibble of the
 * opcode. */
X86_INLINE int x86_cond(const cpu_t *c, int n)
{
    int of = (c->flags & F_OF) != 0, cf = (c->flags & F_CF) != 0;
    int zf = (c->flags & F_ZF) != 0, sf = (c->flags & F_SF) != 0;
    int pf = (c->flags & F_PF) != 0;
    int r;
    switch (n >> 1) {
    case 0: r = of; break;                    /* JO  / JNO  */
    case 1: r = cf; break;                    /* JB  / JNB  */
    case 2: r = zf; break;                    /* JZ  / JNZ  */
    case 3: r = cf || zf; break;              /* JBE / JA   */
    case 4: r = sf; break;                    /* JS  / JNS  */
    case 5: r = pf; break;                    /* JP  / JNP  */
    case 6: r = sf != of; break;              /* JL  / JGE  */
    default: r = zf || (sf != of); break;     /* JLE / JG   */
    }
    return (n & 1) ? !r : r;
}

/* ===================================================================== */
/* ALU primitives                                                        */
/* ===================================================================== */

X86_INLINE uint32_t alu_add(cpu_t *c, uint32_t a, uint32_t b, int w16, uint32_t cin)
{
    uint32_t res = a + b + cin;
    uint32_t sign = w16 ? 0x8000u : 0x80u;
    uint32_t mask = w16 ? 0xFFFFu : 0xFFu;
    set_flag(c, F_CF, (res & ~mask) != 0);
    set_flag(c, F_AF, ((a ^ b ^ res) & 0x10u) != 0);
    set_flag(c, F_OF, ((~(a ^ b) & (a ^ res)) & sign) != 0);
    flags_szp(c, res, w16);
    return res & mask;
}

X86_INLINE uint32_t alu_sub(cpu_t *c, uint32_t a, uint32_t b, int w16, uint32_t bin)
{
    uint32_t res = a - b - bin;
    uint32_t sign = w16 ? 0x8000u : 0x80u;
    uint32_t mask = w16 ? 0xFFFFu : 0xFFu;
    set_flag(c, F_CF, (res & ~mask) != 0);
    set_flag(c, F_AF, ((a ^ b ^ res) & 0x10u) != 0);
    set_flag(c, F_OF, (((a ^ b) & (a ^ res)) & sign) != 0);
    flags_szp(c, res, w16);
    return res & mask;
}

X86_INLINE uint32_t alu_logic(cpu_t *c, uint32_t res, int w16)
{
    uint32_t mask = w16 ? 0xFFFFu : 0xFFu;
    res &= mask;
    set_flag(c, F_CF, 0);
    set_flag(c, F_OF, 0);
    /* The manual calls AF undefined here; real 8088 silicon clears it. */
    set_flag(c, F_AF, 0);
    flags_szp(c, res, w16);
    return res;
}

/* op: 0=ADD 1=OR 2=ADC 3=SBB 4=AND 5=SUB 6=XOR 7=CMP */
X86_INLINE uint32_t alu_op(cpu_t *c, int op, uint32_t a, uint32_t b, int w16)
{
    uint32_t cf = (c->flags & F_CF) ? 1u : 0u;
    switch (op) {
    case 0:  return alu_add(c, a, b, w16, 0);
    case 1:  return alu_logic(c, a | b, w16);
    case 2:  return alu_add(c, a, b, w16, cf);
    case 3:  return alu_sub(c, a, b, w16, cf);
    case 4:  return alu_logic(c, a & b, w16);
    case 5:  return alu_sub(c, a, b, w16, 0);
    case 6:  return alu_logic(c, a ^ b, w16);
    default: alu_sub(c, a, b, w16, 0); return a;   /* CMP discards result */
    }
}

/* INC/DEC leave CF alone. */
X86_INLINE uint32_t alu_inc(cpu_t *c, uint32_t a, int w16)
{
    uint16_t keep = (uint16_t)(c->flags & F_CF);
    uint32_t r = alu_add(c, a, 1, w16, 0);
    c->flags = (uint16_t)((c->flags & (uint16_t)(0xFFFFu ^ F_CF)) | keep);
    return r;
}

X86_INLINE uint32_t alu_dec(cpu_t *c, uint32_t a, int w16)
{
    uint16_t keep = (uint16_t)(c->flags & F_CF);
    uint32_t r = alu_sub(c, a, 1, w16, 0);
    c->flags = (uint16_t)((c->flags & (uint16_t)(0xFFFFu ^ F_CF)) | keep);
    return r;
}

/* ===================================================================== */
/* Shifts and rotates                                                    */
/* ===================================================================== */

/* op: 0=ROL 1=ROR 2=RCL 3=RCR 4=SHL 5=SHR 6=SETMO 7=SAR
 *
 * The 8086 does not mask the shift count to 5 bits the way the 286 and
 * later do: a CL of 200 really does shift two hundred times. Counts arrive
 * here raw. Out of line: it is large and its loops dominate anyway. */
uint16_t x86_shift(cpu_t *c, int op, uint16_t val, uint8_t cnt, int w16);

/* ===================================================================== */
/* Interrupts and faults                                                 */
/* ===================================================================== */

/* Divide error. The 8086 pushes the address of the instruction AFTER the
 * faulting one; the 286 and later push the faulting instruction itself so a
 * handler can fix up and retry. Callers set op_cs:op_ip and ip (the next
 * instruction) before any helper that can fault. */
void x86_divide_error(cpu_t *c);

/* ===================================================================== */
/* Multiply and divide                                                   */
/* ===================================================================== */

X86_INLINE void x86_mul8(cpu_t *c, uint8_t v)
{
    uint16_t p = (uint16_t)(get_r8(c, R_AL) * v);
    c->r[R_AX] = p;
    int hi = (p >> 8) != 0;
    set_flag(c, F_CF, hi); set_flag(c, F_OF, hi);
    flags_szp(c, p & 0xFF, 0);
    set_flag(c, F_ZF, p == 0);
}

X86_INLINE void x86_mul16(cpu_t *c, uint16_t v)
{
    uint32_t p = (uint32_t)c->r[R_AX] * v;
    c->r[R_AX] = (uint16_t)p;
    c->r[R_DX] = (uint16_t)(p >> 16);
    int hi = c->r[R_DX] != 0;
    set_flag(c, F_CF, hi); set_flag(c, F_OF, hi);
    flags_szp(c, p & 0xFFFF, 1);
    set_flag(c, F_ZF, p == 0);
}

X86_INLINE void x86_imul8(cpu_t *c, uint8_t v)
{
    int16_t p = (int16_t)((int8_t)get_r8(c, R_AL) * (int8_t)v);
    c->r[R_AX] = (uint16_t)p;
    int wide = (p != (int16_t)(int8_t)p);
    set_flag(c, F_CF, wide); set_flag(c, F_OF, wide);
    flags_szp(c, (uint32_t)p & 0xFF, 0);
}

X86_INLINE void x86_imul16(cpu_t *c, uint16_t v)
{
    int32_t p = (int32_t)(int16_t)c->r[R_AX] * (int16_t)v;
    c->r[R_AX] = (uint16_t)p;
    c->r[R_DX] = (uint16_t)((uint32_t)p >> 16);
    int wide = (p != (int32_t)(int16_t)p);
    set_flag(c, F_CF, wide); set_flag(c, F_OF, wide);
    flags_szp(c, (uint32_t)p & 0xFFFF, 1);
}

/* IMUL r16, r/m16, imm (80186+). Returns the product's low word. */
X86_INLINE uint16_t x86_imul3(cpu_t *c, uint16_t src, uint16_t imm)
{
    int32_t p = (int32_t)(int16_t)src * (int16_t)imm;
    int wide = (p != (int32_t)(int16_t)p);
    set_flag(c, F_CF, wide); set_flag(c, F_OF, wide);
    flags_szp(c, (uint32_t)p & 0xFFFF, 1);
    return (uint16_t)p;
}

/* Divides return 0 when they took the divide-error interrupt, in which
 * case CS:IP now point at the handler. */
X86_INLINE int x86_div8(cpu_t *c, uint8_t d)
{
    if (d == 0) { x86_divide_error(c); return 0; }
    uint16_t n = c->r[R_AX];
    uint16_t q = (uint16_t)(n / d);
    if (q > 0xFFu) { x86_divide_error(c); return 0; }
    set_r8(c, R_AL, (uint8_t)q);
    set_r8(c, R_AH, (uint8_t)(n % d));
    return 1;
}

X86_INLINE int x86_div16(cpu_t *c, uint16_t v)
{
    if (v == 0) { x86_divide_error(c); return 0; }
    uint32_t n = ((uint32_t)c->r[R_DX] << 16) | c->r[R_AX];
    uint32_t q = n / v;
    if (q > 0xFFFFu) { x86_divide_error(c); return 0; }
    c->r[R_AX] = (uint16_t)q;
    c->r[R_DX] = (uint16_t)(n % v);
    return 1;
}

/* Undocumented 8086/8088 behaviour: a REP/REPNE prefix on IDIV inverts the
 * sign correction in the microcode, negating the quotient. Measured: the
 * 8088 also traps on the most-negative quotient. Both are 8086-only. */
X86_INLINE int x86_idiv8(cpu_t *c, uint8_t v, int rep)
{
    int neg_q = (c->model == CPU_8086 && rep != 0);
    int8_t d = (int8_t)v;
    if (d == 0) { x86_divide_error(c); return 0; }
    int16_t n = (int16_t)c->r[R_AX];
    int16_t q = (int16_t)(n / d);
    if (q > 127 || q < (c->model == CPU_8086 ? -127 : -128)) { x86_divide_error(c); return 0; }
    if (neg_q) q = (int16_t)(-q);
    set_r8(c, R_AL, (uint8_t)q);
    set_r8(c, R_AH, (uint8_t)(int8_t)(n % d));
    return 1;
}

X86_INLINE int x86_idiv16(cpu_t *c, uint16_t v, int rep)
{
    int neg_q = (c->model == CPU_8086 && rep != 0);
    int16_t d = (int16_t)v;
    if (d == 0) { x86_divide_error(c); return 0; }
    int32_t n = (int32_t)(((uint32_t)c->r[R_DX] << 16) | c->r[R_AX]);
    /* -32768 / -1 overflows int32 division only for INT32_MIN, which a
     * DX:AX pair cannot reach with a 16-bit divisor of -1 except 0x80000000,
     * whose quotient (+2^31) is out of range and traps below. */
    if (d == -1 && n == INT32_MIN) { x86_divide_error(c); return 0; }
    int32_t q = n / d;
    if (q > 32767 || q < (c->model == CPU_8086 ? -32767 : -32768)) { x86_divide_error(c); return 0; }
    if (neg_q) q = -q;
    c->r[R_AX] = (uint16_t)q;
    c->r[R_DX] = (uint16_t)(int16_t)(n % d);
    return 1;
}

/* ===================================================================== */
/* BCD and friends                                                       */
/* ===================================================================== */

void x86_daa(cpu_t *c);
void x86_das(cpu_t *c);
void x86_aaa(cpu_t *c);
void x86_aas(cpu_t *c);
/* AAM returns 0 when it took the divide-error interrupt (base 0). */
int  x86_aam(cpu_t *c, uint8_t base);
void x86_aad(cpu_t *c, uint8_t base);

/* ===================================================================== */
/* Flags register transfers                                              */
/* ===================================================================== */

X86_INLINE void x86_pushf(cpu_t *c)
{
    cpu_push16(c, (uint16_t)(c->flags | cpu_flags_fixed(c)));
}

X86_INLINE void x86_popf(cpu_t *c)
{
    c->flags = (uint16_t)((cpu_pop16(c) & FLAGS_MUTABLE) | cpu_flags_fixed(c));
    cpu_irq_state_changed(c);
}

X86_INLINE void x86_sahf(cpu_t *c)
{
    c->flags = (uint16_t)((c->flags & 0xFF00u) | (get_r8(c, R_AH) & 0xD5u) | 0x02u);
}

X86_INLINE void x86_lahf(cpu_t *c)
{
    set_r8(c, R_AH, (uint8_t)((c->flags & 0xD5u) | 0x02u));
}

X86_INLINE void x86_iret(cpu_t *c)
{
    c->ip = cpu_pop16(c);
    c->seg[S_CS] = cpu_pop16(c);
    c->flags = (uint16_t)((cpu_pop16(c) & FLAGS_MUTABLE) | cpu_flags_fixed(c));
    if (c->int_depth > 0) c->int_depth--;
    cpu_irq_state_changed(c);
}

/* STI, and the loads of SS: the next instruction runs before any hardware
 * interrupt is taken. Called before the instruction retires, so the
 * boundary in question is icount + 1. */
X86_INLINE void x86_inhibit_next(cpu_t *c)
{
    c->inhibit_at = c->icount + 1;
}

X86_INLINE void x86_sti(cpu_t *c)
{
    set_flag(c, F_IF, 1);
    x86_inhibit_next(c);
    cpu_irq_state_changed(c);
}

X86_INLINE void x86_cli(cpu_t *c)
{
    set_flag(c, F_IF, 0);
}

X86_INLINE void x86_load_ss(cpu_t *c, uint16_t v)
{
    c->seg[S_SS] = v;
    x86_inhibit_next(c);
}

/* ===================================================================== */
/* 80186 stack-frame instructions                                        */
/* ===================================================================== */

X86_INLINE void x86_pusha(cpu_t *c)
{
    uint16_t sp0 = c->r[R_SP];
    cpu_push16(c, c->r[R_AX]); cpu_push16(c, c->r[R_CX]);
    cpu_push16(c, c->r[R_DX]); cpu_push16(c, c->r[R_BX]);
    cpu_push16(c, sp0);        cpu_push16(c, c->r[R_BP]);
    cpu_push16(c, c->r[R_SI]); cpu_push16(c, c->r[R_DI]);
}

X86_INLINE void x86_popa(cpu_t *c)
{
    c->r[R_DI] = cpu_pop16(c); c->r[R_SI] = cpu_pop16(c);
    c->r[R_BP] = cpu_pop16(c); (void)cpu_pop16(c);
    c->r[R_BX] = cpu_pop16(c); c->r[R_DX] = cpu_pop16(c);
    c->r[R_CX] = cpu_pop16(c); c->r[R_AX] = cpu_pop16(c);
}

X86_INLINE void x86_enter(cpu_t *c, uint16_t size, uint8_t level_raw)
{
    uint8_t level = (uint8_t)(level_raw & 0x1F);
    cpu_push16(c, c->r[R_BP]);
    uint16_t frame = c->r[R_SP];
    if (level > 0) {
        for (unsigned i = 1; i < level; i++) {
            c->r[R_BP] = (uint16_t)(c->r[R_BP] - 2);
            cpu_push16(c, seg_read16(c, c->seg[S_SS], c->r[R_BP]));
        }
        cpu_push16(c, frame);
    }
    c->r[R_BP] = frame;
    c->r[R_SP] = (uint16_t)(c->r[R_SP] - size);
}

X86_INLINE void x86_leave(cpu_t *c)
{
    c->r[R_SP] = c->r[R_BP];
    c->r[R_BP] = cpu_pop16(c);
}

/* PUSH reg16. 8086 quirk: PUSH SP stores the already-decremented SP; the
 * 286 stores the value from before the decrement. */
X86_INLINE void x86_push_reg(cpu_t *c, int r)
{
    uint16_t before = c->r[r];
    c->r[R_SP] = (uint16_t)(c->r[R_SP] - 2);
    seg_write16(c, c->seg[S_SS], c->r[R_SP],
                (c->model >= CPU_80286 && r == R_SP) ? before : c->r[r]);
}

/* ===================================================================== */
/* String instructions: one iteration                                    */
/* ===================================================================== */

/* `src_seg` is the source segment after any override; ES:DI is not
 * overridable. These do one element and step the index registers; the REP
 * loop lives in the caller so that each iteration is its own instruction
 * boundary, which is where the CPU takes interrupts during a REP. */

X86_INLINE int x86_str_delta(const cpu_t *c, int w16)
{
    return (c->flags & F_DF) ? (w16 ? -2 : -1) : (w16 ? 2 : 1);
}

X86_INLINE void x86_movs(cpu_t *c, int w16, uint16_t src_seg)
{
    int delta = x86_str_delta(c, w16);
    if (w16) seg_write16(c, c->seg[S_ES], c->r[R_DI], seg_read16(c, src_seg, c->r[R_SI]));
    else     mem_write8(c, phys(c->seg[S_ES], c->r[R_DI]), mem_read8(c, phys(src_seg, c->r[R_SI])));
    c->r[R_SI] = (uint16_t)(c->r[R_SI] + delta);
    c->r[R_DI] = (uint16_t)(c->r[R_DI] + delta);
}

X86_INLINE void x86_cmps(cpu_t *c, int w16, uint16_t src_seg)
{
    int delta = x86_str_delta(c, w16);
    alu_sub(c, w16 ? seg_read16(c, src_seg, c->r[R_SI]) : mem_read8(c, phys(src_seg, c->r[R_SI])),
               w16 ? seg_read16(c, c->seg[S_ES], c->r[R_DI]) : mem_read8(c, phys(c->seg[S_ES], c->r[R_DI])),
            w16, 0);
    c->r[R_SI] = (uint16_t)(c->r[R_SI] + delta);
    c->r[R_DI] = (uint16_t)(c->r[R_DI] + delta);
}

X86_INLINE void x86_stos(cpu_t *c, int w16)
{
    int delta = x86_str_delta(c, w16);
    if (w16) seg_write16(c, c->seg[S_ES], c->r[R_DI], c->r[R_AX]);
    else     mem_write8(c, phys(c->seg[S_ES], c->r[R_DI]), get_r8(c, R_AL));
    c->r[R_DI] = (uint16_t)(c->r[R_DI] + delta);
}

X86_INLINE void x86_lods(cpu_t *c, int w16, uint16_t src_seg)
{
    int delta = x86_str_delta(c, w16);
    if (w16) c->r[R_AX] = seg_read16(c, src_seg, c->r[R_SI]);
    else     set_r8(c, R_AL, mem_read8(c, phys(src_seg, c->r[R_SI])));
    c->r[R_SI] = (uint16_t)(c->r[R_SI] + delta);
}

X86_INLINE void x86_scas(cpu_t *c, int w16)
{
    int delta = x86_str_delta(c, w16);
    alu_sub(c, w16 ? c->r[R_AX] : get_r8(c, R_AL),
               w16 ? seg_read16(c, c->seg[S_ES], c->r[R_DI]) : mem_read8(c, phys(c->seg[S_ES], c->r[R_DI])),
            w16, 0);
    c->r[R_DI] = (uint16_t)(c->r[R_DI] + delta);
}

/* INS: port DX -> ES:DI (80186+) */
X86_INLINE void x86_ins(cpu_t *c, int w16)
{
    int delta = x86_str_delta(c, w16);
    if (w16) seg_write16(c, c->seg[S_ES], c->r[R_DI],
                         (uint16_t)(c->io_read ? c->io_read(c, c->r[R_DX], 2) : 0xFFFFu));
    else     mem_write8(c, phys(c->seg[S_ES], c->r[R_DI]),
                        (uint8_t)(c->io_read ? c->io_read(c, c->r[R_DX], 1) : 0xFFu));
    c->r[R_DI] = (uint16_t)(c->r[R_DI] + delta);
}

/* OUTS: DS:SI -> port DX (80186+) */
X86_INLINE void x86_outs(cpu_t *c, int w16, uint16_t src_seg)
{
    int delta = x86_str_delta(c, w16);
    if (c->io_write)
        c->io_write(c, c->r[R_DX],
                    w16 ? seg_read16(c, src_seg, c->r[R_SI]) : mem_read8(c, phys(src_seg, c->r[R_SI])),
                    w16 ? 2 : 1);
    c->r[R_SI] = (uint16_t)(c->r[R_SI] + delta);
}

/* ===================================================================== */
/* Port I/O                                                              */
/* ===================================================================== */

X86_INLINE void x86_in(cpu_t *c, uint16_t port, int w16)
{
    uint32_t v = c->io_read ? c->io_read(c, port, w16 ? 2 : 1) : 0xFFFFFFFFu;
    if (w16) c->r[R_AX] = (uint16_t)v; else set_r8(c, R_AL, (uint8_t)v);
}

X86_INLINE void x86_out(cpu_t *c, uint16_t port, int w16)
{
    if (c->io_write)
        c->io_write(c, port, w16 ? c->r[R_AX] : get_r8(c, R_AL), w16 ? 2 : 1);
}

#endif /* F117R_X86_SEM_H */
