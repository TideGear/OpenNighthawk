/* rerun.c - see rerun.h. */
#include "rerun.h"
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
static int g_distance = 1 << 22;                          /* world units: every ring out to the 16-bit limit (ring 4: the whole map) */
void rerun_set_distance(int units) { g_distance = units < 0 ? 0 : units > (1 << 26) ? (1 << 26) : units; }
static unsigned g_walk_calls;
static int walk_second(machine_t *m)
{
    cpu_t *c = &m->cpu;
    static int env_done;
    if (!env_done) {
        env_done = 1;
        const char *e = getenv("F117_WIDE_DISTANCE");
        if (e) g_distance = atoi(e) < 0 ? 0 : atoi(e);
        const char *s = getenv("F117_WIDE_SCENERY");
        if (s && atoi(s) == 0) g_distance = 0;
    }
    if (g_distance <= 0) return 0;
    const uint64_t icount_before = c->icount;
    const double ms_before = walk_ms();
    int passes = 0;
    static double ring_ins[5], ring_cells[5], ring_passes[5]; static unsigned ring_n;
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
        const uint64_t ring_ic = c->icount; const int ring_p0 = passes;
        const long cell = 4096L << (2 * (ring - 1));              /* a cell's width in world units on this ring */
        int radius = (int)((g_distance + cell - 1) / cell);
        if (radius > 7) radius = 7;                               /* the 16-bit limit of a cell offset */
        if (radius < 2) radius = 2;
        for (int r = 1; r <= 4; r++) wr16(m->mem, ds, (uint16_t)(0x04FA + 2 * r), r == ring ? enable[r] : 0);
        if (!enable[ring]) continue;
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
        ring_ins[ring] += (double)(c->icount - ring_ic); ring_cells[ring] += n; ring_passes[ring] += passes - ring_p0;
    }
    ring_n++;
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
                for (int r = 4; r >= 1; r--) fprintf(stderr, "[walk]   ring %d: %.0f cells, %.1f passes, %.0f instructions\n", r, ring_cells[r] / ring_n, ring_passes[r] / ring_n, ring_ins[r] / ring_n);
            }
        }
    }
    for (int i = 0; i < 8; i++) c->r[i] = sv_r[i];
    for (int i = 0; i < 4; i++) c->seg[i] = sv_seg[i];
    c->ip = sv_ip; c->flags = sv_fl;
    return 0;
}

static int g_air_k = -1, g_air_done, g_air_pct = 100;
static int wide_air_exec(void *user, machine_t *m, unsigned k)
{
    if ((int)k == g_walk_k) {
        g_walk_calls++;
        return g_walk_busy ? 0 : walk_second(m);
    }
    if ((int)k != g_air_k) return g_collector.exec ? g_collector.exec(user, m, k) : 0;
    const uint16_t ds = m->cpu.seg[S_DS];
    if (!g_air_done) {
        g_air_done = 1;
        /* Row a of the matrix is the words at B082 + 2a, B088 + 2a and B08E + 2a (a = 0 x', 1 y', 2 z'). The x row is divided by the width
         * factor three times over (the cone; the test is on the centre), and all three rows by the reach: the aircraft's size indicator [DEBE]
         * is z' >> 3 and the game draws a model only
         * once it is big enough (else a dot), while every screen position is a ratio of the three and does not move, so dividing them all
         * by 2 makes an aircraft a model at twice the distance. F117_WIDE_AIR_REACH=PERCENT (diagnostic; 100: the game's own distance). */
        static int reach = -1;
        if (reach < 0) { const char *e = getenv("F117_WIDE_AIR_REACH"); reach = e ? atoi(e) : 200; if (reach < 100) reach = 100; }
        for (int a = 0; a < 3; a++)
            for (int c = 0; c < 3; c++) {
                const uint16_t at = (uint16_t)(0xB082 + 6 * c + 2 * a);
                const int64_t w = (int16_t)rd16(m->mem, ds, at);
                const int64_t div = (int64_t)reach * (a == 0 ? g_air_pct * 3 : 100);       /* in 1/10000 */
                wr16(m->mem, ds, at, (uint16_t)(int16_t)(w * 10000 / div));
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
            g_air_k = -1; g_air_done = 0; g_air_pct = pct;
            if (!getenv("F117_WIDE_NO_AIR") && s->exec_hook_n < EXEC_HOOKS_MAX) {      /* F117_WIDE_NO_AIR (diagnostic): the aircraft test left alone */
                for (unsigned k = 0; k < s->exec_hook_n; k++)       /* VGAME's load segment: the per-frame draw routine's watch (0x1450) is at its relative 0 */
                    if (s->exec_hook[k].ip == 0x1450) {
                        g_air_k = (int)s->exec_hook_n;
                        s->exec_hook[s->exec_hook_n].cs = s->exec_hook[k].cs;
                        s->exec_hook[s->exec_hook_n].ip = 0xB5DC;
                        s->exec_hook_n++;
                        s->hooks.exec = wide_air_exec;
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
    if (g_collector.begin) g_collector.begin(g_collector.user);
    while (!g_done && s->cpu.icount < limit)
        if (machine_run(s, s->cpu.icount + 20000) != RUN_SLICE) break;
    if (g_collector.end) g_collector.end(g_collector.user);
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
