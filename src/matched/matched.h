/* matched.h - Phase 2's hand-written equivalents (see matched.c). */
#ifndef F117R_MATCHED_H
#define F117R_MATCHED_H

#include "recomp_rt.h"

/* Register every matched routine (once; recomp_init calls it). */
void matched_register(void);
unsigned matched_count(void);
const recomp_override *matched_entry(unsigned i);

/* How a matched routine's call into original code is run: until the
 * machine's trap (the call's return) fires. The default is machine_run up
 * to the current run's limit; tests/func_lockstep.c installs a stepper.
 * Returns RUN_TRAP when the call returned. */
typedef int (*matched_runner_fn)(machine_t *m);
extern matched_runner_fn matched_runner;

#endif
