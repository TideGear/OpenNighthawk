/* dos_keyboard.c - extracted DOS/BIOS services; see dos.c for provenance. */
#include "dos_internal.h"
#include "x86_sem.h"
#include <stdlib.h>

/* ===================================================================== */
/* BIOS keyboard                                                         */
/* ===================================================================== */

static int kbd_push(machine_t *m, uint16_t word)
{
    cpu_t *c = &m->cpu;
    uint16_t start = mem_read16(c, BDA_KBD_START), end = mem_read16(c, BDA_KBD_END);
    uint16_t head = mem_read16(c, BDA_KBD_HEAD), tail = mem_read16(c, BDA_KBD_TAIL);
    uint16_t next = (uint16_t)(tail + 2);
    if (next >= end) next = start;
    if (next == head) return 0;                     /* buffer full */
    mem_write16(c, 0x400u + tail, word);
    mem_write16(c, BDA_KBD_TAIL, next);
    return 1;
}

int dos_kbd_pop(machine_t *m, uint16_t *key, int remove)
{
    cpu_t *c = &m->cpu;
    uint16_t start = mem_read16(c, BDA_KBD_START), end = mem_read16(c, BDA_KBD_END);
    uint16_t head = mem_read16(c, BDA_KBD_HEAD), tail = mem_read16(c, BDA_KBD_TAIL);
    if (head == tail) return 0;
    *key = mem_read16(c, 0x400u + head);
    if (remove) {
        uint16_t next = (uint16_t)(head + 2);
        if (next >= end) next = start;
        mem_write16(c, BDA_KBD_HEAD, next);
    }
    return 1;
}

/* US layout, set 1, scancodes 01..58: normal, shifted, ctrl, alt words.
 * The high byte of each word is the scancode the BIOS reports. 0 = none. */
typedef struct { uint16_t n, s, c, a; } keymap;
static const keymap KEYS[0x59] = {
    [0x01] = {0x011B,0x011B,0x011B,0x0100},
    [0x02] = {0x0231,0x0221,0x0000,0x7800}, [0x03] = {0x0332,0x0340,0x0300,0x7900},
    [0x04] = {0x0433,0x0423,0x0000,0x7A00}, [0x05] = {0x0534,0x0524,0x0000,0x7B00},
    [0x06] = {0x0635,0x0625,0x0000,0x7C00}, [0x07] = {0x0736,0x075E,0x071E,0x7D00},
    [0x08] = {0x0837,0x0826,0x0000,0x7E00}, [0x09] = {0x0938,0x092A,0x0000,0x7F00},
    [0x0A] = {0x0A39,0x0A28,0x0000,0x8000}, [0x0B] = {0x0B30,0x0B29,0x0000,0x8100},
    [0x0C] = {0x0C2D,0x0C5F,0x0C1F,0x8200}, [0x0D] = {0x0D3D,0x0D2B,0x0000,0x8300},
    [0x0E] = {0x0E08,0x0E08,0x0E7F,0x0E00}, [0x0F] = {0x0F09,0x0F00,0x9400,0xA500},
    [0x10] = {0x1071,0x1051,0x1011,0x1000}, [0x11] = {0x1177,0x1157,0x1117,0x1100},
    [0x12] = {0x1265,0x1245,0x1205,0x1200}, [0x13] = {0x1372,0x1352,0x1312,0x1300},
    [0x14] = {0x1474,0x1454,0x1414,0x1400}, [0x15] = {0x1579,0x1559,0x1519,0x1500},
    [0x16] = {0x1675,0x1655,0x1615,0x1600}, [0x17] = {0x1769,0x1749,0x1709,0x1700},
    [0x18] = {0x186F,0x184F,0x180F,0x1800}, [0x19] = {0x1970,0x1950,0x1910,0x1900},
    [0x1A] = {0x1A5B,0x1A7B,0x1A1B,0x1A00}, [0x1B] = {0x1B5D,0x1B7D,0x1B1D,0x1B00},
    [0x1C] = {0x1C0D,0x1C0D,0x1C0A,0x1C00},
    [0x1E] = {0x1E61,0x1E41,0x1E01,0x1E00}, [0x1F] = {0x1F73,0x1F53,0x1F13,0x1F00},
    [0x20] = {0x2064,0x2044,0x2004,0x2000}, [0x21] = {0x2166,0x2146,0x2106,0x2100},
    [0x22] = {0x2267,0x2247,0x2207,0x2200}, [0x23] = {0x2368,0x2348,0x2308,0x2300},
    [0x24] = {0x246A,0x244A,0x240A,0x2400}, [0x25] = {0x256B,0x254B,0x250B,0x2500},
    [0x26] = {0x266C,0x264C,0x260C,0x2600}, [0x27] = {0x273B,0x273A,0x0000,0x2700},
    [0x28] = {0x2827,0x2822,0x0000,0x2800}, [0x29] = {0x2960,0x297E,0x0000,0x2900},
    [0x2B] = {0x2B5C,0x2B7C,0x2B1C,0x2B00},
    [0x2C] = {0x2C7A,0x2C5A,0x2C1A,0x2C00}, [0x2D] = {0x2D78,0x2D58,0x2D18,0x2D00},
    [0x2E] = {0x2E63,0x2E43,0x2E03,0x2E00}, [0x2F] = {0x2F76,0x2F56,0x2F16,0x2F00},
    [0x30] = {0x3062,0x3042,0x3002,0x3000}, [0x31] = {0x316E,0x314E,0x310E,0x3100},
    [0x32] = {0x326D,0x324D,0x320D,0x3200}, [0x33] = {0x332C,0x333C,0x0000,0x3300},
    [0x34] = {0x342E,0x343E,0x0000,0x3400}, [0x35] = {0x352F,0x353F,0x0000,0x3500},
    [0x37] = {0x372A,0x372A,0x9600,0x3700},
    [0x39] = {0x3920,0x3920,0x3920,0x3920},
    [0x3B] = {0x3B00,0x5400,0x5E00,0x6800}, [0x3C] = {0x3C00,0x5500,0x5F00,0x6900},
    [0x3D] = {0x3D00,0x5600,0x6000,0x6A00}, [0x3E] = {0x3E00,0x5700,0x6100,0x6B00},
    [0x3F] = {0x3F00,0x5800,0x6200,0x6C00}, [0x40] = {0x4000,0x5900,0x6300,0x6D00},
    [0x41] = {0x4100,0x5A00,0x6400,0x6E00}, [0x42] = {0x4200,0x5B00,0x6500,0x6F00},
    [0x43] = {0x4300,0x5C00,0x6600,0x7000}, [0x44] = {0x4400,0x5D00,0x6700,0x7100},
    /* keypad: the cursor words here; with NumLock xor Shift, the digits */
    [0x47] = {0x4700,0x4737,0x7700,0x0000}, [0x48] = {0x4800,0x4838,0x8D00,0x0000},
    [0x49] = {0x4900,0x4939,0x8400,0x0000}, [0x4A] = {0x4A2D,0x4A2D,0x8E00,0x4A00},
    [0x4B] = {0x4B00,0x4B34,0x7300,0x0000}, [0x4C] = {0x4C00,0x4C35,0x8F00,0x0000},
    [0x4D] = {0x4D00,0x4D36,0x7400,0x0000}, [0x4E] = {0x4E2B,0x4E2B,0x9000,0x4E00},
    [0x4F] = {0x4F00,0x4F31,0x7500,0x0000}, [0x50] = {0x5000,0x5032,0x9100,0x0000},
    [0x51] = {0x5100,0x5133,0x7600,0x0000}, [0x52] = {0x5200,0x5230,0x9200,0x0000},
    [0x53] = {0x5300,0x532E,0x9300,0x0000},
    [0x57] = {0x8500,0x8700,0x8900,0x8B00}, [0x58] = {0x8600,0x8800,0x8A00,0x8C00},
};

/* The ROM's INT 9 translation step, reached from the BIOS stub through
 * INT F9h with the scancode in AL (the stub read port 60 for it). */
void dos_bios_key_irq(machine_t *m)
{
    cpu_t *c = &m->cpu;
    uint8_t sc = get_r8(c, R_AL);
    uint8_t st = mem_read8(c, BDA_SHIFT);
    uint8_t st2 = mem_read8(c, BDA_SHIFT2);
    uint8_t f3 = mem_read8(c, BDA_KBD_FLAGS3);
    const int e0 = (f3 & 0x02) != 0;

    if (sc == 0xE0) { mem_write8(c, BDA_KBD_FLAGS3, (uint8_t)(f3 | 0x02)); return; }
    if (sc == 0xE1) return;
    mem_write8(c, BDA_KBD_FLAGS3, (uint8_t)(f3 & ~0x02));

    const int brk = (sc & 0x80) != 0;
    const uint8_t code = (uint8_t)(sc & 0x7F);

    switch (code) {
    case 0x2A: case 0x36:                   /* shifts (E0 2A is a fake shift) */
        if (e0) return;
        if (brk) st &= (uint8_t)~(code == 0x2A ? 0x02 : 0x01);
        else     st |= (uint8_t)(code == 0x2A ? 0x02 : 0x01);
        mem_write8(c, BDA_SHIFT, st);
        return;
    case 0x1D:
        if (brk) st &= (uint8_t)~0x04; else st |= 0x04;
        if (!e0) { if (brk) st2 &= (uint8_t)~0x01; else st2 |= 0x01; }
        mem_write8(c, BDA_SHIFT, st); mem_write8(c, BDA_SHIFT2, st2);
        return;
    case 0x38:
        if (brk) st &= (uint8_t)~0x08; else st |= 0x08;
        if (!e0) { if (brk) st2 &= (uint8_t)~0x02; else st2 |= 0x02; }
        mem_write8(c, BDA_SHIFT, st); mem_write8(c, BDA_SHIFT2, st2);
        return;
    case 0x3A: case 0x45: case 0x46: {      /* lock keys toggle on make */
        uint8_t bit = code == 0x3A ? 0x40 : code == 0x45 ? 0x20 : 0x10;
        uint8_t held = code == 0x3A ? 0x40 : code == 0x45 ? 0x20 : 0x10;
        if (brk) { st2 &= (uint8_t)~held; }
        else if (!(st2 & held)) { st ^= bit; st2 |= held; }
        mem_write8(c, BDA_SHIFT, st); mem_write8(c, BDA_SHIFT2, st2);
        return;
    }
    case 0x52:                              /* Insert toggles too, and is a key */
        if (!brk && !(st & 0x0C)) st ^= 0x80;
        mem_write8(c, BDA_SHIFT, st);
        break;
    default: break;
    }
    if (brk) return;
    if (code >= sizeof KEYS / sizeof KEYS[0]) return;

    const keymap *k = &KEYS[code];
    uint16_t w;
    const int shift = (st & 0x03) != 0, ctrl = (st & 0x04) != 0, alt = (st & 0x08) != 0;
    if (e0) {
        /* The grey keys: the cursor block reports E0 in the low byte. */
        if (code == 0x1C) w = ctrl ? 0xE00A : 0xE00D;
        else if (code == 0x35) w = 0xE02F;
        else if (code >= 0x47 && code <= 0x53) {
            if (alt) w = (uint16_t)((code + 0x50) << 8);
            else if (ctrl) w = (uint16_t)(k->c & 0xFF00);
            else w = (uint16_t)((code << 8) | 0xE0);
        } else return;
    } else if (alt) w = k->a;
    else if (ctrl) w = k->c;
    else if (code >= 0x47 && code <= 0x53 && code != 0x4A && code != 0x4E) {
        const int num = (st & 0x20) != 0;
        w = (num != shift) ? k->s : k->n;
    } else {
        int sh = shift;
        uint8_t lo = (uint8_t)k->n;
        if ((st & 0x40) && lo >= 'a' && lo <= 'z') sh = !sh;   /* Caps Lock */
        w = sh ? k->s : k->n;
    }
    if (w) kbd_push(m, w);
}

/* ===================================================================== */
/* INT 16h                                                               */
/* ===================================================================== */

/* Words from the grey keys carry E0 in the low byte; the original 84-key
 * functions (AH=00/01) report them as 00, and drop the F11/F12 words. */
static int legacy_ok(uint16_t key) { return (key >> 8) <= 0x84; }
static uint16_t legacy(uint16_t key)
{
    return (uint8_t)key == 0xE0 && (key >> 8) ? (uint16_t)(key & 0xFF00) : key;
}

int dos_int16(machine_t *m)
{
    cpu_t *c = &m->cpu;
    uint8_t ah = (uint8_t)(c->r[R_AX] >> 8);
    uint16_t key;
    if (m->log && getenv("F117R_TRACE_KEYS") && ah != 0x01 && ah != 0x11) {
        uint16_t peek = 0;
        int have = dos_kbd_pop(m, &peek, 0);
        dos_log(m, "[int16] AH=%02X next=%04X%s @%llu (%s)\n", ah, peek, have ? "" : " (empty)",
                (unsigned long long)c->icount, dos_current_program(m));
    }
    switch (ah) {
    case 0x00:                               /* read key, waiting */
        for (;;) {
            if (!dos_kbd_pop(m, &key, 1)) return dos_bios_wait(c);
            if (legacy_ok(key)) { c->r[R_AX] = legacy(key); return 1; }
        }
    case 0x10:
        if (!dos_kbd_pop(m, &key, 1)) return dos_bios_wait(c);
        c->r[R_AX] = key;
        return 1;
    case 0x01:                               /* key available? */
        while (dos_kbd_pop(m, &key, 0) && !legacy_ok(key)) dos_kbd_pop(m, &key, 1);
        if (dos_kbd_pop(m, &key, 0)) {
            c->r[R_AX] = legacy(key);
            c->flags = (uint16_t)(c->flags & (uint16_t)(0xFFFFu ^ F_ZF));
        } else {
            c->flags |= F_ZF;
        }
        return 1;
    case 0x11:
        if (dos_kbd_pop(m, &key, 0)) {
            c->r[R_AX] = key;
            c->flags = (uint16_t)(c->flags & (uint16_t)(0xFFFFu ^ F_ZF));
        } else {
            c->flags |= F_ZF;
        }
        return 1;
    case 0x02:                               /* shift flags */
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | mem_read8(c, BDA_SHIFT));
        return 1;
    case 0x12:
        c->r[R_AX] = (uint16_t)((mem_read8(c, BDA_SHIFT2) << 8) | mem_read8(c, BDA_SHIFT));
        return 1;
    case 0x05:                               /* store a key */
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | (kbd_push(m, c->r[R_CX]) ? 0 : 1));
        return 1;
    default:
        return 1;
    }
}

