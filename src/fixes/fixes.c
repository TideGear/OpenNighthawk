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
#define END_47304   0xFA7167EE4E377EC1ULL
#define SETUP_47304 0xEDD5021CF28E7FC4ULL

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

/* D12. END's tally after a survived mission (0x042C-0x045E): the rating comes
 * back in AX, and ES:BX is the pilot's record, whose word at +2Eh is the best
 * rating and whose dword at +32h is the running total. 0x0443 compares the
 * best with the rating unsigned (`jae`), so a negative best (65,524 for -12)
 * is never replaced by a better one and the first negative rating always
 * replaces the initial 0; 0x0450 zero-extends the rating (`sub dx,dx`), so -12
 * adds as +65,524. The fixes compare signed and sign-extend (`cwd`). Both leave
 * every other path, and both clocks, as they were. */
static int d12_logged;
static int fix_d12_best(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t best = seg_read16(c, c->seg[S_ES], (uint16_t)(c->r[R_BX] + 0x2E)), rating = c->r[R_AX];
    const int keep = (int16_t)best >= (int16_t)rating;
    if (keep != (best >= rating) && !d12_logged) {
        d12_logged = 1;
        dos_log(m, "[fix D12] best %d, rating %d: %s @%llu (ES:BX %04X:%04X)\n", (int16_t)best, (int16_t)rating,
                keep ? "kept" : "replaced", (unsigned long long)c->icount, c->seg[S_ES], c->r[R_BX]);
    }
    c->op_cs = c->seg[S_CS];
    c->op_ip = c->ip;
    c->ip = keep ? 0x044D : 0x0449;
    c->icount += 2;                  /* the cmp and the jae */
    return 1;
}

static int fix_d12_total(machine_t *m)
{
    cpu_t *c = &m->cpu;
    c->r[R_DX] = (c->r[R_AX] & 0x8000) ? 0xFFFF : 0x0000;
    c->op_cs = c->seg[S_CS];
    c->op_ip = c->ip;
    c->ip = 0x0452;
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

/* D1. VGAME's frame-rate controller (0x441D-0x44A9, the tail of 0x3ADA)
 * measures the frame rate every 4S frames and sets S = [0x368E], the
 * divisor of every per-second rate, to it. The frame loop never waits: S
 * follows however fast the machine draws. Above about 16-18 fps the
 * correction carried in [0x43E8] underflows the unsigned tick count and S
 * oscillates, degrading AI and weapon guidance. The fix is a frame limiter:
 * at 0x441D, which every frame passes once, a frame that arrives before its
 * slot waits there (time passes, interrupts are serviced, as in a HLT
 * loop), so no machine draws faster than a GOG-speed one: 11.6 frames a
 * second, measured in flight at 9 MIPS, where S settles at 9. The
 * controller itself is
 * untouched and keeps measuring honestly, so S settles at the paced rate
 * and one game second stays one real second; pinning S without pacing ran
 * the world three times too fast at 40 MIPS. At 40 MIPS the fix gives S 9,
 * 11.6 frames a second and GOG's mission clock to the tick over 160 s of
 * flight. At GOG's speed and below it never waits: the machine runs as the
 * original, hash for hash. */
#define D1_FPS_X10 116
#define D1_GOG_IPS 9000000u          /* GOG DOSBox's cycles=9000, the default */
static uint64_t d1_next;             /* clock at which the next frame may start */
static int d1_logged;
static int fix_d1_pace(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint64_t period = m->ips * 10 / D1_FPS_X10, now = c->icount;
    /* GOG's own speed and slower: the rate varies around 11.6 frame to
     * frame, and holding its faster frames would change the play the
     * default machine gives. */
    if (m->ips <= D1_GOG_IPS) return 0;
    if (d1_next > now + period) d1_next = 0;           /* a new machine */
    if (now >= d1_next) {
        /* This frame's slot has come: the next one is a period later, or a
         * period from now after a slow frame, so time is never banked. */
        d1_next = d1_next && now < d1_next + period ? d1_next + period : now + period;
        return 0;                                      /* run the original */
    }
    if (!d1_logged) {
        d1_logged = 1;
        dos_log(m, "[fix D1] frames paced at %d.%d a second @%llu\n", D1_FPS_X10 / 10, D1_FPS_X10 % 10,(unsigned long long)now);
    }
    c->icount = d1_next < c->stop_at ? d1_next : c->stop_at;   /* wait at 0x441D */
    return 1;
}

/* D8, the laser-guided bomb's pitch clamp. After the guidance writes its pitch demand, VGAME 0x6C0F
 * compares the bomb's pitch with -2048 and, when it is greater (shallower than 11.25 degrees down),
 * stores -2048 (0x6C17): the bomb can dive harder than that and never less, so the guidance can
 * steepen a dive and nothing else, and a bomb released beyond Z / 6.4 of range falls short. With the
 * fix the compare and the store are skipped and the guidance's own demand stands. */
static int d8_logged;
static int fix_d8_clamp(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!d8_logged) {
        d8_logged = 1;
        dos_log(m, "[fix D8] bomb pitch clamp skipped @%llu\n", (unsigned long long)c->icount);
    }
    c->op_cs = c->seg[S_CS];
    c->op_ip = c->ip;
    c->ip = 0x6C1D;
    c->icount += 2;                  /* the compare and the jle */
    return 1;
}

/* D6, "stealth mountains". VGAME's detection (0x5582) multiplies range, bias and the cover of the
 * player's own 2,048-unit sector, cover = [0xB1A0 + (x >> 11) + ((y >> 11) << 4)] & 0x0C. Twenty sectors
 * of the shipped worlds (12 in CE, 7 in NC, 1 in CU) have cover 0, so nothing can ever detect the player in
 * them, from any bearing: flat ground, no base or carrier among them. Not proven a defect (docs/bugs.md D6);
 * with the fix a cover of 0 reads as the lowest nonzero value, 4, so those sectors are no better than any
 * other. The two instructions at 0x55EB (the load and the AND) are replaced. */
static int d6_logged;
static int fix_d6_cover(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t off = (uint16_t)(c->r[R_BX] + c->r[R_SI] + 0xB1A0);
    if (seg_read16(c, c->seg[S_DS], off) & 0x0C) return 0;       /* any other sector: the original's load and AND */
    if (!d6_logged) {
        d6_logged = 1;
        dos_log(m, "[fix D6] cover 0 read as 4 @%llu (sector byte at DS:%04X)\n", (unsigned long long)c->icount, off);
    }
    c->op_cs = c->seg[S_CS];
    c->op_ip = c->ip;
    c->r[R_AX] = 4;
    /* The flags an AND leaves for the result 4: CF, OF, PF, ZF, SF clear (the IMUL that follows overwrites them). */
    c->flags &= (uint16_t)~(0x0001u | 0x0004u | 0x0040u | 0x0080u | 0x0800u);
    c->ip = 0x55F2;
    c->icount += 2;                  /* the load and the AND */
    return 1;
}

/* D3. SETUP's sound question reads a key through MISC.EXE's DOS read (INT 21h
 * AH=01h, which leaves AH 1) and quits to DOS when the word is 100h
 * (0x042D-0x0437): the 0 an extended key reads as first. With NumLock off a
 * keypad digit is an extended key - keypad 1 is End, 4F00h - so it ends the
 * game where the main-row digit selects a driver. The fix takes a keypad
 * key's scan code, which CON holds for the next read, and stores the word
 * the main-row digit reads as (0131h for 1); the original compare then runs
 * on it. Any other extended key still quits. */
static const uint8_t D3_KEYPAD[0x53] = {
    [0x47] = '7', [0x48] = '8', [0x49] = '9', [0x4B] = '4', [0x4D] = '6',
    [0x4F] = '1', [0x50] = '2', [0x51] = '3', [0x52] = '0' };
static int d3_logged;
static int fix_d3_keypad(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t at = (uint16_t)(c->r[R_BP] - 0x46);
    const uint8_t scan = m->con_cache;
    if (seg_read16(c, c->seg[S_SS], at) != 0x100 || scan >= sizeof D3_KEYPAD || !D3_KEYPAD[scan]) return 0;
    m->con_cache = 0;
    seg_write16(c, c->seg[S_SS], at, (uint16_t)(0x100 | D3_KEYPAD[scan]));
    if (!d3_logged) {
        d3_logged = 1;
        dos_log(m, "[fix D3] keypad scan %02X read as '%c' @%llu\n", scan, D3_KEYPAD[scan], (unsigned long long)c->icount);
    }
    return 0;                        /* the original compare, on the digit */
}

static const recomp_override OVERRIDES[] = {
    { "D5", "VGAME.EXE", VGAME_47304, 0x0000, 0x6D2E, fix_d5, "the supply-drop impact gate" },
    { "D1", "VGAME.EXE", VGAME_47304, 0x0000, 0x441D, fix_d1_pace, "frames paced so S never reaches the unstable range" },
    { "D4", "START.EXE", START_47304, 0x0000, 0x8EDC, fix_d4_table, "START's entry: airstrip mission masks" },
    { "D34", "VGAME.EXE", VGAME_47304, 0x0000, 0xE6BE, fix_d34_start, "VGAME's entry: empty extension" },
    { "D34", "VGAME.EXE", VGAME_47304, 0x0000, 0x0F97, fix_d34_append, "destroyed-object append" },
    { "D34", "VGAME.EXE", VGAME_47304, 0x0000, 0x0FBD, fix_d34_lookup, "destroyed-object lookup" },
    { "D34", "VGAME.EXE", VGAME_47304, 0x0000, 0x0F7E, fix_d34_set_type, "found record's type, written" },
    { "D34", "VGAME.EXE", VGAME_47304, 0x0000, 0x0D5E, fix_d34_get_type, "found record's type, read" },
    { "D2", "ASOUND.117", ASOUND_47304, 0x01B6, 0x09F2, fix_d2_speech, "AdLib speech without the busy-wait" },
    { "D12", "END.EXE", END_47304, 0x0000, 0x0443, fix_d12_best, "END's best-rating compare, signed" },
    { "D12", "END.EXE", END_47304, 0x0000, 0x0450, fix_d12_total, "END's rating total, sign-extended" },
    { "D8", "VGAME.EXE", VGAME_47304, 0x0000, 0x6C0F, fix_d8_clamp, "the laser-guided bomb's pitch clamp, skipped" },
    { "D6", "VGAME.EXE", VGAME_47304, 0x0000, 0x55EB, fix_d6_cover, "detection: a sector cover of 0 reads as 4" },
    { "D3", "SETUP.EXE", SETUP_47304, 0x0000, 0x042D, fix_d3_keypad, "the sound question: a keypad digit, not a quit" },
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
    { "D1", "frames are paced on fast machines, so the frame-rate controller never oscillates" },
    { "D2", "AdLib speech plays without stopping the game or risking its busy-wait hang" },
    { "D11", "saves are written to a temporary file and renamed into place, so an interrupted save keeps the old roster" },
    { "D4", "secret-airstrip missions in Libya, North Cape and the Middle East" },
    { "D5", "supply drops earn their delivery credit" },
    { "D12", "END's best-rating and total tally treats ratings as signed" },
    { "D34", "the destroyed-object table keeps records past 30 without overwriting" },
    { "D8", "the laser-guided bomb's guidance can flatten its dive: the 11.25-degree pitch clamp is skipped" },
    { "D3", "keypad digits answer SETUP's sound question with NumLock off, instead of quitting to DOS" },
    { "D6", "no terrain sector hides the player completely: a detection cover of 0 reads as the lowest nonzero cover" },
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
    dos_atomic_saves = fix_on("D11");
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
