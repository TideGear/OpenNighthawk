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
#include "dos_internal.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir_(p) _mkdir(p)
#else
#include <sys/stat.h>
#define mkdir_(p) mkdir(p, 0755)
#endif

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

void dos_ok(cpu_t *c) { c->flags = (uint16_t)(c->flags & (uint16_t)(0xFFFFu ^ F_CF)); }
void dos_fail(cpu_t *c, uint16_t err) { c->flags |= F_CF; c->r[R_AX] = err; }

uint16_t dos_current_psp(const machine_t *m)
{
    return m->nproc ? m->procs[m->nproc - 1].psp_seg : 0;
}

const char *dos_current_program(const machine_t *m)
{
    return m->nproc ? m->procs[m->nproc - 1].name : "(none)";
}

void dos_guest_str(cpu_t *c, uint16_t seg, uint16_t off, char *out, size_t n)
{
    size_t i = 0;
    while (i + 1 < n) {
        uint8_t ch = mem_read8(c, phys(seg, (uint16_t)(off + i)));
        if (!ch) break;
        out[i++] = (char)ch;
    }
    out[i] = 0;
}

void dos_guest_write(cpu_t *c, uint32_t lin, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) mem_write8(c, lin + (uint32_t)i, p[i]);
}

/* Wait inside a BIOS call: point IP back at the INT instruction and idle.
 * Interrupts are taken while waiting whatever the caller's IF, as they are
 * in the ROM routine (which enables them on its own stack frame); the
 * caller's flags come back unchanged when the call finally completes. */
int dos_bios_wait(cpu_t *c)
{
    c->ip = c->op_ip;
    c->seg[S_CS] = c->op_cs;
    c->halted = 2;
    return 1;
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
        dos_wall_clock(m, &t, NULL);
        c->r[R_CX] = (uint16_t)((dos_bcd((unsigned)t.tm_hour) << 8) | dos_bcd((unsigned)t.tm_min));
        c->r[R_DX] = (uint16_t)(dos_bcd((unsigned)t.tm_sec) << 8);
        dos_ok(c);
        break;
    }
    case 0x04: {                             /* RTC date */
        struct tm t;
        dos_wall_clock(m, &t, NULL);
        unsigned y = (unsigned)t.tm_year + 1900u;
        c->r[R_CX] = (uint16_t)((dos_bcd(y / 100) << 8) | dos_bcd(y % 100));
        c->r[R_DX] = (uint16_t)((dos_bcd((unsigned)t.tm_mon + 1) << 8) | dos_bcd((unsigned)t.tm_mday));
        dos_ok(c);
        break;
    }
    default: break;
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

/* What DOSBox runs around each service's callback (src/cpu/callback.cpp):
 * CB_IRET (callback, IRET), the STI kinds CB_IRET_STI/CB_INT16/CB_INT21
 * (STI, callback, IRET: interrupts are taken between the callback and the
 * IRET), CB_MOUSE (a jump, callback, IRET). Measured against it with
 * tools/fidelity.py: 2, 3 and 3 instructions after the INT. */
enum { KIND_IRET, KIND_STI, KIND_JMP };
static const uint8_t SERVICE_KIND[] = { KIND_IRET, KIND_STI, KIND_STI, KIND_STI, KIND_JMP };
#define STUB_IRET     0x09
#define STUB_INT9     0x80
#define STUB_INT8     0xA0
#define STUB_OVERHEAD 0xC0

/* INT 21h calls DOSBox follows with its overhead loop. */
static int dos_overhead(const cpu_t *c, uint8_t vec)
{
    const uint8_t ah = (uint8_t)(c->r[R_AX] >> 8);
    return vec == 0x21 && (ah == 0x0B || ah == 0x2C || (ah == 0x06 && (c->r[R_DX] & 0xFF) == 0xFF));
}

/* A service called directly (the vector still DOSBox's stub) has run in
 * place at the INT; finish it the way DOSBox's stub would. The IRET kinds
 * cost their instructions with interrupts still off. The STI kinds set
 * up the interrupt frame, enable interrupts and continue at an IRET (or
 * the overhead loop), so an interrupt that is due is taken there, inside
 * the call, as in DOSBox. */
static void finish_direct(machine_t *m, unsigned kind, int overhead)
{
    cpu_t *c = &m->cpu;
    if (kind == KIND_IRET) { c->icount += 2; return; }
    if (kind == KIND_JMP) { c->icount += 3; return; }
    cpu_push16(c, c->flags);
    cpu_push16(c, c->seg[S_CS]);
    cpu_push16(c, c->ip);
    c->flags = (uint16_t)((c->flags | F_IF) & (uint16_t)(0xFFFFu ^ F_TF));
    c->seg[S_CS] = m->iret_seg;
    c->ip = overhead ? STUB_OVERHEAD : STUB_IRET;
    c->icount += 2;                                    /* STI, the callback */
    cpu_irq_state_changed(c);
}

static int service(machine_t *m, uint8_t vec)
{
    machine_inventory_service(vec, m->cpu.r[R_AX]);
    switch (vec) {
    case 0x21: return dos_int21(m);
    case 0x10: return dos_int10(m);
    case 0x16: return dos_int16(m);
    case 0x1A: return int1a(m);
    case 0x33: return mouse_int33(m);
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
            const uint16_t cs0 = c->seg[S_CS], ip0 = c->ip;
            const int over = dos_overhead(c, vec);
            int r = service(m, vec);
            /* Not when the call waits, ends the program or starts another. */
            if (r && !c->halted && !m->exited && c->seg[S_CS] == cs0 && c->ip == ip0)
                finish_direct(m, SERVICE_KIND[k], over);
            return r;
        }
        if (vec == 0xE0 + k) {
            /* The callback in a service's stub, reached through a program's
             * handler that chained to the old vector: the caller's frame is
             * at SS:SP, and the stub ends with that frame's IRET. As DOSBox
             * does (CALLBACK_SCF/SZF), the carry and zero results go into the
             * frame's flags, so the IRET restores everything else as the
             * caller had it. */
            const int over = dos_overhead(c, SERVICES[k]);
            int r = service(m, SERVICES[k]);
            if (c->halted != 2 && !m->exited) {
                const uint16_t at = (uint16_t)(c->r[R_SP] + 4);
                uint16_t fl = seg_read16(c, c->seg[S_SS], at);
                const uint16_t res = F_CF | F_ZF;
                seg_write16(c, c->seg[S_SS], at, (uint16_t)((fl & (uint16_t)~res) | (c->flags & res)));
                if (over && c->seg[S_CS] == m->iret_seg) c->ip = STUB_OVERHEAD;
            }
            return r;
        }
    }
    if (vec == 0x11 || vec == 0x12 || vec == 0x15 || vec == 0x20)
        machine_inventory_service(vec, c->r[R_AX]);
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
        return 1;                              /* the stub calls INT 1Ch */
    }
    case 0xF9: dos_bios_key_irq(m); return 1;      /* the INT 9 stub's translation step */
    case 0x20: return dos_terminate(m, 0);
    case 0x11: c->r[R_AX] = mem_read16(c, BDA_EQUIPMENT); c->icount += 2; return 1;
    case 0x12: c->r[R_AX] = mem_read16(c, BDA_MEM_KB); c->icount += 2; return 1;
    case 0x15:                                 /* system services: none */
        if ((c->r[R_AX] >> 8) != 0x4F)         /* 4Fh, the keyboard intercept: CF set, key kept */
            c->r[R_AX] = (uint16_t)((c->r[R_AX] & 0x00FF) | 0x8600);
        c->flags |= F_CF;
        c->icount += 2;
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
    /* As GOG's DOSBox has it when the game starts (measured: tools/
     * fidelity.py): two serial ports and a printer port, the equipment word
     * that says so (with a game port, 80x25 colour, a coprocessor and a
     * mouse), NumLock off, a 4 KB text page, the CGA mode bytes, two hard
     * disks, the printer and serial timeouts, the VGA's display combination
     * index and the video save pointer. */
    mem_write16(c, 0x400, 0x03F8);               /* COM1, COM2 */
    mem_write16(c, 0x402, 0x02F8);
    mem_write16(c, 0x408, 0x0378);               /* LPT1 */
    mem_write16(c, BDA_EQUIPMENT, 0xD426);
    mem_write16(c, BDA_MEM_KB, 640);
    mem_write16(c, BDA_KBD_HEAD, BDA_KBD_BUF - 0x400);
    mem_write16(c, BDA_KBD_TAIL, BDA_KBD_BUF - 0x400);
    mem_write16(c, BDA_KBD_START, BDA_KBD_BUF - 0x400);
    mem_write16(c, BDA_KBD_END, BDA_KBD_BUF_END - 0x400);
    mem_write8 (c, BDA_SHIFT, 0x00);             /* NumLock off, as DOSBox starts */
    mem_write8 (c, BDA_VIDEO_MODE, 0x03);
    mem_write16(c, BDA_VIDEO_COLS, 80);
    mem_write16(c, BDA_PAGE_SIZE, 0x1000);
    mem_write16(c, BDA_CURSOR_TYPE, 0x0607);
    mem_write16(c, BDA_CRTC_BASE, 0x03D4);
    mem_write8 (c, 0x465, 0x29);                 /* CGA mode select */
    mem_write8 (c, 0x466, 0x30);                 /* CGA palette */
    mem_write8 (c, 0x475, 0x02);                 /* hard disks */
    mem_write8 (c, 0x478, 0x01); mem_write8(c, 0x479, 0x01); mem_write8(c, 0x47A, 0x01);   /* LPT timeouts */
    for (uint32_t a = 0x47C; a < 0x480; a++) mem_write8(c, a, 0x01);                       /* COM timeouts */
    mem_write8 (c, BDA_ROWS_M1, 24);
    mem_write16(c, BDA_CHAR_HEIGHT, 16);
    mem_write8 (c, 0x487, 0x60);
    mem_write8 (c, 0x488, 0x09);
    mem_write8 (c, 0x489, 0x51);
    mem_write8 (c, 0x48A, 0x0B);
    mem_write8 (c, BDA_KBD_FLAGS3, 0x10);        /* 101-key keyboard */
    mem_write8 (c, 0x497, 0x10);
    mem_write16(c, 0x4A8, 0x2E8F);               /* video save pointer table C000:2E8F */
    mem_write16(c, 0x4AA, 0xC000);

    /* The ROM starts the tick count at the time of day it reads from the
     * real-time clock. */
    struct tm t;
    unsigned cs;
    dos_wall_clock(m, &t, &cs);
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
    /* The software interrupts nobody handles, INT 1Ch among them: DOSBox's
     * CB_IRET, a callback that does nothing and an IRET - two instructions
     * (NOP; IRET). The IRET alone, at +09h, ends the services' stubs. */
    mem_write8(c, phys(iret_seg, 0x08), 0x90);
    mem_write8(c, phys(iret_seg, STUB_IRET), 0xCF);
    for (int v = 0; v < 256; v++) {
        int hw = (v >= 8 && v <= 0x0F);
        mem_write16(c, (uint32_t)v * 4, hw ? 0x0000 : 0x0008);
        mem_write16(c, (uint32_t)v * 4 + 2, iret_seg);
    }
    /* INT 9, as DOSBox's CB_IRQ1: the scancode offered to INT 15h/4Fh, then
     * translated (INT F9h, the callback) unless the intercept cleared CF,
     * then the EOI. */
    static const uint8_t stub9[] = { 0x50, 0xE4, 0x60, 0xB4, 0x4F, 0xF9, 0xCD, 0x15, 0x73, 0x02, 0xCD, 0xF9,
                                     0xFA, 0xB0, 0x20, 0xE6, 0x20, 0x58, 0xCF };
    for (size_t i = 0; i < sizeof stub9; i++) mem_write8(c, phys(iret_seg, (uint16_t)(STUB_INT9 + i)), stub9[i]);
    mem_write16(c, 9 * 4, STUB_INT9);
    /* INT 8, as DOSBox's CB_IRQ0: the tick (INT F8h, the callback), then
     * INT 1Ch from guest code, then the EOI. */
    static const uint8_t stub8[] = { 0xCD, 0xF8, 0x50, 0x52, 0x1E, 0xCD, 0x1C, 0xFA, 0x1F, 0x5A,
                                     0xB0, 0x20, 0xE6, 0x20, 0x58, 0xCF };
    for (size_t i = 0; i < sizeof stub8; i++) mem_write8(c, phys(iret_seg, (uint16_t)(STUB_INT8 + i)), stub8[i]);
    mem_write16(c, 8 * 4, STUB_INT8);
    /* DOSBox's DOS overhead for the timing-sensitive calls (INT 21h 06h/FFh,
     * 0Bh, 2Ch): PUSH CX; MOV CX,140h; LOOP $; POP CX; IRET. */
    static const uint8_t over[] = { 0x51, 0xB9, 0x40, 0x01, 0xE2, 0xFE, 0x59, 0xCF };
    for (size_t i = 0; i < sizeof over; i++) mem_write8(c, phys(iret_seg, (uint16_t)(STUB_OVERHEAD + i)), over[i]);
    /* The service vectors, each with DOSBox's instructions around its
     * callback (see dos_int_hook): an STI first for INT 16h, 1Ah and 21h, a
     * jump first for INT 33h; then the callback (INT E0h+k) and an IRET. */
    for (unsigned k = 0; k < sizeof SERVICES; k++) {
        const uint16_t at = SERVICE_STUB(k);
        uint8_t s[5];
        int n = 0;
        if (SERVICE_KIND[k] == KIND_STI) s[n++] = 0xFB;
        if (SERVICE_KIND[k] == KIND_JMP) s[n++] = 0x90;
        s[n++] = 0xCD; s[n++] = (uint8_t)(0xE0 + k); s[n++] = 0xCF;
        for (int i = 0; i < n; i++) mem_write8(c, phys(iret_seg, (uint16_t)(at + i)), s[i]);
        mem_write16(c, (uint32_t)SERVICES[k] * 4, at);
    }
    init_bios_data_area(m);

    /* DOS as GOG's DOSBox leaves it when its shell starts the game
     * (measured: tools/fidelity.py). The shell's PSP is at 0118h, its
     * environment at 012Bh; INT 23h runs the shell PSP's INT 20h, INT 24h
     * jumps to the BIOS's default handler (an IRET at F000:1060). The
     * memory chain starts at 016Fh: a DOS block, a free hole of four
     * paragraphs, a block DOSBox keeps for itself, then everything else up
     * to 9FFFh, where a system block covers the way to the upper memory
     * block at D000h. The game is then EXEC'd from the shell, so its
     * environment, PSP and memory come out where DOSBox puts them. */
    static const uint16_t SHELL_PSP = 0x0118, SHELL_ENV = 0x012B;
    mem_write8(c, 0xF1060, 0xCF);
    {
        uint8_t *p = &m->mem[(uint32_t)SHELL_PSP * 16u];
        static const uint8_t head[0x18] = {
            0xCD, 0x20, 0x18, 0x01, 0x00, 0xEA, 0xFF, 0xFF, 0xAD, 0xDE, 0x60, 0x10,
            0x00, 0xF0, 0x00, 0x00, 0x18, 0x01, 0x10, 0x01, 0x18, 0x01, 0x18, 0x01 };
        for (int i = 0; i < 0x18; i++) mem_write8(c, (uint32_t)SHELL_PSP * 16u + (uint32_t)i, head[i]);
        static const uint8_t jft[5] = { 1, 1, 1, 0, 2 };
        for (int i = 0; i < 20; i++) mem_write8(c, phys(SHELL_PSP, (uint16_t)(0x18 + i)), (uint8_t)(i < 5 ? jft[i] : 0xFF));
        mem_write16(c, phys(SHELL_PSP, 0x2C), SHELL_ENV);
        mem_write16(c, phys(SHELL_PSP, 0x32), 20);
        mem_write16(c, phys(SHELL_PSP, 0x34), 0x18);
        mem_write16(c, phys(SHELL_PSP, 0x36), SHELL_PSP);
        mem_write16(c, phys(SHELL_PSP, 0x38), 0xFFFF);
        mem_write16(c, phys(SHELL_PSP, 0x3A), 0xFFFF);
        mem_write16(c, phys(SHELL_PSP, 0x40), 0x0005);
        mem_write8(c, phys(SHELL_PSP, 0x50), 0xCD); mem_write8(c, phys(SHELL_PSP, 0x51), 0x21); mem_write8(c, phys(SHELL_PSP, 0x52), 0xCB);
        for (int f = 0; f < 2; f++)
            for (int i = 1; i <= 11; i++) mem_write8(c, phys(SHELL_PSP, (uint16_t)(0x5C + 16 * f + i)), ' ');
        static const char tail[] = "\x12/INIT AUTOEXEC.BAT";
        for (int i = 0; tail[i]; i++) mem_write8(c, phys(SHELL_PSP, (uint16_t)(0x80 + i)), (uint8_t)tail[i]);
        static const uint8_t int24[5] = { 0xEA, 0x60, 0x10, 0x00, 0xF0 };      /* jmp far F000:1060 */
        for (int i = 0; i < 5; i++) mem_write8(c, phys(SHELL_PSP, (uint16_t)(0x110 + i)), int24[i]);
        (void)p;
        /* The shell's environment and the block it lives in. */
        mem_write8(c, phys(0x012A, 0), 'M'); mem_write16(c, phys(0x012A, 1), SHELL_PSP); mem_write16(c, phys(0x012A, 3), 0x44);
        static const char env[] = "PATH=Z:\\\0COMSPEC=Z:\\COMMAND.COM\0\0\x01\0Z:\\COMMAND.COM";
        for (size_t i = 0; i < sizeof env; i++) mem_write8(c, phys(SHELL_ENV, (uint16_t)i), (uint8_t)env[i]);
        m->env_seg = SHELL_ENV;
        mem_write16(c, 0x23 * 4, 0x0000); mem_write16(c, 0x23 * 4 + 2, SHELL_PSP);
        mem_write16(c, 0x24 * 4, 0x0110); mem_write16(c, 0x24 * 4 + 2, SHELL_PSP);
    }
    m->first_mcb = 0x016F;
    mem_write16(c, phys(0x0080, 0x0024), m->first_mcb);       /* the list of lists' first MCB */
    {
        struct { uint16_t seg; uint8_t type; uint16_t owner, size; const char *name; } chain[] = {
            { 0x016F, 'M', 0x0008, 0x0001, NULL },
            { 0x0171, 'M', 0x0000, 0x0004, NULL },
            { 0x0176, 'M', 0x0040, 0x0010, NULL },
            { 0x0187, 'Z', 0x0000, 0x9E77, NULL },
            { 0x9FFF, 'M', 0x0008, 0x3000, "SC      " },
            { 0xD000, 'Z', 0x0000, 0x0FFF, NULL },
        };
        for (size_t k = 0; k < sizeof chain / sizeof chain[0]; k++) {
            dos_mcb_set_type(m, chain[k].seg, chain[k].type);
            dos_mcb_set_owner(m, chain[k].seg, chain[k].owner);
            dos_mcb_set_size(m, chain[k].seg, chain[k].size);
            if (chain[k].name) dos_mcb_set_name(m, chain[k].seg, (const uint8_t *)chain[k].name);
        }
    }
    m->sft_ref[0] = 1; m->sft_ref[1] = 3; m->sft_ref[2] = 1;      /* AUX, CON, PRN in the shell */

    m->files[0].in_use = m->files[1].in_use = m->files[2].in_use = 1;
    m->files[0].is_device = m->files[1].is_device = m->files[2].is_device = 1;
    m->files[3].in_use = m->files[4].in_use = 1;
    m->files[3].is_device = m->files[4].is_device = 1;
    m->mouse_present = 1;
    mouse_new_video_mode(m);

    if (m->save_dir[0]) mkdir_(m->save_dir);

    exec_params ep;
    memset(&ep, 0, sizeof(ep));
    ep.parent_psp = SHELL_PSP;
    ep.parent_env = SHELL_ENV;
    ep.flags = 0x0202;
    ep.fcb1_seg = ep.fcb2_seg = SHELL_PSP;        /* the shell's parsed (blank) FCBs */
    ep.fcb1_off = 0x5C;
    ep.fcb2_off = 0x6C;
    dos_proc *root = &m->procs[0];
    memset(root, 0, sizeof(*root));
    snprintf(root->name, sizeof(root->name), "%s", dos_basename(program));
    root->psp_seg = SHELL_PSP;                     /* the shell is current while it EXECs */
    m->nproc = 1;
    uint16_t psp = 0;
    uint16_t err = dos_load_program(m, program, &ep, 0xF000, 0x20C8, &psp);
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
