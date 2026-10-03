/* cpu_api.c - flat C API over the CPU core, for the Python vector harnesses
 * (sstest.py, sst286.py). Everything crosses the boundary as scalars so
 * ctypes never has to know the layout of cpu_t.
 *
 * The same API is the oracle's (Reimp tools/x86oracle/oracle_api.c), so the
 * harnesses run unchanged against this core. The standalone CPU has no
 * recompiled code and no machine, so the two hooks those supply are no-ops.
 */
#include "../src/cpu/cpu.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define API __declspec(dllexport)
#else
#  define API __attribute__((visibility("default")))
#endif

uint8_t cpu_codebits[MEM_SIZE / 8];
void cpu_code_written(cpu_t *c, uint32_t lin) { (void)c; (void)lin; }
void cpu_irq_state_changed(cpu_t *c) { (void)c; }

typedef struct {
    cpu_t   cpu;
    uint8_t *mem;
} oracle_t;

API oracle_t *orc_new(void)
{
    oracle_t *o = (oracle_t *)calloc(1, sizeof(oracle_t));
    if (!o) return NULL;
    o->mem = (uint8_t *)calloc(MEM_SIZE, 1);
    if (!o->mem) { free(o); return NULL; }
    cpu_init(&o->cpu, o->mem);
    o->cpu.model = CPU_8086;   /* the oracle's default; sst286 selects 286 */
    cpu_reset(&o->cpu);
    return o;
}

API void orc_free(oracle_t *o)
{
    if (!o) return;
    free(o->mem);
    free(o);
}

API void orc_reset(oracle_t *o) { cpu_reset(&o->cpu); }

API void orc_clear_mem(oracle_t *o) { memset(o->mem, 0, MEM_SIZE); }

/* Register access by index: 0..7 = AX..DI, 8..11 = ES,CS,SS,DS,
 * 12 = IP, 13 = FLAGS. */
API uint32_t orc_get_reg(oracle_t *o, int i)
{
    if (i < 8)  return o->cpu.r[i];
    if (i < 12) return o->cpu.seg[i - 8];
    if (i == 12) return o->cpu.ip;
    if (i == 13) return o->cpu.flags;
    return 0;
}

API void orc_set_reg(oracle_t *o, int i, uint32_t v)
{
    uint16_t w = (uint16_t)v;
    if (i < 8)       o->cpu.r[i] = w;
    else if (i < 12) o->cpu.seg[i - 8] = w;
    else if (i == 12) o->cpu.ip = w;
    else if (i == 13) o->cpu.flags = w;
}

API uint8_t orc_read8(oracle_t *o, uint32_t a)             { return o->mem[a & 0xFFFFF]; }
API void    orc_write8(oracle_t *o, uint32_t a, uint8_t v) { o->mem[a & 0xFFFFF] = v; }

API void orc_write_block(oracle_t *o, uint32_t a, const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) o->mem[(a + i) & 0xFFFFF] = p[i];
}

API void orc_read_block(oracle_t *o, uint32_t a, uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = o->mem[(a + i) & 0xFFFFF];
}

API int orc_step(oracle_t *o) { return cpu_step(&o->cpu); }

API int orc_run(oracle_t *o, uint64_t steps)
{
    for (uint64_t i = 0; i < steps; i++) {
        int rc = cpu_step(&o->cpu);
        if (rc != STOP_NONE) return rc;
    }
    return STOP_STEPS;
}

API uint64_t orc_icount(oracle_t *o) { return o->cpu.icount; }

API int orc_stop_reason(oracle_t *o) { return o->cpu.stop_reason; }

API void orc_set_model(oracle_t *o, int model) { o->cpu.model = model; }

static uint32_t g_bp = 0xFFFFFFFFu;

API void orc_set_bp(oracle_t *o, uint32_t linear) { (void)o; g_bp = linear; }

API int orc_run_to_bp(oracle_t *o, uint64_t max_steps)
{
    for (uint64_t i = 0; i < max_steps; i++) {
        if (phys(o->cpu.seg[S_CS], o->cpu.ip) == g_bp) return STOP_BREAKPOINT;
        int rc = cpu_step(&o->cpu);
        if (rc != STOP_NONE) return rc;
    }
    return STOP_STEPS;
}
