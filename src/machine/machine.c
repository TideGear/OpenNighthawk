/* machine.c - building and tearing down the emulated PC. */
#include "machine.h"

#include <stdlib.h>
#include <string.h>

int machine_boot(machine_t *m, uint8_t *mem, const char *data_dir,
                 const char *save_dir, const char *program, uint64_t ips,
                 uint64_t boot_time_us, const machine_hooks *hooks)
{
    FILE *log = m->log;
    int engine = m->engine;
    void *recomp = m->recomp;
    memset(m, 0, sizeof *m);
    m->log = log;
    m->engine = engine;
    m->recomp = recomp;
    m->mem = mem;
    memset(mem, 0, MEM_SIZE);
    m->ips = ips ? ips : MACHINE_DEFAULT_IPS;
    m->boot_time_us = boot_time_us;
    if (hooks) m->hooks = *hooks;
    snprintf(m->data_dir, sizeof m->data_dir, "%s", data_dir ? data_dir : ".");
    snprintf(m->save_dir, sizeof m->save_dir, "%s", save_dir ? save_dir : "");

    cpu_init(&m->cpu, mem);
    m->cpu.model = CPU_80286;
    m->cpu.user = m;
    m->cpu.int_hook = dos_int_hook;
    m->cpu.io_read = pc_io_read;
    m->cpu.io_write = pc_io_write;

    pc_reset(m);
    return dos_boot(m, program);
}

void machine_shutdown(machine_t *m)
{
    dos_shutdown(m);
}
