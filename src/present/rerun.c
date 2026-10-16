/* rerun.c - see rerun.h. */
#include "rerun.h"
#include "observe.h"
#include "../fixes/fixes.h"

#include <stdlib.h>
#include <string.h>

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

/* The inputs the composition reads, moved t of the way: the aircraft (32-bit X/Y at BA3C/C056; heading, pitch, roll,
 * height above ground and altitude at 2DEE..2DF6); the computer-flown aircraft (VGAME 5852: 24h-byte slots at C16A,
 * [DEFC] of them; +2/+4 the X/Y at 1/32 of +8/+0C, +6 height, +10/+12/+14 attitude, only a slot of the same type
 * and state in both); the weapons in flight (VGAME 683E: twelve 1Ch-byte records at 3C3A; +0/+2/+4 X/Y/height,
 * +6 pitch, +8 heading, +0E life, +10 type, only one alive with the same type in both). */
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
            static const uint16_t f[] = { 0x06, 0x10, 0x12, 0x14 };
            for (int k = 0; k < 4; k++) nudge16(dst, ds, amem, bmem, bds, (uint16_t)(at + f[k]), t);
        }
    for (int i = 0; i < 12; i++) {
        const uint16_t at = (uint16_t)(0x3C3A + 0x1C * i);
        if (!rd16(amem, ds, (uint16_t)(at + 0x0E)) || !rd16(bmem, bds, (uint16_t)(at + 0x0E)) ||
            rd16(amem, ds, (uint16_t)(at + 0x10)) != rd16(bmem, bds, (uint16_t)(at + 0x10))) continue;
        static const uint16_t f[] = { 0x00, 0x02, 0x04, 0x06, 0x08 };
        for (int k = 0; k < 5; k++) nudge16(dst, ds, amem, bmem, bds, (uint16_t)(at + f[k]), t);
    }
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
    s->log = NULL;
    s->on_input = NULL;
    s->on_input_user = NULL;
    s->opl_sched = NULL;
    s->opl_sched_n = s->opl_sched_i = s->opl_sched_cap = 0;
    s->trap_on = 0;
    const uint16_t ds = s->cpu.seg[S_DS];
    if (b && b->valid && t > 0) nudge(g_copy_mem, ds, a->mem, b->mem, b->m.cpu.seg[S_DS], t > 1 ? 1 : t);
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
    while (!g_done && s->cpu.icount < limit)
        if (machine_run(s, s->cpu.icount + 20000) != RUN_SLICE) break;
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
