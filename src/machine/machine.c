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
    free(m->opl_sched);
    memset(m, 0, sizeof *m);
    m->log = log;
    m->engine = engine;
    m->recomp = recomp;
    m->mem = mem;
    memset(mem, 0, MEM_SIZE);
    m->ips = ips ? ips : MACHINE_DEFAULT_IPS;
    m->boot_time_us = boot_time_us;
    { const char *e = getenv("F117R_PIT_CONTROL_IRQ"); m->pit_control_irq = !(e && e[0] == '0'); }
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

/* ---- the inventory (F117R_INVENTORY=FILE): which services and ports the
 * programs use, counted, for the machine-fidelity probes. Counting only. */

static uint32_t g_inv_port[0x10000][2];
static struct { uint32_t key, n; } g_inv_svc[4096];
static int g_inv_on = -1;

static int inv_on(void)
{
    if (g_inv_on < 0) { const char *e = getenv("F117R_INVENTORY"); g_inv_on = e && *e; }
    return g_inv_on;
}

void machine_inventory_port(uint16_t port, int write)
{
    if (inv_on()) g_inv_port[port][write ? 1 : 0]++;
}

void machine_inventory_service(uint8_t vec, uint16_t ax)
{
    if (!inv_on()) return;
    const uint32_t key = ((uint32_t)vec << 16) | ax | 0x80000000u;
    uint32_t h = (key * 2654435761u) & 4095u;
    while (g_inv_svc[h].key && g_inv_svc[h].key != key) h = (h + 1) & 4095u;
    g_inv_svc[h].key = key;
    g_inv_svc[h].n++;
}

static void inventory_write(void)
{
    if (!inv_on()) return;
    FILE *f = fopen(getenv("F117R_INVENTORY"), "a");
    if (!f) return;
    for (int i = 0; i < 4096; i++)
        if (g_inv_svc[i].key)
            fprintf(f, "int %02X ax %04X %u\n", (g_inv_svc[i].key >> 16) & 0xFF,
                    g_inv_svc[i].key & 0xFFFF, g_inv_svc[i].n);
    for (int p = 0; p < 0x10000; p++)
        for (int w = 0; w < 2; w++)
            if (g_inv_port[p][w]) fprintf(f, "port %04X %s %u\n", p, w ? "out" : "in", g_inv_port[p][w]);
    fclose(f);
}

void machine_shutdown(machine_t *m)
{
    inventory_write();
    dos_shutdown(m);
    free(m->opl_sched);
    m->opl_sched = NULL;
    m->opl_sched_n = m->opl_sched_i = m->opl_sched_cap = 0;
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
    for (size_t i = 0; i < sizeof m->dac_display; i++) h = (h ^ m->dac_display[i]) * 1099511628211ull;
    /* Scanout can differ even when current VRAM is identical: include the
     * in-progress and completed display state in engine parity checks. */
    for (size_t i = 0; i < sizeof m->scan_work; i++) h = (h ^ m->scan_work[i]) * 1099511628211ull;
    for (size_t i = 0; i < sizeof m->scan_pixels; i++) h = (h ^ m->scan_pixels[i]) * 1099511628211ull;
    for (size_t i = 0; i < sizeof m->scan_dac; i++) h = (h ^ m->scan_dac[i]) * 1099511628211ull;
    h = (h ^ m->scan_next) * 1099511628211ull;
    h = (h ^ m->scan_frame) * 1099511628211ull;
    h = (h ^ m->scan_time) * 1099511628211ull;
    h = (h ^ m->scan_start) * 1099511628211ull;
    h = (h ^ m->scan_latch) * 1099511628211ull;
    h = (h ^ m->scan_part) * 1099511628211ull;
    h = (h ^ m->scan_valid) * 1099511628211ull;
    h = (h ^ m->scan_mask) * 1099511628211ull;
    h = (h ^ m->scan_blank) * 1099511628211ull;
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
