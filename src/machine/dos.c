/* dos.c - DOS and BIOS services for running the original binaries. See dos.h. */
#include "dos.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* DOS error codes we actually return. */
#define ERR_BAD_FUNCTION   0x01
#define ERR_FILE_NOT_FOUND 0x02
#define ERR_PATH_NOT_FOUND 0x03
#define ERR_TOO_MANY_OPEN  0x04
#define ERR_ACCESS_DENIED  0x05
#define ERR_BAD_HANDLE     0x06
#define ERR_NO_MEMORY      0x08
#define ERR_BAD_FORMAT     0x0B
#define ERR_NO_MORE_FILES  0x12

/* BIOS data area, segment 0x40. Given as linear addresses. */
#define BDA_EQUIPMENT   0x410
#define BDA_MEM_KB      0x413
#define BDA_KBD_HEAD    0x41A
#define BDA_KBD_TAIL    0x41C
#define BDA_KBD_BUF     0x41E   /* 16 words */
#define BDA_KBD_BUF_END 0x43E
#define BDA_VIDEO_MODE  0x449
#define BDA_VIDEO_COLS  0x44A
#define BDA_PAGE_SIZE   0x44C
#define BDA_CURSOR_POS  0x450
#define BDA_CRTC_BASE   0x463
#define BDA_TICKS       0x46C
#define BDA_KBD_START   0x480
#define BDA_KBD_END     0x482
#define BDA_ROWS_M1     0x484
#define BDA_CHAR_HEIGHT 0x485

static int g_text_dirty;

/* The DOSBox MPU-401 UART surface used by RSOUND.LOG. This is intentionally
 * outside dos_t: that structure is written raw into every machine snapshot.
 * It is reset on each fresh DOS boot; snapshot files keep their existing
 * dos_t layout and do not serialize this peripheral state. */
static struct {
    uint8_t queue[16];
    unsigned head, used;
    int uart;
} g_mpu;

static void mpu_queue(uint8_t value)
{
    if (g_mpu.used < sizeof g_mpu.queue) {
        g_mpu.queue[(g_mpu.head + g_mpu.used) % sizeof g_mpu.queue] = value;
        g_mpu.used++;
    }
}

static dos_t *self(cpu_t *c) { return (dos_t *)c->user; }

static void logf_(dos_t *d, const char *fmt, ...)
{
    if (!d->log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(d->log, fmt, ap);
    va_end(ap);
}

/* ---- flags helpers: DOS reports failure via CF ------------------------- */

static void ok(cpu_t *c) { c->flags = (uint16_t)(c->flags & (uint16_t)(F_CF ^ 0xFFFFu)); }
static void fail(cpu_t *c, uint16_t err)
{
    c->flags |= F_CF;
    c->r[R_AX] = err;
}

static uint16_t current_psp(const dos_t *d)
{
    return d->nproc ? d->procs[d->nproc - 1].psp_seg : 0;
}

const char *dos_current_program(const dos_t *d)
{
    return d->nproc ? d->procs[d->nproc - 1].name : "(none)";
}

/* ---- guest memory helpers ---------------------------------------------- */

static void guest_str(cpu_t *c, uint16_t seg, uint16_t off, char *out, size_t n)
{
    size_t i = 0;
    while (i + 1 < n) {
        uint8_t ch = mem_read8(c, phys(seg, (uint16_t)(off + i)));
        if (!ch) break;
        out[i++] = (char)ch;
    }
    out[i] = 0;
}

static void guest_write(cpu_t *c, uint32_t lin, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) mem_write8(c, lin + (uint32_t)i, p[i]);
}

/* Standard DOS output goes through the console device. Keep the text page in
 * video memory as well as logging the bytes: SETUP writes its visible layout
 * with INT 21h, and a final screen dump that only looks at B800 would
 * otherwise report a blank page. */
static int text_mode(const dos_t *d)
{
    return d->vga_mode <= 3 || d->vga_mode == 7;
}

static uint32_t text_vram(const dos_t *d)
{
    return d->vga_mode == 7 ? 0xB0000u : 0xB8000u;
}

static unsigned text_columns(cpu_t *c)
{
    const unsigned cols = mem_read16(c, BDA_VIDEO_COLS);
    return cols == 40 ? 40u : 80u;
}

static void text_clear(cpu_t *c, dos_t *d)
{
    if (!text_mode(d)) return;
    const unsigned cols = text_columns(c);
    const uint32_t base = text_vram(d);
    for (unsigned i = 0; i < cols * 25u; i++) {
        mem_write8(c, base + i * 2u, ' ');
        mem_write8(c, base + i * 2u + 1u, 0x07);
    }
    mem_write16(c, BDA_CURSOR_POS, 0);
    g_text_dirty = 1;
}

static void text_scroll(cpu_t *c, dos_t *d, unsigned cols)
{
    const uint32_t base = text_vram(d);
    for (unsigned i = 0; i < (25u - 1u) * cols * 2u; i++)
        mem_write8(c, base + i,
                   mem_read8(c, base + i + cols * 2u));
    for (unsigned i = (25u - 1u) * cols; i < 25u * cols; i++) {
        mem_write8(c, base + i * 2u, ' ');
        mem_write8(c, base + i * 2u + 1u, 0x07);
    }
}

static void text_cursor_store(cpu_t *c, unsigned row, unsigned col)
{
    mem_write16(c, BDA_CURSOR_POS,
                (uint16_t)(((row & 0xFFu) << 8) | (col & 0xFFu)));
}

static void text_output_char(cpu_t *c, dos_t *d, uint8_t ch)
{
    if (!text_mode(d)) return;
    const unsigned cols = text_columns(c);
    const uint32_t base = text_vram(d);
    const uint16_t pos = mem_read16(c, BDA_CURSOR_POS);
    unsigned row = pos >> 8;
    unsigned col = pos & 0xFFu;
    if (row >= 25u) row = 24u;
    if (col >= cols) col = cols - 1u;

    if (ch == '\r') {
        col = 0;
        g_text_dirty = 1;
    } else if (ch == '\n') {
        row++;
        g_text_dirty = 1;
    } else if (ch == '\b') {
        if (col) col--;
        g_text_dirty = 1;
    } else if (ch == '\t') {
        const unsigned stop = (col + 8u) & ~7u;
        for (unsigned i = col; i < stop; i++) text_output_char(c, d, ' ');
        return;
    } else if (ch >= 0x20u) {
        const uint32_t cell = base + (row * cols + col) * 2u;
        mem_write8(c, cell, ch);
        mem_write8(c, cell + 1u, 0x07);
        g_text_dirty = 1;
        col++;
        if (col >= cols) {
            col = 0;
            row++;
        }
    }

    if (row >= 25u) {
        text_scroll(c, d, cols);
        row = 24u;
    }
    text_cursor_store(c, row, col);
}

static void trace_text_screen(cpu_t *c, dos_t *d, unsigned ah)
{
    if (!(d->trace_dos & DOS_TRACE_TEXT) || !text_mode(d) || !g_text_dirty)
        return;
    const unsigned cols = text_columns(c);
    const uint32_t base = text_vram(d);
    if (ah == 0x201u)
        logf_(d, "[text] before IN 0201h from %s at %04X:%04X\n",
              dos_current_program(d), c->op_cs, c->op_ip);
    else if (ah == 0x100u)
        logf_(d, "[text] at DOS exit from %s at %04X:%04X\n",
              dos_current_program(d), c->op_cs, c->op_ip);
    else
        logf_(d, "[text] before INT 21h/AH=%02X from %s at %04X:%04X\n",
              ah, dos_current_program(d), c->op_cs, c->op_ip);
    for (unsigned row = 0; row < 25u; row++) {
        logf_(d, "[text] %02u:", row);
        for (unsigned col = 0; col < cols; col++) {
            const uint32_t cell = base + (row * cols + col) * 2u;
            logf_(d, " %02X/%02X", mem_read8(c, cell),
                  mem_read8(c, cell + 1u));
        }
        logf_(d, "\n");
    }
    g_text_dirty = 0;
}

/* ---- path translation -------------------------------------------------- */

/* Map a DOS path onto a host path inside the data directory. Everything is
 * flattened to the basename and resolved inside data_dir, which is both what
 * the original saw and what keeps the guest from reaching outside the
 * directory the user pointed us at. */
static void host_path(dos_t *d, const char *dos_path, char *out, size_t n)
{
    const char *base = dos_path;
    for (const char *p = dos_path; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            base = p + 1;
    snprintf(out, n, "%s/%s", d->data_dir, base);
}

/* Case-insensitive open: the original names are upper case and the host
 * filesystem may not be. Tries as given, then upper, then lower. */
static FILE *open_ci(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
    if (f) return f;

    char buf[600];
    const char *slash = strrchr(path, '/');
    size_t dirlen = slash ? (size_t)(slash - path + 1) : 0;
    const char *name = slash ? slash + 1 : path;
    for (int pass = 0; pass < 2; pass++) {
        if (dirlen >= sizeof(buf)) return NULL;
        memcpy(buf, path, dirlen);
        size_t i = 0;
        for (; name[i] && dirlen + i + 1 < sizeof(buf); i++)
            buf[dirlen + i] = (char)(pass ? tolower((unsigned char)name[i])
                                          : toupper((unsigned char)name[i]));
        buf[dirlen + i] = 0;
        f = fopen(buf, mode);
        if (f) return f;
    }
    return NULL;
}

/* See dos.h. Read-write first, because a file the guest created is one it
 * may still write to; read-only media falls back to read-only, which is
 * what the guest would have had anyway. The same case-insensitive open as
 * the original, so a path recorded on one filesystem reopens on another. */
/* A restored dos_t carries the data directory of the run that SAVED it.
 * Until 27 September 2026 that silently won over the restoring run's
 * --data: a route given a private copy of the install, so START and END
 * could write Roster.Fil without touching the shared extract, wrote the
 * shared extract anyway. The restoring run's directory is the one it
 * asked for; open handles under the old directory follow it. */
void dos_rebase_data_dir(dos_t *d, const char *data_dir)
{
    char old[sizeof d->data_dir];
    snprintf(old, sizeof old, "%s", d->data_dir);
    snprintf(d->data_dir, sizeof d->data_dir, "%s", data_dir);
    const size_t n = strlen(old);
    for (int i = 0; i < DOS_MAX_FILES; i++) {
        dos_file *f = &d->files[i];
        if (!f->in_use || f->is_device || !n || strncmp(f->path, old, n) != 0)
            continue;
        char rest[sizeof f->path];
        snprintf(rest, sizeof rest, "%s", f->path + n);
        snprintf(f->path, sizeof f->path, "%s%s", d->data_dir, rest);
    }
}

int dos_reopen_file(dos_t *d, int h, long pos)
{
    if (h < 0 || h >= DOS_MAX_FILES) return 0;
    dos_file *f = &d->files[h];
    if (!f->in_use || f->is_device) { f->fp = NULL; return 1; }
    FILE *nf = open_ci(f->path, "rb+");
    if (!nf) nf = open_ci(f->path, "rb");
    if (!nf) return 0;
    if (pos >= 0 && fseek(nf, pos, SEEK_SET) != 0) { fclose(nf); return 0; }
    f->fp = nf;
    return 1;
}

static uint8_t *read_whole(const char *host, long *out_size)
{
    FILE *f = open_ci(host, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *raw = (uint8_t *)malloc((size_t)sz);
    if (!raw || fread(raw, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f); free(raw); return NULL;
    }
    fclose(f);
    *out_size = sz;
    return raw;
}

/* ---- file handles ------------------------------------------------------ */

static int alloc_handle(dos_t *d)
{
    for (int i = 5; i < DOS_MAX_FILES; i++)
        if (!d->files[i].in_use) return i;
    return -1;
}

static void close_files_of(dos_t *d, uint16_t owner)
{
    for (int i = 5; i < DOS_MAX_FILES; i++)
        if (d->files[i].in_use && d->files[i].owner == owner) {
            if (d->files[i].fp) fclose(d->files[i].fp);
            d->files[i].in_use = 0;
            d->files[i].fp = NULL;
        }
}

/* ---- memory allocation -------------------------------------------------
 * A bump allocator over the arena, with ownership. That is sufficient
 * because of how the shell uses memory: it stays resident at the bottom,
 * loads overlays above itself and shrinks them to fit, then EXECs one phase
 * program at a time into everything that is left. Each child is therefore
 * always the topmost allocation, and freeing it on exit reclaims the space
 * exactly as DOS would. */

static uint16_t arena_top(const dos_t *d)
{
    uint16_t top = d->arena_base_seg;
    for (int i = 0; i < DOS_MAX_BLOCKS; i++)
        if (d->blocks[i].in_use) {
            uint16_t end = (uint16_t)(d->blocks[i].seg + d->blocks[i].paras);
            if (end > top) top = end;
        }
    return top;
}

/* Largest allocation currently possible, in paragraphs. */
static uint16_t arena_avail(const dos_t *d)
{
    uint16_t top = arena_top(d);
    /* One paragraph is reserved where the MCB would sit. */
    return (uint16_t)((d->arena_end_seg > top + 1) ? d->arena_end_seg - top - 1 : 0);
}

static int dos_alloc(dos_t *d, uint16_t paras, uint16_t owner, uint16_t *out_seg)
{
    if (paras > arena_avail(d)) return 0;
    /* Compute the top BEFORE claiming a slot: a recycled slot still carries
     * the extent of its previous occupant, and marking it in use first made
     * arena_top() count that stale extent. That phantom cost START.EXE a
     * third of its memory and produced "Insufficient memory for MCGA
     * graphics" where real DOS had room to spare. */
    uint16_t top = arena_top(d);
    for (int i = 0; i < DOS_MAX_BLOCKS; i++) {
        if (!d->blocks[i].in_use) {
            d->blocks[i].seg = (uint16_t)(top + 1);
            d->blocks[i].paras = paras;
            d->blocks[i].owner = owner;
            d->blocks[i].in_use = 1;
            *out_seg = d->blocks[i].seg;
            return 1;
        }
    }
    return 0;
}

static dos_block *find_block(dos_t *d, uint16_t seg)
{
    for (int i = 0; i < DOS_MAX_BLOCKS; i++)
        if (d->blocks[i].in_use && d->blocks[i].seg == seg) return &d->blocks[i];
    return NULL;
}

static void free_blocks_of(dos_t *d, uint16_t owner)
{
    for (int i = 0; i < DOS_MAX_BLOCKS; i++)
        if (d->blocks[i].in_use && d->blocks[i].owner == owner)
            d->blocks[i].in_use = 0;
}

/* The most a given block could grow to without colliding with the block
 * above it or the end of the arena. */
static uint16_t block_max_paras(const dos_t *d, const dos_block *b)
{
    uint16_t limit = d->arena_end_seg;
    for (int i = 0; i < DOS_MAX_BLOCKS; i++) {
        const dos_block *o = &d->blocks[i];
        if (o->in_use && o != b && o->seg > b->seg && (uint16_t)(o->seg - 1) < limit)
            limit = (uint16_t)(o->seg - 1);     /* leave its MCB paragraph */
    }
    return (uint16_t)(limit - b->seg);
}

/* ---- BIOS keyboard ring buffer ----------------------------------------- */

static uint16_t kbd_rd16(cpu_t *c, uint32_t a) { return mem_read16(c, a); }

int dos_push_key(dos_t *d, uint8_t ascii, uint8_t scancode)
{
    cpu_t *c = d->cpu;
    uint16_t head = kbd_rd16(c, BDA_KBD_HEAD);
    uint16_t tail = kbd_rd16(c, BDA_KBD_TAIL);
    uint16_t next = (uint16_t)(tail + 2);
    if (next >= (BDA_KBD_BUF_END - 0x400)) next = BDA_KBD_BUF - 0x400;
    if (next == head) return 0;                     /* buffer full */
    mem_write16(c, 0x400u + tail, (uint16_t)(((uint16_t)scancode << 8) | ascii));
    mem_write16(c, BDA_KBD_TAIL, next);
    return 1;
}

static int kbd_pop(dos_t *d, uint16_t *key, int remove)
{
    cpu_t *c = d->cpu;
    uint16_t head = kbd_rd16(c, BDA_KBD_HEAD);
    uint16_t tail = kbd_rd16(c, BDA_KBD_TAIL);
    if (head == tail) return 0;
    *key = mem_read16(c, 0x400u + head);
    if (remove) {
        uint16_t next = (uint16_t)(head + 2);
        if (next >= (BDA_KBD_BUF_END - 0x400)) next = BDA_KBD_BUF - 0x400;
        mem_write16(c, BDA_KBD_HEAD, next);
    }
    return 1;
}

/* Set-1 scancodes for the characters a scripted run is likely to type. */
static uint8_t scancode_for(uint8_t ch)
{
    static const char row1[] = "1234567890-=";
    static const char row2[] = "qwertyuiop[]";
    static const char row3[] = "asdfghjkl;'`";
    static const char row4[] = "\\zxcvbnm,./";
    const char *p;
    ch = (uint8_t)tolower(ch);
    if ((p = strchr(row1, ch)) && ch) return (uint8_t)(0x02 + (p - row1));
    if ((p = strchr(row2, ch)) && ch) return (uint8_t)(0x10 + (p - row2));
    if ((p = strchr(row3, ch)) && ch) return (uint8_t)(0x1E + (p - row3));
    if ((p = strchr(row4, ch)) && ch) return (uint8_t)(0x2B + (p - row4));
    /* Shifted symbols map to the key that carries them. */
    {
        static const char shift1[] = "!@#$%^&*()_+";
        static const char shift2[] = "{}";
        static const char shift3[] = ":\"~";
        static const char shift4[] = "|<>?";
        if ((p = strchr(shift1, ch)) && ch) return (uint8_t)(0x02 + (p - shift1));
        if ((p = strchr(shift2, ch)) && ch) return (uint8_t)(0x1A + (p - shift2));
        if ((p = strchr(shift3, ch)) && ch) return (uint8_t)(0x27 + (p - shift3));
        if (ch == '|') return 0x2B;
        if ((p = strchr(shift4 + 1, ch)) && ch) return (uint8_t)(0x33 + (p - (shift4 + 1)));
    }
    switch (ch) {
    case '\r': return 0x1C;
    case 0x1B: return 0x01;
    case '\t': return 0x0F;
    case ' ':  return 0x39;
    case '\b': return 0x0E;
    default:   return 0;
    }
}

/* Does typing `ch` need the shift key held? */
static int shifted_char(uint8_t ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch && strchr("!@#$%^&*()_+{}:\"~|<>?", ch) != NULL);
}

static void kbd_enqueue_m(dos_t *d, uint8_t code, uint8_t ascii, uint64_t due,
                          uint8_t shift_set, uint8_t shift_clear)
{
    if (d->kbd_qn >= (int)(sizeof(d->kbd_q) / sizeof(d->kbd_q[0]))) return;
    int slot = (d->kbd_qh + d->kbd_qn) % (int)(sizeof(d->kbd_q) / sizeof(d->kbd_q[0]));
    d->kbd_q[slot].code = code;
    d->kbd_q[slot].ascii = ascii;
    d->kbd_q[slot].due_tick = due;
    d->kbd_q[slot].shift_set = shift_set;
    d->kbd_q[slot].shift_clear = shift_clear;
    d->kbd_qn++;
}

static void kbd_enqueue(dos_t *d, uint8_t code, uint8_t ascii, uint64_t due)
{
    kbd_enqueue_m(d, code, ascii, due, 0, 0);
}

void dos_kbd_type(dos_t *d, const char *keys, int hold_ticks)
{
    if (hold_ticks < 1) hold_ticks = 1;
    uint64_t t = d->kbd_cursor > d->tick_irq_count ? d->kbd_cursor : d->tick_irq_count;
    for (const char *s = keys; *s; s++) {
        uint8_t ch = (uint8_t)*s, ascii, scan;
        int extended = 0, alt = 0, fshift = 0, numpad = 0;
        if (ch == '\\' && s[1]) {
            s++;
            switch (*s) {
            case 'r': ch = '\r'; break;
            case 'b': ch = 0x08; break; /* BIOS Backspace: ASCII 8 */
            case 'e': ch = 0x1B; break;
            case 't': ch = '\t'; break;
            case '\\': ch = '\\'; break;
            case 'U': ch = 0x48; extended = 1; break;
            case 'D': ch = 0x50; extended = 1; break;
            case 'L': ch = 0x4B; extended = 1; break;
            case 'R': ch = 0x4D; extended = 1; break;
            case 'X': ch = 0x53; extended = 1; break;
            case 'H': ch = 0x47; extended = 1; break;
            case 'I': ch = 0x52; extended = 1; break;   /* Ins  */
            case 'E': ch = 0x4F; extended = 1; break;   /* End  */
            case 'P': ch = 0x49; extended = 1; break;   /* PgUp */
            case 'N': ch = 0x51; extended = 1; break;   /* PgDn */
            /* Function keys, \1 to \9 and \0 for F10. Scancodes 3B..44,
             * with no ASCII - the same shape as the cursor keys above, and
             * the flight engine's own INT 9 reads the scancode anyway. F2
             * cycles the right-hand display at VGAME 0x0CD7E, which is the
             * only way into display mode 2 and therefore the only way to
             * reach the flag at [0xE588]. */
            case '1': case '2': case '3': case '4': case '5':
            case '6': case '7': case '8': case '9':
                ch = (uint8_t)(0x3B + (*s - '1')); extended = 1; break;
            case '0': ch = 0x44; extended = 1; break;
            /* \sN - Shift held over function key N (\s1..\s9, \s0 for
             * F10), the view keys Shift-F1..F4 among them. Like \a, the
             * modifier is the BIOS shift byte's bit 1, not an injected 0x2A
             * (inside VGAME's keypad range); bios_key_irq turns the F-key
             * into 0x54.. the way a real BIOS does, which is what VGAME
             * reads through INT 16h. */
            case 's':
                if (s[1] >= '0' && s[1] <= '9') {
                    s++;
                    ch = (uint8_t)(*s == '0' ? 0x44 : 0x3B + (*s - '1'));
                    extended = 1;
                    fshift = 1;
                }
                break;
            /* \aX - Alt held over the key X, which BIOS reports as that
             * key's scancode with an ASCII of ZERO. Without this there is
             * no way to type the Alt bindings at all, and VGAME has a
             * whole chain of them (0x0C907: Alt-R/I/J/K/L/N, the moving
             * map's pan and zoom). The shift state at 0040:0017 is already
             * maintained for scancode 0x38 by bios_key_irq below, so the
             * only thing missing was a spelling. */
            case 'a':
                if (s[1]) {
                    alt = 1;
                    s++;
                    ch = (uint8_t)*s;
                }
                break;
            /* \kN - keypad key N (0-9 or .) with NumLock on: its own
             * scancode, the digit as the ASCII, and the shift byte's
             * NumLock bit set over the key. VGAME's INT 9 then leaves the
             * stick alone (NumLock and Shift disagree) and the BIOS queues
             * the digit word, 0x4838 for keypad 8. */
            case 'k':
                if (s[1] && strchr("0123456789.", s[1])) {
                    static const char pad[] = "0123456789.";
                    static const uint8_t code[11] = { 0x52, 0x4F, 0x50, 0x51,
                        0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x53 };
                    s++;
                    ch = (uint8_t)*s;
                    numpad = code[strchr(pad, *s) - pad];
                }
                break;
            default:  ch = (uint8_t)*s; break;
            }
        }
        if (numpad) { scan = (uint8_t)numpad; ascii = ch; }
        else if (alt) { scan = scancode_for(ch); ascii = 0; }
        else if (extended) { scan = ch; ascii = 0; }
        else { scan = scancode_for(ch); ascii = ch; }
        if (!scan) continue;
        int shift = !extended && !alt && !numpad && shifted_char(ch);
        if (shift) kbd_enqueue(d, 0x2A, 0, t);
        /* Alt is NOT injected as scancode 0x38. A guest that installs its
         * own INT 9 - the flight engine does - reads port 60h itself and
         * takes its modifiers from the BIOS shift byte at 0040:0017, so an
         * injected 0x38 is not seen as a modifier at all. Worse, 0x38 sits
         * inside the 0x29..0x51 range VGAME's keypad table translates
         * (0x113E8), so injecting it deflects the virtual joystick and the
         * Alt key never reaches its own handler. Setting the byte's bit 3
         * as the make code is delivered, and clearing it after the break,
         * is what the real keyboard does from the guest's point of view.
         *
         * This replaces the first attempt, which did inject 0x38 and which
         * put 0x26 into [0x2CD6] instead of running any handler. */
        const uint8_t mods = (uint8_t)((alt ? 0x08 : 0) | (fshift ? 0x02 : 0)
                                       | (numpad ? 0x20 : 0));
        kbd_enqueue_m(d, scan, ascii, t, mods, 0);
        kbd_enqueue_m(d, (uint8_t)(scan | 0x80), 0, t + (uint64_t)hold_ticks,
                      0, mods);
        if (shift) kbd_enqueue(d, 0xAA, 0, t + (uint64_t)hold_ticks);
        t += (uint64_t)hold_ticks + 1;
    }
    d->kbd_cursor = t;
}

/* The BIOS INT 9 handler's job, reached through the INT F9h stub: keep the
 * shift state at 0040:0017 and put translated make codes into the ring
 * buffer. Break codes and the shift keys themselves produce no key. */
static void bios_key_irq(cpu_t *c, dos_t *d)
{
    uint8_t sc = d->port60;
    uint8_t st = mem_read8(c, 0x417);
    switch (sc) {
    case 0x2A: st |= 0x02; break;          /* left shift down */
    case 0x36: st |= 0x01; break;          /* right shift down */
    case 0xAA: st &= (uint8_t)~0x02; break;
    case 0xB6: st &= (uint8_t)~0x01; break;
    case 0x1D: st |= 0x04; break;          /* ctrl */
    case 0x9D: st &= (uint8_t)~0x04; break;
    case 0x38: st |= 0x08; break;          /* alt */
    case 0xB8: st &= (uint8_t)~0x08; break;
    default:
        if (!(sc & 0x80) && sc != 0xE0 && sc != 0xE1) {
            uint8_t code = sc;
            /* A real BIOS reports F1..F10 under a modifier as their own
             * codes: Alt 0x68.., Ctrl 0x5E.., Shift 0x54... VGAME reads
             * keys through INT 16h, so Shift-F1 (its view key 0x5400)
             * does not exist without this. */
            if (sc >= 0x3B && sc <= 0x44) {
                if (st & 0x08) code = (uint8_t)(sc + 0x2D);
                else if (st & 0x04) code = (uint8_t)(sc + 0x23);
                else if (st & 0x03) code = (uint8_t)(sc + 0x19);
            }
            dos_push_key(d, d->port60_ascii, code);
        }
        break;
    }
    mem_write8(c, 0x417, st);
}

int dos_poll_pending(const dos_t *d)
{
    const cpu_t *c = d->cpu;
    if (!(c->flags & F_IF)) return 0;
    if (d->irq0_pending && !d->irq0_in_service) return 1;         /* dos_timer_poll */
    return d->kbd_qn && !d->irq1_in_service && !d->irq0_in_service
        && d->kbd_q[d->kbd_qh].due_tick <= d->tick_irq_count;   /* dos_kbd_poll */
}

void dos_kbd_poll(dos_t *d)
{
    cpu_t *c = d->cpu;
    if (!d->kbd_qn || d->irq1_in_service || d->irq0_in_service || !(c->flags & F_IF)) return;
    if (d->kbd_q[d->kbd_qh].due_tick > d->tick_irq_count) return;
    uint8_t code = d->kbd_q[d->kbd_qh].code, ascii = d->kbd_q[d->kbd_qh].ascii;
    /* The BIOS shift byte moves with the key, before the guest's INT 9 sees
     * the scancode - see dos_kbd_type on why Alt cannot be injected as a
     * scancode. Set on the make, cleared on the break. */
    uint8_t sh_set = d->kbd_q[d->kbd_qh].shift_set;
    uint8_t sh_clr = d->kbd_q[d->kbd_qh].shift_clear;
    if (sh_set || sh_clr) {
        uint8_t st = mem_read8(c, 0x417);
        st = (uint8_t)((st | sh_set) & (uint8_t)~sh_clr);
        mem_write8(c, 0x417, st);
    }
    d->kbd_qh = (d->kbd_qh + 1) % (int)(sizeof(d->kbd_q) / sizeof(d->kbd_q[0]));
    d->kbd_qn--;
    uint16_t off = mem_read16(c, 9 * 4), seg = mem_read16(c, 9 * 4 + 2);
    d->port60 = code;
    d->port60_ascii = ascii;
    d->irq1_in_service = 1;
    d->kbd_delivered++;
    cpu_push16(c, c->flags);
    cpu_push16(c, c->seg[S_CS]);
    cpu_push16(c, c->ip);
    c->flags = (uint16_t)(c->flags & (uint16_t)((F_IF | F_TF) ^ 0xFFFFu));
    c->seg[S_CS] = seg;
    c->ip = off;
}

void dos_push_keys(dos_t *d, const char *s)
{
    for (; *s; s++) {
        uint8_t ch = (uint8_t)*s;
        if (ch == '\\' && s[1]) {
            s++;
            switch (*s) {
            case 'r': ch = '\r'; break;
            case 'n': ch = '\r'; break;
            case 'e': ch = 0x1B; break;
            case 't': ch = '\t'; break;
            case '\\': ch = '\\'; break;
            /* Extended keys: ASCII 0 with the set-1 scancode, as the BIOS
             * stores them. The front end moves its pointer with the arrows. */
            case 'U': dos_push_key(d, 0, 0x48); continue;   /* up    */
            case 'D': dos_push_key(d, 0, 0x50); continue;   /* down  */
            case 'L': dos_push_key(d, 0, 0x4B); continue;   /* left  */
            case 'R': dos_push_key(d, 0, 0x4D); continue;   /* right */
            case 'X': dos_push_key(d, 0, 0x53); continue;   /* delete */
            case 'H': dos_push_key(d, 0, 0x47); continue;   /* home  */
            case 'I': dos_push_key(d, 0, 0x52); continue;   /* insert */
            case 'E': dos_push_key(d, 0, 0x4F); continue;   /* end   */
            case 'P': dos_push_key(d, 0, 0x49); continue;   /* pgup  */
            case 'N': dos_push_key(d, 0, 0x51); continue;   /* pgdn  */
            /* Function keys, the same spelling dos_kbd_type uses, so the
             * two paths cannot mean different things by \2. */
            case '1': case '2': case '3': case '4': case '5':
            case '6': case '7': case '8': case '9':
                dos_push_key(d, 0, (uint8_t)(0x3B + (*s - '1'))); continue;
            case '0': dos_push_key(d, 0, 0x44); continue;   /* F10 */
            default:  ch = (uint8_t)*s; break;
            }
        }
        dos_push_key(d, ch, scancode_for(ch));
    }
}

/* ---- program loading --------------------------------------------------- */

/* Everything the loader needs to know about how to start a program. */
typedef struct {
    uint16_t env_seg;
    uint16_t cmd_seg, cmd_off;      /* far pointer to a DOS command tail, or 0:0 */
    uint16_t fcb1_seg, fcb1_off;
    uint16_t fcb2_seg, fcb2_off;
    uint16_t parent_psp;
} exec_params;

static void build_psp(dos_t *d, uint16_t psp, uint16_t top_seg,
                      const exec_params *ep, uint16_t ret_cs, uint16_t ret_ip)
{
    cpu_t *c = d->cpu;
    for (int i = 0; i < 0x100; i++) mem_write8(c, phys(psp, (uint16_t)i), 0);
    mem_write8(c, phys(psp, 0x00), 0xCD);          /* INT 20h */
    mem_write8(c, phys(psp, 0x01), 0x20);
    mem_write16(c, phys(psp, 0x02), top_seg);       /* first segment past the block */
    /* Terminate address: where control goes when the program ends. */
    mem_write16(c, phys(psp, 0x0A), ret_ip);
    mem_write16(c, phys(psp, 0x0C), ret_cs);
    mem_write16(c, phys(psp, 0x16), ep->parent_psp);
    mem_write16(c, phys(psp, 0x2C), ep->env_seg);
    mem_write8(c, phys(psp, 0x50), 0xCD);          /* INT 21h ; RETF */
    mem_write8(c, phys(psp, 0x51), 0x21);
    mem_write8(c, phys(psp, 0x52), 0xCB);

    /* Default FCBs, then whatever the caller supplied. */
    if (ep->fcb1_seg || ep->fcb1_off)
        for (int i = 0; i < 16; i++)
            mem_write8(c, phys(psp, (uint16_t)(0x5C + i)),
                       mem_read8(c, phys(ep->fcb1_seg, (uint16_t)(ep->fcb1_off + i))));
    if (ep->fcb2_seg || ep->fcb2_off)
        for (int i = 0; i < 16; i++)
            mem_write8(c, phys(psp, (uint16_t)(0x6C + i)),
                       mem_read8(c, phys(ep->fcb2_seg, (uint16_t)(ep->fcb2_off + i))));

    /* Command tail: length byte, text, CR. */
    if (ep->cmd_seg || ep->cmd_off) {
        uint8_t len = mem_read8(c, phys(ep->cmd_seg, ep->cmd_off));
        if (len > 126) len = 126;
        mem_write8(c, phys(psp, 0x80), len);
        for (int i = 0; i <= len; i++)
            mem_write8(c, phys(psp, (uint16_t)(0x81 + i)),
                       mem_read8(c, phys(ep->cmd_seg, (uint16_t)(ep->cmd_off + 1 + i))));
        mem_write8(c, phys(psp, (uint16_t)(0x81 + len)), 0x0D);
    } else {
        mem_write8(c, phys(psp, 0x80), 0);
        mem_write8(c, phys(psp, 0x81), 0x0D);
    }
}

/* Load a program from `host` into a fresh block and set the CPU up to run
 * it. Returns a DOS error code, or 0 on success with *psp_out filled in.
 * The CPU is not touched until success is certain. */
static uint16_t load_program(dos_t *d, const char *host, const exec_params *ep,
                             uint16_t ret_cs, uint16_t ret_ip, uint16_t *psp_out,
                             const char *name_for_log)
{
    cpu_t *c = d->cpu;
    long fsz = 0;
    uint8_t *raw = read_whole(host, &fsz);
    if (!raw) return ERR_FILE_NOT_FOUND;

    int is_mz = (fsz >= 28 && raw[0] == 'M' && raw[1] == 'Z');
    uint16_t hdr[14] = {0};
    uint32_t hdr_size = 0, img_size = (uint32_t)fsz, body = (uint32_t)fsz;
    uint16_t minalloc = 0, maxalloc = 0xFFFF;
    if (is_mz) {
        memcpy(hdr, raw, sizeof(hdr));
        uint16_t last_page = hdr[1], pages = hdr[2];
        hdr_size = (uint32_t)hdr[4] * 16;
        minalloc = hdr[5];
        maxalloc = hdr[6];
        img_size = pages ? (uint32_t)(pages - 1) * 512 + (last_page ? last_page : 512) : 0;
        if (img_size > (uint32_t)fsz) img_size = (uint32_t)fsz;
        if (img_size < hdr_size) { free(raw); return ERR_BAD_FORMAT; }
        body = img_size - hdr_size;
    }

    /* Memory: DOS gives a program the largest free block unless maxalloc
     * asks for less. Every phase program here asks for everything. */
    uint16_t body_paras = (uint16_t)((body + 15) / 16);
    uint16_t need = (uint16_t)(0x10 + body_paras + (is_mz ? minalloc : 0x10));
    uint16_t avail = arena_avail(d);
    if (need > avail) { free(raw); return ERR_NO_MEMORY; }
    uint16_t want = avail;
    if (is_mz && maxalloc != 0xFFFF) {
        uint32_t cap = 0x10u + body_paras + maxalloc;
        if (cap < want) want = (uint16_t)cap;
        if (want < need) want = need;
    }

    uint16_t psp = 0;
    uint16_t owner_before = current_psp(d);   /* the block's owner is itself */
    (void)owner_before;
    if (!dos_alloc(d, want, 0 /* patched below */, &psp)) { free(raw); return ERR_NO_MEMORY; }
    find_block(d, psp)->owner = psp;
    uint16_t top_seg = (uint16_t)(psp + want);
    uint16_t load_seg = (uint16_t)(psp + 0x10);

    build_psp(d, psp, top_seg, ep, ret_cs, ret_ip);

    if (is_mz) {
        guest_write(c, phys(load_seg, 0), raw + hdr_size, body);
        uint16_t nreloc = hdr[3], reloc_off = hdr[12];
        for (unsigned i = 0; i < nreloc; i++) {
            uint16_t ro, rs;
            memcpy(&ro, raw + reloc_off + i * 4, 2);
            memcpy(&rs, raw + reloc_off + i * 4 + 2, 2);
            uint16_t s = (uint16_t)(load_seg + rs);
            seg_write16(c, s, ro, (uint16_t)(seg_read16(c, s, ro) + load_seg));
        }
        c->seg[S_SS] = (uint16_t)(load_seg + hdr[7]);
        c->r[R_SP]   = hdr[8];
        c->seg[S_CS] = (uint16_t)(load_seg + hdr[11]);
        c->ip        = hdr[10];
    } else {
        /* COM: image at PSP:0100, all segments equal, stack at the top of
         * the block with a zero word pushed for the RET-to-PSP convention. */
        guest_write(c, phys(psp, 0x100), raw, body);
        c->seg[S_SS] = psp;
        c->r[R_SP]   = 0xFFFE;
        seg_write16(c, psp, 0xFFFE, 0);
        c->seg[S_CS] = psp;
        c->ip        = 0x100;
    }
    free(raw);

    c->seg[S_DS] = c->seg[S_ES] = psp;
    c->r[R_AX] = 0;                 /* FCB drive-validity flags */
    c->r[R_BX] = c->r[R_CX] = c->r[R_DX] = 0;
    c->r[R_SI] = c->r[R_DI] = c->r[R_BP] = 0;
    c->flags = cpu_flags_fixed(c) | F_IF;

    logf_(d, "[exec] %-14s %s  psp=%04X load=%04X..%04X (%u paras)  entry %04X:%04X\n",
          name_for_log, is_mz ? "MZ " : "COM", psp, load_seg, top_seg, want,
          c->seg[S_CS], c->ip);
    *psp_out = psp;
    return 0;
}

/* Load an MZ image as an overlay at a caller-chosen segment: no PSP, no
 * memory allocation, no transfer of control. This is INT 21h/4B03, and it
 * is how the shell brings in MISC.EXE and the graphics driver. */
static uint16_t load_overlay(dos_t *d, const char *host, uint16_t load_seg,
                             uint16_t reloc_factor, const char *name_for_log)
{
    cpu_t *c = d->cpu;
    long fsz = 0;
    uint8_t *raw = read_whole(host, &fsz);
    if (!raw) return ERR_FILE_NOT_FOUND;

    uint32_t body;
    unsigned nreloc = 0;
    if (fsz >= 28 && raw[0] == 'M' && raw[1] == 'Z') {
        uint16_t hdr[14];
        memcpy(hdr, raw, sizeof(hdr));
        uint32_t hdr_size = (uint32_t)hdr[4] * 16;
        uint32_t img_size = hdr[2] ? (uint32_t)(hdr[2] - 1) * 512 + (hdr[1] ? hdr[1] : 512) : 0;
        if (img_size > (uint32_t)fsz) img_size = (uint32_t)fsz;
        if (img_size < hdr_size) { free(raw); return ERR_BAD_FORMAT; }
        body = img_size - hdr_size;
        guest_write(c, phys(load_seg, 0), raw + hdr_size, body);
        nreloc = hdr[3];
        for (unsigned i = 0; i < nreloc; i++) {
            uint16_t ro, rs;
            memcpy(&ro, raw + hdr[12] + i * 4, 2);
            memcpy(&rs, raw + hdr[12] + i * 4 + 2, 2);
            uint16_t s = (uint16_t)(load_seg + rs);
            seg_write16(c, s, ro, (uint16_t)(seg_read16(c, s, ro) + reloc_factor));
        }
    } else {
        body = (uint32_t)fsz;
        guest_write(c, phys(load_seg, 0), raw, body);
    }
    free(raw);
    logf_(d, "[overlay] %-12s -> %04X (%u bytes, %u relocations, factor %04X)\n",
          name_for_log, load_seg, body, nreloc, reloc_factor);
    return 0;
}

/* ---- process termination ----------------------------------------------- */

static int terminate(cpu_t *c, dos_t *d, uint8_t code)
{
    uint16_t psp = current_psp(d);
    close_files_of(d, psp);
    free_blocks_of(d, psp);

    if (d->nproc <= 1) {
        logf_(d, "[exit] %s terminated with code %d (root)\n",
              dos_current_program(d), code);
        d->exited = 1;
        d->exit_code = code;
        c->stop_reason = STOP_EXIT;
        return 1;
    }

    dos_proc *p = &d->procs[d->nproc - 1];
    logf_(d, "[exit] %s terminated with code %d -> back to %s\n",
          p->name, code, d->procs[d->nproc - 2].name);
    d->last_child_exit = code;

    /* Resume the parent exactly where its INT 21h left off. DOS itself
     * guarantees only CS:IP; restoring everything is harmless and kinder. */
    memcpy(c->r, p->r, sizeof(c->r));
    memcpy(c->seg, p->seg, sizeof(c->seg));
    c->seg[S_CS] = p->ret_cs;
    c->ip = p->ret_ip;
    c->flags = (uint16_t)((p->flags | F_IF) & (uint16_t)(F_CF ^ 0xFFFFu));
    c->r[R_AX] = 0;
    d->nproc--;
    return 1;
}

/* ---- INT 21h ----------------------------------------------------------- */

static int int21(cpu_t *c, dos_t *d)
{
    uint8_t ah = (uint8_t)(c->r[R_AX] >> 8);
    uint8_t al = (uint8_t)(c->r[R_AX] & 0xFF);
    uint16_t me = current_psp(d);

    if (ah == 0x01 || ah == 0x07 || ah == 0x08 || ah == 0x0A
        || (ah == 0x06 && al == 0xFF))
        trace_text_screen(c, d, ah);
    else if (ah == 0x00 || ah == 0x4C)
        trace_text_screen(c, d, 0x100);

    if (d->trace_dos & DOS_TRACE_ALL)
        logf_(d, "INT21 AH=%02X AL=%02X BX=%04X CX=%04X DX=%04X DS=%04X ES=%04X [%s]\n",
              ah, al, c->r[R_BX], c->r[R_CX], c->r[R_DX], c->seg[S_DS], c->seg[S_ES],
              dos_current_program(d));

    switch (ah) {

    case 0x00:     /* terminate, old style */
        return terminate(c, d, 0);

    case 0x09: {   /* print a '$'-terminated string */
        uint16_t off = c->r[R_DX];
        char buf[1025]; int n = 0;
        for (int i = 0; i < 1024; i++) {
            uint8_t ch = mem_read8(c, phys(c->seg[S_DS], (uint16_t)(off + i)));
            if (ch == '$') break;
            buf[n++] = (char)ch;
        }
        buf[n] = 0;
        logf_(d, "[print] %s\n", buf);
        for (int i = 0; i < n; i++)
            text_output_char(c, d, (uint8_t)buf[i]);
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | '$');
        ok(c);
        return 1;
    }

    /* DOS console input. SETUP reads its menu through these (via the MISC
     * overlay) rather than through INT 16h, so they draw on the same BIOS
     * key buffer the scripted keystrokes are queued into. With nothing
     * queued they return 0 and count the starvation instead of blocking. */
    case 0x01:     /* read char with echo */
    case 0x07:     /* direct read, no echo, no Ctrl-C */
    case 0x08: {   /* read, no echo */
        uint16_t key;
        if (kbd_pop(d, &key, 1)) {
            c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | (key & 0xFF));
            if (ah == 0x01) {
                logf_(d, "[stdin] '%c'\n", (key & 0xFF) >= 0x20 ? (char)(key & 0xFF) : '.');
                text_output_char(c, d, (uint8_t)key);
            }
        } else {
            d->key_starved++;
            c->r[R_AX] = (uint16_t)(c->r[R_AX] & 0xFF00);
        }
        ok(c);
        return 1;
    }

    case 0x02:     /* write char to stdout */
        logf_(d, "%c", (char)(c->r[R_DX] & 0xFF));
        text_output_char(c, d, (uint8_t)c->r[R_DX]);
        ok(c);
        return 1;

    case 0x06: {   /* direct console I/O: DL=FF reads, else writes DL */
        uint16_t key;
        if ((c->r[R_DX] & 0xFF) == 0xFF) {
            if (kbd_pop(d, &key, 1)) {
                c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | (key & 0xFF));
                c->flags = (uint16_t)(c->flags & (uint16_t)(F_ZF ^ 0xFFFFu));
            } else {
                c->r[R_AX] = (uint16_t)(c->r[R_AX] & 0xFF00);
                c->flags |= F_ZF;
            }
        } else {
            logf_(d, "%c", (char)(c->r[R_DX] & 0xFF));
            text_output_char(c, d, (uint8_t)c->r[R_DX]);
        }
        ok(c);
        return 1;
    }

    case 0x0B: {   /* stdin status: FF if a key is waiting */
        uint16_t key;
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | (kbd_pop(d, &key, 0) ? 0xFF : 0x00));
        ok(c);
        return 1;
    }

    case 0x0C: {   /* flush input, then perform AL's function */
        uint16_t key;
        if (al == 0x01 || al == 0x06 || al == 0x07 || al == 0x08) {
            if (kbd_pop(d, &key, 1)) c->r[R_AX] = (uint16_t)(key & 0xFF);
            else { d->key_starved++; c->r[R_AX] = 0; }
        } else {
            c->r[R_AX] = 0;
        }
        ok(c);
        return 1;
    }

    case 0x0E:     /* select disk */
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | 3);
        ok(c);
        return 1;

    case 0x11:     /* FCB find first: nothing matches */
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | 0xFF);
        ok(c);
        return 1;

    case 0x19:     /* current drive: C */
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | 2);
        ok(c);
        return 1;

    case 0x1A:     /* set DTA */
        d->dta = phys(c->seg[S_DS], c->r[R_DX]);
        ok(c);
        return 1;

    case 0x25: {   /* set interrupt vector */
        uint32_t v = (uint32_t)al * 4u;
        mem_write16(c, v, c->r[R_DX]);
        mem_write16(c, v + 2, c->seg[S_DS]);
        if (d->trace_dos & DOS_TRACE_ALL)
            logf_(d, "  set vector %02X -> %04X:%04X\n", al, c->seg[S_DS], c->r[R_DX]);
        ok(c);
        return 1;
    }

    case 0x2A:     /* get date: 1991-03-20, a Wednesday */
        c->r[R_CX] = 1991; c->r[R_DX] = (3 << 8) | 20;
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | 3);
        ok(c);
        return 1;

    case 0x2C: {   /* get time, derived from the tick count so it advances */
        uint32_t t = mem_read16(c, BDA_TICKS) | ((uint32_t)mem_read16(c, BDA_TICKS + 2) << 16);
        uint32_t secs = (uint32_t)(t * 10 / 182);
        c->r[R_CX] = (uint16_t)((((secs / 3600) % 24) << 8) | ((secs / 60) % 60));
        c->r[R_DX] = (uint16_t)(((secs % 60) << 8) | ((t * 55 / 10) % 100));
        ok(c);
        return 1;
    }

    case 0x30:     /* get DOS version: report 5.00 */
        c->r[R_AX] = 0x0005;
        c->r[R_BX] = 0;
        c->r[R_CX] = 0;
        ok(c);
        return 1;

    case 0x33:     /* get/set the Ctrl-Break flag */
        c->r[R_DX] = 0;
        ok(c);
        return 1;

    case 0x35: {   /* get interrupt vector */
        uint32_t v = (uint32_t)al * 4u;
        c->r[R_BX] = mem_read16(c, v);
        c->seg[S_ES] = mem_read16(c, v + 2);
        ok(c);
        return 1;
    }

    case 0x3B:     /* chdir */
        ok(c);
        return 1;

    case 0x3C:     /* create/truncate */
    case 0x3D: {   /* open */
        char dp[520], hp[600];
        guest_str(c, c->seg[S_DS], c->r[R_DX], dp, sizeof(dp));
        host_path(d, dp, hp, sizeof(hp));
        int h = alloc_handle(d);
        if (h < 0) { fail(c, ERR_TOO_MANY_OPEN); return 1; }

        FILE *f;
        if (ah == 0x3C) {
            f = fopen(hp, "wb+");
            if (!f) f = open_ci(hp, "wb+");
        } else {
            const char *mode = ((al & 7) == 0) ? "rb" : "rb+";
            f = open_ci(hp, mode);
            if (!f && (al & 7) != 0) f = open_ci(hp, "rb");
        }
        if (!f) {
            logf_(d, "[file] %s '%s' FAILED (not found)\n",
                  ah == 0x3C ? "create" : "open", dp);
            fail(c, ERR_FILE_NOT_FOUND);
            return 1;
        }
        d->files[h].fp = f;
        d->files[h].in_use = 1;
        d->files[h].owner = me;
        snprintf(d->files[h].path, sizeof(d->files[h].path), "%s", hp);
        /* With the instruction count, because WHEN a file is opened is
         * often the measurement: the gap between two consecutive .PAN
         * opens over that file's cel count is the intro's cel rate,
         * and there was no way to read it off this trace before. */
        if (d->trace_dos & DOS_TRACE_FILES)
            logf_(d, "[file] %s '%s' -> handle %d @%llu %s\n",
                  ah == 0x3C ? "create" : "open", dp, h,
                  (unsigned long long)c->icount, dos_current_program(d));
        if (d->trace_dos & DOS_TRACE_ALL)
            logf_(d, "  %s '%s' -> handle %d  @%llu %s\n",
                  ah == 0x3C ? "create" : "open", dp, h,
                  (unsigned long long)c->icount, dos_current_program(d));
        c->r[R_AX] = (uint16_t)h;
        ok(c);
        return 1;
    }

    case 0x3E: {   /* close */
        uint16_t h = c->r[R_BX];
        if (h < 5) { ok(c); return 1; }
        if (h >= DOS_MAX_FILES || !d->files[h].in_use) { fail(c, ERR_BAD_HANDLE); return 1; }
        if (d->trace_dos & DOS_TRACE_FILES)
            logf_(d, "[file] close '%s' @%llu %s\n", d->files[h].path,
                  (unsigned long long)c->icount, dos_current_program(d));
        fclose(d->files[h].fp);
        d->files[h].in_use = 0;
        d->files[h].fp = NULL;
        ok(c);
        return 1;
    }

    case 0x3F: {   /* read */
        uint16_t h = c->r[R_BX], n = c->r[R_CX];
        if (h < 5) { c->r[R_AX] = 0; ok(c); return 1; }   /* stdin: EOF */
        if (h >= DOS_MAX_FILES || !d->files[h].in_use) { fail(c, ERR_BAD_HANDLE); return 1; }
        uint8_t *tmp = (uint8_t *)malloc(n ? n : 1);
        size_t got = n ? fread(tmp, 1, n, d->files[h].fp) : 0;
        if (d->trace_dos & DOS_TRACE_FILES)
            logf_(d, "[file] read '%s' requested=%u got=%u @%llu %s\n",
                  d->files[h].path, n, (unsigned)got,
                  (unsigned long long)c->icount, dos_current_program(d));
        for (size_t i = 0; i < got; i++)
            mem_write8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_DX] + i)), tmp[i]);
        free(tmp);
        c->r[R_AX] = (uint16_t)got;
        ok(c);
        return 1;
    }

    case 0x40: {   /* write */
        uint16_t h = c->r[R_BX], n = c->r[R_CX];
        if (h < 5) {
            char buf[512]; int k = 0;
            for (uint16_t i = 0; i < n && k < 511; i++)
                buf[k++] = (char)mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_DX] + i)));
            buf[k] = 0;
            logf_(d, "[stdout] %s", buf);
            c->r[R_AX] = n;
            ok(c);
            return 1;
        }
        if (h >= DOS_MAX_FILES || !d->files[h].in_use) { fail(c, ERR_BAD_HANDLE); return 1; }
        uint8_t *tmp = (uint8_t *)malloc(n ? n : 1);
        for (uint16_t i = 0; i < n; i++)
            tmp[i] = mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_DX] + i)));
        size_t put = n ? fwrite(tmp, 1, n, d->files[h].fp) : 0;
        if (d->trace_dos & DOS_TRACE_FILES)
            logf_(d, "[file] write '%s' requested=%u wrote=%u @%llu %s\n",
                  d->files[h].path, n, (unsigned)put,
                  (unsigned long long)c->icount, dos_current_program(d));
        free(tmp);
        c->r[R_AX] = (uint16_t)put;
        ok(c);
        return 1;
    }

    case 0x41: {   /* unlink */
        char dp[520], hp[600];
        guest_str(c, c->seg[S_DS], c->r[R_DX], dp, sizeof(dp));
        host_path(d, dp, hp, sizeof(hp));
        if (remove(hp) != 0) { fail(c, ERR_FILE_NOT_FOUND); return 1; }
        if (d->trace_dos & DOS_TRACE_FILES)
            logf_(d, "[file] unlink '%s' @%llu %s\n", dp,
                  (unsigned long long)c->icount, dos_current_program(d));
        ok(c);
        return 1;
    }

    case 0x42: {   /* lseek */
        uint16_t h = c->r[R_BX];
        if (h >= DOS_MAX_FILES || !d->files[h].in_use || h < 5) { fail(c, ERR_BAD_HANDLE); return 1; }
        long off = (long)(int32_t)(((uint32_t)c->r[R_CX] << 16) | c->r[R_DX]);
        int whence = (al == 1) ? SEEK_CUR : (al == 2) ? SEEK_END : SEEK_SET;
        if (fseek(d->files[h].fp, off, whence) != 0) { fail(c, ERR_BAD_FUNCTION); return 1; }
        if (d->trace_dos & DOS_TRACE_FILES)
            logf_(d, "[file] seek '%s' offset=%ld from=%d @%llu %s\n",
                  d->files[h].path, off, whence,
                  (unsigned long long)c->icount, dos_current_program(d));
        long pos = ftell(d->files[h].fp);
        c->r[R_AX] = (uint16_t)(pos & 0xFFFF);
        c->r[R_DX] = (uint16_t)((pos >> 16) & 0xFFFF);
        ok(c);
        return 1;
    }

    case 0x43:     /* get/set file attributes */
        if (al == 0) c->r[R_CX] = 0x20;   /* archive */
        ok(c);
        return 1;

    case 0x44:     /* ioctl */
        if (al == 0) {
            uint16_t h = c->r[R_BX];
            c->r[R_DX] = (h < 5) ? 0x0080 : 0x0000;   /* bit 7: character device */
        }
        ok(c);
        return 1;

    case 0x47:     /* getcwd: root */
        mem_write8(c, phys(c->seg[S_DS], c->r[R_SI]), 0);
        ok(c);
        return 1;

    case 0x48: {   /* allocate memory */
        uint16_t seg = 0;
        if (!dos_alloc(d, c->r[R_BX], me, &seg)) {
            c->r[R_BX] = arena_avail(d);
            fail(c, ERR_NO_MEMORY);
            return 1;
        }
        if (d->trace_dos & DOS_TRACE_ALL) logf_(d, "  alloc %u paras -> %04X\n", c->r[R_BX], seg);
        c->r[R_AX] = seg;
        ok(c);
        return 1;
    }

    case 0x49: {   /* free memory */
        dos_block *b = find_block(d, c->seg[S_ES]);
        if (b) b->in_use = 0;
        ok(c);
        return 1;
    }

    case 0x4A: {   /* resize a memory block */
        dos_block *b = find_block(d, c->seg[S_ES]);
        if (!b) { fail(c, ERR_BAD_FUNCTION); return 1; }
        uint16_t want = c->r[R_BX], mx = block_max_paras(d, b);
        if (want > mx) {
            c->r[R_BX] = mx;
            fail(c, ERR_NO_MEMORY);
            return 1;
        }
        if (d->trace_dos & DOS_TRACE_ALL) logf_(d, "  resize %04X: %u -> %u paras\n", b->seg, b->paras, want);
        b->paras = want;
        ok(c);
        return 1;
    }

    case 0x4B: {   /* EXEC */
        char dp[520], hp[600];
        guest_str(c, c->seg[S_DS], c->r[R_DX], dp, sizeof(dp));
        host_path(d, dp, hp, sizeof(hp));
        uint16_t pb_seg = c->seg[S_ES], pb = c->r[R_BX];

        if (al == 0x03) {           /* load overlay */
            uint16_t lseg = seg_read16(c, pb_seg, pb);
            uint16_t fac  = seg_read16(c, pb_seg, (uint16_t)(pb + 2));
            uint16_t err = load_overlay(d, hp, lseg, fac, dp);
            if (err) { logf_(d, "[overlay] %s FAILED (%u)\n", dp, err); fail(c, err); return 1; }
            c->r[R_AX] = 0;
            ok(c);
            return 1;
        }
        if (al != 0x00) { fail(c, ERR_BAD_FUNCTION); return 1; }

        if (d->nproc >= DOS_MAX_PROCS) { fail(c, ERR_NO_MEMORY); return 1; }

        exec_params ep;
        memset(&ep, 0, sizeof(ep));
        ep.env_seg  = seg_read16(c, pb_seg, pb);
        if (!ep.env_seg) ep.env_seg = d->env_seg;        /* 0 = inherit */
        ep.cmd_off  = seg_read16(c, pb_seg, (uint16_t)(pb + 2));
        ep.cmd_seg  = seg_read16(c, pb_seg, (uint16_t)(pb + 4));
        ep.fcb1_off = seg_read16(c, pb_seg, (uint16_t)(pb + 6));
        ep.fcb1_seg = seg_read16(c, pb_seg, (uint16_t)(pb + 8));
        ep.fcb2_off = seg_read16(c, pb_seg, (uint16_t)(pb + 10));
        ep.fcb2_seg = seg_read16(c, pb_seg, (uint16_t)(pb + 12));
        ep.parent_psp = me;

        /* Snapshot the parent. Nothing below touches the CPU until the load
         * has definitely succeeded, so a failure leaves the caller intact. */
        dos_proc *np = &d->procs[d->nproc];
        memset(np, 0, sizeof(*np));
        memcpy(np->r, c->r, sizeof(np->r));
        memcpy(np->seg, c->seg, sizeof(np->seg));
        np->ret_cs = c->seg[S_CS];
        np->ret_ip = c->ip;               /* already past the INT 21h */
        np->flags = c->flags;
        snprintf(np->name, sizeof(np->name), "%s", dp);

        uint16_t psp = 0;
        uint16_t err = load_program(d, hp, &ep, np->ret_cs, np->ret_ip, &psp, dp);
        if (err) {
            logf_(d, "[exec] %s FAILED (%u)\n", dp, err);
            fail(c, err);
            return 1;
        }
        np->psp_seg = psp;
        np->start_icount = c->icount;
        d->nproc++;
        return 1;                         /* control is now in the child */
    }

    case 0x4C:     /* terminate with code */
        return terminate(c, d, al);

    case 0x4D:     /* get child's exit code */
        c->r[R_AX] = d->last_child_exit;  /* AH = 0: normal termination */
        ok(c);
        return 1;

    case 0x4E: case 0x4F:   /* find first/next: nothing */
        fail(c, ERR_NO_MORE_FILES);
        return 1;

    default: {
        static unsigned seen[256];
        if (seen[ah]++ < 3)
            logf_(d, "[int21] UNHANDLED AH=%02X AL=%02X from %s at %04X:%04X%s\n",
                  ah, al, dos_current_program(d), c->op_cs, c->op_ip,
                  seen[ah] == 3 ? "  [further occurrences suppressed]" : "");
        fail(c, ERR_BAD_FUNCTION);
        return 1;
    }
    }
}

/* ---- BIOS -------------------------------------------------------------- */

static int int10(cpu_t *c, dos_t *d)
{
    uint8_t ah = (uint8_t)(c->r[R_AX] >> 8);
    uint8_t al = (uint8_t)(c->r[R_AX] & 0xFF);
    switch (ah) {
    case 0x00: {                             /* set video mode */
        d->vga_mode = (uint16_t)(al & 0x7F);
        mem_write8(c, BDA_VIDEO_MODE, (uint8_t)d->vga_mode);
        uint16_t cols = (d->vga_mode == 0 || d->vga_mode == 1 || d->vga_mode == 0x13) ? 40 : 80;
        mem_write16(c, BDA_VIDEO_COLS, cols);
        text_clear(c, d);
        logf_(d, "[video] mode %02Xh set by %s\n", d->vga_mode, dos_current_program(d));
        break;
    }
    case 0x02:                               /* set cursor position */
        mem_write16(c, BDA_CURSOR_POS, c->r[R_DX]);
        break;
    case 0x03:                               /* get cursor position */
        c->r[R_DX] = mem_read16(c, BDA_CURSOR_POS);
        c->r[R_CX] = 0x0607;
        break;
    case 0x08:                               /* read character/attribute */
        c->r[R_AX] = 0x0720;
        break;
    case 0x0E:                               /* teletype output */
        if (d->trace_dos & DOS_TRACE_ALL) logf_(d, "%c", al);
        text_output_char(c, d, al);
        break;
    case 0x0F:                               /* get video mode */
        c->r[R_AX] = (uint16_t)((mem_read16(c, BDA_VIDEO_COLS) << 8) | (d->vga_mode & 0xFF));
        c->r[R_BX] = (uint16_t)(c->r[R_BX] & 0x00FF);   /* page 0 */
        break;
    case 0x12:                               /* alternate select */
        if ((c->r[R_BX] & 0xFF) == 0x10) {   /* EGA info: colour, 256K */
            c->r[R_BX] = 0x0003;
            c->r[R_CX] = 0x0000;
        }
        break;
    case 0x1A:                               /* display combination: VGA colour */
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | 0x1A);
        c->r[R_BX] = 0x0008;
        break;
    case 0x10:                               /* palette / DAC registers */
        /* **APPLIED, as of 20 September.** For a while this was traced but
         * still discarded, under a comment arguing that "nothing here
         * changes what the guest computes". True, and the wrong test: it
         * changes what a CAPTURE SHOWS, and captures are the ground truth
         * this project rests on.
         *
         * What it cost: START's Bulletin Board lights a hovered row by
         * queueing {register, DS:0x11C1, 1} through 0x03517 and flushing
         * it at retrace with exactly this call. With the call discarded,
         * every capture of the board showed the hovered row in its NORMAL
         * colour, so a check comparing the DAC would call a CORRECT port
         * wrong - and a check comparing only pixels would see nothing at
         * all, because the row's pixels never change. Two ways to be
         * misled by one unmodelled BIOS function.
         *
         * AL=10h sets one register from DH/CH/CL; AL=12h sets a block of
         * CX registers from ES:DX upward, three 6-bit bytes each.
         *
         * A fade from black is a run of AX=1012h calls writing the whole
         * DAC a few times a frame. Every other instrument here watches
         * ports 0x3C8/0x3C9, and the intro never touches them: PLAYER
         * writes 3D8/3C4/3C5 and nothing else, so a port trace shows a
         * fade as literally nothing happening. The BIOS path was landing
         * in this `default` arm and being discarded without a word.
         *
         * `--trace-ports` now reports it in the same stream as the OUTs,
         * with the instruction count, so a fade can be seen and timed. */
        if (d->trace_ports) {
            if (al == 0x12)
                logf_(d, "DAC block @%llu first=%u count=%u from %04X:%04X %s\n",
                      (unsigned long long)c->icount, c->r[R_BX],
                      c->r[R_CX], c->seg[S_ES], c->r[R_DX],
                      dos_current_program(d));
            else if (al == 0x10)
                logf_(d, "DAC one @%llu reg=%u rgb=%u,%u,%u %s\n",
                      (unsigned long long)c->icount, c->r[R_BX],
                      (unsigned)(c->r[R_DX] >> 8), (unsigned)(c->r[R_CX] >> 8),
                      (unsigned)(c->r[R_CX] & 0xFF), dos_current_program(d));
            else
                logf_(d, "INT10 AH=10 AL=%02X @%llu %s\n", al,
                      (unsigned long long)c->icount, dos_current_program(d));
        }
        if (al == 0x10) {
            const unsigned reg = c->r[R_BX] & 0xFF;
            d->dac[reg * 3 + 0] = (uint8_t)((c->r[R_DX] >> 8) & 0x3F);
            d->dac[reg * 3 + 1] = (uint8_t)((c->r[R_CX] >> 8) & 0x3F);
            d->dac[reg * 3 + 2] = (uint8_t)(c->r[R_CX] & 0x3F);
        } else if (al == 0x12) {
            unsigned reg = c->r[R_BX] & 0xFF;
            const unsigned n = c->r[R_CX];
            uint16_t off = c->r[R_DX];
            for (unsigned i = 0; i < n && reg < 256; i++, reg++) {
                for (int comp = 0; comp < 3; comp++)
                    d->dac[reg * 3 + comp] = (uint8_t)(
                        mem_read8(c, phys(c->seg[S_ES],
                                          (uint16_t)(off + comp))) & 0x3F);
                off = (uint16_t)(off + 3);
            }
        }
        break;
    default:                                 /* scroll, write char, font: no-ops */
        break;
    }
    return 1;
}

static int int16(cpu_t *c, dos_t *d)
{
    uint8_t ah = (uint8_t)(c->r[R_AX] >> 8);
    uint16_t key;
    if (d->trace_dos & DOS_TRACE_ALL) {
        static unsigned n;
        if (n++ < 40)
            logf_(d, "INT16 AH=%02X from %s at %04X:%04X%s\n", ah,
                  dos_current_program(d), c->op_cs, c->op_ip,
                  n == 40 ? "  [further calls suppressed]" : "");
    }
    switch (ah) {
    case 0x00: case 0x10:                    /* read key, blocking */
        if (kbd_pop(d, &key, 1)) {
            c->r[R_AX] = key;
        } else {
            /* Nothing scripted. Blocking forever would hang the run, so
             * return "no key" and count it; the report shows starvation. */
            d->key_starved++;
            c->r[R_AX] = 0;
        }
        break;
    case 0x01: case 0x11:                    /* key available? */
        if (kbd_pop(d, &key, 0)) {
            c->r[R_AX] = key;
            c->flags = (uint16_t)(c->flags & (uint16_t)(F_ZF ^ 0xFFFFu));
        } else {
            c->r[R_AX] = 0;
            c->flags |= F_ZF;
        }
        break;
    case 0x02: case 0x12:                    /* shift flags */
        c->r[R_AX] = (uint16_t)(c->r[R_AX] & 0xFF00);
        break;
    default:
        break;
    }
    return 1;
}

static int int1a(cpu_t *c, dos_t *d)
{
    uint8_t ah = (uint8_t)(c->r[R_AX] >> 8);
    if (ah == 0x00) {                        /* read the tick count */
        c->r[R_DX] = mem_read16(c, BDA_TICKS);
        c->r[R_CX] = mem_read16(c, BDA_TICKS + 2);
        const uint8_t midnight = d->bios_clock ? mem_read8(c, 0x470) : 0;
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | midnight);
        if (d->bios_clock) mem_write8(c, 0x470, 0);
    } else if (ah == 0x02 || ah == 0x04) {   /* RTC time / date: zeros, CF clear */
        c->r[R_CX] = c->r[R_DX] = 0;
        c->flags = (uint16_t)(c->flags & (uint16_t)(F_CF ^ 0xFFFFu));
    }
    return 1;
}

void dos_mouse_set(dos_t *d, int x, int y, int buttons)
{
    d->mouse_x = x;
    d->mouse_y = y;
    d->mouse_buttons = buttons;
}

/* Microsoft mouse driver, polling subset. The front end is point-and-click;
 * with a mouse reported present it can be driven by exact clicks. The event
 * handler installed through function 0Ch is recorded but not invoked; if a
 * program turns out to depend on it rather than on polling, that shows up as
 * clicks having no effect and the handler will need calling. */
static int int33(cpu_t *c, dos_t *d)
{
    uint16_t fn = c->r[R_AX];
    d->mouse_calls++;
    if (d->trace_dos & DOS_TRACE_ALL) {
        /* The cap is PER PROGRAM. A single static counter spent all
         * forty on SETUP and PLAYER before START had started, so the
         * trace could not answer "which mouse functions does START
         * call" at all - which is the question it exists for. */
        static unsigned n;
        static const char *of;
        const char *who = dos_current_program(d);
        if (who != of) { of = who; n = 0; }
        if (n++ < 40)
            logf_(d, "INT33 AX=%04X from %s%s\n", fn, who,
                  n == 40 ? "  [further calls suppressed]" : "");
    }
    if (!d->mouse_present) {
        if (fn == 0x0000 || fn == 0x0021) { c->r[R_AX] = 0; c->r[R_BX] = 0; }
        return 1;
    }
    uint16_t vx = (uint16_t)(d->mouse_x * 2), vy = (uint16_t)d->mouse_y;
    switch (fn) {
    case 0x0000:                 /* reset and status: present, two buttons */
    case 0x0021:                 /* software reset */
        c->r[R_AX] = 0xFFFF;
        c->r[R_BX] = 2;
        d->mouse_shown = 0;
        d->mouse_hnd_mask = 0;
        d->mouse_xmin = 0; d->mouse_xmax = 639;
        d->mouse_ymin = 0; d->mouse_ymax = 199;
        break;
    case 0x0001: d->mouse_shown = 1; break;
    case 0x0002: d->mouse_shown = 0; break;
    case 0x0003:                 /* position and button status */
        c->r[R_BX] = (uint16_t)d->mouse_buttons;
        c->r[R_CX] = vx;
        c->r[R_DX] = vy;
        break;
    case 0x0004:                 /* set position */
        d->mouse_x = c->r[R_CX] / 2;
        d->mouse_y = c->r[R_DX];
        break;
    case 0x0005:                 /* button press data */
        c->r[R_AX] = (uint16_t)d->mouse_buttons;
        c->r[R_BX] = (uint16_t)((d->mouse_buttons >> (c->r[R_BX] & 1)) & 1);
        c->r[R_CX] = vx; c->r[R_DX] = vy;
        break;
    case 0x0006:                 /* button release data */
        c->r[R_AX] = (uint16_t)d->mouse_buttons;
        c->r[R_BX] = 0;
        c->r[R_CX] = vx; c->r[R_DX] = vy;
        break;
    case 0x0007: d->mouse_xmin = c->r[R_CX]; d->mouse_xmax = c->r[R_DX]; break;
    case 0x0008: d->mouse_ymin = c->r[R_CX]; d->mouse_ymax = c->r[R_DX]; break;
    case 0x000B: c->r[R_CX] = 0; c->r[R_DX] = 0; break;   /* motion counters */
    case 0x000C:                 /* set event handler */
        d->mouse_hnd_mask = c->r[R_CX];
        d->mouse_hnd_off = c->r[R_DX];
        d->mouse_hnd_seg = c->seg[S_ES];
        logf_(d, "[mouse] %s installed an event handler at %04X:%04X mask %04X "
                 "(not invoked by the shim)\n",
              dos_current_program(d), d->mouse_hnd_seg, d->mouse_hnd_off, d->mouse_hnd_mask);
        break;
    case 0x0015: c->r[R_BX] = 0x40; break;                /* state buffer size */
    case 0x0024:                 /* version and type: 8.00, bus mouse */
        c->r[R_BX] = 0x0800; c->r[R_CX] = 0x0400;
        break;
    default:                     /* ratios, page, cursor shapes: accepted */
        break;
    }
    return 1;
}

int dos_int_hook(cpu_t *c, uint8_t vec)
{
    dos_t *d = self(c);
    if (!d) return 0;
    switch (vec) {
    case 0x21: return int21(c, d);
    case 0x10: return int10(c, d);
    case 0x16: return int16(c, d);
    case 0x1A: return int1a(c, d);
    case 0x33: return int33(c, d);
    case 0xF8: {
        if (!d->bios_clock) return 0;
        uint32_t t = mem_read16(c, BDA_TICKS)
                   | ((uint32_t)mem_read16(c, BDA_TICKS + 2) << 16);
        t++;
        if (t >= 0x1800B0u) {
            t = 0;
            mem_write8(c, 0x470, (uint8_t)(mem_read8(c, 0x470) + 1));
        }
        mem_write16(c, BDA_TICKS, (uint16_t)t);
        mem_write16(c, BDA_TICKS + 2, (uint16_t)(t >> 16));
        /* The BIOS invokes the guest's user-timer callback once per tick. */
        cpu_interrupt(c, 0x1C);
        return 1;
    }
    case 0xF9: bios_key_irq(c, d); return 1;   /* the INT 9 stub's translation step */
    case 0x20: return terminate(c, d, 0);
    case 0x11: c->r[R_AX] = mem_read16(c, BDA_EQUIPMENT); return 1;
    case 0x12: c->r[R_AX] = mem_read16(c, BDA_MEM_KB); return 1;
    default: {
        /* Anything else runs the guest's own handler, if it installed one.
         * Report the first few of each so an unexpected interrupt shows up
         * as a diagnosis rather than as a mysterious spin. */
        static unsigned seen[256];
        if (seen[vec]++ < 3)
            logf_(d, "[int] INT %02Xh taken at %04X:%04X (vector -> %04X:%04X)%s\n",
                  vec, c->op_cs, c->op_ip,
                  mem_read16(c, (uint32_t)vec * 4 + 2),
                  mem_read16(c, (uint32_t)vec * 4),
                  seen[vec] == 3 ? "  [further occurrences suppressed]" : "");
        return 0;
    }
    }
}

/* ---- port I/O ---------------------------------------------------------- */

/* The OPL2's status register, defined below with the rest of its model. */
static uint8_t opl_status(dos_t *d, cpu_t *c);
/* The game port's, likewise. Both are read here and modelled further down;
 * without these the compiler takes the implicit-int declaration and the
 * definition is then a redefinition, which is how this file learned the
 * lesson the first time. */
static uint8_t joy_read(dos_t *d, cpu_t *c);
static void joy_trigger(dos_t *d, cpu_t *c);

uint32_t dos_io_read(cpu_t *c, uint16_t port, int width)
{
    dos_t *d = self(c);
    (void)width;
    switch (port) {
    case 0x40: case 0x41: case 0x42: {
        /* A counter that actually COUNTS DOWN.
         *
         * This used to return 0x34 then 0x12 - the two bytes of a fixed
         * 0x1234 - with a comment claiming it advanced. It did not: every
         * latch read the same value, so any driver measuring elapsed time
         * by subtracting two latches got ZERO.
         *
         * MPS_LOGO's AdLib and Roland drivers do exactly that. `asound.log`
         * at 0x00FE latches counter 0 sixteen times across a delay loop,
         * sums the deltas, divides by 16 and then divides 0x5140 by the
         * result - and with every delta zero that is an integer divide by
         * zero. The C runtime printed "R6003 - integer divide by 0", the
         * logo exited 255, and F117.COM then SKIPPED PLAYER.EXE entirely.
         * So choosing a sound card changed the whole boot, and the cause
         * was here rather than in the game.
         *
         * The model: one full counter period is one IRQ0 interval, which
         * is what --irq-every already defines, so the counter runs from
         * its reload value down to 0 across each tick. A latch samples
         * once and the second read returns that sample's high byte, as
         * real hardware does. */
        int ch = port - 0x40;
        if (!d->pit_latch_state[ch]) {
            const uint32_t full = d->pit_divisor[ch] ? d->pit_divisor[ch]
                                                     : 65536u;
            const uint64_t per = d->irq_every ? d->irq_every : 20000u;
            const uint64_t pos = (uint64_t)c->icount % per;
            uint32_t cur = (uint32_t)(full - (uint64_t)full * pos / per);
            if (dos_vga_time && d->ins_per_sec) {
                /* On the ins_per_sec clock, and in mode 3 - the BIOS's
                 * mode and the one the games program - the counter steps
                 * by two and so runs its range twice a period. START's
                 * retrace calibration (0x08DD8) halves its sum for this. */
                const uint64_t clocks = (c->icount - dos_pit_epoch) * 1193182ull
                                      / d->ins_per_sec;
                const unsigned mode = dos_pit_mode[ch] ? (dos_pit_mode[ch] & 7u) : 3u;
                const uint64_t step = (mode & 3u) == 3u ? 2u : 1u;
                cur = (uint32_t)(full - (clocks * step) % full);
            }
            if (cur == 0) cur = 1;     /* a latched 0 reads as "expired" */
            d->pit_latched[ch] = (uint16_t)cur;
        }
        const uint8_t v = d->pit_latch_state[ch]
                        ? (uint8_t)(d->pit_latched[ch] >> 8)
                        : (uint8_t)(d->pit_latched[ch] & 0xFF);
        d->pit_latch_state[ch] ^= 1;
        return v;
    }
    case 0x3DA: {
        /* Input Status Register 1. The game polls this both to wait for
         * retrace and to measure machine speed, so it has to keep changing;
         * a stuck value would hang the guest. */
        unsigned t = ++d->vga_status_reads;
        uint8_t v = 0;
        if (dos_vga_time && d->ins_per_sec) {
            /* Mode 13h on a VGA: 449 lines a frame at 31.469 kHz, 400 of
             * them shown (200 rows, each scanned twice), vertical retrace
             * on lines 412 and 413. Bit 0 is set whenever the beam is not
             * drawing - the horizontal blank, about a fifth of each line,
             * and the whole vertical blank. */
            const uint64_t frame = d->ins_per_sec * 1000ull / 70086ull;
            const uint64_t at = (c->icount % frame) * 449ull;
            const unsigned line = (unsigned)(at / frame);
            const unsigned across = (unsigned)((at % frame) * 100ull / frame);
            if (line >= 400 || across >= 80) v |= 0x01;
            if (line >= 412 && line < 414) v |= 0x08;
            return v;
        }
        if ((t & 0x1F) < 4)  v |= 0x01;   /* display enable */
        if ((t & 0xFF) < 16) v |= 0x08;   /* vertical retrace */
        return v;
    }
    case 0x3C9: {                         /* DAC data readback, auto-advancing */
        uint8_t v = d->dac[d->dac_index * 3 + d->dac_component];
        if (++d->dac_component == 3) {
            d->dac_component = 0;
            d->dac_index = (d->dac_index + 1) & 0xFF;
        }
        return v;
    }
    case 0x60:  return d->port60;         /* keyboard data: last scancode delivered */
    case 0x61:  return d->port61;
    case 0x64:  return 0x14;              /* controller status: nothing ready */
    case 0x330: {
        uint8_t value = 0xFE;             /* DOSBox MPU401_ReadData: empty */
        if (g_mpu.used) {
            value = g_mpu.queue[g_mpu.head];
            g_mpu.head = (g_mpu.head + 1u) % sizeof g_mpu.queue;
            g_mpu.used--;
        }
        return value;
    }
    case 0x331:                         /* UART status: ready-to-write, plus RX */
        return (uint8_t)(0x3Fu | (g_mpu.used ? 0u : 0x80u));
    case 0x201:                            /* the analog game port */
        trace_text_screen(c, d, 0x201);
        return joy_read(d, c);
    case 0x388: case 0x389:
        /* The AdLib status register. This returned a flat 0x00 under a
         * comment that ended in a question mark, and the answer is that
         * it matters: detection is "reset the timers, start timer 1,
         * wait, expect 0xC0" - ASOUND.LOG 0x0140 - and a constant zero
         * fails it forever. sndrun.c has modelled this correctly since
         * it was written; this is the same model. */
        return opl_status(d, c);
    case 0x22A: case 0x22E: return 0xFF;  /* Sound Blaster: absent */
    default:    return 0xFF;
    }
}

int      dos_vga_time;
uint8_t  dos_pit_mode[3];
uint64_t dos_pit_epoch;

/* ---- the OPL2's observable half --------------------------------------- */

/* Microseconds, from where the CPU is and how this run is timed. One IRQ0
 * period is `irq_every` instructions AND one PIT period, so the divisor
 * fixes how much real time that is. Approximate, but it is the only
 * consistent clock a run with an artificial IRQ rate has. */
/* Where this run is in microseconds, derived from the one rate the run
 * actually fixes: --irq-every instructions is one PIT period, and one PIT
 * period is counter 0's divisor over 1,193,182 Hz. Both the OPL timers and
 * the game port's one-shots read it, so they cannot disagree about time.
 *
 * Worth knowing what it implies: at the default --irq-every 20000 with the
 * power-on divisor, one instruction is about 2.7us, so the modelled CPU
 * runs near 0.36 MIPS - several times slower than the 286 this game
 * shipped for. Anything the guest measures AGAINST ITSELF (a delay-loop
 * calibration, a paddle count) comes out scaled by that, which is why
 * docs/re/102-logo-sound-driver.md says the jingle's tempo is not
 * measurable by this route. Raising --irq-every raises the modelled clock. */
static uint64_t dos_now_us(dos_t *d, cpu_t *c)
{
    if (d->ins_per_sec)
        return (uint64_t)c->icount * 1000000ull / d->ins_per_sec;
    const uint64_t div = d->pit_divisor[0] ? d->pit_divisor[0] : 65536u;
    const uint64_t per = d->irq_every ? d->irq_every : 20000u;
    const uint64_t us_per_period = div * 1000000ull / 1193182ull;
    return (uint64_t)c->icount * us_per_period / per;
}

static uint64_t opl_now_us(dos_t *d, cpu_t *c)
{
    return dos_now_us(d, c);
}

/* One axis's one-shot length: 24.2us at rest, 0.011us per ohm across a
 * 100k pot, which is the standard game-card timing. */
static uint64_t joy_us(unsigned axis)
{
    if (axis > 255) axis = 255;
    return 24ull + (unsigned long long)axis * 1100ull / 255ull;
}

/* The `out` that fires them. Any width, any value: the card decodes the
 * write itself, not what is written. */
static void joy_trigger(dos_t *d, cpu_t *c)
{
    const uint64_t now = dos_now_us(d, c);
    /* An axis of 0x100 or more has no pot on it: its one-shot never
     * ends, so a reader counting it runs out of count (VGAME 0x1126E). */
    for (int i = 0; i < 4; i++)
        d->joy_due[i] = d->joy_axis[i] >= 0x100 ? UINT64_MAX
                                                : now + joy_us(d->joy_axis[i]);
}

static uint8_t joy_read(dos_t *d, cpu_t *c)
{
    /* No stick: axis bits low, buttons released. This is what the recipes
     * that answer SETUP's joystick prompt with N have always seen. */
    if (!d->joy_present) return 0xF0;
    const uint64_t now = dos_now_us(d, c);
    uint8_t v = 0;
    for (int i = 0; i < 4; i++)
        if (d->joy_due[i] > now) v |= (uint8_t)(1u << i);
    /* Buttons are active low. */
    v |= (uint8_t)((~d->joy_buttons & 0x0Fu) << 4);
    return v;
}

static void opl_tick(dos_t *d, uint64_t us)
{
    if (d->opl_t1_run && us >= d->opl_t1_due) {
        d->opl_t1_flag = 1;
        d->opl_t1_due += (256u - d->opl_t1_preset) * 80ull;   /* free-running */
    }
    if (d->opl_t2_run && us >= d->opl_t2_due) {
        d->opl_t2_flag = 1;
        d->opl_t2_due += (256u - d->opl_t2_preset) * 320ull;
    }
}

static uint8_t opl_status(dos_t *d, cpu_t *c)
{
    opl_tick(d, opl_now_us(d, c));
    uint8_t s = 0;
    if (d->opl_t1_flag && !d->opl_t1_mask) s |= 0x40;
    if (d->opl_t2_flag && !d->opl_t2_mask) s |= 0x20;
    if (s) s |= 0x80;
    return s;
}

static void opl_write_reg(dos_t *d, cpu_t *c, uint8_t reg, uint8_t val)
{
    const uint64_t us = opl_now_us(d, c);
    switch (reg) {
    case 0x02: d->opl_t1_preset = val; break;
    case 0x03: d->opl_t2_preset = val; break;
    case 0x04:
        if (val & 0x80) {                 /* IRQ reset clears both flags */
            d->opl_t1_flag = d->opl_t2_flag = 0;
            break;                        /* and does nothing else */
        }
        d->opl_t1_mask = (val & 0x40) != 0;
        d->opl_t2_mask = (val & 0x20) != 0;
        if ((val & 1) && !d->opl_t1_run)
            d->opl_t1_due = us + (256u - d->opl_t1_preset) * 80ull;
        if ((val & 2) && !d->opl_t2_run)
            d->opl_t2_due = us + (256u - d->opl_t2_preset) * 320ull;
        d->opl_t1_run = (val & 1) != 0;
        d->opl_t2_run = (val & 2) != 0;
        break;
    default: break;
    }
}

void dos_io_write(cpu_t *c, uint16_t port, uint32_t val, int width)
{
    dos_t *d = self(c);
    (void)width;
    if (port == 0x20) {                   /* 8259 master: OCW2 */
        /* Any EOI form (non-specific 20h, specific 60h-67h) has bit 5 set and
         * ends the in-service state, letting the next IRQ0 through. */
        if (val == 0x61) d->irq1_in_service = 0;               /* specific EOI, IRQ1 */
        else if (val == 0x60) d->irq0_in_service = 0;          /* specific EOI, IRQ0 */
        else if (val & 0x20) {                                 /* non-specific: highest in service */
            if (d->irq0_in_service) d->irq0_in_service = 0;
            else d->irq1_in_service = 0;
        }
        return;
    }
    if (port == 0x201) { joy_trigger(d, c); return; }   /* fire the one-shots */
    if (port == 0x21) return;             /* IMR: masking not modelled */
    if (port == 0x61) {
        d->port61 = (uint8_t)val;
        if (d->trace_ports)
            logf_(d, "SPEAKER %8llu %02X  %04X:%04X %s\n",
                  (unsigned long long)c->icount, d->port61,
                  c->op_cs, c->op_ip, dos_current_program(d));
        return;
    }
    if (port == 0x331) {
        const uint8_t v = (uint8_t)val;
        if (g_mpu.uart && v != 0xFFu) return;
        if (v == 0xFFu) { g_mpu.uart = 0; g_mpu.used = 0; g_mpu.head = 0; }
        else if (v == 0x3Fu) g_mpu.uart = 1;
        mpu_queue(0xFEu);
        if (d->trace_ports)
            logf_(d, "MPU CMD %02X @%llu %04X:%04X %s\n", v,
                  (unsigned long long)c->icount, c->op_cs, c->op_ip,
                  dos_current_program(d));
        return;
    }
    if (port == 0x330) {
        if (d->trace_ports)
            logf_(d, "MIDI %8llu %02X  %04X:%04X %s\n",
                  (unsigned long long)c->icount, (unsigned)(val & 0xFF),
                  c->op_cs, c->op_ip, dos_current_program(d));
        return;
    }
    if (port == 0x388) d->opl_index = (uint8_t)val;   /* OPL2 index */
    else if (port == 0x389)                          /* OPL2 data */
        opl_write_reg(d, c, d->opl_index, (uint8_t)(val & 0xFF));

    if (port == 0x3C8) {                  /* DAC write index */
        d->dac_index = (int)(val & 0xFF);
        d->dac_component = 0;
        return;
    }
    if (port == 0x3C9) {                  /* DAC data: R, G, B then auto-advance */
        d->dac[d->dac_index * 3 + d->dac_component] = (uint8_t)(val & 0x3F);
        if (++d->dac_component == 3) {
            d->dac_component = 0;
            d->dac_index = (d->dac_index + 1) & 0xFF;
            /* Once per completed entry, not once per component: a fade is
             * hundreds of these a second and the interesting number is how
             * the whole palette moves over time.
             *
             * These two branches used to `return` BEFORE the generic
             * trace at the bottom of this function, so `--trace-ports`
             * reported NO DAC activity at all - and a palette fade looked
             * exactly like a fade that was never written. The port was
             * modelled and untraced, which is the worst of both: the
             * guest behaved correctly and no instrument could see it. */
            if (d->trace_ports) {
                const int e = (d->dac_index - 1) & 0xFF;
                logf_(d, "DAC %3d = %2u,%2u,%2u @%llu %s\n",
                      e, d->dac[e * 3], d->dac[e * 3 + 1], d->dac[e * 3 + 2],
                      (unsigned long long)c->icount, dos_current_program(d));
            }
        }
        return;
    }
    if (port == 0x3C7) {                  /* DAC read index */
        d->dac_index = (int)(val & 0xFF);
        d->dac_component = 0;
        return;
    }
    if (port == 0x43) {
        int ch = (val >> 6) & 3;
        if (ch < 3) d->pit_latch_state[ch] = 0;
        if (ch < 3 && (val & 0x30)) dos_pit_mode[ch] = (uint8_t)(((val >> 1) & 7) | 0x80);
        if (d->trace_ports) logf_(d, "PIT cmd %02X (counter %d)\n", val, ch);
        return;
    }
    if (port >= 0x40 && port <= 0x42) {
        int ch = port - 0x40;
        if (d->pit_latch_state[ch] == 0) {
            d->pit_divisor[ch] = (uint16_t)(val & 0xFF);
            d->pit_latch_state[ch] = 1;
        } else {
            d->pit_divisor[ch] = (uint16_t)((d->pit_divisor[ch] & 0xFF) | ((val & 0xFF) << 8));
            d->pit_latch_state[ch] = 0;
            /* Counter 0 only. This counted every counter, and dosrun's
             * loop answers a change by rescheduling IRQ0 from the epoch -
             * which a counter-2 write leaves where it was, so the next
             * IRQ0 was already due and fired at once. The IBM driver's
             * speaker reloads counter 2 a few times a frame: under
             * --vga-time PLAYER took ~20 interrupts a frame, not 4, and
             * played the whole intro at twice its speed. GOG DOSBox holds
             * 4 (dosbox_isr_probe --program PLAYER). */
            if (ch == 0) {
                d->pit_writes++;
                dos_pit_epoch = c->icount;
            }
            /* With the WRITER's address. The PIT case returns before the
             * generic OUT trace at the end of this function - the same
             * shape as the DAC defect above - so a port trace shows the
             * reload and not who asked for it, and "which routine
             * changes the intro's timer rate" could not be answered. */
            logf_(d, "[pit] counter %d reload = %u (%.4f Hz) by %s at %04X:%04X @%llu\n",
                  ch, d->pit_divisor[ch],
                  d->pit_divisor[ch] ? 1193182.0 / d->pit_divisor[ch] : 1193182.0 / 65536.0,
                  dos_current_program(d), c->op_cs, c->op_ip,
                  (unsigned long long)c->icount);
        }
        return;
    }
    /* The instruction count and the writing instruction's address, not just
     * the value: a sound driver's register writes only mean something in
     * order and in time, and the CS:IP says which player routine emitted
     * one. `op_cs:op_ip` is the OUT itself rather than what follows it. */
    if (d->trace_ports || (d->trace_opl &&
                           (port == 0x388 || port == 0x389)))
        logf_(d, "OUT %03X, %02X  @%llu  %04X:%04X %s\n", port, val,
              (unsigned long long)c->icount, c->op_cs, c->op_ip,
              dos_current_program(d));
}

/* ---- timer delivery ---------------------------------------------------- */

static int deliver_irq0(dos_t *d)
{
    cpu_t *c = d->cpu;
    uint16_t off = mem_read16(c, 8 * 4);
    uint16_t seg = mem_read16(c, 8 * 4 + 2);
    if (!seg && !off) return 0;
    d->tick_irq_count++;
    d->irq0_in_service = 1;               /* held until the handler's EOI */
    cpu_push16(c, c->flags);
    cpu_push16(c, c->seg[S_CS]);
    cpu_push16(c, c->ip);
    c->flags = (uint16_t)(c->flags & (uint16_t)((F_IF | F_TF) ^ 0xFFFFu));
    c->seg[S_CS] = seg;
    c->ip = off;
    return 1;
}

int dos_raise_timer_irq(dos_t *d)
{
    cpu_t *c = d->cpu;
    /* Legacy bounded fixtures use an edge clock. Hardware-time captures
     * instead advance BDA time only when the guest chains the BIOS stub. */
    if (!d->bios_clock) {
        uint32_t t = mem_read16(c, BDA_TICKS) | ((uint32_t)mem_read16(c, BDA_TICKS + 2) << 16);
        t++;
        mem_write16(c, BDA_TICKS, (uint16_t)t);
        mem_write16(c, BDA_TICKS + 2, (uint16_t)(t >> 16));
    }

    if (d->irq0_in_service || !(c->flags & F_IF)) {
        /* The 8259 retains one request while IRQ0 is in service or the CPU
         * masks interrupts with CLI. Deliver it after EOI/STI from poll. */
        d->irq0_pending = 1;
        d->irq0_deferred++;
        return 0;
    }
    return deliver_irq0(d);
}

void dos_timer_poll(dos_t *d)
{
    if (d->irq0_pending && !d->irq0_in_service && (d->cpu->flags & F_IF)) {
        d->irq0_pending = 0;
        deliver_irq0(d);
    }
}

void dos_set_bios_clock(dos_t *d)
{
    /* Separate INT 8 from the generic acknowledge/IRET vectors. Keeping
     * this at 0060:0020 leaves the existing INT 9 stub at 0010 intact. */
    static const uint8_t stub[] = {
        0x50, 0xCD, 0xF8, 0xB0, 0x20, 0xE6, 0x20, 0x58, 0xCF
    };
    for (size_t i = 0; i < sizeof stub; i++)
        mem_write8(d->cpu, phys(0x0060, (uint16_t)(0x20 + i)), stub[i]);
    mem_write16(d->cpu, 8 * 4, 0x20);
    mem_write16(d->cpu, 8 * 4 + 2, 0x0060);
    mem_write8(d->cpu, phys(0x0060, 0x30), 0xCF);
    mem_write16(d->cpu, 0x1C * 4, 0x30);
    mem_write16(d->cpu, 0x1C * 4 + 2, 0x0060);
    d->bios_clock = 1;
}

/* ---- boot -------------------------------------------------------------- */

static void init_bios_data_area(cpu_t *c)
{
    mem_write16(c, BDA_EQUIPMENT, 0x0021);       /* floppy present, 80x25 colour */
    mem_write16(c, BDA_MEM_KB, 640);
    mem_write16(c, BDA_KBD_HEAD, BDA_KBD_BUF - 0x400);
    mem_write16(c, BDA_KBD_TAIL, BDA_KBD_BUF - 0x400);
    mem_write16(c, BDA_KBD_START, BDA_KBD_BUF - 0x400);
    mem_write16(c, BDA_KBD_END, BDA_KBD_BUF_END - 0x400);
    mem_write8 (c, BDA_VIDEO_MODE, 0x03);
    mem_write16(c, BDA_VIDEO_COLS, 80);
    mem_write16(c, BDA_PAGE_SIZE, 4000);
    mem_write16(c, BDA_CRTC_BASE, 0x03D4);       /* colour CRTC; VGAME reads this */
    mem_write16(c, BDA_TICKS, 0);
    mem_write16(c, BDA_TICKS + 2, 0);
    mem_write8 (c, BDA_ROWS_M1, 24);
    mem_write16(c, BDA_CHAR_HEIGHT, 16);
    mem_write8 (c, 0x487, 0x60);                 /* EGA/VGA info bytes */
    mem_write8 (c, 0x488, 0x09);
    mem_write8 (c, 0x489, 0x51);                 /* VGA: 400-line, colour */
    mem_write8 (c, 0x48A, 0x08);                 /* display combination: VGA colour */
}

FILE *dos_boot_log;

int dos_boot(dos_t *d, cpu_t *c, const char *exe_path, const char *data_dir)
{
    memset(d, 0, sizeof(*d));
    memset(&g_mpu, 0, sizeof g_mpu);
    d->cpu = c;
    d->log = dos_boot_log ? dos_boot_log : stdout;
    snprintf(d->data_dir, sizeof(d->data_dir), "%s", data_dir ? data_dir : ".");

    /* Every vector points at an IRET so an interrupt nobody handles is a
     * no-op rather than a jump into zeros. The game chains its timer handler
     * onto whatever was there, which makes this the "BIOS" handler too. */
    /* The stand-in handler does what the BIOS INT 8 would: acknowledge the
     * interrupt at the PIC, then return. The game chains its timer handler
     * onto this and relies on it for the EOI on the ticks it does not
     * acknowledge itself. Issuing an EOI for any stray vector is harmless. */
    uint16_t iret_seg = 0x0060;
    static const uint8_t stub[] = { 0x50,            /* push ax      */
                                    0xB0, 0x20,      /* mov al, 20h  */
                                    0xE6, 0x20,      /* out 20h, al  */
                                    0x58,            /* pop ax       */
                                    0xCF };          /* iret         */
    for (size_t i = 0; i < sizeof(stub); i++)
        mem_write8(c, phys(iret_seg, (uint16_t)i), stub[i]);
    for (int v = 0; v < 256; v++) {
        mem_write16(c, (uint32_t)v * 4, 0);
        mem_write16(c, (uint32_t)v * 4 + 2, iret_seg);
    }
    /* INT 9 gets its own stub: the BIOS keyboard handler reads the scancode,
     * translates it into the ring buffer and acknowledges the interrupt. The
     * translation is done by the shim through INT F9h so that a game handler
     * which chains to the BIOS vector (the flight engine does) still gets
     * its keys into the buffer. */
    static const uint8_t stub9[] = { 0x50,            /* push ax      */
                                     0xCD, 0xF9,      /* int F9h      */
                                     0xB0, 0x20,      /* mov al, 20h  */
                                     0xE6, 0x20,      /* out 20h, al  */
                                     0x58,            /* pop ax       */
                                     0xCF };          /* iret         */
    for (size_t i = 0; i < sizeof(stub9); i++)
        mem_write8(c, phys(iret_seg, (uint16_t)(0x10 + i)), stub9[i]);
    mem_write16(c, 9 * 4, 0x10);
    mem_write16(c, 9 * 4 + 2, iret_seg);
    init_bios_data_area(c);

    /* Master environment. Not decoration: the Microsoft C startup reads the
     * environment segment from PSP:2Ch and walks it to build environ and
     * argv[0]; left zero, it parses the interrupt vector table instead and
     * runs off into unmapped memory before reaching main. */
    d->env_seg = 0x0080;
    {
        static const char *vars[] = { "PATH=C:\\F117A", "COMSPEC=C:\\COMMAND.COM" };
        uint16_t o = 0;
        for (size_t v = 0; v < sizeof(vars) / sizeof(vars[0]); v++)
            for (const char *p = vars[v]; ; p++) {
                mem_write8(c, phys(d->env_seg, o++), (uint8_t)*p);
                if (!*p) break;
            }
        mem_write8(c, phys(d->env_seg, o++), 0);
        mem_write16(c, phys(d->env_seg, o), 1);
        o += 2;
        const char *self_path = "C:\\F117A\\F117.COM";
        for (const char *p = self_path; ; p++) {
            mem_write8(c, phys(d->env_seg, o++), (uint8_t)*p);
            if (!*p) break;
        }
    }

    d->arena_base_seg = 0x0100;
    d->arena_end_seg  = 0x9FFF;

    d->files[0].in_use = d->files[1].in_use = d->files[2].in_use = 1;
    d->files[0].is_device = d->files[1].is_device = d->files[2].is_device = 1;

    c->user = d;
    c->int_hook = dos_int_hook;
    c->io_read = dos_io_read;
    c->io_write = dos_io_write;

    /* The root program has no parent to return to; a terminate address of
     * 0060:0000 (the IRET) is as good a sentinel as any. */
    exec_params ep;
    memset(&ep, 0, sizeof(ep));
    ep.env_seg = d->env_seg;
    const char *base = exe_path;
    for (const char *p = exe_path; *p; p++)
        if (*p == '\\' || *p == '/') base = p + 1;

    dos_proc *root = &d->procs[0];
    memset(root, 0, sizeof(*root));
    snprintf(root->name, sizeof(root->name), "%s", base);
    uint16_t psp = 0;
    uint16_t err = load_program(d, exe_path, &ep, iret_seg, 0, &psp, base);
    if (err) {
        fprintf(stderr, "cannot load %s (DOS error %u)\n", exe_path, err);
        return 0;
    }
    root->psp_seg = psp;
    d->nproc = 1;

    return 1;
}

/* Write the root program's command tail into its PSP.
 *
 * This has to run **after** dos_boot, not before: dos_boot memsets the
 * whole dos_t, so anything stored in advance is wiped. Writing straight
 * into the PSP here is also simpler than threading it through
 * exec_params, and the guest has not executed an instruction yet.
 *
 * DOS's layout at PSP:0080 is a length byte, the text, then a CR, with the
 * length not counting the CR. The leading space DOS always leaves is added
 * here so callers pass the arguments alone. */
void dos_set_args(dos_t *d, const char *args)
{
    if (!d || !d->cpu || !d->nproc || !args || !*args) return;
    snprintf(d->args, sizeof d->args, "%s", args);

    char tail[130];
    const int n = snprintf(tail, sizeof tail, " %s", d->args);
    const uint8_t len = (uint8_t)(n > 126 ? 126 : n);
    const uint16_t psp = d->procs[0].psp_seg;
    cpu_t *c = d->cpu;
    mem_write8(c, phys(psp, 0x80), len);
    for (uint8_t i = 0; i < len; i++)
        mem_write8(c, phys(psp, (uint16_t)(0x81 + i)), (uint8_t)tail[i]);
    mem_write8(c, phys(psp, (uint16_t)(0x81 + len)), 0x0D);
}

void dos_shutdown(dos_t *d)
{
    for (int i = 5; i < DOS_MAX_FILES; i++)
        if (d->files[i].in_use && d->files[i].fp)
            fclose(d->files[i].fp);
}
