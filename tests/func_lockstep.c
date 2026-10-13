/* func_lockstep.c - every matched routine (src/matched) held to the original.
 *
 *     func_lockstep [--states N] [--seed S] [--verbose] [--only MODULE:IP,...]
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

static int g_checked_alloc;
static unsigned long long g_alloc_errors;
static unsigned long long g_gradient_full;
static unsigned long long g_sprite_zero, g_sprite_skip;
static unsigned long long g_iret_full, g_iret_special;
static int g_dos;                  /* the routine under test is in DOS_STUBS: INT 21h is answered here */

static int int_h(cpu_t *c, uint8_t vec)
{
    side_t *s = side_of(c);
    s->io = mix(s->io, 0x3000000ull | vec);
    for (int i = 0; i < 8; i++) s->io = mix(s->io, c->r[i]);
    for (int i = 0; i < 4; i++) s->io = mix(s->io, c->seg[i]);
    s->io = mix(s->io, ((uint64_t)c->ip << 16) | c->flags);
    if (g_dos && vec == 0x21) {     /* a DOS that answers: AX (and DX), and CF one time in four */
        s->nread++;
        const uint64_t v = mix(s->io, s->nread);   /* (the record so far is the same on both sides) */
        const int alloc = g_checked_alloc && get_r8(c, R_AH) == 0x48;
        const int wide = get_r8(c, R_AH) == 0x42 || ((v >> 56) & 3) == 1;
        c->r[R_AX] = (uint16_t)(v >> 16);
        if (((v >> 58) & 3) == 0) set_r8(c, R_AL, 0);  /* AL 0 (success, found) one time in four */
        if (alloc) c->r[R_AX] = (v & 1) ? 8 : 0x197A;
        if (wide) c->r[R_DX] = (uint16_t)(v >> 32);
        set_flag(c, F_CF, ((v >> 60) & 3) == 0);
        return 1;
    }
    /* For every other routine, DOS (21h) and the BIOS keyboard (16h) answer here,
     * as port reads do: AX from the call count (a small value one time in four),
     * and a random carry and zero flag, so both outcomes of a call are reached.
     * Through the vector table (the image's first bytes here) a routine's DOS
     * call went off into the image and only the routes could check what follows. */
    if (vec == 0x21 || vec == 0x16) {
        s->nread++;
        const uint64_t v = mix(0x4000000ull | vec, s->nread);
        c->r[R_AX] = (v & 0x300) ? (uint16_t)(v >> 16) : (uint16_t)((v >> 16) & 3);
        c->flags = (uint16_t)((c->flags & ~(F_CF | F_ZF)) | ((v & 0x400) ? F_CF : 0) | ((v & 0x800) ? F_ZF : 0));
        return 1;
    }
    return 0;                       /* not emulated: the CPU dispatches it */
}

/* ---- random states -------------------------------------------------------- */

static uint64_t g_rng;
static uint64_t rnd(void)
{
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return g_rng;
}

/* Each routine draws from a stream of its own, from the run's seed and the
 * routine's module and address (splitmix64 over an FNV-1a of the name): its
 * states do not move when other routines are added, and shards of the table
 * (--shard) can run apart and still test what one run would. */
static uint64_t routine_seed(uint64_t seed, const recomp_override *o)
{
    uint64_t h = 0xCBF29CE484222325ULL;
    for (const char *p = o->module; *p; p++) h = (h ^ (uint8_t)*p) * 0x100000001B3ULL;
    uint64_t z = seed ^ h ^ ((uint64_t)o->seg << 16 | o->ip);
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return (z ^ (z >> 31)) | 1;
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

/* Interrupt handlers (kind 3) that advance the saved IP before IRET: the
 * frame holds back - advance, so a state that resumes at `back` returned
 * through the handler. Half the states save the faulting instruction's IP
 * (fault), the one value that selects the handler's other denominator. */
typedef struct { uint16_t seg, ip, advance, fault; } interrupt_frame_fixture;
static const interrupt_frame_fixture INTERRUPT_FRAMES[] = {
    { 0x0FB2, 0x084E, 4, 0x07C8 },        /* span divide error: idiv [261D] */
};
static const interrupt_frame_fixture *interrupt_frame(const recomp_override *o)
{
    if (o->matched != 3 || strcmp(o->module, "VGAME.EXE")) return NULL;
    for (unsigned k = 0; k < sizeof INTERRUPT_FRAMES / sizeof INTERRUPT_FRAMES[0]; k++)
        if (o->seg == INTERRUPT_FRAMES[k].seg && o->ip == INTERRUPT_FRAMES[k].ip)
            return &INTERRUPT_FRAMES[k];
    return NULL;
}

/* The routine's entry state: registers, and the stack as a caller leaves it
 * (for an interrupt handler, as the CPU's interrupt entry leaves it). */
static void setup_side(cpu_t *c, const recomp_override *o, uint16_t cs, uint16_t ip, const uint16_t *r,
                       const uint16_t *seg, uint16_t flags, uint16_t back, int s, const uint16_t *small)
{
    set_state(c, cs, ip, r, seg, flags);
    const interrupt_frame_fixture *frame = interrupt_frame(o);
    if (o->matched == 3) {                                     /* FLAGS, CS, IP; IF and TF cleared */
        c->r[R_SP] = (uint16_t)(c->r[R_SP] - 2);
        seg_write16(c, c->seg[S_SS], c->r[R_SP], flags);
        c->flags = (uint16_t)(c->flags & (0xFFFFu ^ (F_IF | F_TF)));
        c->int_depth = 1 + ((s >> 3) & 1);                     /* IRET's depth step, nested or not */
    }
    c->r[R_SP] = (uint16_t)(c->r[R_SP] - 2);
    if (o->matched >= 2) {                                     /* far or interrupt: CS too */
        seg_write16(c, c->seg[S_SS], c->r[R_SP], cs);
        c->r[R_SP] = (uint16_t)(c->r[R_SP] - 2);
    }
    seg_write16(c, c->seg[S_SS], c->r[R_SP], (uint16_t)(back - (frame ? frame->advance : 0)));
    /* Half the states put small arguments above it: random words
     * almost never reach a routine's edge cases (zero, -1, 1). */
    const uint16_t args = o->matched == 3 ? 6 : o->matched == 2 ? 4 : 2;   /* above IP, CS, FLAGS */
    for (int a = 0; a < 4 && (s & 2); a++)
        seg_write16(c, c->seg[S_SS], (uint16_t)(c->r[R_SP] + args + 2 * a), small[a]);
    c->stop_at = c->icount + 100000;
}

static unsigned long long g_overrun;    /* routines that ran past the event limit */

/* Sentinel words to plant, by routine (segment, IP): at DS:[reg + disp], or at DS:disp when reg is PLANT_ABS. */
#define PLANT_ABS 8
static const struct { uint16_t seg, ip; uint8_t reg; uint16_t disp; uint16_t val; } PLANTS[] = {
    { 0x0000, 0x0494, PLANT_ABS, 0x0088, 0x0000 }, /* checked allocator: empty error format in planted states */
    { 0x1058, 0x09D8, PLANT_ABS, 0x294B, 0x0001 }, /* HUD's compact layout */
    { 0x1058, 0x0A2C, PLANT_ABS, 0x294B, 0x0001 },
    { 0x0000, 0x4777, PLANT_ABS, 0xB198, 0x0001 },   /* projectile slots: bounded walks and odd-frame refill */
    { 0x0000, 0x4777, PLANT_ABS, 0xB198, 0x0004 },
    { 0x0000, 0x4777, PLANT_ABS, 0x368E, 0x000F },   /* frame-rate estimate S */
    { 0x0000, 0x4777, PLANT_ABS, 0x3D8E, 0x0001 },
    { 0x0000, 0x4777, PLANT_ABS, 0x43EE, 0x0001 },   /* keyboard trigger held */
    { 0x0000, 0x4777, PLANT_ABS, 0x3682, 0x03E8 },   /* ammunition available */
    { 0x0000, 0x4777, PLANT_ABS, 0xC09A, 0x0000 },
    { 0x0000, 0x5046, PLANT_ABS, 0xE008, 0x0014 },   /* ground defences: display 14h up, no warning countdown, no override */
    { 0x0000, 0x5046, PLANT_ABS, 0x3D98, 0x0000 },
    { 0x0000, 0x5046, PLANT_ABS, 0xDF06, 0x0000 },
    { 0x0000, 0x73C8, PLANT_ABS, 0xE304, 0x0002 },   /* target_damage: the weapon-lock slot scan finding a match */
    { 0x0000, 0x73C8, PLANT_ABS, 0xE306, 0x0001 },
    { 0x0000, 0x73C8, PLANT_ABS, 0xE316, 0x0002 },
    { 0x0000, 0x73C8, PLANT_ABS, 0xE318, 0x0002 },
    { 0x120A, 0x0B3B, R_DI, 0xD6B6, 0x8000 },     /* model_prepare_edge: a vertex behind the eye (x high word 8000h) */
    { 0x120A, 0x0B3B, R_BX, 0xD6B6, 0x8000 },
    { 0x0000, 0xDB3C, PLANT_ABS, 0x49F4, 0x0032 },   /* scene_defer_object: a full list of 50 */
    { 0x0000, 0xDB3C, PLANT_ABS, 0xE328, 0x0002 },   /* ... in view mode 2 */
    { 0x0000, 0xDB3C, PLANT_ABS, 0x49AA, 0x0005 },   /* ... with detail class 5 */
    { 0x0000, 0x4E5F, PLANT_ABS, 0x9912, 0x0030 },   /* frame_palette_cycle: the four phases of the clock ... */
    { 0x0000, 0x4E5F, PLANT_ABS, 0x9912, 0x0020 },
    { 0x0000, 0x4E5F, PLANT_ABS, 0x9912, 0x0010 },
    { 0x0000, 0x4E5F, PLANT_ABS, 0x9912, 0x0040 },
    { 0x0000, 0x4E5F, PLANT_ABS, 0x43DC, 0x0000 },   /* ... with the cycle enabled */
    { 0x0000, 0x48AA, PLANT_ABS, 0x3F84, 0x4197 },   /* pic_rle_row: the private stack live, ... */
    { 0x0000, 0x48AA, PLANT_ABS, 0x3F86, 0x0002 },   /* ... so the LZW step runs its body, briefly */
    { 0x11ED, 0x00AE, PLANT_ABS, 0x9688, 0x989B },   /* pic_rle_row, VGAME's copy */
    { 0x11ED, 0x00AE, PLANT_ABS, 0x968A, 0x0002 },
    { 0x0000, 0x890A, PLANT_ABS, 0x8D6E, 0x8F81 },   /* pic_rle_row, START's copy */
    { 0x0000, 0x890A, PLANT_ABS, 0x8D70, 0x0002 },
    { 0x1377, 0x0B09, PLANT_ABS, 0x8606, 0xFE3C },   /* model_fill: the AND, OR, stipple and clear styles */
    { 0x1377, 0x0B09, PLANT_ABS, 0x8606, 0xFD3C },
    { 0x1377, 0x0B09, PLANT_ABS, 0x8606, 0xFC3C },
    { 0x1377, 0x0B09, PLANT_ABS, 0x8606, 0xFB3C },
};

static const char *g_ctx = "";   /* what the comparison in progress is: " (mid-run stop)" */
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
    else if (a->int_depth != b->int_depth) snprintf(why, sizeof why, "interrupt depth %d vs %d", a->int_depth, b->int_depth);
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
        if (!why[0] && g_side[0].overflow && memcmp(g_mem[0], g_mem[1], MEM_SIZE)) {
            uint32_t x = 0;
            while (g_mem[0][x] == g_mem[1][x]) x++;
            snprintf(why, sizeof why, "memory %05X: %02X vs %02X", x, g_mem[0][x], g_mem[1][x]);
        }
    }
    (void)declined;
    if (!why[0]) return 0;
    if (g_shown++ < 40 || g_verbose)
    {
        const uint32_t lin = ((uint32_t)cs * 16u + ip) & 0xFFFFFu;
        printf("  MISMATCH%s %s+%05X (%04X:%04X) [%02X %02X %02X %02X %02X %02X]: %s\n", g_ctx, mod, off, cs, ip,
               g_pristine[lin], g_pristine[lin + 1], g_pristine[lin + 2], g_pristine[lin + 3],
               g_pristine[lin + 4], g_pristine[lin + 5], why);
    }
    return 1;
}


/* START and END, and VGAME's marker, canopy, projectiles, panel helpers,
 * sound gate and scene finish, call drivers through 5-byte thunks in
 * their data segment. In the file each is JMP FAR 0:0 (the game fills them in
 * after loading), so a far call through one runs the program's first bytes
 * from a random state and the state is lost: every routine whose path reaches
 * a draw call could not be compared past it. Here each thunk a far call in the
 * code points at becomes a RETF, a driver that draws nothing; both sides see
 * the same bytes, so what is compared is the routine's own work around the
 * call: its arguments, the code after it and the clock. The driver's drawing
 * is held by the routes. */
static void stub_driver_thunks(uint8_t *pristine, uint32_t at, const rc_module *m, const recomp_override *o)
{
    const int vgame_draw = !strcmp(m->name, "VGAME.EXE") &&
        ((o->seg == 0 && (o->ip == 0x3ADA || o->ip == 0x1EB0 || o->ip == 0xBCEF || o->ip == 0x983E || o->ip == 0x79BE || o->ip == 0x8A90 || o->ip == 0x9515 || o->ip == 0x1E79 || o->ip == 0xB171 || o->ip == 0xD6DD || o->ip == 0x4777 ||
                         o->ip == 0xD3F9 || o->ip == 0xD87C || o->ip == 0x72D8 ||
                           o->ip == 0x8A18 || o->ip == 0x881F || o->ip == 0x796E || o->ip == 0x3A4E ||
                           o->ip == 0xB0C2 || o->ip == 0xC45C)) ||
           (o->seg == 0x114A && (o->ip == 0x0280 || o->ip == 0x03AD || o->ip == 0x03FD || o->ip == 0x014F || o->ip == 0x0006 || o->ip == 0x01DB)) ||
           (o->seg == 0x1058 && (o->ip == 0x0016 || o->ip == 0x09D8 || o->ip == 0x0A2C)) ||
           (o->seg == 0x0FB2 && o->ip == 0x01E6));
    const int checked_alloc = !strcmp(m->name, "DSWAP.EXE") && o->seg == 0 && o->ip == 0x0494;
    if (strcmp(m->name, "START.EXE") && strcmp(m->name, "END.EXE") && !vgame_draw && !checked_alloc) return;
    for (uint32_t i = 0; i + 5 <= m->size; i++) {
        if (m->image[i] != 0x9A) continue;
        const uint32_t lin = (uint32_t)(m->image[i + 3] | m->image[i + 4] << 8) * 16u + (uint32_t)(m->image[i + 1] | m->image[i + 2] << 8);
        if (lin + 5 > m->size) continue;
        static const uint8_t jmp_far_0[5] = { 0xEA, 0, 0, 0, 0 };
        if (!memcmp(m->image + lin, jmp_far_0, 5)) pristine[at + lin] = 0xCB;
    }
}

/* A byte of the loaded image a routine reads as data at a low address: the BIOS data area (0:0489h, the
 * gray-scale flag END's DAC loader tests) lies inside END's image here, where it is a code byte that happens
 * to have the flag set, so the routine's normal path was never reached. Set in both sides' memory. */
static const struct { const char *module; uint16_t ip; uint32_t linear; uint8_t value; } IMAGE_PATCHES[] = {
    /* Panel-frame caller fixture: returning drawing/string/navigation helpers;
     * real trig, distance, bearing and clamp helpers remain executable. */
    { "VGAME.EXE", 0x79BE, 0x1176E, 0xCB },
    { "VGAME.EXE", 0x79BE, 0x1189D, 0xCB },
    { "VGAME.EXE", 0x79BE, 0x114A6, 0xCB },
    { "VGAME.EXE", 0x79BE, 0x115EF, 0xCB },
    { "VGAME.EXE", 0x79BE, 0x0D578, 0xC3 },
    { "VGAME.EXE", 0x79BE, 0x0EB50, 0xC3 },
    { "VGAME.EXE", 0x79BE, 0x0EB9E, 0xC3 },
    { "VGAME.EXE", 0x79BE, 0x0EB10, 0xC3 },
    { "VGAME.EXE", 0x79BE, 0x0EB82, 0xC3 },
    { "VGAME.EXE", 0x79BE, 0x0EBBA, 0xC3 },
    { "VGAME.EXE", 0x79BE, 0x0895E, 0xC3 },
    { "VGAME.EXE", 0x79BE, 0x0898F, 0xC3 },
    { "VGAME.EXE", 0x79BE, 0x08A4F, 0xC3 },
    { "VGAME.EXE", 0x79BE, 0x08A90, 0xC3 },
    { "VGAME.EXE", 0x79BE, 0x0971A, 0xC3 },
    { "VGAME.EXE", 0x79BE, 0x089F7, 0xC3 },
    { "VGAME.EXE", 0x79BE, 0x08A18, 0xC3 },
    { "VGAME.EXE", 0x79BE, 0x0889B, 0xC3 },
    /* Frame-update caller: its per-frame sub-passes, drawing and mission
     * set-up return at once; distance, clamp, shift and divide helpers run. */
    { "VGAME.EXE", 0x3ADA, 0x04C73, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x04973, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x08435, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x09515, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x08462, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x09216, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x094E1, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x05046, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x05852, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x0683E, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x04738, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x04777, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x048B8, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x04958, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x04F2B, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x04FD8, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x03A0D, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x066F8, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x07594, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x04E5F, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x00D14, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x00F3D, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x0450A, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x0D4A5, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x0D441, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x0D578, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x0889B, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x0886A, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x08A4F, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x004C7, 0xC3 },
    { "VGAME.EXE", 0x3ADA, 0x1189D, 0xCB },
    { "VGAME.EXE", 0x3ADA, 0x1176E, 0xCB },
    /* Ground-defences caller: the engagement, the navigation refresh, the map marker and arc and the
     * panel bar return at once; the clamp and the 32-bit divide run. */
    { "VGAME.EXE", 0x5046, 0x0528D, 0xC3 },
    { "VGAME.EXE", 0x5046, 0x08F71, 0xC3 },
    { "VGAME.EXE", 0x5046, 0x08625, 0xC3 },
    { "VGAME.EXE", 0x5046, 0x08719, 0xC3 },
    { "VGAME.EXE", 0x5046, 0x117DC, 0xCB },

    /* Camera caller: returning render/string helpers; original math runs. */
    { "VGAME.EXE", 0xBCEF, 0x0FD66, 0xCB },
    { "VGAME.EXE", 0xBCEF, 0x10039, 0xCB },
    { "VGAME.EXE", 0xBCEF, 0x1E5A8, 0xCB },
    { "VGAME.EXE", 0xBCEF, 0x0C436, 0xC3 },
    { "VGAME.EXE", 0xBCEF, 0x0D826, 0xC3 },
    { "VGAME.EXE", 0xBCEF, 0x0E2B6, 0xC3 },
    { "VGAME.EXE", 0xBCEF, 0x004E6, 0xC3 },
    { "VGAME.EXE", 0xBCEF, 0x0C4BA, 0xC3 },
    { "VGAME.EXE", 0xBCEF, 0x0C5B2, 0xC3 },
    { "VGAME.EXE", 0xBCEF, 0x0D87C, 0xC3 },
    { "VGAME.EXE", 0xBCEF, 0x0DC46, 0xC3 },
    { "VGAME.EXE", 0xBCEF, 0x0C45C, 0xC3 },
    { "VGAME.EXE", 0xBCEF, 0x0D578, 0xC3 },
    { "VGAME.EXE", 0xBCEF, 0x0898F, 0xC3 },
    { "VGAME.EXE", 0xBCEF, 0x0EB50, 0xC3 },
    { "VGAME.EXE", 0xBCEF, 0x0EB9E, 0xC3 },
    { "VGAME.EXE", 0xBCEF, 0x0EB10, 0xC3 },
    { "VGAME.EXE", 0xBCEF, 0x01974, 0xC3 },
    { "VGAME.EXE", 0xBCEF, 0x019FD, 0xC3 },
    /* Moving-object caller: returning projection/drawing helpers.
     * Real range, bearing, absolute value, scaling and trail-size math runs. */
    { "VGAME.EXE", 0x983E, 0x0B5DC, 0xC3 },
    { "VGAME.EXE", 0x983E, 0x0BB0D, 0xC3 },
    { "VGAME.EXE", 0x983E, 0x0D9FE, 0xC3 },
    { "VGAME.EXE", 0x983E, 0x0BA56, 0xC3 },
    { "VGAME.EXE", 0x983E, 0x114A6, 0xCB },
    /* HUD caller: returning clipped line; preserve real rotation math. */
    { "VGAME.EXE", 0x0016, 0x0FE40, 0xCB },
    /* Navigation-grid caller: returning far rectangle fill. */
    { "VGAME.EXE", 0x8A90, 0x0FD66, 0xCB },
    /* Arming caller: returning picture loader; actual driver on routes. */
    { "START.EXE", 0x07EE, 0x35FC, 0xC3 },
    /* Polygon caller fixture: returning span reset and clipped edge. The
     * actual rasterizer and graphics driver are retained on real routes. */
    { "VGAME.EXE", 0x01E6, 0x1003F, 0xC3 },
    { "VGAME.EXE", 0x01E6, 0x101C6, 0xC3 },
    /* Moving-map caller fixture: returning scene/projection/walk helpers. */
    { "VGAME.EXE", 0x11A8, 0x0D826, 0xC3 },
    { "VGAME.EXE", 0x11A8, 0x1E670, 0xCB },
    { "VGAME.EXE", 0x11A8, 0x1E5BC, 0xCB },
    { "VGAME.EXE", 0x11A8, 0x011FE, 0xC3 },
    { "VGAME.EXE", 0x11A8, 0x0D87C, 0xC3 },
    /* Camera-gradient caller fixture: returning polygon fill. */
    { "VGAME.EXE", 0x1974, 0x0FD06, 0xCB },
    { "VGAME.EXE", 0x19FD, 0x0FD06, 0xCB },
    /* Screen-loader caller fixture: returning fade/load/palette helpers. */
    { "START.EXE", 0x3641, 0x3088, 0xC3 },
    { "START.EXE", 0x3641, 0x35FC, 0xC3 },
    { "START.EXE", 0x3641, 0x826C, 0xC3 },
    { "START.EXE", 0x3641, 0x3070, 0xC3 },
    { "START.EXE", 0x3641, 0x3693, 0xC3 },

    { "END.EXE", 0x42E4, 0x0489, 0x00 },
    /* Returning failure cleanup tests the caller after its normally terminal exit. */
    { "DSWAP.EXE", 0x0494, 0x0E36, 0xC3 },
    /* The loader relocates each installer's MOV AX,segment operand. */
    { "VGAME.EXE", 0x1B73, 0x1B85, 0x7A },
    { "VGAME.EXE", 0x1B73, 0x1B86, 0x19 },
    { "START.EXE", 0x85B8, 0x85CA, 0x7A },
    { "START.EXE", 0x85B8, 0x85CB, 0x19 },
    { "END.EXE", 0x4557, 0x4569, 0x7A },
    { "END.EXE", 0x4557, 0x456A, 0x19 },
    { "DSWAP.EXE", 0x0523, 0x0535, 0x7A },
    { "DSWAP.EXE", 0x0523, 0x0536, 0x19 },
    { "SETUP.EXE", 0x0C35, 0x0C47, 0x7A },
    { "SETUP.EXE", 0x0C35, 0x0C48, 0x19 },
};

static void patch_image_for(uint8_t *pristine, const recomp_override *o)
{
    for (unsigned i = 0; i < sizeof IMAGE_PATCHES / sizeof IMAGE_PATCHES[0]; i++)
        if (!strcmp(IMAGE_PATCHES[i].module, o->module) && IMAGE_PATCHES[i].ip == o->ip)
            pristine[IMAGE_PATCHES[i].linear] = IMAGE_PATCHES[i].value;
}

/* Argument words a routine compares for exactly, planted in the first argument slots in some states (the
 * small random arguments never reach them). */
static const struct { const char *module; uint16_t ip; int arg; uint16_t v[2]; } ARG_PLANTS[] = {
    { "VGAME.EXE", 0x01E6, 1, { 2, 4 } }, /* far frame: vertex count at BP+8 */
    { "VGAME.EXE", 0x1974, 0, { 110, 169 } },
    { "VGAME.EXE", 0x19FD, 0, { 110, 169 } },
    { "VGAME.EXE", 0x19FD, 1, { 0, 1 } },
    { "START.EXE", 0x3641, 1, { 0x0000, 0x3693 } }, /* no overlay or a returning overlay */
    { "START.EXE", 0x1B37, 0, { 0x0064, 0x0065 } },     /* the map caption's kinds 64h and 65h */
    { "VGAME.EXE", 0x73C8, 0, { 0x0001, 0x0002 } },      /* target_damage: a target matching a planted weapon-lock slot */
};

/* Exact code ranges when the default 0x300-byte window is unsuitable:
 * long/shared tails, or small walkers whose unrelated callees are nearby. */
static const struct { const char *module; uint16_t ip, from, span; } SPANS[] = {
    { "VGAME.EXE", 0x3ADA, 0x3ADA, 0x0A30 }, /* frame update through RET */
    { "VGAME.EXE", 0x1EB0, 0x1EB0, 0x0DD7 }, /* flight input through RET */
    { "VGAME.EXE", 0xBCEF, 0xBCEF, 0x0747 }, /* complete camera image */
    { "VGAME.EXE", 0x983E, 0x983E, 0x06BC }, /* entire moving-object pass */
    { "VGAME.EXE", 0x0016, 0x0016, 0x09C2 }, /* HUD pass through RET */
    { "VGAME.EXE", 0x79BE, 0x79BE, 0x082A }, /* whole cockpit pass through RET */
    { "VGAME.EXE", 0x8A90, 0x8A90, 0x0446 }, /* entire grid through RET at 8ED5 */
    /* A random initializer can consume the walker's return address and
     * RET to the outer caller. Its RET is not the walker's own return even
     * when it lies within 0x300 bytes (SETUP 1886, seed 0x66A860898896).
     * Near walkers end at +0Eh; far-pointer walkers at +12h. */
    { "VGAME.EXE", 0xE924, 0xE924, 0x000F },
    { "START.EXE", 0x9142, 0x9142, 0x000F },
    { "END.EXE", 0x5038, 0x5038, 0x000F },
    { "PLAYER.EXE", 0x12FC, 0x12FC, 0x000F },
    { "DSWAP.EXE", 0x0EE4, 0x0EE4, 0x000F },
    { "SETUP.EXE", 0x177C, 0x177C, 0x000F },
    { "VGAME.EXE", 0xE933, 0xE933, 0x0013 },
    { "START.EXE", 0x9151, 0x9151, 0x0013 },
    { "END.EXE", 0x5047, 0x5047, 0x0013 },
    { "PLAYER.EXE", 0x130B, 0x130B, 0x0013 },
    { "MPS_LOGO.EXE", 0x0283, 0x0283, 0x0013 },
    { "DSWAP.EXE", 0x0EF3, 0x0EF3, 0x0013 },
    { "SETUP.EXE", 0x178B, 0x178B, 0x0013 },
    { "END.EXE", 0x3021, 0x3021, 0x0700 },      /* the scorer: its RET is at 0x03680 */
    { "START.EXE", 0x9D06, 0x9D06, 0x0500 },    /* the formatter: its exit is at entry + 4CFh */
    { "END.EXE", 0x5678, 0x5678, 0x0500 },
    { "START.EXE", 0x94FA, 0x94FA, 0x1000 },    /* itoa and ltoa: the shared conversion returns at 0x0A4B6 */
    { "START.EXE", 0x9516, 0x9516, 0x1000 },
    { "START.EXE", 0xA210, 0x98A8, 0x0990 },    /* the C runtime's DOS returns: START 0x098A8-0x098C9 */
    { "START.EXE", 0xA4B8, 0x98A8, 0x0C10 },
    { "START.EXE", 0xA520, 0x98A8, 0x0D00 },
    { "END.EXE", 0x5096, 0x5096, 0x0410 },      /* ... END 0x05482-0x054A3 */
};
static uint16_t g_from, g_span = 0x300; /* the routine under test's code: from g_from, g_span bytes */

/* Routines whose paths go on after DOS calls: here INT 21h is answered by int_h (AX and sometimes DX
 * from the same hash as port reads, CF set one time in four) instead of running through a vector that
 * the loaded image's bytes make up, which seldom comes back. Both sides get the same answers. */
static const struct { const char *module; uint16_t ip; } DOS_STUBS[] = {
    { "VGAME.EXE", 0x1B96 },
    { "START.EXE", 0x85DB },
    { "END.EXE", 0x457A },
    { "DSWAP.EXE", 0x0546 },
    { "SETUP.EXE", 0x0C58 },
    { "DSWAP.EXE", 0x0494 },
    { "DSWAP.EXE", 0x04E0 }, { "DSWAP.EXE", 0x0506 },
    { "VGAME.EXE", 0x1B73 },
    { "START.EXE", 0x85B8 },
    { "END.EXE", 0x4557 },
    { "DSWAP.EXE", 0x0523 },
    { "SETUP.EXE", 0x0C35 },
    { "START.EXE", 0xA210 },            /* close */
    { "START.EXE", 0xA4B8 },            /* unlink */
    { "START.EXE", 0xA520 },            /* lseek */
    { "END.EXE", 0x5096 },
    { "START.EXE", 0x800A }, { "END.EXE", 0x3FA4 },     /* START's and END's own wrappers */
    { "START.EXE", 0x8030 }, { "END.EXE", 0x3FCA },
    { "START.EXE", 0x8123 }, { "END.EXE", 0x40BD },
    { "START.EXE", 0x8144 }, { "END.EXE", 0x40DE },
    { "START.EXE", 0x816F }, { "END.EXE", 0x4109 },
    { "START.EXE", 0x8075 }, { "START.EXE", 0x804E },
    { "START.EXE", 0x9873 }, { "END.EXE", 0x544D },     /* the run-time messages */
    { "START.EXE", 0x9800 }, { "END.EXE", 0x53DA },
    { "START.EXE", 0x9115 }, { "END.EXE", 0x500B },     /* the vectors put back */
};

/* Entry points reaching shared code below the entry: watch that code too,
 * and recognize a RET there when the routine jumps back to a shared exit. */
static const struct { const char *module; uint16_t seg, ip, below; } CODE_BELOW[] = {
    { "VGAME.EXE", 0x0000, 0x1E79, 0x020C }, /* timer tick: shared rate-control tails from 1C6D */
    { "VGAME.EXE", 0x0FB2, 0x05A4, 0x004F }, /* boundary edge tail-jumps into column store */
    { "VGAME.EXE", 0x0FB2, 0x06A6, 0x0151 },     /* clipped edge: shared column/emitter flow from 0555 */
    { "VGAME.EXE", 0x130D, 0x0577, 0x0018 },     /* bisection's equal-word midpoint cases at 055F/056B */
    { "VGAME.EXE", 0x1377, 0x0968, 0x00CC },     /* model fill: the jump to the common exit 089C */
    { "VGAME.EXE", 0x1377, 0x099F, 0x0007 },     /* stipple fill, exit at 0998 */
    { "VGAME.EXE", 0x1377, 0x0A1C, 0x0007 },     /* AND fill from a row, exit at 0A15 */
    { "VGAME.EXE", 0x1377, 0x0A9C, 0x0007 },     /* OR fill from a row, exit at 0A95 */
    { "VGAME.EXE", 0x1377, 0x0B4F, 0x0007 },     /* solid fill from a row, exit at 0B48 */
    { "VGAME.EXE", 0x1377, 0x0B09, 0x019E },     /* fill by style: the clear (096B), stipple, AND and OR fills */
    { "VGAME.EXE", 0x1377, 0x0B1B, 0x01B0 },
    { "VGAME.EXE", 0x1377, 0x0B23, 0x01B8 },
    { "VGAME.EXE", 0x1377, 0x0B2B, 0x01C0 },
    { "VGAME.EXE", 0x1377, 0x0B33, 0x01C8 },
    { "VGAME.EXE", 0x1377, 0x0886, 0x0012 },     /* planar fill: a skipped row's tail at 0874 */
    { "VGAME.EXE", 0x1377, 0x08A3, 0x002F },     /* planar fill rows: 0874, and the exit at 089C */
    { "VGAME.EXE", 0x1377, 0x094A, 0x00D6 },     /* planar one-byte span: on to 092E, then 08A3, 0874, 089C */
    { "VGAME.EXE", 0x1377, 0x00F3, 0x0031 },     /* planar row offsets: the 640-wide loop (00C2) and its RET (00F2) lie below */
};

static uint16_t code_below(const recomp_override *o)
{
    for (unsigned i = 0; i < sizeof CODE_BELOW / sizeof CODE_BELOW[0]; i++)
        if (!strcmp(CODE_BELOW[i].module, o->module) && CODE_BELOW[i].seg == o->seg && CODE_BELOW[i].ip == o->ip)
            return CODE_BELOW[i].below;
    return 0;
}

static machine_t g_m;           /* side 1's CPU lives in a machine: matched code takes one */

/* Code the C runtime's routines share and reach by a JMP, which can lie
 * before the routine or past its 0x300 bytes: the endings of the DOS calls
 * (sm4_dos_end in matched.c) and the number conversion itoa and ltoa end in
 * (sm4_ntoa). A RET there, at the caller's level, is the routine's own. */
static const struct { const char *module; uint16_t seg, lo, hi; } SHARED_ENDINGS[] = {
    { "VGAME.EXE", 0x0000, 0xF0CC, 0xF0F4 },
    { "PLAYER.EXE", 0x0000, 0x186E, 0x1896 },
    { "MPS_LOGO.EXE", 0x0146, 0x0B3E, 0x0B66 },
    { "DSWAP.EXE", 0x0000, 0x13EA, 0x1412 },
    { "VGAME.EXE", 0x0000, 0xF542, 0xF5A2 },
    { "PLAYER.EXE", 0x0000, 0x1F70, 0x1FD0 },
    { "MPS_LOGO.EXE", 0x0146, 0x1A70, 0x1AD0 },
};
static uint16_t g_end_lo[4], g_end_hi[4];   /* the routine's module's shared code */
static int g_nend;

/* A matched routine's call into original code, run here by plain stepping:
 * the harness has no events to service. */
/* Step until the routine's own RET (near, or far) taken with SP at entry_sp. */
/* The routine's own RET: at the caller's stack level and inside the routine's
 * code (within 0x300 bytes from its entry, or its SPANS range). A random callee that pops one word
 * too many returns to the caller's address from far away - a RET the routine
 * did not make, after which the two sides are not comparable. */
static int g_trace;                     /* print every instruction of a re-run, to see where two sides part */
static uint16_t g_reach;                /* the furthest instruction run inside the routine, from g_from */
static uint16_t g_below;                /* how far the routine's code reaches below its entry (CODE_BELOW) */
/* For those routines the original's code is watched after every step: a write that lands in it and is
 * written back later (a row table over the code when DS = CS) runs changed instructions and leaves no
 * trace in the end state. */
static int g_self_written, g_watch_transient;
static uint64_t g_camera_seen;
/* Large named routines whose instruction coverage is tracked: which of
 * their instruction starts the original ran in this state, and over
 * compared states (all, and those the named routine finished itself). A
 * floor fails a 4,000-state run whose fixtures stopped reaching the arms. */
static const struct { const char *module; uint16_t seg, ip, len; unsigned insns, floor; const char *what; } COVERED[] = {
    { "VGAME.EXE", 0, 0x1EB0, 0x0DD7, 1274, 1250, "flight input" },
    { "VGAME.EXE", 0, 0x3ADA, 0x0A30, 848, 820, "frame update" },
    { "VGAME.EXE", 0, 0x5046, 0x0247, 195, 190, "ground defences" },
};
static int g_flight_fixture, g_frame_fixture, g_state_index;   /* flight-input fixtures; the state being compared */
static int g_cov = -1;                  /* the COVERED entry under test, or -1 */
static uint16_t g_cov_ip;
static unsigned long long g_cov_escapes;
static unsigned long long g_cov_skips[6];   /* fault, long run, bad return, code written, declined, unfinished */
static uint8_t g_cov_seen[0x1000], g_cov_cover[0x1000], g_cov_whole[0x1000];
/* The stack depths the original had at each instruction (up to four;
 * five means more): a callee return the original reached at the call's
 * depth, or up to 16 bytes above it (what the callee's RET n removed),
 * must be reached by the named code too. */
static uint16_t g_cov_sp[0x1000][4];
static uint8_t g_cov_nsp[0x1000];

static int cov_reached(uint16_t ip, uint16_t lo, uint16_t span)
{
    const unsigned k = (uint16_t)(ip - g_cov_ip);
    if (g_cov < 0 || k >= COVERED[g_cov].len) return 0;
    if (g_cov_nsp[k] > 4) return 1;
    for (unsigned j = 0; j < g_cov_nsp[k]; j++) if ((uint16_t)(g_cov_sp[k][j] - lo) <= span) return 1;
    return 0;
}
static uint64_t g_moving_seen;
static int g_camera_fixture;
static int g_hud_fixture;
static int g_ground_fixture;
static uint64_t g_hud_seen;
static uint64_t g_cockpit_seen;
static unsigned g_nav_seen;
static unsigned g_panel_seen;           /* original branches in the current panel fixture */
static unsigned g_arms_seen;            /* original arming setup branches in this fixture */
static int run_to_ret(cpu_t *a, uint16_t entry_sp, int kind, int *steps, uint16_t cs, uint16_t ip)
{
    const uint32_t watch = phys(cs, (uint16_t)(ip - g_below));
    const int watching = (g_below || g_watch_transient) && a->mem == g_mem[0];
    while (*steps < 100000) {
        const unsigned watch_bytes = (ip == 0x3ADA || ip == 0x1EB0 || ip == 0xBCEF || ip == 0x983E || ip == 0x0016 || ip == 0x79BE || ip == 0x8A90) ? g_span : 0x300u + g_below;
        if (watching && !g_self_written && memcmp(a->mem + watch, g_pristine + watch, watch_bytes)) g_self_written = 1;
        if (g_trace) printf("      %04X:%04X clk %llu sp %04X\n", a->seg[S_CS], a->ip, (unsigned long long)a->icount, a->r[R_SP]);
        const uint16_t here = a->ip;
        const int own = a->seg[S_CS] == cs && (uint16_t)(here - g_from) < g_span;
        if (ip == 0x07EE && own && a->mem == g_mem[0]) {
            static const uint16_t branch[] = { 0x0822, 0x0841, 0x0862, 0x08B7, 0x08EA };
            for (unsigned k = 0; k < sizeof branch / sizeof branch[0]; k++)
                if (here == branch[k]) g_arms_seen |= 1u << k;
            if (here == 0x0820 && x86_cond(a, 7)) g_arms_seen |= 1u << 5;
            if (here == 0x08E8 && !(a->flags & F_ZF)) g_arms_seen |= 1u << 6;
        }
        if (g_camera_fixture && own && a->mem == g_mem[0]) {
            static const uint16_t branch[] = { 0xBD14, 0xBD1C, 0xBD48, 0xBD59,
                0xBD63, 0xBD6D, 0xBD80, 0xBDDA, 0xBEC9, 0xBE7F, 0xBF2E,
                0xBF42, 0xBF5E, 0xBF7D, 0xBF90, 0xBFB2, 0xC058, 0xC079,
                0xC0AC, 0xC101, 0xC10A, 0xC145, 0xC150, 0xC168, 0xC1DC,
                0xC266, 0xC2C5, 0xC2E4, 0xC348, 0xC378, 0xC3AD, 0xC3FD };
            for (unsigned k = 0; k < sizeof branch / sizeof branch[0]; k++)
                if (here == branch[k]) g_camera_seen |= UINT64_C(1) << k;
        }
        if (ip == 0x983E && own && a->mem == g_mem[0]) {
            static const uint16_t branch[] = { 0x984B, 0x98D5, 0x98EF, 0x9916,
                0x9923, 0x994A, 0x9988, 0x99A3, 0x99CB, 0x99D3, 0x99DB,
                0x99F3, 0x9A1D, 0x9A7C, 0x9A9D, 0x9AB7, 0x9B42,
                0x9B99, 0x9BCA, 0x9C19, 0x9C63, 0x9C77, 0x9CBD,
                0x9D0E, 0x9AFC, 0x9B16, 0x9D39, 0x9D9A, 0x9E1B,
                0x9E16, 0x9E6C, 0x9ECA };
            for (unsigned k = 0; k < sizeof branch / sizeof branch[0]; k++)
                if (here == branch[k]) g_moving_seen |= UINT64_C(1) << k;
        }
        if (ip == 0x0016 && own && a->mem == g_mem[0]) {
            static const uint16_t branch[] = { 0x0049, 0x00D9, 0x01D6, 0x022E,
                0x0262, 0x030F, 0x0370, 0x0377, 0x0397, 0x0418, 0x04A0,
                0x0544, 0x059B, 0x05F7, 0x0653, 0x0697, 0x0709, 0x079B,
                0x0803, 0x0806, 0x082D, 0x087A, 0x08FB, 0x0924, 0x092B,
                0x093F, 0x0946, 0x0985, 0x098C, 0x09A0, 0x09A7, 0x09CA };
            for (unsigned k = 0; k < sizeof branch / sizeof branch[0]; k++)
                if (here == branch[k]) g_hud_seen |= UINT64_C(1) << k;
        }
        if (ip == 0x79BE && own && a->mem == g_mem[0]) {
            static const uint16_t branch[] = { 0x79D3, 0x79E2, 0x7A06, 0x7A2E,
                0x7A6D, 0x7A97, 0x7AAC, 0x7AE2, 0x7B81, 0x7C02, 0x7C4D,
                0x7C66, 0x7C96, 0x7CCD, 0x7D59, 0x7DBE, 0x7DF9, 0x7E39,
                0x7E67, 0x7EDA, 0x7F0C, 0x7F3F, 0x7F58, 0x7FA5, 0x801B,
                0x803D, 0x8080, 0x80F8, 0x8125, 0x8155, 0x8181, 0x81E4 };
            for (unsigned k = 0; k < sizeof branch / sizeof branch[0]; k++)
                if (here == branch[k]) g_cockpit_seen |= UINT64_C(1) << k;
        }
        if (ip == 0x8A90 && own && a->mem == g_mem[0]) {
            static const uint16_t branch[] = { 0x8ADC, 0x8B55, 0x8BB8,
                0x8C18, 0x8C3F, 0x8C8F, 0x8D45, 0x8CAD, 0x8CC0,
                0x8D62, 0x8DA1, 0x8DC5, 0x8DF3, 0x8E17, 0x8E5B,
                0x8E6A, 0x8EB7, 0x8EC5, 0x8ED3 };
            for (unsigned k = 0; k < sizeof branch / sizeof branch[0]; k++)
                if (here == branch[k]) g_nav_seen |= 1u << k;
        }
        if (ip == 0x9515 && own && a->mem == g_mem[0]) {
            static const uint16_t branch[] = { 0x9570, 0x957D, 0x9582, 0x9587,
                0x958C, 0x9591, 0x9596, 0x959B, 0x9532, 0x96DD,
                0x9670, 0x9699, 0x961C, 0x9646 };
            for (unsigned k = 0; k < sizeof branch / sizeof branch[0]; k++)
                if (here == branch[k]) g_panel_seen |= 1u << k;
            if (here == 0x95A4 && x86_cond(a, 7)) g_panel_seen |= 1u << 14;
        }
        if (g_cov >= 0 && own && a->mem == g_mem[0] && (uint16_t)(here - g_cov_ip) < COVERED[g_cov].len) {
            const unsigned k = (uint16_t)(here - g_cov_ip);
            g_cov_seen[k] = 1;
            if (!cov_reached(here, a->r[R_SP], 0)) {
                if (g_cov_nsp[k] < 4) g_cov_sp[k][g_cov_nsp[k]] = a->r[R_SP];
                if (g_cov_nsp[k] < 5) g_cov_nsp[k]++;
            }
        }
        if (own && (uint16_t)(here - g_from) > g_reach) g_reach = (uint16_t)(here - g_from);
        int inside = own;
        for (int k = 0; k < g_nend && !inside; k++) inside = a->seg[S_CS] == cs && here >= g_end_lo[k] && here < g_end_hi[k];
        const uint8_t op = a->mem[phys(a->seg[S_CS], a->ip)];
        const uint16_t sp_before = a->r[R_SP];
        const int depth_before = a->int_depth;
        cpu_step(a);
        if (a->flags & F_TF) cpu_interrupt(a, 1);
        ++*steps;
        /* A HUD divide fault is a comparable boundary even though it cannot
         * return normally. Stop before interpreting the random vector target. */
        if (g_camera_fixture && own && a->int_depth && here == 0xBE53) return 2;
        if (g_ground_fixture && a->int_depth > depth_before)
            return own && here == 0x5122 ? 2 : 0;
        if (g_hud_fixture && own && a->int_depth &&
            (here == 0x0051 || here == 0x0063 || here == 0x01BC || here == 0x0349)) return 2;
        /* The routine's own return: near RET (kind 1), far RETF (2), IRET (3). */
        const int is_ret = kind == 3 ? op == 0xCF : kind == 2 ? (op == 0xCB || op == 0xCA) : (op == 0xC3 || op == 0xC2);
        if (is_ret && sp_before == entry_sp) return inside;
        /* The C runtime's near stackavail takes its return address off into
         * CX and returns by JMP CX: that, with the address already popped. */
        if (kind == 1 && op == 0xFF && a->mem[phys(a->seg[S_CS], (uint16_t)(here + 1))] == 0xE1 &&
                sp_before == (uint16_t)(entry_sp + 2) && a->ip == a->r[R_CX])
            return inside;
    }
    return 0;
}

static unsigned long long g_probed;     /* mid-run stops compared */
static unsigned long long g_finished;   /* states the original code finished */
static int g_ncalls;           /* guest calls made by the routine under test */
static int g_lost;              /* a call into original code never came back */
static int g_stopped;           /* a call into original code was stopped by the event limit, as a run loop would */
static int g_callover;          /* a call into original code was started with the clock already past the limit */
/* The routine's own return: the caller's address with the stack back above
 * it. A call into original code can end the routine itself (_write's flush
 * leaves through _write's ending on a failed write); the run stops there, as
 * the original side does. */
static uint16_t g_back_cs, g_back_ip, g_back_sp;
static int step_runner(machine_t *mm)
{
    cpu_t *c = &mm->cpu;
    const uint16_t call_sp = (uint16_t)(c->r[R_SP] + (mm->trap_cs == c->seg[S_CS] ? 2 : 4));  /* before the CALL pushed */
    g_ncalls++;
    if (c->icount > c->stop_at) g_callover = 1;  /* the CALL itself ran at or after the limit */
    for (int i = 0; i < 200000; i++) {
        if (c->ip == mm->trap_ip && c->r[R_SP] == mm->trap_sp && c->seg[S_CS] == mm->trap_cs) return RUN_TRAP;
        if (c->ip == g_back_ip && c->r[R_SP] == g_back_sp && c->seg[S_CS] == g_back_cs) {
            /* A tracked routine's call whose return was never caught although
             * the original did come back there from this call's depth: the
             * original then finished
             * the routine and the state would compare equal without testing
             * the named code after it. (A callee that wanders off in both
             * engines - random code bytes, a random stack - never returns
             * in the original either.) */
            if (g_cov >= 0 && mm->trap_cs == g_back_cs && cov_reached(mm->trap_ip, call_sp, 16)) {
                g_cov_escapes++;
                printf("  ESCAPED CALL (state %d): trap %04X:%04X sp %04X, at %04X:%04X sp %04X flags %04X\n", g_state_index,
                       mm->trap_cs, mm->trap_ip, mm->trap_sp, c->seg[S_CS], c->ip, c->r[R_SP], c->flags);
            }
            return RUN_SLICE;
        }
        if (c->icount >= c->stop_at) { g_stopped = 1; return RUN_SLICE; }
        if (g_trace) printf("      (callee) %04X:%04X clk %llu sp %04X\n", c->seg[S_CS], c->ip, (unsigned long long)c->icount, c->r[R_SP]);
        cpu_step(c);
        if (c->flags & F_TF) cpu_interrupt(c, 1);
    }
    g_lost = 1;
    return RUN_SLICE;
}

int main(int argc, char **argv)
{
    int states = 2000;
    const char *only = NULL;        /* --only MODULE:IP[,MODULE:IP...]: just those routines (IP in hex), for quick runs */
    unsigned shard = 0, shards = 1; /* --shard K/N: every Nth routine from K (tools/func_lockstep_par.py) */
    uint64_t seed = 0x5EED0F117AULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--states") && i + 1 < argc) states = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--verbose")) g_verbose = 1;
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--shard") && i + 1 < argc && sscanf(argv[++i], "%u/%u", &shard, &shards) == 2 && shard < shards) {}
        else { fprintf(stderr, "usage: func_lockstep [--states N] [--seed S] [--verbose] [--only MODULE:IP,...] [--shard K/N]\n"); return 2; }
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
        if (mi % shards != shard) continue;
        g_rng = routine_seed(seed, o);
        if (only) {
            char key[64];
            snprintf(key, sizeof key, "%s:%X", o->module, (unsigned)o->ip);
            const char *f = strstr(only, key);
            if (!f || (f != only && f[-1] != ',') || (f[strlen(key)] && f[strlen(key)] != ',')) continue;
        }
        const rc_module *m = NULL;
        for (unsigned k = 0; k < RC_NMODULES; k++)
            if (!strcmp(RC_MODULES[k]->name, o->module) && RC_MODULES[k]->file_hash == o->file_hash) m = RC_MODULES[k];
        if (!m) { printf("%s %04X:%04X: module not in the generated code\n", o->module, o->seg, o->ip); bad++; continue; }
        g_below = code_below(o);
        /* The pair swap can overwrite its own epilogue, fall through into
         * the adjacent opposite-order helper, then restore the code bytes.
         * PLAYER's format reader can similarly overwrite its RET through
         * an aliased frame slot. Final memory misses these changed-code inputs. */
        g_camera_fixture = !strcmp(o->module, "VGAME.EXE") && o->seg == 0 && o->ip == 0xBCEF;
        g_flight_fixture = !strcmp(o->module, "VGAME.EXE") && o->seg == 0 && o->ip == 0x1EB0;
        g_frame_fixture = !strcmp(o->module, "VGAME.EXE") && o->seg == 0 && o->ip == 0x3ADA;
        g_cov = -1;
        for (unsigned k = 0; k < sizeof COVERED / sizeof COVERED[0]; k++)
            if (!strcmp(o->module, COVERED[k].module) && o->seg == COVERED[k].seg && o->ip == COVERED[k].ip) g_cov = (int)k;
        if (g_cov >= 0) { g_cov_ip = COVERED[g_cov].ip; g_cov_escapes = 0; memset(g_cov_skips, 0, sizeof g_cov_skips); memset(g_cov_cover, 0, sizeof g_cov_cover); memset(g_cov_whole, 0, sizeof g_cov_whole); }
        g_hud_fixture = !strcmp(o->module, "VGAME.EXE") && o->seg == 0x1058 && o->ip == 0x0016;
        g_ground_fixture = !strcmp(o->module, "VGAME.EXE") && o->seg == 0 && o->ip == 0x5046;
        g_watch_transient = (!strcmp(o->module, "VGAME.EXE") && o->seg == 0x1058 && o->ip == 0x0016) ||
            (o->seg == 0 &&
            ((!strcmp(o->module, "VGAME.EXE") && (o->ip == 0x5046 || o->ip == 0x3ADA || o->ip == 0x1EB0 || o->ip == 0xBCEF || o->ip == 0x983E || o->ip == 0x79BE || o->ip == 0x8A90)) ||
             (!strcmp(o->module, "START.EXE") && (o->ip == 0x39B3 || o->ip == 0x39D5)) ||
             (!strcmp(o->module, "END.EXE") && (o->ip == 0x1CB1 || o->ip == 0x1CD3)) ||
             (!strcmp(o->module, "PLAYER.EXE") && o->ip == 0x1E90)));
        for (uint32_t a = 0; a < MEM_SIZE; a += 8) { uint64_t v = rnd(); memcpy(g_pristine + a, &v, 8); }
        const uint32_t at = (uint32_t)base * 16u + m->origin;
        memcpy(g_pristine + at, m->image, m->size);
        stub_driver_thunks(g_pristine, at, m, o);
        patch_image_for(g_pristine, o);
        memcpy(g_mem[0], g_pristine, MEM_SIZE);
        memcpy(g_mem[1], g_pristine, MEM_SIZE);
        const uint16_t cs = (uint16_t)(base + o->seg), ip = o->ip;
        g_from = ip; g_span = 0x300;
        for (unsigned k = 0; k < sizeof SPANS / sizeof SPANS[0]; k++)
            if (!strcmp(SPANS[k].module, o->module) && SPANS[k].ip == o->ip) { g_from = SPANS[k].from; g_span = SPANS[k].span; }
        if (g_below) { g_from = (uint16_t)(ip - g_below); g_span = (uint16_t)(0x300u + g_below); }   /* CODE_BELOW as a range */
        g_checked_alloc = !strcmp(o->module, "DSWAP.EXE") && o->seg == 0 && o->ip == 0x0494;
        g_alloc_errors = 0;
        g_gradient_full = 0;
        g_sprite_zero = g_sprite_skip = 0;
        g_iret_full = g_iret_special = 0;
        const interrupt_frame_fixture *irq_frame = interrupt_frame(o);
        const int sprite_run = !strcmp(o->module, "MPS_LOGO.EXE") && o->seg == 0x006D && o->ip == 0x0678;
        const int camera_gradients = !strcmp(o->module, "VGAME.EXE") && o->seg == 0 && (o->ip == 0x1974 || o->ip == 0x19FD);
        const int objectives_panel = !strcmp(o->module, "VGAME.EXE") && o->seg == 0 && o->ip == 0x9515;
        uint64_t camera_branches = 0;
        unsigned long long camera_faults = 0;
        uint64_t moving_branches = 0;
        unsigned hud_fault_mask = 0;
        unsigned long long hud_faults = 0;
        unsigned ground_fault_mask = 0;
        unsigned long long ground_faults = 0;
        uint64_t hud_branches = 0;
        uint64_t cockpit_branches = 0;
        unsigned nav_branches = 0;
        unsigned panel_branches = 0;
        const int armament_screen = !strcmp(o->module, "START.EXE") && o->seg == 0 && o->ip == 0x07EE;
        unsigned arms_branches = 0;
        unsigned long long arms_returning = 0, arms_actual = 0;
        g_dos = 0;
        for (unsigned k = 0; k < sizeof DOS_STUBS / sizeof DOS_STUBS[0]; k++)
            if (!strcmp(DOS_STUBS[k].module, o->module) && DOS_STUBS[k].ip == o->ip) g_dos = 1;
        g_nend = 0;
        for (unsigned k = 0; k < sizeof SHARED_ENDINGS / sizeof SHARED_ENDINGS[0] && g_nend < 4; k++)
            if (!strcmp(SHARED_ENDINGS[k].module, o->module) && SHARED_ENDINGS[k].seg == o->seg) {
                g_end_lo[g_nend] = SHARED_ENDINGS[k].lo; g_end_hi[g_nend++] = SHARED_ENDINGS[k].hi;
            }
        unsigned long long mc = 0, ms = 0, mb = 0;
        for (int s = 0; s < states; s++) {
            if (s == states / 2) {
                /* The second half runs on memory of mostly 00, FF and 01
                 * bytes: random words almost never hit a routine's exact
                 * tests (a -1 sentinel, a zero count), these hit them often. */
                static const uint8_t pick[4] = { 0x00, 0xFF, 0x01, 0x00 };
                for (uint32_t a = 0; a < MEM_SIZE; a++) g_pristine[a] = pick[rnd() & 3];
                memcpy(g_pristine + at, m->image, m->size);
                stub_driver_thunks(g_pristine, at, m, o);
                patch_image_for(g_pristine, o);
                memcpy(g_mem[0], g_pristine, MEM_SIZE);
                memcpy(g_mem[1], g_pristine, MEM_SIZE);
            }
            uint16_t r[8], seg[4];
            for (int k = 0; k < 8; k++) r[k] = (uint16_t)rnd();
            for (int k = 0; k < 4; k++) seg[k] = (uint16_t)rnd();
            if (s % 3 == 0) { seg[S_DS] = cs; seg[S_ES] = cs; }
            /* Sentinel words a routine tests for exactly, planted where its
             * registers point: random memory has none, and the 00/FF/01 half
             * has no 80h byte. Each in half the states, by routine. */
            for (unsigned pk = 0; pk < sizeof PLANTS / sizeof PLANTS[0]; pk++) {
                if (PLANTS[pk].seg != o->seg || PLANTS[pk].ip != o->ip || (rnd() & 1)) continue;
                const uint32_t a = phys(seg[S_DS], (uint16_t)((PLANTS[pk].reg == PLANT_ABS ? 0 : r[PLANTS[pk].reg]) + PLANTS[pk].disp));
                if (a + 1 >= at && a < at + m->size + 2) continue;           /* not into the image */
                for (int k = 0; k < 2; k++) {
                    const uint8_t b = (uint8_t)(PLANTS[pk].val >> (8 * k));
                    g_pristine[(a + (uint32_t)k) & 0xFFFFF] = g_mem[0][(a + (uint32_t)k) & 0xFFFFF] =
                        g_mem[1][(a + (uint32_t)k) & 0xFFFFF] = b;
                }
            }
            /* A quarter of the states take small counts in AX, CX and DX,
             * for routines whose arguments are registers (an exact fit, a
             * zero length). */
            if ((s & 0x0C) == 0x0C) {
                static const uint16_t tiny[8] = { 0, 1, 2, 3, 0xFFFF, 0xFFFE, 0, 1 };
                r[R_AX] = tiny[rnd() & 7]; r[R_CX] = tiny[rnd() & 7]; r[R_DX] = tiny[rnd() & 7];
            }
            /* Explicit transparent and zero-length sprite pairs; zero repeats
             * 256 pixels, a case absent from the shipped logo asset. */
            if (sprite_run && (s & 7) == 0) {
                static const uint16_t pairs[8] = { 0xFC00, 0xFC01, 0xFCFF, 0x0000,
                    0x7F00, 0xFF01, 0xFFFE, 0x00FF };
                r[R_AX] = pairs[(s >> 3) & 7];
            }
            /* One state in seven plants a part-switch escape (8000h-800Fh) and,
             * after it, an ordinary small word at ES:SI: routines that read a
             * tagged stream from there never meet a tag in random memory. */
            if (s % 7 == 3 && (phys(seg[S_ES], r[R_SI]) < at || phys(seg[S_ES], r[R_SI]) >= at + m->size + 2)) {
                /* (never inside the loaded image: it would persist in the routine's own code) */
                const uint32_t a = phys(seg[S_ES], r[R_SI]);
                const uint16_t tag = (uint16_t)(0x8000u | (rnd() & 0x0F));
                for (int k = 0; k < 2; k++) {
                    const uint8_t b = (uint8_t)(tag >> (8 * k));
                    g_pristine[(a + (uint32_t)k) & 0xFFFFF] = g_mem[0][(a + (uint32_t)k) & 0xFFFFF] =
                        g_mem[1][(a + (uint32_t)k) & 0xFFFFF] = b;
                }
            }
            r[R_SP] = (uint16_t)((r[R_SP] | 0x0100) & 0xFFFE);
            const uint16_t back = irq_frame && (s & 1) ? (uint16_t)(irq_frame->fault + irq_frame->advance)
                                                       : (uint16_t)(ip + 0x8000);
            const uint16_t flags = (uint16_t)((rnd() & 0x0ED5u) | 0x0002u);
            uint16_t small[4];
            for (int a = 0; a < 4; a++)
                small[a] = (s & 4) ? (uint16_t)((int)(rnd() % 3) - 1) : (uint16_t)(rnd() % 32);
            for (unsigned pk = 0; pk < sizeof ARG_PLANTS / sizeof ARG_PLANTS[0]; pk++)
                if (!strcmp(ARG_PLANTS[pk].module, o->module) && ARG_PLANTS[pk].ip == o->ip && (rnd() & 1))
                    small[ARG_PLANTS[pk].arg] = ARG_PLANTS[pk].v[rnd() & 1];
            /* Equal gradient offsets exercise the complete sixteen-strip walk.
             * Leave other states random and never plant into the code image. */
            if (camera_gradients && (s & 7) == 2) {
                int safe = 1;
                for (unsigned j = 0; j < 34; j++) {
                    const uint32_t a = phys(seg[S_DS], (uint16_t)(0x0D22 + j));
                    if (a >= at && a < at + m->size + 2) safe = 0;
                }
                if (safe) {
                    for (unsigned j = 0; j < 34; j++) {
                        const uint32_t a = phys(seg[S_DS], (uint16_t)(0x0D22 + j));
                        g_pristine[a] = g_mem[0][a] = g_mem[1][a] = 0;
                    }
                    small[0] = o->ip == 0x1974 ? 169 : 110;
                    small[1] = (uint16_t)((s >> 3) & 1);
                }
            }
            /* A controlled helper result reaches the caller's own IDIV
             * fault frame. Other states execute the real range helper. */
            if (g_camera_fixture) {
                static const uint8_t forced_range[] = { 0xB8, 0xFF, 0xFF, 0xC3 };
                const int force_range = (s & 7) == 2 && ((s >> 3) % 5) == 0;
                for (unsigned j = 0; j < 4; j++) {
                    const uint32_t address = at + 0xC6B3 + j;
                    g_pristine[address] = g_mem[0][address] = g_mem[1][address] =
                        force_range ? forced_range[j] : m->image[0xC6B3 + j];
                }
            }
            /* Small camera worlds exercise all view modes and zoom signs;
             * retain random states alongside them and exclude loaded code. */
            if (g_camera_fixture && ((s & 7) == 1 || (s & 7) == 2)) {
                const unsigned v = (unsigned)s >> 3;
                static const uint16_t modes[] = { 0, 1, 2, 3, 4, 0xFFFF };
                static const uint16_t zooms[] = { 0, 1, 2, 0xFFFF, 0xFFFE, 31 };
                static const uint16_t heights[] = { 0, 15, 16, 1000, 0xFFFF };
                struct { uint16_t at, value; } words[96]; unsigned n = 0;
#define CAMERA_WORD(a_, v_) do { words[n].at = (uint16_t)(a_); words[n++].value = (uint16_t)(v_); } while (0)
                CAMERA_WORD(0x4070, 0x2100); CAMERA_WORD(0x4010, 0x2000);
                CAMERA_WORD(0x990E, (v & 1) ? 3 : 1); CAMERA_WORD(0x034A, (v / 7) & 1);
                CAMERA_WORD(0x43E0, (v / 5) & 1); CAMERA_WORD(0x483E, (v / 11) & 1);
                CAMERA_WORD(0x40B6, (v / 13) & 1); CAMERA_WORD(0x294A, (v & 1) ? 0xFF00 : 0);
                CAMERA_WORD(0xC0D0, 1000); CAMERA_WORD(0xC0DE, 1000); CAMERA_WORD(0x2DF4, heights[v % 5]);
                CAMERA_WORD(0x2DEE, v * 0x1237); CAMERA_WORD(0x2DF0, v * 0x0321); CAMERA_WORD(0x2DF2, v * 0x0111);
                CAMERA_WORD(0xB2B4, 32000); CAMERA_WORD(0xB2B6, 0); CAMERA_WORD(0xB77E, 32000); CAMERA_WORD(0xB780, 16); CAMERA_WORD(0xB788, heights[(v / 3) % 5]);
                CAMERA_WORD(0x3D94, 0); CAMERA_WORD(0x3D92, 0); CAMERA_WORD(0xB2D6, (v & 2) ? 0x80 : 0);
                CAMERA_WORD(0xB2DC, v % 4); CAMERA_WORD(0xC0D8, v % 3); CAMERA_WORD(0xDED0, (v + 1) % 3);
                CAMERA_WORD(0xC182, (v & 4) ? 0x20 : 0); CAMERA_WORD(0x368A, (v / 17) & 1);
                CAMERA_WORD(0x9D3C, (v & 8) ? v % 16 : 0); CAMERA_WORD(0x9D3E, (v / 19) & 1);
                for (unsigned j = 0; j < 8; j++) {
                    CAMERA_WORD(0x436E + 2 * j, j == 0 ? 0 : 100 + 25 * j);
                    CAMERA_WORD(0x437E + 2 * j, 0x100 + 32 * j);
                }
                for (unsigned j = 0; j < 4; j++) { CAMERA_WORD(0x0960 + 2 * j, (j * 2) | ((j * 2 + 1) << 8)); CAMERA_WORD(0x09E0 + 2 * j, 0x0707); }
                for (unsigned j = 0; j < 14; j++) CAMERA_WORD(0x3C3A + 2 * j, j < 2 ? 1000 : j == 2 ? 100 : j == 4 ? 0x4000 : 0);
#undef CAMERA_WORD
                const uint16_t args[] = { (uint16_t)(v % 8), (uint16_t)(1000 + 100 * (v % 7)), (uint16_t)(1000 - 50 * (v % 7)), heights[(v / 2) % 5],
                    (uint16_t)(v * 0x0137), (uint16_t)(v * 0x0311), (uint16_t)(v * 0x0123), modes[(v / 3) % 6], zooms[(v / 18) % 6] };
                int safe = 1;
                for (unsigned j = 0; j < n; j++) for (unsigned byte = 0; byte < 2; byte++) {
                    const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                    if (address >= at && address < at + m->size + 2) safe = 0;
                }
                for (unsigned j = 0; j < 9; j++) for (unsigned byte = 0; byte < 2; byte++) {
                    const uint32_t address = phys(seg[S_SS], (uint16_t)(r[R_SP] + 2*j + byte));
                    if (address >= at && address < at + m->size + 2) safe = 0;
                }
                if (safe) {
                    for (unsigned j = 0; j < n; j++) for (unsigned byte = 0; byte < 2; byte++) {
                        const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                        g_pristine[address] = g_mem[0][address] = g_mem[1][address] = (uint8_t)(words[j].value >> (8 * byte));
                    }
                    for (unsigned j = 0; j < 9; j++) for (unsigned byte = 0; byte < 2; byte++) {
                        const uint32_t address = phys(seg[S_SS], (uint16_t)(r[R_SP] + 2*j + byte));
                        g_pristine[address] = g_mem[0][address] = g_mem[1][address] = (uint8_t)(args[j] >> (8 * byte));
                    }
                    for (unsigned j = 0; j < 4; j++) small[j] = args[j];
                }
            }
            /* Flight input: in some states the key poll (EBC4) reports the
             * pushed-back word [929C] and counts it down, and the BIOS read
             * (EE6A) returns a chosen key once and an ordinary key after, so
             * every command arm runs and the pause waits end; the quit
             * prompt's driver poll reports a key and its read answers Y or N.
             * Other classes plant small worlds for post-ejection, low-speed
             * pitch recovery, ground braking and touchdown. Everything else
             * stays random. */
            g_state_index = s;
            if (g_flight_fixture) {
                static const uint8_t poll[] = { 0xA1, 0x9C, 0x92, 0xFF, 0x0E, 0x9C, 0x92, 0xC3 };
                static const uint16_t deltas[] = { 0x011B, 0x0A15, 0x00FD, 0x0032, 0x00CC, 0x0012, 0x02C3, 0x0900, 0x0600, 0x0F63, 0x019D };
                const unsigned v = (unsigned)s >> 3;
                const int keyed = (s & 7) == 1;
                uint16_t key = 0;
                for (unsigned j = 0; j <= v % 13 && j < 11; j++) key = (uint16_t)(key + deltas[j]);
                if (v % 13 == 11) key = 0x1E61;
                if (v % 13 == 12) key = 0;
                const uint32_t h = (uint32_t)v * 0x9E3779B1u;
                static const uint8_t read[] = { 0xA1, 0x9E, 0x92, 0xC7, 0x06, 0x9E, 0x92, 0x61, 0x1E, 0xC3 };
                const uint8_t prompt[] = { 0x31, 0xC0, 0xCB, 0, 0, 0xB8, (h >> 20) & 1 ? 0x79 : 0x6E, 0x00, 0xCB };
                for (unsigned j = 0; j < sizeof poll; j++) {
                    const uint32_t address = at + 0xEBC4 + j;
                    g_pristine[address] = g_mem[0][address] = g_mem[1][address] = keyed ? poll[j] : m->image[0xEBC4 + j];
                }
                for (unsigned j = 0; j < sizeof read; j++) {
                    const uint32_t address = at + 0xEE6A + j;
                    g_pristine[address] = g_mem[0][address] = g_mem[1][address] = keyed ? read[j] : m->image[0xEE6A + j];
                }
                /* The quit prompt's page saves/restores (1039:00D1/00E8/0101)
                 * and its message (D52E) return at once in keyed states:
                 * whole-page copies and text drawing outrun the step limit. */
                static const struct { uint32_t at; uint8_t op; } quick[] = {
                    { 0x10461, 0xCB }, { 0x10478, 0xCB }, { 0x10491, 0xCB }, { 0x0D52E, 0xC3 } };
                for (unsigned j = 0; j < sizeof quick / sizeof quick[0]; j++) {
                    const uint32_t address = at + quick[j].at;
                    g_pristine[address] = g_mem[0][address] = g_mem[1][address] = keyed ? quick[j].op : m->image[quick[j].at];
                }
                /* The crash pause (04C7) waits for timer ticks no state
                 * delivers; the joystick read and re-centre (1058:0CB2,
                 * 1058:0C91) poll the game port with interrupts off. In half
                 * of the fixture states they return at once. */
                const int quick_io = (s & 7) != 0 && (v & 1);
                static const struct { uint32_t at; uint8_t op; } io[] = { { 0x004C7, 0xC3 }, { 0x11232, 0xCB }, { 0x11211, 0xCB } };
                for (unsigned j = 0; j < sizeof io / sizeof io[0]; j++) {
                    const uint32_t address = at + io[j].at;
                    g_pristine[address] = g_mem[0][address] = g_mem[1][address] =
                        quick_io || (j == 0 && ((s & 7) == 4 || (s & 7) >= 6)) ? io[j].op : m->image[io[j].at];
                }
                for (unsigned j = 0; j < sizeof prompt; j++) {          /* thunks 1E42:02DC, 1E42:02E1 */
                    const uint32_t address = at + 0x1E6FC + j;
                    if (keyed && (j < 3 || j >= 5)) g_pristine[address] = g_mem[0][address] = g_mem[1][address] = prompt[j];
                    else if (!keyed) g_pristine[address] = g_mem[0][address] = g_mem[1][address] = m->image[0x1E6FC + j] == 0xEA ? 0xCB : m->image[0x1E6FC + j];
                }
                struct { uint16_t at, value; } words[48]; unsigned n = 0;
#define FLIGHT_WORD(a_, v_) do { words[n].at = (uint16_t)(a_); words[n++].value = (uint16_t)(v_); } while (0)
                static const uint16_t rates[] = { 1, 2, 3, 5, 8, 15, 30 };
                if ((s & 7) != 0) FLIGHT_WORD(0x368E, rates[v % 7]);
                if (keyed) {
                    FLIGHT_WORD(0x929C, key ? 1 : 0); FLIGHT_WORD(0x929E, key);
                    FLIGHT_WORD(0x2E0A, (h >> 24) % 3 == 0 ? 100 : (h >> 24) % 3 == 1 ? 0 : 7);
                    FLIGHT_WORD(0x2E0E, (h >> 16) & 1); FLIGHT_WORD(0xC09A, (h >> 17) & 1);
                    FLIGHT_WORD(0xC0CE, (h >> 18) & 1 ? 0 : 50); FLIGHT_WORD(0x9B34, (h >> 19) & 1 ? 8 : 0);
                    FLIGHT_WORD(0x368C, (h >> 21) & 1);
                    if (v % 13 == 1 && (h >> 22) & 1) {    /* released brakes at full throttle on the ground */
                        FLIGHT_WORD(0x9B34, 8); FLIGHT_WORD(0xC0CE, 50); FLIGHT_WORD(0x2E0A, 100);
                    }
                }
                if ((s & 7) == 2) {                  /* low-speed pitch recovery */
                    FLIGHT_WORD(0x9B34, 1 | ((v & 1) << 3)); FLIGHT_WORD(0xE472, (v / 2) & 1 ? 0 : 0xFFF0);
                    FLIGHT_WORD(0xB03A, 0); FLIGHT_WORD(0xB07E, 0x0200 + 0x40 * (v % 5));
                    FLIGHT_WORD(0x2DF2, (v / 4) & 1 ? 0x0800 : 0x3800); FLIGHT_WORD(0x991A, (v / 8) % 4 == 0);
                    FLIGHT_WORD(0x98F6, 0x0400); FLIGHT_WORD(0x2DF0, (v / 16) & 1 ? 0x0200 : 0x0600);
                    FLIGHT_WORD(0x2DF4, 20 * ((v / 32) % 4)); FLIGHT_WORD(0x2DF6, 20 * ((v / 32) % 4));
                    FLIGHT_WORD(0xC0CE, 0); FLIGHT_WORD(0xC09A, 0); FLIGHT_WORD(0x3664, 0);
                }
                if ((s & 7) == 3) {                  /* after ejection */
                    static const uint16_t counts[] = { 1, 0x0F, 0x10, 0x13, 0x1F, 0x40 };
                    FLIGHT_WORD(0xC09A, counts[v % 6]); FLIGHT_WORD(0x3D8E, (v / 6) % 4 ? 0x21 : 0x22);
                    FLIGHT_WORD(0x2DF4, (v / 24) & 1 ? 0 : 300); FLIGHT_WORD(0x2DF6, (v / 24) & 1 ? 0 : 300);
                    FLIGHT_WORD(0xC0CE, 0); FLIGHT_WORD(0x3D96, (v / 48) & 1 ? 0xFFFF : 0);
                    FLIGHT_WORD(0x9912, (v / 96) & 1 ? 8 : 1); FLIGHT_WORD(0xDEF6, (v / 192) & 1 ? 0x100 : 0);
                }
                if ((s & 7) == 4 || (s & 7) >= 6) {  /* descending onto the ground */
                    static const uint16_t grounds[] = { 0, 40, 0x1000, 0x1F00 };
                    const uint16_t ground = grounds[v % 4];
                    FLIGHT_WORD(0xC0CE, ground); FLIGHT_WORD(0x2DF6, ground + 1 + (v / 4) % 3);
                    FLIGHT_WORD(0x2DF4, ground + 1); FLIGHT_WORD(0x2DF0, 0xC000); FLIGHT_WORD(0x98F6, 0);
                    FLIGHT_WORD(0xB07E, (v / 12) & 1 ? 0x3000 : 0x1000); FLIGHT_WORD(0x3DA2, (v / 24) % 5 != 0); FLIGHT_WORD(0x368E, 1);
                    FLIGHT_WORD(0xC09A, (v / 36) & 1); FLIGHT_WORD(0x9B34, (v / 72) & 1);
                    FLIGHT_WORD(0xE00C, (v / 144) % 4); FLIGHT_WORD(0x2DF2, (v / 288) & 1 ? 0x2000 : 0);
                    FLIGHT_WORD(0x9910, 0); FLIGHT_WORD(0x2E0A, 100); FLIGHT_WORD(0xB19C, 100);
                }
                if ((s & 7) == 5) {                  /* braking on the ground */
                    static const uint16_t speeds[] = { 0, 0x100, 0x1B0, 0x400, 0x2000 };
                    FLIGHT_WORD(0x9B34, 8); FLIGHT_WORD(0xC0CE, (v / 5) % 3 ? 50 : 0);
                    FLIGHT_WORD(0x2DF4, (v / 15) % 3 ? 50 : 60); FLIGHT_WORD(0x2DF6, 50); FLIGHT_WORD(0xB07E, speeds[v % 5]);
                }
#undef FLIGHT_WORD
                int safe = 1;
                for (unsigned j = 0; j < n; j++) for (unsigned byte = 0; byte < 2; byte++) {
                    const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                    if (address >= at && address < at + m->size + 2) safe = 0;
                }
                if (safe) for (unsigned j = 0; j < n; j++) for (unsigned byte = 0; byte < 2; byte++) {
                    const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                    g_pristine[address] = g_mem[0][address] = g_mem[1][address] = (uint8_t)(words[j].value >> (8 * byte));
                }
                /* The aircraft record's word +40h (read through [9924]) weighs
                 * the touchdown test and the ground brake; captured states
                 * hold zero there. */
                if (safe && ((s & 7) == 2 || (s & 7) == 4 || (s & 7) >= 5)) {
                    const uint16_t record = g_mem[0][phys(seg[S_DS], 0x9924)] | g_mem[0][phys(seg[S_DS], 0x9925)] << 8;
                    const uint16_t record_seg = g_mem[0][phys(seg[S_DS], 0x9926)] | g_mem[0][phys(seg[S_DS], 0x9927)] << 8;
                    const uint16_t value = (uint16_t)(v % 5);
                    for (unsigned byte = 0; byte < 2; byte++) {
                        const uint32_t address = phys(record_seg, (uint16_t)(record + 0x40 + byte));
                        if (address >= at && address < at + m->size + 2) continue;
                        g_pristine[address] = g_mem[0][address] = g_mem[1][address] = (uint8_t)(value >> (8 * byte));
                    }
                }
            }
            /* Frame update: small airfield worlds. The field type [E00C]
             * indexes 16-byte records at B2D0h (X, Y, flags); the player
             * sits on or near one, low and slow, so the carrier wire and
             * catapult, field contact, landing objectives (18-byte records
             * at E304h) and the tower message arms run. */
            if (g_frame_fixture && (s & 7) != 0) {
                const unsigned v = (unsigned)s >> 3;
                const uint32_t h = (uint32_t)v * 0x9E3779B1u;
                const uint16_t t = (uint16_t)(v % 4), rec = (uint16_t)(0xB2D0 + 16 * t);
                const uint16_t fx = (uint16_t)(0x1000 + 0x40 * (v % 7)), fy = (uint16_t)(0x2000 + 0x30 * (v % 5));
                struct { uint16_t at, value; } words[48]; unsigned n = 0;
#define FRAME_WORD(a_, v_) do { words[n].at = (uint16_t)(a_); words[n++].value = (uint16_t)(v_); } while (0)
                static const uint16_t rates[] = { 1, 2, 4, 8, 15 };
                FRAME_WORD(0x368E, rates[(v / 7) % 5]); FRAME_WORD(0xE00C, t); FRAME_WORD(0xC0E0, t + 1);
                FRAME_WORD(rec, fx); FRAME_WORD(rec + 2, fy);
                for (unsigned j = 0; j < t; j++) FRAME_WORD(0xB2D6 + 16 * j, 0);   /* not eligible */
                /* The map cell comes from the 32-bit position: X = (P+10h)>>5,
                 * Y = 8000h - ((P+10h)>>5). */
                const uint16_t px = (uint16_t)(fx + ((s & 7) == 1 ? 0 : (h >> 17) & 1 ? 0 : 0x40));
                const uint16_t py = (uint16_t)((s & 7) == 1 ? fy + 0x10 + (v % 6) : fy + ((h >> 18) & 1 ? 0 : 0x30));
                const uint16_t qy = (uint16_t)(0x8000 - py);
                FRAME_WORD(0xBA3C, px << 5); FRAME_WORD(0xBA3E, px >> 11); FRAME_WORD(0xC056, qy << 5); FRAME_WORD(0xC058, qy >> 11);
                FRAME_WORD(0x9922, (h >> 16) & 1 ? 0 : 0x300);
                if ((s & 7) == 1) {                  /* carrier: wire and catapult */
                    FRAME_WORD(rec + 6, 0x0300 | ((h >> 21) & 1 ? 0x0800 : 0));
                    FRAME_WORD(0x2DF4, 0x80); FRAME_WORD(0xC0CE, 0x80); FRAME_WORD(0xB194, (h >> 22) & 1 ? 0x60 : 0x40);
                    FRAME_WORD(0xB19A, 1 + ((h >> 19) & 1)); FRAME_WORD(0x2DEE, (h >> 20) & 1 ? 0x0100 : 0x3000);
                } else {                             /* a field: contact, objectives, tower */
                    static const uint16_t flags[] = { 0x0101, 0x0401, 0x0501, 0x0D01, 0x0201 };
                    FRAME_WORD(rec + 6, flags[v % 5]); FRAME_WORD(0x2DF4, (h >> 19) & 1 ? 0 : 0x20);
                    FRAME_WORD(0xC0CE, 0); FRAME_WORD(0xB194, (h >> 20) & 1); static const uint16_t ticks[] = { 0x10, 0x13, 0x80, 0x00 };
                    FRAME_WORD(0x3D8E, ticks[(h >> 21) & 3]);
                    for (unsigned j = 0; j < 2; j++) {
                        FRAME_WORD(0xE304 + 18 * j, (h >> (22 + j)) & 1 ? 4 : 3); FRAME_WORD(0xE306 + 18 * j, j == ((h >> 24) & 1) ? t : 9);
                    }
                    FRAME_WORD(0x9912, 1); FRAME_WORD(0xDEB4, (h >> 25) & 1 ? 5 : 0);
                    FRAME_WORD(0x3DC8, (v / 5) % 4); FRAME_WORD(0x3DC2, (h >> 26) & 1);
                    FRAME_WORD(0xE31A, (h >> 27) & 1 ? t : 7); FRAME_WORD(0x3666, (h >> 28) & 1 ? 0x1000 : 0x2000);
                    FRAME_WORD(0x46F2, (h >> 29) & 1 ? 2 : 1); FRAME_WORD(0xE008, (h >> 30) & 1 ? 0x14 : 0x13);
                }
#undef FRAME_WORD
                int safe = 1;
                for (unsigned j = 0; j < n; j++) for (unsigned byte = 0; byte < 2; byte++) {
                    const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                    if (address >= at && address < at + m->size + 2) safe = 0;
                }
                if (safe) for (unsigned j = 0; j < n; j++) for (unsigned byte = 0; byte < 2; byte++) {
                    const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                    g_pristine[address] = g_mem[0][address] = g_mem[1][address] = (uint8_t)(words[j].value >> (8 * byte));
                }
            }
            /* Small moving-object worlds separate target cycling, shadow
             * height, detail reduction, distant dots and exterior rendering.
             * Returning projection supplies controlled depth; real range and
             * bearing math still executes against these coordinates. */
            if (!strcmp(o->module, "VGAME.EXE") && o->seg == 0 && o->ip == 0x983E &&
                ((s & 7) == 1 || (s & 7) == 2)) {
                const unsigned v = (unsigned)s >> 3;
                static const uint16_t views[] = { 0, 0x8B, 0x80, 0x81 };
                static const uint16_t heights[] = { 0, 128, 999, 1500, 1600, 65535 };
                static const uint16_t depths[] = { 0xFFFF, 0xFFF0, 0xFFEF, 0xFFE1, 0xFFE0, 0xFF01, 0xFF00, 0, 1 };
                struct { uint16_t at, value; } words[512];
                unsigned n = 0;
#define MOVING_PLANT(a_, v_) do { words[n].at = (uint16_t)(a_); words[n++].value = (uint16_t)(v_); } while (0)
                MOVING_PLANT(0xC0A6, views[v % 4]); MOVING_PLANT(0x2DF4, heights[(v / 4) % 6]);
                MOVING_PLANT(0x368A, (v / 7) % 2); MOVING_PLANT(0x368C, (v / 5) % 2); MOVING_PLANT(0x9922, (v & 2) ? 100 : 0);
                MOVING_PLANT(0xC0D0, 1000); MOVING_PLANT(0xC0DE, 1000); MOVING_PLANT(0xE582, 0);
                MOVING_PLANT(0x2DEE, (v / 3) * 0x2137); MOVING_PLANT(0x2DF0, 0); MOVING_PLANT(0x2DF2, 0);
                MOVING_PLANT(0x3D94, v % 3 == 0 ? 0xFFFF : v % 3 == 1 ? 0x80 : 0);
                MOVING_PLANT(0xB2BA, (v / 11) % 3); MOVING_PLANT(0xE008, v % 3 == 0 ? 0x13 : v % 3 == 1 ? 0x19 : 0);
                MOVING_PLANT(0xE32A, (v / 9) % 3); MOVING_PLANT(0xC0E0, v % 7 == 0 ? 0 : 3);
                MOVING_PLANT(0x3D92, (v / 5) % 3 == 0 ? 0xFFFF : (v / 5) % 3 == 1 ? 0x80 : 0);
                MOVING_PLANT(0xDEFC, v % 13 == 0 ? 0 : 4); MOVING_PLANT(0xE00C, 0);
                MOVING_PLANT(0x9930, 3200); MOVING_PLANT(0x9B36, 3200);
                MOVING_PLANT(0x4A10, v % 17 == 0 ? 0xFFFF : 100); MOVING_PLANT(0x4A18, 80); MOVING_PLANT(0xDEBE, depths[(v / 2) % 9]);
                MOVING_PLANT(0x4010, 0x2000); MOVING_PLANT(0x4028, 0x2040); MOVING_PLANT(0x4040, 0x2080);
                MOVING_PLANT(0xC62E, (v & 1) ? 0 : 100); MOVING_PLANT(0xC0D2, 1000); MOVING_PLANT(0xC0E4, 1000);
                MOVING_PLANT(0xB782, (v & 4) ? 1 : 0); MOVING_PLANT(0xC09A, (v & 8) ? 1 : 0); MOVING_PLANT(0xC0CE, 100);
                for (unsigned j = 0; j < 3; j++) {
                    const uint16_t object_at = (uint16_t)(0xB2D0 + 16 * j);
                    MOVING_PLANT(object_at, 1000 + 20 * j); MOVING_PLANT(object_at + 2, 1000 - 20 * j);
                    MOVING_PLANT(object_at + 6, (v & 8) ? 0x80 : 0x701); MOVING_PLANT(object_at + 12, 1);
                }
                for (unsigned j = 0; j < 4; j++) {
                    const uint16_t object_at = (uint16_t)(0xC16C + 36 * j);
                    for (unsigned k = 0; k < 18; k++) {
                        const uint16_t value = k == 0 ? (uint16_t)(1000 + 10 * j) : k == 1 ? (uint16_t)(1000 - 10 * j) :
                            k == 2 ? (uint16_t)((v & 16) ? 1000 : 100) : k == 3 || k == 5 ? 32000 :
                            k == 7 ? 0x4000 : k == 10 ? (uint16_t)j : k == 11 ? (uint16_t)((v & 32) ? 0 : ((j & 1) ? 0x22 : 2)) :
                            k == 12 ? (uint16_t)((v & 64) ? 0 : 1) : 0;
                        MOVING_PLANT(object_at + 2 * k, value);
                    }
                }
                for (unsigned j = 0; j < 16; j++) {
                    const uint16_t object_at = (uint16_t)(0x3A08 + 8 * j);
                    MOVING_PLANT(object_at, ((v + j) & 3) ? 1000 : 0); MOVING_PLANT(object_at + 2, 1000); MOVING_PLANT(object_at + 4, 100); MOVING_PLANT(object_at + 6, (v + j) % 5);
                }
                for (unsigned j = 0; j < 12; j++) {
                    const uint16_t object_at = (uint16_t)(0x3C3A + 28 * j);
                    for (unsigned k = 0; k < 14; k++) {
                        const uint16_t value = k < 2 ? 1000 : k == 2 ? 100 : k == 4 ? 0x4000 : k == 6 ? 0xF000 :
                            k == 7 ? (uint16_t)(((v + j) & 1) ? 1 : 0) : k == 8 ? (uint16_t)(j % 4) : 0;
                        MOVING_PLANT(object_at + 2 * k, value);
                    }
                }
#undef MOVING_PLANT
                int safe = 1;
                for (unsigned j = 0; j < n; j++) for (unsigned byte = 0; byte < 2; byte++) {
                    const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                    if (address >= at && address < at + m->size + 2) safe = 0;
                }
                if (safe) for (unsigned j = 0; j < n; j++) for (unsigned byte = 0; byte < 2; byte++) {
                    const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                    g_pristine[address] = g_mem[0][address] = g_mem[1][address] = (uint8_t)(words[j].value >> (8 * byte));
                }
            }
            /* HUD worlds: both layouts and all three modes, tape boundary
             * values, signed pitch bands and roll quadrants. Graphics thunks
             * return, while the original fixed-point trig remains live. */
            if (!strcmp(o->module, "VGAME.EXE") && o->seg == 0x1058 && o->ip == 0x0016 &&
                ((s & 7) == 1 || (s & 7) == 2)) {
                const unsigned v = (unsigned)s >> 3;
                const int half = (v / 3) & 1;
                const int fault = (s & 7) == 2 && v % 11 == 0 ? (int)((v / 11) % 4) : -1;
                static const uint16_t speed[] = { 0, 49, 50, 51, 12749, 12799 };
                static const uint16_t height[] = { 0, 99, 100, 999, 1000, 65535 };
                static const uint16_t pitch[] = { 0, 1, 0xFFFF, 0x1000, 0xF000, 0x4000, 0xC000, 0x7FFF, 0x8000 };
                const struct { uint16_t at, value; } words[] = {
                    { 0x294A, (uint16_t)((fault >= 0 ? (fault == 2 ? 1 : 0) : v % 3) | (half << 8)) },
                    { 0xB194, (uint16_t)(fault == 0 ? 12800 : fault == 1 ? 49 : fault == 3 ? 100 : speed[(v / 6) % 6]) }, { 0x2DF6, (uint16_t)(fault == 3 ? 999 : height[(v / 7) % 6]) },
                    { 0x2DF0, pitch[(v / 5) % 9] }, { 0x2DF2, (uint16_t)(v * 0x1379) },
                    { 0x2DEE, (uint16_t)(v * 0x2173) }, { 0xE56E, (uint16_t)(v & 1) },
                    { 0x291D, (uint16_t)(half ? 78 : 90) }, { 0x291F, 17 },
                    { 0x2920, (uint16_t)(half ? 10 : 20) }, { 0x2922, (uint16_t)((fault == 1 || fault == 3) ? 8 : !half) },
                    { 0x2923, (uint16_t)(half ? 102 : 49) }, { 0x2925, (uint16_t)(half ? 204 : 255) },
                    { 0x2927, (uint16_t)(half ? 18 : 45) }, { 0x2929, (uint16_t)(fault == 2 ? 65535 : half ? 175 : 248) },
                    { 0x292B, (uint16_t)(fault == 2 ? 0 : half ? 4 : 10) }, { 0x292D, (uint16_t)(half ? 2 : 5) },
                    { 0x292F, (uint16_t)(half ? -23 : -50) }, { 0x2931, (uint16_t)(half ? -8 : -12) },
                    { 0x2933, (uint16_t)(half ? 9 : 13) }, { 0x2935, (uint16_t)(half ? 23 : 50) },
                    { 0x2937, (uint16_t)(half ? 26 : 52) }, { 0x2939, (uint16_t)(half ? 52 : 104) },
                    { 0x293A, (uint16_t)(half ? 31 : 79) }, { 0x293C, (uint16_t)(half ? 32 : 36) },
                    { 0x293E, (uint16_t)(half ? 60 : 52) }, { 0x2940, 159 },
                    { 0x2942, 5200 }, { 0x2944, 160 }, { 0x2946, 68 }
                };
                int safe = 1;
                for (unsigned j = 0; j < sizeof words / sizeof words[0]; j++)
                    for (unsigned byte = 0; byte < 2; byte++) {
                        const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                        if (address >= at && address < at + m->size + 2) safe = 0;
                    }
                if (safe) for (unsigned j = 0; j < sizeof words / sizeof words[0]; j++)
                    for (unsigned byte = 0; byte < 2; byte++) {
                        const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                        g_pristine[address] = g_mem[0][address] = g_mem[1][address] =
                            (uint8_t)(words[j].value >> (8 * byte));
                    }
            }
            /* Valid panel worlds exercise full/compact/external views, mode
             * changes, warning lamps, waypoint advance, message countdown and
             * the left-altitude inset. Keep some random and budget-stop states. */
            if (!strcmp(o->module, "VGAME.EXE") && o->ip == 0x79BE &&
                ((s & 7) == 1 || (s & 7) == 2)) {
                const unsigned v = (unsigned)s >> 3;
                static const uint16_t views[] = { 0, 0, 0, 0x42, 0x80 };
                static const uint16_t heights[] = { 99, 100, 101, 0xFFFF };
                const struct { uint16_t at, value; } words[] = {
                    { 0x368C, (uint16_t)((v & 32) ? 0 : 1) },
                    { 0x2DF6, heights[v & 3] }, { 0x991E, (uint16_t)(v & 1) },
                    { 0xC0A6, views[v % 5] }, { 0x294A, (uint16_t)((v / 5) % 3) },
                    { 0x294B, (uint16_t)((v / 15) % 3) },
                    { 0xE32A, (uint16_t)(v % 3) }, { 0x40CA, (uint16_t)((v + 1) % 3) },
                    { 0x2E08, (uint16_t)(v & 1) }, { 0xE474, 200 }, { 0xB194, 100 },
                    { 0xC0CE, 1000 }, { 0x2DF4, (uint16_t)((v & 4) ? 1000 : 1001) },
                    { 0x3D8E, (uint16_t)((v & 8) ? 0 : 1) }, { 0x4742, 1 },
                    { 0x4010, 0x2000 }, { 0x4028, 0x2040 }, { 0x4040, 0x2080 },
                    { 0x3680, 0 }, { 0x3670, 0 }, { 0x3672, 4 }, { 0x3682, 100 },
                    { 0x9924, 0x1000 }, { 0x9926, seg[S_DS] },
                    { 0x1040, (uint16_t)((v & 2) ? 0 : 1) }, { 0x1042, (uint16_t)(v & 1) },
                    { 0x2CA0, 0x0605 }, { 0x98F6, 0 }, { 0xC0D0, 0xFC00 }, { 0xC0DE, 0xFD00 },
                    { 0xE31A, 0 }, { 0xB2D0, 0xFC00 }, { 0xB2D2, 0xFD00 },
                    { 0xB2D6, (uint16_t)((v & 1) ? 0x200 : 0) }, { 0x3666, 0xFFFF },
                    { 0xDF06, 1 }, { 0x9B34, (uint16_t)((v & 2) ? 0x1601 : 0x1600) },
                    { 0xB2B8, (uint16_t)(v & 1) }, { 0x368A, (uint16_t)(v & 1) },
                    { 0xE586, (uint16_t)((v & 16) ? 16 : 0xFFF0) }, { 0xE00C, 0 },
                    { 0xE57C, 0 }, { 0xE572, 0 }, { 0x36A6, 0 }, { 0x33A0, 7 },
                    { 0x3DA8, (uint16_t)(v % 3) }, { 0x3664, (uint16_t)((v & 4) ? 8 : 0) },
                    { 0x2EAE, (uint16_t)((v & 1) ? 0 : 3) },
                    { 0x2E9E, 0xFC00 }, { 0x2EA0, 0xFD00 },
                    { 0x2EAA, 0xF000 }, { 0x2EAC, 0xF000 },
                    { 0x2DEE, 0 }, { 0x40CC, 1 }
                };
                int safe = 1;
                for (unsigned j = 0; j < sizeof words / sizeof words[0]; j++)
                    for (unsigned byte = 0; byte < 2; byte++) {
                        const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                        if (address >= at && address < at + m->size + 2) safe = 0;
                    }
                if (safe) for (unsigned j = 0; j < sizeof words / sizeof words[0]; j++)
                    for (unsigned byte = 0; byte < 2; byte++) {
                        const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                        g_pristine[address] = g_mem[0][address] = g_mem[1][address] =
                            (uint8_t)(words[j].value >> (8 * byte));
                    }
            }
            /* Small navigation worlds expose marker kinds and highlights while
             * keeping the real projection/trig helpers. Include budget-stop
             * states; leave the remaining states entirely random. */
            if (!strcmp(o->module, "VGAME.EXE") && o->ip == 0x8A90 &&
                ((s & 7) == 1 || (s & 7) == 2)) {
                const unsigned variant = (unsigned)s >> 3;
                static const uint16_t zoom[] = { 0, 1, 2, 3, 0xFFFF, 0xFF00, 0xFE01, 0x0100 };
                static const uint16_t object_flags[] = { 0, 0x80, 0x280, 0x201, 8, 0x209 };
                struct { uint16_t at, value; } words[160];
                unsigned nw = 0;
#define NAV_WORD(a, v) do { words[nw].at = (uint16_t)(a); words[nw++].value = (uint16_t)(v); } while (0)
                NAV_WORD(0x4010, 0x2000); NAV_WORD(0x4028, 0x2040);
                NAV_WORD(0x40B0, zoom[variant & 7]); NAV_WORD(0x990E, (variant >> 3) & 1);
                NAV_WORD(0xC0D0, 0xFC00); NAV_WORD(0xC0DE, 0xFD00);
                NAV_WORD(0x2DEE, 0); NAV_WORD(0x2DF4, 1000);
                NAV_WORD(0xDEFC, variant % 4); NAV_WORD(0xC0E0, (variant / 4) % 4);
                NAV_WORD(0xE32A, 1 + (variant & 1));
                NAV_WORD(0x3D92, 0); NAV_WORD(0x3D94, 0); NAV_WORD(0x3D98, 1);
                NAV_WORD(0xC0AA, (variant & 1) ? 0 : 0xFFFF); NAV_WORD(0x98A2, 5);
                NAV_WORD(0x33A0, 1); NAV_WORD(0x33B2, 0);
                for (unsigned j = 0; j < 3; j++) {
                    NAV_WORD(0xC16C + 0x24*j, 0xFC00); NAV_WORD(0xC16E + 0x24*j, 0xFD00);
                    NAV_WORD(0xC170 + 0x24*j, (j == 0) ? 0xFFFF : (j == 1) ? 1000 : 2001);
                    NAV_WORD(0xC17A + 0x24*j, 0xF800);
                    NAV_WORD(0xC182 + 0x24*j, (variant & 16) ? 0 : 2);
                    NAV_WORD(0xC184 + 0x24*j, (variant & 32) ? 0 : 1);
                    NAV_WORD(0xB2D0 + 16*j, 0xFC00); NAV_WORD(0xB2D2 + 16*j, 0xFD00);
                    NAV_WORD(0xB2D4 + 16*j, (variant >> 4) & 1);
                    NAV_WORD(0xB2D6 + 16*j, object_flags[(variant + j) % 6]);
                }
                for (unsigned j = 0; j < 12; j++) {
                    NAV_WORD(0x3C48 + 0x1C*j, (j == 0 || j == 8) ? 1 : 0);
                    if (j == 0 || j == 8) {
                        NAV_WORD(0x3C3A + 0x1C*j, 0xFC00); NAV_WORD(0x3C3C + 0x1C*j, 0xFD00);
                        NAV_WORD(0x3C42 + 0x1C*j, 0); NAV_WORD(0x3C4A + 0x1C*j, variant & 1);
                    }
                }
                for (unsigned j = 0; j < 4; j++) {
                    NAV_WORD(0x39D8 + 12*j, 0xFC00); NAV_WORD(0x39DA + 12*j, 0xFD00);
                    NAV_WORD(0x39DE + 12*j, j + 1); NAV_WORD(0x39E0 + 12*j, 1);
                }
#undef NAV_WORD
                int safe = 1;
                for (unsigned j = 0; j < nw; j++) for (unsigned byte = 0; byte < 2; byte++) {
                    const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                    if (address >= at && address < at + m->size + 2) safe = 0;
                }
                if (safe) for (unsigned j = 0; j < nw; j++) for (unsigned byte = 0; byte < 2; byte++) {
                    const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                    g_pristine[address] = g_mem[0][address] = g_mem[1][address] =
                        (uint8_t)(words[j].value >> (8 * byte));
                }
            }
            /* Half the arming states isolate the caller with returning drawing,
             * formatting and mouse helpers. The other half retain those actual
             * helpers (only the picture loader is always replaced here).
             * Restore the original first byte when switching back. */
            const int returning_arms = armament_screen && (s & 8);
            if (armament_screen) {
                static const uint16_t calls[] = { 0x0D65, 0x959E, 0x3567, 0x0E96, 0x3E84 };
                for (unsigned j = 0; j < sizeof calls / sizeof calls[0]; j++) {
                    const uint32_t address = at + calls[j];
                    g_pristine[address] = g_mem[0][address] = g_mem[1][address] =
                        returning_arms ? 0xC3 : m->image[calls[j]];
                }
            }
            /* Valid settings/mission far pointers reach the arming caller's
             * four/two/zero-station and enabled/disabled weapon paths. */
            if (armament_screen && (returning_arms || (s & 7) == 1 || (s & 7) == 2)) {
                const unsigned variant = (unsigned)s >> 3;
                const struct { uint16_t at, value; } words[] = {
                    { 0xCACA, 0x1000 }, { 0xCACC, seg[S_DS] },
                    { 0xE096, 0x1200 }, { 0xE098, seg[S_DS] },
                    { 0x1042, (uint16_t)(variant % 3) },
                    { 0xD2A2, (uint16_t)(variant & 1) },
                    { 0x1238, 0 }, { 0x123A, 6 }, { 0x123C, 12 }, { 0x123E, 18 },
                    { 0x122E, (uint16_t)((variant & 2) ? 0 : 100) },
                    { 0xD878, 0x0100 }, { 0xD87A, 0x0001 }, { 0xD87C, 0x0100 },
                    { 0xD87E, 0x0001 }, { 0xD880, 0x0100 }, { 0xD882, 0x0001 },
                    { 0xD884, 0x0100 }, { 0xD886, 0x0001 }
                };
                int safe = 1;
                for (unsigned j = 0; j < sizeof words / sizeof words[0]; j++)
                    for (unsigned byte = 0; byte < 2; byte++) {
                        const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                        if (address >= at && address < at + m->size + 2) safe = 0;
                    }
                if (safe) for (unsigned j = 0; j < sizeof words / sizeof words[0]; j++)
                    for (unsigned byte = 0; byte < 2; byte++) {
                        const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                        g_pristine[address] = g_mem[0][address] = g_mem[1][address] =
                            (uint8_t)(words[j].value >> (8 * byte));
                    }
            }
            /* Small objective records expose all eight phrase targets, invalid
             * kinds, completed flags, deadlines and empty/nonempty places.
             * Leave three quarters random, and never plant into loaded code.
             * Actual string/format/display helpers still run on both sides. */
            if (objectives_panel && ((s & 7) == 1 || (s & 7) == 2)) {
                /* Include states 1 mod 4: the harness also probes an exact
                 * instruction-budget stop inside those planted paths. */
                const unsigned variant = (unsigned)s >> 3;
                const uint16_t kind = (uint16_t)(variant % 10);
                const uint16_t done = (uint16_t)(((variant / 10) & 3) << 13);
                const uint16_t place = (uint16_t)((variant / 40) & 1);
                const struct { uint16_t at, value; } words[] = {
                    { 0xE304, kind }, { 0xE316, (uint16_t)(9 - kind) },
                    { 0xE306, 0 }, { 0xE318, 1 },
                    { 0xE30A, 2 }, { 0xE31C, 2 }, { 0xE31A, 0 },
                    { 0x9B34, done }, { 0xDEB4, 100 },
                    { 0x9912, (uint16_t)((variant & 1) ? 101 : 99) },
                    { 0xB2DC, 1 }, { 0xB2EC, 1 },
                    { 0xB2CE, place }, { 0xB2DE, place },
                    { 0xDF08, 0x2C00 }, { 0xDF0A, 0x2C10 },
                    { 0x2C00, 0 }, { 0x2C10, 0x0058 }
                };
                int safe = 1;
                for (unsigned j = 0; j < sizeof words / sizeof words[0]; j++)
                    for (unsigned byte = 0; byte < 2; byte++) {
                        const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                        if (address >= at && address < at + m->size + 2) safe = 0;
                    }
                if (safe) for (unsigned j = 0; j < sizeof words / sizeof words[0]; j++)
                    for (unsigned byte = 0; byte < 2; byte++) {
                        const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                        g_pristine[address] = g_mem[0][address] = g_mem[1][address] =
                            (uint8_t)(words[j].value >> (8 * byte));
                    }
            }
            /* Correlated, explicitly staged reload faults. Independent PLANTS
             * do not guarantee that a live, unsuppressed site has an expired
             * timer and the intended divisor together. Index zero is served
             * with this positive budget; skip bars so no callee is involved. */
            if (g_ground_fixture && ((s & 7) == 1 || (s & 7) == 2)) {
                const struct { uint16_t at, value; } words[] = {
                    { 0x368C, 0 }, { 0x992C, 1 }, { 0x3688, 1 },
                    { 0xB2D4, 1 }, { 0xB2D6, 0 }, { 0xB2DA, 1 },
                    { 0xB2D8, (uint16_t)((s & 7) == 1 ? 0xFFC0 : 0xFFBC) },
                    { 0x368E, 0x80 }
                };
                int safe = 1;
                for (unsigned j = 0; j < sizeof words / sizeof words[0]; j++)
                    for (unsigned byte = 0; byte < 2; byte++) {
                        const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                        if (address >= at && address < at + m->size + 2) safe = 0;
                    }
                if (safe) for (unsigned j = 0; j < sizeof words / sizeof words[0]; j++)
                    for (unsigned byte = 0; byte < 2; byte++) {
                        const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                        g_pristine[address] = g_mem[0][address] = g_mem[1][address] =
                            (uint8_t)(words[j].value >> (8 * byte));
                    }
            }
            for (int k = 0; k < 2; k++)
                setup_side(k ? &g_m.cpu : &g_cpu[0], o, cs, ip, r, seg, flags, back, s, small);
            int steps = 0;
            cpu_t *a = &g_cpu[0];
            /* Run the original until the routine's own near RET - the first
             * one taken with the stack back at the caller's level - and
             * accept the state only when it returns to the pushed address. */
            const uint16_t entry_sp = (uint16_t)(r[R_SP] - (o->matched == 3 ? 6 : o->matched == 2 ? 4 : 2));
            g_back_cs = cs; g_back_ip = back; g_back_sp = r[R_SP];
            g_reach = 0; g_camera_seen = 0; g_moving_seen = 0; g_hud_seen = 0; g_cockpit_seen = 0; g_nav_seen = 0; g_panel_seen = 0; g_arms_seen = 0;
            g_self_written = 0;
            if (g_cov >= 0) { memset(g_cov_seen, 0, sizeof g_cov_seen); memset(g_cov_nsp, 0, sizeof g_cov_nsp); }
            const int returned = run_to_ret(a, entry_sp, o->matched, &steps, cs, ip);
            if (returned == 2 && !g_self_written) {
                g_side[0].overflow = g_side[1].overflow = 1;
                g_lost = 0; g_ncalls = 0; g_callover = 0;
                const int ran = o->fn(&g_m);
                if (g_ground_fixture) {
                    /* Require the named code to yield BEFORE IDIV; only then
                     * let the interpreter take the fault and compare its full
                     * interrupt frame, registers, memory, flags and clock. */
                    if (!ran || g_lost || g_m.cpu.seg[S_CS] != cs || g_m.cpu.ip != 0x5122 || g_m.cpu.int_depth) {
                        fprintf(stderr, "ground reload fault did not hand back at 5122 (state %d)\n", s);
                        mc++; mb++; restore(); continue;
                    }
                    ground_fault_mask |= a->r[R_CX] == 0 ? 1 : 2;
                    ground_faults++;
                    cpu_step(&g_m.cpu);
                    if (g_m.cpu.flags & F_TF) cpu_interrupt(&g_m.cpu, 1);
                    g_cpu[1] = g_m.cpu; mc++;
                    g_ctx = " (ground reload divide fault)";
                    mb += compare(o->module, ip, cs, ip, 0);
                    g_ctx = ""; restore(); continue;
                }
                if (!ran || g_lost || !g_m.cpu.int_depth) { if (g_cov >= 0) g_cov_skips[0]++; ms++; restore(); continue; }
                g_cpu[1] = g_m.cpu; mc++; if (g_camera_fixture) camera_faults++; else hud_faults++;
                if (a->op_ip == 0x0051) hud_fault_mask |= 1;
                if (a->op_ip == 0x0063) hud_fault_mask |= 2;
                if (a->op_ip == 0x01BC) hud_fault_mask |= 4;
                if (a->op_ip == 0x0349) hud_fault_mask |= 8;
                g_ctx = g_camera_fixture ? " (camera divide fault)" : " (HUD divide fault)";
                mb += compare(o->module, ((uint32_t)o->seg << 4) + ip, cs, ip, 0);
                g_ctx = ""; restore(); continue;
            }
            const uint16_t reach = g_reach;
            if (!returned || a->seg[S_CS] != cs || a->ip != back || g_self_written) steps = 100000;
            g_side[0].overflow = g_side[1].overflow = 1;      /* compare all memory */
            /* A real return lands back at the caller's stack level (RET n
             * pops at most a few words); a wild jump that happens to reach
             * the return address - a slide through zeroed memory - does not. */
            const uint16_t popped = (uint16_t)(a->r[R_SP] - entry_sp);
            if (steps >= 100000 || popped < 2 || popped > 18) { if (getenv("FLWHY") && o->ip == 0x0815) printf("why1 s=%d steps=%d popped=%u\n", s, steps, popped); if (g_cov >= 0) g_cov_skips[steps >= 100000 ? 1 : 2]++; ms++; restore(); continue; }
            /* A random state that makes the original write over its own code
             * (a copy aimed at the routine) runs instructions it was not; the
             * game never does, and no equivalent can follow it. The window is
             * 0x300 bytes (what run_to_ret counts as the routine's own: a long
             * routine's fixed data words can land in its own code when DS = CS),
             * or to the furthest instruction the original ran in the routine
             * when that lies further: START's route leg runs to +0x123, and a
             * write landing past +0x100 went unseen at one seed. */
            const uint32_t self = phys(cs, g_from);
            const size_t window = reach + 8u > g_span ? (size_t)reach + 8u : g_span;
            if (memcmp(g_mem[0] + self, g_pristine + self, window)) { if (g_cov >= 0) g_cov_skips[3]++; ms++; restore(); continue; }
            {   /* and the shared code it reaches by a JMP */
                int over = 0;
                for (int k = 0; k < g_nend && !over; k++) {
                    const uint32_t lo = phys(cs, g_end_lo[k]);
                    over = memcmp(g_mem[0] + lo, g_pristine + lo, (size_t)(uint16_t)(g_end_hi[k] - g_end_lo[k])) != 0;
                }
                if (over) { if (g_cov >= 0) g_cov_skips[3]++; ms++; restore(); continue; }
            }
            /* The event limit: a routine told it has one instruction fewer than
             * the original takes must decline. Running anyway would carry the
             * clock past a checkpoint or a frame boundary - nothing in the
             * comparison below can see that, as it gives every routine room.
             * (A routine that calls original code legitimately returns partway,
             * having run the callee to the limit; only a run with no such call
             * is an overrun.) */
            g_lost = 0; g_ncalls = 0; g_callover = 0;
            g_m.cpu.stop_at = g_m.cpu.icount + (uint64_t)steps - 1;
            const int probe_ran = o->fn(&g_m);
            if (probe_ran && (g_callover || (!g_ncalls && g_m.cpu.stop_at && g_m.cpu.icount > g_m.cpu.stop_at))) {   /* (a limit of 0 is STI or POPF asking the run loop to look at interrupts: the routine stopped there) */
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
            { const int fr = o->fn(&g_m); if (getenv("FLWHY") && o->ip == 0x0815) printf("why2 s=%d fn=%d lost=%d ip=%04X\n", s, fr, g_lost, g_m.cpu.ip); if (!fr || g_lost) { if (g_cov >= 0) g_cov_skips[4]++; ms++; restore(); continue; } }
            /* An interrupt handler runs whole or declines: it has no partway
             * return, so resuming anywhere but the original's address is a
             * difference, not a state to finish and skip. */
            if (o->matched == 3 && (g_m.cpu.seg[S_CS] != cs || g_m.cpu.ip != back)) {
                printf("  MISMATCH %s+%05X (%04X:%04X): IRET resumed at %04X:%04X, the original at %04X:%04X\n",
                       o->module, ((uint32_t)o->seg << 4) + ip, o->seg, ip, g_m.cpu.seg[S_CS], g_m.cpu.ip, cs, back);
                mc++; mb++; restore(); break;
            }
            /* A routine that ran out of room after a call returns with the
             * machine partway through it, as the original would be; the run
             * loop then finishes it with the original code, and so does this. */
            const int whole = g_m.cpu.seg[S_CS] == cs && g_m.cpu.ip == back;
            if (g_m.cpu.seg[S_CS] != cs || g_m.cpu.ip != back) {
                int more = 0;
                if (!run_to_ret(&g_m.cpu, entry_sp, o->matched, &more, cs, ip)) { if (g_cov >= 0) g_cov_skips[5]++; ms++; restore(); continue; }
                g_finished++;
            }
            mc++;
            if (g_cov >= 0) for (unsigned k = 0; k < COVERED[g_cov].len; k++) {
                g_cov_cover[k] |= g_cov_seen[k]; if (whole) g_cov_whole[k] |= g_cov_seen[k];
            }
            if (g_camera_fixture) camera_branches |= g_camera_seen;
            if (!strcmp(o->module, "VGAME.EXE") && o->seg == 0 && o->ip == 0x983E) moving_branches |= g_moving_seen;
            if (!strcmp(o->module, "VGAME.EXE") && o->seg == 0x1058 && o->ip == 0x0016) hud_branches |= g_hud_seen;
            if (!strcmp(o->module, "VGAME.EXE") && o->ip == 0x79BE) cockpit_branches |= g_cockpit_seen;
            if (!strcmp(o->module, "VGAME.EXE") && o->ip == 0x8A90) nav_branches |= g_nav_seen;
            if (objectives_panel) panel_branches |= g_panel_seen;
            if (armament_screen) {
                arms_branches |= g_arms_seen;
                if (returning_arms) arms_returning++; else arms_actual++;
            }
            if (o->matched == 3) {
                g_iret_full++;
                if (irq_frame && back == (uint16_t)(irq_frame->fault + irq_frame->advance)) g_iret_special++;
            }
            if (sprite_run && (r[R_AX] & 0xFF00) == 0xFC00) g_sprite_skip++;
            if (sprite_run && (r[R_AX] & 0xFF) == 0 && (r[R_AX] & 0xFF00) != 0xFC00) g_sprite_zero++;
            if (g_checked_alloc && g_cpu[0].r[R_AX] == 8) g_alloc_errors++;
            if (camera_gradients && seg_read16(&g_cpu[0], g_cpu[0].seg[S_SS], (uint16_t)(entry_sp - 6)) == 16) g_gradient_full++;
            g_cpu[1] = g_m.cpu;                /* compare() reads g_cpu[1] */
            if (compare(o->module, ((uint32_t)o->seg << 4) + ip, cs, ip, 0)) {
                mb++;
                if (g_verbose) {
                    /* Where do the two sides part? Re-run the state with every instruction printed. */
                    memcpy(g_mem[0], g_pristine, MEM_SIZE); memcpy(g_mem[1], g_pristine, MEM_SIZE);
                    for (int k = 0; k < 2; k++) { g_side[k].io = 0; g_side[k].nread = 0; }   /* port and DOS answers as they were */
                    setup_side(&g_cpu[0], o, cs, ip, r, seg, flags, back, s, small);
                    setup_side(&g_m.cpu, o, cs, ip, r, seg, flags, back, s, small);
                    g_trace = 1;
                    printf("    original, instruction by instruction:\n");
                    int st2 = 0;
                    run_to_ret(&g_cpu[0], entry_sp, o->matched, &st2, cs, ip);
                    printf("    matched, then the original from where it stopped:\n");
                    g_lost = 0; g_stopped = 0;
                    o->fn(&g_m);
                    printf("      matched returned at %04X:%04X clk %llu\n", g_m.cpu.seg[S_CS], g_m.cpu.ip, (unsigned long long)g_m.cpu.icount);
                    int st3 = 0;
                    if (g_m.cpu.ip != back) run_to_ret(&g_m.cpu, entry_sp, o->matched, &st3, cs, ip);
                    g_trace = 0;
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
            /* A stop inside the routine. The run loop stops at exact instruction
             * counts - a checkpoint, a frame - and a matched routine that has made
             * a call or declined partway leaves the machine mid-routine, to be
             * finished by the original code. That state must be the original's at
             * that clock. Stop the matched side at a random count inside the
             * original's path; if it left the machine at some count e, run the
             * original for exactly e instructions from the same entry state and
             * compare everything. (One state in four: it copies memory twice.) */
            if (!mb && steps >= 2 && (s & 3) == 1) {
                setup_side(&g_cpu[0], o, cs, ip, r, seg, flags, back, s, small);
                setup_side(&g_m.cpu, o, cs, ip, r, seg, flags, back, s, small);
                const uint64_t start = g_m.cpu.icount;
                const uint64_t k = 1 + rnd() % (uint64_t)(steps - 1);
                g_m.cpu.stop_at = start + k;
                g_lost = 0; g_ncalls = 0; g_stopped = 0; g_callover = 0;
                const int pr = o->fn(&g_m);
                if (pr && g_callover) {
                    printf("  OVERRUN %s %04X:%04X: a call started past the limit (stop at +%llu)\n", o->module, o->seg, ip, (unsigned long long)k);
                    g_overrun++; mb++;
                }
                if (pr && !g_callover && !g_lost && g_m.cpu.icount - start < (uint64_t)steps) {
                    const uint64_t e = g_m.cpu.icount - start;
                    cpu_t *a = &g_cpu[0];
                    /* by clock, not by step: an instruction can advance the clock by more than one */
                    for (int guard = 0; a->icount < start + e && guard < 200000; guard++) { cpu_step(a); if (a->flags & F_TF) cpu_interrupt(a, 1); }
                    g_side[0].overflow = g_side[1].overflow = 1;
                    g_cpu[1] = g_m.cpu;
                    if (a->icount != start + e) goto probe_done;      /* the clock jumped over the stop: not comparable */
                    g_ctx = " (mid-run stop)";
                    mc++;
                    if (compare(o->module, ((uint32_t)o->seg << 4) + ip, cs, ip, 0)) mb++;
                    g_ctx = "";
                    g_probed++;
                }
                probe_done:
                g_side[0].overflow = g_side[1].overflow = 1;      /* whatever it wrote, put all of memory back */
                restore();
            }
            if (mb) break;
        }
        if (shards > 1) printf("@%u ", mi);                       /* the table index, for the merge */
        printf("%-10s %04X:%04X %-34s %6llu states compared, %4llu skipped, %s\n", o->module, o->seg, ip,
               o->what, mc, ms, mb ? "MISMATCH" : mc ? "equal" : "not testable here (routes only)");
        if (g_checked_alloc) printf("  completed allocation-error states: %llu\n", g_alloc_errors);
        if (camera_gradients) printf("  completed 16-band states: %llu\n", g_gradient_full);
        if (g_cov >= 0) {
            unsigned all = 0, whole = 0;
            for (unsigned k = 0; k < COVERED[g_cov].len; k++) { all += g_cov_cover[k]; whole += g_cov_whole[k]; }
            if (g_cov_escapes) { fprintf(stderr, "%s: %llu calls escaped their return\n", COVERED[g_cov].what, g_cov_escapes); mb++; }
            printf("  %s instructions run by compared states: %u, by states the named routine finished: %u (of %u)\n",
                   COVERED[g_cov].what, all, whole, COVERED[g_cov].insns);
            printf("  skipped: %llu fault, %llu long run, %llu bad return, %llu code written, %llu declined, %llu unfinished\n",
                   g_cov_skips[0], g_cov_skips[1], g_cov_skips[2], g_cov_skips[3], g_cov_skips[4], g_cov_skips[5]);
            /* Seeds reach flight input 1,260-1,274 and the frame update
             * 827-848; fewer means the fixtures stopped reaching whole arms. */
            if (states >= 4000 && whole < COVERED[g_cov].floor) { fprintf(stderr, "%s fixtures reached too little of the routine\n", COVERED[g_cov].what); mb++; }
            const char *cover_path = getenv("F117_COVER");
            FILE *cover = cover_path ? fopen(cover_path, "w") : NULL;
            if (cover) {
                for (unsigned k = 0; k < COVERED[g_cov].len; k++)
                    if (g_cov_cover[k]) fprintf(cover, "%04X %d\n", g_cov_ip + k, g_cov_whole[k]);
                fclose(cover);
            }
        }
        if (g_ground_fixture) {
            printf("  ground reload divide fault states: %llu; mask %X (required 3)\n", ground_faults, ground_fault_mask);
            if (states >= 4000 && ground_fault_mask != 3) { fprintf(stderr, "ground fixtures missed required divide faults\n"); mb++; }
        }
        if (g_camera_fixture) {
            printf("  completed camera branch mask: %08llX (required FFFFFFFF)\n", (unsigned long long)camera_branches);
            printf("  camera divide fault states: %llu\n", camera_faults);
            if (states >= 4000 && !camera_faults) { fprintf(stderr, "camera fixtures missed divide fault boundary\n"); mb++; }
            if (states >= 4000 && camera_branches != UINT64_C(0xFFFFFFFF)) {
                fprintf(stderr, "camera fixtures missed required branches\n"); mb++;
            }
        }
        if (!strcmp(o->module, "VGAME.EXE") && o->seg == 0 && o->ip == 0x983E) {
            printf("  completed moving-object branch mask: %08llX (required FFFFFFFF)\n", (unsigned long long)moving_branches);
            if (states >= 4000 && moving_branches != UINT64_C(0xFFFFFFFF)) {
                fprintf(stderr, "moving-object fixtures missed required branches\n"); mb++;
            }
        }
        if (!strcmp(o->module, "VGAME.EXE") && o->seg == 0x1058 && o->ip == 0x0016) {
            printf("  completed HUD branch mask: %08llX (required FFFFFFFF)\n", (unsigned long long)hud_branches);
            printf("  HUD divide fault states: %llu; mask %X (required F)\n", hud_faults, hud_fault_mask);
            if (states >= 4000 && hud_fault_mask != 15) {
                fprintf(stderr, "HUD fixtures missed required divide faults\n"); mb++;
            }
            if (states >= 4000 && hud_branches != UINT64_C(0xFFFFFFFF)) {
                fprintf(stderr, "HUD fixtures missed required branches\n"); mb++;
            }
        }
        if (!strcmp(o->module, "VGAME.EXE") && o->ip == 0x79BE) {
            printf("  completed cockpit branch mask: %08llX (required FFFFFFFF)\n", (unsigned long long)cockpit_branches);
            if (states >= 4000 && cockpit_branches != UINT64_C(0xFFFFFFFF)) {
                fprintf(stderr, "cockpit fixture missed required branches\n"); mb++;
            }
        }
        if (!strcmp(o->module, "VGAME.EXE") && o->ip == 0x8A90) {
            printf("  completed navigation branch mask: %05X (required 7FFFF)\n", nav_branches);
            if (states >= 4000 && nav_branches != 0x7FFFF) {
                fprintf(stderr, "navigation fixture missed required branches\n"); mb++;
            }
        }
        if (objectives_panel) {
            printf("  completed panel branch mask: %04X (required 7FFF)\n", panel_branches);
            if (states >= 4000 && panel_branches != 0x7FFF) {
                fprintf(stderr, "objectives panel fixture missed required branches\n"); mb++;
            }
        }
        if (armament_screen) {
            printf("  completed arming branch mask: %02X (required 7F); returning/actual-helper states: %llu/%llu\n",
                   arms_branches, arms_returning, arms_actual);
            if (states >= 4000 && (arms_branches != 0x7F || !arms_returning || !arms_actual)) {
                fprintf(stderr, "arming fixture missed required branches or helper modes\n"); mb++;
            }
        }
        if (sprite_run) printf("  completed sprite zero/transparent states: %llu/%llu\n", g_sprite_zero, g_sprite_skip);
        if (o->matched == 3) printf("  completed interrupt/special-site states: %llu/%llu\n", g_iret_full, g_iret_special);
        compared += mc; skipped += ms; bad += mb;
    }
    printf("matched routines: %u, %llu states compared (%llu finished by the original code), %llu skipped, %llu mismatching\n",
           matched_count(), compared, g_finished, skipped, bad);
    return bad ? 1 : 0;
}
