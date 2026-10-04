/* machine.c - building and tearing down the emulated PC. */
#include "machine.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

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

uint64_t machine_state_hash(const machine_t *m)
{
    uint64_t h = 1469598103934665603ull;
    for (uint32_t a = 0; a < MEM_SIZE; a++) h = (h ^ m->mem[a]) * 1099511628211ull;
    for (int r = 0; r < 8; r++) h = (h ^ m->cpu.r[r]) * 1099511628211ull;
    for (int s = 0; s < 4; s++) h = (h ^ m->cpu.seg[s]) * 1099511628211ull;
    h = (h ^ m->cpu.ip) * 1099511628211ull;
    h = (h ^ m->cpu.flags) * 1099511628211ull;
    h = (h ^ m->io_hash) * 1099511628211ull;
    return h;
}

uint64_t machine_local_time_us(void)
{
    time_t now = time(NULL);
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    /* Re-encode the local broken-down time as a UTC count. */
    int y = lt.tm_year + 1900, mo = lt.tm_mon + 1;
    /* days from 1970-01-01 (civil-from-days inverse, Howard Hinnant) */
    y -= mo <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (unsigned)(mo + (mo > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)lt.tm_mday - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    const long long days = (long long)era * 146097 + (long long)doe - 719468;
    const long long secs = days * 86400 + lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
    return (uint64_t)secs * 1000000ull;
}
