/* ROM-free regressions for code overrides (recomp_rt.h): where they are
 * placed, when they run, that a disabled one changes nothing, and how a
 * recorded session names the ones that were on. */
#include "machine.h"
#include "recomp_rt.h"
#include "inputlog.h"
#include "fixes.h"
#include "observe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static int failures, calls, answer = 1;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static int skip_two(machine_t *mm)
{
    calls++;
    if (!answer) return 0;
    mm->cpu.ip = (uint16_t)(mm->cpu.ip + 2);
    mm->cpu.icount++;
    return 1;
}

static uint64_t fnv1a64(const uint8_t *p, size_t n)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}

static struct { uint64_t at; uint8_t reg, val; } writes[16];
static int nwrites;
static void opl_seen(void *user, uint64_t icount, uint8_t reg, uint8_t val)
{
    (void)user;
    if (nwrites < 16) { writes[nwrites].at = icount; writes[nwrites].reg = reg; writes[nwrites].val = val; }
    nwrites++;
}

static int step_at(uint16_t cs, uint16_t ip)
{
    m.cpu.seg[S_CS] = cs;
    m.cpu.ip = ip;
    return recomp_override_step(&m);
}


/* The observer reads a projected vertex without touching the machine. */
static int obs_calls, obs_range;
static int32_t obs_xf[3], obs_px[2];
static void obs_vertex_seen(void *u, uint64_t icount, const int32_t xf[3], const int32_t px[2], int range)
{
    (void)u; (void)icount;
    obs_calls++; obs_range = range;
    for (int k = 0; k < 3; k++) obs_xf[k] = xf[k];
    for (int k = 0; k < 2; k++) obs_px[k] = px[k];
}
static void check_observer(void)
{
    static const f117_observer o = { 0, 0, obs_vertex_seen };
    memset(m.mem + 0x20000, 0, 0x100);
    m.cpu.seg[S_DS] = 0x2000;
    /* camera-space vertex at DS:0010: x = -2, y = 0x00012345, z with the high word 0x0200 (near) */
    const uint32_t xf[3] = { 0xFFFFFFFEu, 0x00012345u, 0x02001234u };
    for (int k = 0; k < 3; k++)
        for (int b = 0; b < 4; b++) m.mem[0x20010 + 4 * k + b] = (uint8_t)(xf[k] >> (8 * b));
    const uint32_t px[2] = { 0xFFFFFF9Du, 0x000000C8u };           /* -99, 200 */
    for (int k = 0; k < 2; k++)
        for (int b = 0; b < 4; b++) m.mem[0x20040 + 4 * k + b] = (uint8_t)(px[k] >> (8 * b));
    const uint64_t before = fnv1a64(m.mem + 0x20000, 0x100);
    const uint16_t sp = m.cpu.r[R_SP];
    observe_vertex(&m, 0x0010, 0x0040);                              /* no observer: nothing */
    CHECK(obs_calls == 0);
    observe_set(&o);
    observe_vertex(&m, 0x0010, 0x0040);
    CHECK(obs_calls == 1 && obs_range == 0);
    CHECK(obs_xf[0] == -2 && obs_xf[1] == 0x12345 && obs_xf[2] == 0x02001234);
    CHECK(obs_px[0] == -99 && obs_px[1] == 200);
    m.mem[0x20010 + 0x0A] = 0x80;                                    /* z high word 0x8000.. behind the eye */
    m.mem[0x20010 + 0x0B] = 0xFF;
    observe_vertex(&m, 0x0010, 0x0040);
    CHECK(obs_calls == 2 && obs_range == 2 && obs_px[0] == 0 && obs_px[1] == 0);
    m.mem[0x20010 + 0x0A] = 0x10; m.mem[0x20010 + 0x0B] = 0x00;     /* z high word 0x10: mid range */
    observe_vertex(&m, 0x0010, 0x0040);
    CHECK(obs_range == 1 && obs_px[0] == -99);
    m.mem[0x20010 + 0x0A] = 0x34; m.mem[0x20010 + 0x0B] = 0x12;     /* restore the bytes compared below */
    m.mem[0x20010 + 0x0A] = (uint8_t)(xf[2] >> 16); m.mem[0x20010 + 0x0B] = (uint8_t)(xf[2] >> 24);
    CHECK(fnv1a64(m.mem + 0x20000, 0x100) == before);                /* it read, and wrote nothing */
    CHECK(m.cpu.r[R_SP] == sp);
    observe_set(NULL);
}

int main(void)
{
    m.mem = calloc(1, MEM_SIZE);
    if (!m.mem) return 1;
    cpu_init(&m.cpu, m.mem);
    m.cpu.user = &m;
    recomp_init(&m);

    static uint8_t file[0x400];
    for (unsigned i = 0; i < sizeof file; i++) file[i] = (uint8_t)(i * 7);
    const uint64_t hash = fnv1a64(file, sizeof file);
    const recomp_override good = { "T1", "GAME.OVL", hash, 0x0001, 0x0020, skip_two, "test" };
    const recomp_override other = { "T2", "GAME.OVL", hash ^ 1, 0x0000, 0x0040, skip_two, "wrong file" };
    const recomp_override named = { "T3", "OTHER.OVL", 0, 0x0000, 0x0050, skip_two, "wrong name" };
    /* Matched routines (src/matched) are registered first, by recomp_init. */
    const int base = recomp_override_add(&good);
    CHECK(base >= 0);
    CHECK(recomp_override_add(&other) == base + 1);
    CHECK(recomp_override_add(&named) == base + 2);
    CHECK(recomp_override_add(&(recomp_override){ "X", "Y", 0, 0, 0, NULL, "" }) == -1);

    /* Registered but off: nothing is placed, before or after a load. */
    recomp_module_load(NULL, &m, "game.ovl", file, sizeof file, MODLOAD_OVERLAY, 0x2000, 0x2000);
    CHECK(recomp_overrides_live == 0);
    CHECK(step_at(0x2001, 0x0020) == 0 && calls == 0);

    /* On: placed at CS = load segment + 1, IP 0x20 (linear 0x20030) only. */
    CHECK(recomp_override_enable("T1", 1) == 1);
    CHECK(recomp_overrides_live == 1);
    CHECK(step_at(0x2001, 0x0020) == 1 && calls == 1 && m.cpu.ip == 0x0022);
    CHECK(step_at(0x2001, 0x0021) == 0 && calls == 1);
    /* The same linear address under another CS is a different instruction. */
    CHECK(step_at(0x2000, 0x0030) == 0 && calls == 1);
    /* Declining asks for the original instruction. */
    answer = 0;
    CHECK(step_at(0x2001, 0x0020) == -1 && calls == 2);
    answer = 1;

    /* A file whose hash differs, or another module's name, places nothing. */
    CHECK(recomp_override_enable("T2", 1) == 1 && recomp_override_enable("T3", 1) == 1);
    CHECK(recomp_overrides_live == 1);
    CHECK(step_at(0x2000, 0x0040) == 0 && step_at(0x2000, 0x0050) == 0);

    /* "all" switches every one; off removes the placement at once. */
    CHECK(recomp_override_enable("all", 0) == 3);
    CHECK(recomp_overrides_live == 0);
    CHECK(step_at(0x2001, 0x0020) == 0 && calls == 2);
    CHECK(recomp_override_enable("missing", 1) == 0);

    /* Enabled before the module arrives: placed by the load itself, and
     * gone when another load overwrites the module. */
    recomp_override_enable("T1", 1);
    recomp_module_load(NULL, &m, "game.ovl", file, sizeof file, MODLOAD_OVERLAY, 0x3000, 0x3000);
    CHECK(recomp_overrides_live == 2);
    CHECK(step_at(0x3001, 0x0020) == 1 && calls == 3);
    static uint8_t junk[0x40];
    recomp_module_load(NULL, &m, "junk.ovl", junk, sizeof junk, MODLOAD_OVERLAY, 0x3002, 0x3002);
    CHECK(recomp_overrides_live == 1);
    CHECK(step_at(0x3001, 0x0020) == 0 && calls == 3);
    CHECK(step_at(0x2001, 0x0020) == 1 && calls == 4);

    /* The enabled ids, each once, are what a recorded session names. */
    char ids[64];
    recomp_override_enable("all", 0);
    recomp_override_ids(ids, sizeof ids);
    CHECK(!strcmp(ids, ""));
    recomp_override_enable("T3", 1);
    recomp_override_enable("T1", 1);
    recomp_override_ids(ids, sizeof ids);
    CHECK(!strcmp(ids, "T1 T3"));

    /* The log line: written only when fixes are on, read back exactly. */
    const char *path = "test_overrides.log";
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (f) {
        inputlog_header(f, 9000000, 1);
        inputlog_fixes(f, "");
        fclose(f);
        char back[64] = "x";
        inputlog_read_fixes(path, back, sizeof back);
        CHECK(!strcmp(back, ""));
    }
    f = fopen(path, "w");
    if (f) {
        inputlog_header(f, 9000000, 1);
        inputlog_fixes(f, ids);
        fprintf(f, "K 100 1E\n");
        fclose(f);
        char back[64] = "";
        inputlog_read_fixes(path, back, sizeof back);
        CHECK(!strcmp(back, "T1 T3"));
        uint64_t ips = 0, t = 0;
        inputlog_read_header(path, &ips, &t);
        CHECK(ips == 9000000 && t == 1);
        inputlog_reader *r = inputlog_open(path);
        CHECK(r && !inputlog_done(r));     /* the comment line is not an input */
        inputlog_close(r);
    }
    remove(path);

    /* Data corrections (D4's world half): only the named file of the right
     * size, only the byte expected there, only while the fix is on, and
     * wherever the read window falls. */
    static uint8_t wld[2159];
    uint8_t got[16];
    memset(wld, 0, sizeof wld);
    memcpy(got, wld + 0x530, 16);
    fixes_file_data(NULL, &m, "lb.wld", 2159, 0x530, got, 16);
    CHECK(got[0x0A] == 0);                                     /* off */
    CHECK(fixes_enable("D4", 1) == 1);
    fixes_file_data(NULL, &m, "lb.wld", 2159, 0x530, got, 16);
    CHECK(got[0x0A] == 1);                                     /* LB.WLD 0x53A */
    memset(got, 0, sizeof got);
    fixes_file_data(NULL, &m, "LB.WLD", 2160, 0x530, got, 16);
    CHECK(got[0x0A] == 0);                                     /* another size */
    fixes_file_data(NULL, &m, "PG.WLD", 2159, 0x530, got, 16);
    CHECK(got[0x0A] == 0);                                     /* another file */
    got[0x0A] = 7;
    fixes_file_data(NULL, &m, "LB.WLD", 2159, 0x530, got, 16);
    CHECK(got[0x0A] == 7);                                     /* not the shipped byte */
    uint8_t one = 0;
    fixes_file_data(NULL, &m, "LB.WLD", 2159, 0x53A, &one, 1);
    CHECK(one == 1);                                           /* a one-byte window */
    uint8_t nc[2] = { 0x08, 0x08 };
    fixes_file_data(NULL, &m, "NC.WLD", 2383, 0x1F1, nc, 1);
    fixes_file_data(NULL, &m, "NC.WLD", 2383, 0x201, nc + 1, 1);
    CHECK(nc[0] == 0x09 && nc[1] == 0x09);                     /* North Cape's credit bit */
    char on[64];
    fixes_enabled(on, sizeof on);
    CHECK(!strcmp(on, "D4"));
    fixes_enable("all", 0);
    one = 0;
    fixes_file_data(NULL, &m, "LB.WLD", 2159, 0x53A, &one, 1);
    CHECK(one == 0);

    /* A fix's scheduled OPL writes (D2's speech): each at its own time,
     * a shadow address read when issued, and the guest's channel-0 writes
     * held off only while any remain. */
    m.ips = 9000000;
    pc_reset(&m);
    m.hooks.opl_write = opl_seen;
    m.cpu.icount = 1000;
    CHECK(machine_opl_scheduled_until(&m) == 0);
    CHECK(machine_opl_schedule(&m, 1500, 0x43, 0x08, 0));
    CHECK(machine_opl_schedule(&m, 2000, 0xB0, 0, 0x5000));
    CHECK(machine_opl_scheduled_until(&m) == 2000);
    pc_io_write(&m.cpu, 0x388, 0x43, 1);
    pc_io_write(&m.cpu, 0x389, 0x11, 1);                   /* channel 0: held off */
    pc_io_write(&m.cpu, 0x388, 0x44, 1);
    pc_io_write(&m.cpu, 0x389, 0x22, 1);                   /* channel 1: passes */
    CHECK(nwrites == 1 && writes[0].reg == 0x44 && m.opl_sched_dropped == 1);
    m.cpu.icount = 1499; pc_events(&m);
    CHECK(nwrites == 1);                                   /* not yet due */
    m.mem[0x5000] = 0x31;
    m.cpu.icount = 2100; pc_events(&m);
    CHECK(nwrites == 3 && writes[1].at == 1500 && writes[1].val == 0x08);
    CHECK(writes[2].at == 2000 && writes[2].reg == 0xB0 && writes[2].val == 0x31);
    CHECK(machine_opl_scheduled_until(&m) == 0);
    pc_io_write(&m.cpu, 0x388, 0xB0, 1);
    pc_io_write(&m.cpu, 0x389, 0x20, 1);                   /* free again */
    CHECK(nwrites == 4 && writes[3].reg == 0xB0 && writes[3].val == 0x20);
    machine_shutdown(&m);

    check_observer();
    recomp_shutdown(&m);
    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    printf("code overrides: all checks passed\n");
    return 0;
}
