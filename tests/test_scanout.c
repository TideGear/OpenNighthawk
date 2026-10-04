/* ROM-free temporal scanout tests: snapshots cannot pass these cases. */
#include "machine.h"
#include "present.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static present_frame frame;
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void advance(void)
{
    m.cpu.icount = m.scan_next;
    pc_events(&m);
}

int main(void)
{
    m.mem = calloc(1, MEM_SIZE);
    if (!m.mem) return 1;
    cpu_init(&m.cpu, m.mem);
    m.cpu.user = &m;
    m.ips = MACHINE_DEFAULT_IPS;
    m.frame_len = 128413;
    m.irq0_next = ~0ull;
    m.vsync_next = m.frame_len * 412 / 449;
    vga_set_mode(&m, 0x13, 1);
    advance();  /* frame begins, address latched */
    for (unsigned group = 1; group <= 4; group++) {
        memset(m.mem + 0xA0000, (int)group, 64000);
        advance();
        if (group < 4) CHECK(!m.scan_valid);
    }
    CHECK(m.scan_valid);
    present_capture(&m, &frame);
    for (unsigned group = 0; group < 4; group++) {
        CHECK(frame.vram[group * 16000] == group + 1);
        CHECK(frame.vram[group * 16000 + 15999] == group + 1);
    }
    /* Guest writes do not alter an already published frame. */
    memset(m.mem + 0xA0000, 99, 64000);
    present_capture(&m, &frame);
    CHECK(frame.vram[0] == 1 && frame.vram[63999] == 4);
    /* Display start is latched for the whole frame, with a 64K wrap. */
    m.crtc[0x0C] = 0x3F; m.crtc[0x0D] = 0xFC;
    m.mem[0xAFFF0] = 10; m.mem[0xA0000] = 11;
    advance();
    m.crtc[0x0C] = m.crtc[0x0D] = 0;
    advance();
    present_capture(&m, &frame);
    CHECK(frame.vram[0] == 1);  /* old frame until all groups complete */
    advance(); advance();
    pc_io_write(&m.cpu, 0x3C8, 1, 1);
    pc_io_write(&m.cpu, 0x3C9, 31, 1);
    pc_io_write(&m.cpu, 0x3C9, 0, 1);
    pc_io_write(&m.cpu, 0x3C9, 0, 1);
    pc_io_write(&m.cpu, 0x3C6, 0x7F, 1);
    advance();
    present_capture(&m, &frame);
    CHECK(frame.vram[0] == 10 && frame.vram[16] == 11);
    CHECK(frame.dac[3] == 31 && frame.pel_mask == 0xFF && m.scan_mask == 0x7F);
    CHECK(frame.icount == m.scan_time);
    /* A register change after retrace cannot change the next frame's
     * already latched address. This distinguishes a frame-start latch. */
    m.crtc[0x0D] = 5;
    m.cpu.icount = m.vsync_next;
    pc_events(&m);
    m.crtc[0x0D] = 0;
    advance();
    CHECK(m.scan_start == 20);
    /* Equal VRAM with a different scanned picture must fail parity. */
    uint64_t hash = machine_state_hash(&m);
    m.scan_pixels[0] ^= 1;
    CHECK(machine_state_hash(&m) != hash);
    vga_set_mode(&m, 3, 0);
    CHECK(!m.scan_valid && m.scan_next == ~0ull);
    present_capture(&m, &frame);
    CHECK(frame.text);
    vga_set_mode(&m, 0x13, 1);
    CHECK(!m.scan_valid);
    advance(); advance(); advance(); advance(); advance();
    present_capture(&m, &frame);
    CHECK(!frame.text && frame.vram[0] == 0);
    free(m.mem);
    printf("VGA scanout: %d failures\n", failures);
    return failures != 0;
}
