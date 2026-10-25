/* An override runs only while its module's code is still in memory at its entry.
 * A module's memory can be reused without another module loading over it (a buffer, a
 * later program's data); its instance then stays live. Batch bd found a matched RET
 * firing there and changing a route. The check compares the opcode byte only, since
 * relocation and run-time patches change operands, never opcodes. */
#include "machine.h"
#include "recomp_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static int failed, calls;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);failed++;} } while(0)
static int body(machine_t *p) { calls++; cpu_step(&p->cpu); return 1; }

static int step_at(uint16_t ip)
{
    m.cpu.seg[S_CS] = 0x6000; m.cpu.ip = ip;
    m.cpu.icount = 1000; m.cpu.stop_at = 2000; m.cpu.charged = 0; m.cpu.io_cross = 0;
    return recomp_override_step(&m);
}

int main(void)
{
    /* 0: mov ax,1234h  3: nop */
    static const uint8_t code[] = { 0xB8, 0x34, 0x12, 0x90 };
    m.mem = calloc(1, MEM_SIZE); if (!m.mem) return 1;
    cpu_init(&m.cpu, m.mem); m.cpu.user = &m; m.cpu.int_hook = dos_int_hook;
    m.engine = ENGINE_RECOMP; m.ips = MACHINE_DEFAULT_IPS;
    recomp_init(&m);
    const recomp_override o = { "matched", "STALE.OVL", 0, 0, 0, body, "mov ax,1234h", 1 };
    CHECK(recomp_override_add(&o) >= 0);
    recomp_module_load(NULL, &m, "STALE.OVL", code, sizeof code, MODLOAD_OVERLAY, 0x6000, 0x6000);
    memcpy(m.mem + 0x60000, code, sizeof code);          /* the loader writes the image after announcing it */

    CHECK(step_at(0) == 1 && calls == 1);                /* the module's code: the override runs */
    m.mem[0x60001] = 0x78;                               /* an operand changes (a relocation, a patch) */
    CHECK(step_at(0) == 1 && calls == 2);                /* still the routine */
    m.mem[0x60000] = 0x90;                               /* the memory is reused: another opcode there */
    CHECK(step_at(0) == 0 && calls == 2);                /* the original instruction runs instead */
    m.mem[0x60000] = 0xB8;                               /* the module's opcode is back */
    CHECK(step_at(0) == 1 && calls == 3);

    recomp_shutdown(&m); free(m.mem);
    if (!failed) puts("stale override: runs on its module's opcode, declines on reused memory, ignores operand changes");
    return failed ? 1 : 0;
}
