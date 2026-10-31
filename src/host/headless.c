/* headless.c - run the game without a window: tests, captures, parity.
 *
 *   f117run --data DIR [--save DIR] [--steps N] [--ips N] [--log FILE]
 *           [--engine interp|recomp] [--time-us N]
 *           [--type WHEN:KEYS]... [--click WHEN:X,Y]... [--move WHEN:X,Y]... [--hold MS]
 *           [--record FILE] [--replay FILE]
 *           [--hash-every N] [--hash-from N] [--peek LINEAR] [--dump LINEAR:LENGTH] [--observe FILE:FROM:TO] [--trace FROM:TO:FILE]
 *           [--coverage FILE] [--screen FILE.ppm] [--shots EVERY:PREFIX | --shots-vga PREFIX] [--shots-start CLOCK] [--shot-meta FILE]
 *           [--shots-changed] [--frame-log FILE] [--no-mouse] [--present scan|replay|interp]
 *           [--present-scale N] [--present-line-width N] [--present-age interp|extrapolate]
 *           [--fix ID|all|none]... [--list-fixes]
 *           [--opl-log FILE] [--midi-log FILE] [--speaker-log FILE]
 *           [--sound-sweep ID,ID,... [--sound-span CLOCKS] [--sound-repeat CLOCKS] [--sound-out PREFIX]]   (VGAME running at the end of --steps)
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
 *
 * F117_PRESENT_DIAG=DIR (with --present-scale N > 1; off when unset, nothing in the machine or
 * the pictures changes): beside each shot or --screen picture, DIR gets BASE.tag (one byte per
 * pixel of the N-times picture naming the path that produced it, src/present/diagtag.h),
 * BASE.diag.ppm (the same in false colour) and BASE.txt (what the picture was). At the end the
 * sub-pixel builder's counters are printed as [diag] lines.
 *
 * With --present-scale N > 1 the pitch ladder, tapes and heading strip are drawn from the HUD's state
 * (src/present/hud_state.h); F117_PRESENT_HUD=old draws them from the original's rounded library calls as
 * before, and a [present] line says what the state-driven HUD did. F117R_OBSERVE_HUD=1 adds the HUD state's
 * record ('h') to an --observe log.
 */
#include "machine.h"
#include "keys.h"
#include "present.h"
#include "observe.h"
#include "observe_pose.h"
#include "remaster_scene.h"
#include "scene_mesh.h"
#include "band_state.h"
#include "recomp_rt.h"
#include "fixes.h"
#include "inputlog.h"
#include "drawfeed.h"
#include "diagtag.h"
uint16_t dos_current_psp(const machine_t *m);
#include "hud_overlay.h"

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
 * "V icount x y z px py range" for each projected vertex. Both engines
 * and timing profiles; it reads and changes nothing. */
static FILE *g_obs_file;
static uint64_t g_obs_from, g_obs_to;
static unsigned g_scene_limit,g_scene_count;
/* F117R_OBSERVE_KINDS=LETTERS keeps only the records of those kinds (a corpus of a few kinds over a long flight
 * without gigabytes of the rest); unset, every record is written. */
static int obs_kind(char kind)
{
    static int ready;
    static const char *keep;
    if (!ready) { ready = 1; keep = getenv("F117R_OBSERVE_KINDS"); if (keep && !*keep) keep = NULL; }
    return !keep || strchr(keep, kind) != NULL;
}
static int obs_want_scene(void *user,uint64_t clock)
{
    (void)user;
    if(clock<g_obs_from || clock>=g_obs_to || g_scene_count>=g_scene_limit || !obs_kind('i') || !obs_kind('j'))return 0;
    g_scene_count++;return 1;
}
static void obs_phase(void *u, uint64_t icount)
{
    (void)u;
    if (icount >= g_obs_from && icount < g_obs_to && obs_kind('P')) fprintf(g_obs_file, "P %llu\n", (unsigned long long)icount);
}
static void obs_vertex(void *u, uint64_t icount, const int32_t xf[3], const int32_t px[2], int range,
                       uint16_t xf_at, uint16_t px_at)
{
    (void)u;
    if (icount >= g_obs_from && icount < g_obs_to && obs_kind('V'))
        fprintf(g_obs_file, "V %llu %d %d %d %d %d %d %04X %04X\n", (unsigned long long)icount, xf[0], xf[1], xf[2], px[0], px[1], range,
                xf_at, px_at);
}
static void obs_prim(void *u, uint64_t icount, char kind, const int32_t *v, int n)
{
    (void)u;
    if (icount < g_obs_from || icount >= g_obs_to || !obs_kind(kind)) return;
    fprintf(g_obs_file, "%c %llu", kind, (unsigned long long)icount);
    for (int k = 0; k < n; k++) fprintf(g_obs_file, " %d", v[k]);
    fputc(10, g_obs_file);
}
/* F117R_OBSERVE_HUD=1 also logs the HUD pass's starting state ('h', hud_state.h), F117R_OBSERVE_BANDS=1 the sky and
 * ground band pass's ('o', band_state.h); without them the log is as it always was. */
static f117_observer g_obs = { 0, obs_phase, obs_vertex, obs_prim };

/* --remaster-shot FILE.ppm:FROM: the first view composition at or after clock FROM draws the model requests gathered
 * since the one before it with the Remastered fill (remaster_scene.c, remaster_raster.c) and writes the picture, and
 * the original's frame beside it as FILE.ppm.orig.ppm. A research aid: read-only, nothing here reaches the game. */
static remaster_scene *g_rm_scene;
static remaster_frame g_rm_frame;
static uint64_t g_rm_from;
static char g_rm_path[600];
static int g_rm_done;
size_t g_rm_last_n;
static void write_shot(const machine_t *m, const char *path);
static void rm_pose(void *u, uint64_t icount, machine_t *m, uint16_t model, uint16_t bank, const int16_t pos[3])
{
    (void)u; (void)icount;
    const size_t before = g_rm_frame.n;
    int added = remaster_scene_add(g_rm_scene, m->mem, MEM_SIZE, m->cpu.seg[S_DS], bank, model, pos, &g_rm_frame);
    {   /* the cockpit's map display draws models through the same pipeline into its own small viewport (DS:85FA..8600):
         * only the full-width 3-D view's requests belong in the main picture */
        const uint8_t *d = m->mem + ((size_t)m->cpu.seg[S_DS] << 4);
        const int vl = (int16_t)(d[0x85FA] | d[0x85FB] << 8), vr = (int16_t)(d[0x85FE] | d[0x85FF] << 8);
        if (vr - vl < 300 && added > 0) { g_rm_frame.n = before; added = 0; }
    }
    if (getenv("F117_RM_DUMP") && icount >= g_rm_from - 20000000) {
        fprintf(stderr, "req %llu model %u bank %04X pos %d,%d,%d added %d flip %d\n", (unsigned long long)icount, model, bank, pos[0], pos[1], pos[2], added, m->mem[((size_t)m->cpu.seg[S_DS] << 4) + 0x0346] & 1);
        const uint8_t *ds = m->mem + ((size_t)m->cpu.seg[S_DS] << 4);
        const int gx = (int16_t)(ds[0x8602] | ds[0x8603] << 8), gy = (int16_t)(ds[0x8604] | ds[0x8605] << 8);
        for (size_t k = before; k < g_rm_frame.n && k < before + 3; k++) {
            const remaster_prim *p = &g_rm_frame.prim[k];
            if (p->n && p->v[0].z > 0) fprintf(stderr, "  prim kind %d n %d pixel %u at %d,%d zhi %d\n", p->kind, p->n, p->pixel,
                                              (int)(gx + 256.0 * p->v[0].x / p->v[0].z), (int)(gy + 256.0 * p->v[0].y / p->v[0].z), p->v[0].z >> 16);
        }
    }
}
static uint32_t rm_rgb(const uint8_t *c)
{
    return 0xFF000000u | ((uint32_t)((c[2] << 2) | (c[2] >> 4)) << 16) | ((uint32_t)((c[1] << 2) | (c[1] >> 4)) << 8) |
           (uint32_t)((c[0] << 2) | (c[0] >> 4));
}
static const remaster_prim *g_rm_sort;
static int rm_cmp(const void *a, const void *b)
{
    const remaster_prim *p = &g_rm_sort[*(const size_t *)a], *q = &g_rm_sort[*(const size_t *)b];
    if (p->zkey != q->zkey) return p->zkey > q->zkey ? -1 : 1;                 /* far first */
    if (p->request != q->request) return p->request < q->request ? -1 : 1;
    return *(const size_t *)a < *(const size_t *)b ? -1 : 1;
}
static void rm_compose(void *u, machine_t *m)
{
    (void)u;
    if (getenv("F117_RM_DUMP") && m->cpu.icount >= g_rm_from - 20000000) fprintf(stderr, "compose %llu n %zu\n", (unsigned long long)m->cpu.icount, g_rm_frame.n);
    static unsigned composes;
    if (g_rm_done) return;
    if (m->cpu.icount < g_rm_from) { remaster_frame_clear(&g_rm_frame); composes = 0; { extern size_t g_rm_last_n; g_rm_last_n = 0; } return; }
    /* one view composition holds a whole picture (about forty model requests; the flip bit at DS:0346 toggles per
     * composition): a frame is the requests since the previous composition. F117_RM_WINDOW=N gathers N of them instead. */
    static int win = -1;
    if (win < 0) { const char *e = getenv("F117_RM_WINDOW"); win = e ? atoi(e) : 1; }
    if (win > 1 && ++composes < (unsigned)win) return;
    const uint8_t *ds = m->mem + ((size_t)m->cpu.seg[S_DS] << 4);
    #define DSW(o) ((int)(int16_t)(ds[o] | ds[(o) + 1] << 8))
    const int vl = DSW(0x85FA), vt = DSW(0x85FC), vr = DSW(0x85FE), vb = DSW(0x8600), gx = DSW(0x8602), gy = DSW(0x8604);
    /* the original frame is 1280 x 960 at 4x; F117_RM_WIDTH wider (16:9 at this height is 1707) keeps the scale and adds
     * columns about the projection centre: the picture grows at its sides and nothing that was drawn moves */
    const int w0 = 1280, h = 960;
    int w = w0;
    { const char *e = getenv("F117_RM_WIDTH"); if (e && atoi(e) > w0) w = atoi(e); }
    const int xoff = (w - w0) / 2;
    const double kx = w0 / 320.0, ky = h / 200.0;
    remaster_target t = { w, h, (uint32_t *)malloc((size_t)w * h * 4), (float *)malloc((size_t)w * h * 4), kx * gx + xoff, ky * gy,
                          256.0 * kx, 256.0 * ky, 0x10000 };
    remaster_clear(&t, 0xFF2A2A2Au);
    t.index = (uint8_t *)calloc((size_t)w * h, 1);
    for (int i = 0; i < 256; i++) t.pal[i] = rm_rgb(&m->dac[3 * i]);
    const int hy = (int)(ky * gy);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) t.color[y * w + x] = y < hy ? 0xFFC08040u : 0xFF305030u;
    {   /* sky and ground from the band pass's own state (band_state.h), unrounded and smooth: each class (16 sky bands,
         * 16 ground bands) is painted flat into float colour, then blurred across the bands (normalised, so the
         * horizon stays sharp) and dithered on the way out, so a gradient shows no steps */
        int32_t rec[BAND_REC_BASE] = { BAND_REC_VERSION, 0, 0 };
        for (int k = 0; k < BAND_NWORDS; k++) rec[3 + k] = band_rec_byte[k] ? ds[band_rec_off[k]] : (uint16_t)(ds[band_rec_off[k]] | ds[band_rec_off[k] + 1] << 8);
        band_state st; static band_fine_list fl;
        if (band_state_load(rec, BAND_REC_BASE, &st) && band_fine(&st, NULL, &fl) == 0) {
            const size_t np = (size_t)w * h;
            float *acc = (float *)calloc(np * 4, sizeof(float)), *tmp = (float *)malloc(np * 4 * sizeof(float));
            uint8_t *mask = (uint8_t *)calloc(np, 1);
            for (int cls = 0; cls < 2; cls++) {
                memset(acc, 0, np * 4 * sizeof(float)); memset(mask, 0, np);
                for (int q = cls * 16; q < (cls + 1) * 16 && q < fl.n; q++) {
                    const band_fquad *b = &fl.q[q];
                    if (!b->valid) continue;
                    const uint8_t *c = &m->dac[3 * b->colour];
                    const float cr = (float)((c[0] << 2) | (c[0] >> 4)), cg = (float)((c[1] << 2) | (c[1] >> 4)), cb = (float)((c[2] << 2) | (c[2] >> 4));
                    double X[4], Y[4];
                    for (int j = 0; j < 4; j++) { X[j] = (b->x[j] + 0.5) * kx + xoff; Y[j] = (b->y[j] + 0.5) * ky; }
                    double mnx = X[0], mxx = X[0], mny = Y[0], mxy = Y[0];
                    for (int j = 1; j < 4; j++) { if (X[j] < mnx) mnx = X[j]; if (X[j] > mxx) mxx = X[j]; if (Y[j] < mny) mny = Y[j]; if (Y[j] > mxy) mxy = Y[j]; }
                    const int bx0 = mnx < 0 ? 0 : (int)mnx, bx1 = mxx > w - 1 ? w - 1 : (int)mxx, by0 = mny < 0 ? 0 : (int)mny, by1 = mxy > h - 1 ? h - 1 : (int)mxy;
                    for (int y = by0; y <= by1; y++) for (int x = bx0; x <= bx1; x++) {
                        int pos = 0, neg = 0;
                        for (int j = 0; j < 4; j++) {
                            const int j2 = (j + 1) & 3;
                            const double e = (X[j2] - X[j]) * (y + 0.5 - Y[j]) - (Y[j2] - Y[j]) * (x + 0.5 - X[j]);
                            if (e > 0) pos = 1; else if (e < 0) neg = 1;
                        }
                        if (pos && neg) continue;
                        float *a4 = &acc[((size_t)y * w + x) * 4];
                        a4[0] = cr; a4[1] = cg; a4[2] = cb; a4[3] = 1.0f; mask[(size_t)y * w + x] = 1; t.index[(size_t)y * w + x] = b->colour;
                    }
                }
                /* two vertical box blurs of about a band's height (a triangle kernel), weights carried in channel 3 */
                const int rad = (int)(ky * 3.0);
                for (int pass = 0; pass < 2; pass++) {
                    for (int x = 0; x < w; x++) {
                        double sum[4] = { 0, 0, 0, 0 };
                        for (int y = 0; y < rad && y < h; y++) for (int k = 0; k < 4; k++) sum[k] += acc[((size_t)y * w + x) * 4 + k];
                        for (int y = 0; y < h; y++) {
                            if (y + rad < h) for (int k = 0; k < 4; k++) sum[k] += acc[((size_t)(y + rad) * w + x) * 4 + k];
                            if (y - rad - 1 >= 0) for (int k = 0; k < 4; k++) sum[k] -= acc[((size_t)(y - rad - 1) * w + x) * 4 + k];
                            for (int k = 0; k < 4; k++) tmp[((size_t)y * w + x) * 4 + k] = (float)sum[k];
                        }
                    }
                    memcpy(acc, tmp, np * 4 * sizeof(float));
                }
                unsigned noise = 12345u;
                for (size_t i = 0; i < np; i++) {
                    if (!mask[i] || acc[i * 4 + 3] <= 0) continue;
                    noise = noise * 1664525u + 1013904223u;
                    const float d = ((noise >> 16) & 255) / 255.0f - 0.5f;
                    int v[3];
                    for (int k = 0; k < 3; k++) { v[k] = (int)(acc[i * 4 + k] / acc[i * 4 + 3] + 0.5f + d); if (v[k] < 0) v[k] = 0; if (v[k] > 255) v[k] = 255; }
                    t.color[i] = 0xFF000000u | ((uint32_t)v[2] << 16) | ((uint32_t)v[1] << 8) | (uint32_t)v[0];
                }
            }
            free(acc); free(tmp); free(mask);
        }
    }
    long px = 0;
    {   /* the original paints model by model, far to near: one depth group per request, groups in that order */
        const size_t n = g_rm_frame.n;
        size_t *order = (size_t *)malloc((n ? n : 1) * sizeof *order);
        for (size_t k = 0; k < n; k++) order[k] = k;
        /* Remastered's point is depth-correct drawing: one depth buffer across every model, with a tie (coplanar
         * ground, a marking on it) going to the later draw in the game's own order. F117_RM_PAINTER=1 reproduces the
         * original's whole-object sort instead (far to near, depth only inside a model), for comparison. */
        const int painter = getenv("F117_RM_PAINTER") != NULL;
        if (painter) {
            g_rm_sort = g_rm_frame.prim;
            qsort(order, n, sizeof *order, rm_cmp);
            t.gen = (uint32_t *)calloc((size_t)w * h, sizeof(uint32_t));
        }
        uint32_t group = 0; uint32_t last_req = 0;
        for (size_t i = 0; i < n; i++) {
            const remaster_prim *p = &g_rm_frame.prim[order[i]];
            { static int only = -2, skip = -2; if (only == -2) { const char *e = getenv("F117_RM_ONLY"), *k = getenv("F117_RM_SKIP"); only = e ? atoi(e) : -1; skip = k ? atoi(k) : -1; }
              if ((only >= 0 && p->model != only) || (skip >= 0 && p->model == skip)) continue; }   /* debugging: one model, or all but one */
            if (!i || p->request != last_req) { group++; last_req = p->request; }
            t.cur_gen = group;
            if (p->style == 0xFB) continue;                                   /* "clear without drawing": the original paints nothing */
            if (p->style == 0xFC) continue;                                   /* stipple: not modelled yet */
            t.op = p->style == 0xFD ? 1 : p->style == 0xFE ? 2 : 0;           /* OR / AND into the page (VGAME 1377:0A89, vg4_fill_logic) */
            t.opval = (uint8_t)p->colour_word;
            t.cur_index = p->pixel;
            uint32_t col = rm_rgb(&m->dac[3 * p->pixel]);
            if (p->style < 0xFB) {                                            /* a colour pair: the original dithers; the average is the smooth equivalent */
                const uint32_t b = rm_rgb(&m->dac[3 * p->pixel2]);
                col = 0xFF000000u | ((((col & 255) + (b & 255)) / 2) & 255) | (((((col >> 8) & 255) + ((b >> 8) & 255)) / 2) << 8) | (((((col >> 16) & 255) + ((b >> 16) & 255)) / 2) << 16);
            }
            if (p->kind == SCENE_POLYGON && p->n >= 3) px += remaster_polygon(&t, p->v, p->n, col);
            else if (p->kind == SCENE_LINES) for (int j = 0; j + 1 < p->n; j++) px += remaster_line(&t, p->v[j], p->v[j + 1], 2.0, col);
        }
        free(order); free(t.gen); t.gen = NULL; t.op = 0;
    }
    static present_frame f;
    present_capture(m, &f);
    if (getenv("F117_RM_DUMP")) {
        for (int i = 126; i < 140; i++) fprintf(stderr, "dac %d = %d,%d,%d | shown %d,%d,%d\n", i, m->dac[3 * i], m->dac[3 * i + 1], m->dac[3 * i + 2], f.dac[3 * i], f.dac[3 * i + 1], f.dac[3 * i + 2]);
        for (int y = 60; y < 106; y += 9) fprintf(stderr, "vram row %d: %d %d %d %d %d\n", y, f.vram[y * 320 + 20], f.vram[y * 320 + 80], f.vram[y * 320 + 160], f.vram[y * 320 + 240], f.vram[y * 320 + 300]);
    }
    if (getenv("F117_RM_DUMP")) for (size_t k = 0; k < g_rm_frame.n; k++) {
        const remaster_prim *p = &g_rm_frame.prim[k];
        double sx = 0, sy = 0; int ok = 0;
        for (int j = 0; j < p->n; j++) if (p->v[j].z > 0x10000) { sx += gx + 256.0 * p->v[j].x / p->v[j].z; sy += gy - 256.0 * p->v[j].y / p->v[j].z; ok++; }
        int under = -1;
        if (ok) { const int ix = (int)(sx / ok), iy = (int)(sy / ok); if (ix >= 0 && ix < 320 && iy >= 0 && iy < 200) under = f.vram[iy * 320 + ix]; }
        fprintf(stderr, "prim model %u kind %d n %d idx %u word %04X shade %u zhi %d under %d at %d,%d pix %u rgb %d,%d,%d\n", p->model, p->kind, p->n, p->colour_index,
                p->colour_word, p->shade, p->n ? p->v[0].z >> 16 : 0, under, ok ? (int)(sx / ok) : -1, ok ? (int)(sy / ok) : -1, p->pixel, m->dac[3 * p->pixel], m->dac[3 * p->pixel + 1], m->dac[3 * p->pixel + 2]);
    }
    const int x0 = w > w0 ? 0 : (int)(kx * vl), x1 = w > w0 ? w : (int)(kx * (vr + 1)), y0 = (int)(ky * vt), y1 = (int)(ky * (vb + 1));
    (void)y1;
    FILE *o = fopen(g_rm_path, "wb");
    if (o) {   /* left: the render in the viewport's place on a dark page; right: the whole original frame, uncropped */
        fprintf(o, "P6\n%d %d\n255\n", w * 2, h);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (y >= y0 && y < y1 && x >= x0 && x < x1) { const uint32_t v = t.color[y * w + x]; fputc(v & 255, o); fputc((v >> 8) & 255, o); fputc((v >> 16) & 255, o); }
                else { fputc(16, o); fputc(16, o); fputc(16, o); }
            }
            for (int x = 0; x < w; x++) {
                const int ox = x - xoff; const uint8_t dk[3] = { 4, 4, 4 }; const uint8_t *c = (ox < 0 || ox >= w0) ? dk : &f.dac[3 * f.vram[(y * 200 / h) * 320 + ox * 320 / w0]];
                fputc((c[0] << 2) | (c[0] >> 4), o); fputc((c[1] << 2) | (c[1] >> 4), o); fputc((c[2] << 2) | (c[2] >> 4), o);
            }
        }
    }
    if (o) fclose(o);
    {   /* a blend of the render and the original's viewport pixels, to see misalignment and what is missing */
        char bp[640];
        snprintf(bp, sizeof bp, "%s.blend.ppm", g_rm_path);
        FILE *bo = fopen(bp, "wb");
        if (bo) {
            fprintf(bo, "P6\n%d %d\n255\n", x1 - x0, y1 - y0);
            for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
                const uint32_t v = t.color[y * w + x];
                const int ox = x - xoff; const uint8_t dk[3] = { 4, 4, 4 }; const uint8_t *c = (ox < 0 || ox >= w0) ? dk : &f.dac[3 * f.vram[(y * 200 / h) * 320 + ox * 320 / w0]];
                const int ob[3] = { (c[0] << 2) | (c[0] >> 4), (c[1] << 2) | (c[1] >> 4), (c[2] << 2) | (c[2] >> 4) };
                const int mb[3] = { (int)(v & 255), (int)((v >> 8) & 255), (int)((v >> 16) & 255) };
                for (int k = 0; k < 3; k++) fputc((ob[k] + mb[k]) / 2, bo);
            }
            fclose(bo);
        }
    }
    fprintf(stderr, "remaster-shot: %zu primitives, %ld pixels, viewport %d,%d-%d,%d centre %d,%d, clock %llu -> %s\n", g_rm_frame.n, px,
            vl, vt, vr, vb, gx, gy, (unsigned long long)m->cpu.icount, g_rm_path);
    free(t.color); free(t.inv_z); free(t.index);
    g_rm_done = 1;
}
/* the matched projection only reports a vertex (and so the first one's pose) to an observer that asks for vertices */
static void rm_vertex(void *u, uint64_t icount, const int32_t xf[3], const int32_t px[2], int range, uint16_t xf_at, uint16_t px_at)
{
    (void)u; (void)icount; (void)xf; (void)px; (void)range; (void)xf_at; (void)px_at;
}
static f117_observer g_rm_obs;

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


/* Call the sound driver's entry with an id from where the copy stands, as the game's own request does (the
 * argument pushed, a far call, the caller popping it). 1 when the driver returned. */
static int sweep_request(machine_t *w, unsigned id, uint16_t tseg, uint16_t toff)
{
    cpu_t *c = &w->cpu;
    const uint16_t cs = c->seg[S_CS], ip = c->ip;
    cpu_push16(c, (uint16_t)id);
    cpu_push16(c, cs);
    cpu_push16(c, ip);
    c->seg[S_CS] = tseg;
    c->ip = toff;
    c->icount++;
    w->trap_on = 1;
    w->trap_cs = cs;
    w->trap_ip = ip;
    w->trap_sp = (uint16_t)(c->r[R_SP] + 4);
    const int rc = machine_run(w, c->icount + w->ips / 10);
    w->trap_on = 0;
    if (rc != RUN_TRAP) return 0;
    c->r[R_SP] = (uint16_t)(c->r[R_SP] + 2);
    return 1;
}

/* --sound-sweep IDS (diagnostic, for the sound board, docs/sound-board.md): with VGAME running at a quiet point
 * (the end of --steps), render each listed sound id through whichever driver the route chose. For each id the
 * machine is copied; the game's own sound-request stub (the far jump slot at load+1E42:0313) is turned into a bare
 * return in the copy, so the game's engine notes and calls make no sound; the driver's entry is called directly
 * with the id (the stub's jump target, the id pushed as the game pushes it); then the copy runs on for `span`
 * instructions with the OPL, MIDI and speaker writes logged to PREFIX<id>.opl, .midi and .spk. PREFIXsweep.tsv lists
 * id, the clock the request began and ended at, and the write counts. The real machine is not touched. */
static int sound_sweep(machine_t *real, const char *list, uint64_t span, uint64_t repeat, const char *prefix)
{
    if (!strstr(dos_current_program(real), "VGAME")) {
        fprintf(stderr, "--sound-sweep needs VGAME running at the end of --steps (program is %s)\n", dos_current_program(real));
        return 1;
    }
    const uint16_t load = (uint16_t)(dos_current_psp(real) + 0x10);
    const uint32_t stub = (((uint32_t)(uint16_t)(load + 0x1E42)) << 4) + 0x0313;
    if (real->mem[stub] != 0xEA) {
        fprintf(stderr, "--sound-sweep: no far jump at %04X:0313 (byte %02X)\n", (unsigned)(load + 0x1E42), real->mem[stub]);
        return 1;
    }
    const uint16_t toff = (uint16_t)(real->mem[stub + 1] | real->mem[stub + 2] << 8);
    const uint16_t tseg = (uint16_t)(real->mem[stub + 3] | real->mem[stub + 4] << 8);
    fprintf(stderr, "sound sweep: driver entry %04X:%04X (stub %04X:0313)\n", tseg, toff, (unsigned)(load + 0x1E42));
    static machine_t work;
    uint8_t *wmem = (uint8_t *)malloc(MEM_SIZE);
    char path[700];
    snprintf(path, sizeof path, "%ssweep.tsv", prefix);
    FILE *tsv = fopen(path, "w");
    if (!wmem || !tsv) { fprintf(stderr, "--sound-sweep: cannot write %s\n", path); return 1; }
    fprintf(tsv, "id\tstart\tend\topl_writes\tmidi_bytes\tspeaker_changes\n");
    char *copy = (char *)malloc(strlen(list) + 1);
    strcpy(copy, list);
    int failures = 0;
    for (char *tok = strtok(copy, ", "); tok; tok = strtok(NULL, ", ")) {
        const unsigned id = (unsigned)strtoul(tok, NULL, 0);
        work = *real;
        memcpy(wmem, real->mem, MEM_SIZE);
        work.mem = wmem;
        work.cpu.mem = wmem;
        work.cpu.user = &work;
        work.cpu.cover = NULL;
        work.log = NULL;
        work.on_input = NULL;
        work.on_input_user = NULL;
        work.opl_sched = NULL;
        work.opl_sched_n = work.opl_sched_i = work.opl_sched_cap = 0;
        work.hooks.opl_write = on_opl;
        work.hooks.midi_byte = on_midi;
        work.hooks.speaker = on_speaker;
        g_speaker_machine = &work;
        wmem[stub] = 0xCB;                                  /* the game's own requests now return at once */
        const char *kinds[3] = { "opl", "midi", "spk" };
        FILE **logs[3] = { &g_opl_log, &g_midi_log, &g_speaker_log };
        for (int k = 0; k < 3; k++) {
            snprintf(path, sizeof path, "%s%u.%s", prefix, id, kinds[k]);
            *logs[k] = fopen(path, "w");
            if (!*logs[k]) { fprintf(stderr, "--sound-sweep: cannot write %s\n", path); return 1; }
        }
        const uint64_t w0 = work.opl_writes, m0 = work.midi_bytes, s0 = work.speaker_changes;
        cpu_t *c = &work.cpu;
        const uint16_t home_cs = c->seg[S_CS];              /* the game's own code: where requests are made from */
        const uint64_t start = c->icount;
        if (!sweep_request(&work, id, tseg, toff)) { fprintf(stderr, "sound %u: the driver did not return\n", id); failures++; }
        const uint64_t called = c->icount;
        const uint64_t end = called + span;
        uint64_t next_request = repeat ? called + repeat : ~0ull;
        while (c->icount < end) {
            uint64_t until = c->icount + work.ips / 100 < end ? c->icount + work.ips / 100 : end;
            if (next_request < until) until = next_request;
            if (until <= c->icount) until = c->icount + 1;
            if (machine_run(&work, until) != RUN_SLICE) break;
            if (c->icount >= next_request) {
                /* a held sound is asked for again each step, as the game does; ask from the game's own code with
                 * interrupts on, never from inside the driver's own tick */
                for (int tries = 0; tries < 2000 && !(c->seg[S_CS] == home_cs && (c->flags & F_IF)); tries++)
                    if (machine_run(&work, c->icount + 200) != RUN_SLICE) break;
                sweep_request(&work, id, tseg, toff);
                next_request = c->icount + repeat;
            }
        }
        for (int k = 0; k < 3; k++) { fclose(*logs[k]); *logs[k] = NULL; }
        fprintf(tsv, "%u\t%llu\t%llu\t%llu\t%llu\t%llu\n", id, (unsigned long long)start, (unsigned long long)c->icount,
                (unsigned long long)(work.opl_writes - w0), (unsigned long long)(work.midi_bytes - m0),
                (unsigned long long)(work.speaker_changes - s0));
        fprintf(stderr, "sound %u: %llu OPL writes, %llu MIDI bytes, %llu speaker changes\n", id,
                (unsigned long long)(work.opl_writes - w0), (unsigned long long)(work.midi_bytes - m0),
                (unsigned long long)(work.speaker_changes - s0));
    }
    fclose(tsv);
    free(copy);
    free(wmem);
    return failures ? 1 : 0;
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
static int g_replay;                       /* 1 replay, 2 interp (with g_rerun: drawn by the original on a copy) */
static int g_rerun;
static int g_scale, g_age, g_line_width;    /* optional presentation settings */
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
    g_live.shown_hi = g_live.shown_tag = NULL;
    if (!g_replay) return 0;
    drawlive_update(&g_live, &g_feed);
    return g_replay == 2 ? drawlive_present_interp(&g_live, m, f) : drawlive_present(&g_live, m, f);
}

/* F117_PRESENT_DIAG: the origin plane of the picture just presented, beside the shot. The
 * false colours group diagtag.h's tags by class: greens refined polygons; reds polygons left
 * scaled; yellows fine strokes; white text; blues sprites and artwork; greys copies; the dark
 * colours what nothing wrote. */
static const uint8_t g_diag_rgb[DT_COUNT][3] = {
    [DT_SEED] = { 40, 40, 40 },
    [DT_MODEL_SOLID] = { 0, 170, 0 }, [DT_MODEL_PATTERN] = { 130, 230, 130 }, [DT_MODEL_NEAR] = { 0, 230, 170 },
    [DT_SPANPOLY_FINE] = { 0, 110, 90 },
    [DT_FB_UNATTEMPTED] = { 255, 0, 0 }, [DT_FB_NEAR_SHAPE] = { 220, 0, 40 }, [DT_FB_NEAR_CROSSING] = { 255, 60, 60 },
    [DT_FB_FEW_EDGES] = { 200, 40, 0 }, [DT_FB_NO_VERTICES] = { 255, 90, 0 }, [DT_FB_BEHIND] = { 190, 0, 80 },
    [DT_FB_NO_DIVISOR] = { 255, 120, 120 }, [DT_FB_BEYOND] = { 140, 0, 0 },
    [DT_GUARD] = { 255, 190, 80 },
    [DT_SPAN_NOT_POLY] = { 180, 0, 180 }, [DT_SPAN_REJECT] = { 255, 0, 255 }, [DT_SPAN_BEYOND] = { 200, 0, 110 },
    [DT_SPAN_LINE] = { 255, 255, 190 }, [DT_SPAN_DEGENERATE] = { 160, 0, 200 },
    [DT_SPAN_THIN] = { 255, 255, 120 }, [DT_SPAN_SHADE] = { 110, 0, 150 },
    [DT_HUD_FINE] = { 255, 255, 0 }, [DT_OUTLINE_FINE] = { 255, 215, 0 }, [DT_TICKS_FINE] = { 220, 220, 0 },
    [DT_HUD_COARSE] = { 160, 160, 0 }, [DT_OUTLINE_COARSE] = { 160, 130, 0 }, [DT_TICKS_COARSE] = { 130, 130, 0 },
    [DT_TEXT] = { 255, 255, 255 }, [DT_SPRITE] = { 0, 200, 255 }, [DT_SPRITE_RLE] = { 150, 230, 255 },
    [DT_BLIT_UNHELD] = { 70, 70, 255 }, [DT_BLIT_COARSE] = { 150, 150, 150 }, [DT_PAGECOPY] = { 100, 100, 100 },
    [DT_X_DELTA] = { 120, 80, 40 }, [DT_OTHER] = { 255, 0, 200 },
};

/* Holes in the fine picture of a window (original pixels, x1 and y1 exclusive), counted in fine pixels.
 * unwritten: no path wrote the pixel in this frame (the seed's scaled copy of an earlier frame shows).
 * stray: a pixel the scene paths wrote (refined or scaled polygon, guard repairs) whose colour is
 * none of the nine coarse cells around its own, so no shift of a boundary by up to a cell can explain it:
 * a gap that shows what lies under the polygons, or geometry drawn where the original drew another colour.
 * The coarse picture is the original's N = 1 rendition of the same frame. */
static void diag_holes(const uint8_t *coarse, const uint8_t *hi, const uint8_t *tag, int n, const int rect[4],
                       unsigned long long *unwritten, unsigned long long *stray, uint8_t *mask)
{
    const int w = 320 * n;
    *unwritten = *stray = 0;
    for (int cy = rect[1]; cy < rect[3]; cy++)
        for (int cx = rect[0]; cx < rect[2]; cx++) {
            uint8_t near[9];
            int nn = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    const int x = cx + dx, y = cy + dy;
                    near[nn++] = (x < 0 || x > 319 || y < 0 || y > 199) ? coarse[cy * 320 + cx] : coarse[y * 320 + x];
                }
            for (int j = 0; j < n; j++)
                for (int i = 0; i < n; i++) {
                    const size_t at = (size_t)(cy * n + j) * w + (size_t)cx * n + i;
                    const uint8_t t = tag[at];
                    if (t == DT_SEED) { (*unwritten)++; if (mask) mask[at] = 1; continue; }
                    if (t > DT_SPAN_SHADE && t != DT_BAND_STATE) continue;   /* the band state's bands are scene too */
                    int k = 0;
                    while (k < 9 && near[k] != hi[at]) k++;
                    if (k == 9) { (*stray)++; if (mask) mask[at] = 2; }
                }
        }
}

/* F117_PRESENT_DIAG_HIST=FILE appends, for every shot, one line: the shot's name, the plan kind and t,
 * the count of each origin tag inside the window F117_PRESENT_DIAG_RECT=x0,y0,x1,y1 (original
 * pixels; default 0,0,320,91, the 3-D view), then the window's unwritten and stray pixels (diag_holes).
 * F117_PRESENT_DIAG_HIST_ONLY=1 then writes no images. */
static void diag_window(int rect[4])
{
    rect[0] = 0; rect[1] = 0; rect[2] = 320; rect[3] = 91;
    const char *r = getenv("F117_PRESENT_DIAG_RECT");
    if (r) sscanf(r, "%d,%d,%d,%d", &rect[0], &rect[1], &rect[2], &rect[3]);
}

static void diag_hist(const char *stem, int n, const uint8_t *coarse)
{
    const char *file = getenv("F117_PRESENT_DIAG_HIST");
    if (!file || !*file) return;
    int rect[4];
    diag_window(rect);
    unsigned long long cnt[256] = { 0 };
    const int w = 320 * n;
    for (int y = rect[1] * n; y < rect[3] * n; y++)
        for (int x = rect[0] * n; x < rect[2] * n; x++) cnt[g_live.shown_tag[(size_t)y * w + x]]++;
    unsigned long long unwritten, stray;
    diag_holes(coarse, g_live.shown_hi, g_live.shown_tag, n, rect, &unwritten, &stray, NULL);
    FILE *o = fopen(file, "a");
    if (!o) return;
    fprintf(o, "%s %d %.6f", stem, g_live.diag_kind, g_live.diag_t);
    for (int k = 0; k < DT_COUNT; k++) fprintf(o, " %llu", cnt[k]);
    fprintf(o, " %llu %llu\n", unwritten, stray);
    fclose(o);
}

static void diag_write(const char *shot_path, int n, const uint8_t *coarse)
{
    const char *dir = getenv("F117_PRESENT_DIAG");
    if (!dir || !*dir || !g_live.shown_tag || n < 1) return;
    const char *slash = strrchr(shot_path, '/'), *back = strrchr(shot_path, '\\');
    const char *base = slash > back ? slash + 1 : back ? back + 1 : shot_path;
    char stem[300], path[900];
    snprintf(stem, sizeof stem, "%s", base);
    char *dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    const int w = 320 * n, h = 200 * n;
    diag_hist(stem, n, coarse);
    if (getenv("F117_PRESENT_DIAG_HIST_ONLY")) return;
    snprintf(path, sizeof path, "%s/%s.tag", dir, stem);
    FILE *o = fopen(path, "wb");
    if (o) { fwrite(g_live.shown_tag, 1, (size_t)w * h, o); fclose(o); }
    snprintf(path, sizeof path, "%s/%s.diag.ppm", dir, stem);
    if ((o = fopen(path, "wb"))) {
        fprintf(o, "P6\n%d %d\n255\n", w, h);
        static uint8_t row[3 * 320 * 16];
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const uint8_t t = g_live.shown_tag[(size_t)y * w + x];
                static const uint8_t band_rgb[3] = { 0, 150, 200 };       /* the band state's (band_state.h) */
                memcpy(row + 3 * x, t == DT_BAND_STATE ? band_rgb : g_diag_rgb[t < DT_COUNT ? t : DT_OTHER], 3);
            }
            fwrite(row, 3, (size_t)w, o);
        }
        fclose(o);
    }
    snprintf(path, sizeof path, "%s/%s.txt", dir, stem);
    if ((o = fopen(path, "w"))) {
        fprintf(o, "scale %d width %d height %d\nplan %d t %.6f older %llu newer %llu\n", n, w, h, g_live.diag_kind,
                g_live.diag_t, (unsigned long long)g_live.diag_older, (unsigned long long)g_live.diag_newer);
        fprintf(o, "plan kinds: 1 replay (logic frame), 2 older, 3 newer, 4 in-between\n");
        int rect[4];
        unsigned long long unwritten, stray;
        diag_window(rect);
        const size_t cells = (size_t)w * h;
        uint8_t *mask = (uint8_t *)calloc(cells, 1);
        diag_holes(coarse, g_live.shown_hi, g_live.shown_tag, n, rect, &unwritten, &stray, mask);
        if (mask) {
            snprintf(path, sizeof path, "%s/%s.holes", dir, stem);
            FILE *hm = fopen(path, "wb");
            if (hm) { fwrite(mask, 1, cells, hm); fclose(hm); }
            free(mask);
        }
        fprintf(o, "window %d,%d,%d,%d holes: unwritten %llu stray %llu\n", rect[0], rect[1], rect[2], rect[3], unwritten, stray);
        fclose(o);
    }
}

/* A shot: the finer picture when one was presented, with the frame's palette. */
static void write_shot(const machine_t *m, const char *path)
{
    static present_frame f;
    capture(m, &f);
    if (!g_live.shown_hi) { present_frame_write_ppm(&f, path); return; }
    diag_write(path, g_scale, f.vram);
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
    const char *sweep_ids = NULL, *sweep_out = "sweep_";
    uint64_t sweep_span = 0, sweep_repeat = 0;
    const char *shot_meta_path = NULL;
    const char *frame_log_path = NULL;     /* --frame-log FILE: each shot's clock, its picture's scan time, the steps so far, screen-off */
    int shots_changed = 0;
    int no_mouse = 0;                      /* --no-mouse: INT 33h answers as with no driver loaded */
    uint64_t steps = 100000000ull, ips = MACHINE_DEFAULT_IPS, hold_ms = 60, boot_ms = 0;
    unsigned sim_hz = 8;
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
        else if (!strcmp(a, "--sim-hz") && v) {
            if (strcmp(v, "8") && strcmp(v, "15")) {
                fprintf(stderr, "--sim-hz requires 8 or 15\n"); return 2;
            }
            sim_hz = (unsigned)atoi(v); i++;
        }
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
            if (getenv("F117R_OBSERVE_HUD")) g_obs.hud = 1;
            if (getenv("F117R_OBSERVE_BANDS")) g_obs.bands = 1;
            if (getenv("F117R_OBSERVE_IDENT")) g_obs.ident = 1;
            const char *scene=getenv("F117R_OBSERVE_SCENE");
            if(scene) {
                char *end;const unsigned long limit=strtoul(scene,&end,10);
                if(!*scene || *end || limit<1 || limit>65536) { fprintf(stderr,"F117R_OBSERVE_SCENE requires a request limit 1..65536\n");return 2; }
                g_scene_limit=(unsigned)limit;g_scene_count=0;g_obs.scene=1;g_obs.want_scene=obs_want_scene;
            }
            observe_set(&g_obs); i++;
        }
        else if (!strcmp(a, "--remaster-shot") && v) {
            snprintf(g_rm_path, sizeof g_rm_path, "%s", v);
            char *c3 = strrchr(g_rm_path, ':');
            if (!c3 || c3 == g_rm_path + 1) { fprintf(stderr, "--remaster-shot FILE.ppm:FROM\n"); return 2; }
            *c3 = 0; g_rm_from = strtoull(c3 + 1, NULL, 0);
            g_rm_scene = remaster_scene_new();
            g_rm_obs.compose = rm_compose;
            g_rm_obs.vertex = rm_vertex;
            observe_set(&g_rm_obs);
            observe_pose_set(rm_pose, NULL); i++;
        }
        else if (!strcmp(a, "--dump") && v) { dump_at = strtoul(v, NULL, 16); const char *c2 = strchr(v, ':'); dump_len = c2 ? strtoul(c2 + 1, NULL, 16) : 0x100; i++; }
        else if (!strcmp(a, "--hash-from") && v) { hash_from = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--coverage") && v) { coverage = v; i++; }
        else if (!strcmp(a, "--opl-log") && v) { opl_log = v; i++; }
        else if (!strcmp(a, "--midi-log") && v) { midi_log = v; i++; }
        else if (!strcmp(a, "--speaker-log") && v) { speaker_log = v; i++; }
        else if (!strcmp(a, "--sound-sweep") && v) { sweep_ids = v; i++; }
        else if (!strcmp(a, "--sound-span") && v) { sweep_span = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--sound-repeat") && v) { sweep_repeat = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--sound-out") && v) { sweep_out = v; i++; }
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
            if (strcmp(v, "replay") && strcmp(v, "scan") && strcmp(v, "interp") && strcmp(v, "rerun")) {
                fprintf(stderr, "--present takes scan, replay, interp or rerun\n");
                return 2;
            }
            g_rerun = !strcmp(v, "rerun");
            g_replay = !strcmp(v, "replay") ? 1 : !strcmp(v, "interp") || g_rerun ? 2 : 0;
            i++;
        }
        else if (!strcmp(a, "--present-scale") && v) {
            g_scale = atoi(v);
            if (g_scale < 1 || g_scale > 16) { fprintf(stderr, "--present-scale takes 1 to 16\n"); return 2; }
            i++;
        }
        else if (!strcmp(a, "--present-line-width") && v) {
            char *end;
            const long width = strtol(v, &end, 10);
            if (!*v || *end || width < 0 || width > 20) { fprintf(stderr, "--present-line-width takes 0 to 20 (0 scales with resolution)\n"); return 2; }
            g_line_width = (int)width;
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
            if (!strcmp(v, "none")) fixes_enable("all", 0);
            else if (!fixes_enable(v, 1)) { fprintf(stderr, "no fix %s (--list-fixes)\n", v); return 2; }
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
        if (!inputlog_read_cadence(replay, &sim_hz)) {
            fprintf(stderr, "%s has an invalid simulation cadence\n", replay); return 2;
        }
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
        drawlive_set_line_width(&g_live, g_line_width);
        if (g_rerun) { drawfeed_set_rerun(&g_feed); drawlive_set_rerun(&g_live); }
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
        inputlog_cadence(g_record, sim_hz);
        char ids[256];
        fixes_enabled(ids, sizeof ids);
        inputlog_fixes(g_record, ids);
    }
    if (!machine_boot(&m, mem, data, save, "F117.COM", ips, time_us, &hooks)) {
        fprintf(stderr, "%s\n", m.fault);
        return 1;
    }
    m.on_input = on_input;
    m.sim_hz = sim_hz;
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
    if (sweep_ids && sound_sweep(&m, sweep_ids, sweep_span ? sweep_span : m.ips * 2, sweep_repeat, sweep_out)) fprintf(stderr, "the sound sweep did not complete\n");
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
        if (g_scale > 1) {
            uint64_t hs[HUD_STATS];
            hud_overlay_stats(&g_live.list, hs);
            printf("[present] state-driven HUD (the replay since its last seed): ");
            for (int k = 0; k < HUD_STATS; k++) printf("%s%s %llu", k ? ", " : "", hud_overlay_stat_name(k), (unsigned long long)hs[k]);
            printf("\n");
            const band_cover *bc = &g_live.hr->band;
            printf("[present] state-driven sky and ground (logic frames since the last seed): band passes %llu, at low detail %llu, "
                   "bands drawn from the state %llu, kept the original's (wrapped) %llu, records that left the prediction %llu, "
                   "band polygons painted %llu, left scaled %llu\n",
                   (unsigned long long)bc->passes, (unsigned long long)bc->low_detail, (unsigned long long)bc->replaced,
                   (unsigned long long)bc->kept, (unsigned long long)bc->mismatched, (unsigned long long)bc->painted,
                   (unsigned long long)bc->scaled);
        }
        if (g_scale > 1 && getenv("F117_PRESENT_DIAG") && *getenv("F117_PRESENT_DIAG"))
            for (int k = 0; k < HS_COUNT; k++)
                printf("[diag] %s: %llu\n", hires_stat_name(k), (unsigned long long)g_live.hr->stats[k]);
        if (g_scale > 1 && g_replay == 2 && getenv("F117_PRESENT_DIAG") && *getenv("F117_PRESENT_DIAG"))
            for (int k = 0; k < IS_COUNT; k++)
                printf("[diag] interp %s: %llu\n", interp_stat_name(k), (unsigned long long)g_live.istats[k]);
        printf("[present] replay: %llu logic frames, %llu equal to the display at their close, %llu not, %llu before a seed; "
               "%llu dropped\n", (unsigned long long)g_live.frames, (unsigned long long)g_live.exact,
               (unsigned long long)g_live.inexact, (unsigned long long)g_live.unseeded, (unsigned long long)g_feed.dropped);
        if (g_rerun) {
            printf("[present] rerun: %llu in-between pictures drawn by the original on a copy, %llu fell back to "
                   "interpolation; at t = 0 the copy drew its frame exactly %llu times, not %llu; %.0f instructions a copy\n",
                   (unsigned long long)g_live.rerun_drawn, (unsigned long long)g_live.rerun_fell_back,
                   (unsigned long long)g_live.rerun_t0_exact, (unsigned long long)g_live.rerun_t0_inexact,
                   g_live.rerun_drawn + g_live.rerun_t0_exact + g_live.rerun_t0_inexact
                       ? (double)g_live.rerun_insn / (double)(g_live.rerun_drawn + g_live.rerun_t0_exact + g_live.rerun_t0_inexact) : 0.0);
            printf("[present] isolation: %llu copies checked, %llu changed the real machine, %llu DOS calls blocked\n",
                   (unsigned long long)g_live.rlist.checks, (unsigned long long)g_live.rlist.changed,
                   (unsigned long long)g_live.rlist.blocked);
        }
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
