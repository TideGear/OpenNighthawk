/* headless.c - run the game without a window: tests, captures, parity.
 *
 *   f117run --data DIR [--save DIR] [--steps N] [--ips N] [--log FILE]
 *           [--engine interp|recomp] [--time-us N]
 *           [--type WHEN:KEYS]... [--click WHEN:X,Y]... [--move WHEN:X,Y]... [--hold MS]
 *           [--record FILE] [--replay FILE]
 *           [--hash-every N] [--hash-from N] [--peek LINEAR] [--dump LINEAR:LENGTH] [--observe FILE:FROM:TO] [--trace FROM:TO:FILE]
 *           [--coverage FILE] [--screen FILE.ppm] [--shots EVERY:PREFIX | --shots-vga PREFIX] [--shots-start CLOCK] [--shot-meta FILE]
 *           [--shots-changed] [--frame-log FILE] [--no-mouse] [--present scan|replay|interp]
 *           [--present-scale N] [--present-age interp|extrapolate]
 *           [--fix ID|all]... [--list-fixes]
 *           [--opl-log FILE] [--midi-log FILE] [--speaker-log FILE]
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
 *
 * --present replay takes every picture (--screen, the shots) from the Stage 1
 * replay of an in-process draw feed (src/present/drawfeed.h) rather than from
 * the scanned-out VGA, and checks it: each logic frame against the display
 * at its close, and at every VGA frame the presented picture against the
 * scanned-out one ([present] at the end). The machine runs as without it.
 * --present interp shows the in-between frames of the last two logic frames
 * instead (Stage 3), and checks every pair drawn at its two ends;
 * --present-age extrapolate predicts past the newer frame instead of trailing
 * it. --present-scale N draws the presented picture N times finer (Stage 2):
 * the shots and --screen are then 320N x 200N.
 */
#include "machine.h"
#include "keys.h"
#include "present.h"
#include "observe.h"
#include "recomp_rt.h"
#include "fixes.h"
#include "inputlog.h"
#include "drawfeed.h"

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
/* --observe FILE:FROM:TO: log what the original draws (matched/observe.c) for
 * the instructions in [FROM, TO): "P icount" for each phase of a picture and
 * "V icount x y z px py range" for each projected vertex. Recompiled engine
 * only; it reads and changes nothing. */
static FILE *g_obs_file;
static uint64_t g_obs_from, g_obs_to;
static void obs_phase(void *u, uint64_t icount)
{
    (void)u;
    if (icount >= g_obs_from && icount < g_obs_to) fprintf(g_obs_file, "P %llu\n", (unsigned long long)icount);
}
static void obs_vertex(void *u, uint64_t icount, const int32_t xf[3], const int32_t px[2], int range,
                       uint16_t xf_at, uint16_t px_at)
{
    (void)u;
    if (icount >= g_obs_from && icount < g_obs_to)
        fprintf(g_obs_file, "V %llu %d %d %d %d %d %d %04X %04X\n", (unsigned long long)icount, xf[0], xf[1], xf[2], px[0], px[1], range,
                xf_at, px_at);
}
static void obs_prim(void *u, uint64_t icount, char kind, const int32_t *v, int n)
{
    (void)u;
    if (icount < g_obs_from || icount >= g_obs_to) return;
    fprintf(g_obs_file, "%c %llu", kind, (unsigned long long)icount);
    for (int k = 0; k < n; k++) fprintf(g_obs_file, " %d", v[k]);
    fputc(10, g_obs_file);
}
static const f117_observer g_obs = { 0, obs_phase, obs_vertex, obs_prim };

static FILE *g_opl_log;
static FILE *g_midi_log;
static FILE *g_speaker_log;
static machine_t *g_speaker_machine;
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
static void on_speaker(void *user, uint64_t icount)
{
    (void)user;
    if (g_speaker_log) {
        uint16_t reload;
        int mode;
        unsigned port61 = (unsigned)pc_speaker_state(g_speaker_machine, &reload, &mode);
        /* the last field tells a control word (the counter waits for a count) from a count */
        fprintf(g_speaker_log, "%llu %02X %u %d %llu %u\n",
                (unsigned long long)icount, port61, (unsigned)reload, mode,
                (unsigned long long)g_speaker_machine->pit[2].epoch_clk,
                (unsigned)g_speaker_machine->pit[2].null_count);
    }
}

static void on_input(void *user, const machine_input *in)
{
    (void)user;
    if (g_record) inputlog_write(g_record, in);
}

static int capture(const machine_t *m, present_frame *f);

/* --shots-changed: a shot is written only when its picture differs from the
 * last one written, so a long capture keeps each picture once, named by the
 * clock at which it was first sampled. Returns whether it wrote. */
static int shot_write_changed(const machine_t *m, const char *path)
{
    static present_frame f;
    static uint32_t buf[640 * 400], last[640 * 400];
    static int last_w, last_h;
    int w, h;
    capture(m, &f);
    present_render(&f, buf, &w, &h, 0);
    if (w == last_w && h == last_h && !memcmp(buf, last, (size_t)w * h * sizeof buf[0])) return 0;
    memcpy(last, buf, (size_t)w * h * sizeof buf[0]);
    last_w = w; last_h = h;
    FILE *o = fopen(path, "wb");
    if (!o) return 0;
    fprintf(o, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint8_t p[3] = { (uint8_t)(buf[i] >> 16), (uint8_t)(buf[i] >> 8), (uint8_t)buf[i] };
        fwrite(p, 1, 3, o);
    }
    fclose(o);
    return 1;
}

/* --present replay (see the top). At each VGA frame the presented picture is
 * the replay of the last logic frame closed, the scanned-out one what the VGA
 * read. They are equal, or the scan already shows the next frame (the replay
 * presents a frame at its close, the first phase after its present), or the
 * scan caught the original drawing on the display: the next frame decides. */
static int g_replay;                       /* 1 replay, 2 interp */
static int g_scale, g_age;                 /* --present-scale, --present-age (1 extrapolate) */
static drawfeed g_feed;
static drawlive g_live;
static struct {
    uint64_t vsyncs, presented, equal, next, mid, unsettled, frames_seen, seen_seq;
    uint64_t pend_seq;
    int npend;
    uint64_t pend[64];                     /* hashes of the scans that differed, for the next frame */
} g_pc;

static uint64_t picture_hash(const uint8_t *p)
{
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < 64000; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

/* The screen as presented: the scanned-out frame, or with --present replay
 * the replayed picture in its place (the palette is the scan's). */
static int capture(const machine_t *m, present_frame *f)
{
    present_capture(m, f);
    g_live.shown_hi = NULL;
    if (!g_replay) return 0;
    drawlive_update(&g_live, &g_feed);
    return g_replay == 2 ? drawlive_present_interp(&g_live, m, f) : drawlive_present(&g_live, m, f);
}

/* A shot: the finer picture when one was presented, with the frame's palette. */
static void write_shot(const machine_t *m, const char *path)
{
    static present_frame f;
    capture(m, &f);
    if (!g_live.shown_hi) { present_frame_write_ppm(&f, path); return; }
    const int n = g_scale, w = 320 * n, h = 200 * n;
    FILE *o = fopen(path, "wb");
    if (!o) return;
    fprintf(o, "P6\n%d %d\n255\n", w, h);
    static uint8_t row[3 * 320 * 16];
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t *c = &f.dac[3 * g_live.shown_hi[(size_t)y * w + x]];
            for (int k = 0; k < 3; k++) row[3 * x + k] = (uint8_t)((c[k] << 2) | (c[k] >> 4));
        }
        fwrite(row, 3, (size_t)w, o);
    }
    fclose(o);
}

static void on_vsync_check(void *user, uint64_t icount)
{
    (void)user; (void)icount;
    const machine_t *m = g_speaker_machine;
    static present_frame f;
    drawlive_update(&g_live, &g_feed);
    present_capture(m, &f);
    g_pc.vsyncs++;
    if (g_replay == 2) {                    /* interpolation: the pictures are drawn, not compared */
        g_pc.presented += (uint64_t)drawlive_present_interp(&g_live, m, &f);
        return;
    }
    /* the scans that differed from the frame before: the next frame, or a drawing */
    if (g_pc.npend && g_live.picture_seq != g_pc.pend_seq) {
        const int next = g_live.shown && g_live.picture_seq == g_pc.pend_seq + 1;
        const uint64_t h = picture_hash(g_live.picture);
        for (int k = 0; k < g_pc.npend; k++) {
            if (!next) g_pc.unsettled++;
            else if (g_pc.pend[k] == h) g_pc.next++;
            else g_pc.mid++;
        }
        g_pc.npend = 0;
    }
    if (f.text || !drawlive_current(&g_live, m)) return;
    g_pc.presented++;
    if (!memcmp(f.vram, g_live.picture, 64000)) {
        g_pc.equal++;
        if (!g_pc.frames_seen || g_live.picture_seq != g_pc.seen_seq) g_pc.frames_seen++;
        g_pc.seen_seq = g_live.picture_seq;
    } else if (g_pc.npend < 64) {
        g_pc.pend_seq = g_live.picture_seq;
        g_pc.pend[g_pc.npend++] = picture_hash(f.vram);
    } else g_pc.unsettled++;
}

static uint64_t state_hash(const machine_t *m)
{
    return machine_state_hash(m);
}

int main(int argc, char **argv)
{
    const char *data = NULL, *save = NULL, *log_path = NULL, *screen = NULL;
    const char *record = NULL, *replay = NULL, *coverage = NULL, *opl_log = NULL;
    const char *midi_log = NULL, *speaker_log = NULL;
    const char *shot_meta_path = NULL;
    const char *frame_log_path = NULL;     /* --frame-log FILE: each shot's clock, its picture's scan time, the steps so far, screen-off */
    int shots_changed = 0;
    int no_mouse = 0;                      /* --no-mouse: INT 33h answers as with no driver loaded */
    uint64_t steps = 100000000ull, ips = MACHINE_DEFAULT_IPS, hold_ms = 60, boot_ms = 0;
    uint64_t time_us = 0, hash_every = 0, hash_from = 0;
    uint64_t trace_from = 0, trace_to = 0;
    uint32_t peek_at = 0;                  /* --peek LINEAR (hex): print the word there at every hash line */
    int peek_on = 0;
    uint32_t dump_at = 0, dump_len = 0;    /* --dump LINEAR:LENGTH (hex): the bytes there, at the end of the run */
    static char trace_path[600];
    int engine = ENGINE_INTERP;
    uint64_t shot_every = 0, next_shot = 0, shot_start = 0;
    uint64_t shot_period_whole = 0, shot_period_remainder = 0, shot_period_fraction = 0;
    int shot_vga = 0;
    static char shot_prefix[512] = "shot";

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--data") && v) { data = v; i++; }
        else if (!strcmp(a, "--save") && v) { save = v; i++; }
        else if (!strcmp(a, "--steps") && v) { steps = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--ips") && v) { ips = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--timing") && v) {
            /* 386: the 386DX/33 profile, the clock in its cycles (src/cpu/timing386.h) */
            if (strcmp(v, "386")) { fprintf(stderr, "--timing takes 386\n"); return 2; }
            _putenv_s("F117R_TIMING", "386");
            ips = MACHINE_386_IPS;
            i++;
        }
        else if (!strcmp(a, "--log") && v) { log_path = v; i++; }
        else if (!strcmp(a, "--screen") && v) { screen = v; i++; }
        else if (!strcmp(a, "--hold") && v) { hold_ms = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--time-us") && v) { time_us = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--boot-ms") && v) { boot_ms = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--hash-every") && v) { hash_every = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--peek") && v) { peek_at = strtoul(v, NULL, 16); peek_on = 1; i++; }
        else if (!strcmp(a, "--observe") && v) {
            /* FILE may hold a drive letter's colon: split FROM and TO off the right. */
            char path[600]; unsigned long long f = 0, t = ~0ull;
            snprintf(path, sizeof path, "%s", v);
            char *p2 = strrchr(path, ':'), *p1 = NULL;
            if (p2) { *p2 = 0; p1 = strrchr(path, ':'); }
            if (p2 && p1 && p1 > path + 1) { t = strtoull(p2 + 1, NULL, 0); *p1 = 0; f = strtoull(p1 + 1, NULL, 0); }
            else if (p2) *p2 = ':';                     /* no window: the whole run */
            g_obs_file = fopen(path, "w"); g_obs_from = f; g_obs_to = t;
            if (!g_obs_file) { fprintf(stderr, "cannot write %s\n", path); return 2; }
            observe_set(&g_obs); i++;
        }
        else if (!strcmp(a, "--dump") && v) { dump_at = strtoul(v, NULL, 16); const char *c2 = strchr(v, ':'); dump_len = c2 ? strtoul(c2 + 1, NULL, 16) : 0x100; i++; }
        else if (!strcmp(a, "--hash-from") && v) { hash_from = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--coverage") && v) { coverage = v; i++; }
        else if (!strcmp(a, "--opl-log") && v) { opl_log = v; i++; }
        else if (!strcmp(a, "--midi-log") && v) { midi_log = v; i++; }
        else if (!strcmp(a, "--speaker-log") && v) { speaker_log = v; i++; }
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
        else if (!strcmp(a, "--shots-start") && v) { shot_start = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--shot-meta") && v) { shot_meta_path = v; i++; }
        else if (!strcmp(a, "--shots-changed")) shots_changed = 1;
        else if (!strcmp(a, "--no-mouse")) no_mouse = 1;
        else if (!strcmp(a, "--frame-log") && v) { frame_log_path = v; i++; }
        else if (!strcmp(a, "--present") && v) {
            if (strcmp(v, "replay") && strcmp(v, "scan") && strcmp(v, "interp")) {
                fprintf(stderr, "--present takes scan, replay or interp\n");
                return 2;
            }
            g_replay = !strcmp(v, "replay") ? 1 : !strcmp(v, "interp") ? 2 : 0;
            i++;
        }
        else if (!strcmp(a, "--present-scale") && v) {
            g_scale = atoi(v);
            if (g_scale < 1 || g_scale > 16) { fprintf(stderr, "--present-scale takes 1 to 16\n"); return 2; }
            i++;
        }
        else if (!strcmp(a, "--present-age") && v) {
            if (strcmp(v, "interp") && strcmp(v, "extrapolate")) { fprintf(stderr, "--present-age takes interp or extrapolate\n"); return 2; }
            g_age = !strcmp(v, "extrapolate");
            i++;
        }
        else if (!strcmp(a, "--shots-vga") && v) {
            shot_vga = 1; shot_every = 0;
            snprintf(shot_prefix, sizeof shot_prefix, "%s", v);
            i++;
        }
        else if (!strcmp(a, "--shots") && v) {
            shot_vga = 0;
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
    if (g_replay && g_obs_file) { fprintf(stderr, "--observe and --present replay both observe the draw; choose one\n"); return 2; }
    if (replay) {
        inputlog_read_header(replay, &ips, &time_us);
        char ids[256];
        inputlog_read_fixes(replay, ids, sizeof ids);
        if (!fixes_enable_list(ids)) { fprintf(stderr, "%s names an unknown fix: %s\n", replay, ids); return 2; }
    }
    if (!time_us) time_us = machine_local_time_us();
    const char *timing = getenv("F117R_TIMING");
    if (timing && !strcmp(timing, "386") && ips != MACHINE_386_IPS) {
        fprintf(stderr, "386 timing requires %llu cycles/s; replay/options specify %llu\n",
                (unsigned long long)MACHINE_386_IPS, (unsigned long long)ips);
        return 2;
    }

    static machine_t m;
    g_speaker_machine = &m;
    uint8_t *mem = (uint8_t *)malloc(MEM_SIZE);
    m.log = log_path ? fopen(log_path, "w") : stdout;
    m.engine = engine;
    recomp_init(&m);
    if (coverage) recomp_set_coverage(&m, coverage);
    machine_hooks hooks;
    memset(&hooks, 0, sizeof hooks);
    hooks.module_load = on_load;
    hooks.file_data = fixes_file_data;
    if (g_replay) {
        drawfeed_init(&g_feed, mem);
        drawlive_init(&g_live);
        g_live.interp = g_live.check = g_replay == 2;
        g_live.extrapolate = g_age;
        if (g_scale) drawlive_set_scale(&g_live, g_scale);
        observe_set(&g_feed.obs);
        hooks.vsync = on_vsync_check;
    }
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
    if (speaker_log) {
        g_speaker_log = fopen(speaker_log, "w");
        if (!g_speaker_log) { fprintf(stderr, "cannot write %s\n", speaker_log); return 1; }
        hooks.speaker = on_speaker;
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
    if (no_mouse) m.mouse_present = 0;
    if (boot_ms) { pc_advance_boot(&m, ips * boot_ms / 1000ull); steps += ips * boot_ms / 1000ull; }
    if (shot_every || shot_vga) next_shot = shot_start;
    if (shot_vga) {
        const uint64_t numerator = ips * 359200ull;
        shot_period_whole = numerator / 25175000ull;
        shot_period_remainder = numerator % 25175000ull;
        if (!shot_period_whole) { fprintf(stderr, "--shots-vga requires a positive VGA period\n"); return 2; }
    }
    FILE *shot_meta = shot_meta_path ? fopen(shot_meta_path, "w") : NULL;
    if (shot_meta_path && !shot_meta) { fprintf(stderr, "cannot write %s\n", shot_meta_path); return 1; }
    FILE *frame_log = frame_log_path ? fopen(frame_log_path, "w") : NULL;
    if (frame_log_path && !frame_log) { fprintf(stderr, "cannot write %s\n", frame_log_path); return 1; }
    if (frame_log) fprintf(frame_log, "icount,frame_icount,video_mode,steps,written,blank%s\n", g_replay ? ",replay" : "");
    if (shot_meta)
        fprintf(shot_meta, "requested_icount,frame_icount,video_mode,scan_valid,frame_blank,seq1,scan_part,scan_next,vsync_next,scan_frame,nonzero_pixels,nonblack_palette_entries,visible_pixels\n");
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
        if ((shot_every || shot_vga) && next_shot > m.cpu.icount && next_shot < until) until = next_shot;
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
        if ((shot_every || shot_vga) && m.cpu.icount >= next_shot) {
            char path[600];
            snprintf(path, sizeof path, "%s_%011llu.ppm", shot_prefix, (unsigned long long)m.cpu.icount);
            if (shot_meta) {
                static present_frame f;
                capture(&m, &f);
                unsigned nonzero_pixels = 0, nonblack_palette_entries = 0, visible_pixels = 0;
                for (unsigned p = 0; p < 256; p++)
                    if (f.dac[p * 3] || f.dac[p * 3 + 1] || f.dac[p * 3 + 2])
                        nonblack_palette_entries++;
                for (unsigned p = 0; p < 64000; p++) {
                    unsigned color = f.vram[p];
                    nonzero_pixels += color != 0;
                    visible_pixels += f.dac[color * 3] || f.dac[color * 3 + 1] || f.dac[color * 3 + 2];
                }
                fprintf(shot_meta, "%llu,%llu,%u,%u,%u,%u,%u,%llu,%llu,%llu,%u,%u,%u\n",
                        (unsigned long long)m.cpu.icount, (unsigned long long)f.icount,
                        m.video_mode, m.scan_valid, f.blank, m.seq[1], m.scan_part,
                        (unsigned long long)m.scan_next, (unsigned long long)m.vsync_next,
                        (unsigned long long)m.scan_frame, nonzero_pixels,
                        nonblack_palette_entries, visible_pixels);
            }
            int written = 1;
            if (shots_changed) written = shot_write_changed(&m, path);
            else write_shot(&m, path);
            if (frame_log) {
                static present_frame lf;
                const int replayed = capture(&m, &lf);
                fprintf(frame_log, "%llu,%llu,%u,%llu,%d,%d", (unsigned long long)m.cpu.icount,
                        (unsigned long long)lf.icount, m.video_mode, (unsigned long long)m.interp_steps, written, lf.blank);
                if (g_replay) fprintf(frame_log, ",%d", replayed);
                fputc(10, frame_log);
            }
            /* Keep the capture clock periodic: an instruction finishing
             * just past the deadline must not shift every later sample. */
            if (shot_vga) {
                next_shot += shot_period_whole;
                shot_period_fraction += shot_period_remainder;
                if (shot_period_fraction >= 25175000ull) {
                    next_shot++;
                    shot_period_fraction -= 25175000ull;
                }
            } else next_shot += shot_every;
        }
        if (m.cpu.icount >= next_hash) {
            printf("[hash] %llu %016llx %s\n", (unsigned long long)m.cpu.icount,
                   (unsigned long long)state_hash(&m), dos_current_program(&m));
            if (peek_on)
                printf("[peek] %llu %05X %04X\n", (unsigned long long)m.cpu.icount, peek_at, mem_read16(&m.cpu, peek_at));
            next_hash += hash_every;
        }
        if (rc != RUN_SLICE) break;
    }
    if (trace) fclose(trace);
    if (g_obs_file) fclose(g_obs_file);
    if (dump_len) {
        const cpu_t *rr = &m.cpu;
        printf("[regs] %llu CS:IP=%04X:%04X AX=%04X BX=%04X CX=%04X DX=%04X SI=%04X DI=%04X BP=%04X SP=%04X DS=%04X ES=%04X SS=%04X FL=%04X\n", (unsigned long long)rr->icount, rr->seg[S_CS], rr->ip, rr->r[R_AX], rr->r[R_BX], rr->r[R_CX], rr->r[R_DX], rr->r[R_SI], rr->r[R_DI], rr->r[R_BP], rr->r[R_SP], rr->seg[S_DS], rr->seg[S_ES], rr->seg[S_SS], rr->flags);
        printf("[dump] %05X:%X", dump_at, dump_len);
        for (uint32_t k = 0; k < dump_len; k++) printf("%s%02X", k % 32 ? "" : "\n", mem_read8(&m.cpu, dump_at + k));
        printf("\n");
    }
    if (g_record) fclose(g_record);
    if (g_opl_log) fclose(g_opl_log);
    if (g_midi_log) fclose(g_midi_log);
    if (g_speaker_log) fclose(g_speaker_log);
    if (shot_meta) fclose(shot_meta);
    if (frame_log) fclose(frame_log);
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
    if (g_replay) {
        drawlive_update(&g_live, &g_feed);
        if (g_scale)
            printf("[present] scale %d: %llu model polygons, %llu refilled from their vertices; over the logic frames, "
                   "%llu flat coarse pixels, %llu kept their value on all %d fine pixels, %llu restored by the guard\n",
                   g_scale, (unsigned long long)g_live.hr->stats[HS_POLYGONS], (unsigned long long)g_live.hr->stats[HS_REFILLED],
                   (unsigned long long)g_live.fine_flat, (unsigned long long)g_live.fine_agree, g_scale * g_scale,
                   (unsigned long long)g_live.fine_restored);
        printf("[present] replay: %llu logic frames, %llu equal to the display at their close, %llu not, %llu before a seed; "
               "%llu dropped\n", (unsigned long long)g_live.frames, (unsigned long long)g_live.exact,
               (unsigned long long)g_live.inexact, (unsigned long long)g_live.unseeded, (unsigned long long)g_feed.dropped);
        if (g_replay == 2)
            printf("[present] interp: %llu frame pairs, %llu exact at both ends (t = e and 1 - e, e 1e-6, or 1e-9 with a scale), %llu not; "
                   "%llu VGA frames, %llu presented, %llu of them in-between frames, which moved %llu polygons, "
                   "%llu span fills, %llu outline edges and %llu HUD lines, and held %llu unpaired polygons\n",
                   (unsigned long long)g_live.pairs, (unsigned long long)g_live.pairs_exact,
                   (unsigned long long)g_live.pairs_inexact, (unsigned long long)g_pc.vsyncs,
                   (unsigned long long)g_pc.presented, (unsigned long long)g_live.inbetweens,
                   (unsigned long long)g_live.istats[IS_POLYS_MOVED], (unsigned long long)g_live.istats[IS_SPANS_MOVED],
                   (unsigned long long)g_live.istats[IS_OUTLINE_MOVED], (unsigned long long)g_live.istats[IS_HUD_MOVED],
                   (unsigned long long)g_live.istats[IS_HELD_UNPAIRED]);
        else
            printf("[present] %llu VGA frames, %llu presented from the replay: %llu equal to the scanned-out picture, "
                   "%llu the next logic frame already scanned out, %llu the original drawing on the display, %llu unsettled; "
                   "%llu logic frames equal to a scanned-out picture\n",
                   (unsigned long long)g_pc.vsyncs, (unsigned long long)g_pc.presented, (unsigned long long)g_pc.equal,
                   (unsigned long long)g_pc.next, (unsigned long long)g_pc.mid, (unsigned long long)(g_pc.unsettled + g_pc.npend),
                   (unsigned long long)g_pc.frames_seen);
    }
    for (int pass = 0; pass < 4 && g_nsamp; pass++) {
        int best = 0;
        for (int k = 1; k < g_nsamp; k++) if (g_samp[k].n > g_samp[best].n) best = k;
        if (!g_samp[best].n) break;
        printf("[sample] %04X:%04X (%05X) x%llu\n", g_samp[best].cs, g_samp[best].ip,
               g_samp[best].lin, (unsigned long long)g_samp[best].n);
        g_samp[best].n = 0;
    }
    if (screen) write_shot(&m, screen);
    recomp_shutdown(&m);
    machine_shutdown(&m);
    return rc == RUN_FAULT ? 1 : 0;
}
