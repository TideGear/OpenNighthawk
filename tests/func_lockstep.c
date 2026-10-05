/* func_lockstep.c - every matched routine (src/matched) held to the original.
 *
 *     func_lockstep [--states N] [--seed S] [--verbose]
 *
 * Phase 2 replaces translated routines with hand-written C that must be
 * equal to the original, not similar. For each matched routine, the
 * module's image is placed in memory, the machine is put in N random states
 * (registers, flags, every byte of memory outside the image, with a return
 * address pushed that lies outside the routine), and the routine runs
 * twice: by the interpreter, instruction by instruction until it returns
 * there, and by the matched C. Everything is compared, as insn_lockstep
 * does for single instructions: registers, segments, IP, flags, the clock,
 * every byte written, every port touched and every interrupt raised.
 *
 * A state in which the original does not return within the step budget
 * (random data can send a loop a long way) is counted, not compared; so is
 * one the matched routine declines. Build with -DF117R_GEN_DIR.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_gen.h"
#include "matched.h"

/* ---- the CPU hooks: record, never emulate ---------------------------------
 * Port reads return a value derived from the port, width and count so far;
 * port writes and interrupts are folded into a hash. Both sides get
 * identical hooks. The real run-time is linked in (matched code calls it),
 * so memory is compared and restored whole rather than by tracked writes. */

typedef struct {
    int      ndirty, overflow;
    uint32_t dirty[1];
    uint64_t io;            /* port writes, reads, interrupts */
    uint32_t nread;
} side_t;

static side_t g_side[2];

static side_t *side_of(cpu_t *c) { return (side_t *)c->user; }

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

/* The routine's entry state: registers, and the stack as a caller leaves it. */
static void setup_side(cpu_t *c, const recomp_override *o, uint16_t cs, uint16_t ip, const uint16_t *r,
                       const uint16_t *seg, uint16_t flags, uint16_t back, int s, const uint16_t *small)
{
    set_state(c, cs, ip, r, seg, flags);
    c->r[R_SP] = (uint16_t)(c->r[R_SP] - 2);
    if (o->matched == 2) {                                     /* a far routine: CS too */
        seg_write16(c, c->seg[S_SS], c->r[R_SP], cs);
        c->r[R_SP] = (uint16_t)(c->r[R_SP] - 2);
    }
    seg_write16(c, c->seg[S_SS], c->r[R_SP], back);            /* the caller's return address */
    /* Half the states put small arguments above it: random words
     * almost never reach a routine's edge cases (zero, -1, 1). */
    const uint16_t args = o->matched == 2 ? 4 : 2;             /* above IP, and CS when far */
    for (int a = 0; a < 4 && (s & 2); a++)
        seg_write16(c, c->seg[S_SS], (uint16_t)(c->r[R_SP] + args + 2 * a), small[a]);
    c->stop_at = c->icount + 100000;
}

static unsigned long long g_overrun;    /* routines that ran past the event limit */

static int g_verbose, g_shown;

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


static machine_t g_m;           /* side 1's CPU lives in a machine: matched code takes one */

/* A matched routine's call into original code, run here by plain stepping:
 * the harness has no events to service. */
/* Step until the routine's own RET (near, or far) taken with SP at entry_sp. */
static int run_to_ret(cpu_t *a, uint16_t entry_sp, int far, int *steps)
{
    while (*steps < 100000) {
        const uint8_t op = a->mem[phys(a->seg[S_CS], a->ip)];
        const uint16_t sp_before = a->r[R_SP];
        cpu_step(a);
        if (a->flags & F_TF) cpu_interrupt(a, 1);
        ++*steps;
        const int is_ret = far ? (op == 0xCB || op == 0xCA) : (op == 0xC3 || op == 0xC2);
        if (is_ret && sp_before == entry_sp) return 1;
    }
    return 0;
}

static unsigned long long g_finished;   /* states the original code finished */
static int g_ncalls;           /* guest calls made by the routine under test */
static int g_lost;              /* a call into original code never came back */
static int step_runner(machine_t *mm)
{
    cpu_t *c = &mm->cpu;
    g_ncalls++;
    for (int i = 0; i < 200000; i++) {
        if (c->ip == mm->trap_ip && c->r[R_SP] == mm->trap_sp && c->seg[S_CS] == mm->trap_cs) return RUN_TRAP;
        cpu_step(c);
        if (c->flags & F_TF) cpu_interrupt(c, 1);
    }
    g_lost = 1;
    return RUN_SLICE;
}

int main(int argc, char **argv)
{
    int states = 2000;
    g_rng = 0x5EED0F117AULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--states") && i + 1 < argc) states = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) g_rng = strtoull(argv[++i], NULL, 0) | 1;
        else if (!strcmp(argv[i], "--verbose")) g_verbose = 1;
        else { fprintf(stderr, "usage: func_lockstep [--states N] [--seed S] [--verbose]\n"); return 2; }
    }
    if (RC_NMODULES == 0) { fprintf(stderr, "no generated code linked in (build with F117R_GEN_DIR)\n"); return 2; }

    g_pristine = (uint8_t *)malloc(MEM_SIZE);
    g_mem[0] = (uint8_t *)malloc(MEM_SIZE);
    g_mem[1] = (uint8_t *)malloc(MEM_SIZE);
    if (!g_pristine || !g_mem[0] || !g_mem[1]) return 2;
    for (int k = 0; k < 2; k++) {
        cpu_t *c = k ? &g_m.cpu : &g_cpu[0];
        cpu_init(c, g_mem[k]);
        c->model = CPU_80286;
        c->user = &g_side[k];
        c->io_read = io_r;
        c->io_write = io_w;
        c->int_hook = int_h;
        c->cover = NULL;
    }
    g_m.mem = g_mem[1];
    matched_runner = step_runner;

    /* Loaded at segment 0 the unrelocated image is also the relocated one:
     * every fixup adds 0, so far calls and segment constants are right. */
    const uint16_t base = 0;
    unsigned long long compared = 0, skipped = 0, bad = 0;
    for (unsigned mi = 0; mi < matched_count(); mi++) {
        const recomp_override *o = matched_entry(mi);
        const rc_module *m = NULL;
        for (unsigned k = 0; k < RC_NMODULES; k++)
            if (!strcmp(RC_MODULES[k]->name, o->module) && RC_MODULES[k]->file_hash == o->file_hash) m = RC_MODULES[k];
        if (!m) { printf("%s %04X:%04X: module not in the generated code\n", o->module, o->seg, o->ip); bad++; continue; }
        for (uint32_t a = 0; a < MEM_SIZE; a += 8) { uint64_t v = rnd(); memcpy(g_pristine + a, &v, 8); }
        const uint32_t at = (uint32_t)base * 16u + m->origin;
        memcpy(g_pristine + at, m->image, m->size);
        memcpy(g_mem[0], g_pristine, MEM_SIZE);
        memcpy(g_mem[1], g_pristine, MEM_SIZE);
        const uint16_t cs = (uint16_t)(base + o->seg), ip = o->ip;
        unsigned long long mc = 0, ms = 0, mb = 0;
        for (int s = 0; s < states; s++) {
            if (s == states / 2) {
                /* The second half runs on memory of mostly 00, FF and 01
                 * bytes: random words almost never hit a routine's exact
                 * tests (a -1 sentinel, a zero count), these hit them often. */
                static const uint8_t pick[4] = { 0x00, 0xFF, 0x01, 0x00 };
                for (uint32_t a = 0; a < MEM_SIZE; a++) g_pristine[a] = pick[rnd() & 3];
                memcpy(g_pristine + at, m->image, m->size);
                memcpy(g_mem[0], g_pristine, MEM_SIZE);
                memcpy(g_mem[1], g_pristine, MEM_SIZE);
            }
            uint16_t r[8], seg[4];
            for (int k = 0; k < 8; k++) r[k] = (uint16_t)rnd();
            for (int k = 0; k < 4; k++) seg[k] = (uint16_t)rnd();
            if (s % 3 == 0) { seg[S_DS] = cs; seg[S_ES] = cs; }
            /* A quarter of the states take small counts in AX, CX and DX,
             * for routines whose arguments are registers (an exact fit, a
             * zero length). */
            if ((s & 0x0C) == 0x0C) {
                static const uint16_t tiny[8] = { 0, 1, 2, 3, 0xFFFF, 0xFFFE, 0, 1 };
                r[R_AX] = tiny[rnd() & 7]; r[R_CX] = tiny[rnd() & 7]; r[R_DX] = tiny[rnd() & 7];
            }
            r[R_SP] = (uint16_t)((r[R_SP] | 0x0100) & 0xFFFE);
            const uint16_t back = (uint16_t)(ip + 0x8000);
            const uint16_t flags = (uint16_t)((rnd() & 0x0ED5u) | 0x0002u);
            uint16_t small[4];
            for (int a = 0; a < 4; a++)
                small[a] = (s & 4) ? (uint16_t)((int)(rnd() % 3) - 1) : (uint16_t)(rnd() % 32);
            for (int k = 0; k < 2; k++)
                setup_side(k ? &g_m.cpu : &g_cpu[0], o, cs, ip, r, seg, flags, back, s, small);
            int steps = 0;
            cpu_t *a = &g_cpu[0];
            /* Run the original until the routine's own near RET - the first
             * one taken with the stack back at the caller's level - and
             * accept the state only when it returns to the pushed address. */
            const uint16_t entry_sp = (uint16_t)(r[R_SP] - (o->matched == 2 ? 4 : 2));
            const int returned = run_to_ret(a, entry_sp, o->matched == 2, &steps);
            if (!returned || a->seg[S_CS] != cs || a->ip != back) steps = 100000;
            g_side[0].overflow = g_side[1].overflow = 1;      /* compare all memory */
            /* A real return lands back at the caller's stack level (RET n
             * pops at most a few words); a wild jump that happens to reach
             * the return address - a slide through zeroed memory - does not. */
            const uint16_t popped = (uint16_t)(a->r[R_SP] - entry_sp);
            if (steps >= 100000 || popped < 2 || popped > 18) { ms++; restore(); continue; }
            /* A random state that makes the original write over its own code
             * (a copy aimed at the routine) runs instructions it was not; the
             * game never does, and no equivalent can follow it. */
            const uint32_t self = phys(cs, ip);
            if (memcmp(g_mem[0] + self, g_pristine + self, 0x100)) { ms++; restore(); continue; }
            /* The event limit: a routine told it has one instruction fewer than
             * the original takes must decline. Running anyway would carry the
             * clock past a checkpoint or a frame boundary - nothing in the
             * comparison below can see that, as it gives every routine room.
             * (A routine that calls original code legitimately returns partway,
             * having run the callee to the limit; only a run with no such call
             * is an overrun.) */
            g_lost = 0; g_ncalls = 0;
            g_m.cpu.stop_at = g_m.cpu.icount + (uint64_t)steps - 1;
            const int probe_ran = o->fn(&g_m);
            if (probe_ran && !g_ncalls && g_m.cpu.icount > g_m.cpu.stop_at) {
                printf("  OVERRUN %s+%05X: ran to %llu with the limit at %llu (the original takes %d)\n", o->module,
                       ((uint32_t)o->seg << 4) + ip, (unsigned long long)g_m.cpu.icount - 1000,
                       (unsigned long long)g_m.cpu.stop_at - 1000, steps);
                g_overrun++; mb++;
                memcpy(g_mem[1], g_pristine, MEM_SIZE);
                restore();
                break;
            }
            if (probe_ran) {                                   /* it wrote: back to the entry state */
                memcpy(g_mem[1], g_pristine, MEM_SIZE);
                g_side[1].ndirty = 0; g_side[1].overflow = 1; g_side[1].io = 0; g_side[1].nread = 0;
            }
            setup_side(&g_m.cpu, o, cs, ip, r, seg, flags, back, s, small);
            g_lost = 0;
            /* A state whose call into original code wanders off (a far call
             * through a slot only the running game fills) returned on the
             * original side by accident, not through the routine. */
            if (!o->fn(&g_m) || g_lost) { ms++; restore(); continue; }
            /* A routine that ran out of room after a call returns with the
             * machine partway through it, as the original would be; the run
             * loop then finishes it with the original code, and so does this. */
            if (g_m.cpu.seg[S_CS] != cs || g_m.cpu.ip != back) {
                int more = 0;
                if (!run_to_ret(&g_m.cpu, entry_sp, o->matched == 2, &more)) { ms++; restore(); continue; }
                g_finished++;
            }
            mc++;
            g_cpu[1] = g_m.cpu;                /* compare() reads g_cpu[1] */
            if (compare(o->module, ((uint32_t)o->seg << 4) + ip, cs, ip, 0)) {
                mb++;
                if (g_verbose) {
                    printf("    state: AX %04X CX %04X DX %04X BX %04X SP %04X BP %04X SI %04X DI %04X"
                           " DS %04X ES %04X SS %04X flags %04X, %d steps\n",
                           r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], seg[S_DS], seg[S_ES], seg[S_SS], flags, steps);
                    for (int k = 0; k < 2; k++) {
                        const cpu_t *x = &g_cpu[k];
                        printf("    %s: AX %04X CX %04X DX %04X BX %04X SP %04X BP %04X SI %04X DI %04X IP %04X flags %04X\n",
                               k ? "matched " : "original", x->r[0], x->r[1], x->r[2], x->r[3], x->r[4], x->r[5],
                               x->r[6], x->r[7], x->ip, x->flags);
                    }
                }
            }
            restore();
            if (mb) break;
        }
        printf("%-10s %04X:%04X %-34s %6llu states compared, %4llu skipped, %s\n", o->module, o->seg, ip,
               o->what, mc, ms, mb ? "MISMATCH" : mc ? "equal" : "not testable here (routes only)");
        compared += mc; skipped += ms; bad += mb;
    }
    printf("matched routines: %u, %llu states compared (%llu finished by the original code), %llu skipped, %llu mismatching\n",
           matched_count(), compared, g_finished, skipped, bad);
    return bad ? 1 : 0;
}
