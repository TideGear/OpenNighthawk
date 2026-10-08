/* timing386.h - the 386DX/33 timing profile: what 86Box's 386DX/33 charges.
 *
 * With the profile on, an instruction advances the clock by the cycles 86Box's exec386_2386 core
 * charges for it (tools/ref86box/timing386.md): its CLOCK_CYCLES, the prefetch queue's refills
 * (prefetch_run), the prefixes, and the device cycles its memory and port accesses cost. The
 * machine's clock then counts CPU cycles at 33,333,333 a second. Off (the default), an instruction
 * costs one clock, as every recorded route assumes.
 *
 * The interpreter records what the cost depends on while it executes (t386_* fields of cpu_t) and
 * calls t386_step at the end of each step. */
#ifndef F117R_TIMING386_H
#define F117R_TIMING386_H

#include "cpu.h"

enum {
    T386_NORMAL, T386_JCC, T386_LOOP, T386_JCXZ, T386_REPSTR, T386_INTO, T386_HLT,
    T386_ENTER, T386_UD, T386_SHIFTN, T386_NOTIMED
};

typedef struct {
    int16_t has, instr, bytes, modrm, reads, reads_l, writes, writes_l;
} t386_pf_t;

typedef struct {
    uint8_t   kind;
    int16_t   cycles[2];     /* register form, memory form */
    t386_pf_t pf[2];
    uint8_t   flush;         /* 1 always, 2 when the branch is taken */
    int16_t   per;           /* cycles per string element */
} t386_op_t;

/* Memory and prefetch wait states: 2 with the board's external cache on, 6 off (cpu.c:4477-4519). */
#define T386_MEM_CACHED 2
#define T386_MEM_UNCACHED 6

/* Natively answered services, cycles per call beyond this machine's INT, stub and IRET (src/machine/
 * dos.c t386_service_cycles; calibrated with tools/ref86box/probe386.py). */
#define T386_SVC_SET13_FROM13      2655679
#define T386_SVC_SET13_FROM3       2414565
#define T386_SVC_SET3              1704399
#define T386_SVC_CURSOR            355
#define T386_SVC_WRITE_CHAR_TEXT   508
#define T386_SVC_TELETYPE_13       4847
#define T386_SVC_TELETYPE_TEXT     728
#define T386_SVC_GET_MODE          207
#define T386_SVC_DAC_BASE          99   /* measured at 256 colours only: the split is an estimate */
#define T386_SVC_DAC_EACH          194
#define T386_SVC_KEY_CHECK         188
#define T386_SVC_TICKS             89
#define T386_SVC_DOS_STDIN_STATUS  3459    /* FreeDOS 1.3 */
#define T386_SVC_DOS_GET_TIME      4037    /* FreeDOS 1.3 */

/* The overhead stub's cost under the profile at a LOOP count of n: T386_DOS_LOOP_BASE +
 * T386_DOS_LOOP_EACH * n beyond the call's own INT (calibrated with probe386.py). */
#define T386_DOS_LOOP_BASE         6
#define T386_DOS_LOOP_EACH         13

/* Turn the profile on: memory at `mem_wait` cycles (T386_MEM_*), VGA memory [vga_lo, vga_lo +
 * vga_size) at `vga_byte` cycles a byte, the prefetch queue empty. */
void t386_enable(cpu_t *c, int mem_wait, uint32_t vga_lo, uint32_t vga_size, uint32_t vga_byte);

/* Called by cpu_step after the instruction ran: the cycles it costs, which the caller adds to the
 * clock in place of one. */
uint32_t t386_step(cpu_t *c, uint8_t op, int stop);

#endif
