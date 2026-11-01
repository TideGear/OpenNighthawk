/* Speculative execution must write only the copy and reject host DOS calls,
 * including a call chained through the native callback stub. */
#include "rerun.h"
#include "observe.h"
#include "timing386.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static machine_t m, before;
static uint8_t memory[MEM_SIZE], saved[MEM_SIZE];
static rerun_snap snap;
static rerun_list list;
static int sounds;
static void speaker(void *u, uint64_t clock) { (void)u; (void)clock; sounds++; }
static const machine_t *g_coll_real;
static int g_coll_begin, g_coll_end, g_coll_exec, g_coll_wrong;
static void coll_begin(void *u) { (void)u; g_coll_begin++; }
static void coll_end(void *u) { (void)u; g_coll_end++; }
static int coll_exec(void *u, machine_t *cm, unsigned k)
{
    (void)u; (void)k;
    g_coll_exec++;
    if (cm == g_coll_real || cm->cpu.user == g_coll_real) g_coll_wrong++;       /* the real machine must never be handed over */
    return 0;
}

int main(void)
{
    const uint8_t vectors[] = { 0x21, 0xE3, 0x20 };
    f117_observer obs = {0};
    for (int profile = 0; profile < 2; profile++) for (int k = 0; k < 3; k++) {
        memset(&m, 0, sizeof m);
        memset(memory, 0, sizeof memory);
        cpu_init(&m.cpu, memory);
        m.mem = memory; m.ips = profile ? MACHINE_386_IPS : MACHINE_DEFAULT_IPS;
        m.cpu.user = &m;
        if (profile) t386_enable(&m.cpu, T386_MEM_CACHED, 0xA0000, 0x20000, 32);
        m.cpu.io_write = pc_io_write; m.cpu.io_read = pc_io_read; m.cpu.int_hook = dos_int_hook;
        pc_reset(&m);
        m.cpu.seg[S_CS] = 0x1000; m.cpu.ip = 0; m.cpu.icount = 100;
        m.hooks.speaker = speaker;
        /* mov byte [0200],55; mov al,3; out 61,al; mov ah,40; int vec */
        const uint8_t code[] = {0xC6,0x06,0x00,0x02,0x55,0xB0,3,0xE6,0x61,0xB4,0x40,0xCD,vectors[k]};
        memcpy(memory + 0x10000, code, sizeof code);
        observe_set(&obs);
        rerun_snap_take(&snap, &m);
        before = m; memcpy(saved, memory, MEM_SIZE);
        const uint64_t checks = list.checks, blocked = list.blocked;
        CHECK(!rerun_draw(&snap, NULL, 0, &list, 1));
        CHECK(list.insn > 0 && list.checks == checks + 1 && list.blocked == blocked + 1 && !list.changed);
        CHECK(!memcmp(&m, &before, sizeof m) && !memcmp(memory, saved, MEM_SIZE));
        CHECK(!sounds && g_f117_observer == &obs);
    }
    {   /* a collector sees the copy's machine, is begun and ended once, and the real machine stays byte for byte as it was */
        memset(&m, 0, sizeof m);
        memset(memory, 0, sizeof memory);
        cpu_init(&m.cpu, memory);
        m.mem = memory; m.ips = MACHINE_DEFAULT_IPS;
        m.cpu.user = &m;
        m.cpu.io_write = pc_io_write; m.cpu.io_read = pc_io_read; m.cpu.int_hook = dos_int_hook;
        pc_reset(&m);
        m.cpu.seg[S_CS] = 0x1000; m.cpu.ip = 0; m.cpu.icount = 100;
        const uint8_t code[] = {0xC6,0x06,0x00,0x02,0x55,0xB0,3,0xE6,0x61,0xB4,0x40,0xCD,0x21};
        memcpy(memory + 0x10000, code, sizeof code);
        m.exec_hook_n = 1; m.exec_hook[0].cs = 0x1000; m.exec_hook[0].ip = 5;      /* at the `mov al, 3` */
        observe_set(&obs);
        rerun_snap_take(&snap, &m);
        before = m; memcpy(saved, memory, MEM_SIZE);
        g_coll_real = &m; g_coll_begin = g_coll_end = g_coll_exec = g_coll_wrong = 0;
        const rerun_collector coll = { coll_begin, coll_exec, coll_end, NULL };
        rerun_set_collector(&coll);
        rerun_draw(&snap, NULL, 0, &list, 1);
        rerun_set_collector(NULL);
        CHECK(g_coll_begin == 1 && g_coll_end == 1 && g_coll_exec == 1 && !g_coll_wrong);
        CHECK(!list.changed && !memcmp(&m, &before, sizeof m) && !memcmp(memory, saved, MEM_SIZE));
        g_coll_exec = 0;                                       /* with no collector the copy's hook reaches nobody */
        rerun_draw(&snap, NULL, 0, &list, 1);
        CHECK(!g_coll_exec && !list.changed);
    }
    observe_set(NULL); rerun_snap_free(&snap); rerun_list_free(&list);
    puts("rerun: private memory/devices, silent hooks, blocked DOS, restored observer and the collector's copy-only hooks pass");
    return 0;
}
