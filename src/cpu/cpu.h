/* cpu.h - 8086/80186/80286 real-mode CPU state, memory and stack.
 *
 * Derived from the F-117A Reimp project's oracle (tools/x86oracle/cpu.h at
 * Reimp commit cfb8cec9), which is validated against the SingleStepTests
 * 8088 and 80286 hardware vectors. See docs/provenance.md for what changed
 * and why.
 *
 * Two execution engines share this state and everything in x86_sem.h:
 *   - the interpreter (cpu.c), which decodes at run time and is the
 *     reference, and
 *   - the recompiled code (generated C), which was decoded at build time.
 * Because both call the same semantic helpers and both stop for events at
 * the same instruction boundaries, a run under one is the run under the
 * other. tests/ proves the first half against silicon vectors and the
 * lockstep mode proves the second against the interpreter.
 *
 * Changes from the oracle that matter for parity with real hardware:
 *   - MOV SS / POP SS and STI hold off interrupts for one instruction, as
 *     the CPU does (`inhibit_at`).
 *   - Every memory write checks a bitmap of bytes that recompiled code was
 *     translated from, so code that is rewritten is never run stale.
 *   - `stop_at` is the instruction count at which the run loop must look at
 *     pending events. Device code lowers it; both engines test it before
 *     every instruction.
 */
#ifndef F117R_CPU_H
#define F117R_CPU_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---- register file indices (match ModR/M encoding) --------------------- */
enum { R_AX, R_CX, R_DX, R_BX, R_SP, R_BP, R_SI, R_DI };
enum { R_AL, R_CL, R_DL, R_BL, R_AH, R_CH, R_DH, R_BH };
enum { S_ES, S_CS, S_SS, S_DS };

/* ---- FLAGS bits -------------------------------------------------------- */
#define F_CF 0x0001u
#define F_PF 0x0004u
#define F_AF 0x0010u
#define F_ZF 0x0040u
#define F_SF 0x0080u
#define F_TF 0x0100u
#define F_IF 0x0200u
#define F_DF 0x0400u
#define F_OF 0x0800u

/* On the 8086 the top four bits read back as 1 and bit 1 is always 1. */
#define FLAGS_8086_SET   0xF002u
#define FLAGS_MUTABLE    0x0FD5u

enum { CPU_8086 = 86, CPU_80186 = 186, CPU_80286 = 286 };

#define MEM_SIZE 0x100000u  /* 1 MiB */

typedef struct cpu cpu_t;

/* Return non-zero from an int_hook to say "handled, do not push an IRET
 * frame". */
typedef int  (*int_hook_fn)(cpu_t *c, uint8_t vec);
typedef void (*io_write_fn)(cpu_t *c, uint16_t port, uint32_t val, int width);
typedef uint32_t (*io_read_fn)(cpu_t *c, uint16_t port, int width);

/* Reasons a run can stop. */
enum {
    STOP_NONE = 0,
    STOP_STEPS,        /* instruction budget exhausted / event due */
    STOP_BREAKPOINT,
    STOP_HLT,
    STOP_EXIT,         /* guest asked DOS to terminate */
    STOP_FAULT,        /* undefined instruction or internal error */
    STOP_WATCHDOG
};

struct cpu {
    uint16_t r[8];        /* AX CX DX BX SP BP SI DI */
    uint16_t seg[4];      /* ES CS SS DS */
    uint16_t ip;
    uint16_t flags;

    uint8_t *mem;         /* MEM_SIZE bytes */
    int      model;       /* CPU_8086 / CPU_80186 / CPU_80286 */

    /* decode state for the instruction in flight */
    int      seg_override; /* -1 or S_* */
    int      rep_prefix;   /* 0, 0xF2, 0xF3 */
    uint16_t op_ip;        /* IP at the start of the current instruction */
    uint16_t op_cs;

    /* control */
    int      halted;       /* HLT: waiting for an interrupt */
    int      stop_reason;
    uint64_t icount;       /* instructions retired; the machine's clock */

    /* The run loop looks at events when icount >= stop_at. */
    uint64_t stop_at;
    /* No hardware interrupt is taken at the boundary where icount equals
     * this: the one after STI, MOV SS or POP SS. ~0 when unused. */
    uint64_t inhibit_at;

    int      int_depth;

    /* hooks */
    int_hook_fn   int_hook;
    io_read_fn    io_read;
    io_write_fn   io_write;

    /* Execution coverage, one byte per linear address in [cover_lo,
     * cover_hi]; bit 0 = an instruction started here. NULL disables it. */
    uint8_t      *cover;
    uint32_t      cover_lo, cover_hi;

    void    *user;         /* host context (the machine) */
};

/* ---- recompiled-code tracking ------------------------------------------
 * One bit per linear byte that some recompiled instruction was translated
 * from. A write that changes such a byte calls cpu_code_written, which
 * invalidates the translations covering it and makes the running code stop
 * at the next instruction boundary. Defined in the recomp runtime; a build
 * without recompiled code leaves every bit clear. */
extern uint8_t cpu_codebits[MEM_SIZE / 8];
void cpu_code_written(cpu_t *c, uint32_t lin);

/* ---- lifecycle --------------------------------------------------------- */
void cpu_init(cpu_t *c, uint8_t *mem);
void cpu_reset(cpu_t *c);

/* Execute exactly one instruction (one iteration, for a REP string op).
 * Returns STOP_NONE or a STOP_* code. */
int  cpu_step(cpu_t *c);

/* Raise a software interrupt (pushes FLAGS:CS:IP, clears IF/TF), giving the
 * host hook the first chance at it. */
void cpu_interrupt(cpu_t *c, uint8_t vec);
/* A hardware interrupt: the same frame, but never offered to the hook. */
void cpu_hw_interrupt(cpu_t *c, uint8_t vec);

/* ---- memory ------------------------------------------------------------ */
static inline uint32_t phys(uint16_t s, uint16_t o)
{
    return (uint32_t)(((uint32_t)s << 4) + o) & 0xFFFFFu;
}

static inline uint8_t mem_read8(cpu_t *c, uint32_t a)
{
    return c->mem[a & 0xFFFFFu];
}

static inline uint16_t mem_read16(cpu_t *c, uint32_t a)
{
    /* The 8086 wraps inside the 20-bit space. */
    return (uint16_t)(c->mem[a & 0xFFFFFu] |
                      ((uint16_t)c->mem[(a + 1) & 0xFFFFFu] << 8));
}

static inline void mem_write8(cpu_t *c, uint32_t a, uint8_t v)
{
    a &= 0xFFFFFu;
    if ((cpu_codebits[a >> 3] >> (a & 7)) & 1u) {
        if (c->mem[a] != v) { c->mem[a] = v; cpu_code_written(c, a); }
        return;
    }
    c->mem[a] = v;
}

static inline void mem_write16(cpu_t *c, uint32_t a, uint16_t v)
{
    mem_write8(c, a, (uint8_t)(v & 0xFF));
    mem_write8(c, a + 1, (uint8_t)(v >> 8));
}

/* Segment-relative word access. A word at offset 0xFFFF takes its second
 * byte from offset 0x0000 of the same segment, so every segmented word
 * access goes through these rather than through a linear address. */
static inline uint16_t seg_read16(cpu_t *c, uint16_t seg, uint16_t off)
{
    uint8_t lo = mem_read8(c, phys(seg, off));
    uint8_t hi = mem_read8(c, phys(seg, (uint16_t)(off + 1)));
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static inline void seg_write16(cpu_t *c, uint16_t seg, uint16_t off, uint16_t v)
{
    mem_write8(c, phys(seg, off), (uint8_t)(v & 0xFF));
    mem_write8(c, phys(seg, (uint16_t)(off + 1)), (uint8_t)(v >> 8));
}

/* ---- register helpers -------------------------------------------------- */
static inline uint8_t get_r8(const cpu_t *c, int i)
{
    return (i < 4) ? (uint8_t)(c->r[i] & 0xFF) : (uint8_t)(c->r[i - 4] >> 8);
}
static inline void set_r8(cpu_t *c, int i, uint8_t v)
{
    if (i < 4) c->r[i] = (uint16_t)((c->r[i] & 0xFF00) | v);
    else       c->r[i - 4] = (uint16_t)((c->r[i - 4] & 0x00FF) | ((uint16_t)v << 8));
}

/* ---- stack ------------------------------------------------------------- */
static inline void cpu_push16(cpu_t *c, uint16_t v)
{
    c->r[R_SP] = (uint16_t)(c->r[R_SP] - 2);
    seg_write16(c, c->seg[S_SS], c->r[R_SP], v);
}

static inline uint16_t cpu_pop16(cpu_t *c)
{
    uint16_t v = seg_read16(c, c->seg[S_SS], c->r[R_SP]);
    c->r[R_SP] = (uint16_t)(c->r[R_SP] + 2);
    return v;
}

/* ---- misc -------------------------------------------------------------- */
/* FLAGS bits that always read as 1: the top four plus bit 1 on the
 * 8086/80186; only bit 1 on the 80286 in real mode. */
static inline uint16_t cpu_flags_fixed(const cpu_t *c)
{
    return (c->model >= CPU_80286) ? 0x0002u : FLAGS_8086_SET;
}

const char *cpu_stop_name(int reason);
void cpu_dump(const cpu_t *c, FILE *f);

/* Called whenever IF, TF or the interrupt shadow may have changed, so the
 * run loop re-examines pending interrupts at the next boundary. Supplied
 * by the machine; the test harness supplies a no-op. */
void cpu_irq_state_changed(cpu_t *c);

#endif /* F117R_CPU_H */
