/* ROM-free regressions for code overrides (recomp_rt.h): where they are
 * placed, when they run, that a disabled one changes nothing, and how a
 * recorded session names the ones that were on. */
#include "machine.h"
#include "recomp_rt.h"
#include "inputlog.h"

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

static int step_at(uint16_t cs, uint16_t ip)
{
    m.cpu.seg[S_CS] = cs;
    m.cpu.ip = ip;
    return recomp_override_step(&m);
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
    CHECK(recomp_override_add(&good) == 0);
    CHECK(recomp_override_add(&other) == 1);
    CHECK(recomp_override_add(&named) == 2);
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

    recomp_shutdown(&m);
    if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
    printf("code overrides: all checks passed\n");
    return 0;
}
