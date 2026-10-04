/* recomp_none.c - the recomp runtime for a build with no generated
 * translation: every instruction is interpreted. */
#include "recomp_rt.h"

uint8_t cpu_codebits[MEM_SIZE / 8];

void cpu_code_written(cpu_t *c, uint32_t lin) { (void)c; (void)lin; }

void recomp_init(machine_t *m) { (void)m; }

void recomp_module_load(void *user, machine_t *m, const char *name,
                        const uint8_t *file, size_t len, int kind,
                        uint16_t load_seg, uint16_t reloc)
{
    (void)user; (void)m; (void)name; (void)file; (void)len; (void)kind; (void)load_seg; (void)reloc;
}

int recomp_run(machine_t *m) { (void)m; return 0; }

void recomp_report(machine_t *m, FILE *f)
{
    fprintf(f, "[recomp] none built in: %llu instructions interpreted\n",
            (unsigned long long)m->interp_steps);
}

void recomp_write_misses(machine_t *m, const char *path) { (void)m; (void)path; }
