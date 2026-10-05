/* Scalar API for observing a running guest and sending normal input.
 * No guest-memory/register writes: diagnostic pilots must fly the game.
 * The core's code bitmap is process-global, so one live machine is allowed.
 */
#include "machine.h"
#include "recomp_rt.h"
#include "fixes.h"
#include "keys.h"
#include "inputlog.h"
#include "present.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif

typedef struct {
    machine_t m;
    FILE *record;
} run_api;

static run_api *live;
static char error[512];

static void recorded(void *user, const machine_input *in)
{
    run_api *r = (run_api *)user;
    if (r->record) inputlog_write(r->record, in);
}

API void f117_machine_close(run_api *r)
{
    if (!r) return;
    if (r->record) fclose(r->record);
    recomp_shutdown(&r->m);
    machine_shutdown(&r->m);
    if (r->m.log) fclose(r->m.log);
    free(r->m.mem);
    fixes_enable("all", 0);          /* switches are process-wide: the next machine starts clean */
    if (live == r) live = NULL;
    free(r);
}

API run_api *f117_machine_open(const char *data, const char *save,
                              const char *log, uint64_t ips,
                              uint64_t time_us, int engine)
{
    if (live) {
        snprintf(error, sizeof error, "only one live machine per process");
        return NULL;
    }
    error[0] = 0;
    run_api *r = (run_api *)calloc(1, sizeof *r);
    if (!r) { snprintf(error, sizeof error, "cannot allocate machine"); return NULL; }
    r->m.mem = (uint8_t *)malloc(MEM_SIZE);
    if (!r->m.mem) { free(r); snprintf(error, sizeof error, "cannot allocate guest RAM"); return NULL; }
    if (log && !(r->m.log = fopen(log, "w"))) {
        snprintf(error, sizeof error, "cannot write machine log");
        free(r->m.mem); free(r); return NULL;
    }
    r->m.engine = engine;
    recomp_init(&r->m);
    machine_hooks hooks = {0};
    hooks.module_load = recomp_module_load;
    if (!machine_boot(&r->m, r->m.mem, data, save, "F117.COM", ips, time_us, &hooks)) {
        snprintf(error, sizeof error, "%s", r->m.fault);
        f117_machine_close(r);
        return NULL;
    }
    r->m.on_input = recorded;
    r->m.on_input_user = r;
    live = r;
    return r;
}

API const char *f117_machine_error(run_api *r) { return r ? r->m.fault : error; }
/* Switch a fix (docs/bugs.md) on or off; the number switched, 0 if none. */
API int f117_machine_fix(run_api *r, const char *id, int on) { (void)r; return fixes_enable(id, on); }
API int f117_machine_run(run_api *r, uint64_t until) { return machine_run(&r->m, until); }
API uint64_t f117_machine_clock(run_api *r) { return r->m.cpu.icount; }
API uint64_t f117_machine_hash(run_api *r) { return machine_state_hash(&r->m); }
API const char *f117_machine_program(run_api *r) { return dos_current_program(&r->m); }
API uint64_t f117_machine_start(run_api *r)
{
    return r->m.nproc ? r->m.procs[r->m.nproc - 1].start_icount : 0;
}
API uint16_t f117_machine_psp(run_api *r)
{
    return r->m.nproc ? r->m.procs[r->m.nproc - 1].psp_seg : 0;
}
API uint8_t f117_machine_read8(run_api *r, uint32_t address)
{
    return r->m.mem[address & (MEM_SIZE - 1)];
}
API uint16_t f117_machine_read16(run_api *r, uint32_t address)
{
    return (uint16_t)(f117_machine_read8(r, address) | ((uint16_t)f117_machine_read8(r, address + 1) << 8));
}
API uint32_t f117_machine_read32(run_api *r, uint32_t address)
{
    return f117_machine_read16(r, address) | ((uint32_t)f117_machine_read16(r, address + 2) << 16);
}

API int f117_machine_key(run_api *r, uint64_t at, uint8_t scan)
{
    if (at < r->m.cpu.icount || r->m.in_qn >= (int)(sizeof r->m.in_q / sizeof r->m.in_q[0])) return 0;
    machine_input in = {0};
    in.at = at; in.type = INPUT_KEY; in.byte = scan;
    machine_input_at(&r->m, &in);
    return !r->m.fault[0];
}
API int f117_machine_type(run_api *r, uint64_t at, uint64_t hold,
                          uint64_t gap, const char *keys)
{
    if (at < r->m.cpu.icount) return 0;
    uint8_t make[4], release[4];
    int nm, nr;
    while (keys_next(&keys, make, &nm, release, &nr)) {
        for (int i = 0; i < nm; i++) if (!f117_machine_key(r, at, make[i])) return 0;
        for (int i = 0; i < nr; i++) if (!f117_machine_key(r, at + hold, release[i])) return 0;
        at += hold + gap;
    }
    return 1;
}
API int f117_machine_mouse(run_api *r, uint64_t at, int x, int y, unsigned buttons)
{
    if (at < r->m.cpu.icount || r->m.in_qn >= (int)(sizeof r->m.in_q / sizeof r->m.in_q[0])) return 0;
    machine_input in = {0};
    in.at = at; in.type = INPUT_MOUSE;
    in.x = (int16_t)x; in.y = (int16_t)y; in.buttons = (uint16_t)buttons;
    machine_input_at(&r->m, &in);
    return !r->m.fault[0];
}
API int f117_machine_record(run_api *r, const char *path)
{
    if (r->record || r->m.cpu.icount) return 0;
    r->record = fopen(path, "w");
    if (!r->record) return 0;
    inputlog_header(r->record, r->m.ips, r->m.boot_time_us);
    return 1;
}
API int f117_machine_screen(run_api *r, const char *path)
{
    return present_write_ppm(&r->m, path);
}
