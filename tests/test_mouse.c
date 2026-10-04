/* ROM-free regressions for the guest-visible software cursor. */
#include "machine.h"
#include "present.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void call(unsigned fn)
{
    m.cpu.r[R_AX] = (uint16_t)fn;
    mouse_int33(&m);
}

int main(void)
{
    m.mem = calloc(1, MEM_SIZE);
    if (!m.mem) return 1;
    cpu_init(&m.cpu, m.mem);
    m.cpu.user = &m;
    m.mouse_present = 1;
    m.video_mode = 0x13;
    mouse_new_video_mode(&m);
    call(0);
    m.mouse_x = 0; m.mouse_y = 0;
    /* One inverted pixel, leaving all other pixels unchanged. */
    for (int i = 0; i < 16; i++) m.mouse_masks[i] = 0xFFFF;
    memset(m.mouse_masks + 16, 0, 32);
    m.mouse_masks[16] = 0x8000;
    m.mem[0xA0000] = 0x72;
    call(1);
    CHECK(m.mem[0xA0000] == 0x7D);
    /* Rendering must neither XOR again nor read a synthetic cursor layer. */
    static present_frame frame;
    present_capture(&m, &frame);
    CHECK(frame.vram[0] == 0x7D);
    m.mem[0xA0000] = 0x39;
    present_capture(&m, &frame);
    CHECK(frame.vram[0] == 0x39);
    call(2);
    CHECK(m.mem[0xA0000] == 0x72);
    call(2); call(1);
    CHECK(m.mouse_hidden == 1 && m.mem[0xA0000] == 0x72);
    call(1);
    CHECK(m.mem[0xA0000] == 0x7D);
    m.cpu.r[R_CX] = 4; m.cpu.r[R_DX] = 0; call(4);
    CHECK(m.mem[0xA0000] == 0x72 && m.mem[0xA0002] == 0x0F);
    call(0);
    CHECK(m.mem[0xA0002] == 0 && m.mouse_hidden == 1);

    /* Negative hotspot and bottom/right clipping never cross VGA bounds. */
    m.mouse_x = 0; m.mouse_y = 0;
    m.mouse_hot_x = 4; m.mouse_hot_y = 3;
    for (int i = 0; i < 32; i++) m.mouse_masks[i] = 0xFFFF;
    m.mem[0x9FFFF] = 0xAB; m.mem[0xAFA00] = 0xCD;
    call(1);
    CHECK(m.mem[0xA0000] == (0x72 ^ 15));
    CHECK(m.mem[0x9FFFF] == 0xAB && m.mem[0xAFA00] == 0xCD);
    call(2);
    CHECK(m.mem[0xA0000] == 0x72);
    m.mouse_hot_x = m.mouse_hot_y = 0;
    m.mouse_x = 638; m.mouse_y = 199;
    m.mem[0xAF9FF] = 0x47;
    call(1);
    CHECK(m.mem[0xAF9FF] == 0x48 && m.mem[0xAFA00] == 0xCD);
    call(2);
    CHECK(m.mem[0xAF9FF] == 0x47);
    call(1);
    mouse_new_video_mode(&m);
    memset(m.mem + 0xA0000, 0, 64000);
    call(2);
    CHECK(m.mem[0xAF9FF] == 0);

    m.video_mode = 3; mouse_new_video_mode(&m);
    m.mouse_x = 8; m.mouse_y = 8;
    mem_write16(&m.cpu, 0xB8000 + 162, 0x1E41);
    call(1);
    CHECK(mem_read16(&m.cpu, 0xB8000 + 162) == ((0x1E41 & 0x77FF) ^ 0x7700));
    call(2);
    CHECK(mem_read16(&m.cpu, 0xB8000 + 162) == 0x1E41);
    free(m.mem);
    printf("software cursor: %d failures\n", failures);
    return failures != 0;
}
