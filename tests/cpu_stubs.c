/* cpu_stubs.c - the two hooks the CPU core expects from the machine and the
 * recomp run-time, as no-ops, for tests that run the core on its own. */
#include "cpu.h"

uint8_t cpu_codebits[MEM_SIZE / 8];
void cpu_code_written(cpu_t *c, uint32_t lin) { (void)c; (void)lin; }
void cpu_irq_state_changed(cpu_t *c) { (void)c; }
