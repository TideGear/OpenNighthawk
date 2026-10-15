/* rerun_probe.h - EXPERIMENT (branch draw-rerun-probe, not for master).
 *
 * F117_RERUN_PROBE=FILE: feasibility of drawing in-between frames by re-running the original's own
 * view composition (VGAME 2F9D) on a throwaway copy of the machine. At every 2F9D entry the machine
 * is snapshotted; between run slices, for each logic frame k whose successor's records are complete:
 *   1. the copy of frame k runs 2F9D to its return unchanged: its draw records must equal the start
 *      of what the real game recorded from that entry (determinism and isolation of the copy);
 *   2. the copy runs again with the aircraft position and attitude halfway to frame k+1's: its
 *      projected vertices are compared with the midpoint of frames k and k+1 (does nudging work);
 *   3. the real machine's memory is hashed before and after (the copy never touches it).
 * Costs (instructions, host microseconds) and verdicts are written to FILE. */
#ifndef F117R_RERUN_PROBE_H
#define F117R_RERUN_PROBE_H

#include "machine.h"

int  rerun_probe_start(machine_t *m, const char *path);   /* installs the hook and its observer */
void rerun_probe_service(machine_t *m);                    /* between slices: process ready frames */
void rerun_probe_finish(void);                             /* summary */

#endif
