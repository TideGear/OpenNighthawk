/* ROM-free DAC regressions: BIOS calls and direct ports share one device. */
#include "machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void bios(unsigned ax)
{
    m.cpu.r[R_AX] = (uint16_t)ax;
    /* INT 10h's callback alias, with a harmless synthetic interrupt frame. */
    dos_int_hook(&m.cpu, 0xE0);
}

int main(void)
{
    m.mem = calloc(1, MEM_SIZE);
    if (!m.mem) return 1;
    cpu_init(&m.cpu, m.mem);
    m.cpu.user = &m;
    m.ips = MACHINE_DEFAULT_IPS;
    m.pel_mask = 0xFF;
    m.cpu.seg[S_ES] = 0x2000;
    m.cpu.r[R_SP] = 0x8000;
    const uint8_t bytes[6] = { 1, 2, 3, 4, 5, 6 };
    /* The BIOS buffer is linear even at ES:FFFF; the DAC wraps at 256. */
    memcpy(m.mem + 0x2FFFF, bytes, sizeof bytes);
    m.cpu.r[R_BX] = 255; m.cpu.r[R_CX] = 2; m.cpu.r[R_DX] = 0xFFFF;
    bios(0x1012);
    CHECK(memcmp(m.dac + 255 * 3, bytes, 3) == 0);
    CHECK(memcmp(m.dac, bytes + 3, 3) == 0);
    CHECK(m.dac_widx == 1 && m.dac_state == 0 && m.dac_comp == 0);
    CHECK(m.cpu.icount == 7 * 6);
    const uint64_t before = m.cpu.icount;
    m.cpu.r[R_BX] = 255; m.cpu.r[R_CX] = 2; m.cpu.r[R_DX] = 0xFFFE;
    bios(0x1017);
    CHECK(memcmp(m.mem + 0x2FFFE, bytes, sizeof bytes) == 0);
    CHECK(m.dac_ridx == 1 && m.dac_widx == 0 && m.dac_state == 3 && m.dac_comp == 0);
    CHECK(m.cpu.icount - before == 6 + 6 * 8);
    m.cpu.r[R_BX] = 4; m.cpu.r[R_CX] = 0x7FC1; m.cpu.r[R_DX] = 0x4000;
    bios(0x1010);
    CHECK(m.dac[12] == 0 && m.dac[13] == 63 && m.dac[14] == 1);
    CHECK(m.dac_widx == 5 && m.dac_state == 0);
    m.cpu.r[R_BX] = 4; m.cpu.r[R_DX] = 0x1234;
    bios(0x1015);
    CHECK(m.cpu.r[R_DX] == 0x0034 && m.cpu.r[R_CX] == 0x3F01);
    CHECK(m.dac_widx == 5 && m.dac_ridx == 5 && m.dac_state == 3);
    pc_io_write(&m.cpu, 0x3C7, 27, 1);
    CHECK(pc_io_read(&m.cpu, 0x3C8, 1) == 28);
    /* A zero-length block still selects the DAC address and direction. */
    m.cpu.r[R_BX] = 77; m.cpu.r[R_CX] = 0; m.cpu.r[R_DX] = 0;
    bios(0x1012);
    CHECK(m.dac_widx == 77 && m.dac_state == 0 && m.dac_comp == 0);
    /* Render palette changes only after blue; reads see red immediately. */
    pc_io_write(&m.cpu, 0x3C8, 4, 1);
    pc_io_write(&m.cpu, 0x3C9, 22, 1);
    CHECK(m.dac[12] == 22 && m.dac_display[12] == 0);
    pc_io_write(&m.cpu, 0x3C9, 23, 1);
    CHECK(m.dac[13] == 23 && m.dac_display[13] == 63);
    pc_io_write(&m.cpu, 0x3C9, 24, 1);
    CHECK(m.dac_display[12] == 22 && m.dac_display[13] == 23 && m.dac_display[14] == 24);
    pc_io_write(&m.cpu, 0x3C8, 4, 1);
    pc_io_write(&m.cpu, 0x3C9, 25, 1);
    pc_io_write(&m.cpu, 0x3C6, 0x7F, 1);
    CHECK(m.dac_display[12] == 25);
    pc_io_write(&m.cpu, 0x3C9, 26, 1);
    pc_io_write(&m.cpu, 0x3C6, 0x7F, 1);
    CHECK(m.dac[13] == 26 && m.dac_display[13] == 23);
    /* With a mask, an unrelated completed triplet can publish a partial
     * source colour for one alias while the source's own display stays old. */
    pc_io_write(&m.cpu, 0x3C6, 3, 1);
    CHECK(m.dac_display[0] == 4 && m.dac_display[12] == 4);
    pc_io_write(&m.cpu, 0x3C8, 0, 1);
    pc_io_write(&m.cpu, 0x3C9, 17, 1);
    pc_io_write(&m.cpu, 0x3C8, 4, 1);
    pc_io_write(&m.cpu, 0x3C9, 1, 1);
    pc_io_write(&m.cpu, 0x3C9, 2, 1);
    pc_io_write(&m.cpu, 0x3C9, 3, 1);
    CHECK(m.dac_display[0] == 4 && m.dac_display[12] == 17);
    /* DOSBox omits I/O delay when fewer than three bus cycles remain in
     * the CPU slice. Check either side of the boundary, not just averages. */
    m.cpu.icount = 8976;
    pc_io_read(&m.cpu, 0x3C7, 1);
    CHECK(m.cpu.icount == 8984);
    m.cpu.icount = 8977;
    pc_io_read(&m.cpu, 0x3C7, 1);
    CHECK(m.cpu.icount == 8977);
    m.cpu.icount = 8982;
    pc_io_write(&m.cpu, 0x3C8, 0, 1);
    CHECK(m.cpu.icount == 8988);
    m.cpu.icount = 8983;
    pc_io_write(&m.cpu, 0x3C8, 0, 1);
    CHECK(m.cpu.icount == 8983);
    /* PIT and VGA events also split DOSBox's millisecond budget. */
    m.cpu.icount = 100; m.irq0_next = 124;
    pc_io_read(&m.cpu, 0x3C7, 1);
    CHECK(m.cpu.icount == 108);
    m.cpu.icount = 101;
    pc_io_read(&m.cpu, 0x3C7, 1);
    CHECK(m.cpu.icount == 101);
    m.irq0_next = 0; m.frame_len = 128413;
    m.cpu.icount = m.frame_len * 100 / 449 - 1;
    const uint64_t edge = m.cpu.icount;
    pc_io_write(&m.cpu, 0x3C8, 0, 1);
    CHECK(m.cpu.icount == edge);
    free(m.mem);
    printf("VGA DAC: %d failures\n", failures);
    return failures != 0;
}
