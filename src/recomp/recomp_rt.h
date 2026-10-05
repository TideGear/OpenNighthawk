/* recomp_rt.h - the recompiled code's runtime: which translations exist,
 * where each module is loaded now, and whether its bytes still match.
 *
 * See docs/architecture.md. In short: the recompiler translates each of
 * the game's modules from its unpacked, unrelocated image; at run time
 * every program or overlay DOS loads is matched to its module by the file
 * it came from, and registered at its load segment. A control transfer
 * into registered memory runs the translation when, and only when, the
 * bytes there are the bytes it was translated from (with the relocations
 * this load applied). Anything else - code no translation covers, a module
 * whose bytes changed, a CS that does not match - is interpreted, one
 * instruction at a time, by the reference interpreter. Correctness never
 * depends on how much was translated.
 */
#ifndef F117R_RECOMP_RT_H
#define F117R_RECOMP_RT_H

#include "machine.h"

void recomp_init(machine_t *m);
/* machine_hooks.module_load */
void recomp_module_load(void *user, machine_t *m, const char *name,
                        const uint8_t *file, size_t len, int kind,
                        uint16_t load_seg, uint16_t reloc);
/* Translated code from CS:IP until cpu.stop_at; 0 if none applies here. */
int  recomp_run(machine_t *m);
/* Record the instruction about to be interpreted (coverage / misses). */
void recomp_note_interp(machine_t *m);
/* Write coverage to `path` (appending) as instances are replaced and at
 * shutdown; NULL or "" turns it off. F117R_COVERAGE sets it at init. */
void recomp_set_coverage(machine_t *m, const char *path);
void recomp_shutdown(machine_t *m);
/* Counters: translated vs interpreted instructions, invalidations, misses. */
void recomp_report(machine_t *m, FILE *f);
/* Write every interpreted address inside a registered module (the misses
 * the next recompile should cover) to `path`, appending. */
void recomp_write_misses(machine_t *m, const char *path);

/* ---- code overrides ----------------------------------------------------
 * Hand-written C that replaces the original's code at one address of one
 * module, under either engine: how code fixes attach (docs/bugs.md). An
 * override is registered off. Enabled, every instruction start at its
 * address runs the C instead; a translated region containing the address
 * is refused, so the interpreter, which looks first, reaches it. The C
 * returns 1 having left CS:IP and the clock where the replaced code would
 * have, or 0 to run the original instruction there after all. With none
 * enabled nothing changes: the run loop tests one counter. */
typedef int (*recomp_override_fn)(machine_t *m);
typedef struct {
    const char *id;          /* the switch, e.g. "D5" */
    const char *module;      /* upper-case file name, e.g. "VGAME.EXE" */
    uint64_t    file_hash;   /* FNV-1a 64 of the file as DOS reads it; 0 = any */
    uint16_t    seg, ip;     /* CS = load segment + seg */
    recomp_override_fn fn;
    const char *what;        /* one line, for --list-fixes */
} recomp_override;
/* Register (off); the index, or -1 when the table is full. */
int  recomp_override_add(const recomp_override *o);
/* Switch every override with this id ("all" for every one); how many. */
int  recomp_override_enable(const char *id, int on);
void recomp_override_list(FILE *f);
/* Enabled overrides now placed in loaded modules. */
extern int recomp_overrides_live;
/* At CS:IP: 1 an override ran, -1 one asked for the original instruction,
 * 0 none is placed here. */
int  recomp_override_step(machine_t *m);

#endif
