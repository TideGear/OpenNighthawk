/* rerun.c - see rerun.h. */
#include "rerun.h"
#include "tls.h"
#include "observe.h"
#include "../fixes/fixes.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

void rerun_snap_take(rerun_snap *s, const machine_t *m)
{
    if (!s->mem) s->mem = (uint8_t *)malloc(MEM_SIZE);
    if (!s->fix_pending) s->fix_pending = malloc(fixes_pending_size());
    if (!s->mem || !s->fix_pending) { s->valid = 0; return; }
    s->m = *m;
    memcpy(s->mem, m->mem, MEM_SIZE);
    fixes_pending_save(s->fix_pending);
    s->valid = 1;
}

void rerun_snap_free(rerun_snap *s)
{
    free(s->mem);
    free(s->fix_pending);
    memset(s, 0, sizeof *s);
}

void rerun_snap_swap(rerun_snap *a, rerun_snap *b)
{
    static machine_t tm;              /* the machine structure is large: not on the stack */
    tm = a->m; a->m = b->m; b->m = tm;
    uint8_t *mem = a->mem; a->mem = b->mem; b->mem = mem;
    void *fix_pending = a->fix_pending; a->fix_pending = b->fix_pending; b->fix_pending = fix_pending;
    const int valid = a->valid; a->valid = b->valid; b->valid = valid;
}

void rerun_list_free(rerun_list *l)
{
    free(l->vals);
    free(l->rec);
    memset(l, 0, sizeof *l);
}

/* ---- the copy's observer ------------------------------------------------- */

static rerun_list *g_out;
static int g_done;            /* the frame closed: a phase after the present */

static void append(char kind, uint64_t icount, const int32_t *v, int n)
{
    if (!g_out || g_done) return;
    if (kind == 'P' && g_out->presented) { g_done = 1; return; }
    if (kind == 'Z' || kind == 'Y' || kind == 'J' || kind == 'I') return;   /* as drawfeed's steps keep them; P marks the phase */
    const size_t need = g_out->nvals + 4 + (size_t)n;
    if (need > g_out->cvals) {
        size_t cap = g_out->cvals ? g_out->cvals : 1 << 16;
        while (cap < need) cap *= 2;
        int32_t *vals = (int32_t *)realloc(g_out->vals, cap * sizeof *vals);
        if (!vals) return;
        g_out->vals = vals;
        g_out->cvals = cap;
    }
    int32_t *p = g_out->vals + g_out->nvals;
    p[0] = kind; p[1] = n; p[2] = (int32_t)(uint32_t)icount; p[3] = (int32_t)(uint32_t)(icount >> 32);
    if (n) memcpy(p + 4, v, (size_t)n * sizeof *v);
    g_out->nvals = need;
    if (kind == 'D' && n >= 1 && v[0] == 44) g_out->presented = 1;
}

static void on_vertex(void *u, uint64_t icount, const int32_t xf[3], const int32_t px[2], int range, uint16_t xf_at, uint16_t px_at)
{
    (void)u;
    const int32_t v[8] = { xf[0], xf[1], xf[2], px[0], px[1], range, xf_at, px_at };
    append('V', icount, v, 8);
}

static void on_prim(void *u, uint64_t icount, char kind, const int32_t *v, int n) { (void)u; append(kind, icount, v, n); }

static void on_phase(void *u, uint64_t icount) { (void)u; append('P', icount, NULL, 0); }

/* the same records the presentation's feed asks for (drawfeed.c), without page dumps */
static const f117_observer g_copy_obs = { .user = NULL, .frame_phase = on_phase, .vertex = on_vertex, .prim = on_prim,
                                          .sources = 1, .hud = 1, .bands = 1 };

/* ---- the copy -------------------------------------------------------------- */

static machine_t g_copy;
static uint8_t  *g_copy_mem;
static void     *g_pending;
static void     *g_fix_pending;
static void     *g_fix_after;
static machine_t g_before;
static uint8_t *g_before_mem;
static int g_blocked;

/* No DOS operation, including a chained call to its native stub, may touch
 * the host from a speculative draw. BIOS/device operations use the copy. */
static int copy_int(cpu_t *c, uint8_t vec)
{
    if (vec == 0x21 || vec == 0xE3 || vec == 0x20) {
        /* The draw polls DOS's console status and reads its clock. These
         * services use only the copy's memory and emulated time. */
        const unsigned ah = c->r[R_AX] >> 8;
        if (vec != 0x20 && (ah == 0x0B || ah == 0x2C)) return dos_int_hook(c, vec);
        static int reports;
        if (reports++ < 4) fprintf(stderr, "[rerun] blocked DOS vector %02X AX %04X at %04X:%04X\n", vec, c->r[R_AX], c->op_cs, c->op_ip);
        g_blocked = 1;
        machine_of(c)->exited = 1;
        c->stop_at = 0;
        return 1;
    }
    return dos_int_hook(c, vec);
}

static uint16_t rd16(const uint8_t *mem, uint16_t seg, uint16_t off)
{
    const uint32_t a = ((uint32_t)seg << 4) + off;
    return (uint16_t)(mem[a & 0xFFFFF] | mem[(a + 1) & 0xFFFFF] << 8);
}

static void wr16(uint8_t *mem, uint16_t seg, uint16_t off, uint16_t v)
{
    const uint32_t a = ((uint32_t)seg << 4) + off;
    mem[a & 0xFFFFF] = (uint8_t)v;
    mem[(a + 1) & 0xFFFFF] = (uint8_t)(v >> 8);
}

static uint32_t rd32(const uint8_t *mem, uint16_t seg, uint16_t off)
{
    return rd16(mem, seg, off) | (uint32_t)rd16(mem, seg, (uint16_t)(off + 2)) << 16;
}

static void wr32(uint8_t *mem, uint16_t seg, uint16_t off, uint32_t v)
{
    wr16(mem, seg, off, (uint16_t)v);
    wr16(mem, seg, (uint16_t)(off + 2), (uint16_t)(v >> 16));
}

static int32_t lerp_i(int32_t d, double t) { const double x = d * t; return (int32_t)(x < 0 ? x - 0.5 : x + 0.5); }

/* a 16-bit value moved t of the way along the signed (wrapping) difference */
static void nudge16(uint8_t *dst, uint16_t ds, const uint8_t *amem, const uint8_t *bmem, uint16_t bds, uint16_t at, double t)
{
    const uint16_t va = rd16(amem, ds, at), vb = rd16(bmem, bds, at);
    wr16(dst, ds, at, (uint16_t)(va + lerp_i((int16_t)(uint16_t)(vb - va), t)));
}

static uint32_t nudge32(uint8_t *dst, uint16_t ds, const uint8_t *amem, const uint8_t *bmem, uint16_t bds, uint16_t at, double t)
{
    const uint32_t va = rd32(amem, ds, at), vb = rd32(bmem, bds, at);
    const uint32_t v = va + (uint32_t)lerp_i((int32_t)(vb - va), t);
    wr32(dst, ds, at, v);
    return v;
}

static int16_t sdiff(uint16_t a, uint16_t b) { return (int16_t)(uint16_t)(b - a); }

/* An orientation (heading, pitch and roll, 16-bit angles of a full turn) moved t of the way. The game keeps pitch within
 * a quarter turn, so a model that pitches through the vertical is stored as the same orientation spelled with heading
 * and roll half a turn away and the pitch reflected (psi + 8000h, 8000h - theta, phi + 8000h): each step then changes
 * heading and roll by nearly half a turn while the model barely moves. Moving each angle by its own shortest difference
 * would twist the model half a turn about the vertical between the two steps, so b is taken in whichever of the two
 * spellings is nearer a. */
static void nudge_att(uint8_t *dst, uint16_t ds, const uint8_t *amem, const uint8_t *bmem, uint16_t bds,
                      uint16_t hdg, uint16_t pit, uint16_t rol, double t)
{
    const uint16_t ha = rd16(amem, ds, hdg), pa = rd16(amem, ds, pit), ra = rd16(amem, ds, rol);
    uint16_t hb = rd16(bmem, bds, hdg), pb = rd16(bmem, bds, pit), rb = rd16(bmem, bds, rol);
    const int direct = abs(sdiff(ha, hb)) + abs(sdiff(pa, pb)) + abs(sdiff(ra, rb));
    const uint16_t h2 = (uint16_t)(hb + 0x8000), p2 = (uint16_t)(0x8000 - pb), r2 = (uint16_t)(rb + 0x8000);
    if (abs(sdiff(ha, h2)) + abs(sdiff(pa, p2)) + abs(sdiff(ra, r2)) < direct) { hb = h2; pb = p2; rb = r2; }
    wr16(dst, ds, hdg, (uint16_t)(ha + lerp_i(sdiff(ha, hb), t)));
    wr16(dst, ds, pit, (uint16_t)(pa + lerp_i(sdiff(pa, pb), t)));
    wr16(dst, ds, rol, (uint16_t)(ra + lerp_i(sdiff(ra, rb), t)));
}

/* The inputs the composition reads, moved t of the way: the aircraft (32-bit X/Y at BA3C/C056; heading, pitch, roll,
 * height above ground and altitude at 2DEE..2DF6); the computer-flown aircraft (VGAME 5852: 24h-byte slots at C16A,
 * [DEFC] of them; +2/+4 the X/Y at 1/32 of +8/+0C, +6 height, +10/+12/+14 attitude moved as one orientation, only a
 * slot of the same type and state in both); the weapons in flight (VGAME 683E: twelve 1Ch-byte records at 3C3A;
 * +0/+2/+4 X/Y/height, +6 speed, +8/+0A/+0C heading/pitch/roll, +0E life, +10 type, only one alive with the same type
 * in both). */
static void nudge(uint8_t *dst, uint16_t ds, const uint8_t *amem, const uint8_t *bmem, uint16_t bds, double t)
{
    nudge32(dst, ds, amem, bmem, bds, 0xBA3C, t);
    nudge32(dst, ds, amem, bmem, bds, 0xC056, t);
    static const uint16_t own[] = { 0x2DEE, 0x2DF0, 0x2DF2, 0x2DF4, 0x2DF6 };
    for (int k = 0; k < 5; k++) nudge16(dst, ds, amem, bmem, bds, own[k], t);
    const uint16_t n = rd16(amem, ds, 0xDEFC);
    if (n == rd16(bmem, bds, 0xDEFC))
        for (uint16_t i = 0; i < n && i < 64; i++) {
            const uint16_t at = (uint16_t)(0xC16A + 0x24 * i);
            if (rd16(amem, ds, (uint16_t)(at + 0x16)) != rd16(bmem, bds, (uint16_t)(at + 0x16)) ||
                rd16(amem, ds, (uint16_t)(at + 0x18)) != rd16(bmem, bds, (uint16_t)(at + 0x18))) continue;
            const uint32_t x = nudge32(dst, ds, amem, bmem, bds, (uint16_t)(at + 0x08), t);
            const uint32_t y = nudge32(dst, ds, amem, bmem, bds, (uint16_t)(at + 0x0C), t);
            wr16(dst, ds, (uint16_t)(at + 2), (uint16_t)(x >> 5));
            wr16(dst, ds, (uint16_t)(at + 4), (uint16_t)(y >> 5));
            nudge16(dst, ds, amem, bmem, bds, (uint16_t)(at + 0x06), t);
            nudge_att(dst, ds, amem, bmem, bds, (uint16_t)(at + 0x10), (uint16_t)(at + 0x12), (uint16_t)(at + 0x14), t);
        }
    for (int i = 0; i < 12; i++) {
        const uint16_t at = (uint16_t)(0x3C3A + 0x1C * i);
        if (!rd16(amem, ds, (uint16_t)(at + 0x0E)) || !rd16(bmem, bds, (uint16_t)(at + 0x0E)) ||
            rd16(amem, ds, (uint16_t)(at + 0x10)) != rd16(bmem, bds, (uint16_t)(at + 0x10))) continue;
        for (int k = 0; k < 3; k++) nudge16(dst, ds, amem, bmem, bds, (uint16_t)(at + 2 * k), t);
        nudge_att(dst, ds, amem, bmem, bds, (uint16_t)(at + 0x08), (uint16_t)(at + 0x0A), (uint16_t)(at + 0x0C), t);
    }
}

static rerun_collector g_collector;
void rerun_set_collector(const rerun_collector *c)
{
    if (c) g_collector = *c; else memset(&g_collector, 0, sizeof g_collector);
}

/* The widened view for the margins (docs/remastered-design.md, step 4). VGAME's cull (0x0E3AC, which calls 0x0E3BF) rejects an object whose
 * view-space |x'| exceeds 1.25 x ((|z'| + T) >> cl), T the side allowance of the mode, table[mode] at [E56A] (eight words). T is all the
 * allowance the game makes for an object's own size, so a hangar was dropped as soon as its centre crossed the original screen's edge.
 * In the copy only, every T is raised to 18,000 (the sum with |z'| must stay under 0x7FFF: the far plane is 13,332), which every caller
 * of the cull sees (it is reached by direct calls in compiled code as well as through its entry, where a hook would have missed it). */
static int g_wide_pct = 100;
void rerun_set_wide(int percent) { g_wide_pct = percent < 100 ? 100 : percent > 400 ? 400 : percent; }

/* Aircraft (the parked jets, the other planes) are tested by their own routine, VGAME 0xB5DC, before any model is asked for: it puts the
 * aircraft's position into view space with the matrix at DS:[B082..] (row a of it: the words at B082 + 2a, B088 + 2a, B08E + 2a) and keeps
 * it while |x'| is within about |z'|, a 45-degree cone, so a jet in the wide margins is never drawn. The aircraft loop reaches 0xB5DC
 * through the run loop, so an exec hook sees it: at the first call of a copy's frame it divides the x row by the view's width factor,
 * which widens that cone by the same factor (the aircraft's own drawing does not read that row; its position on the 320-pixel screen,
 * [4A10], is off in the copy, which only the margins see). */
/* Scenery distance. The terrain walk (VGAME 04E6) draws, on each of four rings, only nine cells of a 4 x 4 block around the viewer's own cell
 * (the sector table at [034C], offsets -1..2). The rings are four grids over the one static world: ring 4 the coarsest (an 8 x 8 map of cells
 * 262,144 units wide), ring 1 the finest (a 256 x 256 map of cells 4,096 units wide, from 0810's scales 1, 4, 16 and 64), and each object
 * lives on exactly one ring by size, so the small things (lit windows, roofs, patches, the radar dish) show only a few cells out. The
 * guest keeps a cell's position as a 16-bit (offset << 12), so an offset beyond -8..7 cells wraps: that is the far limit of any ring
 * (28,672 units on ring 1, 114,688 on ring 2, 458,752 on ring 3, the whole map on ring 4), and why shifting the table six or twelve
 * cells drew nothing. The copy's extra walks therefore feed the walker explicit cells: row r of the table is the X offsets of sector r and the
 * Y offsets of sector r + 2 (nine pairs), so a pass writes up to nine chosen (dx, dy) offsets into the rows of the current sector, enables
 * one ring, and calls 04E6 with all eight original words (pitch forced level, because a steep downward pitch reads the near tables [04CC] instead).
 * The guest's own cull (E3AC, E3BF) and tile lookup (0871) then decide what is seen; cells the game's own call draws are skipped. The far plane
 * [48E6] is raised and the side allowances lowered so their sum with the depth stays under 7FFFh. The reach is adjustable:
 * rerun_set_distance (world units; the menu's Draw distance) or F117_WIDE_DISTANCE; 0 turns the extra walks off. See docs/scenery-distance.md. */
/* The terrain map, read natively (VGAME 0871 and 0810, matched in matched.c): which tile a cell holds on a ring, and whether it holds anything.
 * Level 4 is an 8-wide byte map at DS:B03C (cells offset by 2), level 3 a 16-wide one at 9F98; levels 2, 1 and 0 read a 4 x 4 block of their parent
 * at 9D40, 9B38 and 9934. A cell outside [0506 + 2 level] answers tile 0, which is a real tile: the guest would draw tile 0's objects there, so
 * the extra walks never ask for such a cell. A tile's object count is [061E + 2 (32 ring + tile)]; a cell with none draws nothing. */
static int cell_tile(const machine_t *m, uint16_t ds, int level, int x, int y)
{
    if (level == 4) { x += 2; y += 2; }
    if (x < 0 || y < 0 || x >= (int16_t)rd16(m->mem, ds, (uint16_t)(0x506 + 2 * level)) || y >= (int16_t)rd16(m->mem, ds, (uint16_t)(0x506 + 2 * level))) return -1;
    if (level == 4) return m->mem[(((uint32_t)ds << 4) + (uint16_t)(0xB03C + x + y * 8)) & 0xFFFFF];
    if (level == 3) return m->mem[(((uint32_t)ds << 4) + (uint16_t)(0x9F98 + x + y * 16)) & 0xFFFFF];
    static const uint16_t block[3] = { 0x9934, 0x9B38, 0x9D40 };
    const int parent = cell_tile(m, ds, level + 1, x >> 2, y >> 2);
    if (parent < 0) return 0;
    return m->mem[(((uint32_t)ds << 4) + (uint16_t)(block[level] + parent * 16 + (y & 3) * 4 + (x & 3))) & 0xFFFFF];
}
/* The viewer's cell on a ring: 0810's scale of a position long (level 1 as it is, 2 to 4 divided by 4, 16, 64 to nearest), shifted down by 12. */
static int64_t cell_scaled(int level, uint32_t v)
{
    static const int shift[5] = { 0, 0, 2, 4, 6 };
    int64_t s = (int32_t)v;
    if (shift[level]) s = (s + ((int64_t)1 << (shift[level] - 1))) >> shift[level];
    return s;
}
static int cell_base(int level, uint32_t v) { return (int)(cell_scaled(level, v) >> 12); }
extern int remaster_prim_source;     /* remaster_scene.c: which placement is emitting primitives (diagnostic, F117_PRIM_HIST) */
static int g_walk_k = -1, g_walk_busy;
static double walk_ms(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    return (double)clock() * 1000.0 / CLOCKS_PER_SEC;
#endif
}
static int g_distance = 0;                           /* the Near setting: far scenery one scale coarser (4 times the reach). Far (20000) is two, Maximum (1 << 22) three: the whole map */
void rerun_set_distance(int units) { g_distance = units < 0 ? 0 : units > (1 << 26) ? (1 << 26) : units; }
static unsigned g_walk_calls;
enum { PROF_MARKS = 8 };
static int g_prof = -1;
static uint64_t g_prof_ic[PROF_MARKS]; static double g_prof_ms[PROF_MARKS]; static int g_prof_set[PROF_MARKS];
static machine_t *g_prof_m;
static double walk_ms(void);
void rerun_prof_mark(int label)
{
    if (g_prof <= 0 || label < 0 || label >= PROF_MARKS || !g_prof_m) return;
    g_prof_ic[label] = g_prof_m->cpu.icount; g_prof_ms[label] = walk_ms(); g_prof_set[label] = 1;
}
F117_TLS static int g_coarse_busy;
int rerun_coarse_active(void) { return g_coarse_busy; }
int rerun_extra_active(void) { return g_walk_busy; }
static int g_fine2 = 1, g_fine2_test;                    /* ring 1 past its reach, drawn at ring 2's scale; test: no ring-1 passes, all children coarse */
static int walk_second(machine_t *m)
{
    cpu_t *c = &m->cpu;
    static int env_done;
    if (!env_done) {
        env_done = 1;
        if (getenv("F117_FINE2_OFF")) g_fine2 = 0;
        if (getenv("F117_FINE2_TEST")) g_fine2_test = 1;
        const char *e = getenv("F117_WIDE_DISTANCE");
        if (e) g_distance = atoi(e) < 0 ? 0 : atoi(e);
        const char *s = getenv("F117_WIDE_SCENERY");
        if (s && atoi(s) == 0) g_distance = 0;
    }
    if (g_distance <= 0) return 0;
    if (getenv("F117_FINE_DIAG")) {
        static int once;
        if (!once) {
            once = 1;
            const uint16_t dsd = c->seg[S_DS];
            for (int r = 1; r <= 4; r++) {
                unsigned lo = 0xFFFF, hi = 0, tot = 0, used = 0;
                for (int t = 0; t < 32; t++) {
                    const unsigned p = rd16(m->mem, dsd, (uint16_t)(0xE330 + 2 * (r * 32 + t))), n = rd16(m->mem, dsd, (uint16_t)(0x061E + 2 * (r * 32 + t)));
                    if (n) { used++; if (p < lo) lo = p; if (p + 7 * n > hi) hi = p + 7 * n; tot += n; }
                }
                fprintf(stderr, "[fine] ring %d: %u tiles used, %u entries, lists %04X..%04X\n", r, used, tot, lo, hi);
            }
            unsigned best = 0, best_at = 0, run = 0;
            for (unsigned a = 0; a < 0x10000; a++) {
                if (m->mem[(((uint32_t)dsd << 4) + a) & 0xFFFFF] == 0) { if (++run > best) { best = run; best_at = a - run + 1; } } else run = 0;
            }
            fprintf(stderr, "[fine] DS %04X SS %04X SP %04X; longest zero run %u at %04X\n", dsd, c->seg[S_SS], c->r[R_SP], best, best_at);
            for (int t = 0; t < 4; t++) fprintf(stderr, "[fine] ring1 tile %d ptr %04X n %u first entry %04X %04X %04X %02X\n", t,
                rd16(m->mem, dsd, (uint16_t)(0xE330 + 2 * (32 + t))), rd16(m->mem, dsd, (uint16_t)(0x061E + 2 * (32 + t))),
                rd16(m->mem, dsd, rd16(m->mem, dsd, (uint16_t)(0xE330 + 2 * (32 + t)))), rd16(m->mem, dsd, (uint16_t)(rd16(m->mem, dsd, (uint16_t)(0xE330 + 2 * (32 + t))) + 2)),
                rd16(m->mem, dsd, (uint16_t)(rd16(m->mem, dsd, (uint16_t)(0xE330 + 2 * (32 + t))) + 4)), m->mem[(((uint32_t)dsd << 4) + (uint16_t)(rd16(m->mem, dsd, (uint16_t)(0xE330 + 2 * (32 + t))) + 6)) & 0xFFFFF]);
        }
    }
    const uint64_t icount_before = c->icount;
    const double ms_before = walk_ms();
    int passes = 0;
    static double ring_ins[5], ring_cells[5], ring_passes[5], ring_ms[5]; static unsigned ring_n;
    const size_t values_before = g_out ? g_out->nvals : 0;
    const uint16_t ds = c->seg[S_DS], ss = c->seg[S_SS];
    uint16_t arguments[8];                                              /* 04E6's eight words: heading, pitch, three world-coordinate longs */
    for (int i = 0; i < 8; i++) arguments[i] = rd16(m->mem, ss, (uint16_t)(c->r[R_SP] + 2 + 2 * i));
    arguments[1] = 0;                                                    /* level: a steep downward pitch would read the near tables [04CC] instead */
    static int far_plane = -1;
    if (far_plane < 0) { const char *e = getenv("F117_WIDE_SCENERY_FAR"); far_plane = e ? atoi(e) : 30000; if (far_plane > 32000) far_plane = 32000; }
    uint16_t table[72], enable[5], allow[8], far_was;
    const uint16_t base = rd16(m->mem, ds, 0xE56A);
    for (int i = 0; i < 72; i++) table[i] = rd16(m->mem, ds, (uint16_t)(0x034C + 2 * i));
    for (int r = 1; r <= 4; r++) enable[r] = rd16(m->mem, ds, (uint16_t)(0x04FA + 2 * r));
    for (int md = 0; md < 8; md++) allow[md] = rd16(m->mem, ds, (uint16_t)(base + 2 * md));
    far_was = rd16(m->mem, ds, 0x48E6);
    const unsigned sector = ((uint16_t)(0x1000 - arguments[0])) >> 13, sector_y = (sector + 2) & 7;
    uint16_t sv_r[8], sv_seg[4];
    for (int i = 0; i < 8; i++) sv_r[i] = c->r[i];
    for (int i = 0; i < 4; i++) sv_seg[i] = c->seg[i];
    const uint16_t sv_ip = c->ip, sv_fl = c->flags;
    const uint8_t on = m->trap_on;
    const uint16_t tcs = m->trap_cs, tip = m->trap_ip, tsp = m->trap_sp;
    const uint64_t until = m->run_until, stop = c->stop_at;
    wr16(m->mem, ds, 0x48E6, (uint16_t)far_plane);
    for (int md = 0; md < 8; md++) if (allow[md] > 32600 - far_plane) wr16(m->mem, ds, (uint16_t)(base + 2 * md), (uint16_t)(32600 - far_plane));
    int bad = 0;
    for (int ring = 4; ring >= 1; ring--) {
        const uint64_t ring_ic = c->icount; const int ring_p0 = passes; const double ring_t0 = walk_ms();
        const long cell = 4096L << (2 * (ring - 1));              /* a cell's width in world units on this ring */
        int radius = (int)((g_distance + cell - 1) / cell);
        if (radius > 7) radius = 7;                               /* the 16-bit limit of a cell offset */
        if (radius < 2) radius = 2;
        for (int r = 1; r <= 4; r++) wr16(m->mem, ds, (uint16_t)(0x04FA + 2 * r), r == ring ? enable[r] : 0);
        if (!enable[ring]) continue;
        if (ring == 1 && g_fine2_test) continue;
        const int base_x = cell_base(ring, arguments[2] | (uint32_t)arguments[3] << 16), base_y = cell_base(ring, arguments[4] | (uint32_t)arguments[5] << 16);
        int dx[256], dy[256], n = 0;                              /* outward from the viewer's cell, skipping the nine the game's own call draws */
        for (int k = 0; k <= radius; k++)
            for (int y = -k; y <= k; y++)
                for (int x = -k; x <= k; x++) {
                    if ((x < 0 ? -x : x) != k && (y < 0 ? -y : y) != k) continue;
                    int own = 0;
                    for (int i = 0; i < 9; i++) if ((int16_t)table[sector * 9 + i] == x && (int16_t)table[sector_y * 9 + i] == y) own = 1;
                    if (own || n >= 256) continue;
                    const int t = cell_tile(m, ds, ring, base_x + x, base_y + y);       /* outside the map, or no objects there: nothing to draw */
                    if (t < 0 || rd16(m->mem, ds, (uint16_t)(0x061E + 2 * (ring * 32 + t))) == 0) continue;
                    dx[n] = x; dy[n] = y; n++;
                }
        int pad_x = 0, pad_y = 0, have_pad = 0;                  /* an in-map cell with no objects fills the last pass (an outside cell would answer tile 0) */
        for (int y = -7; y <= 7 && !have_pad; y++)
            for (int x = -7; x <= 7 && !have_pad; x++) {
                const int t = cell_tile(m, ds, ring, base_x + x, base_y + y);
                if (t >= 0 && rd16(m->mem, ds, (uint16_t)(0x061E + 2 * (ring * 32 + t))) == 0) { pad_x = x; pad_y = y; have_pad = 1; }
            }
        for (int at = 0; at < n; at += 9) {
            for (int i = 0; i < 9; i++) {
                const int j = at + i < n ? at + i : (have_pad ? -1 : at);   /* (no empty cell anywhere: the pass's first cell again) */
                wr16(m->mem, ds, (uint16_t)(0x034C + 2 * (sector * 9 + i)), (uint16_t)(int16_t)(j >= 0 ? dx[j] : pad_x));
                wr16(m->mem, ds, (uint16_t)(0x034C + 2 * (sector_y * 9 + i)), (uint16_t)(int16_t)(j >= 0 ? dy[j] : pad_y));
            }
            for (int i = 0; i < 8; i++) c->r[i] = sv_r[i];
            for (int i = 0; i < 4; i++) c->seg[i] = sv_seg[i];
            c->flags = sv_fl;
            for (int i = 7; i >= 0; i--) cpu_push16(c, arguments[i]);
            cpu_push16(c, 0x04E6);                                    /* the return address: never run, the trap stops at it */
            c->ip = 0x04E6;
            m->trap_on = 1; m->trap_cs = c->seg[S_CS]; m->trap_ip = 0x04E6; m->trap_sp = (uint16_t)(c->r[R_SP] + 2);
            g_walk_busy = 1;
            int rc = RUN_SLICE;
            const uint64_t limit = c->icount + 200000000ull;          /* a throwaway copy: no slice to keep, only a bound */
            while (rc == RUN_SLICE && c->icount < limit) rc = machine_run(m, limit);
            g_walk_busy = 0;
            if (rc != RUN_TRAP) bad++;
            passes++;
        }
        ring_ms[ring] += walk_ms() - ring_t0; ring_ins[ring] += (double)(c->icount - ring_ic); ring_cells[ring] += n; ring_passes[ring] += passes - ring_p0;
    }
    if (enable[1] && enable[2] && g_fine2) {
        /* Ring 1's small objects past its own 16-bit reach, drawn through ring 2's coarser scale (a quarter of the units, so four times the
         * range). The ring-2 cell is a 4 x 4 block of ring-1 cells (block table 9B38), so for each ring-2 cell a merged tile list is built in
         * scratch memory from its ring-1 children: each entry's x and y moved by the child's place in the block and all three divided by 4. The
         * ring-2 slot of that cell's tile is pointed at the list for the pass and put back after. The children the ring-1 passes already draw
         * (within r1 cells of the viewer) are left out, so every object is drawn once. */
        const int ring1_cells = (int)((g_distance + 4095) / 4096);
        int r1 = ring1_cells > 7 ? 7 : ring1_cells; if (r1 < 2) r1 = 2;
        const int b1x = cell_base(1, arguments[2] | (uint32_t)arguments[3] << 16), b1y = cell_base(1, arguments[4] | (uint32_t)arguments[5] << 16);
        const int b2x = cell_base(2, arguments[2] | (uint32_t)arguments[3] << 16), b2y = cell_base(2, arguments[4] | (uint32_t)arguments[5] << 16);
        enum { SCRATCH = 0x0F00, MAXITEMS = 225 };
        static unsigned scratch_at = 0xFFFFFFFFu;
        if (scratch_at == 0xFFFFFFFFu) {                          /* a run of zero bytes in the data segment, found once */
            unsigned run = 0; scratch_at = 0;
            for (unsigned a = 0; a < 0x10000; a++) {
                if (m->mem[(((uint32_t)ds << 4) + a) & 0xFFFFF] == 0) { if (++run >= SCRATCH + 16) { scratch_at = a - run + 1 + 8; break; } } else run = 0;
            }
        }
        if (scratch_at) {
            uint8_t keep[SCRATCH];
            const double cb0 = walk_ms();
            for (unsigned k = 0; k < SCRATCH; k++) keep[k] = m->mem[(((uint32_t)ds << 4) + scratch_at + k) & 0xFFFFF];
            struct item { int pdx, pdy, t2, at, n; } items[MAXITEMS]; int nitems = 0;
            int pad_x = 0, pad_y = 0, have_pad = 0, pad_t = 0;
            unsigned used = 0;
            for (int k = 0; k <= 7; k++)
                for (int py = -k; py <= k; py++)
                    for (int px = -k; px <= k; px++) {
                        if ((px < 0 ? -px : px) != k && (py < 0 ? -py : py) != k) continue;
                        const int t2 = cell_tile(m, ds, 2, b2x + px, b2y + py);
                        if (t2 < 0 || nitems >= MAXITEMS) continue;
                        struct { int16_t x, y, z; uint8_t type; } list[160]; int ln = 0;
                        for (int j = 0; j < 4; j++)
                            for (int i = 0; i < 4; i++) {
                                const int fx = (b2x + px) * 4 + i, fy = (b2y + py) * 4 + j;
                                const int dxc = fx - b1x, dyc = fy - b1y, d = (dxc < 0 ? -dxc : dxc) > (dyc < 0 ? -dyc : dyc) ? (dxc < 0 ? -dxc : dxc) : (dyc < 0 ? -dyc : dyc);
                                if ((!g_fine2_test && d <= r1) || d > ring1_cells) continue;
                                const int t1 = cell_tile(m, ds, 1, fx, fy);
                                if (t1 < 0) continue;
                                const unsigned cnt = rd16(m->mem, ds, (uint16_t)(0x061E + 2 * (32 + t1))), p = rd16(m->mem, ds, (uint16_t)(0xE330 + 2 * (32 + t1)));
                                for (unsigned e = 0; e < cnt && ln < 160; e++) {
                                    const uint16_t a = (uint16_t)(p + 7 * e);
                                    list[ln].x = (int16_t)(((int16_t)rd16(m->mem, ds, a) + 4096 * i - 6144 + 2) >> 2);
                                    list[ln].y = (int16_t)(((int16_t)rd16(m->mem, ds, (uint16_t)(a + 2)) + 4096 * j - 6144 + 2) >> 2);
                                    list[ln].z = (int16_t)(((int16_t)rd16(m->mem, ds, (uint16_t)(a + 4)) + 2) >> 2);
                                    list[ln].type = m->mem[(((uint32_t)ds << 4) + (uint16_t)(a + 6)) & 0xFFFFF];
                                    ln++;
                                }
                            }
                        if (!ln || used + 7u * ln > SCRATCH) continue;
                        for (int e = 0; e < ln; e++) {
                            const uint16_t a = (uint16_t)(scratch_at + used + 7 * e);
                            wr16(m->mem, ds, a, (uint16_t)list[e].x); wr16(m->mem, ds, (uint16_t)(a + 2), (uint16_t)list[e].y);
                            wr16(m->mem, ds, (uint16_t)(a + 4), (uint16_t)list[e].z); m->mem[(((uint32_t)ds << 4) + (uint16_t)(a + 6)) & 0xFFFFF] = list[e].type;
                        }
                        items[nitems].pdx = px; items[nitems].pdy = py; items[nitems].t2 = t2; items[nitems].at = (int)(scratch_at + used); items[nitems].n = ln;
                        used += 7u * ln; nitems++;
                    }
            unsigned item_tiles = 0;
            for (int q = 0; q < nitems; q++) item_tiles |= 1u << items[q].t2;
            for (int y = -7; y <= 7 && !have_pad; y++)           /* an empty in-map cell whose tile no item uses fills the last pass */
                for (int x = -7; x <= 7 && !have_pad; x++) {
                    const int t = cell_tile(m, ds, 2, b2x + x, b2y + y);
                    if (t >= 0 && !(item_tiles >> t & 1) && rd16(m->mem, ds, (uint16_t)(0x061E + 2 * (64 + t))) == 0) { pad_x = x; pad_y = y; pad_t = t; have_pad = 1; }
                }
            { static double acc; static unsigned nn; acc += walk_ms() - cb0; if (++nn % 300 == 0 && getenv("F117_WIDE_WALK_STATS")) fprintf(stderr, "[walk]   coarse list build: %.2f ms a copy\n", acc / nn); }
            /* the passes: up to nine cells with distinct tiles (the slot is by tile), the rest padded with the empty cell */
            int done[MAXITEMS] = { 0 }, left = nitems;
            for (int r = 1; r <= 4; r++) wr16(m->mem, ds, (uint16_t)(0x04FA + 2 * r), r == 2 ? enable[r] : 0);
            while (left > 0) {
                int pick[9], np = 0; unsigned tiles = 0;
                for (int q = 0; q < nitems && np < 9; q++) if (!done[q] && !(tiles >> items[q].t2 & 1)) { pick[np++] = q; tiles |= 1u << items[q].t2; done[q] = 1; left--; }
                if (!np) break;
                uint16_t sp[9], sn[9];
                for (int i = 0; i < 9; i++) {
                    const int q = i < np ? pick[i] : (have_pad ? -1 : pick[0]);
                    wr16(m->mem, ds, (uint16_t)(0x034C + 2 * (sector * 9 + i)), (uint16_t)(int16_t)(q >= 0 ? items[q].pdx : pad_x));
                    wr16(m->mem, ds, (uint16_t)(0x034C + 2 * (sector_y * 9 + i)), (uint16_t)(int16_t)(q >= 0 ? items[q].pdy : pad_y));
                }
                for (int i = 0; i < np; i++) {
                    const uint16_t sl = (uint16_t)(2 * (64 + items[pick[i]].t2));
                    sp[i] = rd16(m->mem, ds, (uint16_t)(0xE330 + sl)); sn[i] = rd16(m->mem, ds, (uint16_t)(0x061E + sl));
                    wr16(m->mem, ds, (uint16_t)(0xE330 + sl), (uint16_t)items[pick[i]].at); wr16(m->mem, ds, (uint16_t)(0x061E + sl), (uint16_t)items[pick[i]].n);
                }
                for (int i = 0; i < 8; i++) c->r[i] = sv_r[i];
                for (int i = 0; i < 4; i++) c->seg[i] = sv_seg[i];
                c->flags = sv_fl;
                for (int i = 7; i >= 0; i--) cpu_push16(c, arguments[i]);
                cpu_push16(c, 0x04E6);
                c->ip = 0x04E6;
                m->trap_on = 1; m->trap_cs = c->seg[S_CS]; m->trap_ip = 0x04E6; m->trap_sp = (uint16_t)(c->r[R_SP] + 2);
                g_walk_busy = 1; g_coarse_busy = 1;
                int rc = RUN_SLICE;
                const uint64_t limit = c->icount + 200000000ull;
                while (rc == RUN_SLICE && c->icount < limit) rc = machine_run(m, limit);
                g_walk_busy = 0; g_coarse_busy = 0;
                if (rc != RUN_TRAP) bad++;
                passes++;
                for (int i = 0; i < np; i++) {
                    const uint16_t sl = (uint16_t)(2 * (64 + items[pick[i]].t2));
                    wr16(m->mem, ds, (uint16_t)(0xE330 + sl), sp[i]); wr16(m->mem, ds, (uint16_t)(0x061E + sl), sn[i]);
                }
            }
            for (unsigned k = 0; k < SCRATCH; k++) m->mem[(((uint32_t)ds << 4) + scratch_at + k) & 0xFFFFF] = keep[k];
            if (getenv("F117_FINE_DIAG")) fprintf(stderr, "[fine] coarse: %d cells, %u scratch bytes\n", nitems, used);
        }
    }
    ring_n++;
    if (getenv("F117_WIDE_WALK_STATS")) { static unsigned mx, calls; const unsigned d = rd16(m->mem, ds, 0x49F4); if (d > mx) mx = d; if (++calls % 300 == 0) fprintf(stderr, "[walk] deferred list after the extra walks: %u of 50 now, %u at most\n", d, mx); }
    m->trap_on = on; m->trap_cs = tcs; m->trap_ip = tip; m->trap_sp = tsp; m->run_until = until; c->stop_at = stop;
    for (int i = 0; i < 72; i++) wr16(m->mem, ds, (uint16_t)(0x034C + 2 * i), table[i]);
    for (int r = 1; r <= 4; r++) wr16(m->mem, ds, (uint16_t)(0x04FA + 2 * r), enable[r]);
    for (int md = 0; md < 8; md++) wr16(m->mem, ds, (uint16_t)(base + 2 * md), allow[md]);
    wr16(m->mem, ds, 0x48E6, far_was);
    if (bad) fprintf(stderr, "[wide] %d extra terrain walks did not return\n", bad);
    {   /* F117_WIDE_WALK_STATS: what the extra walks cost, every 300th copy (stderr) */
        static int stats = -1; static double ms_sum, ins_sum, val_sum, pass_sum; static unsigned n;
        if (stats < 0) stats = getenv("F117_WIDE_WALK_STATS") != NULL;
        if (stats) {
            ms_sum += walk_ms() - ms_before; ins_sum += (double)(c->icount - icount_before); pass_sum += passes; val_sum += (double)((g_out ? g_out->nvals : 0) - values_before);
            if (++n % 300 == 0) {
                fprintf(stderr, "[walk] mean of %u: %.2f ms, %.0f instructions, %.1f passes, %.0f values\n", n, ms_sum / n, ins_sum / n, pass_sum / n, val_sum / n);
                for (int r = 4; r >= 1; r--) fprintf(stderr, "[walk]   ring %d: %.0f cells, %.1f passes, %.0f instructions, %.2f ms\n", r, ring_cells[r] / ring_n, ring_passes[r] / ring_n, ring_ins[r] / ring_n, ring_ms[r] / ring_n);
            }
        }
    }
    for (int i = 0; i < 8; i++) c->r[i] = sv_r[i];
    for (int i = 0; i < 4; i++) c->seg[i] = sv_seg[i];
    c->ip = sv_ip; c->flags = sv_fl;
    return 0;
}

/* ---- Native far scenery ------------------------------------------------------------------------------------------------------------
 * The extra terrain walks above feed the game's own walker explicit cells. The walker keeps a cell's position as a 16-bit word in its ring's
 * units, which is the limit of a ring's reach, and it repeats for every in-between frame the whole per-cell and per-object setup of objects that
 * never move (about four fifths of its placements are culled at once). This path does not use the walker. The terrain is static: once per
 * mission (keyed on the tile data) every object of every ring is listed with its position in its ring's units (cell * 4096 + 2048 + the
 * entry's offsets, the walker's own sum); each copy then, for every object, takes the finest scale (ring 1 .. 4: the unit grows by 4 each time)
 * at which its offset from the eye fits the game's 16-bit camera-relative words, runs the game's own camera transform and cull (VGAME E3BF,
 * ported exactly) on it, and hands only the survivors to the game's placement routine (E2B6) with the scale in [E328] and the eye's position
 * in the same units in [49AC..49B0]: exactly what the walker leaves there. The renderer shrinks a model drawn at a coarser scale than its
 * own ring by the same factor (rerun_scale_shift). Objects the game's own call draws (its nine cells of a ring, where the ring is drawn at
 * all) are left to it. The copy only: the live machine is never touched. */
typedef struct { int32_t x, y, z; int16_t cx, cy; uint8_t ring, type, entry; } far_inst;
static far_inst *g_inst; static size_t g_ninst, g_cap_inst; static uint64_t g_world_key;
/* The objects are listed cell by cell (row-major per ring), so a window of cells is a few runs of the list: cell_first[ring][row * side + column] is where a cell's
 * objects start, with one more entry at the end. */
static uint32_t *g_cell_first[5]; static int g_cell_lo[5], g_cell_side[5];
F117_TLS static int g_scale_shift;
int rerun_scale_shift(void) { return g_scale_shift ? g_scale_shift : (g_coarse_busy ? 2 : 0); }

static uint64_t world_key(const machine_t *m, uint16_t ds)
{
    uint64_t h = 1469598103934665603ull;
#define MIX(v) (h = (h ^ (uint64_t)(uint8_t)(v)) * 1099511628211ull)
    for (int i = 0; i < 12; i++) MIX(m->mem[(((uint32_t)ds << 4) + 0x506 + i) & 0xFFFFF]);
    for (int r = 1; r <= 4; r++)
        for (int t = 0; t < 32; t++) {
            const unsigned slot = (unsigned)(r * 32 + t), n = rd16(m->mem, ds, (uint16_t)(0x061E + 2 * slot)), p = rd16(m->mem, ds, (uint16_t)(0xE330 + 2 * slot));
            MIX(n); MIX(n >> 8); MIX(p); MIX(p >> 8);
            for (unsigned k = 0; k < 7 * n; k++) MIX(m->mem[(((uint32_t)ds << 4) + (uint16_t)(p + k)) & 0xFFFFF]);
        }
    for (unsigned k = 0; k < 64; k++) MIX(m->mem[(((uint32_t)ds << 4) + (uint16_t)(0xB03C + k)) & 0xFFFFF]);
    for (unsigned k = 0; k < 256; k++) MIX(m->mem[(((uint32_t)ds << 4) + (uint16_t)(0x9F98 + k)) & 0xFFFFF]);
    static const uint16_t blocks[3] = { 0x9934, 0x9B38, 0x9D40 };
    for (int b = 0; b < 3; b++) for (unsigned k = 0; k < 0x204; k++) MIX(m->mem[(((uint32_t)ds << 4) + (uint16_t)(blocks[b] + k)) & 0xFFFFF]);
#undef MIX
    return h;
}

static void world_build(const machine_t *m, uint16_t ds)
{
    g_ninst = 0;
    for (int r = 1; r <= 4; r++) {
        const int size = (int16_t)rd16(m->mem, ds, (uint16_t)(0x506 + 2 * r));
        const int lo = r == 4 ? -2 : 0, hi = r == 4 ? size - 2 : size;       /* ring 4's map has a two-cell border (cell_tile adds 2) */
        const int side = hi - lo > 0 ? hi - lo : 0;
        free(g_cell_first[r]);
        g_cell_first[r] = (uint32_t *)calloc((size_t)side * side + 1, sizeof(uint32_t));
        g_cell_lo[r] = lo; g_cell_side[r] = g_cell_first[r] ? side : 0;
        for (int y = lo; y < hi; y++)
            for (int x = lo; x < hi; x++) {
                if (g_cell_first[r]) g_cell_first[r][(size_t)(y - lo) * side + (x - lo)] = (uint32_t)g_ninst;
                const int t = cell_tile(m, ds, r, x, y);
                if (t < 0) continue;
                const unsigned slot = (unsigned)(r * 32 + t), n = rd16(m->mem, ds, (uint16_t)(0x061E + 2 * slot)), p = rd16(m->mem, ds, (uint16_t)(0xE330 + 2 * slot));
                for (unsigned e = 0; e < n; e++) {
                    if (g_ninst == g_cap_inst) {
                        g_cap_inst = g_cap_inst ? g_cap_inst * 2 : 4096;
                        g_inst = (far_inst *)realloc(g_inst, g_cap_inst * sizeof *g_inst);
                        if (!g_inst) { g_cap_inst = g_ninst = 0; return; }
                    }
                    const uint16_t a = (uint16_t)(p + 7 * e);
                    far_inst *f = &g_inst[g_ninst++];
                    f->x = x * 4096 + 2048 + (int16_t)rd16(m->mem, ds, a);
                    f->y = y * 4096 + 2048 + (int16_t)rd16(m->mem, ds, (uint16_t)(a + 2));
                    f->z = (int16_t)rd16(m->mem, ds, (uint16_t)(a + 4));
                    f->cx = (int16_t)x; f->cy = (int16_t)y; f->ring = (uint8_t)r; f->entry = (uint8_t)e;
                    f->type = m->mem[(((uint32_t)ds << 4) + (uint16_t)(a + 6)) & 0xFFFFF];
                }
            }
        if (g_cell_first[r]) g_cell_first[r][(size_t)side * side] = (uint32_t)g_ninst;
    }
}

/* E3BF camera transform. Keep the game's 32-bit wrap before taking the
 * high word of the doubled dot product. */
static int16_t far_axis(const int16_t matrix[9], int axis, int16_t x, int16_t y, int16_t z)
{
    uint32_t a = (uint32_t)((int32_t)matrix[6 + axis] * y);
    a += (uint32_t)((int32_t)matrix[3 + axis] * z);
    a += (uint32_t)((int32_t)matrix[axis] * x);
    return (int16_t)((a << 1) >> 16);
}

/* Side allowances are in the object's own ring units. */
int rerun_why_model(unsigned model)               /* F117_FAR_WHY=M0,M1,...: is this model one of the listed (diagnostic) */
{
    static int init, n, list[8];
    if (!init) { init = 1; const char *e = getenv("F117_FAR_WHY"); for (const char *q = e; q && *q && n < 8; ) { list[n++] = atoi(q); const char *c2 = strchr(q, ','); q = c2 ? c2 + 1 : NULL; } }
    for (int i = 0; i < n; i++) if ((int)model == list[i]) return 1;
    return 0;
}
static int g_up_raised;                          /* the wide copy raised the up allowance table (F117_WIDE_UP_ALLOW), so far_cull clamps it to leave room for the depth */
static const uint16_t *g_allow_saved;           /* the across-test table as the copy had it (the wide copy raises it), before far_scenery lowers it for the coarser levels */
static F117_TLS int g_cull_reason;                       /* which check of the last far_cull call rejected the object (1 far plane, 2 near plane, 3 across, 4 up, 5 metric), diagnostic */
/* The environment switches far_cull and the choosing read, taken once before the choosing runs on several threads. */
static int why_on, nskip, skip, force_base;
static void far_cull_init(void)
{
    static int done;
    if (done) return;
    why_on = getenv("F117_CULL_WHY") != NULL;
    nskip = getenv("F117_CULL_SKIP_NEAR") != NULL;                       /* diagnostic: own-level objects ignore the per-class near plane */
    { const char *e = getenv("F117_CULL_SKIP"); skip = e ? atoi(e) : 0; }  /* diagnostic mask: 1 across test, 2 up test, 4 metric test (own-level objects only) */
    force_base = getenv("F117_NATIVE_FORCE_BASE") != NULL;               /* experiment, native-walk: ring 4's first entry of a tile is never culled */
    done = 1;
}
static int far_cull(const machine_t *m, uint16_t ds, uint16_t ss, const int16_t matrix[9],
                    int16_t x, int16_t y, int16_t z, int16_t zh, unsigned lod, int shift)
{
    static unsigned why[7];                  /* F117_CULL_WHY: which check first rejects an own-level object (shift 0), printed every 20000 */
#define CULL_FAIL(k) do { g_cull_reason = (k) + 1; if (why_on && shift == 0) { why[k]++; if ((why[0] + why[1] + why[2] + why[3] + why[4] + why[5] + why[6]) % 20000 == 0) fprintf(stderr, "[cull-why] own-level objects: far plane %u, near plane %u, across %u, up %u, metric %u, visible %u, early-visible %u\n", why[0], why[1], why[2], why[3], why[4], why[5], why[6]); } return 0; } while (0)
    if (zh > (int16_t)rd16(m->mem, ds, 0x48E6)) CULL_FAIL(0);
    const uint16_t lod2 = (uint16_t)(lod << 1);
    if (zh < (int16_t)rd16(m->mem, ds, (uint16_t)(lod2 + 0x48F6)) && !(nskip && shift == 0)) CULL_FAIL(1);
    if (rd16(m->mem, ds, 0x438E) != 0) return 1;
    const int16_t xh = far_axis(matrix, 0, x, y, z), yh = far_axis(matrix, 1, x, y, z);
    const int16_t di = (int16_t)(zh < 0 ? (uint16_t)(-zh) : (uint16_t)zh);   /* |z|, 8000h staying 8000h */
    const uint16_t t_across = (uint16_t)(rd16(m->mem, ds, 0xE56A) + lod2), t_up = (uint16_t)(rd16(m->mem, ds, 0xE570) + lod2);
    const unsigned cl = (m->mem[(((uint32_t)ds << 4) + 0x294B) & 0xFFFFF] ^ 1) & 31;
    /* Own-level objects in native-walk mode: the original table value, not the lowered one (the lowering keeps the coarser levels' sums under 8000h and would reject objects the game keeps). */
    const int16_t across_word = g_allow_saved && shift == 0 && lod < 8 ? (int16_t)g_allow_saved[lod] : (int16_t)rd16(m->mem, ds, t_across);
    int16_t ax = (int16_t)((int16_t)(di + (across_word >> shift)) >> cl);
    int16_t si = (int16_t)(ax + (ax >> 2));
    /* The game's cone (1.25 x the depth plus the allowance) is for its own 4:3 picture. An object drawn at a coarser level has only a small allowance
     * (the lowered table, shifted down), so its cone stays at the original's and a far light in the wide margins is dropped. Widen the cone by the
     * wide picture's factor for those objects (docs/owner-issues.md R30); F117_FAR_WIDE_ACROSS=0 restores the original cone. */
    static int widen = -1;
    if (widen < 0) { const char *e = getenv("F117_FAR_WIDE_ACROSS"); widen = e ? atoi(e) : 1; }
    if (widen && shift > 0 && g_wide_pct > 100) { int32_t w = ((int32_t)si * g_wide_pct) / 100; si = (int16_t)(w > 32767 ? 32767 : w); }
    const int16_t axx = xh < 0 ? (int16_t)-xh : xh;
    if (axx > si && !((skip & 1) && shift == 0)) CULL_FAIL(2);
    /* A raised up allowance (the wide copy) must leave room for the depth in 16 bits, as the across allowance is lowered in the far walks. */
    int16_t up_word = (int16_t)rd16(m->mem, ss, t_up);
    if (g_up_raised) { const int room = 32600 - (int)(int16_t)rd16(m->mem, ds, 0x48E6); if (up_word > room && room > 0) up_word = (int16_t)room; }   /* only with the raised table: off, the game's own value is used unchanged */
    ax = (int16_t)((int16_t)(di + (up_word >> shift)) >> cl);
    int16_t up = ax;
    if (m->mem[(((uint32_t)ds << 4) + 0x368C) & 0xFFFFF] != 0) up = (int16_t)(((int16_t)((up >> 3) + ax)) >> 1);
    const int16_t ayy = yh < 0 ? (int16_t)-yh : yh;
    /* The same for the up test: the original's 3D view ends at the top of the cockpit panel, but the wide picture's margins show the world down to the
     * bottom of the screen, so a far object below the old view's edge was dropped there (R30, lower margins). Percent of the up cone for coarser-level
     * objects in a wide picture: F117_FAR_WIDE_UP (default 400. Measured on the owner's flights, 7 Nov: 300 and 600 are identical to 400 on one session, 200 loses 93 pixels on the other, 100 loses more; the ceiling is the 16-bit sum with the depth; 100 = the game's cone; shown to add the missing lower-margin lights, docs/owner-issues.md R30). */
    static int widen_up = -1;
    if (widen_up < 0) { const char *e = getenv("F117_FAR_WIDE_UP"); widen_up = e ? atoi(e) : 400; if (widen_up < 100) widen_up = 100; }
    if (widen_up > 100 && shift > 0 && g_wide_pct > 100) { int32_t u = ((int32_t)up * widen_up) / 100; up = (int16_t)(u > 32767 ? 32767 : u); }
    if (ayy > up && !((skip & 2) && shift == 0)) CULL_FAIL(3);
    const int16_t metric = (int16_t)(((int16_t)(axx + ayy) >> 2) + di);
    if (!((skip & 4) && shift == 0) && metric > (int16_t)rd16(m->mem, ds, 0x48E6)) CULL_FAIL(4);
    g_cull_reason = 0;
    if (why_on && shift == 0) why[5]++;
    return 1;
}

/* VGAME 1039:011A, scene_detail_level, as E2B6 keeps it in DS:48C4 (the fade by depth): 0 for an orthographic view, else the signed byte of the camera
 * z high word's top (a negative one as 0) halved, the class [49AA] taken off, clamped to 0..31 and halved again; 0 when [43E6] is clear. */
static uint8_t far_detail(const machine_t *m, uint16_t ds, int16_t zh, unsigned lod)
{
    if (m->mem[(((uint32_t)ds << 4) + 0x7D8E) & 0xFFFFF]) return 0;
    int a = (int8_t)(uint8_t)((uint16_t)zh >> 8);
    if (a < 0) a = 0;
    a >>= 1;
    int t = (int8_t)(uint8_t)(a - (int)lod);
    if (t < 0) t = 0;
    if (t > 0x1F) t = 0x1F;
    return m->mem[(((uint32_t)ds << 4) + 0x43E6) & 0xFFFFF] ? (uint8_t)(t >> 1) : 0;
}

/* VGAME 0FBD, scene_override (matched.c): the override table at DS:B79E ([9932] entries of five bytes: ring, entry in the tile, cell x, cell y, and a
 * replacement type), searched from the last down; what the game keeps there is the state of changed objects (a destroyed building shows its wreck). The walker
 * asks it for every entry, with the ring it walks, the entry's number in its tile and the cell's absolute coordinates (low bytes). -1 when there is none. */
static int far_override(const machine_t *m, uint16_t ds, const far_inst *f)
{
    const int count = (int16_t)rd16(m->mem, ds, 0x9932);
    for (int i = count - 1; i >= 0; i--) {
        const uint32_t at = ((uint32_t)ds << 4) + (uint16_t)(0xB79E + 5 * i);
        if (m->mem[at & 0xFFFFF] == f->ring && m->mem[(at + 1) & 0xFFFFF] == f->entry && m->mem[(at + 2) & 0xFFFFF] == (uint8_t)f->cx && m->mem[(at + 3) & 0xFFFFF] == (uint8_t)f->cy)
            return m->mem[(at + 4) & 0xFFFFF];
    }
    return -1;
}

typedef struct { int idx, level, ring; uint32_t distance_squared; int16_t x, y, z, ox, oy, oz; uint16_t model; uint8_t darken; uint8_t own, defers; int8_t own_k; uint8_t entry; } far_pick;
static int g_order_own;
static int far_pick_cmp(const void *a, const void *b)
{
    const far_pick *p = (const far_pick *)a, *q = (const far_pick *)b;
    if (p->level != q->level) return q->level - p->level;                   /* coarsest (farthest) scale first */
    if (g_order_own) {                                                      /* F117_NATIVE_ORDER=1 (with F117_NATIVE_WALK): the game's own cells last, in its visiting order, entries in tile order */
        if ((p->own_k >= 0) != (q->own_k >= 0)) return p->own_k >= 0 ? 1 : -1;
        if (p->own_k >= 0) return p->own_k != q->own_k ? p->own_k - q->own_k : (int)p->entry - (int)q->entry;
    }
    return p->distance_squared < q->distance_squared ? 1 : p->distance_squared > q->distance_squared ? -1 : 0;
}

/* The picks' order, far_pick_cmp's (coarsest level first, then the farthest first), by a stable radix sort: four counting passes (the squared distance
 * descending in 11, 11 and 10 bits, then the level descending) instead of qsort's comparator calls, which took 0.74 ms for 9,800 picks. Equal keys keep
 * their listing order (qsort's order for them was unspecified). Opt-in (F117_PICK_RADIX=1): it changed 5 pixels of 42 of 1,401 Maximum frames (9 at Far) of the banked
 * flight against qsort's tie order, with the other two flights identical (docs/scenery-distance.md). */
static int g_pick_fast = -1;                      /* the Far object order setting: 0 exact (qsort), 1 fast (radix); -1 not set: F117_PICK_RADIX decides */
void rerun_set_pick_order(int fast) { g_pick_fast = fast ? 1 : 0; }

static void far_sort_picks(far_pick *p, size_t n)
{
    static far_pick *tmp; static size_t cap;
    if (g_pick_fast < 0) g_pick_fast = getenv("F117_PICK_RADIX") != NULL;        /* the Far object order setting wins once it has been set */
    const int use_qsort = !g_pick_fast;
    if (n < 2) return;
    if (use_qsort || n > 0x7FFFFFFF) { qsort(p, n, sizeof *p, far_pick_cmp); return; }
    if (cap < n) { far_pick *q = (far_pick *)realloc(tmp, n * sizeof *tmp); if (!q) { qsort(p, n, sizeof *p, far_pick_cmp); return; } tmp = q; cap = n; }
    static unsigned count[2048];
    far_pick *from = p, *to = tmp;
    for (int pass = 0; pass < 4; pass++) {
        const int buckets = pass == 3 ? 4 : 2048;
        memset(count, 0, (size_t)buckets * sizeof *count);
        for (size_t i = 0; i < n; i++) {
            const uint32_t d = ~from[i].distance_squared;
            count[pass == 0 ? d & 2047u : pass == 1 ? (d >> 11) & 2047u : pass == 2 ? d >> 22 : (unsigned)(4 - from[i].level) & 3u]++;
        }
        unsigned sum = 0;
        for (int b = 0; b < buckets; b++) { const unsigned c = count[b]; count[b] = sum; sum += c; }
        for (size_t i = 0; i < n; i++) {
            const uint32_t d = ~from[i].distance_squared;
            to[count[pass == 0 ? d & 2047u : pass == 1 ? (d >> 11) & 2047u : pass == 2 ? d >> 22 : (unsigned)(4 - from[i].level) & 3u]++] = from[i];
        }
        far_pick *t = from; from = to; to = t;
    }
}

static int g_native_walk;                                 /* F117_NATIVE_WALK=1: the game's own terrain walk is skipped in the copy and its cells placed natively too */
static int g_far_native = 1;                              /* the far scenery is placed natively (docs/scenery-distance.md); F117_FAR_WALKER=1 returns to the extra terrain walks */

/* ---- The far scenery's choosing, one row of cells at a time, so that rows can run on several threads. Each row writes its own list; the lists are joined in row order,
 * which is the order the single loop produced them in, so the picks (and qsort's result on them) are the same. ---- */
typedef struct {
    int ring_i, lo;
    int64_t cx0, cx1, lower[5][3], upper[5][3];
} choose_ring;
typedef struct { int ring; int64_t cy; } choose_row;
typedef struct { far_pick *p; size_t n, cap; unsigned seen, culled, deep; int fail; } choose_buf;
typedef struct {
    machine_t *m;
    uint16_t ds, ss;
    const int16_t *matrix;
    const uint16_t *model_by_type;
    const uint8_t *class_by_type;
    int64_t (*eye_l)[3];
    int64_t (*base)[2];
    int extra_levels, reach;
    const int *game_would, *game_draws, *step_lo, *step_hi, *own_x, *own_y;
    const choose_ring *rings;
    const choose_row *rows;
    choose_buf *bufs;
    int nrows, parts;
} choose_ctx;

static void far_choose_row(const choose_ctx *cc, const choose_ring *rg, int64_t cy, choose_buf *o)
{
    machine_t *m = cc->m;
    const uint16_t ds = cc->ds, ss = cc->ss;
    const cpu_t *c = &m->cpu;
    const int16_t *matrix = cc->matrix;
    const uint16_t *model_by_type = cc->model_by_type;
    const uint8_t *class_by_type = cc->class_by_type;
    int64_t (*eye_l)[3] = cc->eye_l;
    int64_t (*base)[2] = cc->base;
    const int extra_levels = cc->extra_levels, reach = cc->reach;
    const int *game_would = cc->game_would, *game_draws = cc->game_draws, *step_lo = cc->step_lo, *step_hi = cc->step_hi, *own_x = cc->own_x, *own_y = cc->own_y;
    const int64_t (*lower)[3] = rg->lower, (*upper)[3] = rg->upper;
    const int ring_i = rg->ring_i, lo = rg->lo, side = g_cell_side[ring_i];
    const int64_t cx0 = rg->cx0, cx1 = rg->cx1;
    unsigned tr_seen = 0, tr_culled = 0, tr_deep = 0;
    (void)ring_i;
    {
          const size_t first_i = g_cell_first[ring_i][(size_t)(cy - lo) * side + (size_t)(cx0 - lo)], last_i = g_cell_first[ring_i][(size_t)(cy - lo) * side + (size_t)(cx1 - lo) + 1];
          for (size_t i = first_i; i < last_i; i++) {
        const far_inst *f = &g_inst[i];
        const int r = f->ring;
        tr_seen++;
        const int64_t dcx = f->cx - base[r][0], dcy = f->cy - base[r][1];
        far_pick pk; int chosen = 0, resolved = 0, same_type = 0, last_reason = 0;
        pk.own_k = -1; pk.entry = f->entry;
        uint16_t w = 0; uint8_t klass = 0;
        for (int l = r; l <= 4 && l <= r + extra_levels && !chosen; l++) {
            if (eye_l[l][2] >= 0x7FFF || eye_l[l][2] < 0) continue;
            const int s = 2 * (l - r);
            if (f->x < lower[l][0] || f->x >= upper[l][0] ||
                f->y < lower[l][1] || f->y >= upper[l][1] ||
                f->z < lower[l][2] || f->z >= upper[l][2]) continue;
            const int64_t half = s ? ((int64_t)1 << (s - 1)) : 0;
            const int64_t qx = (f->x + half) >> s, qy = (f->y + half) >> s, qz = (f->z + half) >> s;
            const int64_t ez = eye_l[l][2] < 2 ? 2 : eye_l[l][2];
            const int64_t ddx = qx - eye_l[l][0], ddy = qy - eye_l[l][1], ddz = qz - ez;
            pk.x = (int16_t)(uint16_t)qx; pk.y = (int16_t)(uint16_t)qy; pk.z = (int16_t)(uint16_t)qz;
            pk.ox = (int16_t)(uint16_t)eye_l[l][0]; pk.oy = (int16_t)(uint16_t)eye_l[l][1]; pk.oz = (int16_t)(uint16_t)ez;
            const int16_t x = (int16_t)(pk.x - pk.ox), y = (int16_t)(pk.y - pk.oy), z = (int16_t)(pk.z - pk.oz);
            const int16_t zh = far_axis(matrix, 2, x, y, z);
            if ((zh < 0 ? -(int)zh : (int)zh) >= reach) {
                if (l == r + extra_levels || l == 4) tr_deep++;
                continue;
            }
            if (!resolved) {
                const int ov = far_override(m, ds, f);
                const unsigned type = (ov < 0 ? f->type : (unsigned)ov) & 0x7F;
                same_type = ov >= 0 && type == (f->type & 0x7Fu);
                w = (uint16_t)(model_by_type[type] | (same_type ? 0x8000u : 0));
                klass = class_by_type[type];
                resolved = 1;
            }
            g_cull_reason = 0;
            const int visible = (force_base && g_native_walk && f->ring == 4 && f->entry == 0) || far_cull(m, ds, ss, matrix, x, y, z, zh, klass & 7u, s);
            last_reason = visible ? 0 : g_cull_reason;
            chosen = 1; pk.level = l; pk.model = w; pk.defers = (klass & 0x80) != 0 || same_type; pk.own = same_type ? 1 : 0;    /* (an own, deferring pick goes through the game) */
            if (!visible) { chosen = -1; tr_culled++; break; }
            pk.darken = far_detail(m, ds, zh, klass & 7u);
            {   /* F117_FAR_DARKEN=N (experiment, R29): every far pick's darken byte set to N, to test whether lights appear with it */
                static int forced = -2; if (forced == -2) { const char *e = getenv("F117_FAR_DARKEN"); forced = e ? atoi(e) : -1; }
                if (forced >= 0) pk.darken = (uint8_t)forced;
            }
            /* The comparator groups by ring first, so the ring's scale and
             * the square root do not affect ordering within that group. */
            pk.distance_squared = (uint32_t)(ddx * ddx + ddy * ddy + ddz * ddz);
        }
        {   /* F117_FAR_WHY=M0,M1,... (diagnostic): what the selection did with every object of those model numbers in the cells around the viewer, in the first copies */
            static int why_init; static unsigned why_left; static int why_models[8], why_n;
            if (!why_init) {
                why_init = 1; const char *e = getenv("F117_FAR_WHY");
                for (const char *q = e; q && *q && why_n < 8; ) { why_models[why_n++] = atoi(q); const char *c2 = strchr(q, ','); q = c2 ? c2 + 1 : NULL; }
                { const char *mx = getenv("F117_FAR_WHY_MAX"); why_left = why_n ? (mx ? (unsigned)atoi(mx) : 200u) : 0; }
            }
            static unsigned long long why_from; static int why_from_init;
            if (!why_from_init) { why_from_init = 1; const char *e2 = getenv("F117_FAR_WHY_FROM"); why_from = e2 ? strtoull(e2, NULL, 0) : 0; }
            static int why_all = -1; if (why_all < 0) why_all = getenv("F117_FAR_WHY_ALL") != NULL;   /* every cell, not only those around the viewer */
            if (why_left && c->icount >= why_from && (why_all || (dcx >= -2 && dcx <= 3 && dcy >= -2 && dcy <= 3))) {
                const unsigned mi = model_by_type[f->type & 0x7F] & 0x7F;
                for (int q = 0; q < why_n; q++)
                    if ((int)mi == why_models[q]) {
                        why_left--;
                        int owncell = 0;     /* chosen, but left to the game's own walk (its own cell at its own level): the placement below skips it */
                        if (chosen == 1 && pk.level == r && game_would[r] && game_draws[r])
                            for (int k = step_lo[r]; k < step_hi[r]; k++) if (own_x[k] == dcx && own_y[k] == dcy) owncell = 1;
                        fprintf(stderr, "[why] clock %llu model %u ring %d cell %d,%d entry %d %spos %d,%d,%d level %d detail %u: %s\n", (unsigned long long)c->icount, mi, f->ring, (int)dcx, (int)dcy, f->entry,
                                owncell ? "owncell " : "",
                                (int)(int16_t)(uint16_t)(f->x - eye_l[f->ring][0]), (int)(int16_t)(uint16_t)(f->z - (eye_l[f->ring][2] < 2 ? 2 : eye_l[f->ring][2])), (int)(int16_t)(uint16_t)-(f->y - eye_l[f->ring][1]),
                                chosen == 1 ? pk.level : -1, chosen == 1 ? (unsigned)pk.darken : 0u,
                                chosen == 1 ? "chosen" : chosen == -1 ? (last_reason == 1 ? "culled by the game's visibility test, check 1 far plane" : last_reason == 2 ? "culled by the game's visibility test, check 2 near plane" : last_reason == 3 ? "culled by the game's visibility test, check 3 across" : last_reason == 4 ? "culled by the game's visibility test, check 4 up" : last_reason == 5 ? "culled by the game's visibility test, check 5 metric" : "culled by the game's visibility test, check ?") : "no level within reach or bounds");
                    }
            }
        }
        if (chosen != 1) continue;
        if (pk.level == r && game_would[r]) {                                     /* the game's own cells */
            int own = 0, own_k = -1;
            for (int k = step_lo[r]; k < step_hi[r]; k++) if (own_x[k] == dcx && own_y[k] == dcy) { own = 1; if (own_k < 0) own_k = k; }
            if (own && game_draws[r]) continue;
            pk.own = (uint8_t)own; pk.own_k = (int8_t)own_k;
        }
        pk.idx = (int)i; pk.ring = r;
        pk.idx = (int)i; pk.ring = r;
        if (o->n == o->cap) {
            const size_t nc = o->cap ? o->cap * 2 : 256;
            far_pick *q = (far_pick *)realloc(o->p, nc * sizeof *q);
            if (!q) { o->fail = 1; return; }
            o->p = q; o->cap = nc;
        }
        o->p[o->n++] = pk;
          }
    }
    o->seen += tr_seen; o->culled += tr_culled; o->deep += tr_deep;
}

static void far_choose_part(void *arg, int part)
{
    const choose_ctx *cc = (const choose_ctx *)arg;
    for (int j = part; j < cc->nrows; j += cc->parts) {
        choose_buf *o = &cc->bufs[j];
        o->n = 0; o->seen = o->culled = o->deep = 0; o->fail = 0;
        far_choose_row(cc, &cc->rings[cc->rows[j].ring - 1], cc->rows[j].cy, o);
    }
}

static void (*g_choose_par)(void (*fn)(void *, int), void *arg, int parts);
static int g_choose_parts;
void rerun_set_parallel(void (*run)(void (*fn)(void *, int), void *arg, int parts), int parts) { g_choose_par = run; g_choose_parts = parts; }

static int far_scenery_body(machine_t *m);
static int far_scenery(machine_t *m)
{
    rerun_prof_mark(2);
    const int rc = far_scenery_body(m);
    rerun_prof_mark(3);
    return rc;
}
static int far_scenery_body(machine_t *m)
{
    cpu_t *c = &m->cpu;
    static int env_done;
    if (!env_done) {
        env_done = 1;
        const char *e = getenv("F117_WIDE_DISTANCE");
        if (e) g_distance = atoi(e) < 0 ? 0 : atoi(e);
        const char *s = getenv("F117_WIDE_SCENERY");
        if (s && atoi(s) == 0) g_distance = 0;
        g_native_walk = getenv("F117_NATIVE_WALK") != NULL;
        g_order_own = g_native_walk && getenv("F117_NATIVE_ORDER") != NULL;
    }
    if (g_distance <= 0) return 0;
    const double ms0 = walk_ms();
    const uint16_t ds = c->seg[S_DS], ss = c->seg[S_SS];
    uint16_t arguments[8];
    for (int i = 0; i < 8; i++) arguments[i] = rd16(m->mem, ss, (uint16_t)(c->r[R_SP] + 2 + 2 * i));
    const uint64_t key = world_key(m, ds);
    if (key != g_world_key || !g_ninst) { world_build(m, ds); g_world_key = key; }
    if (!g_ninst) return 0;
    const uint32_t eye_long[3] = { arguments[2] | (uint32_t)arguments[3] << 16, arguments[4] | (uint32_t)arguments[5] << 16, arguments[6] | (uint32_t)arguments[7] << 16 };
    int64_t eye_l[5][3];                                                    /* the eye in each level's units, 810's rounding */
    for (int l = 1; l <= 4; l++) for (int a = 0; a < 3; a++) eye_l[l][a] = cell_scaled(l, eye_long[a]);
    uint16_t enable[5];
    for (int r = 1; r <= 4; r++) enable[r] = rd16(m->mem, ds, (uint16_t)(0x04FA + 2 * r));
    /* The game's own call: a ring is drawn when enabled and its eye height fits (0 .. 7FFEh in the ring's units); it draws nine cells a sector's
     * table names (or, below -60 degrees of pitch, the near tables [04CC]/[04DE]). Those cells are left to it. */
    const unsigned sector = ((uint16_t)(0x1000 - arguments[0])) >> 13, sector_y = (sector + 2) & 7;
    int own_x[16], own_y[16];
    const int near_tables = (int16_t)arguments[1] < (int16_t)0xD555;
    /* The walk's step rules (VGAME 0581 to 05A1 and 0764 to 0775): a ring takes steps 0 to 8 of the table, except that ring 4 at detail [990E] >= 2 goes on to step 15 (reading the
     * table's following entries, as the game does) and rings 1 to 3 at detail < 2 skip steps 0 to 3. Followed only in native-walk mode (F117_NATIVE_WALK). */
    const int detail = (int16_t)rd16(m->mem, ds, 0x990E);
    const int nown = g_native_walk ? 16 : 9;
    int step_lo[5], step_hi[5];
    for (int r = 1; r <= 4; r++) { step_lo[r] = g_native_walk && r != 4 && detail < 2 ? 4 : 0; step_hi[r] = g_native_walk && r == 4 && detail >= 2 ? 16 : 9; }
    for (int i = 0; i < nown; i++) {
        if (near_tables) { own_x[i] = (int16_t)rd16(m->mem, ds, (uint16_t)(0x04CC + 2 * i)); own_y[i] = (int16_t)rd16(m->mem, ds, (uint16_t)(0x04DE + 2 * i)); }
        else { own_x[i] = (int16_t)rd16(m->mem, ds, (uint16_t)(0x034C + 2 * (sector * 9 + i))); own_y[i] = (int16_t)rd16(m->mem, ds, (uint16_t)(0x034C + 2 * (sector_y * 9 + i))); }
    }
    /* the reach: how many coarser levels an object may be drawn at beyond its own ring (each 4 times the reach): Near 1, Far 2, Maximum 3 (the whole map) */
    const int extra_levels = g_distance >= (1 << 22) ? 3 : g_distance >= 16000 ? 2 : 1;
    int game_draws[5] = { 0 }, game_would[5] = { 0 }, radius[5] = { 0 };
    int64_t base[5][2];
    for (int r = 1; r <= 4; r++) {
        game_would[r] = enable[r] && eye_l[r][2] >= 0 && eye_l[r][2] < 0x7FFF;      /* the game's own walk draws this ring */
        game_draws[r] = !g_native_walk && game_would[r];                               /* ... and the far scenery leaves its cells to it */
        base[r][0] = eye_l[r][0] >> 12; base[r][1] = eye_l[r][1] >> 12;
        const long cell = 4096L << (2 * (r - 1));
        long rad = (g_distance + cell - 1) / cell; if (rad < 2) rad = 2;
        radius[r] = 1 << 20;                                  /* no cell radius: the level limit decides */
        (void)rad;
    }
    const uint16_t base_w = rd16(m->mem, ds, 0xE56A), far_was = rd16(m->mem, ds, 0x48E6), e328 = rd16(m->mem, ds, 0xE328);
    uint16_t allow[8], org[3];
    for (int md = 0; md < 8; md++) allow[md] = rd16(m->mem, ds, (uint16_t)(base_w + 2 * md));
    g_allow_saved = g_native_walk ? allow : NULL;
    for (int i = 0; i < 3; i++) org[i] = rd16(m->mem, ds, (uint16_t)(0x49AC + 2 * i));
    static int far_plane = -1;
    if (far_plane < 0) { const char *e = getenv("F117_WIDE_SCENERY_FAR"); far_plane = e ? atoi(e) : 30000; if (far_plane > 32000) far_plane = 32000; }
    wr16(m->mem, ds, 0x48E6, (uint16_t)far_plane);
    for (int md = 0; md < 8; md++) if (allow[md] > 32600 - far_plane) wr16(m->mem, ds, (uint16_t)(base_w + 2 * md), (uint16_t)(32600 - far_plane));
    /* Model mapping and class are stable during selection. Guest placement
     * can change the class table later, so keep this cache local. */
    int16_t matrix[9];
    for (unsigned i = 0; i < 9; i++) matrix[i] = (int16_t)rd16(m->mem, ds, (uint16_t)(0x49BE + 2 * i));
    uint16_t model_by_type[128];
    uint8_t class_by_type[128];
    for (unsigned type = 0; type < 128; type++) {
        const uint16_t model = rd16(m->mem, ds, (uint16_t)(0x0510 + 2 * type));
        const unsigned bank = (model & 0x7FFF) >> 8;
        const uint16_t classes = rd16(m->mem, ds, (uint16_t)(0x0A60 + 2 * bank));
        model_by_type[type] = model;
        class_by_type[type] = m->mem[(((uint32_t)ds << 4) + (uint16_t)(classes + (model & 0x7F))) & 0xFFFFF];
    }
    /* the picks: each object once, at the finest level where its camera depth is under the game's reach (8191 units; 8000 here) */
    static far_pick *picks; static size_t pick_cap;
    size_t npick = 0;
    const int level_lim = 24000, reach = 8000;
    unsigned tr_seen = 0, tr_culled = 0, tr_deep = 0;
    static choose_row *rows; static choose_buf *bufs; static size_t rows_cap;
    choose_ring rings[4];
    int nrows = 0;
    for (int ring_i = 1; ring_i <= 4; ring_i++) {
        if (!enable[ring_i] || !g_cell_side[ring_i]) continue;
        /* the cells an object of this ring can be drawn from: its coarsest allowed level reaches 8000 units of that level, plus a margin for the object's own offset */
        const int top = ring_i + extra_levels < 4 ? ring_i + extra_levels : 4;
        const double reach_cells = 8000.0 * (double)(1 << (2 * (top - 1))) / (4096.0 * (double)(1 << (2 * (ring_i - 1))));
        const int rad = (int)reach_cells + 3;
        /* Hoist the exact rounded-coordinate bounds out of the object loop. */
        int64_t lower[5][3], upper[5][3];
        for (int l = ring_i; l <= top; l++) {
            const int64_t scale = (int64_t)1 << (2 * (l - ring_i));
            for (int axis = 0; axis < 3; axis++) {
                const int64_t eye = eye_l[l][axis];
                const int64_t origin = axis == 2 && eye < 2 ? 2 : eye;
                lower[l][axis] = (origin - level_lim) * scale - scale / 2;
                upper[l][axis] = (origin + level_lim + 1) * scale - scale / 2;
            }
        }
        const int lo = g_cell_lo[ring_i], side = g_cell_side[ring_i];
        int64_t cx0 = base[ring_i][0] - rad, cx1 = base[ring_i][0] + rad, cy0 = base[ring_i][1] - rad, cy1 = base[ring_i][1] + rad;
        if (cx0 < lo) cx0 = lo; if (cy0 < lo) cy0 = lo;
        if (cx1 > lo + side - 1) cx1 = lo + side - 1; if (cy1 > lo + side - 1) cy1 = lo + side - 1;
        choose_ring *rg = &rings[ring_i - 1];
        rg->ring_i = ring_i; rg->lo = lo; rg->cx0 = cx0; rg->cx1 = cx1;
        memcpy(rg->lower, lower, sizeof lower); memcpy(rg->upper, upper, sizeof upper);
        for (int64_t cy = cy0; cy <= cy1; cy++) {
            if ((size_t)nrows == rows_cap) {
                const size_t nc = rows_cap ? rows_cap * 2 : 512;
                choose_row *r2 = (choose_row *)realloc(rows, nc * sizeof *r2);
                choose_buf *b2 = (choose_buf *)realloc(bufs, nc * sizeof *b2);
                if (r2) rows = r2;
                if (b2) { memset(b2 + rows_cap, 0, (nc - rows_cap) * sizeof *b2); bufs = b2; }
                if (!r2 || !b2) return 0;
                rows_cap = nc;
            }
            rows[nrows].ring = ring_i; rows[nrows].cy = cy; nrows++;
        }
    }
    {
        far_cull_init();
        static int serial = -1;            /* the diagnostics that print or count while choosing keep it on one thread */
        if (serial < 0) serial = getenv("F117_FAR_WHY") != NULL || getenv("F117_CULL_WHY") != NULL || getenv("F117_NATIVE_FORCE_BASE") != NULL ? 1 : 0;
        choose_ctx cx = { m, ds, ss, matrix, model_by_type, class_by_type, eye_l, base, extra_levels, reach, game_would, game_draws, step_lo, step_hi, own_x, own_y, rings, rows, bufs, nrows, 1 };
        if (!serial && g_choose_par && g_choose_parts > 1 && nrows >= 8) { cx.parts = g_choose_parts; g_choose_par(far_choose_part, &cx, cx.parts); }
        else far_choose_part(&cx, 0);
        size_t total = 0;
        for (int j = 0; j < nrows; j++) { if (bufs[j].fail) return 0; total += bufs[j].n; }
        if (total > pick_cap) {
            size_t nc = pick_cap ? pick_cap : 1024;
            while (nc < total) nc *= 2;
            far_pick *q = (far_pick *)realloc(picks, nc * sizeof *picks);
            if (!q) { pick_cap = npick = 0; return 0; }
            picks = q; pick_cap = nc;
        }
        for (int j = 0; j < nrows; j++) {
            if (bufs[j].n) memcpy(picks + npick, bufs[j].p, bufs[j].n * sizeof *picks);
            npick += bufs[j].n; tr_seen += bufs[j].seen; tr_culled += bufs[j].culled; tr_deep += bufs[j].deep;
        }
    }
    const double sort_t0 = walk_ms();
    far_sort_picks(picks, npick);
    { static double acc; static unsigned nn; acc += walk_ms() - sort_t0; if (++nn % 600 == 0 && getenv("F117_SORT_PROBE")) fprintf(stderr, "[sort] %.3f ms a copy to sort %zu picks\n", acc / nn, npick); }
    {   /* F117_FAR_TRACE=FROM: one line a copy from that clock: the attitude, and what the far scenery chose */
        static int tr = -1; static unsigned long long tr_from;
        if (tr < 0) { const char *e = getenv("F117_FAR_TRACE"); tr = e != NULL; tr_from = e ? strtoull(e, NULL, 0) : 0; }
        if (tr && c->icount >= tr_from)
            fprintf(stderr, "[far-trace] clock %llu heading %04X pitch %04X roll %04X: %u seen, %u culled, %u too deep, %zu drawn\n", (unsigned long long)c->icount,
                    rd16(m->mem, ds, 0x2DEE), rd16(m->mem, ds, 0x2DF0), rd16(m->mem, ds, 0x2DF2), tr_seen, tr_culled, tr_deep, npick);
    }
    /* place them: natively where the renderer has a template for the model, else through the game's own placement (which makes one) */
    const double ms1 = walk_ms();
    uint16_t sv_r[8], sv_seg[4];
    for (int i = 0; i < 8; i++) sv_r[i] = c->r[i];
    for (int i = 0; i < 4; i++) sv_seg[i] = c->seg[i];
    const uint16_t sv_ip = c->ip, sv_fl = c->flags;
    const uint8_t on = m->trap_on;
    const uint16_t tcs = m->trap_cs, tip = m->trap_ip, tsp = m->trap_sp;
    const uint64_t until = m->run_until, stop = c->stop_at;
    int bad = 0; unsigned native = 0, guest = 0;
    static int stats = -1;
    if (stats < 0) stats = getenv("F117_WIDE_WALK_STATS") != NULL;
    double guest_ms = 0.0;
    /* the walker's frame, as the renderer's hook reads it: the eye longs at BP + 8, BP + 0Ch, BP + 10h */
    for (int i = 0; i < 8; i++) c->r[i] = sv_r[i];
    for (int i = 0; i < 4; i++) c->seg[i] = sv_seg[i];
    c->flags = sv_fl;
    for (int i = 7; i >= 0; i--) cpu_push16(c, arguments[i]);
    cpu_push16(c, 0x04E6);
    cpu_push16(c, c->r[R_BP]);
    c->r[R_BP] = c->r[R_SP];
    const uint16_t frame_sp = c->r[R_SP];
    /* E2B6 defers an object whose class byte has bit 7 (the table at [0A60 + 2 * bank], by model) into the game's own 50-entry list, which drops
     * its nearest entry when full: far objects placed through the game filled it and pushed out the game's own (lights and buildings flickered while
     * banking). In the copy, while the far scenery is placed, those bits are cleared so such an object is drawn at once (the renderer orders by depth),
     * and put back after: the list the game's own frame uses is not touched. */
    struct { uint16_t at; uint8_t was; } nodefer[256]; unsigned n_nodefer = 0;
    for (size_t k = 0; k < npick && n_nodefer < 256; k++) {
        if (picks[k].own || !picks[k].defers) continue;                /* own cells keep deferral, and selection already read the class bits */
        const unsigned bank_idx = (picks[k].model & 0x7FFF) >> 8, idx = picks[k].model & 0x7F;
        const uint16_t at = (uint16_t)(rd16(m->mem, ds, (uint16_t)(0x0A60 + 2 * bank_idx)) + idx);
        uint8_t *p = &m->mem[(((uint32_t)ds << 4) + at) & 0xFFFFF];
        if (!(*p & 0x80)) continue;
        nodefer[n_nodefer].at = at; nodefer[n_nodefer].was = *p; n_nodefer++;
        *p &= 0x7F;
    }
    for (size_t k = 0; k < npick; k++) {
        const far_pick *pk = &picks[k];
        const far_inst *f = &g_inst[pk->idx];
        if (g_collector.native && !(pk->own && pk->defers)) {
            rerun_native spec;
            const unsigned bank_idx = (pk->model & 0x7FFF) >> 8;
            spec.bank = rd16(m->mem, ds, (uint16_t)(0xB78A + 2 * bank_idx));
            spec.model = (uint16_t)(pk->model & 0x7F);
            spec.pos[0] = (int16_t)(uint16_t)(pk->x - pk->ox);
            spec.pos[1] = (int16_t)(uint16_t)(pk->z - pk->oz);
            spec.pos[2] = (int16_t)(uint16_t)(0 - (uint16_t)(pk->y - pk->oy));
            spec.darken = pk->darken; spec.level = pk->level; spec.shrink = 2 * (pk->level - f->ring);
            spec.eye[0] = eye_long[0]; spec.eye[1] = eye_long[1]; spec.eye[2] = eye_long[2];
            remaster_prim_source = 1;
            const int took = g_collector.native(g_collector.user, m, &spec);
            remaster_prim_source = 0;
            if (took) { native++; continue; }
        }
        guest++;
        remaster_prim_source = pk->own && pk->defers ? 3 : 2;
        const double guest_start = stats ? walk_ms() : 0.0;
        { static int set_flags = -1; if (set_flags < 0) set_flags = getenv("F117_NATIVE_C06E") != NULL;   /* experiment: the walk sets [C070] and [C06E] from [98A4] before each entry */
          if (set_flags) { const uint16_t v98 = rd16(m->mem, ds, 0x98A4); wr16(m->mem, ds, 0xC070, v98); wr16(m->mem, ds, 0xC06E, v98); } }
        wr16(m->mem, ds, 0xE328, (uint16_t)pk->level);
        wr16(m->mem, ds, 0x49AC, (uint16_t)pk->ox); wr16(m->mem, ds, 0x49AE, (uint16_t)pk->oy); wr16(m->mem, ds, 0x49B0, (uint16_t)pk->oz);
        g_scale_shift = 2 * (pk->level - f->ring);
        g_walk_busy = 1;
        c->r[R_SP] = frame_sp;
        cpu_push16(c, (uint16_t)pk->z); cpu_push16(c, (uint16_t)pk->y); cpu_push16(c, (uint16_t)pk->x);
        cpu_push16(c, 0); cpu_push16(c, 0); cpu_push16(c, 0);
        cpu_push16(c, pk->model);
        cpu_push16(c, 0x06A6);
        c->ip = 0xE2B6;
        m->trap_on = 1; m->trap_cs = c->seg[S_CS]; m->trap_ip = 0x06A6; m->trap_sp = (uint16_t)(c->r[R_SP] + 2);
        int rc = RUN_SLICE;
        const uint64_t limit = c->icount + 200000000ull;
        while (rc == RUN_SLICE && c->icount < limit) rc = machine_run(m, limit);
        if (g_collector.placement_return) g_collector.placement_return(g_collector.user, m, rc == RUN_TRAP);
        if (stats) guest_ms += walk_ms() - guest_start;
        if (rc != RUN_TRAP) bad++;
        {   /* F117_FAR_WHY (diagnostic): a pick of one of those models went through the game's own placement */
            static int wl = -1; static unsigned left = 200;
            if (wl < 0) wl = getenv("F117_FAR_WHY") != NULL;
            if (wl && left && rerun_why_model(pk->model & 0x7F)) {
                left--;
                fprintf(stderr, "[why] model %u ring %d level %d own %d defers %d: placed by the game, result %s\n", (unsigned)(pk->model & 0x7F), pk->ring, pk->level, pk->own, pk->defers, rc == RUN_TRAP ? "returned" : "did not return");
            }
        }
        g_walk_busy = 0; g_scale_shift = 0; remaster_prim_source = 0;
    }
    g_walk_busy = 0; g_scale_shift = 0; remaster_prim_source = 0;
    while (n_nodefer) { n_nodefer--; m->mem[(((uint32_t)ds << 4) + nodefer[n_nodefer].at) & 0xFFFFF] = nodefer[n_nodefer].was; }
    m->trap_on = on; m->trap_cs = tcs; m->trap_ip = tip; m->trap_sp = tsp; m->run_until = until; c->stop_at = stop;
    wr16(m->mem, ds, 0x48E6, far_was);
    for (int md = 0; md < 8; md++) wr16(m->mem, ds, (uint16_t)(base_w + 2 * md), allow[md]);
    wr16(m->mem, ds, 0xE328, e328);
    for (int i = 0; i < 3; i++) wr16(m->mem, ds, (uint16_t)(0x49AC + 2 * i), org[i]);
    for (int i = 0; i < 8; i++) c->r[i] = sv_r[i];
    for (int i = 0; i < 4; i++) c->seg[i] = sv_seg[i];
    c->ip = sv_ip; c->flags = sv_fl;
    if (g_native_walk) { wr16(m->mem, ds, 0xE328, 0); c->ip = cpu_pop16(c); }     /* the walk is done: return from it (a near call; the caller drops the arguments) */
    if (bad) fprintf(stderr, "[far] %d placements did not return\n", bad);
    {   /* F117_FAR_TRACE: how full the game's 50-entry deferred-draw list is once the far scenery has been placed (a full list drops its nearest entry) */
        static int tr = -1; static unsigned mx;
        if (tr < 0) tr = getenv("F117_FAR_TRACE") != NULL;
        if (tr) { const unsigned d = rd16(m->mem, ds, 0x49F4); if (d > mx) mx = d; fprintf(stderr, "[far-trace] deferred %u of 50 (most %u), guest-placed %u, native %u\n", d, mx, guest, native); }
    }
    {
        static double sel_ms, run_ms, fallback_ms; static unsigned n, picked, total, nat, gst;
        if (stats) {
            fallback_ms += guest_ms;
            sel_ms += ms1 - ms0; run_ms += walk_ms() - ms1; picked += (unsigned)npick; total += (unsigned)g_ninst; nat += native; gst += guest;
            ++n; if (n <= 5 || n % 300 == 0) fprintf(stderr, "[far] mean of %u: %.2f ms choosing (%u objects listed), %.2f ms placing %.0f survivors (%.0f native, %.0f by the game in %.2f ms)\n", n, sel_ms / n, total / n, run_ms / n, (double)picked / n, (double)nat / n, (double)gst / n, fallback_ms / n);
        }
    }
    return 0;
}

static int g_air_k = -1, g_air_done, g_air_pct = 100;
/* R41/R42 groundwork (F117_WIDE_AIR_PROBE=1, diagnostic): the game's own matrix is left alone and, at the return of the aircraft loop's call of 0xB5DC (9BDF),
 * the point's true camera-space x', y', z' and screen position are computed from the unwidened matrix and compared with what the routine stored in [4A10]/[4A18]. */
static int g_air_ret_k = -1;
static int16_t g_air_orig[3][3];       /* the matrix words as the game left them, [vector component][axis] (B082 + 6c + 2a) */
static int16_t g_air_r[3][3], g_air_w[3][3];   /* F117_WIDE_DOTS: this frame's matrix divided by the reach only, and also with the x row widened */
static int16_t g_air_ow[3][3];                 /* F117_WIDE_DOTS: the game's own matrix with only the x row widened (a model of a caller without the reach) */
/* The offset 0xB5DC turns into the body frame: ([C0D0] - x, (alt - [2DF4]) >> 5, y - [C0DE]) as B792 pairs them with the matrix's rows, in the
 * outside view ([C0A6] bit 7) less the camera's own offset, the 32-bit [BA3C] - [B2B4], [B788] - [2DF4] and [C056] - [B77E], each >> 5 (0x0EF74). */
static void air_vector(machine_t *m,uint16_t ds, int16_t x, int16_t y, int16_t alt, int16_t v[3])
{
    int16_t ox = (int16_t)(rd16(m->mem, ds, 0xC0D0) - (uint16_t)x), oy = (int16_t)((uint16_t)y - rd16(m->mem, ds, 0xC0DE));
    int16_t oa = (int16_t)((int16_t)((uint16_t)alt - rd16(m->mem, ds, 0x2DF4)) >> 5);
    if (m->mem[(((uint32_t)ds << 4) + 0xC0A6) & 0xFFFFF] & 0x80) {
        const int32_t dx = (int32_t)(((uint32_t)rd16(m->mem, ds, 0xBA3E) << 16 | rd16(m->mem, ds, 0xBA3C)) - ((uint32_t)rd16(m->mem, ds, 0xB2B6) << 16 | rd16(m->mem, ds, 0xB2B4)));
        const int32_t dy = (int32_t)(((uint32_t)rd16(m->mem, ds, 0xC058) << 16 | rd16(m->mem, ds, 0xC056)) - ((uint32_t)rd16(m->mem, ds, 0xB780) << 16 | rd16(m->mem, ds, 0xB77E)));
        const int32_t da = (int32_t)(int16_t)rd16(m->mem, ds, 0xB788) - (int32_t)rd16(m->mem, ds, 0x2DF4);
        ox = (int16_t)(ox - (int16_t)(uint16_t)(dx >> 5)); oy = (int16_t)(oy - (int16_t)(uint16_t)(dy >> 5)); oa = (int16_t)(oa - (int16_t)(uint16_t)(da >> 5));
    }
    v[0] = ox; v[1] = oa; v[2] = oy;
}
static int g_air_dots = -1;              /* F117_WIDE_DOTS (default on, 0 disables): per-call matrix (true placement for dots) and margin dots (R41/R42) */
static int g_air_last_model;             /* F117_WIDE_DOTS: the last 0xB5DC call goes on to draw a 3D model (widened matrix) */
static int16_t air_fixmul(int16_t a, int16_t b)      /* VGAME 104E:0000: (a * b) shifted right 14, rounded by the next bit, then the sums are 32-bit in B792 */
{
    const int32_t q = (int32_t)a * (int32_t)b;
    return (int16_t)(((q << 1) >> 16) + (((uint32_t)q >> 14) & 1u));
}
/* F117_WIDE_DOTS, at the entry of 0xB5DC from the aircraft loop (returning to 9BDF) or the projectile loop (9D89), for a call that will not draw a
 * model, so the caller draws a dot if the point is on the screen: the point's true screen position from the game's own matrix (the one the call
 * sees), exactly as the routine computes it (matched.c, camera_effect_point): in front when z' < 0; x and y halved when [294B] is set;
 * x = (x' * 256) / z' + A0h; q = (y' * 256) / z', y = q - (q >> 2) plus 3Ch / 34h ([294B] set / clear) when [368C] is set, else 64h. The game
 * keeps a point only inside 0..13Fh across and 0..60h (0..C7h with [368C] clear) down; one outside the 320 x 200 picture is drawn in the margin
 * as the game draws its dot: one pixel in the pen colour (pen 0x886A, then the far line call), 15 for an aircraft (9B16), 29h for projectile
 * slots 0 to 7 and 13 for slots 8 to 11 (9D39, the slot at the caller's [bp - 22]). A point inside the picture that the game drops is under the
 * cockpit (outside the view's rows), so the game's own picture stands. F117_WIDE_DOTS_LOG=1 (diagnostic) logs each margin dot and checks the
 * prediction against the routine's own [4A10]/[4A18] at the next call (nothing between them writes those words in either loop). */
/* A smoke-trail puff (the trail loop's call of 0xB5DC, returning to 9A04): the game draws it as a scaled sprite at the projected point and drops it when that point is
 * off the 320-pixel screen. Everything below is read from the matched code, not measured: the loop draws only when -256 < [DEBE] < 0 (9A0E, 9A15); the size is
 * camimage_scale(age, |256 / [DEBE]|) (9A1D, EE0C, far 114A:0461: age 0: v/2, 1: v/2 + v/8, 2: v/2 + v/4, 3: v/2 + v/4 + v/8, else v); the deferred record (D9FE) carries
 * x = [4A10], y = [4A18], size and the model word 400h + ((age & 14) >> 1), and the compose walk (DC46) draws kind = model & 7 with compose_draw_sprite (1AE9): the sprite's
 * far pointer is [kind * 4 + 920A]:[kind * 4 + 9208], its top left (x - half(size), y - half(t)) with t = size - (size >> 2) and half(v) = v >> 1 for v >= 0, its drawn width size
 * and height t; the blitter (MGRAPHIC 0C95) reads the sprite's width and height words, then a record per row: [n: word][k: word] and n pixel bytes (k transparent pixels
 * before them, colour 0 transparent), and scales across and down with its Bresenham error terms: row error starts at -t and adds the sprite's height per drawn row, taking the
 * source row on while it is not negative (each step the drawn height off), likewise across with the width and the drawn width. Here the whole sprite is generated and only the
 * pixels outside the 4:3 picture's x range 0..319 are added (inside it the game's own picture stands, a puff it dropped included), rows inside the 3D clip window. */
static void wide_puff_overlay(machine_t *m, uint16_t ds, int sx, int sy, int size, unsigned kind, uint16_t table, unsigned kinds, int layer, int whole)
{
    if (!g_collector.overlay_rect || size <= 0 || size > 400 || kind >= kinds) return;
    static int full = -1;                           /* whole: the game dropped the sprite (its centre is off the 320 screen), so all of it is drawn, the overhang into the 4:3 picture too. F117_WIDE_PUFF_FULL=1 (diagnostic, the proof of the decode): the whole sprite, inside the 4:3 picture too, over the game's own: the picture must not change */
    if (full < 0) full = getenv("F117_WIDE_PUFF_FULL") != NULL;
    const int all = full || whole;
    {   static int plog = -1;                       /* F117_WIDE_DOTS_LOG=1: each puff with the sprite's extent */
        if (plog < 0) { const char *e = getenv("F117_WIDE_DOTS_LOG"); plog = e ? (e[0] == '3' ? 2 : 1) : 0; }   /* 3: every puff, inside the 4:3 picture too (the R44 size check) */
        if (plog && (plog == 2 || all || sx - (size >> 1) < 0 || sx + size - (size >> 1) > 320))
            fprintf(stderr, "[wide-puff] clock %llu x %d y %d size %d kind %u\n", (unsigned long long)m->cpu.icount, sx, sy, size, kind);
    }
    const uint16_t seg = rd16(m->mem, ds, (uint16_t)(table + 2 + kind * 4)), off = rd16(m->mem, ds, (uint16_t)(table + kind * 4));
    const uint32_t base = (((uint32_t)seg << 4) + off) & 0xFFFFF;
    const int sw = (int)(m->mem[base] | m->mem[(base + 1) & 0xFFFFF] << 8), sh = (int)(m->mem[(base + 2) & 0xFFFFF] | m->mem[(base + 3) & 0xFFFFF] << 8);
    const int dw = size, dh = size - (size >> 2);
    if (sw <= 0 || sh <= 0 || sw > 256 || sh > 256 || dh <= 0) return;
    const int x0 = sx - (dw >> 1), y0 = sy - (dh >> 1);
    uint32_t row = (base + 4) & 0xFFFFF;
    int ry = 0, ev = -dh;
    uint8_t line[260];
    for (int r = 0; r < dh && ry < sh; r++) {
        const int n = (int)(m->mem[row] | m->mem[(row + 1) & 0xFFFFF] << 8), k = (int)(m->mem[(row + 2) & 0xFFFFF] | m->mem[(row + 3) & 0xFFFFF] << 8);
        if (n > 0 && n < 512) {
            memset(line, 0, sizeof line);
            for (int i = 0; i < n && k + i < sw; i++) line[k + i] = m->mem[(row + 4 + (uint32_t)i) & 0xFFFFF];
            int cur = 0, e = -dw, run_x = 0, run_c = 0;
            for (int i = 0; i <= dw; i++) {
                const int c = i < dw && cur < sw ? line[cur] : 0;
                if (i == dw || c != run_c) {                                  /* the run [run_x, i) of colour run_c ends */
                    if (run_c) {
                        const int xa = x0 + run_x, xb = x0 + i;
                        { static int rl = -1; if (rl < 0) rl = getenv("F117_WIDE_PUFF_RECTS") != NULL;      /* every run of the generated sprite, to compare with the game's own footprint */
                          if (rl && full) fprintf(stderr, "[puff-rect] puff %d,%d size %d: x %d y %d w %d colour %d\n", sx, sy, size, xa, y0 + r, xb - xa, run_c); }
                        if (all) g_collector.overlay_rect(g_collector.user, (double)xa, (double)(y0 + r), (double)(xb - xa), 1.0, 0xFF00u | (unsigned)run_c, 1, layer);
                        else if (xa < 0) g_collector.overlay_rect(g_collector.user, (double)xa, (double)(y0 + r), (double)((xb < 0 ? xb : 0) - xa), 1.0, 0xFF00u | (unsigned)run_c, 1, layer);
                        if (!all && xb > 320) { const int xs = xa > 320 ? xa : 320; g_collector.overlay_rect(g_collector.user, (double)xs, (double)(y0 + r), (double)(xb - xs), 1.0, 0xFF00u | (unsigned)run_c, 1, layer); }
                    }
                    run_x = i; run_c = c;
                }
                if (i < dw) { e += sw; if (e >= 0) { do { cur++; e -= dw; } while (e >= 0); } }
            }
        }
        ev += sh;
        if (ev >= 0) { do { row = (row + (uint32_t)(m->mem[row] | m->mem[(row + 1) & 0xFFFFF] << 8) + 4) & 0xFFFFF; ry++; ev -= dh; } while (ev >= 0 && ry < sh); }
    }
}

/* A puff past the game's cut (|[DEBE]| >= 256, so the sprite would be one game pixel or less): the loop does not draw it (9A0E, 9A15), the overlay draws it at its true size,
 * 256 / |[DEBE]| times the age factor (1/2, 5/8, 3/4, 7/8, 1) game pixels square, at least one screen pixel (the compositor's minimum), in the sprite's most frequent colour: below one game
 * pixel the blitter's footprint is one source pixel that is often transparent, so a colour has to be chosen; the mode is the choice, stated in the audit. Rows clip to the 3D window. */
static unsigned wide_sprite_mode_colour(machine_t *m, uint16_t ds, uint16_t table, unsigned kind)
{
    const uint16_t seg = rd16(m->mem, ds, (uint16_t)(table + 2 + kind * 4)), off = rd16(m->mem, ds, (uint16_t)(table + kind * 4));
    const uint32_t base = (((uint32_t)seg << 4) + off) & 0xFFFFF;
    const int sw = (int)(m->mem[base] | m->mem[(base + 1) & 0xFFFFF] << 8), sh = (int)(m->mem[(base + 2) & 0xFFFFF] | m->mem[(base + 3) & 0xFFFFF] << 8);
    if (sw <= 0 || sh <= 0 || sw > 256 || sh > 256) return 0;
    unsigned count[256] = { 0 };
    uint32_t row = (base + 4) & 0xFFFFF;
    for (int r = 0; r < sh; r++) {
        const int n = (int)(m->mem[row] | m->mem[(row + 1) & 0xFFFFF] << 8), k = (int)(m->mem[(row + 2) & 0xFFFFF] | m->mem[(row + 3) & 0xFFFFF] << 8);
        if (n < 0 || n > 512) return 0;
        for (int i = 0; i < n && k + i < sw; i++) count[m->mem[(row + 4 + (uint32_t)i) & 0xFFFFF]]++;
        row = (row + (uint32_t)n + 4) & 0xFFFFF;
    }
    unsigned best = 0, bc = 0;
    for (unsigned c = 1; c < 256; c++) if (count[c] > bc) { bc = count[c]; best = c; }
    return best;
}
static void wide_puff_far(machine_t *m, uint16_t ds, int64_t z, int64_t x, int64_t y, int age, unsigned kind, int zoom)
{
    if (!g_collector.overlay_rect || z >= 0 || kind >= 8) return;
    const double depth = -(double)(z >> 3);                         /* |[DEBE]| >= 256 here */
    static const double f[5] = { 0.5, 0.625, 0.75, 0.875, 1.0 };
    const double size = 256.0 / depth * f[age < 0 ? 0 : age > 4 ? 4 : age];
    const int64_t px = (x * 256) / z + 0xA0;
    const int16_t q = (int16_t)((y * 256) / z);
    int py = q - (int16_t)(q >> 2);
    py += rd16(m->mem, ds, 0x368C) ? (zoom ? 0x3C : 0x34) : 0x64;
    if (px < -4000 || px > 4000 || py < -4000 || py > 4000) return;
    const unsigned c = wide_sprite_mode_colour(m, ds, 0x9208, kind);
    if (!c) return;
    static int plog = -1;
    if (plog < 0) { const char *e = getenv("F117_WIDE_DOTS_LOG"); plog = e && e[0] == '3'; }
    if (plog) fprintf(stderr, "[wide-puff-far] clock %llu x %lld y %d depth %.0f size %.3f kind %u colour %u\n", (unsigned long long)m->cpu.icount, (long long)px, py, depth, size, kind, c);
    {   const double fx = (double)x * 256.0 / (double)z + 160.0, fy = (double)y * 256.0 / (double)z * 0.75 + (double)(py - (q - (int16_t)(q >> 2)));   /* exact projection, as for the wreck dot */
    g_collector.overlay_rect(g_collector.user, fx - size / 2, fy - size / 2, size, size, 0xFF00u | c, 1, -(int)(-z > 2147483647 ? 2147483647 : -z));   /* true depth (-z', not |[DEBE]| = -z' >> 3): the original depth-sorts puffs with the other deferred objects */ }
}

static struct { int valid, on; int16_t sx, sy; uint16_t ret; } g_dot_prev;
static uint16_t abs_w(uint16_t v) { const uint16_t sg = (v & 0x8000) ? 0xFFFF : 0; return (uint16_t)((v ^ sg) - sg); }
/* The falling wreck (the call returning to 9DFD) and what the game draws of it. Every step below is read from the matched code:
 * - 9E00..9E0F (matched.c 34805): the wreck model (model 11, BB0D) is requested only when [C62E] is not zero and -256 < [DEBE] < 0, [DEBE] = z' >> 3 of the call's own B5DC;
 * - BB0D (matched.c 48553): x32 = [C0D2] << 5 and y32 = [C0E4] << 5 less the view origin ([BA3C]:[BA3E], [C056]:[C058] + 0x01000000 for y), z = [C62E] - [2DF4];
 *   all three shifted left by the scale (5 if [B782] is zero else 6, less 3, or 2 when [294B] is set; negative: arithmetic right), and the model is dropped when the low word of
 *   x or y has |v| >= 7FFFh; with bit 7 of [C0A6] a second-view correction applies (not modelled: no dot then);
 * - E2B6 (matched.c 13347 ff.): the point (x low, -y low, z + (z != 0)) goes to E3BF, whose camera space is the matrix at 49BE/49C4/49CA (word [49CA + 2i] * y, [49C4 + 2i] * z,
 *   [49BE + 2i] * x, summed, doubled; i = 0 x, 1 y, 2 z, high words) and which culls the object when the z high word is above [48E6] or below [48F6 + 2 * class]
 *   (class = the low three bits of the byte at [[0A60 + 2 * (model >> 8)] + model & 7Fh], for model 11), when |x| or |y| leaves the cockpit frustum ([438E] clear: the second frustum is not modelled),
 *   or when the range metric ((|x| + |y|) >> 2) + |z| is above [48E6].
 * Returns 1 when the game draws the model, 0 when it does not, -1 when the state is one this does not model. */
static int wreck_game_draws(machine_t *m, uint16_t ds, uint16_t ss)
{
    if (rd16(m->mem, ds, 0xC62E) == 0 || (m->mem[(((uint32_t)ds << 4) + 0xC0A6) & 0xFFFFF] & 0x80) || rd16(m->mem, ds, 0x438E) != 0) return -1;
    const int zoom = m->mem[(((uint32_t)ds << 4) + 0x294B) & 0xFFFFF] != 0;
    const uint32_t ox = (uint32_t)rd16(m->mem, ds, 0xBA3C) | (uint32_t)rd16(m->mem, ds, 0xBA3E) << 16, oy = (uint32_t)rd16(m->mem, ds, 0xC056) | (uint32_t)rd16(m->mem, ds, 0xC058) << 16;
    int32_t rx = (int32_t)(((uint32_t)rd16(m->mem, ds, 0xC0D2) << 5) - ox), ry = (int32_t)(((uint32_t)rd16(m->mem, ds, 0xC0E4) << 5) + oy - 0x01000000u);
    int16_t rz = (int16_t)(rd16(m->mem, ds, 0xC62E) - rd16(m->mem, ds, 0x2DF4));
    const int sc = (rd16(m->mem, ds, 0xB782) == 0 ? 5 : 6) - (zoom ? 2 : 3);
    if (sc > 0) { rx = (int32_t)((uint32_t)rx << sc); ry = (int32_t)((uint32_t)ry << sc); rz = (int16_t)((uint16_t)rz << sc); }
    else if (sc < 0) { rx >>= -sc; ry >>= -sc; rz = (int16_t)(rz >> -sc); }
    if (abs_w((uint16_t)rx) >= 0x7FFF || abs_w((uint16_t)ry) >= 0x7FFF) return 0;
    const int16_t bp = (int16_t)(uint16_t)rx, bx = (int16_t)(-(int16_t)(uint16_t)ry), cx = (int16_t)(rz + 1);   /* the height is not zero here */
    int32_t hi[3];
    for (int i = 0; i < 3; i++) {
        const int32_t acc = (int32_t)(int16_t)rd16(m->mem, ds, (uint16_t)(0x49CA + 2 * i)) * bx + (int32_t)(int16_t)rd16(m->mem, ds, (uint16_t)(0x49C4 + 2 * i)) * cx + (int32_t)(int16_t)rd16(m->mem, ds, (uint16_t)(0x49BE + 2 * i)) * bp;
        hi[i] = (int16_t)(((uint32_t)acc << 1) >> 16);
    }
    const int16_t zh = (int16_t)hi[2];
    const uint16_t model = 11, tab = rd16(m->mem, ds, (uint16_t)(0x0A60 + 2 * ((model >> 8) & 0x7F)));
    const unsigned lod = (m->mem[(((uint32_t)ds << 4) + (uint16_t)(tab + (model & 0xFF))) & 0xFFFFF]) & 7u, lod2 = lod * 2;
    if (zh > (int16_t)rd16(m->mem, ds, 0x48E6)) return 0;
    if (zh < (int16_t)rd16(m->mem, ds, (uint16_t)(0x48F6 + lod2))) return 0;
    const unsigned cl = (unsigned)(zoom ^ 1);
    const int16_t az = (int16_t)abs_w((uint16_t)zh);
    int16_t lim = (int16_t)((int16_t)(az + (int16_t)(rd16(m->mem, ds, 0xE56A) + lod2)) >> cl);
    const int16_t xlim = (int16_t)(lim + (lim >> 2)), ax = (int16_t)abs_w((uint16_t)hi[0]);
    if (ax > xlim) return 0;
    int16_t up = (int16_t)((int16_t)(az + (int16_t)rd16(m->mem, ss, (uint16_t)(rd16(m->mem, ds, 0xE570) + lod2))) >> cl);
    if (m->mem[(((uint32_t)ds << 4) + 0x368C) & 0xFFFFF]) up = (int16_t)(((int16_t)((up >> 3) + up)) >> 1);
    const int16_t ay = (int16_t)abs_w((uint16_t)hi[1]);
    if (ay > up) return 0;
    const int16_t metric = (int16_t)((int16_t)((int16_t)(ax + ay) >> 2) + az);
    {   static int dl = -1; if (dl < 0) { const char *e = getenv("F117_WIDE_DOTS_LOG"); dl = e && e[0] == '3'; }
        if (dl) fprintf(stderr, "[wreck-calc] rx %d ry %d rz %d sc %d | bp %d bx %d cx %d | hi %d %d %d lod %u | xlim %d up %d metric %d 48E6 %d near %d\n", (int)rx, (int)ry, (int)rz, sc, bp, bx, cx, hi[0], hi[1], hi[2], lod, xlim, up, metric, (int)(int16_t)rd16(m->mem, ds, 0x48E6), (int)(int16_t)rd16(m->mem, ds, (uint16_t)(0x48F6 + lod2))); }
    return !(metric > (int16_t)rd16(m->mem, ds, 0x48E6));
}
static void wide_wreck(machine_t *m, uint16_t ds, uint16_t ss, const int32_t comp[3], int zoom)
{
    static int on = -1;                             /* on by default; F117_WIDE_WRECK=0 draws no dot (the game's own picture) */
    if (on < 0) { const char *e = getenv("F117_WIDE_WRECK"); on = !(e && e[0] == '0'); }
    if (!on || !g_collector.overlay_rect || comp[2] >= 0) return;
    const int16_t debe = (int16_t)(uint16_t)(comp[2] >> 3);
    const int in_range = debe < 0 && debe > -256;
    const int drawn = in_range ? wreck_game_draws(m, ds, ss) : 0;
    static int wl = -1; if (wl < 0) { const char *e = getenv("F117_WIDE_DOTS_LOG"); wl = e && e[0] == '3'; }
    const int64_t sx = ((int64_t)comp[0] * 256) / comp[2] + 0xA0;
    const int16_t q = (int16_t)(((int64_t)comp[1] * 256) / comp[2]);
    int sy = q - (int16_t)(q >> 2);
    const int rows = rd16(m->mem, ds, 0x368C) != 0;
    sy += rows ? (zoom ? 0x3C : 0x34) : 0x64;
    if (wl) fprintf(stderr, "[wreck] clock %llu z' %d debe %d in_range %d game_draws %d at %lld,%d\n", (unsigned long long)m->cpu.icount, (int)(-comp[2]), (int)debe, in_range, drawn, (long long)sx, sy);
    if (drawn != 0) return;                         /* the game draws it (1), or this state is not modelled (-1): no dot */
    const int inside = sx >= 0 && sx <= 0x13F;
    if (sy < 0 || sy > (inside ? (rows ? 0x60 : 0xC7) : 199) || sx < -4000 || sx > 4000) return;
    const double depth = -(double)comp[2], w = depth > 8191.0 ? 8191.0 / depth : 1.0;       /* the one far-size rule */
    {   /* centred on the exact projection (the routine's arithmetic without its integer truncation): the original draws a model at its true sub-pixel place, and with a
     * depth test a dot on the rounded pixel can fall under the horizon's ground, which the true point is above (seen at depth 4,000, 12 Nov) */
    const double fx = (double)comp[0] * 256.0 / (double)comp[2] + 160.0, fq = (double)comp[1] * 256.0 / (double)comp[2];
    const double fy = fq * 0.75 + (rows ? (zoom ? 0x3C : 0x34) : 0x64);
    g_collector.overlay_rect(g_collector.user, fx - w / 2, fy - w / 2, w, w, 0xFF00u, 0, -(int)(depth < 1.0 ? 1.0 : depth));   /* at its true depth: the original draws the wreck as a depth-tested model */   /* palette index 0: the wreck's pixels in the original are black */ }
}


/* The gun tracer (the display pass, section 2, matched.c 68428): its tail is projected by the call returning to A203 and its head, half a velocity step ahead, by the call returning to
 * A236; the game draws the line (114A:0006, clipped to the 320 x 91 view) only when BOTH ends are on the screen: "the gun's reach is the window". A tracer with an end off the 320 screen
 * but both ends in front of the eye is drawn here by the part of the line outside x 0..319, in the pen the game would use: 0Dh for the player's slots (index below [B198]), 29h for an
 * enemy's (A24E..A251; the slot is the caller's local at bp - 24h). The hit tests are the game's own and stay: a tracer in a margin cannot hit, as in the original. */
static struct { int front, on; double x, y; } g_tail;
static void wide_tracer_line(machine_t *m, uint16_t ds, uint16_t ss, double hx, double hy)
{
    if (!g_collector.overlay_line || !g_tail.front) return;
    const int slot = (int16_t)rd16(m->mem, ss, (uint16_t)(m->cpu.r[R_BP] - 0x24)), players = (int16_t)rd16(m->mem, ds, 0xB198);
    const unsigned colour = 0xFF00u | (slot < players ? 0x0Du : 0x29u);
    const double x0 = g_tail.x, y0 = g_tail.y, x1 = hx, y1 = hy;
    if (x0 == x1 && y0 == y1) return;
    {   static int tlog = -1;                       /* F117_WIDE_DOTS_LOG=1: each tracer drawn in a margin */
        if (tlog < 0) tlog = getenv("F117_WIDE_DOTS_LOG") != NULL;
        if (tlog) fprintf(stderr, "[wide-tracer] clock %llu slot %d (%d player slots) from %.1f,%.1f to %.1f,%.1f pen %02X\n", (unsigned long long)m->cpu.icount, slot, players, x0, y0, x1, y1, colour & 255);
    }
    /* the part with x < 0 and the part with x >= 320, by the line's parameter */
    if (getenv("F117_WIDE_TRACER_FULL")) { g_collector.overlay_line(g_collector.user, x0, y0, x1, y1, colour); return; }
    const double lim[2] = { 0.0, 320.0 };
    for (int side = 0; side < 2; side++) {
        double t0 = 0.0, t1 = 1.0;
        const double dx = x1 - x0;
        if (dx == 0.0) { if (side == 0 ? x0 >= lim[0] : x0 < lim[1]) continue; }
        else {
            const double tc = (lim[side] - x0) / dx;                 /* where the line crosses the edge */
            if ((side == 0) == (dx > 0.0)) { if (tc < t1) t1 = tc; } else { if (tc > t0) t0 = tc; }   /* x < 0 is the early part if the line runs right, the late part if left */
        }
        if (t1 <= t0) continue;
        g_collector.overlay_line(g_collector.user, x0 + dx * t0, y0 + (y1 - y0) * t0, x0 + dx * t1, y0 + (y1 - y0) * t1, colour);
    }
}

static void wide_dot_at_entry(machine_t *m, uint16_t ds, uint16_t ss, uint16_t sp, uint16_t ret)
{
    static int log = -1;
    if (log < 0) { const char *e = getenv("F117_WIDE_DOTS_LOG"); log = e ? (e[0] == '2' ? 2 : 1) : 0; }
    const unsigned view = m->mem[(((uint32_t)ds << 4) + 0xC0A6) & 0xFFFFF], zoom = m->mem[(((uint32_t)ds << 4) + 0x294B) & 0xFFFFF];
    if (log && g_dot_prev.valid) {                  /* the previous call's prediction against what the routine stored */
        static unsigned long long checked, differ; static unsigned shown;
        const uint16_t gx = rd16(m->mem, ds, 0x4A10), gy = rd16(m->mem, ds, 0x4A18);
        const int same = g_dot_prev.on ? gx == (uint16_t)g_dot_prev.sx && gy == (uint16_t)g_dot_prev.sy : gx == 0xFFFF;
        checked++;
        if (!same) {
            differ++;
            if (shown < 20) { shown++; fprintf(stderr, "[wide-dot] DIFFERS clock %llu caller %04X view %02X zoom %u: predicted %s %d,%d, game %u,%u\n", (unsigned long long)m->cpu.icount, g_dot_prev.ret, view, zoom, g_dot_prev.on ? "on" : "off", g_dot_prev.sx, g_dot_prev.sy, gx, gy); }
        }
        if (checked % 500 == 0) fprintf(stderr, "[wide-dot] check: %llu dot calls predicted, %llu differ\n", checked, differ);
    }
    if (log == 2) {                                 /* the active text block's rectangle (matched.c 13934: words +14, +16, +18, +20 of the record at [4010]) every 2000th call */
        static unsigned calls;
        if (calls++ % 2000 == 0) {
            const uint16_t blk = rd16(m->mem, ds, 0x4010);
            fprintf(stderr, "[wide-block] clock %llu caller %04X block %04X +4 %u +14 %d +16 %d +18 %d +20 %d | 4028 block %04X +14 %d +16 %d +18 %d +20 %d\n", (unsigned long long)m->cpu.icount, ret, blk, rd16(m->mem, ds, (uint16_t)(blk + 4)),
                    (int16_t)rd16(m->mem, ds, (uint16_t)(blk + 14)), (int16_t)rd16(m->mem, ds, (uint16_t)(blk + 16)), (int16_t)rd16(m->mem, ds, (uint16_t)(blk + 18)), (int16_t)rd16(m->mem, ds, (uint16_t)(blk + 20)),
                    rd16(m->mem, ds, 0x4028), (int16_t)rd16(m->mem, ds, (uint16_t)(rd16(m->mem, ds, 0x4028) + 14)), (int16_t)rd16(m->mem, ds, (uint16_t)(rd16(m->mem, ds, 0x4028) + 16)),
                    (int16_t)rd16(m->mem, ds, (uint16_t)(rd16(m->mem, ds, 0x4028) + 18)), (int16_t)rd16(m->mem, ds, (uint16_t)(rd16(m->mem, ds, 0x4028) + 20)));
        }
    }
    g_dot_prev.valid = 0;
    if (ret != 0x9BDF && ret != 0x9D89 && ret != 0x9A04 && ret != 0x9DFD && ret != 0xA298 && ret != 0xA203 && ret != 0xA236 && log < 2) return;   /* F117_WIDE_DOTS_LOG=2: every caller's true position too (flight display pass, puffs) */
    int16_t v[3];
    air_vector(m, ds, (int16_t)rd16(m->mem, ss, (uint16_t)(sp + 2)), (int16_t)rd16(m->mem, ss, (uint16_t)(sp + 4)), (int16_t)rd16(m->mem, ss, (uint16_t)(sp + 6)), v);
    int32_t comp[3];
    for (int a = 0; a < 3; a++) { comp[a] = 0; for (int c = 0; c < 3; c++) comp[a] += air_fixmul(g_air_orig[c][a], v[c]); }
    if (zoom) { comp[0] >>= 1; comp[1] >>= 1; }     /* SAR, RCR: the 32-bit value halved */
    if (ret == 0xA203) g_tail.front = 0;
    if (log && ret == 0x9D89 && comp[2] < 0) {     /* step 1 of the far-size question: how deep do missiles and bullets get? a line at each new record, per projectile slot class */
        static int32_t deepest[2];
        const int player = (int16_t)rd16(m->mem, ss, (uint16_t)(m->cpu.r[R_BP] - 0x22)) >= 8;
        if (-comp[2] > deepest[player]) { deepest[player] = -comp[2]; fprintf(stderr, "[proj-depth] clock %llu %s projectile: new deepest %d world units\n", (unsigned long long)m->cpu.icount, player ? "player" : "enemy", (int)deepest[player]); }
    }
    if (ret == 0x9DFD) { if (comp[2] < 0) wide_wreck(m, ds, ss, comp, (int)zoom); return; }
    if (comp[2] >= 0) { g_dot_prev.valid = ret == 0x9BDF || ret == 0x9D89 || ret == 0xA203 || ret == 0xA236; g_dot_prev.on = 0; g_dot_prev.ret = ret; return; }   /* behind the eye: off, and no dot */
    if (ret == 0xA298) {                            /* the impact blast (display pass section 3, matched.c 68847): the sprite 1A98(x, y, size, phase), same driver call as a puff's */
        static int blast_on = -1;
        if (blast_on < 0) { const char *e = getenv("F117_WIDE_PUFFS"); blast_on = !(e && e[0] == '0'); }     /* the sprite family shares the switch */
        const int16_t depth = (int16_t)(uint16_t)(comp[2] >> 3), clock = (int16_t)rd16(m->mem, ds, 0x9D3C);
        if (blast_on && depth < 0 && clock != 0) {
            int size = abs((int)(256 / (int)depth));                                /* 0x100 / [DEBE], IDIV, ABS (A2A6..A2AE) */
            if (rd16(m->mem, ds, 0xC0DC) == 0) size <<= 1;                          /* a ground blast is twice as wide (A2B3) */
            const int phase = 0x10 - abs((int)clock);                                /* A2C0..A2C3 */
            const int64_t px = ((int64_t)comp[0] * 256) / comp[2] + 0xA0;
            const int16_t q = (int16_t)(((int64_t)comp[1] * 256) / comp[2]);
            int py = q - (int16_t)(q >> 2);
            py += rd16(m->mem, ds, 0x368C) ? (zoom ? 0x3C : 0x34) : 0x64;
            if (phase >= 0 && phase < 16) wide_puff_overlay(m, ds, (int)px, py, size, (unsigned)phase, 0x91B8, 16, 0, !(px >= 0 && px <= 0x13F && py >= 0 && py <= (rd16(m->mem, ds, 0x368C) ? 0x60 : 0xC7)));
        }
        return;
    }
    if (ret == 0x9A04) {                            /* a smoke puff: the sprite outside the 4:3 picture (see wide_puff_overlay) */
        /* F117_WIDE_PUFFS=1 (off by default): none of the ten long recorded flights scanned (11 Nov) contains a smoke trail, so the sprite decode has not been
         * proven on a real puff; F117_WIDE_PUFF_FULL=1 (the proof mode) enables it too. */
        static int puffs_on = -1;      /* on by default (owner, 11 Nov 22:27); F117_WIDE_PUFFS=0 turns the margin puffs and the blast off */
        if (puffs_on < 0) { const char *e = getenv("F117_WIDE_PUFFS"); puffs_on = !(e && e[0] == '0'); }
        const int16_t depth = (int16_t)(uint16_t)(comp[2] >> 3);
        if (puffs_on && depth < 0 && depth > -256) {
            const uint16_t si = m->cpu.r[R_SI];
            const int age = (int16_t)rd16(m->mem, ds, (uint16_t)(si + 0x3A0E));
            const int v = abs((int)(int16_t)(256 / depth));
            int size = v;
            if (age == 0) size = v >> 1;
            else if (age == 1) size = (v >> 1) + (v >> 3);
            else if (age == 2) size = (v >> 1) + (v >> 2);
            else if (age == 3) size = (v >> 1) + (v >> 2) + (v >> 3);
            const int64_t px = ((int64_t)comp[0] * 256) / comp[2] + 0xA0;
            const int16_t q = (int16_t)(((int64_t)comp[1] * 256) / comp[2]);
            int py = q - (int16_t)(q >> 2);
            py += rd16(m->mem, ds, 0x368C) ? (zoom ? 0x3C : 0x34) : 0x64;
            wide_puff_overlay(m, ds, (int)px, py, size, (unsigned)((rd16(m->mem, ds, (uint16_t)(si + 0x3A0E)) & 14) >> 1) & 7u, 0x9208, 8, -(int)(comp[2] < -1 ? -comp[2] : 1), !(px >= 0 && px <= 0x13F && py >= 0 && py <= (rd16(m->mem, ds, 0x368C) ? 0x60 : 0xC7)));
        } else if (puffs_on && comp[2] < 0 && (comp[2] >> 3) <= -256) {          /* past the game's cut: true size, one screen pixel at least (R44 item 3 follow-up) */
            const uint16_t si = m->cpu.r[R_SI];
            wide_puff_far(m, ds, comp[2], comp[0], comp[1], (int16_t)rd16(m->mem, ds, (uint16_t)(si + 0x3A0E)), ((rd16(m->mem, ds, (uint16_t)(si + 0x3A0E)) & 14) >> 1) & 7u, zoom);
        }
        return;
    }
    const int64_t sx = ((int64_t)comp[0] * 256) / comp[2] + 0xA0;
    const int16_t q = (int16_t)(((int64_t)comp[1] * 256) / comp[2]);
    int16_t sy = (int16_t)(q - (int16_t)(q >> 2));
    const int rows = rd16(m->mem, ds, 0x368C) != 0;
    sy = (int16_t)(sy + (rows ? (zoom ? 0x3C : 0x34) : 0x64));
    int64_t sx_t = sx;
    if (ret == 0xA203 || ret == 0xA236) {        /* F117_WIDE_TRACER_SHIFT=N (diagnostic, proves the margin path on a real tracer): the tracer's x moved by N game pixels */
        static int shift = -999;
        if (shift == -999) { const char *e = getenv("F117_WIDE_TRACER_SHIFT"); shift = e ? atoi(e) : 0; }
        sx_t += shift;
    }
    const int on = sx_t >= 0 && sx_t <= 0x13F && sy >= 0 && sy <= (rows ? 0x60 : 0xC7);
    if (log == 2) fprintf(stderr, "[wide-pt] clock %llu caller %04X view %02X zoom %u true x %lld y %d %s\n", (unsigned long long)m->cpu.icount, ret, view, zoom, (long long)sx, sy, on ? "on" : "off");
    if (log == 2 && (ret == 0xAD26 || ret == 0xA4ED))     /* the detection ring's projection (air unit AD26, ground site A4ED): the state B4F5 will test (matched.c 24061) */
        fprintf(stderr, "[wide-ring] clock %llu caller %04X countdown %d S %d detector %d margin %d predicted %s x %lld y %d\n", (unsigned long long)m->cpu.icount, ret,
                (int16_t)rd16(m->mem, ds, 0x3D98), (int16_t)rd16(m->mem, ds, 0x368E), (int16_t)rd16(m->mem, ds, 0xC0AA), (int)(int16_t)rd16(m->mem, ds, 0x368E) - (int)(int16_t)rd16(m->mem, ds, 0x3D98), on ? "on" : "off", (long long)sx, sy);
    if (ret != 0x9BDF && ret != 0x9D89 && ret != 0xA203 && ret != 0xA236 && !(log == 2 && (ret == 0xAD26 || ret == 0xA4ED))) return;
    if (ret == 0xA203 || ret == 0xA236) {
        static int tr_on = -1;                      /* on by default (owner, 11 Nov 22:27); F117_WIDE_TRACERS=0 turns the margin tracers off */
        if (tr_on < 0) { const char *e = getenv("F117_WIDE_TRACERS"); tr_on = !(e && e[0] == '0'); }
        if (ret == 0xA203) { g_tail.front = 1; g_tail.on = on; g_tail.x = (double)sx_t + 0.5; g_tail.y = (double)sy + 0.5; }
        else if (tr_on && (getenv("F117_WIDE_TRACER_FULL") || !(on && g_tail.on))) wide_tracer_line(m, ds, ss, (double)sx_t + 0.5, (double)sy + 0.5);   /* F117_WIDE_TRACER_FULL=1 (diagnostic): the whole line over the game's own, to see that it lands on it */
        if (ret == 0xA236) g_tail.front = 0;
    }
    g_dot_prev.valid = ret == 0x9BDF || ret == 0x9D89 || ret == 0xA203 || ret == 0xA236; g_dot_prev.on = on; g_dot_prev.sx = (int16_t)sx; g_dot_prev.sy = sy; g_dot_prev.ret = ret;
    if (ret == 0xA203 || ret == 0xA236 || ret == 0x9A04) return;
    if (on || !(sx < 0 || sx > 0x13F || sy < 0 || sy > 199) || !g_collector.overlay_dot) return;
    unsigned colour = 0xFF0F;
    if (ret == 0x9D89) colour = (int16_t)rd16(m->mem, ss, (uint16_t)(m->cpu.r[R_BP] - 0x22)) < 8 ? 0xFF29 : 0xFF0D;
    if (log) fprintf(stderr, "[wide-dot] clock %llu %s view %02X zoom %u x %lld y %d colour %04X\n", (unsigned long long)m->cpu.icount, ret == 0x9D89 ? "projectile" : "aircraft", view, zoom, (long long)sx, sy, colour);
    g_collector.overlay_dot(g_collector.user, (double)sx, (double)sy, colour);
}

/* R43, far aircraft: see private/r43_far_aircraft.py and docs/wide-margin-audit.md. Called once per frame from the walk hook. */
static void far_aircraft_dots(machine_t *m)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("F117_WIDE_FAR_AIRCRAFT"); on = e ? (e[0] == '2' ? 2 : e[0] == '1') : 0; }      /* off until the owner has seen a picture; 2 (diagnostic): every aircraft, the ones the game draws included: the overlay must land on the game's own dot */
    if (!on || !g_collector.overlay_rect || (on == 1 && g_distance <= 4800)) return;
    const uint16_t ds = m->cpu.seg[S_DS];
    if (m->mem[(((uint32_t)ds << 4) + 0xC0A6) & 0xFFFFF] & 0x80) return;                           /* the cockpit view's camera; the outside views are not done */
    const int count = (int16_t)rd16(m->mem, ds, 0xDEFC);
    if (count <= 0 || count > 64) return;
    int16_t A[3][3];
    for (int a = 0; a < 3; a++) for (int c = 0; c < 3; c++) A[a][c] = (int16_t)rd16(m->mem, ds, (uint16_t)(0xB082 + 6 * c + 2 * a));
    const int rows = rd16(m->mem, ds, 0x368C) != 0, zoom = m->mem[(((uint32_t)ds << 4) + 0x294B) & 0xFFFFF] != 0;
    for (int i = 0; i < count; i++) {
        const uint16_t rec = (uint16_t)(0xC16A + 36 * i);
        if (!(m->mem[(((uint32_t)ds << 4) + rec + 0x18) & 0xFFFFF] & 2)) continue;                 /* the loop's first test: flags (C182) & 2 */
        const int16_t x = (int16_t)rd16(m->mem, ds, (uint16_t)(rec + 2)), y = (int16_t)rd16(m->mem, ds, (uint16_t)(rec + 4)), alt = (int16_t)rd16(m->mem, ds, (uint16_t)(rec + 6));
        int dx = (int16_t)(rd16(m->mem, ds, 0xC0D0) - (uint16_t)x), dy = (int16_t)(rd16(m->mem, ds, 0xC0DE) - (uint16_t)y);      /* point_range, then vg_dist */
        dx = dx < 0 ? -dx : dx; dy = dy < 0 ? -dy : dy;
        int range = (dx > dy ? dx : dy) + ((dx > dy ? dy : dx) >> 1);
        if (range > 0x7FFF) range = 0x7FFF;
        if (on == 1 && (range < 4800 || range > g_distance)) continue;                              /* the game draws it, or the setting's reach ends */
        int16_t v[3];
        air_vector(m, ds, x, y, alt, v);
        int32_t comp[3];
        for (int a = 0; a < 3; a++) { comp[a] = 0; for (int c = 0; c < 3; c++) comp[a] += air_fixmul(A[c][a], v[c]); }
        if (zoom) { comp[0] >>= 1; comp[1] >>= 1; }
        if (comp[2] >= 0) continue;                                                                 /* behind the eye */
        const int64_t sx = ((int64_t)comp[0] * 256) / comp[2] + 0xA0;
        const int16_t q = (int16_t)(((int64_t)comp[1] * 256) / comp[2]);
        const int sy = (int16_t)(q - (int16_t)(q >> 2)) + (rows ? (zoom ? 0x3C : 0x34) : 0x64);
        const double depth = -(double)comp[2], w = depth > 8191.0 ? 8191.0 / depth : 1.0;          /* the one far-size rule */
        {   static int flog = -1;                      /* F117_WIDE_DOTS_LOG=1: each unit the far-aircraft overlay draws */
            if (flog < 0) flog = getenv("F117_WIDE_DOTS_LOG") != NULL;
            if (flog) fprintf(stderr, "[far-aircraft] clock %llu unit %d range %d depth %.0f at %lld,%d width %.3f flags %02X type %u\n", (unsigned long long)m->cpu.icount, i, range, depth, (long long)sx, sy, w, m->mem[(((uint32_t)ds << 4) + rec + 0x18) & 0xFFFFF], rd16(m->mem, ds, (uint16_t)(rec + 0x16)));
        }
        g_collector.overlay_rect(g_collector.user, (double)sx + 0.5 - w / 2, (double)sy + 0.5 - w / 2, w, w, 0xFF0F, 0, 3);
    }
}

/* F117_STAGE_PUFFS=1 (diagnostic, the proof of the smoke puff overlay): no recorded flight has a smoke trail, so each frame, in the render copy only (it is thrown away and the real game
 * is not touched), four puffs are put into the game's own trail table (16 slots of 8 bytes at DS:3A08: east, north, altitude, age; matched.c 34606) at world positions that project (by the
 * live matrix at B082, B792's pairing, fixmul = a * b / 32768) to chosen screen points at depth [DEBE] = -8: left margin, across the left edge, across the right edge, right margin. The
 * game's own puff loop (9A04) then draws them through its normal sprite path. Positions are solved for the cockpit view; the camera is the aircraft ([C0A6] bit 7 clear). */
static void stage_puffs(machine_t *m)
{
    static int on = -1, blast = -1;
    if (on < 0) { on = getenv("F117_STAGE_PUFFS") != NULL; blast = getenv("F117_STAGE_BLAST") != NULL; }
    if (!on && !blast) return;
    const uint16_t ds = m->cpu.seg[S_DS];
    if (m->mem[(((uint32_t)ds << 4) + 0xC0A6) & 0xFFFFF] & 0x80) return;
    double A[3][3];
    for (int a = 0; a < 3; a++) for (int c = 0; c < 3; c++) A[a][c] = (int16_t)rd16(m->mem, ds, (uint16_t)(0xB082 + 6 * c + 2 * a)) / 32768.0;   /* comp_a = sum_c A[a][c] * v_c, v = (east offset, altitude offset, north offset) */
    const double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (fabs(det) < 0.5) return;
    double inv[3][3];
    inv[0][0] = (A[1][1] * A[2][2] - A[1][2] * A[2][1]) / det; inv[0][1] = (A[0][2] * A[2][1] - A[0][1] * A[2][2]) / det; inv[0][2] = (A[0][1] * A[1][2] - A[0][2] * A[1][1]) / det;
    inv[1][0] = (A[1][2] * A[2][0] - A[1][0] * A[2][2]) / det; inv[1][1] = (A[0][0] * A[2][2] - A[0][2] * A[2][0]) / det; inv[1][2] = (A[0][2] * A[1][0] - A[0][0] * A[1][2]) / det;
    inv[2][0] = (A[1][0] * A[2][1] - A[1][1] * A[2][0]) / det; inv[2][1] = (A[0][1] * A[2][0] - A[0][0] * A[2][1]) / det; inv[2][2] = (A[0][0] * A[1][1] - A[0][1] * A[1][0]) / det;
    const int rows = rd16(m->mem, ds, 0x368C) != 0, zoom = m->mem[(((uint32_t)ds << 4) + 0x294B) & 0xFFFFF] != 0;
    const int off = rows ? (zoom ? 0x3C : 0x34) : 0x64;
    static const int px[4] = { -40, 8, 312, 358 }, age[4] = { 0, 1, 2, 3 };
    for (int i = 0; i < 4 && on; i++) {
        const double zc = -64.0;                                              /* [DEBE] = z' >> 3 = -8: the sprite is 256 / 8 = 32 wide before the age's scale */
        const double xc = (px[i] - 160) * zc / 256.0, q = (40 - off) * 4.0 / 3.0, yc = q * zc / 256.0;
        const double c[3] = { xc, yc, zc };
        double v[3];
        for (int r = 0; r < 3; r++) v[r] = inv[r][0] * c[0] + inv[r][1] * c[1] + inv[r][2] * c[2];
        const int ox = (int)lround(v[0]), oa = (int)lround(v[1]), oy = (int)lround(v[2]);
        const uint16_t slot = (uint16_t)(0x3A08 + 8 * (8 + i));
        wr16(m->mem, ds, slot, (uint16_t)(rd16(m->mem, ds, 0xC0D0) - (uint16_t)ox));
        wr16(m->mem, ds, (uint16_t)(slot + 2), (uint16_t)(rd16(m->mem, ds, 0xC0DE) + (uint16_t)oy));
        wr16(m->mem, ds, (uint16_t)(slot + 4), (uint16_t)(rd16(m->mem, ds, 0x2DF4) + (uint16_t)(oa * 32)));
        wr16(m->mem, ds, (uint16_t)(slot + 6), (uint16_t)age[i]);
    }
    if (blast) {                                    /* F117_STAGE_BLAST=X: an air blast (no unit followed) at screen x X (default -30), depth -8, clock 0Ch (phase 4) */
        const int bx = atoi(getenv("F117_STAGE_BLAST"));
        const double zc = -64.0, xc = ((bx ? bx : -30) - 160) * zc / 256.0, q = (60 - off) * 4.0 / 3.0, yc = q * zc / 256.0;
        const double c[3] = { xc, yc, zc };
        double v[3];
        for (int r = 0; r < 3; r++) v[r] = inv[r][0] * c[0] + inv[r][1] * c[1] + inv[r][2] * c[2];
        const int ox = (int)lround(v[0]), oa = (int)lround(v[1]), oy = (int)lround(v[2]);
        wr16(m->mem, ds, 0xC0CC, (uint16_t)(rd16(m->mem, ds, 0xC0D0) - (uint16_t)ox));
        wr16(m->mem, ds, 0xC0D6, (uint16_t)(rd16(m->mem, ds, 0xC0DE) + (uint16_t)oy));
        wr16(m->mem, ds, 0xC0DC, (uint16_t)(rd16(m->mem, ds, 0x2DF4) + (uint16_t)(oa * 32)));
        wr16(m->mem, ds, 0x992E, 0xFFFF);
        wr16(m->mem, ds, 0x9D3C, 0x000C);
    }
}

static int g_probe_label_k = -1, g_probe_marker_k = -1;   /* F117_RING_PROBE=1 (diagnostic): hooks at B4F5 (marker label) and B171 (lock marker) */
static int wide_air_exec(void *user, machine_t *m, unsigned k)
{
    if ((int)k == g_probe_label_k || (int)k == g_probe_marker_k) {   /* log the arguments (cdecl: [sp+2..] after the return address) */
        const uint16_t ds = m->cpu.seg[S_DS], ss = m->cpu.seg[S_SS], sp = m->cpu.r[R_SP];
        if ((int)k == g_probe_label_k)
            fprintf(stderr, "[ring-probe] clock %llu B4F5 label text %04X pen %d margin %d ret %04X | 4A10 %u 4A18 %u 368C %u 294B %u countdown %d S %d\n", (unsigned long long)m->cpu.icount,
                    rd16(m->mem, ss, (uint16_t)(sp + 2)), (int16_t)rd16(m->mem, ss, (uint16_t)(sp + 4)), (int16_t)rd16(m->mem, ss, (uint16_t)(sp + 6)), rd16(m->mem, ss, sp),
                    rd16(m->mem, ds, 0x4A10), rd16(m->mem, ds, 0x4A18), rd16(m->mem, ds, 0x368C), m->mem[(((uint32_t)ds << 4) + 0x294B) & 0xFFFFF], (int16_t)rd16(m->mem, ds, 0x3D98), (int16_t)rd16(m->mem, ds, 0x368E));
        else
            fprintf(stderr, "[ring-probe] clock %llu B171 marker x %d y %d size %d locked %d pen %d ret %04X\n", (unsigned long long)m->cpu.icount, (int16_t)rd16(m->mem, ss, (uint16_t)(sp + 2)), (int16_t)rd16(m->mem, ss, (uint16_t)(sp + 4)),
                    (int16_t)rd16(m->mem, ss, (uint16_t)(sp + 6)), (int16_t)rd16(m->mem, ss, (uint16_t)(sp + 8)), (int16_t)rd16(m->mem, ss, (uint16_t)(sp + 10)), rd16(m->mem, ss, sp));
        return 0;
    }
    if ((int)k == g_walk_k) {
        g_walk_calls++;
        stage_puffs(m);
        far_aircraft_dots(m);
        {   /* F117_VIEW_LOG=1 (diagnostic): once per frame the view matrix words [49BE..] and the aircraft test's [B082..] (the turned-view reference warps one
             * view to the other with them), the heading [2DEE], the picture's row offset flags [368C] [294B] and the clock */
            static int vlog = -1;
            if (vlog < 0) vlog = getenv("F117_VIEW_LOG") != NULL;
            if (vlog) {
                const uint16_t dsv = m->cpu.seg[S_DS];
                fprintf(stderr, "[view] ds %04X clock %llu heading %u 368C %u 294B %u 49BE", dsv, (unsigned long long)m->cpu.icount, rd16(m->mem, dsv, 0x2DEE), rd16(m->mem, dsv, 0x368C), m->mem[(((uint32_t)dsv << 4) + 0x294B) & 0xFFFFF]);
                for (int i = 0; i < 9; i++) fprintf(stderr, " %d", (int)(int16_t)rd16(m->mem, dsv, (uint16_t)(0x49BE + 2 * i)));
                fprintf(stderr, " B082");
                for (int i = 0; i < 9; i++) fprintf(stderr, " %d", (int)(int16_t)rd16(m->mem, dsv, (uint16_t)(0xB082 + 2 * i)));
                fputc(10, stderr);
            }
        }
        { static int init; if (!init) { init = 1; if (getenv("F117_FAR_WALKER")) g_far_native = 0; } }
        return g_walk_busy ? 0 : (g_far_native ? far_scenery(m) : walk_second(m));
    }
    if ((int)k == g_air_ret_k) {         /* 9BDF: back from B5DC for an aircraft; the arguments x, y, alt are still at SS:SP */
        const uint16_t dsr = m->cpu.seg[S_DS], ssr = m->cpu.seg[S_SS], spr = m->cpu.r[R_SP];
        if (!(m->mem[(((uint32_t)dsr << 4) + 0xC0A6) & 0xFFFFF] & 0x80)) {
            const int16_t x = (int16_t)rd16(m->mem, ssr, spr), y = (int16_t)rd16(m->mem, ssr, (uint16_t)(spr + 2)), alt = (int16_t)rd16(m->mem, ssr, (uint16_t)(spr + 4));
            const int16_t ox = (int16_t)(rd16(m->mem, dsr, 0xC0D0) - (uint16_t)x), oy = (int16_t)((uint16_t)y - rd16(m->mem, dsr, 0xC0DE));
            const int16_t oa = (int16_t)((int16_t)((uint16_t)alt - rd16(m->mem, dsr, 0x2DF4)) >> 5);
            const int16_t v[3] = { ox, oa, oy };
            int32_t comp[3];
            for (int a = 0; a < 3; a++) { comp[a] = 0; for (int c = 0; c < 3; c++) comp[a] += air_fixmul(g_air_orig[c][a], v[c]); }
            static unsigned long long total, match_pos, match_neg, offscreen_game, inside_wide; static unsigned shown;
            const uint16_t gx = rd16(m->mem, dsr, 0x4A10), gy = rd16(m->mem, dsr, 0x4A18);
            const int64_t zp = comp[2];
            if (zp != 0) {
                const int64_t sx_pos = ((int64_t)comp[0] * 256) / zp + 0xA0, sx_neg = ((int64_t)comp[0] * 256) / (-zp) + 0xA0;
                if (gx != 0xFFFF) {
                    total++;
                    {   /* the y the margin dots use (F117_WIDE_DOTS), against the game's [4A18] */
                        static unsigned long long ytotal, ymatch; static unsigned yshown;
                        const int16_t q = (int16_t)(((int64_t)comp[1] * 256) / zp);
                        int16_t sy = (int16_t)(q - (int16_t)(q >> 2));
                        if (rd16(m->mem, dsr, 0x368C)) sy = (int16_t)(sy + (m->mem[(((uint32_t)dsr << 4) + 0x294B) & 0xFFFFF] ? 0x3C : 0x34)); else sy = (int16_t)(sy + 0x64);
                        ytotal++; if ((uint16_t)sy == gy) ymatch++;
                        else if (yshown < 6) { yshown++; fprintf(stderr, "[air-probe] y: game %u, formula %d (q %d)\n", gy, sy, q); }
                        if (ytotal % 100 == 0) fprintf(stderr, "[air-probe] y formula matched %llu of %llu on-screen points\n", ymatch, ytotal);
                    }
                    if ((uint16_t)sx_pos == gx) match_pos++;
                    if ((uint16_t)sx_neg == gx) match_neg++;
                    if (shown < 8 && (uint16_t)sx_pos != gx && (uint16_t)sx_neg != gx) { shown++; fprintf(stderr, "[air-probe] game x %u y %u; x' %d y' %d z' %d -> pos %lld neg %lld\n", gx, gy, comp[0], comp[1], comp[2], (long long)sx_pos, (long long)sx_neg); }
                } else {
                    offscreen_game++;
                    static unsigned long long behind, left_far, left_near, right_near, right_far, centre;
                    const int64_t sx = ((int64_t)comp[0] * 256) / zp + 0xA0;
                    if (zp > 0) behind++;                       /* z' is negative in front */
                    else if (sx < -160) left_far++; else if (sx < 0) left_near++; else if (sx >= 640) right_far++; else if (sx >= 320) right_near++; else centre++;
                    if (offscreen_game % 500 == 0) fprintf(stderr, "[air-probe] %llu points off screen for the game (and %llu compared on screen, + formula matched %llu): behind the eye %llu; in front with the true x < -160: %llu, -160..-1: %llu, 0..319 (cut by the view's rows or cone): %llu, 320..639: %llu, 640 or more: %llu\n", offscreen_game, total, match_pos, behind, left_far, left_near, centre, right_near, right_far);
                }
                if (total && total % 25 == 0 && 0) fprintf(stderr, "[air-probe]");
                (void)inside_wide;
            }
        }
        return 0;
    }
    if ((int)k != g_air_k) return g_collector.exec ? g_collector.exec(user, m, k) : 0;
    const uint16_t ds = m->cpu.seg[S_DS];
    if (!g_air_done) {
        g_air_done = 1;
        g_dot_prev.valid = 0;                    /* the margin dots' check never spans two frames */
        /* Row a of the matrix is the words at B082 + 2a, B088 + 2a and B08E + 2a (a = 0 x', 1 y', 2 z'). The x row is divided by the width
         * factor three times over (the cone; the test is on the centre), and all three rows by the reach: the aircraft's size indicator [DEBE]
         * is z' >> 3 and the game draws a model only
         * once it is big enough (else a dot), while every screen position is a ratio of the three and does not move, so dividing them all
         * by 2 makes an aircraft a model at twice the distance. F117_WIDE_AIR_REACH=PERCENT (diagnostic; 100: the game's own distance). */
        static int reach = -1;
        if (reach < 0) { const char *e = getenv("F117_WIDE_AIR_REACH"); reach = e ? atoi(e) : 200; if (reach < 100) reach = 100; }
        static int probe = -1;
        if (probe < 0) probe = getenv("F117_WIDE_AIR_PROBE") != NULL;
        for (int a = 0; a < 3; a++)
            for (int c = 0; c < 3; c++) {
                const uint16_t at = (uint16_t)(0xB082 + 6 * c + 2 * a);
                const int64_t w = (int16_t)rd16(m->mem, ds, at);
                g_air_orig[c][a] = (int16_t)w;
                const int64_t div = (int64_t)reach * (a == 0 ? g_air_pct * 3 : 100);       /* in 1/10000 */
                g_air_r[c][a] = (int16_t)(w * 10000 / ((int64_t)reach * 100));
                g_air_w[c][a] = (int16_t)(w * 10000 / div);
                g_air_ow[c][a] = (int16_t)(a == 0 ? w * 10000 / ((int64_t)g_air_pct * 300) : w);
                if (!probe && g_air_dots <= 0) wr16(m->mem, ds, at, (uint16_t)(int16_t)(w * 10000 / div));
            }
    }
    /* The test is on the aircraft's centre, so a jet right beside the viewer, its centre level with the eye or just behind it, was dropped
     * while its nose or tail was still in view. For the aircraft walk's call (the one returning to 9BDF) such a jet, its centre less than its
     * size (F117_AIR_RADIUS, 0: off) behind the eye, is tested at a point moved forward along the view to just in front of the eye. A jet in
     * front is left alone: the same depth also says how near it is ([DEBE], a model only when near), so moving every jet would turn near
     * ones into dots. Only the copy's test sees the moved point (the arguments are the caller's copies, dropped after the call), and the model
     * is drawn at its own position, Remastered clipping what is behind the eye. The offsets are x = C0D0 - x, y = y - C0DE and
     * alt = (alt - 2DF4) >> 5, and B792 pairs the z' column's words with them as B082 + 4 with x, B088 + 4 with alt and B08E + 4 with y;
     * z' is negative in front. */
    const uint16_t ss = m->cpu.seg[S_SS], sp = m->cpu.r[R_SP];
    static int radius = -1;
    if (radius < 0) { const char *e = getenv("F117_AIR_RADIUS"); radius = e ? atoi(e) : 800; }
    if (radius > 0 && rd16(m->mem, ss, sp) == 0x9BDF && !(m->mem[(((uint32_t)ds << 4) + 0xC0A6) & 0xFFFFF] & 0x80)) {
        const double wx = (int16_t)rd16(m->mem, ds, 0xB082 + 4), wa = (int16_t)rd16(m->mem, ds, 0xB088 + 4), wy = (int16_t)rd16(m->mem, ds, 0xB08E + 4);
        const double l = sqrt(wx * wx + wa * wa + wy * wy);
        const int16_t x = (int16_t)rd16(m->mem, ss, (uint16_t)(sp + 2)), y = (int16_t)rd16(m->mem, ss, (uint16_t)(sp + 4)), alt = (int16_t)rd16(m->mem, ss, (uint16_t)(sp + 6));
        const double ox = (int16_t)(rd16(m->mem, ds, 0xC0D0) - (uint16_t)x), oy = (int16_t)((uint16_t)y - rd16(m->mem, ds, 0xC0DE));
        const double oa = (int16_t)((uint16_t)alt - rd16(m->mem, ds, 0x2DF4)) >> 5;
        const double along = l > 0.0 ? (wx * ox + wy * oy + wa * oa) / l : -1.0;   /* the centre's distance behind the eye (negative: in front) */
        if (along > -16.0 && along < radius) {
            const double step = along + 16.0;                                           /* to 16 units in front */
            const int sx = (int)lround(-step * wx / l), sy = (int)lround(-step * wy / l), sa = (int)lround(-step * wa / l);
            wr16(m->mem, ss, (uint16_t)(sp + 2), (uint16_t)(x - sx));
            wr16(m->mem, ss, (uint16_t)(sp + 4), (uint16_t)(y + sy));
            wr16(m->mem, ss, (uint16_t)(sp + 6), (uint16_t)(alt + 32 * sa));
        }
    }
    if (g_air_dots > 0) {
        /* The routine's depth [DEBE] is the 32-bit z' >> 3 from the matrix in memory (the reach-only one; its z column is the same in both). The caller
         * draws a 3D model when: the aircraft loop (9BDF), [DEBE] < 0 and, shifted right by the byte at [bp - 38], above -32; projectiles (9D89), above -32;
         * the flare (9DFD), between -256 and 0; smoke puffs (9A04) never. Only such a call gets the widened x row (its model is placed by its own position);
         * every other call keeps the game's placement and cull, so a far dot lands where the original puts it. */
        /* Which matrix the call sees (R44): the Remastered reach (an aircraft or projectile a model at twice the distance) is for the two loops that
         * choose between a model and a dot, and only when the reach says model: then the reach-divided matrix with the widened x row, whose [DEBE]
         * is the one the reach intends. Everything else - every dot, smoke puffs (sized from [DEBE]), the flare's distance, the flight display pass's
         * markers - sees the game's own matrix, so its placement, size and cut are the original's to the pixel (a flare model gets only the widened x
         * row). The dot decision agrees with the game's: a point the reach calls a dot has an even more negative [DEBE] in the game's own matrix. */
        const uint16_t ret = rd16(m->mem, ss, sp);
        int16_t v[3];
        air_vector(m, ds, (int16_t)rd16(m->mem, ss, (uint16_t)(sp + 2)), (int16_t)rd16(m->mem, ss, (uint16_t)(sp + 4)), (int16_t)rd16(m->mem, ss, (uint16_t)(sp + 6)), v);
        const int reach_call = ret == 0x9BDF || ret == 0x9D89;
        int32_t z = 0;
        for (int c = 0; c < 3; c++) z += air_fixmul(reach_call ? g_air_r[c][2] : g_air_orig[c][2], v[c]);
        const int16_t depth = (int16_t)(uint16_t)(z >> 3);
        int model = 0;
        if (ret == 0x9BDF) {
            if (depth < 0) {
                const unsigned cl = m->mem[(((uint32_t)ss << 4) + (uint16_t)(m->cpu.r[R_BP] - 38)) & 0xFFFFF];
                const int16_t shifted = cl >= 15 ? (int16_t)-1 : (int16_t)(depth >> cl);
                model = shifted > -32;
            }
        } else if (ret == 0x9D89) model = depth > -32;
        else if (ret == 0x9DFD) model = depth < 0 && depth > -256;
        g_air_last_model = model;
        int16_t (*mat)[3] = model ? (reach_call ? g_air_w : g_air_ow) : g_air_orig;
        for (int a = 0; a < 3; a++)
            for (int c = 0; c < 3; c++)
                wr16(m->mem, ds, (uint16_t)(0xB082 + 6 * c + 2 * a), (uint16_t)mat[c][a]);
        wide_dot_at_entry(m, ds, ss, sp, (model && ret != 0x9DFD) ? 0 : ret);
    }
    return 0;
}

int rerun_draw(const rerun_snap *a, const rerun_snap *b, double t, rerun_list *out, int check)
{
    out->nvals = 0;
    out->nrec = 0;
    out->presented = 0;
    out->insn = 0;
    if (!a || !a->valid) return 0;
    if (!g_copy_mem && !(g_copy_mem = (uint8_t *)malloc(MEM_SIZE))) return 0;
    if (!g_pending && !(g_pending = malloc(observe_pending_size()))) return 0;
    if (!g_fix_pending && !(g_fix_pending = malloc(fixes_pending_size()))) return 0;
    const machine_t *real_machine = (const machine_t *)a->m.cpu.user;
    if (check) {
        if (!real_machine || (!g_before_mem && !(g_before_mem = (uint8_t *)malloc(MEM_SIZE)))) return 0;
        if (!g_fix_after && !(g_fix_after = malloc(fixes_pending_size()))) return 0;
        g_before = *real_machine;
        memcpy(g_before_mem, real_machine->mem, MEM_SIZE);
    }
    machine_t *s = &g_copy;
    *s = a->m;
    memcpy(g_copy_mem, a->mem, MEM_SIZE);
    /* every way back to the real machine, cut */
    s->mem = g_copy_mem;
    s->cpu.mem = g_copy_mem;
    s->cpu.user = s;
    s->cpu.cover = NULL;
    s->cpu.int_hook = copy_int;
    memset(s->files, 0, sizeof s->files);
    memset(&s->hooks, 0, sizeof s->hooks);
    if (g_collector.exec) { s->hooks.exec = g_collector.exec; s->hooks.user = g_collector.user; }
    s->log = NULL;
    s->on_input = NULL;
    s->on_input_user = NULL;
    s->opl_sched = NULL;
    s->opl_sched_n = s->opl_sched_i = s->opl_sched_cap = 0;
    s->trap_on = 0;
    const uint16_t ds = s->cpu.seg[S_DS];
    if (b && b->valid && t > 0) nudge(g_copy_mem, ds, a->mem, b->mem, b->m.cpu.seg[S_DS], t > 1 ? 1 : t);
    {   /* F117_TURN_VIEW=N (diagnostic, the turned-view reference, docs/wide-margin-audit.md): the copy's pilot turned by N units of the game's angle
         * (65536 = 360 degrees) before the frame is drawn. The forward view's heading is [2DEE] (matched.c, compose_forward: [E478] = [2DEE], the view
         * matrix is built from it), so the copy draws the same world from a heading turned by N; the game's own 4:3 picture of the turned view is the
         * reference for what the wide picture shows in its margin. The copy is thrown away: nothing of the real game changes. */
        static int turn = -99999;
        if (turn == -99999) { const char *e = getenv("F117_TURN_VIEW"); turn = e ? atoi(e) : 0; }
        if (turn) wr16(g_copy_mem, ds, 0x2DEE, (uint16_t)(rd16(g_copy_mem, ds, 0x2DEE) + (unsigned)turn));
    }
    {
        static int env = -2;
        if (env == -2) { const char *e = getenv("F117_WIDE_VIEW"); env = e ? atoi(e) : -1; }   /* F117_WIDE_VIEW=PERCENT (diagnostic; 100: off) overrides */
        const int pct = env >= 100 ? env : g_wide_pct;
        if (pct > 100) {
            /* F117_WIDE_REACH=N (experiment, R9): the draw distance raised by N detail steps in the copy: D4A5 sets the six ring sizes
             * [48D6 + 2i] = 0x20 << (i + d), [48E2] = [48DE] + [48E0] and the far plane [48E6] = (d + 1) x 0xD05 from the detail d = [990E];
             * the same words are rewritten here for d + N. The side allowance then has to leave room: the cull adds it to |z'| in 16 bits. */
            static int reach = -1;
            if (reach < 0) { const char *e = getenv("F117_WIDE_REACH"); reach = e ? atoi(e) : 0; }
            unsigned allowance = 18000u;
            if (reach > 0) {
                const unsigned d = (unsigned)rd16(g_copy_mem, ds, 0x990E) + (unsigned)reach;
                for (unsigned i = 0; i < 6; i++) wr16(g_copy_mem, ds, (uint16_t)(0x48D6 + 2 * i), (uint16_t)(0x20u << (i + d)));
                wr16(g_copy_mem, ds, 0x48E2, (uint16_t)(rd16(g_copy_mem, ds, 0x48DE) + rd16(g_copy_mem, ds, 0x48E0)));
                const unsigned far_ = (d + 1u) * 0xD05u;
                wr16(g_copy_mem, ds, 0x48E6, (uint16_t)far_);
                allowance = far_ < 30000u ? 32000u - far_ : 2000u;
                if (allowance > 18000u) allowance = 18000u;
            }
            static int far_env = -1;
            if (far_env < 0) { const char *e = getenv("F117_WIDE_FAR"); far_env = e ? atoi(e) : 0; }   /* F117_WIDE_FAR=UNITS (experiment, R9): the far plane [48E6] in the copy */
            if (far_env > 0) {
                wr16(g_copy_mem, ds, 0x48E6, (uint16_t)far_env);
                allowance = far_env < 30000 ? (unsigned)(32000 - far_env) : 2000u;      /* |z'| + allowance must stay under 0x7FFF */
                if (allowance > 18000u) allowance = 18000u;
            }
            const uint16_t base = rd16(g_copy_mem, ds, 0xE56A);
            for (int md = 0; md < 8; md++) {
                const uint16_t at = (uint16_t)(base + 2 * md);
                /* The near plane tests an object's centre, so a hangar beside the taxiing jet vanished while its near half was still in
                 * view. The game's own side allowance for the class (the table's first value) is its measure of how far the class
                 * reaches from its centre: the near plane moves back by that much (F117_WIDE_NEAR=0 leaves it). */
                static int near_on = -1;
                if (near_on < 0) { const char *e = getenv("F117_WIDE_NEAR"); near_on = !(e && e[0] == '0'); }
                if (near_on) {
                    const int back = rd16(g_copy_mem, ds, at) > 12000 ? 12000 : (int)rd16(g_copy_mem, ds, at);
                    const int v = (int16_t)rd16(g_copy_mem, ds, (uint16_t)(0x48F6 + 2 * md)) - back;
                    wr16(g_copy_mem, ds, (uint16_t)(0x48F6 + 2 * md), (uint16_t)(int16_t)(v < -32000 ? -32000 : v));
                }
                if (rd16(g_copy_mem, ds, at) < allowance) wr16(g_copy_mem, ds, at, (uint16_t)allowance);
            }
            /* The same for the vertical allowance: the cull's up test (|y'| against (|z'| + up allowance) >> cl, the table at ss:[E570], eight words)
             * dropped buildings and terrain once they passed the original view's top or bottom edge, which the wide picture shows in its lower margins
             * (owner, 7 Nov 12:09: things disappear as they move into the bottom left or right). F117_WIDE_UP_ALLOW=0 leaves it. */
            static int up_val = -1;      /* F117_WIDE_UP_ALLOW=N: the allowance in world units (0 off; default the same as the sideways one) */
            if (up_val < 0) { const char *e = getenv("F117_WIDE_UP_ALLOW"); up_val = e ? atoi(e) : (int)allowance; if (up_val > 18000) up_val = 18000; }
            g_up_raised = up_val > 0;
            if (up_val > 0) {
                const uint16_t ssg = s->cpu.seg[S_SS], base_up = rd16(g_copy_mem, ds, 0xE570);
                for (int md = 0; md < 8; md++) {
                    const uint16_t at = (uint16_t)(base_up + 2 * md);
                    if (rd16(g_copy_mem, ssg, at) < (unsigned)up_val) wr16(g_copy_mem, ssg, at, (uint16_t)up_val);
                }
            }
            g_air_k = -1; g_air_done = 0; g_air_pct = pct;
            g_probe_label_k = g_probe_marker_k = -1;
            const int ring_probe = getenv("F117_RING_PROBE") != NULL;
            if (ring_probe && s->exec_hook_n + 2 <= EXEC_HOOKS_MAX) {   /* the probe takes the two spare slots: the aircraft test and the walk are not installed */
                for (unsigned k = 0; k < s->exec_hook_n; k++)
                    if (s->exec_hook[k].ip == 0x1450) {
                        g_probe_label_k = (int)s->exec_hook_n; s->exec_hook[s->exec_hook_n].cs = s->exec_hook[k].cs; s->exec_hook[s->exec_hook_n].ip = 0xB4F5; s->exec_hook_n++;
                        g_probe_marker_k = (int)s->exec_hook_n; s->exec_hook[s->exec_hook_n].cs = s->exec_hook[k].cs; s->exec_hook[s->exec_hook_n].ip = 0xB171; s->exec_hook_n++;
                        s->hooks.exec = wide_air_exec;
                        break;
                    }
            }
            if (!ring_probe && !getenv("F117_WIDE_NO_AIR") && s->exec_hook_n < EXEC_HOOKS_MAX) {      /* F117_WIDE_NO_AIR (diagnostic): the aircraft test left alone */
                for (unsigned k = 0; k < s->exec_hook_n; k++)       /* VGAME's load segment: the per-frame draw routine's watch (0x1450) is at its relative 0 */
                    if (s->exec_hook[k].ip == 0x1450) {
                        g_air_k = (int)s->exec_hook_n;
                        s->exec_hook[s->exec_hook_n].cs = s->exec_hook[k].cs;
                        s->exec_hook[s->exec_hook_n].ip = 0xB5DC;
                        s->exec_hook_n++;
                        s->hooks.exec = wide_air_exec;
                        g_air_ret_k = -1;
                        if (g_air_dots < 0) { const char *e = getenv("F117_WIDE_DOTS"); g_air_dots = !(e && e[0] == '0'); }   /* on by default (owner, 11 Nov); F117_WIDE_DOTS=0 restores the squashed placement */
                        /* The table has room for two hooks past the live ones (EXEC_HOOKS_MAX): this one and the far scenery walk's below. The margin dots
                         * therefore work from this entry hook alone (R44: two return hooks took the walk's slot and the far scenery vanished without a word).
                         * The probe's return hook (diagnostic) is installed after the walk, only if there is room. */
                        break;
                    }
            }
            g_walk_k = -1;
            if (s->exec_hook_n < EXEC_HOOKS_MAX)                    /* the walk's reach is decided when it runs (rerun_set_distance), not here */
                for (unsigned k = 0; k < s->exec_hook_n; k++)
                    if (s->exec_hook[k].ip == 0x1450) {
                        g_walk_k = (int)s->exec_hook_n;
                        s->exec_hook[s->exec_hook_n].cs = s->exec_hook[k].cs;
                        s->exec_hook[s->exec_hook_n].ip = 0x04E6;
                        s->exec_hook_n++;
                        s->hooks.exec = wide_air_exec;
                        break;
                    }
            if (g_air_k >= 0 && getenv("F117_WIDE_AIR_PROBE") && s->exec_hook_n < EXEC_HOOKS_MAX) {
                g_air_ret_k = (int)s->exec_hook_n;
                s->exec_hook[s->exec_hook_n].cs = s->exec_hook[g_air_k].cs;
                s->exec_hook[s->exec_hook_n].ip = 0x9BDF;
                s->exec_hook_n++;
            }
            {   static int warned;                                  /* a hook that did not fit is a picture that silently lacks something: say so */
                const int want_air = !getenv("F117_WIDE_NO_AIR"), want_probe = getenv("F117_WIDE_AIR_PROBE") != NULL;
                int watch = 0;                                      /* VGAME's draw watch present: the hooks belong to this program */
                for (unsigned k = 0; k < s->exec_hook_n; k++) if (s->exec_hook[k].ip == 0x1450) watch = 1;
                if (!warned && watch && ((want_air && g_air_k < 0) || g_walk_k < 0 || (want_probe && g_air_ret_k < 0))) {
                    warned = 1;
                    fprintf(stderr, "[wide] WARNING: render-copy hooks missing (table of %d full or no draw watch): aircraft test %s, far scenery walk %s, probe %s\n",
                            (int)EXEC_HOOKS_MAX, g_air_k >= 0 ? "on" : "MISSING", g_walk_k >= 0 ? "on" : "MISSING", !want_probe ? "not asked" : g_air_ret_k >= 0 ? "on" : "MISSING");
                }
            }
            if (getenv("F117_WIDE_SCENERY_DIAG_FROM")) {
                static int reported;
                if (!reported) {
                    reported = 1;
                    fprintf(stderr,"[scenery] armed view %d distance %d hooks %u walk %d copy %04X:%04X\n",
                        pct,g_distance,s->exec_hook_n,g_walk_k,
                        s->exec_hook_n ? s->exec_hook[0].cs : 0,
                        s->exec_hook_n ? s->exec_hook[0].ip : 0);
                }
            }
        }
    }
    /* run it under its own observer, the real one's pending state set aside */
    const f117_observer *real = g_f117_observer;
    observe_pending_save(g_pending);
    fixes_pending_save(g_fix_pending);
    fixes_pending_restore(a->fix_pending);
    observe_pending_clear();
    observe_set(&g_copy_obs);
    g_out = out;
    g_done = 0;
    g_blocked = 0;
    const uint64_t start = s->cpu.icount, limit = start + (s->ips / 4 > 4000000ull ? s->ips / 4 : 4000000ull);
    if (g_prof < 0) g_prof = getenv("F117_PROFILE") != NULL;
    if (g_prof) { g_prof_m = s; memset(g_prof_set, 0, sizeof g_prof_set); g_prof_ic[0] = s->cpu.icount; g_prof_ms[0] = walk_ms(); g_prof_set[0] = 1; }
    if (g_collector.begin) g_collector.begin(g_collector.user);
    while (!g_done && s->cpu.icount < limit)
        if (machine_run(s, s->cpu.icount + 20000) != RUN_SLICE) break;
    if (g_collector.end) g_collector.end(g_collector.user);
    if (g_prof) {
        rerun_prof_mark(7);
        /* labels: 0 the copy starts, 1 the per-frame draw routine is entered (the end of the frame being drawn), 2 the game's terrain walk begins (the far
         * scenery starts), 3 the far scenery is done, 4 the HUD pass, 7 the copy ends. Taken in the order they happened. */
        static const char *name[PROF_MARKS] = { "copy start", "frame draw routine", "terrain walk", "far scenery done", "HUD pass", "", "", "copy end" };
        static double sum_ms[PROF_MARKS][PROF_MARKS], sum_ic[PROF_MARKS][PROF_MARKS]; static unsigned n, seen[PROF_MARKS][PROF_MARKS];
        int order[PROF_MARKS], k = 0;
        for (int l = 0; l < PROF_MARKS; l++) if (g_prof_set[l]) order[k++] = l;
        for (int i = 1; i < k; i++) for (int j = i; j > 0 && g_prof_ic[order[j]] < g_prof_ic[order[j - 1]]; j--) { const int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }
        for (int i = 1; i < k; i++) {
            const int a = order[i - 1], c2 = order[i];
            sum_ms[a][c2] += g_prof_ms[c2] - g_prof_ms[a]; sum_ic[a][c2] += (double)(g_prof_ic[c2] - g_prof_ic[a]); seen[a][c2]++;
        }
        if (++n % 240 == 0) {
            fprintf(stderr, "[profile] mean of %u copies, per stretch (wall ms, original instructions):\n", n);
            double tot_ms = 0, tot_ic = 0;
            for (int a = 0; a < PROF_MARKS; a++) for (int c2 = 0; c2 < PROF_MARKS; c2++) if (seen[a][c2]) {
                const double ms = sum_ms[a][c2] / n, ic = sum_ic[a][c2] / n;       /* per copy, whether or not the stretch occurs in every one */
                fprintf(stderr, "[profile]   %-18s to %-18s %6.2f ms %9.0f\n", name[a], name[c2], ms, ic);
                tot_ms += ms; tot_ic += ic;
            }
            fprintf(stderr, "[profile]   %-42s %6.2f ms %9.0f\n", "total", tot_ms, tot_ic);
        }
    }
    if (getenv("F117_WIDE_VIEW_DIAG")) fprintf(stderr, "[wide] copy records %d, vertices-ish values %zu\n", out->nrec, out->nvals);
    if (getenv("F117_WIDE_SCENERY_DIAG_FROM")) {
        static unsigned reported;
        if (reported++ < 4)
            fprintf(stderr,"[scenery] draw clock %llu walk calls %u collector %d\n",
                (unsigned long long)a->m.cpu.icount,g_walk_calls,g_collector.exec != NULL);
    }
    g_out = NULL;
    observe_set(real);
    observe_pending_restore(g_pending);
    fixes_pending_restore(g_fix_pending);
    free(s->opl_sched);               /* a fix may have scheduled private speech during the copy */
    s->opl_sched = NULL;
    out->blocked += g_blocked != 0;
    if (check) {
        fixes_pending_save(g_fix_after);
        out->checks++;
        if (memcmp(&g_before, real_machine, sizeof g_before) || memcmp(g_before_mem, real_machine->mem, MEM_SIZE) ||
            memcmp(g_fix_pending, g_fix_after, fixes_pending_size())) {
            out->changed++;
            return 0;
        }
    }
    out->insn = s->cpu.icount - start;
    /* the records, as interp_rec over the values */
    out->nrec = 0;
    out->first = -1;
    for (size_t at = 0; at + 4 <= out->nvals;) {
        const int32_t *p = out->vals + at;
        if (p[0] == 'P') {                            /* not a draw record: it marks where the frame begins */
            if (out->first < 0) out->first = out->nrec;
            at += 4 + (size_t)p[1];
            continue;
        }
        if (out->nrec == out->crec) {
            out->crec = out->crec ? 2 * out->crec : 4096;
            interp_rec *rec = (interp_rec *)realloc(out->rec, (size_t)out->crec * sizeof *out->rec);
            if (!rec) { out->crec /= 2; return 0; }
            out->rec = rec;
        }
        const interp_rec r = { (char)p[0], (uint64_t)(uint32_t)p[2] | (uint64_t)(uint32_t)p[3] << 32, p + 4, p[1] };
        out->rec[out->nrec++] = r;
        at += 4 + (size_t)p[1];
    }
    return out->presented && g_done && !g_blocked;
}
