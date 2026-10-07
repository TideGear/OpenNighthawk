/* recomp.c - the run-time of the recompiled code. See recomp_rt.h.
 *
 * An INSTANCE is one module where DOS put it: the module's image occupies
 * linear [lo, hi) from the instance's base segment (plus 0x100 for a .COM).
 * Every program and overlay DOS loads becomes an instance, recognised by
 * the hash of its file; one the recompiler never saw is registered anyway,
 * untranslated, so coverage and misses can still name it.
 *
 * Dispatch: CS:IP inside an instance, an entry for that image offset whose
 * region expects this CS (base + region segment), and a region whose bytes
 * are verified - then the region runs. Otherwise the interpreter runs one
 * instruction. A region is verified by comparing every byte it was
 * translated from (relocation sites and far-branch operands excepted: the
 * translation reads those from memory) with memory now; the verdict holds
 * until a write changes one of those bytes, which bumps the instance's
 * generation and makes the running code stop at the next instruction.
 */
#include "recomp_rt.h"
#include "recomp_gen.h"
#include "matched.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t cpu_codebits[MEM_SIZE / 8];
unsigned long long rc_mutant_hits;

#define MAX_PLACED 256        /* overrides placed in one loaded module */

typedef struct instance {
    const rc_module *mod;          /* NULL: a module with no translation */
    char      name[16];
    uint64_t  file_hash;
    uint16_t  base;                /* CS of the image's segment 0 */
    uint32_t  lo, hi;              /* linear range of the image */
    uint32_t  gen;                 /* bumped by every write to a verified byte */
    uint32_t *ok_gen;              /* per region: the generation it verified at (0 = never) */
    uint32_t *bad_gen;             /* per region: the generation it failed at */
    /* coverage / misses: interpreted instruction starts in this image */
    uint8_t  *seen;                /* per image byte: 1 = an instruction started here */
    uint16_t *seen_seg;            /* the segment (CS - base) it ran under */
    uint8_t  *seen_bytes;          /* 4 bytes per offset, as executed */
    int       live;
    int       ran;                 /* some region has verified: its code is running */
    int       floating;            /* recognised by content, not loaded by DOS */
    /* enabled overrides placed in this image, and the regions they refuse */
    uint32_t  ov_lin[MAX_PLACED];
    int       ov_index[MAX_PLACED];
    int       nov;
    uint8_t  *refused;             /* per region: contains an override */
} instance;

#define MAX_INST 32

typedef struct {
    instance  inst[MAX_INST];
    instance *by_para[0x10000];    /* paragraph -> instance */
    uint32_t *entry_of[64];        /* per module: image offset -> entry index + 1 */
    int       coverage;            /* record interpreted instructions */
    char      coverage_path[600];
    uint64_t  dispatches, region_runs, verify_ok, verify_fail, code_writes;
    uint64_t  translated_start;    /* icount bookkeeping for the report */
    /* Code segments already checked for a floating module and found none,
     * since the last program load (one bit per CS value). */
    uint8_t   floating_no[0x10000 / 8];
    int       nfloating;           /* live floating instances */
    instance *last_exec;           /* the program DOS loaded last */
} rt_t;

static rt_t g_rt;

static uint64_t fnv1a64(const uint8_t *p, size_t n)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}

static int module_index(const rc_module *m)
{
    for (unsigned i = 0; i < RC_NMODULES && i < 64; i++)
        if (RC_MODULES[i] == m) return (int)i;
    return -1;
}

/* The image-offset index, built once per module. */
static const uint32_t *entry_index(const rc_module *m)
{
    int k = module_index(m);
    if (k < 0) return NULL;
    if (!g_rt.entry_of[k]) {
        uint32_t *t = (uint32_t *)calloc(m->size ? m->size : 1, sizeof *t);
        for (uint32_t i = 0; i < m->nentries; i++)
            if (m->entries[i].off < m->size && !t[m->entries[i].off]) t[m->entries[i].off] = i + 1;
        g_rt.entry_of[k] = t;
    }
    return g_rt.entry_of[k];
}

static void set_codebits(const instance *in, int on)
{
    if (!in->mod) return;
    const rc_module *m = in->mod;
    for (uint32_t r = 0; r < m->nruns; r++)
        for (uint32_t k = 0; k < m->runs[r].len; k++) {
            uint32_t a = (in->lo + m->runs[r].off + k) & 0xFFFFFu;
            if (on) cpu_codebits[a >> 3] |= (uint8_t)(1u << (a & 7));
            else    cpu_codebits[a >> 3] &= (uint8_t)~(1u << (a & 7));
        }
}

static void flush_coverage(instance *in);

/* ---- code overrides (see recomp_rt.h) ---------------------------------- */

#define MAX_OVERRIDES 1024

static recomp_override g_ov[MAX_OVERRIDES];
static int g_ov_on[MAX_OVERRIDES];
static unsigned long long g_ov_hits[MAX_OVERRIDES];   /* times it ran */
static int g_nov;
static uint8_t g_ov_bits[MEM_SIZE / 8];   /* linear address -> an override is placed */
int recomp_overrides_live;

static void count_live_overrides(void)
{
    int n = 0;
    for (int i = 0; i < MAX_INST; i++) if (g_rt.inst[i].live) n += g_rt.inst[i].nov;
    recomp_overrides_live = n;
}

static void unplace_overrides(instance *in)
{
    for (int k = 0; k < in->nov; k++)
        g_ov_bits[in->ov_lin[k] >> 3] &= (uint8_t)~(1u << (in->ov_lin[k] & 7));
    in->nov = 0;
    if (in->refused && in->mod) memset(in->refused, 0, in->mod->nregions ? in->mod->nregions : 1);
}

/* Place the enabled overrides that belong to this module, and refuse every
 * translated region whose bytes include one of their addresses: a region
 * jumps within itself without returning to the dispatcher. */
static void place_overrides(instance *in)
{
    unplace_overrides(in);
    if (!in->live || in->floating) return;
    for (int i = 0; i < g_nov && in->nov < MAX_PLACED; i++) {
        const recomp_override *o = &g_ov[i];
        if (!g_ov_on[i] || strcmp(o->module, in->name) != 0) continue;
        if (o->file_hash && o->file_hash != in->file_hash) continue;
        const uint32_t lin = phys((uint16_t)(in->base + o->seg), o->ip);
        if (lin < in->lo || lin >= in->hi) continue;
        in->ov_lin[in->nov] = lin;
        in->ov_index[in->nov++] = i;
        g_ov_bits[lin >> 3] |= (uint8_t)(1u << (lin & 7));
        if (!in->mod) continue;
        const rc_module *m = in->mod;
        if (!in->refused) in->refused = (uint8_t *)calloc(m->nregions ? m->nregions : 1, 1);
        if (!in->refused) continue;
        const uint32_t off = lin - in->lo;
        for (uint32_t r = 0; r < m->nregions; r++)
            for (uint32_t k = 0; k < m->regions[r].nruns; k++) {
                const rc_run *run = &m->runs[m->regions[r].run_first + k];
                if (off >= run->off && off < run->off + run->len) { in->refused[r] = 1; break; }
            }
    }
}

int recomp_override_add(const recomp_override *o)
{
    if (g_nov >= MAX_OVERRIDES || !o || !o->id || !o->module || !o->fn) return -1;
    g_ov[g_nov] = *o;
    const char *off = getenv("F117R_NO_MATCHED");
    g_ov_on[g_nov] = o->matched && !(off && off[0] == '1');
    /* F117R_MATCHED_LIMIT=N: only the first N matched routines, in table
     * order - for bisecting a divergence between the engines. */
    static int limit = -2, seen;
    if (limit == -2) { const char *l = getenv("F117R_MATCHED_LIMIT"); limit = l && *l ? atoi(l) : -1; }
    if (o->matched && limit >= 0 && seen++ >= limit) g_ov_on[g_nov] = 0;
    return g_nov++;
}

int recomp_override_enable(const char *id, int on)
{
    int n = 0;
    for (int i = 0; i < g_nov; i++)
        if (!g_ov[i].matched && (!strcmp(id, "all") || !strcmp(id, g_ov[i].id))) { g_ov_on[i] = on != 0; n++; }
    for (int i = 0; i < MAX_INST; i++) if (g_rt.inst[i].live) place_overrides(&g_rt.inst[i]);
    count_live_overrides();
    return n;
}

void recomp_override_ids(char *out, size_t n)
{
    size_t used = 0;
    if (n) out[0] = 0;
    for (int i = 0; i < g_nov; i++) {
        if (!g_ov_on[i] || g_ov[i].matched) continue;
        int seen = 0;
        for (int k = 0; k < i; k++) if (g_ov_on[k] && !strcmp(g_ov[k].id, g_ov[i].id)) seen = 1;
        if (seen) continue;
        int w = snprintf(out + used, n > used ? n - used : 0, "%s%s", used ? " " : "", g_ov[i].id);
        if (w > 0) used += (size_t)w;
    }
}

void recomp_override_list(FILE *f)
{
    for (int i = 0; i < g_nov; i++)
        fprintf(f, "%-6s %-3s %s %04X:%04X  %s\n", g_ov[i].id, g_ov_on[i] ? "on" : "off",
                g_ov[i].module, g_ov[i].seg, g_ov[i].ip, g_ov[i].what ? g_ov[i].what : "");
}

/* F117R_SHADOW=FROM:TO (instruction counts): in that window every matched
 * routine that runs is checked against the original on the live machine. The
 * state is saved, the matched routine runs and its result is kept, the state
 * is put back, and the original code runs (every matched routine off) up to
 * the clock the matched one stopped at; the two end states - registers,
 * flags, clock, and all of memory - are compared and any difference printed.
 * A routine that declines is checked to have changed nothing. The original's
 * result is the one the run continues from, so a shadowed run stays on the
 * original's path. Devices are not part of the snapshot: use it on routines
 * that touch no ports. */
static uint64_t g_shadow_lo, g_shadow_hi;
static int g_shadow_on, g_shadow_off;
static uint8_t *g_shadow_mem[2];
static unsigned long long g_shadow_checked, g_shadow_declined, g_shadow_bad;

static void shadow_parse(void)
{
    static int done;
    if (done) return;
    done = 1;
    const char *e = getenv("F117R_SHADOW");
    if (!e || !*e) return;
    unsigned long long lo = 0, hi = ~0ull;
    if (sscanf(e, "%llu:%llu", &lo, &hi) < 1) return;
    g_shadow_lo = lo; g_shadow_hi = hi;
    g_shadow_mem[0] = (uint8_t *)malloc(MEM_SIZE);
    g_shadow_mem[1] = (uint8_t *)malloc(MEM_SIZE);
    g_shadow_on = g_shadow_mem[0] && g_shadow_mem[1];
}

static const char *shadow_cpu_diff(const cpu_t *a, const cpu_t *b)
{
    if (memcmp(a->r, b->r, sizeof a->r)) return "registers";
    if (memcmp(a->seg, b->seg, sizeof a->seg)) return "segments";
    if (a->ip != b->ip) return "ip";
    if (a->flags != b->flags) return "flags";
    if (a->icount != b->icount) return "clock";
    return NULL;
}

/* Runs the routine under shadow; returns what o->fn returned. */
static int shadow_run(machine_t *m, const recomp_override *o)
{
    cpu_t *c = &m->cpu;
    const cpu_t before = *c;
    memcpy(g_shadow_mem[0], m->mem, MEM_SIZE);
    const int r = o->fn(m);
    g_shadow_checked++;
    if (!r) {                                                     /* a decline must leave everything as it was */
        g_shadow_declined++;
        const char *why = shadow_cpu_diff(&before, c);
        if (!why && memcmp(g_shadow_mem[0], m->mem, MEM_SIZE)) why = "memory";
        if (why) {
            g_shadow_bad++;
            fprintf(stderr, "[shadow] %s %04X:%04X declined at clock %llu but changed %s\n", o->module, o->seg, o->ip,
                    (unsigned long long)before.icount, why);
            *c = before;
            memcpy(m->mem, g_shadow_mem[0], MEM_SIZE);
        }
        return 0;
    }
    const cpu_t after = *c;
    memcpy(g_shadow_mem[1], m->mem, MEM_SIZE);
    *c = before;
    memcpy(m->mem, g_shadow_mem[0], MEM_SIZE);
    const uint64_t run_until = m->run_until;
    g_shadow_off = 1;
    machine_run(m, after.icount);
    g_shadow_off = 0;
    m->run_until = run_until;                                     /* the outer run's limits, which the nested run overwrote */
    const uint64_t replay_stop = c->stop_at;
    c->stop_at = after.stop_at;
    (void)replay_stop;
    const char *why = shadow_cpu_diff(c, &after);
    uint32_t at = 0;
    if (!why) {
        while (at < MEM_SIZE && m->mem[at] == g_shadow_mem[1][at]) at++;
        if (at < MEM_SIZE) why = "memory";
    }
    if (why) {
        g_shadow_bad++;
        fprintf(stderr, "[shadow] %s %04X:%04X from clock %llu to %llu: %s differs", o->module, o->seg, o->ip,
                (unsigned long long)before.icount, (unsigned long long)after.icount, why);
        if (!strcmp(why, "memory")) {
            fprintf(stderr, " at %05X (original %02X, matched %02X)", at, m->mem[at], g_shadow_mem[1][at]);
            unsigned nd = 0;
            for (uint32_t x = at; x < MEM_SIZE && nd < 24; x++)
                if (m->mem[x] != g_shadow_mem[1][x]) { fprintf(stderr, "%s%05X:%02X/%02X", nd ? " " : "; ", x, m->mem[x], g_shadow_mem[1][x]); nd++; }
        } else {
            fprintf(stderr, "; AX %04X/%04X BX %04X/%04X CX %04X/%04X DX %04X/%04X SI %04X/%04X DI %04X/%04X BP %04X/%04X SP %04X/%04X IP %04X/%04X FL %04X/%04X",
                    c->r[R_AX], after.r[R_AX], c->r[R_BX], after.r[R_BX], c->r[R_CX], after.r[R_CX], c->r[R_DX], after.r[R_DX],
                    c->r[R_SI], after.r[R_SI], c->r[R_DI], after.r[R_DI], c->r[R_BP], after.r[R_BP], c->r[R_SP], after.r[R_SP],
                    c->ip, after.ip, c->flags, after.flags);
        }
        fprintf(stderr, " (original/matched)\n");
    }
    return 1;                                                     /* the machine now holds the original's result */
}

int recomp_override_step(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t cs = c->seg[S_CS];
    const uint32_t lin = phys(cs, c->ip);
    if (!((g_ov_bits[lin >> 3] >> (lin & 7)) & 1)) return 0;
    instance *in = g_rt.by_para[lin >> 4];
    if (!in || !in->live) return 0;
    for (int k = 0; k < in->nov; k++) {
        const recomp_override *o = &g_ov[in->ov_index[k]];
        if (in->ov_lin[k] != lin || (uint16_t)(in->base + o->seg) != cs) continue;
        if (o->matched && m->engine != ENGINE_RECOMP) return 0;
        if (o->matched && g_shadow_off) return 0;
        if (o->matched && g_shadow_on && c->icount >= g_shadow_lo && c->icount < g_shadow_hi) {
            if (!shadow_run(m, o)) return -1;
        } else if (!o->fn(m)) return -1;
        g_ov_hits[in->ov_index[k]]++;
        return 1;
    }
    return 0;
}

static void unregister(instance *in)
{
    if (!in->live) return;
    if (in->floating && g_rt.nfloating) g_rt.nfloating--;
    if (g_rt.last_exec == in) g_rt.last_exec = NULL;
    unplace_overrides(in);
    free(in->refused);
    flush_coverage(in);
    set_codebits(in, 0);
    for (uint32_t p = in->lo >> 4; p <= ((in->hi - 1) >> 4) && p < 0x10000; p++)
        if (g_rt.by_para[p] == in) g_rt.by_para[p] = NULL;
    free(in->ok_gen); free(in->bad_gen);
    free(in->seen); free(in->seen_seg); free(in->seen_bytes);
    memset(in, 0, sizeof *in);
}

void recomp_init(machine_t *m)
{
    (void)m;
    for (int i = 0; i < MAX_INST; i++) unregister(&g_rt.inst[i]);
    matched_register();
    shadow_parse();
    count_live_overrides();
    memset(cpu_codebits, 0, sizeof cpu_codebits);
    const char *cov = getenv("F117R_COVERAGE");
    if (cov && *cov) {
        g_rt.coverage = 1;
        snprintf(g_rt.coverage_path, sizeof g_rt.coverage_path, "%s", cov);
    }
}

void recomp_set_coverage(machine_t *m, const char *path)
{
    (void)m;
    g_rt.coverage = path && *path;
    snprintf(g_rt.coverage_path, sizeof g_rt.coverage_path, "%s", path ? path : "");
}

/* The size of an untranslated EXEC'd program: to the end of its block. */
static uint32_t block_bytes(machine_t *m, uint16_t seg)
{
    const uint16_t end = dos_block_end(m, seg);
    return end ? (uint32_t)(end - seg) * 16u : 0x10000u;
}

void recomp_module_load(void *user, machine_t *m, const char *name,
                        const uint8_t *file, size_t len, int kind,
                        uint16_t load_seg, uint16_t reloc)
{
    (void)user; (void)reloc;
    const uint64_t h = fnv1a64(file, len);
    const rc_module *mod = NULL;
    for (unsigned i = 0; i < RC_NMODULES; i++)
        if (!RC_MODULES[i]->floating && RC_MODULES[i]->file_hash == h) { mod = RC_MODULES[i]; break; }
    /* A new program may place a decompressor anywhere: forget which code
     * segments held none. */
    memset(g_rt.floating_no, 0, sizeof g_rt.floating_no);

    uint32_t origin = kind == MODLOAD_COM ? 0x100u : 0u;
    uint32_t lo = (uint32_t)load_seg * 16u + origin;
    uint32_t size;
    if (mod) size = mod->size;
    else if (kind == MODLOAD_OVERLAY) size = (uint32_t)len;
    else size = block_bytes(m, load_seg) - origin;
    if (!size) size = 16;
    uint32_t hi = lo + size;
    if (hi > MEM_SIZE) hi = MEM_SIZE;

    /* Anything this load overwrites is gone. */
    for (int i = 0; i < MAX_INST; i++) {
        instance *o = &g_rt.inst[i];
        if (o->live && o->lo < hi && lo < o->hi) unregister(o);
    }
    instance *in = NULL;
    for (int i = 0; i < MAX_INST && !in; i++) if (!g_rt.inst[i].live) in = &g_rt.inst[i];
    if (!in) { unregister(&g_rt.inst[0]); in = &g_rt.inst[0]; }

    memset(in, 0, sizeof *in);
    in->live = 1;
    in->mod = mod;
    snprintf(in->name, sizeof in->name, "%s", name);
    for (char *p = in->name; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    in->file_hash = h;
    in->base = load_seg;
    in->lo = lo;
    in->hi = hi;
    in->gen = 1;
    if (mod) {
        in->ok_gen = (uint32_t *)calloc(mod->nregions ? mod->nregions : 1, sizeof(uint32_t));
        in->bad_gen = (uint32_t *)calloc(mod->nregions ? mod->nregions : 1, sizeof(uint32_t));
        entry_index(mod);
        set_codebits(in, 1);
    }
    for (uint32_t p = lo >> 4; p <= ((hi - 1) >> 4) && p < 0x10000; p++) g_rt.by_para[p] = in;
    if (kind != MODLOAD_OVERLAY) g_rt.last_exec = in;
    place_overrides(in);
    count_live_overrides();
    dos_log(m, "[recomp] %s at %04X: %s\n", in->name, load_seg,
            mod ? "translated" : "no translation (interpreted)");
}

void cpu_code_written(cpu_t *c, uint32_t lin)
{
    instance *in = g_rt.by_para[lin >> 4];
    g_rt.code_writes++;
    if (in && lin >= in->lo && lin < in->hi) {
        in->gen++;
        /* Which translated bytes get rewritten, once each: a byte the
         * program writes as it runs is data the discovery took for code,
         * or code that really is modified - either way worth knowing. */
        static uint32_t logged[64];
        static int nlogged;
        machine_t *m = machine_of(c);
        if (m && m->log && nlogged < 64 && in->ran) {
            int seen = 0;
            for (int i = 0; i < nlogged; i++) if (logged[i] == lin) { seen = 1; break; }
            if (!seen) {
                logged[nlogged++] = lin;
                dos_log(m, "[recomp] write to translated byte %s+%05X by %04X:%04X @%llu\n",
                        in->name, lin - in->lo, c->op_cs, c->op_ip, (unsigned long long)c->icount);
            }
        }
    }
    c->stop_at = 0;      /* the running region may just have changed */
}

/* Are the bytes this region was translated from the bytes in memory? */
static int verify(const uint8_t *mem, const instance *in, const rc_region *r)
{
    const rc_module *m = in->mod;
    for (uint32_t k = 0; k < r->nruns; k++) {
        const rc_run *run = &m->runs[r->run_first + k];
        const uint32_t a = in->lo + run->off;
        if (a + run->len > MEM_SIZE) return 0;
        if (memcmp(mem + a, m->image + run->off, run->len) != 0) return 0;
    }
    return 1;
}

/* Code running outside every loaded module: is it a floating module (the
 * LZEXE decompressor, which copies itself above the program it unpacks)?
 * Recognised by its code at CS:probe_off, and registered there - over
 * paragraphs no loaded module owns, so the program being unpacked keeps
 * its own. */
static instance *try_floating(machine_t *m, uint16_t cs)
{
    if ((g_rt.floating_no[cs >> 3] >> (cs & 7)) & 1) return NULL;
    for (unsigned i = 0; i < RC_NMODULES; i++) {
        const rc_module *mod = RC_MODULES[i];
        if (!mod->floating) continue;
        const uint32_t at = (uint32_t)cs * 16u + mod->probe_off;
        if (at + mod->probe_len > MEM_SIZE ||
            memcmp(m->mem + at, mod->image + mod->probe_off, mod->probe_len) != 0)
            continue;
        instance *in = NULL;
        for (int k = 0; k < MAX_INST && !in; k++) if (!g_rt.inst[k].live) in = &g_rt.inst[k];
        if (!in) break;
        memset(in, 0, sizeof *in);
        in->live = 1;
        in->floating = 1;
        in->mod = mod;
        snprintf(in->name, sizeof in->name, "%s", mod->name);
        in->base = cs;
        in->lo = (uint32_t)cs * 16u;
        in->hi = in->lo + mod->size;
        if (in->hi > MEM_SIZE) in->hi = MEM_SIZE;
        in->gen = 1;
        in->ok_gen = (uint32_t *)calloc(mod->nregions ? mod->nregions : 1, sizeof(uint32_t));
        in->bad_gen = (uint32_t *)calloc(mod->nregions ? mod->nregions : 1, sizeof(uint32_t));
        entry_index(mod);
        set_codebits(in, 1);
        for (uint32_t p = in->lo >> 4; p <= ((in->hi - 1) >> 4) && p < 0x10000; p++) {
            instance *o = g_rt.by_para[p];
            if (!o || !o->live || o->floating) g_rt.by_para[p] = in;
        }
        g_rt.nfloating++;
        return in;
    }
    g_rt.floating_no[cs >> 3] |= (uint8_t)(1u << (cs & 7));
    return NULL;
}

/* Once a loaded module's code runs, the decompressor that unpacked it is
 * finished and its copy is just memory the program will reuse. */
static void drop_floating(void)
{
    for (int k = 0; k < MAX_INST; k++)
        if (g_rt.inst[k].live && g_rt.inst[k].floating) unregister(&g_rt.inst[k]);
}

/* The verified region of instance `in` at linear `lin` under CS, or NULL. */
static const rc_region *region_at(machine_t *m, instance *in, uint32_t lin, uint16_t cs)
{
    if (!in || !in->live || !in->mod || lin < in->lo || lin >= in->hi) return NULL;
    const rc_module *mod = in->mod;
    const uint32_t off = lin - in->lo;
    const uint32_t *idx = g_rt.entry_of[module_index(mod)];
    if (!idx || !idx[off]) return NULL;
    /* The same bytes may be translated under more than one code segment
     * (seg:ip and seg+n:ip-16n alias); entries for one offset are adjacent. */
    uint32_t k = idx[off] - 1, ri = UINT32_MAX;
    for (; k < mod->nentries && mod->entries[k].off == off; k++)
        if ((uint16_t)(in->base + mod->regions[mod->entries[k].region].seg) == cs) {
            ri = mod->entries[k].region;
            break;
        }
    if (ri == UINT32_MAX) return NULL;                     /* reached under another CS */
    const rc_region *r = &mod->regions[ri];
    if (in->refused && in->refused[ri]) return NULL;       /* an override is placed in it */
    if (in->ok_gen[ri] != in->gen) {
        if (in->bad_gen[ri] == in->gen) return NULL;
        if (verify(m->mem, in, r)) { in->ok_gen[ri] = in->gen; g_rt.verify_ok++; in->ran = 1; }
        else { in->bad_gen[ri] = in->gen; g_rt.verify_fail++; return NULL; }
    }
    return r;
}

/* The region to run at CS:IP, verified, or NULL. */
static const rc_region *lookup(machine_t *m, instance **out)
{
    cpu_t *c = &m->cpu;
    const uint16_t cs = c->seg[S_CS];
    const uint32_t lin = phys(cs, c->ip);
    instance *in = g_rt.by_para[lin >> 4];
    const rc_region *r = region_at(m, in, lin, cs);
    if (r) {
        if (g_rt.nfloating && in == g_rt.last_exec)
            drop_floating();     /* the unpacked program's own code is running */
        *out = in;
        return r;
    }
    /* Not a loaded module's code: a floating module already found, or one
     * found now. The decompressor's first steps run inside the area the
     * program will be unpacked into, so these are searched by range rather
     * than through the paragraph map. */
    for (int k = 0; k < MAX_INST; k++) {
        instance *f = &g_rt.inst[k];
        if (f->live && f->floating && (r = region_at(m, f, lin, cs)) != NULL) { *out = f; return r; }
    }
    instance *f = try_floating(m, cs);
    if (f && (r = region_at(m, f, lin, cs)) != NULL) { *out = f; return r; }
    return NULL;
}

int recomp_run(machine_t *m)
{
    cpu_t *c = &m->cpu;
    int ran = 0;
    while (c->icount < c->stop_at && !c->halted && !(c->flags & F_TF)) {
        /* A nested call's return (a matched routine's guest_call): report
         * at once, so the run loop takes RUN_TRAP there. Without this poll
         * one recomp_run batch runs to stop_at, blowing past the return
         * point; execution re-enters matched routines on the way, a C
         * nesting level per call. Short slices unwind at each boundary,
         * but a long single run (a strike replay's post-credit second, any
         * machine_api run over dense model drawing) overflows the C stack.
         * -1 is nonzero, so the run loop continues into its trap check. */
        if (m->trap_on && c->seg[S_CS] == m->trap_cs && c->ip == m->trap_ip &&
                c->r[R_SP] == m->trap_sp)
            return -1;
        instance *in = NULL;
        const rc_region *r = lookup(m, &in);
        if (!r) break;
        g_rt.dispatches++;
        if (!r->fn(c)) break;
        ran = 1;
    }
    return ran;
}

/* ---- coverage: what the interpreter ran inside known modules ---------- */

/* ---- the miss profile (F117R_MISS_PROFILE): where interpreted time goes - */

typedef struct { uint32_t lin; uint16_t cs; uint64_t n; int why; char name[16]; uint32_t off; } miss_t;
static miss_t g_miss[4096];
static int g_nmiss, g_profile = -1;

enum { WHY_OUTSIDE, WHY_NOMOD, WHY_NOENTRY, WHY_CS, WHY_VERIFY };
static const char *WHY[] = { "outside any module", "module has no translation",
                             "no translation at this address", "translated under another CS",
                             "bytes differ from the translation" };

static void profile_miss(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint32_t lin = phys(c->seg[S_CS], c->ip);
    int k;
    for (k = 0; k < g_nmiss; k++) if (g_miss[k].lin == lin && g_miss[k].cs == c->seg[S_CS]) break;
    if (k == g_nmiss) {
        if (g_nmiss >= 4096) return;
        g_nmiss++;
        miss_t *e = &g_miss[k];
        memset(e, 0, sizeof *e);
        e->lin = lin; e->cs = c->seg[S_CS];
        instance *in = g_rt.by_para[lin >> 4];
        if (!in || lin < in->lo || lin >= in->hi) { e->why = WHY_OUTSIDE; snprintf(e->name, sizeof e->name, "-"); }
        else {
            snprintf(e->name, sizeof e->name, "%s", in->name);
            e->off = lin - in->lo;
            if (!in->mod) e->why = WHY_NOMOD;
            else {
                const uint32_t *idx = g_rt.entry_of[module_index(in->mod)];
                if (!idx || !idx[e->off]) e->why = WHY_NOENTRY;
                else {
                    const rc_region *r = &in->mod->regions[in->mod->entries[idx[e->off] - 1].region];
                    e->why = (uint16_t)(in->base + r->seg) != c->seg[S_CS] ? WHY_CS : WHY_VERIFY;
                }
            }
        }
    }
    g_miss[k].n++;
}

void recomp_note_interp(machine_t *m)
{
    if (g_profile < 0) { const char *e = getenv("F117R_MISS_PROFILE"); g_profile = e && *e && *e != '0'; }
    if (g_profile) profile_miss(m);
    if (!g_rt.coverage) return;
    cpu_t *c = &m->cpu;
    const uint32_t lin = phys(c->seg[S_CS], c->ip);
    instance *in = g_rt.by_para[lin >> 4];
    if (!in || lin < in->lo || lin >= in->hi) return;
    const uint32_t size = in->hi - in->lo, off = lin - in->lo;
    if (!in->seen) {
        in->seen = (uint8_t *)calloc(size, 1);
        in->seen_seg = (uint16_t *)calloc(size, sizeof(uint16_t));
        in->seen_bytes = (uint8_t *)calloc(size, 4);
        if (!in->seen || !in->seen_seg || !in->seen_bytes) return;
    }
    if (in->seen[off]) return;
    in->seen[off] = 1;
    in->seen_seg[off] = (uint16_t)(c->seg[S_CS] - in->base);
    for (int k = 0; k < 4; k++) in->seen_bytes[off * 4 + k] = c->mem[(lin + (uint32_t)k) & 0xFFFFF];
}

static void flush_coverage(instance *in)
{
    if (!in->seen || !g_rt.coverage_path[0]) return;
    FILE *f = fopen(g_rt.coverage_path, "a");
    if (!f) return;
    const uint32_t size = in->hi - in->lo;
    const uint32_t origin = in->lo - (uint32_t)in->base * 16u;
    for (uint32_t off = 0; off < size; off++) {
        if (!in->seen[off]) continue;
        const uint16_t seg = in->seen_seg[off];
        const uint32_t ip = off + origin - (uint32_t)seg * 16u;
        fprintf(f, "%s %016llX %04X %04X %02X%02X%02X%02X\n", in->name,
                (unsigned long long)in->file_hash, seg, ip & 0xFFFF,
                in->seen_bytes[off * 4], in->seen_bytes[off * 4 + 1],
                in->seen_bytes[off * 4 + 2], in->seen_bytes[off * 4 + 3]);
    }
    fclose(f);
    memset(in->seen, 0, size);
}

void recomp_write_misses(machine_t *m, const char *path)
{
    (void)m;
    char keep[600];
    snprintf(keep, sizeof keep, "%s", g_rt.coverage_path);
    snprintf(g_rt.coverage_path, sizeof g_rt.coverage_path, "%s", path);
    for (int i = 0; i < MAX_INST; i++) if (g_rt.inst[i].live) flush_coverage(&g_rt.inst[i]);
    snprintf(g_rt.coverage_path, sizeof g_rt.coverage_path, "%s", keep);
}

void recomp_shutdown(machine_t *m)
{
    (void)m;
    for (int i = 0; i < MAX_INST; i++) if (g_rt.inst[i].live) flush_coverage(&g_rt.inst[i]);
}

void recomp_report(machine_t *m, FILE *f)
{
    const uint64_t total = m->cpu.icount;
    fprintf(f, "[recomp] %u modules built in; %llu dispatches; regions verified %llu, "
               "refused %llu; %llu writes to translated bytes; %llu instructions "
               "interpreted (of %llu clock)\n",
            RC_NMODULES, (unsigned long long)g_rt.dispatches,
            (unsigned long long)g_rt.verify_ok, (unsigned long long)g_rt.verify_fail,
            (unsigned long long)g_rt.code_writes, (unsigned long long)m->interp_steps,
            (unsigned long long)total);
    if (rc_mutant_hits)
        fprintf(f, "[recomp] the planted mutation ran %llu times\n", rc_mutant_hits);
    for (int i = 0; i < g_nov; i++)
        if (g_ov_hits[i])
            fprintf(f, "[%s] %s %04X:%04X %s: ran %llu times\n", g_ov[i].matched ? "matched" : "override",
                    g_ov[i].module, g_ov[i].seg, g_ov[i].ip, g_ov[i].what ? g_ov[i].what : "",
                    (unsigned long long)g_ov_hits[i]);
    if (g_shadow_on)
        fprintf(f, "[shadow] window %llu:%llu: %llu matched routines checked (%llu declined), %llu differing\n", (unsigned long long)g_shadow_lo, (unsigned long long)g_shadow_hi, g_shadow_checked, g_shadow_declined, g_shadow_bad);
    for (int pass = 0; pass < 25 && g_nmiss; pass++) {
        int best = 0;
        for (int k = 1; k < g_nmiss; k++) if (g_miss[k].n > g_miss[best].n) best = k;
        if (!g_miss[best].n) break;
        miss_t *e = &g_miss[best];
        fprintf(f, "[miss] %10llu x %04X:%04X %-12s +%05X  %s\n", (unsigned long long)e->n,
                e->cs, (unsigned)((e->lin - (uint32_t)e->cs * 16u) & 0xFFFF), e->name, e->off, WHY[e->why]);
        e->n = 0;
    }
}

