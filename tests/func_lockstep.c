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
        const int wide = get_r8(c, R_AH) == 0x42 || ((v >> 56) & 3) == 1;
        c->r[R_AX] = (uint16_t)(v >> 16);
        if (((v >> 58) & 3) == 0) set_r8(c, R_AL, 0);  /* AL 0 (success, found) one time in four */
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

/* Sentinel words to plant, by routine (segment, IP): at DS:[reg + disp], or at DS:disp when reg is PLANT_ABS. */
#define PLANT_ABS 8
static const struct { uint16_t seg, ip; uint8_t reg; uint16_t disp; uint16_t val; } PLANTS[] = {
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


/* START and END, and VGAME's weapon-lock marker and canopy, call the graphics driver through 5-byte thunks in
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
    if (strcmp(m->name, "START.EXE") && strcmp(m->name, "END.EXE") &&
        (strcmp(m->name, "VGAME.EXE") || (o->ip != 0xB171 && o->ip != 0xD6DD) || o->seg != 0)) return;
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
    { "END.EXE", 0x42E4, 0x0489, 0x00 },
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
    { "START.EXE", 0x1B37, 0, { 0x0064, 0x0065 } },     /* the map caption's kinds 64h and 65h */
};

/* Routines whose own RET lies outside the 0x300 bytes from their entry (a long routine entered at
 * its first piece, or one that ends in a shared tail placed before it): where their code starts and
 * how far it reaches. */
static const struct { const char *module; uint16_t ip, from, span; } SPANS[] = {
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

/* Entry points inside a longer routine whose RET lies before the entry (a second entry that jumps back to
 * a shared exit): the routine's own code starts `below` bytes under the entry, and a RET there is its own. */
static const struct { const char *module; uint16_t seg, ip, below; } CODE_BELOW[] = {
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
static int g_self_written;
static int run_to_ret(cpu_t *a, uint16_t entry_sp, int far, int *steps, uint16_t cs, uint16_t ip)
{
    const uint32_t watch = phys(cs, (uint16_t)(ip - g_below));
    const int watching = g_below && a->mem == g_mem[0];
    while (*steps < 100000) {
        if (watching && !g_self_written && memcmp(a->mem + watch, g_pristine + watch, 0x300u + g_below)) g_self_written = 1;
        if (g_trace) printf("      %04X:%04X clk %llu sp %04X\n", a->seg[S_CS], a->ip, (unsigned long long)a->icount, a->r[R_SP]);
        const uint16_t here = a->ip;
        const int own = a->seg[S_CS] == cs && (uint16_t)(here - g_from) < g_span;
        if (own && (uint16_t)(here - g_from) > g_reach) g_reach = (uint16_t)(here - g_from);
        int inside = own;
        for (int k = 0; k < g_nend && !inside; k++) inside = a->seg[S_CS] == cs && here >= g_end_lo[k] && here < g_end_hi[k];
        const uint8_t op = a->mem[phys(a->seg[S_CS], a->ip)];
        const uint16_t sp_before = a->r[R_SP];
        cpu_step(a);
        if (a->flags & F_TF) cpu_interrupt(a, 1);
        ++*steps;
        const int is_ret = far ? (op == 0xCB || op == 0xCA) : (op == 0xC3 || op == 0xC2);
        if (is_ret && sp_before == entry_sp) return inside;
        /* The C runtime's near stackavail takes its return address off into
         * CX and returns by JMP CX: that, with the address already popped. */
        if (!far && op == 0xFF && a->mem[phys(a->seg[S_CS], (uint16_t)(here + 1))] == 0xE1 &&
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
    g_ncalls++;
    if (c->icount > c->stop_at) g_callover = 1;  /* the CALL itself ran at or after the limit */
    for (int i = 0; i < 200000; i++) {
        if (c->ip == mm->trap_ip && c->r[R_SP] == mm->trap_sp && c->seg[S_CS] == mm->trap_cs) return RUN_TRAP;
        if (c->ip == g_back_ip && c->r[R_SP] == g_back_sp && c->seg[S_CS] == g_back_cs) return RUN_SLICE;
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
            const uint16_t back = (uint16_t)(ip + 0x8000);
            const uint16_t flags = (uint16_t)((rnd() & 0x0ED5u) | 0x0002u);
            uint16_t small[4];
            for (int a = 0; a < 4; a++)
                small[a] = (s & 4) ? (uint16_t)((int)(rnd() % 3) - 1) : (uint16_t)(rnd() % 32);
            for (unsigned pk = 0; pk < sizeof ARG_PLANTS / sizeof ARG_PLANTS[0]; pk++)
                if (!strcmp(ARG_PLANTS[pk].module, o->module) && ARG_PLANTS[pk].ip == o->ip && (rnd() & 1))
                    small[ARG_PLANTS[pk].arg] = ARG_PLANTS[pk].v[rnd() & 1];
            for (int k = 0; k < 2; k++)
                setup_side(k ? &g_m.cpu : &g_cpu[0], o, cs, ip, r, seg, flags, back, s, small);
            int steps = 0;
            cpu_t *a = &g_cpu[0];
            /* Run the original until the routine's own near RET - the first
             * one taken with the stack back at the caller's level - and
             * accept the state only when it returns to the pushed address. */
            const uint16_t entry_sp = (uint16_t)(r[R_SP] - (o->matched == 2 ? 4 : 2));
            g_back_cs = cs; g_back_ip = back; g_back_sp = r[R_SP];
            g_reach = 0;
            g_self_written = 0;
            const int returned = run_to_ret(a, entry_sp, o->matched == 2, &steps, cs, ip);
            const uint16_t reach = g_reach;
            if (!returned || a->seg[S_CS] != cs || a->ip != back || g_self_written) steps = 100000;
            g_side[0].overflow = g_side[1].overflow = 1;      /* compare all memory */
            /* A real return lands back at the caller's stack level (RET n
             * pops at most a few words); a wild jump that happens to reach
             * the return address - a slide through zeroed memory - does not. */
            const uint16_t popped = (uint16_t)(a->r[R_SP] - entry_sp);
            if (steps >= 100000 || popped < 2 || popped > 18) { if (getenv("FLWHY") && o->ip == 0x0815) printf("why1 s=%d steps=%d popped=%u\n", s, steps, popped); ms++; restore(); continue; }
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
            if (memcmp(g_mem[0] + self, g_pristine + self, window)) { ms++; restore(); continue; }
            {   /* and the shared code it reaches by a JMP */
                int over = 0;
                for (int k = 0; k < g_nend && !over; k++) {
                    const uint32_t lo = phys(cs, g_end_lo[k]);
                    over = memcmp(g_mem[0] + lo, g_pristine + lo, (size_t)(uint16_t)(g_end_hi[k] - g_end_lo[k])) != 0;
                }
                if (over) { ms++; restore(); continue; }
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
            { const int fr = o->fn(&g_m); if (getenv("FLWHY") && o->ip == 0x0815) printf("why2 s=%d fn=%d lost=%d ip=%04X\n", s, fr, g_lost, g_m.cpu.ip); if (!fr || g_lost) { ms++; restore(); continue; } }
            /* A routine that ran out of room after a call returns with the
             * machine partway through it, as the original would be; the run
             * loop then finishes it with the original code, and so does this. */
            if (g_m.cpu.seg[S_CS] != cs || g_m.cpu.ip != back) {
                int more = 0;
                if (!run_to_ret(&g_m.cpu, entry_sp, o->matched == 2, &more, cs, ip)) { ms++; restore(); continue; }
                g_finished++;
            }
            mc++;
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
                    run_to_ret(&g_cpu[0], entry_sp, o->matched == 2, &st2, cs, ip);
                    printf("    matched, then the original from where it stopped:\n");
                    g_lost = 0; g_stopped = 0;
                    o->fn(&g_m);
                    printf("      matched returned at %04X:%04X clk %llu\n", g_m.cpu.seg[S_CS], g_m.cpu.ip, (unsigned long long)g_m.cpu.icount);
                    int st3 = 0;
                    if (g_m.cpu.ip != back) run_to_ret(&g_m.cpu, entry_sp, o->matched == 2, &st3, cs, ip);
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
        compared += mc; skipped += ms; bad += mb;
    }
    printf("matched routines: %u, %llu states compared (%llu finished by the original code), %llu skipped, %llu mismatching\n",
           matched_count(), compared, g_finished, skipped, bad);
    return bad ? 1 : 0;
}
