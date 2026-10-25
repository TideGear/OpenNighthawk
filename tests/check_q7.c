/* Staged, game-data-dependent Q7 regression. Build with F117R_GEN_DIR, then
 * run check_q7 path/to/VGAME.EXE. No original bytes are embedded here. */
#include "fixes.h"
#include "matched.h"
#include "reloc_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static int failures;
#undef CHECK
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); failures++; } } while (0)

static int run(machine_t *mm)
{
    cpu_t *c = &mm->cpu;
    while (c->icount < c->stop_at) {
        if (mm->trap_on && c->seg[S_CS] == mm->trap_cs &&
            c->ip == mm->trap_ip && c->r[R_SP] == mm->trap_sp) return RUN_TRAP;
        if (recomp_override_step(mm) > 0) continue;
        if (mm->engine == ENGINE_RECOMP && recomp_run(mm)) continue;
        if (cpu_step(c)) return RUN_FAULT;
    }
    return RUN_SLICE;
}

static void put(uint16_t at, int value) { seg_write16(&m.cpu, 0x5000, at, (uint16_t)value); }
typedef struct { int x, y, z, far, zoom, up, original, fixed; } sample;

static void prepare(const rc_module *mod, const uint8_t *file, size_t len,
                    const sample *s, int wrapper, int fixed, int engine, uint64_t budget)
{
    memset(m.mem, 0, MEM_SIZE);
    cpu_init(&m.cpu, m.mem); m.cpu.user = &m;
    m.cpu.seg[S_CS] = 0x1000;
    m.cpu.seg[S_DS] = m.cpu.seg[S_SS] = 0x5000;
    m.cpu.r[R_SP] = 0xF000; m.cpu.r[R_SI] = 0xABCD; m.cpu.r[R_DI] = 0x1234;
    m.cpu.r[R_BP] = (uint16_t)(s->z * 2);
    m.cpu.r[R_BX] = (uint16_t)(s->x * 2); m.cpu.r[R_CX] = (uint16_t)(s->y * 2);
    m.cpu.ip = wrapper ? 0xE3AC : 0xE3BF;
    m.cpu.icount = 1000; m.cpu.stop_at = 1000 + budget;
    m.engine = engine; m.ips = MACHINE_DEFAULT_IPS;
    fixes_enable("all", 0); fixes_enable("Q7", fixed);
    recomp_module_load(NULL, &m, "VGAME.EXE", file, len, MODLOAD_EXEC, 0x1000, 0x1000);
    CHECK(test_load_generated_image(m.mem, MEM_SIZE, mod, 0x1000));
    /* A diagonal half-scale transform produces the requested high words. */
    for (unsigned at = 0x49BE; at <= 0x49CE; at += 2) put((uint16_t)at, 0);
    put(0x49CA, 0x4000); put(0x49C6, 0x4000); put(0x49C2, 0x4000);
    put(0x48E6, s->far); put(0x48F6, -32768); put(0x438E, s->zoom);
    put(0x49AA, 0); put(0x4966, s->up); put(0x4976, s->up);
    put(0xE56A, 0x4966); put(0xE570, 0x4976); put(0x294B, 0);
    put(0x49A8, 0x7EEE); put(0xF000, 0xFF00);
    if (wrapper) { put(0xF002, s->z * 2); put(0xF004, s->x * 2); put(0xF006, s->y * 2); }
    m.trap_on = 1; m.trap_cs = 0x1000; m.trap_ip = 0xFF00; m.trap_sp = 0xF002;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: check_q7 path/to/VGAME.EXE\n"); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    fseek(f, 0, SEEK_END); const long size = ftell(f); rewind(f);
    if (size <= 0) { fclose(f); return 2; }
    uint8_t *file = malloc((size_t)size), *saved = malloc(MEM_SIZE);
    m.mem = calloc(1, MEM_SIZE);
    if (!file || !saved || !m.mem) return 2;
    if (fread(file, 1, (size_t)size, f) != (size_t)size) return 2;
    fclose(f);
    const rc_module *mod = NULL;
    for (unsigned k = 0; k < RC_NMODULES; k++)
        if (!strcmp(RC_MODULES[k]->name, "VGAME.EXE")) mod = RC_MODULES[k];
    if (!mod) { fprintf(stderr, "VGAME translation required\n"); return 2; }
    cpu_init(&m.cpu, m.mem); recomp_init(&m); matched_runner = run;
    const sample cases[] = {
        {20, 40, 1000, 1050, 1, 0, 1080, 1015},
        {-20, -40, 1000, 1050, 1, 0, 1080, 1015},
        {0, 0, 8000, 8100, 1, 0, 8562, 8000},
        {0, 0, -1000, 2000, 1, 30000, 1179, 1000},
        {20, 40, 1000, 1050, 0, 0, 1015, 1015},
        {400, 40, 1000, 1050, 1, 0, 0x7EEE, 0x7EEE}, /* outside frustum */
        {20, 40, 1000, 900, 1, 0, 0x7EEE, 0x7EEE},   /* initial z cull */
    };
    unsigned checks = 0;
    for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        const sample *s = &cases[k];
        for (int wrapper = 0; wrapper < 2; wrapper++) for (int fixed = 0; fixed < 2; fixed++) {
            cpu_t reference = {0};
            for (int engine = ENGINE_INTERP; engine <= ENGINE_RECOMP; engine++) {
                prepare(mod, file, (size_t)size, s, wrapper, fixed, engine, 1000);
                CHECK(run(&m) == RUN_TRAP);
                const int expected = fixed ? s->fixed : s->original;
                CHECK(seg_read16(&m.cpu, 0x5000, 0x49A8) == expected);
                CHECK(m.cpu.r[R_AX] == (expected == 0x7EEE || expected > s->far ? 1 : 0));
                CHECK(m.cpu.r[R_SI] == 0xABCD);
                if (engine == ENGINE_INTERP) { reference = m.cpu; memcpy(saved, m.mem, MEM_SIZE); }
                else {
                    CHECK(!memcmp(reference.r, m.cpu.r, sizeof reference.r));
                    CHECK(reference.flags == m.cpu.flags && reference.ip == m.cpu.ip);
                    CHECK(reference.icount == m.cpu.icount);
                    CHECK(!memcmp(saved, m.mem, MEM_SIZE));
                }
                checks++;
            }
        }
    }
    /* Stop at every instruction boundary: the hook must not skip an event. */
    for (unsigned limit = 1; limit < 150; limit++) {
        prepare(mod, file, (size_t)size, &cases[0], 0, 1, ENGINE_RECOMP, limit);
        run(&m); CHECK(m.cpu.icount <= 1000 + limit);
    }
    recomp_shutdown(&m); free(m.mem); free(file); free(saved);
    printf("Q7: %u staged runs, both fix states and engines; 149 event limits; %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
