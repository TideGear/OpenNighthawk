/* fixes.c - the switchable fixes. See fixes.h.
 *
 * A fix is code overrides (recomp_rt.h), data corrections applied as DOS
 * reads a file, or both. Each is pinned to the shipped files it was written
 * against: an override by the program's file hash, a data correction by
 * file name, size and the byte it replaces.
 *
 * The recompiler reads OVERRIDES below (recompiler/recomp.py
 * override_sites) to give each address a region of its own; keep each
 * entry's id, module, hash, segment and offset on one line in this form.
 */
#include "fixes.h"
#include "recomp_rt.h"

#include <string.h>

/* The files GOG ships: MicroProse's final 473.04 update. */
#define VGAME_47304 0x8287450CCA85106FULL
#define START_47304 0xC65ECC83823E4907ULL
#define ASOUND_47304 0x9CD012D9D4A2CF30ULL

/* D5. VGAME's impact gate at 0x06D1C admits weapon types 1Eh, 1Dh and 1Ch;
 * anything else reaches the jmp at 0x6D2E to the skip at 0x6EBB. A supply
 * drop is type 26h, so the credit path at 0x6E4C - which tests for 26h
 * itself, at 0x6D35 - never runs. The fix lets 26h continue to 0x6D31 as
 * the three admitted types do; every other type still takes the jmp. */
static int fix_d5(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t type = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] - 0x12));
    c->op_cs = c->seg[S_CS];
    c->op_ip = c->ip;
    c->ip = type == 0x26 ? 0x6D31 : 0x6EBB;
    c->icount++;                     /* the one instruction it replaces */
    return 1;
}

/* D4, the program half. START's mission table (DGROUP 0A95h:11DA, 82
 * entries of 12 bytes) opens each entry with its theatre mask; entries 0-3,
 * the secret-airstrip landings and supply drops, carry 0002h, the Persian
 * Gulf alone. At START's entry, once LZEXE has unpacked it and before any
 * of its own code runs, the four become 0027h: Libya, the Gulf, North Cape
 * and the Middle East, the theatres whose world data carries airstrips.
 * START is loaded afresh after every flight, so this runs each time. The
 * original instruction then runs as it would have. */
static int fix_d4_table(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t dgroup = (uint16_t)(c->seg[S_CS] + 0x0A95);
    for (unsigned i = 0; i < 4; i++) {
        const uint32_t at = phys(dgroup, (uint16_t)(0x11DA + i * 12));
        if (mem_read8(c, at) == 0x02 && mem_read8(c, at + 1) == 0x00) mem_write8(c, at, 0x27);
    }
    return 0;
}

/* D34. VGAME 0x00F3D appends a 5-byte record (level, object, cell x,
 * cell y, type) for every distinct object destroyed or replaced, to the
 * table at DS:B79E with its count at [0x9932], and never stops: record 31
 * lands on the target camera's depth and the F7/F8 flag at B838, later ones
 * on the world lookup's result and the event log. The table has room for
 * 30. With the fix the first 30 stay where the original keeps them and the
 * rest go to an extension outside the guest, the way the Reimp's scene.c
 * does. Every place a record is reached is covered: the append (0x0F97),
 * the lookup (0x0FBD, which leaves the record's index in [0x950C]), and the
 * two that use that index, a type write (0x0F7E) and a type read
 * (0x0D5E). Each declines, so the original runs, until the table is full:
 * a fixed flight is the original's to the instruction until record 31. */
#define D34_ROOM  30
#define D34_EXTRA 4096
static uint8_t d34_ext[D34_EXTRA][5];
static int d34_n, d34_found_logged;

static uint16_t ds_read16(cpu_t *c, uint16_t off) { return seg_read16(c, c->seg[S_DS], off); }
static void ds_write16(cpu_t *c, uint16_t off, uint16_t v)
{
    mem_write8(c, phys(c->seg[S_DS], off), (uint8_t)v);
    mem_write8(c, phys(c->seg[S_DS], (uint16_t)(off + 1)), (uint8_t)(v >> 8));
}

/* VGAME's entry: each flight starts with an empty extension. */
static int fix_d34_start(machine_t *m)
{
    (void)m;
    d34_n = 0;
    d34_found_logged = 0;
    return 0;
}

/* 0x0F97: the append, reached when the lookup found no record. */
static int fix_d34_append(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (ds_read16(c, 0x9932) < D34_ROOM) return 0;
    const uint16_t src = (uint16_t)(seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] + 4)) + 0x0E);
    if (d34_n < D34_EXTRA) {
        for (int k = 0; k < 5; k++) d34_ext[d34_n][k] = mem_read8(c, phys(c->seg[S_DS], (uint16_t)(src + k)));
        d34_n++;
        dos_log(m, "[fix D34] destroyed-object record %d kept past the table @%llu\n",
                D34_ROOM + d34_n, (unsigned long long)c->icount);
    }
    c->op_cs = c->seg[S_CS]; c->op_ip = c->ip;
    c->ip = 0x0FB1;
    c->icount += 10;                 /* the append's own instructions */
    return 1;
}

/* 0x0FBD: lookup(level, object, x, y), newest record first. AL is the
 * record's type, AH the high byte of the table count less one (always 0
 * here: the leak Q3 needs 256 records in the table, which the fix never
 * lets it hold), [0x950C] the index or -1, BX the last index times 5. */
static int fix_d34_lookup(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!d34_n) return 0;
    const uint16_t ss = c->seg[S_SS], sp = c->r[R_SP];
    uint8_t key[4];
    for (int k = 0; k < 4; k++) key[k] = (uint8_t)seg_read16(c, ss, (uint16_t)(sp + 2 + 2 * k));
    const int in_table = ds_read16(c, 0x9932);
    int found = -1, scanned = 0;
    uint8_t type = 0;
    for (int i = D34_ROOM + d34_n - 1; i >= 0 && found < 0; i--) {
        if (i >= D34_ROOM + d34_n || (i < D34_ROOM && i >= in_table)) continue;
        uint8_t rec[5];
        for (int k = 0; k < 5; k++)
            rec[k] = i < D34_ROOM ? mem_read8(c, phys(c->seg[S_DS], (uint16_t)(0xB79E + i * 5 + k)))
                                  : d34_ext[i - D34_ROOM][k];
        scanned++;
        c->r[R_BX] = (uint16_t)(i * 5);
        if (!memcmp(rec, key, 4)) { found = i; type = rec[4]; }
    }
    if (found >= D34_ROOM && !d34_found_logged) {
        d34_found_logged = 1;
        dos_log(m, "[fix D34] lookup found record %d past the table @%llu\n",
                found + 1, (unsigned long long)c->icount);
    }
    ds_write16(c, 0x950C, (uint16_t)found);
    c->r[R_AX] = (uint16_t)((uint16_t)((in_table - 1) & 0xFF00) | type);
    c->op_cs = c->seg[S_CS]; c->op_ip = c->ip;
    c->ip = seg_read16(c, ss, sp);   /* near ret */
    c->r[R_SP] = (uint16_t)(sp + 2);
    c->icount += 8u + 9u * (unsigned)scanned;
    return 1;
}

/* 0x0F7E: imul bx, [0x950C], 5 / mov [bx+B7A2], al - a found record's
 * type rewritten; past the table, in the extension. */
static int fix_d34_set_type(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const int16_t i = (int16_t)ds_read16(c, 0x950C);
    if (i < D34_ROOM) return 0;
    if (i - D34_ROOM < d34_n) d34_ext[i - D34_ROOM][4] = (uint8_t)c->r[R_AX];
    c->r[R_BX] = (uint16_t)(i * 5);
    c->op_cs = c->seg[S_CS]; c->op_ip = c->ip;
    c->ip = 0x0F87;
    c->icount += 2;
    return 1;
}

/* 0x0D5E: imul bx, [0x950C], 5 / mov al, [bx+B7A2] - the same, read. */
static int fix_d34_get_type(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const int16_t i = (int16_t)ds_read16(c, 0x950C);
    if (i < D34_ROOM) return 0;
    const uint8_t type = i - D34_ROOM < d34_n ? d34_ext[i - D34_ROOM][4] : 0;
    c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0xFF00) | type);
    c->r[R_BX] = (uint16_t)(i * 5);
    c->op_cs = c->seg[S_CS]; c->op_ip = c->ip;
    c->ip = 0x0D67;
    c->icount += 2;
    return 1;
}

/* D2. ASOUND.117 0x2552 speaks a word on the AdLib: 0x1DBA silences the
 * music voices and spins until the timer's sequencer has cleared them,
 * 0x2577 programs channel 0 and masks the timer interrupt, 0x2661 writes
 * one carrier level per sample, timed on PIT counter 2 (mode 2, divisor
 * 150), and 0x2593 keys off and unmasks. The game stops for the whole word
 * and the spin can deadlock. The fix plays the same OPL writes - the same
 * setup, levels and timing - from the machine's own schedule and returns at
 * once: nothing waits and the timer keeps running, so the game and music
 * go on while the word plays on channel 0 (the music's own channel-0 writes
 * are held off meanwhile; afterwards the channel is restored from the
 * driver's register shadow at DS:1A06). The Sound Blaster arms ([17F2] 1
 * or 2) are left as they are.
 *
 * BX is a CS-relative list of (end, start, segment) runs ended by a zero
 * word; a run's bytes are at (segment + [17E2]):start..end-1, and each
 * byte's level is CS:[18h + byte / 4]. Segment 0x01B6 is the code's place
 * in the image, DS the image's start. */
static int fix_d2_speech(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t cs = c->seg[S_CS], ds = (uint16_t)(cs - 0x01B6);
    if (seg_read16(c, ds, 0x17F2) != 0) return 0;          /* not the AdLib arm */
    const uint64_t ips = m->ips;
    const uint64_t pit = 1193182ull;
    uint64_t at = c->icount;
    const uint64_t busy = machine_opl_scheduled_until(m);
    if (busy > at) at = busy;                               /* after a word still playing */
    static const uint8_t setup[][2] = {
        { 0x20, 0x23 }, { 0x23, 0x28 }, { 0x40, 0x3F }, { 0x43, 0x3F }, { 0x60, 0xAF },
        { 0x63, 0xAF }, { 0x80, 0x0D }, { 0x83, 0x0F }, { 0xC0, 0x05 }, { 0xE0, 0x00 },
        { 0xE3, 0x02 }, { 0xB0, 0x01 }, { 0xA0, 0x8F }, { 0xB0, 0x2E },
    };
    int ok = 1;
    for (unsigned i = 0; i < sizeof setup / sizeof setup[0]; i++)
        ok &= machine_opl_schedule(m, at, setup[i][0], setup[i][1], 0);
    at += 0x988 * ips / pit;                                /* 0x2649's counter-0 wait */
    ok &= machine_opl_schedule(m, at, 0xA0, 0x00, 0);
    ok &= machine_opl_schedule(m, at, 0xB0, 0x20, 0);
    const uint16_t base = seg_read16(c, ds, 0x17E2);
    uint64_t n = 0;
    for (uint16_t bx = c->r[R_BX];; bx = (uint16_t)(bx + 6)) {
        const uint16_t end = seg_read16(c, cs, bx);
        if (!end) break;
        const uint16_t start = seg_read16(c, cs, (uint16_t)(bx + 2));
        const uint16_t seg = (uint16_t)(seg_read16(c, cs, (uint16_t)(bx + 4)) + base);
        for (uint16_t si = start; si != end; si++) {
            const uint8_t level = mem_read8(c, phys(cs, (uint16_t)(0x18 + (mem_read8(c, phys(seg, si)) >> 2))));
            n++;
            ok &= machine_opl_schedule(m, at + n * 150u * ips / pit, 0x43, level, 0);
        }
    }
    at += (n + 1) * 150u * ips / pit;
    ok &= machine_opl_schedule(m, at, 0xB0, 0x00, 0);       /* 0x2593's key-off */
    /* Channel 0 as the music left it in the driver's shadow, key last. */
    static const uint8_t restore[] = { 0x20, 0x23, 0x40, 0x43, 0x60, 0x63, 0x80, 0x83,
                                       0xE0, 0xE3, 0xC0, 0xA0, 0xB0 };
    for (unsigned i = 0; i < sizeof restore; i++)
        ok &= machine_opl_schedule(m, at, restore[i], 0, phys(ds, (uint16_t)(0x1A06 + restore[i])));
    if (!ok) return 0;                                      /* no memory: speak as the original */
    dos_log(m, "[fix D2] word of %llu samples scheduled @%llu\n",
            (unsigned long long)n, (unsigned long long)c->icount);
    c->op_cs = cs; c->op_ip = c->ip;
    c->ip = seg_read16(c, c->seg[S_SS], c->r[R_SP]);       /* near ret */
    c->r[R_SP] = (uint16_t)(c->r[R_SP] + 2);
    c->icount += 4;
    return 1;
}

static const recomp_override OVERRIDES[] = {
    { "D5", "VGAME.EXE", VGAME_47304, 0x0000, 0x6D2E, fix_d5, "the supply-drop impact gate" },
    { "D4", "START.EXE", START_47304, 0x0000, 0x8EDC, fix_d4_table, "START's entry: airstrip mission masks" },
    { "D34", "VGAME.EXE", VGAME_47304, 0x0000, 0xE6BE, fix_d34_start, "VGAME's entry: empty extension" },
    { "D34", "VGAME.EXE", VGAME_47304, 0x0000, 0x0F97, fix_d34_append, "destroyed-object append" },
    { "D34", "VGAME.EXE", VGAME_47304, 0x0000, 0x0FBD, fix_d34_lookup, "destroyed-object lookup" },
    { "D34", "VGAME.EXE", VGAME_47304, 0x0000, 0x0F7E, fix_d34_set_type, "found record's type, written" },
    { "D34", "VGAME.EXE", VGAME_47304, 0x0000, 0x0D5E, fix_d34_get_type, "found record's type, read" },
    { "D2", "ASOUND.117", ASOUND_47304, 0x01B6, 0x09F2, fix_d2_speech, "AdLib speech without the busy-wait" },
};

/* A byte corrected as it is read. */
typedef struct {
    const char *id, *file;
    long size, offset;
    uint8_t from, to;
} data_fix;

/* D4, the world half (the Reimp's src/core/world.c, by file offset): the
 * airstrip object's class byte, 0 in Libya, the Middle East and North
 * Cape, becomes 1 as in the Gulf; North Cape's two airstrips gain flag
 * bit 0, which the flight engine needs to credit a landing. */
static const data_fix DATA[] = {
    { "D4", "LB.WLD", 2159, 0x53A, 0x00, 0x01 },
    { "D4", "ME.WLD", 2146, 0x597, 0x00, 0x01 },
    { "D4", "NC.WLD", 2383, 0x607, 0x00, 0x01 },
    { "D4", "NC.WLD", 2383, 0x1F1, 0x08, 0x09 },
    { "D4", "NC.WLD", 2383, 0x201, 0x08, 0x09 },
};

static const struct { const char *id, *what; } FIXES[] = {
    { "D2", "AdLib speech plays without stopping the game or risking its busy-wait hang" },
    { "D4", "secret-airstrip missions in Libya, North Cape and the Middle East" },
    { "D5", "supply drops earn their delivery credit" },
    { "D34", "the destroyed-object table keeps records past 30 without overwriting" },
};
#define NFIXES (sizeof FIXES / sizeof FIXES[0])

static int g_on[NFIXES];

void fixes_register(void)
{
    static int done;
    if (done) return;
    done = 1;
    for (unsigned i = 0; i < sizeof OVERRIDES / sizeof OVERRIDES[0]; i++) recomp_override_add(&OVERRIDES[i]);
}

static int fix_on(const char *id)
{
    for (unsigned i = 0; i < NFIXES; i++) if (!strcmp(FIXES[i].id, id)) return g_on[i];
    return 0;
}

int fixes_enable(const char *id, int on)
{
    fixes_register();
    int n = 0;
    for (unsigned i = 0; i < NFIXES; i++) {
        if (strcmp(id, "all") && strcmp(id, FIXES[i].id)) continue;
        g_on[i] = on != 0;
        recomp_override_enable(FIXES[i].id, on);
        n++;
    }
    return n;
}

void fixes_list(FILE *f)
{
    fixes_register();
    for (unsigned i = 0; i < NFIXES; i++)
        fprintf(f, "%-4s %-3s %s\n", FIXES[i].id, g_on[i] ? "on" : "off", FIXES[i].what);
}

void fixes_enabled(char *out, size_t n)
{
    size_t used = 0;
    if (n) out[0] = 0;
    for (unsigned i = 0; i < NFIXES; i++) {
        if (!g_on[i]) continue;
        int w = snprintf(out + used, n > used ? n - used : 0, "%s%s", used ? " " : "", FIXES[i].id);
        if (w > 0) used += (size_t)w;
    }
}

int fixes_enable_list(const char *ids)
{
    char id[32];
    int ok = 1;
    for (const char *p = ids; *p; ) {
        while (*p == ' ') p++;
        size_t len = strcspn(p, " ");
        if (!len) break;
        snprintf(id, sizeof id, "%.*s", (int)len, p);
        if (!fixes_enable(id, 1)) ok = 0;
        p += len;
    }
    return ok;
}

static int same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        const char x = (char)(*a >= 'a' && *a <= 'z' ? *a - 32 : *a);
        const char y = (char)(*b >= 'a' && *b <= 'z' ? *b - 32 : *b);
        if (x != y) return 0;
    }
    return !*a && !*b;
}

void fixes_file_data(void *user, machine_t *m, const char *name, long size, long pos,
                     uint8_t *buf, size_t n)
{
    (void)user; (void)m;
    for (unsigned i = 0; i < sizeof DATA / sizeof DATA[0]; i++) {
        const data_fix *d = &DATA[i];
        if (d->offset < pos || d->offset >= pos + (long)n || d->size != size) continue;
        if (!same_name(d->file, name) || !fix_on(d->id)) continue;
        uint8_t *b = buf + (d->offset - pos);
        if (*b == d->from) *b = d->to;
    }
}
