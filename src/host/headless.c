/* headless.c - run the game without a window: tests, captures, parity.
 *
 *   f117run --data DIR [--save DIR] [--steps N] [--ips N] [--log FILE]
 *           [--engine interp|recomp] [--time-us N]
 *           [--type WHEN:KEYS]... [--click WHEN:X,Y]... [--move WHEN:X,Y]... [--hold MS]
 *           [--record FILE] [--replay FILE]
 *           [--hash-every N] [--hash-from N] [--trace FROM:TO:FILE]
 *           [--coverage FILE] [--screen FILE.ppm] [--shots EVERY:PREFIX]
 *           [--fix ID|all]... [--list-fixes]
 *           [--opl-log FILE] [--midi-log FILE]
 *
 * WHEN is an absolute clock count, or PROG+N: N after the program PROG
 * (e.g. START.EXE) first starts. --type types KEYS (escapes in keys.h),
 * each key held --hold ms of emulated time; --click moves the mouse to
 * mode-13h pixel (X,Y) and clicks the left button; --move hovers with buttons released.
 *
 * Every input is scheduled at an exact clock count from inside the machine
 * (a program's start is a machine event), so a run does not depend on how
 * this loop slices it, and --record writes a log that --replay - here or in
 * the windowed game - reproduces to the instruction.
 *
 * Parity: run the same inputs with --engine interp and --engine recomp and
 * compare the [hash] lines (all of memory and the registers); --hash-from
 * and a smaller --hash-every narrow a difference down, and --trace writes
 * every instruction boundary in a window (CS:IP, registers, flags) for a
 * line-by-line diff.
 */
#include "machine.h"
#include "keys.h"
#include "present.h"
#include "recomp_rt.h"
#include "fixes.h"
#include "inputlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    char prog[16];          /* empty: absolute */
    uint64_t after;
    char keys[256];         /* keys, or "@x,y" for mouse position (move_only omits the click) */
    int done;
    int move_only;
} script_cmd;

#define MAX_CMDS 4096
static script_cmd g_cmd[MAX_CMDS];
static int g_ncmd;
static uint64_t g_hold;
static FILE *g_record;

static struct { uint32_t lin; uint16_t cs, ip; uint64_t n; } g_samp[256];
static int g_nsamp;

static void at_key(machine_t *m, uint64_t at, uint8_t b)
{
    machine_input in = { 0 };
    in.at = at; in.type = INPUT_KEY; in.byte = b;
    machine_input_at(m, &in);
}

static void at_mouse(machine_t *m, uint64_t at, int x, int y, int buttons)
{
    machine_input in = { 0 };
    in.at = at; in.type = INPUT_MOUSE; in.x = (int16_t)x; in.y = (int16_t)y;
    in.buttons = (uint16_t)buttons;
    machine_input_at(m, &in);
}

/* Lay a command out in time from `t`: each key held for g_hold, the next
 * key g_hold after its release; a click presses g_hold after the pointer
 * arrives and releases 2*g_hold later. */
static void schedule(machine_t *m, script_cmd *cmd, uint64_t t)
{
    if (cmd->keys[0] == '@') {
        int x = 0, y = 0;
        sscanf(cmd->keys + 1, "%d,%d", &x, &y);
        at_mouse(m, t, x, y, 0);
        if (!cmd->move_only) {
            at_mouse(m, t + g_hold, x, y, 1);
            at_mouse(m, t + 3 * g_hold, x, y, 0);
        }
    } else {
        /* "~MS:KEYS" holds each of these keys MS milliseconds. */
        const char *k = cmd->keys;
        uint64_t hold = g_hold;
        if (k[0] == '~') {
            hold = strtoull(k + 1, NULL, 10) * (m->ips / 1000u);
            const char *colon = strchr(k, ':');
            k = colon ? colon + 1 : k + strlen(k);
        }
        uint8_t mk[4], br[4];
        int nm, nb;
        while (keys_next(&k, mk, &nm, br, &nb)) {
            for (int i = 0; i < nm; i++) at_key(m, t, mk[i]);
            for (int i = 0; i < nb; i++) at_key(m, t + hold, br[i]);
            t += hold + g_hold;
        }
    }
    cmd->done = 1;
}

/* The program-start event: schedule this program's commands now, at their
 * exact counts. Then hand on to the recomp run-time. */
static void on_load(void *user, machine_t *m, const char *name, const uint8_t *file,
                    size_t len, int kind, uint16_t load_seg, uint16_t reloc)
{
    recomp_module_load(user, m, name, file, len, kind, load_seg, reloc);
    if (kind == MODLOAD_OVERLAY) return;
    for (int k = 0; k < g_ncmd; k++) {
        script_cmd *c = &g_cmd[k];
        if (!c->done && c->prog[0] && !_stricmp(c->prog, name))
            schedule(m, c, m->cpu.icount + c->after);
    }
}

/* --opl-log: every OPL register write with its clock, for comparing the
 * music against a DOSBox raw OPL capture (tools/dosbox_compare.py). */
static FILE *g_opl_log;
static FILE *g_midi_log;
static void on_midi(void *user, uint64_t icount, uint8_t byte)
{
    (void)user;
    if (g_midi_log) fprintf(g_midi_log, "%llu %02X\n", (unsigned long long)icount, byte);
}
static void on_opl(void *user, uint64_t icount, uint8_t reg, uint8_t val)
{
    (void)user;
    if (g_opl_log) fprintf(g_opl_log, "%llu %02X %02X\n", (unsigned long long)icount, reg, val);
}

static void on_input(void *user, const machine_input *in)
{
    (void)user;
    if (g_record) inputlog_write(g_record, in);
}

static uint64_t state_hash(const machine_t *m)
{
    return machine_state_hash(m);
}

int main(int argc, char **argv)
{
    const char *data = NULL, *save = NULL, *log_path = NULL, *screen = NULL;
    const char *record = NULL, *replay = NULL, *coverage = NULL, *opl_log = NULL;
    const char *midi_log = NULL;
    uint64_t steps = 100000000ull, ips = MACHINE_DEFAULT_IPS, hold_ms = 60;
    uint64_t time_us = 0, hash_every = 0, hash_from = 0;
    uint64_t trace_from = 0, trace_to = 0;
    static char trace_path[600];
    int engine = ENGINE_INTERP;
    uint64_t shot_every = 0, next_shot = 0;
    static char shot_prefix[512] = "shot";

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
        else if (!strcmp(a, "--hash-from") && v) { hash_from = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--coverage") && v) { coverage = v; i++; }
        else if (!strcmp(a, "--opl-log") && v) { opl_log = v; i++; }
        else if (!strcmp(a, "--midi-log") && v) { midi_log = v; i++; }
        else if (!strcmp(a, "--record") && v) { record = v; i++; }
        else if (!strcmp(a, "--replay") && v) { replay = v; i++; }
        else if (!strcmp(a, "--trace") && v) {
            char rest[600];
            unsigned long long f = 0, t = 0;
            if (sscanf(v, "%llu:%llu:%599s", &f, &t, rest) != 3) { fprintf(stderr, "--trace FROM:TO:FILE\n"); return 2; }
            trace_from = f; trace_to = t;
            snprintf(trace_path, sizeof trace_path, "%s", strchr(strchr(v, ':') + 1, ':') + 1);
            i++;
        }
        else if (!strcmp(a, "--shots") && v) {
            shot_every = strtoull(v, NULL, 0);
            const char *colon = strchr(v, ':');
            if (colon) snprintf(shot_prefix, sizeof shot_prefix, "%s", colon + 1);
            i++;
        }
        else if (!strcmp(a, "--engine") && v) { engine = !strcmp(v, "recomp") ? ENGINE_RECOMP : ENGINE_INTERP; i++; }
        else if (!strcmp(a, "--fix") && v) {
            if (!fixes_enable(v, 1)) { fprintf(stderr, "no fix %s (--list-fixes)\n", v); return 2; }
            i++;
        }
        else if (!strcmp(a, "--list-fixes")) { fixes_list(stdout); return 0; }
        else if ((!strcmp(a, "--type") || !strcmp(a, "--click") || !strcmp(a, "--move")) && v && g_ncmd < MAX_CMDS) {
            script_cmd *t = &g_cmd[g_ncmd++];
            const char *colon = strchr(v, ':');
            if (!colon) { fprintf(stderr, "%s wants WHEN:WHAT\n", a); return 2; }
            char when[64];
            snprintf(when, sizeof when, "%.*s", (int)(colon - v), v);
            char *plus = strchr(when, '+');
            if (plus) { *plus = 0; snprintf(t->prog, sizeof t->prog, "%s", when); t->after = strtoull(plus + 1, NULL, 0); }
            else t->after = strtoull(when, NULL, 0);
            t->move_only = !strcmp(a, "--move");
            snprintf(t->keys, sizeof t->keys, "%s%s", (!strcmp(a, "--click") || t->move_only) ? "@" : "", colon + 1);
            i++;
        } else { fprintf(stderr, "unknown or incomplete option %s\n", a); return 2; }
    }
    if (!data) { fprintf(stderr, "usage: f117run --data DIR [options]\n"); return 2; }
    if (replay) {
        inputlog_read_header(replay, &ips, &time_us);
        char ids[256];
        inputlog_read_fixes(replay, ids, sizeof ids);
        if (!fixes_enable_list(ids)) { fprintf(stderr, "%s names an unknown fix: %s\n", replay, ids); return 2; }
    }
    if (!time_us) time_us = machine_local_time_us();

    static machine_t m;
    uint8_t *mem = (uint8_t *)malloc(MEM_SIZE);
    m.log = log_path ? fopen(log_path, "w") : stdout;
    m.engine = engine;
    recomp_init(&m);
    if (coverage) recomp_set_coverage(&m, coverage);
    machine_hooks hooks;
    memset(&hooks, 0, sizeof hooks);
    hooks.module_load = on_load;
    hooks.file_data = fixes_file_data;
    if (opl_log) {
        g_opl_log = fopen(opl_log, "w");
        if (!g_opl_log) { fprintf(stderr, "cannot write %s\n", opl_log); return 1; }
        hooks.opl_write = on_opl;
    }
    if (midi_log) {
        g_midi_log = fopen(midi_log, "w");
        if (!g_midi_log) { fprintf(stderr, "cannot write %s\n", midi_log); return 1; }
        hooks.midi_byte = on_midi;
    }
    g_hold = ips * hold_ms / 1000ull;
    if (record) {
        g_record = fopen(record, "w");
        if (!g_record) { fprintf(stderr, "cannot write %s\n", record); return 1; }
        inputlog_header(g_record, ips, time_us);
        char ids[256];
        fixes_enabled(ids, sizeof ids);
        inputlog_fixes(g_record, ids);
    }
    if (!machine_boot(&m, mem, data, save, "F117.COM", ips, time_us, &hooks)) {
        fprintf(stderr, "%s\n", m.fault);
        return 1;
    }
    m.on_input = on_input;
    for (int k = 0; k < g_ncmd; k++)
        if (!g_cmd[k].prog[0]) schedule(&m, &g_cmd[k], g_cmd[k].after);
    inputlog_reader *player = NULL;
    if (replay && !(player = inputlog_open(replay))) { fprintf(stderr, "cannot read %s\n", replay); return 1; }

    FILE *trace = NULL;
    uint64_t next_hash = hash_every ? (hash_from ? hash_from : hash_every) : ~0ull;
    clock_t t0 = clock();
    int rc = RUN_SLICE;
    while (m.cpu.icount < steps) {
        uint64_t until = m.cpu.icount + ips / 100;       /* 10 ms slices */
        if (until > steps) until = steps;
        if (next_hash < until) until = next_hash;
        if (shot_every && next_shot > m.cpu.icount && next_shot < until) until = next_shot;
        if (trace_path[0] && m.cpu.icount < trace_to) {
            /* Inside the trace window: one instruction boundary at a time. */
            if (m.cpu.icount >= trace_from) {
                until = m.cpu.icount + 1;
                if (!trace) trace = fopen(trace_path, "w");
            } else if (trace_from < until) {
                until = trace_from;
            }
        }
        if (until <= m.cpu.icount) until = m.cpu.icount + 1;
        /* Queue replayed inputs well before their time: one emulated
         * second past the slice's end. */
        inputlog_feed(player, &m, until + ips);
        rc = machine_run(&m, until);
        if (trace && m.cpu.icount <= trace_to) {
            const cpu_t *c = &m.cpu;
            fprintf(trace, "%llu %04X:%04X AX=%04X BX=%04X CX=%04X DX=%04X SI=%04X DI=%04X BP=%04X SP=%04X "
                    "DS=%04X ES=%04X SS=%04X FL=%04X\n", (unsigned long long)c->icount,
                    c->seg[S_CS], c->ip, c->r[R_AX], c->r[R_BX], c->r[R_CX], c->r[R_DX],
                    c->r[R_SI], c->r[R_DI], c->r[R_BP], c->r[R_SP], c->seg[S_DS], c->seg[S_ES],
                    c->seg[S_SS], c->flags);
        }
        {
            uint32_t lin = phys(m.cpu.seg[S_CS], m.cpu.ip);
            int slot = -1;
            for (int s = 0; s < g_nsamp; s++) if (g_samp[s].lin == lin) { slot = s; break; }
            if (slot < 0 && g_nsamp < 256) { slot = g_nsamp++; g_samp[slot].lin = lin;
                g_samp[slot].cs = m.cpu.seg[S_CS]; g_samp[slot].ip = m.cpu.ip; g_samp[slot].n = 0; }
            if (slot >= 0) g_samp[slot].n++;
        }
        if (shot_every && m.cpu.icount >= next_shot) {
            char path[600];
            snprintf(path, sizeof path, "%s_%011llu.ppm", shot_prefix, (unsigned long long)m.cpu.icount);
            present_write_ppm(&m, path);
            /* Keep the capture clock periodic: an instruction finishing
             * just past the deadline must not shift every later sample. */
            next_shot += shot_every;
        }
        if (m.cpu.icount >= next_hash) {
            printf("[hash] %llu %016llx %s\n", (unsigned long long)m.cpu.icount,
                   (unsigned long long)state_hash(&m), dos_current_program(&m));
            next_hash += hash_every;
        }
        if (rc != RUN_SLICE) break;
    }
    if (trace) fclose(trace);
    if (g_record) fclose(g_record);
    if (g_opl_log) fclose(g_opl_log);
    if (g_midi_log) fclose(g_midi_log);
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("stopped at icount %llu (%s) after %.1f s host time, %.1f M instr/s; "
           "interpreted %llu; program %s; exit %s; final hash %016llx\n",
           (unsigned long long)m.cpu.icount, rc == RUN_FAULT ? m.fault : rc == RUN_EXITED ? "exited" : "budget",
           secs, secs > 0 ? (double)m.cpu.icount / secs / 1e6 : 0.0,
           (unsigned long long)m.interp_steps, dos_current_program(&m),
           m.exited ? "yes" : "no", (unsigned long long)state_hash(&m));
    printf("[devices] %llu OPL writes, %llu MIDI bytes, %llu speaker changes\n",
           (unsigned long long)m.opl_writes, (unsigned long long)m.midi_bytes,
           (unsigned long long)m.speaker_changes);
    recomp_report(&m, stdout);
    for (int pass = 0; pass < 4 && g_nsamp; pass++) {
        int best = 0;
        for (int k = 1; k < g_nsamp; k++) if (g_samp[k].n > g_samp[best].n) best = k;
        if (!g_samp[best].n) break;
        printf("[sample] %04X:%04X (%05X) x%llu\n", g_samp[best].cs, g_samp[best].ip,
               g_samp[best].lin, (unsigned long long)g_samp[best].n);
        g_samp[best].n = 0;
    }
    if (screen) present_write_ppm(&m, screen);
    recomp_shutdown(&m);
    machine_shutdown(&m);
    return rc == RUN_FAULT ? 1 : 0;
}
