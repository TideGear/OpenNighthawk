/* mouse.c - the INT 33h driver and its software cursor. */
#include "machine.h"

#include <string.h>

static const uint16_t DEFAULT_MOUSE_MASKS[32] = {
    0x3FFF, 0x1FFF, 0x0FFF, 0x07FF, 0x03FF, 0x01FF, 0x00FF, 0x007F,
    0x003F, 0x001F, 0x01FF, 0x10FF, 0x30FF, 0xF87F, 0xF87F, 0xFC3F,
    0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
    0x7F80, 0x7C00, 0x6C00, 0x4600, 0x0600, 0x0300, 0x0300, 0x0000 };

/* The cursor is guest-visible video memory. Hiding restores the saved
 * rectangle even if the program has written under it since. Capturing
 * a frame never redraws it: only driver calls and pointer motion do. */
void mouse_restore_cursor(machine_t *m)
{
    if (m->mouse_hidden || !m->mouse_background) return;
    if (m->mouse_background_text) {
        mem_write16(&m->cpu, m->mouse_back_address, m->mouse_back_text);
    } else {
        for (int y = 0; y < 16; y++) {
            const int sy = m->mouse_back_y + y;
            if (sy < 0 || sy >= 200) continue;
            for (int x = 0; x < 16; x++) {
                const int sx = m->mouse_back_x + x;
                if (sx < 0 || sx >= 320) continue;
                mem_write8(&m->cpu, 0xA0000u + (uint32_t)(sy * 320 + sx),
                           m->mouse_back_pixels[y * 16 + x]);
            }
        }
    }
    m->mouse_background = 0;
}

void mouse_draw_cursor(machine_t *m)
{
    if (m->mouse_hidden) return;
    mouse_restore_cursor(m);
    if (m->video_mode != 0x13) {
        const int col = mouse_gran_x(m, m->mouse_x) / 8;
        const int row = m->mouse_y / 8;
        if (col < 0 || col >= 80 || row < 0 || row >= 25) return;
        m->mouse_back_address = 0xB8000u + (uint32_t)(row * 80 + col) * 2u;
        m->mouse_back_text = mem_read16(&m->cpu, m->mouse_back_address);
        m->mouse_background_text = 1;
        m->mouse_background = 1;
        mem_write16(&m->cpu, m->mouse_back_address,
                    (uint16_t)((m->mouse_back_text & m->mouse_text_and) ^ m->mouse_text_xor));
        return;
    }
    m->mouse_back_x = mouse_gran_x(m, m->mouse_x) / 2 - m->mouse_hot_x;
    m->mouse_back_y = m->mouse_y - m->mouse_hot_y;
    m->mouse_background_text = 0;
    for (int y = 0; y < 16; y++) {
        const int sy = m->mouse_back_y + y;
        if (sy < 0 || sy >= 200) continue;
        for (int x = 0; x < 16; x++) {
            const int sx = m->mouse_back_x + x;
            if (sx < 0 || sx >= 320) continue;
            const uint32_t address = 0xA0000u + (uint32_t)(sy * 320 + sx);
            const uint16_t bit = (uint16_t)(0x8000u >> x);
            const uint8_t background = mem_read8(&m->cpu, address);
            m->mouse_back_pixels[y * 16 + x] = background;
            uint8_t pixel = (m->mouse_masks[y] & bit) ? background : 0;
            if (m->mouse_masks[16 + y] & bit) pixel ^= 0x0F;
            mem_write8(&m->cpu, address, pixel);
        }
    }
    m->mouse_background = 1;
}

void mouse_new_video_mode(machine_t *m)
{
    /* Discard the old background rather than overwrite the new page. */
    m->mouse_hidden = 1;
    m->mouse_background = 0;
    m->mouse_xmin = 0; m->mouse_xmax = 639;
    m->mouse_ymin = 0; m->mouse_ymax = 199;
    m->mouse_hot_x = m->mouse_hot_y = 0;
    memcpy(m->mouse_masks, DEFAULT_MOUSE_MASKS, sizeof m->mouse_masks);
    m->mouse_text_and = 0x77FF;
    m->mouse_text_xor = 0x7700;
}

static void mouse_reset(machine_t *m)
{
    mouse_restore_cursor(m);
    mouse_new_video_mode(m);
    m->mouse_hnd_mask = 0;
    m->mouse_xmin = 0; m->mouse_xmax = 639;
    m->mouse_ymin = 0; m->mouse_ymax = 199;
    m->mouse_x = 320; m->mouse_y = 100;
    memset(m->mouse_press, 0, sizeof m->mouse_press);
    memset(m->mouse_release, 0, sizeof m->mouse_release);
    m->mouse_mickey_x = m->mouse_mickey_y = 0;
    memcpy(m->mouse_masks, DEFAULT_MOUSE_MASKS, sizeof m->mouse_masks);
    m->mouse_hot_x = m->mouse_hot_y = 0;
    m->mouse_driver_installed = 1;
}

int mouse_int33(machine_t *m)
{
    cpu_t *c = &m->cpu;
    uint16_t fn = c->r[R_AX];
    if (!m->mouse_present) {
        if (fn == 0x0000 || fn == 0x0021) { c->r[R_AX] = 0; c->r[R_BX] = 0; }
        return 1;
    }
    switch (fn) {
    case 0x0000: case 0x0021:                 /* reset: present, three buttons (DOSBox) */
        mouse_reset(m);
        c->r[R_AX] = 0xFFFF;
        c->r[R_BX] = 3;
        break;
    case 0x0001:
        if (m->mouse_hidden > 0) m->mouse_hidden--;
        mouse_draw_cursor(m);
        break;
    case 0x0002:
        mouse_restore_cursor(m);
        m->mouse_hidden++;
        break;
    case 0x0003:
        c->r[R_BX] = (uint16_t)m->mouse_buttons;
        c->r[R_CX] = (uint16_t)mouse_gran_x(m, m->mouse_x);
        c->r[R_DX] = (uint16_t)m->mouse_y;
        break;
    case 0x0004: {                            /* DOSBox: clamp; keep a position equal to the rounded one */
        int x = (int16_t)c->r[R_CX], y = (int16_t)c->r[R_DX];
        if (x >= m->mouse_xmax) m->mouse_x = m->mouse_xmax;
        else if (m->mouse_xmin >= x) m->mouse_x = m->mouse_xmin;
        else if (x != mouse_gran_x(m, m->mouse_x)) m->mouse_x = x;
        if (y >= m->mouse_ymax) m->mouse_y = m->mouse_ymax;
        else if (m->mouse_ymin >= y) m->mouse_y = m->mouse_ymin;
        else if (y != m->mouse_y) m->mouse_y = y;
        mouse_draw_cursor(m);
        break;
    }
    case 0x0005: case 0x0006: {               /* press / release data */
        int b = c->r[R_BX] & 1;
        c->r[R_AX] = (uint16_t)m->mouse_buttons;
        if (fn == 5) {
            c->r[R_BX] = (uint16_t)m->mouse_press[b];
            c->r[R_CX] = (uint16_t)m->mouse_press_x[b];
            c->r[R_DX] = (uint16_t)m->mouse_press_y[b];
            m->mouse_press[b] = 0;
        } else {
            c->r[R_BX] = (uint16_t)m->mouse_release[b];
            c->r[R_CX] = (uint16_t)m->mouse_rel_x[b];
            c->r[R_DX] = (uint16_t)m->mouse_rel_y[b];
            m->mouse_release[b] = 0;
        }
        break;
    }
    case 0x0007: {
        int a = (int16_t)c->r[R_CX], b = (int16_t)c->r[R_DX];
        m->mouse_xmin = a < b ? a : b; m->mouse_xmax = a < b ? b : a;
        if (m->mouse_x < m->mouse_xmin) m->mouse_x = m->mouse_xmin;
        if (m->mouse_x > m->mouse_xmax) m->mouse_x = m->mouse_xmax;
        break;
    }
    case 0x0008: {
        int a = (int16_t)c->r[R_CX], b = (int16_t)c->r[R_DX];
        m->mouse_ymin = a < b ? a : b; m->mouse_ymax = a < b ? b : a;
        if (m->mouse_y < m->mouse_ymin) m->mouse_y = m->mouse_ymin;
        if (m->mouse_y > m->mouse_ymax) m->mouse_y = m->mouse_ymax;
        break;
    }
    case 0x0009:                              /* graphics cursor shape */
        m->mouse_hot_x = (int16_t)c->r[R_BX];
        m->mouse_hot_y = (int16_t)c->r[R_CX];
        for (int i = 0; i < 32; i++)
            m->mouse_masks[i] = seg_read16(c, c->seg[S_ES], (uint16_t)(c->r[R_DX] + i * 2));
        mouse_draw_cursor(m);
        break;
    case 0x000A:
        m->mouse_text_and = c->r[R_CX];
        m->mouse_text_xor = c->r[R_DX];
        break;
    case 0x000B:                              /* motion counters */
        c->r[R_CX] = (uint16_t)m->mouse_mickey_x;
        c->r[R_DX] = (uint16_t)m->mouse_mickey_y;
        m->mouse_mickey_x = m->mouse_mickey_y = 0;
        break;
    case 0x000C:
        m->mouse_hnd_mask = c->r[R_CX];
        m->mouse_hnd_off = c->r[R_DX];
        m->mouse_hnd_seg = c->seg[S_ES];
        dos_log(m, "[mouse] %s installed an event handler at %04X:%04X mask %04X (not called)\n",
                dos_current_program(m), m->mouse_hnd_seg, m->mouse_hnd_off, m->mouse_hnd_mask);
        break;
    case 0x0015: c->r[R_BX] = 0x40; break;
    case 0x0024: c->r[R_BX] = 0x0800; c->r[R_CX] = 0x0400; break;   /* 8.00, PS/2 */
    default: break;                           /* ratios, pages, exclusion: accepted */
    }
    return 1;
}

