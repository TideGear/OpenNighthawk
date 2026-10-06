/* matched.h - Phase 2's hand-written equivalents (see matched.c). */
#ifndef F117R_MATCHED_H
#define F117R_MATCHED_H

#include "recomp_rt.h"

/* The shipped programs, by file hash (FNV-1a 64 of the file as DOS reads it). */
#define VGAME_47304 0x8287450CCA85106FULL
#define START_47304 0xC65ECC83823E4907ULL
#define END_47304   0xFA7167EE4E377EC1ULL
#define PLAYER_47304 0xF61A7BE2C4607B24ULL
#define DSWAP_47304 0xF947E1BD62AA2812ULL
#define SETUP_47304 0xEDD5021CF28E7FC4ULL
#define MPS_LOGO_47304 0x02B05A7CAB0B4197ULL


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
