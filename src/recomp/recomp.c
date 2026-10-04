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

#include <stdlib.h>
#include <string.h>

uint8_t cpu_codebits[MEM_SIZE / 8];

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

static void unregister(instance *in)
{
    if (!in->live) return;
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
    for (int i = 0; i < DOS_MAX_BLOCKS; i++) {
        const dos_block *b = &m->blocks[i];
        if (b->in_use && seg >= b->seg && seg < (uint16_t)(b->seg + b->paras))
            return (uint32_t)(b->seg + b->paras - seg) * 16u;
    }
    return 0x10000;
}

void recomp_module_load(void *user, machine_t *m, const char *name,
                        const uint8_t *file, size_t len, int kind,
                        uint16_t load_seg, uint16_t reloc)
{
    (void)user; (void)reloc;
    const uint64_t h = fnv1a64(file, len);
    const rc_module *mod = NULL;
    for (unsigned i = 0; i < RC_NMODULES; i++)
        if (RC_MODULES[i]->file_hash == h) { mod = RC_MODULES[i]; break; }

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
    dos_log(m, "[recomp] %s at %04X: %s\n", in->name, load_seg,
            mod ? "translated" : "no translation (interpreted)");
}

void cpu_code_written(cpu_t *c, uint32_t lin)
{
    instance *in = g_rt.by_para[lin >> 4];
    g_rt.code_writes++;
    if (in && lin >= in->lo && lin < in->hi) in->gen++;
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

/* The region to run at CS:IP, verified, or NULL. */
static const rc_region *lookup(machine_t *m, instance **out)
{
    cpu_t *c = &m->cpu;
    const uint32_t lin = phys(c->seg[S_CS], c->ip);
    instance *in = g_rt.by_para[lin >> 4];
    if (!in || !in->mod || lin < in->lo || lin >= in->hi) return NULL;
    const rc_module *mod = in->mod;
    const uint32_t off = lin - in->lo;
    const uint32_t *idx = g_rt.entry_of[module_index(mod)];
    if (!idx || !idx[off]) return NULL;
    const uint32_t ri = mod->entries[idx[off] - 1].region;
    const rc_region *r = &mod->regions[ri];
    if ((uint16_t)(in->base + r->seg) != c->seg[S_CS]) return NULL;   /* reached under another CS */
    if (in->ok_gen[ri] != in->gen) {
        if (in->bad_gen[ri] == in->gen) return NULL;
        if (verify(m->mem, in, r)) { in->ok_gen[ri] = in->gen; g_rt.verify_ok++; }
        else { in->bad_gen[ri] = in->gen; g_rt.verify_fail++; return NULL; }
    }
    *out = in;
    return r;
}

int recomp_run(machine_t *m)
{
    cpu_t *c = &m->cpu;
    int ran = 0;
    while (c->icount < c->stop_at && !c->halted && !(c->flags & F_TF)) {
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

void recomp_note_interp(machine_t *m)
{
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
}

