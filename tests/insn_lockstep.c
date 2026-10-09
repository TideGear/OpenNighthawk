/* insn_lockstep.c - every translated instruction of the game, one at a time,
 * held to the interpreter.
 *
 *     insn_lockstep [--states N] [--seed S] [--only NAME] [--timing386]
 *                   [--base-seg SEG] [--verbose]
 *
 * The routes prove parity for the code they run, about half of the game.
 * This covers the rest: for every instruction start the translation has, in
 * every module, the image is placed in memory, the machine is put in N
 * random states (registers, flags, every byte of memory outside the image),
 * and one instruction is run twice - by the machine's interpreter step
 * (cpu_step, which the silicon vectors validate, then the single-step trap
 * when TF is set), and by the generated region entered at that
 * instruction with the clock set to stop after it. Everything an
 * instruction can change is compared: registers, segments, IP, flags, the
 * clock and interrupt state, every byte written, every port read and
 * written, and every interrupt raised with the registers at that moment.
 *
 * A region may decline an instruction (return 0: the interpreter takes it);
 * those are counted, not compared. Build with -DF117R_GEN_DIR (the
 * generated code is linked in); the program says so if it is not.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_gen.h"

unsigned long long rc_mutant_hits;
int rc_instruction_budget = -1;

/* ---- the machine hooks: record, never emulate ----------------------------
 * Every write is logged (all memory is marked as code, so mem_write8 reports
 * each changed byte), every port read returns a value derived from the
 * port, width and count so far, and every port write and interrupt is
 * folded into a hash. Both CPUs get identical hooks. */

uint8_t cpu_codebits[MEM_SIZE / 8];

#define MAX_DIRTY 8192
typedef struct {
    uint32_t dirty[MAX_DIRTY];
    int      ndirty, overflow;
    uint64_t io;            /* port writes, reads, interrupts */
    uint32_t nread;
} side_t;

static side_t g_side[2];

static side_t *side_of(cpu_t *c) { return (side_t *)c->user; }

void cpu_code_written(cpu_t *c, uint32_t lin)
{
    side_t *s = side_of(c);
    if (s->ndirty < MAX_DIRTY) s->dirty[s->ndirty++] = lin;
    else s->overflow = 1;
}

void cpu_irq_state_changed(cpu_t *c) { (void)c; }

static uint64_t mix(uint64_t h, uint64_t v)
{
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h * 0x100000001B3ull;
}

static void io_w(cpu_t *c, uint16_t port, uint32_t val, int width)
{
    side_t *s = side_of(c);
    s->io = mix(s->io, 0x1000000ull | ((uint64_t)port << 8) | (uint64_t)width);
    s->io = mix(s->io, val);
}

static uint32_t io_r(cpu_t *c, uint16_t port, int width)
{
    side_t *s = side_of(c);
    s->nread++;
    s->io = mix(s->io, 0x2000000ull | ((uint64_t)port << 8) | (uint64_t)width);
    uint64_t v = mix((uint64_t)port * 31u + (uint64_t)width, s->nread);
    return width == 1 ? (uint32_t)(v & 0xFF) : (uint32_t)(v & 0xFFFF);
}

static int int_h(cpu_t *c, uint8_t vec)
{
    side_t *s = side_of(c);
    s->io = mix(s->io, 0x3000000ull | vec);
    for (int i = 0; i < 8; i++) s->io = mix(s->io, c->r[i]);
    for (int i = 0; i < 4; i++) s->io = mix(s->io, c->seg[i]);
    s->io = mix(s->io, ((uint64_t)c->ip << 16) | c->flags);
    return 0;                       /* not emulated: the CPU dispatches it */
}

/* ---- random states -------------------------------------------------------- */

static uint64_t g_rng;
static uint64_t rnd(void)
{
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return g_rng;
}

static uint8_t *g_pristine, *g_mem[2];
static cpu_t g_cpu[2];

static void restore(void)
{
    for (int k = 0; k < 2; k++) {
        side_t *s = &g_side[k];
        for (int i = 0; i < s->ndirty; i++) g_mem[k][s->dirty[i]] = g_pristine[s->dirty[i]];
        if (s->overflow) memcpy(g_mem[k], g_pristine, MEM_SIZE);
        s->ndirty = 0; s->overflow = 0; s->io = 0; s->nread = 0;
    }
}

static void set_state(cpu_t *c, uint16_t cs, uint16_t ip, const uint16_t *r, const uint16_t *seg, uint16_t flags)
{
    memcpy(c->r, r, sizeof c->r);
    memcpy(c->seg, seg, sizeof c->seg);
    c->seg[S_CS] = cs;
    c->ip = ip;
    c->op_ip = ip;
    c->op_cs = cs;
    c->flags = flags;
    c->seg_override = -1;
    c->rep_prefix = 0;
    c->halted = 0;
    c->icount = 1000;
    c->stop_at = 1001;
    c->inhibit_at = 0;
    c->int_depth = 0;
}

static int g_verbose, g_shown, g_timing386;

static int compare(const char *mod, uint32_t off, uint16_t cs, uint16_t ip, int declined)
{
    cpu_t *a = &g_cpu[0], *b = &g_cpu[1];
    char why[256] = "";
    static const char *RN[8] = { "AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI" };
    if (memcmp(a->r, b->r, sizeof a->r)) {
        int n = 0;
        for (int i = 0; i < 8; i++)
            if (a->r[i] != b->r[i])
                n += snprintf(why + n, sizeof why - (size_t)n, "%s %04X vs %04X ", RN[i], a->r[i], b->r[i]);
    }
    else if (memcmp(a->seg, b->seg, sizeof a->seg)) snprintf(why, sizeof why, "segments");
    else if (a->ip != b->ip) snprintf(why, sizeof why, "ip %04X vs %04X", a->ip, b->ip);
    else if (a->flags != b->flags) snprintf(why, sizeof why, "flags %04X vs %04X", a->flags, b->flags);
    else if (a->icount != b->icount) snprintf(why, sizeof why, "clock %llu vs %llu",
                                              (unsigned long long)a->icount, (unsigned long long)b->icount);
    else if (a->halted != b->halted) snprintf(why, sizeof why, "halted %d vs %d", a->halted, b->halted);
    else if (a->inhibit_at != b->inhibit_at) snprintf(why, sizeof why, "interrupt shadow");
    else if (g_timing386 && (a->t386_pf_bytes != b->t386_pf_bytes ||
             a->t386_pf_prefixes != b->t386_pf_prefixes ||
             a->t386_chunk != b->t386_chunk || a->t386_chunk_n != b->t386_chunk_n ||
             a->t386_chunk_held != b->t386_chunk_held))
        snprintf(why, sizeof why, "386 prefetch or REP chunk");
    else if (g_side[0].io != g_side[1].io) snprintf(why, sizeof why, "ports or interrupts");
    else if (g_side[0].overflow != g_side[1].overflow) snprintf(why, sizeof why, "write volume");
    else {
        for (int k = 0; k < 2 && !why[0]; k++)
            for (int i = 0; i < g_side[k].ndirty; i++) {
                uint32_t x = g_side[k].dirty[i];
                if (g_mem[0][x] != g_mem[1][x]) {
                    snprintf(why, sizeof why, "memory %05X: %02X vs %02X", x, g_mem[0][x], g_mem[1][x]);
                    break;
                }
            }
        if (!why[0] && g_side[0].overflow && memcmp(g_mem[0], g_mem[1], MEM_SIZE))
            snprintf(why, sizeof why, "memory");
    }
    (void)declined;
    if (!why[0]) return 0;
    if (g_shown++ < 40 || g_verbose)
    {
        const uint32_t lin = ((uint32_t)cs * 16u + ip) & 0xFFFFFu;
        printf("  MISMATCH %s+%05X (%04X:%04X) [%02X %02X %02X %02X %02X %02X]: %s\n", mod, off, cs, ip,
               g_pristine[lin], g_pristine[lin + 1], g_pristine[lin + 2], g_pristine[lin + 3],
               g_pristine[lin + 4], g_pristine[lin + 5], why);
    }
    return 1;
}

int main(int argc, char **argv)
{
    int states = 8;
    const char *only = NULL;
    uint16_t base = 0x1000;
    g_rng = 0x5EED0F117AULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--states") && i + 1 < argc) states = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) g_rng = strtoull(argv[++i], NULL, 0) | 1;
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--verbose")) g_verbose = 1;
        else if (!strcmp(argv[i], "--timing386")) g_timing386 = 1;
        else if (!strcmp(argv[i], "--base-seg") && i + 1 < argc) base = (uint16_t)strtoul(argv[++i], NULL, 0);
        else { fprintf(stderr, "usage: insn_lockstep [--states N] [--seed S] [--only NAME] [--timing386] [--base-seg SEG] [--verbose]\n"); return 2; }
    }
    if (RC_NMODULES == 0) { fprintf(stderr, "no generated code linked in (build with F117R_GEN_DIR)\n"); return 2; }

    memset(cpu_codebits, 0xFF, sizeof cpu_codebits);
    g_pristine = (uint8_t *)malloc(MEM_SIZE);
    g_mem[0] = (uint8_t *)malloc(MEM_SIZE);
    g_mem[1] = (uint8_t *)malloc(MEM_SIZE);
    if (!g_pristine || !g_mem[0] || !g_mem[1]) return 2;
    for (int k = 0; k < 2; k++) {
        cpu_init(&g_cpu[k], g_mem[k]);
        g_cpu[k].model = CPU_80286;
        g_cpu[k].user = &g_side[k];
        g_cpu[k].io_read = io_r;
        g_cpu[k].io_write = io_w;
        g_cpu[k].int_hook = int_h;
        g_cpu[k].cover = NULL;
    }

    unsigned long long tested = 0, declined = 0, bad = 0, total_insns = 0;
    for (unsigned mi = 0; mi < RC_NMODULES; mi++) {
        const rc_module *m = RC_MODULES[mi];
        if (only && _stricmp(only, m->name)) continue;
        /* Fresh random memory around the image, for each module. */
        for (uint32_t i = 0; i < MEM_SIZE; i += 8) {
            uint64_t v = rnd();
            memcpy(g_pristine + i, &v, 8);
        }
        const uint32_t at = (uint32_t)base * 16u + m->origin;
        if (at + m->size > MEM_SIZE) { printf("%s does not fit\n", m->name); continue; }
        memcpy(g_pristine + at, m->image, m->size);
        memcpy(g_mem[0], g_pristine, MEM_SIZE);
        memcpy(g_mem[1], g_pristine, MEM_SIZE);
        unsigned long long mt = 0, md = 0, mb = 0;
        for (uint32_t e = 0; e < m->nentries; e++) {
            const rc_entry *en = &m->entries[e];
            const rc_region *rg = &m->regions[en->region];
            const uint16_t cs = (uint16_t)(base + rg->seg);
            const int32_t ipl = (int32_t)(at + en->off) - (int32_t)cs * 16;
            if (ipl < 0 || ipl > 0xFFFF) continue;
            const uint16_t ip = (uint16_t)ipl;
            total_insns++;
            int failed = 0;
            for (int s = 0; s < states; s++) {
                uint16_t r[8], seg[4];
                for (int i = 0; i < 8; i++) r[i] = (uint16_t)rnd();
                for (int i = 0; i < 4; i++) seg[i] = (uint16_t)rnd();
                /* A third of the states keep DS/ES/SS on the module's own
                 * segments, the rest anywhere: both kinds of data. */
                if (s % 3 == 0) { seg[S_DS] = cs; seg[S_ES] = cs; seg[S_SS] = cs; }
                if (s & 1) r[R_CX] &= 0x000F;           /* short REP counts too */
                uint16_t flags = (uint16_t)((rnd() & 0x0ED5u) | 0x0002u);   /* no TF */
                set_state(&g_cpu[0], cs, ip, r, seg, flags);
                set_state(&g_cpu[1], cs, ip, r, seg, flags);
                if (g_timing386) {
                    const int wait = s & 1 ? T386_MEM_CACHED : T386_MEM_UNCACHED;
                    const int pf = (int)(rnd() % 17);
                    const int prefixes = (int)(rnd() % 4);
                    const int chunk = (s & 2) ? (int)(rnd() % 101) : -1;
                    const int chunk_n = chunk < 0 ? 0 : (int)(rnd() % 20);
                    const uint32_t held = chunk < 0 ? 0 : (uint32_t)(rnd() % 1000);
                    for (int k = 0; k < 2; k++) {
                        cpu_t *c = &g_cpu[k];
                        t386_enable(c, wait, 0xA0000u, 0x20000u, 32);
                        c->t386_pf_bytes = pf;
                        c->t386_pf_prefixes = prefixes;
                        c->t386_chunk = chunk;
                        c->t386_chunk_n = chunk_n;
                        c->t386_chunk_held = held;
                        /* REP elements and zero-count shifts can retire with
                         * no clock advance. Stop by instruction count too. */
                    }
                }
                /* The machine's interpreter step (pc.c interp_step): the
                 * instruction, then the single-step trap if TF is set. */
                cpu_step(&g_cpu[0]);
                if (g_cpu[0].flags & F_TF) cpu_interrupt(&g_cpu[0], 1);
                rc_instruction_budget = g_timing386 ? 1 : -1;
                int ran = rg->fn(&g_cpu[1]);
                if (!ran) {
                    if (s == 0 && g_verbose) {
                        const uint8_t *p = &g_pristine[(at + en->off) & 0xFFFFFu];
                        printf("  declined %s+%05X [%02X %02X %02X %02X]\n", m->name, en->off, p[0], p[1], p[2], p[3]);
                    }
                    md++; restore(); continue;
                }
                mt++;
                if (compare(m->name, en->off, cs, ip, 0)) { failed = 1; }
                restore();
                if (failed) break;
            }
            if (failed) mb++;
        }
        printf("%-14s %6u instruction starts, %8llu states compared, %6llu declined, %llu mismatching\n",
               m->name, m->nentries, mt, md, mb);
        tested += mt; declined += md; bad += mb;
    }
    printf("all modules: %llu instruction starts, %llu states compared, %llu declined, %llu instructions mismatching\n",
           total_insns, tested, declined, bad);
    return bad ? 1 : 0;
}
