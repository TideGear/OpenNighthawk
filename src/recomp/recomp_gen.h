/* recomp_gen.h - what generated code includes, and the tables it exports.
 *
 * The recompiler (recompiler/recomp.py) writes one set of C files per
 * module of the original game - each program, overlay and driver - from the
 * user's own copy. Each module is a list of REGIONS (one C function each,
 * roughly one of the original's functions, with a label on every
 * instruction so execution can resume anywhere after an interrupt) and a
 * table mapping every translated instruction to its region.
 *
 * Generated code is derived from the original executables. It is written
 * into the build tree and is never part of the repository.
 */
#ifndef F117R_RECOMP_GEN_H
#define F117R_RECOMP_GEN_H

#include "x86_sem.h"
#include "timing386.h"

/* A region: returns 1 after running at least the CHECK of its entry
 * instruction (CS:IP say where to go next), 0 to have the interpreter take
 * the instruction at CS:IP. */
typedef int (*rc_fn)(cpu_t *c);

typedef struct {
    rc_fn    fn;
    uint16_t seg;            /* CS = load segment + seg */
    uint32_t run_first;      /* this region's validated byte runs: RUNS[run_first..+nruns) */
    uint32_t nruns;
} rc_region;

typedef struct {
    uint32_t off;            /* image offset of the instruction's first byte */
    uint32_t region;
} rc_entry;

typedef struct {
    uint32_t off, len;       /* image bytes that must equal IMAGE[off..off+len) */
} rc_run;

typedef struct {
    const char     *name;    /* upper-case file name, for messages */
    uint64_t        file_hash;   /* FNV-1a 64 of the file as DOS reads it */
    uint32_t        size;    /* bytes in the unrelocated image */
    uint32_t        origin;  /* 0x100 for a .COM (image at PSP:0100), else 0 */
    const uint8_t  *image;   /* the unrelocated image (expected bytes) */
    const rc_region *regions;
    uint32_t        nregions;
    const rc_entry *entries; /* sorted by off */
    uint32_t        nentries;
    const rc_run   *runs;
    uint32_t        nruns;
    uint32_t        ninsns;
    /* A FLOATING module is not loaded by DOS but placed by the program
     * itself - the LZEXE decompressor copies itself to high memory before
     * it runs. It is recognised where it runs by its code: the image bytes
     * [probe_off, probe_off+probe_len) at CS:probe_off. file_hash is 0. */
    uint32_t        floating;
    uint32_t        probe_off, probe_len;
} rc_module;

/* How often a planted mutation ran (recompiler --mutate; 0 otherwise). */
extern unsigned long long rc_mutant_hits;
/* Diagnostics can stop after N translated instructions even when a REP
 * element or zero-count shift advances no clock. -1 means unlimited. */
extern int rc_instruction_budget;

/* The generated list (recomp_modules.c in the generated directory). */
extern const rc_module *const RC_MODULES[];
extern const unsigned RC_NMODULES;

/* ---- the vocabulary of a translated instruction ------------------------
 * CHECK  stop here when an event is due; record where this instruction is
 * IC     it retired
 * EXIT   leave the region for the dispatcher, continuing at a near IP
 * INTERP leave and have the interpreter run the instruction at IP
 * CODE8/CODE16  an operand byte/word read from memory as the CPU fetches
 *               it (loader-relocated or run-time-patched operands) */
#define CHECK(ip_) do { if (c->icount >= c->stop_at) { c->ip = (ip_); return 1; } \
                        c->op_ip = (ip_); } while (0)
#define TIMING(next_, modrm_, seg_, rep_, ns_, nr_, nl_) do { if (c->t386) { \
    if (rc_instruction_budget == 0) { c->ip = c->op_ip; return 1; } \
    if (rc_instruction_budget > 0) rc_instruction_budget--; \
    t386_begin(c, next_, modrm_, seg_, rep_, ns_, nr_, nl_); } } while (0)
#define IC(op_)    (c->icount += c->t386 ? t386_step(c, op_, c->t386_fault) : 1)
#define EXIT(ip_)  do { c->ip = (uint16_t)(ip_); return 1; } while (0)
#define INTERP(ip_) do { c->ip = (uint16_t)(ip_); return 0; } while (0)
/* Instruction bytes use the prefetch timing, as cpu_step's fetch8/fetch16
 * do; they must not also charge the data bus when code executes in VRAM. */
#define CODE8(o_)  (c->mem[phys(c->seg[S_CS], (uint16_t)(o_))])
#define CODE16(o_) ((uint16_t)(CODE8(o_) | ((uint16_t)CODE8((o_) + 1) << 8)))

#endif /* F117R_RECOMP_GEN_H */
