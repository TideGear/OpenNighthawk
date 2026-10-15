/* rerun_probe.c - EXPERIMENT (branch draw-rerun-probe, not for master); see rerun_probe.h. */
#include "rerun_probe.h"
#include "observe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern void (*g_frame_compose_probe)(machine_t *m);

typedef struct { uint64_t *h; int n, cap; int32_t *vx; int nv, cv; } sink_t;

#define RING 4
static machine_t g_snap[RING];
static uint8_t  *g_snap_mem[RING];
static sink_t    g_real[RING], g_scr;
static sink_t   *g_sink;
static long long g_nsnap, g_done;
static int       g_in_scratch;
static FILE     *g_out;
static uint8_t  *g_scr_mem;
static machine_t g_scr_m;
static struct {
    long long frames, skipped_view, skipped_late, trapped, equal, unequal, mem_changed, nudge_frames;
    double us_sum, us_max; unsigned long long insn_sum, insn_max;
    unsigned long long v_cmp, v_ok;
} g_tot;

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001B3ull; }
    return h;
}

static void push_h(sink_t *s, uint64_t h)
{
    if (s->n == s->cap) { s->cap = s->cap ? 2 * s->cap : 4096; s->h = realloc(s->h, sizeof *s->h * (size_t)s->cap); }
    s->h[s->n++] = h;
}

static void push_v(sink_t *s, int32_t x, int32_t y, int32_t range)
{
    if (s->nv + 3 > s->cv) { s->cv = s->cv ? 2 * s->cv : 4096; s->vx = realloc(s->vx, sizeof *s->vx * (size_t)s->cv); }
    s->vx[s->nv++] = x; s->vx[s->nv++] = y; s->vx[s->nv++] = range;
}

static void ob_phase(void *u, uint64_t ic) { (void)u; (void)ic; if (g_sink) push_h(g_sink, fnv(0xCBF29CE484222325ull, "P", 1)); }

static void ob_vertex(void *u, uint64_t ic, const int32_t xf[3], const int32_t px[2], int range, uint16_t xa, uint16_t pa)
{
    (void)u; (void)ic;
    if (!g_sink) return;
    uint64_t h = fnv(0xCBF29CE484222325ull, "V", 1);
    h = fnv(h, xf, 12); h = fnv(h, &range, sizeof range); h = fnv(h, &xa, 2); h = fnv(h, &pa, 2);
    if (range != 2) h = fnv(h, px, 8);
    push_h(g_sink, h);
    push_v(g_sink, range != 2 ? px[0] : 0, range != 2 ? px[1] : 0, range);
}

static void ob_prim(void *u, uint64_t ic, char kind, const int32_t *v, int n)
{
    (void)u; (void)ic;
    if (!g_sink) return;
    uint64_t h = fnv(0xCBF29CE484222325ull, &kind, 1);
    push_h(g_sink, fnv(h, v, sizeof *v * (size_t)n));
}

static f117_observer g_ob = { .user = NULL, .frame_phase = ob_phase, .vertex = ob_vertex, .prim = ob_prim };

static void probe(machine_t *m)
{
    if (g_in_scratch) return;
    const int slot = (int)(g_nsnap % RING);
    if (!g_snap_mem[slot]) g_snap_mem[slot] = malloc(MEM_SIZE);
    g_snap[slot] = *m;
    memcpy(g_snap_mem[slot], m->mem, MEM_SIZE);
    g_real[slot].n = g_real[slot].nv = 0;
    g_sink = &g_real[slot];
    g_nsnap++;
}

static uint16_t rd16(const uint8_t *mem, uint16_t seg, uint16_t off)
{
    const uint32_t a = ((uint32_t)seg << 4) + off;
    return (uint16_t)(mem[a & 0xFFFFF] | mem[(a + 1) & 0xFFFFF] << 8);
}

static void wr16(uint8_t *mem, uint16_t seg, uint16_t off, uint16_t v)
{
    const uint32_t a = ((uint32_t)seg << 4) + off;
    mem[a & 0xFFFFF] = (uint8_t)v; mem[(a + 1) & 0xFFFFF] = (uint8_t)(v >> 8);
}

static uint32_t rd32(const uint8_t *mem, uint16_t seg, uint16_t off)
{
    return rd16(mem, seg, off) | (uint32_t)rd16(mem, seg, (uint16_t)(off + 2)) << 16;
}

static double now_us(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

/* Run 2F9D on a copy of snapshot a to its return; with b, nudge the aircraft halfway to b first. */
static int scratch(const machine_t *a, const uint8_t *amem, const machine_t *b, const uint8_t *bmem,
                   unsigned long long *insn, double *us)
{
    if (!g_scr_mem) g_scr_mem = malloc(MEM_SIZE);
    machine_t *s = &g_scr_m;
    *s = *a;
    memcpy(g_scr_mem, amem, MEM_SIZE);
    s->mem = g_scr_mem; s->cpu.mem = g_scr_mem;
    s->cpu.user = s;                    /* the CPU calls back into its machine: this copy, never the real one */
    s->cpu.cover = NULL;
    memset(&s->hooks, 0, sizeof s->hooks);
    s->log = NULL; s->on_input = NULL; s->on_input_user = NULL;
    s->opl_sched = NULL; s->opl_sched_n = s->opl_sched_i = s->opl_sched_cap = 0;
    snprintf(s->data_dir, sizeof s->data_dir, "Z:/f117-rerun-no-files");
    snprintf(s->save_dir, sizeof s->save_dir, "Z:/f117-rerun-no-files");
    const uint16_t ds = s->cpu.seg[S_DS];
    if (b) {
        const uint16_t bds = b->cpu.seg[S_DS];
        static const uint16_t pos32[] = { 0xBA3C, 0xC056 };
        for (int k = 0; k < 2; k++) {
            const uint32_t va = rd32(amem, ds, pos32[k]), vb = rd32(bmem, bds, pos32[k]);
            const uint32_t mid = va + (uint32_t)((int32_t)(vb - va) / 2);
            wr16(g_scr_mem, ds, pos32[k], (uint16_t)mid); wr16(g_scr_mem, ds, (uint16_t)(pos32[k] + 2), (uint16_t)(mid >> 16));
        }
        static const uint16_t w16[] = { 0x2DEE, 0x2DF0, 0x2DF2, 0x2DF4, 0x2DF6 };
        for (int k = 0; k < 5; k++) {
            const uint16_t va = rd16(amem, ds, w16[k]), vb = rd16(bmem, bds, w16[k]);
            wr16(g_scr_mem, ds, w16[k], (uint16_t)(va + (int16_t)(vb - va) / 2));
        }
    }
    const uint16_t ss = s->cpu.seg[S_SS], sp = s->cpu.r[R_SP];
    s->trap_on = 1; s->trap_cs = s->cpu.seg[S_CS]; s->trap_ip = rd16(g_scr_mem, ss, sp); s->trap_sp = (uint16_t)(sp + 2);
    g_scr.n = g_scr.nv = 0;
    sink_t *saved = g_sink;
    g_sink = &g_scr; g_in_scratch = 1;
    const uint64_t start = s->cpu.icount;
    const double t0 = now_us();
    int rc;
    do rc = machine_run(s, s->cpu.icount + 2000000); while (rc == RUN_SLICE && s->cpu.icount - start < 3000000ull);
    *us = now_us() - t0;
    *insn = s->cpu.icount - start;
    g_in_scratch = 0; g_sink = saved;
    if (rc != RUN_TRAP && g_out)
        fprintf(g_out, "  no return: rc %d at %04X:%04X SP %04X; trap %04X:%04X SP %04X; entry stack %04X %04X %04X; fault '%s'\n",
                rc, s->cpu.seg[S_CS], s->cpu.ip, s->cpu.r[R_SP], s->trap_cs, s->trap_ip, s->trap_sp,
                rd16(amem, ss, sp), rd16(amem, ss, (uint16_t)(sp + 2)), rd16(amem, ss, (uint16_t)(sp + 4)), s->fault);
    return rc == RUN_TRAP;
}

static void process(machine_t *m, long long k)
{
    const int ka = (int)(k % RING), kb = (int)((k + 1) % RING);
    const machine_t *a = &g_snap[ka], *b = &g_snap[kb];
    g_tot.frames++;
    const uint16_t ds = a->cpu.seg[S_DS];
    if (b->cpu.seg[S_DS] != ds || rd16(g_snap_mem[ka], ds, 0xC0A6) != rd16(g_snap_mem[kb], ds, 0xC0A6)) { g_tot.skipped_view++; return; }
    const uint64_t h0 = fnv(0xCBF29CE484222325ull, m->mem, MEM_SIZE) ^ machine_state_hash(m);
    unsigned long long insn; double us;
    const int ok = scratch(a, g_snap_mem[ka], NULL, NULL, &insn, &us);
    g_tot.trapped += ok;
    /* the copy's records must be the start of what the real run recorded from this entry */
    const sink_t *r = &g_real[ka];
    int first_bad = -1;
    for (int i = 0; i < g_scr.n; i++) if (i >= r->n || g_scr.h[i] != r->h[i]) { first_bad = i; break; }
    const int equal = ok && first_bad < 0 && g_scr.n > 0;
    g_tot.equal += equal; g_tot.unequal += !equal;
    g_tot.us_sum += us; if (us > g_tot.us_max) g_tot.us_max = us;
    g_tot.insn_sum += insn; if (insn > g_tot.insn_max) g_tot.insn_max = insn;
    const int nv_a = g_scr.nv;                                   /* this frame's 2F9D vertices */
    /* nudged halfway to frame k+1 */
    unsigned long long insn2; double us2;
    const int ok2 = scratch(a, g_snap_mem[ka], b, g_snap_mem[kb], &insn2, &us2);
    unsigned long long cmp = 0, good = 0;
    const sink_t *rb = &g_real[kb];
    if (ok2 && g_scr.nv == nv_a && rb->nv >= nv_a) {
        g_tot.nudge_frames++;
        for (int i = 0; i + 2 < nv_a; i += 3) {
            if (r->vx[i + 2] == 2 || rb->vx[i + 2] == 2 || g_scr.vx[i + 2] == 2) continue;
            const double mx = 0.5 * (r->vx[i] + rb->vx[i]), my = 0.5 * (r->vx[i + 1] + rb->vx[i + 1]);
            const double dx = abs(rb->vx[i] - r->vx[i]), dy = abs(rb->vx[i + 1] - r->vx[i + 1]);
            const double ex = g_scr.vx[i] - mx, ey = g_scr.vx[i + 1] - my;
            cmp++;
            good += (ex < 0 ? -ex : ex) <= 1.0 + 0.25 * dx && (ey < 0 ? -ey : ey) <= 1.0 + 0.25 * dy;
        }
    }
    g_tot.v_cmp += cmp; g_tot.v_ok += good;
    const uint64_t h1 = fnv(0xCBF29CE484222325ull, m->mem, MEM_SIZE) ^ machine_state_hash(m);
    g_tot.mem_changed += h0 != h1;
    if (g_out)
        fprintf(g_out, "%lld icount %llu trap %d records %d/%d first_bad %d equal %d insn %llu us %.0f | nudged trap %d "
                "verts %d/%d/%d within %llu/%llu insn %llu us %.0f | real_mem_changed %d\n",
                k, (unsigned long long)a->cpu.icount, ok, g_scr.n, r->n, first_bad, equal, insn, us, ok2,
                nv_a, g_scr.nv, rb->nv, good, cmp, insn2, us2, h0 != h1);
}

int rerun_probe_start(machine_t *m, const char *path)
{
    (void)m;
    g_out = fopen(path, "w");
    if (!g_out) return 0;
    g_frame_compose_probe = probe;
    observe_set(&g_ob);
    return 1;
}

void rerun_probe_service(machine_t *m)
{
    if (g_nsnap - g_done > RING - 1) { g_tot.skipped_late += g_nsnap - (RING - 1) - g_done; g_done = g_nsnap - (RING - 1); }
    while (g_done + 2 < g_nsnap) process(m, g_done++);
}

void rerun_probe_finish(void)
{
    if (!g_out) return;
    const long long n = g_tot.equal + g_tot.unequal;
    fprintf(g_out, "SUMMARY frames %lld (skipped: view change %lld, late %lld) re-runs %lld returned %lld; records equal to the "
            "real run %lld, unequal %lld; real memory changed %lld; insn mean %.0f max %llu; host us mean %.0f max %.0f; "
            "nudged frames compared %lld, vertices within the midpoint tolerance %llu of %llu\n",
            g_tot.frames, g_tot.skipped_view, g_tot.skipped_late, n, g_tot.trapped, g_tot.equal, g_tot.unequal, g_tot.mem_changed,
            n ? (double)g_tot.insn_sum / n : 0.0, g_tot.insn_max, n ? g_tot.us_sum / n : 0.0, g_tot.us_max,
            g_tot.nudge_frames, g_tot.v_ok, g_tot.v_cmp);
    fclose(g_out);
    g_out = NULL;
}
