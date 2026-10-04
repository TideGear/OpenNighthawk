/* dos.c - DOS and the PC BIOS, as much of them as the game uses.
 *
 * Begun as the Reimp oracle's DOS (tools/x86oracle/dos.c at cfb8cec9): the
 * loader, the EXEC/overlay/terminate machinery that F117.COM's phase chain
 * needs, the bump allocator, the INT 21h file calls. The oracle served
 * scripted captures, so it returned "no key" from a blocking read rather
 * than wait, drew no BIOS text, started the clock at midnight and wrote
 * saves into the install. Here the game is played, so:
 *
 *   - Blocking reads (INT 16h AH=00, INT 21h AH=01/07/08) wait, the way the
 *     BIOS does: the CPU idles in the call and interrupts keep arriving.
 *   - INT 10h's text services work (SETUP draws its screens with AH=09).
 *   - The BIOS keyboard handler translates set-1 scancodes for a US layout
 *     with the shift states, as the ROM does, instead of being handed the
 *     ASCII by a script.
 *   - The tick count starts at the time of day and DOS reports the date.
 *   - Files the game writes go to a save directory, and reads look there
 *     first - the same overlay GOG's DOSBox mounts over the install.
 *   - Every program and overlay placed in memory is announced to the host,
 *     which is how the recompiled code learns where each module now lives.
 */
#include "machine.h"
#include "x86_sem.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#define mkdir_(p) _mkdir(p)
#else
#include <dirent.h>
#include <sys/stat.h>
#define mkdir_(p) mkdir(p, 0755)
#endif

#define ERR_BAD_FUNCTION   0x01
#define ERR_FILE_NOT_FOUND 0x02
#define ERR_PATH_NOT_FOUND 0x03
#define ERR_TOO_MANY_OPEN  0x04
#define ERR_ACCESS_DENIED  0x05
#define ERR_BAD_HANDLE     0x06
#define ERR_NO_MEMORY      0x08
#define ERR_BAD_FORMAT     0x0B
#define ERR_NO_MORE_FILES  0x12

/* BIOS data area, as linear addresses. */
#define BDA_EQUIPMENT   0x410
#define BDA_MEM_KB      0x413
#define BDA_SHIFT       0x417
#define BDA_SHIFT2      0x418
#define BDA_KBD_HEAD    0x41A
#define BDA_KBD_TAIL    0x41C
#define BDA_KBD_BUF     0x41E   /* 16 words */
#define BDA_KBD_BUF_END 0x43E
#define BDA_VIDEO_MODE  0x449
#define BDA_VIDEO_COLS  0x44A
#define BDA_PAGE_SIZE   0x44C
#define BDA_CURSOR_POS  0x450
#define BDA_CURSOR_TYPE 0x460
#define BDA_CRTC_BASE   0x463
#define BDA_TICKS       0x46C
#define BDA_MIDNIGHT    0x470
#define BDA_KBD_START   0x480
#define BDA_KBD_END     0x482
#define BDA_ROWS_M1     0x484
#define BDA_CHAR_HEIGHT 0x485
#define BDA_KBD_FLAGS3  0x496

/* ===================================================================== */
/* Helpers                                                               */
/* ===================================================================== */

void dos_log(machine_t *m, const char *fmt, ...)
{
    if (!m->log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(m->log, fmt, ap);
    va_end(ap);
    fflush(m->log);
}

static void ok(cpu_t *c) { c->flags = (uint16_t)(c->flags & (uint16_t)(0xFFFFu ^ F_CF)); }
static void fail(cpu_t *c, uint16_t err) { c->flags |= F_CF; c->r[R_AX] = err; }

static uint16_t current_psp(const machine_t *m)
{
    return m->nproc ? m->procs[m->nproc - 1].psp_seg : 0;
}

const char *dos_current_program(const machine_t *m)
{
    return m->nproc ? m->procs[m->nproc - 1].name : "(none)";
}

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

/* Wait inside a BIOS call: point IP back at the INT instruction and idle.
 * Interrupts are taken while waiting whatever the caller's IF, as they are
 * in the ROM routine (which enables them on its own stack frame); the
 * caller's flags come back unchanged when the call finally completes. */
static int bios_wait(cpu_t *c)
{
    c->ip = c->op_ip;
    c->seg[S_CS] = c->op_cs;
    c->halted = 2;
    return 1;
}

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

static void console_text(machine_t *m, const char *s, size_t n)
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
/* Files: the install, overlaid by the save directory                    */
/* ===================================================================== */

/* The guest's names are flattened to their basename and resolved inside the
 * directories the user chose, which is both what the game expects (it
 * lives in one directory) and what keeps it from reaching anywhere else. */
static const char *dos_basename(const char *p)
{
    const char *base = p;
    for (const char *q = p; *q; q++)
        if (*q == '\\' || *q == '/' || *q == ':') base = q + 1;
    return base;
}

/* Case-insensitive open: the original names are upper case and the host
 * filesystem may not be. Tries as given, then upper, then lower. */
static FILE *open_ci(const char *dir, const char *name, const char *mode, char *found, size_t fn)
{
    char path[1100];
    for (int pass = 0; pass < 3; pass++) {
        char nm[260];
        size_t i = 0;
        for (; name[i] && i + 1 < sizeof nm; i++)
            nm[i] = pass == 0 ? name[i]
                  : (char)(pass == 1 ? toupper((unsigned char)name[i]) : tolower((unsigned char)name[i]));
        nm[i] = 0;
        snprintf(path, sizeof path, "%s/%s", dir, nm);
        FILE *f = fopen(path, mode);
        if (f) { if (found) snprintf(found, fn, "%s", path); return f; }
    }
    return NULL;
}

static int file_exists_ci(const char *dir, const char *name, char *found, size_t fn)
{
    FILE *f = open_ci(dir, name, "rb", found, fn);
    if (f) { fclose(f); return 1; }
    return 0;
}

static const char *write_dir(const machine_t *m)
{
    return m->save_dir[0] ? m->save_dir : m->data_dir;
}

/* Open for reading: the save directory first, then the install. */
static FILE *open_read(machine_t *m, const char *name, char *found, size_t fn)
{
    FILE *f = NULL;
    if (m->save_dir[0]) f = open_ci(m->save_dir, name, "rb", found, fn);
    if (!f) f = open_ci(m->data_dir, name, "rb", found, fn);
    return f;
}

/* Open read-write: copy an install file into the save directory first, so
 * the install itself is never written. */
static FILE *open_rw(machine_t *m, const char *name, char *found, size_t fn)
{
    if (m->save_dir[0]) {
        FILE *f = open_ci(m->save_dir, name, "rb+", found, fn);
        if (f) return f;
        char src[1100];
        FILE *in = open_ci(m->data_dir, name, "rb", src, sizeof src);
        if (!in) return NULL;
        char dst[1100];
        snprintf(dst, sizeof dst, "%s/%s", m->save_dir, name);
        FILE *out = fopen(dst, "wb+");
        if (!out) { fclose(in); return NULL; }
        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
        fclose(in);
        fflush(out);
        fseek(out, 0, SEEK_SET);
        if (found) snprintf(found, fn, "%s", dst);
        return out;
    }
    return open_ci(m->data_dir, name, "rb+", found, fn);
}

static FILE *open_create(machine_t *m, const char *name, char *found, size_t fn)
{
    char existing[1100];
    char path[1100];
    /* Keep the case of a file that already exists there. */
    if (file_exists_ci(write_dir(m), name, existing, sizeof existing))
        snprintf(path, sizeof path, "%s", existing);
    else
        snprintf(path, sizeof path, "%s/%s", write_dir(m), name);
    FILE *f = fopen(path, "wb+");
    if (f && found) snprintf(found, fn, "%s", path);
    return f;
}

static uint8_t *read_whole(machine_t *m, const char *name, long *out_size)
{
    FILE *f = open_read(m, name, NULL, 0);
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *raw = (uint8_t *)malloc((size_t)sz);
    if (!raw || fread(raw, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(raw); return NULL; }
    fclose(f);
    *out_size = sz;
    return raw;
}

static int alloc_handle(machine_t *m)
{
    for (int i = 5; i < DOS_MAX_FILES; i++)
        if (!m->files[i].in_use) return i;
    return -1;
}

static void close_files_of(machine_t *m, uint16_t owner)
{
    for (int i = 5; i < DOS_MAX_FILES; i++)
        if (m->files[i].in_use && m->files[i].owner == owner) {
            if (m->files[i].fp) fclose(m->files[i].fp);
            m->files[i].in_use = 0;
            m->files[i].fp = NULL;
        }
}

/* ---- find first / next ------------------------------------------------
 * The DTA layout DOS uses: 21 reserved bytes (ours hold the search state),
 * attribute, time, date, size, 13-byte name. */

static int wild_match(const char *pat, const char *name)
{
    /* DOS 8.3 matching: '?' any one character, '*' the rest of the part. */
    char pb[13], pe[4], nb[13], ne[4];
    const char *dot;
    size_t n;
    dot = strchr(pat, '.');
    n = dot ? (size_t)(dot - pat) : strlen(pat);
    if (n > 8) n = 8;
    memcpy(pb, pat, n); pb[n] = 0;
    snprintf(pe, sizeof pe, "%.3s", dot ? dot + 1 : "");
    dot = strrchr(name, '.');
    n = dot ? (size_t)(dot - name) : strlen(name);
    if (n > 8) return 0;
    memcpy(nb, name, n); nb[n] = 0;
    if (dot && strlen(dot + 1) > 3) return 0;
    snprintf(ne, sizeof ne, "%.3s", dot ? dot + 1 : "");
    const char *ps[2] = { pb, pe }, *ns[2] = { nb, ne };
    for (int part = 0; part < 2; part++) {
        const char *p = ps[part], *q = ns[part];
        size_t i = 0;
        for (; p[i]; i++) {
            if (p[i] == '*') goto next_part;
            char a = (char)toupper((unsigned char)p[i]);
            char b = (char)toupper((unsigned char)q[i]);
            if (a == '?') { if (!q[i]) continue; continue; }
            if (a != b) return 0;
        }
        if (q[i]) return 0;
    next_part:;
    }
    return 1;
}

typedef struct { char name[13]; long size; } found_file;

static int list_dir(const char *dir, const char *pat, found_file *out, int max)
{
    int n = 0;
#ifdef _WIN32
    char spec[1100];
    snprintf(spec, sizeof spec, "%s/*", dir);
    struct _finddata_t fd;
    intptr_t h = _findfirst(spec, &fd);
    if (h == -1) return 0;
    do {
        if (fd.attrib & _A_SUBDIR) continue;
        if (strlen(fd.name) > 12 || !wild_match(pat, fd.name)) continue;
        if (n < max) {
            for (size_t i = 0; i <= strlen(fd.name); i++)
                out[n].name[i] = (char)toupper((unsigned char)fd.name[i]);
            out[n].size = (long)fd.size;
            n++;
        }
    } while (_findnext(h, &fd) == 0);
    _findclose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || strlen(e->d_name) > 12 || !wild_match(pat, e->d_name)) continue;
        if (n < max) {
            for (size_t i = 0; i <= strlen(e->d_name); i++)
                out[n].name[i] = (char)toupper((unsigned char)e->d_name[i]);
            char p[1100]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
            struct stat st; out[n].size = stat(p, &st) == 0 ? (long)st.st_size : 0;
            n++;
        }
    }
    closedir(d);
#endif
    return n;
}

static found_file g_find[256];
static int g_find_n, g_find_pos;

static int find_step(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (g_find_pos >= g_find_n) return 0;
    found_file *f = &g_find[g_find_pos++];
    uint32_t dta = m->dta;
    mem_write8(c, dta + 0x15, 0x20);
    mem_write16(c, dta + 0x16, 0x6000);       /* 12:00:00 */
    mem_write16(c, dta + 0x18, (uint16_t)(((1992 - 1980) << 9) | (6 << 5) | 5));
    mem_write16(c, dta + 0x1A, (uint16_t)(f->size & 0xFFFF));
    mem_write16(c, dta + 0x1C, (uint16_t)((f->size >> 16) & 0xFFFF));
    for (int i = 0; i < 13; i++) mem_write8(c, dta + 0x1E + (uint32_t)i, 0);
    for (int i = 0; f->name[i] && i < 12; i++) mem_write8(c, dta + 0x1E + (uint32_t)i, (uint8_t)f->name[i]);
    return 1;
}

static void find_first(machine_t *m, const char *pat)
{
    g_find_n = g_find_pos = 0;
    found_file tmp[256];
    int n1 = m->save_dir[0] ? list_dir(m->save_dir, pat, g_find, 256) : 0;
    int n2 = list_dir(m->data_dir, pat, tmp, 256);
    g_find_n = n1;
    for (int i = 0; i < n2 && g_find_n < 256; i++) {
        int dup = 0;
        for (int j = 0; j < n1; j++) if (!strcmp(g_find[j].name, tmp[i].name)) { dup = 1; break; }
        if (!dup) g_find[g_find_n++] = tmp[i];
    }
}

/* ===================================================================== */
/* Memory allocation                                                     */
/* ===================================================================== */
/* A bump allocator over the arena, with ownership - sufficient because of
 * how the shell uses memory: it stays resident at the bottom, loads overlays
 * above itself and shrinks them to fit, then EXECs one phase program at a
 * time into everything that is left. Each child is therefore the topmost
 * allocation, and freeing it on exit reclaims the space exactly as DOS
 * would. (The oracle's reasoning, kept with its allocator.) */

static uint16_t arena_top(const machine_t *m)
{
    uint16_t top = m->arena_base_seg;
    for (int i = 0; i < DOS_MAX_BLOCKS; i++)
        if (m->blocks[i].in_use) {
            uint16_t end = (uint16_t)(m->blocks[i].seg + m->blocks[i].paras);
            if (end > top) top = end;
        }
    return top;
}

static uint16_t arena_avail(const machine_t *m)
{
    uint16_t top = arena_top(m);
    return (uint16_t)((m->arena_end_seg > top + 1) ? m->arena_end_seg - top - 1 : 0);
}

static int dos_alloc(machine_t *m, uint16_t paras, uint16_t owner, uint16_t *out_seg)
{
    if (paras > arena_avail(m)) return 0;
    /* The top is computed BEFORE claiming a slot: a recycled slot still
     * carries its previous occupant's extent. */
    uint16_t top = arena_top(m);
    for (int i = 0; i < DOS_MAX_BLOCKS; i++) {
        if (!m->blocks[i].in_use) {
            m->blocks[i].seg = (uint16_t)(top + 1);
            m->blocks[i].paras = paras;
            m->blocks[i].owner = owner;
            m->blocks[i].in_use = 1;
            *out_seg = m->blocks[i].seg;
            return 1;
        }
    }
    return 0;
}

static dos_block *find_block(machine_t *m, uint16_t seg)
{
    for (int i = 0; i < DOS_MAX_BLOCKS; i++)
        if (m->blocks[i].in_use && m->blocks[i].seg == seg) return &m->blocks[i];
    return NULL;
}

static void free_blocks_of(machine_t *m, uint16_t owner)
{
    for (int i = 0; i < DOS_MAX_BLOCKS; i++)
        if (m->blocks[i].in_use && m->blocks[i].owner == owner)
            m->blocks[i].in_use = 0;
}

static uint16_t block_max_paras(const machine_t *m, const dos_block *b)
{
    uint16_t limit = m->arena_end_seg;
    for (int i = 0; i < DOS_MAX_BLOCKS; i++) {
        const dos_block *o = &m->blocks[i];
        if (o->in_use && o != b && o->seg > b->seg && (uint16_t)(o->seg - 1) < limit)
            limit = (uint16_t)(o->seg - 1);
    }
    return (uint16_t)(limit - b->seg);
}

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

static int kbd_pop(machine_t *m, uint16_t *key, int remove)
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
static void bios_key_irq(machine_t *m)
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
/* Program loading                                                       */
/* ===================================================================== */

typedef struct {
    uint16_t env_seg;
    uint16_t cmd_seg, cmd_off;
    uint16_t fcb1_seg, fcb1_off;
    uint16_t fcb2_seg, fcb2_off;
    uint16_t parent_psp;
} exec_params;

static void build_psp(machine_t *m, uint16_t psp, uint16_t top_seg,
                      const exec_params *ep, uint16_t ret_cs, uint16_t ret_ip)
{
    cpu_t *c = &m->cpu;
    for (int i = 0; i < 0x100; i++) mem_write8(c, phys(psp, (uint16_t)i), 0);
    mem_write8(c, phys(psp, 0x00), 0xCD);          /* INT 20h */
    mem_write8(c, phys(psp, 0x01), 0x20);
    mem_write16(c, phys(psp, 0x02), top_seg);
    mem_write16(c, phys(psp, 0x0A), ret_ip);        /* terminate address */
    mem_write16(c, phys(psp, 0x0C), ret_cs);
    mem_write16(c, phys(psp, 0x16), ep->parent_psp);
    for (int i = 0; i < 20; i++)                    /* job file table */
        mem_write8(c, phys(psp, (uint16_t)(0x18 + i)), (uint8_t)(i < 5 ? i : 0xFF));
    mem_write16(c, phys(psp, 0x2C), ep->env_seg);
    mem_write16(c, phys(psp, 0x32), 20);
    mem_write16(c, phys(psp, 0x34), 0x18);
    mem_write16(c, phys(psp, 0x36), psp);
    mem_write8(c, phys(psp, 0x50), 0xCD);          /* INT 21h ; RETF */
    mem_write8(c, phys(psp, 0x51), 0x21);
    mem_write8(c, phys(psp, 0x52), 0xCB);

    if (ep->fcb1_seg || ep->fcb1_off)
        for (int i = 0; i < 16; i++)
            mem_write8(c, phys(psp, (uint16_t)(0x5C + i)),
                       mem_read8(c, phys(ep->fcb1_seg, (uint16_t)(ep->fcb1_off + i))));
    if (ep->fcb2_seg || ep->fcb2_off)
        for (int i = 0; i < 16; i++)
            mem_write8(c, phys(psp, (uint16_t)(0x6C + i)),
                       mem_read8(c, phys(ep->fcb2_seg, (uint16_t)(ep->fcb2_off + i))));

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

static uint16_t load_program(machine_t *m, const char *name, const exec_params *ep,
                             uint16_t ret_cs, uint16_t ret_ip, uint16_t *psp_out)
{
    cpu_t *c = &m->cpu;
    long fsz = 0;
    uint8_t *raw = read_whole(m, dos_basename(name), &fsz);
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

    /* DOS gives a program the largest free block unless maxalloc asks for
     * less. Every phase program here asks for everything. */
    uint16_t body_paras = (uint16_t)((body + 15) / 16);
    uint16_t need = (uint16_t)(0x10 + body_paras + (is_mz ? minalloc : 0x10));
    uint16_t avail = arena_avail(m);
    if (need > avail) { free(raw); return ERR_NO_MEMORY; }
    uint16_t want = avail;
    if (is_mz && maxalloc != 0xFFFF) {
        uint32_t cap = 0x10u + body_paras + maxalloc;
        if (cap < want) want = (uint16_t)cap;
        if (want < need) want = need;
    }

    uint16_t psp = 0;
    if (!dos_alloc(m, want, 0, &psp)) { free(raw); return ERR_NO_MEMORY; }
    find_block(m, psp)->owner = psp;
    uint16_t top_seg = (uint16_t)(psp + want);
    uint16_t load_seg = (uint16_t)(psp + 0x10);

    build_psp(m, psp, top_seg, ep, ret_cs, ret_ip);

    /* Announced before the image is written, so whatever this load
     * replaces is forgotten before its bytes change. */
    if (m->hooks.module_load)
        m->hooks.module_load(m->hooks.user, m, dos_basename(name), raw, (size_t)fsz,
                             is_mz ? MODLOAD_EXEC : MODLOAD_COM,
                             is_mz ? load_seg : psp, is_mz ? load_seg : psp);

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
        /* COM: image at PSP:0100, all segments equal, a zero word pushed
         * for the RET-to-PSP convention. */
        guest_write(c, phys(psp, 0x100), raw, body);
        c->seg[S_SS] = psp;
        c->r[R_SP]   = 0xFFFE;
        seg_write16(c, psp, 0xFFFE, 0);
        c->seg[S_CS] = psp;
        c->ip        = 0x100;
    }

    c->seg[S_DS] = c->seg[S_ES] = psp;
    c->r[R_AX] = 0;
    c->r[R_BX] = c->r[R_CX] = c->r[R_DX] = 0;
    c->r[R_SI] = c->r[R_DI] = c->r[R_BP] = 0;
    c->flags = (uint16_t)(cpu_flags_fixed(c) | F_IF);
    cpu_irq_state_changed(c);

    dos_log(m, "[exec] %-14s %s  psp=%04X load=%04X..%04X (%u paras)  entry %04X:%04X @%llu\n",
            name, is_mz ? "MZ " : "COM", psp, load_seg, top_seg, want,
            c->seg[S_CS], c->ip, (unsigned long long)c->icount);
    free(raw);
    *psp_out = psp;
    return 0;
}

/* INT 21h/4B03: an MZ image at a caller-chosen segment, no PSP, no
 * allocation, no transfer of control. How the shell brings in MISC.EXE,
 * the graphics driver and the sound driver. */
static uint16_t load_overlay(machine_t *m, const char *name, uint16_t load_seg,
                             uint16_t reloc_factor)
{
    cpu_t *c = &m->cpu;
    long fsz = 0;
    uint8_t *raw = read_whole(m, dos_basename(name), &fsz);
    if (!raw) return ERR_FILE_NOT_FOUND;

    uint32_t body;
    unsigned nreloc = 0;
    if (m->hooks.module_load)
        m->hooks.module_load(m->hooks.user, m, dos_basename(name), raw, (size_t)fsz,
                             MODLOAD_OVERLAY, load_seg, reloc_factor);
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
    dos_log(m, "[overlay] %-12s -> %04X (%u bytes, %u relocations, factor %04X) @%llu\n",
            name, load_seg, body, nreloc, reloc_factor, (unsigned long long)c->icount);
    free(raw);
    return 0;
}

static int terminate(machine_t *m, uint8_t code)
{
    cpu_t *c = &m->cpu;
    uint16_t psp = current_psp(m);
    close_files_of(m, psp);
    free_blocks_of(m, psp);

    if (m->nproc <= 1) {
        dos_log(m, "[exit] %s terminated with code %d (root)\n", dos_current_program(m), code);
        m->exited = 1;
        m->exit_code = code;
        c->stop_at = 0;
        return 1;
    }

    dos_proc *p = &m->procs[m->nproc - 1];
    dos_log(m, "[exit] %s terminated with code %d -> back to %s @%llu\n",
            p->name, code, m->procs[m->nproc - 2].name, (unsigned long long)c->icount);
    m->last_child_exit = code;

    /* Resume the parent where its INT 21h left off. DOS guarantees only
     * CS:IP; restoring the rest is harmless, as the oracle found. */
    memcpy(c->r, p->r, sizeof(c->r));
    memcpy(c->seg, p->seg, sizeof(c->seg));
    c->seg[S_CS] = p->ret_cs;
    c->ip = p->ret_ip;
    c->flags = (uint16_t)((p->flags | F_IF) & (uint16_t)(0xFFFFu ^ F_CF));
    c->r[R_AX] = 0;
    m->nproc--;
    cpu_irq_state_changed(c);
    return 1;
}

/* ===================================================================== */
/* Clock                                                                 */
/* ===================================================================== */

/* The wall-clock time at boot plus the emulated time since, broken down.
 * boot_time_us is LOCAL time counted as if it were UTC (what a PC's
 * real-time clock holds), so it is broken down with gmtime: the same
 * recorded boot time means the same DOS clock in every time zone. */
static void wall_clock(machine_t *m, struct tm *out, unsigned *centis)
{
    uint64_t us = m->boot_time_us + machine_now_us(m);
    time_t secs = (time_t)(us / 1000000ull);
    struct tm *t = gmtime(&secs);
    if (t) *out = *t; else memset(out, 0, sizeof *out);
    if (centis) *centis = (unsigned)((us / 10000ull) % 100ull);
}

static uint8_t bcd(unsigned v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

/* ===================================================================== */
/* INT 21h                                                               */
/* ===================================================================== */

static int int21(machine_t *m)
{
    cpu_t *c = &m->cpu;
    uint8_t ah = (uint8_t)(c->r[R_AX] >> 8);
    uint8_t al = (uint8_t)(c->r[R_AX] & 0xFF);
    uint16_t me = current_psp(m);

    switch (ah) {

    case 0x00:     /* terminate, old style */
        return terminate(m, 0);

    case 0x01:     /* read char with echo */
    case 0x07:     /* direct read, no echo, no Ctrl-C */
    case 0x08: {   /* read, no echo */
        uint16_t key;
        if (!kbd_pop(m, &key, 1)) return bios_wait(c);
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | (key & 0xFF));
        if (ah == 0x01) { char ch = (char)key; console_text(m, &ch, 1); }
        return 1;
    }

    case 0x02: {   /* write char */
        char ch = (char)(c->r[R_DX] & 0xFF);
        console_text(m, &ch, 1);
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | (uint8_t)ch);
        return 1;
    }

    case 0x06: {   /* direct console I/O: DL=FF reads, else writes DL */
        uint16_t key;
        if ((c->r[R_DX] & 0xFF) == 0xFF) {
            if (kbd_pop(m, &key, 1)) {
                c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | (key & 0xFF));
                c->flags = (uint16_t)(c->flags & (uint16_t)(0xFFFFu ^ F_ZF));
            } else {
                c->r[R_AX] = (uint16_t)(c->r[R_AX] & 0xFF00);
                c->flags |= F_ZF;
            }
        } else {
            char ch = (char)(c->r[R_DX] & 0xFF);
            console_text(m, &ch, 1);
        }
        return 1;
    }

    case 0x09: {   /* print a '$'-terminated string */
        uint16_t off = c->r[R_DX];
        char buf[1025]; int n = 0;
        for (int i = 0; i < 1024; i++) {
            uint8_t ch = mem_read8(c, phys(c->seg[S_DS], (uint16_t)(off + i)));
            if (ch == '$') break;
            buf[n++] = (char)ch;
        }
        console_text(m, buf, (size_t)n);
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | '$');
        return 1;
    }

    case 0x0B: {   /* stdin status: FF if a key is waiting */
        uint16_t key;
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | (kbd_pop(m, &key, 0) ? 0xFF : 0x00));
        return 1;
    }

    case 0x0C: {   /* flush input, then perform AL's function */
        uint16_t key;
        if (al == 0x01 || al == 0x06 || al == 0x07 || al == 0x08 || al == 0x0A) {
            /* Flush only on the first entry: a wait re-enters this call. */
            if (c->halted != 2) while (kbd_pop(m, &key, 1)) {}
            c->r[R_AX] = (uint16_t)((uint16_t)(al << 8) | al);
            return int21(m);
        }
        while (kbd_pop(m, &key, 1)) {}
        return 1;
    }

    case 0x0D:     /* disk reset */
        return 1;

    case 0x0E:     /* select disk: report drives A..C */
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | 3);
        return 1;

    case 0x11: case 0x12:   /* FCB find: nothing */
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | 0xFF);
        return 1;

    case 0x19:     /* current drive: C */
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | 2);
        return 1;

    case 0x1A:     /* set DTA */
        m->dta = phys(c->seg[S_DS], c->r[R_DX]);
        return 1;

    case 0x25: {   /* set interrupt vector */
        uint32_t v = (uint32_t)al * 4u;
        mem_write16(c, v, c->r[R_DX]);
        mem_write16(c, v + 2, c->seg[S_DS]);
        return 1;
    }

    case 0x2A: {   /* get date */
        struct tm t;
        wall_clock(m, &t, NULL);
        c->r[R_CX] = (uint16_t)(t.tm_year + 1900);
        c->r[R_DX] = (uint16_t)(((t.tm_mon + 1) << 8) | t.tm_mday);
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | (uint8_t)t.tm_wday);
        return 1;
    }

    case 0x2C: {   /* get time */
        struct tm t;
        unsigned cs;
        wall_clock(m, &t, &cs);
        c->r[R_CX] = (uint16_t)((t.tm_hour << 8) | t.tm_min);
        c->r[R_DX] = (uint16_t)((t.tm_sec << 8) | cs);
        return 1;
    }

    case 0x2B: case 0x2D:   /* set date / time: accepted, ignored */
        c->r[R_AX] = (uint16_t)(c->r[R_AX] & 0xFF00);
        return 1;

    case 0x2F:     /* get DTA */
        c->seg[S_ES] = (uint16_t)(m->dta >> 4);
        c->r[R_BX] = (uint16_t)(m->dta & 0xF);
        return 1;

    case 0x30:     /* DOS version: 5.00 */
        c->r[R_AX] = 0x0005;
        c->r[R_BX] = 0;
        c->r[R_CX] = 0;
        return 1;

    case 0x33:     /* Ctrl-Break flag */
        c->r[R_DX] = (uint16_t)(c->r[R_DX] & 0xFF00);
        return 1;

    case 0x35: {   /* get interrupt vector */
        uint32_t v = (uint32_t)al * 4u;
        c->r[R_BX] = mem_read16(c, v);
        c->seg[S_ES] = mem_read16(c, v + 2);
        return 1;
    }

    case 0x36:     /* free disk space: plenty */
        c->r[R_AX] = 64;      /* sectors per cluster */
        c->r[R_BX] = 0x7FFF;  /* free clusters */
        c->r[R_CX] = 512;
        c->r[R_DX] = 0xFFFF;
        return 1;

    case 0x3B:     /* chdir */
        ok(c);
        return 1;

    case 0x3C:     /* create/truncate */
    case 0x3D: {   /* open */
        char dp[520], hp[1100];
        guest_str(c, c->seg[S_DS], c->r[R_DX], dp, sizeof(dp));
        const char *base = dos_basename(dp);
        int h = alloc_handle(m);
        if (h < 0) { fail(c, ERR_TOO_MANY_OPEN); return 1; }
        FILE *f;
        if (ah == 0x3C) f = open_create(m, base, hp, sizeof hp);
        else if ((al & 7) == 0) f = open_read(m, base, hp, sizeof hp);
        else f = open_rw(m, base, hp, sizeof hp);
        if (!f) {
            dos_log(m, "[file] %s '%s' failed (not found) in %s\n",
                    ah == 0x3C ? "create" : "open", dp, dos_current_program(m));
            fail(c, ERR_FILE_NOT_FOUND);
            return 1;
        }
        m->files[h].fp = f;
        m->files[h].in_use = 1;
        m->files[h].is_device = 0;
        m->files[h].owner = me;
        snprintf(m->files[h].path, sizeof(m->files[h].path), "%s", hp);
        dos_log(m, "[file] %s '%s' -> %d @%llu %s\n", ah == 0x3C ? "create" : "open",
                dp, h, (unsigned long long)c->icount, dos_current_program(m));
        c->r[R_AX] = (uint16_t)h;
        ok(c);
        return 1;
    }

    case 0x3E: {   /* close */
        uint16_t h = c->r[R_BX];
        if (h < 5) { ok(c); return 1; }
        if (h >= DOS_MAX_FILES || !m->files[h].in_use) { fail(c, ERR_BAD_HANDLE); return 1; }
        fclose(m->files[h].fp);
        m->files[h].in_use = 0;
        m->files[h].fp = NULL;
        ok(c);
        return 1;
    }

    case 0x3F: {   /* read */
        uint16_t h = c->r[R_BX], n = c->r[R_CX];
        if (h < 5) { c->r[R_AX] = 0; ok(c); return 1; }   /* stdin: EOF */
        if (h >= DOS_MAX_FILES || !m->files[h].in_use) { fail(c, ERR_BAD_HANDLE); return 1; }
        uint8_t *tmp = (uint8_t *)malloc(n ? n : 1);
        size_t got = n ? fread(tmp, 1, n, m->files[h].fp) : 0;
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
            if (h == 1 || h == 2) console_text(m, buf, (size_t)k);
            c->r[R_AX] = n;
            ok(c);
            return 1;
        }
        if (h >= DOS_MAX_FILES || !m->files[h].in_use) { fail(c, ERR_BAD_HANDLE); return 1; }
        uint8_t *tmp = (uint8_t *)malloc(n ? n : 1);
        for (uint16_t i = 0; i < n; i++)
            tmp[i] = mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_DX] + i)));
        size_t put;
        for (uint16_t i = 0; i < n; i++) machine_io_note(m, 0x400000000ull | tmp[i], (uint64_t)h << 16 | i);
        if (n) put = fwrite(tmp, 1, n, m->files[h].fp);
        else {
            /* A zero-length write truncates the file at the position. */
            long pos = ftell(m->files[h].fp);
#ifdef _WIN32
            fflush(m->files[h].fp);
            _chsize(_fileno(m->files[h].fp), pos);
#endif
            (void)pos;
            put = 0;
        }
        fflush(m->files[h].fp);
        free(tmp);
        c->r[R_AX] = (uint16_t)put;
        ok(c);
        return 1;
    }

    case 0x41: {   /* unlink: only ever in the save directory */
        char dp[520], hp[1100];
        guest_str(c, c->seg[S_DS], c->r[R_DX], dp, sizeof(dp));
        if (!file_exists_ci(write_dir(m), dos_basename(dp), hp, sizeof hp) || remove(hp) != 0) {
            fail(c, ERR_FILE_NOT_FOUND);
            return 1;
        }
        ok(c);
        return 1;
    }

    case 0x42: {   /* lseek */
        uint16_t h = c->r[R_BX];
        if (h >= DOS_MAX_FILES || !m->files[h].in_use || h < 5) { fail(c, ERR_BAD_HANDLE); return 1; }
        long off = (long)(int32_t)(((uint32_t)c->r[R_CX] << 16) | c->r[R_DX]);
        int whence = (al == 1) ? SEEK_CUR : (al == 2) ? SEEK_END : SEEK_SET;
        if (fseek(m->files[h].fp, off, whence) != 0) { fail(c, ERR_BAD_FUNCTION); return 1; }
        long pos = ftell(m->files[h].fp);
        c->r[R_AX] = (uint16_t)(pos & 0xFFFF);
        c->r[R_DX] = (uint16_t)((pos >> 16) & 0xFFFF);
        ok(c);
        return 1;
    }

    case 0x43: {   /* get/set file attributes */
        char dp[520];
        guest_str(c, c->seg[S_DS], c->r[R_DX], dp, sizeof(dp));
        FILE *f = open_read(m, dos_basename(dp), NULL, 0);
        if (!f) { fail(c, ERR_FILE_NOT_FOUND); return 1; }
        fclose(f);
        if (al == 0) c->r[R_CX] = 0x20;   /* archive */
        ok(c);
        return 1;
    }

    case 0x44:     /* ioctl */
        if (al == 0) {
            uint16_t h = c->r[R_BX];
            c->r[R_DX] = (h < 5) ? 0x0080 : 0x0002;   /* bit 7: character device */
        } else if (al == 8) {
            c->r[R_AX] = 1;                         /* fixed disk */
        }
        ok(c);
        return 1;

    case 0x47:     /* getcwd: root */
        mem_write8(c, phys(c->seg[S_DS], c->r[R_SI]), 0);
        ok(c);
        return 1;

    case 0x48: {   /* allocate memory */
        uint16_t seg = 0;
        if (!dos_alloc(m, c->r[R_BX], me, &seg)) {
            c->r[R_BX] = arena_avail(m);
            fail(c, ERR_NO_MEMORY);
            return 1;
        }
        c->r[R_AX] = seg;
        ok(c);
        return 1;
    }

    case 0x49: {   /* free memory */
        dos_block *b = find_block(m, c->seg[S_ES]);
        if (b) b->in_use = 0;
        ok(c);
        return 1;
    }

    case 0x4A: {   /* resize a memory block */
        dos_block *b = find_block(m, c->seg[S_ES]);
        if (!b) { fail(c, ERR_BAD_FUNCTION); return 1; }
        uint16_t want = c->r[R_BX], mx = block_max_paras(m, b);
        if (want > mx) {
            c->r[R_BX] = mx;
            fail(c, ERR_NO_MEMORY);
            return 1;
        }
        b->paras = want;
        ok(c);
        return 1;
    }

    case 0x4B: {   /* EXEC */
        char dp[520];
        guest_str(c, c->seg[S_DS], c->r[R_DX], dp, sizeof(dp));
        uint16_t pb_seg = c->seg[S_ES], pb = c->r[R_BX];

        if (al == 0x03) {           /* load overlay */
            uint16_t lseg = seg_read16(c, pb_seg, pb);
            uint16_t fac  = seg_read16(c, pb_seg, (uint16_t)(pb + 2));
            uint16_t err = load_overlay(m, dp, lseg, fac);
            if (err) { dos_log(m, "[overlay] %s FAILED (%u)\n", dp, err); fail(c, err); return 1; }
            c->r[R_AX] = 0;
            ok(c);
            return 1;
        }
        if (al != 0x00) { fail(c, ERR_BAD_FUNCTION); return 1; }
        if (m->nproc >= DOS_MAX_PROCS) { fail(c, ERR_NO_MEMORY); return 1; }

        exec_params ep;
        memset(&ep, 0, sizeof(ep));
        ep.env_seg  = seg_read16(c, pb_seg, pb);
        if (!ep.env_seg) ep.env_seg = m->env_seg;
        ep.cmd_off  = seg_read16(c, pb_seg, (uint16_t)(pb + 2));
        ep.cmd_seg  = seg_read16(c, pb_seg, (uint16_t)(pb + 4));
        ep.fcb1_off = seg_read16(c, pb_seg, (uint16_t)(pb + 6));
        ep.fcb1_seg = seg_read16(c, pb_seg, (uint16_t)(pb + 8));
        ep.fcb2_off = seg_read16(c, pb_seg, (uint16_t)(pb + 10));
        ep.fcb2_seg = seg_read16(c, pb_seg, (uint16_t)(pb + 12));
        ep.parent_psp = me;

        /* Snapshot the parent. Nothing below touches the CPU until the load
         * has definitely succeeded, so a failure leaves the caller intact. */
        dos_proc *np = &m->procs[m->nproc];
        memset(np, 0, sizeof(*np));
        memcpy(np->r, c->r, sizeof(np->r));
        memcpy(np->seg, c->seg, sizeof(np->seg));
        np->ret_cs = c->seg[S_CS];
        np->ret_ip = c->ip;               /* already past the INT 21h */
        np->flags = c->flags;
        snprintf(np->name, sizeof(np->name), "%s", dos_basename(dp));
        for (char *p = np->name; *p; p++) *p = (char)toupper((unsigned char)*p);

        uint16_t psp = 0;
        m->nproc++;                       /* so the load is attributed to the child */
        uint16_t err = load_program(m, dp, &ep, np->ret_cs, np->ret_ip, &psp);
        if (err) {
            m->nproc--;
            dos_log(m, "[exec] %s FAILED (%u)\n", dp, err);
            fail(c, err);
            return 1;
        }
        np->psp_seg = psp;
        np->start_icount = c->icount;
        return 1;                         /* control is now in the child */
    }

    case 0x4C:     /* terminate with code */
        return terminate(m, al);

    case 0x4D:     /* child's exit code */
        c->r[R_AX] = m->last_child_exit;
        ok(c);
        return 1;

    case 0x4E: {   /* find first */
        char dp[520];
        guest_str(c, c->seg[S_DS], c->r[R_DX], dp, sizeof(dp));
        find_first(m, dos_basename(dp));
        if (!find_step(m)) { fail(c, ERR_FILE_NOT_FOUND); return 1; }
        ok(c);
        return 1;
    }
    case 0x4F:     /* find next */
        if (!find_step(m)) { fail(c, ERR_NO_MORE_FILES); return 1; }
        ok(c);
        return 1;

    case 0x50:     /* set PSP */
        if (m->nproc) m->procs[m->nproc - 1].psp_seg = c->r[R_BX];
        return 1;
    case 0x51: case 0x62:   /* get PSP */
        c->r[R_BX] = me;
        return 1;

    case 0x56: {   /* rename, inside the save directory */
        char a[520], b[520], ha[1100], hb[1100];
        guest_str(c, c->seg[S_DS], c->r[R_DX], a, sizeof a);
        guest_str(c, c->seg[S_ES], c->r[R_DI], b, sizeof b);
        if (!file_exists_ci(write_dir(m), dos_basename(a), ha, sizeof ha)) { fail(c, ERR_FILE_NOT_FOUND); return 1; }
        snprintf(hb, sizeof hb, "%s/%s", write_dir(m), dos_basename(b));
        if (rename(ha, hb) != 0) { fail(c, ERR_ACCESS_DENIED); return 1; }
        ok(c);
        return 1;
    }

    case 0x57:     /* file date and time */
        if (al == 0) { c->r[R_CX] = 0x6000; c->r[R_DX] = (uint16_t)(((1992 - 1980) << 9) | (6 << 5) | 5); }
        ok(c);
        return 1;

    case 0x58:     /* allocation strategy */
        if (al == 0) c->r[R_AX] = 0;
        ok(c);
        return 1;

    default: {
        static unsigned seen[256];
        if (seen[ah]++ < 3)
            dos_log(m, "[int21] UNHANDLED AH=%02X AL=%02X from %s at %04X:%04X\n",
                    ah, al, dos_current_program(m), c->op_cs, c->op_ip);
        fail(c, ERR_BAD_FUNCTION);
        return 1;
    }
    }
}

/* ===================================================================== */
/* INT 10h                                                               */
/* ===================================================================== */

static int int10(machine_t *m)
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
            const unsigned reg = c->r[R_BX] & 0xFF;
            m->dac[reg * 3 + 0] = (uint8_t)((c->r[R_DX] >> 8) & 0x3F);
            m->dac[reg * 3 + 1] = (uint8_t)((c->r[R_CX] >> 8) & 0x3F);
            m->dac[reg * 3 + 2] = (uint8_t)(c->r[R_CX] & 0x3F);
            break;
        }
        case 0x12: {
            unsigned reg = c->r[R_BX] & 0xFF;
            uint16_t off = c->r[R_DX];
            for (unsigned i = 0; i < c->r[R_CX] && reg < 256; i++, reg++) {
                for (int k = 0; k < 3; k++)
                    m->dac[reg * 3 + k] = (uint8_t)(mem_read8(c, phys(c->seg[S_ES], (uint16_t)(off + k))) & 0x3F);
                off = (uint16_t)(off + 3);
            }
            break;
        }
        case 0x15: {
            const unsigned reg = c->r[R_BX] & 0xFF;
            c->r[R_DX] = (uint16_t)((c->r[R_DX] & 0x00FF) | (m->dac[reg * 3] << 8));
            c->r[R_CX] = (uint16_t)((m->dac[reg * 3 + 1] << 8) | m->dac[reg * 3 + 2]);
            break;
        }
        case 0x17: {
            unsigned reg = c->r[R_BX] & 0xFF;
            uint16_t off = c->r[R_DX];
            for (unsigned i = 0; i < c->r[R_CX] && reg < 256; i++, reg++) {
                for (int k = 0; k < 3; k++)
                    mem_write8(c, phys(c->seg[S_ES], (uint16_t)(off + k)), m->dac[reg * 3 + k]);
                off = (uint16_t)(off + 3);
            }
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
            c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | 0x1A);
            c->r[R_BX] = 0x0008;
        }
        break;
    default: break;
    }
    return 1;
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

static int int16(machine_t *m)
{
    cpu_t *c = &m->cpu;
    uint8_t ah = (uint8_t)(c->r[R_AX] >> 8);
    uint16_t key;
    if (m->log && getenv("F117R_TRACE_KEYS") && ah != 0x01 && ah != 0x11) {
        uint16_t peek = 0;
        int have = kbd_pop(m, &peek, 0);
        dos_log(m, "[int16] AH=%02X next=%04X%s @%llu (%s)\n", ah, peek, have ? "" : " (empty)",
                (unsigned long long)c->icount, dos_current_program(m));
    }
    switch (ah) {
    case 0x00:                               /* read key, waiting */
        for (;;) {
            if (!kbd_pop(m, &key, 1)) return bios_wait(c);
            if (legacy_ok(key)) { c->r[R_AX] = legacy(key); return 1; }
        }
    case 0x10:
        if (!kbd_pop(m, &key, 1)) return bios_wait(c);
        c->r[R_AX] = key;
        return 1;
    case 0x01:                               /* key available? */
        while (kbd_pop(m, &key, 0) && !legacy_ok(key)) kbd_pop(m, &key, 1);
        if (kbd_pop(m, &key, 0)) {
            c->r[R_AX] = legacy(key);
            c->flags = (uint16_t)(c->flags & (uint16_t)(0xFFFFu ^ F_ZF));
        } else {
            c->flags |= F_ZF;
        }
        return 1;
    case 0x11:
        if (kbd_pop(m, &key, 0)) {
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

/* ===================================================================== */
/* INT 1Ah                                                               */
/* ===================================================================== */

static int int1a(machine_t *m)
{
    cpu_t *c = &m->cpu;
    uint8_t ah = (uint8_t)(c->r[R_AX] >> 8);
    switch (ah) {
    case 0x00:                               /* read the tick count */
        c->r[R_DX] = mem_read16(c, BDA_TICKS);
        c->r[R_CX] = mem_read16(c, BDA_TICKS + 2);
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | mem_read8(c, BDA_MIDNIGHT));
        mem_write8(c, BDA_MIDNIGHT, 0);
        break;
    case 0x02: {                             /* RTC time */
        struct tm t;
        wall_clock(m, &t, NULL);
        c->r[R_CX] = (uint16_t)((bcd((unsigned)t.tm_hour) << 8) | bcd((unsigned)t.tm_min));
        c->r[R_DX] = (uint16_t)(bcd((unsigned)t.tm_sec) << 8);
        ok(c);
        break;
    }
    case 0x04: {                             /* RTC date */
        struct tm t;
        wall_clock(m, &t, NULL);
        unsigned y = (unsigned)t.tm_year + 1900u;
        c->r[R_CX] = (uint16_t)((bcd(y / 100) << 8) | bcd(y % 100));
        c->r[R_DX] = (uint16_t)((bcd((unsigned)t.tm_mon + 1) << 8) | bcd((unsigned)t.tm_mday));
        ok(c);
        break;
    }
    default: break;
    }
    return 1;
}

/* ===================================================================== */
/* INT 33h - the mouse driver                                            */
/* ===================================================================== */

static const uint16_t DEFAULT_MOUSE_MASKS[32] = {
    0x3FFF, 0x1FFF, 0x0FFF, 0x07FF, 0x03FF, 0x01FF, 0x00FF, 0x007F,
    0x003F, 0x001F, 0x01FF, 0x10FF, 0x30FF, 0xF87F, 0xF87F, 0xFC3F,
    0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
    0x7F80, 0x7C00, 0x6C00, 0x4600, 0x0600, 0x0300, 0x0300, 0x0000 };

static void mouse_reset(machine_t *m)
{
    m->mouse_hidden = 1;
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

static int int33(machine_t *m)
{
    cpu_t *c = &m->cpu;
    uint16_t fn = c->r[R_AX];
    if (!m->mouse_present) {
        if (fn == 0x0000 || fn == 0x0021) { c->r[R_AX] = 0; c->r[R_BX] = 0; }
        return 1;
    }
    switch (fn) {
    case 0x0000: case 0x0021:                 /* reset: present, two buttons */
        mouse_reset(m);
        c->r[R_AX] = 0xFFFF;
        c->r[R_BX] = 2;
        break;
    case 0x0001: if (m->mouse_hidden > 0) m->mouse_hidden--; break;
    case 0x0002: m->mouse_hidden++; break;
    case 0x0003:
        c->r[R_BX] = (uint16_t)m->mouse_buttons;
        c->r[R_CX] = (uint16_t)m->mouse_x;
        c->r[R_DX] = (uint16_t)m->mouse_y;
        break;
    case 0x0004: {
        int x = (int16_t)c->r[R_CX], y = (int16_t)c->r[R_DX];
        if (x < m->mouse_xmin) x = m->mouse_xmin;
        if (x > m->mouse_xmax) x = m->mouse_xmax;
        if (y < m->mouse_ymin) y = m->mouse_ymin;
        if (y > m->mouse_ymax) y = m->mouse_ymax;
        m->mouse_x = x; m->mouse_y = y;
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

/* ===================================================================== */
/* The interrupt hook                                                    */
/* ===================================================================== */

/* The services reached through the vector table. Each vector starts out
 * pointing at a stub `int (0xE0+k) ; retf 2` at 0060:0040+8k. A direct
 * INT n while the vector is still that stub is serviced in place; once a
 * program has hooked the vector its handler runs, and if it chains to the
 * old vector the stub's alias interrupt is serviced instead. */
static const uint8_t SERVICES[] = { 0x10, 0x16, 0x1A, 0x21, 0x33 };
#define SERVICE_STUB(k) ((uint16_t)(0x40 + 8 * (k)))

static int service(machine_t *m, uint8_t vec)
{
    switch (vec) {
    case 0x21: return int21(m);
    case 0x10: return int10(m);
    case 0x16: return int16(m);
    case 0x1A: return int1a(m);
    case 0x33: return int33(m);
    default:   return 0;
    }
}

int dos_int_hook(cpu_t *c, uint8_t vec)
{
    machine_t *m = machine_of(c);
    if (!m) return 0;
    for (unsigned k = 0; k < sizeof SERVICES; k++) {
        if (vec == SERVICES[k]) {
            if (mem_read16(c, (uint32_t)vec * 4) != SERVICE_STUB(k) ||
                mem_read16(c, (uint32_t)vec * 4 + 2) != m->iret_seg)
                return 0;                      /* the program's own handler */
            return service(m, vec);
        }
        if (vec == 0xE0 + k) {
            /* Chained to from a program's handler: the caller's frame is at
             * SS:SP, and the stub returns with RETF 2, so the interrupt
             * enable, trap and direction flags come back from that frame
             * as an IRET would have brought them. */
            int r = service(m, SERVICES[k]);
            if (c->halted != 2) {
                uint16_t fl = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_SP] + 4));
                const uint16_t keep = F_IF | F_TF | F_DF;
                c->flags = (uint16_t)((c->flags & (uint16_t)~keep) | (fl & keep));
                cpu_irq_state_changed(c);
            }
            return r;
        }
    }
    switch (vec) {
    case 0xF8: {                              /* the BIOS INT 8 stub's tick step */
        uint32_t t = mem_read16(c, BDA_TICKS) | ((uint32_t)mem_read16(c, BDA_TICKS + 2) << 16);
        t++;
        if (t >= 0x1800B0u) {
            t = 0;
            mem_write8(c, BDA_MIDNIGHT, (uint8_t)(mem_read8(c, BDA_MIDNIGHT) + 1));
        }
        mem_write16(c, BDA_TICKS, (uint16_t)t);
        mem_write16(c, BDA_TICKS + 2, (uint16_t)(t >> 16));
        cpu_interrupt(c, 0x1C);                /* the user timer hook */
        return 1;
    }
    case 0xF9: bios_key_irq(m); return 1;      /* the INT 9 stub's translation step */
    case 0x20: return terminate(m, 0);
    case 0x11: c->r[R_AX] = mem_read16(c, BDA_EQUIPMENT); return 1;
    case 0x12: c->r[R_AX] = mem_read16(c, BDA_MEM_KB); return 1;
    case 0x15:                                 /* system services: none */
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0x00FF) | 0x8600);
        c->flags |= F_CF;
        return 1;
    default: return 0;                         /* the guest's vector, or the IRET stub */
    }
}

/* ===================================================================== */
/* Boot                                                                  */
/* ===================================================================== */

static void init_bios_data_area(machine_t *m)
{
    cpu_t *c = &m->cpu;
    mem_write16(c, BDA_EQUIPMENT, 0x0026);       /* mouse port, 80x25 colour, no floppy maths */
    mem_write16(c, BDA_MEM_KB, 640);
    mem_write16(c, BDA_KBD_HEAD, BDA_KBD_BUF - 0x400);
    mem_write16(c, BDA_KBD_TAIL, BDA_KBD_BUF - 0x400);
    mem_write16(c, BDA_KBD_START, BDA_KBD_BUF - 0x400);
    mem_write16(c, BDA_KBD_END, BDA_KBD_BUF_END - 0x400);
    mem_write8 (c, BDA_SHIFT, 0x20);             /* NumLock on, as a BIOS boots */
    mem_write8 (c, BDA_VIDEO_MODE, 0x03);
    mem_write16(c, BDA_VIDEO_COLS, 80);
    mem_write16(c, BDA_PAGE_SIZE, 4000);
    mem_write16(c, BDA_CURSOR_TYPE, 0x0607);
    mem_write16(c, BDA_CRTC_BASE, 0x03D4);
    mem_write8 (c, BDA_ROWS_M1, 24);
    mem_write16(c, BDA_CHAR_HEIGHT, 16);
    mem_write8 (c, 0x487, 0x60);
    mem_write8 (c, 0x488, 0x09);
    mem_write8 (c, 0x489, 0x51);
    mem_write8 (c, 0x48A, 0x08);
    mem_write8 (c, BDA_KBD_FLAGS3, 0x10);        /* 101-key keyboard */

    /* The ROM starts the tick count at the time of day it reads from the
     * real-time clock. */
    struct tm t;
    unsigned cs;
    wall_clock(m, &t, &cs);
    uint64_t secs = (uint64_t)t.tm_hour * 3600u + (uint64_t)t.tm_min * 60u + (uint64_t)t.tm_sec;
    uint32_t ticks = (uint32_t)((secs * 1000u + cs * 10u) * 1193182ull / 65536ull / 1000u);
    mem_write16(c, BDA_TICKS, (uint16_t)ticks);
    mem_write16(c, BDA_TICKS + 2, (uint16_t)(ticks >> 16));
}

int dos_boot(machine_t *m, const char *program)
{
    cpu_t *c = &m->cpu;
    const uint16_t iret_seg = 0x0060;
    m->iret_seg = iret_seg;

    /* Every vector points at a stand-in handler that acknowledges the
     * interrupt at the PIC and returns, so an interrupt nobody handles is a
     * no-op rather than a jump into zeros. The game chains its timer
     * handler onto whatever was there. */
    static const uint8_t stub[] = { 0x50, 0xB0, 0x20, 0xE6, 0x20, 0x58, 0xCF };
    for (size_t i = 0; i < sizeof stub; i++) mem_write8(c, phys(iret_seg, (uint16_t)i), stub[i]);
    /* A plain IRET for the software interrupts (no EOI). */
    mem_write8(c, phys(iret_seg, 0x08), 0xCF);
    for (int v = 0; v < 256; v++) {
        int hw = (v >= 8 && v <= 0x0F);
        mem_write16(c, (uint32_t)v * 4, hw ? 0x0000 : 0x0008);
        mem_write16(c, (uint32_t)v * 4 + 2, iret_seg);
    }
    /* INT 9: read the scancode, let the ROM translation (INT F9h) see it,
     * acknowledge. A game handler that chains here still gets its keys
     * into the BIOS buffer. */
    static const uint8_t stub9[] = { 0x50, 0xE4, 0x60, 0xCD, 0xF9, 0xB0, 0x20, 0xE6, 0x20, 0x58, 0xCF };
    for (size_t i = 0; i < sizeof stub9; i++) mem_write8(c, phys(iret_seg, (uint16_t)(0x10 + i)), stub9[i]);
    mem_write16(c, 9 * 4, 0x10);
    /* INT 8: count the tick (INT F8h, which also calls INT 1Ch), then EOI. */
    static const uint8_t stub8[] = { 0x50, 0xCD, 0xF8, 0xB0, 0x20, 0xE6, 0x20, 0x58, 0xCF };
    for (size_t i = 0; i < sizeof stub8; i++) mem_write8(c, phys(iret_seg, (uint16_t)(0x20 + i)), stub8[i]);
    mem_write16(c, 8 * 4, 0x20);
    /* The service vectors: see dos_int_hook. */
    for (unsigned k = 0; k < sizeof SERVICES; k++) {
        const uint16_t at = SERVICE_STUB(k);
        const uint8_t s[5] = { 0xCD, (uint8_t)(0xE0 + k), 0xCA, 0x02, 0x00 };
        for (int i = 0; i < 5; i++) mem_write8(c, phys(iret_seg, (uint16_t)(at + i)), s[i]);
        mem_write16(c, (uint32_t)SERVICES[k] * 4, at);
    }
    init_bios_data_area(m);

    /* The master environment. The Microsoft C startup reads it from
     * PSP:2Ch to build environ and argv[0]; left zero, it parses the vector
     * table instead and runs off into unmapped memory. */
    m->env_seg = 0x0080;
    {
        static const char *vars[] = { "PATH=C:\\", "COMSPEC=C:\\COMMAND.COM" };
        uint16_t o = 0;
        for (size_t v = 0; v < sizeof(vars) / sizeof(vars[0]); v++)
            for (const char *p = vars[v]; ; p++) {
                mem_write8(c, phys(m->env_seg, o++), (uint8_t)*p);
                if (!*p) break;
            }
        mem_write8(c, phys(m->env_seg, o++), 0);
        mem_write16(c, phys(m->env_seg, o), 1);
        o += 2;
        const char *self_path = "C:\\F117.COM";
        for (const char *p = self_path; ; p++) {
            mem_write8(c, phys(m->env_seg, o++), (uint8_t)*p);
            if (!*p) break;
        }
    }

    m->arena_base_seg = 0x0100;
    m->arena_end_seg  = 0x9FFF;
    m->files[0].in_use = m->files[1].in_use = m->files[2].in_use = 1;
    m->files[0].is_device = m->files[1].is_device = m->files[2].is_device = 1;
    m->mouse_present = 1;
    memcpy(m->mouse_masks, DEFAULT_MOUSE_MASKS, sizeof m->mouse_masks);

    if (m->save_dir[0]) mkdir_(m->save_dir);

    exec_params ep;
    memset(&ep, 0, sizeof(ep));
    ep.env_seg = m->env_seg;
    dos_proc *root = &m->procs[0];
    memset(root, 0, sizeof(*root));
    snprintf(root->name, sizeof(root->name), "%s", dos_basename(program));
    m->nproc = 1;
    uint16_t psp = 0;
    uint16_t err = load_program(m, program, &ep, iret_seg, 0x08, &psp);
    if (err) {
        m->nproc = 0;
        snprintf(m->fault, sizeof m->fault, "cannot load %s from %s (DOS error %u)",
                 program, m->data_dir, err);
        return 0;
    }
    root->psp_seg = psp;
    return 1;
}

void dos_shutdown(machine_t *m)
{
    for (int i = 5; i < DOS_MAX_FILES; i++)
        if (m->files[i].in_use && m->files[i].fp) {
            fclose(m->files[i].fp);
            m->files[i].fp = NULL;
            m->files[i].in_use = 0;
        }
}
