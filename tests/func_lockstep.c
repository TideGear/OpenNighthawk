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
static struct { uint16_t ax; uint8_t cf; uint16_t put; } g_dos_script[16];   /* a fixture may fix the first answers of INT 21h (AX, CF);
                                                                               put 100h|b: a read (3Fh) stores byte b at DS:DX */
static unsigned g_dos_script_n;

static int g_native_int10;   /* answer INT 10h too (the string writer under test) */
static int int_h(cpu_t *c, uint8_t vec)
{
    side_t *s = side_of(c);
    s->io = mix(s->io, 0x3000000ull | vec);
    for (int i = 0; i < 8; i++) s->io = mix(s->io, c->r[i]);
    for (int i = 0; i < 4; i++) s->io = mix(s->io, c->seg[i]);
    s->io = mix(s->io, ((uint64_t)c->ip << 16) | c->flags);
    if (g_dos && vec == 0x21) {     /* a DOS that answers: AX (and DX), and CF one time in four */
        s->nread++;
        const uint8_t ah_in = get_r8(c, R_AH);
        const uint16_t dx_in = c->r[R_DX];
        const uint64_t v = mix(s->io, s->nread);   /* (the record so far is the same on both sides) */
        const int alloc = g_checked_alloc && get_r8(c, R_AH) == 0x48;
        const int wide = get_r8(c, R_AH) == 0x42 || ((v >> 56) & 3) == 1;
        c->r[R_AX] = (uint16_t)(v >> 16);
        if (((v >> 58) & 3) == 0) set_r8(c, R_AL, 0);  /* AL 0 (success, found) one time in four */
        if (alloc) c->r[R_AX] = (v & 1) ? 8 : 0x197A;
        if (wide) c->r[R_DX] = (uint16_t)(v >> 32);
        set_flag(c, F_CF, ((v >> 60) & 3) == 0);
        if (s->nread <= g_dos_script_n) {
            c->r[R_AX] = g_dos_script[s->nread - 1].ax; set_flag(c, F_CF, g_dos_script[s->nread - 1].cf);
            if ((g_dos_script[s->nread - 1].put & 0x100) && ah_in == 0x3F)   /* the byte a read brings */
                mem_write8(c, phys(c->seg[S_DS], dx_in), (uint8_t)g_dos_script[s->nread - 1].put);
        }
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
    if (vec == 0x33) {              /* the mouse driver: reset answers FFFFh or 0; function 3 gives buttons 0-7, column, row */
        s->nread++;
        const uint64_t v = mix(0x5000000ull | vec, s->nread);
        if (c->r[R_AX] == 3) { c->r[R_BX] = (uint16_t)(v & 7); c->r[R_CX] = (uint16_t)(v >> 8); c->r[R_DX] = (uint16_t)(v >> 24); }
        else if (c->r[R_AX] == 0) c->r[R_AX] = (v & 0x100) ? 0xFFFF : 0;
        return 1;
    }
    if (vec == 0x10 && g_native_int10) { s->nread++; return 1; }   /* the BIOS video calls leave the registers alone */
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
/* Only the authored zero-count binding case exceeds the usual guard. */
static unsigned g_step_cap = 100000;
/* Authored cases also run at s % 4 == 1, where mid-run stops are probed. */
static int g_fixture_args;

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
    for (int a = 0; a < 4 && ((s & 2) || g_fixture_args); a++)
        seg_write16(c, c->seg[S_SS], (uint16_t)(c->r[R_SP] + args + 2 * a), small[a]);
    c->stop_at = c->icount + g_step_cap;
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
        ((o->seg == 0 && (o->ip == 0x2F9D || o->ip == 0x683E || o->ip == 0x5852 || o->ip == 0x3ADA || o->ip == 0x1EB0 || o->ip == 0xBCEF || o->ip == 0x983E || o->ip == 0x79BE || o->ip == 0x8A90 || o->ip == 0x9515 || o->ip == 0x1E79 || o->ip == 0xB171 || o->ip == 0xD6DD || o->ip == 0x4777 ||
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
    /* Sky low-detail entry: the line builder it calls (0FB2:0012) returns at once. */
    { "VGAME.EXE", 0x0000, 0x0FB32, 0xC3 },
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
    /* Projectile caller: impact, damage, scene look-ups, objectives,
     * drawing, sounds and strings return at once; distance, angle,
     * clamp and shift helpers run. */
    { "VGAME.EXE", 0x683E, 0x04ABA, 0xC3 },
    { "VGAME.EXE", 0x683E, 0x07243, 0xC3 },
    { "VGAME.EXE", 0x683E, 0x072D8, 0xC3 },
    { "VGAME.EXE", 0x683E, 0x073C8, 0xC3 },
    { "VGAME.EXE", 0x683E, 0x07594, 0xC3 },
    { "VGAME.EXE", 0x683E, 0x0761A, 0xC3 },
    { "VGAME.EXE", 0x683E, 0x078FD, 0xC3 },
    { "VGAME.EXE", 0x683E, 0x08342, 0xC3 },
    { "VGAME.EXE", 0x683E, 0x08625, 0xC3 },
    { "VGAME.EXE", 0x683E, 0x0889B, 0xC3 },
    { "VGAME.EXE", 0x683E, 0x08A4F, 0xC3 },
    { "VGAME.EXE", 0x683E, 0x08A67, 0xC3 },
    { "VGAME.EXE", 0x683E, 0x0B850, 0xC3 },
    { "VGAME.EXE", 0x683E, 0x0D3F9, 0xC3 },
    { "VGAME.EXE", 0x683E, 0x0EB10, 0xC3 },
    { "VGAME.EXE", 0x683E, 0x0EB50, 0xC3 },
    /* View compose: the 3D scene, panel redraws, text and the driver's
     * far routines return at once; the clamp, abs, copy, shift, matrix, trig
     * and bearing helpers run. */
    { "VGAME.EXE", 0x2F9D, 0x003CE, 0xC3 },
    { "VGAME.EXE", 0x2F9D, 0x001C1, 0xC3 },
    { "VGAME.EXE", 0x2F9D, 0x03A3B, 0xC3 },
    { "VGAME.EXE", 0x2F9D, 0x03A7D, 0xC3 },
    { "VGAME.EXE", 0x2F9D, 0x08462, 0xC3 },
    { "VGAME.EXE", 0x2F9D, 0x0895E, 0xC3 },
    { "VGAME.EXE", 0x2F9D, 0x089F7, 0xC3 },
    { "VGAME.EXE", 0x2F9D, 0x0D52E, 0xC3 },
    { "VGAME.EXE", 0x2F9D, 0x0EB82, 0xC3 },
    { "VGAME.EXE", 0x2F9D, 0x0EBBA, 0xC3 },
    { "VGAME.EXE", 0x2F9D, 0x11720, 0xCB },       /* 114A:0280 */
    { "VGAME.EXE", 0x2F9D, 0x11799, 0xCB },       /* 114A:02FF */
    { "VGAME.EXE", 0x2F9D, 0x1189D, 0xCB },       /* 114A:03FD */
    { "VGAME.EXE", 0x2F9D, 0x0FD06, 0xCB },       /* 0FB2:01E6 */
    { "VGAME.EXE", 0x2F9D, 0x1042D, 0xCB },       /* 1039:009D */
    { "VGAME.EXE", 0x2F9D, 0x10447, 0xCB },       /* 1039:00B7 */
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
    /* These are jump-table handlers inside a live formatter frame, not
     * callable functions. A returning scan thunk isolates the eight-instruction
     * reset; real routes separately prove the untouched scan and its callers. */
    if (o->seg == 0 && !strcmp(o->module, "START.EXE") && o->ip == 0x9D67) pristine[0x9D19] = 0xC3;
    if (o->seg == 0 && !strcmp(o->module, "END.EXE") && o->ip == 0x56D9) pristine[0x568B] = 0xC3;
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
    { "VGAME.EXE", 0xD578, 0xD578, 0x00EF },
    { "VGAME.EXE", 0xD8AF, 0xD8AF, 0x00F3 },
    { "END.EXE", 0x3D4F, 0x3D4F, 0x00F6 },
    { "END.EXE", 0x04D2, 0x04D2, 0x00F8 },
    { "SETUP.EXE", 0x0A9F, 0x0A9F, 0x0103 },
    { "PLAYER.EXE", 0x03FE, 0x03FE, 0x0121 },
    { "PLAYER.EXE", 0x0BE2, 0x0BE2, 0x012A },
    { "END.EXE", 0x1607, 0x1607, 0x0101 },
    { "END.EXE", 0x02A4, 0x02A4, 0x0135 },
    { "VGAME.EXE", 0x03CE, 0x03CE, 0x00F9 },
    { "VGAME.EXE", 0x8462, 0x8462, 0x0113 },
    { "VGAME.EXE", 0x0CE9, 0x0CE9, 0x0196 },
    { "VGAME.EXE", 0x0B05, 0x0B05, 0x0133 },
    { "START.EXE", 0x30A0, 0x30A0, 0x010D },
    { "START.EXE", 0x1308, 0x1308, 0x0115 },
    { "START.EXE", 0x3F33, 0x3F33, 0x012C },
    { "START.EXE", 0x0448, 0x0448, 0x0133 },
    { "VGAME.EXE", 0x0330, 0x0330, 0x018C },
    { "VGAME.EXE", 0xE586, 0xE586, 0x0137 },
    { "PLAYER.EXE", 0x005C, 0x005C, 0x013E },
    { "DSWAP.EXE", 0x0010, 0x0010, 0x0209 },
    { "DSWAP.EXE", 0x117E, 0x117E, 0x018E },
    { "DSWAP.EXE", 0x1D66, 0x1D66, 0x01AB },
    { "MPS_LOGO.EXE", 0x08CC, 0x08CC, 0x0192 },
    { "MPS_LOGO.EXE", 0x0072, 0x0072, 0x02C8 },
    { "MPS_LOGO.EXE", 0x1894, 0x1894, 0x01AB },
    { "SETUP.EXE", 0x19B6, 0x19B6, 0x018E },
    { "VGAME.EXE", 0x033F, 0x033F, 0x01D8 },
    { "PLAYER.EXE", 0x1602, 0x1602, 0x018E },
    { "VGAME.EXE", 0xF458, 0xF458, 0x00E9 },
    { "START.EXE", 0x40F0, 0x40F0, 0x0149 },
    { "END.EXE", 0x2092, 0x2092, 0x016E },
    { "END.EXE", 0x0714, 0x0714, 0x016C },
    { "START.EXE", 0x74B1, 0x74B1, 0x0160 },
    { "START.EXE", 0x64E5, 0x64E5, 0x037C },
    { "VGAME.EXE", 0xBB0D, 0xBB0D, 0x0152 },
    { "START.EXE", 0x008D, 0x008D, 0x0182 },
    { "VGAME.EXE", 0xF5F2, 0xF5F2, 0x01AB },
    { "START.EXE", 0x0963, 0x0963, 0x019E },
    { "VGAME.EXE", 0x09A0, 0x09A0, 0x0165 },
    { "PLAYER.EXE", 0x0244, 0x0244, 0x01BA },
    { "START.EXE", 0x623E, 0x623E, 0x01BD },
    { "VGAME.EXE", 0x17B4, 0x17B4, 0x01C0 },
    { "VGAME.EXE", 0x0D14, 0x0D14, 0x0229 },
    { "START.EXE", 0x6861, 0x6861, 0x021B },
    { "VGAME.EXE", 0x528D, 0x528D, 0x02F5 },
    { "VGAME.EXE", 0x63F2, 0x63F2, 0x0306 },
    { "START.EXE", 0x0FA3, 0x0FA3, 0x0238 },
    { "VGAME.EXE", 0x04E6, 0x04E6, 0x032A },
    { "VGAME.EXE", 0xDC46, 0xDC46, 0x0363 },
    { "VGAME.EXE", 0x1450, 0x1450, 0x0364 },
    { "START.EXE", 0x7904, 0x7904, 0x0301 },

    { "START.EXE", 0x39F7, 0x39F7, 0x02F3 },
    { "END.EXE", 0x1CF5, 0x1CF5, 0x02F0 },

    { "END.EXE", 0x2BEC, 0x2BEC, 0x0435 },
    { "START.EXE", 0x6DB8, 0x6DB8, 0x03F5 },
    { "START.EXE", 0x149C, 0x149C, 0x039E },
    { "MPS_LOGO.EXE", 0x0112, 0x0112, 0x048F },
    { "SETUP.EXE", 0x0010, 0x0010, 0x04F3 },
    { "START.EXE", 0x4239, 0x4239, 0x0EF5 },
    { "VGAME.EXE", 0x2F9D, 0x2F9D, 0x0A23 }, /* view compose through RET */
    { "VGAME.EXE", 0x683E, 0x683E, 0x0A05 }, /* projectiles through RET */
    { "VGAME.EXE", 0x5852, 0x5852, 0x0BA0 }, /* aircraft traffic through RET */
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
    { "START.EXE", 0x9D67, 0x9D19, 0x0062 }, /* reset and its returning scan thunk */
    { "END.EXE", 0x56D9, 0x568B, 0x0062 },
    { "START.EXE", 0x94FA, 0x94FA, 0x1000 },    /* itoa and ltoa: the shared conversion returns at 0x0A4B6 */
    { "START.EXE", 0x9516, 0x9516, 0x1000 },
    { "START.EXE", 0xA210, 0x98A8, 0x0990 },    /* the C runtime's DOS returns: START 0x098A8-0x098C9 */
    { "START.EXE", 0xA4B8, 0x98A8, 0x0C10 },
    { "START.EXE", 0xA520, 0x98A8, 0x0D00 },
    { "START.EXE", 0xA59A, 0x98A8, 0x0EB0 },    /* the open: the C runtime DOS return START 0x098BD-0x098C9 is its error exit */
    { "MPS_LOGO.EXE", 0x1894, 0x0B3E, 0x0F12 }, /* MPS_LOGO's copy: its error ending at 0B53 below the entry, and the 1A3F mask test */
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
    { "START.EXE", 0xA59A },            /* open */
    { "MPS_LOGO.EXE", 0x1894 },         /* open */
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
    { "VGAME.EXE", 0x120A, 0x0CE9, 0x0024 },     /* model fill stream: the condition tail and RET at 0CC5-0CE8 (shared with model_face 0CA8) */
    { "VGAME.EXE", 0x130D, 0x033F, 0x001E },     /* model edge clip: the sign-extend tail at 0321 (031B, the near-RET accept, is handed to the original) */
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
 * floor fails a 4,000-state run whose fixtures stopped reaching the arms.
 * A bounded handler completes its named region at `boundary`, after `clocks`,
 * rather than returning to the caller. Merely handing everything to original
 * code must not satisfy its coverage floor. */
static const struct { const char *module; uint16_t seg, ip, len; unsigned insns, floor; const char *what; uint16_t boundary; unsigned clocks; } COVERED[] = {
    { "START.EXE", 0, 0x39F7, 0x02F3, 265, 265, "menu input and pointer update" },
    { "END.EXE", 0, 0x1CF5, 0x02F0, 267, 267, "menu input and pointer update" },
    { "VGAME.EXE", 0, 0x1450, 0x0364, 301, 301, "project and draw sky and ground bands" },
    { "START.EXE", 0, 0x7904, 0x0301, 272, 272, "choose a world record by type" },
    { "VGAME.EXE", 0, 0xDC46, 0x0363, 295, 295, "walk and compose the scene objects" },
    { "VGAME.EXE", 0, 0x04E6, 0x032A, 276, 276, "walk the terrain tile rings and compose each tile" },
    { "START.EXE", 0x0000, 0x0FA3, 0x0238, 224, 224, "dispatch the briefing page key events" },
    { "VGAME.EXE", 0x0000, 0x63F2, 0x0306, 276, 276, "raise a traffic slot lock, announce and fire" },
    { "VGAME.EXE", 0x0000, 0x528D, 0x02F5, 259, 259, "ground site engagement: sighting, alert level, fire" },
    { "START.EXE", 0x0000, 0x6861, 0x021B, 240, 240, "draw one pilot's roster row" },
    { "VGAME.EXE", 0x0000, 0x0D14, 0x0229, 190, 190, "nearest scene object around a world position" },
    { "VGAME.EXE", 0x0000, 0x17B4, 0x01C0, 168, 168, "cockpit attitude indicator: rotated gauge corners and panel draw" },
    { "START.EXE", 0x0000, 0x623E, 0x01BD, 182, 182, "roster page event loop: pick, swap and confirm pilots" },
    { "PLAYER.EXE", 0x0000, 0x0244, 0x01BA, 180, 180, "stream a compressed picture file in" },
    { "VGAME.EXE", 0x0000, 0x09A0, 0x0165, 128, 128, "load the flight's pair of table files and their substitutions" },
    { "START.EXE", 0x0000, 0x0963, 0x019E, 177, 177, "the arms (weapons) page event loop" },
    { "START.EXE", 0x0000, 0xA59A, 0x01AB, 163, 163, "open or create a file as the C runtime does" },
    { "VGAME.EXE", 0x0000, 0xF5F2, 0x01AB, 163, 163, "open a file handle" },
    { "START.EXE", 0x0000, 0x008D, 0x0182, 155, 155, "draw the pilot detail card" },
    { "VGAME.EXE", 0x0000, 0xBB0D, 0x0152, 124, 124, "draw a model at a world position relative to the view" },
    { "START.EXE", 0x0000, 0x64E5, 0x037C, 372, 372, "draw one pilot's record sheet" },
    { "START.EXE", 0x0000, 0x74B1, 0x0160, 152, 152, "make the world page driver objects" },
    { "END.EXE", 0x0000, 0x0714, 0x016C, 149, 149, "advance the debrief replay by one event" },
    { "END.EXE", 0x0000, 0x2092, 0x016E, 141, 141, "wait for a prompt answer" },
    { "START.EXE", 0x0000, 0x40F0, 0x0149, 112, 112, "load the theatre, generate the mission, take out random sites" },
    { "VGAME.EXE", 0x0000, 0xF458, 0x00E9, 94, 94, "read from a file handle, text mode translated" },
    { "PLAYER.EXE", 0x0000, 0x1602, 0x018E, 204, 204, "C start-up: DOS version, program name and argument vector" },
    { "VGAME.EXE", 0x130D, 0x033F, 0x01D8, 206, 206, "clip a model edge to the window" },
    { "SETUP.EXE", 0x0000, 0x19B6, 0x018E, 204, 204, "C start-up: DOS version, program name and argument vector" },
    { "MPS_LOGO.EXE", 0x0146, 0x1894, 0x01AB, 163, 163, "open or create a file as the C runtime does" },
    { "MPS_LOGO.EXE", 0x0006, 0x0072, 0x02C8, 248, 248, "play the introductory music and timed sprite animation" },
    { "MPS_LOGO.EXE", 0x0146, 0x08CC, 0x0192, 205, 205, "C start-up: far-returning DOS version, program name and argument vector" },
    { "DSWAP.EXE", 0x0000, 0x1D66, 0x01AB, 163, 163, "open or create a file with runtime modes" },
    { "DSWAP.EXE", 0x0000, 0x117E, 0x018E, 204, 204, "C start-up: DOS version, program name and argument vector" },
    { "DSWAP.EXE", 0x0000, 0x0010, 0x0209, 197, 197, "initialize the loader and retry resource files on drives A and B" },
    { "PLAYER.EXE", 0x0000, 0x005C, 0x013E, 120, 120, "Prepare playback memory, timer services and video mode" },
    { "VGAME.EXE", 0x0000, 0xE586, 0x0137, 119, 119, "allocate a buffer and read a whole file in FFFF-byte chunks" },
    { "VGAME.EXE", 0x0FB2, 0x0330, 0x018C, 149, 149, "clip a raster line and draw its visible part" },
    { "START.EXE", 0x0000, 0x0448, 0x0133, 129, 129, "play the 18-frame launch animation, a key cuts it short" },
    { "START.EXE", 0x0000, 0x3F33, 0x012C, 111, 111, "wait up to n ticks for released buttons, then a button or Enter/Esc/space" },
    { "START.EXE", 0x0000, 0x1308, 0x0115, 138, 138, "show the decoding-orders screen, generate the mission, draw its briefing page" },
    { "START.EXE", 0x0000, 0x30A0, 0x010D, 96, 96, "fade the palette from one level to another over a number of frames" },
    { "VGAME.EXE", 0x0000, 0x0B05, 0x0133, 112, 112, "load the scenery file: ring tile counts, entry counts and seven-byte object entries" },
    { "VGAME.EXE", 0x120A, 0x0CE9, 0x0196, 154, 154, "run a model face's command stream" },
    { "VGAME.EXE", 0x0000, 0x8462, 0x0113, 110, 110, "redraw the moving map: clamp the centre ahead of the aircraft, draw the grid, mark the map objects" },
    { "VGAME.EXE", 0x0000, 0x03CE, 0x00F9, 78, 78, "dispatch the 3-D scene: window into the viewport block, page, pacing wait, palette, flags, scale, frame set-up, terrain walk and the after-walk passes" },
    { "END.EXE", 0x0000, 0x02A4, 0x0135, 149, 149, "load and show the debriefing pages" },
    { "END.EXE", 0x0000, 0x1607, 0x0101, 92, 92, "fade the palette between two brightnesses" },
    { "PLAYER.EXE", 0x0000, 0x0BE2, 0x012A, 139, 139, "Unpack one LZ91 block from the file buffer to a linear address" },
    { "PLAYER.EXE", 0x0000, 0x03FE, 0x0121, 106, 106, "Check the stream buffer has room for the next block, wrapping the tail to its start" },
    { "SETUP.EXE", 0x0000, 0x0A9F, 0x0103, 107, 107, "load an overlay file into a memory block of its own" },
    { "END.EXE", 0x0000, 0x04D2, 0x00F8, 94, 94, "draw the prepared debrief mission route" },
    { "END.EXE", 0x0000, 0x3D4F, 0x00F6, 103, 103, "prepare seven graphics regions for the debrief" },
    { "VGAME.EXE", 0x0000, 0xD8AF, 0x00F3, 85, 85, "configure the raster viewport and projection centre" },
    { "VGAME.EXE", 0x0000, 0xD578, 0x00EF, 77, 77, "light or extinguish a cockpit element" },
    { "END.EXE", 0, 0x2BEC, 0x0435, 444, 444, "debrief narrative and measured layout" },
    { "START.EXE", 0, 0x6DB8, 0x03F5, 399, 399, "pilot options dialog" },
    { "START.EXE", 0, 0x149C, 0x039E, 365, 365, "flight plan display" },
    { "PLAYER.EXE", 0x0000, 0x0B84, 0x001E, 15, 15, "refill the compressed-picture buffer" },
    { "PLAYER.EXE", 0x0000, 0x0BA2, 0x002B, 16, 16, "open a compressed picture and reset input" },
    { "START.EXE", 0x0000, 0x8CE6, 0x0021, 15, 15, "restore the BIOS timer and INT 8 vector" },
    { "END.EXE", 0x0000, 0x4BCA, 0x0021, 15, 15, "restore the BIOS timer and INT 8 vector" },
    { "SETUP.EXE", 0x0000, 0x0FFE, 0x0021, 15, 15, "restore the BIOS timer and INT 8 vector" },
    { "PLAYER.EXE", 0x0000, 0x0F47, 0x0024, 15, 15, "read the mouse buttons" },
    { "END.EXE", 0x0000, 0x44B9, 0x002E, 18, 18, "read the mouse position and buttons" },
    { "START.EXE", 0x0000, 0x8545, 0x002E, 18, 18, "read the mouse position and buttons" },
    { "END.EXE", 0x0000, 0x44E7, 0x0034, 19, 19, "reset and range the mouse" },
    { "START.EXE", 0x0000, 0x8573, 0x0034, 19, 19, "reset and range the mouse" },
    { "SETUP.EXE", 0x0237, 0x0004, 0x0028, 17, 17, "probe for a mouse driver" },
    { "SETUP.EXE", 0x0000, 0x0C84, 0x002D, 20, 20, "find a file and compare its name" },
    { "PLAYER.EXE", 0x026B, 0x004D, 0x0026, 18, 18, "restore the timer for the clock hook" },
    { "MPS_LOGO.EXE", 0x00DA, 0x0491, 0x0033, 26, 26, "rebuild a block of seven-byte records" },
    { "END.EXE", 0x0000, 0x45AD, 0x0059, 38, 38, "rebuild a block of five-byte records" },
    { "SETUP.EXE", 0x0000, 0x14BE, 0x004B, 36, 36, "write a string with the BIOS" },
    { "PLAYER.EXE", 0x0000, 0x05F6, 0x006D, 46, 46, "copy bytes between linear addresses" },
    { "SETUP.EXE", 0x0000, 0x1387, 0x0065, 51, 51, "call a software interrupt with register blocks" },
    { "SETUP.EXE", 0x0000, 0x0BFB, 0x003A, 27, 25, "free a memory block" },
    { "MPS_LOGO.EXE", 0x00DA, 0x04C4, 0x0016, 11, 10, "free a memory block" },
    { "MPS_LOGO.EXE", 0x00DA, 0x043D, 0x0054, 38, 35, "load a file into a segment" },
    { "START.EXE", 0x0000, 0xA8C2, 0x0050, 39, 39, "resize a heap block" },
    { "MPS_LOGO.EXE", 0x006D, 2, 0x0081, 63, 63, "sprite colour shift" },
    { "VGAME.EXE", 0x0FAE, 0x0002, 61, 27, 27, "segment entry binding" },
    { "START.EXE", 0, 0x9D67, 20, 8, 8, "formatter conversion state", 0x9D19, 8 },
    { "END.EXE", 0, 0x56D9, 20, 8, 8, "formatter conversion state", 0x568B, 8 },
    { "MPS_LOGO.EXE", 0x006D, 0x0112, 0x048F, 434, 434, "sprite-set loader" },
    { "SETUP.EXE", 0, 0x0010, 0x04F3, 505, 505, "setup main" },
    { "START.EXE", 0, 0x4239, 0x0EF5, 1332, 1332, "mission generator" },
    { "VGAME.EXE", 0, 0x2F9D, 0x0A23, 938, 938, "view compose" },
    { "VGAME.EXE", 0, 0x683E, 0x0A05, 886, 886, "projectiles" },
    { "VGAME.EXE", 0, 0x5852, 0x0BA0, 998, 990, "aircraft traffic" },
    { "VGAME.EXE", 0, 0x1EB0, 0x0DD7, 1274, 1250, "flight input" },
    { "VGAME.EXE", 0, 0x3ADA, 0x0A30, 848, 820, "frame update" },
    { "VGAME.EXE", 0, 0x5046, 0x0247, 195, 190, "ground defences" },
};
/* Routines whose arms end the program: the named code sets AH = 4Ch and leaves the INT 21h at `end` to
 * the original code, where the machine's DOS ends the program (here the harness answers it and the
 * fixture's code after it returns). A named routine stopped there has completed its named region. */
static const struct { const char *module; uint16_t seg, ip, end; } PROGRAM_ENDS[] = {
    { "SETUP.EXE", 0x0000, 0x0A9F, 0x0BA0 },    /* the overlay loader's error arms: MOV AX,4C00h / INT 21h */
};
static int program_end_reached(const recomp_override *o, const cpu_t *c, uint16_t cs)
{
    for (unsigned k = 0; k < sizeof PROGRAM_ENDS / sizeof PROGRAM_ENDS[0]; k++)
        if (!strcmp(PROGRAM_ENDS[k].module, o->module) && PROGRAM_ENDS[k].seg == o->seg && PROGRAM_ENDS[k].ip == o->ip)
            return c->seg[S_CS] == cs && c->ip == PROGRAM_ENDS[k].end && get_r8(c, R_AH) == 0x4C;
    return 0;
}
static int g_flight_fixture, g_frame_fixture, g_traffic_fixture, g_projectile_fixture, g_view_fixture, g_mission_fixture, g_state_index;   /* flight-input fixtures; the state being compared */
static int g_cov = -1;                  /* the COVERED entry under test, or -1 */
static uint16_t g_cov_ip;
static unsigned long long g_cov_escapes;
static unsigned long long g_cov_skips[6];   /* fault, long run, bad return, code written, declined, unfinished */
static uint8_t g_cov_seen[0x1000], g_cov_cover[0x1000], g_cov_whole[0x1000];
/* A fuzzed write can turn a callee's RET into RET n. The original then
 * returns to its caller at a different stack depth, so the normal trap
 * cannot catch it. Keep exact original return states: an earlier iteration
 * reaching the same IP at the normal depth is not evidence of an escape.
 * A full record falls back to the conservative escaped-call check. */
static struct { uint64_t clock; uint16_t from_cs, from_ip, r[8], seg[4], flags, ip; } g_changed_ret[128];
static unsigned g_changed_ret_n;
static int g_callee_finished;

static int changed_return(const cpu_t *c, uint16_t cs, uint16_t ip)
{
    const uint8_t op = c->mem[phys(cs, ip)];
    if (op != 0xC3 && op != 0xC2 && op != 0xCB && op != 0xCA) return 0;
    const unsigned len = op == 0xC2 || op == 0xCA ? 3 : 1;
    for (unsigned k = 0; k < len; k++) {
        const uint32_t at = phys(cs, (uint16_t)(ip + k));
        if (c->mem[at] != g_pristine[at]) return 1;
    }
    return 0;
}

static int same_changed_return(const cpu_t *c, uint16_t cs, uint16_t ip)
{
    for (unsigned k = 0; k < g_changed_ret_n; k++)
        if (g_changed_ret[k].clock == c->icount && g_changed_ret[k].from_cs == cs &&
            g_changed_ret[k].from_ip == ip && g_changed_ret[k].ip == c->ip &&
            g_changed_ret[k].flags == c->flags &&
            !memcmp(g_changed_ret[k].r, c->r, sizeof c->r) &&
            !memcmp(g_changed_ret[k].seg, c->seg, sizeof c->seg)) return 1;
    return 0;
}
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
static const uint16_t PROJECTILE_DIVIDES[] = { 0x6B0C, 0x6BD0, 0x6BFD, 0x6C32, 0x6C67, 0x6C94, 0x6DCC, 0x6EF5 };
static const uint16_t TRAFFIC_DIVIDES[] = {
    0x5B99, 0x5E0D, 0x5E89, 0x5E9D, 0x5F67, 0x5FF0, 0x6179, 0x6280
};
static uint64_t g_hud_seen;
static uint64_t g_cockpit_seen;
static unsigned g_nav_seen;
static unsigned g_panel_seen;           /* original branches in the current panel fixture */
static unsigned g_arms_seen;            /* original arming setup branches in this fixture */
static int run_to_ret(cpu_t *a, uint16_t entry_sp, int kind, int *steps, uint16_t cs, uint16_t ip)
{
    const uint32_t watch = phys(cs, (uint16_t)(ip - g_below));
    const int watching = (g_below || g_watch_transient) && a->mem == g_mem[0];
    while ((unsigned)*steps < g_step_cap) {
        const unsigned watch_bytes = (ip == 0x149C || ip == 0x6DB8 || ip == 0x2BEC || ip == 0x0112 || ip == 0x0010 || ip == 0x4239 || ip == 0x2F9D || ip == 0x683E || ip == 0x5852 || ip == 0x3ADA || ip == 0x1EB0 || ip == 0xBCEF || ip == 0x983E || ip == 0x0016 || ip == 0x79BE || ip == 0x8A90) ? g_span : 0x300u + g_below;
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
        const uint16_t from_cs = a->seg[S_CS];
        const int changed = g_cov >= 0 && a->mem == g_mem[0] && !own && changed_return(a, from_cs, here);
        cpu_step(a);
        if (changed && a->seg[S_CS] == cs && (uint16_t)(a->ip - g_from) < g_span &&
            g_changed_ret_n < sizeof g_changed_ret / sizeof g_changed_ret[0]) {
            const unsigned k = g_changed_ret_n++;
            g_changed_ret[k].clock = a->icount;
            g_changed_ret[k].from_cs = from_cs; g_changed_ret[k].from_ip = here;
            g_changed_ret[k].ip = a->ip; g_changed_ret[k].flags = a->flags;
            memcpy(g_changed_ret[k].r, a->r, sizeof a->r);
            memcpy(g_changed_ret[k].seg, a->seg, sizeof a->seg);
        }
        if (a->flags & F_TF) cpu_interrupt(a, 1);
        ++*steps;
        /* A HUD divide fault is a comparable boundary even though it cannot
         * return normally. Stop before interpreting the random vector target. */
        if (g_camera_fixture && own && a->int_depth && here == 0xBE53) return 2;
        if (g_mission_fixture && a->int_depth > depth_before)
            return own && (here==0x499D || here==0x4A7C) ? 2 : 0;
        if (g_ground_fixture && a->int_depth > depth_before)
            return own && here == 0x5122 ? 2 : 0;
        if (g_projectile_fixture && a->int_depth > depth_before) {
            for (unsigned k = 0; own && k < sizeof PROJECTILE_DIVIDES / sizeof PROJECTILE_DIVIDES[0]; k++)
                if (here == PROJECTILE_DIVIDES[k]) return 2;
            return 0;
        }
        if (g_traffic_fixture && a->int_depth > depth_before) {
            for (unsigned k = 0; own && k < sizeof TRAFFIC_DIVIDES / sizeof TRAFFIC_DIVIDES[0]; k++)
                if (here == TRAFFIC_DIVIDES[k]) return 2;
            return 0;   /* do not follow a random callee's fault vector */
        }
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
        /* PLAYER's C start-up argument set-up (1602) pops its return address into [1ABC], lowers the stack for the
         * argument vector and leaves by JMP [1ABC] (at 178C). */
        if (kind == 1 && ip == 0x1602 && here == 0x178C && op == 0xFF) return inside;
        /* The far-model argv builder keeps its allocation and returns by LJMP. */
        if (kind == 2 && ip == 0x08CC && here == 0x0A5A && op == 0xFF) return inside;
        /* DSWAP argv setup returns indirectly with its stack allocation retained. */
        if (kind == 1 && ip == 0x117E && here == 0x1308 && op == 0xFF) return inside;
        if (kind == 1 && ip == 0x19B6 && here == 0x1B40 && op == 0xFF) return inside;   /* SETUP's copy: JMP [0EBA] */
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
    int changed_return_matched = 0, return_seen = 0;
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
            /* VGAME 0FB2:0330's callees (arithmetic, the draw thunk's probe and
             * the divide-error handler) all return normally, so reaching its
             * caller's return inside a nested run is an escape even when the
             * requested trap IP is not an instruction cov_reached knows. */
            if (changed_return_matched) {
                g_callee_finished = 1;
                g_finished++;
            }
            if (!changed_return_matched && g_cov >= 0 && mm->trap_cs == g_back_cs &&
                ((!strcmp(COVERED[g_cov].module, "VGAME.EXE") &&
                  COVERED[g_cov].seg == 0x0FB2 && COVERED[g_cov].ip == 0x0330) ||
                 cov_reached(mm->trap_ip,
                    (uint16_t)(call_sp - ((!strcmp(COVERED[g_cov].module, "MPS_LOGO.EXE") &&
                    COVERED[g_cov].seg == 6 && COVERED[g_cov].ip == 0x72 && mm->trap_ip == 0x007D) ? 0x0318 : 0)), 16))) {
                g_cov_escapes++;
                printf("  ESCAPED CALL (state %d): trap %04X:%04X sp %04X, at %04X:%04X sp %04X flags %04X\n", g_state_index,
                       mm->trap_cs, mm->trap_ip, mm->trap_sp, c->seg[S_CS], c->ip, c->r[R_SP], c->flags);
            }
            return RUN_SLICE;
        }
        if (c->icount >= c->stop_at) { g_stopped = 1; return RUN_SLICE; }
        if (g_trace) printf("      (callee) %04X:%04X clk %llu sp %04X\n", c->seg[S_CS], c->ip, (unsigned long long)c->icount, c->r[R_SP]);
        const uint64_t limit = c->stop_at;
        const uint16_t from_cs = c->seg[S_CS], from_ip = c->ip;
        const uint8_t op = mem_read8(c, phys(from_cs, from_ip));
        const int changed = g_cov >= 0 && changed_return(c, from_cs, from_ip);
        cpu_step(c);
        if ((op == 0xC3 || op == 0xC2 || op == 0xCB || op == 0xCA) &&
            c->seg[S_CS] == mm->trap_cs && c->ip == mm->trap_ip) {
            if (!return_seen && changed && c->r[R_SP] != mm->trap_sp &&
                same_changed_return(c, from_cs, from_ip)) changed_return_matched = 1;
            return_seen = 1;
        }
        /* This runner has a fixed clock limit and no pending PC events. IRET
         * clears stop_at for machine_run to recompute its event window; keep
         * the fixed limit so VGAME 0FB2:0330's divide-error handler returns
         * to the trap instead of letting the original finish the routine. */
        if (op == 0xCF && c->stop_at == 0 && g_cov >= 0 && !strcmp(COVERED[g_cov].module, "VGAME.EXE") &&
            COVERED[g_cov].seg == 0x0FB2 && COVERED[g_cov].ip == 0x0330)
            c->stop_at = limit;
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
        g_traffic_fixture = !strcmp(o->module, "VGAME.EXE") && o->seg == 0 && o->ip == 0x5852;
        g_projectile_fixture = !strcmp(o->module, "VGAME.EXE") && o->seg == 0 && o->ip == 0x683E;
        g_view_fixture = !strcmp(o->module, "VGAME.EXE") && o->seg == 0 && o->ip == 0x2F9D;
        g_mission_fixture = !strcmp(o->module, "START.EXE") && o->seg == 0 && o->ip == 0x4239;
        g_cov = -1;
        for (unsigned k = 0; k < sizeof COVERED / sizeof COVERED[0]; k++)
            if (!strcmp(o->module, COVERED[k].module) && o->seg == COVERED[k].seg && o->ip == COVERED[k].ip) g_cov = (int)k;
        if (g_cov >= 0) { g_cov_ip = (uint16_t)(COVERED[g_cov].ip - ((o->seg == 0x130D && o->ip == 0x033F) || (o->seg == 0x120A && o->ip == 0x0CE9) ? g_below : 0)); g_cov_escapes = 0; memset(g_cov_skips, 0, sizeof g_cov_skips); memset(g_cov_cover, 0, sizeof g_cov_cover); memset(g_cov_whole, 0, sizeof g_cov_whole); }
        g_hud_fixture = !strcmp(o->module, "VGAME.EXE") && o->seg == 0x1058 && o->ip == 0x0016;
        g_ground_fixture = !strcmp(o->module, "VGAME.EXE") && o->seg == 0 && o->ip == 0x5046;
        g_watch_transient = (!strcmp(o->module, "START.EXE") && o->seg == 0 && o->ip == 0x149C) || (!strcmp(o->module, "START.EXE") && o->seg == 0 && o->ip == 0x6DB8) || (!strcmp(o->module, "END.EXE") && o->seg == 0 && o->ip == 0x2BEC) || (!strcmp(o->module, "VGAME.EXE") && o->seg == 0x0FAE && o->ip == 2) ||
            (!strcmp(o->module, "MPS_LOGO.EXE") && o->seg == 0x006D && o->ip == 2) || (!strcmp(o->module, "MPS_LOGO.EXE") && o->seg == 0x006D && o->ip == 0x0112) || (!strcmp(o->module, "VGAME.EXE") && o->seg == 0x1058 && o->ip == 0x0016) ||
            (o->seg == 0 &&
            ((!strcmp(o->module, "VGAME.EXE") && (o->ip == 0x2F9D || o->ip == 0x683E || o->ip == 0x5046 || o->ip == 0x5852 || o->ip == 0x3ADA || o->ip == 0x1EB0 || o->ip == 0xBCEF || o->ip == 0x983E || o->ip == 0x79BE || o->ip == 0x8A90)) ||
             (!strcmp(o->module, "START.EXE") && (o->ip == 0x4239 || o->ip == 0x39B3 || o->ip == 0x39D5)) ||
             (!strcmp(o->module, "END.EXE") && (o->ip == 0x1CB1 || o->ip == 0x1CD3)) ||
             (!strcmp(o->module, "PLAYER.EXE") && o->ip == 0x1E90) ||
             (!strcmp(o->module, "SETUP.EXE") && o->ip == 0x0010)));
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
        unsigned projectile_fault_mask = 0;
        unsigned long long projectile_faults = 0;
        unsigned mission_fault_mask = 0; unsigned long long mission_faults = 0;
        unsigned traffic_fault_mask = 0;
        unsigned long long traffic_faults = 0;
        uint64_t hud_branches = 0;
        uint64_t cockpit_branches = 0;
        unsigned nav_branches = 0;
        unsigned panel_branches = 0;
        const int armament_screen = !strcmp(o->module, "START.EXE") && o->seg == 0 && o->ip == 0x07EE;
        unsigned arms_branches = 0;
        unsigned long long arms_returning = 0, arms_actual = 0;
        g_dos = 0; g_dos_script_n = 0; memset(g_dos_script, 0, sizeof g_dos_script);
        for (unsigned k = 0; k < sizeof DOS_STUBS / sizeof DOS_STUBS[0]; k++)
            if (!strcmp(DOS_STUBS[k].module, o->module) && DOS_STUBS[k].ip == o->ip) g_dos = 1;
        g_nend = 0;
        for (unsigned k = 0; k < sizeof SHARED_ENDINGS / sizeof SHARED_ENDINGS[0] && g_nend < 4; k++)
            if (!strcmp(SHARED_ENDINGS[k].module, o->module) && SHARED_ENDINGS[k].seg == o->seg) {
                g_end_lo[g_nend] = SHARED_ENDINGS[k].lo; g_end_hi[g_nend++] = SHARED_ENDINGS[k].hi;
            }
        const int bind_entries = !strcmp(o->module, "VGAME.EXE") && o->seg == 0x0FAE && o->ip == 2;
        const int zero_loops = (!strcmp(o->module, "MPS_LOGO.EXE") && o->seg == 0x00DA && o->ip == 0x0491) ||
                               (!strcmp(o->module, "END.EXE") && o->seg == 0 && o->ip == 0x45AD);
        unsigned bind_modes = 0, bind_zero = 0;
        const int sprite_colour = !strcmp(o->module, "MPS_LOGO.EXE") && o->seg == 0x006D && o->ip == 2;
        unsigned colour_modes = 0;
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
            uint16_t flags = (uint16_t)((rnd() & 0x0ED5u) | 0x0002u);
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
            g_changed_ret_n = 0; g_callee_finished = 0;
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
            /* Aircraft traffic: a few slots ([DEFC] 1-8, 24h-byte records
             * at C16Ah) and field records ([E56C] 3-8) instead of random
             * counts of thousands, so the loops finish; the records' own
             * words stay random. */
            if (g_traffic_fixture && (s & 7) != 0) {
                const unsigned v = (unsigned)s >> 3;
                const uint32_t h = (uint32_t)v * 0x9E3779B1u;
                struct { uint16_t at, value; } words[160]; unsigned n = 0;
#define TRAFFIC_WORD(a_, v_) do { words[n].at = (uint16_t)(a_); words[n++].value = (uint16_t)(v_); } while (0)
                const uint16_t slots = (uint16_t)((s & 7) >= 4 ? 6 + v % 3 : 1 + v % 6);
                TRAFFIC_WORD(0xDEFC, slots);
                TRAFFIC_WORD(0x3D9C, (h >> 16) & 1 ? 0xFFFF : (h >> 17) % slots);
                TRAFFIC_WORD(0xC0E0, 1 + (h >> 20) % 4);
                TRAFFIC_WORD(0xE56C, 3 + (h >> 22) % 6);           /* field records searched */
                if ((s & 7) >= 2) {
                    /* A traffic world near the player (B2CC/B786/B79A): each
                     * slot active, airborne or parked, hunting, returning to
                     * a field, in formation or patrolling; fields of type 5
                     * with flag 1 for the patrol's random pick. */
                    static const uint16_t flags[] = { 0x0003, 0x0007, 0x0103, 0x0001, 0x0083, 0x0143, 0x0043, 0x0203 };
                    const uint16_t px = (uint16_t)(0x2000 + 0x20 * (v % 5)), py = (uint16_t)(0x3000 + 0x18 * (v % 7));
                    TRAFFIC_WORD(0xB2CC, px); TRAFFIC_WORD(0xB786, py); TRAFFIC_WORD(0xB79A, 0x200);
                    TRAFFIC_WORD(0xC0D0, px); TRAFFIC_WORD(0xC0DE, py); TRAFFIC_WORD(0x2DF4, 0x200);
                    TRAFFIC_WORD(0x3D90, (h >> 25) & 1 ? 5 : 0); TRAFFIC_WORD(0x9F94, (h >> 26) & 1 ? 0 : 10);
                    TRAFFIC_WORD(0x39E0, (h >> 27) & 1); TRAFFIC_WORD(0x39D8, px + 0x40); TRAFFIC_WORD(0x39DA, py + 0x40);
                    TRAFFIC_WORD(0x3688, 0); TRAFFIC_WORD(0x3D9C, (h >> 28) & 1 ? 0xFFFF : 0);
                    const uint16_t patrol = (uint16_t)(1 + (v / 3) % (slots >= 6 ? slots - 5 : 1));
                    TRAFFIC_WORD(0x9912, (s & 7) == 3 ? ((h >> 29) & 1 ? 0x40 : 0x38) : (uint16_t)(patrol << 4));
                    for (unsigned j = 0; j < slots; j++) {
                        const uint16_t rec = (uint16_t)(0xC16A + 0x24 * j);
                        const uint32_t hj = h ^ (j * 0x85EBCA6Bu);
                        TRAFFIC_WORD(rec + 0x18, flags[(hj >> 8) & 7]);                   /* C182: state */
                        TRAFFIC_WORD(rec + 0x1A, (hj >> 11) & 1 ? 0x100 : 0);             /* C184 */
                        TRAFFIC_WORD(rec + 0x1C, 1 + ((hj >> 12) & 3));                   /* C186: timer */
                        TRAFFIC_WORD(rec + 0x02, px + 0x30 * (j + 1) - 0x60);             /* C16C: X */
                        TRAFFIC_WORD(rec + 0x04, py + 0x28 * (j + 1) - 0x50);             /* C16E: Y */
                        TRAFFIC_WORD(rec + 0x06, 0x180 + 0x40 * (j & 3));                 /* C170: height */
                        TRAFFIC_WORD(rec + 0x16, 5);                                      /* C180: type */
                        TRAFFIC_WORD(rec + 0x00, j % 3);                                  /* C16A: home field */
                        if ((s & 7) == 3 && j == 1 && (hj >> 14) & 1) {                   /* far away */
                            TRAFFIC_WORD(rec + 0x02, px + 0x6000); TRAFFIC_WORD(rec + 0x18, 0x0003);
                        }
                        if ((s & 7) == 2 && j == 0) {                                     /* lined up on the player */
                            /* Cross both shooting-range thresholds (200h/400h). */
                            const uint16_t range = (v & 1) ? 0x280 : 0x100;
                            TRAFFIC_WORD(rec + 0x02, px); TRAFFIC_WORD(rec + 0x04, (hj >> 15) & 1 ? py - range : py + range);
                            TRAFFIC_WORD(rec + 0x06, 0x200); TRAFFIC_WORD(rec + 0x18, 0x0003); TRAFFIC_WORD(rec + 0x1A, 0x100);
                            TRAFFIC_WORD(rec + 0x10, (hj >> 16) & 1 ? 0 : 0x8000); TRAFFIC_WORD(rec + 0x12, 0);
                        }
                    }
                    if ((s & 7) == 2) { TRAFFIC_WORD(0x3D90, 5); TRAFFIC_WORD(0x9F94, 0); TRAFFIC_WORD(0x39E0, 0); TRAFFIC_WORD(0x3D8E, (h >> 17) & 1 ? 0 : 4); }
                    for (unsigned j = 0; j < 8; j++) {
                        TRAFFIC_WORD(0xB2D6 + 16 * j, (h >> (j + 1)) & 1 ? 0x0001 : 0x0201);
                        TRAFFIC_WORD(0xB2D8 + 16 * j, 5);
                    }
                }
#undef TRAFFIC_WORD
                int safe = 1;
                for (unsigned j = 0; j < n; j++) for (unsigned byte = 0; byte < 2; byte++) {
                    const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                    if (address >= at && address < at + m->size + 2) safe = 0;
                }
                if (safe) for (unsigned j = 0; j < n; j++) for (unsigned byte = 0; byte < 2; byte++) {
                    const uint32_t address = phys(seg[S_DS], (uint16_t)(words[j].at + byte));
                    g_pristine[address] = g_mem[0][address] = g_mem[1][address] = (uint8_t)(words[j].value >> (8 * byte));
                }
                if (safe && (s & 7) == 5) for (unsigned j = 0; j < 0x400; j++) {
                    const uint32_t address = phys(seg[S_DS], (uint16_t)(0x3ABA + j));
                    if (address < at || address >= at + m->size + 2)
                        g_pristine[address] = g_mem[0][address] = g_mem[1][address] = (j & 1) ? 0x02 : 0x00;
                }
            }
            if (g_traffic_fixture) {
                /* A returning terrain probe sets the obstacle marker in one
                 * fixture class; all other classes execute the real probe. */
                static const uint8_t hit[] = { 0xC6, 0x06, 0xB2, 0xC6, 0x01, 0xC3 };
                for (unsigned j = 0; j < sizeof hit; j++) {
                    const uint32_t address = at + 0x78FD + j;
                    g_pristine[address] = g_mem[0][address] = g_mem[1][address] = (s & 7) == 6 ? hit[j] : m->image[0x78FD + j];
                }
            }
            /* Projectiles: twelve 1Ch-byte records at 3C3Ah (X, Y, height,
             * speed, heading, pitch, roll, life, type, warhead, locked
             * target, owner, aim point); slots 0-7 are incoming weapons,
             * 8-11 the player's own. Types 1Ch-23h with the
             * weapon table's guidance kind (+0Ch of 12h-byte records at
             * 3394h) spread over -1..7; a few traffic slots and fields. */
            if (g_projectile_fixture && (s & 7) != 0) {
                const unsigned v = (unsigned)s >> 3;
                const uint32_t h = (uint32_t)v * 0x9E3779B1u;
                /* At most 244 words, including all twelve weapon records. */
                struct { uint16_t at, value; } words[256]; unsigned n = 0;
#define SHOT_WORD(a_, v_) do { if (n >= sizeof words / sizeof words[0]) { fprintf(stderr, "projectile fixture capacity exceeded\n"); return 1; } words[n].at = (uint16_t)(a_); words[n++].value = (uint16_t)(v_); } while (0)
                static const uint16_t kinds[] = { 0xFFFF, 0, 1, 2, 3, 4, 5, 6, 7 };
                const uint16_t px = (uint16_t)(0x2000 + 0x10 * (v % 7)), py = (uint16_t)(0x3000 + 0x10 * (v % 5));
                const uint16_t slots = (uint16_t)(1 + v % 5);
                SHOT_WORD(0xDEFC, slots); SHOT_WORD(0xC0D0, px); SHOT_WORD(0xC0DE, py); SHOT_WORD(0x2DF4, 0x200);
                SHOT_WORD(0x39E0, (h >> 16) & 1); SHOT_WORD(0x39D8, px + 0x20); SHOT_WORD(0x39DA, py + 0x20);
                SHOT_WORD(0x3D94, (h >> 17) & 1 ? 0xFFFF : 1); SHOT_WORD(0x3664, (h >> 18) & 1);
                SHOT_WORD(0x3D90, (h >> 19) & 1 ? 5 : 0); SHOT_WORD(0x9F94, 2); SHOT_WORD(0x9B34, (h >> 20) & 3 ? 0 : 0x30);
                /* A live radar decoy makes guidance-kind 3 test its aspect guard. */
                SHOT_WORD(0x39E4, px + 0x30); SHOT_WORD(0x39E6, py - 0x20);
                SHOT_WORD(0x39EA, 2); SHOT_WORD(0x39EC, 5);
                SHOT_WORD(0x9912, 1); SHOT_WORD(0xDEB4, (h >> 21) & 1 ? 5 : 0); SHOT_WORD(0x3686, (h >> 22) & 3);
                for (unsigned t = 0; t < 8; t++) {
                    const uint16_t rec = (uint16_t)(0x3394 + 0x12 * (0x1C + t));
                    SHOT_WORD(rec + 0x0A, 0x800 + 0x100 * t);                     /* 339Eh: top speed */
                    SHOT_WORD(rec + 0x0C, kinds[(v + t) % 9]);                    /* 33A0h: guidance */
                    SHOT_WORD(rec + 0x0E, 0x10 + t);                              /* 33A2h: turn */
                }
                for (unsigned k = 0; k < 12; k++) {
                    const uint16_t rec = (uint16_t)(0x3C3A + 0x1C * k);
                    const uint32_t hk = h ^ (k * 0x85EBCA6Bu);
                    SHOT_WORD(rec + 0x00, px + 0x20 * k - 0x80); SHOT_WORD(rec + 0x02, py + 0x18 * k - 0x60);
                    SHOT_WORD(rec + 0x04, (hk >> 8) & 3 ? 0x100 + 0x20 * k : 0xFFC0);    /* height, some below ground */
                    SHOT_WORD(rec + 0x06, 0x10 + (hk >> 10) % 0x30);                    /* 3C40h speed */
                    SHOT_WORD(rec + 0x08, (uint16_t)(hk >> 4)); SHOT_WORD(rec + 0x0A, (hk >> 12) & 1 ? 0xF000 : 0x0400);
                    SHOT_WORD(rec + 0x0E, (hk >> 13) % 4 ? 1 + (hk >> 15) % 40 : 0);   /* 3C48h life, some empty */
                    SHOT_WORD(rec + 0x10, (hk >> 14) % 5 == 0 ? 0x26 : 0x1C + (k + v) % 8);   /* 3C4Ah type */
                    SHOT_WORD(rec + 0x12, (hk >> 17) % 3);                              /* 3C4Ch warhead */
                    SHOT_WORD(rec + 0x14, (hk >> 19) % 3 == 0 ? 0xFFFF : (hk >> 20) & 1);   /* 3C4Eh locked target */
                    SHOT_WORD(rec + 0x16, (hk >> 21) % 4 == 0 ? 0 : (hk >> 22) & 1 ? 3 : 0xFFFF);   /* 3C50h owner */
                    SHOT_WORD(rec + 0x18, px + ((hk >> 23) & 1 ? 0x10 : 0)); SHOT_WORD(rec + 0x1A, py);   /* 3C52h aim */
                }
                for (unsigned j = 0; j < slots; j++) {
                    const uint16_t rec = (uint16_t)(0xC16A + 0x24 * j);
                    SHOT_WORD(rec + 0x18, (h >> (j + 3)) & 1 ? 0x0003 : 0x000B); SHOT_WORD(rec + 0x1A, 0x100);
                    SHOT_WORD(rec + 0x02, px + 0x10 * j); SHOT_WORD(rec + 0x04, py - 0x10 * j); SHOT_WORD(rec + 0x06, 0x180);
                }
                for (unsigned j = 0; j < 4; j++) {
                    SHOT_WORD(0xB2D0 + 16 * j, px + 0x30 * j); SHOT_WORD(0xB2D2 + 16 * j, py);
                    SHOT_WORD(0xB2D6 + 16 * j, (h >> (j + 8)) & 1 ? 0x0010 : 0x0090);
                    SHOT_WORD(0xE304 + 18 * (j & 1), 3); SHOT_WORD(0xE306 + 18 * (j & 1), j & 1);
                }
#undef SHOT_WORD
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
            /* View compose: the view mode [C0A6] runs through every value the
             * routine tells apart (cockpit, the three side looks, the
             * fixed-point, behind, ahead and tracked-object looks, and modes
             * it leaves to the previous frame's angles), with the previous
             * mode and cockpit flag differing from it in both directions,
             * the camera pitch around the vertical, low and normal altitude,
             * the tracked-object id in every record kind (world slot,
             * ground target, airfield) and the caption, countdown and
             * panel words each on and off. */
            if (g_view_fixture && (s & 7) != 0) {
                const unsigned v = (unsigned)s >> 3;
                uint64_t z = (uint64_t)v * 0x9E3779B97F4A7C15ull;
                z ^= z >> 30; z *= 0xBF58476D1CE4E5B9ull; z ^= z >> 27; z *= 0x94D049BB133111EBull; z ^= z >> 31;
#define VB(shift_, bits_) ((unsigned)((z >> (shift_)) & ((1u << (bits_)) - 1u)))
                static const uint16_t modes[] = { 0, 0x41, 0x42, 0x43, 0x44, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C,
                                                  0x8D, 0x40, 0x50, 0x20, 0x84, 0x88, 0x89, 0x8B };
                static const uint16_t pitches[] = { 0x0200, 0x5000, 0x8000, 0xB000, 0x3FFF, 0xFC00 };
                static const uint16_t objects[] = { 3, 0x83, 0x103, 0x183, 7, 0x85, 0x105 };
                const unsigned nmodes = sizeof modes / sizeof modes[0];
                const uint16_t mode = modes[v % nmodes];
                struct { uint16_t at, value; } words[256]; unsigned n = 0;
#define VIEW_WORD(a_, v_) do { if (n >= sizeof words / sizeof words[0]) { fprintf(stderr, "view fixture capacity exceeded\n"); return 1; } words[n].at = (uint16_t)(a_); words[n++].value = (uint16_t)(v_); } while (0)
                VIEW_WORD(0xC0A6, mode);
                VIEW_WORD(0x2E7A, VB(0, 2) == 0 ? mode : modes[VB(2, 5) % nmodes]);
                VIEW_WORD(0x368C, VB(8, 1));
                VIEW_WORD(0x2DEE, v * 0x1237 + VB(60, 3)); VIEW_WORD(0x2DF0, pitches[VB(9, 3) % 6]); VIEW_WORD(0x2DF2, v * 0x0555 + 0x0100);
                VIEW_WORD(0x2DF4, VB(12, 2) == 0 ? 0xFFF0 : VB(12, 2) == 1 ? 0 : 0x0300);
                VIEW_WORD(0x3D9E, ((const uint16_t[]){ 0, 1, 2, 3, 5, 9, 0xFFFF, 4 })[VB(14, 3)]);
                VIEW_WORD(0xDF06, VB(17, 1) ? 0 : 5); VIEW_WORD(0xE008, VB(18, 2) == 0 ? 0x13 : 0);
                VIEW_WORD(0xBA3A, VB(20, 1)); VIEW_WORD(0x2E08, VB(21, 1)); VIEW_WORD(0x2E0C, VB(22, 1) ? 0 : 2);
                VIEW_WORD(0x43E4, VB(23, 1)); VIEW_WORD(0x2DF6, ((const uint16_t[]){ 50, 0x64, 0xC8 })[VB(24, 2) % 3]);
                VIEW_WORD(0x40CC, VB(26, 1)); VIEW_WORD(0x9D3C, VB(27, 1)); VIEW_WORD(0xE32A, VB(28, 1) ? 1 : 2);
                VIEW_WORD(0x3D92, ((const uint16_t[]){ 0x80, 0x10, 0x90, 0 })[VB(29, 2)]);
                VIEW_WORD(0x3D94, ((const uint16_t[]){ 0x80, 0x20, 0xA0, 1 })[VB(31, 2)]);
                VIEW_WORD(0xDED4, objects[VB(33, 3) % 7]); VIEW_WORD(0xB07C, objects[VB(36, 3) % 7]);
                for (unsigned k = 0; k < 12; k++) {         /* weapon records: life [+0Eh] and height [+04h] */
                    const uint16_t rec = (uint16_t)(0x3C3A + 0x1C * k);
                    const uint64_t zk = (z >> (k % 32)) * (2 * k + 1);
                    VIEW_WORD(rec + 0x0E, (zk >> 3) & 1 ? 5 : 0); VIEW_WORD(rec + 0x04, (zk >> 5) & 1 ? 0x300 : 0x40);
                }
#undef VIEW_WORD
#undef VB
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
            /* Returning stand-ins: one label fixture deliberately retypes
             * the live record (26h, by SI) to reach the supply arm; this is
             * staged coverage, not normal renderer behaviour. Other classes
             * answer "no field" from B850 or effectiveness 10 from 8A67. */
            if (g_projectile_fixture) {
                static const struct { uint32_t at; uint8_t code[7]; unsigned len; int cls; } answer[] = {
                    { 0x08342, { 0xC7, 0x84, 0x4A, 0x3C, 0x26, 0x00, 0xC3 }, 7, 3 },
                    { 0x0B850, { 0xB8, 0xFF, 0xFF, 0xC3 }, 4, 4 },
                    { 0x08A67, { 0xB8, 0x0A, 0x00, 0xC3 }, 4, 5 } };
                for (unsigned k = 0; k < sizeof answer / sizeof answer[0]; k++)
                    for (unsigned j = 0; j < answer[k].len; j++) {
                        const uint32_t address = at + answer[k].at + j;
                        g_pristine[address] = g_mem[0][address] = g_mem[1][address] =
                            (s & 7) == answer[k].cls ? answer[k].code[j] : j == 0 ? 0xC3 : m->image[answer[k].at + j];
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
            /* Mission-generator fixtures: coherent synthetic targets, settings,
             * objective tables and returning search/math/weapon helpers.
             * One eighth remain unconstrained random controls. rand(), clamps,
             * shifts, bearing and formatting are real. Normal routes retain
             * every actual helper and world file.
             *
             * Modes 1/2 reject 101/one nearby searches before succeeding;
             * 3 fixes a temporarily unsuitable primary; 4 rejects short routes;
             * 5 has no matching mission entry on the first attempt; 6 rejects
             * a pair with both bit-3 codes; 7 changes secondary timing at the
             * accepted primary-time call (a deliberately staged, otherwise
             * unreachable arm); 8 rejects one no-contact scan; 9 uses quality
             * zero; 10 reaches long-flight options; 11 reaches upper clamps.
             * Mode 0 includes both zero-divisor boundaries. */
            if (!strcmp(o->module, "START.EXE") && o->ip == 0x4239 && (s & 7)) {
                seg[S_DS] = seg[S_SS] = 0x0A95; /* small-model near pointers include frame copies */
                flags = (uint16_t)(flags & (0xFFFFu ^ F_DF)); /* C caller contract; random controls retain both directions. */
                r[R_SP] = 0xFF00;
#define MW8(a_, v_) do { const uint32_t ma_ = phys(seg[S_DS], (uint16_t)(a_)); \
    g_pristine[ma_] = g_mem[0][ma_] = g_mem[1][ma_] = (uint8_t)((v_)&0xFFu); } while (0)
#define MW16(a_, v_) do { MW8(a_, (uint16_t)(v_)); MW8((a_)+1, (uint16_t)(v_)>>8); } while (0)
                const unsigned mv=(unsigned)s>>3, mf=(unsigned)s&7;
                const unsigned mt=mf==3 || mf==4 ? 0 : 1+mv/8%8;
                const unsigned mm=mf==2?1:mf==3?2:mf==4?3:0;
                const unsigned reg=mt==0?32:8, max=mt==0?40:12;
                const unsigned mode=(mv/8)%12;
                for(unsigned j=0;j<36;j++)MW8(0xE05E + j,0);
                MW16(0xE0A0,mode); MW16(0xE0A2,0); MW16(0xE0A4,0); MW16(0xE0A6,0); MW16(0xE0A8,0);
                MW16(0xE0AA, mode==3?3:(mv/32&1)?1:3); MW16(0xE0AE,(mv/32&1)?2:4);
                MW16(0xE0AC, mode==10?0x4800:0x2800);
                MW16(0xCACA, 0); MW16(0xCACC, 0x5000);
                MW16(0xE096, 0); MW16(0xE098, 0x5200);
                MW16(0xCACE, max); MW16(0xD33C, max); MW16(0xE084, reg);
                MW16(0xCABE, mode==9?0:1+mv%3); MW16(0xAE8C, 117+s); MW16(0xAE8E, 0);
                MW16(0xD890, 6); MW16(0xB38E, mt);
                for(unsigned j=0;j<2;j++) {
                    const uint16_t rec=(uint16_t)(0xD3BE + 36*j);
                    for(unsigned b=0;b<36;b++)MW8(rec+b,0);
                    MW16(rec+2, 0x3000 + 0x100*j); MW16(rec+4,0x4000);
                    MW16(rec+0x16, 2); MW16(rec+0x1A, j==0 && mode==0?0:0x40);
                    MW16(rec+0x18, ((mv&4)||mode==10?0:0x80)|((mv&1)?0x40:0)|((mv&2)?0x100:0));
                }
                for (unsigned j=0;j<256;j++) MW8(0xCAD8 + j, (mv&1)?0x10:0);
                for (unsigned j=0;j<128;j++) MW8(0xD2A6 + j, j==0?2:0);
                for (unsigned j=0;j<82*12;j++) MW8(0x11DA + j, 0);
                for (unsigned j=0;j<2;j++) {
                    MW16(0x11DA + 12*j, 0x1FF); MW16(0x11DC + 12*j, 7);
                    MW16(0x11DE + 12*j, j?1:1+mv%((mm&1)?4:8)); MW16(0x11E0 + 12*j, 2);
                    MW16(0x11E2 + 12*j, mode==6?8:(!j && (mode==7 || mode==10))?2:!j && mode==11?0x10:0);
                    MW16(0x11E4 + 12*j, j?0:(mv/8%3==0?0:mv/8%3==1?1:0xFFFE));
                }
                for(unsigned j=0;j<2;j++) {
                    const unsigned ent=j?55:5;
                    MW16(0x11DA + 12*ent, 1); MW16(0x11DC + 12*ent, 7);
                    MW16(0x11DE + 12*ent, j?5:2); MW16(0x11E0 + 12*ent, 2); MW16(0x11E2 + 12*ent, 2);
                }
                MW16(0x11DA + 24,0x1FF); /* eligible theatre, impossible tension: scan rejection */
                for (unsigned j=0;j<max;j++) {
                    const uint16_t rec=(uint16_t)(0xCBDE + 16*j);
                    for(unsigned b=0;b<16;b++)MW8(rec+b,0);
                    MW16(rec, mt==7 && j==reg?0x11:j); MW16(rec+2, mode==11?0x7000 + 16*j:0x2400 + 0x100*j);
                    MW16(rec+4, mode==11?0x7000 + 16*j:0x4000 + 0x80*j); MW16(rec+6, j%4==0?0:j%4==1?1:j%4==2?0x15:2);
                    MW16(rec+8, (j<reg?((mm&1) && j<=4?0x801:1):j==reg+1?1:j==reg+2?0x301:0x101)|((mv&2)?8:0));
                }
                for(unsigned j=0;j<0x100;j++) {
                    const uint32_t ma_=0x50000 + j;
                    g_pristine[ma_]=g_mem[0][ma_]=g_mem[1][ma_]=0;
                }
                g_pristine[0x50038]=g_mem[0][0x50038]=g_mem[1][0x50038]=(uint8_t)mt;
                g_pristine[0x5003A]=g_mem[0][0x5003A]=g_mem[1][0x5003A]=(uint8_t)(mv/8%3);
                g_pristine[0x50042]=g_mem[0][0x50042]=g_mem[1][0x50042]=(uint8_t)(mv&1);
                g_pristine[0x5003C]=g_mem[0][0x5003C]=g_mem[1][0x5003C]=(uint8_t)mm;
                g_pristine[0x5003E]=g_mem[0][0x5003E]=g_mem[1][0x5003E]=(uint8_t)(mode==9?0:1+mv%3);
                static const uint8_t mission_near_stub[]={
                    0x83,0x3E,0xA0,0xE0,0x01,0x75,0x0F,0xFF,0x06,0xA2,0xE0,0x83,
                    0x3E,0xA2,0xE0,0x65,0x7F,0x78,0xB8,0xFF,0xFF,0xC3,0x83,0x3E,
                    0xA0,0xE0,0x02,0x75,0x0F,0xFF,0x06,0xA2,0xE0,0x83,0x3E,0xA2,
                    0xE0,0x01,0x75,0x62,0xB8,0xFF,0xFF,0xC3,0x83,0x3E,0xA0,0xE0,
                    0x05,0x75,0x3F,0xFF,0x06,0xA2,0xE0,0x83,0x3E,0xA2,0xE0,0x01,
                    0x75,0x1A,0xC7,0x06,0xDA,0x11,0x00,0x00,0xC7,0x06,0xE6,0x11,
                    0x00,0x00,0xC7,0x06,0x16,0x12,0x00,0x00,0xC7,0x06,0x6E,0x14,
                    0x00,0x00,0xEB,0x32,0xC7,0x06,0xDA,0x11,0xFF,0x01,0xC7,0x06,
                    0xE6,0x11,0xFF,0x01,0xC7,0x06,0x16,0x12,0x01,0x00,0xC7,0x06,
                    0x6E,0x14,0x01,0x00,0xEB,0x18,0x83,0x3E,0xA0,0xE0,0x06,0x75,
                    0x11,0xFF,0x06,0xA2,0xE0,0x83,0x3E,0xA2,0xE0,0x01,0x74,0x06,
                    0xC7,0x06,0xEE,0x11,0x00,0x00,0xA1,0xAE,0xE0,0xC3,
                };
                static const uint8_t mission_type_stub[]={
                    0x83,0x3E,0xA0,0xE0,0x03,0x75,0x1F,0xFF,0x06,0xA4,0xE0,0x83,
                    0x3E,0xA4,0xE0,0x01,0x75,0x0A,0xC7,0x06,0x16,0xCC,0x01,0x00,
                    0xB8,0x03,0x00,0xC3,0xC7,0x06,0x16,0xCC,0x01,0x08,0xB8,0x03,
                    0x00,0xC3,0xA1,0xAA,0xE0,0xC3,
                };
                static const uint8_t mission_distance_entry[]={
                    0xE9,0xD3,0x94,
                };
                static const uint8_t mission_distance_stub[]={
                    0x53,0x83,0x3E,0xA0,0xE0,0x04,0x75,0x10,0xFF,0x06,0xA6,0xE0,
                    0x83,0x3E,0xA6,0xE0,0x14,0x7F,0x05,0xB8,0x00,0x00,0xEB,0x42,
                    0x83,0x3E,0xA0,0xE0,0x07,0x75,0x12,0x89,0xE3,0x36,0x81,0x7F,
                    0x02,0x8F,0x48,0x75,0x2E,0xC7,0x06,0x76,0xE0,0x02,0x00,0xEB,
                    0x26,0x83,0x3E,0xA0,0xE0,0x08,0x75,0x1F,0x89,0xE3,0x36,0x81,
                    0x7F,0x02,0xFC,0x48,0x75,0x15,0xFF,0x06,0xA8,0xE0,0xA1,0xA8,
                    0xE0,0x3B,0x06,0x84,0xE0,0x77,0x08,0x8B,0x46,0xC2,0x2D,0xFF,
                    0x7F,0xEB,0x03,0xA1,0xAC,0xE0,0x5B,0xC3,
                };
                static const uint8_t mission_unit_stub[]={
                    0xC3,
                };
                static const uint8_t mission_point_distance_stub[]={
                    0xB8,0x00,0x10,0xC3,
                };
                static const uint8_t mission_weapon_stub[]={
                    0xB8,0x01,0x00,0xC3,
                };
                static const struct { unsigned at; const uint8_t *code; unsigned len; } helpers[]={
                    {0x512e,mission_near_stub,sizeof mission_near_stub},
                    {0x51e8,mission_type_stub,sizeof mission_type_stub},
                    {0x532a,mission_distance_entry,sizeof mission_distance_entry},
                    {0xe800,mission_distance_stub,sizeof mission_distance_stub},
                    {0x5356,mission_unit_stub,sizeof mission_unit_stub},
                    {0x5413,mission_point_distance_stub,sizeof mission_point_distance_stub},
                    {0x5282,mission_weapon_stub,sizeof mission_weapon_stub},
                };
                for(unsigned j=0;j<sizeof helpers/sizeof helpers[0];j++)for(unsigned b=0;b<helpers[j].len;b++)
                    g_pristine[helpers[j].at+b]=g_mem[0][helpers[j].at+b]=g_mem[1][helpers[j].at+b]=helpers[j].code[b];
#undef MW8
#undef MW16
            }

            /* Synthetic setup worlds with returning video, keyboard and joystick
             * stand-ins. The copies remain real. The abort/exit helper returns
             * deliberately, to compare code after otherwise terminating arms.
             * Mode 6 clears the sound-question flag in a helper: staged coverage.
             * At load base zero the BIOS 0000:04F0 pointer aliases this routine's
             * code. Use its existing word as the settings segment without changing
             * code; real routes use the actual BIOS pointer and all actual helpers. */
            if (!strcmp(o->module,"SETUP.EXE") && o->seg==0 && o->ip==0x0010 && (s&7)) {
                seg[S_DS]=seg[S_SS]=0x024C; r[R_SP]=0xFF00;
                flags=(uint16_t)(flags & (0xFFFFu ^ F_DF));
#define SW8(a_,v_) do { const uint32_t sa_=phys(seg[S_DS],(uint16_t)(a_)); \
    g_pristine[sa_]=g_mem[0][sa_]=g_mem[1][sa_]=(uint8_t)(v_); } while(0)
#define SW16(a_,v_) do { SW8(a_,(uint16_t)(v_)); SW8((a_)+1,(uint16_t)(v_)>>8); } while(0)
                const unsigned mode=((unsigned)s>>3)%12;
                SW16(0x2000,mode);SW16(0x2002,0);SW16(0x2004,0);SW16(0x2010,0);
                for(unsigned j=0;j<64;j++)SW8(0x2100+j,0);
                static const uint16_t initial_keys[]={0x100,0,0x59,0x79,0x4E,0x6E,0,0x100,0x40,0x59,0x79,0x4E};
                SW16(0x2008,initial_keys[mode]);SW16(0x200A,mode&1?'N':'Y');
                SW16(0x200C,mode%3==0?0x100:mode%3==1?0x20:0x35);
                SW16(0x200E,0x31+(mode%4));SW16(0x2012,mode&1);SW16(0x2014,mode&1);
                static const uint8_t machine_types[]={'M','m','C','E'};
                SW8(0x05C7,machine_types[mode%4]);SW8(0x05C8,0);
                for(unsigned j=0;j<4;j++) {
                    SW16(0x0778+2*j,0x2200+4*j);
                    SW8(0x2200+4*j,'A'+j);SW8(0x2201+4*j,0);
                }
                const uint16_t settings_seg=(uint16_t)(g_pristine[0x04F0]|((uint16_t)g_pristine[0x04F1]<<8));
                const uint32_t settings_at=phys(settings_seg,0);
                for(unsigned j=0;j<256;j++)g_pristine[settings_at+j]=g_mem[0][settings_at+j]=g_mem[1][settings_at+j]=0;
                static const uint8_t setup_return_near[]={0xC3};
                static const uint8_t setup_return_far[]={0xCB};
                static const uint8_t setup_keyboard_jump[]={
                    0xE9,0x8B,0xDD,
                };
                static const uint8_t setup_keyboard_read[]={
                    0x53,0x89,0xE3,0x36,0x81,0x7F,0x02,0xF0,0x01,0x75,0x15,0xFF,
                    0x06,0x02,0x20,0x83,0x3E,0x02,0x20,0x01,0x75,0x05,0xA1,0x08,
                    0x20,0xEB,0x18,0xA1,0x0A,0x20,0xEB,0x13,0xFF,0x06,0x04,0x20,
                    0x83,0x3E,0x04,0x20,0x01,0x75,0x05,0xA1,0x0C,0x20,0xEB,0x03,
                    0xA1,0x0E,0x20,0x5B,0xCB,
                };
                static const uint8_t setup_button_jump[]={
                    0xE9,0x86,0xDE,
                };
                static const uint8_t setup_button_poll[]={
                    0x53,0x89,0xE3,0x36,0x8B,0x5F,0x02,0x81,0xFB,0x96,0x02,0x75,
                    0x15,0xFF,0x06,0x10,0x20,0x83,0x3E,0x10,0x20,0x01,0x75,0x05,
                    0xB8,0x01,0x00,0xEB,0x1E,0xB8,0x00,0x00,0xEB,0x19,0xD1,0xE3,
                    0x81,0xE3,0x3E,0x00,0xFF,0x87,0x00,0x21,0x83,0xBF,0x00,0x21,
                    0x01,0x75,0x05,0xB8,0x00,0x00,0xEB,0x03,0xB8,0x01,0x00,0x5B,
                    0xCB,
                };
                static const uint8_t setup_detect_jump[]={
                    0xE9,0xF9,0xE5,
                };
                static const uint8_t setup_interface_detect[]={
                    0x83,0x3E,0x00,0x20,0x06,0x75,0x05,0x36,0xC6,0x46,0xAA,0x00,
                    0xA1,0x12,0x20,0xCB,
                };
                static const uint8_t setup_drive_check[]={
                    0xA1,0x14,0x20,0xC3,
                };
                static const struct { unsigned at; const uint8_t *code; unsigned len; } setup_helpers[]={
                    {0x2f32,setup_keyboard_jump,sizeof setup_keyboard_jump},
                    {0x10cc0,setup_keyboard_read,sizeof setup_keyboard_read},
                    {0x2f37,setup_button_jump,sizeof setup_button_jump},
                    {0x10dc0,setup_button_poll,sizeof setup_button_poll},
                    {0x2374,setup_detect_jump,sizeof setup_detect_jump},
                    {0x10970,setup_interface_detect,sizeof setup_interface_detect},
                    {0x503,setup_return_near,sizeof setup_return_near},
                    {0x512,setup_return_near,sizeof setup_return_near},
                    {0x53b,setup_return_near,sizeof setup_return_near},
                    {0xa4b,setup_return_near,sizeof setup_return_near},
                    {0xa9f,setup_return_near,sizeof setup_return_near},
                    {0xba2,setup_return_near,sizeof setup_return_near},
                    {0xc35,setup_return_near,sizeof setup_return_near},
                    {0xfc0,setup_return_near,sizeof setup_return_near},
                    {0xffe,setup_return_near,sizeof setup_return_near},
                    {0x1387,setup_return_near,sizeof setup_return_near},
                    {0x14be,setup_return_near,sizeof setup_return_near},
                    {0x1509,setup_return_near,sizeof setup_return_near},
                    {0xc84,setup_drive_check,sizeof setup_drive_check},
                    {0x239c,setup_return_far,sizeof setup_return_far},
                    {0x23bd,setup_return_far,sizeof setup_return_far},
                    {0x2485,setup_return_far,sizeof setup_return_far},
                };
                for(unsigned j=0;j<sizeof setup_helpers/sizeof setup_helpers[0];j++)for(unsigned b=0;b<setup_helpers[j].len;b++)
                    g_pristine[setup_helpers[j].at+b]=g_mem[0][setup_helpers[j].at+b]=g_mem[1][setup_helpers[j].at+b]=setup_helpers[j].code[b];
#undef SW8
#undef SW16
            }

            /* Authored sprite streams and heap records; no game assets. File,
             * allocation, streaming and decoder stand-ins return; real near/far
             * string copies and pointer normalisation remain. Modes stage open,
             * signature, allocation, name and format failures, signed frame
             * counts and palette variants. Unconstrained controls remain. */
            if (!strcmp(o->module,"MPS_LOGO.EXE") && o->seg==0x006D && o->ip==0x0112 && (s&7)) {
                seg[S_DS]=seg[S_SS]=0x3000;r[R_SP]=0xFF00;flags=(uint16_t)(flags & (0xFFFFu ^ F_DF));
                const unsigned mode=((unsigned)s>>3)%24;
                const unsigned type=mode==5?0:mode==6?6:1+mode%5;
                const unsigned frames=(mode==7 || mode==10)?0:mode==8?0xFFFF:1+mode%3;
                const unsigned colours=mode==9?0:1+mode%3;
                small[0]=0x4000;small[1]=(mode&1)?2:0;small[2]=0x5000;small[3]=0;
#define LW8(a_,v_) do { const uint32_t la_=phys(seg[S_DS],(uint16_t)(a_)); \
    g_pristine[la_]=g_mem[0][la_]=g_mem[1][la_]=(uint8_t)(v_); } while(0)
#define LW16(a_,v_) do { LW8(a_,(uint16_t)(v_));LW8((a_)+1,(uint16_t)(v_)>>8); } while(0)
                static const uint8_t magic[]={'F','X','H','D','R','0',0};
                static const uint8_t filename[]={'s','h','a','p','e','.','s','s',0};
                static const uint8_t suffix[]={'.','s','s',0};
                for(unsigned j=0;j<sizeof magic;j++)LW8(0x0059+j,magic[j]);
                for(unsigned j=0;j<sizeof filename;j++)LW8(0x4000+j,filename[j]);
                for(unsigned j=0;j<sizeof suffix;j++)LW8(0x0052+j,suffix[j]);
                for(unsigned j=0;j<6;j++)LW8(0x004A+j,3+j);
                LW16(0x6000,mode);LW16(0x6002,0x7000);LW16(0x6004,mode&1);
                for(unsigned j=0;j<512;j++)LW8(0x7000+j,0);
                for(unsigned j=0;j<sizeof magic;j++)LW8(0x7000+j,mode==2 && j==0?'X':magic[j]);
                for(unsigned j=0;j<sizeof filename;j++)LW8(0x7007+j,mode==4 && j==0?'X':filename[j]);
                LW16(0x7010,0x100);LW16(0x7012,0);LW16(0x7014,0x100);LW16(0x7016,0);
                unsigned stream=0x7018;
                for(unsigned j=0;j<(frames<10?frames:0);j++) {LW16(stream,32*j);LW16(stream+2,0);stream+=4;}
                for(unsigned j=0;j<colours;j++) {LW8(stream,10+j);LW8(stream+1,20+j);LW8(stream+2,30+j);LW8(stream+3,0);stream+=4;}
                for(unsigned j=0;j<1024;j++)g_pristine[0x50000+j]=g_mem[0][0x50000+j]=g_mem[1][0x50000+j]=0;
                for(unsigned j=0;j<1024;j++)LW8(0x5000+j,0);
#define LH8(a_,v_) do {const uint32_t lh_=0x50000+(a_);g_pristine[lh_]=g_mem[0][lh_]=g_mem[1][lh_]=(uint8_t)(v_);}while(0)
#define LH16(a_,v_) do {LH8(a_,(uint16_t)(v_));LH8((a_)+1,(uint16_t)(v_)>>8);}while(0)
                LH8(0x0A,type);LH16(0x10,frames);LH8(0x12,colours);
                for(unsigned j=0;j<(frames<10?frames:0);j++)for(unsigned k=4;k<14;k+=2)LH16(0x22+14*j+k,4+k);
                static const uint8_t logo_helper0[]={0xE9,0x47,0xE4};
                static const uint8_t logo_helper1[]={0x56,0x57,0x51,0x06,0x53,0x89,0xE3,0x36,0x8B,0x7F,0x0E,0x36,0x8B,0x4F,0x12,0x8B,0x36,0x02,0x60,0x1E,0x07,0xFC,0xF3,0xA4,0x89,0x36,0x02,0x60,0xB8,0x01,0x00,0x5B,0x07,0x59,0x5F,0x5E,0xCB};
                static const uint8_t logo_helper2[]={0xE9,0x41,0xE8};
                static const uint8_t logo_helper3[]={0x83,0x3E,0x00,0x60,0x03,0x75,0x05,0x31,0xC0,0x31,0xD2,0xCB,0xB8,0x00,0x00,0xBA,0x00,0x50,0xCB};
                static const uint8_t logo_helper4[]={0xE9,0x5D,0xE6};
                static const uint8_t logo_helper5[]={0x83,0x3E,0x00,0x60,0x01,0x75,0x04,0xB8,0x00,0x00,0xCB,0xB8,0x01,0x00,0xCB};
                static const uint8_t logo_helper6[]={0xE9,0xC3,0xEA};
                static const uint8_t logo_helper7[]={0xA1,0x04,0x60,0xCB};
                static const uint8_t logo_helper8[]={0xCB};
                static const uint8_t logo_helper9[]={0xCB};
                static const uint8_t logo_helper10[]={0xCB};
                static const uint8_t logo_helper11[]={0xCB};
                static const uint8_t logo_helper12[]={0xCB};
                static const uint8_t logo_helper13[]={0xCB};
                static const struct { unsigned at; const uint8_t *code; unsigned len; } logo_helpers[]={{0x01816,logo_helper0,sizeof logo_helper0},{0x0FC60,logo_helper1,sizeof logo_helper1},{0x0050C,logo_helper2,sizeof logo_helper2},{0x0ED50,logo_helper3,sizeof logo_helper3},{0x01800,logo_helper4,sizeof logo_helper4},{0x0FE60,logo_helper5,sizeof logo_helper5},{0x0048A,logo_helper6,sizeof logo_helper6},{0x0EF50,logo_helper7,sizeof logo_helper7},{0x00566,logo_helper8,sizeof logo_helper8},{0x00552,logo_helper9,sizeof logo_helper9},{0x0171A,logo_helper10,sizeof logo_helper10},{0x00030,logo_helper11,sizeof logo_helper11},{0x00754,logo_helper12,sizeof logo_helper12},{0x006D2,logo_helper13,sizeof logo_helper13}};
                for(unsigned j=0;j<sizeof logo_helpers/sizeof logo_helpers[0];j++)for(unsigned b=0;b<logo_helpers[j].len;b++)
                    g_pristine[logo_helpers[j].at+b]=g_mem[0][logo_helpers[j].at+b]=g_mem[1][logo_helpers[j].at+b]=logo_helpers[j].code[b];

#undef LW8
#undef LW16
#undef LH8
#undef LH16
            }
            if (!strcmp(o->module, "SETUP.EXE") && o->seg == 0 && o->ip == 0x0C84) {
                seg[S_DS] = seg[S_SS] = 0x024C; r[R_SP] = 0xFF00;
                flags = (uint16_t)(flags & (0xFFFFu ^ F_DF));
#define NW8(seg_,off_,value_) do { uint32_t a_=phys((seg_),(uint16_t)(off_)); g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(value_); } while(0)
                /* The find-first call (INT 21h at 0C90) is replaced by MOV AL,k so that "found" is
                 * reached: the machine's DOS answers "not found" for the template. */
                const unsigned found = (s & 7) != 7;
                NW8(0, 0x0C90, 0xB0); NW8(0, 0x0C91, found ? 0x00 : 0x01);
                const unsigned len = (unsigned)s % 6;
                for (unsigned j = 0; j < 8; j++) { NW8(0x024C, 0x0ACE + j, 0); NW8(0x024C, 0x0B0B + j, 0); }
                for (unsigned j = 0; j < len; j++) { NW8(0x024C, 0x0ACE + j, 'A' + j); NW8(0x024C, 0x0B0B + j, 'A' + j); }
                const unsigned bad = ((unsigned)s >> 3) % (len + 2);       /* a mismatch inside the name, or none */
                if (bad < len) NW8(0x024C, 0x0B0B + bad, 'a' + bad);
#undef NW8
            }
            if (!strcmp(o->module, "MPS_LOGO.EXE") && o->seg == 0x00DA && o->ip == 0x0491) {
                seg[S_DS] = seg[S_SS] = 0x00DA; r[R_SP] = 0xFF00;
                flags = (uint16_t)(flags & (0xFFFFu ^ F_DF));
#define NW8(seg_,off_,value_) do { uint32_t a_=phys((seg_),(uint16_t)(off_)); g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(value_); } while(0)
#define NW16(seg_,off_,value_) do { uint16_t v_=(uint16_t)(value_); NW8(seg_,off_,v_);NW8(seg_,(uint16_t)((off_)+1),v_>>8); } while(0)
                uint32_t x_=(uint32_t)s*2654435761u+4242u;
#define NEXT() (x_=x_*1103515245u+12345u, (x_>>16))
                /* The argument is a segment: its header holds the index, count, fill word and sources.
                 * State 1 has count 0: the loop runs 65,536 times. */
                const uint16_t arg_seg=0x3000;
                small[0]=arg_seg; NW16(0x00DA,0xFF00,arg_seg);
                for(unsigned j=0;j<0x80;j++) NW8(arg_seg,j,NEXT());
                /* Vary the loader-relocated MOV AX,segment operand; the zero-base image alone misses this. */
                NW16(o->seg,0x04AD,(s & 1) ? 0x4000 : 0x00DA);
                NW16(arg_seg,0x2E,NEXT()%4);
                NW16(arg_seg,0x30,(s==1)?0:1+NEXT()%4);
#undef NEXT
#undef NW16
#undef NW8
            }
            if (!strcmp(o->module, "END.EXE") && o->seg == 0 && o->ip == 0x45AD) {
                seg[S_DS] = seg[S_SS] = 0x05FA; r[R_SP] = 0xFF00;
                flags = (uint16_t)(flags & (0xFFFFu ^ F_DF));
#define NW8(seg_,off_,value_) do { uint32_t a_=phys((seg_),(uint16_t)(off_)); g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(value_); } while(0)
#define NW16(seg_,off_,value_) do { uint16_t v_=(uint16_t)(value_); NW8(seg_,off_,v_);NW8(seg_,(uint16_t)((off_)+1),v_>>8); } while(0)
                uint32_t x_=(uint32_t)s*2654435761u+9191u;
#define NEXT() (x_=x_*1103515245u+12345u, (x_>>16))
                const uint16_t arg_seg=0x3000;
                small[0]=arg_seg; NW16(0x05FA,0xFF00,arg_seg);
                for(unsigned j=0;j<0x80;j++) NW8(arg_seg,j,NEXT());
                NW16(arg_seg,0x1C,NEXT()%8);
                NW16(arg_seg,0x22,(s==1)?0:1+NEXT()%4);
                if (s == 1) {
                    /* The full 65,536-iteration write must not overwrite the return frame.
                     * Zero sources/fill also keep the routine's own [2612] flag clear. */
                    seg[S_SS] = 0x6000;
                    for (unsigned j = 0; j < 0x10000; j++) NW8(arg_seg,j,0);
                    NW16(0x6000,0xFF00,arg_seg);
                }
#undef NEXT
#undef NW16
#undef NW8
            }
            if (!strcmp(o->module, "PLAYER.EXE") && o->seg == 0 && o->ip == 0x05F6) {
                seg[S_DS] = seg[S_SS] = 0x6000; r[R_SP] = 0xFF00;
                flags = (uint16_t)(flags & (0xFFFFu ^ F_DF));
#define NW8(seg_,off_,value_) do { uint32_t a_=phys((seg_),(uint16_t)(off_)); g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(value_); } while(0)
#define NW16(seg_,off_,value_) do { uint16_t v_=(uint16_t)(value_); NW8(seg_,off_,v_);NW8(seg_,(uint16_t)((off_)+1),v_>>8); } while(0)
                uint32_t x_=(uint32_t)s*2654435761u+555u;
#define NEXT() (x_=x_*1103515245u+12345u, (x_>>16))
                /* Two linear addresses (low word, high word) inside scratch memory at 40000h..4FFFFh,
                 * sometimes overlapping, and a count of 0 to 24 bytes (0 in one state in eight). */
                const uint32_t lin_a = 0x40000u + (NEXT() & 0x0FFFu);
                const uint32_t lin_b = (s & 1) ? lin_a + (NEXT() % 12) : 0x42000u + (NEXT() & 0x0FFFu);
                const unsigned count = (s & 7) == 0 ? 0 : 1 + NEXT() % 24;
                NW16(0x6000, 0xFF00, lin_a); NW16(0x6000, 0xFF02, lin_a >> 16);
                NW16(0x6000, 0xFF04, lin_b); NW16(0x6000, 0xFF06, lin_b >> 16);
                NW16(0x6000, 0xFF08, count);
                small[0] = (uint16_t)lin_a; small[1] = (uint16_t)(lin_a >> 16); small[2] = (uint16_t)lin_b; small[3] = (uint16_t)(lin_b >> 16);
                for (unsigned j = 0; j < 0x3100; j++) NW8(0x4000, j, NEXT());
#undef NEXT
#undef NW16
#undef NW8
            }
            if ((!strcmp(o->module, "SETUP.EXE") && o->seg == 0 && (o->ip == 0x0BFB || o->ip == 0x1387)) ||
                (!strcmp(o->module, "MPS_LOGO.EXE") && o->seg == 0x00DA && (o->ip == 0x04C4 || o->ip == 0x043D))) {
                /* The DOS calls inside these routines are replaced by two-byte instructions that set the
                 * outcome from the state's seed (CLC/STC for the carry; MOV AH,80h to fill a read), so each
                 * arm - success and failure, a full buffer - is reached. */
                seg[S_DS] = seg[S_SS] = 0x6000; r[R_SP] = 0xFF00;
                flags = (uint16_t)(flags & (0xFFFFu ^ F_DF));
#define NW8(seg_,off_,value_) do { uint32_t a_=phys((seg_),(uint16_t)(off_)); g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(value_); } while(0)
#define NW16(seg_,off_,value_) do { uint16_t v_=(uint16_t)(value_); NW8(seg_,off_,v_);NW8(seg_,(uint16_t)((off_)+1),v_>>8); } while(0)
                uint32_t x_=(uint32_t)s*2654435761u+8080u;
#define NEXT() (x_=x_*1103515245u+12345u, (x_>>16))
#define PATCH(at_,b0_,b1_) do { NW8(o->seg,(at_),(b0_)); NW8(o->seg,(at_)+1,(b1_)); } while(0)
#define CARRY(at_) do { if (NEXT() & 1) PATCH(at_, 0xF9, 0x90); else PATCH(at_, 0xF8, 0x90); } while(0)
                for (unsigned j = 0; j < 0x300; j++) NW8(0x6000, 0xFD00 + j, 0);
                if (o->ip == 0x0BFB) {                         /* SETUP: free a block */
                    CARRY(0x0C17); PATCH(0x0C20, 0x90, 0x90);
                    /* Replace only the terminal error helper with a jump to the real epilogue,
                     * so failed frees return and their message argument is compared too. */
                    PATCH(0x0B9D, 0xE9, 0x85); NW8(0, 0x0B9F, 0x00);
                    NW16(0x6000, 0xFF00, NEXT()); NW8(0x6000, 0x078E, (NEXT() & 3) == 0);
                    small[0] = NEXT();
                } else if (o->ip == 0x04C4) {                   /* MPS_LOGO: free a block (far) */
                    CARRY(0x04CE);
                    PATCH(0x040E, 0xE9, 0xC7); NW8(o->seg, 0x0410, 0); /* error helper jumps to POP ES / RETF */
                    NW16(0x6000, 0xFF00, NEXT());
                } else if (o->ip == 0x043D) {                   /* MPS_LOGO: load a file */
                    NW16(0x6000, 0x0082, (s & 7) == 0 ? 0 : 0x1000 + (NEXT() & 0xFF));
                    PATCH(0x040E, 0xE9, 0x7E); NW8(o->seg, 0x0410, 0); /* error helper jumps to POP DS / RET */
                    CARRY(0x0449);                              /* open */
                    if (NEXT() & 1) PATCH(0x045E, 0xB4, 0x80); else CARRY(0x045E);   /* read: full buffer, or carry */
                    CARRY(0x0484);                              /* close */
                } else {                                        /* SETUP 1387: the interrupt call */
                    static const uint16_t vectors[] = { 0x10, 0x21, 0x16, 0x1A, 0x33, 0 };
                    PATCH(0x13B9, 0x90, 0x90); PATCH(0x13BE, 0x90, 0x90); PATCH(0x13C3, 0x90, 0x90); PATCH(0x13C8, 0x90, 0x90);
                    const uint16_t vec = vectors[NEXT() % 6];
                    NW16(0x6000, 0xFF00, vec); NW16(0x6000, 0xFF02, 0x1000); NW16(0x6000, 0xFF04, 0x1100);
                    small[0] = vec; small[1] = 0x1000; small[2] = 0x1100;
                    for (unsigned j = 0; j < 8; j++) { NW8(0x6000, 0x1000 + j, NEXT()); NW8(0x6000, 0x1100 + j, NEXT()); }
                }
#undef CARRY
#undef PATCH
#undef NEXT
#undef NW16
#undef NW8
            }
            if (!strcmp(o->module, "START.EXE") && o->seg == 0 && o->ip == 0xA8C2) {
                seg[S_DS] = seg[S_SS] = 0x6000; r[R_SP] = 0xFF00;
                flags = (uint16_t)(flags & (0xFFFFu ^ F_DF));
#define NW8(seg_,off_,value_) do { uint32_t a_=phys((seg_),(uint16_t)(off_)); g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(value_); } while(0)
#define NW16(seg_,off_,value_) do { uint16_t v_=(uint16_t)(value_); NW8(seg_,off_,v_);NW8(seg_,(uint16_t)((off_)+1),v_>>8); } while(0)
                uint32_t x_=(uint32_t)s*2654435761u+6060u;
#define NEXT() (x_=x_*1103515245u+12345u, (x_>>16))
                /* The DOS call (INT 21h at A8FC) becomes CLC or STC; the block record sits in the data
                 * segment with its far flag, sizes and saved size from the seed. */
                if (NEXT() & 1) { NW8(0, 0xA8FC, 0xF9); NW8(0, 0xA8FD, 0x90); } else { NW8(0, 0xA8FC, 0xF8); NW8(0, 0xA8FD, 0x90); }
                const uint16_t rec = 0x1000 + 16 * (NEXT() & 0x1F);
                for (unsigned j = 0; j < 0x400; j++) NW8(0x6000, 0x1000 + j, NEXT());
                NW8(0x6000, rec + 2, NEXT() & 7);
                NW16(0x6000, rec + 4, NEXT() % 0x40);
                NW16(0x6000, rec - 2, NEXT() % 0x40);
                NW16(0x6000, 0xAE58, NEXT());
                r[R_BX] = rec;
                r[R_AX] = (NEXT() % 3 == 0) ? (uint16_t)(NEXT() & 0xF) : (uint16_t)(NEXT() % 0x400);
#undef NEXT
#undef NW16
#undef NW8
            }
            if (!strcmp(o->module, "SETUP.EXE") && o->seg == 0 && o->ip == 0x14BE) {
                seg[S_DS] = seg[S_SS] = 0x6000; r[R_SP] = 0xFF00;
                flags = (uint16_t)(flags & (0xFFFFu ^ F_DF));
#define NW8(seg_,off_,value_) do { uint32_t a_=phys((seg_),(uint16_t)(off_)); g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(value_); } while(0)
#define NW16(seg_,off_,value_) do { uint16_t v_=(uint16_t)(value_); NW8(seg_,off_,v_);NW8(seg_,(uint16_t)((off_)+1),v_>>8); } while(0)
                uint32_t x_=(uint32_t)s*2654435761u+1414u;
#define NEXT() (x_=x_*1103515245u+12345u, (x_>>16))
                /* Row, column (both FFh: no cursor move) and a string of up to eight bytes, zero-ended:
                 * letters, FFh attribute escapes (also a trailing one), and the BIOS answers in int_h. */
                const unsigned nomove = (NEXT() & 3) == 0;
                const uint16_t row = nomove ? 0xFFFF : (uint16_t)(NEXT() & 0x1F1F), col = nomove ? 0xFFFF : (uint16_t)(NEXT() & 0x1F1F);
                NW16(0x6000, 0xFF00, row); NW16(0x6000, 0xFF02, col); NW16(0x6000, 0xFF04, 0x2000);
                small[0] = row; small[1] = col; small[2] = 0x2000;
                const unsigned len = NEXT() % 9;
                for (unsigned j = 0; j < len; j++) NW8(0x6000, 0x2000 + j, (NEXT() % 5 == 0) ? 0xFF : 'a' + NEXT() % 26);
                NW8(0x6000, 0x2000 + len, 0); NW8(0x6000, 0x2000 + len + 1, (NEXT() & 1) ? 0 : 0x1F);
                NW8(0x6000, 0x2000 + len + 2, 0);
#undef NEXT
#undef NW16
#undef NW8
            }
            if ((!strcmp(o->module, "PLAYER.EXE") && o->seg == 0 && o->ip == 0x0F47) ||
                (!strcmp(o->module, "END.EXE") && o->seg == 0 && (o->ip == 0x44B9 || o->ip == 0x44E7)) ||
                (!strcmp(o->module, "START.EXE") && o->seg == 0 && (o->ip == 0x8545 || o->ip == 0x8573)) ||
                (!strcmp(o->module, "SETUP.EXE") && o->seg == 0x0237 && o->ip == 0x0004)) {
                /* The mouse driver (INT 33h) is answered by int_h: buttons 0-7 (both together included),
                 * column and row from the call count, and a reset answer of FFFFh or 0. The routine's data
                 * goes to a scratch segment, away from the code. For the driver probe, the INT 21h answer
                 * leaves BX and ES as the state has them: zero, or a pointer at an IRET, in some states. */
                seg[S_DS] = seg[S_SS] = 0x6000; r[R_SP] = 0xFF00;
                flags = (uint16_t)(flags & (0xFFFFu ^ F_DF));
                if (o->ip == 0x0004) {
                    uint32_t x_ = (uint32_t)s * 2654435761u + 7007u;
#define NEXT() (x_ = x_ * 1103515245u + 12345u, (x_ >> 16))
                    r[R_BX] = (NEXT() & 3) == 0 ? 0 : (uint16_t)(0x1000 + (NEXT() & 0xFF));
                    seg[S_ES] = (NEXT() & 3) == 0 ? 0 : 0x6000;
                    if (NEXT() & 1) { const uint32_t a_ = phys(seg[S_ES], r[R_BX]); g_pristine[a_] = g_mem[0][a_] = g_mem[1][a_] = 0xCF; }
#undef NEXT
                }
            }
            if (!strcmp(o->module, "START.EXE") && o->seg == 0 && o->ip == 0x149C) {
                seg[S_DS] = seg[S_SS] = 0x0A95; r[R_SP] = 0xFF00;
                flags = (uint16_t)(flags & (0xFFFFu ^ F_DF));
#define NW8(seg_,off_,value_) do { uint32_t a_=phys((seg_),(uint16_t)(off_)); g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(value_); } while(0)
#define NW16(seg_,off_,value_) do { uint16_t v_=(uint16_t)(value_); NW8(seg_,off_,v_);NW8(seg_,(uint16_t)((off_)+1),v_>>8); } while(0)
                uint32_t x_=(uint32_t)s*2654435761u+777u;
#define NEXT() (x_=x_*1103515245u+12345u, (x_>>16))
#define EMIT(b_) NW8(0x0A95,at++,(b_))
#define EMITC(b_) NW8(0,at++,(b_))
                for(unsigned j=0;j<0x200;j++) NW8(0x0A95,0xFD00+j,0);
                for(unsigned j=0;j<0x900;j++) NW8(0x0A95,0xF000+j,0);
                /* The skip flag (the caller's argument): zero in half the states. */
                small[0]=(s&1)?0:(uint16_t)(1+NEXT()%0x7F);
                NW16(0x0A95,0xFF00,small[0]);
                /* Airfield records at CBDE + 16 * index; the four indexes, sometimes takeoff == landing. */
                for(unsigned j=0;j<0x90;j++) NW8(0x0A95,0xCBDE + j,NEXT());
                for(unsigned k=0;k<9;k++) {
                    NW16(0x0A95,0xCBDE + 16*k,(k&1)?0:1+NEXT()%15);       /* name index, or 0: use the byte at +E */
                    NW8(0x0A95,0xCBDE + 16*k + 0xE,NEXT()%16);
                }
                const unsigned ia=NEXT()%8;
                NW16(0x0A95,0xE062,ia); NW16(0x0A95,0xE060,NEXT()%8); NW16(0x0A95,0xE072,NEXT()%8);
                NW16(0x0A95,0xE074,(s&4)?ia:NEXT()%8);
                /* Airfield names: a pointer table at DC70 into short strings. */
                for(unsigned k=0;k<16;k++) {
                    NW16(0x0A95,0xDC70+2*k,0xF800+16*k);
                    const unsigned len=NEXT()%9;
                    for(unsigned j=0;j<len;j++) NW8(0x0A95,0xF800+16*k+j,'a'+NEXT()%26);
                }
                NW16(0x0A95,0xD88E,NEXT()); NW16(0x0A95,0xE09C,NEXT());
                for(unsigned j=0;j<0x40;j++) { NW8(0x0A95,0xDC00+j,NEXT()); NW8(0x0A95,0xDC18+j,NEXT()); }
                /* Argument-logging stubs: each folds its stack words into one checksum cell, so a
                 * wrong argument is a difference. 2F4C and 405F also return scripted bytes. */
                static const struct { uint16_t at, args, cell, tab, ctr; } near_stubs[]={
                    {0x2F4C,5,0xF500,0xF410,0xF400},{0x405F,1,0xF502,0xF420,0xF402},
                    {0x2B1B,6,0xF504,0,0},{0x2651,2,0xF506,0,0},{0x31FA,4,0xF508,0,0}};
                for(unsigned j=0;j<sizeof near_stubs/sizeof near_stubs[0];j++) {
                    unsigned at=near_stubs[j].at; const uint16_t cell=near_stubs[j].cell;
                    EMITC(0x55);EMITC(0x8B);EMITC(0xEC);
                    EMITC(0xB9);EMITC(near_stubs[j].args);EMITC(0);                  /* mov cx,args */
                    EMITC(0x8D);EMITC(0x5E);EMITC(0x04);                              /* lea bx,[bp+4] */
                    EMITC(0x8B);EMITC(0x07);EMITC(0x83);EMITC(0xC3);EMITC(0x02);        /* mov ax,[bx] / add bx,2 */
                    EMITC(0x8B);EMITC(0x16);EMITC(cell);EMITC(cell>>8);                /* mov dx,[cell] */
                    EMITC(0xD1);EMITC(0xC2);EMITC(0x33);EMITC(0xD0);                   /* rol dx,1 / xor dx,ax */
                    EMITC(0x89);EMITC(0x16);EMITC(cell);EMITC(cell>>8);                /* mov [cell],dx */
                    EMITC(0xE2);EMITC(0xED);                                         /* loop */
                    if(near_stubs[j].tab) {
                        EMITC(0x8B);EMITC(0x1E);EMITC(near_stubs[j].ctr);EMITC(near_stubs[j].ctr>>8);     /* mov bx,[ctr] */
                        EMITC(0xFF);EMITC(0x06);EMITC(near_stubs[j].ctr);EMITC(near_stubs[j].ctr>>8);     /* inc word [ctr] */
                        EMITC(0x8A);EMITC(0x87);EMITC(near_stubs[j].tab);EMITC(near_stubs[j].tab>>8);     /* mov al,[bx+tab] */
                    }
                    EMITC(0x5D);EMITC(0xC3);
                }
                NW8(0,0x3588,0xC3);
                {   /* the far drawing slot 6A26 (seven words) */
                    NW8(0x0A95,0x6A26,0xEA); NW16(0x0A95,0x6A27,0xF080); NW16(0x0A95,0x6A29,0x0A95);
                    unsigned at=0xF080; const uint16_t cell=0xF50A;
                    EMIT(0x55);EMIT(0x8B);EMIT(0xEC);EMIT(0xB9);EMIT(7);EMIT(0);EMIT(0x8D);EMIT(0x5E);EMIT(0x06);
                    EMIT(0x8B);EMIT(0x07);EMIT(0x83);EMIT(0xC3);EMIT(0x02);
                    EMIT(0x8B);EMIT(0x16);EMIT(cell);EMIT(cell>>8);EMIT(0xD1);EMIT(0xC2);EMIT(0x33);EMIT(0xD0);
                    EMIT(0x89);EMIT(0x16);EMIT(cell);EMIT(cell>>8);EMIT(0xE2);EMIT(0xED);EMIT(0x5D);EMIT(0xCB);
                }
                static const uint8_t legret[]={0,0,1,2,0x80,0xFF}, waitret[]={0,1,0,0xFF};
                for(unsigned j=0;j<8;j++) { NW8(0x0A95,0xF410+j,legret[NEXT()%6]); NW8(0x0A95,0xF420+j,waitret[NEXT()%4]); }
#undef EMIT
#undef EMITC
#undef NEXT
#undef NW8
#undef NW16
            }
            if ((o->seg == 0) && ((!strcmp(o->module,"START.EXE") && o->ip==0x39F7) ||
                                 (!strcmp(o->module,"END.EXE") && o->ip==0x1CF5))) {
                const int front=!strcmp(o->module,"START.EXE");
                const uint16_t dg=front?0x0A95:0x05FA;
                seg[S_DS]=dg; seg[S_SS]=(s&32)?0x6000:dg; r[R_SP]=0xFF00;
                flags=(uint16_t)(flags & (0xFFFFu ^ F_DF));
#define NW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_)); g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define NW16(s_,o_,v_) do { uint16_t vv_=(uint16_t)(v_); NW8((s_),(o_),vv_); NW8((s_),(uint16_t)((o_)+1),vv_>>8); } while(0)
                uint32_t rng_=(uint32_t)s*2654435761u+(uint32_t)routine_seed(seed,o);
#define NEXT() (rng_=rng_*1103515245u+12345u,rng_>>16)
                for(unsigned j=0;j<0x200;j++) NW8(seg[S_SS],0xFD00+j,NEXT());
                for(unsigned j=0;j<0x800;j++) NW8(dg,0xF000+j,0);
                const uint16_t cursor=front?0xE08A:0x7214, bounds=front?0xDC26:0x6DB0;
                const uint16_t record_ptr=front?0xE096:0x7222, axes=front?0x69B2:0x23F4;
                NW16(dg,record_ptr,0x1000); NW16(dg,record_ptr+2,0x3000);
                NW8(0x3000,0x1072,(s>>4)&3);
                NW8(dg,axes,NEXT());NW8(dg,axes+1,NEXT());
                NW8(dg,axes+4,NEXT()&1);NW8(dg,axes+5,NEXT()&1);
                NW16(dg,bounds,10);NW16(dg,bounds+2,190);NW16(dg,bounds+4,10);NW16(dg,bounds+6,310);
                static const uint16_t pos[]={0,9,10,11,50,100,160,180,190,191,310,311,0x7FFF,0x8000,0xFFFF};
                NW16(dg,cursor,pos[NEXT()%15]);NW16(dg,cursor+2,pos[NEXT()%15]);
                for(unsigned j=4;j<10;j+=2) NW16(dg,cursor+j,NEXT());
                NW8(dg,cursor+8,NEXT());NW8(dg,cursor+9,NEXT());
                NW16(dg,cursor-2,NEXT());
                static const uint16_t keys[]={0,0x1000,0x4800,0x4B00,0x4D00,0x5000,0x5300,8,13,27,65,0xFFFF};
                const uint16_t key=keys[(s-1)%12];
                NW16(dg,front?0xB306:0x55BC,(s&64)?key:keys[NEXT()%12]);
                NW16(dg,0xF600,NEXT()&1);NW16(dg,0xF602,NEXT()&1);
                const uint16_t table=(s%31==0 && seg[S_SS]==dg)?0xFFE0:0x1200;
                small[0]=table; NW16(seg[S_SS],0xFF00,table);
                for(unsigned j=0;j<0x80;j++) NW8(dg,table+j,NEXT());
                NW8(dg,table+3,(s%9));
                for(unsigned j=0;j<8;j++) {
                    const unsigned x=15+NEXT()%290,y=15+NEXT()%170,w=NEXT()%16,h=NEXT()%16;
                    NW16(dg,table+4+8*j,x);NW16(dg,table+6+8*j,y);
                    NW16(dg,table+8+8*j,x+w);NW16(dg,table+10+8*j,y+h);
                }
                /* Returning driver/draw stubs hash every stack argument, including far-call frames.
                 * Cells and return values are separate, so call order and arguments stay observable. */
                static const struct { uint16_t a,b; uint8_t far,args,mode; } stubs[]={
                    {0x6B89,0x25CB,1,1,1}, {0x6B7A,0x25BC,1,0,2}, {0x6B7F,0x25C1,1,0,3},
                    {0x8545,0x44B9,0,0,0}, {0x7611,0x3E61,0,0,0}, {0x85A7,0x451B,0,0,0},
                    {0x6A8A,0x24CC,1,8,0}, {0x3693,0x1992,0,4,0}, {0x3E5E,0x2023,0,0,0},
                    {0x8BEA,0x452C,0,4,4}
                };
                for(unsigned j=0;j<sizeof stubs/sizeof stubs[0];j++) {
                    const uint16_t slot=front?stubs[j].a:stubs[j].b, target_seg=stubs[j].far?dg:0;
                    const uint16_t code=(uint16_t)(0xF000+0xB0*j),cell=(uint16_t)(0xF700+2*j),result=(uint16_t)(0xF740+2*j);
                    if(stubs[j].far) { NW8(target_seg,slot,0xEA);NW16(target_seg,slot+1,code);NW16(target_seg,slot+3,dg); }
                    else { NW8(0,slot,0xE9);NW16(0,slot+1,(uint16_t)(code-slot-3)); }
                    const uint16_t cs_=stubs[j].far?dg:0;
                    unsigned at_=code;
#define EMIT(v_) NW8(cs_,at_++,(v_))
                    EMIT(0x55);EMIT(0x8B);EMIT(0xEC);EMIT(0x50);EMIT(0x53);
                    EMIT(0xFF);EMIT(0x06);EMIT(cell);EMIT(cell>>8); /* inc word [cell] */
                    const unsigned nargs=stubs[j].args+(!front && j==4);
                    for(unsigned k=0;k<nargs;k++) {
                        EMIT(0x8B);EMIT(0x46);EMIT((stubs[j].far?6:4)+2*k);
                        EMIT(0x8B);EMIT(0x1E);EMIT(cell);EMIT(cell>>8);
                        EMIT(0xD1);EMIT(0xC3);EMIT(0x33);EMIT(0xD8);
                        EMIT(0x89);EMIT(0x1E);EMIT(cell);EMIT(cell>>8);
                    }
                    EMIT(0x5B);EMIT(0x58);
                    if(stubs[j].mode==1) {
                        EMIT(0x53);EMIT(0x8B);EMIT(0x5E);EMIT(0x06);EMIT(0xD1);EMIT(0xE3);
                        EMIT(0x8B);EMIT(0x87);EMIT(0x00);EMIT(0xF6);EMIT(0x5B);
                    } else {
                        EMIT(0xA1);EMIT(result);EMIT(result>>8);
                    }
                    EMIT(0x5D);EMIT(stubs[j].far?0xCB:0xC3);
                    static const uint16_t distances[]={0,1,100,999,1000,1001,0x7FFF,0xFFFF};
                    NW16(dg,result,stubs[j].mode==2?((s>>6)&1):stubs[j].mode==3?key:stubs[j].mode==4?distances[NEXT()%8]:NEXT());
#undef EMIT
                }
#undef NEXT
#undef NW16
#undef NW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0x1450) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                /* The quad and left point arrays overlap the live band table. */
                if((s&15)==8) { seg[S_SS]=dg;r[R_SP]=0xC160; }
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define HW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define HW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);HW8((s_),(o_),v__);HW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define HRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
                for(unsigned j=0;j<0x200;j++)HW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),HRAND());
                for(unsigned j=0;j<0x200;j++)HW8(dg,0xF200+j,0);
                small[0]=0x2000;HW16(seg[S_SS],r[R_SP],0x2000);HW16(dg,0x2004,HRAND());
                static const uint16_t edge[]={0,1,0x1FF,0x200,0x201,0x7FFF,0x8000,0xFFFF};
                static const uint8_t detail[]={0,2,3,4,127,128,255};
                HW8(dg,0x990E,detail[s%7]);
                HW16(dg,0x438E,(s>>3)&1);HW16(dg,0x43E0,(s>>4)&1);
                HW16(dg,0x49B0,edge[HRAND()%8]);HW16(dg,0x49B6,edge[HRAND()%8]);
                HW16(dg,0x49B8,edge[HRAND()%8]);HW16(dg,0x49BA,HRAND());HW16(dg,0x49BC,HRAND());
                HW8(dg,0x4368,(s%4)?0:(1+HRAND()%31));HW8(dg,0x294B,(s>>2)&1);
                HW16(dg,0x0D1E,HRAND());HW16(dg,0x0D20,HRAND());
                for(unsigned j=0;j<16;j++)HW16(dg,0x0D22+2*j,HRAND());
                /* Returning far draw probes fold argument order and all eight
                 * vertex words into guest cells. Arithmetic helpers stay original. */
                for(unsigned which=0;which<2;which++) {
                    unsigned out_=which?0x01E6:0;const uint16_t cs_=0x0FB2,cell=0xF200+2*which;
#define HEMIT(v_) HW8(cs_,out_++,(v_))
                    HEMIT(0x55);HEMIT(0x8B);HEMIT(0xEC);HEMIT(0x50);HEMIT(0x53);HEMIT(0x56);
                    HEMIT(0xFF);HEMIT(0x06);HEMIT(cell);HEMIT(cell>>8);
                    for(unsigned j=0;j<(which?4:1);j++) {
                        HEMIT(0x8B);HEMIT(0x46);HEMIT(6+2*j);
                        HEMIT(0x8B);HEMIT(0x1E);HEMIT(cell);HEMIT(cell>>8);
                        HEMIT(0xD1);HEMIT(0xC3);HEMIT(0x33);HEMIT(0xD8);
                        HEMIT(0x89);HEMIT(0x1E);HEMIT(cell);HEMIT(cell>>8);
                    }
                    if(which) {
                        HEMIT(0x8B);HEMIT(0x76);HEMIT(0x0A); /* DS:SI vertices */
                        for(unsigned j=0;j<8;j++) {
                            HEMIT(0x8B);HEMIT(0x44);HEMIT(2*j);
                            HEMIT(0x8B);HEMIT(0x1E);HEMIT(cell);HEMIT(cell>>8);
                            HEMIT(0xD1);HEMIT(0xC3);HEMIT(0x33);HEMIT(0xD8);
                            HEMIT(0x89);HEMIT(0x1E);HEMIT(cell);HEMIT(cell>>8);
                        }
                    }
                    HEMIT(0x5E);HEMIT(0x5B);HEMIT(0x58);HEMIT(0x5D);HEMIT(0xCB);
#undef HEMIT
                }
#undef HRAND
#undef HW16
#undef HW8
            }
            if (!strcmp(o->module,"START.EXE") && o->seg==0 && o->ip==0x7904) {
                const uint16_t dg=0x5000;seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xFF00;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define TW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define TW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);TW8((s_),(o_),v__);TW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define TRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
                const unsigned kind=(s%64==0)?1:(s%64==1)?2:(s%64==4)?128:(s%64==5)?255:16;
                small[0]=(uint16_t)kind;TW16(seg[S_SS],r[R_SP],kind);
                TW8(dg,(uint16_t)(0xD2A6+(int8_t)kind),kind==16?16:0);
                for(unsigned j=0;j<0x200;j++)TW8(seg[S_SS],0xFD00+j,TRAND());
                for(unsigned j=0;j<0x800;j++)TW8(0x7000,0x1000+j,TRAND());
                const int alias=(s%64==3);
                TW16(dg,0xD336,alias?0xCA8C:0x1000);TW16(dg,0xD338,alias?dg:0x7000);
                for(unsigned j=0;j<256;j++)TW8(dg,0xCAD8+j,3);
                TW8(dg,0xCBC8,0); /* one populated tile; other tiles still test skip paths */
                const unsigned count=alias?1:(s%13==0)?0:3;
                for(unsigned level=1;level<=3;level++) {
                    const uint16_t records=0x2000+level*0x100;
                    if(level==3) { TW16(dg,0xC91C,records);TW16(dg,0x0FCA,count); }
                    else { TW16(dg,0xC85C+64*level,records);TW16(dg,0x0F0A+64*level,(kind==2 && level==1)?0:count); }
                    for(unsigned j=0;j<3;j++) {
                        TW16(dg,records+7*j,(kind==1 && j==0)?0xEFFF:TRAND());TW16(dg,records+7*j+2,TRAND());
                        TW16(dg,records+7*j+4,TRAND());TW8(dg,records+7*j+6,(j==0 || (j==1 && (s&8)))?kind:(kind+1));
                    }
                }
                /* Level one selects a nonzero x tile with a negative local x,
                 * forcing the low-word carry into the signed high word. */
                TW16(dg,0xF800,s%17==0?0xFFFF:0);TW16(dg,0xF802,kind==1?16:((s>>3)&1));
                TW16(dg,0xF810,0);TW16(dg,0xF812,0);
                for(unsigned which=0;which<2;which++) {
                    const uint16_t slot=which?0x76C0:0x7C56,code=which?0xF100:0xF000,cell=0xF810+2*which;
                    TW8(0,slot,0xE9);TW16(0,slot+1,(uint16_t)(code-slot-3));
                    unsigned out_=code;
#define TEMIT(v_) TW8(0,out_++,(v_))
                    TEMIT(0x55);TEMIT(0x8B);TEMIT(0xEC);TEMIT(0x53);
                    TEMIT(0xFF);TEMIT(0x06);TEMIT(cell);TEMIT(cell>>8);
                    for(unsigned j=0;j<(which?1:3);j++) {
                        TEMIT(0x8B);TEMIT(0x46);TEMIT(4+2*j);TEMIT(0x8B);TEMIT(0x1E);TEMIT(cell);TEMIT(cell>>8);
                        TEMIT(0xD1);TEMIT(0xC3);TEMIT(0x33);TEMIT(0xD8);TEMIT(0x89);TEMIT(0x1E);TEMIT(cell);TEMIT(cell>>8);
                    }
                    TEMIT(0x5B);TEMIT(0xA1);TEMIT(0x00+2*which);TEMIT(0xF8);TEMIT(0x5D);TEMIT(0xC3);
#undef TEMIT
                }
#undef TRAND
#undef TW16
#undef TW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0xDC46) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define KW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define KW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);KW8((s_),(o_),v__);KW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define KRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
                for(unsigned j=0;j<0x200;j++)KW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),KRAND());
                /* Object table: up to three records, each its own random mix of
                 * kinds, flags and positions. */
                const unsigned count=(s%9==0)?0:1+(unsigned)(KRAND()%3);
                KW16(dg,0x49F4,count);
                static const uint8_t kinds[16]={0,1,10,15,0x18,0x18,0x18,0x19,0x24,0x25,0x25,0x26,0x27,0x30,0xF0,0x7F};
                for(unsigned i=0;i<3;i++) {
                    const uint16_t rec=(uint16_t)(0x2000+0x40*i);
                    KW16(dg,0x4E6C+2*i,rec);
                    const uint32_t h=KRAND()|(KRAND()<<16);
                    unsigned kind=kinds[h%16];if(kind==0x7F)kind=(h>>4)&0xFF;
                    unsigned hi=((h>>8)%7==0)?1+((h>>11)%3):0;
                    const unsigned special_draw=((h>>14)%9==0);
                    uint16_t word8=(uint16_t)(kind|(hi<<8)|(special_draw?0x0400:0)|(((h>>15)&1)<<15));
                    for(unsigned j=0;j<0x20;j++)KW8(dg,rec+j,KRAND());
                    KW16(dg,rec+8,word8);
                    KW8(dg,rec+9,(uint8_t)((word8>>8)|(((h>>20)&1)?0x80:0)));
                    KW8(dg,rec+6,(uint8_t)(((h>>21)%3==0?0x03:0)|(((h>>23)&1)?0x20:0)|((h>>24)&0x1C)));
                    if((h>>26)&1)KW16(dg,rec+0x0E,0);
                }
                KW16(dg,0xC09E,(s%3==0)?0:(uint16_t)KRAND());
                KW16(dg,0x2DF4,(s%5==0)?0:(s%5==1)?0x80:(uint16_t)KRAND());
                KW16(dg,0xE00C,KRAND()&15);
                for(unsigned j=0;j<16;j++)KW8(dg,0xB2D7+16*j,KRAND());
                KW16(dg,0xB07E,(s&8)?(uint16_t)KRAND():0);
                KW8(dg,0x9B34,KRAND());
                KW16(dg,0x9908,(s&16)?(uint16_t)KRAND():0);
                KW16(dg,0x98A4,KRAND());
                for(unsigned j=0;j<0x200;j++)KW8(dg,0xB78A+j,KRAND());
                for(unsigned j=0;j<0x200;j++)KW8(dg,0x4ED2+j,KRAND());
                for(unsigned j=0;j<12;j++)KW16(dg,0x49D6+2*j,(s%11==0)?(uint16_t)(j*3):(uint16_t)KRAND());
                /* The marker record at [7D7E]: bit 7 of either byte of the second
                 * word rejects it. */
                KW16(dg,0x7D7E,0x2400);
                KW16(dg,0x2400,KRAND());
                KW16(dg,0x2402,(s%4==0)?(uint16_t)KRAND():(uint16_t)(KRAND()&0x7F7F));
                KW16(dg,0x2404,KRAND());
                for(unsigned j=0;j<8;j++)KW16(dg,0xF830+2*j,KRAND());
                for(unsigned j=0;j<8;j++)KW16(dg,0xF810+2*j,0);
                /* Returning probes fold every argument, in order, into a guest cell
                 * and count the call; the 104E:0 arithmetic helper stays original. */
#define KSTUB(sg_,off_,cell_,nargs_,retcell_,far_) do { unsigned out_=(off_);const uint16_t sgx_=(sg_); \
    KW8(sgx_,out_++,0x55);KW8(sgx_,out_++,0x8B);KW8(sgx_,out_++,0xEC);KW8(sgx_,out_++,0x53); \
    KW8(sgx_,out_++,0xFF);KW8(sgx_,out_++,0x06);KW8(sgx_,out_++,(cell_));KW8(sgx_,out_++,(cell_)>>8); \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        KW8(sgx_,out_++,0x8B);KW8(sgx_,out_++,0x46);KW8(sgx_,out_++,(far_)?6+2*j_:4+2*j_); \
        KW8(sgx_,out_++,0x8B);KW8(sgx_,out_++,0x1E);KW8(sgx_,out_++,(cell_));KW8(sgx_,out_++,(cell_)>>8); \
        KW8(sgx_,out_++,0xD1);KW8(sgx_,out_++,0xC3);KW8(sgx_,out_++,0x33);KW8(sgx_,out_++,0xD8); \
        KW8(sgx_,out_++,0x89);KW8(sgx_,out_++,0x1E);KW8(sgx_,out_++,(cell_));KW8(sgx_,out_++,(cell_)>>8); } \
    KW8(sgx_,out_++,0x5B);KW8(sgx_,out_++,0xA1);KW8(sgx_,out_++,(retcell_));KW8(sgx_,out_++,(retcell_)>>8); \
    KW8(sgx_,out_++,0x5D);KW8(sgx_,out_++,(far_)?0xCB:0xC3); } while(0)
                KSTUB(0x1452,0x03AB,0xF810,4,0xF830,1);
                KSTUB(0x120A,0x00CF,0xF812,6,0xF832,1);
                {
                    static const struct { uint16_t slot,code,cell,ret; unsigned args; } near_[4]={
                        {0x1AE9,0xF200,0xF814,0xF834,4},{0xE123,0xF260,0xF816,0xF836,0},
                        {0xE289,0xF2C0,0xF818,0xF838,0},{0x1B2E,0xF320,0xF81A,0xF83A,4}};
                    for(unsigned k=0;k<4;k++) {
                        KW8(0,near_[k].slot,0xE9);KW16(0,near_[k].slot+1,(uint16_t)(near_[k].code-near_[k].slot-3));
                        KSTUB(0,near_[k].code,near_[k].cell,near_[k].args,near_[k].ret,0);
                    }
                }
#undef KSTUB
#undef KRAND
#undef KW16
#undef KW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0x04E6) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* Arguments: heading, pitch, then three (low, high) position longs.
                 * The pitch straddles D555h; small positions keep 810's result in
                 * range for the last ring check. */
                uint16_t arg[8];
                arg[0]=(uint16_t)XRAND();
                arg[1]=(s&1)?(uint16_t)(0xD555+(int)(XRAND()%5)-2):(uint16_t)XRAND();
                for(unsigned j=0;j<3;j++) {
                    arg[2+2*j]=(XRAND()&3)?(uint16_t)(XRAND()&0x3FFF):(uint16_t)XRAND();
                    arg[3+2*j]=(XRAND()%8==0)?(uint16_t)(XRAND()&3):0;
                }
                if(XRAND()%4==0)arg[6]=(uint16_t)(XRAND()&0x5F);
                else if(XRAND()%8==0)arg[6]=(XRAND()&1)?0x7FFE:0x7FFF;
                for(unsigned j=0;j<8;j++)XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),arg[j]);
                for(unsigned j=0;j<4;j++)small[j]=arg[j];
                XW16(dg,0x990E,XRAND()%4);
                XW16(dg,0x0348,(XRAND()%7<3)?0:(uint16_t)(1+XRAND()%3));
                for(unsigned j=0;j<5;j++)XW16(dg,0x04FA+2*j,(XRAND()%8==0)?0:(uint16_t)(1+XRAND()%9));
                for(unsigned j=0;j<0x100;j++)XW8(dg,0x034C+j,XRAND());
                for(unsigned j=0;j<0x20;j++)XW8(dg,0x04CC+j,XRAND());
                for(unsigned j=0;j<0x30;j++)XW8(dg,0x04DE + j,XRAND());
                for(unsigned j=0;j<0x100;j++)XW8(dg,0x0510+j,XRAND());
                XW16(dg,0x98A4,XRAND());XW16(dg,0x950A,XRAND());
                /* Tile lists: ring * 32 + tile selects a pointer to up to six
                 * seven-byte entries, each with its own type byte. */
                for(unsigned slot=0;slot<5*32;slot++) {
                    const uint16_t list=(uint16_t)(0x2000+0x40*(slot&15));
                    XW16(dg,0xE330+2*slot,list);
                    XW16(dg,0x061E + 2*slot,(XRAND()%6==0)?0:(uint16_t)(1+XRAND()%5));
                }
                for(unsigned k=0;k<16;k++)
                    for(unsigned e=0;e<6;e++) {
                        const uint16_t at=(uint16_t)(0x2000+0x40*k+7*e);
                        XW16(dg,at,XRAND());XW16(dg,at+2,XRAND());XW16(dg,at+4,XRAND());
                        XW8(dg,at+6,(uint8_t)(((XRAND()%3==0)?((1+XRAND()%3)|(XRAND()&0x40)):(XRAND()&0x7F))|((XRAND()&1)?0x80:0)));
                    }
                /* Return lists for the decision stubs: E3AC culls, 0871 finds a tile
                 * (-1 none), 0FBD says whether an entry repeats the last hit. */
                for(unsigned j=0;j<32;j++) {
                    XW16(dg,0xF400+2*j,(XRAND()%3==0)?1:0);
                    XW16(dg,0xF440+2*j,(XRAND()%5==0)?0xFFFF:(uint16_t)(XRAND()%32));
                    XW16(dg,0xF480+2*j,(XRAND()%2)?(uint16_t)(1+XRAND()%3):0);
                    XW16(dg,0xF4C0+2*j,XRAND());
                }
                for(unsigned j=0;j<16;j++)XW16(dg,0xF800+2*j,0);
                /* Returning probes: fold every argument in order into a guest cell,
                 * count the call, return the next word of a list. D9E7, C436, 810 and
                 * F018 are arithmetic or setters and stay original. */
#define XSTUB(off_,cell_,nargs_,list_) do { unsigned out_=(off_);const uint16_t sgx_=0; \
    XW8(sgx_,out_++,0x55);XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0xEC);XW8(sgx_,out_++,0x53); \
    XW8(sgx_,out_++,0xFF);XW8(sgx_,out_++,0x06);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x46);XW8(sgx_,out_++,4+2*j_); \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); \
        XW8(sgx_,out_++,0xD1);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x33);XW8(sgx_,out_++,0xD8); \
        XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); } \
    XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+2);XW8(sgx_,out_++,((cell_)+2)>>8); \
    XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x87);XW8(sgx_,out_++,(list_));XW8(sgx_,out_++,(list_)>>8); \
    XW8(sgx_,out_++,0x83);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x02); \
    XW8(sgx_,out_++,0x83);XW8(sgx_,out_++,0xE3);XW8(sgx_,out_++,0x3E); \
    XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+2);XW8(sgx_,out_++,((cell_)+2)>>8); \
    XW8(sgx_,out_++,0x5B);XW8(sgx_,out_++,0x5D);XW8(sgx_,out_++,0xC3); } while(0)
                {
                    static const struct { uint16_t slot,code,cell,list; unsigned args; } stub_[4]={
                        {0xE3AC,0xF200,0xF800,0xF400,3},{0x0871,0xF260,0xF804,0xF440,3},
                        {0x0FBD,0xF2C0,0xF808,0xF480,4},{0xE2B6,0xF320,0xF80C,0xF4C0,7}};
                    for(unsigned k=0;k<4;k++) {
                        XW8(0,stub_[k].slot,0xE9);XW16(0,stub_[k].slot+1,(uint16_t)(stub_[k].code-stub_[k].slot-3));
                        XSTUB(stub_[k].code,stub_[k].cell,stub_[k].args,stub_[k].list);
                    }
                }
#undef XSTUB
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"START.EXE") && o->seg==0 && o->ip==0x0FA3) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The routine takes no arguments; the words above SP are noise,
                 * and the harness's small[] copies of them match. */
                for(unsigned j=0;j<8;j++) {
                    const uint16_t w=(uint16_t)XRAND();
                    XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w);
                    if(j<4)small[j]=w;
                }
                /* [6D21] is the number of row events; [B2E6] the number of listed
                 * rows. Both straddle the interesting values. */
                unsigned nrows;
                switch(XRAND()%8) {
                    case 0: nrows=0; break;
                    case 1: case 2: case 3: nrows=10+XRAND()%6; break;
                    case 4: nrows=1+XRAND()%9; break;
                    case 5: nrows=XRAND()&0xFF; break;
                    case 6: nrows=(XRAND()&1)?0x7F:0x80; break;
                    default: nrows=0xFF; break;
                }
                XW8(dg,0x6D21,nrows);XW16(dg,0x6D22,XRAND());
                unsigned lrows;
                switch(XRAND()%8) {
                    case 0: lrows=0; break;
                    case 1: lrows=1; break;
                    case 2: case 3: case 4: case 5: lrows=2+XRAND()%6; break;
                    case 6: lrows=XRAND()%24; break;
                    default: lrows=1+XRAND()%3; break;
                }
                /* The row tables and the records they name (CBDEh + 16 * signed
                 * byte); word 6 of a record is zero about half the time. */
                for(unsigned j=0;j<0x240;j++)XW8(dg,0xB274+j,XRAND());
                for(unsigned j=0;j<0x1010;j++)XW8(dg,0xC3DE + j,XRAND());
                for(unsigned k=0;k<256;k++) {
                    if(XRAND()%2)XW16(dg,0xC3DE + 16*k+6,0);
                    else XW16(dg,0xC3DE + 16*k+6,(uint16_t)(1+XRAND()%0xFFFF));
                }
                XW16(dg,0xB2E6,lrows);
                XW8(dg,0xB284,XRAND());
                {
                    uint8_t pane;
                    switch(XRAND()%4) {
                        case 0: pane=0; break;
                        case 1: pane=(uint8_t)(1+XRAND()%3); break;
                        case 2: pane=1; break;
                        default: pane=(uint8_t)XRAND(); break;
                    }
                    XW8(dg,0xB2E8,pane);
                    switch(XRAND()%5) {
                        case 0: XW8(dg,0xB285,0); break;
                        case 1: XW8(dg,0xB285,1+XRAND()%3); break;
                        case 2: XW8(dg,0xB285,pane); break;
                        case 3: XW8(dg,0xB285,(XRAND()&1)?0x80:0xFF); break;
                        default: XW8(dg,0xB285,XRAND()); break;
                    }
                }
                XW8(dg,0xD2A2,XRAND());
                /* The poll's script (F400): up to 14 events, then an ending event.
                 * Rows come from near [6D21], pane keys, the specials and noise. */
                {
                    static const uint16_t odd[]={0x62,0x64,0x11F,0x121,0xFFFF,0x8000,0x7FFF,0xFFF7,0x00FF,0x0100,0x0009,0x000A};
                    unsigned nk=(XRAND()%4==0)?(XRAND()%2):(XRAND()%14);
                    for(unsigned j=0;j<32;j++) {
                        uint16_t key;
                        if(j==nk)key=(XRAND()&1)?7:0x63;
                        else switch(XRAND()%16) {
                            case 0: case 1: case 2: case 3: case 4: case 5: key=(uint16_t)(1+XRAND()%8); break;
                            case 6: case 7: key=0x120; break;
                            case 8: key=0; break;
                            case 9: case 10: key=(uint16_t)(9+XRAND()%(nrows>9?nrows-9:3)); break;
                            case 11: key=(uint16_t)(nrows-2+XRAND()%4); break;
                            case 12: key=odd[XRAND()%(sizeof odd/sizeof odd[0])]; break;
                            case 13: key=(uint16_t)(XRAND()%0x140); break;
                            default: key=(uint16_t)XRAND(); break;
                        }
                        XW16(dg,0xF400+2*j,key);
                    }
                }
                /* 393D's replies: a code in AL around 8, noise in AH. */
                for(unsigned j=0;j<32;j++) {
                    uint16_t v;
                    switch(XRAND()%4) {
                        case 0: v=8; break;
                        case 1: v=9; break;
                        default: v=(uint16_t)(XRAND()%16); break;
                    }
                    XW16(dg,0xF440+2*j,(XRAND()&1)?(uint16_t)(v|(XRAND()&0xFF00)):v);
                }
                /* The other callees return noise. */
                for(unsigned j=0;j<8*32;j++)XW16(dg,0xF480+2*j,XRAND());
                for(unsigned j=0;j<0x20;j++)XW16(dg,0xF800+2*j,0);
                /* Returning probes: fold every argument in order into a guest cell,
                 * count the call, return the next word of a list. Every callee of the
                 * loop draws, waits, polls or redraws, so each is a probe. */
#define XSTUB(off_,cell_,nargs_,list_) do { unsigned out_=(off_);const uint16_t sgx_=0; \
    XW8(sgx_,out_++,0x55);XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0xEC);XW8(sgx_,out_++,0x53); \
    XW8(sgx_,out_++,0xFF);XW8(sgx_,out_++,0x06);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x46);XW8(sgx_,out_++,4+2*j_); \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); \
        XW8(sgx_,out_++,0xD1);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x33);XW8(sgx_,out_++,0xD8); \
        XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); } \
    XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+2);XW8(sgx_,out_++,((cell_)+2)>>8); \
    XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x87);XW8(sgx_,out_++,(list_));XW8(sgx_,out_++,(list_)>>8); \
    XW8(sgx_,out_++,0x83);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x02); \
    XW8(sgx_,out_++,0x83);XW8(sgx_,out_++,0xE3);XW8(sgx_,out_++,0x3E); \
    XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+2);XW8(sgx_,out_++,((cell_)+2)>>8); \
    XW8(sgx_,out_++,0x5B);XW8(sgx_,out_++,0x5D);XW8(sgx_,out_++,0xC3); } while(0)
                {
                    static const struct { uint16_t slot,code,cell,list; unsigned args; } stub_[10]={
                        {0x0F4E,0xF200,0xF800,0xF480,0},{0x38AC,0xF260,0xF804,0xF400,1},
                        {0x3588,0xF2C0,0xF808,0xF4C0,0},{0x141D,0xF320,0xF80C,0xF500,1},
                        {0x3517,0xF380,0xF810,0xF540,3},{0x20D6,0xF3E0,0xF814,0xF580,0},
                        {0x3989,0xF440,0xF818,0xF5C0,0},{0x1308,0xF4A0,0xF81C,0xF600,0},
                        {0x393D,0xF500,0xF820,0xF440,2},{0x1C2F,0xF560,0xF824,0xF640,1}};
                    for(unsigned k=0;k<10;k++) {
                        XW8(0,stub_[k].slot,0xE9);XW16(0,stub_[k].slot+1,(uint16_t)(stub_[k].code-stub_[k].slot-3));
                        XSTUB(stub_[k].code,stub_[k].cell,stub_[k].args,stub_[k].list);
                    }
                }
#undef XSTUB
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0x63F2) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
#define XR8(o_) (g_mem[0][phys(dg,(uint16_t)(o_))])
#define XR16(o_) ((uint16_t)(XR8(o_)|(XR8((o_)+1)<<8)))
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
#define XFILL(lo_,hi_) do { for(unsigned a_=(lo_);a_<(hi_);a_++)XW8(dg,a_,XRAND()); } while(0)
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* Random data under every table the routine and its original
                 * callees (56A9, 56BB, 4ABA, 8A4F, the string copies) read. */
                XFILL(0x2DE0,0x2E00);XFILL(0x2EA0,0x3E80);XFILL(0x9500,0x9540);XFILL(0x98A0,0x98E0);
                XFILL(0x9900,0x9930);XFILL(0xB060,0xB0A0);XFILL(0xB2A0,0xB300);XFILL(0xC060,0xC4E0);
                XFILL(0xDEF0,0xDF10);XFILL(0xE560,0xE590);
                /* The argument: a slot index (1 in 8 beyond the first sixteen). */
                const unsigned idx=(XPICK(8)==0)?16+XPICK(8):XPICK(16);
                const uint16_t rec=(uint16_t)(0xC16C+36*idx);
                const unsigned slot=idx&7;
                XW16(seg[S_SS],r[R_SP],idx);small[0]=(uint16_t)idx;
                /* Weapon: a name (terminated) and a kind per entry. */
                const unsigned wk=XPICK(8);
                XW16(dg,0x9520,wk);
                for(unsigned e=0;e<8;e++) {
                    const unsigned n=1+XPICK(7);
                    for(unsigned j=0;j<n;j++)XW8(dg,0x2FF4+32*e+j,'A'+XPICK(26));
                    XW8(dg,0x2FF4+32*e+n,0);
                    XW16(dg,0x300C+32*e,(XPICK(8)==0)?XPICK(16):XPICK(8));
                }
                const unsigned kind=XR16(0x300C+32*wk);
                const unsigned kind_flags=XR8(0x2EBA+14*kind+4);
                /* Missile types: a name each, a rate whose >> 6 is never zero
                 * (it divides). */
                for(unsigned t=0;t<16;t++) {
                    const unsigned n=1+XPICK(6);
                    for(unsigned j=0;j<n;j++)XW8(dg,0x3394+18*t+j,'a'+XPICK(26));
                    XW8(dg,0x3394+18*t+n,0);
                    uint16_t rate=(uint16_t)XRAND();
                    if(rate<64)rate=(uint16_t)(rate+64);
                    XW16(dg,0x339E + 18*t,rate);
                }
                for(unsigned j=0;j<2;j++) {
                    const unsigned n=1+XPICK(5);
                    for(unsigned k2=0;k2<n;k2++)XW8(dg,(j?0x3E52:0x3E45)+k2,'A'+XPICK(26));
                    XW8(dg,(j?0x3E52:0x3E45)+n,0);
                }
                /* The record: flag bytes with bit 3 either way, a type (0 in
                 * 1 of 7), a free or busy missile slot. */
                {
                    uint8_t f1=(uint8_t)(XRAND()&0xF7),f2=(uint8_t)(XRAND()&0xF7);
                    if(XPICK(2))f1|=8;
                    if(XPICK(2))f2|=8;
                    XW8(dg,rec+0x16,f1);XW8(dg,rec+0x17,f2);
                    XW16(dg,rec+0x1C,(XPICK(7)==0)?0:1+XPICK(15));
                    XW16(dg,0x3C48+0x1C*slot,(XPICK(5)<2)?0:1+XPICK(0xFFFE));
                }
                const unsigned type=XR16(rec+0x1C);
                const uint16_t heading=XR16(rec+0x0E);
                /* 3686, 3688, 368A, 368E, B080, 9B34 and the counters. */
                XW16(dg,0x3686,(XPICK(8)==0)?(uint16_t)XRAND():XPICK(6));
                XW16(dg,0x3688,(XPICK(8)==0)?(uint16_t)XRAND():XPICK(6));
                {
                    const unsigned sel=XPICK(10);
                    static const uint8_t cls[6]={0,1,2,3,4,5};
                    XW8(dg,0x368A,sel<6?cls[sel]:(sel==6?0x1F:(sel==7?0x20:(uint8_t)XRAND())));
                }
                XW16(dg,0x368E,(XPICK(6)==0)?(uint16_t)XRAND():1+XPICK(0x40));
                const uint16_t q=XR16(0x3686);
                {
                    const unsigned sel=XPICK(4);
                    const uint16_t base=(uint16_t)(2*q);
                    XW16(dg,0xB080,sel<3?(uint16_t)(base+(int)sel-1):(uint16_t)XRAND());
                }
                XW8(dg,0x9B34,XRAND());
                XW16(dg,0xDF02,(XPICK(8)==0)?0xFFFF:XRAND());
                XW16(dg,0xDEFA,(XPICK(8)==0)?0xFFFF:XRAND());
                {
                    static const uint16_t c951a[8]={0,1,2,5,0xFE,0xFF,0x100,0xFFFF};
                    XW16(dg,0x951A,(XPICK(3)==0)?(uint16_t)XRAND():c951a[XPICK(8)]);
                }
                XW16(dg,0xE56C,XPICK(4));
                XW16(dg,0x39E0,XPICK(2)?(uint16_t)(XRAND()|1):0);
                for(unsigned j=0;j<4;j++)XW16(dg,0xB2D4+16*j,XPICK(2)?(uint16_t)XRAND():0);
                {
                    const unsigned off=(XPICK(8)==0)?0xFFFFu-XPICK(0x40):0x2A00+XPICK(0x100);
                    XW16(dg,0xE574,off);XW16(dg,0xE576,XPICK(2)?dg:(uint16_t)(dg+0x10));
                }
                /* The measurement 5582 reports: strength, range, bearing. */
                uint16_t strength;
                switch(XPICK(10)) {
                case 0: strength=0; break;
                case 1: strength=0xFFFF; break;
                case 2: strength=0x8000; break;
                case 3: strength=(uint16_t)(0x8001+XPICK(0x7FFE)); break;
                case 4: case 5: case 6: strength=(uint16_t)(1+XPICK(0x70)); break;
                case 7: strength=0x7FFF; break;
                case 8: strength=(uint16_t)(1+XPICK(0x7FFF)); break;
                default: strength=(uint16_t)(0x62+XPICK(5)); break;
                }
                uint16_t range;
                {
                    const unsigned cl=XR8(0x368A)&0x1F;
                    const uint16_t a8=(uint16_t)(cl>3?0:8>>cl);
                    const uint16_t b=(uint16_t)(2*(4-q));
                    switch(XPICK(10)) {
                    case 0: case 1: range=(uint16_t)(a8+(int)XPICK(3)-1); break;
                    case 2: case 3: range=(uint16_t)(b+(int)XPICK(3)-1); break;
                    case 4: range=0; break;
                    case 5: range=0xFFFF; break;
                    case 6: range=(uint16_t)XPICK(16); break;
                    case 7: range=(uint16_t)XPICK(0x40); break;
                    default: range=(uint16_t)XRAND(); break;
                    }
                }
                uint16_t bearing;
                {
                    int delta;
                    switch(XPICK(12)) {
                    case 0: case 1: { static const int d[4]={0x30FF,0x3100,-0x30FF,-0x3100}; delta=d[XPICK(4)]; } break;
                    case 2: { static const int d[4]={0x17FF,0x1800,-0x17FF,-0x1800}; delta=d[XPICK(4)]; } break;
                    case 3: delta=0x8000; break;
                    case 4: case 5: delta=(int)XPICK(0x100)-0x80; break;
                    case 10: delta=XPICK(2)?0x7FFF:-0x7FFF; break;
                    case 11: delta=0; break;
                    default: delta=(int)(int16_t)XRAND(); break;
                    }
                    bearing=(uint16_t)(heading+delta);
                }
                /* What the routine itself will do with them (to place the
                 * boundary values): the distance of the bearing from the
                 * heading may zero the strength. */
                uint16_t eff=strength;
                {
                    const uint16_t diff=(uint16_t)(bearing-heading);
                    const uint16_t ab=((int16_t)diff<0)?(uint16_t)(-(int16_t)diff):diff;
                    if((int16_t)strength>0 && !(kind_flags&2) && (int8_t)(ab>>8)>0x30)eff=0;
                }
                {
                    static const uint16_t tgt[5]={0x63,0x64,0x65,0x62,0x66};
                    const unsigned sel=XPICK(10);
                    uint16_t d3d8a;
                    if(sel<4)d3d8a=(uint16_t)(tgt[XPICK(5)]-eff);
                    else if(sel<6)d3d8a=(uint16_t)(tgt[XPICK(5)]-strength);
                    else if(sel==6)d3d8a=tgt[XPICK(5)];
                    else if(sel==7)d3d8a=(uint16_t)XPICK(0x80);
                    else d3d8a=(uint16_t)XRAND();
                    XW16(dg,0x3D8A,d3d8a);
                }
                /* The lock level, placed so level + gain (or - 20h) lands on or
                 * beside C0h, FFh, 0 and the clamp's C000h. */
                {
                    const uint16_t d3d8a=XR16(0x3D8A);
                    const int rise=(int16_t)(uint16_t)(eff+d3d8a)>0x64;
                    uint16_t t=(uint16_t)(XR16(0x3688)+q+2);
                    t=(uint16_t)(t<<4);
                    const unsigned cl=(XR8(0x9B34)&0x10)?1:0;
                    const uint16_t gain=(uint16_t)((int16_t)t>>cl);
                    static const uint16_t vt[12]={0xC0,0xC1,0xBF,0xC0,0xC1,0xFF,0x100,0xFFFF,0,0xC000,0xC001,0xBFFF};
                    uint16_t level;
                    if(XPICK(10)<6) {
                        const uint16_t v=vt[XPICK(12)];
                        level=rise?(uint16_t)(v-gain):(uint16_t)(v+0x20);
                    } else level=XPICK(3)?(uint16_t)XPICK(0x120):(uint16_t)XRAND();
                    XW16(dg,rec+0x20,level);
                }
                /* The type's stock against the range. */
                if(XPICK(5)<3)XW16(dg,0x339C+18*type,(uint16_t)(range+(int)XPICK(3)-1));
                /* Callees that draw, request sound or measure are replaced by
                 * probes: each folds its arguments, in order, into a guest cell,
                 * counts the call and returns a fixture value; 5582 also stores
                 * the range and bearing through its two pointers. EF36, EE9C,
                 * EE0C, EF68, C67A, 56A9, 56BB, 4ABA, 8A4F and the string copies
                 * stay original. */
                XW16(dg,0xF800,0);XW16(dg,0xF802,0);XW16(dg,0xF820,0);XW16(dg,0xF830,0);
                XW16(dg,0xF810,range);XW16(dg,0xF812,bearing);XW16(dg,0xF814,strength);
                XW16(dg,0xF822,XRAND());XW16(dg,0xF832,XRAND());
#define XSTUB(sg_,off_,cell_,nargs_,far_,ptrs_,v10_,va_,ret_) do { \
    uint8_t cb_[160]; unsigned n_=0; \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53;cb_[n_++]=0x57; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=(cell_)&255;cb_[n_++]=(cell_)>>8; \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=(cell_)&255;cb_[n_++]=(cell_)>>8; \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
        cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=(cell_)&255;cb_[n_++]=(cell_)>>8; } \
    if(ptrs_) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x7E;cb_[n_++]=0x0E; \
        cb_[n_++]=0xA1;cb_[n_++]=(v10_)&255;cb_[n_++]=(v10_)>>8; \
        cb_[n_++]=0x36;cb_[n_++]=0x89;cb_[n_++]=0x05; \
        cb_[n_++]=0x8B;cb_[n_++]=0x7E;cb_[n_++]=0x0C; \
        cb_[n_++]=0xA1;cb_[n_++]=(va_)&255;cb_[n_++]=(va_)>>8; \
        cb_[n_++]=0x36;cb_[n_++]=0x89;cb_[n_++]=0x05; } \
    cb_[n_++]=0xA1;cb_[n_++]=(ret_)&255;cb_[n_++]=(ret_)>>8; \
    cb_[n_++]=0x5F;cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k_=0;k_<n_;k_++)XW8((sg_),(off_)+k_,cb_[k_]); } while(0)
                XW8(0,0x5582,0xE9);XW16(0,0x5583,(uint16_t)(0xF200-0x5582-3));
                XSTUB(0,0xF200,0xF800,6,0,1,0xF810,0xF812,0xF814);
                XW8(0,0xD3F9,0xE9);XW16(0,0xD3FA,(uint16_t)(0xF300-0xD3F9-3));
                XSTUB(0,0xF300,0xF820,2,0,0,0,0,0xF822);
                XSTUB(0x114A,0x033C,0xF830,4,1,0,0,0,0xF832);
#undef XSTUB
#undef XFILL
#undef XPICK
#undef XRAND
#undef XR16
#undef XR8
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0x528D) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define RND(n_) ((unsigned)(XRAND()%(unsigned)(n_)))
#define PCT(p_) (RND(100)<(unsigned)(p_))
#define PICK(a_) ((a_)[RND(sizeof(a_)/sizeof((a_)[0]))])
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* Object records: two banks of thirty-two (an index of 100h and up
                 * reaches the second), random but for the chosen one. */
                for(unsigned j=0;j<0x200;j++) { XW8(dg,0xB2D0+j,XRAND());XW8(dg,0xC2D0+j,XRAND()); }
                const unsigned t_k=RND(32);
                const uint16_t t_n=PCT(25)?(uint16_t)(0x100+t_k):(uint16_t)t_k;
                const uint16_t t_rec=(uint16_t)(0xB2D0+16*t_n);
                const unsigned t_slot=t_k&7;
                /* Type: 15h is the one the engagement test excludes. */
                uint16_t t_type;
                { const unsigned q=RND(100); t_type=(q<25)?0x15:(q<35)?(uint16_t)(0x14+2*RND(2)):(uint16_t)RND(0x20); }
                /* Visibility (the return of 5582, stubbed), the exposure sum
                 * around the 32h and 64h thresholds, and the alert level. */
                uint16_t t_size;
                { static const uint16_t b_[]={1,0x62,0x63,0x64,0x65};
                  const unsigned q=RND(100);
                  if(q<8)t_size=0; else if(q<14)t_size=(uint16_t)(0x8000|XRAND()); else if(q<20)t_size=0xFFFF;
                  else if(q<45)t_size=PICK(b_); else if(q<60)t_size=0x7FFF; else t_size=(uint16_t)(1+RND(0x70)); }
                const int t_bright=PCT(65);
                uint16_t t_sum;
                if(t_bright) { static const uint16_t b_[]={0x65,0x66,0x65,0x7FFF,0x1000};
                    t_sum=PCT(50)?PICK(b_):(uint16_t)(0x65+RND(0x200)); }
                else { static const uint16_t b_[]={0x64,0x63,0x33,0x32,0x31,0x30,0,0x8000,0xFFFF};
                    t_sum=PCT(65)?PICK(b_):(uint16_t)(int16_t)(-0x40+(int)RND(0x70)); }
                XW16(dg,0x3D8A,(uint16_t)(t_sum-t_size));
                const uint16_t t_a=PCT(85)?(uint16_t)RND(0x30):(uint16_t)XRAND();
                const uint16_t t_b=PCT(85)?(uint16_t)RND(0x30):(uint16_t)XRAND();
                const int t_bit4=PCT(50);
                const uint16_t t_inc=(uint16_t)((int16_t)(uint16_t)((uint16_t)(t_a+t_b+1)<<4)>>(t_bit4?1:0));
                XW16(dg,0x3688,t_a);XW16(dg,0x3686,t_b);
                XW8(dg,0x9B34,(uint8_t)((XRAND()&0xEF)|(t_bit4?0x10:0)));
                uint16_t t_level;
                if(t_bright) {
                    static const uint16_t f_[]={0x7E,0x7F,0x80,0xBF,0xC0,0xC1,0xF9,0xFA,0xFB,0xFE,0xFF,0x100,0x101,0x200,0x7FFF,0x8000,0xFFFF};
                    const unsigned q=RND(100);
                    if(q<10)t_level=0; else if(q<14)t_level=(uint16_t)XRAND();
                    else t_level=(uint16_t)((PCT(75)?PICK(f_):(uint16_t)RND(0x100))-t_inc);
                } else {
                    static const uint16_t l_[]={0,1,0xF,0x10,0x11,0x7FFF,0x8000,0x8010,0xFFFF,0x7F,0x80,0xC0,0xFA};
                    t_level=PCT(80)?PICK(l_):(uint16_t)RND(0x100);
                }
                /* The record: type, flags (+6 bit 2 seen, bit 4 engaged; +7 bit 0
                 * skips the cover, bit 4 warned) and level. */
                { uint8_t f6=(uint8_t)XRAND(),f7=(uint8_t)XRAND();
                  f6=PCT(25)?(uint8_t)(f6|0x04):(uint8_t)(f6&~0x04);
                  f7=PCT(30)?(uint8_t)(f7|0x01):(uint8_t)(f7&~0x01);
                  f7=PCT(40)?(uint8_t)(f7|0x10):(uint8_t)(f7&~0x10);
                  XW16(dg,t_rec+4,t_type);XW8(dg,t_rec+6,f6);XW8(dg,t_rec+7,f7);XW16(dg,t_rec+8,t_level); }
                /* Argument: the object index. */
                for(unsigned j=0;j<8;j++)XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),(j==0)?t_n:(uint16_t)XRAND());
                small[0]=t_n;
                for(unsigned j=1;j<4;j++)small[j]=(uint16_t)XRAND();
                /* Flags and limits read on the way. */
                XW16(dg,0x368C,PCT(75)?(uint16_t)(1+RND(5)):0);
                XW16(dg,0xDF06,PCT(25)?(uint16_t)(1+RND(3)):0);
                { static const uint16_t e_[]={0x13,0x15,0,0xFFFF};
                  XW16(dg,0xE008,PCT(30)?0x14:(PCT(50)?PICK(e_):(uint16_t)XRAND())); }
                XW16(dg,0x39E0,PCT(40)?(uint16_t)(1+RND(3)):0);
                XW16(dg,0x368E,(uint16_t)XRAND());
                XW16(dg,0xB080,PCT(5)?(uint16_t)XRAND():PCT(70)?(uint16_t)(t_b-RND(3)):(uint16_t)(t_b+1+RND(3)));
                { static const uint16_t g_[]={0x4FF,0x500,0x501,0x502,0x7FFF,0x8000,0xFFFF,0x900,0x1000};
                  XW16(dg,0x9922,PCT(20)?(uint16_t)XRAND():PICK(g_)); }
                { static const uint16_t c_[]={0,0xFFFF,0x7FFF,0x7FFE,0x8000};
                  XW16(dg,0xDF02,PCT(40)?PICK(c_):(uint16_t)XRAND());XW16(dg,0xDEFE,PCT(40)?PICK(c_):(uint16_t)XRAND()); }
                /* Outputs start random, so a write the body skipped shows. */
                XW16(dg,0xC09C,XRAND());XW16(dg,0xC0AA,XRAND());XW16(dg,0xC166,XRAND());XW16(dg,0xC168,XRAND());
                XW16(dg,0xE578,XRAND());XW16(dg,0x98A2,XRAND());XW16(dg,0x3D98,XRAND());XW16(dg,0x3D9A,XRAND());
                /* Type tables: [2EBA + 14 * type] (flag byte +4), reach [339C + 18 * type]. */
                for(unsigned j=0;j<0x1C0;j++)XW8(dg,0x2EBA+j,XRAND());
                static const uint16_t t_reach[]={0,0x40,0x100,0x7FFF,0x8000,0xFFFF};
                for(unsigned j=0;j<0x240;j++)XW8(dg,0x339C+j,XRAND());
                for(unsigned j=0;j<32;j++)XW16(dg,0x339C+18*j,PCT(15)?(uint16_t)XRAND():PICK(t_reach));
                /* Shot slots (28 bytes at [3C3A]); the chosen one's word +0Eh. */
                for(unsigned j=0;j<0x120;j++)XW8(dg,0x3C3A+j,XRAND());
                XW16(dg,0x3C48+28*t_slot,PCT(65)?0:(uint16_t)(1+RND(0xFFFF)));
                /* The far record behind [E574]. */
                { const uint16_t es_seg=PCT(50)?dg:0x7000,es_off=(uint16_t)(0x2400+0x40*RND(4));
                  for(unsigned j=0;j<0x60;j++)XW8(es_seg,(uint16_t)(es_off+j),XRAND());
                  XW16(dg,0xE574,es_off);XW16(dg,0xE576,es_seg); }
                /* The probe return: range and bearing through the pointers, then the
                 * visibility. Range from the reach classes (equal sometimes). */
                { static const uint16_t br_[]={0x4000,0x7F00,0x8000,0x0000,0x80FF,0xFF00,0x0100};
                  uint16_t t_dist=PCT(40)?PICK(t_reach):(uint16_t)XRAND();
                  if(PCT(15))t_dist=(uint16_t)(t_dist+1-2*RND(2));
                  XW16(dg,0xF820,t_size);XW16(dg,0xF822,t_dist);
                  XW16(dg,0xF824,PCT(40)?(uint16_t)(PICK(br_)+RND(0x100)):(uint16_t)XRAND()); }
                for(unsigned j=0;j<16;j++)XW16(dg,0xF800+2*j,0);
                /* Probes: fold every argument in order into a guest cell and count the
                 * call; 5582's also writes the range and bearing through its pointers
                 * (stack segment) and returns the visibility. The long helpers EF36,
                 * EF68 and EE9C stay original. */
#define XSTUB(sg_,off_,cell_,nargs_,retcell_,outs_,far_) do { unsigned out_=(off_);const uint16_t sgx_=(sg_); \
    XW8(sgx_,out_++,0x55);XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0xEC);XW8(sgx_,out_++,0x53); \
    XW8(sgx_,out_++,0xFF);XW8(sgx_,out_++,0x06);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x46);XW8(sgx_,out_++,(far_)?6+2*j_:4+2*j_); \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); \
        XW8(sgx_,out_++,0xD1);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x33);XW8(sgx_,out_++,0xD8); \
        XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); } \
    if(outs_) { \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x5E);XW8(sgx_,out_++,0x0C); \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x0E);XW8(sgx_,out_++,(retcell_)+4);XW8(sgx_,out_++,((retcell_)+4)>>8); \
        XW8(sgx_,out_++,0x36);XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x0F); \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x5E);XW8(sgx_,out_++,0x0E); \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x0E);XW8(sgx_,out_++,(retcell_)+2);XW8(sgx_,out_++,((retcell_)+2)>>8); \
        XW8(sgx_,out_++,0x36);XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x0F); } \
    XW8(sgx_,out_++,0x5B); \
    if(retcell_) { XW8(sgx_,out_++,0xA1);XW8(sgx_,out_++,(retcell_));XW8(sgx_,out_++,(retcell_)>>8); } \
    XW8(sgx_,out_++,0x5D);XW8(sgx_,out_++,(far_)?0xCB:0xC3); } while(0)
                XSTUB(0x114A,0x033C,0xF818,4,0,0,1);
                {
                    static const struct { uint16_t slot,code,cell; unsigned args; uint16_t ret; unsigned outs; } near_[12]={
                        {0x5582,0xF200,0xF800,6,0xF820,1},{0x94E1,0xF300,0xF802,0,0,0},{0x8625,0xF360,0xF804,4,0,0},
                        {0x94AD,0xF3C0,0xF806,0,0,0},{0x9216,0xF420,0xF808,0,0,0},{0x4B03,0xF480,0xF80A,1,0,0},
                        {0xEB10,0xF4E0,0xF80C,2,0,0},{0x8A4F,0xF540,0xF80E,1,0,0},{0x56A9,0xF5A0,0xF810,0,0,0},
                        {0x56BB,0xF600,0xF812,0,0,0},{0xD3F9,0xF660,0xF814,2,0,0},{0x4ABA,0xF6C0,0xF816,2,0,0}};
                    for(unsigned k=0;k<12;k++) {
                        XW8(0,near_[k].slot,0xE9);XW16(0,near_[k].slot+1,(uint16_t)(near_[k].code-near_[k].slot-3));
                        XSTUB(0,near_[k].code,near_[k].cell,near_[k].args,near_[k].ret,near_[k].outs,0);
                    }
                }
#undef XSTUB
#undef PICK
#undef PCT
#undef RND
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"START.EXE") && o->seg==0 && o->ip==0x6861) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The arguments: the slot and the row's y. A slot is mostly 0..9
                 * (the ten pilots), sometimes beyond, negative or anything, but
                 * never one whose record would overlap the stack or the stubs'
                 * cells in the data segment. */
                unsigned slot;
                switch(XPICK(10)) {
                    case 0: slot=10+XPICK(6); break;
                    case 1: slot=(uint16_t)(-1-(int)XPICK(4)); break;
                    case 2: slot=XRAND()&0xFFFF; break;
                    default: slot=XPICK(10); break;
                }
                {
                    uint16_t rec_=(uint16_t)(0xD892+0x50*(int16_t)slot);
                    if(rec_>=0xEDC0 || rec_<0x0040 || (uint16_t)(rec_+0x50)<rec_) slot=XPICK(10);
                }
                const uint16_t rec=(uint16_t)(0xD892+0x50*(int16_t)slot);
                const uint16_t yarg=(XPICK(4)==0)?(uint16_t)XRAND():(uint16_t)(XPICK(0xC8));
                XW16(seg[S_SS],r[R_SP],slot);small[0]=(uint16_t)slot;
                XW16(seg[S_SS],r[R_SP]+2,yarg);small[1]=yarg;
                for(unsigned j=2;j<8;j++) {
                    const uint16_t w=(uint16_t)XRAND();
                    XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w);
                    if(j<4)small[j]=w;
                }
                /* The tables: rank strings' pointers at 1D78h, status strings'
                 * pointers at 0E40h (random words; they are only passed on). */
                for(unsigned j=0;j<0x120;j++)XW8(dg,0x1D78+j,XRAND());
                for(unsigned j=0;j<0x120;j++)XW8(dg,0x0E40+j,XRAND());
                /* The ten records and the one the slot names. */
                for(unsigned j=0;j<0x50*10;j++)XW8(dg,0xD892+j,XRAND());
                for(unsigned j=0;j<0x50;j++)XW8(dg,(uint16_t)(rec+j),XRAND());
                {
                    /* Rank and status index: mostly small, sometimes large so
                     * that the doubled index wraps. */
                    unsigned rk,st;
                    switch(XPICK(6)) {
                        case 0: rk=XPICK(0x90); break;
                        case 1: rk=XRAND()&0xFFFF; break;
                        case 2: rk=0x7FFF+XPICK(2); break;
                        default: rk=XPICK(8); break;
                    }
                    switch(XPICK(6)) {
                        case 0: st=XPICK(0x90); break;
                        case 1: st=XRAND()&0xFFFF; break;
                        case 2: st=0x7FFF+XPICK(2); break;
                        default: st=XPICK(4); break;
                    }
                    XW16(dg,(uint16_t)(rec+0x20),rk);
                    XW16(dg,(uint16_t)(rec+0x4E),st);
                    XW16(dg,(uint16_t)(rec+0x32),XRAND());
                    XW16(dg,(uint16_t)(rec+0x34),XRAND());
                    XW16(dg,(uint16_t)(rec+0x36),XRAND());
                }
                /* The callees' replies. Each stub keeps a count (F802 + 2k), a
                 * list index (F820 + 2k) and folds a number naming it and then
                 * every argument into one shared cell (F800), so an argument,
                 * its order, or the order of the calls shows. */
                for(unsigned j=0;j<0x40;j+=2)XW16(dg,0xF800+j,0);
                for(unsigned k=0;k<7;k++)for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*k+2*j,XRAND());
                /* The widths 0373C returns: six calls, in the order long (E0h),
                 * word (F6h), status (23h centre), then the same again; each
                 * near its boundary half of the time. */
                {
                    static const uint16_t tg[3]={0xE0,0xF6,0x23};
                    for(unsigned n=0;n<6;n++) {
                        const uint16_t t=tg[n%3];
                        uint16_t v;
                        switch(XPICK(10)) {
                            case 0: case 1: case 2: case 3: v=(uint16_t)(t+(int)XPICK(7)-3); break;
                            case 4: v=(uint16_t)XPICK(0x80); break;
                            case 5: v=(uint16_t)XPICK(0x1000); break;
                            case 6: v=(uint16_t)(0x8000+XPICK(0x200)); break;
                            case 7: v=(uint16_t)(0xFFFF-XPICK(0x40)); break;
                            case 8: v=(uint16_t)(t+(int)XPICK(2)); break;
                            default: v=(uint16_t)XRAND(); break;
                        }
                        XW16(dg,0xF400+0x40*4+2*n,v);
                    }
                }
#define XSTUB(sg_,off_,k_,id_,nargs_,far_) do { \
    uint8_t cb_[200]; unsigned n_=0; \
    const unsigned cnt_=0xF802+2*(k_),ptr_=0xF820+2*(k_),list_=0xF400+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(id_)&255;cb_[n_++]=(id_)>>8; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
        cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; } \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x83;cb_[n_++]=0xC3;cb_[n_++]=0x02; \
    cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
                {
                    static const struct { uint16_t entry,code,id; uint8_t args; } nb_[6]={
                        {0x959E,0xF200,0x0A51,4},{0x3567,0xF300,0x1B62,4},{0x9516,0xF400,0x2C73,4},
                        {0x373C,0xF500,0x3D84,2},{0x94FA,0xF600,0x4E95,3},{0x94AC,0xF700,0x5FA6,2}};
                    /* Far callee 0A95:6A8A, then the six near ones (their entries
                     * patched to jump to the stubs in segment 0). */
                    XSTUB(0x0A95,0x6A8A,0,0x6B07,8,1);
                    for(unsigned k=0;k<6;k++) {
                        XW8(0,nb_[k].entry,0xE9);XW16(0,nb_[k].entry+1,(uint16_t)(nb_[k].code-nb_[k].entry-3));
                        XSTUB(0,nb_[k].code,k+1,nb_[k].id,nb_[k].args,0);
                    }
                }
#undef XSTUB
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0x0D14) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* Arguments: two world longs (low, high). The low words straddle the
                 * carries of 810's rounding and of the position sums; the cell
                 * numbers stay small so the lists the stubs name exist. */
                uint16_t arg[8];
                for(unsigned k=0;k<2;k++) {
                    uint16_t lo,hi;
                    switch(XPICK(8)) {
                    case 0: lo=(uint16_t)(0xFFFC+XPICK(4));hi=(XRAND()&1)?0xFFFF:0; break;
                    case 1: lo=(uint16_t)(0x0FFC+0x1000*XPICK(16)+XPICK(4));hi=0; break;
                    case 2: lo=(uint16_t)XRAND();hi=(uint16_t)XRAND(); break;
                    case 3: lo=(uint16_t)(XRAND()&3);hi=(XRAND()&1)?0xFFFF:0; break;
                    default: lo=(uint16_t)(XRAND()&0x7FFF);hi=XPICK(4)?0:(uint16_t)(XRAND()&3); break;
                    }
                    arg[2*k]=lo;arg[2*k+1]=hi;
                }
                for(unsigned j=4;j<8;j++)arg[j]=(uint16_t)XRAND();
                for(unsigned j=0;j<8;j++)XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),arg[j]);
                for(unsigned j=0;j<4;j++)small[j]=arg[j];
                /* The nine neighbours: offsets 4CC / 4DE index the position table 4F4
                 * (and are added to the cell numbers). Mostly tiny, sometimes any. */
                for(unsigned j=0;j<9;j++) {
                    uint16_t a=(uint16_t)XPICK(3),b=(uint16_t)XPICK(3);
                    switch(XPICK(8)) {
                    case 0: a=(uint16_t)XRAND(); break;
                    case 1: b=(uint16_t)XRAND(); break;
                    case 2: a=0xFFFF; break;
                    case 3: b=0xFFFF; break;
                    default: break;
                    }
                    XW16(dg,0x04CC+2*j,a);XW16(dg,0x04DE + 2*j,b);
                }
                for(unsigned j=0;j<0x80;j++) {
                    uint16_t v;
                    switch(XPICK(4)) {
                    case 0: v=(uint16_t)XRAND(); break;
                    case 1: v=(uint16_t)(XRAND()&0x0FFF); break;
                    case 2: v=(uint16_t)(0x1000*XPICK(3)); break;
                    default: v=(uint16_t)(-(int)(XRAND()&0x0FFF)); break;
                    }
                    XW16(dg,0x04F4+2*j,v);
                }
                /* Type tables: visibility C630, the 7E0 flag byte, the override
                 * types B7A2 (five bytes an entry). */
                for(unsigned j=0;j<0x80;j++) {
                    XW8(dg,0xC630+j,(XPICK(10)<3)?0:(uint8_t)(1+XPICK(255)));
                    XW8(dg,0x07E0+j,(XPICK(2))?0:(uint8_t)(1+XPICK(255)));
                }
                for(unsigned j=0;j<0x110;j++)XW8(dg,0xB790+j,XRAND());
                XW16(dg,0x3DA4,XPICK(2)?0:(uint16_t)(1+XPICK(0xFFFF)));
                XW16(dg,0xE476,XRAND());
                for(unsigned j=0;j<0x20;j++)XW8(dg,0xBA40+j,XRAND());
                /* Cell lists: slot = 32 * level + cell. A pointer, a count and up to
                 * four seven-byte objects (x, y, a word, the type byte). An object
                 * is sometimes a copy of the one before it, so distances tie. */
                for(unsigned slot=0;slot<100;slot++) {
                    const uint16_t list=(uint16_t)(0xD000+0x20*slot);
                    unsigned n;
                    switch(XPICK(10)) {
                    case 0: case 1: case 2: n=0; break;
                    case 3: case 4: n=1; break;
                    case 5: case 6: n=2; break;
                    case 7: case 8: n=3; break;
                    default: n=4; break;
                    }
                    XW16(dg,0xE330+2*slot,list);
                    XW16(dg,0x061E + 2*slot,n);
                    uint16_t px=0,py=0,pw=0;uint8_t pt=0;
                    for(unsigned e=0;e<5;e++) {
                        uint16_t x,y,w;uint8_t t;
                        if(e&&XPICK(3)==0) { x=px;y=py;w=pw;t=pt; }
                        else {
                            switch(XPICK(6)) {
                            case 0: x=(uint16_t)XRAND(); break;
                            case 1: x=(XRAND()&1)?0x8000:0x7FFF; break;
                            case 2: x=(uint16_t)(0x0800+XPICK(0x100)-0x80); break;
                            default: x=(uint16_t)XPICK(0x1000); break;
                            }
                            switch(XPICK(6)) {
                            case 0: y=(uint16_t)XRAND(); break;
                            case 1: y=(XRAND()&1)?0x8000:0x7FFF; break;
                            case 2: y=(uint16_t)(0x0800+XPICK(0x100)-0x80); break;
                            default: y=(uint16_t)XPICK(0x1000); break;
                            }
                            w=(uint16_t)XRAND();
                            t=(uint8_t)((XRAND()&0x7F)|((XRAND()&1)?0x80:0));
                        }
                        px=x;py=y;pw=w;pt=t;
                        XW16(dg,list+7*e,x);XW16(dg,list+7*e+2,y);XW16(dg,list+7*e+4,w);XW8(dg,list+7*e+6,t);
                    }
                }
                /* Return lists for the decision stubs: 0871 finds a cell (-1 none),
                 * 0FBD finds an override (AL nonzero) and leaves its index in 950C. */
                for(unsigned j=0;j<32;j++) {
                    uint16_t cell;
                    switch(XPICK(10)) {
                    case 0: case 1: cell=0xFFFF; break;
                    case 2: cell=0xFFFE; break;
                    case 3: cell=(XRAND()&1)?0x7FFF:0x8000; break;
                    case 4: cell=(XRAND()&1)?0:31; break;
                    default: cell=(uint16_t)XPICK(32); break;
                    }
                    XW16(dg,0xF400+2*j,cell);
                    uint16_t found=(XPICK(5)<2)?(uint16_t)(XRAND()&0xFF00):(uint16_t)((XRAND()&0xFF00)|(1+XPICK(255)));
                    XW16(dg,0xF440+2*j,found);
                    uint16_t idx;
                    switch(XPICK(10)) {
                    case 0: idx=0xFFFF; break;
                    case 1: idx=(uint16_t)XRAND(); break;
                    default: idx=(uint16_t)XPICK(30); break;
                    }
                    XW16(dg,0xF480+2*j,idx);
                }
                for(unsigned j=0;j<8;j++)XW16(dg,0xF800+2*j,0);
                XW16(dg,0xF804,(uint16_t)(2*XPICK(32)));XW16(dg,0xF814,(uint16_t)(2*XPICK(32)));
                /* Returning probes: fold every argument in order into a guest cell
                 * (+2), count the calls (+0), return the next word of a list (the
                 * index is at +4); 0FBD also stores the next word of a second list
                 * in [950C]. 810, F018 and EE0C are arithmetic and stay original. */
#define XSTUB(off_,cell_,nargs_,list_,list2_) do { unsigned out_=(off_);const uint16_t sgx_=0; \
    XW8(sgx_,out_++,0x55);XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0xEC);XW8(sgx_,out_++,0x53); \
    XW8(sgx_,out_++,0xFF);XW8(sgx_,out_++,0x06);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x46);XW8(sgx_,out_++,4+2*j_); \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+2);XW8(sgx_,out_++,((cell_)+2)>>8); \
        XW8(sgx_,out_++,0xD1);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x33);XW8(sgx_,out_++,0xD8); \
        XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+2);XW8(sgx_,out_++,((cell_)+2)>>8); } \
    XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+4);XW8(sgx_,out_++,((cell_)+4)>>8); \
    XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x87);XW8(sgx_,out_++,(list_));XW8(sgx_,out_++,(list_)>>8); \
    if(list2_) { \
        XW8(sgx_,out_++,0x50); \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x87);XW8(sgx_,out_++,(list2_));XW8(sgx_,out_++,(list2_)>>8); \
        XW8(sgx_,out_++,0xA3);XW8(sgx_,out_++,0x0C);XW8(sgx_,out_++,0x95); \
        XW8(sgx_,out_++,0x58); } \
    XW8(sgx_,out_++,0x83);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x02); \
    XW8(sgx_,out_++,0x83);XW8(sgx_,out_++,0xE3);XW8(sgx_,out_++,0x3E); \
    XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+4);XW8(sgx_,out_++,((cell_)+4)>>8); \
    XW8(sgx_,out_++,0x5B);XW8(sgx_,out_++,0x5D);XW8(sgx_,out_++,0xC3); } while(0)
                {
                    static const struct { uint16_t slot,code,cell,list,list2; unsigned args; } stub_[2]={
                        {0x0871,0xF200,0xF800,0xF400,0,3},{0x0FBD,0xF260,0xF810,0xF440,0xF480,4}};
                    for(unsigned k=0;k<2;k++) {
                        XW8(0,stub_[k].slot,0xE9);XW16(0,stub_[k].slot+1,(uint16_t)(stub_[k].code-stub_[k].slot-3));
                        XSTUB(stub_[k].code,stub_[k].cell,stub_[k].args,stub_[k].list,stub_[k].list2);
                    }
                }
                /* One state in sixteen has no objects in any cell, so the "none
                 * found" exit (0F37) is reached at every seed. Drawn last, so the
                 * other states keep every earlier draw. */
                if(XPICK(16)==0)
                    for(unsigned slot=0;slot<100;slot++)XW16(dg,0x061E + 2*slot,0);
#undef XSTUB
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0x17B4) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define RND(n_) ((unsigned)(XRAND()%(unsigned)(n_)))
#define PCT(p_) (RND(100)<(unsigned)(p_))
#define PICK(a_) ((a_)[RND(sizeof(a_)/sizeof((a_)[0]))])
                /* The stack below SP and the frame's own bytes start random in both
                 * segments: the lanes are stored through DS and read back through
                 * SS, so a store that went to the wrong segment shows when SS is
                 * not DS. */
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                for(unsigned j=0;j<0x200;j++)XW8(dg,(uint16_t)(r[R_SP]-0x200+j),XRAND());
                for(unsigned j=0;j<8;j++)XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),XRAND());
                for(unsigned j=0;j<4;j++)small[j]=(uint16_t)XRAND();
                /* The window record behind [0D1C]: its words 0 and +4 are written. */
                { const uint16_t rec=(uint16_t)(0x2400+0x20*RND(4));
                  for(unsigned j=0;j<0x80;j++)XW8(dg,(uint16_t)(0x2400+j),XRAND());
                  XW16(dg,0x0D1C,rec); }
                /* The view byte (sign-extended, then 1 - v), the pitch figure
                 * around its floor of 20h, the roll figure (shifted left four
                 * as a long), and the two attitude factors. */
                { static const uint8_t v_[]={0,1,2,0x7F,0x80,0x81,0xFF,0xFE};
                  XW8(dg,0x40B6,PCT(60)?PICK(v_):(uint8_t)XRAND()); }
                { static const uint16_t p_[]={0,1,0x1F,0x20,0x21,0x22,0x7FFF,0x8000,0xFFFF,0xFFE0,0x0100,0x1000};
                  XW16(dg,0x9F42,PCT(65)?PICK(p_):(uint16_t)XRAND()); }
                { static const uint16_t q_[]={0,1,0x7FFF,0x8000,0xFFFF,0x0FFF,0xF000,0x0800,0xFFFE,0x1234};
                  XW16(dg,0x9928,PCT(40)?PICK(q_):(uint16_t)XRAND()); }
                { static const uint16_t f_[]={0,1,0x7FFF,0x8000,0xFFFF,0x4000,0x00FF};
                  XW16(dg,0x9914,PCT(30)?PICK(f_):(uint16_t)XRAND());
                  XW16(dg,0x98F8,PCT(30)?PICK(f_):(uint16_t)XRAND()); }
                /* Outputs start random, so a write the body skipped shows. */
                XW16(dg,0x2293,XRAND());XW16(dg,0x2295,XRAND());
                /* Probes: fold every argument in order into a guest cell and count the
                 * call, answer from a list (the 104E helper, a different word each
                 * call) or a fixed word. EF68 and EE9C stay original. */
                for(unsigned j=0;j<16;j++)XW16(dg,0xF800+2*j,0);
                { static const uint16_t e_[]={0,1,0x7FFF,0x8000,0xFFFF};
                  for(unsigned j=0;j<32;j++)XW16(dg,0xF840+2*j,PCT(15)?PICK(e_):(uint16_t)XRAND()); }
                XW16(dg,0xF820,XRAND());
                XW16(dg,0xF80E,2*RND(32));
#define XSTUB(sg_,off_,cell_,nargs_,retcell_,list_,far_) do { unsigned out_=(off_);const uint16_t sgx_=(sg_); \
    XW8(sgx_,out_++,0x55);XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0xEC);XW8(sgx_,out_++,0x53); \
    XW8(sgx_,out_++,0xFF);XW8(sgx_,out_++,0x06);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x46);XW8(sgx_,out_++,(far_)?6+2*j_:4+2*j_); \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); \
        XW8(sgx_,out_++,0xD1);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x33);XW8(sgx_,out_++,0xD8); \
        XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); } \
    if(list_) { \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+2);XW8(sgx_,out_++,((cell_)+2)>>8); \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x87);XW8(sgx_,out_++,(list_));XW8(sgx_,out_++,(list_)>>8); \
        XW8(sgx_,out_++,0x83);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x02); \
        XW8(sgx_,out_++,0x83);XW8(sgx_,out_++,0xE3);XW8(sgx_,out_++,0x3E); \
        XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+2);XW8(sgx_,out_++,((cell_)+2)>>8); } \
    XW8(sgx_,out_++,0x5B); \
    if(retcell_) { XW8(sgx_,out_++,0xA1);XW8(sgx_,out_++,(retcell_));XW8(sgx_,out_++,(retcell_)>>8); } \
    XW8(sgx_,out_++,0x5D);XW8(sgx_,out_++,(far_)?0xCB:0xC3); } while(0)
                XSTUB(0x1E42,0x0250,0xF800,2,0xF820,0,1);        /* the box query: window id in AX */
                XSTUB(0x1E42,0x019C,0xF802,1,0,0,1);             /* window select */
                XSTUB(0x0FB2,0x0246,0xF804,5,0,0,1);             /* panel clear */
                XSTUB(0x0FB2,0x01E6,0xF806,4,0,0,1);             /* polygon draw */
                XSTUB(0x114A,0x03FD,0xF808,7,0,0,1);             /* frame draw */
                XSTUB(0x1E42,0x0700,0xF80A,8,0,0,1);             /* end marks, entered by a jump from 01EC */
                XW8(0x1E42,0x01EC,0xE9);XW16(0x1E42,0x01ED,(uint16_t)(0x0700-0x01EC-3));
                XSTUB(0x104E,0x0000,0xF80C,2,0,0xF840,1);        /* the angle helper */
#undef XSTUB
#undef PICK
#undef PCT
#undef RND
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"START.EXE") && o->seg==0 && o->ip==0x623E) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The routine takes no arguments; the words above SP are noise,
                 * and the harness's small[] copies of them match. */
                for(unsigned j=0;j<8;j++) {
                    const uint16_t w=(uint16_t)XRAND();
                    XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w);
                    if(j<4)small[j]=w;
                }
                /* The data the loop reads: the poll's argument words, the roster
                 * (D842h up, 50h bytes a record, ten pilots and a margin), the
                 * flag bytes around [D2A2] and the two bytes at [D878]. */
                for(unsigned j=0;j<0x20;j++)XW8(dg,0x7040+j,XRAND());
                for(unsigned j=0;j<0x50*12+0x20;j++)XW8(dg,0xD830+j,XRAND());
                for(unsigned j=0;j<0x10;j++)XW8(dg,0xD298+j,XRAND());
                /* The three far pointers: [CBD8] (a word is cleared), [CACA] (the
                 * current pilot's record) and [E096] (a record whose +22h is
                 * looked at), each in a segment of its own. */
                const uint16_t p0off=(uint16_t)XPICK(0x3000), p1off=(uint16_t)XPICK(0x6000), p2off=(uint16_t)XPICK(0x3000);
                XW16(dg,0xCBD8,p0off);XW16(dg,0xCBDA,0x7800);
                XW16(dg,0xCACA,p1off);XW16(dg,0xCACC,0x7000);
                XW16(dg,0xE096,p2off);XW16(dg,0xE098,0x7C00);
                XW16(0x7800,p0off,XRAND());
                for(unsigned j=0;j<0x60;j++)XW8(0x7000,(uint16_t)(p1off+j),XRAND());
                for(unsigned j=0;j<0x30;j++)XW8(0x7C00,(uint16_t)(p2off+j),XRAND());
                XW16(0x7C00,(uint16_t)(p2off+0x22),(XRAND()&1)?0:(uint16_t)(1+XPICK(0xFFFF)));
                /* The current pilot's index: mostly 0..9, otherwise near the
                 * boundaries of FFh - index, sometimes negative; never one whose
                 * copy lands on the stack, the stubs' cells or the far pointers. */
                unsigned idx;
                switch(XPICK(8)) {
                    case 0: idx=0xFF+XPICK(4); break;
                    case 1: idx=(uint16_t)(-1-(int)XPICK(0x20)); break;
                    case 2: idx=0xF0+XPICK(0x20); break;
                    case 3: idx=XRAND()&0xFFFF; break;
                    default: idx=XPICK(10); break;
                }
                for(unsigned tries=0;tries<4;tries++) {
                    const unsigned d0=(uint16_t)(0xD892+0x50*(uint16_t)idx);
                    const unsigned d1=d0+0x50;
                    const int bad = d0<0x0100 || d1>0xEC00 || (d0<0xCAD0&&d1>0xCAC8) || (d0<0xCBE0&&d1>0xCBD6)
                                    || (d0<0xE09C&&d1>0xE094);
                    if(!bad)break;
                    idx=XPICK(10);
                }
                XW16(0x7000,p1off,idx);
                /* The current record's fields: +4Eh (the status the exit looks at),
                 * +42h, +38h, +3Ch. */
                {
                    uint16_t st;
                    switch(XPICK(8)) {
                        case 0: case 1: case 2: case 3: st=0; break;
                        case 4: st=1; break;
                        case 5: st=2; break;
                        case 6: st=(uint16_t)(3+XPICK(3)); break;
                        default: st=(uint16_t)(1+XPICK(0xFFFF)); break;
                    }
                    XW16(0x7000,(uint16_t)(p1off+0x4E),st);
                    XW16(0x7000,(uint16_t)(p1off+0x38),(XRAND()&1)?0:(uint16_t)XRAND());
                }
                /* The roster's status words (D890h + 50h * event): mostly empty,
                 * 1, 2, or something else. */
                for(unsigned k=0;k<10;k++) {
                    uint16_t st;
                    switch(XPICK(10)) {
                        case 0: case 1: case 2: case 3: st=0; break;
                        case 4: case 5: st=1; break;
                        case 6: case 7: st=2; break;
                        case 8: st=(uint16_t)(3+XPICK(3)); break;
                        default: st=(uint16_t)(1+XPICK(0xFFFF)); break;
                    }
                    XW16(dg,(uint16_t)(0xD890+0x50*(k+1)),st);
                }
                /* [D2A2]: zero about half the time; its neighbours vary alone. */
                XW8(dg,0xD2A2,(XRAND()&1)?0:(uint8_t)(1+XPICK(255)));
                /* The stubs' state and lists. Each stub keeps a count (F802 + 2k),
                 * a list index (F820 + 2k) and folds a number naming it and then
                 * every argument into one shared cell (F800). */
                for(unsigned j=0;j<0x40;j+=2)XW16(dg,0xF800+j,0);
                for(unsigned k=0;k<9;k++)for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*k+2*j,XRAND());
                /* 38AC's events: the pilot rows 1..10, the exits 11 and 99, the
                 * number 162h, their neighbours and noise; an exit at a place
                 * of its own. 3CEA answers 2 half the time; 393D gives numbers
                 * around 1..10. */
                {
                    static const uint16_t odd[]={12,98,100,353,355,0x8000,0x7FFF,0x8001,0xFFFF,0xFFFE,0x0162+0x100,0x00FF,0x0100,0x0009,0x000A,0x000B,0x0064,0x0063+0x8000};
                    unsigned nk;
                    switch(XPICK(4)) {
                        case 0: nk=XPICK(2); break;
                        case 1: nk=XPICK(32); break;
                        default: nk=XPICK(12); break;
                    }
                    for(unsigned j=0;j<32;j++) {
                        uint16_t key;
                        if(j==nk)key=(XRAND()&1)?11:99;
                        else switch(XPICK(20)) {
                            case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: key=(uint16_t)(1+XPICK(10)); break;
                            case 8: key=0; break;
                            case 9: key=11; break;
                            case 10: key=99; break;
                            case 11: case 12: case 13: key=0x162; break;
                            case 14: case 15: key=odd[XPICK(sizeof odd/sizeof odd[0])]; break;
                            case 16: key=(uint16_t)(1+XPICK(10)); break;
                            default: key=(uint16_t)XRAND(); break;
                        }
                        XW16(dg,0xF400+0x40*1+2*j,key);
                    }
                    for(unsigned j=0;j<32;j++) {
                        uint16_t v;
                        switch(XPICK(8)) {
                            case 0: case 1: case 2: case 3: v=2; break;
                            case 4: v=(uint16_t)XPICK(4); break;
                            case 5: v=0x8002; break;
                            default: v=(uint16_t)XRAND(); break;
                        }
                        XW16(dg,0xF400+0x40*2+2*j,v);
                    }
                    for(unsigned j=0;j<32;j++) {
                        uint16_t v;
                        switch(XPICK(10)) {
                            case 0: case 1: case 2: case 3: v=(uint16_t)(1+XPICK(10)); break;
                            case 4: v=0; break;
                            case 5: v=11; break;
                            case 6: v=(uint16_t)(XRAND()|0x8000); break;
                            case 7: v=(uint16_t)(10+XPICK(2)); break;
                            default: v=(uint16_t)XRAND(); break;
                        }
                        XW16(dg,0xF400+0x40*5+2*j,v);
                    }
                }
#define XSTUB(off_,k_,id_,nargs_) do { \
    uint8_t cb_[200]; unsigned n_=0; \
    const unsigned cnt_=0xF802+2*(k_),ptr_=0xF820+2*(k_),list_=0xF400+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(id_)&255;cb_[n_++]=(id_)>>8; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(4+2*j_); \
        cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
        cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; } \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x83;cb_[n_++]=0xC3;cb_[n_++]=0x02; \
    cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8(0,(off_)+k2_,cb_[k2_]); } while(0)
                {
                    static const struct { uint16_t entry,code,id; uint8_t args; } nb_[9]={
                        {0x3641,0xF200,0x0A51,2},{0x38AC,0xF300,0x1B62,1},{0x3CEA,0xF400,0x2C73,3},
                        {0x3517,0xF500,0x3D84,3},{0x6DB8,0xF600,0x4E95,0},{0x393D,0xF700,0x5FA6,2},
                        {0x6AB3,0xF800,0x60B7,1},{0x3989,0xF900,0x71C8,0},{0x7272,0xFA00,0x82D9,1}};
                    for(unsigned k=0;k<9;k++) {
                        XW8(0,nb_[k].entry,0xE9);XW16(0,nb_[k].entry+1,(uint16_t)(nb_[k].code-nb_[k].entry-3));
                        if(k==7) {
                            /* 3989: the first call leaves the current record alone; every
                             * later one clears its status word (+4Eh), so that a loop
                             * whose first exit finds a status goes on to a second. */
                            const unsigned cnt_=0xF802+2*7;
                            uint8_t cb_[64]; unsigned n_=0;
                            cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53;cb_[n_++]=0x06;
                            cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8;
                            cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8;
                            cb_[n_++]=0xD1;cb_[n_++]=0xC3;
                            cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=nb_[k].id&255;cb_[n_++]=nb_[k].id>>8;
                            cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8;
                            cb_[n_++]=0x83;cb_[n_++]=0x3E;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8;cb_[n_++]=0x01;
                            cb_[n_++]=0x76;cb_[n_++]=0x0A;
                            cb_[n_++]=0xC4;cb_[n_++]=0x1E;cb_[n_++]=0xCA;cb_[n_++]=0xCA;
                            cb_[n_++]=0x26;cb_[n_++]=0xC7;cb_[n_++]=0x47;cb_[n_++]=0x4E;cb_[n_++]=0x00;cb_[n_++]=0x00;
                            cb_[n_++]=0x07;cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=0xC3;
                            for(unsigned k2_=0;k2_<n_;k2_++)XW8(0,nb_[k].code+k2_,cb_[k2_]);
                        } else {
                            XSTUB(nb_[k].code,k,nb_[k].id,nb_[k].args);
                        }
                    }
                }
#undef XSTUB
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"PLAYER.EXE") && o->seg==0 && o->ip==0x0244) {
                const uint16_t dg=0x5000,tg_seg=0x7000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                /* The stack, and the area the buffer is in (it is DS:BP-24h whichever SS is). */
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                for(unsigned j=0;j<0x200;j++)XW8(dg,(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The extension strings. DS:E9 is mostly the dot alone; sometimes no
                 * string at all, a letter, or one that runs on into the string at EB. */
                for(unsigned j=0;j<0x10;j++)XW8(dg,0x00E0+j,1+XPICK(255));
                {
                    const unsigned k9=XPICK(20);
                    if(k9<11) { XW8(dg,0x00E9,'.');XW8(dg,0x00EA,0); }
                    else if(k9<14) { XW8(dg,0x00E9,0);XW8(dg,0x00EA,'.'); }
                    else if(k9<17) { XW8(dg,0x00E9,'A'+XPICK(26));XW8(dg,0x00EA,(XPICK(2))?'.':'B'); }
                    else { XW8(dg,0x00E9,'.');XW8(dg,0x00EA,'Z'); }
                    const unsigned le=XPICK(10)<2?0:(XPICK(8)==0?XPICK(6):1+XPICK(3));
                    for(unsigned j=0;j<le;j++)XW8(dg,0x00EB+j,(XPICK(6)==0)?'.':(0x41+XPICK(26)));
                    XW8(dg,0x00EB+le,0);
                    XW8(dg,0x00EB+le+1,1+XPICK(255));
                }
                unsigned e9len=0,eblen=0;
                while(e9len<0x20&&g_pristine[phys(dg,(uint16_t)(0x00E9+e9len))])e9len++;
                while(eblen<0x20&&g_pristine[phys(dg,(uint16_t)(0x00EB+eblen))])eblen++;
                /* The name: what fits the buffer with the two strings added (23h bytes
                 * and the zero) when the stack is the buffer's own memory. */
                const uint16_t name_at=(uint16_t)(0x2200+XPICK(0x400));
                {
                    const int room_=0x23-(int)e9len-(int)eblen;
                    const unsigned maxn=(s&4)?0x30:(unsigned)(room_<0?0:room_);
                    unsigned nlen;
                    switch(XPICK(10)) {
                        case 0: nlen=0; break;
                        case 1: nlen=maxn; break;
                        case 2: nlen=maxn>0?maxn-1:0; break;
                        case 3: case 4: nlen=XPICK(maxn+1); break;
                        default: nlen=XPICK(maxn<10?maxn+1:10); break;
                    }
                    if(nlen>maxn)nlen=maxn;
                    const unsigned dot=(nlen>0&&XPICK(5)<2)?XPICK(nlen):nlen+1;
                    for(unsigned j=0;j<nlen;j++) {
                        unsigned ch=0x20+XPICK(0x5F);
                        if(ch=='.')ch='/';
                        if(j==dot)ch='.';
                        XW8(dg,(uint16_t)(name_at+j),ch);
                    }
                    XW8(dg,(uint16_t)(name_at+nlen),0);
                    for(unsigned j=1;j<8;j++)XW8(dg,(uint16_t)(name_at+nlen+j),XRAND());
                }
                /* The arguments: the name's address, then words nothing reads. */
                XW16(seg[S_SS],r[R_SP],name_at);small[0]=name_at;
                for(unsigned j=1;j<8;j++) {
                    const uint16_t w=(uint16_t)XRAND();
                    XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w);
                    if(j<4)small[j]=w;
                }
                /* The streaming state. The table is at the long TAB_HI:TAB_LO, read
                 * as segment (long >> 4), offset TAB_LO & 15: TAB_HI = 7 and any high
                 * nibbles, TAB_LO = 16 k + n is the linear address 70000h + TAB_LO. */
                const uint16_t tab_lo=(uint16_t)(XPICK(0x10)+0x10*XPICK(0x40));
                const uint16_t tab_hi=(uint16_t)(7+0x10*XPICK(0x10));
                XW16(dg,0x207E,tab_lo);XW16(dg,0x2080,tab_hi);
                /* The table's entries: a few small or mixed ones, then 0FFFFh. */
                for(unsigned j=0;j<0x400;j++) {
                    uint16_t v;
                    if(j>=24)v=0xFFFF;
                    else switch(XPICK(8)) {
                        case 0: v=0; break;
                        case 1: case 2: v=(uint16_t)(1+XPICK(0x40)); break;
                        case 3: case 4: case 5: v=(uint16_t)(1+XPICK(0x400)); break;
                        case 6: v=(uint16_t)(XRAND()&0x7FFF); break;
                        default: v=(uint16_t)XRAND(); break;
                    }
                    XW16(tg_seg,(uint16_t)(tab_lo+2*j),v);
                }
                /* The callees' replies: lists of 32 words, index 0 first. */
                const unsigned rounds=XPICK(4);                   /* how often the end flag says "not yet" */
                uint16_t be2_first=0;
                for(unsigned j=0;j<32;j++) {
                    uint16_t v;
                    /* 0BA2: the open result, nonzero mostly (the whole word is tested) */
                    v=(XPICK(5)==0)?0:((XPICK(3)==0)?(uint16_t)(0x0100*(1+XPICK(255))):(uint16_t)(1+XPICK(3)));
                    XW16(dg,0xF400+0x40*0+2*j,v);
                    /* 03FE: room for a block; zero at first, then always nonzero */
                    v=(j<3&&XPICK(2))?0:((XPICK(4)==0)?(uint16_t)(0x0100*(1+XPICK(255))):(uint16_t)(1+XPICK(0xFFFF)));
                    XW16(dg,0xF400+0x40*1+2*j,v);
                    XW16(dg,0xF400+0x40*2+2*j,XRAND());
                    /* 0BE2: the block's length */
                    switch(XPICK(10)) {
                        case 0: case 1: v=0; break;
                        case 2: case 3: case 4: v=(uint16_t)(1+XPICK(0x200)); break;
                        case 5: case 6: v=(uint16_t)(0x8000+XPICK(0x8000)); break;
                        default: v=(uint16_t)XRAND(); break;
                    }
                    XW16(dg,0xF400+0x40*3+2*j,v);
                    if(j==0)be2_first=v;
                    /* 0664: the entry count */
                    switch(XPICK(12)) {
                        case 0: case 1: v=0; break;
                        case 2: case 3: v=(uint16_t)(1+XPICK(0x20)); break;
                        case 4: v=0x03FF; break;
                        case 5: v=0x0400; break;
                        case 6: v=0x0401; break;
                        case 7: case 8: v=(uint16_t)(0x8000+XPICK(0x8000)); break;
                        default: v=(uint16_t)XRAND(); break;
                    }
                    XW16(dg,0xF400+0x40*4+2*j,v);
                    XW16(dg,0xF400+0x40*5+2*j,XRAND());
                    XW16(dg,0xF400+0x40*6+2*j,XRAND());
                    /* 0BDC: the end flag, "not yet" for the first rounds */
                    v=(j<rounds)?0:((XPICK(4)==0)?(uint16_t)(0x0100*(1+XPICK(255))):(uint16_t)(1+XPICK(3)));
                    XW16(dg,0xF400+0x40*7+2*j,v);
                    /* 0BCD: the close result */
                    v=(XPICK(2))?0:(uint16_t)(1+XPICK(0xFFFF));
                    XW16(dg,0xF400+0x40*8+2*j,v);
                }
                /* The cursor, the end of the last picture and the entry count. The
                 * first round's walk is aimed half of the time: the cursor after the
                 * first block equals the end plus a few entries, give or take one. */
                {
                    uint32_t used,cur;
                    switch(XPICK(5)) {
                        case 0: used=((uint32_t)0xFFFF<<16)|(XRAND()&0xFFFF); break;
                        case 1: used=((uint32_t)XPICK(3)<<16)|(XRAND()&0xFFFF); break;
                        default: used=(XPICK(2))?(XRAND()&0xFFFF):(XRAND()&0x3FF); break;
                    }
                    if(XPICK(2)&&(used>>16)!=0xFFFF) {
                        const unsigned jj=XPICK(5);
                        uint32_t t=used;
                        for(unsigned i=0;i<=jj;i++) {
                            const uint16_t a=(uint16_t)(tab_lo+2*i);
                            t+=(uint32_t)(g_pristine[phys(tg_seg,a)]|(g_pristine[phys(tg_seg,(uint16_t)(a+1))]<<8));
                        }
                        static const int dd[6]={0,0,0,-1,1,0};
                        t+=(uint32_t)dd[XPICK(6)];
                        cur=t-(uint32_t)be2_first;
                    } else {
                        cur=(uint32_t)(XPICK(4)==0?XPICK(4):XPICK(2))<<16;
                        cur|=(XPICK(3)==0)?(0xFFFFu-XPICK(0x100)):(XRAND()&0x7FFF);
                    }
                    XW16(dg,0x2082,(uint16_t)cur);XW16(dg,0x2084,(uint16_t)(cur>>16));
                    XW16(dg,0x2094,(uint16_t)used);XW16(dg,0x2096,(uint16_t)(used>>16));
                    XW16(dg,0x1DE8,(uint16_t)(1+XPICK(0xFFFF)));
                }
                for(unsigned j=0;j<0x40;j+=2)XW16(dg,0xF800+j,0);
                /* The stubs: each counts its calls (F802 + 2k), keeps a list index
                 * (F820 + 2k), folds a number naming it and then every argument into
                 * the shared cell F800 and returns the next word of its list. */
#define XSTUB(off_,k_,id_,nargs_) do { \
    uint8_t cb_[200]; unsigned n_=0; \
    const unsigned cnt_=0xF802+2*(k_),ptr_=0xF820+2*(k_),list_=0xF400+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(id_)&255;cb_[n_++]=(id_)>>8; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(4+2*j_); \
        cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
        cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; } \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x83;cb_[n_++]=0xC3;cb_[n_++]=0x02; \
    cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8(0,(off_)+k2_,cb_[k2_]); } while(0)
                {
                    static const struct { uint16_t entry,code,id; uint8_t args; } nb_[9]={
                        {0x0BA2,0xF200,0x0A51,1},{0x03FE,0xF300,0x1B62,0},{0x0696,0xF400,0x2C73,0},
                        {0x0BE2,0xF500,0x3D84,2},{0x0664,0xF600,0x4E95,2},{0x1476,0xF700,0x5FA6,3},
                        {0x05F6,0xF780,0x6AB7,5},{0x0BDC,0xF340,0x7BC8,0},{0x0BCD,0xF440,0x8CD9,0}};
                    for(unsigned k=0;k<9;k++) {
                        XW8(0,nb_[k].entry,0xE9);XW16(0,nb_[k].entry+1,(uint16_t)(nb_[k].code-nb_[k].entry-3));
                        XSTUB(nb_[k].code,k,nb_[k].id,nb_[k].args);
                    }
                }
#undef XSTUB
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0x09A0) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define RND(n_) ((unsigned)(XRAND()%(unsigned)(n_)))
#define PCT(p_) (RND(100)<(unsigned)(p_))
#define PICK(a_) ((a_)[RND(sizeof(a_)/sizeof((a_)[0]))])
                /* The stack below SP starts random in both segments (the frame's
                 * locals are read back through SS but some tables are stored through
                 * DS); the argument is the name pointer, only folded by the stubs. */
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                for(unsigned j=0;j<0x200;j++)XW8(dg,(uint16_t)(r[R_SP]-0x200+j),XRAND());
                for(unsigned j=0;j<8;j++)XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),XRAND());
                for(unsigned j=0;j<4;j++)small[j]=(uint16_t)XRAND();
                /* The tables the routine fills and reads: random to start with, so a
                 * store the body skipped or aimed wrongly shows. */
                for(unsigned j=0;j<0x500;j++)XW8(dg,0x0500+j,XRAND());
                XW16(dg,0xB78C,XRAND());XW16(dg,0xB78E,XRAND());
                /* The two loaded blocks: a segment each, a count word, random bytes. */
                const uint16_t seg1=(uint16_t)(0x7000+0x100*RND(4));
                const uint16_t seg2=(uint16_t)(0x7800+0x100*RND(4));
                uint16_t n1,n2;
                { static const uint16_t a_[]={0,1,2,3,4,5,7,8,0x0F,0x10,0x11,0x20,0x40,0x7F,0x80,0x81,0x83,0x90};
                  n1=PCT(55)?PICK(a_):(uint16_t)RND(0x30); }
                { static const uint16_t a_[]={0,1,2,3,0xFFFF,0xFFFE,0x8000,0x7F,0x10,0x20,0x40,0x80,0x81,0xFF,0x100,0x101,0x8001};
                  const unsigned q=RND(100);
                  if(q<50)n2=PICK(a_);
                  else if(q<62)n2=(uint16_t)(0xFFFF-RND(0x40));
                  else n2=(uint16_t)RND(0x30); }
                for(unsigned j=0;j<0x800;j++) { XW8(seg1,j,XRAND());XW8(seg2,j,XRAND()); }
                XW16(seg1,0,n1);XW16(seg2,0,n2);
                /* What the loader (stubbed) answers: nothing (a failed load) now and
                 * then, else the block's segment. */
                { const uint16_t a1=PCT(14)?0:seg1;
                  const uint16_t a2=PCT(14)?0:seg2;
                  for(unsigned j=0;j<32;j++)XW16(dg,0xF840+2*j,(uint16_t)XRAND());
                  XW16(dg,0xF840,a1);XW16(dg,0xF842,a2); }
                /* The substitution bytes: zero or not, each. */
                { static const uint8_t v_[]={0,1,2,0x7F,0x80,0xFF,0x0F,0x10,0x20,0x40};
                  XW8(dg,0xE30B,PCT(40)?0:(PCT(60)?PICK(v_):(uint8_t)XRAND()));
                  XW8(dg,0xE31D,PCT(40)?0:(PCT(60)?PICK(v_):(uint8_t)XRAND())); }
                /* The far record behind [9924]: the word at +38h is 6 about half the
                 * time, else near it. */
                { const uint16_t rseg=(uint16_t)(0x7C00+0x100*RND(2));
                  const uint16_t roff=(uint16_t)(0x40*RND(4));
                  static const uint16_t w_[]={5,7,0,0xFFFF,0x0106,0x8006,0x0600,0x0004,0x000E};
                  for(unsigned j=0;j<0x180;j++)XW8(rseg,(uint16_t)(roff+j),XRAND());
                  XW16(rseg,roff+0x38,PCT(50)?6:(PCT(60)?PICK(w_):(uint16_t)XRAND()));
                  XW16(dg,0x9924,roff);XW16(dg,0x9926,rseg); }
                /* [0610] and the outputs the substitutions and the tail write start random. */
                XW16(dg,0x0610,XRAND());XW16(dg,0x05C2,XRAND());XW16(dg,0x051A,XRAND());
                for(unsigned j=0;j<16;j++)XW16(dg,0xF800+2*j,0);
                /* Probes: fold every argument in order into a guest cell and count the
                 * call; the loader's also answers the next word of a list. */
#define XSTUB(sg_,off_,cell_,nargs_,retcell_,list_,far_) do { unsigned out_=(off_);const uint16_t sgx_=(sg_); \
    XW8(sgx_,out_++,0x55);XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0xEC);XW8(sgx_,out_++,0x53); \
    XW8(sgx_,out_++,0xFF);XW8(sgx_,out_++,0x06);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x46);XW8(sgx_,out_++,(far_)?6+2*j_:4+2*j_); \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); \
        XW8(sgx_,out_++,0xD1);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x33);XW8(sgx_,out_++,0xD8); \
        XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); } \
    if(list_) { \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+2);XW8(sgx_,out_++,((cell_)+2)>>8); \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x87);XW8(sgx_,out_++,(list_));XW8(sgx_,out_++,(list_)>>8); \
        XW8(sgx_,out_++,0x83);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x02); \
        XW8(sgx_,out_++,0x83);XW8(sgx_,out_++,0xE3);XW8(sgx_,out_++,0x3E); \
        XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+2);XW8(sgx_,out_++,((cell_)+2)>>8); } \
    XW8(sgx_,out_++,0x5B); \
    if(retcell_) { XW8(sgx_,out_++,0xA1);XW8(sgx_,out_++,(retcell_));XW8(sgx_,out_++,(retcell_)>>8); } \
    XW8(sgx_,out_++,0x5D);XW8(sgx_,out_++,(far_)?0xCB:0xC3); } while(0)
                {
                    static const struct { uint16_t slot,code,cell; unsigned args; uint16_t list; } near_[3]={
                        {0x0CF3,0xF200,0xF800,2,0},{0xE586,0xF260,0xF804,1,0xF840},{0x0CDD,0xF2C0,0xF808,1,0}};
                    for(unsigned k=0;k<3;k++) {
                        XW8(0,near_[k].slot,0xE9);XW16(0,near_[k].slot+1,(uint16_t)(near_[k].code-near_[k].slot-3));
                        XSTUB(0,near_[k].code,near_[k].cell,near_[k].args,0,near_[k].list,0);
                    }
                }
#undef XSTUB
#undef PICK
#undef PCT
#undef RND
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"START.EXE") && o->seg==0 && o->ip==0x0963) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The routine takes no arguments; the words above SP are noise,
                 * and the harness's small[] copies of them match. */
                for(unsigned j=0;j<8;j++) {
                    const uint16_t w=(uint16_t)XRAND();
                    XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w);
                    if(j<4)small[j]=w;
                }
                /* The slot table behind [E096] and the table behind [CACA]: each a
                 * far pointer into a segment of its own, into the data segment or
                 * into the stack's; the second sometimes the same pointer as the
                 * first (so its word at +42h is the first table's slot 1 word at
                 * +40h + 2). Offsets sometimes sit at the top of the segment so
                 * that the sums wrap. */
                uint16_t pseg[2],poff[2];
                for(unsigned t=0;t<2;t++) {
                    static const uint16_t sg[4]={0x7000,0x7200,0x5000,0x6000};
                    pseg[t]=sg[XPICK(4)];
                    if(pseg[t]==0x7000||pseg[t]==0x7200) {
                        switch(XPICK(4)) {
                            case 0: poff[t]=(uint16_t)(0xFF80+XPICK(0x80)); break;
                            case 1: poff[t]=(uint16_t)XRAND(); break;
                            default: poff[t]=(uint16_t)XPICK(0x400); break;
                        }
                    } else poff[t]=(uint16_t)(0x0400+XPICK(0x2000));
                }
                if(XPICK(5)==0){pseg[1]=pseg[0];poff[1]=poff[0];}
                XW16(dg,0xE096,poff[0]);XW16(dg,0xE098,pseg[0]);
                XW16(dg,0xCACA,poff[1]);XW16(dg,0xCACC,pseg[1]);
                for(unsigned t=0;t<2;t++)
                    for(unsigned j=0;j<0x60;j++)XW8(pseg[t],(uint16_t)(poff[t]+j),XRAND());
                /* Each slot's word at +38h is the filled marker 12h about half the
                 * time, and sometimes a neighbour of it. The word at +42h (for
                 * [CACA]) is zero about half the time. */
                for(unsigned n=0;n<4;n++) {
                    uint16_t v;
                    switch(XPICK(6)) {
                        case 0: case 1: case 2: v=0x12; break;
                        case 3: v=(uint16_t)(0x11+2*XPICK(2)); break;
                        case 4: v=(uint16_t)(0x1200+XPICK(2)*0x12); break;
                        default: v=(uint16_t)XRAND(); break;
                    }
                    XW16(pseg[0],(uint16_t)(poff[0]+0x38+2*n),v);
                }
                XW16(pseg[1],(uint16_t)(poff[1]+0x42),(XPICK(2))?(uint16_t)0:(uint16_t)(1+XPICK(0xFFFF)));
                /* The item table at 1EC5h + event. */
                for(unsigned j=0;j<0x30;j++)XW8(dg,0x1EC0+j,XRAND());
                /* The poll's script (list of stub k=1): events for the arms page.
                 * Items 1..12h, slots 13h..16h (repeats of the last slot on
                 * purpose), the ends 17h, 18h, 63h, the dead zone 19h..62h and
                 * everything above, with the boundaries exactly. */
                {
                    static const uint16_t odd[]={0x0000,0x0001,0x0012,0x0013,0x0016,0x0017,0x0018,0x0019,0x0062,0x0063,0x0064,0x0100,0x0113,0x0163,0x8013,0xFFFF,0x7FFF,0x8000,0x0014,0x0015};
                    static const uint16_t end_[]={0x17,0x18,0x63};
                    unsigned nk=(XPICK(4)==0)?(XPICK(2)):(XPICK(14));
                    uint16_t last=0x13;
                    for(unsigned j=0;j<32;j++) {
                        uint16_t key;
                        if(j==nk)key=end_[XPICK(3)];
                        else switch(XPICK(12)) {
                            case 0: case 1: case 2: case 3: key=(uint16_t)(1+XPICK(0x12)); break;
                            case 4: case 5: case 6: key=(uint16_t)(0x13+XPICK(4)); break;
                            case 7: key=last; break;
                            case 8: key=(uint16_t)(0x19+XPICK(0x4A)); break;
                            case 9: key=odd[XPICK(sizeof odd/sizeof odd[0])]; break;
                            case 10: key=(uint16_t)(0x11+XPICK(4)); break;
                            default: key=(uint16_t)XRAND(); break;
                        }
                        /* The terminators 17h/18h/63h before the planned end would cut
                         * the script short; only keep them there half the time. */
                        if(j!=nk&&(key==0x17||key==0x18||key==0x63)&&XPICK(2))key=(uint16_t)(0x19+XPICK(0x40));
                        if(key>=0x13&&key<=0x16)last=key;
                        XW16(dg,0xF400+0x40*1+2*j,key);
                    }
                }
                /* The other callees return noise (only 38AC's result steers). */
                for(unsigned k=0;k<11;k++) {
                    if(k==1)continue;
                    for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*k+2*j,XRAND());
                }
                for(unsigned j=0;j<0x40;j+=2)XW16(dg,0xF800+j,0);
                /* Returning probes: fold a number naming the callee and then every
                 * argument, in order, into one shared cell (F800), count the call
                 * (F802 + 2k), keep a list index (F820 + 2k) and return the next
                 * word of the callee's list (F400 + 40h * k). */
#define XSTUB(sg_,off_,k_,id_,nargs_,far_) do { \
    uint8_t cb_[200]; unsigned n_=0; \
    const unsigned cnt_=0xF802+2*(k_),ptr_=0xF820+2*(k_),list_=0xF400+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(id_)&255;cb_[n_++]=(id_)>>8; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
        cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; } \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x83;cb_[n_++]=0xC3;cb_[n_++]=0x02; \
    cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
                {
                    /* k: 0 3641, 1 38AC, 2 3989, 3 the driver (far), 4 35FC, 5 2B1B,
                     * 6 405F, 7 3588, 8 0D65, 9 0E96, 10 3517. */
                    static const struct { uint16_t entry; uint8_t args; } nb_[11]={
                        {0x3641,2},{0x38AC,1},{0x3989,0},{0,1},{0x35FC,2},{0x2B1B,6},
                        {0x405F,1},{0x3588,0},{0x0D65,2},{0x0E96,0},{0x3517,3}};
                    for(unsigned k=0;k<11;k++) {
                        const uint16_t code=(uint16_t)(0xF200+0x90*k);
                        const uint16_t id=(uint16_t)(0x0A51+0x1111*k);
                        if(k==3) {
                            XSTUB(0x0A95,0x6BB1,k,id,nb_[k].args,1);
                        } else {
                            XW8(0,nb_[k].entry,0xE9);XW16(0,nb_[k].entry+1,(uint16_t)(code-nb_[k].entry-3));
                            XSTUB(0,code,k,id,nb_[k].args,0);
                        }
                    }
                }
#undef XSTUB
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"START.EXE") && o->seg==0 && o->ip==0xA59A) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
                /* The stack below SP: noise (the locals sit here). [bp-1] is never written by
                 * the routine, only read after the DOS read: 1Ah half of the time. The entry's
                 * return address goes at SP-2, the saved BP at SP-4, so BP-1 is SP-5. */
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                if(XRAND()&1)XW8(seg[S_SS],(uint16_t)(r[R_SP]-5),0x1A);
                /* The four arguments: path, oflag, shflag, pmode. */
                {
                    uint16_t path=(uint16_t)XRAND();
                    uint16_t oflag,share,pmode;
                    if(XRAND()%8==0)oflag=(uint16_t)XRAND();
                    else {
                        oflag=(uint16_t)(XRAND()&3);
                        if(XRAND()&1)oflag|=0x0008;
                        if(XRAND()&1)oflag|=0x0100;
                        if(XRAND()&1)oflag|=0x0200;
                        if(XRAND()&1)oflag|=0x0400;
                        if(XRAND()%3==0)oflag|=0x4000;
                        if(XRAND()%3==0)oflag|=0x8000;
                    }
                    switch(XRAND()%4) {
                        case 0: case 1: share=0; break;
                        case 2: share=(uint16_t)(XRAND()&0xFF); break;
                        default: share=(uint16_t)XRAND(); break;
                    }
                    switch(XRAND()%4) {
                        case 0: pmode=0; break;
                        case 1: pmode=0x80; break;
                        case 2: pmode=0x180; break;
                        default: pmode=(uint16_t)XRAND(); break;
                    }
                    XW16(seg[S_SS],r[R_SP],path);XW16(seg[S_SS],r[R_SP]+2,oflag);
                    XW16(seg[S_SS],r[R_SP]+4,share);XW16(seg[S_SS],r[R_SP]+6,pmode);
                    small[0]=path;small[1]=oflag;small[2]=share;small[3]=pmode;
                }
                for(unsigned j=0;j<8;j++)XW16(seg[S_SS],(uint16_t)(r[R_SP]+8+2*j),(uint16_t)XRAND());
                /* The runtime's data: noise, then the cells the routine and its callees read. */
                for(unsigned j=0;j<0x3C0;j++)XW8(dg,0xAE40+j,XRAND());
                switch(XRAND()%8) {                               /* the DOS version: below 3 or not, 3 exactly */
                    case 0: XW8(dg,0xAE5A,2); break;
                    case 1: XW8(dg,0xAE5A,0); break;
                    case 2: XW8(dg,0xAE5A,1); break;
                    case 3: XW8(dg,0xAE5A,(XRAND()&1)?0xFF:0x80); break;
                    case 4: XW8(dg,0xAE5A,(uint8_t)(4+XRAND()%6)); break;
                    default: XW8(dg,0xAE5A,3); break;
                }
                switch(XRAND()%4) {                               /* the permission mask */
                    case 0: XW16(dg,0xAE54,0); break;
                    case 1: XW16(dg,0xAE54,0x80); break;
                    case 2: XW16(dg,0xAE54,0xFF7F); break;
                    default: XW16(dg,0xAE54,(uint16_t)XRAND()); break;
                }
                switch(XRAND()%8) {                               /* the handle count */
                    case 0: XW16(dg,0xAE5F,0); break;
                    case 1: case 2: XW16(dg,0xAE5F,(uint16_t)(1+XRAND()%4)); break;
                    case 3: XW16(dg,0xAE5F,(uint16_t)(XRAND()%0x20)); break;
                    case 4: XW16(dg,0xAE5F,(uint16_t)(0x20+XRAND()%0x2000)); break;
                    case 5: XW16(dg,0xAE5F,0xFFFF); break;
                    default: XW16(dg,0xAE5F,(uint16_t)XRAND()); break;
                }
                XW8(dg,0xB16B,XRAND());                           /* the default mode, bit 7 */
                /* DOS's answers, in the order the calls are made: the open fails with "not found"
                 * or another error about half of the time, later calls answer carry one time in
                 * three, AX is a small handle or a count, 0 (read: end of file), 2, or noise. */
                g_dos_script_n=10;
                for(unsigned j=0;j<10;j++) {
                    unsigned k=XRAND()%10;
                    uint16_t ax;
                    if(k<2) ax=0; else if(k<4) ax=(uint16_t)(1+XRAND()%5);
                    else if(k<6) ax=2; else if(k<8) ax=(uint16_t)(XRAND()%0x20); else ax=(uint16_t)XRAND();
                    g_dos_script[j].ax=ax;
                    g_dos_script[j].cf=(j==0)?(XRAND()%5<2):(XRAND()%3==0);
                }
                /* the errno table (xlatb at AE92) is the noise written above */
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0xF5F2) {
                const uint16_t dg=0x5000;
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define RND(n_) ((unsigned)(XRAND()%(unsigned)(n_)))
#define PCT(p_) (RND(100)<(unsigned)(p_))
#define PICK(a_) ((a_)[RND(sizeof(a_)/sizeof((a_)[0]))])
                seg[S_DS]=dg;seg[S_SS]=PCT(50)?dg:0x6000;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                /* The stack below SP (return address, saved BP and the four
                 * local bytes at SP-8..SP-5) starts random in both segments. */
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                for(unsigned j=0;j<0x200;j++)XW8(dg,(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The four arguments: name, open flags, sharing, permission. */
                uint16_t arg_[4];
                arg_[0]=(uint16_t)XRAND();
                if(PCT(50))arg_[0]=(uint16_t)((arg_[0]&0xFF7F)|(PCT(50)?0x80:0));                 /* DL bit 7: a device */
                { unsigned of_=RND(4);                                                           /* access mode */
                  if(PCT(35))of_|=0x0008;
                  if(PCT(55))of_|=0x0100;
                  if(PCT(45))of_|=0x0200;
                  if(PCT(40))of_|=0x0400;
                  if(PCT(35))of_|=0x4000;
                  if(PCT(35))of_|=0x8000;
                  if(PCT(8))of_^=(unsigned)XRAND();
                  if(PCT(6))of_=(unsigned)XRAND();
                  arg_[1]=(uint16_t)of_; }
                { static const uint16_t share_[]={0,0,0,0x10,0x20,0x30,0x40,0x70,0x01,0x80};
                  arg_[2]=(uint16_t)((XRAND()&0xFF00)|(PCT(55)?0:PICK(share_)));
                  if(PCT(25))arg_[2]=(uint16_t)XRAND(); }
                { static const uint16_t perm_[]={0,1,0x80,0x180,0x100,0x1A4,0x81,0xFF7F,0xFFFF,0x7F};
                  arg_[3]=PCT(55)?PICK(perm_):(uint16_t)XRAND(); }
                for(unsigned j=0;j<4;j++){XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),arg_[j]);small[j]=arg_[j];}
                /* The last-byte local [bp-1] (SP-5): Ctrl-Z about half the time. */
                { static const uint8_t last_[]={0x19,0x1B,0x00,0xFF,0x9A};
                  XW8(seg[S_SS],(uint16_t)(r[R_SP]-5),PCT(50)?0x1A:PCT(30)?PICK(last_):(uint8_t)XRAND()); }
                /* The C runtime's cells: DOS major version around its floor of
                 * 3, the default-mode byte, the handle table size (a handle from
                 * DOS is mostly large, sometimes 0-3), the permission mask, the
                 * error cells and the error table, and the handle flag table. */
                uint8_t dosver_;
                { static const uint8_t dosv_[]={0,1,2,3,3,3,4,5,6,7,0xFF,0x10};
                  dosver_=PCT(85)?PICK(dosv_):(uint8_t)XRAND();
                  XW8(dg,0x926A,dosver_); }
                { static const uint8_t dflt_[]={0x00,0x80,0xFF,0x7F,0x01,0xC0};
                  XW8(dg,0x9401,PCT(80)?PICK(dflt_):(uint8_t)XRAND()); }
                { static const uint16_t nfile_[]={0,1,2,3,4,0xFFFF,0x8000,0x7FFF,20,0x0100};
                  XW16(dg,0x926F,PCT(75)?PICK(nfile_):(uint16_t)XRAND()); }
                { static const uint16_t umask_[]={0,0xFFFF,0x0080,0xFF7F,0x007F,0x0180,0x0100,0x8000,0x00FF};
                  XW16(dg,0x9264,PCT(70)?PICK(umask_):(uint16_t)XRAND()); }
                XW16(dg,0x9262,XRAND());XW16(dg,0x926D,XRAND());
                for(unsigned j=0;j<0x40;j++)XW8(dg,0x92A4+j,XRAND());
                for(unsigned j=0;j<0x20;j++)XW8(dg,0x9271+j,XRAND());
                /* INT 21h is answered by the harness' DOS (g_dos): AX and CF come from a
                 * hash of every register at the call and of the call count, so AX is never
                 * 2 by chance and the "file not found" branch (open fails with AX 2) would
                 * stay unreached. In a share of the states the registers the routine does
                 * not use (ES, SI, DI) are searched until the FIRST call (the open, whose
                 * registers are all known here) is answered with AX 2 and CF set. */
                g_dos=1;
                if(PCT(16)){
                    const uint8_t bh_=(dosver_>=3)?(uint8_t)arg_[2]:0;
                    cpu_t sc_=g_cpu[0];sc_.flags=flags;
                    alu_logic(&sc_,(uint32_t)(((arg_[1]&3)|bh_)&0xFF),0);          /* the OR AL, BH before the call */
                    const uint16_t fl_=sc_.flags;
                    const uint16_t reg_[6]={(uint16_t)(0x3D00|(((arg_[1]&3)|bh_)&0xFF)),arg_[1],arg_[0],(uint16_t)((bh_<<8)|(r[R_BX]&0xFF)),0xEFF8,0xEFFC};
                    uint64_t h0_=mix(0,0x3000000ull|0x21);
                    for(unsigned j=0;j<6;j++)h0_=mix(h0_,reg_[j]);
                    unsigned si_=(unsigned)XRAND(),di_=(unsigned)XRAND(),es_=(unsigned)XRAND();
                    int found_=0;
                    for(unsigned n_=0;n_<4000000u&&!found_;n_++){
                        const uint16_t sj_=(uint16_t)(si_+(n_>>24));
                        uint64_t h1_=mix(mix(h0_,sj_),(uint16_t)(di_+(n_>>16)));
                        h1_=mix(h1_,(uint16_t)(es_+n_));
                        h1_=mix(h1_,cs);h1_=mix(h1_,seg[S_SS]);h1_=mix(h1_,seg[S_DS]);
                        h1_=mix(h1_,((uint64_t)0xF640<<16)|fl_);
                        const uint64_t v_=mix(h1_,1);
                        if(((v_>>16)&0xFFFF)==2&&((v_>>58)&3)!=0&&((v_>>60)&3)==0){
                            r[R_SI]=sj_;r[R_DI]=(uint16_t)(di_+(n_>>16));seg[S_ES]=(uint16_t)(es_+n_);found_=1;
                        }
                    }
                }
#undef PICK
#undef PCT
#undef RND
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"START.EXE") && o->seg==0 && o->ip==0x008D) {
                const uint16_t dg=0x5000;
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                seg[S_DS]=dg;seg[S_SS]=XPICK(2)?(uint16_t)0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The routine takes no arguments; the words above SP are noise,
                 * and the harness's small[] copies of them match. */
                for(unsigned j=0;j<8;j++) {
                    const uint16_t w=(uint16_t)XRAND();
                    XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w);
                    if(j<4)small[j]=w;
                }
                /* The six string tables and the rank table (random words, only
                 * passed on to the stubs): a neighbourhood of each, so that a
                 * table address one word off reads a different value. */
                static const uint16_t tb[7]={0x1D78,0x9ABE,0xA155,0x9522,0x92FA,0x94D0,0x94AC};
                static const uint16_t fo[7]={0x20,0x38,0x3A,0x3C,0x3E,0x40,0x42};
                for(unsigned t=0;t<7;t++)
                    for(unsigned j=0;j<0x110;j++)XW8(dg,(uint16_t)(tb[t]-8+j),XRAND());
                /* The record table behind the far pointer [CACA]: in a segment of
                 * its own, in the data segment or in the stack's. Offsets sometimes
                 * sit at the top of the segment so that the sums wrap. */
                uint16_t pseg,poff;
                {
                    static const uint16_t sg[4]={0x7000,0x7200,0x5000,0x6000};
                    pseg=sg[XPICK(4)];
                    if(pseg==0x7000||pseg==0x7200) {
                        switch(XPICK(4)) {
                            case 0: poff=(uint16_t)(0xFF80+XPICK(0x80)); break;
                            case 1: poff=(uint16_t)XRAND(); break;
                            default: poff=(uint16_t)XPICK(0x400); break;
                        }
                    } else if(pseg==0x5000) poff=(uint16_t)(0xD400+XPICK(0x600));
                    else poff=(uint16_t)(0xB000+XPICK(0x1000));
                }
                XW16(dg,0xCACA,poff);XW16(dg,0xCACC,pseg);
                for(unsigned j=0;j<0x60;j++)XW8(pseg,(uint16_t)(poff+j),XRAND());
                /* The word at the start (multiplied by 50h): small, around the
                 * point where 50h * w passes 10000h, or anything. */
                {
                    uint16_t w;
                    switch(XPICK(6)) {
                        case 0: w=(uint16_t)(0x332+XPICK(4)); break;
                        case 1: w=(uint16_t)XRAND(); break;
                        case 2: w=(uint16_t)(0x7FFF+XPICK(3)); break;
                        default: w=(uint16_t)XPICK(12); break;
                    }
                    XW16(pseg,poff,w);
                }
                /* The seven index words, each doubled: mostly small, sometimes at
                 * the doubling's edge, sometimes anything; a word's table entry
                 * is then a fresh random word (and its neighbours), unless the
                 * address would hit the stack, the stubs' cells, the pointer or
                 * the record table. */
                for(unsigned t=0;t<7;t++) {
                    uint16_t w,addr;
                    for(unsigned tries=0;;tries++) {
                        switch(XPICK(8)) {
                            case 0: w=(uint16_t)(0x7FFF+XPICK(2)); break;
                            case 1: w=(uint16_t)(0xFFFF-XPICK(3)); break;
                            case 2: w=(uint16_t)XRAND(); break;
                            case 3: w=(uint16_t)(0x4000+XPICK(0x400)); break;
                            default: w=(uint16_t)XPICK(0x40); break;
                        }
                        if(tries>=64)w=(uint16_t)XPICK(0x40);
                        addr=(uint16_t)(2*w+tb[t]);
                        if(addr<0xE9E0
                           && !(addr+3>=0xCAC0 && addr<0xCAD0)
                           && !(addr+3>=0xD2A0 && addr<0xD2A8)
                           && !(addr+3>=0xDC10 && addr<0xDC30)
                           && !(pseg==dg && (uint16_t)(addr+3-poff)<0x68))break;
                    }
                    XW16(pseg,(uint16_t)(poff+fo[t]),w);
                    for(unsigned j=0;j<6;j++)XW8(dg,(uint16_t)(addr-2+j),XRAND());
                }
                /* The byte [D2A2] picks the pointer position: zero half the time,
                 * else 1 or anything, its neighbours noise. */
                {
                    uint8_t b;
                    switch(XPICK(6)) {
                        case 0: case 1: case 2: b=0; break;
                        case 3: b=1; break;
                        case 4: b=(uint8_t)(0x80+XPICK(2)*0x7F); break;
                        default: b=(uint8_t)(1+XPICK(255)); break;
                    }
                    XW8(dg,0xD2A1,XRAND());XW8(dg,0xD2A2,b);XW8(dg,0xD2A3,XRAND());
                }
                XW16(dg,0xDC24,XRAND());XW16(dg,0xDC1C,XRAND());
                /* The callees return noise (nothing steers); each stub folds a
                 * number naming it and then every argument, in order, into one
                 * shared cell (F800), counts its calls (F802 + 2k), keeps a list
                 * index (F820 + 2k) and returns the next word of its list. */
                for(unsigned k=0;k<6;k++)for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*k+2*j,XRAND());
                for(unsigned j=0;j<0x40;j+=2)XW16(dg,0xF800+j,0);
#define XSTUB(sg_,off_,k_,id_,nargs_,far_) do { \
    uint8_t cb_[200]; unsigned n_=0; \
    const unsigned cnt_=0xF802+2*(k_),ptr_=0xF820+2*(k_),list_=0xF400+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(id_)&255;cb_[n_++]=(id_)>>8; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
        cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; } \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x83;cb_[n_++]=0xC3;cb_[n_++]=0x02; \
    cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
                {
                    /* k: 0 far driver 0A95:6A8A (8 words), 1 3E84 (2), 2 6A7C (1),
                     * 3 959E (4: the second call has three, the fourth word is then
                     * the buffer's first word), 4 3567 (4), 5 02BA (0). */
                    static const struct { uint16_t entry,code,id; uint8_t args; } nb_[6]={
                        {0,0,0x6B07,8},{0x3E84,0xF300,0x1B62,2},{0x6A7C,0xF400,0x2C73,1},
                        {0x959E,0xF500,0x3D84,4},{0x3567,0xF600,0x4E95,4},{0x02BA,0xF700,0x5FA6,0}};
                    XSTUB(0x0A95,0x6A8A,0,nb_[0].id,nb_[0].args,1);
                    for(unsigned k=1;k<6;k++) {
                        XW8(0,nb_[k].entry,0xE9);XW16(0,nb_[k].entry+1,(uint16_t)(nb_[k].code-nb_[k].entry-3));
                        XSTUB(0,nb_[k].code,k,nb_[k].id,nb_[k].args,0);
                    }
                }
#undef XSTUB
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0xBB0D) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The view: two modes ([C0A6] bit 7), the origins and the second view's
                 * values, with edges that make carries and borrows of the long sums. */
                const unsigned sv=XPICK(2);
                XW8(dg,0xC0A6,(sv?0x80:0)|(XRAND()&0x7F));
                {
                    static const uint16_t org[10]={0xBA3C,0xBA3E,0xC056,0xC058,0xB2B4,0xB2B6,0xB77E,0xB780,0xB788,0x2DF4};
                    for(unsigned j=0;j<10;j++) {
                        uint16_t v;
                        switch(XPICK(7)) {
                        case 0: v=0; break;
                        case 1: v=0xFFFF; break;
                        case 2: v=0x8000; break;
                        case 3: v=0x7FFF; break;
                        case 4: v=(uint16_t)(XRAND()&0x0FFF); break;
                        default: v=(uint16_t)XRAND(); break;
                        }
                        XW16(dg,org[j],v);
                    }
                }
                {
                    const uint16_t b294=(XPICK(10)<4)?0:(uint16_t)(XPICK(2)?1:(2+XPICK(254)));
                    XW8(dg,0x294B,b294);
                    XW8(dg,0x40B6,XPICK(2)?0:(uint8_t)(1+XPICK(255)));
                    XW16(dg,0x4010,XRAND());XW16(dg,0x4028,XRAND());
                    XW16(dg,0xE328,XRAND());
                    for(unsigned j=0;j<3;j++)XW16(dg,0x49AC+2*j,XRAND());
                    for(unsigned j=0;j<0x80;j++)XW16(dg,0x0510+2*j,XRAND());
                    const int base=(b294==0)?3:2;
                    /* The values the routine reads back from the data segment. */
#define XRD16(o_) ((uint32_t)(g_mem[0][phys(dg,(uint16_t)(o_))]|(g_mem[0][phys(dg,(uint16_t)(o_))+1]<<8)))
                    const uint32_t ox=XRD16(0xBA3C)|(XRD16(0xBA3E)<<16);
                    const uint32_t oy=XRD16(0xC056)|(XRD16(0xC058)<<16);
                    const uint32_t ox2=XRD16(0xB2B4)|(XRD16(0xB2B6)<<16);
                    const uint32_t oy2=XRD16(0xB77E)|(XRD16(0xB780)<<16);
                    const uint16_t oz=(uint16_t)XRD16(0x2DF4),oz2=(uint16_t)XRD16(0xB788);
                    /* The scale: net shift sh = exponent - base, mostly small, both signs, and
                     * a stretch of wild exponents (including wrap-around). */
                    int sh;
                    uint16_t e;
                    const unsigned wild=(XPICK(16)==0);
                    switch(XPICK(10)) {
                    case 0: case 1: case 2: sh=0; break;
                    case 3: case 4: sh=1+(int)XPICK(6); break;
                    case 5: sh=7+(int)XPICK(20); break;
                    case 6: case 7: case 8: sh=-(int)(1+XPICK(12)); break;
                    default: sh=-(int)(13+XPICK(18)); break;
                    }
                    e=(uint16_t)(sh+base);
                    if(wild) {
                        switch(XPICK(4)) {
                        case 0: e=0x8000; break;
                        case 1: e=0x7FFF; break;
                        case 2: e=(uint16_t)(0x8000+XPICK(4)); break;
                        default: e=(uint16_t)XRAND(); break;
                        }
                    }
                    /* The positions after scaling, as the range test sees them (low word at
                     * the edges 7FFE..8001); then the values before scaling that give them. */
                    uint32_t px=0,py=0;
                    for(unsigned k=0;k<2;k++) {
                        uint16_t lo;
                        uint32_t post;
                        if(XPICK(5)<2) {
                            static const uint16_t edge[12]={0x7FFE,0x7FFF,0x8000,0x8001,0xFFFF,0x0000,0x0001,0xFFFE,0x7FFD,0x8002,0xFFFD,0x7FFC};
                            lo=edge[XPICK(12)];
                        } else if(XPICK(2)) lo=(uint16_t)(XRAND()&0x0FFF);
                        else lo=(uint16_t)(-(int)(XRAND()&0x0FFF));
                        switch(XPICK(4)) {
                        case 0: post=(uint32_t)lo|((uint32_t)XRAND()<<16); break;
                        default: post=(uint32_t)lo|((lo&0x8000)?0xFFFF0000u:0u); break;
                        }
                        uint32_t pre;
                        if(sh>0) pre=(uint32_t)((int32_t)post>>(sh>31?31:sh));
                        else if(sh<0) pre=post<<((-sh)>31?31:(-sh));
                        else pre=post;
                        if(k==0)px=pre; else py=pre;
                    }
                    uint16_t zw;
                    const uint16_t zpre=(XPICK(3)==0)?(uint16_t)XRAND():(uint16_t)(XRAND()&0x00FF);
                    uint32_t wx=px+(sv?ox2:ox),wy=py+(sv?oy2:oy)-0x01000000u;
                    zw=(uint16_t)(zpre+(sv?oz2:oz));
                    switch(XPICK(10)) {
                    case 0: case 1: zw=0; break;
                    case 2: zw=1; break;
                    default: break;
                    }
                    if(wild&&XPICK(2)) { wx=((uint32_t)XRAND()<<16)|XRAND();wy=((uint32_t)XRAND()<<16)|XRAND(); }
                    if(XPICK(24)==0) { wx=((uint32_t)XRAND()<<16)|XRAND(); }
                    if(XPICK(24)==0) { wy=((uint32_t)XRAND()<<16)|XRAND(); }
                    uint16_t model;
                    if(XPICK(2)) model=(uint16_t)(0x0100|(XRAND()&0x80FF));
                    else model=(uint16_t)(XRAND()&0xFEFF);
                    uint16_t arg[10]={model,(uint16_t)wx,(uint16_t)(wx>>16),(uint16_t)wy,(uint16_t)(wy>>16),zw,
                                      (uint16_t)XRAND(),(uint16_t)XRAND(),(uint16_t)XRAND(),e};
                    for(unsigned j=0;j<10;j++)XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),arg[j]);
                    for(unsigned j=0;j<4;j++)small[j]=arg[j];
#undef XRD16
                }
                /* The model drawing routine E2B6 takes seven words: a returning probe folds
                 * every argument in order into a guest cell (+2), counts the calls (+0) and
                 * returns the next word of a list (the index is at +4). The other callees
                 * (C436, D9E7, EF80, EF9C, EE0C) are arithmetic or setters and stay original. */
                for(unsigned j=0;j<32;j++)XW16(dg,0xF440+2*j,XRAND());
                for(unsigned j=0;j<8;j++)XW16(dg,0xF800+2*j,0);
#define XSTUB(off_,cell_,nargs_,list_) do { unsigned out_=(off_);const uint16_t sgx_=0; \
    XW8(sgx_,out_++,0x55);XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0xEC);XW8(sgx_,out_++,0x53); \
    XW8(sgx_,out_++,0xFF);XW8(sgx_,out_++,0x06);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x46);XW8(sgx_,out_++,4+2*j_); \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+2);XW8(sgx_,out_++,((cell_)+2)>>8); \
        XW8(sgx_,out_++,0xD1);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x33);XW8(sgx_,out_++,0xD8); \
        XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+2);XW8(sgx_,out_++,((cell_)+2)>>8); } \
    XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+4);XW8(sgx_,out_++,((cell_)+4)>>8); \
    XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x87);XW8(sgx_,out_++,(list_));XW8(sgx_,out_++,(list_)>>8); \
    XW8(sgx_,out_++,0x83);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x02); \
    XW8(sgx_,out_++,0x83);XW8(sgx_,out_++,0xE3);XW8(sgx_,out_++,0x3E); \
    XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+4);XW8(sgx_,out_++,((cell_)+4)>>8); \
    XW8(sgx_,out_++,0x5B);XW8(sgx_,out_++,0x5D);XW8(sgx_,out_++,0xC3); } while(0)
                {
                    const uint16_t slot=0xE2B6,code=0xF200;
                    XW8(0,slot,0xE9);XW16(0,slot+1,(uint16_t)(code-slot-3));
                    XSTUB(code,0xF800,7,0xF440);
                }
#undef XSTUB
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"START.EXE") && o->seg==0 && o->ip==0x64E5) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The argument: the pilot's slot. Mostly 0..9 (the ten pilots),
                 * sometimes beyond, negative or anything, but never one whose
                 * record would overlap the stack or the stubs' cells. */
                unsigned slot;
                switch(XPICK(10)) {
                    case 0: slot=10+XPICK(6); break;
                    case 1: slot=(uint16_t)(-1-(int)XPICK(4)); break;
                    case 2: slot=XRAND()&0xFFFF; break;
                    default: slot=XPICK(10); break;
                }
                {
                    uint16_t rec_=(uint16_t)(0xD892+0x50*(int16_t)slot);
                    if(rec_>=0xEDC0 || rec_<0x0040 || (uint16_t)(rec_+0x50)<rec_) slot=XPICK(10);
                }
                const uint16_t rec=(uint16_t)(0xD892+0x50*(int16_t)slot);
                XW16(seg[S_SS],r[R_SP],slot);small[0]=(uint16_t)slot;
                for(unsigned j=1;j<10;j++) {
                    const uint16_t w=(uint16_t)XRAND();
                    XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w);
                    if(j<4)small[j]=w;
                }
                /* The tables the record's words index: the rank strings'
                 * pointers (1D78h) and the three attribute tables (931Eh,
                 * 94F0h, 94ACh); random words, they are only passed on. */
                for(unsigned j=0;j<0x120;j++)XW8(dg,0x1D78 + j,XRAND());
                for(unsigned j=0;j<0x120;j++)XW8(dg,0x931E + j,XRAND());
                for(unsigned j=0;j<0x120;j++)XW8(dg,0x94F0 + j,XRAND());
                for(unsigned j=0;j<0x120;j++)XW8(dg,0x94AC + j,XRAND());
                /* The ten records and the one the slot names. */
                for(unsigned j=0;j<0x50*10;j++)XW8(dg,0xD892+j,XRAND());
                for(unsigned j=0;j<0x50;j++)XW8(dg,(uint16_t)(rec+j),XRAND());
                /* The window words the routine overwrites start random. */
                XW16(dg,0xDC24,XRAND());XW16(dg,0xDC1C,XRAND());
                {
                    /* The four indices: mostly small, sometimes large so that
                     * the doubled index wraps, sometimes at the boundary. */
                    static const uint16_t ixo[4]={0x20,0x3E,0x40,0x42};
                    for(unsigned n=0;n<4;n++) {
                        unsigned v;
                        switch(XPICK(6)) {
                            case 0: v=XPICK(0x90); break;
                            case 1: v=XRAND()&0xFFFF; break;
                            case 2: v=0x7FFF+XPICK(2); break;
                            default: v=XPICK(8); break;
                        }
                        XW16(dg,(uint16_t)(rec+ixo[n]),v);
                    }
                    XW16(dg,(uint16_t)(rec+0x2E),XRAND());
                    XW16(dg,(uint16_t)(rec+0x30),XRAND());
                    /* The six award counts, in the order the routine tests
                     * them. Each is zero about half the time; a state in
                     * eight has all six non-zero, one in eight none. Non-zero
                     * values include 1, FFFFh and ones with a zero low byte
                     * (a byte test would miss them). */
                    static const uint16_t awo[6]={0x2C,0x26,0x2A,0x28,0x24,0x22};
                    const unsigned mode=XPICK(8);
                    for(unsigned n=0;n<6;n++) {
                        unsigned v;
                        const unsigned zero=(mode==0)?1:(mode==1)?0:(XPICK(2)==0);
                        if(zero) v=0;
                        else switch(XPICK(6)) {
                            case 0: v=1; break;
                            case 1: v=0xFFFF; break;
                            case 2: v=0x100*(1+XPICK(255)); break;
                            case 3: v=0x8000; break;
                            default: v=XRAND()|1; break;
                        }
                        XW16(dg,(uint16_t)(rec+awo[n]),v);
                    }
                }
                /* The callees' replies. Each stub keeps a count (F802 + 2k), a
                 * list index (F820 + 2k) and folds a number naming it and then
                 * every argument into one shared cell (F800), so an argument,
                 * its order, or the order of the calls shows. */
                for(unsigned j=0;j<0x40;j+=2)XW16(dg,0xF800+j,0);
                for(unsigned k=0;k<5;k++)for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*k+2*j,XRAND());
#define XSTUB(sg_,off_,k_,id_,nargs_,far_) do { \
    uint8_t cb_[200]; unsigned n_=0; \
    const unsigned cnt_=0xF802+2*(k_),ptr_=0xF820+2*(k_),list_=0xF400+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(id_)&255;cb_[n_++]=(id_)>>8; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
        cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; } \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x83;cb_[n_++]=0xC3;cb_[n_++]=0x02; \
    cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
                {
                    /* The four near callees (their entries patched to jump to
                     * the stubs in segment 0): 0x959E (sprintf), 0x3567 (text),
                     * 0x2A84 (centred text), 0x6A7C (last word of a string,
                     * one argument), and the far driver call 0A95:6A8A. */
                    static const struct { uint16_t entry,code,id; uint8_t args; } nb_[4]={
                        {0x959E,0xF200,0x0A51,4},{0x3567,0xF300,0x1B62,4},
                        {0x2A84,0xF400,0x2C73,4},{0x6A7C,0xF500,0x3D84,1}};
                    XSTUB(0x0A95,0x6A8A,0,0x6B07,8,1);
                    for(unsigned k=0;k<4;k++) {
                        XW8(0,nb_[k].entry,0xE9);XW16(0,nb_[k].entry+1,(uint16_t)(nb_[k].code-nb_[k].entry-3));
                        XSTUB(0,nb_[k].code,k+1,nb_[k].id,nb_[k].args,0);
                    }
                }
#undef XSTUB
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"START.EXE") && o->seg==0 && o->ip==0x74B1) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                /* The stack pointer: the usual place, a random one, and the low
                 * values where the pushes wrap below zero. With the stack in the
                 * data segment, keep it clear of the stubs' cells (F3C0..F900). */
                switch(XPICK(6)) {
                    case 0: r[R_SP]=0xF000; break;
                    case 1: r[R_SP]=(uint16_t)(0x0100+2*XPICK(0x1800)); break;
                    case 2: r[R_SP]=(uint16_t)(2*XPICK(0x10)); break;
                    case 3: r[R_SP]=(uint16_t)(0xFA00+2*XPICK(0x100)); break;
                    case 4: r[R_SP]=(uint16_t)(0xFFE0+2*XPICK(0x10)); break;
                    default: r[R_SP]=(uint16_t)(0x4000+2*XPICK(0x1000)); break;
                }
                if(seg[S_SS]==dg && r[R_SP]>0xF300 && r[R_SP]<0xF980) r[R_SP]=0xF000;
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The routine takes no arguments; the words above SP are noise,
                 * and the harness's small[] copies of them match. */
                for(unsigned j=0;j<8;j++) {
                    const uint16_t w=(uint16_t)XRAND();
                    XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w);
                    if(j<4)small[j]=w;
                }
                /* The ten data words the handles go to: noise first. */
                {
                    static const uint16_t cell[10]={0xD29E,0xD330,0xCABA,0xD32E,0xD08E,0xD88E,0xE082,0xE09A,0xE09C,0xCAC4};
                    for(unsigned j=0;j<10;j++)XW16(dg,cell[j],XRAND());
                }
                /* The callees' lists: noise, except the two driver answers that
                 * steer the routine (k=1 for 6B34, k=4 for 6B3E): the first word
                 * of each is zero about half the time, otherwise a nonzero word
                 * from the edge values (a zero low byte, a lone sign bit...). */
                for(unsigned k=0;k<6;k++)
                    for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*k+2*j,XRAND());
                for(unsigned k=1;k<=4;k+=3) {
                    static const uint16_t nz[]={0x0001,0x0100,0x8000,0xFFFF,0x00FF,0x7FFF,0x0080,0x8001};
                    uint16_t v;
                    if(XPICK(2)==0)v=0;
                    else if(XPICK(2)==0)v=nz[XPICK(sizeof nz/sizeof nz[0])];
                    else v=(uint16_t)(1+XPICK(0xFFFF));
                    XW16(dg,0xF400+0x40*k,v);
                }
                for(unsigned j=0;j<0x40;j+=2)XW16(dg,0xF800+j,0);
                /* Returning probes: fold a number naming the callee and then every
                 * argument, in order, into one shared cell (F800), count the call
                 * (F802 + 2k), keep a list index (F820 + 2k) and return the next
                 * word of the callee's list (F400 + 40h * k). */
#define XSTUB(sg_,off_,k_,id_,nargs_,far_) do { \
    uint8_t cb_[200]; unsigned n_=0; \
    const unsigned cnt_=0xF802+2*(k_),ptr_=0xF820+2*(k_),list_=0xF400+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(id_)&255;cb_[n_++]=(id_)>>8; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
        cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; } \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x83;cb_[n_++]=0xC3;cb_[n_++]=0x02; \
    cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
                {
                    /* k: 0 35FC (near, 2 words), 1 0A95:6B34 (far, none), 2 76D1
                     * (near, 1), 3 0A95:6B39 (far, 5), 4 0A95:6B3E (far, none),
                     * 5 3E20 (near, 1). The three driver entries are five bytes
                     * apart (far jumps in the original), so each gets a far jump
                     * to its stub in segment 0 rather than the stub itself. */
                    static const struct { uint16_t entry; uint8_t args; uint8_t far_; } nb_[6]={
                        {0x35FC,2,0},{0x6B34,0,1},{0x76D1,1,0},{0x6B39,5,1},{0x6B3E,0,1},{0x3E20,1,0}};
                    for(unsigned k=0;k<6;k++) {
                        const uint16_t code=(uint16_t)(0xF200+0x90*k);
                        const uint16_t id=(uint16_t)(0x0A51+0x1111*k);
                        if(nb_[k].far_) {
                            XW8(0x0A95,nb_[k].entry,0xEA);XW16(0x0A95,nb_[k].entry+1,code);XW16(0x0A95,nb_[k].entry+3,0);
                        } else {
                            XW8(0,nb_[k].entry,0xE9);XW16(0,nb_[k].entry+1,(uint16_t)(code-nb_[k].entry-3));
                        }
                        XSTUB(0,code,k,id,nb_[k].args,nb_[k].far_);
                    }
                }
#undef XSTUB
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"END.EXE") && o->seg==0 && o->ip==0x0714) {
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                /* END's own data segment 05FA (where the driver's far entry
                 * 05FA:24CC also lies) or a separate one; the stack in the data
                 * segment or in its own. */
                const uint16_t dg=XPICK(2)?0x05FA:0x5000;
                seg[S_DS]=dg;seg[S_SS]=XPICK(2)?0x6000:dg;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                /* The stack pointer: the usual place, random ones, and the edges
                 * where the pushes wrap below zero or the arguments above FFFF.
                 * With the stack in the data segment it stays clear of the event
                 * table, the cells at 55xx, the driver stub and the stubs' cells. */
                switch(XPICK(5)) {
                    case 0: r[R_SP]=0xF000; break;
                    case 1: r[R_SP]=(uint16_t)(0x0300+2*XPICK(0x0800)); break;
                    case 2: r[R_SP]=(uint16_t)(2*XPICK(0x10)); break;
                    case 3: r[R_SP]=(uint16_t)(0xFFE0+2*XPICK(0x10)); break;
                    default: r[R_SP]=(uint16_t)(0x3000+2*XPICK(0x0800)); break;
                }
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The far pointer [55DE] and the word at +3Ah of its block (2 about
                 * half the time, otherwise neighbours of 2 and noise). Offsets
                 * sometimes sit at the top of the segment so that +3Ah wraps. */
                uint16_t mseg,moff;
                {
                    static const uint16_t sg[4]={0x7000,0x7200,0x6000,0};
                    mseg=sg[XPICK(4)];if(!mseg)mseg=dg;
                    if(mseg==0x7000||mseg==0x7200)
                        moff=XPICK(3)==0?(uint16_t)(0xFFC0+XPICK(0x40)):(uint16_t)XRAND();
                    else moff=(uint16_t)(0x4400+XPICK(0x0800));
                }
                uint16_t mode;
                {
                    static const uint16_t near2[]={0,1,3,0x0102,0x8002,0xFFFF,0x0200,0x4002};
                    if(XPICK(2))mode=2;
                    else if(XPICK(3))mode=near2[XPICK(sizeof near2/sizeof near2[0])];
                    else mode=(uint16_t)XRAND();
                }
                /* The event index (argument 0): 0, small, just below the wrap to 0
                 * (the walk then meets index 0 again), or anywhere, so that the
                 * record pointer 6 * index + 5E38h wraps. The records must stay
                 * clear of the cells the routine and the stubs use. */
                uint16_t index;
                switch(XPICK(8)) {
                    case 0: case 1: index=0; break;
                    case 2: case 3: case 4: index=(uint16_t)(1+XPICK(40)); break;
                    case 5: index=(uint16_t)(0xFFFF-XPICK(6)); break;
                    default: index=(uint16_t)XRAND(); break;
                }
                uint16_t rec=(uint16_t)(6*index+0x5E38);
                {
                    const uint16_t lo=rec,hi=(uint16_t)(rec+0x50);
#define XHIT(a_,b_) (lo<hi?(lo<(b_)&&(a_)<hi):((a_)<hi||lo<(b_)))
                    int bad=XHIT(0x5590,0x5600)||XHIT(0xF300,0xF980);
                    if(dg==0x05FA)bad=bad||XHIT(0x24B0,0x2600)||XHIT(0x9200,0x9700);
                    if(seg[S_SS]==dg)bad=bad||XHIT((uint16_t)(r[R_SP]-0x200),(uint16_t)(r[R_SP]+0x20));
                    if(mseg==dg)bad=bad||XHIT(moff,(uint16_t)(moff+0x3C));
#undef XHIT
                    if(bad){index=(uint16_t)(1+XPICK(40));rec=(uint16_t)(6*index+0x5E38);}
                }
                /* The words above SP: the index, then noise; small[] matches. */
                for(unsigned j=0;j<8;j++) {
                    const uint16_t w=j?(uint16_t)XRAND():index;
                    XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w);
                    if(j<4)small[j]=w;
                }
                /* The records: a run of pass-through events (kind 9, or 6 and 7
                 * when the mode word is 2; the top two bits of the kind byte set
                 * at random), then one that stops the walk: kind 0 (the end), 6 or
                 * 7 under another mode, the neighbours of 9, 6, 7 and 0, and noise.
                 * Sometimes a 6 or 7 sits in the run under another mode, so it
                 * stops the walk early. */
                {
                    const unsigned run=XPICK(4)==0?0:XPICK(5);
                    for(unsigned j=0;j<13;j++) {
                        const uint16_t at=(uint16_t)(rec+6*j);
                        for(unsigned b=0;b<6;b++)XW8(dg,(uint16_t)(at+b),XRAND());
                        uint8_t kind;
                        const uint8_t top=(uint8_t)(XPICK(4)<<6);
                        if(j<run) {
                            if(mode==2&&XPICK(2))kind=(uint8_t)(6+XPICK(2));
                            else kind=9;
                        } else if(j==run) {
                            static const uint8_t ends[]={8,0x0A,5,1,0x3F,0x39,0x10,0x29,0x19,0x2,0x3E};
                            switch(XPICK(10)) {
                                case 0: case 1: case 2: kind=0; break;
                                case 3: case 4: kind=(uint8_t)(6+XPICK(2)); break;
                                case 5: case 6: kind=ends[XPICK(sizeof ends)]; break;
                                default: kind=(uint8_t)XRAND(); break;
                            }
                        } else kind=(uint8_t)XRAND();
                        XW8(dg,(uint16_t)(at+4),(uint8_t)(kind|top));
                    }
                }
                /* The mode block, after the stack and the records. */
                for(unsigned j=0;j<0x40;j+=2)XW16(mseg,(uint16_t)(moff+j),XRAND());
                XW16(mseg,(uint16_t)(moff+0x3A),mode);
                XW16(dg,0x55DE,moff);XW16(dg,0x55E0,mseg);
                /* The previous position. */
                XW16(dg,0x55AA,XRAND());XW16(dg,0x55AC,XRAND());
                /* The stubs' lists: noise (no return value steers the routine). */
                for(unsigned k=0;k<7;k++)
                    for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*k+2*j,XRAND());
                for(unsigned j=0;j<0x40;j+=2)XW16(dg,0xF800+j,0);
                /* Returning probes: fold a number naming the callee and then every
                 * argument, in order, into one shared cell (F800), count the call
                 * (F802 + 2k), keep a list index (F820 + 2k) and return the next
                 * word of the callee's list (F400 + 40h * k). */
#define XSTUB(sg_,off_,k_,id_,nargs_,far_) do { \
    uint8_t cb_[200]; unsigned n_=0; \
    const unsigned cnt_=0xF802+2*(k_),ptr_=0xF820+2*(k_),list_=0xF400+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(id_)&255;cb_[n_++]=(id_)>>8; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
        cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; } \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x83;cb_[n_++]=0xC3;cb_[n_++]=0x02; \
    cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
                {
                    /* k: 0 E82 (none), 1 0DA3 (1), 2 11A2 (1), 3 4206 (6), 4 880 (2),
                     * 5 A98 (2), 6 the driver 05FA:24CC (far, 8). ECD and EE0 (the
                     * map scales) stay original code. */
                    static const struct { uint16_t entry; uint8_t args; } nb_[6]={
                        {0x0E82,0},{0x0DA3,1},{0x11A2,1},{0x4206,6},{0x0880,2},{0x0A98,2}};
                    for(unsigned k=0;k<6;k++) {
                        const uint16_t code=(uint16_t)(0xF200+0x90*k);
                        const uint16_t id=(uint16_t)(0x0A51+0x1111*k);
                        XW8(0,nb_[k].entry,0xE9);XW16(0,nb_[k].entry+1,(uint16_t)(code-nb_[k].entry-3));
                        XSTUB(0,code,k,id,nb_[k].args,0);
                    }
                    XSTUB(0x05FA,0x24CC,6,(uint16_t)(0x0A51+0x1111*6),8,1);
                }
#undef XSTUB
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"END.EXE") && o->seg==0 && o->ip==0x2092) {
                const uint16_t dg=0x5000;
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                seg[S_DS]=dg;seg[S_SS]=XPICK(2)?(uint16_t)0x6000:dg;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                /* The stack pointer: the usual place, a random one, and the
                 * values where the pushes wrap at the segment's ends. Random
                 * ones stay below 7000h, clear of the pointer [7222] and of the
                 * settings record (8000h..D000h when it shares a segment). */
                switch(XPICK(8)) {
                    case 0: case 1: case 2: case 3: r[R_SP]=0xF000; break;
                    case 4: case 5: r[R_SP]=(uint16_t)(0x1000+2*XPICK(0x3000)); break;
                    case 6: r[R_SP]=(uint16_t)(2*XPICK(8)); break;
                    default: r[R_SP]=(uint16_t)(0xFFE0+2*XPICK(0x10)); break;
                }
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The timeout, argument 0: zero, one, two, a short count, FFFFh
                 * (no timeout), FFFEh, or a long count. A long wait is ended by
                 * the callees' lists below (never by the count). */
                uint16_t t;
                switch(XPICK(16)) {
                    case 0: case 1: t=0; break;
                    case 2: t=1; break;
                    case 3: t=2; break;
                    case 4: case 5: case 6: case 7: case 8: t=(uint16_t)(3+XPICK(30)); break;
                    case 9: case 10: case 11: case 12: t=0xFFFF; break;
                    case 13: t=0xFFFE; break;
                    case 14: t=(uint16_t)(0x8000-XPICK(2)); break;
                    default: t=(uint16_t)(0x40+XRAND()%0xFF80u); break;
                }
                const int longwait=(t>=0x40);
                for(unsigned j=0;j<8;j++) {
                    const uint16_t w=j?(uint16_t)XRAND():t;
                    XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w);
                    if(j<4)small[j]=w;
                }
                /* The settings record behind the far pointer [7222]: its byte
                 * 72h holds the joystick (bit 0) and mouse (bit 1) flags beside
                 * random bits; its neighbours are noise. The record sits in a
                 * segment of its own (sometimes at the top, so +72h wraps), in
                 * the data segment or in the stack's. */
                {
                    uint16_t pseg,poff;
                    switch(XPICK(4)) {
                        case 0: pseg=0x7000;poff=(uint16_t)(0xFF80+XPICK(0x80)); break;
                        case 1: pseg=0x7000;poff=(uint16_t)XRAND(); break;
                        case 2: pseg=dg;poff=(uint16_t)(0x8000+XPICK(0x5000)); break;
                        default: pseg=0x6000;poff=(uint16_t)(0x8000+XPICK(0x5000)); break;
                    }
                    XW16(dg,0x7220,XRAND());XW16(dg,0x7222,poff);XW16(dg,0x7224,pseg);XW16(dg,0x7226,XRAND());
                    for(unsigned j=0x6E;j<0x78;j++)XW8(pseg,(uint16_t)(poff+j),XRAND());
                    uint8_t b=(uint8_t)(XRAND()&0xFC);
                    if(XPICK(2))b|=1;
                    if(XPICK(2))b|=2;
                    XW8(pseg,(uint16_t)(poff+0x72),b);
                }
                /* The callees' lists (F400 + 40h * k): noise for the frame wait
                 * (k=0), the mouse read (1), the quit confirmation (3) and the
                 * sound (7). The button answers (2: mouse 44A4, 4: joystick
                 * 25CB) are zero half the time, else an edge value or anything.
                 * The key-ready answer (5) is zero (a key) half the time. The
                 * keys (6): 1000h exactly, its neighbours, Enter, Esc and Space
                 * with any high byte, their neighbours, or anything. For a long
                 * wait the button lists end in zeros (entries 16 on), the
                 * key-ready list too, and the keys from entry 8 on are Enter, Esc
                 * or Space: both loops then end within some 30 passes. */
                for(unsigned k=0;k<8;k++)
                    for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*k+2*j,XRAND());
                for(unsigned j=0;j<32;j++) {
                    static const uint16_t edge[]={0x0001,0x0100,0x8000,0xFFFF,0x0080,0x00FF,0xFF00,0x0002};
                    for(unsigned k=2;k<=4;k+=2) {
                        uint16_t v;
                        if(XPICK(2)==0 || (longwait && j>=16))v=0;
                        else if(XPICK(2))v=edge[XPICK(sizeof edge/sizeof edge[0])];
                        else v=(uint16_t)(1+XPICK(0xFFFF));
                        XW16(dg,0xF400+0x40*k+2*j,v);
                    }
                    {
                        uint16_t v;
                        if(XPICK(2)==0 || (longwait && j>=16))v=0;
                        else if(XPICK(2))v=edge[XPICK(sizeof edge/sizeof edge[0])];
                        else v=(uint16_t)(1+XPICK(0xFFFF));
                        XW16(dg,0xF400+0x40*5+2*j,v);
                    }
                    {
                        static const uint8_t term[3]={0x0D,0x1B,0x20};
                        static const uint8_t near_[8]={0x0C,0x0E,0x1A,0x1C,0x1F,0x21,0x8D,0x00};
                        static const uint16_t big[6]={0x1000,0x1000,0x0FFF,0x1001,0x1100,0x0010};
                        const uint16_t hi=(uint16_t)(XPICK(3)==0?0:(XRAND()&0xFF00));
                        uint16_t v;
                        unsigned pick=XPICK(8);
                        if(longwait && j>=8)pick=0;
                        switch(pick) {
                            case 0: case 1: case 2: v=(uint16_t)(hi|term[XPICK(3)]); break;
                            case 3: case 4: v=big[XPICK(6)]; break;
                            case 5: case 6: v=(uint16_t)(hi|near_[XPICK(8)]); break;
                            default: v=(uint16_t)XRAND(); break;
                        }
                        XW16(dg,0xF400+0x40*6+2*j,v);
                    }
                }
                for(unsigned j=0;j<0x40;j+=2)XW16(dg,0xF800+j,0);
                /* Returning probes: fold a number naming the callee and then every
                 * argument, in order, into one shared cell (F800), count the call
                 * (F802 + 2k), keep a list index (F820 + 2k) and return the next
                 * word of the callee's list (F400 + 40h * k). */
#define XSTUB(sg_,off_,k_,id_,nargs_,far_) do { \
    uint8_t cb_[200]; unsigned n_=0; \
    const unsigned cnt_=0xF802+2*(k_),ptr_=0xF820+2*(k_),list_=0xF400+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(id_)&255;cb_[n_++]=(id_)>>8; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
        cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; } \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x83;cb_[n_++]=0xC3;cb_[n_++]=0x02; \
    cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
                {
                    /* k: 0 4393 (near, none), 1 44B9 (near, none), 2 44A4 (near,
                     * 1), 3 3E61 (near, 1), 4 05FA:25CB (far, 1), 5 05FA:25BC
                     * (far, none), 6 05FA:25C1 (far, none), 7 05FA:25F3 (far, 1).
                     * The driver entries are five-byte far jumps in the original,
                     * so each gets a far jump to its stub in segment 0. */
                    static const struct { uint16_t entry; uint8_t args; uint8_t far_; } nb_[8]={
                        {0x4393,0,0},{0x44B9,0,0},{0x44A4,1,0},{0x3E61,1,0},
                        {0x25CB,1,1},{0x25BC,0,1},{0x25C1,0,1},{0x25F3,1,1}};
                    for(unsigned k=0;k<8;k++) {
                        const uint16_t code=(uint16_t)(0xF200+0x60*k);
                        const uint16_t id=(uint16_t)(0x0A51+0x1111*k);
                        if(nb_[k].far_) {
                            XW8(0x05FA,nb_[k].entry,0xEA);XW16(0x05FA,nb_[k].entry+1,code);XW16(0x05FA,nb_[k].entry+3,0);
                        } else {
                            XW8(0,nb_[k].entry,0xE9);XW16(0,nb_[k].entry+1,(uint16_t)(code-nb_[k].entry-3));
                        }
                        XSTUB(0,code,k,id,nb_[k].args,nb_[k].far_);
                    }
                }
#undef XSTUB
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"START.EXE") && o->seg==0 && o->ip==0x40F0) {
                const uint16_t dg=0x5000;
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
#define XR8(s_,o_) (g_mem[0][phys((s_),(uint16_t)(o_))])
#define XR16(s_,o_) ((uint16_t)(XR8((s_),(o_))|(XR8((s_),(uint16_t)((o_)+1))<<8)))
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                seg[S_DS]=dg;seg[S_SS]=XPICK(2)?0x6000:dg;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                /* The stack: the usual place, random ones, and the low values where
                 * the pushes wrap below zero. With the stack in the data segment it
                 * stays clear of the cells the routine writes (B38Ch..D5E0h) and of
                 * the stubs' lists and cells (F400h..F840h). */
                switch(XPICK(5)) {
                    case 0: r[R_SP]=0xF000; break;
                    case 1: r[R_SP]=(uint16_t)(0x2800+2*XPICK(0x3000)); break;
                    case 2: r[R_SP]=(uint16_t)(2*XPICK(0x10)); break;
                    case 3: r[R_SP]=(uint16_t)(0xFFE0+2*XPICK(0x10)); break;
                    default: r[R_SP]=(uint16_t)(0x0100+2*XPICK(0x6000)); break;
                }
                if(seg[S_SS]==dg && r[R_SP]>0xB000 && r[R_SP]<0xD800) r[R_SP]=0xF000;
                if(seg[S_SS]==dg && r[R_SP]>0xF300 && r[R_SP]<0xFA00) r[R_SP]=0xF000;
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The routine takes no arguments; the words above SP are noise,
                 * and the harness's small[] copies of them match. */
                for(unsigned j=0;j<8;j++) {
                    const uint16_t w=(uint16_t)XRAND();
                    XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w);
                    if(j<4)small[j]=w;
                }
                /* The pilot record behind [CACA] and the mission record behind
                 * [E096]: far pointers into a segment of their own or into the
                 * data segment, sometimes near the top so the field sums wrap,
                 * sometimes the same pointer. */
                uint16_t pseg[2],poff[2];
                for(unsigned t=0;t<2;t++) {
                    static const uint16_t sg[4]={0x7000,0x7200,0x5000,0x6800};
                    pseg[t]=sg[XPICK(4)];
                    if(pseg[t]!=dg) {
                        switch(XPICK(4)) {
                            case 0: poff[t]=(uint16_t)(0xFF80+XPICK(0x80)); break;
                            case 1: poff[t]=(uint16_t)XRAND(); break;
                            default: poff[t]=(uint16_t)XPICK(0x400); break;
                        }
                    } else poff[t]=(uint16_t)(0x0400+XPICK(0x2000));
                }
                if(XPICK(5)==0){pseg[1]=pseg[0];poff[1]=poff[0];}
                XW16(dg,0xCACA,poff[0]);XW16(dg,0xCACC,pseg[0]);
                XW16(dg,0xE096,poff[1]);XW16(dg,0xE098,pseg[1]);
                for(unsigned t=0;t<2;t++)
                    for(unsigned j=0;j<0x48;j++)XW8(pseg[t],(uint16_t)(poff[t]+j),XRAND());
                /* The theatre (+38h of the pilot record) is small most of the time,
                 * so its word comes from the table at 0D6Ah; the table is noise. */
                for(unsigned j=0;j<0x40;j++)XW8(dg,0x0D6A+j,XRAND());
                if(XPICK(4))XW16(pseg[0],(uint16_t)(poff[0]+0x38),(uint16_t)(XPICK(4)?XPICK(8):0x8000+XPICK(8)));
                /* The cells the routine overwrites start as noise. */
                XW16(dg,0xCABE,XRAND());XW16(dg,0xB38E,XRAND());XW16(dg,0xB38C,XRAND());
                XW16(dg,0xCAD2,XRAND());XW16(dg,0xCAD4,XRAND());XW8(dg,0xD33A,XRAND());
                /* The site records (16 bytes from CBDEh, and the ones below it that a
                 * drawn number of FFFFh or F000h + n wraps to): noise, then a zero
                 * word at +6 a quarter of the time and the byte at +9 with its bits
                 * 0 and 2 in every combination. */
                for(unsigned a=0xBBDE;a<0xD5E0;a+=2)XW16(dg,a,XRAND());
                for(unsigned k=0;k<0xA0;k++) {
                    const uint16_t at=(uint16_t)(0xCBDE+16*k);
                    if(XPICK(4)==0)XW16(dg,at+6,0);
                    else if(XPICK(4)==0)XW16(dg,at+6,(uint16_t)(1u<<XPICK(16)));
                    static const uint8_t b9[8]={0x00,0x00,0x00,0x01,0x04,0x05,0xFA,0x00};
                    uint8_t v=b9[XPICK(8)];
                    if(XPICK(4)==0)v=(uint8_t)XRAND();
                    XW8(dg,at+9,v);
                }
                /* The 16 x 16 map at CAD8h: no byte with bit 10h, a single one
                 * (often in a corner), noise, or rare ones; the bytes just after it
                 * have the bit when the map has none. */
                {
                    const unsigned mode=XPICK(4);
                    for(unsigned j=0;j<0x100;j++) {
                        uint8_t v=(uint8_t)XRAND();
                        if(mode<2||(mode==3&&XPICK(64)))v=(uint8_t)(v&~0x10u);
                        XW8(dg,0xCAD8+j,v);
                    }
                    if(mode==1) {
                        static const uint8_t corner[4]={0x00,0x0F,0xF0,0xFF};
                        const unsigned at=XPICK(2)?corner[XPICK(4)]:XPICK(0x100);
                        XW8(dg,0xCAD8+at,(uint8_t)(XRAND()|0x10));
                    }
                    for(unsigned j=0x100;j<0x106;j++)XW8(dg,0xCAD8+j,(mode<2)?(uint8_t)(XRAND()|0x10):(uint8_t)XRAND());
                    XW8(dg,0xCAD7,(uint8_t)(XRAND()|0x10));
                }
                /* The last site's number, signed: none (negative, the most negative
                 * and minus one), only site 0, a few, up to 9Fh. */
                uint16_t last;
                switch(XPICK(8)) {
                    case 0: { static const uint16_t neg[4]={0xFFFF,0x8000,0xFFF0,0xFFFE}; last=neg[XPICK(4)]; } break;
                    case 1: last=0; break;
                    case 2: last=(uint16_t)(0x80+XPICK(0x20)); break;
                    default: last=(uint16_t)(1+XPICK(0x28)); break;
                }
                XW16(dg,0xD33C,last);
                const unsigned nsite=((int16_t)last<0)?0u:(unsigned)last+1u;
                /* The two targets: a site in range most of the time (sometimes the
                 * same one), or the number with a high bit that aliases a site. */
                uint16_t tgt[2];
                for(unsigned t=0;t<2;t++) {
                    switch(XPICK(5)) {
                        case 0: tgt[t]=(uint16_t)XRAND(); break;
                        case 1: tgt[t]=(uint16_t)(0x1000+XPICK(nsite?nsite:1)); break;
                        default: tgt[t]=(uint16_t)XPICK(nsite?nsite:4); break;
                    }
                }
                if(XPICK(6)==0)tgt[1]=tgt[0];
                XW16(dg,0xE060,tgt[0]);XW16(dg,0xE072,tgt[1]);
                /* The callees' lists: noise, but 76C0's (k=2) is the draw. Its first
                 * word is the number of sites to take out, the rest are the drawn
                 * sites and then the third pass's answers (zero marks a site). */
                for(unsigned k=0;k<3;k++)
                    for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*k+2*j,XRAND());
                {
                    uint16_t lst[32];
                    static const uint16_t cnt[8]={0,1,2,3,1,2,3,5};
                    lst[0]=cnt[XPICK(8)];
                    if(XPICK(16)==0)lst[0]=4;
                    for(unsigned j=1;j<32;j++) {
                        switch(XPICK(10)) {
                            case 0: lst[j]=tgt[0]; break;
                            case 1: lst[j]=tgt[1]; break;
                            case 2: case 3: lst[j]=0; break;
                            case 4: lst[j]=(uint16_t)(1+XPICK(2)); break;
                            case 5: lst[j]=XPICK(2)?0xFFFF:(uint16_t)(0xF000+XPICK(nsite?nsite:4)); break;
                            case 6: lst[j]=(uint16_t)(0x1000*(1+XPICK(15))+XPICK(nsite?nsite:4)); break;
                            default: lst[j]=(uint16_t)XPICK(nsite?nsite:0xA0); break;
                        }
                    }
                    /* The draw must end: one site that can be taken somewhere in the
                     * list when any is asked for (the list repeats). */
                    if(lst[0]) {
                        unsigned ok=0;
                        for(unsigned j=0;j<32&&!ok;j++) {
                            const uint16_t v=lst[j],at=(uint16_t)((v<<4)+0xCBDE);
                            if(v!=tgt[0]&&v!=tgt[1]&&XR16(dg,at+6)!=0&&!(XR8(dg,at+9)&5))ok=1;
                        }
                        if(!ok) {
                            const unsigned j=1+XPICK(31);
                            uint16_t v=(uint16_t)XPICK(nsite?nsite:0xA0);
                            while(v==tgt[0]||v==tgt[1])v=(uint16_t)((v+1)%0xA0);
                            const uint16_t at=(uint16_t)((v<<4)+0xCBDE);
                            XW16(dg,at+6,(uint16_t)(1+XPICK(0xFFFF)));
                            XW8(dg,at+9,(uint8_t)(XRAND()&~5u));
                            lst[j]=v;
                        }
                    }
                    for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*2+2*j,lst[j]);
                }
                for(unsigned j=0;j<0x40;j+=2)XW16(dg,0xF800+j,0);
                /* Returning probes: fold a number naming the callee and then every
                 * argument, in order, into one shared cell (F800), count the call
                 * (F802 + 2k), keep a list index (F820 + 2k) and return the next
                 * word of the callee's list (F400 + 40h * k). */
#define XSTUB(sg_,off_,k_,id_,nargs_,far_) do { \
    uint8_t cb_[200]; unsigned n_=0; \
    const unsigned cnt_=0xF802+2*(k_),ptr_=0xF820+2*(k_),list_=0xF400+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(id_)&255;cb_[n_++]=(id_)>>8; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
        cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; } \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x83;cb_[n_++]=0xC3;cb_[n_++]=0x02; \
    cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
                {
                    /* k: 0 5466 (the theatre's files, 1 word), 1 4239 (the mission
                     * generator, none), 2 76C0 (rand, 1 word). */
                    static const struct { uint16_t entry; uint8_t args; } nb_[3]={
                        {0x5466,1},{0x4239,0},{0x76C0,1}};
                    for(unsigned k=0;k<3;k++) {
                        const uint16_t code=(uint16_t)(0xF200+0x90*k);
                        const uint16_t id=(uint16_t)(0x0A51+0x1111*k);
                        XW8(0,nb_[k].entry,0xE9);XW16(0,nb_[k].entry+1,(uint16_t)(code-nb_[k].entry-3));
                        XSTUB(0,code,k,id,nb_[k].args,0);
                    }
                }
#undef XSTUB
#undef XPICK
#undef XRAND
#undef XR16
#undef XR8
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0xF458) {
                const uint16_t dg=0x5000;
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define RND(n_) ((unsigned)(XRAND()%(unsigned)(n_)))
#define PCT(p_) (RND(100)<(unsigned)(p_))
#define PICK(a_) ((a_)[RND(sizeof(a_)/sizeof((a_)[0]))])
                seg[S_DS]=dg;seg[S_SS]=PCT(50)?dg:0x6000;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                /* The stack below SP (return address, saved BP, the local word whose
                 * high byte is [bp-1]) starts random in both segments. */
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                for(unsigned j=0;j<0x200;j++)XW8(dg,(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The C runtime's cells: the handle table size, the DOS version (the
                 * error mapper reads it), the error cells and the error table. */
                uint16_t nfile_;
                { static const uint16_t nf_[]={20,20,20,0x10,5,1,0,0xFFFF,0x8000,0x7FFF};
                  nfile_=PCT(85)?PICK(nf_):(uint16_t)XRAND();
                  XW16(dg,0x926F,nfile_); }
                { static const uint8_t dosv_[]={2,3,3,4,5,6,0,0xFF};
                  XW8(dg,0x926A,PCT(85)?PICK(dosv_):(uint8_t)XRAND()); }
                XW16(dg,0x9262,XRAND());XW16(dg,0x926D,XRAND());
                for(unsigned j=0;j<0x40;j++)XW8(dg,0x92A4+j,XRAND());
                for(unsigned j=0;j<0x20;j++)XW8(dg,0x9271+j,XRAND());
                /* The handle: mostly below the table size (its boundary both ways). */
                uint16_t fd_;
                { const unsigned q_=RND(100);
                  if(q_<70)fd_=(uint16_t)(nfile_ && nfile_<0x20 ? RND(nfile_) : RND(0x20));
                  else if(q_<78)fd_=(uint16_t)(nfile_-1);
                  else if(q_<86)fd_=nfile_;
                  else if(q_<93)fd_=(uint16_t)(nfile_+1);
                  else fd_=(uint16_t)XRAND(); }
                /* Its flag byte: 02h end of file, 04h, 40h device, 80h text mode. */
                { uint8_t fl_=(uint8_t)(XRAND()&0x3D);
                  if(PCT(12))fl_|=0x02;
                  if(PCT(40))fl_|=0x04;
                  if(PCT(35))fl_|=0x40;
                  if(PCT(75))fl_|=0x80;
                  XW8(dg,(uint16_t)(fd_+0x9271),fl_); }
                /* The count asked and the count DOS answers (mostly not above it). */
                uint16_t n_,got_;
                { const unsigned q_=RND(100);
                  if(q_<7)n_=0;
                  else if(q_<25)n_=1;
                  else if(q_<55)n_=(uint16_t)(2+RND(7));
                  else if(q_<88)n_=(uint16_t)(9+RND(56));
                  else n_=(uint16_t)XRAND(); }
                { const unsigned top_=n_<64?n_:64;
                  const unsigned q_=RND(100);
                  if(q_<45)got_=(uint16_t)top_;
                  else if(q_<85)got_=(uint16_t)RND(top_+1);
                  else if(q_<92)got_=0;
                  else got_=(uint16_t)RND(70); }
                /* The buffer: in the middle of the segment, or at its end (DI wraps
                 * past FFFFh, and the count's subtraction borrows). */
                uint16_t buf_;
                { const unsigned q_=RND(100);
                  if(q_<75)buf_=(uint16_t)(0x1000+RND(0x6000));
                  else if(q_<90)buf_=(uint16_t)(0xFFC0+RND(0x40));
                  else buf_=(uint16_t)(0xA000+RND(0x4000)); }
                /* What DOS "read": CR, LF, Ctrl-Z and other bytes; a CR as the last
                 * byte half of the time (no Ctrl-Z before it then), an LF first now
                 * and then. Two bytes more after the count. */
                { uint8_t b_[72];
                  for(unsigned j=0;j<72;j++) {
                      const unsigned q_=RND(100);
                      b_[j]=q_<22?0x0D:q_<44?0x0A:q_<48?0x1A:q_<52?0x0C:q_<56?0x0B:(uint8_t)XRAND();
                  }
                  if(PCT(30))b_[0]=0x0A;
                  if(got_>0&&got_<=70&&PCT(50)) {
                      for(unsigned j=0;j+1<got_;j++) if(b_[j]==0x1A) b_[j]=0x1B;
                      b_[got_-1]=0x0D;
                  }
                  for(unsigned j=0;j<72;j++)XW8(dg,(uint16_t)(buf_+j),b_[j]); }
                const uint16_t arg_[4]={fd_,buf_,n_,(uint16_t)XRAND()};
                for(unsigned j=0;j<4;j++){XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),arg_[j]);small[j]=arg_[j];}
                /* The console hook: its signature (or a near miss) and a returning probe
                 * at 0:F200 that folds BX (the handle) and AX into a cell (+2), counts its
                 * calls (+0) and answers AX from a list (the index at +4). */
                { static const uint16_t sig_[]={0xD6D7,0xD7D6,0x56D6,0xD6D6^0x8000,0};
                  XW16(dg,0x9406,PCT(45)?0xD6D6:PCT(70)?PICK(sig_):(uint16_t)XRAND()); }
                XW16(dg,0x9408,0xF200);
                for(unsigned j=0;j<3;j++)XW16(dg,0xF800+2*j,XRAND());
                for(unsigned j=0;j<32;j++)XW16(dg,0xF840+2*j,XRAND());
                { static const uint8_t stub_[]={
                      0x55,0x8B,0xEC,0x53,                         /* push bp; mov bp,sp; push bx */
                      0xFF,0x06,0x00,0xF8,                         /* inc word [F800] */
                      0x8B,0x1E,0x02,0xF8,0xD1,0xC3,0x33,0x5E,0xFE,/* mov bx,[F802]; rol bx,1; xor bx,[bp-2] (BX in) */
                      0xD1,0xC3,0x33,0xD8,                         /* rol bx,1; xor bx,ax */
                      0x89,0x1E,0x02,0xF8,                         /* mov [F802],bx */
                      0x8B,0x1E,0x04,0xF8,0x8B,0x87,0x40,0xF8,     /* mov bx,[F804]; mov ax,[bx+F840] */
                      0x83,0xC3,0x02,0x83,0xE3,0x3E,0x89,0x1E,0x04,0xF8, /* the index on */
                      0x5B,0x5D,0xC3 };                            /* pop bx; pop bp; ret */
                  for(unsigned j=0;j<sizeof stub_;j++)XW8(0,0xF200+j,stub_[j]); }
                /* DOS (g_dos) with its first three answers fixed: the read of n bytes
                 * (the count, an error now and then); then the one-byte read of a file
                 * or the device information; then the device's one-byte read or the
                 * seek back. A read stores its byte (put) at DS:DX: with SS = DS that
                 * is the local [bp-1], LF about half of the time. */
                g_dos=1;g_dos_script_n=3;
                { static const uint16_t err_[]={5,6,0x0D,0x1F,0x21,0x22,0x30,0x0105};
                  const int cf_=PCT(12);
                  g_dos_script[0].ax=cf_?(PCT(75)?PICK(err_):(uint16_t)XRAND()):got_;
                  g_dos_script[0].cf=(uint8_t)cf_;g_dos_script[0].put=0; }
                for(unsigned k=1;k<3;k++) {
                    static const uint16_t one_[]={0,0,1,1,1,1,1,2,0xFFFF,0x8000};
                    g_dos_script[k].ax=PCT(85)?PICK(one_):(uint16_t)XRAND();
                    g_dos_script[k].cf=(uint8_t)PCT(18);
                    { const unsigned q_=RND(100);
                      g_dos_script[k].put=(uint16_t)(q_<55?0x10A:q_<65?0x10D:q_<75?0x10B:q_<90?(0x100|(XRAND()&0xFF)):0); }
                }
#undef PICK
#undef PCT
#undef RND
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"PLAYER.EXE") && o->seg==0 && o->ip==0x1602) {
                const uint16_t dg=0x5000, psp=0x4000, env=0x3000;
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
                seg[S_DS]=dg;seg[S_SS]=(XRAND()%4==0)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                for(unsigned j=0;j<0x800;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x800+j),XRAND());
                /* The data words: the program prefix segment (in both data and stack segments, as the
                 * routine reads it through SS later), stale name pointer and return slot. */
                XW16(dg,0x1A60,psp);XW16(seg[S_SS],0x1A60,psp);
                XW16(dg,0x1A83,0x0010);XW16(dg,0x1A85,0x2000);
                XW16(seg[S_SS],0x1A83,0x0010);XW16(seg[S_SS],0x1A85,0x2000);
                {const unsigned sl_=XRAND()%10;for(unsigned j=0;j<sl_;j++)XW8(0x2000,0x10+j,1+XRAND()%255);XW8(0x2000,0x10+sl_,0);}
                XW16(dg,0x1ABC,(uint16_t)XRAND());XW16(seg[S_SS],0x1ABC,back);
                /* The environment block: strings, an empty one, a count word, the program name. Now and
                 * then the block has no zero for 8000h bytes (the REP SCASB runs out of count). */
                {
                    const unsigned mode_=XRAND()%24;
                    unsigned at_=0;
                    if(mode_==0) {
                        for(unsigned j=0;j<0x8010;j++)XW8(env,(uint16_t)j,1+XRAND()%255);
                        for(unsigned j=0x8010;j<0x8030;j++)XW8(env,(uint16_t)j,(XRAND()%3)?0:(uint8_t)(1+XRAND()%255));
                    } else {
                        const unsigned n_=(mode_<4)?0:XRAND()%5;
                        for(unsigned k=0;k<n_;k++) {
                            const unsigned len_=1+XRAND()%12;
                            for(unsigned j=0;j<len_;j++)XW8(env,at_++,1+XRAND()%255);
                            XW8(env,at_++,0);
                        }
                        XW8(env,at_++,0);                       /* the empty string ends the block */
                        if(n_==0&&(XRAND()&1))XW8(env,at_++,0);  /* an empty environment is two zeros */
                        XW16(env,at_,XRAND()%3);at_+=2;           /* the word count */
                        const unsigned nl_=(XRAND()%8==0)?0:(XRAND()%6==0)?(1+XRAND()%80):(1+XRAND()%12);
                        for(unsigned j=0;j<nl_;j++)XW8(env,at_++,1+XRAND()%255);
                        for(unsigned j=0;j<0x20;j++)XW8(env,at_+j,0);
                    }
                    XW16(psp,0x2C,env);
                }
                /* The command tail at prefix:81h: blanks, tabs, quotes, backslash runs, letters; ended by a
                 * carriage return, a zero, or (rarely) left unterminated within the 7Fh bytes. */
                {
                    unsigned at_=0;
                    const unsigned want_=(XRAND()%5==0)?0:(XRAND()%6==0)?(40+XRAND()%80):(1+XRAND()%30);
                    const unsigned lead_=(XRAND()%3==0)?XRAND()%4:0;
                    for(unsigned j=0;j<lead_&&at_<0x7E;j++)XW8(psp,0x81+at_++,(XRAND()&1)?0x20:9);
                    while(at_<want_&&at_<0x7E) {
                        const unsigned t_=XRAND()%24;
                        if(t_<7)      XW8(psp,0x81+at_++,"abcxyz-"[XRAND()%7]);
                        else if(t_<11)XW8(psp,0x81+at_++,0x20);
                        else if(t_<12)XW8(psp,0x81+at_++,9);
                        else if(t_<16)XW8(psp,0x81+at_++,0x22);
                        else if(t_<22) {
                            const unsigned run_=1+XRAND()%6;
                            for(unsigned j=0;j<run_&&at_<0x7E;j++)XW8(psp,0x81+at_++,0x5C);
                            if(XRAND()%2&&at_<0x7E)XW8(psp,0x81+at_++,0x22);
                        }
                        else if(t_<23)XW8(psp,0x81+at_++,(XRAND()%3)?'q':(XRAND()%2)?0x0D:0);
                        else          XW8(psp,0x81+at_++,0x5C);
                    }
                    const unsigned endk_=XRAND()%10;
                    if(at_<0x7E) {
                        XW8(psp,0x81+at_,(endk_<7)?0x0D:(endk_<9)?0:'z');
                        XW8(psp,0x82+at_,0x0D);
                    }
                    XW8(psp,0x80,(uint8_t)at_);
                }
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0x130D && o->ip==0x033F) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                for(unsigned j=0;j<4;j++)small[j]=(uint16_t)XRAND();
                for(unsigned j=0;j<4;j++)XW16(seg[S_SS],(uint16_t)(r[R_SP]+4+2*j),small[j]);
                /* The scratch record, the overflow note and the window: random first. */
                for(unsigned j=0;j<0x80;j++)XW8(dg,0x8590+j,XRAND());
                /* Window: xmin [85FA], ymin [85FC], xmax [85FE], ymax [8600]. */
                int32_t lo_[2],hi_[2];
                const unsigned cls=XRAND()%12;     /* 0-5 narrow, 6-7 wide, 8-9 wide inside the window as unsigned words, 10-11 wide with a stray word */
                const unsigned wt=(cls==8||cls==9)?6u:XRAND()%8;
                for(unsigned a=0;a<2;a++) {
                    int32_t lo,hi;
                    if(wt<3) { lo=(int32_t)(XRAND()%0x40); hi=lo+0x20+(int32_t)(XRAND()%0x100); }
                    else if(wt<5) { lo=(int32_t)(int16_t)XRAND(); hi=(int32_t)(int16_t)XRAND(); if(lo>hi){int32_t t_=lo;lo=hi;hi=t_;} }
                    else if(wt==5) { lo=-0x7F00+(int32_t)(XRAND()%0x300); hi=0x7C00+(int32_t)(XRAND()%0x300); }
                    else if(wt==6) { lo=0x7000+(int32_t)(XRAND()%0x1000); hi=0xE000+(int32_t)(XRAND()%0x1FF0); }   /* unsigned-high window: the wide path can be inside */
                    else { lo=(int32_t)(XRAND()%8); hi=lo+(int32_t)(XRAND()%4); }                                  /* tiny window */
                    lo_[a]=lo;hi_[a]=hi;
                }
                XW16(dg,0x85FA,lo_[0]);XW16(dg,0x85FC,lo_[1]);XW16(dg,0x85FE,hi_[0]);XW16(dg,0x8600,hi_[1]);
                /* The record at SI (and its twin at SI-10h): two ends, each x then y as
                 * (low, high) words. Each coordinate lands inside, on an edge, just outside or far outside. */
                const uint16_t rec=(uint16_t)(0x2100+2*(XRAND()%0x300));
                r[R_SI]=rec;
                for(unsigned j=0;j<0x30;j++)XW8(dg,(uint16_t)(rec-0x20+j),XRAND());
                uint16_t lowv[4];                  /* x1 y1 x2 y2 */
                for(unsigned k=0;k<4;k++) {
                    const unsigned a=k&1;
                    const int32_t lo=lo_[a],hi=hi_[a];
                    const unsigned t=XRAND()%12;
                    int32_t v;
                    switch(t) {
                    case 0: v=lo-1-(int32_t)(XRAND()%3); break;
                    case 1: v=hi+1+(int32_t)(XRAND()%3); break;
                    case 2: v=(XRAND()&1)?lo:hi; break;
                    case 6: v=lo-(int32_t)(XRAND()%0x300); break;
                    case 7: v=hi+(int32_t)(XRAND()%0x300); break;
                    case 8: v=lo-0x2000+(int32_t)(XRAND()%0x4000); break;
                    case 9: v=(XRAND()&1)?-0x7FF0+(int32_t)(XRAND()%0x20):0x7FF0-(int32_t)(XRAND()%0x20); break;
                    case 10: v=(int32_t)(int16_t)XRAND(); break;
                    default: v=(hi>lo)?lo+(int32_t)(XRAND()%(uint32_t)(hi-lo+1)):lo; break;
                    }
                    lowv[k]=(uint16_t)v;
                }
                if(cls==8||cls==9) {       /* hi words 0 and the window unsigned-high: never every end inside (that path ends in a near RET the lockstep cannot follow) */
                    int in_=1;
                    for(unsigned k=0;k<4;k++){const unsigned a=k&1;if(lowv[k]<lo_[a]||lowv[k]>hi_[a])in_=0;}
                    if(in_)lowv[XRAND()%4]=(uint16_t)(hi_[XRAND()&1]+1+XRAND()%3);
                }
                static const unsigned slot_[4]={0,4,8,0xC};     /* x1 y1 x2 y2 */
                for(unsigned k=0;k<4;k++) {
                    uint16_t highw=(lowv[k]&0x8000)?0xFFFF:0;
                    if(cls==8||cls==9) highw=0;
                    else if(cls>=6) {
                        const unsigned h=XRAND()%12;
                        if(h==0)highw=1;else if(h==1)highw=0xFFFF;else if(h==2)highw=0xFFFE;else if(h==3)highw=0;
                        else if(h==4)highw=(uint16_t)XRAND();
                    }
                    XW16(dg,rec+slot_[k],lowv[k]);
                    XW16(dg,rec+slot_[k]+2,highw);
                }
                /* One wide state in four takes a random word anywhere in the record. */
                if(cls>=10) XW16(dg,rec+2*(XRAND()%8),(uint16_t)XRAND());
                /* The wide path ends in a near RET (and the lockstep cannot follow the original there)
                 * when both ends are inside the window as 32-bit values: keep one end out. */
#define XR16(o_) ((uint16_t)(g_mem[0][phys(dg,(uint16_t)(o_))]|(g_mem[0][phys(dg,(uint16_t)((o_)+1))]<<8)))
                {
                    int wide_=0,inside_=1;
                    for(unsigned k=0;k<4;k++) {
                        const uint16_t lw=XR16(rec+slot_[k]),hw=XR16(rec+slot_[k]+2);
                        if(hw!=((lw&0x8000)?0xFFFFu:0u))wide_=1;
                        const uint16_t wmin=XR16(0x85FA + 2*(k&1)),wmax=XR16(0x85FE + 2*(k&1));
                        if(hw!=0||lw<wmin||lw>wmax)inside_=0;
                    }
                    if(wide_&&inside_)XW16(dg,rec+0xA,1);
                }
#undef XR16
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"SETUP.EXE") && o->seg==0 && o->ip==0x19B6) {
                const uint16_t dg=0x5000, psp=0x4000, env=0x3000;
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
                seg[S_DS]=dg;seg[S_SS]=(XRAND()%4==0)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                for(unsigned j=0;j<0x800;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x800+j),XRAND());
                /* The data words: the program prefix segment (in both data and stack segments, as the
                 * routine reads it through SS later), stale name pointer and return slot. */
                XW16(dg,0x0E84,psp);XW16(seg[S_SS],0x0E84,psp);
                XW16(dg,0x0EA7,0x0010);XW16(dg,0x0EA9,0x2000);
                XW16(seg[S_SS],0x0EA7,0x0010);XW16(seg[S_SS],0x0EA9,0x2000);
                {const unsigned sl_=XRAND()%10;for(unsigned j=0;j<sl_;j++)XW8(0x2000,0x10+j,1+XRAND()%255);XW8(0x2000,0x10+sl_,0);}
                XW16(dg,0x0EBA,(uint16_t)XRAND());XW16(seg[S_SS],0x0EBA,back);
                /* The environment block: strings, an empty one, a count word, the program name. Now and
                 * then the block has no zero for 8000h bytes (the REP SCASB runs out of count). */
                {
                    const unsigned mode_=XRAND()%24;
                    unsigned at_=0;
                    if(mode_==0) {
                        for(unsigned j=0;j<0x8010;j++)XW8(env,(uint16_t)j,1+XRAND()%255);
                        for(unsigned j=0x8010;j<0x8030;j++)XW8(env,(uint16_t)j,(XRAND()%3)?0:(uint8_t)(1+XRAND()%255));
                    } else {
                        const unsigned n_=(mode_<4)?0:XRAND()%5;
                        for(unsigned k=0;k<n_;k++) {
                            const unsigned len_=1+XRAND()%12;
                            for(unsigned j=0;j<len_;j++)XW8(env,at_++,1+XRAND()%255);
                            XW8(env,at_++,0);
                        }
                        XW8(env,at_++,0);                       /* the empty string ends the block */
                        if(n_==0&&(XRAND()&1))XW8(env,at_++,0);  /* an empty environment is two zeros */
                        XW16(env,at_,XRAND()%3);at_+=2;           /* the word count */
                        const unsigned nl_=(XRAND()%8==0)?0:(XRAND()%6==0)?(1+XRAND()%80):(1+XRAND()%12);
                        for(unsigned j=0;j<nl_;j++)XW8(env,at_++,1+XRAND()%255);
                        for(unsigned j=0;j<0x20;j++)XW8(env,at_+j,0);
                    }
                    XW16(psp,0x2C,env);
                }
                /* The command tail at prefix:81h: blanks, tabs, quotes, backslash runs, letters; ended by a
                 * carriage return, a zero, or (rarely) left unterminated within the 7Fh bytes. */
                {
                    unsigned at_=0;
                    const unsigned want_=(XRAND()%5==0)?0:(XRAND()%6==0)?(40+XRAND()%80):(1+XRAND()%30);
                    const unsigned lead_=(XRAND()%3==0)?XRAND()%4:0;
                    for(unsigned j=0;j<lead_&&at_<0x7E;j++)XW8(psp,0x81+at_++,(XRAND()&1)?0x20:9);
                    while(at_<want_&&at_<0x7E) {
                        const unsigned t_=XRAND()%24;
                        if(t_<7)      XW8(psp,0x81+at_++,"abcxyz-"[XRAND()%7]);
                        else if(t_<11)XW8(psp,0x81+at_++,0x20);
                        else if(t_<12)XW8(psp,0x81+at_++,9);
                        else if(t_<16)XW8(psp,0x81+at_++,0x22);
                        else if(t_<22) {
                            const unsigned run_=1+XRAND()%6;
                            for(unsigned j=0;j<run_&&at_<0x7E;j++)XW8(psp,0x81+at_++,0x5C);
                            if(XRAND()%2&&at_<0x7E)XW8(psp,0x81+at_++,0x22);
                        }
                        else if(t_<23)XW8(psp,0x81+at_++,(XRAND()%3)?'q':(XRAND()%2)?0x0D:0);
                        else          XW8(psp,0x81+at_++,0x5C);
                    }
                    const unsigned endk_=XRAND()%10;
                    if(at_<0x7E) {
                        XW8(psp,0x81+at_,(endk_<7)?0x0D:(endk_<9)?0:'z');
                        XW8(psp,0x82+at_,0x0D);
                    }
                    XW8(psp,0x80,(uint8_t)at_);
                }
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"MPS_LOGO.EXE") && o->seg==0x0146 && o->ip==0x1894) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
                /* The stack below SP: noise (the locals sit here). [bp-1] is never written by
                 * the routine, only read after the DOS read: 1Ah half of the time. The far
                 * return address goes at SP-4, the saved BP at SP-6, so BP-1 is SP-7. */
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                if(XRAND()&1)XW8(seg[S_SS],(uint16_t)(r[R_SP]-7),0x1A);
                /* The four arguments: path, oflag, shflag, pmode. */
                {
                    uint16_t path=(uint16_t)XRAND();
                    uint16_t oflag,share,pmode;
                    if(XRAND()%8==0)oflag=(uint16_t)XRAND();
                    else {
                        oflag=(uint16_t)(XRAND()&3);
                        if(XRAND()&1)oflag|=0x0008;
                        if(XRAND()&1)oflag|=0x0100;
                        if(XRAND()&1)oflag|=0x0200;
                        if(XRAND()&1)oflag|=0x0400;
                        if(XRAND()%3==0)oflag|=0x4000;
                        if(XRAND()%3==0)oflag|=0x8000;
                    }
                    switch(XRAND()%6) {
                        case 0: case 1: share=0; break;
                        case 2: share=(uint16_t)(XRAND()&0xFF); break;
                        case 3: share=(XRAND()&1)?0x80:0x0180; break;      /* only bit 7 of the share byte */
                        default: share=(uint16_t)XRAND(); break;
                    }
                    switch(XRAND()%4) {
                        case 0: pmode=0; break;
                        case 1: pmode=0x80; break;
                        case 2: pmode=0x180; break;
                        default: pmode=(uint16_t)XRAND(); break;
                    }
                    XW16(seg[S_SS],r[R_SP],path);XW16(seg[S_SS],r[R_SP]+2,oflag);
                    XW16(seg[S_SS],r[R_SP]+4,share);XW16(seg[S_SS],r[R_SP]+6,pmode);
                    small[0]=path;small[1]=oflag;small[2]=share;small[3]=pmode;
                }
                for(unsigned j=0;j<8;j++)XW16(seg[S_SS],(uint16_t)(r[R_SP]+8+2*j),(uint16_t)XRAND());
                /* The runtime's data: noise, then the cells the routine and its callees read. */
                for(unsigned j=0;j<0x3C0;j++)XW8(dg,0x01F0+j,XRAND());
                switch(XRAND()%8) {                               /* the DOS version: below 3 or not, 3 exactly */
                    case 0: XW8(dg,0x0200,2); break;
                    case 1: XW8(dg,0x0200,0); break;
                    case 2: XW8(dg,0x0200,1); break;
                    case 3: XW8(dg,0x0200,(XRAND()&1)?0xFF:0x80); break;
                    case 4: XW8(dg,0x0200,(uint8_t)(4+XRAND()%6)); break;
                    default: XW8(dg,0x0200,3); break;
                }
                switch(XRAND()%4) {                               /* the permission mask */
                    case 0: XW16(dg,0x01FA,0); break;
                    case 1: XW16(dg,0x01FA,0x80); break;
                    case 2: XW16(dg,0x01FA,0xFF7F); break;
                    default: XW16(dg,0x01FA,(uint16_t)XRAND()); break;
                }
                switch(XRAND()%8) {                               /* the handle count */
                    case 0: XW16(dg,0x0205,0); break;
                    case 1: case 2: XW16(dg,0x0205,(uint16_t)(1+XRAND()%4)); break;
                    case 3: XW16(dg,0x0205,(uint16_t)(XRAND()%0x20)); break;
                    case 4: XW16(dg,0x0205,(uint16_t)(0x20+XRAND()%0x2000)); break;
                    case 5: XW16(dg,0x0205,0xFFFF); break;
                    default: XW16(dg,0x0205,(uint16_t)XRAND()); break;
                }
                XW8(dg,0x0447,XRAND());                           /* the default mode, bit 7 */
                /* DOS's answers, in the order the calls are made: the open fails with "not found"
                 * or another error about half of the time, later calls answer carry one time in
                 * three, AX is a small handle or a count, 0 (read: end of file), 2, or noise. */
                g_dos_script_n=10;
                for(unsigned j=0;j<10;j++) {
                    unsigned k=XRAND()%10;
                    uint16_t ax;
                    if(k<2) ax=0; else if(k<4) ax=(uint16_t)(1+XRAND()%5);
                    else if(k<6) ax=2; else if(k<8) ax=(uint16_t)(XRAND()%0x20); else ax=(uint16_t)XRAND();
                    g_dos_script[j].ax=ax;
                    g_dos_script[j].cf=(j==0)?(XRAND()%5<2):(XRAND()%3==0);
                }
                /* the errno table (xlatb at 0268) is the noise written above */
#undef XRAND
#undef XW16
#undef XW8
            }
            if(!strcmp(o->module,"MPS_LOGO.EXE") && o->seg==6 && o->ip==0x72) {
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t vv_=(uint16_t)(v_);XW8((s_),(o_),vv_);XW8((s_),(o_)+1,vv_>>8); } while(0)
                seg[S_DS]=0x5000;seg[S_SS]=(XRAND()&1)?0x6000:seg[S_DS];r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));g_watch_transient=1;
                for(unsigned j=0;j<0x500;j++) { XW8(seg[S_SS],0xEB00+j,XRAND());XW8(seg[S_DS],0xEB00+j,XRAND()); }
                uint16_t argc_=(uint16_t[]){0,1,2,3,0x7FFF,0x8000,0xFFFF}[XRAND()%7];
                uint16_t argv_=(uint16_t)(0x2000+2*(XRAND()%32)),str_=(uint16_t)(0x3000+2*(XRAND()%32));
                XW16(seg[S_SS],0xF000,argc_);XW16(seg[S_SS],0xF002,argv_);small[0]=argc_;small[1]=argv_;
                small[2]=small[3]=(uint16_t)XRAND();XW16(seg[S_SS],0xF004,small[2]);XW16(seg[S_SS],0xF006,small[3]);
                XW16(seg[S_DS],argv_+2,str_);
                static const uint8_t chars_[]={ 'A','I','N','R','T','a','i','n','r','t',0x60,0x61,0x7A,0x7B,0x80,0xFF,0 };
                XW8(seg[S_DS],str_,chars_[XRAND()%(sizeof chars_)]);
                /* The real stack allocator stays executable and is guaranteed
                 * enough room. The other helpers use far returning probes. */
                XW16(seg[S_DS],0x0232,0x1000);
                static const struct { uint16_t sg,ip;unsigned args; } probes_[]={
                    {6,0x000C,1},{0xDA,0x0352,2},{0xDA,0x0491,1},{0xDA,0x0307,2},
                    {6,0x0044,0},{6,0x03E4,1},{6,0x038E,2},{0x6D,0x0112,3},
                    {6,0x033A,2},{0xDA,0x030E,1},{0x45,0x0062,0},{0x6D,0x05BE,9},
                    {0x146,0x0724,1},{0x6D,0x05A2,1},{0xDA,0x0315,0},{0xDA,0x04C4,1}};
                uint16_t sprite_off_=0x2000,sprite_seg_=0x8000;
                switch(XRAND()%8) {case 0:sprite_off_=0;break;case 1:sprite_seg_=0;break;case 2:sprite_off_=sprite_seg_=0;break;default:break;}
                unsigned frames_=(sprite_off_==0 && sprite_seg_==0)?0:XRAND()%5;
                XW16(sprite_seg_,sprite_off_+0x10,frames_);
                for(unsigned j=1;j<6;j++) {
                    for(unsigned k=0;k<14;k++)XW8(sprite_seg_,sprite_off_+0x14+14*j+k,XRAND());
                    XW16(sprite_seg_,sprite_off_+0x18+14*j,(XRAND()&1)?XRAND():0);
                }
                uint16_t clock_lo_=(XRAND()%4==0)?0:((XRAND()&1)?(uint16_t)(0xFFF0+XRAND()%16):(uint16_t)XRAND());
                uint16_t clock_hi_=(uint16_t[]){0,0xFFFF,0x7FFF,0x8000}[XRAND()%4];
                uint16_t key_=(uint16_t[]){0,0x011B,0x1000,0x011A,0x011C,0x0FFF,0x1001,0xFFFF}[XRAND()%8];
                unsigned keys_enabled_=XRAND()&1;
                for(unsigned p=0;p<sizeof probes_/sizeof probes_[0];p++) {
                    uint16_t cell_=(uint16_t)(0xB000+8*p),list_=(uint16_t)(0xD000+0x80*p);
                    XW16(seg[S_DS],cell_,0);XW16(seg[S_DS],cell_+2,0);XW16(seg[S_DS],cell_+4,0);
                    for(unsigned j=0;j<32;j++) {
                        uint16_t ax_=(uint16_t)XRAND(),dx_=(uint16_t)XRAND();
                        if(p==1)ax_=(XRAND()%3)?(uint16_t)(1+XRAND()%100):0;
                        if(p==3)ax_=(j==0 && (XRAND()&1))?1:0;
                        if(p==7){ax_=sprite_off_;dx_=sprite_seg_;}
                        if(p==10){ax_=(j==0)?clock_lo_:(j<4?0:0xFFFF);dx_=(j==0)?clock_hi_:(j==1?0x8000:(j==2?clock_hi_:0x7FFF));}
                        if(p==12)ax_=keys_enabled_?((j&1)?key_:1):0;
                        XW16(seg[S_DS],list_+4*j,ax_);XW16(seg[S_DS],list_+4*j+2,dx_);
                    }
                    unsigned out_=0x1000+0x100*p;
                    /* Far jump to isolated code, preserving the original far
                     * return frame. Fold arguments in order, then use a pair
                     * list for AX:DX, advancing a separate call counter. */
                    XW8(probes_[p].sg,probes_[p].ip,0xEA);XW16(probes_[p].sg,probes_[p].ip+1,out_);XW16(probes_[p].sg,probes_[p].ip+3,0x7000);
#define EMIT(b) XW8(0x7000,out_++,(b))
#define WORD(v) do { uint16_t w_=(uint16_t)(v);EMIT(w_);EMIT(w_>>8); } while(0)
                    EMIT(0x55);EMIT(0x8B);EMIT(0xEC);EMIT(0x53);
                    EMIT(0xFF);EMIT(0x06);WORD(cell_+4);
                    for(unsigned j=0;j<probes_[p].args;j++) {
                        EMIT(0x8B);EMIT(0x46);EMIT(6+2*j);
                        EMIT(0x8B);EMIT(0x1E);WORD(cell_);
                        EMIT(0xD1);EMIT(0xC3);EMIT(0x33);EMIT(0xD8);
                        EMIT(0x89);EMIT(0x1E);WORD(cell_);
                    }
                    EMIT(0x8B);EMIT(0x1E);WORD(cell_+2);
                    EMIT(0x8B);EMIT(0x87);WORD(list_);EMIT(0x8B);EMIT(0x97);WORD(list_+2);
                    EMIT(0x83);EMIT(0xC3);EMIT(4);EMIT(0x83);EMIT(0xE3);EMIT(0x7C);
                    EMIT(0x89);EMIT(0x1E);WORD(cell_+2);EMIT(0x5B);EMIT(0x5D);EMIT(0xCB);
#undef WORD
#undef EMIT
                }
#undef XW16
#undef XW8
#undef XRAND
            }



            if (!strcmp(o->module,"MPS_LOGO.EXE") && o->seg==0x0146 && o->ip==0x08CC) {
                const uint16_t dg=0x5000, psp=0x4000, env=0x3000;
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
                seg[S_DS]=dg;seg[S_SS]=(XRAND()%4==0)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                g_dos=1;g_dos_script_n=1;{static const uint8_t major_[]={0,1,2,2,2,3,5,6,7,255};const uint16_t minor_=(uint16_t)(XRAND()&255);const uint8_t majorver_=major_[XRAND()%10];g_dos_script[0].ax=(uint16_t)((minor_<<8)|majorver_);}g_dos_script[0].cf=(uint8_t)(XRAND()&1);
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                for(unsigned j=0;j<0x800;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x800+j),XRAND());
                /* The data words: the program prefix segment (in both data and stack segments, as the
                 * routine reads it through SS later), stale name pointer and return slot. */
                XW16(dg,0x01FE,psp);XW16(seg[S_SS],0x01FE,psp);
                XW16(dg,0x0221,0x0010);XW16(dg,0x0223,0x2000);
                XW16(seg[S_SS],0x0221,0x0010);XW16(seg[S_SS],0x0223,0x2000);
                {const unsigned sl_=XRAND()%10;for(unsigned j=0;j<sl_;j++)XW8(0x2000,0x10+j,1+XRAND()%255);XW8(0x2000,0x10+sl_,0);}
                XW16(dg,0x0264,(uint16_t)XRAND());XW16(seg[S_SS],0x0264,back);XW16(seg[S_SS],0x0266,cs);
                /* The environment block: strings, an empty one, a count word, the program name. Now and
                 * then the block has no zero for 8000h bytes (the REP SCASB runs out of count). */
                {
                    const unsigned mode_=XRAND()%24;
                    unsigned at_=0;
                    if(mode_==0) {
                        for(unsigned j=0;j<0x8010;j++)XW8(env,(uint16_t)j,1+XRAND()%255);
                        for(unsigned j=0x8010;j<0x8030;j++)XW8(env,(uint16_t)j,(XRAND()%3)?0:(uint8_t)(1+XRAND()%255));
                    } else {
                        const unsigned n_=(mode_<4)?0:XRAND()%5;
                        for(unsigned k=0;k<n_;k++) {
                            const unsigned len_=1+XRAND()%12;
                            for(unsigned j=0;j<len_;j++)XW8(env,at_++,1+XRAND()%255);
                            XW8(env,at_++,0);
                        }
                        XW8(env,at_++,0);                       /* the empty string ends the block */
                        if(n_==0&&(XRAND()&1))XW8(env,at_++,0);  /* an empty environment is two zeros */
                        XW16(env,at_,XRAND()%3);at_+=2;           /* the word count */
                        const unsigned nl_=(XRAND()%8==0)?0:(XRAND()%6==0)?(1+XRAND()%80):(1+XRAND()%12);
                        for(unsigned j=0;j<nl_;j++)XW8(env,at_++,1+XRAND()%255);
                        for(unsigned j=0;j<0x20;j++)XW8(env,at_+j,0);
                    }
                    XW16(psp,0x2C,env);
                }
                /* The command tail at prefix:81h: blanks, tabs, quotes, backslash runs, letters; ended by a
                 * carriage return, a zero, or (rarely) left unterminated within the 7Fh bytes. */
                {
                    unsigned at_=0;
                    const unsigned want_=(XRAND()%5==0)?0:(XRAND()%6==0)?(40+XRAND()%80):(1+XRAND()%30);
                    const unsigned lead_=(XRAND()%3==0)?XRAND()%4:0;
                    for(unsigned j=0;j<lead_&&at_<0x7E;j++)XW8(psp,0x81+at_++,(XRAND()&1)?0x20:9);
                    while(at_<want_&&at_<0x7E) {
                        const unsigned t_=XRAND()%24;
                        if(t_<7)      XW8(psp,0x81+at_++,"abcxyz-"[XRAND()%7]);
                        else if(t_<11)XW8(psp,0x81+at_++,0x20);
                        else if(t_<12)XW8(psp,0x81+at_++,9);
                        else if(t_<16)XW8(psp,0x81+at_++,0x22);
                        else if(t_<22) {
                            const unsigned run_=1+XRAND()%6;
                            for(unsigned j=0;j<run_&&at_<0x7E;j++)XW8(psp,0x81+at_++,0x5C);
                            if(XRAND()%2&&at_<0x7E)XW8(psp,0x81+at_++,0x22);
                        }
                        else if(t_<23)XW8(psp,0x81+at_++,(XRAND()%3)?'q':(XRAND()%2)?0x0D:0);
                        else          XW8(psp,0x81+at_++,0x5C);
                    }
                    const unsigned endk_=XRAND()%10;
                    if(at_<0x7E) {
                        XW8(psp,0x81+at_,(endk_<7)?0x0D:(endk_<9)?0:'z');
                        XW8(psp,0x82+at_,0x0D);
                    }
                    XW8(psp,0x80,(uint8_t)at_);
                }
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"DSWAP.EXE") && o->seg==0 && o->ip==0x1D66) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
                /* The stack below SP: noise (the locals sit here). [bp-1] is never written by
                 * the routine, only read after the DOS read: 1Ah half of the time. The near
                 * return address goes at SP-2, saved BP at SP-4, so BP-1 is SP-5. */
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                if(XRAND()&1)XW8(seg[S_SS],(uint16_t)(r[R_SP]-5),0x1A);
                /* The four arguments: path, oflag, shflag, pmode. */
                {
                    uint16_t path=(uint16_t)XRAND();
                    uint16_t oflag,share,pmode;
                    if(XRAND()%8==0)oflag=(uint16_t)XRAND();
                    else {
                        oflag=(uint16_t)(XRAND()&3);
                        if(XRAND()&1)oflag|=0x0008;
                        if(XRAND()&1)oflag|=0x0100;
                        if(XRAND()&1)oflag|=0x0200;
                        if(XRAND()&1)oflag|=0x0400;
                        if(XRAND()%3==0)oflag|=0x4000;
                        if(XRAND()%3==0)oflag|=0x8000;
                    }
                    switch(XRAND()%6) {
                        case 0: case 1: share=0; break;
                        case 2: share=(uint16_t)(XRAND()&0xFF); break;
                        case 3: share=(XRAND()&1)?0x80:0x0180; break;      /* only bit 7 of the share byte */
                        default: share=(uint16_t)XRAND(); break;
                    }
                    switch(XRAND()%4) {
                        case 0: pmode=0; break;
                        case 1: pmode=0x80; break;
                        case 2: pmode=0x180; break;
                        default: pmode=(uint16_t)XRAND(); break;
                    }
                    XW16(seg[S_SS],r[R_SP],path);XW16(seg[S_SS],r[R_SP]+2,oflag);
                    XW16(seg[S_SS],r[R_SP]+4,share);XW16(seg[S_SS],r[R_SP]+6,pmode);
                    small[0]=path;small[1]=oflag;small[2]=share;small[3]=pmode;
                }
                for(unsigned j=0;j<8;j++)XW16(seg[S_SS],(uint16_t)(r[R_SP]+8+2*j),(uint16_t)XRAND());
                /* The runtime's data: noise, then the cells the routine and its callees read. */
                for(unsigned j=0;j<0x3C0;j++)XW8(dg,0x26B0+j,XRAND());
                switch(XRAND()%8) {                               /* the DOS version: below 3 or not, 3 exactly */
                    case 0: XW8(dg,0x26CA,2); break;
                    case 1: XW8(dg,0x26CA,0); break;
                    case 2: XW8(dg,0x26CA,1); break;
                    case 3: XW8(dg,0x26CA,(XRAND()&1)?0xFF:0x80); break;
                    case 4: XW8(dg,0x26CA,(uint8_t)(4+XRAND()%6)); break;
                    default: XW8(dg,0x26CA,3); break;
                }
                switch(XRAND()%4) {                               /* the permission mask */
                    case 0: XW16(dg,0x26C4,0); break;
                    case 1: XW16(dg,0x26C4,0x80); break;
                    case 2: XW16(dg,0x26C4,0xFF7F); break;
                    default: XW16(dg,0x26C4,(uint16_t)XRAND()); break;
                }
                switch(XRAND()%8) {                               /* the handle count */
                    case 0: XW16(dg,0x26CF,0); break;
                    case 1: case 2: XW16(dg,0x26CF,(uint16_t)(1+XRAND()%4)); break;
                    case 3: XW16(dg,0x26CF,(uint16_t)(XRAND()%0x20)); break;
                    case 4: XW16(dg,0x26CF,(uint16_t)(0x20+XRAND()%0x2000)); break;
                    case 5: XW16(dg,0x26CF,0xFFFF); break;
                    default: XW16(dg,0x26CF,(uint16_t)XRAND()); break;
                }
                XW8(dg,0x28D3,XRAND());                           /* the default mode, bit 7 */
                /* DOS's answers, in the order the calls are made: the open fails with "not found"
                 * or another error about half of the time, later calls answer carry one time in
                 * three, AX is a small handle or a count, 0 (read: end of file), 2, or noise. */
                g_dos=1;g_dos_script_n=10;
                for(unsigned j=0;j<10;j++) {
                    unsigned k=XRAND()%10;
                    uint16_t ax;
                    if(k<2) ax=0; else if(k<4) ax=(uint16_t)(1+XRAND()%5);
                    else if(k<6) ax=2; else if(k<8) ax=(uint16_t)(XRAND()%0x20); else ax=(uint16_t)XRAND();
                    g_dos_script[j].ax=ax;
                    g_dos_script[j].cf=(j==0)?(XRAND()%5<2):(XRAND()%3==0);
                }
                /* the errno table (xlatb at 26FC) is the noise written above */
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"DSWAP.EXE") && o->seg==0 && o->ip==0x117E) {
                const uint16_t dg=0x5000, psp=0x4000, env=0x3000;
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
                seg[S_DS]=dg;seg[S_SS]=(XRAND()%4==0)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                g_dos=1;g_dos_script_n=1;{static const uint8_t major_[]={0,1,2,2,2,3,5,6,7,255};const uint16_t minor_=(uint16_t)(XRAND()&255);const uint8_t majorver_=major_[XRAND()%10];g_dos_script[0].ax=(uint16_t)((minor_<<8)|majorver_);}g_dos_script[0].cf=(uint8_t)(XRAND()&1);
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                for(unsigned j=0;j<0x800;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x800+j),XRAND());
                /* The data words: the program prefix segment (in both data and stack segments, as the
                 * routine reads it through SS later), stale name pointer and return slot. */
                XW16(dg,0x26C8,psp);XW16(seg[S_SS],0x26C8,psp);
                XW16(dg,0x26EB,0x0010);XW16(dg,0x26ED,0x2000);
                XW16(seg[S_SS],0x26EB,0x0010);XW16(seg[S_SS],0x26ED,0x2000);
                {const unsigned sl_=XRAND()%10;for(unsigned j=0;j<sl_;j++)XW8(0x2000,0x10+j,1+XRAND()%255);XW8(0x2000,0x10+sl_,0);}
                XW16(dg,0x26FA,(uint16_t)XRAND());XW16(seg[S_SS],0x26FA,back);
                /* The environment block: strings, an empty one, a count word, the program name. Now and
                 * then the block has no zero for 8000h bytes (the REP SCASB runs out of count). */
                {
                    const unsigned mode_=XRAND()%24;
                    unsigned at_=0;
                    if(mode_==0) {
                        for(unsigned j=0;j<0x8010;j++)XW8(env,(uint16_t)j,1+XRAND()%255);
                        for(unsigned j=0x8010;j<0x8030;j++)XW8(env,(uint16_t)j,(XRAND()%3)?0:(uint8_t)(1+XRAND()%255));
                    } else {
                        const unsigned n_=(mode_<4)?0:XRAND()%5;
                        for(unsigned k=0;k<n_;k++) {
                            const unsigned len_=1+XRAND()%12;
                            for(unsigned j=0;j<len_;j++)XW8(env,at_++,1+XRAND()%255);
                            XW8(env,at_++,0);
                        }
                        XW8(env,at_++,0);                       /* the empty string ends the block */
                        if(n_==0&&(XRAND()&1))XW8(env,at_++,0);  /* an empty environment is two zeros */
                        XW16(env,at_,XRAND()%3);at_+=2;           /* the word count */
                        const unsigned nl_=(XRAND()%8==0)?0:(XRAND()%6==0)?(1+XRAND()%80):(1+XRAND()%12);
                        for(unsigned j=0;j<nl_;j++)XW8(env,at_++,1+XRAND()%255);
                        for(unsigned j=0;j<0x20;j++)XW8(env,at_+j,0);
                    }
                    XW16(psp,0x2C,env);
                }
                /* The command tail at prefix:81h: blanks, tabs, quotes, backslash runs, letters; ended by a
                 * carriage return, a zero, or (rarely) left unterminated within the 7Fh bytes. */
                {
                    unsigned at_=0;
                    const unsigned want_=(XRAND()%5==0)?0:(XRAND()%6==0)?(40+XRAND()%80):(1+XRAND()%30);
                    const unsigned lead_=(XRAND()%3==0)?XRAND()%4:0;
                    for(unsigned j=0;j<lead_&&at_<0x7E;j++)XW8(psp,0x81+at_++,(XRAND()&1)?0x20:9);
                    while(at_<want_&&at_<0x7E) {
                        const unsigned t_=XRAND()%24;
                        if(t_<7)      XW8(psp,0x81+at_++,"abcxyz-"[XRAND()%7]);
                        else if(t_<11)XW8(psp,0x81+at_++,0x20);
                        else if(t_<12)XW8(psp,0x81+at_++,9);
                        else if(t_<16)XW8(psp,0x81+at_++,0x22);
                        else if(t_<22) {
                            const unsigned run_=1+XRAND()%6;
                            for(unsigned j=0;j<run_&&at_<0x7E;j++)XW8(psp,0x81+at_++,0x5C);
                            if(XRAND()%2&&at_<0x7E)XW8(psp,0x81+at_++,0x22);
                        }
                        else if(t_<23)XW8(psp,0x81+at_++,(XRAND()%3)?'q':(XRAND()%2)?0x0D:0);
                        else          XW8(psp,0x81+at_++,0x5C);
                    }
                    const unsigned endk_=XRAND()%10;
                    if(at_<0x7E) {
                        XW8(psp,0x81+at_,(endk_<7)?0x0D:(endk_<9)?0:'z');
                        XW8(psp,0x82+at_,0x0D);
                    }
                    XW8(psp,0x80,(uint8_t)at_);
                }
#undef XRAND
#undef XW16
#undef XW8
            }
            if(!strcmp(o->module,"DSWAP.EXE") && o->seg==0 && o->ip==0x0010) {
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t w__=(uint16_t)(v_);XW8((s_),(o_),w__);XW8((s_),(o_)+1,w__>>8); } while(0)
                seg[S_DS]=0x5000;seg[S_SS]=(XRAND()&1)?0x6000:seg[S_DS];r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));g_watch_transient=1;
                for(unsigned j=0;j<0x200;j++){XW8(seg[S_SS],0xEE00+j,XRAND());XW8(seg[S_DS],0xEE00+j,XRAND());}
                uint16_t argv_=(uint16_t)(0x3000+2*(XRAND()%32));
                small[0]=(uint16_t)XRAND();small[1]=argv_;small[2]=(uint16_t)XRAND();small[3]=(uint16_t)XRAND();
                for(unsigned j=0;j<4;j++)XW16(seg[S_SS],r[R_SP]+2*j,small[j]);
                XW16(seg[S_DS],argv_+2,0x3400+2*(XRAND()%32));
                for(unsigned j=0;j<8;j++){XW16(seg[S_DS],0x6A+2*j,XRAND());XW16(seg[S_DS],0x70+2*j,XRAND());}
                XW16(0,0x04F0,0x8000);XW16(0x8000,0x1A,XRAND());XW16(0x8000,0x1E,XRAND());
                static const uint8_t drive_flags_[]={0,1,0x3F,0x40,0x80,0xC0,0xFF};
                XW8(0,0x0410,drive_flags_[XRAND()%7]);
                /* Near and far probes fold all arguments and count calls.
                 * Return lists eventually succeed, bounding every retry loop. */
                static const struct { uint16_t sg,ip;unsigned args; } stub_[]={
                    {0,0x0FF8,1},{0,0x0F2E,2},{0,0x0E36,1},{0,0x085A,1},
                    {0x225,0x08DC,1},{0x225,0x0A53,2},{0x225,0x0922,1},{0,0x0494,1},
                    {0,0x10D0,0},{0,0x0523,0},{0,0x10F4,2},{0,0x107A,4},
                    {0,0x1120,2},{0,0x023E,1},{0x225,0x0A9E,0},{0x225,0x0AA3,0},{0,0x0219,1}};
                static const uint16_t drive_choices_[]={0,1,2,0xFFFF};
                static const uint16_t key_choices_[]={0x42,0x62,0x41,0x43,0x61,0x63,0,0xFF};
                for(unsigned k=0;k<sizeof stub_/sizeof stub_[0];k++) {
                    uint16_t cell_=(uint16_t)(0xB000+8*k),list_=(uint16_t)(0xC000+0x40*k);
                    XW16(seg[S_DS],cell_,0);XW16(seg[S_DS],cell_+2,0);XW16(seg[S_DS],cell_+4,0);
                    for(unsigned j=0;j<32;j++) {
                        uint16_t ax_=(uint16_t)XRAND();
                        if(k==0)ax_=(uint16_t)(XRAND()%3);
                        if(k==1)ax_=(j>=3)?(uint16_t)(1+XRAND()%7):((XRAND()&1)?0:(uint16_t)(1+XRAND()%7));
                        if(k==7)ax_=(uint16_t)(0x8100+XRAND()%128);
                        if(k==8)ax_=drive_choices_[XRAND()%4];
                        if(k==10)ax_=(j>=16 || XRAND()%4==0)?(uint16_t)(XRAND()&0xFF):(uint16_t)(0x100|(XRAND()&0xFFFF));
                        if(k==14)ax_=(j>2 || (XRAND()&1))?0:1;
                        if(k==15)ax_=key_choices_[XRAND()%8];
                        XW16(seg[S_DS],list_+2*j,ax_);
                    }
                    /* Separate segments keep large argument-folding stubs
                     * clear of the original's code and its low memory tables. */
                    unsigned out_=stub_[k].sg?0x1000+0x100*k:0xD100+0x100*k; const uint16_t code_seg_=stub_[k].sg?0x7000:0;
                    if(stub_[k].sg==0) {
                        XW8(0,stub_[k].ip,0xE9);XW16(0,stub_[k].ip+1,(uint16_t)(out_-stub_[k].ip-3));

                    } else {
                        XW8(stub_[k].sg,stub_[k].ip,0xEA);XW16(stub_[k].sg,stub_[k].ip+1,out_);XW16(stub_[k].sg,stub_[k].ip+3,0x7000);
                    }
#define EMIT(b_) XW8(code_seg_,out_++,(b_))
#define WORD(v_) do { uint16_t w_=(uint16_t)(v_);EMIT(w_);EMIT(w_>>8); } while(0)
                    EMIT(0x55);EMIT(0x8B);EMIT(0xEC);EMIT(0x53);
                    EMIT(0xFF);EMIT(0x06);WORD(cell_+4);
                    for(unsigned j=0;j<stub_[k].args;j++) {
                        EMIT(0x8B);EMIT(0x46);EMIT((stub_[k].sg?6:4)+2*j);
                        EMIT(0x8B);EMIT(0x1E);WORD(cell_);EMIT(0xD1);EMIT(0xC3);EMIT(0x33);EMIT(0xD8);
                        EMIT(0x89);EMIT(0x1E);WORD(cell_);
                    }
                    EMIT(0x8B);EMIT(0x1E);WORD(cell_+2);EMIT(0x8B);EMIT(0x87);WORD(list_);
                    EMIT(0x83);EMIT(0xC3);EMIT(2);EMIT(0x83);EMIT(0xE3);EMIT(0x3E);
                    EMIT(0x89);EMIT(0x1E);WORD(cell_+2);EMIT(0x5B);EMIT(0x5D);EMIT(stub_[k].sg?0xCB:0xC3);
#undef WORD
#undef EMIT
                }
#undef XW16
#undef XW8
#undef XRAND
            }

            if (!strcmp(o->module,"PLAYER.EXE") && o->seg==0 && o->ip==0x005C) {
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XW8(s_,o_,v_) do { const uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { const uint16_t v__=(uint16_t)(v_);XW8(s_,o_,v__);XW8(s_,(o_)+1,v__>>8); } while(0)
                const uint16_t dg=0x5000, hand=0x3000;
                seg[S_DS]=dg;seg[S_SS]=(XRAND()&1)?dg:0x6000;r[R_SP]=0xE000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],r[R_SP]-0x200+j,XRAND());
                for(unsigned j=0x1E50;j<0x20A0;j++)XW8(dg,j,XRAND());
                XW16(0,0x04F0,hand);XW16(hand,0x1C,XRAND());
                static const uint16_t sizes_[]={0,1,0x1FFF,0x2000,0x2001,0x7FFF,0x8000,0xFFFF};
                static const uint16_t bases_[]={0,1,0x0FFF,0x1000,0x7F80,0xFF7F,0xFF80,0xFFFF};
                static const uint16_t err_[]={0,7,8,8,8,9,0xFFFF};
                const uint16_t size_=sizes_[XRAND()%8],base_=bases_[XRAND()%8];
                XW16(seg[S_SS],r[R_SP]-6,size_);
                XW16(dg,0xD000,err_[XRAND()%7]);XW16(dg,0xD002,(XRAND()&1)?0:1+XRAND()%15);
                XW16(dg,0xD010,size_);XW16(dg,0xD012,base_);
                static const uint16_t entry_[]={0x0F6C,0x14A0,0x1476,0x0DB0,0x0E0C,0x0583,0x0520,0x0540,0x000A};
                static const uint8_t argc_[]={1,2,3,0,1,0,0,1,0};
                for(unsigned k=0;k<9;k++) {
                    const uint16_t cell_=(uint16_t)(0xD100+8*k);
                    const uint16_t stub_=(uint16_t)(0xF000+0x100*k);
                    const uint16_t sg_=k==8?0x026B:cs;
                    uint16_t at_=k==8?entry_[k]:stub_;
#define XE8(v_) XW8(sg_,at_++,(v_))
#define XE16(v_) do { const uint16_t w_=(uint16_t)(v_);XE8(w_);XE8(w_>>8); } while(0)
                    XW16(dg,cell_,XRAND());XW16(dg,cell_+2,0);
                    if(k!=8) {XW8(cs,entry_[k],0xE9);XW16(cs,entry_[k]+1,(uint16_t)(stub_-entry_[k]-3));}
                    XE8(0x55);XE8(0x89);XE8(0xE5);XE8(0x53);XE8(0x56);XE8(0x57);
                    XE8(0x8B);XE8(0x1E);XE16(cell_);
                    for(unsigned a=0;a<argc_[k];a++) {
                        XE8(0xD1);XE8(0xC3);XE8(0x8B);XE8(0x46);XE8(4+2*a);XE8(0x31);XE8(0xC3);
                    }
                    XE8(0x89);XE8(0x1E);XE16(cell_);
                    if(k==1) {
                        XE8(0x8B);XE8(0x36);XE16(cell_+2);XE8(0xD1);XE8(0xE6);
                        XE8(0x8B);XE8(0x84);XE16(0xD010);XE8(0x8B);XE8(0x7E);XE8(6);XE8(0x89);XE8(0x05);
                        XE8(0x8B);XE8(0x84);XE16(0xD000);
                    } else {
                        const uint16_t result_=k==6?((XRAND()&1)?0x13:(uint16_t)XRAND()):(uint16_t)XRAND();
                        XE8(0xB8);XE16(result_);
                    }
                    XE8(0xFF);XE8(0x06);XE16(cell_+2);
                    XE8(0x5F);XE8(0x5E);XE8(0x5B);XE8(0x5D);XE8(k==8?0xCB:0xC3);
#undef XE16
#undef XE8
                }
#undef XW16
#undef XW8
#undef XRAND
            }
            if(!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0xE586) {
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t w__=(uint16_t)(v_);XW8((s_),(o_),w__);XW8((s_),(o_)+1,w__>>8); } while(0)
                seg[S_DS]=0x5000;seg[S_SS]=(XRAND()&1)?0x6000:seg[S_DS];r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));g_watch_transient=1;
                for(unsigned j=0;j<0x200;j++){XW8(seg[S_SS],0xEE00+j,XRAND());XW8(seg[S_DS],0xEE00+j,XRAND());}
                for(unsigned j=0;j<4;j++){small[j]=(uint16_t)XRAND();XW16(seg[S_SS],r[R_SP]+2*j,small[j]);}
                uint16_t stream_=(uint16_t)(0x3000+0x10*(XRAND()%16));
                static const uint8_t handles_[]={0,1,0x7F,0x80,0xFE,0xFF};
                XW8(seg[S_DS],stream_+7,handles_[XRAND()%6]);
                static const uint32_t sizes_[]={0,1,14,15,16,17,0xFFFE,0xFFFF,0x10000,0x10001,0x1FFFE,0x1FFFF,0x20000,0xFFFF0,0xFFFF1,0x80000000,0xFFFFFFF0,0xFFFF0000,0xFFFFFFFF};
                uint32_t bytes_=sizes_[XRAND()%(sizeof sizes_/sizeof sizes_[0])];
                uint32_t rounded_=(bytes_+15u);rounded_=(uint32_t)((int32_t)rounded_>>4);
                uint16_t free_=(uint16_t[]){0,1,2,0x100,0x1000,0x8000,0xFFFF}[XRAND()%7];
                if(XRAND()%3==0)free_=(uint16_t)((rounded_&0xFFFF)+((XRAND()%3)-1));
                uint16_t allocated_=(uint16_t)(0x8000+XRAND()%256);
                uint16_t alloc_result_=(XRAND()%4==0)?5:0;
                for(unsigned j=0;j<0x20;j++)XW8(seg[S_DS],0xC5F0+j,XRAND());
                for(unsigned j=0;j<0x10;j++)XW8(seg[S_DS],0xE00E + j,XRAND());
                static const struct { uint16_t sg,ip;unsigned args; } stub_[]={
                    {0,0xEA1C,2},{0,0xEC90,1},{0x1E42,0x0214,0},
                    {0,0xE946,1},{0,0xEE52,2},{0,0xED10,4}};
                for(unsigned k=0;k<sizeof stub_/sizeof stub_[0];k++) {
                    uint16_t cell_=(uint16_t)(0xA000+8*k);XW16(seg[S_DS],cell_,0);XW16(seg[S_DS],cell_+2,0);
                    uint16_t ax_=(uint16_t)XRAND(),dx_=(uint16_t)XRAND();
                    if(k==0)ax_=(XRAND()%5==0)?0:stream_;
                    if(k==1){ax_=(uint16_t)bytes_;dx_=(uint16_t)(bytes_>>16);}
                    if(k==2)ax_=free_;
                    if(k==4)ax_=alloc_result_;
                    unsigned out_=stub_[k].sg?0x1000:0xD000+0x100*k;
                    const uint16_t code_seg_=stub_[k].sg?0x7000:0;
                    if(stub_[k].sg) {
                        XW8(stub_[k].sg,stub_[k].ip,0xEA);XW16(stub_[k].sg,stub_[k].ip+1,out_);XW16(stub_[k].sg,stub_[k].ip+3,code_seg_);
                    } else {XW8(0,stub_[k].ip,0xE9);XW16(0,stub_[k].ip+1,(uint16_t)(out_-stub_[k].ip-3));}
#define EMIT(b_) XW8(code_seg_,out_++,(b_))
#define WORD(v_) do { uint16_t w_=(uint16_t)(v_);EMIT(w_);EMIT(w_>>8); } while(0)
                    EMIT(0x55);EMIT(0x8B);EMIT(0xEC);EMIT(0x53);
                    EMIT(0xFF);EMIT(0x06);WORD(cell_+2);
#define FOLD_AX() do { EMIT(0x8B);EMIT(0x1E);WORD(cell_);EMIT(0xD1);EMIT(0xC3);EMIT(0x33);EMIT(0xD8);EMIT(0x89);EMIT(0x1E);WORD(cell_); } while(0)
                    for(unsigned j=0;j<stub_[k].args;j++) {EMIT(0x8B);EMIT(0x46);EMIT((stub_[k].sg?6:4)+2*j);FOLD_AX();}
                    /* The read probe consumes the register pack at EVERY
                     * call, so a wrong full-chunk value cannot be hidden by
                     * the final remainder overwriting that global field. */
                    if(k==5) {
                        static const uint16_t fields_[]={0xC5F6,0xC5F8,0xC5FA,0xC5FC,0xE014};
                        for(unsigned j=0;j<5;j++){EMIT(0xA1);WORD(fields_[j]);FOLD_AX();}
                    }
                    /* DOS allocation writes through DS, just as EE52 does,
                     * even when the caller's frame is in another SS. */
                    if(k==4) {EMIT(0x8B);EMIT(0x5E);EMIT(6);EMIT(0xC7);EMIT(0x07);WORD(allocated_);}
                    EMIT(0xB8);WORD(ax_);EMIT(0xBA);WORD(dx_);EMIT(0x5B);EMIT(0x5D);EMIT(stub_[k].sg?0xCB:0xC3);
#undef FOLD_AX
#undef WORD
#undef EMIT
                }
#undef XW16
#undef XW8
#undef XRAND
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0x0FB2 && o->ip==0x0330) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags&~F_DF);
#define LW8(s_,o_,v_) do { const uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define LW16(s_,o_,v_) do { const uint16_t w_=(uint16_t)(v_);LW8((s_),(o_),w_);LW8((s_),(o_)+1,w_>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define LRN() (rng_=rng_*1664525u+1013904223u,rng_>>16)
                for(unsigned j=0;j<0x200;j++)LW8(seg[S_SS],r[R_SP]-0x200+j,LRN());
                for(unsigned j=0;j<0x40;j++)LW8(dg,0x2280+j,LRN());
                LW16(0,0,LRN());LW16(0,2,LRN());
                /* A genuinely relocated handler operand: the unchanged original handler
                 * is copied to the selected segment, so overflow still executes it. */
                const uint16_t handler_seg_=(LRN()&1)?0x0FB2:0x3A00;
                if(handler_seg_!=0x0FB2)for(unsigned j=0;j<0x21;j++) {
                    const uint8_t b_=g_pristine[phys(0x0FB2,0x04DD+j)];LW8(handler_seg_,0x04DD+j,b_);
                }
                LW16(0x0FB2,0x034B,handler_seg_);
                /* The runtime draw thunk is a RETF probe. It stores all four register
                 * arguments and counts calls without changing any input register. */
                const uint16_t draw_seg_=(LRN()&1)?0x1E42:0x3B00;
                LW16(0x0FB2,0x0374,draw_seg_);
                { static const uint8_t code_[]={0xA3,0x00,0xE0,0x89,0x1E,0x02,0xE0,
                    0x89,0x0E,0x04,0xE0,0x89,0x16,0x06,0xE0,0xFF,0x06,0x08,0xE0,0xCB};
                  for(unsigned j=0;j<sizeof code_;j++)LW8(draw_seg_,0x01B5+j,code_[j]); }
                for(unsigned j=0;j<10;j++)LW8(dg,0xE000+j,LRN());
                /* Independent boundaries, ordinary visible/offscreen coordinates, exact
                 * borders, and signed extremes exercise both delta overflow halves. */
                static const uint16_t bounds_[]={1,2,3,7,31,159,199,319,639,0x1000,0x3FFF,0x7FFF};
                const uint16_t width_=bounds_[LRN()%(sizeof bounds_/sizeof bounds_[0])];
                const uint16_t height_=bounds_[LRN()%(sizeof bounds_/sizeof bounds_[0])];
                LW16(dg,0x2293,width_);LW16(dg,0x2295,height_);
                for(unsigned j=0;j<4;j++) {
                    const uint16_t bound_=(j&1)?height_:width_;
                    uint16_t v_;switch(LRN()%16) {
                      case 0:v_=0;break;case 1:v_=1;break;case 2:v_=0xFFFF;break;
                      case 3:v_=bound_;break;case 4:v_=(uint16_t)(bound_-1);break;
                      case 5:v_=(uint16_t)(bound_+1);break;case 6:v_=0x8000;break;
                      case 7:v_=0x7FFF;break;case 8:v_=0x8001;break;
                      case 9:v_=(uint16_t)(LRN()%(bound_+1));break;
                      case 10:v_=(uint16_t)(-1-(int)(LRN()%256));break;
                      case 11:v_=(uint16_t)(bound_+1+LRN()%256);break;
                      default:v_=(uint16_t)LRN();break;
                    }
                    static const uint16_t cells_[]={0x2299,0x229D,0x229B,0x229F};LW16(dg,cells_[j],v_);
                }
#undef LRN
#undef LW16
#undef LW8
            }
            if (!strcmp(o->module,"START.EXE") && o->seg==0 && o->ip==0x0448) {
                const uint16_t dg=0x5000;
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                seg[S_DS]=dg;seg[S_SS]=XPICK(2)?(uint16_t)0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The routine takes no arguments; the words above SP are noise,
                 * and the harness's small[] copies of them match. */
                for(unsigned j=0;j<8;j++) {
                    const uint16_t w=(uint16_t)XRAND();
                    XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w);
                    if(j<4)small[j]=w;
                }
                /* The two sprite record tables (12 bytes a record at 6Ah and at 106h),
                 * indexed by a signed byte: every record any index can reach lies in
                 * 0..80Fh or F900..FFFF, all noise. The index bytes themselves live
                 * at 136h and 148h, inside that range. */
                for(unsigned j=0;j<0x810;j++)XW8(dg,j,XRAND());
                for(unsigned j=0xF900;j<0x10000;j++)XW8(dg,j,XRAND());
                /* The frame's sprite numbers: -1 (none) often, small mostly, at
                 * times anything (the IMUL's sign and high word). */
                for(unsigned j=0;j<0x24;j++) {
                    uint8_t b;
                    switch(XPICK(10)) {
                        case 0: case 1: case 2: b=0xFF; break;
                        case 3: b=(uint8_t)XRAND(); break;
                        case 4: { static const uint8_t e[6]={0x80,0x7F,0xFE,0x00,0x01,0xAB}; b=e[XPICK(6)]; } break;
                        default: b=(uint8_t)XPICK(16); break;
                    }
                    XW8(dg,0x0136+j,b);
                }
                /* The tick byte the wait reads back, and its neighbours. */
                XW8(dg,0xADFB,XRAND());XW8(dg,0xADFC,XRAND());XW8(dg,0xADFD,XRAND());
                /* The callees: each stub folds a number naming it and then every
                 * argument, in order, into one shared cell (F800), counts its calls
                 * (F802 + 2k), keeps a list index (F820 + 2k) and returns the next
                 * word of its list (F400 + 40h * k). The driver's rectangle copy
                 * also adds its answer's low byte to the tick byte [ADFC], as the
                 * timer would advance it while the frame is drawn. */
                for(unsigned k=0;k<4;k++)for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*k+2*j,XRAND());
                for(unsigned j=0;j<0x40;j+=2)XW16(dg,0xF800+j,0);
                /* The wait's answers (k 2): zero for every frame in half the
                 * states (the animation runs out; the 19th answer, the last wait's,
                 * is noise and ignored), else a press at one frame, its answer
                 * non-zero in either byte. */
                {
                    const unsigned press=XPICK(2)?XPICK(18):99u;
                    for(unsigned j=0;j<32;j++) {
                        uint16_t v=0;
                        if(j==press) {
                            static const uint16_t e[6]={0x0001,0x0100,0x8000,0xFFFF,0x0080,0x1234};
                            v=XPICK(3)?e[XPICK(6)]:(uint16_t)(XRAND()|1);
                        } else if(j>=18 || j>press) v=(uint16_t)XRAND();
                        XW16(dg,0xF400+0x40*2+2*j,v);
                    }
                }
#define XSTUB(sg_,off_,k_,id_,nargs_,far_,tick_) do { \
    uint8_t cb_[200]; unsigned n_=0; \
    const unsigned cnt_=0xF802+2*(k_),ptr_=0xF820+2*(k_),list_=0xF400+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(id_)&255;cb_[n_++]=(id_)>>8; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
        cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; } \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    if(tick_) { cb_[n_++]=0x00;cb_[n_++]=0x06;cb_[n_++]=0xFC;cb_[n_++]=0xAD; } \
    cb_[n_++]=0x83;cb_[n_++]=0xC3;cb_[n_++]=0x02; \
    cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
                /* k: 0 the driver's rectangle copy 0A95:6A8A (8 words), 1 the
                 * driver's sound thunk 0A95:6BB1 (1), 2 the wait 3F33 (1), 3 the
                 * fade 3088 (1). */
                XSTUB(0x0A95,0x6A8A,0,0x6B07,8,1,1);
                XSTUB(0x0A95,0x6BB1,1,0x1B62,1,1,0);
                XW8(0,0x3F33,0xE9);XW16(0,0x3F34,(uint16_t)(0xF300-0x3F33-3));
                XSTUB(0,0xF300,2,0x2C73,1,0,0);
                XW8(0,0x3088,0xE9);XW16(0,0x3089,(uint16_t)(0xF500-0x3088-3));
                XSTUB(0,0xF500,3,0x3D84,1,0,0);
#undef XSTUB
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"START.EXE") && o->seg==0 && o->ip==0x3F33) {
                const uint16_t dg=0x5000;
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                seg[S_DS]=dg;seg[S_SS]=XPICK(2)?0x6000:dg;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                /* The stack pointer: the usual place, random ones, and the low and
                 * high values where the frame wraps. With the stack in the data
                 * segment, keep it clear of the stubs' lists and cells (F400..F840)
                 * and of the pointer [E096]. */
                switch(XPICK(6)) {
                    case 0: r[R_SP]=0xF000; break;
                    case 1: r[R_SP]=(uint16_t)(0x0100+2*XPICK(0x1800)); break;
                    case 2: r[R_SP]=(uint16_t)(2*XPICK(0x10)); break;
                    case 3: r[R_SP]=(uint16_t)(0xFA00+2*XPICK(0x100)); break;
                    case 4: r[R_SP]=(uint16_t)(0xFFE0+2*XPICK(0x10)); break;
                    default: r[R_SP]=(uint16_t)(0x4000+2*XPICK(0x1000)); break;
                }
                if(seg[S_SS]==dg && r[R_SP]>0xF300 && r[R_SP]<0xF980) r[R_SP]=0xF000;
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The tick budget n (argument 0): none, one, a few, many, and a
                 * few large ones (then the lists below make the waits end). The
                 * words above it are noise; small[] matches them. */
                uint16_t n;
                int large=0;
                switch(XPICK(10)) {
                    case 0: n=0; break;
                    case 1: n=1; break;
                    case 2: n=2; break;
                    case 3: case 4: case 5: n=(uint16_t)(3+XPICK(10)); break;
                    case 6: case 7: n=(uint16_t)(13+XPICK(30)); break;
                    case 8: { static const uint16_t big[4]={0xFFFF,0x8000,0x7FFF,0x0100}; n=big[XPICK(4)]; large=1; } break;
                    default: n=(uint16_t)(1+XPICK(4)); break;
                }
                for(unsigned j=0;j<8;j++) {
                    const uint16_t w=j?(uint16_t)XRAND():n;
                    XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w);
                    if(j<4)small[j]=w;
                }
                /* The record behind [E096]: its own segment or the data segment
                 * (clear of every stack), sometimes near the top so +72h wraps. Its byte at +72h has bits 0
                 * and 1 (which devices are asked) in every combination. */
                {
                    static const uint16_t sg[4]={0x7000,0x7200,0x5000,0x8000};
                    const uint16_t pseg=sg[XPICK(4)];
                    uint16_t poff;
                    if(pseg!=dg) {
                        switch(XPICK(4)) {
                            case 0: poff=(uint16_t)(0xFF80+XPICK(0x80)); break;
                            case 1: poff=(uint16_t)XRAND(); break;
                            default: poff=(uint16_t)XPICK(0x400); break;
                        }
                    } else poff=(uint16_t)(0x8000+XPICK(0x2000));
                    XW16(dg,0xE096,poff);XW16(dg,0xE098,pseg);
                    for(unsigned j=0;j<0x80;j++)XW8(pseg,(uint16_t)(poff+j),XRAND());
                    XW8(pseg,(uint16_t)(poff+0x72),(uint8_t)((XRAND()&~3u)|XPICK(4)));
                }
                /* The callees' lists (32 words each, cycled):
                 *   k 0 8427 the tick wait (near, none): noise
                 *   k 1 0A95:6B89 a driver button (far, 1 word): held or not
                 *   k 2 8545 the mouse read (near, none): noise
                 *   k 3 8530 a mouse button (near, 1 word): held or not
                 *   k 4 0A95:6B7A key waiting? (far, none): zero means a key
                 *   k 5 0A95:6B7F the key (far, none): the answer keys and near misses
                 *   k 6 7611 the quit prompt (near, none): noise
                 * A button or "no key" answer is zero with a chance chosen per
                 * state, else a nonzero word from edge values (zero low byte...). */
                for(unsigned k=0;k<7;k++)
                    for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*k+2*j,XRAND());
                {
                    static const uint16_t nz[]={0x0001,0x0100,0x8000,0xFFFF,0x00FF,0x7FFF,0x0080,0x8001,0x0002};
                    static const unsigned pz[6]={0,1,2,3,4,2};   /* zeros in 4ths */
                    const unsigned kk[3]={1,3,4};
                    for(unsigned t=0;t<3;t++) {
                        const unsigned p=pz[XPICK(6)];
                        for(unsigned j=0;j<32;j++) {
                            uint16_t v;
                            if(XPICK(4)<p)v=0;
                            else if(XPICK(2))v=nz[XPICK(sizeof nz/sizeof nz[0])];
                            else v=(uint16_t)(1+XPICK(0xFFFF));
                            /* With a large budget, buttons are free after the first
                             * eight answers so the release wait ends. */
                            if(large&&t<2&&j>=8)v=0;
                            XW16(dg,0xF400+0x40*kk[t]+2*j,v);
                        }
                    }
                    static const uint16_t keys[]={
                        0x000D,0x001B,0x0020,0x1000,0x1C0D,0x011B,0x3920,0x100D,0xFF20,0x801B,
                        0x000C,0x000E,0x001A,0x001C,0x001F,0x0021,0x0FFF,0x1001,0x2000,0x0000,
                        0x0D00,0x1B00,0x2000,0x0F00,0x1100,0x9000};
                    for(unsigned j=0;j<32;j++) {
                        uint16_t v;
                        if(XPICK(4))v=keys[XPICK(sizeof keys/sizeof keys[0])];
                        else v=(uint16_t)XRAND();
                        XW16(dg,0xF400+0x40*5+2*j,v);
                    }
                    /* With a large budget a key that answers is waiting at least
                     * once in the cycle. */
                    if(large) {
                        const unsigned at=0xF400+0x40*4+2*XPICK(32);
                        XW16(dg,at,0);
                        for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*5+2*j,keys[XPICK(3)]);
                    }
                }
                for(unsigned j=0;j<0x40;j+=2)XW16(dg,0xF800+j,0);
                /* Returning probes: fold a number naming the callee and then every
                 * argument, in order, into one shared cell (F800), count the call
                 * (F802 + 2k), keep a list index (F820 + 2k) and return the next
                 * word of the callee's list (F400 + 40h * k). */
#define XSTUB(sg_,off_,k_,id_,nargs_,far_) do { \
    uint8_t cb_[200]; unsigned n_=0; \
    const unsigned cnt_=0xF802+2*(k_),ptr_=0xF820+2*(k_),list_=0xF400+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(id_)&255;cb_[n_++]=(id_)>>8; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
        cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; } \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x83;cb_[n_++]=0xC3;cb_[n_++]=0x02; \
    cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
                {
                    /* The three driver entries are five bytes apart (far jumps in
                     * the original), so each gets a far jump to its stub in
                     * segment 0; the near entries get a near jump. */
                    static const struct { uint16_t entry; uint8_t args; uint8_t far_; } nb_[7]={
                        {0x8427,0,0},{0x6B89,1,1},{0x8545,0,0},{0x8530,1,0},{0x6B7A,0,1},{0x6B7F,0,1},{0x7611,0,0}};
                    for(unsigned k=0;k<7;k++) {
                        const uint16_t code=(uint16_t)(0xF200+0x90*k);
                        const uint16_t id=(uint16_t)(0x0A51+0x1111*k);
                        if(nb_[k].far_) {
                            XW8(0x0A95,nb_[k].entry,0xEA);XW16(0x0A95,nb_[k].entry+1,code);XW16(0x0A95,nb_[k].entry+3,0);
                        } else {
                            XW8(0,nb_[k].entry,0xE9);XW16(0,nb_[k].entry+1,(uint16_t)(code-nb_[k].entry-3));
                        }
                        XSTUB(0,code,k,id,nb_[k].args,nb_[k].far_);
                    }
                }
#undef XSTUB
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"START.EXE") && o->seg==0 && o->ip==0x1308) {
                const uint16_t dg=0x5000;
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                seg[S_DS]=dg;seg[S_SS]=XPICK(2)?(uint16_t)0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* No arguments; the words above SP are noise, and the harness's
                 * small[] copies of them match. */
                for(unsigned j=0;j<8;j++) {
                    const uint16_t w=(uint16_t)XRAND();
                    XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w);
                    if(j<4)small[j]=w;
                }
                /* The two colours (11C4h, 11C7h) and their neighbours, the
                 * palette's last entries (67DAh..67E5h) and every entry the
                 * fourth copy can reach (67E3h - 3 * s for any signed byte s:
                 * 6664h..6965h): all noise. */
                for(unsigned j=0x11C0;j<0x11D0;j++)XW8(dg,j,XRAND());
                for(unsigned j=0x6660;j<0x6970;j++)XW8(dg,j,XRAND());
                /* s, the signed byte [B2E8]: mostly 0..3 (the three entries and
                 * the one past them), at times the extremes or anything. */
                {
                    uint8_t b;
                    switch(XPICK(8)) {
                        case 0: b=(uint8_t)XRAND(); break;
                        case 1: { static const uint8_t e[6]={0x80,0x7F,0xFF,0xFE,0x81,0x55}; b=e[XPICK(6)]; } break;
                        default: b=(uint8_t)XPICK(4); break;
                    }
                    XW8(dg,0xB2E7,XRAND());XW8(dg,0xB2E8,b);XW8(dg,0xB2E9,XRAND());
                }
                /* [D2A2]: zero (the first mission, the message path) in half the
                 * states, else non-zero in any bit; [D096] and the neighbours noise. */
                {
                    uint8_t f=0;
                    if(XPICK(2)) { static const uint8_t e[5]={0x01,0x80,0xFF,0x10,0x02}; f=XPICK(2)?e[XPICK(5)]:(uint8_t)(XRAND()|1); }
                    XW8(dg,0xD2A1,XRAND());XW8(dg,0xD2A2,f);XW8(dg,0xD2A3,XRAND());
                    XW8(dg,0xD095,XRAND());XW8(dg,0xD096,XRAND()|1);XW8(dg,0xD097,XRAND());
                }
                /* The callees: each stub folds a number naming it and then every
                 * argument, in order, into one shared cell (F800), counts its calls
                 * (F802 + 2k), keeps a list index (F820 + 2k) and returns the next
                 * word of its list (F400 + 40h * k) in AX. */
                for(unsigned k=0;k<9;k++)for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*k+2*j,XRAND());
                for(unsigned j=0;j<0x40;j+=2)XW16(dg,0xF800+j,0);
#define XSTUB(sg_,off_,k_,id_,nargs_,far_) do { \
    uint8_t cb_[200]; unsigned n_=0; \
    const unsigned cnt_=0xF802+2*(k_),ptr_=0xF820+2*(k_),list_=0xF400+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(id_)&255;cb_[n_++]=(id_)>>8; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
        cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; } \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x83;cb_[n_++]=0xC3;cb_[n_++]=0x02; \
    cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
#define XNEAR(callee_,stub_,k_,id_,nargs_) do { \
    XW8(0,(callee_),0xE9);XW16(0,(callee_)+1,(uint16_t)((stub_)-(callee_)-3)); \
    XSTUB(0,(stub_),(k_),(id_),(nargs_),0); } while(0)
                /* k: 0 the driver's rectangle copy 0A95:6A8A (8 words), 1 the fade
                 * 3070 (1), 2 the wait 405F (1, called twice), 3 the text writer
                 * 2B1B (6), 4 40F0 (0), 5 3588 (0), 6 the generator 1F3F (0),
                 * 7 the briefing page 141D (1), 8 the message line 1B37 (1). */
                XSTUB(0x0A95,0x6A8A,0,0x6B07,8,1);
                XNEAR(0x3070,0xF200,1,0x1B62,1);
                XNEAR(0x405F,0xF300,2,0x2C73,1);
                XNEAR(0x2B1B,0xF500,3,0x3D84,6);
                XNEAR(0x40F0,0xF600,4,0x4E95,0);
                XNEAR(0x3588,0xF700,5,0x5FA6,0);
                XNEAR(0x1F3F,0xF800,6,0x60B7,0);
                XNEAR(0x141D,0xF900,7,0x71C8,1);
                XNEAR(0x1B37,0xFA00,8,0x82D9,1);
#undef XNEAR
#undef XSTUB
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"START.EXE") && o->seg==0 && o->ip==0x30A0) {
                const uint16_t dg=0x5000;
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                seg[S_DS]=dg;seg[S_SS]=XPICK(2)?(uint16_t)0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                /* The long multiply 0x0978E (RET 8): the original in half the
                 * states (at most two passes then: up to 46080 instructions a
                 * pass), in the other half a 7-instruction stand-in that keeps
                 * every argument word in DX:AX (AX = v lo + level lo, DX = v hi ^
                 * level hi ^ AX; SS: operands), so three passes fit the 100000
                 * step cap. The original bytes are kept from the first state. */
                static uint8_t lmul_[23];
                static int lmul_saved_=0;
                if(!lmul_saved_) { for(unsigned j=0;j<23;j++)lmul_[j]=g_pristine[phys(0,(uint16_t)(0x978E + j))]; lmul_saved_=1; }
                const int cheap_=XPICK(2);
                {
                    static const uint8_t st[23]={0x8B,0xDC, 0x36,0x8B,0x47,0x02, 0x36,0x03,0x47,0x06,
                        0x36,0x8B,0x57,0x04, 0x36,0x33,0x57,0x08, 0x33,0xD0, 0xC2,0x08,0x00};
                    for(unsigned j=0;j<23;j++)XW8(0,0x978E + j,cheap_?st[j]:lmul_[j]);
                }
                /* The fade: from, to (levels), frames, and the step count it
                 * leads to, c = low word of (signed [B304] at 0x3100) * frames.
                 * Passes over the palette: 1 for frames 0 or from = to, c + 1
                 * for c >= 2, else 2. Classes: 0 frames 0; 1 from = to (and,
                 * with the DAC write stub disturbing the level's high word on
                 * the first pass, two passes); 2 from != to with c <= 1 (c 0
                 * gives the FFh divisor); 3 (stand-in multiply only) c = 2, the
                 * one class that adds the step. */
                static const uint16_t lv_[9]={0x0000,0x0100,0x0040,0x7FFF,0x8000,0xFFFF,0xFFF0,0x00FF,0x0001};
                uint16_t from_=XPICK(3)?lv_[XPICK(9)]:(uint16_t)XRAND();
                uint16_t to_=XPICK(3)?lv_[XPICK(9)]:(uint16_t)XRAND();
                const unsigned cls_=XPICK(cheap_?4:3);
                uint8_t beff_=0; uint16_t frames_=0;
                if(cls_==3) {
                    static const uint8_t pb[9]={0x01,0x02,0xFE,0xFF,0x02,0x7F,0x81,0x03,0xFD};
                    static const uint16_t pf[9]={0x0002,0x0001,0xFFFF,0xFFFE,0x8001,0x7EFE,0x8102,0x5556,0xAAAA};
                    const unsigned k=XPICK(9); beff_=pb[k]; frames_=pf[k];
                } else if(cls_==0) {
                    beff_=(uint8_t)(XPICK(2)?XRAND():XPICK(3)); frames_=0;
                } else {
                    switch(XPICK(4)) {
                        case 0: beff_=0; frames_=(uint16_t)(XRAND()|1); break;
                        case 1: {
                            static const uint8_t pb[8]={0x01,0xFF,0x80,0x02,0xFF,0x01,0x7F,0x80};
                            static const uint16_t pf[8]={0x0001,0xFFFF,0x0200,0x8000,0x0001,0xFFFF,0x0400,0x8000};
                            const unsigned k=XPICK(8); beff_=pb[k]; frames_=pf[k];
                        } break;
                        default:
                            do { beff_=(uint8_t)XRAND(); frames_=(uint16_t)XRAND(); }
                            while(!frames_ || (int16_t)(uint16_t)((int16_t)(int8_t)beff_*(int16_t)frames_)>1);
                            break;
                    }
                }
                const int16_t c_=(int16_t)(uint16_t)((int16_t)(int8_t)beff_*(int16_t)frames_);
                if(cls_==1) to_=from_;
                if(cls_>=2 && to_==from_) to_=(uint16_t)(from_+1+XPICK(0x200));
                if(cls_>=2 && c_==-1 && (uint16_t)(to_-from_)==0x8000) to_^=1;   /* IDIV 8000h / -1 would fault */
                /* [B304] on entry: 0 (the first pass goes to 0x031AD, whose stub
                 * adds the high byte of its first answer to it) or c's factor. */
                const uint8_t b0_=(beff_==0||XPICK(2))?0:beff_;
                const uint16_t pert_=(cls_==1&&b0_!=0&&XPICK(2))?(uint16_t)(XPICK(2)?(XRAND()|1):(XPICK(2)?0x0001:0x8000)):0;
                /* The source palette (DS): the real 64E3h, one that wraps past
                 * FFFFh, or anywhere; VGA levels 0..3Fh, any byte, or a mix. */
                uint16_t pal_;
                switch(XPICK(4)) {
                    case 0: pal_=0x64E3; break;
                    case 1: pal_=(uint16_t)(0xFE00+XPICK(0x100)); break;
                    default: pal_=(uint16_t)(0x0100+XPICK(0xE000)); break;
                }
                {
                    const unsigned style=XPICK(3);
                    for(unsigned j=0;j<0x300;j++) {
                        const uint8_t v=(uint8_t)(style==0?XPICK(64):style==1?XRAND():(XPICK(2)?XPICK(64):XRAND()));
                        XW8(dg,pal_+j,v);
                    }
                }
                /* [64E2] the DAC queue count (0x08427's stub takes one off),
                 * [ADFB] the timer hook flag, [B304] the ticks per step. */
                {
                    const uint8_t q=(uint8_t)(XPICK(2)?0:1+XPICK(3));
                    XW8(dg,0x64E1,XRAND());XW8(dg,0x64E2,q);
                    uint8_t h=0;
                    if(XPICK(2)) { static const uint8_t e[3]={0x01,0x80,0xFF}; h=XPICK(2)?e[XPICK(3)]:(uint8_t)(XRAND()|1); }
                    XW8(dg,0xADFA,XRAND());XW8(dg,0xADFB,h);XW8(dg,0xADFC,XRAND());
                    XW8(dg,0xB303,XRAND());XW8(dg,0xB304,b0_);XW8(dg,0xB305,XRAND());
                }
                /* The stack: noise below SP, the four arguments at SP, noise above. */
                for(unsigned j=0;j<0x500;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x500+j),XRAND());
                {
                    const uint16_t a[4]={pal_,from_,to_,frames_};
                    for(unsigned j=0;j<12;j++) {
                        const uint16_t w=j<4?a[j]:(uint16_t)XRAND();
                        XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w);
                        if(j<4)small[j]=w;
                    }
                }
                /* The callees: each stub folds a number naming it and then every
                 * argument, in order, into one shared cell (F800), counts its calls
                 * (F802 + 2k), keeps a list index (F820 + 2k) and returns the next
                 * word of its list (F400 + 40h * k) in AX; a tail runs before it
                 * returns. */
                for(unsigned k=0;k<6;k++)for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*k+2*j,XRAND());
                for(unsigned j=0;j<0x40;j+=2)XW16(dg,0xF800+j,0);
                {   /* k 0, the sound thunk: 0..3 busy answers (either byte), then 0. */
                    const unsigned nb=XPICK(4);
                    for(unsigned j=0;j<32;j++) {
                        static const uint16_t e[3]={0x0001,0x0100,0x8000};
                        const uint16_t v=j<nb?(XPICK(2)?e[XPICK(3)]:(uint16_t)(XRAND()|1)):0;
                        XW16(dg,0xF400+2*j,v);
                    }
                }
                /* k 3, the DAC write: 0 (no disturbance) but the first answer in
                 * disturbing states. */
                for(unsigned j=0;j<32;j++)XW16(dg,0xF4C0+2*j,j==0?pert_:0);
                /* k 4, the tick wait: its first answer's high byte is c's factor
                 * when [B304] starts at 0. */
                if(b0_==0) { const uint16_t at4=0xF500; XW8(dg,at4+1,beff_); }
#define XSTUBT(sg_,off_,k_,id_,nargs_,far_,tail_,ntail_) do { \
    uint8_t cb_[200]; unsigned n_=0; \
    const unsigned cnt_=0xF802+2*(k_),ptr_=0xF820+2*(k_),list_=0xF400+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(id_)&255;cb_[n_++]=(id_)>>8; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
        cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; } \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x83;cb_[n_++]=0xC3;cb_[n_++]=0x02; \
    cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    for(unsigned t_=0;t_<(unsigned)(ntail_);t_++)cb_[n_++]=(tail_)[t_]; \
    cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
#define XNEAR(callee_,stub_,k_,id_,nargs_,tail_,ntail_) do { \
    XW8(0,(callee_),0xE9);XW16(0,(callee_)+1,(uint16_t)((stub_)-(callee_)-3)); \
    XSTUBT(0,(stub_),(k_),(id_),(nargs_),0,(tail_),(ntail_)); } while(0)
                {
                    /* 0x08364: MOV BX,[BP+4] / XOR SS:[BX-6],AX (the level's high
                     * word at BP-306h when called with BP-300h). 0x031AD: ADD
                     * [B304],AH (the ticks it counts). */
                    static const uint8_t dac_tail[7]={0x8B,0x5E,0x04, 0x36,0x31,0x47,0xFA};
                    static const uint8_t tick_tail[4]={0x00,0x26,0x04,0xB3};
                    static const uint8_t none_[1]={0x90};
                    /* k: 0 the driver's sound thunk 0A95:6BB1 (1 word), 1 the DAC
                     * queue 8427 (0; DEC BYTE [64E2] first, then JMP to its
                     * stub), 2 the timer restore 8CE6 (0), 3 the DAC write 8364
                     * (1), 4 the tick wait 31AD (1), 5 the timer hook 8CA8 (0). */
                    XSTUBT(0x0A95,0x6BB1,0,0x6B07,1,1,none_,0);
                    XW8(0,0x8427,0xE9);XW16(0,0x8428,(uint16_t)(0xF300-0x8427-3));
                    XW8(0,0xF300,0xFE);XW8(0,0xF301,0x0E);XW8(0,0xF302,0xE2);XW8(0,0xF303,0x64);
                    XW8(0,0xF304,0xE9);XW16(0,0xF305,(uint16_t)(0xF320-0xF307));
                    XSTUBT(0,0xF320,1,0x1B62,0,0,none_,0);
                    XNEAR(0x8CE6,0xF400,2,0x2C73,0,none_,0);
                    XNEAR(0x8364,0xF500,3,0x3D84,1,dac_tail,7);
                    XNEAR(0x31AD,0xF600,4,0x4E95,1,tick_tail,4);
                    XNEAR(0x8CA8,0xF700,5,0x5FA6,0,none_,0);
                }
                (void)c_;
#undef XNEAR
#undef XSTUBT
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0x0B05) {
                const uint16_t dg=0x5000;
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define RND(n_) ((unsigned)(XRAND()%(unsigned)(n_)))
#define PCT(p_) (RND(100)<(unsigned)(p_))
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                seg[S_DS]=dg;seg[S_SS]=(XRAND()&1)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                /* The stack below SP starts random in both segments: the frame is
                 * read through SS, while fread stores the byte word through DS. */
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                for(unsigned j=0;j<0x200;j++)XW8(dg,(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The argument: the name pointer, only folded by the probes. */
                for(unsigned j=0;j<4;j++){small[j]=(uint16_t)XRAND();XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),small[j]);}
                /* Tables the routine reads and fills start random, so a store aimed
                 * wrongly or skipped shows. Ring tile counts for rings 1..4 (ring 0's
                 * comes from the read), tile entry counts for tiles 1.. of each ring
                 * (tile 0's comes from the read). */
                for(unsigned j=0;j<0x200;j++)XW8(dg,0x0600+j,XRAND());
                for(unsigned j=0;j<0x600;j++)XW8(dg,0xA090+j,XRAND());
                for(unsigned j=0;j<0x180;j++)XW8(dg,0xE320+j,XRAND());
                XW16(dg,0x991C,XRAND());
                for(unsigned r_=1;r_<5;r_++)XW16(dg,0x0614+2*r_,PCT(15)?0:(PCT(10)?(uint16_t)(9+RND(4)):(uint16_t)(1+RND(5))));
                for(unsigned k=0;k<5*32;k++)XW16(dg,0x061E + 2*k,PCT(25)?0:(uint16_t)(1+RND(4)));
                /* Answer lists: the stream (fopen), the words fread stores (the
                 * header, ring 0's tile count, each ring's tile 0 entry count, then
                 * entry words), fclose's result. */
                for(unsigned j=0;j<32;j++){XW16(dg,0xF840+2*j,XRAND());XW16(dg,0xF8C0+2*j,XRAND());}
                for(unsigned j=0;j<32;j++)XW16(dg,0xF880+2*j,PCT(30)?(uint16_t)RND(8):(uint16_t)XRAND());
                XW16(dg,0xF882,PCT(12)?0:(uint16_t)(1+RND(7)));
                for(unsigned j=2;j<7;j++)XW16(dg,0xF880+2*j,PCT(20)?0:(uint16_t)(1+RND(5)));
                if(PCT(10))XW16(dg,0xF840,0);
                for(unsigned j=0;j<16;j++)XW16(dg,0xF800+2*j,0);
                /* Probes: fold every argument in order into a guest cell and count
                 * the call; fopen/fread/fclose answer the next word of a list, and
                 * fread also stores that word at its buffer (DS, as the runtime does). */
#define XSTUB(off_,cell_,nargs_,list_,store_) do { unsigned out_=(off_);const uint16_t sgx_=0; \
    XW8(sgx_,out_++,0x55);XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0xEC);XW8(sgx_,out_++,0x53); \
    XW8(sgx_,out_++,0xFF);XW8(sgx_,out_++,0x06);XW8(sgx_,out_++,(cell_)+2);XW8(sgx_,out_++,((cell_)+2)>>8); \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x46);XW8(sgx_,out_++,4+2*j_); \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); \
        XW8(sgx_,out_++,0xD1);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x33);XW8(sgx_,out_++,0xD8); \
        XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); } \
    if(list_) { \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+4);XW8(sgx_,out_++,((cell_)+4)>>8); \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x87);XW8(sgx_,out_++,(list_));XW8(sgx_,out_++,(list_)>>8); \
        XW8(sgx_,out_++,0x83);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x02); \
        XW8(sgx_,out_++,0x83);XW8(sgx_,out_++,0xE3);XW8(sgx_,out_++,0x3E); \
        XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+4);XW8(sgx_,out_++,((cell_)+4)>>8); } \
    if(store_) { XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x5E);XW8(sgx_,out_++,0x04);XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x07); } \
    XW8(sgx_,out_++,0x5B);XW8(sgx_,out_++,0x5D);XW8(sgx_,out_++,0xC3); } while(0)
                {
                    static const struct { uint16_t slot,code,cell; unsigned args; uint16_t list; int store; } near_[4]={
                        {0x0CF3,0xF200,0xF800,2,0,0},{0xEA1C,0xF260,0xF808,2,0xF840,0},
                        {0xEA30,0xF2C0,0xF810,4,0xF880,1},{0xE946,0xF380,0xF818,1,0xF8C0,0}};
                    for(unsigned k=0;k<4;k++) {
                        XW8(0,near_[k].slot,0xE9);XW16(0,near_[k].slot+1,(uint16_t)(near_[k].code-near_[k].slot-3));
                        XSTUB(near_[k].code,near_[k].cell,near_[k].args,near_[k].list,near_[k].store);
                    }
                }
#undef XSTUB
#undef PCT
#undef RND
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0x120A && o->ip==0x0CE9) {
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(s&4)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(uint16_t)((o_)+1),v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define RND(n_) ((unsigned)(XRAND()%(unsigned)(n_)))
#define PCT(p_) (RND(100)<(unsigned)(p_))
#define PICK(a_) ((a_)[RND(sizeof(a_)/sizeof((a_)[0]))])
                /* The stack below SP starts random in both segments. */
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                for(unsigned j=0;j<0x200;j++)XW8(dg,(uint16_t)(r[R_SP]-0x200+j),XRAND());
                for(unsigned j=0;j<4;j++)small[j]=(uint16_t)XRAND();
                /* AH survives into the condition's AND and the points' pushed colour. */
                r[R_AX]=(uint16_t)XRAND();r[R_BP]=(uint16_t)XRAND();
                /* The condition table [7D4E] and the colour table [7D80]. */
                const uint16_t condtab=(uint16_t)(0x3000+2*RND(16)),coltab=(uint16_t)(0x3400+2*RND(16));
                XW16(dg,0x7D4E,condtab);XW16(dg,0x7D80,coltab);
                for(unsigned j=0;j<0x140;j++)XW8(dg,0x3000+j,XRAND());
                for(unsigned j=0;j<0x240;j++)XW8(dg,0x3400+j,XRAND());
                /* Outputs start random, so a write the body skipped shows. */
                XW16(dg,0x7D6E,XRAND());XW16(dg,0x7D70,XRAND());XW16(dg,0x7D7E,XRAND());
                /* Edge records (24 bytes from 5EEAh): only the flag word +2 is read. */
                for(unsigned j=0;j<256;j++)XW16(dg,0x5EEA+24*j+2,XRAND());
                /* Vertices at D6B4h, 8 bytes each: x (low, high), y (low, high), the
                 * high words around the 0/FFFFh window and 8000h (behind the eye). */
                {
                    static const uint16_t hi_[]={0x8000,0x0000,0xFFFF,0x0001,0xFFFE,0x0000,0xFFFF,0x7FFF};
                    static const uint16_t lo_[]={0x0000,0xFFFF,0x8000,0x7FFF,0x0001,0xFFFE};
                    for(unsigned j=0;j<256;j++) {
                        const uint16_t at=(uint16_t)(0xD6B4+8*j);
                        XW16(dg,at,PCT(25)?PICK(lo_):(uint16_t)XRAND());
                        XW16(dg,at+2,PCT(85)?PICK(hi_):(uint16_t)XRAND());
                        XW16(dg,at+4,PCT(25)?PICK(lo_):(uint16_t)XRAND());
                        XW16(dg,at+6,PCT(85)?(PCT(15)?0x8000:PICK(hi_)):(uint16_t)XRAND());
                    }
                    XW16(dg,0x8602,PCT(30)?PICK(lo_):(uint16_t)XRAND());
                    XW16(dg,0x8604,PCT(30)?PICK(lo_):(uint16_t)XRAND());
                }
                /* The command stream at ES:SI (ES never DS; one state in ten wraps
                 * past FFFFh). Unread bytes stay random. */
                const uint16_t es_=(s&8)?0x7400:0x7000;
                seg[S_ES]=es_;
                uint16_t p=PCT(10)?(uint16_t)(0xFF00+RND(0xF0)):(uint16_t)(0x1000+RND(0x800));
                r[R_SI]=p;
                for(unsigned j=0;j<0x400;j++)XW8(es_,(uint16_t)(p-0x10+j),XRAND());
#define XS(v_) do { XW8(es_,p,(v_)); p=(uint16_t)(p+1); } while(0)
                {
                    uint8_t used_[128];
                    for(unsigned j=0;j<128;j++)used_[j]=0;
                    static const uint8_t cmpb_[]={0x00,0x01,0x7F,0x80,0x81,0xFF,0xFE,0x40};
                    const unsigned ncmd=1+RND(7);
                    int ended=0;
                    for(unsigned k=0;k<ncmd && !ended;k++) {
                        const unsigned t=RND(100);
                        if(t<30) {                       /* condition 80h+k */
                            unsigned idx=RND(128);
                            while(used_[idx])idx=(idx+1)&127;
                            used_[idx]=1;
                            const uint8_t b=PCT(60)?PICK(cmpb_):(uint8_t)XRAND();
                            const uint16_t sx=(uint16_t)(int16_t)(int8_t)b;
                            XS(0x80|idx);XS(b);
                            if(PCT(50)) XW16(dg,condtab+2*idx,sx);
                            else {
                                const unsigned w=RND(4);
                                const uint16_t v=(w==0)?(uint16_t)b:(w==1)?(uint16_t)(sx+1):(w==2)?(uint16_t)(sx-1):(uint16_t)XRAND();
                                XW16(dg,condtab+2*idx,(v==sx)?(uint16_t)(sx^0x0100):v);
                                if(PCT(15)) { XS(0xFF); ended=1; }
                                else {
                                    uint8_t sk=(uint8_t)XRAND();
                                    if(sk==0xFF)sk=0xDF;
                                    XS(sk);
                                    p=(uint16_t)(p+(sk&0x1F)+1);      /* passed over: stays random */
                                }
                            }
                        } else if(t<55) {                /* polygon from n edges */
                            const unsigned n=PCT(8)?0:1+RND(31);
                            XS(n);
                            for(unsigned j=0;j<n;j++)XS(PCT(10)?0xFF:(uint8_t)XRAND());
                            XS((n==0||PCT(25))?0xFF:(uint8_t)RND(0xFF));
                        } else if(t<75) {                /* lines 40h+n */
                            const unsigned n=1+RND(31);
                            XS(0x40|n);XS((uint8_t)XRAND());
                            for(unsigned j=0;j<n;j++)XS(PCT(10)?0xFF:(uint8_t)XRAND());
                        } else {                         /* points 20h+n (bit 6 ignored) */
                            const unsigned n=1+RND(31);
                            XS(0x20|n|(PCT(50)?0x40:0));XS((uint8_t)XRAND());
                            for(unsigned j=0;j<n;j++)XS(PCT(10)?0xFF:(uint8_t)XRAND());
                        }
                    }
                    if(!ended)XS(0xFF);
                }
#undef XS
                /* Far callees, all stubs: each folds its inputs in order into a
                 * guest cell (AX first, then CX, SI, ES, the argument or the cells
                 * named), counts the call, returns the next word of a list in AX and
                 * DX and writes it over the registers the original saves round the
                 * call. 1377:004C and 0055 lie 9 bytes apart: each jumps to its
                 * stub further on in 1377. */
                for(unsigned j=0;j<16;j++)XW16(dg,0xF800+2*j,0);
                for(unsigned j=0;j<4*32;j++)XW16(dg,0xF400+2*j,XRAND());
                {
                    /* items: 1 AX, 2 CX, 3 SI, 4 ES, 5 a cell, 6 the stack argument.
                     * clobbers: 1 CX, 2 SI, 4 ES. */
                    static const struct { uint16_t sg,at,code,cell,list; uint8_t clob; uint8_t it[8]; uint16_t mem[2]; } fs_[4]={
                        {0x130D,0x004A,0x004A,0xF800,0xF400,1|2,  {1,2,3,4,5,5,0},{0x7D6E,0x7D70}},
                        {0x130D,0x0116,0x0116,0xF804,0xF440,1|2|4,{1,6,2,3,4,5,5,0},{0x7D6E,0x7D70}},
                        {0x1377,0x004C,0x0E00,0xF808,0xF480,4,    {1,2,3,4,0},{0,0}},
                        {0x1377,0x0055,0x0E80,0xF80C,0xF4C0,1|2|4,{1,2,3,4,5,0},{0x7D7E,0}}};
                    for(unsigned k=0;k<4;k++) {
                        const uint16_t sg=fs_[k].sg,cell=fs_[k].cell,list=fs_[k].list;
                        uint16_t q=fs_[k].code;
                        unsigned mi=0;
#define XE(b_) do { XW8(sg,q,(b_)); q=(uint16_t)(q+1); } while(0)
                        if(fs_[k].code!=fs_[k].at) { XW8(sg,fs_[k].at,0xE9);XW16(sg,fs_[k].at+1,(uint16_t)(fs_[k].code-fs_[k].at-3)); }
                        XE(0x55);XE(0x8B);XE(0xEC);
                        XE(0xFF);XE(0x06);XE(cell&0xFF);XE(cell>>8);
                        for(unsigned i=0;fs_[k].it[i];i++) {
                            switch(fs_[k].it[i]) {
                            case 2: XE(0x8B);XE(0xC1); break;
                            case 3: XE(0x8B);XE(0xC6); break;
                            case 4: XE(0x8C);XE(0xC0); break;
                            case 5: XE(0xA1);XE(fs_[k].mem[mi]&0xFF);XE(fs_[k].mem[mi]>>8);mi++; break;
                            case 6: XE(0x8B);XE(0x46);XE(0x06); break;
                            default: break;
                            }
                            XE(0x8B);XE(0x1E);XE(cell&0xFF);XE(cell>>8);
                            XE(0xD1);XE(0xC3);XE(0x33);XE(0xD8);
                            XE(0x89);XE(0x1E);XE(cell&0xFF);XE(cell>>8);
                        }
                        XE(0x8B);XE(0x1E);XE((cell+2)&0xFF);XE((cell+2)>>8);
                        XE(0x8B);XE(0x87);XE(list&0xFF);XE(list>>8);
                        XE(0x83);XE(0xC3);XE(0x02);XE(0x83);XE(0xE3);XE(0x3E);
                        XE(0x89);XE(0x1E);XE((cell+2)&0xFF);XE((cell+2)>>8);
                        XE(0x8B);XE(0xD0);                               /* mov dx, ax */
                        if(fs_[k].clob&1) { XE(0x8B);XE(0xC8); }         /* mov cx, ax */
                        if(fs_[k].clob&2) { XE(0x8B);XE(0xF0); }         /* mov si, ax */
                        if(fs_[k].clob&4) { XE(0x8E);XE(0xC0); }         /* mov es, ax */
                        XE(0x5D);XE(0xCB);
#undef XE
                    }
                }
#undef PICK
#undef PCT
#undef RND
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0x8462) {
                const uint16_t dg=0x5000;
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define RND(n_) ((unsigned)(XRAND()%(unsigned)(n_)))
#define PCT(p_) (RND(100)<(unsigned)(p_))
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                seg[S_DS]=dg;seg[S_SS]=(XRAND()&1)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The arguments: the aircraft's map x and y. */
                for(unsigned j=0;j<4;j++){small[j]=(uint16_t)XRAND();XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),small[j]);}
                /* The map switch, the cells the routine clears, the zoom (byte and
                 * word reads), the heading, the grid's object and the result cells. */
                XW16(dg,0x368C,PCT(12)?0:(PCT(30)?1:(uint16_t)XRAND()));
                XW16(dg,0xDF06,XRAND());XW16(dg,0xC070,XRAND());XW16(dg,0xC06E,XRAND());
                XW16(dg,0x40B2,XRAND());XW16(dg,0x40B4,XRAND());
                {
                    const uint16_t zb=PCT(80)?(uint16_t)RND(10):(uint16_t)XRAND();
                    XW8(dg,0x40AE,zb);XW8(dg,0x40AF,PCT(60)?0:(PCT(50)?0xFF:XRAND()));
                }
                XW16(dg,0x2DEE,XRAND());XW16(dg,0x4058,XRAND());
                /* The object count (signed) and the 16-byte records from B2D0h:
                 * x, y, live, flags. */
                {
                    uint16_t n_;
                    if(PCT(8))n_=(uint16_t)(0x8000u|XRAND());
                    else if(PCT(8))n_=(uint16_t)RND(2);
                    else n_=(uint16_t)(2+RND(22));
                    XW16(dg,0xC0E0,n_);
                }
                for(unsigned j=0;j<0x1C0;j++)XW8(dg,0xB2C0+j,XRAND());
                for(unsigned i=0;i<26;i++) {
                    const uint16_t rec=(uint16_t)(0xB2D0+16*i);
                    if(PCT(25))XW16(dg,rec+4,0);
                    if(PCT(60)) {
                        const uint16_t f_=(uint16_t)((XRAND()&~0x84u)|0x04u);
                        XW16(dg,rec+6,f_);
                    }
                }
                /* Probes: fold every argument in order into a guest cell, count the
                 * call, answer the next word of the probe's own list. */
                for(unsigned j=0;j<0x40;j++)XW16(dg,0xF800+2*j,0);
                for(unsigned j=0;j<7*32;j++)XW16(dg,0xF900+2*j,PCT(25)?(uint16_t)(RND(16)-8):(uint16_t)XRAND());
#define XSTUB(sg_,off_,cell_,nargs_,list_,argbase_,retop_) do { unsigned out_=(off_);const uint16_t sgx_=(sg_); \
    XW8(sgx_,out_++,0x55);XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0xEC);XW8(sgx_,out_++,0x53); \
    XW8(sgx_,out_++,0xFF);XW8(sgx_,out_++,0x06);XW8(sgx_,out_++,(cell_)+2);XW8(sgx_,out_++,((cell_)+2)>>8); \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x46);XW8(sgx_,out_++,(argbase_)+2*j_); \
        XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); \
        XW8(sgx_,out_++,0xD1);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x33);XW8(sgx_,out_++,0xD8); \
        XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_));XW8(sgx_,out_++,(cell_)>>8); } \
    XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+4);XW8(sgx_,out_++,((cell_)+4)>>8); \
    XW8(sgx_,out_++,0x8B);XW8(sgx_,out_++,0x87);XW8(sgx_,out_++,(list_));XW8(sgx_,out_++,(list_)>>8); \
    XW8(sgx_,out_++,0x83);XW8(sgx_,out_++,0xC3);XW8(sgx_,out_++,0x02); \
    XW8(sgx_,out_++,0x83);XW8(sgx_,out_++,0xE3);XW8(sgx_,out_++,0x3E); \
    XW8(sgx_,out_++,0x89);XW8(sgx_,out_++,0x1E);XW8(sgx_,out_++,(cell_)+4);XW8(sgx_,out_++,((cell_)+4)>>8); \
    XW8(sgx_,out_++,0x5B);XW8(sgx_,out_++,0x5D);XW8(sgx_,out_++,(retop_)); } while(0)
                {
                    /* Near callees: C818 r*sin, C831 r*cos, C67A clamp, 11A8 grid,
                     * 8625 marker, 94AD copy to screen. */
                    static const struct { uint16_t slot,code,cell; unsigned args; uint16_t list; } near_[6]={
                        {0xC818,0xF200,0xF800,2,0xF900},{0xC831,0xF280,0xF808,2,0xF940},
                        {0xC67A,0xF300,0xF810,3,0xF980},{0x11A8,0xF380,0xF818,4,0xF9C0},
                        {0x8625,0xF400,0xF820,4,0xFA00},{0x94AD,0xF480,0xF828,0,0xFA40}};
                    for(unsigned k=0;k<6;k++) {
                        XW8(0,near_[k].slot,0xE9);XW16(0,near_[k].slot+1,(uint16_t)(near_[k].code-near_[k].slot-3));
                        XSTUB(0,near_[k].code,near_[k].cell,near_[k].args,near_[k].list,4,0xC3);
                    }
                    /* Far 114A:0280, the window clear: written in place, RETF. */
                    XSTUB(0x114A,0x0280,0xF830,2,0xFA80,6,0xCB);
                }
#undef XSTUB
#undef PCT
#undef RND
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0x03CE) {
                const uint16_t dg=0x5000;
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define RND(n_) ((unsigned)(XRAND()%(unsigned)(n_)))
#define PCT(p_) (RND(100)<(unsigned)(p_))
#define PICK(a_) ((a_)[RND(sizeof(a_)/sizeof((a_)[0]))])
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                seg[S_DS]=dg;seg[S_SS]=(XRAND()&1)?0x6000:dg;r[R_SP]=0xF000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                /* The stack below SP starts random in both segments. */
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                for(unsigned j=0;j<0x200;j++)XW8(dg,(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The viewport block, and the area around [0344], start random. */
                for(unsigned j=0;j<0x400;j++)XW8(dg,(uint16_t)(0x2000+j),XRAND());
                for(unsigned j=0;j<0x60;j++)XW8(dg,(uint16_t)(0x0300+j),XRAND());
                for(unsigned j=0;j<0x110;j++)XW8(dg,(uint16_t)(j),XRAND());
                /* The thirteen argument words: a1 a2 a3, x32 y32 z32 (low, high), x y w h. */
                uint16_t arg[13];
                { static const uint16_t e_[]={0,1,2,0x7FFF,0x8000,0x8001,0xFFFF,0x0140,0x005B,0x006B,0x009D};
                  for(unsigned j=0;j<13;j++)arg[j]=PCT(30)?PICK(e_):(uint16_t)XRAND(); }
                /* The block: usually away from everything; sometimes placed so that
                 * one of its stores lands on [0344] itself (the block address is
                 * re-read before every store), with the stored value a safe block. */
                uint16_t blk=(uint16_t)(0x2000+RND(0x200));
                { const unsigned mode=RND(10);
                  const uint16_t safe=(uint16_t)(0x2200+RND(0x100));
                  if(mode==0){blk=0x0344-0x0E;arg[10]=safe;}
                  else if(mode==1){blk=0x0344-0x10;arg[12]=(uint16_t)(safe-arg[10]+1);}
                  else if(mode==2){blk=0x0344-0x12;arg[9]=safe;}
                  else if(mode==3){blk=0x0344-0x14;arg[11]=(uint16_t)(safe-arg[9]+1);}
                  else if(mode==4){blk=0x0344;}
                  else if(mode==5){blk=0x0344-4;} }
                XW16(dg,0x0344,blk);
                for(unsigned j=0;j<13;j++)XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),arg[j]);
                for(unsigned j=0;j<4;j++)small[j]=arg[j];
                /* The words and bytes the routine tests, with their boundaries. */
                { static const uint16_t v_[]={3,3,3,2,4,0,0x0103,0xFF03,0x8003,0xFFFF};
                  XW16(dg,0x990E,PCT(85)?PICK(v_):(uint16_t)XRAND()); }
                { static const uint8_t v_[]={0x13,0x14,0x15,0,0xFF,0x80,0x7F,0x01,0x93,0x94};
                  XW8(dg,0x11E2,PCT(80)?PICK(v_):(uint8_t)XRAND()); }
                { static const uint16_t v_[]={0,0,0,1,0x8000,0x0100,0xFFFF,0x00FF};
                  XW16(dg,0x368C,PCT(80)?PICK(v_):(uint16_t)XRAND()); }
                { static const uint8_t v_[]={0,0,0,1,0x80,0xFF};
                  XW8(dg,0x294B,PCT(80)?PICK(v_):(uint8_t)XRAND()); }
                XW8(dg,0x294C,XRAND());XW8(dg,0x11E3,XRAND());
                /* Outputs and pass-through cells start random. */
                XW16(dg,0x43E8,XRAND());XW16(dg,0x7D80,XRAND());
                XW16(dg,0xC05C,XRAND());XW16(dg,0xC070,XRAND());XW16(dg,0xC06E,XRAND());XW16(dg,0x98A4,XRAND());
                XW16(dg,0xC6B2,XRAND());XW8(dg,0x0347,XRAND());
                /* Probe cells: fold, count, list index for each stub; answer lists. */
                for(unsigned j=0;j<0x40;j++)XW8(dg,0xF800+j,0);
                for(unsigned j=0;j<0x200;j++)XW8(dg,0xF900+j,XRAND());
                /* Probes: fold FLAGS at entry, count the call, fold every argument in
                 * order and a few witness words (stores the routine must have made
                 * before the call, or not yet), answer the next word of a list in AX;
                 * 9EFA also stores AL over the byte [C6B2] the routine keeps. */
#define XFOLD(sg_,out_,cell_) do { \
    XW8(sg_,out_++,0x8B);XW8(sg_,out_++,0x1E);XW8(sg_,out_++,(cell_));XW8(sg_,out_++,(cell_)>>8); \
    XW8(sg_,out_++,0xD1);XW8(sg_,out_++,0xC3);XW8(sg_,out_++,0x33);XW8(sg_,out_++,0xD8); \
    XW8(sg_,out_++,0x89);XW8(sg_,out_++,0x1E);XW8(sg_,out_++,(cell_));XW8(sg_,out_++,(cell_)>>8); } while(0)
                {
                    struct stub_t { uint16_t sg,slot,code,cell,list; unsigned args; int far_; uint16_t put; uint16_t wit[6]; unsigned nwit; };
                    const struct stub_t stub_[7]={
                        {0x1E42,0x01FB,0x0700,0xF800,0xF900,0,1,0,{(uint16_t)(blk+0x0E),(uint16_t)(blk+0x10),(uint16_t)(blk+0x12),(uint16_t)(blk+0x14)},4},
                        {0,0x04C7,0xF200,0xF808,0xF940,1,0,0,{blk,(uint16_t)(blk+4),0x7D80},3},
                        {0,0xD826,0xF300,0xF810,0xF980,9,0,0,{(uint16_t)(blk+4),0x7D80,0xC05C,0xC070,0xC06E,0x98A4},6},
                        {0,0x04E6,0xF480,0xF818,0xF9C0,8,0,0,{0x0346,0x7D80,0xC6B2},3},
                        {0,0x983E,0xF600,0xF820,0xFA00,0,0,0,{0x7D80,0x0346},2},
                        {0,0xD87C,0xF680,0xF828,0xFA40,0,0,0,{0x7D80,0xC6B2},2},
                        {0,0x9EFA,0xF700,0xF830,0xFA80,0,0,0xC6B2,{0x7D80,0xC6B2,0x0346},3}};
                    for(unsigned k=0;k<7;k++) {
                        const struct stub_t *t_=&stub_[k];const uint16_t sg_=t_->sg;unsigned out_=t_->code;
                        XW8(sg_,t_->slot,0xE9);XW16(sg_,t_->slot+1,(uint16_t)(t_->code-t_->slot-3));
                        XW8(sg_,out_++,0x55);XW8(sg_,out_++,0x8B);XW8(sg_,out_++,0xEC);XW8(sg_,out_++,0x53);
                        XW8(sg_,out_++,0x9C);XW8(sg_,out_++,0x58);XFOLD(sg_,out_,t_->cell);
                        XW8(sg_,out_++,0xFF);XW8(sg_,out_++,0x06);XW8(sg_,out_++,t_->cell+2);XW8(sg_,out_++,(t_->cell+2)>>8);
                        for(unsigned j=0;j<t_->args;j++) {
                            XW8(sg_,out_++,0x8B);XW8(sg_,out_++,0x46);XW8(sg_,out_++,(t_->far_?6:4)+2*j);XFOLD(sg_,out_,t_->cell); }
                        for(unsigned j=0;j<t_->nwit;j++) {
                            XW8(sg_,out_++,0xA1);XW8(sg_,out_++,t_->wit[j]);XW8(sg_,out_++,t_->wit[j]>>8);XFOLD(sg_,out_,t_->cell); }
                        XW8(sg_,out_++,0x8B);XW8(sg_,out_++,0x1E);XW8(sg_,out_++,t_->cell+4);XW8(sg_,out_++,(t_->cell+4)>>8);
                        XW8(sg_,out_++,0x8B);XW8(sg_,out_++,0x87);XW8(sg_,out_++,t_->list);XW8(sg_,out_++,t_->list>>8);
                        XW8(sg_,out_++,0x83);XW8(sg_,out_++,0xC3);XW8(sg_,out_++,0x02);
                        XW8(sg_,out_++,0x83);XW8(sg_,out_++,0xE3);XW8(sg_,out_++,0x3E);
                        XW8(sg_,out_++,0x89);XW8(sg_,out_++,0x1E);XW8(sg_,out_++,t_->cell+4);XW8(sg_,out_++,(t_->cell+4)>>8);
                        if(t_->put){XW8(sg_,out_++,0xA2);XW8(sg_,out_++,t_->put);XW8(sg_,out_++,t_->put>>8);}
                        XW8(sg_,out_++,0x5B);XW8(sg_,out_++,0x5D);XW8(sg_,out_++,t_->far_?0xCB:0xC3);
                    }
                }
#undef XFOLD
#undef PICK
#undef PCT
#undef RND
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"END.EXE") && o->seg==0 && o->ip==0x02A4) {
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                /* END's own data segment 05FA (where the driver thunks 05FA:24CC
                 * and 05FA:2558 also lie) or a separate one; the stack in the data
                 * segment or in its own. */
                const uint16_t dg=XPICK(2)?0x05FA:0x5000;
                seg[S_DS]=dg;seg[S_SS]=XPICK(2)?0x6000:dg;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                /* The stack pointer: the usual place, random ones, and the edges
                 * where the pushes wrap below zero. With the stack in the data
                 * segment it stays clear of the cells at 55DE, 5B9E and 5DB6, the
                 * thunks, the stubs (CS F200-FD00 is DS 9260-9D60 when DS is 05FA)
                 * and the stubs' cells (F400-F840). */
                switch(XPICK(5)) {
                    case 0: r[R_SP]=0xF000; break;
                    case 1: r[R_SP]=(uint16_t)(0x0300+2*XPICK(0x0800)); break;
                    case 2: r[R_SP]=(uint16_t)(2*XPICK(0x10)); break;
                    case 3: r[R_SP]=(uint16_t)(0xFFE0+2*XPICK(0x10)); break;
                    default: r[R_SP]=(uint16_t)(0x3000+2*XPICK(0x0800)); break;
                }
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x200+j),XRAND());
                /* The routine takes no arguments: the words above SP are noise;
                 * small[] matches them. */
                for(unsigned j=0;j<8;j++) {
                    const uint16_t w=(uint16_t)XRAND();
                    XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w);
                    if(j<4)small[j]=w;
                }
                /* The theatre word at +38h of the record behind [55DE]: the eight
                 * real theatres most of the time, the edges of the doubling (7FFFh,
                 * 8000h, FFFFh: SHL carries and the table index wraps) and noise.
                 * The record's offset sometimes sits at the top of its segment so
                 * that +38h wraps. */
                uint16_t mseg,moff;
                {
                    static const uint16_t sg[4]={0x7000,0x7200,0x6000,0};
                    mseg=sg[XPICK(4)];if(!mseg)mseg=dg;
                    if(mseg==0x7000||mseg==0x7200)
                        moff=XPICK(3)==0?(uint16_t)(0xFFC0+XPICK(0x40)):(uint16_t)XRAND();
                    else moff=(uint16_t)(0x4400+XPICK(0x0800));
                }
                uint16_t theatre;
                {
                    static const uint16_t edge[]={0x7FFF,0x8000,0xFFFF,0x8007,0x4000,8,0xFFF8};
                    switch(XPICK(6)) {
                        case 0: case 1: case 2: theatre=(uint16_t)XPICK(8); break;
                        case 3: theatre=edge[XPICK(sizeof edge/sizeof edge[0])]; break;
                        default: theatre=(uint16_t)XRAND(); break;
                    }
                }
                for(unsigned j=0;j<0x40;j+=2)XW16(mseg,(uint16_t)(moff+j),XRAND());
                XW16(mseg,(uint16_t)(moff+0x38),theatre);
                /* The name for that theatre (table at CEh), a varied word. */
                XW16(dg,(uint16_t)(2*theatre+0x00CE),XRAND());
                XW16(dg,0x55DE,moff);XW16(dg,0x55E0,mseg);
                /* The flag byte and the sprite segment start as noise; every stub
                 * folds the flag in and leaves its fold in both, so each of the
                 * routine's two clears and two reads of [5DB6] is seen. */
                XW8(dg,0x5B9E,XRAND());XW16(dg,0x5DB6,XRAND());
                /* The stubs' lists: noise (no return value steers the routine). */
                for(unsigned k=0;k<11;k++)
                    for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*k+2*j,XRAND());
                for(unsigned j=0;j<0x40;j+=2)XW16(dg,0xF800+j,0);
                /* Returning probes: fold a number naming the callee, the caller's
                 * FLAGS (PUSHF at entry: the flags of the routine's last ADD SP or
                 * SHL before a call are seen), the flag byte [5B9E] and then every
                 * argument, in order, into one shared cell
                 * (F800), store the fold in [5B9E] (low byte) and [5DB6], count the
                 * call (F802 + 2k), keep a list index (F820 + 2k) and return the
                 * next word of the callee's list (F400 + 40h * k). */
#define XSTUB(sg_,off_,k_,id_,nargs_,far_) do { \
    uint8_t cb_[220]; unsigned n_=0; \
    const unsigned cnt_=0xF802+2*(k_),ptr_=0xF820+2*(k_),list_=0xF400+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x53; \
    cb_[n_++]=0x9C;cb_[n_++]=0x58; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(id_)&255;cb_[n_++]=(id_)>>8; \
    cb_[n_++]=0x33;cb_[n_++]=0xD8; \
    cb_[n_++]=0xA0;cb_[n_++]=0x9E;cb_[n_++]=0x5B; \
    cb_[n_++]=0x32;cb_[n_++]=0xD8; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
    for(unsigned j_=0;j_<(nargs_);j_++) { \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
        cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xF8; } \
    cb_[n_++]=0x88;cb_[n_++]=0x1E;cb_[n_++]=0x9E;cb_[n_++]=0x5B; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0xB6;cb_[n_++]=0x5D; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x83;cb_[n_++]=0xC3;cb_[n_++]=0x02; \
    cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=ptr_&255;cb_[n_++]=ptr_>>8; \
    cb_[n_++]=0x5B;cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
                {
                    /* k: 0 15EF (1), 1 100D (2), 2 0FDA (2), 3 15D7 (1), 4 11A2 (1),
                     * 5 05CA (none), 6 04D2 (1), 7 1992 (4), 8 2049 (2); far, behind
                     * a far jump at the thunk: 9 05FA:24CC (8), 10 05FA:2558 (none). */
                    static const struct { uint16_t entry; uint8_t args; } nb_[9]={
                        {0x15EF,1},{0x100D,2},{0x0FDA,2},{0x15D7,1},{0x11A2,1},
                        {0x05CA,0},{0x04D2,1},{0x1992,4},{0x2049,2}};
                    for(unsigned k=0;k<9;k++) {
                        const uint16_t code=(uint16_t)(0xF200+0x100*k);
                        const uint16_t id=(uint16_t)(0x0A51+0x1111*k);
                        XW8(0,nb_[k].entry,0xE9);XW16(0,nb_[k].entry+1,(uint16_t)(code-nb_[k].entry-3));
                        XSTUB(0,code,k,id,nb_[k].args,0);
                    }
                    static const struct { uint16_t thunk; uint8_t args; } fb_[2]={{0x24CC,8},{0x2558,0}};
                    for(unsigned k=9;k<11;k++) {
                        const uint16_t code=(uint16_t)(0xF200+0x100*k);
                        const uint16_t id=(uint16_t)(0x0A51+0x1111*k);
                        XW8(0x05FA,fb_[k-9].thunk,0xEA);XW16(0x05FA,fb_[k-9].thunk+1,code);XW16(0x05FA,fb_[k-9].thunk+3,0);
                        XSTUB(0,code,k,id,fb_[k-9].args,1);
                    }
                }
#undef XSTUB
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"END.EXE") && o->seg==0 && o->ip==0x1607) {
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);uint16_t o__=(uint16_t)(o_);XW8((s_),o__,v__);XW8((s_),(uint16_t)(o__+1),v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XPICK(n_) ((unsigned)(XRAND()%(n_)))
                /* END's data segment 05FA (the driver thunk 05FA:25F3 lies in it)
                 * or a separate one; the stack in it or in its own. */
                const uint16_t dg=XPICK(2)?0x05FA:0x5000;
                seg[S_DS]=dg;seg[S_SS]=XPICK(2)?0x6000:dg;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                /* The frame is 316h bytes below the return address. With the
                 * stack in the data segment it stays clear of [50CB], [55BA],
                 * the thunk at 25F3, the stubs' cells (F400-F840) and the stub
                 * code (CS F200-F700 is DS 9260-9760 when DS is 05FA); near 0
                 * it wraps to the top of the segment. */
                switch(XPICK(4)) {
                    case 0: r[R_SP]=0xF000; break;
                    case 1: r[R_SP]=(uint16_t)(0x1000+2*XPICK(0x0400)); break;
                    case 2: r[R_SP]=(uint16_t)(2*XPICK(0x0100)); break;
                    default: r[R_SP]=(uint16_t)(0x3000+2*XPICK(0x0800)); break;
                }
                for(unsigned j=0;j<0x400;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x400+j),XRAND());
                /* The colour table: real 6-bit values, the sign edges and noise;
                 * its offset sometimes near the top of DS so BX+SI wraps, and
                 * (stack in DS) sometimes under the frame being written. */
                uint16_t pal;
                switch(XPICK(4)) {
                    case 0: pal=(uint16_t)(0x6000+XPICK(0x1000)); break;
                    case 1: pal=(uint16_t)(0x0400+XPICK(0x0800)); break;
                    case 2: pal=(uint16_t)(0xFD00+XPICK(0x0300)); break;
                    default: pal=(uint16_t)(0xA000+XPICK(0x3000)); break;
                }
                {
                    static const uint8_t edge[]={0,0x3F,0x7F,0x80,0xFF,0x40,0xC0};
                    for(unsigned j=0;j<0x300;j++) {
                        uint8_t v;
                        switch(XPICK(5)) {
                            case 0: case 1: case 2: v=(uint8_t)XPICK(0x40); break;
                            case 3: v=edge[XPICK(sizeof edge)]; break;
                            default: v=(uint8_t)XRAND(); break;
                        }
                        XW8(dg,(uint16_t)(pal+j),v);
                    }
                }
                /* The arguments (from, to, steps), the speed byte [55BA] and the
                 * bytes the show probes leave in it after each pass. A model of
                 * the routine counts the passes: each costs 300h elements, so a
                 * state may have 2 with the real 539C and 3 with the short 539C
                 * stub (the step cap is 100000 instructions); a state whose IDIV
                 * would fault is drawn again. */
                uint16_t from=0,to=0,steps=0;uint8_t b0=0,bl[32];
                const unsigned cat=XPICK(8);
                const int short539=cat>=5||XPICK(3)==0;
                const unsigned limit=short539?3:2;
                for(unsigned attempt=0;;attempt++) {
                    static const uint16_t sedge[]={0x7FFF,0x8000,0xFFFF,0x8001,0x00FF,0x0101,0x0100,0x0000};
                    uint16_t sv[2];
                    for(unsigned k=0;k<2;k++) {
                        switch(XPICK(8)) {
                            case 0: sv[k]=0; break;
                            case 1: sv[k]=0x100; break;
                            case 2: sv[k]=(uint16_t)XPICK(0x101); break;
                            case 3: sv[k]=(uint16_t)(0x40*XPICK(5)); break;
                            case 4: sv[k]=sedge[XPICK(8)]; break;
                            case 5: sv[k]=(uint16_t)XPICK(0x40); break;
                            default: sv[k]=(uint16_t)XRAND(); break;
                        }
                    }
                    from=sv[0];to=sv[1];
                    {
                        static const uint16_t tedge[]={0x7FFF,0x8000,0xFFFF,0x8001,0xFFFE};
                        switch(XPICK(6)) {
                            case 0: steps=1; break;
                            case 1: steps=2; break;
                            case 2: steps=(uint16_t)(1+XPICK(0x20)); break;
                            case 3: steps=tedge[XPICK(5)]; break;
                            default: steps=(uint16_t)XRAND(); break;
                        }
                    }
                    {
                        static const uint8_t bedge[]={0,1,2,0xFF,0xFE,0x80,0x7F,3};
                        b0=XPICK(3)==0?(uint8_t)XRAND():bedge[XPICK(8)];
                        for(unsigned j=0;j<32;j++)bl[j]=XPICK(2)?b0:(XPICK(2)?(uint8_t)XRAND():bedge[XPICK(8)]);
                    }
                    if(cat<=1)steps=0;
                    if(cat==2)to=from;
                    if(cat>=5) {
                        /* Counter 2 from the byte the first probe leaves: v * steps
                         * = 2 (mod 10000h), including IMUL's overflow. */
                        static const uint8_t vb[]={1,2,0xFF,0xFE,2,0xFE};
                        static const uint16_t vs[]={2,1,0xFFFE,0xFFFF,0x8001,0x7FFF};
                        const unsigned k=XPICK(8);
                        if(k<6) { bl[0]=vb[k];steps=vs[k]; }
                        else {
                            uint16_t v=(uint16_t)(int16_t)(int8_t)(uint8_t)(XRAND()|1),inv=v;
                            for(unsigned it=0;it<5;it++)inv=(uint16_t)((uint32_t)inv*(2u-(uint32_t)v*inv));
                            bl[0]=(uint8_t)v;steps=(uint16_t)(2*inv);
                        }
                        if(to==from)to=(uint16_t)(to+1+XPICK(0x200));
                    }
                    /* The model. */
                    int32_t sc=steps?(int32_t)(int16_t)from:(int32_t)(int16_t)to;
                    uint16_t counter=0,step=0;unsigned np=0;int bad=0;
                    for(;;) {
                        if(++np>limit) { bad=1;break; }
                        const uint8_t b=bl[(np-1)&31];          /* left by this pass's probe */
                        if((int32_t)(int16_t)to==sc)break;
                        if(!counter) {
                            counter=(uint16_t)((int32_t)(int8_t)b*(int32_t)(int16_t)steps);
                            const int32_t d=counter?(int32_t)(int16_t)counter:0xFF;
                            const int32_t n=(int32_t)(int16_t)(uint16_t)(to-from);
                            if(n==-32768&&(d==-1||d==1)) { bad=1;break; }
                            step=(uint16_t)(int16_t)(n/d);
                        }
                        counter--;
                        if((int16_t)counter>0)sc+=(int16_t)step; else sc=(int32_t)(int16_t)to;
                    }
                    if(!bad&&(cat<5||np==3))break;
                    if(attempt>200) { steps=0;break; }
                }
                XW16(seg[S_SS],r[R_SP],pal);XW16(seg[S_SS],(uint16_t)(r[R_SP]+2),from);
                XW16(seg[S_SS],(uint16_t)(r[R_SP]+4),to);XW16(seg[S_SS],(uint16_t)(r[R_SP]+6),steps);
                small[0]=pal;small[1]=from;small[2]=to;small[3]=steps;
                for(unsigned j=4;j<8;j++)XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),XRAND());
                XW8(dg,0x55BA,b0);
                /* The timer flag; when set, the driver says busy a few times. */
                XW8(dg,0x50CB,XPICK(2)?0:(uint8_t)(1+XPICK(255)));
                const unsigned waits=cat>=5?XPICK(2):XPICK(4);
                for(unsigned k=0;k<5;k++)
                    for(unsigned j=0;j<32;j++)XW16(dg,0xF400+0x40*k+2*j,XRAND());
                {
                    static const uint16_t busy[]={1,0x8000,0xFFFF,0x0100};
                    for(unsigned j=0;j<32;j++) {
                        uint16_t v=0;
                        if(j<waits) { v=XPICK(2)?busy[XPICK(4)]:(uint16_t)XRAND();if(!v)v=1; }
                        XW16(dg,0xF400+2*j,v);
                    }
                }
                for(unsigned j=0;j<32;j++)XW8(dg,0xF400+0x40*3+2*j,bl[j]);
                XW16(dg,0xF800,XRAND());
                for(unsigned j=2;j<0x40;j+=2)XW16(dg,0xF800+j,0);
                /* Returning probes: fold a number naming the callee, the caller's
                 * FLAGS (PUSHF at entry) and every argument in order into DS:F800,
                 * count the call (F802 + 2k), keep a list index (F820 + 2k) and
                 * return the next word of the list at F400 + 40h * k. The two
                 * show probes (42D0, 1708) also fold the speed byte, the caller's
                 * locals below the buffer they are given (counter, colour, step,
                 * index, brightness) and four buffer words, and leave the low
                 * byte of their (shared) list in [55BA]. */
#define XEMIT(b_) XW8(0,cp_++,(b_))
#define XWORD(v_) do { const uint16_t w_=(uint16_t)(v_);XEMIT(w_);XEMIT(w_>>8); } while(0)
                {
                    static const struct { uint16_t entry; uint8_t args, far_, probe, list; } sb_[5]={
                        {0x25F3,1,1,0,0},{0x4BCA,0,0,0,1},{0x4B8C,0,0,0,2},{0x42D0,1,0,1,3},{0x1708,1,0,1,3}};
                    static const int16_t fold_[]={-0x12,-0x10,-0x0E,-0x0C,-0x0A,-0x08,-0x06,0x000,0x07F,0x155,0x2FE};
                    for(unsigned k=0;k<5;k++) {
                        uint16_t cp_=(uint16_t)(0xF200+0x100*k);
                        const uint16_t id=(uint16_t)(0x0A51+0x1111*k),cnt=(uint16_t)(0xF802+2*k);
                        const uint16_t ptr=(uint16_t)(0xF820+2*sb_[k].list),list=(uint16_t)(0xF400+0x40*sb_[k].list);
                        if(sb_[k].far_) { XW8(0x05FA,sb_[k].entry,0xEA);XW16(0x05FA,sb_[k].entry+1,cp_);XW16(0x05FA,sb_[k].entry+3,0); }
                        else { XW8(0,sb_[k].entry,0xE9);XW16(0,sb_[k].entry+1,(uint16_t)(cp_-sb_[k].entry-3)); }
                        XEMIT(0x55);XEMIT(0x8B);XEMIT(0xEC);XEMIT(0x53);XEMIT(0x9C);XEMIT(0x58);
                        XEMIT(0xFF);XEMIT(0x06);XWORD(cnt);
                        XEMIT(0x8B);XEMIT(0x1E);XWORD(0xF800);XEMIT(0xD1);XEMIT(0xC3);
                        XEMIT(0x81);XEMIT(0xF3);XWORD(id);XEMIT(0x33);XEMIT(0xD8);
                        XEMIT(0x89);XEMIT(0x1E);XWORD(0xF800);
                        for(unsigned j=0;j<sb_[k].args;j++) {
                            XEMIT(0x8B);XEMIT(0x46);XEMIT((sb_[k].far_?6:4)+2*j);
                            XEMIT(0x8B);XEMIT(0x1E);XWORD(0xF800);XEMIT(0xD1);XEMIT(0xC3);XEMIT(0x33);XEMIT(0xD8);
                            XEMIT(0x89);XEMIT(0x1E);XWORD(0xF800);
                        }
                        if(sb_[k].probe) {
                            XEMIT(0x8B);XEMIT(0x16);XWORD(0xF800);
                            XEMIT(0xA0);XWORD(0x55BA);XEMIT(0xD1);XEMIT(0xC2);XEMIT(0x33);XEMIT(0xD0);
                            XEMIT(0x8B);XEMIT(0x5E);XEMIT(0x04);
                            for(unsigned j=0;j<sizeof fold_/sizeof fold_[0];j++) {
                                XEMIT(0x36);XEMIT(0x8B);XEMIT(0x87);XWORD(fold_[j]);
                                XEMIT(0xD1);XEMIT(0xC2);XEMIT(0x33);XEMIT(0xD0);
                            }
                            XEMIT(0x89);XEMIT(0x16);XWORD(0xF800);
                        }
                        XEMIT(0x8B);XEMIT(0x1E);XWORD(ptr);XEMIT(0x8B);XEMIT(0x87);XWORD(list);
                        XEMIT(0x83);XEMIT(0xC3);XEMIT(0x02);XEMIT(0x83);XEMIT(0xE3);XEMIT(0x3E);
                        XEMIT(0x89);XEMIT(0x1E);XWORD(ptr);
                        if(sb_[k].probe) { XEMIT(0xA2);XWORD(0x55BA); }
                        XEMIT(0x5B);XEMIT(0x5D);XEMIT(sb_[k].far_?0xCB:0xC3);
                    }
                }
                /* The long multiply 539C (RET 8) stays the original's in most
                 * states; the short stub (pop the return address and both longs,
                 * DX = ((a.hi ^ b.lo) + b.hi + a.lo), DL += DH, JMP back) costs
                 * 10 instructions and tells the operands apart. */
                if(short539) {
                    static const uint8_t m539[]={0x59,0x58,0x5A,0x5B,0x33,0xD3,0x5B,0x03,0xD3,0x03,0xD0,0x02,0xD6,0xFF,0xE1};
                    for(unsigned j=0;j<sizeof m539;j++)XW8(0,0x539C+j,m539[j]);
                }
#undef XWORD
#undef XEMIT
#undef XPICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if(!strcmp(o->module,"PLAYER.EXE") && o->seg==0 && o->ip==0x0BE2) {
                /* LZ91 block unpacker: a well-formed stream, encoded in the
                 * decoder's own read order, placed at a resumed input pointer
                 * (often just below the buffer end 11B7h, so every refill site
                 * runs), with a resumed flag word and bit count. The refill
                 * 0B84 stays original code: its INT 21h read is answered by
                 * the harness and brings no bytes, so the stream's bytes from
                 * 11B8h on are also placed 1000h lower, where the refilled
                 * pointer finds them (0B84 itself carries 11B7h down). */
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XW8(s_,o_,v_) do { const uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { const uint16_t v__=(uint16_t)(v_);XW8(s_,o_,v__);XW8(s_,(o_)+1,v__>>8); } while(0)
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(XRAND()&1)?dg:0x6000;r[R_SP]=0xE000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],r[R_SP]-0x200+j,XRAND());
                /* The destination: a linear address; the high word's upper
                 * bits are junk the shifts drop one time in four. */
                const uint32_t lin_=0x72000u+(uint32_t)(XRAND()&0x7FFF);
                small[0]=(uint16_t)lin_;
                small[1]=(uint16_t)((lin_>>16)|((XRAND()&3)==0?(XRAND()&0xFFF0):0));
                small[2]=(uint16_t)XRAND();small[3]=(uint16_t)XRAND();
                for(unsigned j=0;j<4;j++)XW16(seg[S_SS],r[R_SP]+2*j,small[j]);
                /* The encoder: bits go into the current flag word (the resumed
                 * BP word first when the count is nonzero); when the count runs
                 * out the next word is reserved at the stream's current end
                 * and the bit still belongs to the old word. */
                uint8_t st_[0x400];unsigned len_=0;
                for(unsigned j=0;j<sizeof st_;j++)st_[j]=(uint8_t)XRAND();
                uint16_t bpw_=(uint16_t)XRAND();
                const unsigned k_=(XRAND()%3==0)?0:1+XRAND()%16;
                int wi_=-1;unsigned bi_=0,cnt_=k_;
                if(k_==0){wi_=0;len_=2;cnt_=16;}
#define XBIT(b_) do { const unsigned b__=(unsigned)(b_)&1u; cnt_--; \
    if(wi_<0) bpw_=(uint16_t)((bpw_&~(1u<<bi_))|(b__<<bi_)); \
    else { uint16_t w__=(uint16_t)(st_[wi_]|(st_[wi_+1]<<8)); w__=(uint16_t)((w__&~(1u<<bi_))|(b__<<bi_)); \
           st_[wi_]=(uint8_t)w__; st_[wi_+1]=(uint8_t)(w__>>8); } \
    bi_++; if(cnt_==0){ wi_=(int)len_; len_+=2; cnt_=16; bi_=0; } } while(0)
#define XBYTE(v_) do { st_[len_++]=(uint8_t)(v_); } while(0)
#define XWORD(v_) do { const uint16_t x__=(uint16_t)(v_); XBYTE(x__); XBYTE(x__>>8); } while(0)
                static const uint8_t lens_[]={2,3,0x7F,0x80,0xFE,0xFF};
                const unsigned ntok_=(XRAND()%8==0)?0:1+XRAND()%40;
                for(unsigned t=0;t<ntok_;t++) {
                    const unsigned kind_=XRAND()%4;
                    if(kind_<2) { XBIT(1);XBYTE(XRAND()); }
                    else if(kind_==2) { XBIT(0);XBIT(0);XBIT(XRAND()>>7);XBIT(XRAND()>>7);XBYTE(XRAND()); }
                    else {
                        XBIT(0);XBIT(1);
                        if(XRAND()%3) XWORD((XRAND()&0xF8FF)|((1+XRAND()%7)<<8));
                        else {
                            XWORD(XRAND()&0xF8FF);
                            XBYTE((XRAND()&1)?lens_[XRAND()%6]:2+XRAND()%254);
                        }
                    }
                }
                /* The end: 0 finishes the stream, 1 only this block. */
                XBIT(0);XBIT(1);XWORD(XRAND()&0xF8FF);XBYTE((XRAND()&1)?0:1);
                len_+=2;    /* (a word the count may have reserved; random bytes) */
                const uint16_t v0_=(XRAND()&1)?(uint16_t)(0x11B8-XRAND()%48):(uint16_t)(0x01B8+XRAND()%0xF00);
                for(unsigned j=0;j<len_;j++) {
                    const uint16_t v_=(uint16_t)(v0_+j);
                    if(v_<=0x11B8)XW8(dg,v_,st_[j]);
                    if(v_>=0x11B8)XW8(dg,v_-0x1000,st_[j]);
                }
                XW16(dg,0x01B6,v0_);XW16(dg,0x11BA,bpw_);XW16(dg,0x11BC,k_);
#undef XWORD
#undef XBYTE
#undef XBIT
#undef XW16
#undef XW8
#undef XRAND
            }
            if (!strcmp(o->module,"PLAYER.EXE") && o->seg==0 && o->ip==0x03FE) {
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XW8(s_,o_,v_) do { const uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { const uint16_t v__=(uint16_t)(v_);XW8(s_,o_,v__);XW8(s_,(o_)+1,v__>>8); } while(0)
#define XL(o_,v_) do { const uint32_t l_=(uint32_t)(v_);XW16(dg,(o_),l_);XW16(dg,(o_)+2,l_>>16); } while(0)
/* A signed distance: equal, one apart, one high word apart, small or anything. */
#define XDELTA(v_) do { static const uint32_t d_[]={0,1,0xFFFFFFFFu,0x10000u,0xFFFF0000u,0x10001u,0xFFFEFFFFu,0xFFFFu,0xFFFF0001u}; \
    const unsigned k_=XRAND()%12;const uint32_t r1_=XRAND(),r2_=XRAND(); \
    (v_)=k_<9?d_[k_]:k_==9?(r1_&0xFF):k_==10?(uint32_t)0-(r1_&0xFF):((r1_<<16)|r2_); } while(0)
/* A distance of at least `min_`. */
#define XUP(v_,min_) do { static const uint32_t u_[]={0,1,0x10000u,0x10001u,0xFFFFu,0x8000u}; \
    const unsigned k_=XRAND()%8;const uint32_t r1_=XRAND(),r2_=XRAND(); \
    (v_)=(uint32_t)(min_)+(k_<6?u_[k_]:k_==6?(r1_&0xFF):(((r1_&3)<<16)|r2_)); } while(0)
/* A long whose high word is small, all ones or anything. */
#define XR32(v_) do { static const uint16_t h_[]={0,1,4,9,0xFFFF}; \
    const unsigned k_=XRAND()%7;const uint32_t r1_=XRAND(),r2_=XRAND(); \
    (v_)=((uint32_t)(k_<5?h_[k_]:r1_)<<16)|r2_; } while(0)
                const uint16_t dg=0x5000;
                seg[S_DS]=dg;seg[S_SS]=(XRAND()&1)?dg:0x6000;r[R_SP]=0xE000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                for(unsigned j=0;j<0x200;j++)XW8(seg[S_SS],r[R_SP]-0x200+j,XRAND());
                for(unsigned j=0x1DC0;j<0x20C0;j++)XW8(dg,j,XRAND());
                /* The incomplete picture's length (low word of cursor - used) and the table count. */
                static const uint16_t di_[]={0,1,0x1FFF,0x2000,0x2001,0x7FFF,0x8000,0xFFFF};
                const unsigned dk_=XRAND()%10;
                const uint16_t di=dk_<8?di_[dk_]:(uint16_t)XRAND();
                static const uint16_t count_[]={0,0,0,1,0x8000,0xFFFF};
                const unsigned ck_=XRAND()%7;
                const uint16_t count=ck_<6?count_[ck_]:(uint16_t)XRAND();
                const uint16_t kept=count?(di<0x2000?0x2000:di):0;
                uint32_t used,cur,end,mark,limit,start,d,u;
                const unsigned mode=XRAND()%8;
                XR32(start);XR32(used);
                if(mode>=3 && mode<=5) {
                    /* Past the limit: the mark near the end of a block at the start. */
                    XDELTA(d);mark=start+0xC000u+kept+d;
                    XUP(u,0);used=mark+u;cur=used+di;end=cur+0xC000u;
                    XUP(u,1);limit=end-u;
                } else if(mode==0) {
                    /* Not wrapped: the end of the block near the limit. */
                    cur=used+di;end=cur+0xC000u;XUP(u,0);mark=used-u;
                    XDELTA(d);limit=end+d;
                } else if(mode==1) {
                    /* Wrapped: the end of the block near the mark. */
                    cur=used+di;end=cur+0xC000u;XDELTA(d);mark=end+d;
                    XDELTA(d);limit=end+d;
                } else if(mode==2) {
                    /* The mark near the last complete picture. */
                    cur=used+di;end=cur+0xC000u;XDELTA(d);mark=used+d;
                    XDELTA(d);limit=end+d;
                } else {
                    XR32(cur);XR32(mark);XR32(limit);
                }
                XL(0x2082,cur);XL(0x2094,used);XL(0x1E6E,mark);XL(0x1E60,limit);XL(0x1E6A,start);
                XW16(dg,0x1DE8,count);
                /* 0x005F6 (copy, five words, plain RET): a stub at F200 folds its arguments
                 * in order into the cell D100, counts the call at D102 and returns a value. */
                {
                    const uint16_t cell_=0xD100,stub_=0xF200;
                    uint16_t at_=stub_;
#define XE8(v_) XW8(cs,at_++,(v_))
#define XE16(v_) do { const uint16_t w_=(uint16_t)(v_);XE8(w_);XE8(w_>>8); } while(0)
                    XW16(dg,cell_,XRAND());XW16(dg,cell_+2,0);
                    XW8(cs,0x05F6,0xE9);XW16(cs,0x05F7,(uint16_t)(stub_-0x05F6-3));
                    XE8(0x55);XE8(0x89);XE8(0xE5);XE8(0x53);XE8(0x56);XE8(0x57);
                    XE8(0x8B);XE8(0x1E);XE16(cell_);
                    for(unsigned a=0;a<5;a++) {
                        XE8(0xD1);XE8(0xC3);XE8(0x8B);XE8(0x46);XE8(4+2*a);XE8(0x31);XE8(0xC3);
                    }
                    XE8(0x89);XE8(0x1E);XE16(cell_);
                    {const uint16_t result_=(uint16_t)XRAND();XE8(0xB8);XE16(result_);}
                    XE8(0xFF);XE8(0x06);XE16(cell_+2);
                    XE8(0x5F);XE8(0x5E);XE8(0x5B);XE8(0x5D);XE8(0xC3);
#undef XE16
#undef XE8
                }
#undef XR32
#undef XUP
#undef XDELTA
#undef XL
#undef XW16
#undef XW8
#undef XRAND
            }
            if (!strcmp(o->module,"SETUP.EXE") && o->seg==0 && o->ip==0x0A9F) {
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(uint16_t)((o_)+1),v__>>8); } while(0)
                /* Data segment: the program's own (024Ch) or another; the stack in it or apart. */
                static const uint16_t sps_[3]={0xF304,0xF504,0xFD04};
                const uint16_t ds_=(XRAND()%3)?0x024C:0x5000;
                const uint16_t ss_=(XRAND()&1)?ds_:0x6000;
                const uint16_t sp_=sps_[XRAND()%3];
                const uint16_t e_=(uint16_t)(sp_-2);              /* SP at entry: the return address */
                seg[S_DS]=ds_;seg[S_SS]=ss_;r[R_SP]=sp_;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                /* The three relocated MOV AX,DGROUP operands (0AB8, 0B2F, 0B84): one segment as the
                 * loader makes them, or now and then different ones, so each read shows. */
                const uint16_t segs_[4]={0x024C,ds_,ss_,0x4000};
                const uint16_t v1_=(XRAND()&1)?0x024C:segs_[XRAND()%4];
                const uint16_t v2_=(XRAND()%3)?v1_:segs_[XRAND()%4];
                const uint16_t v3_=(XRAND()%3)?v2_:segs_[XRAND()%4];
                XW16(0,0x0AB8,v1_);XW16(0,0x0B2F,v2_);XW16(0,0x0B84,v3_);
                /* The stack, and the same area in the segment SS becomes after the load. */
                for(unsigned j=0;j<0x40;j++)XW8(v2_,(uint16_t)(sp_-0x30+j),XRAND());
                for(unsigned j=0;j<0x40;j++)XW8(ss_,(uint16_t)(sp_-0x30+j),XRAND());
                {
                    uint16_t arg_[4];
                    arg_[0]=(XRAND()&1)?(uint16_t)(0x0100+(XRAND()&0x0FFF)):(uint16_t)XRAND();   /* the file name */
                    for(unsigned j=1;j<4;j++)arg_[j]=(uint16_t)XRAND();
                    for(unsigned j=0;j<4;j++){XW16(ss_,(uint16_t)(sp_+2*j),arg_[j]);small[j]=arg_[j];}
                }
                /* The program's end (INT 21h AH=4Ch at 0BA0) is answered here and falls into 0BA2:
                 * there, and at the address a bare RET at 0B9C takes from the saved BP (E-2, as SS:E-12
                 * holds), LEA SP,[BP+2] / JMP 0B9C returns to the caller through the routine's own RET. */
                {
                    const uint16_t stub_[2]={0x0BA2,(uint16_t)(e_-2)};
                    for(unsigned k=0;k<2;k++) {
                        const uint16_t rel_=(uint16_t)(0x0B9C-(stub_[k]+6));
                        XW8(0,stub_[k],0x8D);XW8(0,stub_[k]+1,0x66);XW8(0,stub_[k]+2,0x02);
                        XW8(0,stub_[k]+3,0xE9);XW16(0,stub_[k]+4,rel_);
                    }
                }
                if(v2_!=ss_) {                                    /* the frame as the new SS sees it */
                    XW16(v2_,(uint16_t)(e_-12),(uint16_t)(e_-2));  /* the saved BP: MOV SP,BP */
                    XW16(v2_,e_,back);
                }
                /* DOS: the impossible 48h request fails (or, rarely, not); the real one gives a block
                 * (or fails); the overlay load succeeds or fails with 2, 8 or another code (AH set
                 * too, near 2 and 8); the shrink succeeds or fails. Later answers are random. */
                const uint16_t blk_=(uint16_t)(0x7000+0x10*(XRAND()%64));
                g_dos=1;g_dos_script_n=8;
                for(unsigned k=0;k<8;k++){g_dos_script[k].ax=(uint16_t)XRAND();g_dos_script[k].cf=(uint8_t)(XRAND()&1);g_dos_script[k].put=0;}
                g_dos_script[0].cf=(uint8_t)(XRAND()%8!=0);
                if(g_dos_script[0].cf)g_dos_script[0].ax=8;
                g_dos_script[1].cf=(uint8_t)(XRAND()%8==0);
                g_dos_script[1].ax=g_dos_script[1].cf?(uint16_t)((XRAND()&1)?8:7):blk_;
                {
                    static const uint16_t codes_[12]={2,8,1,3,7,9,0x0102,0x0108,0x0802,0x0208,0x0005,0x0012};
                    g_dos_script[2].cf=(uint8_t)(XRAND()&1);
                    if(g_dos_script[2].cf)g_dos_script[2].ax=(XRAND()%8==0)?(uint16_t)XRAND():codes_[XRAND()%12];
                }
                g_dos_script[3].cf=(uint8_t)(XRAND()%5==0);
                /* The block the routine reads back: its own when it reads [0786h] where it wrote it. */
                uint16_t es_=blk_;
                if(v2_!=ds_) {
                    es_=(XRAND()&1)?blk_:(uint16_t)(0x7800+0x10*(XRAND()%64));
                    XW16(v2_,0x0786,es_);
                }
                for(unsigned j=0;j<0x30;j++){XW8(blk_,j,XRAND());XW8(es_,j,XRAND());}
                {
                    uint16_t h1_=(uint16_t)XRAND(),h2_=(uint16_t)XRAND();
                    if(XRAND()%4==0)h1_=(uint16_t)(XRAND()&0x00FF);
                    if(XRAND()%4==0)h2_=(uint16_t)(0xFFF0|(XRAND()&15));
                    XW16(es_,0x1E,h1_);XW16(es_,0x20,h2_);
                    const uint16_t par_=(uint16_t)((h1_>>4)+(h2_>>4));
                    if(v2_!=ds_) {                                /* the room it compares with: around the size */
                        const unsigned k_=XRAND()%6;
                        const uint16_t room_=k_==0?par_:k_==1?(uint16_t)(par_-1):k_==2?(uint16_t)(par_+1):
                                             k_==3?(uint16_t)XRAND():k_==4?0:0xFBFF;
                        XW16(v2_,0x0788,room_);
                    }
                }
                if(v3_!=ds_) {                                    /* the flag and the result where it reads them */
                    if(v3_!=v2_)XW16(v3_,0x0786,XRAND());
                    XW8(v3_,0x078E,(XRAND()%4==0)?(uint8_t)(1+XRAND()%255):0);
                }
                XW8(ds_,0x078E,(uint8_t)(XRAND()|1));             /* cleared by the routine */
#undef XW16
#undef XW8
#undef XRAND
            }
            if (!strcmp(o->module,"END.EXE") && o->seg==0 && o->ip==0x04D2) {

#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define PICK(n_) ((unsigned)(XRAND()%(n_)))
                const uint16_t dg=(XRAND()&1)?0x5000:0x5200;
                seg[S_DS]=dg;seg[S_SS]=(XRAND()&1)?dg:0x6000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                { static const uint16_t sp_[]={0xF000,0x0100,0x0020,0xFFF0};r[R_SP]=sp_[PICK(4)]; }
                for(unsigned j=0;j<0x100;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x100+j),XRAND());
                for(unsigned j=0;j<8;j++){const uint16_t w_=(uint16_t)XRAND();XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w_);if(j<4)small[j]=w_;}
                XW16(dg,0xE800,XRAND());
                for(unsigned k=0;k<6;k++){
                    XW16(dg,0xE810+2*k,0);
                    XW16(dg,0xEA00+2*k,XRAND());XW16(dg,0xEA40+2*k,XRAND());
                    for(unsigned j=0;j<32;j++)XW16(dg,0xE900+0x40*k+2*j,XRAND());
                }
                /* Returning probes fold the caller's FLAGS, callee identity
                 * and every argument in order. Each call returns a different
                 * fixture word and clobbers AX/BX/CX/DX; SI/DI/BP survive. */
#define XSTUB(sg_,off_,k_,args_,far_) do { \
    uint8_t cb_[220];unsigned n_=0;const unsigned cnt_=0xE810+2*(k_),list_=0xE900+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x9C;cb_[n_++]=0x58; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xE8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(uint8_t)(0x51+17*(k_));cb_[n_++]=0x0A; \
    for(unsigned j_=0;j_<(args_);j_++){ \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; } \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xE8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xE3;cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x0E;cb_[n_++]=(uint8_t)(2*(k_));cb_[n_++]=0xEA; \
    cb_[n_++]=0x8B;cb_[n_++]=0x16;cb_[n_++]=(uint8_t)(0x40+2*(k_));cb_[n_++]=0xEA; \
    cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
#define XNEAR(entry_,k_,args_) do { const uint16_t stub_=(uint16_t)(0xF200+0x100*(k_)); \
    XW8(0,(entry_),0xE9);XW16(0,(entry_)+1,(uint16_t)(stub_-(entry_)-3));XSTUB(0,stub_,k_,args_,0); } while(0)
#define XFAR(sg_,off_,k_,args_) do { const uint16_t stub_=(uint16_t)(0x1000+0x100*(k_)); \
    XW8((sg_),(off_),0xEA);XW16((sg_),(off_)+1,stub_);XW16((sg_),(off_)+3,0x3000); \
    XSTUB(0x3000,stub_,k_,args_,1); } while(0)

                const uint16_t wait_=(XRAND()&1)?0:(uint16_t)(1+PICK(255));
                small[0]=(uint16_t)((XRAND()&0xFF00)|wait_);XW16(seg[S_SS],r[R_SP],small[0]);
                XW16(dg,0x54B8,PICK(7));
                XW16(dg,0x5DA0,XRAND());XW16(dg,0x720C,XRAND());XW16(dg,0x7226,XRAND());
                for(unsigned j=0;j<8;j++){
                    static const uint8_t ids_[]={0,1,2,3,0x7F,0x80,0xFF,8};
                    const uint8_t id_=ids_[PICK(8)];const uint16_t rec_=(uint16_t)(0x56EA+16*(int16_t)(int8_t)id_);
                    XW8(dg,0x5440+2*j,id_);XW8(dg,0x5441+2*j,(XRAND()&1)?0:(uint8_t)(1+PICK(255)));
                    XW16(dg,0x54BA+2*j,XRAND());XW16(dg,0x5532+2*j,XRAND());
                    XW16(dg,(uint16_t)(rec_+6),(XRAND()&1)?0:(uint16_t)(1+XRAND()));
                    XW8(dg,(uint16_t)(rec_+8),(uint8_t)XRAND());
                }
                for(unsigned j=0;j<32;j++)XW16(dg,0xE940+2*j,PICK(3)==0?0:(uint16_t)(1+XRAND()));
                XNEAR(0x1992,0,4);XNEAR(0x2092,1,1);XFAR(0x05FA,0x25F3,2,1);

#undef XFAR
#undef XNEAR
#undef XSTUB
#undef PICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"END.EXE") && o->seg==0 && o->ip==0x3D4F) {

#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define PICK(n_) ((unsigned)(XRAND()%(n_)))
                const uint16_t dg=(XRAND()&1)?0x5000:0x5200;
                seg[S_DS]=dg;seg[S_SS]=(XRAND()&1)?dg:0x6000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                { static const uint16_t sp_[]={0xF000,0x0100,0x0020,0xFFF0};r[R_SP]=sp_[PICK(4)]; }
                for(unsigned j=0;j<0x100;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x100+j),XRAND());
                for(unsigned j=0;j<8;j++){const uint16_t w_=(uint16_t)XRAND();XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w_);if(j<4)small[j]=w_;}
                XW16(dg,0xE800,XRAND());
                for(unsigned k=0;k<6;k++){
                    XW16(dg,0xE810+2*k,0);
                    XW16(dg,0xEA00+2*k,XRAND());XW16(dg,0xEA40+2*k,XRAND());
                    for(unsigned j=0;j<32;j++)XW16(dg,0xE900+0x40*k+2*j,XRAND());
                }
                /* Returning probes fold the caller's FLAGS, callee identity
                 * and every argument in order. Each call returns a different
                 * fixture word and clobbers AX/BX/CX/DX; SI/DI/BP survive. */
#define XSTUB(sg_,off_,k_,args_,far_) do { \
    uint8_t cb_[220];unsigned n_=0;const unsigned cnt_=0xE810+2*(k_),list_=0xE900+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x9C;cb_[n_++]=0x58; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xE8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(uint8_t)(0x51+17*(k_));cb_[n_++]=0x0A; \
    for(unsigned j_=0;j_<(args_);j_++){ \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; } \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xE8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xE3;cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x0E;cb_[n_++]=(uint8_t)(2*(k_));cb_[n_++]=0xEA; \
    cb_[n_++]=0x8B;cb_[n_++]=0x16;cb_[n_++]=(uint8_t)(0x40+2*(k_));cb_[n_++]=0xEA; \
    cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
#define XNEAR(entry_,k_,args_) do { const uint16_t stub_=(uint16_t)(0xF200+0x100*(k_)); \
    XW8(0,(entry_),0xE9);XW16(0,(entry_)+1,(uint16_t)(stub_-(entry_)-3));XSTUB(0,stub_,k_,args_,0); } while(0)
#define XFAR(sg_,off_,k_,args_) do { const uint16_t stub_=(uint16_t)(0x1000+0x100*(k_)); \
    XW8((sg_),(off_),0xEA);XW16((sg_),(off_)+1,stub_);XW16((sg_),(off_)+3,0x3000); \
    XSTUB(0x3000,stub_,k_,args_,1); } while(0)

                XNEAR(0x100D,0,2);XFAR(0x05FA,0x2576,1,0);
                XFAR(0x05FA,0x257B,2,5);XFAR(0x05FA,0x2580,3,0);XNEAR(0x1FE5,4,1);
                for(unsigned j=0;j<0x80;j++)XW8(dg,0x6D70+j,XRAND());
                XW16(dg,0x5DA0,XRAND());XW16(dg,0x5DB6,XRAND());XW16(dg,0x643E,XRAND());
                XW16(dg,0x5B9C,XRAND());XW16(dg,0x720C,XRAND());XW16(dg,0x7226,XRAND());

#undef XFAR
#undef XNEAR
#undef XSTUB
#undef PICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0xD8AF) {

#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define PICK(n_) ((unsigned)(XRAND()%(n_)))
                const uint16_t dg=(XRAND()&1)?0x5000:0x5200;
                seg[S_DS]=dg;seg[S_SS]=(XRAND()&1)?dg:0x6000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                { static const uint16_t sp_[]={0xF000,0x0100,0x0020,0xFFF0};r[R_SP]=sp_[PICK(4)]; }
                for(unsigned j=0;j<0x100;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x100+j),XRAND());
                for(unsigned j=0;j<8;j++){const uint16_t w_=(uint16_t)XRAND();XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w_);if(j<4)small[j]=w_;}
                XW16(dg,0xE800,XRAND());
                for(unsigned k=0;k<6;k++){
                    XW16(dg,0xE810+2*k,0);
                    XW16(dg,0xEA00+2*k,XRAND());XW16(dg,0xEA40+2*k,XRAND());
                    for(unsigned j=0;j<32;j++)XW16(dg,0xE900+0x40*k+2*j,XRAND());
                }
                /* Returning probes fold the caller's FLAGS, callee identity
                 * and every argument in order. Each call returns a different
                 * fixture word and clobbers AX/BX/CX/DX; SI/DI/BP survive. */
#define XSTUB(sg_,off_,k_,args_,far_) do { \
    uint8_t cb_[220];unsigned n_=0;const unsigned cnt_=0xE810+2*(k_),list_=0xE900+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x9C;cb_[n_++]=0x58; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xE8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(uint8_t)(0x51+17*(k_));cb_[n_++]=0x0A; \
    for(unsigned j_=0;j_<(args_);j_++){ \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; } \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xE8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xE3;cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x0E;cb_[n_++]=(uint8_t)(2*(k_));cb_[n_++]=0xEA; \
    cb_[n_++]=0x8B;cb_[n_++]=0x16;cb_[n_++]=(uint8_t)(0x40+2*(k_));cb_[n_++]=0xEA; \
    cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
#define XNEAR(entry_,k_,args_) do { const uint16_t stub_=(uint16_t)(0xF200+0x100*(k_)); \
    XW8(0,(entry_),0xE9);XW16(0,(entry_)+1,(uint16_t)(stub_-(entry_)-3));XSTUB(0,stub_,k_,args_,0); } while(0)
#define XFAR(sg_,off_,k_,args_) do { const uint16_t stub_=(uint16_t)(0x1000+0x100*(k_)); \
    XW8((sg_),(off_),0xEA);XW16((sg_),(off_)+1,stub_);XW16((sg_),(off_)+3,0x3000); \
    XSTUB(0x3000,stub_,k_,args_,1); } while(0)

                const uint16_t block_=(PICK(5)==0)?0xFFF0:(uint16_t)(0x2000+2*PICK(0x80));
                for(unsigned j=0;j<0x18;j++)XW8(dg,(uint16_t)(block_+j),XRAND());
                static const uint16_t bounds_[]={0,1,2,0x7FFF,0x8000,0xFFFF,319,199,139,109};
                const uint16_t top_=PICK(3)==0?0:bounds_[PICK(10)];
                const uint16_t id_=(uint16_t)XRAND();
                XW16(dg,block_,id_);XW16(dg,(uint16_t)(block_+0x0E),top_);
                XW16(dg,(uint16_t)(block_+0x10),bounds_[PICK(10)]);
                XW16(dg,(uint16_t)(block_+0x12),bounds_[PICK(10)]);
                XW16(dg,(uint16_t)(block_+0x14),bounds_[PICK(10)]);
                XW16(dg,(uint16_t)(2*id_-0x79EE),XRAND());
                XW16(dg,0x368C,(XRAND()&1)?0:(uint16_t)(1+XRAND()));
                {static const uint8_t modes_[]={0,1,2,0x80,0xFF};XW8(dg,0x294B,modes_[PICK(5)]);}
                XW16(seg[S_SS],r[R_SP],block_);small[0]=block_;
                XFAR(0x1E42,0x025F,0,1);XFAR(0x1E42,0x0250,1,2);XFAR(0x1E42,0x019C,2,1);

#undef XFAR
#undef XNEAR
#undef XSTUB
#undef PICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module,"VGAME.EXE") && o->seg==0 && o->ip==0xD578) {

#define XW8(s_,o_,v_) do { uint32_t a_=phys((s_),(uint16_t)(o_));g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(v_); } while(0)
#define XW16(s_,o_,v_) do { uint16_t v__=(uint16_t)(v_);XW8((s_),(o_),v__);XW8((s_),(o_)+1,v__>>8); } while(0)
                uint32_t rng_=(uint32_t)routine_seed(seed,o)+(uint32_t)s*2654435761u;
#define XRAND() (rng_=rng_*1664525u+1013904223u,rng_>>16)
#define PICK(n_) ((unsigned)(XRAND()%(n_)))
                const uint16_t dg=(XRAND()&1)?0x5000:0x5200;
                seg[S_DS]=dg;seg[S_SS]=(XRAND()&1)?dg:0x6000;
                flags=(uint16_t)(flags & (0xFFFFu^F_DF));
                { static const uint16_t sp_[]={0xF000,0x0100,0x0020,0xFFF0};r[R_SP]=sp_[PICK(4)]; }
                for(unsigned j=0;j<0x100;j++)XW8(seg[S_SS],(uint16_t)(r[R_SP]-0x100+j),XRAND());
                for(unsigned j=0;j<8;j++){const uint16_t w_=(uint16_t)XRAND();XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),w_);if(j<4)small[j]=w_;}
                XW16(dg,0xE800,XRAND());
                for(unsigned k=0;k<6;k++){
                    XW16(dg,0xE810+2*k,0);
                    XW16(dg,0xEA00+2*k,XRAND());XW16(dg,0xEA40+2*k,XRAND());
                    for(unsigned j=0;j<32;j++)XW16(dg,0xE900+0x40*k+2*j,XRAND());
                }
                /* Returning probes fold the caller's FLAGS, callee identity
                 * and every argument in order. Each call returns a different
                 * fixture word and clobbers AX/BX/CX/DX; SI/DI/BP survive. */
#define XSTUB(sg_,off_,k_,args_,far_) do { \
    uint8_t cb_[220];unsigned n_=0;const unsigned cnt_=0xE810+2*(k_),list_=0xE900+0x40*(k_); \
    cb_[n_++]=0x55;cb_[n_++]=0x8B;cb_[n_++]=0xEC;cb_[n_++]=0x9C;cb_[n_++]=0x58; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xE8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; \
    cb_[n_++]=0x81;cb_[n_++]=0xF3;cb_[n_++]=(uint8_t)(0x51+17*(k_));cb_[n_++]=0x0A; \
    for(unsigned j_=0;j_<(args_);j_++){ \
        cb_[n_++]=0x8B;cb_[n_++]=0x46;cb_[n_++]=(uint8_t)(((far_)?6:4)+2*j_); \
        cb_[n_++]=0xD1;cb_[n_++]=0xC3;cb_[n_++]=0x33;cb_[n_++]=0xD8; } \
    cb_[n_++]=0x89;cb_[n_++]=0x1E;cb_[n_++]=0x00;cb_[n_++]=0xE8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x1E;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0xFF;cb_[n_++]=0x06;cb_[n_++]=cnt_&255;cb_[n_++]=cnt_>>8; \
    cb_[n_++]=0xD1;cb_[n_++]=0xE3;cb_[n_++]=0x83;cb_[n_++]=0xE3;cb_[n_++]=0x3E; \
    cb_[n_++]=0x8B;cb_[n_++]=0x87;cb_[n_++]=list_&255;cb_[n_++]=list_>>8; \
    cb_[n_++]=0x8B;cb_[n_++]=0x0E;cb_[n_++]=(uint8_t)(2*(k_));cb_[n_++]=0xEA; \
    cb_[n_++]=0x8B;cb_[n_++]=0x16;cb_[n_++]=(uint8_t)(0x40+2*(k_));cb_[n_++]=0xEA; \
    cb_[n_++]=0x5D;cb_[n_++]=(far_)?0xCB:0xC3; \
    for(unsigned k2_=0;k2_<n_;k2_++)XW8((sg_),(off_)+k2_,cb_[k2_]); } while(0)
#define XNEAR(entry_,k_,args_) do { const uint16_t stub_=(uint16_t)(0xF200+0x100*(k_)); \
    XW8(0,(entry_),0xE9);XW16(0,(entry_)+1,(uint16_t)(stub_-(entry_)-3));XSTUB(0,stub_,k_,args_,0); } while(0)
#define XFAR(sg_,off_,k_,args_) do { const uint16_t stub_=(uint16_t)(0x1000+0x100*(k_)); \
    XW8((sg_),(off_),0xEA);XW16((sg_),(off_)+1,stub_);XW16((sg_),(off_)+3,0x3000); \
    XSTUB(0x3000,stub_,k_,args_,1); } while(0)

                static const uint16_t indices_[]={0,1,2,3,4,7,0x7FFF,0x8000,0xFFFF};
                const uint16_t el_=indices_[PICK(9)],rec_=(uint16_t)(0x4708+12*el_);
                const uint16_t group_=PICK(3)==0?4:(uint16_t)XRAND();
                const uint16_t pal_=(uint16_t)(0x46A2+16*group_);
                for(unsigned j=0;j<8;j++)XW16(dg,(uint16_t)(pal_+2*j),XRAND());
                XW16(dg,rec_,XRAND());XW16(dg,(uint16_t)(rec_+2),XRAND());
                XW16(dg,(uint16_t)(rec_+4),XRAND());XW16(dg,(uint16_t)(rec_+6),XRAND());
                XW16(dg,(uint16_t)(rec_+8),group_);XW16(dg,(uint16_t)(rec_+10),XRAND());
                small[0]=(uint16_t)XRAND();small[1]=el_;small[2]=(XRAND()&1)?0:(uint16_t)(1+PICK(65535));
                for(unsigned j=0;j<3;j++)XW16(seg[S_SS],(uint16_t)(r[R_SP]+2*j),small[j]);
                XFAR(0x1E42,0x01E7,0,7);

#undef XFAR
#undef XNEAR
#undef XSTUB
#undef PICK
#undef XRAND
#undef XW16
#undef XW8
            }
            if (!strcmp(o->module, "START.EXE") && o->seg == 0 && o->ip == 0x6DB8) {
                seg[S_DS] = seg[S_SS] = 0x0A95; r[R_SP] = 0xFF00;
                flags = (uint16_t)(flags & (0xFFFFu ^ F_DF));
#define NW8(seg_,off_,value_) do { uint32_t a_=phys((seg_),(uint16_t)(off_)); g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(value_); } while(0)
#define NW16(seg_,off_,value_) do { uint16_t v_=(uint16_t)(value_); NW8(seg_,off_,v_);NW8(seg_,(uint16_t)((off_)+1),v_>>8); } while(0)
                uint32_t x_=(uint32_t)s*2654435761u+12345u;
#define NEXT() (x_=x_*1103515245u+12345u, (x_>>16))
                for(unsigned j=0;j<0x200;j++) NW8(0x0A95,0xFD00+j,0);
                /* Scratch: stub code at F000, key script at F410, checksum cells at F500. */
                for(unsigned j=0;j<0x600;j++) NW8(0x0A95,0xF000+j,0);
                for(unsigned j=0;j<0x40;j++) { NW8(0x0A95,0xDC00+j,NEXT()); NW8(0x0A95,0xDC18+j,NEXT()); }
                /* Far drawing slots become jumps to argument-logging stubs: each folds its
                 * stack words into one checksum cell, so a wrong argument is a difference. */
                static const struct { uint16_t slot, code, cell, args; } far_stubs[]={
                    {0x6A8A,0xF000,0xF500,8},{0x6A26,0xF080,0xF502,7},{0x6BB1,0xF100,0xF504,1},{0x69D1,0xF140,0xF506,2}};
                for(unsigned j=0;j<sizeof far_stubs/sizeof far_stubs[0];j++) {
                    const uint16_t code=far_stubs[j].code;
                    NW8(0x0A95,far_stubs[j].slot,0xEA); NW16(0x0A95,far_stubs[j].slot+1,code); NW16(0x0A95,far_stubs[j].slot+3,0x0A95);
                    unsigned at=code;
#define EMIT(b_) NW8(0x0A95,at++,(b_))
                    EMIT(0x55);EMIT(0x8B);EMIT(0xEC);EMIT(0x50);EMIT(0x53);
                    for(unsigned k=0;k<far_stubs[j].args;k++) {
                        EMIT(0x8B);EMIT(0x46);EMIT(6+2*k);                           /* mov ax,[bp+6+2k] */
                        EMIT(0x8B);EMIT(0x1E);EMIT(far_stubs[j].cell);EMIT(far_stubs[j].cell>>8);   /* mov bx,[cell] */
                        EMIT(0xD1);EMIT(0xC3);EMIT(0x33);EMIT(0xD8);                 /* rol bx,1 / xor bx,ax */
                        EMIT(0x89);EMIT(0x1E);EMIT(far_stubs[j].cell);EMIT(far_stubs[j].cell>>8);   /* mov [cell],bx */
                    }
                    EMIT(0x5B);EMIT(0x58);EMIT(0x5D);EMIT(0xCB);
                }
                /* Character width: 1 + ((char ^ [F510]) & 7). */
                NW8(0x0A95,0x6AA3,0xEA); NW16(0x0A95,0x6AA4,0xF180); NW16(0x0A95,0x6AA6,0x0A95);
                {   unsigned at=0xF180;
                    EMIT(0x55);EMIT(0x8B);EMIT(0xEC);EMIT(0x8B);EMIT(0x46);EMIT(0x06);
                    EMIT(0x33);EMIT(0x06);EMIT(0x10);EMIT(0xF5);EMIT(0x25);EMIT(0x07);EMIT(0x00);
                    EMIT(0x05);EMIT(0x01);EMIT(0x00);EMIT(0x5D);EMIT(0xCB); }
                NW16(0x0A95,0xF510,NEXT());
                /* The hit poll (38AC) returns scripted keys, then cancels. */
                static const uint16_t keymap[16]={0,1,2,3,4,5,6,7,8,9,10,0x63,0x64,11,0x100,3};
                const unsigned keycount=1+(NEXT()%15);
                for(unsigned j=0;j<keycount;j++) NW16(0x0A95,0xF410+2*j,keymap[NEXT()&15]);
                for(unsigned j=keycount;j<16;j++) NW16(0x0A95,0xF410+2*j,0x63);
                NW16(0x0A95,0xF400,0);
                {   static const uint8_t poll[]={0x8B,0x1E,0x00,0xF4,0x83,0x06,0x00,0xF4,0x02,0x8B,0x87,0x10,0xF4,0xC3};
                    for(unsigned j=0;j<sizeof poll;j++) NW8(0,0x38AC+j,poll[j]); }
                                NW8(0,0x8427,0xC3);
                /* Record at 3000:1000, fields in range; backup slots at D892 + 50h * slot. */
                const unsigned slot=NEXT()&3;
                const unsigned alias=(s&31)==31;
                const uint16_t rec_seg=0x3000, rec_off=0x1000;
                for(unsigned j=0;j<0x80;j++) NW8(rec_seg,rec_off+j,NEXT());
                NW16(rec_seg,rec_off+0,slot);
                NW16(rec_seg,rec_off+0x20,NEXT()%7);
                NW16(rec_seg,rec_off+0x3C,NEXT()&3);
                NW16(rec_seg,rec_off+0x3E,NEXT()%3);
                NW16(rec_seg,rec_off+0x40,NEXT()%3);
                NW16(rec_seg,rec_off+0x42,NEXT()&1);
                NW16(0x0A95,0xCACA,rec_off); NW16(0x0A95,0xCACC,rec_seg);
                NW16(0x0A95,0xCABA,NEXT());
                for(unsigned j=0;j<0x140;j++) NW8(0x0A95,0xD892+j,NEXT());
                {   /* a name: words of letters, the surname possibly empty or alone */
                    const unsigned words=1+(NEXT()%3);
                    unsigned at=0xD892+0x50*slot+2;
                    for(unsigned w=0;w<words;w++) {
                        const unsigned len=(w==words-1)?(NEXT()%38):(1+NEXT()%8);
                        for(unsigned j=0;j<len;j++) NW8(0x0A95,at++,'A'+(NEXT()%26));
                        if(w!=words-1) NW8(0x0A95,at++,' ');
                    }
                    NW8(0x0A95,at,0);
                }
                NW16(0x0A95,0xE08A,NEXT()); NW16(0x0A95,0xE08C,NEXT()); NW8(0x0A95,0xD096,NEXT());
                NW8(0x0A95,0x64E2,0);
                (void)alias;
#undef EMIT
#undef NEXT
#undef NW8
#undef NW16
            }
            if (!strcmp(o->module, "END.EXE") && o->seg == 0 && o->ip == 0x2BEC) {
                seg[S_DS] = seg[S_SS] = 0x05FA; r[R_SP] = 0xFF00;
                flags = (uint16_t)(flags & (0xFFFFu ^ F_DF));
#define NW8(seg_,off_,value_) do { uint32_t a_=phys((seg_),(uint16_t)(off_)); g_pristine[a_]=g_mem[0][a_]=g_mem[1][a_]=(uint8_t)(value_); } while(0)
#define NW16(seg_,off_,value_) do { uint16_t v_=(uint16_t)(value_); NW8(seg_,off_,v_);NW8(seg_,(uint16_t)((off_)+1),v_>>8); } while(0)
                for(unsigned j=0;j<0x200;j++) NW8(0x05FA,0xFD00+j,0);
                for(unsigned j=0;j<1024;j++) NW8(0x05FA,0x6986+j,0);
                for(unsigned j=0;j<0x80;j++) { NW8(0x3000,0x1000+j,0);NW8(0x3000,0x2000+j,0); }
                for(unsigned j=0;j<22;j++) NW8(0x05FA,0x6D8C+j,0);
                NW16(0x05FA,0x531E,0);
                const uint16_t narrative_seg = (s & 63) >= 62 ? 0x05FA : 0x3000;
                const uint16_t narrative_off = (s & 63) == 62 ? 0x6986 : (s & 63) == 63 ? 0xFD80 : 0x1000;
                for(unsigned j=0;j<0x80;j++) NW8(narrative_seg,narrative_off+j,0);
                NW16(0x05FA,0x7222,narrative_off);NW16(0x05FA,0x7224,narrative_seg);
                NW16(0x05FA,0x55DE,0x2000);NW16(0x05FA,0x55E0,0x3000);
                static const uint8_t name[]={67,72,69,67,75,0};
                for(unsigned j=0;j<sizeof name;j++) NW8(0x05FA,0x7228+j,name[j]);
                const unsigned mode=(unsigned)s;
                NW16(narrative_seg,narrative_off+0x26,mode&3);NW16(narrative_seg,narrative_off+0x28,(mode>>2)&1);NW16(narrative_seg,narrative_off+0x30,(mode>>3)&1);
                NW16(0x3000,0x203A,(mode>>4)%3);
                NW8(0x05FA,0x6440,(mode>>5)&1);NW8(0x05FA,0x6D89,(mode>>6)&1);NW8(0x05FA,0x720E,(mode>>7)&1);
                static const uint8_t totals[]={0,1,2,11,12,13,255};
                NW8(0x05FA,0x64C8,totals[mode%7]);NW8(0x05FA,0x55DC,0);
                NW8(0x05FA,0x6984,(mode>>3)%3);NW8(0x05FA,0x6D88,(mode>>5)%3);
                NW8(0x05FA,0x5B9A,(mode>>2)%3);NW8(0x05FA,0x55DB,(mode>>1)&1);NW8(0x05FA,0x55E8,mode&1);
                /* Returning drawing/input helpers; string operations stay real. */
                static const uint16_t ui[]={0x0FA4,0x4722,0x0FDA,0x42BE,0x1A75,0x206A};
                for(unsigned j=0;j<sizeof ui/sizeof ui[0];j++) NW8(0,ui[j],0xC3);
                /* Return a measured text height through the supplied context. */
                static const uint16_t heights[]={0,8,40,184,185,0xFFFF};
                const uint16_t height=heights[(mode>>8)%6];
                const uint8_t wrap[]={0x53,0x8B,0xDC,0x36,0x8B,0x5F,0x04,0xC7,0x47,0x0A,(uint8_t)height,(uint8_t)(height>>8),0x5B,0xC3};
                for(unsigned j=0;j<sizeof wrap;j++) NW8(0,0x1A75+j,wrap[j]);
                /* The continue prompt records its two arguments, so a wrong position is a difference. */
                static const uint8_t prompt[]={0x8B,0xDC,0x8B,0x47,0x02,0xA3,0x00,0x90,0x8B,0x47,0x04,0xA3,0x02,0x90,0xC3};
                for(unsigned j=0;j<sizeof prompt;j++) NW8(0,0x206A+j,prompt[j]);
                NW8(0,0x3021,0xB8);NW16(0,0x3022,(mode%5)*75);NW8(0,0x3024,0xC3);
#undef NW8
#undef NW16
            }
            /* Author-written overlay headers. Separate memory also permits the
             * original zero-count LOOP to complete all 65,536 iterations. */
            g_fixture_args = (bind_entries || sprite_colour) && (s & 7) == 1;
            g_step_cap = (!strcmp(o->module,"START.EXE") && o->seg==0 && o->ip==0x7904) ? 2000000 : (bind_entries && (s == 1 || s == 2)) || (zero_loops && s == 1) ? 500000 : 100000;
            g_native_int10 = !strcmp(o->module, "SETUP.EXE") && o->seg == 0 && o->ip == 0x14BE;
            if (bind_entries && ((s & 7) == 1 || (s & 7) == 2)) {
                const unsigned mode = (s >> 3) & 7;
                static const uint16_t counts[] = {1,2,255,16384,255,7,8,3};
                seg[S_DS] = mode == 4 ? 0x7000 : 0x3000;
                seg[S_SS] = mode == 5 || mode == 6 ? 0x7000 : 0x5000;
                r[R_SP] = mode == 5 || mode == 6 ? 0x0028 : 0xFF00;
                /* ENTER-equivalent saves replace the header count at SS:22h. */
                if (mode == 5 || mode == 6) r[R_BP] = (uint16_t)(mode == 5 ? 2 : 1);
                small[0] = 0x7000;
                const uint16_t fields[][2] = {{0x18,0x3456},{0x1C,mode == 1 || mode == 7 ? 0xABFF : 0},{0x22,(s == 1 || s == 2) ? 0 : counts[mode]}};
                for (unsigned j = 0; j < sizeof fields / sizeof fields[0]; j++) {
                    uint32_t a = phys(0x7000, fields[j][0]);
                    for (int k = 0; k < 2; k++) {
                        const uint8_t b = (uint8_t)(fields[j][1] >> (k * 8));
                        g_pristine[a+k] = g_mem[0][a+k] = g_mem[1][a+k] = b;
                    }
                }
            }
            if (sprite_colour && ((s & 7) == 1 || (s & 7) == 2)) {
                static const uint8_t streams[][16] = {
                    {0xFF}, {0xFE,0xFF}, {0xF9,0xFF}, {0xF9,0xFE,0xFF},
                    {0xF9,1,0xFC,2,0x7F,0xFF}, {0,0xFF}, {0,0xFE,0xFF},
                    {0,0xFC,0x10,0xFD,0xAB,0x7F,0xFE,0xF9,2,0x20,0xFF}
                };
                const unsigned mode = (s >> 3) & 15;
                seg[S_DS] = 0x3000; seg[S_SS] = 0x5000; r[R_SP] = 0xFF00;
                small[0] = mode >= 10 ? 0xFFFC : 0x0800;
                small[1] = 0x7000;
                static const uint16_t bases[] = {0,1,255,2};
                small[2] = bases[(s >> 7) & 3];
                /* Live locals/saved BP, or caller arguments, become the stream. */
                if (mode == 8) {
                    seg[S_SS] = 0x5000; r[R_SP] = 0x0100; r[R_BP] = 0xFF11;
                    small[0] = 0x00F8; small[1] = 0x5000;
                } else if (mode == 9) {
                    r[R_SP] = 0x0100; small[0] = 0x0102; small[1] = 0x5000; small[2] = 0xFF00;
                }
                for (unsigned j = 0; j < sizeof streams[0]; j++) {
                    const uint32_t a = phys(small[1], (uint16_t)(small[0]+j));
                    g_pristine[a] = g_mem[0][a] = g_mem[1][a] = streams[mode & 7][j];
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
            if (zero_loops && s == 1 && (returned != 1 || g_self_written)) {
                fprintf(stderr, "record-table zero-count fixture did not return intact\n");
                return 1;
            }
            if (returned == 2 && !g_self_written) {
                g_side[0].overflow = g_side[1].overflow = 1;
                g_lost = 0; g_ncalls = 0; g_callover = 0;
                const int ran = o->fn(&g_m);
                if (g_mission_fixture) {
                    if (!ran || g_lost || !g_m.cpu.int_depth) {
                        fprintf(stderr, "mission generator missed divide fault (state %d)\n",s);
                        mc++; mb++; restore(); continue;
                    }
                    mission_fault_mask |= a->op_ip==0x499D ? 1 : 2;
                    mission_faults++; g_cpu[1]=g_m.cpu; mc++;
                    g_ctx=" (mission generator divide fault)";
                    mb += compare(o->module, ip, cs, ip, 0);
                    g_ctx=""; restore(); continue;
                }
                if (g_projectile_fixture) {
                    if (!ran || g_lost || !g_m.cpu.int_depth) {
                        fprintf(stderr, "projectile update missed divide fault (state %d, original %04X, named %04X:%04X, depth %d, lost %d)\n",
                                s, a->op_ip, g_m.cpu.seg[S_CS], g_m.cpu.ip, g_m.cpu.int_depth, g_lost);
                        mc++; mb++; restore(); continue;
                    }
                    for (unsigned k = 0; k < sizeof PROJECTILE_DIVIDES / sizeof PROJECTILE_DIVIDES[0]; k++)
                        if (a->op_ip == PROJECTILE_DIVIDES[k]) projectile_fault_mask |= 1u << k;
                    projectile_faults++; g_cpu[1] = g_m.cpu; mc++;
                    g_ctx = " (projectile divide fault)";
                    mb += compare(o->module, ip, cs, ip, 0);
                    g_ctx = ""; restore(); continue;
                }
                if (g_traffic_fixture) {
                    if (!ran || g_lost || !g_m.cpu.int_depth) {
                        fprintf(stderr, "aircraft traffic missed divide fault (state %d)\n", s);
                        mc++; mb++; restore(); continue;
                    }
                    for (unsigned k = 0; k < sizeof TRAFFIC_DIVIDES / sizeof TRAFFIC_DIVIDES[0]; k++)
                        if (a->op_ip == TRAFFIC_DIVIDES[k]) traffic_fault_mask |= 1u << k;
                    traffic_faults++; g_cpu[1] = g_m.cpu; mc++;
                    g_ctx = " (aircraft traffic divide fault)";
                    mb += compare(o->module, ip, cs, ip, 0);
                    g_ctx = ""; restore(); continue;
                }
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
            if (!returned || a->seg[S_CS] != cs || a->ip != back || g_self_written) steps = (int)g_step_cap;
            g_side[0].overflow = g_side[1].overflow = 1;      /* compare all memory */
            /* A real return lands back at the caller's stack level (RET n
             * pops at most a few words); a wild jump that happens to reach
             * the return address - a slide through zeroed memory - does not. */
            const uint16_t popped = (uint16_t)(a->r[R_SP] - entry_sp);
            const int setargv_ = (!strcmp(o->module, "PLAYER.EXE") && o->ip == 0x1602) ||
                (!strcmp(o->module, "SETUP.EXE") && o->ip == 0x19B6) ||
                (!strcmp(o->module, "DSWAP.EXE") && o->seg == 0 && o->ip == 0x117E) ||
                (!strcmp(o->module, "MPS_LOGO.EXE") && o->seg == 0x0146 && o->ip == 0x08CC);   /* returns by JMP with the vector left on the stack */
            if ((unsigned)steps >= g_step_cap || ((popped < 2 || popped > 18) && !(setargv_ && (uint16_t)(0u - popped) < 0x400u))) { if (getenv("FLWHY") && o->ip == 0x0815) printf("why1 s=%d steps=%d popped=%u\n", s, steps, popped); if (g_cov >= 0) g_cov_skips[(unsigned)steps >= g_step_cap ? 1 : 2]++; ms++; restore(); continue; }
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
            {   /* and the returning stubs planted at its callees: a routine that overwrites one (a fuzzed pointer
                 * aimed at it) leaves the callee running garbage, and no comparison can follow it */
                int over = 0;
                for (unsigned k = 0; k < sizeof IMAGE_PATCHES / sizeof IMAGE_PATCHES[0] && !over; k++)
                    if (!strcmp(IMAGE_PATCHES[k].module, o->module) && IMAGE_PATCHES[k].ip == o->ip)
                        over = g_mem[0][IMAGE_PATCHES[k].linear] != g_pristine[IMAGE_PATCHES[k].linear];
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
            const int covered_region = !g_callee_finished && (whole || (g_cov >= 0 && COVERED[g_cov].boundary &&
                g_m.cpu.seg[S_CS] == cs && g_m.cpu.ip == COVERED[g_cov].boundary &&
                g_m.cpu.icount == 1000 + COVERED[g_cov].clocks) ||
                (g_cov >= 0 && program_end_reached(o, &g_m.cpu, cs)));
            if (g_m.cpu.seg[S_CS] != cs || g_m.cpu.ip != back) {
                int more = 0;
                if (!run_to_ret(&g_m.cpu, entry_sp, o->matched, &more, cs, ip)) { if (g_cov >= 0) g_cov_skips[5]++; ms++; restore(); continue; }
                g_finished++;
            }
            mc++;
            if (sprite_colour && ((s & 7) == 1 || (s & 7) == 2) && whole) colour_modes |= 1u << ((s >> 3) & 15);
            if (bind_entries && ((s & 7) == 1 || (s & 7) == 2) && whole) { bind_modes |= 1u << ((s >> 3) & 7); if (s == 1 || s == 2) bind_zero++; }
            if (g_cov >= 0) for (unsigned k = 0; k < COVERED[g_cov].len; k++) {
                g_cov_cover[k] |= g_cov_seen[k]; if (covered_region) g_cov_whole[k] |= g_cov_seen[k];
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
                if (pr && (g_callover || (!g_ncalls && g_m.cpu.stop_at && g_m.cpu.icount > g_m.cpu.stop_at))) {
                    printf("  OVERRUN %s %04X:%04X: ran to +%llu with the limit at +%llu\n", o->module, o->seg, ip,
                           (unsigned long long)(g_m.cpu.icount - start), (unsigned long long)k);
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
        if (sprite_colour) { printf("  sprite colour modes %04X\n", colour_modes); if (states >= 4000 && colour_modes != 65535) mb++; }
        if (bind_entries) { printf("  binding modes %02X, completed zero-count wraps %u\n", bind_modes, bind_zero); if (states >= 4000 && (bind_modes != 255 || !bind_zero)) mb++; }
        if (g_checked_alloc) printf("  completed allocation-error states: %llu\n", g_alloc_errors);
        if (camera_gradients) printf("  completed 16-band states: %llu\n", g_gradient_full);
        if (g_cov >= 0) {
            unsigned all = 0, whole = 0;
            for (unsigned k = 0; k < COVERED[g_cov].len; k++) { all += g_cov_cover[k]; whole += g_cov_whole[k]; }
            if (g_cov_escapes) { fprintf(stderr, "%s: %llu calls escaped their return\n", COVERED[g_cov].what, g_cov_escapes); mb++; }
            printf("  %s instructions run by compared states: %u, by states completing the named region: %u (of %u)\n",
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
        if (g_mission_fixture) {
            printf("  mission generator divide faults: %llu; sites %X (required 3)\n",mission_faults,mission_fault_mask);
            if (states>=4000 && mission_fault_mask!=3) {fprintf(stderr,"mission fixture missed divide faults\n");mb++;}
        }
        if (g_ground_fixture) {
            printf("  ground reload divide fault states: %llu; mask %X (required 3)\n", ground_faults, ground_fault_mask);
            if (states >= 4000 && ground_fault_mask != 3) { fprintf(stderr, "ground fixtures missed required divide faults\n"); mb++; }
        }
        if (g_projectile_fixture) {
            printf("  projectile divide fault states: %llu; sites %02X\n", projectile_faults, projectile_fault_mask);
            if (states >= 4000 && !projectile_faults) { fprintf(stderr, "projectile fixtures missed divide faults\n"); mb++; }
        }
        if (g_traffic_fixture) {
            printf("  aircraft traffic divide fault states: %llu; sites %02X\n", traffic_faults, traffic_fault_mask);
            if (states >= 4000 && !traffic_faults) { fprintf(stderr, "traffic fixtures missed divide faults\n"); mb++; }
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
