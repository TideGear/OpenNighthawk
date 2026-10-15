/* rerun_probe.c - EXPERIMENT (branch draw-rerun-probe, not for master); see rerun_probe.h. */
#include "rerun_probe.h"
#include "observe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern void (*g_frame_compose_probe)(machine_t *m);

/* records: hash and kind; vertices: x, y, range, batch start (4 values each) */
typedef struct { uint64_t *h; char *k; int n, cap; int32_t *vx; int nv, cv; } sink_t;

#define RING 4
static machine_t g_snap[RING];
static uint8_t  *g_snap_mem[RING];
static sink_t    g_real[RING], g_scr;
static sink_t   *g_sink;
static long long g_nsnap, g_done;
static int       g_in_scratch;
static int       g_nudge_slots;   /* traffic slots the last nudged copy moved */
static int       g_nudge_weapons; /* weapons in flight it moved */
static long long g_weapon_frames;
static long long g_slot_frames, g_slot_sum;
static FILE     *g_out;
static uint8_t  *g_scr_mem;
static machine_t g_scr_m;
static struct {
    long long frames, skipped_view, skipped_late, trapped, equal, unequal, mem_changed, nudge_frames;
    double us_sum, us_max; unsigned long long insn_sum, insn_max;
    unsigned long long v_cmp, v_ok, v_mid, v_tripled;
    long long equal_wo_n;
} g_tot;

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001B3ull; }
    return h;
}

static void push_h(sink_t *s, uint64_t h, char kind)
{
    if (s->n == s->cap) {
        s->cap = s->cap ? 2 * s->cap : 4096;
        s->h = realloc(s->h, sizeof *s->h * (size_t)s->cap);
        s->k = realloc(s->k, (size_t)s->cap);
    }
    s->k[s->n] = kind;
    s->h[s->n++] = h;
}

static int32_t g_caller;   /* the last model request's caller (its 'I' record) */

static void push_v(sink_t *s, int32_t x, int32_t y, int32_t range, int32_t start)
{
    if (s->nv + 5 > s->cv) { s->cv = s->cv ? 2 * s->cv : 4096; s->vx = realloc(s->vx, sizeof *s->vx * (size_t)s->cv); }
    s->vx[s->nv++] = x; s->vx[s->nv++] = y; s->vx[s->nv++] = range; s->vx[s->nv++] = start; s->vx[s->nv++] = g_caller;
}

static void ob_phase(void *u, uint64_t ic) { (void)u; (void)ic; if (g_sink) push_h(g_sink, fnv(0xCBF29CE484222325ull, "P", 1), 'P'); }

static void ob_vertex(void *u, uint64_t ic, const int32_t xf[3], const int32_t px[2], int range, uint16_t xa, uint16_t pa)
{
    (void)u; (void)ic;
    if (!g_sink) return;
    uint64_t h = fnv(0xCBF29CE484222325ull, "V", 1);
    h = fnv(h, xf, 12); h = fnv(h, &range, sizeof range); h = fnv(h, &xa, 2); h = fnv(h, &pa, 2);
    if (range != 2) h = fnv(h, px, 8);
    push_h(g_sink, h, 'V');
    push_v(g_sink, range != 2 ? px[0] : 0, range != 2 ? px[1] : 0, range, xa == 0xC6B4);
}

static void ob_prim(void *u, uint64_t ic, char kind, const int32_t *v, int n)
{
    (void)u; (void)ic;
    if (!g_sink) return;
    if (kind == 'I') { if (n > 3) g_caller = v[2] | v[3] << 16; return; }   /* identity: not a draw record */
    uint64_t h = fnv(0xCBF29CE484222325ull, &kind, 1);
    push_h(g_sink, fnv(h, v, sizeof *v * (size_t)n), kind);
}

static f117_observer g_ob = { .user = NULL, .frame_phase = ob_phase, .vertex = ob_vertex, .prim = ob_prim, .ident = 1 };

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
        /* Computer-flown aircraft (VGAME 5852): 24h-byte slots at C16A, [DEFC] of them; +6 height, +8/+0C 32-bit
         * X/Y, +10/+12/+14 heading/pitch/roll, +16 type, +18 state. A slot moves halfway only when it holds the
         * same type in the same state in both frames (a reused slot steps instead of gliding). */
        g_nudge_slots = 0;
        /* Weapons in flight (VGAME 683E): twelve 1Ch-byte records at 3C3A; from its movement code +0/+2 X/Y and +4
         * height (16-bit), +6 pitch, +8 heading, +0E life (0 = ended), +10 type. A weapon moves halfway only when it
         * is alive with the same type in both frames. */
        if (getenv("F117_RERUN_NUDGE_WEAPONS"))
            for (int i = 0; i < 12; i++) {
                const uint16_t at = (uint16_t)(0x3C3A + 0x1C * i);
                if (!rd16(amem, ds, (uint16_t)(at + 0x0E)) || !rd16(bmem, bds, (uint16_t)(at + 0x0E)) ||
                    rd16(amem, ds, (uint16_t)(at + 0x10)) != rd16(bmem, bds, (uint16_t)(at + 0x10))) continue;
                static const uint16_t f16[] = { 0x00, 0x02, 0x04, 0x06, 0x08 };
                for (int f = 0; f < 5; f++) {
                    const uint16_t o = (uint16_t)(at + f16[f]);
                    const uint16_t va = rd16(amem, ds, o), vb = rd16(bmem, bds, o);
                    wr16(g_scr_mem, ds, o, (uint16_t)(va + (int16_t)(vb - va) / 2));
                }
                g_nudge_weapons++;
            }
        if (getenv("F117_RERUN_NUDGE_TRAFFIC")) {
            const uint16_t n = rd16(amem, ds, 0xDEFC);
            for (uint16_t i = 0; i < n && i < 64 && n == rd16(bmem, bds, 0xDEFC); i++) {
                const uint16_t at = (uint16_t)(0xC16A + 0x24 * i);
                if (rd16(amem, ds, (uint16_t)(at + 0x16)) != rd16(bmem, bds, (uint16_t)(at + 0x16)) ||
                    rd16(amem, ds, (uint16_t)(at + 0x18)) != rd16(bmem, bds, (uint16_t)(at + 0x18))) continue;
                for (int f = 0; f < 2; f++) {
                    const uint16_t o = (uint16_t)(at + (f ? 0x0C : 0x08));
                    const uint32_t va = rd32(amem, ds, o), vb = rd32(bmem, bds, o);
                    const uint32_t m2 = va + (uint32_t)((int32_t)(vb - va) / 2);
                    wr16(g_scr_mem, ds, o, (uint16_t)m2); wr16(g_scr_mem, ds, (uint16_t)(o + 2), (uint16_t)(m2 >> 16));
                    /* +2/+4 hold the same position at 1/32 of the precision (observed: x32 >> 5) */
                    wr16(g_scr_mem, ds, (uint16_t)(at + (f ? 4 : 2)), (uint16_t)(m2 >> 5));
                }
                static const uint16_t f16[] = { 0x06, 0x10, 0x12, 0x14 };
                for (int f = 0; f < 4; f++) {
                    const uint16_t o = (uint16_t)(at + f16[f]);
                    const uint16_t va = rd16(amem, ds, o), vb = rd16(bmem, bds, o);
                    wr16(g_scr_mem, ds, o, (uint16_t)(va + (int16_t)(vb - va) / 2));
                }
                g_nudge_slots++;
                if (getenv("F117_RERUN_SLOT_DEBUG") && i == 0 && g_out)
                    fprintf(g_out, "  slot0 a: cell %04X %04X h %04X x %08X y %08X hdg %04X type %04X state %04X | b: cell %04X %04X x %08X y %08X\n",
                            rd16(amem, ds, (uint16_t)(at + 2)), rd16(amem, ds, (uint16_t)(at + 4)), rd16(amem, ds, (uint16_t)(at + 6)),
                            rd32(amem, ds, (uint16_t)(at + 8)), rd32(amem, ds, (uint16_t)(at + 0x0C)), rd16(amem, ds, (uint16_t)(at + 0x10)),
                            rd16(amem, ds, (uint16_t)(at + 0x16)), rd16(amem, ds, (uint16_t)(at + 0x18)),
                            rd16(bmem, bds, (uint16_t)(at + 2)), rd16(bmem, bds, (uint16_t)(at + 4)),
                            rd32(bmem, bds, (uint16_t)(at + 8)), rd32(bmem, bds, (uint16_t)(at + 0x0C)));
            }
        }
    }
    const uint16_t ss = s->cpu.seg[S_SS], sp = s->cpu.r[R_SP];
    s->trap_on = 1; s->trap_cs = s->cpu.seg[S_CS]; s->trap_ip = rd16(g_scr_mem, ss, sp); s->trap_sp = (uint16_t)(sp + 2);
    g_scr.n = g_scr.nv = 0;
    sink_t *saved = g_sink;
    const int32_t saved_caller = g_caller;
    g_sink = &g_scr; g_in_scratch = 1;
    const uint64_t start = s->cpu.icount;
    const double t0 = now_us();
    int rc;
    do rc = machine_run(s, s->cpu.icount + 2000000); while (rc == RUN_SLICE && s->cpu.icount - start < 3000000ull);
    *us = now_us() - t0;
    *insn = s->cpu.icount - start;
    g_in_scratch = 0; g_sink = saved; g_caller = saved_caller;
    if (rc != RUN_TRAP && g_out)
        fprintf(g_out, "  no return: rc %d at %04X:%04X SP %04X; trap %04X:%04X SP %04X; entry stack %04X %04X %04X; fault '%s'\n",
                rc, s->cpu.seg[S_CS], s->cpu.ip, s->cpu.r[R_SP], s->trap_cs, s->trap_ip, s->trap_sp,
                rd16(amem, ss, sp), rd16(amem, ss, (uint16_t)(sp + 2)), rd16(amem, ss, (uint16_t)(sp + 4)), s->fault);
    return rc == RUN_TRAP;
}

/* The original's batches (one per model chunk, starting at the C6B4 slot) of the first nv vertices. */
#define MAXB 4096
typedef struct { int at[MAXB], n[MAXB], count; } batches_t;

static void batches(const int32_t *vx, int nv, batches_t *b)
{
    b->count = 0;
    for (int i = 0; i < nv; i++) {
        if ((vx[5 * i + 3] || b->count == 0) && b->count < MAXB) { b->at[b->count] = i; b->n[b->count] = 0; b->count++; }
        b->n[b->count - 1]++;
    }
}

/* Align two batch lists in order by equal vertex counts (longest common subsequence): map[i] = j or -1. */
static void align(const batches_t *a, const batches_t *b, int *map)
{
    const int na = a->count, nb = b->count;
    int *best = calloc((size_t)(na + 1) * (nb + 1), sizeof *best);
#define BEST(i, j) best[(size_t)(i) * (nb + 1) + (j)]
    for (int i = na - 1; i >= 0; i--)
        for (int j = nb - 1; j >= 0; j--) {
            int v = BEST(i + 1, j) > BEST(i, j + 1) ? BEST(i + 1, j) : BEST(i, j + 1);
            if (a->n[i] == b->n[j] && BEST(i + 1, j + 1) + 1 > v) v = BEST(i + 1, j + 1) + 1;
            BEST(i, j) = v;
        }
    for (int i = 0; i < na; i++) map[i] = -1;
    for (int i = 0, j = 0; i < na && j < nb;) {
        if (a->n[i] == b->n[j] && BEST(i, j) == BEST(i + 1, j + 1) + 1) { map[i] = j; i++; j++; }
        else if (BEST(i + 1, j) >= BEST(i, j + 1)) i++;
        else j++;
    }
#undef BEST
    free(best);
}

static batches_t g_ba, g_bb, g_bm;
#define NCALL 64
static struct { int32_t caller; unsigned long long cmp, good; } g_call[NCALL];
static int g_ncall;

static void tally(int32_t caller, int good)
{
    int k = 0;
    while (k < g_ncall && g_call[k].caller != caller) k++;
    if (k == g_ncall) { if (g_ncall == NCALL) return; g_call[g_ncall++].caller = caller; }
    g_call[k].cmp++; g_call[k].good += good;
}
static int g_map_ma[MAXB], g_map_ab[MAXB];

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
    /* without the 'n' records (the observer's own pending-line state, not the machine's) */
    int bad_wo_n = 0;
    for (int i = 0, j = 0;; i++, j++) {
        while (i < g_scr.n && g_scr.k[i] == 'n') i++;
        while (j < r->n && r->k[j] == 'n') j++;
        if (i >= g_scr.n) break;
        if (j >= r->n || g_scr.h[i] != r->h[j]) { bad_wo_n = 1; break; }
    }
    g_tot.equal_wo_n += ok && !bad_wo_n && g_scr.n > 0;
    const int equal = ok && first_bad < 0 && g_scr.n > 0;
    g_tot.equal += equal; g_tot.unequal += !equal;
    g_tot.us_sum += us; if (us > g_tot.us_max) g_tot.us_max = us;
    g_tot.insn_sum += insn; if (insn > g_tot.insn_max) g_tot.insn_max = insn;
    const int nv_a = g_scr.nv / 5;                               /* this frame's 2F9D vertices */
    if (!equal && first_bad >= 0 && g_out) {
        fprintf(g_out, "  unequal at record %d: copy kind '%c', real kind '%c'; copy around:", first_bad,
                first_bad < g_scr.n ? g_scr.k[first_bad] : '-', first_bad < r->n ? r->k[first_bad] : '-');
        for (int i = first_bad > 3 ? first_bad - 3 : 0; i < first_bad + 4 && i < g_scr.n; i++) fprintf(g_out, " %c", g_scr.k[i]);
        fprintf(g_out, "; real around:");
        for (int i = first_bad > 3 ? first_bad - 3 : 0; i < first_bad + 4 && i < r->n; i++) fprintf(g_out, " %c", r->k[i]);
        fprintf(g_out, "\n");
    }
    /* frame k+1's own 2F9D vertex count, from an unnudged copy of it */
    unsigned long long insn_b; double us_b;
    scratch(b, g_snap_mem[kb], NULL, NULL, &insn_b, &us_b);
    const int nv_b = g_scr.nv / 5;
    /* nudged halfway to frame k+1 */
    unsigned long long insn2; double us2;
    const int ok2 = scratch(a, g_snap_mem[ka], b, g_snap_mem[kb], &insn2, &us2);
    unsigned long long cmp = 0, good = 0, mid = 0, tripled = 0;
    g_slot_frames += g_nudge_slots > 0; g_slot_sum += g_nudge_slots; g_weapon_frames += g_nudge_weapons > 0; g_nudge_weapons = 0;
    const sink_t *rb = &g_real[kb];
    if (ok2 && rb->nv / 5 >= nv_b) {
        g_tot.nudge_frames++;
        batches(r->vx, nv_a, &g_ba); batches(rb->vx, nv_b, &g_bb); batches(g_scr.vx, g_scr.nv / 5, &g_bm);
        align(&g_bm, &g_ba, g_map_ma); align(&g_ba, &g_bb, g_map_ab);
        for (int i = 0; i < g_scr.nv / 5; i++) mid += g_scr.vx[5 * i + 2] != 2;
        for (int x = 0; x < g_bm.count; x++) {
            const int ia = g_map_ma[x], ib = ia >= 0 ? g_map_ab[ia] : -1;
            if (ib < 0) continue;
            for (int q = 0; q < g_bm.n[x]; q++) {
                const int32_t *vm = &g_scr.vx[5 * (g_bm.at[x] + q)], *va = &r->vx[5 * (g_ba.at[ia] + q)], *vb = &rb->vx[5 * (g_bb.at[ib] + q)];
                if (vm[2] == 2 || va[2] == 2 || vb[2] == 2) continue;
                tripled++;
                const double mx = 0.5 * (va[0] + vb[0]), my = 0.5 * (va[1] + vb[1]);
                const double dx = abs(vb[0] - va[0]), dy = abs(vb[1] - va[1]);
                const double ex = vm[0] - mx, ey = vm[1] - my;
                cmp++;
                const int ok_v = (ex < 0 ? -ex : ex) <= 1.0 + 0.25 * dx && (ey < 0 ? -ey : ey) <= 1.0 + 0.25 * dy;
                good += ok_v;
                tally(vm[4], ok_v);
            }
        }
    }
    g_tot.v_mid += mid; g_tot.v_tripled += tripled;
    g_tot.v_cmp += cmp; g_tot.v_ok += good;
    const uint64_t h1 = fnv(0xCBF29CE484222325ull, m->mem, MEM_SIZE) ^ machine_state_hash(m);
    g_tot.mem_changed += h0 != h1;
    if (g_out)
        fprintf(g_out, "%lld icount %llu trap %d records %d/%d first_bad %d equal %d insn %llu us %.0f | nudged trap %d "
                "verts a %d b %d mid %d, matched %llu of %llu, within %llu insn %llu us %.0f slots %d | real_mem_changed %d\n",
                k, (unsigned long long)a->cpu.icount, ok, g_scr.n, r->n, first_bad, equal, insn, us, ok2,
                nv_a, nv_b, g_scr.nv / 5, tripled, mid, good, insn2, us2, g_nudge_slots, h0 != h1);
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
            "nudged frames compared %lld, nudged vertices matched to both real frames %llu of %llu, within the midpoint tolerance %llu of %llu\n",
            g_tot.frames, g_tot.skipped_view, g_tot.skipped_late, n, g_tot.trapped, g_tot.equal, g_tot.unequal, g_tot.mem_changed,
            n ? (double)g_tot.insn_sum / n : 0.0, g_tot.insn_max, n ? g_tot.us_sum / n : 0.0, g_tot.us_max,
            g_tot.nudge_frames, g_tot.v_tripled, g_tot.v_mid, g_tot.v_ok, g_tot.v_cmp);
    fprintf(g_out, "traffic slots nudged: %lld frames with any, %lld slot-frames; weapons nudged in %lld frames; "
            "re-runs equal without the observer's 'n' records %lld\n",
            g_slot_frames, g_slot_sum, g_weapon_frames, g_tot.equal_wo_n);
    for (int k = 0; k < g_ncall; k++)
        fprintf(g_out, "caller %04X:%04X: %llu of %llu vertices within the midpoint tolerance\n",
                (unsigned)(g_call[k].caller >> 16) & 0xFFFF, (unsigned)g_call[k].caller & 0xFFFF, g_call[k].good, g_call[k].cmp);
    fclose(g_out);
    g_out = NULL;
}
