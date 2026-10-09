/* dos_files.c - extracted DOS/BIOS services; see dos.c for provenance. */
#include "dos_internal.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <direct.h>
#define mkdir_(p) _mkdir(p)
#else
#include <dirent.h>
#include <sys/stat.h>
#define mkdir_(p) mkdir(p, 0755)
#endif

/* ===================================================================== */
/* Files: the install, overlaid by the save directory                    */
/* ===================================================================== */

/* The guest's names are flattened to their basename and resolved inside the
 * directories the user chose, which is both what the game expects (it
 * lives in one directory) and what keeps it from reaching anywhere else. */
const char *dos_basename(const char *p)
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
FILE *dos_open_read(machine_t *m, const char *name, char *found, size_t fn)
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

int dos_atomic_saves;

static FILE *open_create(machine_t *m, const char *name, char *found, size_t fn, char *temp, size_t tn)
{
    char existing[1100];
    char path[1100];
    /* Keep the case of a file that already exists there. */
    if (file_exists_ci(write_dir(m), name, existing, sizeof existing))
        snprintf(path, sizeof path, "%s", existing);
    else
        snprintf(path, sizeof path, "%s/%s", write_dir(m), name);
    if (temp) temp[0] = 0;
    if (dos_atomic_saves && m->save_dir[0] && temp) {
        snprintf(temp, tn, "%s.f117r-tmp", path);
        FILE *f = fopen(temp, "wb+");
        if (f && found) snprintf(found, fn, "%s", path);
        if (!f) temp[0] = 0;
        return f;
    }
    FILE *f = fopen(path, "wb+");
    if (f && found) snprintf(found, fn, "%s", path);
    return f;
}

/* Fix D11's commit: the finished temporary file replaces the real one. */
static void commit_temp(const char *temp, const char *path)
{
#ifdef _WIN32
    MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    rename(temp, path);
#endif
}

uint8_t *dos_read_whole(machine_t *m, const char *name, long *out_size)
{
    FILE *f = dos_open_read(m, name, NULL, 0);
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

/* The handle table in a PSP (at +18h, 20 entries) holds system file table
 * numbers, as DOS keeps it: what a program sees if it looks, and what a
 * child inherits. Entries 0-2 are AUX, CON and PRN, as in DOSBox. */
static void jft_set(machine_t *m, uint16_t psp, unsigned h, uint8_t sft)
{
    if (psp && h < 20) mem_write8(&m->cpu, phys(psp, (uint16_t)(0x18 + h)), sft);
}

static uint8_t sft_alloc(machine_t *m)
{
    for (unsigned i = 0; i < sizeof m->sft_ref; i++)
        if (!m->sft_ref[i]) { m->sft_ref[i] = 1; return (uint8_t)i; }
    return 0xFF;
}

void dos_sft_release(machine_t *m, uint8_t sft)
{
    if (sft < sizeof m->sft_ref && m->sft_ref[sft]) m->sft_ref[sft]--;
}

static void close_handle(machine_t *m, unsigned h)
{
    if (m->files[h].fp) fclose(m->files[h].fp);
    if (m->files[h].temp[0]) commit_temp(m->files[h].temp, m->files[h].path);
    m->files[h].temp[0] = 0;
    jft_set(m, m->files[h].owner, h, 0xFF);
    dos_sft_release(m, m->files[h].sft);
    m->files[h].in_use = 0;
    m->files[h].fp = NULL;
}

/* DOSBox's modify_cycles (DATA_TRANSFERS_TAKE_CYCLES): a DOS read or write
 * costs 4 cycles a byte, or, when that does not fit in what is left of the
 * CPU's current slice, the rest of the slice less 5. DOSBox's slice ends at
 * its next event: the end of the millisecond, the next timer interrupt, or
 * the next of its VGA events - the frame start, the screen drawn in four
 * parts (lines 100-400), retrace start and end (vga_draw.cpp). */
static void transfer_cost(machine_t *m, uint32_t bytes)
{
    cpu_t *c = &m->cpu;
    const uint64_t left = pc_slice_left(m);
    const uint64_t want = 4ull * bytes;
    const uint64_t cost = want + 5 < left ? want : left > 5 ? left - 5 : 0;
    c->icount += cost;
    c->charged += cost;
}

void dos_close_files_of(machine_t *m, uint16_t owner)
{
    for (int i = 5; i < DOS_MAX_FILES; i++)
        if (m->files[i].in_use && m->files[i].owner == owner) close_handle(m, (unsigned)i);
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
/* Clock                                                                 */
/* ===================================================================== */

/* The wall-clock time at boot plus the emulated time since, broken down.
 * boot_time_us is LOCAL time counted as if it were UTC (what a PC's
 * real-time clock holds), so it is broken down with gmtime: the same
 * recorded boot time means the same DOS clock in every time zone. */
void dos_wall_clock(machine_t *m, struct tm *out, unsigned *centis)
{
    uint64_t us = m->boot_time_us + machine_now_us(m);
    time_t secs = (time_t)(us / 1000000ull);
    struct tm *t = gmtime(&secs);
    if (t) *out = *t; else memset(out, 0, sizeof *out);
    if (centis) *centis = (unsigned)((us / 10000ull) % 100ull);
}

uint8_t dos_bcd(unsigned v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

/* ===================================================================== */
/* INT 21h                                                               */
/* ===================================================================== */

/* One byte from CON, as DOS's console device gives it (DOSBox's
 * device_CON::Read): an extended key - ASCII 0, or E0 with a scan code -
 * reads as 0 and its scan code is held for the next read. 0: no key. */
static int con_read(machine_t *m, uint8_t *ch)
{
    uint16_t key;
    if (m->con_cache) { *ch = m->con_cache; m->con_cache = 0; return 1; }
    if (!dos_kbd_pop(m, &key, 1)) return 0;
    *ch = (uint8_t)key;
    if ((*ch == 0 || *ch == 0xE0) && (key >> 8)) { m->con_cache = (uint8_t)(key >> 8); *ch = 0; }
    return 1;
}

/* Whether CON has a byte to read (DOSBox's GetInformation): a held scan code
 * or a key in the BIOS buffer; an empty word there is dropped. */
static int con_ready(machine_t *m)
{
    uint16_t key;
    if (m->con_cache) return 1;
    if (!dos_kbd_pop(m, &key, 0)) return 0;
    if (key) return 1;
    dos_kbd_pop(m, &key, 1);
    return 0;
}

int dos_int21(machine_t *m)
{
    cpu_t *c = &m->cpu;
    uint8_t ah = (uint8_t)(c->r[R_AX] >> 8);
    uint8_t al = (uint8_t)(c->r[R_AX] & 0xFF);
    uint16_t me = dos_current_psp(m);

    /* DOSBox records the caller's stack in the current PSP on every call
     * but the PSP ones: SS:SP inside its handler (past the 6-byte
     * interrupt frame) less the 18 bytes its EXEC would save. */
    if (me && ah != 0x50 && ah != 0x51 && ah != 0x62 && ah != 0x64 && ah < 0x6C) {
        mem_write16(c, phys(me, 0x2E), (uint16_t)(c->r[R_SP] - 24));
        mem_write16(c, phys(me, 0x30), c->seg[S_SS]);
    }

    switch (ah) {

    case 0x00:     /* dos_terminate, old style */
        return dos_terminate(m, 0);

    case 0x01:     /* read char with echo */
    case 0x07:     /* direct read, no echo, no Ctrl-C */
    case 0x08: {   /* read, no echo */
        uint8_t key;
        if (!con_read(m, &key)) return dos_bios_wait(c);
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | key);
        if (ah == 0x01) { char ch = (char)key; dos_console_text(m, &ch, 1); }
        return 1;
    }

    case 0x02: {   /* write char */
        char ch = (char)(c->r[R_DX] & 0xFF);
        dos_console_text(m, &ch, 1);
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | (uint8_t)ch);
        return 1;
    }

    case 0x06: {   /* direct console I/O: DL=FF reads, else writes DL */
        uint8_t key;
        if ((c->r[R_DX] & 0xFF) == 0xFF) {
            if (con_ready(m) && con_read(m, &key)) {
                c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | key);
                c->flags = (uint16_t)(c->flags & (uint16_t)(0xFFFFu ^ F_ZF));
            } else {
                c->r[R_AX] = (uint16_t)(c->r[R_AX] & 0xFF00);
                c->flags |= F_ZF;
            }
        } else {
            char ch = (char)(c->r[R_DX] & 0xFF);
            dos_console_text(m, &ch, 1);
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
        dos_console_text(m, buf, (size_t)n);
        return 1;                           /* AL unchanged, as in DOSBox */
    }

    case 0x0B: {   /* stdin status: FF if a key is waiting */
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | (con_ready(m) ? 0xFF : 0x00));
        return 1;
    }

    case 0x0C: {   /* flush input, then perform AL's function */
        uint8_t key;
        if (al == 0x01 || al == 0x06 || al == 0x07 || al == 0x08 || al == 0x0A) {
            /* Flush only on the first entry: a wait re-enters this call. */
            if (c->halted != 2) while (con_ready(m) && con_read(m, &key)) {}
            c->r[R_AX] = (uint16_t)((uint16_t)(al << 8) | al);
            return dos_int21(m);
        }
        while (con_ready(m) && con_read(m, &key)) {}
        return 1;
    }

    case 0x0D:     /* disk reset */
        return 1;

    case 0x0E:     /* select disk: report drives A..C */
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | 3);
        return 1;

    case 0x11: case 0x12: { /* FCB find first / next (DOSBox DOS_FCBFindFirst, SaveFindResult) */
        const uint16_t fs = c->seg[S_DS], fx = c->r[R_DX];
        const int ext = mem_read8(c, phys(fs, fx)) == 0xFF;
        const uint16_t fo = (uint16_t)(fx + (ext ? 7 : 0));
        const uint8_t attr = ext ? mem_read8(c, phys(fs, (uint16_t)(fx + 6))) : 0x20;
        char nm[12];
        for (int i = 0; i < 11; i++) nm[i] = (char)mem_read8(c, phys(fs, (uint16_t)(fo + 1 + i)));
        nm[11] = 0;
        const char *rname = NULL;
        long rsize = 0;
        uint8_t rattr = 0x20;
        if (ah == 0x11 && attr == 0x08) {
            /* The volume label: a mounted directory is "C_DRIVE" in DOSBox. */
            rname = "C_DRIVE"; rattr = 0x08;
            g_find_n = g_find_pos = 0;
        } else {
            if (ah == 0x11) {
                char pat[13]; int k = 0;
                for (int i = 0; i < 8 && nm[i] != ' '; i++) pat[k++] = nm[i];
                if (nm[8] != ' ') { pat[k++] = '.'; for (int i = 8; i < 11 && nm[i] != ' '; i++) pat[k++] = nm[i]; }
                pat[k] = 0;
                find_first(m, pat);
            }
            if (g_find_pos < g_find_n) { rname = g_find[g_find_pos].name; rsize = g_find[g_find_pos].size; g_find_pos++; }
        }
        dos_log(m, "[fcb] find %s '%s' attr %02X -> %s, from %s at %04X:%04X\n", ah == 0x11 ? "first" : "next",
                nm, attr, rname ? rname : "nothing", dos_current_program(m), c->op_cs, c->op_ip);
        if (!rname) { c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | 0xFF); return 1; }
        /* The result, as an FCB at the DTA: drive (C: is 3), name and
         * extension space-padded, size, date, time; the extended header and
         * attribute when the search FCB was extended. */
        const uint32_t d = m->dta;
        for (uint32_t i = 0; i < (ext ? 40u : 33u); i++) mem_write8(c, d + i, 0);
        const uint32_t p = ext ? d + 7 : d;
        if (ext) { mem_write8(c, d, 0xFF); mem_write8(c, d + 6, rattr); }
        uint8_t drv = mem_read8(c, phys(fs, fo));
        mem_write8(c, p, (uint8_t)(drv ? drv : 3));
        const char *dot = strchr(rname, '.');
        const size_t bl = dot ? (size_t)(dot - rname) : strlen(rname);
        for (size_t i = 0; i < 8; i++) mem_write8(c, p + 1 + (uint32_t)i, (uint8_t)(i < bl ? rname[i] : ' '));
        for (size_t i = 0; i < 3; i++)
            mem_write8(c, p + 9 + (uint32_t)i, (uint8_t)(dot && i < strlen(dot + 1) ? dot[1 + i] : ' '));
        mem_write16(c, p + 16, (uint16_t)(rsize & 0xFFFF));
        mem_write16(c, p + 18, (uint16_t)((rsize >> 16) & 0xFFFF));
        if (rattr != 0x08) {
            mem_write16(c, p + 20, (uint16_t)(((1992 - 1980) << 9) | (6 << 5) | 5));
            mem_write16(c, p + 22, 0x6000);
        }
        c->r[R_AX] = (uint16_t)(c->r[R_AX] & 0xFF00);
        return 1;
    }

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
        dos_wall_clock(m, &t, NULL);
        c->r[R_CX] = (uint16_t)(t.tm_year + 1900);
        c->r[R_DX] = (uint16_t)(((t.tm_mon + 1) << 8) | t.tm_mday);
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | (uint8_t)t.tm_wday);
        return 1;
    }

    case 0x2C: {   /* get time */
        struct tm t;
        unsigned cs;
        dos_wall_clock(m, &t, &cs);
        c->r[R_CX] = (uint16_t)((t.tm_hour << 8) | t.tm_min);
        c->r[R_DX] = (uint16_t)((t.tm_sec << 8) | cs);
        return 1;
    }

    case 0x2B:     /* set date: accepted, ignored */
        c->r[R_AX] = (uint16_t)(c->r[R_AX] & 0xFF00);
        return 1;
    case 0x2D:     /* set time: checked, then ignored (DOSBox) */
        c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) |
            ((c->r[R_CX] >> 8) > 23 || (c->r[R_CX] & 0xFF) > 59 ||
             (c->r[R_DX] >> 8) > 59 || (c->r[R_DX] & 0xFF) > 99 ? 0xFF : 0x00));
        return 1;

    case 0x2F:     /* get DTA */
        c->seg[S_ES] = (uint16_t)(m->dta >> 4);
        c->r[R_BX] = (uint16_t)(m->dta & 0xF);
        return 1;

    case 0x30:     /* DOS 5.00: BH the OEM for AL=0, DOS-in-HMA for AL=1, else kept; BL 0 (DOSBox) */
        if (al == 0) c->r[R_BX] = 0xFF00;
        else if (al == 1) c->r[R_BX] = 0x1000;
        else c->r[R_BX] &= 0xFF00;
        c->r[R_AX] = 0x0005;
        c->r[R_CX] = 0;
        return 1;

    case 0x33:     /* break checking, boot drive, true version (DOSBox) */
        switch (al) {
        case 0: c->r[R_DX] = (uint16_t)((c->r[R_DX] & 0xFF00) | m->break_check); break;
        case 1: m->break_check = (c->r[R_DX] & 0xFF) != 0; break;
        case 2: { uint8_t old = m->break_check; m->break_check = (c->r[R_DX] & 0xFF) != 0;
                  c->r[R_DX] = (uint16_t)((c->r[R_DX] & 0xFF00) | old); break; }
        case 5: c->r[R_DX] = (uint16_t)((c->r[R_DX] & 0xFF00) | 3); break;
        case 6: c->r[R_BX] = 0x0005; c->r[R_DX] = 0x1000; break;
        default: break;
        }
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
        dos_ok(c);
        return 1;

    case 0x3C:     /* create/truncate */
    case 0x3D: {   /* open */
        char dp[520], hp[1100], tp[1120];
        dos_guest_str(c, c->seg[S_DS], c->r[R_DX], dp, sizeof(dp));
        const char *base = dos_basename(dp);
        int h = alloc_handle(m);
        if (h < 0) { dos_fail(c, ERR_TOO_MANY_OPEN); return 1; }
        FILE *f;
        tp[0] = 0;
        if (ah == 0x3C) f = open_create(m, base, hp, sizeof hp, tp, sizeof tp);
        else if ((al & 7) == 0) f = dos_open_read(m, base, hp, sizeof hp);
        else f = open_rw(m, base, hp, sizeof hp);
        if (!f) {
            dos_log(m, "[file] %s '%s' failed (not found) in %s\n",
                    ah == 0x3C ? "create" : "open", dp, dos_current_program(m));
            dos_fail(c, ERR_FILE_NOT_FOUND);
            return 1;
        }
        m->files[h].fp = f;
        m->files[h].in_use = 1;
        m->files[h].is_device = 0;
        m->files[h].owner = me;
        m->files[h].sft = sft_alloc(m);
        jft_set(m, me, (unsigned)h, m->files[h].sft);
        snprintf(m->files[h].path, sizeof(m->files[h].path), "%s", hp);
        snprintf(m->files[h].temp, sizeof(m->files[h].temp), "%s", tp);
        dos_log(m, "[file] %s '%s' -> %d @%llu %s\n", ah == 0x3C ? "create" : "open",
                dp, h, (unsigned long long)c->icount, dos_current_program(m));
        c->r[R_AX] = (uint16_t)h;
        dos_ok(c);
        return 1;
    }

    case 0x3E: {   /* close */
        uint16_t h = c->r[R_BX];
        if (h < 5) { dos_ok(c); return 1; }
        if (h >= DOS_MAX_FILES || !m->files[h].in_use) { dos_fail(c, ERR_BAD_HANDLE); return 1; }
        close_handle(m, h);
        dos_ok(c);
        return 1;
    }

    case 0x3F: {   /* read */
        uint16_t h = c->r[R_BX], n = c->r[R_CX];
        if (h < 5) { c->r[R_AX] = 0; dos_ok(c); return 1; }   /* stdin: EOF */
        if (h >= DOS_MAX_FILES || !m->files[h].in_use) { dos_fail(c, ERR_BAD_HANDLE); return 1; }
        uint8_t *tmp = (uint8_t *)malloc(n ? n : 1);
        const long pos = m->hooks.file_data ? ftell(m->files[h].fp) : 0;
        size_t got = n ? fread(tmp, 1, n, m->files[h].fp) : 0;
        if (m->hooks.file_data && got) {
            FILE *fp = m->files[h].fp;
            const long after = ftell(fp);
            fseek(fp, 0, SEEK_END);
            const long size = ftell(fp);
            fseek(fp, after, SEEK_SET);
            m->hooks.file_data(m->hooks.user, m, dos_basename(m->files[h].path), size, pos, tmp, got);
        }
        for (size_t i = 0; i < got; i++)
            mem_write8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_DX] + i)), tmp[i]);
        free(tmp);
        c->r[R_AX] = (uint16_t)got;
        transfer_cost(m, (uint32_t)got);
        dos_ok(c);
        return 1;
    }

    case 0x40: {   /* write */
        uint16_t h = c->r[R_BX], n = c->r[R_CX];
        if (h < 5) {
            char buf[512]; int k = 0;
            for (uint16_t i = 0; i < n && k < 511; i++)
                buf[k++] = (char)mem_read8(c, phys(c->seg[S_DS], (uint16_t)(c->r[R_DX] + i)));
            if (h == 1 || h == 2) dos_console_text(m, buf, (size_t)k);
            c->r[R_AX] = n;
            transfer_cost(m, n);
            dos_ok(c);
            return 1;
        }
        if (h >= DOS_MAX_FILES || !m->files[h].in_use) { dos_fail(c, ERR_BAD_HANDLE); return 1; }
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
        transfer_cost(m, (uint32_t)put);
        dos_ok(c);
        return 1;
    }

    case 0x41: {   /* unlink: only ever in the save directory */
        char dp[520], hp[1100];
        dos_guest_str(c, c->seg[S_DS], c->r[R_DX], dp, sizeof(dp));
        if (!file_exists_ci(write_dir(m), dos_basename(dp), hp, sizeof hp) || remove(hp) != 0) {
            dos_fail(c, ERR_FILE_NOT_FOUND);
            return 1;
        }
        dos_ok(c);
        return 1;
    }

    case 0x42: {   /* lseek */
        uint16_t h = c->r[R_BX];
        if (h >= DOS_MAX_FILES || !m->files[h].in_use || h < 5) { dos_fail(c, ERR_BAD_HANDLE); return 1; }
        long off = (long)(int32_t)(((uint32_t)c->r[R_CX] << 16) | c->r[R_DX]);
        int whence = (al == 1) ? SEEK_CUR : (al == 2) ? SEEK_END : SEEK_SET;
        if (fseek(m->files[h].fp, off, whence) != 0) { dos_fail(c, ERR_BAD_FUNCTION); return 1; }
        long pos = ftell(m->files[h].fp);
        c->r[R_AX] = (uint16_t)(pos & 0xFFFF);
        c->r[R_DX] = (uint16_t)((pos >> 16) & 0xFFFF);
        dos_ok(c);
        return 1;
    }

    case 0x43: {   /* get/set file attributes */
        char dp[520];
        dos_guest_str(c, c->seg[S_DS], c->r[R_DX], dp, sizeof(dp));
        FILE *f = dos_open_read(m, dos_basename(dp), NULL, 0);
        if (!f) { dos_fail(c, ERR_FILE_NOT_FOUND); return 1; }
        fclose(f);
        if (al == 0) c->r[R_CX] = c->r[R_AX] = 0x20;   /* archive; AX too (DOSBox) */
        else c->r[R_AX] = 0x0202;                      /* set: AX destroyed (DOSBox) */
        dos_ok(c);
        return 1;
    }

    case 0x44:     /* ioctl */
        if (al == 0) {
            /* DOSBox: the console's device word for the standard handles, a
             * disk file's drive (C:) for the rest; AX gets the same. */
            uint16_t h = c->r[R_BX];
            if (h >= DOS_MAX_FILES || !m->files[h].in_use) { dos_fail(c, ERR_BAD_HANDLE); return 1; }
            c->r[R_DX] = m->files[h].is_device ? 0x80D3 : 0x0002;
            c->r[R_AX] = c->r[R_DX];
        } else if (al == 8) {
            c->r[R_AX] = 1;                         /* fixed disk */
        }
        dos_ok(c);
        return 1;

    case 0x47:     /* getcwd: root */
        mem_write8(c, phys(c->seg[S_DS], c->r[R_SI]), 0);
        dos_ok(c);
        return 1;

    case 0x48: {   /* allocate memory */
        uint16_t seg = 0, paras = c->r[R_BX];
        if (!dos_mem_alloc(m, &seg, &paras)) {
            c->r[R_BX] = paras;
            dos_fail(c, ERR_NO_MEMORY);
            return 1;
        }
        c->r[R_AX] = seg;
        dos_ok(c);
        return 1;
    }

    case 0x49: {   /* free memory */
        uint16_t e = dos_mem_free(m, c->seg[S_ES]);
        if (e) { dos_fail(c, e); return 1; }
        dos_ok(c);
        return 1;
    }

    case 0x4A: {   /* resize a memory block: AX = ES on success (DOSBox) */
        uint16_t paras = c->r[R_BX];
        uint16_t e = dos_mem_resize(m, c->seg[S_ES], &paras);
        if (e) {
            if (e == ERR_NO_MEMORY) c->r[R_BX] = paras;
            dos_fail(c, e);
            return 1;
        }
        c->r[R_AX] = c->seg[S_ES];
        dos_ok(c);
        return 1;
    }

    case 0x4B: {   /* EXEC */
        char dp[520];
        dos_guest_str(c, c->seg[S_DS], c->r[R_DX], dp, sizeof(dp));
        uint16_t pb_seg = c->seg[S_ES], pb = c->r[R_BX];

        if (al == 0x03) {           /* load overlay */
            uint16_t lseg = seg_read16(c, pb_seg, pb);
            uint16_t fac  = seg_read16(c, pb_seg, (uint16_t)(pb + 2));
            uint16_t err = dos_load_overlay(m, dp, lseg, fac);
            if (err) { dos_log(m, "[overlay] %s FAILED (%u)\n", dp, err); dos_fail(c, err); return 1; }
            dos_ok(c);                                  /* AX unchanged, as in DOSBox */
            return 1;
        }
        if (al != 0x00) { dos_fail(c, ERR_BAD_FUNCTION); return 1; }
        if (m->nproc >= DOS_MAX_PROCS) { dos_fail(c, ERR_NO_MEMORY); return 1; }

        exec_params ep;
        memset(&ep, 0, sizeof(ep));
        ep.env_seg  = seg_read16(c, pb_seg, pb);
        ep.parent_env = me ? mem_read16(c, phys(me, 0x2C)) : 0;
        ep.flags = c->flags;
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
        /* The parent stays the current PSP while the child loads, as in
         * DOSBox: the environment and program blocks are allocated by it,
         * then given to the child. */
        np->psp_seg = me;
        m->nproc++;
        uint16_t err = dos_load_program(m, dp, &ep, np->ret_cs, np->ret_ip, &psp);
        if (err) {
            m->nproc--;
            dos_log(m, "[exec] %s FAILED (%u)\n", dp, err);
            dos_fail(c, err);
            return 1;
        }
        np->psp_seg = psp;
        np->start_icount = c->icount;
        return 1;                         /* control is now in the child */
    }

    case 0x4C:     /* dos_terminate with code */
        return dos_terminate(m, al);

    case 0x4D:     /* child's exit code and how it ended (flags untouched, as in DOSBox) */
        c->r[R_AX] = (uint16_t)(((uint16_t)m->return_mode << 8) | m->last_child_exit);
        return 1;

    case 0x4E: {   /* find first */
        char dp[520];
        dos_guest_str(c, c->seg[S_DS], c->r[R_DX], dp, sizeof(dp));
        find_first(m, dos_basename(dp));
        if (!find_step(m)) { dos_fail(c, ERR_FILE_NOT_FOUND); return 1; }
        dos_ok(c);
        return 1;
    }
    case 0x4F:     /* find next */
        if (!find_step(m)) { dos_fail(c, ERR_NO_MORE_FILES); return 1; }
        dos_ok(c);
        return 1;

    case 0x50:     /* set PSP */
        if (m->nproc) m->procs[m->nproc - 1].psp_seg = c->r[R_BX];
        return 1;
    case 0x52:     /* list of lists: DOSBox's, at 0080:0026; the first MCB before it */
        c->seg[S_ES] = 0x0080;
        c->r[R_BX] = 0x0026;
        return 1;

    case 0x51: case 0x62:   /* get PSP */
        c->r[R_BX] = me;
        return 1;

    case 0x56: {   /* rename, inside the save directory */
        char a[520], b[520], ha[1100], hb[1100];
        dos_guest_str(c, c->seg[S_DS], c->r[R_DX], a, sizeof a);
        dos_guest_str(c, c->seg[S_ES], c->r[R_DI], b, sizeof b);
        if (!file_exists_ci(write_dir(m), dos_basename(a), ha, sizeof ha)) { dos_fail(c, ERR_FILE_NOT_FOUND); return 1; }
        snprintf(hb, sizeof hb, "%s/%s", write_dir(m), dos_basename(b));
        if (rename(ha, hb) != 0) { dos_fail(c, ERR_ACCESS_DENIED); return 1; }
        dos_ok(c);
        return 1;
    }

    case 0x57:     /* file date and time */
        if (al == 0) { c->r[R_CX] = 0x6000; c->r[R_DX] = (uint16_t)(((1992 - 1980) << 9) | (6 << 5) | 5); }
        dos_ok(c);
        return 1;

    case 0x58:     /* allocation strategy */
        if (al == 0) c->r[R_AX] = 0;
        dos_ok(c);
        return 1;

    default: {
        static unsigned seen[256];
        if (seen[ah]++ < 3)
            dos_log(m, "[int21] UNHANDLED AH=%02X AL=%02X from %s at %04X:%04X\n",
                    ah, al, dos_current_program(m), c->op_cs, c->op_ip);
        dos_fail(c, ERR_BAD_FUNCTION);
        return 1;
    }
    }
}

