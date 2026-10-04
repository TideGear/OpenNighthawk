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
/* Counters: translated vs interpreted instructions, invalidations, misses. */
void recomp_report(machine_t *m, FILE *f);
/* Write every interpreted address inside a registered module (the misses
 * the next recompile should cover) to `path`, appending. */
void recomp_write_misses(machine_t *m, const char *path);

#endif
