/* headless.c - run the game without a window, for tests and captures.
 *
 *   f117run --data DIR [--save DIR] [--steps N] [--ips N] [--log FILE]
 *           [--type ICOUNT:KEYS]... [--hold MS] [--screen FILE.ppm]
 *           [--engine interp|recomp] [--time-us N] [--hash-every N]
 *
 * --type types KEYS (see keys.h) starting at ICOUNT, each key held --hold
 * milliseconds of emulated time. ICOUNT may be written "PROG+N": N
 * instructions after the program PROG was last started. The screen is
 * written at the end, as a PPM of the mode-13h frame or the text page.
 * --time-us fixes the wall-clock time the machine boots at, so a run is
 * reproducible to the instruction.
 */
#include "machine.h"
#include "keys.h"
#include "present.h"
#include "recomp_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { uint64_t at; uint8_t b; } timed_byte;

typedef struct {
    char prog[16];          /* empty: absolute icount */
    uint64_t after;
    char keys[256];
    int done;
} type_cmd;

static struct { uint32_t lin; uint16_t cs, ip; uint64_t n; } g_samp[256];
static int g_nsamp;

static timed_byte g_q[8192];
static int g_qn;

static void schedule(uint64_t at, uint8_t b)
{
    if (g_qn < (int)(sizeof g_q / sizeof g_q[0])) { g_q[g_qn].at = at; g_q[g_qn].b = b; g_qn++; }
}

/* Lay a key script out in time: each key pressed for `hold`, the next key
 * `hold` after its release. */
static void schedule_keys(const char *keys, uint64_t start, uint64_t hold)
{
    uint8_t mk[4], br[4];
    int nm, nb;
    uint64_t t = start;
    while (keys_next(&keys, mk, &nm, br, &nb)) {
        for (int i = 0; i < nm; i++) schedule(t, mk[i]);
        for (int i = 0; i < nb; i++) schedule(t + hold, br[i]);
        t += 2 * hold;
    }
}

static int cmp_tb(const void *a, const void *b)
{
    const timed_byte *x = a, *y = b;
    return x->at < y->at ? -1 : x->at > y->at;
}

static uint64_t prog_start(machine_t *m, const char *prog, int *found)
{
    for (int i = m->nproc - 1; i >= 0; i--)
        if (!_stricmp(m->procs[i].name, prog)) { *found = 1; return m->procs[i].start_icount; }
    *found = 0;
    return 0;
}

int main(int argc, char **argv)
{
    const char *data = NULL, *save = NULL, *log_path = NULL, *screen = NULL;
    uint64_t steps = 100000000ull, ips = MACHINE_DEFAULT_IPS, hold_ms = 60;
    uint64_t time_us = 0, hash_every = 0;
    int engine = ENGINE_INTERP;
    static type_cmd cmds[64];
    int ncmd = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--data") && v) { data = v; i++; }
        else if (!strcmp(a, "--save") && v) { save = v; i++; }
        else if (!strcmp(a, "--steps") && v) { steps = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--ips") && v) { ips = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--log") && v) { log_path = v; i++; }
        else if (!strcmp(a, "--screen") && v) { screen = v; i++; }
        else if (!strcmp(a, "--hold") && v) { hold_ms = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--time-us") && v) { time_us = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--hash-every") && v) { hash_every = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--engine") && v) { engine = !strcmp(v, "recomp") ? ENGINE_RECOMP : ENGINE_INTERP; i++; }
        else if (!strcmp(a, "--type") && v && ncmd < 64) {
            type_cmd *t = &cmds[ncmd++];
            const char *colon = strchr(v, ':');
            if (!colon) { fprintf(stderr, "--type wants ICOUNT:KEYS\n"); return 2; }
            char when[64];
            snprintf(when, sizeof when, "%.*s", (int)(colon - v), v);
            char *plus = strchr(when, '+');
            if (plus) { *plus = 0; snprintf(t->prog, sizeof t->prog, "%s", when); t->after = strtoull(plus + 1, NULL, 0); }
            else t->after = strtoull(when, NULL, 0);
            snprintf(t->keys, sizeof t->keys, "%s", colon + 1);
            i++;
        } else { fprintf(stderr, "unknown or incomplete option %s\n", a); return 2; }
    }
    if (!data) { fprintf(stderr, "usage: f117run --data DIR [options]\n"); return 2; }
    if (!time_us) time_us = (uint64_t)time(NULL) * 1000000ull;

    static machine_t m;
    uint8_t *mem = (uint8_t *)malloc(MEM_SIZE);
    m.log = log_path ? fopen(log_path, "w") : stdout;
    m.engine = engine;
    recomp_init(&m);
    machine_hooks hooks;
    memset(&hooks, 0, sizeof hooks);
    hooks.module_load = recomp_module_load;
    if (!machine_boot(&m, mem, data, save, "F117.COM", ips, time_us, &hooks)) {
        fprintf(stderr, "%s\n", m.fault);
        return 1;
    }

    const uint64_t hold = ips * hold_ms / 1000ull;
    int qpos = 0;
    uint64_t next_hash = hash_every ? hash_every : ~0ull;
    clock_t t0 = clock();
    int rc = RUN_SLICE;
    while (m.cpu.icount < steps) {
        /* Commands whose program has started become timed bytes. */
        for (int k = 0; k < ncmd; k++) {
            type_cmd *t = &cmds[k];
            if (t->done) continue;
            uint64_t base = 0;
            if (t->prog[0]) {
                int found;
                base = prog_start(&m, t->prog, &found);
                if (!found) continue;
            }
            if (m.cpu.icount >= base + t->after) {
                schedule_keys(t->keys, m.cpu.icount, hold);
                qsort(g_q + qpos, (size_t)(g_qn - qpos), sizeof g_q[0], cmp_tb);
                t->done = 1;
            }
        }
        while (qpos < g_qn && g_q[qpos].at <= m.cpu.icount) machine_key_byte(&m, g_q[qpos++].b);

        uint64_t until = m.cpu.icount + ips / 100;       /* 10 ms slices */
        if (qpos < g_qn && g_q[qpos].at < until) until = g_q[qpos].at;
        if (until > steps) until = steps;
        if (next_hash < until) until = next_hash;
        if (until <= m.cpu.icount) until = m.cpu.icount + 1;
        rc = machine_run(&m, until);
        {
            /* A cheap PC sampler: where each slice ended. Enough to locate a
             * spin without tracing. */
            uint32_t lin = phys(m.cpu.seg[S_CS], m.cpu.ip);
            int slot = -1;
            for (int s = 0; s < g_nsamp; s++) if (g_samp[s].lin == lin) { slot = s; break; }
            if (slot < 0 && g_nsamp < 256) { slot = g_nsamp++; g_samp[slot].lin = lin;
                g_samp[slot].cs = m.cpu.seg[S_CS]; g_samp[slot].ip = m.cpu.ip; g_samp[slot].n = 0; }
            if (slot >= 0) g_samp[slot].n++;
        }
        if (m.cpu.icount >= next_hash) {
            uint64_t h = 1469598103934665603ull;
            for (uint32_t a = 0; a < MEM_SIZE; a++) h = (h ^ mem[a]) * 1099511628211ull;
            for (int r = 0; r < 8; r++) h = (h ^ m.cpu.r[r]) * 1099511628211ull;
            printf("[hash] %llu %016llx %s\n", (unsigned long long)m.cpu.icount,
                   (unsigned long long)h, dos_current_program(&m));
            next_hash += hash_every;
        }
        if (rc != RUN_SLICE) break;
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("stopped at icount %llu (%s) after %.1f s host time, %.1f M instr/s; "
           "interpreted %llu; program %s; exit %s\n",
           (unsigned long long)m.cpu.icount, rc == RUN_FAULT ? m.fault : rc == RUN_EXITED ? "exited" : "budget",
           secs, secs > 0 ? (double)m.cpu.icount / secs / 1e6 : 0.0,
           (unsigned long long)m.interp_steps, dos_current_program(&m),
           m.exited ? "yes" : "no");
    recomp_report(&m, stdout);
    for (int pass = 0; pass < 8 && g_nsamp; pass++) {
        int best = 0;
        for (int k = 1; k < g_nsamp; k++) if (g_samp[k].n > g_samp[best].n) best = k;
        if (!g_samp[best].n) break;
        printf("[sample] %04X:%04X (%05X) x%llu\n", g_samp[best].cs, g_samp[best].ip,
               g_samp[best].lin, (unsigned long long)g_samp[best].n);
        g_samp[best].n = 0;
    }
    if (screen) present_write_ppm(&m, screen);
    machine_shutdown(&m);
    return rc == RUN_FAULT ? 1 : 0;
}
