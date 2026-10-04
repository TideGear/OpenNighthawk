/* present.c - turning the emulated VGA's state into pixels. */
#include "present.h"
#include "font8x16.h"

#include <stdio.h>
#include <string.h>

void present_capture(const machine_t *m, present_frame *f)
{
    f->text = m->video_mode != 0x13;
    f->blank = (m->seq[1] & 0x20) != 0;
    if (f->text) memcpy(f->vram, m->mem + 0xB8000, 4000);
    else memcpy(f->vram, m->mem + 0xA0000, 64000);
    memcpy(f->dac, m->dac, 768);
    f->pel_mask = m->pel_mask;
    f->cursor_pos = (uint16_t)(m->mem[0x450] | (m->mem[0x451] << 8));
    f->cursor_type = (uint16_t)(m->mem[0x460] | (m->mem[0x461] << 8));
    f->mouse_shown = m->mouse_driver_installed && m->mouse_hidden == 0;
    f->mouse_x = m->mouse_x / 2;      /* mode 13h: driver x is doubled */
    f->mouse_y = m->mouse_y;
    memcpy(f->mouse_masks, m->mouse_masks, sizeof f->mouse_masks);
    f->mouse_hot_x = m->mouse_hot_x;
    f->mouse_hot_y = m->mouse_hot_y;
    f->icount = m->cpu.icount;
}

static uint32_t dac_rgb(const uint8_t *dac, unsigned i)
{
    const uint8_t *p = &dac[i * 3];
    uint32_t r = (uint32_t)((p[0] << 2) | (p[0] >> 4));
    uint32_t g = (uint32_t)((p[1] << 2) | (p[1] >> 4));
    uint32_t b = (uint32_t)((p[2] << 2) | (p[2] >> 4));
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* The text attribute's colour through the default attribute palette:
 * 0-5 and 7 map to themselves, 6 to 0x14 (brown), 8-15 to 0x38-0x3F, all
 * read from the EGA-compatible block of the DAC. */
static uint32_t text_colour(const present_frame *f, unsigned i)
{
    static const uint8_t attr_pal[16] = { 0, 1, 2, 3, 4, 5, 0x14, 7,
                                          0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F };
    static const uint8_t ega[64][3] = {
#define C(v) ((v) ? 42 : 0)
#define L(v) ((v) ? 21 : 0)
#define E(i) { (uint8_t)(C((i) & 4) + L((i) & 32)), (uint8_t)(C((i) & 2) + L((i) & 16)), (uint8_t)(C((i) & 1) + L((i) & 8)) }
        E(0),E(1),E(2),E(3),E(4),E(5),E(6),E(7),E(8),E(9),E(10),E(11),E(12),E(13),E(14),E(15),
        E(16),E(17),E(18),E(19),E(20),E(21),E(22),E(23),E(24),E(25),E(26),E(27),E(28),E(29),E(30),E(31),
        E(32),E(33),E(34),E(35),E(36),E(37),E(38),E(39),E(40),E(41),E(42),E(43),E(44),E(45),E(46),E(47),
        E(48),E(49),E(50),E(51),E(52),E(53),E(54),E(55),E(56),E(57),E(58),E(59),E(60),E(61),E(62),E(63)
#undef E
#undef L
#undef C
    };
    (void)f;
    const uint8_t *p = ega[attr_pal[i & 15]];
    uint32_t r = (uint32_t)((p[0] << 2) | (p[0] >> 4));
    uint32_t g = (uint32_t)((p[1] << 2) | (p[1] >> 4));
    uint32_t b = (uint32_t)((p[2] << 2) | (p[2] >> 4));
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

void present_render(const present_frame *f, uint32_t *out, int *w, int *h, int blink)
{
    if (f->text) {
        *w = 640; *h = 400;
        for (int row = 0; row < 25; row++)
            for (int col = 0; col < 80; col++) {
                const uint8_t ch = f->vram[(row * 80 + col) * 2];
                const uint8_t at = f->vram[(row * 80 + col) * 2 + 1];
                uint32_t fg = text_colour(f, at & 15), bg = text_colour(f, (at >> 4) & 7);
                if ((at & 0x80) && blink) fg = bg;
                for (int y = 0; y < 16; y++) {
                    uint8_t bits = FONT8X16[ch][y];
                    uint32_t *o = out + (row * 16 + y) * 640 + col * 8;
                    for (int x = 0; x < 8; x++) o[x] = (bits & (0x80 >> x)) ? fg : bg;
                }
            }
        /* the hardware cursor: scan lines from the cursor type, doubled
         * from the 8-line CGA convention the BIOS keeps */
        const int crow = f->cursor_pos >> 8, ccol = f->cursor_pos & 0xFF;
        const int start = (f->cursor_type >> 8) & 0x1F, end = f->cursor_type & 0x1F;
        if (blink == 0 && crow < 25 && ccol < 80 && !(f->cursor_type & 0x2000) && start <= end) {
            uint32_t fg = text_colour(f, f->vram[(crow * 80 + ccol) * 2 + 1] & 15);
            for (int y = start * 2; y <= end * 2 + 1 && y < 16; y++)
                for (int x = 0; x < 8; x++) out[(crow * 16 + y) * 640 + ccol * 8 + x] = fg;
        }
        if (f->blank) for (int i = 0; i < 640 * 400; i++) out[i] = 0xFF000000u;
        return;
    }

    *w = 320; *h = 200;
    uint32_t pal[256];
    for (unsigned i = 0; i < 256; i++) pal[i] = dac_rgb(f->dac, i & f->pel_mask);
    if (f->blank) {
        for (int i = 0; i < 64000; i++) out[i] = 0xFF000000u;
        return;
    }
    if (!f->mouse_shown) {
        for (int i = 0; i < 64000; i++) out[i] = pal[f->vram[i]];
        return;
    }
    /* The driver's cursor, as it would have drawn it into the page: the
     * screen mask ANDs the pixel to colour 0, the cursor mask XORs 15. */
    static uint8_t px[64000];
    memcpy(px, f->vram, sizeof px);
    const int x0 = f->mouse_x - f->mouse_hot_x, y0 = f->mouse_y - f->mouse_hot_y;
    for (int y = 0; y < 16; y++) {
        const int sy = y0 + y;
        if (sy < 0 || sy >= 200) continue;
        for (int x = 0; x < 16; x++) {
            const int sx = x0 + x;
            if (sx < 0 || sx >= 320) continue;
            const uint16_t bit = (uint16_t)(0x8000u >> x);
            uint8_t v = px[sy * 320 + sx];
            if (!(f->mouse_masks[y] & bit)) v = 0;
            if (f->mouse_masks[16 + y] & bit) v ^= 0x0F;
            px[sy * 320 + sx] = v;
        }
    }
    for (int i = 0; i < 64000; i++) out[i] = pal[px[i]];
}

int present_write_ppm(const machine_t *m, const char *path)
{
    static present_frame f;
    static uint32_t buf[640 * 400];
    int w, h;
    present_capture(m, &f);
    present_render(&f, buf, &w, &h, 0);
    FILE *o = fopen(path, "wb");
    if (!o) return 0;
    fprintf(o, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint8_t p[3] = { (uint8_t)(buf[i] >> 16), (uint8_t)(buf[i] >> 8), (uint8_t)buf[i] };
        fwrite(p, 1, 3, o);
    }
    fclose(o);
    return 1;
}
