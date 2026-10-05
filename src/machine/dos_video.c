/* dos_video.c - extracted DOS/BIOS services; see dos.c for provenance. */
#include "dos_internal.h"
#include "x86_sem.h"

/* ===================================================================== */
/* Text console                                                          */
/* ===================================================================== */

static int text_mode(const machine_t *m)
{
    return m->video_mode <= 3 || m->video_mode == 7;
}

static uint32_t text_vram(const machine_t *m)
{
    return m->video_mode == 7 ? 0xB0000u : 0xB8000u;
}

static unsigned text_columns(cpu_t *c)
{
    const unsigned cols = mem_read16(c, BDA_VIDEO_COLS);
    return cols == 40 ? 40u : 80u;
}

static void text_scroll_up(machine_t *m, unsigned top, unsigned left, unsigned bottom,
                           unsigned right, unsigned lines, uint8_t attr)
{
    cpu_t *c = &m->cpu;
    const unsigned cols = text_columns(c);
    const uint32_t base = text_vram(m);
    if (right >= cols) right = cols - 1;
    if (bottom > 24) bottom = 24;
    if (top > bottom || left > right) return;
    const unsigned height = bottom - top + 1;
    if (lines == 0 || lines > height) lines = height;
    for (unsigned r = top; r <= bottom; r++)
        for (unsigned x = left; x <= right; x++) {
            uint32_t dst = base + (r * cols + x) * 2u;
            if (r + lines <= bottom) {
                uint32_t src = base + ((r + lines) * cols + x) * 2u;
                mem_write8(c, dst, mem_read8(c, src));
                mem_write8(c, dst + 1, mem_read8(c, src + 1));
            } else {
                mem_write8(c, dst, ' ');
                mem_write8(c, dst + 1, attr);
            }
        }
}

static void text_scroll_down(machine_t *m, unsigned top, unsigned left, unsigned bottom,
                             unsigned right, unsigned lines, uint8_t attr)
{
    cpu_t *c = &m->cpu;
    const unsigned cols = text_columns(c);
    const uint32_t base = text_vram(m);
    if (right >= cols) right = cols - 1;
    if (bottom > 24) bottom = 24;
    if (top > bottom || left > right) return;
    const unsigned height = bottom - top + 1;
    if (lines == 0 || lines > height) lines = height;
    for (int r = (int)bottom; r >= (int)top; r--)
        for (unsigned x = left; x <= right; x++) {
            uint32_t dst = base + ((unsigned)r * cols + x) * 2u;
            if (r - (int)lines >= (int)top) {
                uint32_t src = base + (((unsigned)r - lines) * cols + x) * 2u;
                mem_write8(c, dst, mem_read8(c, src));
                mem_write8(c, dst + 1, mem_read8(c, src + 1));
            } else {
                mem_write8(c, dst, ' ');
                mem_write8(c, dst + 1, attr);
            }
        }
}

static void text_output_char(machine_t *m, uint8_t ch)
{
    cpu_t *c = &m->cpu;
    if (!text_mode(m)) return;
    const unsigned cols = text_columns(c);
    const uint32_t base = text_vram(m);
    const uint16_t pos = mem_read16(c, BDA_CURSOR_POS);
    unsigned row = pos >> 8, col = pos & 0xFFu;
    if (row >= 25u) row = 24u;
    if (col >= cols) col = cols - 1u;

    switch (ch) {
    case '\r': col = 0; break;
    case '\n': row++; break;
    case '\b': if (col) col--; break;
    case 7:    break;                        /* bell */
    default: {
        const uint32_t cell = base + (row * cols + col) * 2u;
        mem_write8(c, cell, ch);
        col++;
        if (col >= cols) { col = 0; row++; }
        break;
    }
    }
    if (row >= 25u) {
        uint8_t attr = mem_read8(c, base + (24u * cols) * 2u + 1u);
        text_scroll_up(m, 0, 0, 24, cols - 1, 1, attr ? attr : 0x07);
        row = 24u;
    }
    mem_write16(c, BDA_CURSOR_POS, (uint16_t)(((row & 0xFFu) << 8) | (col & 0xFFu)));
}

void dos_console_text(machine_t *m, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) text_output_char(m, (uint8_t)s[i]);
    if (m->hooks.console) {
        char buf[600];
        size_t k = n < sizeof buf - 1 ? n : sizeof buf - 1;
        memcpy(buf, s, k);
        buf[k] = 0;
        m->hooks.console(m->hooks.user, buf);
    }
}

/* ===================================================================== */
/* INT 10h                                                               */
/* ===================================================================== */

int dos_int10(machine_t *m)
{
    cpu_t *c = &m->cpu;
    uint8_t ah = (uint8_t)(c->r[R_AX] >> 8);
    uint8_t al = (uint8_t)(c->r[R_AX] & 0xFF);
    switch (ah) {
    case 0x00: {                             /* set video mode */
        uint8_t mode = (uint8_t)(al & 0x7F);
        vga_set_mode(m, mode, !(al & 0x80));
        mem_write8(c, BDA_VIDEO_MODE, mode);
        uint16_t cols = (mode == 0 || mode == 1 || mode == 0x13) ? 40 : 80;
        mem_write16(c, BDA_VIDEO_COLS, cols);
        mem_write16(c, BDA_CURSOR_POS, 0);
        break;
    }
    case 0x01: mem_write16(c, BDA_CURSOR_TYPE, c->r[R_CX]); break;
    case 0x02: mem_write16(c, BDA_CURSOR_POS, c->r[R_DX]); break;
    case 0x03:
        c->r[R_DX] = mem_read16(c, BDA_CURSOR_POS);
        c->r[R_CX] = mem_read16(c, BDA_CURSOR_TYPE);
        break;
    case 0x05: break;                        /* page: only page 0 is used */
    case 0x06: case 0x07: {                  /* scroll window */
        unsigned top = (c->r[R_CX] >> 8) & 0xFF, left = c->r[R_CX] & 0xFF;
        unsigned bottom = (c->r[R_DX] >> 8) & 0xFF, right = c->r[R_DX] & 0xFF;
        uint8_t attr = (uint8_t)(c->r[R_BX] >> 8);
        if (text_mode(m)) {
            if (ah == 6) text_scroll_up(m, top, left, bottom, right, al, attr);
            else         text_scroll_down(m, top, left, bottom, right, al, attr);
        }
        break;
    }
    case 0x08: {                             /* read character/attribute */
        if (!text_mode(m)) { c->r[R_AX] = 0; break; }
        uint16_t pos = mem_read16(c, BDA_CURSOR_POS);
        uint32_t cell = text_vram(m) + (((pos >> 8) * text_columns(c)) + (pos & 0xFF)) * 2u;
        c->r[R_AX] = mem_read16(c, cell);
        break;
    }
    case 0x09: case 0x0A: {                  /* write char (and attribute) CX times */
        if (!text_mode(m)) break;
        uint16_t pos = mem_read16(c, BDA_CURSOR_POS);
        unsigned cols = text_columns(c);
        uint32_t cell = text_vram(m) + (((pos >> 8) * cols) + (pos & 0xFF)) * 2u;
        for (unsigned i = 0; i < c->r[R_CX] && cell < text_vram(m) + cols * 25u * 2u; i++, cell += 2) {
            mem_write8(c, cell, al);
            if (ah == 0x09) mem_write8(c, cell + 1, (uint8_t)c->r[R_BX]);
        }
        break;
    }
    case 0x0E: text_output_char(m, al); break;
    case 0x0F:
        c->r[R_AX] = (uint16_t)((mem_read16(c, BDA_VIDEO_COLS) << 8) | m->video_mode);
        c->r[R_BX] = (uint16_t)(c->r[R_BX] & 0x00FF);
        break;
    case 0x10:                               /* palette / DAC */
        switch (al) {
        case 0x00: if ((c->r[R_BX] & 0xFF) < 16) m->attr[c->r[R_BX] & 0xFF] = (uint8_t)(c->r[R_BX] >> 8); break;
        case 0x10: {
            pc_io_write(c, 0x3C8, c->r[R_BX] & 0xFF, 1);
            pc_io_write(c, 0x3C9, c->r[R_DX] >> 8, 1);
            pc_io_write(c, 0x3C9, c->r[R_CX] >> 8, 1);
            pc_io_write(c, 0x3C9, c->r[R_CX] & 0xFF, 1);
            break;
        }
        case 0x12: {
            uint32_t address = phys(c->seg[S_ES], c->r[R_DX]);
            pc_io_write(c, 0x3C8, c->r[R_BX] & 0xFF, 1);
            /* BIOS accesses a linear buffer; the DAC's eight-bit index
             * wraps at 256 even when the block crosses that boundary. */
            for (uint32_t i = 0; i < (uint32_t)c->r[R_CX] * 3u; i++)
                pc_io_write(c, 0x3C9, mem_read8(c, address + i), 1);
            break;
        }
        case 0x15: {
            pc_io_write(c, 0x3C7, c->r[R_BX] & 0xFF, 1);
            const uint8_t red = (uint8_t)pc_io_read(c, 0x3C9, 1);
            const uint8_t green = (uint8_t)pc_io_read(c, 0x3C9, 1);
            const uint8_t blue = (uint8_t)pc_io_read(c, 0x3C9, 1);
            c->r[R_DX] = (uint16_t)((c->r[R_DX] & 0xFF) | ((uint16_t)red << 8));
            c->r[R_CX] = (uint16_t)(((uint16_t)green << 8) | blue);
            break;
        }
        case 0x17: {
            uint32_t address = phys(c->seg[S_ES], c->r[R_DX]);
            pc_io_write(c, 0x3C7, c->r[R_BX] & 0xFF, 1);
            for (uint32_t i = 0; i < (uint32_t)c->r[R_CX] * 3u; i++)
                mem_write8(c, address + i, (uint8_t)pc_io_read(c, 0x3C9, 1));
            break;
        }
        default: break;
        }
        break;
    case 0x11: break;                        /* fonts */
    case 0x12:                               /* alternate select */
        if ((c->r[R_BX] & 0xFF) == 0x10) { c->r[R_BX] = 0x0003; c->r[R_CX] = 0x0009; }
        break;
    case 0x1A:                               /* display combination: VGA colour */
        if (al == 0) {
            c->r[R_AX] = 0x001A;                 /* AH cleared, as DOSBox returns it */
            c->r[R_BX] = 0x0008;
        }
        break;
    default: break;
    }
    return 1;
}

