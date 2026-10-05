/* pc.c - the devices the game touches, timed by the CPU's instruction count.
 *
 * The 8259 interrupt controller, the 8253 timer, the keyboard controller,
 * the VGA's registers and DAC, the AdLib's timers, the MPU-401 in UART mode,
 * the analog game port and the speaker gate.
 *
 * The device models began as the Reimp oracle's (tools/x86oracle/dos.c at
 * cfb8cec9), whose comments record why each one has to move rather than
 * return a constant: the PIT read-back that a sound driver's speed
 * calibration divides by, the OPL status a card detection waits on, the
 * game-port one-shots a paddle routine counts. What changed here is that
 * the oracle was built to run scripted captures, so it took shortcuts a
 * playable machine cannot: it had no interrupt mask, delivered keys on
 * timer ticks, kept only one timer counter's state, and gated nothing. This
 * file models the parts as the hardware behaves.
 */
#include "machine.h"
#include "x86_sem.h"

#include <stdlib.h>
#include <string.h>

#define PIT_HZ 1193182ull

static void wake(machine_t *m) { m->cpu.stop_at = 0; }

/* a * b / d without overflowing 64 bits for any a the machine will reach */
static uint64_t muldiv(uint64_t a, uint64_t b, uint64_t d)
{
    return (a / d) * b + ((a % d) * b) / d;
}

uint64_t machine_now_us(const machine_t *m)
{
    return muldiv(m->cpu.icount, 1000000ull, m->ips);
}

uint64_t pc_pit_clock(const machine_t *m)
{
    return muldiv(m->cpu.icount, PIT_HZ, m->ips);
}

/* The first icount at which the PIT clock has reached `clk`. */
static uint64_t icount_at_clock(const machine_t *m, uint64_t clk)
{
    uint64_t ic = muldiv(clk, m->ips, PIT_HZ);
    while (muldiv(ic, PIT_HZ, m->ips) < clk) ic++;
    return ic;
}

/* ===================================================================== */
/* 8259                                                                  */
/* ===================================================================== */

/* The IRQ the controller would hand the CPU now, or -1: the lowest-numbered
 * request that is unmasked and not outranked by one already in service. */
static int pic_pending(const machine_t *m)
{
    uint8_t req = (uint8_t)(m->pic_irr & (uint8_t)~m->pic_imr);
    for (int n = 0; n < 8; n++) {
        if (m->pic_isr & (1u << n)) return -1;   /* in service at equal or higher priority */
        if (req & (1u << n)) return n;
    }
    return -1;
}

static void pic_raise(machine_t *m, int n)
{
    m->pic_irr |= (uint8_t)(1u << n);
}

static void pic_eoi(machine_t *m, int specific, int n)
{
    if (specific) { m->pic_isr &= (uint8_t)~(1u << n); return; }
    for (int i = 0; i < 8; i++)
        if (m->pic_isr & (1u << i)) { m->pic_isr &= (uint8_t)~(1u << i); return; }
}

/* ===================================================================== */
/* 8253                                                                  */
/* ===================================================================== */

static uint32_t pit_full(const pit_counter *p)
{
    return p->reload ? p->reload : 65536u;
}

/* Is counter `ch` counting? Counters 0 and 1 have their gates tied high;
 * counter 2's is port 61 bit 0. */
static int pit_gate(const machine_t *m, int ch)
{
    return ch != 2 || (m->port61 & 1);
}

/* The count a latch would capture now. */
static uint16_t pit_count(const machine_t *m, int ch)
{
    const pit_counter *p = &m->pit[ch];
    const uint32_t full = pit_full(p);
    uint64_t now = pc_pit_clock(m);
    uint64_t t = now > p->epoch_clk ? now - p->epoch_clk : 0;
    if (p->null_count) return (uint16_t)full;
    switch (p->mode & 7) {
    case 2: case 6:
        return (uint16_t)(full - (uint32_t)(t % full));
    case 3: case 7: {
        /* Square wave: the count steps by two and runs its range twice a
         * period (an odd count loses one on alternate halves; the
         * difference is invisible at the precision anything reads). */
        uint32_t half = full / 2 ? full / 2 : 1;
        uint32_t v = full - 2u * (uint32_t)(t % half);
        return (uint16_t)(v & 0xFFFEu);
    }
    default:   /* 0, 1, 4, 5: one pass down, then wrapping */
        return (uint16_t)((full - (uint32_t)(t % 65536u)) & 0xFFFF);
    }
}

/* The counter's OUT pin. Only counter 2's is visible (port 61 bit 5) and
 * audible (the speaker). */
static int pit_out(const machine_t *m, int ch)
{
    const pit_counter *p = &m->pit[ch];
    if (!pit_gate(m, ch) && ((p->mode & 3) == 2 || (p->mode & 3) == 3)) return 1;
    const uint32_t full = pit_full(p);
    uint64_t now = pc_pit_clock(m);
    uint64_t t = now > p->epoch_clk ? now - p->epoch_clk : 0;
    switch (p->mode & 7) {
    case 0: return t >= full;
    case 2: case 6: return (t % full) != full - 1;
    case 3: case 7: return (t % full) < (full + 1) / 2;
    default: return 1;
    }
}

/* Schedule counter 0's next interrupt edge after the present. */
static void pit0_schedule(machine_t *m)
{
    pit_counter *p = &m->pit[0];
    const uint64_t full = pit_full(p);
    if ((p->mode & 3) != 2 && (p->mode & 3) != 3) {
        /* modes 0/1/4/5: one edge at terminal count, then none */
        if ((p->mode & 7) == 0 && !p->null_count) {
            uint64_t at = icount_at_clock(m, p->epoch_clk + full);
            m->irq0_next = at > m->cpu.icount ? at : ~0ull;
        } else {
            m->irq0_next = ~0ull;
        }
        return;
    }
    const uint64_t now = pc_pit_clock(m);
    uint64_t k = now > p->epoch_clk ? (now - p->epoch_clk) / full + 1 : 1;
    m->irq0_next = icount_at_clock(m, p->epoch_clk + k * full);
}

static void pit_loaded(machine_t *m, int ch)
{
    pit_counter *p = &m->pit[ch];
    p->null_count = 0;
    p->epoch_clk = pc_pit_clock(m);
    if (ch == 0) {
        pit0_schedule(m);
        wake(m);
        /* The rate the program chose, and when: the timing fact most
         * comparable with a real machine or DOSBox (F117R_TRACE_PIT). */
        if (m->log && getenv("F117R_TRACE_PIT"))
            dos_log(m, "[pit] counter 0 mode %u reload %u @%llu by %04X:%04X (%s)\n",
                    p->mode, p->reload, (unsigned long long)m->cpu.icount,
                    m->cpu.op_cs, m->cpu.op_ip, dos_current_program(m));
    }
    if (ch == 2 && m->hooks.speaker) m->hooks.speaker(m->hooks.user, m->cpu.icount);
}

static void pit_write(machine_t *m, int ch, uint8_t v)
{
    pit_counter *p = &m->pit[ch];
    switch (p->access) {
    case 1: p->reload = v; pit_loaded(m, ch); break;
    case 2: p->reload = (uint16_t)(v << 8); pit_loaded(m, ch); break;
    default:
        if (!p->write_hi_next) {
            p->reload = (uint16_t)((p->reload & 0xFF00) | v);
            p->write_hi_next = 1;
        } else {
            p->reload = (uint16_t)((p->reload & 0x00FF) | (v << 8));
            p->write_hi_next = 0;
            pit_loaded(m, ch);
        }
        break;
    }
}

static uint8_t pit_read(machine_t *m, int ch)
{
    pit_counter *p = &m->pit[ch];
    uint16_t v;
    if (p->latched) v = p->latch;
    else v = pit_count(m, ch);
    switch (p->access) {
    case 1: p->latched = 0; return (uint8_t)v;
    case 2: p->latched = 0; return (uint8_t)(v >> 8);
    default:
        if (!p->read_hi_next) {
            if (!p->latched) { p->latch = v; p->latched = 1; }
            p->read_hi_next = 1;
            return (uint8_t)p->latch;
        }
        p->read_hi_next = 0;
        p->latched = 0;
        return (uint8_t)(p->latch >> 8);
    }
}

static void pit_control(machine_t *m, uint8_t v)
{
    int ch = (v >> 6) & 3;
    if (ch == 3) return;                       /* 8254 read-back: not on an 8253 */
    pit_counter *p = &m->pit[ch];
    if ((v & 0x30) == 0) {                     /* counter latch command */
        if (!p->latched) {
            p->latch = pit_count(m, ch);
            p->latched = 1;
            p->read_hi_next = 0;
        }
        return;
    }
    /* DOSBox raises IRQ0 when a control word reaches counter 0 while its
     * output is low (cancels it for mode 0). Its core takes the interrupt at
     * the next STI/IRET/POPF or slice end, not at once, so a following INT
     * 21h entry (STI first) sees it before the call's work is done. */
    if (ch == 0 && m->pit_control_irq) {
        const unsigned mode = (v >> 1) & 7, nm = mode > 5 ? mode - 4 : mode;
        if (nm == 0) { m->pic_irr &= 0xFEu; m->irq0_held = 0; }
        else if (!p->null_count && !pit_out(m, 0)) {
            m->irq0_held = 1;
            m->irq0_hold_until = m->cpu.icount + 25;
        }
    }
    p->access = (uint8_t)((v >> 4) & 3);
    p->mode = (uint8_t)((v >> 1) & 7);
    p->write_hi_next = 0;
    p->read_hi_next = 0;
    p->latched = 0;
    p->null_count = 1;                         /* stops until a count is written */
    if (ch == 0) { m->irq0_next = ~0ull; wake(m); }
    if (ch == 2 && m->hooks.speaker) m->hooks.speaker(m->hooks.user, m->cpu.icount);
}

int pc_speaker_state(const machine_t *m, uint16_t *reload, int *mode)
{
    if (reload) *reload = m->pit[2].reload;
    if (mode) *mode = m->pit[2].mode;
    return m->port61 & 3;
}

/* ===================================================================== */
/* AdLib (the timers and status a driver can observe)                    */
/* ===================================================================== */

static void opl_tick(machine_t *m, uint64_t us)
{
    if (m->opl_t1_run && us >= m->opl_t1_due) {
        m->opl_t1_flag = 1;
        uint64_t per = (256u - m->opl_t1_preset) * 80ull;
        while (m->opl_t1_due <= us) m->opl_t1_due += per;   /* free-running */
    }
    if (m->opl_t2_run && us >= m->opl_t2_due) {
        m->opl_t2_flag = 1;
        uint64_t per = (256u - m->opl_t2_preset) * 320ull;
        while (m->opl_t2_due <= us) m->opl_t2_due += per;
    }
}

static uint8_t opl_status(machine_t *m)
{
    opl_tick(m, machine_now_us(m));
    uint8_t s = 0;
    if (m->opl_t1_flag && !m->opl_t1_mask) s |= 0x40;
    if (m->opl_t2_flag && !m->opl_t2_mask) s |= 0x20;
    if (s) s |= 0x80;
    return (uint8_t)(s | 0x06);   /* an OPL2 reads 06 in the unused bits */
}

static void opl_write_reg(machine_t *m, uint8_t reg, uint8_t val);

/* Channel 0's registers: both operators (0 and 3) and the channel's own. */
static int opl_channel0(uint8_t reg)
{
    switch (reg & 0xE0) {
    case 0x20: case 0x40: case 0x60: case 0x80: case 0xE0: return (reg & 0x1F) == 0 || (reg & 0x1F) == 3;
    default: return reg == 0xA0 || reg == 0xB0 || reg == 0xC0;
    }
}

int machine_opl_schedule(machine_t *m, uint64_t at, uint8_t reg, uint8_t val, uint32_t shadow)
{
    if (m->opl_sched_i == m->opl_sched_n) m->opl_sched_i = m->opl_sched_n = 0;
    if (m->opl_sched_n == m->opl_sched_cap) {
        const uint32_t cap = m->opl_sched_cap ? m->opl_sched_cap * 2 : 4096;
        void *p = realloc(m->opl_sched, cap * sizeof *m->opl_sched);
        if (!p) return 0;
        m->opl_sched = p;
        m->opl_sched_cap = cap;
    }
    m->opl_sched[m->opl_sched_n++] = (struct machine_opl_event){ at, shadow, reg, val };
    if (m->opl_sched_n == m->opl_sched_i + 1) wake(m);
    return 1;
}

uint64_t machine_opl_scheduled_until(const machine_t *m)
{
    return m->opl_sched_i < m->opl_sched_n ? m->opl_sched[m->opl_sched_n - 1].at : 0;
}

static void opl_sched_issue(machine_t *m, uint64_t now)
{
    while (m->opl_sched_i < m->opl_sched_n && m->opl_sched[m->opl_sched_i].at <= now) {
        const struct machine_opl_event *e = &m->opl_sched[m->opl_sched_i++];
        const uint8_t val = e->shadow ? m->mem[e->shadow] : e->val;
        const uint64_t keep = m->cpu.icount;
        m->cpu.icount = e->at;                 /* the hook stamps its own time */
        opl_write_reg(m, e->reg, val);
        m->cpu.icount = keep;
    }
}

static void opl_write_reg(machine_t *m, uint8_t reg, uint8_t val)
{
    const uint64_t us = machine_now_us(m);
    switch (reg) {
    case 0x02: m->opl_t1_preset = val; break;
    case 0x03: m->opl_t2_preset = val; break;
    case 0x04:
        if (val & 0x80) {                 /* IRQ reset clears both flags */
            m->opl_t1_flag = m->opl_t2_flag = 0;
            break;                        /* and does nothing else */
        }
        m->opl_t1_mask = (val & 0x40) != 0;
        m->opl_t2_mask = (val & 0x20) != 0;
        if ((val & 1) && !m->opl_t1_run)
            m->opl_t1_due = us + (256u - m->opl_t1_preset) * 80ull;
        if ((val & 2) && !m->opl_t2_run)
            m->opl_t2_due = us + (256u - m->opl_t2_preset) * 320ull;
        m->opl_t1_run = (val & 1) != 0;
        m->opl_t2_run = (val & 2) != 0;
        break;
    default: break;
    }
    if (m->hooks.opl_write) m->hooks.opl_write(m->hooks.user, m->cpu.icount, reg, val);
}

/* ===================================================================== */
/* Game port                                                             */
/* ===================================================================== */

/* One axis's one-shot: 24.2us at rest, 0.011us per ohm across a 100k pot. */
static uint64_t joy_us(unsigned axis)
{
    if (axis > 255) axis = 255;
    return 24ull + (unsigned long long)axis * 1100ull / 255ull;
}

static void joy_trigger(machine_t *m)
{
    const uint64_t now = machine_now_us(m);
    for (int i = 0; i < 4; i++)
        m->joy_due[i] = m->joy_axis[i] >= 0x100 ? UINT64_MAX : now + joy_us(m->joy_axis[i]);
}

static uint8_t joy_read(machine_t *m)
{
    if (!m->joy_present) return 0xFF;     /* no stick: nothing answers the port (DOSBox, a PC) */
    const uint64_t now = machine_now_us(m);
    uint8_t v = 0;
    for (int i = 0; i < 4; i++)
        if (m->joy_due[i] > now) v |= (uint8_t)(1u << i);
    v |= (uint8_t)((~m->joy_buttons & 0x0Fu) << 4);   /* active low */
    return v;
}

static void joy_apply(machine_t *m, int present, const unsigned axis[4], unsigned buttons)
{
    m->joy_present = present;
    for (int i = 0; i < 4; i++) m->joy_axis[i] = axis ? axis[i] : 0x100;
    m->joy_buttons = buttons;
}

/* ===================================================================== */
/* VGA                                                                   */
/* ===================================================================== */

/* The standard VGA BIOS palette that a mode set loads: the sixteen CGA
 * colours, sixteen greys, then a hue ring of 24 at three saturations and
 * three intensities, then eight blacks. */
static void vga_default_palette(machine_t *m)
{
    static const uint8_t cga[16][3] = {
        {0,0,0},{0,0,42},{0,42,0},{0,42,42},{42,0,0},{42,0,42},{42,21,0},{42,42,42},
        {21,21,21},{21,21,63},{21,63,21},{21,63,63},{63,21,21},{63,21,63},{63,63,21},{63,63,63}};
    static const uint8_t grey[16] = {0,5,8,11,14,17,20,24,28,32,36,40,45,50,56,63};
    static const uint8_t lv[9][5] = {
        {0,16,31,47,63},{31,39,47,55,63},{45,49,54,58,63},
        {0,7,14,21,28},{14,17,21,24,28},{20,22,24,26,28},
        {0,4,8,12,16},{8,10,12,14,16},{11,12,13,15,16}};
    static const uint8_t ring[24][3] = {
        {0,0,4},{1,0,4},{2,0,4},{3,0,4},{4,0,4},{4,0,3},{4,0,2},{4,0,1},
        {4,0,0},{4,1,0},{4,2,0},{4,3,0},{4,4,0},{3,4,0},{2,4,0},{1,4,0},
        {0,4,0},{0,4,1},{0,4,2},{0,4,3},{0,4,4},{0,3,4},{0,2,4},{0,1,4}};
    memset(m->dac, 0, sizeof m->dac);
    for (int i = 0; i < 16; i++) memcpy(&m->dac[i * 3], cga[i], 3);
    for (int i = 0; i < 16; i++) memset(&m->dac[(16 + i) * 3], grey[i], 3);
    for (int b = 0; b < 9; b++)
        for (int i = 0; i < 24; i++)
            for (int k = 0; k < 3; k++)
                m->dac[(32 + b * 24 + i) * 3 + k] = lv[b][ring[i][k]];
}

/* The registers a mode set leaves, as GOG's DOSBox (machine=svga_s3) leaves
 * them (measured: tools/fidelity.py): sequencer 0-4, CRTC 0-18h, graphics
 * 0-8, attribute 0-14h, miscellaneous output. */
typedef struct {
    uint8_t seq[5], crtc[0x19], gc[9], attr[0x15], misc;
} vga_regs;

static const vga_regs VGA_MODE3 = {
    { 0x00, 0x00, 0x03, 0x00, 0x07 },
    { 0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F, 0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00,
      0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x0F, 0xFF },
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
      0x0C, 0x00, 0x0F, 0x08, 0x00 },
    0x67
};
static const vga_regs VGA_MODE13 = {
    { 0x00, 0x01, 0x0F, 0x00, 0x0E },
    { 0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF },
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
      0x41, 0x00, 0x0F, 0x00, 0x00 },
    0x63
};

static void vga_load_regs(machine_t *m, const vga_regs *r)
{
    memset(m->seq, 0, sizeof m->seq);
    memset(m->gc, 0, sizeof m->gc);
    memset(m->crtc, 0, sizeof m->crtc);
    memset(m->attr, 0, sizeof m->attr);
    memcpy(m->seq, r->seq, sizeof r->seq);
    memcpy(m->crtc, r->crtc, sizeof r->crtc);
    memcpy(m->gc, r->gc, sizeof r->gc);
    memcpy(m->attr, r->attr, sizeof r->attr);
    m->misc_out = r->misc;
}

void vga_set_mode(machine_t *m, uint8_t mode, int clear)
{
    m->video_mode = mode;
    m->pel_mask = 0xFF;
    if (mode == 0x13) {
        vga_load_regs(m, &VGA_MODE13);
        vga_default_palette(m);
        if (clear) memset(m->mem + 0xA0000, 0, 0x10000);
    } else {
        vga_load_regs(m, &VGA_MODE3);
        vga_default_palette(m);
        if (clear) {
            for (uint32_t i = 0; i < 80 * 25; i++) {
                m->mem[0xB8000 + i * 2] = ' ';
                m->mem[0xB8000 + i * 2 + 1] = 0x07;
            }
        }
    }
    memcpy(m->dac_display, m->dac, sizeof m->dac_display);
    mouse_new_video_mode(m);
    m->scan_valid = 0;
    m->scan_latch = 0;
    m->scan_part = 0;
    m->scan_next = ~0ull;
    if (mode == 0x13 && m->frame_len) {
        /* Start with the next complete frame, not a partial old mode. */
        m->scan_frame = (m->cpu.icount / m->frame_len + 1) * m->frame_len;
        m->scan_next = m->scan_frame;
    }
    if (m->log) dos_log(m, "[video] mode %02Xh set by %s\n", mode, dos_current_program(m));
}

uint16_t vga_start_address(const machine_t *m)
{
    return (uint16_t)((m->crtc[0x0C] << 8) | m->crtc[0x0D]);
}

/* Input Status Register 1. Mode 13h on a VGA: 449 lines a frame at
 * 31.469 kHz, 400 of them shown, vertical retrace on lines 412 and 413.
 * Bit 0 is set whenever the beam is not drawing. */
static uint8_t vga_status(machine_t *m)
{
    const uint64_t frame = m->frame_len;
    const uint64_t at = (m->cpu.icount % frame) * 449ull;
    const unsigned line = (unsigned)(at / frame);
    const unsigned across = (unsigned)((at % frame) * 100ull / frame);
    uint8_t v = 0;
    if (line >= 400 || across >= 80) v |= 0x01;
    if (line >= 412 && line < 414) v |= 0x08;
    m->attr_flip = 0;                       /* reading it resets the flip-flop */
    return v;
}

/* ===================================================================== */
/* MPU-401                                                               */
/* ===================================================================== */

static void mpu_queue(machine_t *m, uint8_t value)
{
    if (m->mpu_used < sizeof m->mpu_q) {
        m->mpu_q[(m->mpu_head + m->mpu_used) % sizeof m->mpu_q] = value;
        m->mpu_used++;
    }
}

/* ===================================================================== */
/* Port I/O                                                              */
/* ===================================================================== */

/* DOSBox's CPU budget is split by millisecond, PIT and VGA events. This
 * is also the slice model used to limit DOS file-transfer costs. */
uint64_t pc_slice_left(const machine_t *m)
{
    const uint64_t now = m->cpu.icount;
    const uint64_t per_ms = m->ips / 1000u;
    if (!per_ms) return 1;
    uint64_t left = per_ms - now % per_ms;
    if (m->irq0_next > now && m->irq0_next - now < left)
        left = m->irq0_next - now;
    if (m->frame_len) {
        const uint64_t f = m->frame_len;
        const uint64_t vint = f * 400u / 449u + per_ms * 5u / 1000u;
        const uint64_t ev[] = { f * 100u / 449u, f * 200u / 449u,
            f * 300u / 449u, f * 400u / 449u, vint,
            f * 412u / 449u, f * 414u / 449u, f };
        const uint64_t pos = now % f;
        for (size_t i = 0; i < sizeof ev / sizeof ev[0]; i++)
            if (ev[i] > pos && ev[i] - pos < left)
                left = ev[i] - pos;
    }
    return left;
}

static void io_delay(machine_t *m, int write)
{
    /* Charge before the device answers. The AdLib detection depends on
     * real bus time passing while it polls the timer status. */
    /* DOSBox 0.74's IO_USEC_read_delay / write_delay: CPU_CycleMax / 1024
     * cycles per read, / 1365 per write (9000 cycles a millisecond: 8 and 6). */
    const uint64_t per_ms = m->ips / 1000u;
    const uint64_t delay = write ? per_ms / 1365u : per_ms / 1024u;
    /* DOSBox suppresses the bus delay when less than three delays remain
     * in the current CPU slice (IO_USEC_read/write_delay). */
    if (pc_slice_left(m) >= 3u * delay) m->cpu.icount += delay;
}

static uint8_t io_read8(machine_t *m, uint16_t port);

uint32_t pc_io_read(cpu_t *c, uint16_t port, int width)
{
    machine_t *m = machine_of(c);
    machine_inventory_port(port, 0);
    io_delay(m, 0);
    if (width == 2)
        return io_read8(m, port) | ((uint32_t)io_read8(m, (uint16_t)(port + 1)) << 8);
    return io_read8(m, port);
}

static uint8_t io_read8(machine_t *m, uint16_t port)
{
    switch (port) {
    case 0x20: return m->pic_read_isr ? m->pic_isr : m->pic_irr;
    case 0x21: return m->pic_imr;
    case 0x40: case 0x41: case 0x42: return pit_read(m, port - 0x40);
    case 0x43: return 0xFF;
    case 0x60:
        if (m->kbd_obf) { m->kbd_obf = 0; wake(m); }
        return m->port60;
    case 0x61: {
        /* Bit 4 is the refresh request, toggling every 15.085us; bit 5 is
         * counter 2's output. */
        uint64_t ns = muldiv(m->cpu.icount, 1000000000ull, m->ips);
        uint8_t v = (uint8_t)(m->port61 & 0x0F);
        if ((ns / 15085) & 1) v |= 0x10;
        if (pit_out(m, 2)) v |= 0x20;
        return v;
    }
    case 0x64: return (uint8_t)(0x1C | (m->kbd_obf ? 1 : 0));
    case 0x201: return joy_read(m);
    case 0x388: return opl_status(m);
    case 0x389: return 0xFF;
    case 0x330: {
        uint8_t value = 0xFE;
        if (m->mpu_used) {
            value = m->mpu_q[m->mpu_head];
            m->mpu_head = (m->mpu_head + 1u) % sizeof m->mpu_q;
            m->mpu_used--;
        }
        return value;
    }
    case 0x331: return (uint8_t)(0x3Fu | (m->mpu_used ? 0u : 0x80u));
    case 0x3C1: return m->attr[m->attr_idx & 0x1F];
    case 0x3C2: return 0x70;                 /* input status 0, as DOSBox reads it */
    case 0x3C4: return m->seq_idx;
    case 0x3C5: return m->seq[m->seq_idx & 7];
    case 0x3C6: return m->pel_mask;
    case 0x3C7: return m->dac_state;
    case 0x3C8: return m->dac_widx;
    case 0x3C9: {
        uint8_t v = m->dac[m->dac_ridx * 3 + m->dac_comp];
        if (++m->dac_comp == 3) { m->dac_comp = 0; m->dac_ridx++; }
        return v;
    }
    case 0x3CC: return m->misc_out;
    case 0x3CE: return m->gc_idx;
    case 0x3CF: return m->gc[m->gc_idx & 15];
    case 0x3D4: return m->crtc_idx;
    case 0x3D5: return m->crtc[m->crtc_idx & 31];
    case 0x3DA: return vga_status(m);
    default:    return 0xFF;
    }
}

static void io_write8(machine_t *m, uint16_t port, uint8_t v);

void pc_io_write(cpu_t *c, uint16_t port, uint32_t val, int width)
{
    machine_t *m = machine_of(c);
    machine_inventory_port(port, 1);
    io_delay(m, 1);
    machine_io_note(m, ((uint64_t)port << 24) | ((uint64_t)width << 16) | (val & 0xFFFFu), m->cpu.icount);
    io_write8(m, port, (uint8_t)val);
    if (width == 2) io_write8(m, (uint16_t)(port + 1), (uint8_t)(val >> 8));
}

static void io_write8(machine_t *m, uint16_t port, uint8_t v)
{
    switch (port) {
    case 0x20:
        if (v & 0x10) { m->pic_icw_step = (v & 1) ? 3 : 2; m->pic_imr = 0; break; } /* ICW1 */
        if ((v & 0x18) == 0x08) {            /* OCW3 */
            if (v & 2) m->pic_read_isr = v & 1;
            break;
        }
        if (v & 0x20) pic_eoi(m, (v & 0x40) != 0, v & 7);   /* OCW2: EOI */
        wake(m);
        break;
    case 0x21:
        if (m->pic_icw_step) { m->pic_icw_step--; if (m->pic_icw_step == 1 && 0) {} break; }
        m->pic_imr = v;
        wake(m);
        break;
    case 0x40: case 0x41: case 0x42: pit_write(m, port - 0x40, v); break;
    case 0x43: pit_control(m, v); break;
    case 0x61: {
        uint8_t old = m->port61;
        m->port61 = v;
        /* A rising gate on counter 2 restarts modes 1, 2, 3 and 5. */
        if (!(old & 1) && (v & 1)) m->pit[2].epoch_clk = pc_pit_clock(m);
        if ((old ^ v) & 3) { m->speaker_changes++; if (m->hooks.speaker) m->hooks.speaker(m->hooks.user, m->cpu.icount); }
        break;
    }
    case 0x201: joy_trigger(m); break;
    case 0x330:
        m->midi_bytes++;
        if (m->hooks.midi_byte) m->hooks.midi_byte(m->hooks.user, m->cpu.icount, v);
        break;
    case 0x331:
        if (m->mpu_uart && v != 0xFFu) break;
        if (v == 0xFFu) { m->mpu_uart = 0; m->mpu_used = 0; m->mpu_head = 0; }
        else if (v == 0x3Fu) m->mpu_uart = 1;
        mpu_queue(m, 0xFEu);
        break;
    case 0x388: m->opl_index = v; break;
    case 0x389:
        m->opl_writes++;
        if (m->opl_sched_i < m->opl_sched_n && opl_channel0(m->opl_index)) { m->opl_sched_dropped++; break; }
        opl_write_reg(m, m->opl_index, v);
        break;
    case 0x3C0:
        if (!m->attr_flip) m->attr_idx = v;
        else m->attr[m->attr_idx & 0x1F] = v;
        m->attr_flip ^= 1;
        break;
    case 0x3C2: m->misc_out = v; break;
    case 0x3C4: m->seq_idx = v; break;
    case 0x3C5: m->seq[m->seq_idx & 7] = v; break;
    case 0x3C6:
        /* VGA_DAC_UpdateColor rebuilds the render palette on a mask write,
         * including any RGB components not yet completed by a blue write. */
        if (m->pel_mask != v) {
            m->pel_mask = v;
            for (unsigned i = 0; i < 256; i++)
                memcpy(m->dac_display + i * 3, m->dac + (i & v) * 3, 3);
        }
        break;
    case 0x3C7:
        m->dac_ridx = v; m->dac_widx = (uint8_t)(v + 1);
        m->dac_comp = 0; m->dac_state = 0x03;
        break;                              /* read mode reads 3 */
    case 0x3C8: m->dac_widx = v; m->dac_comp = 0; m->dac_state = 0x00; break;
    case 0x3C9:
        m->dac[m->dac_widx * 3 + m->dac_comp] = (uint8_t)(v & 0x3F);
        if (++m->dac_comp == 3) {
            /* DOSBox's VGA_DAC_SendColor runs after the complete triplet;
             * port readback can already see the earlier red/green writes. */
            unsigned index = m->dac_widx;
            memcpy(m->dac_display + index * 3,
                   m->dac + (index & m->pel_mask) * 3, 3);
            if ((index & m->pel_mask) == index)
                for (unsigned i = index + 1; i < 256; i++)
                    if ((i & m->pel_mask) == index)
                        memcpy(m->dac_display + i * 3, m->dac + index * 3, 3);
            m->dac_comp = 0; m->dac_widx++;
        }
        break;
    case 0x3CE: m->gc_idx = v; break;
    case 0x3CF: m->gc[m->gc_idx & 15] = v; break;
    case 0x3D4: m->crtc_idx = v; break;
    case 0x3D5: m->crtc[m->crtc_idx & 31] = v; break;
    default: break;
    }
}

/* ===================================================================== */
/* Keyboard                                                              */
/* ===================================================================== */

static void key_apply(machine_t *m, uint8_t b)
{
    if (m->kbd_qn >= (int)sizeof m->kbd_q) return;
    m->kbd_q[(m->kbd_qh + m->kbd_qn) % (int)sizeof m->kbd_q] = b;
    m->kbd_qn++;
    wake(m);
}

/* A byte goes to port 60 when the last one has been read, no sooner than a
 * millisecond after it: the controller's own pace. */
static void kbd_poll(machine_t *m)
{
    if (!m->kbd_qn || m->kbd_obf || m->cpu.icount < m->kbd_next) return;
    m->port60 = m->kbd_q[m->kbd_qh];
    m->kbd_qh = (m->kbd_qh + 1) % (int)sizeof m->kbd_q;
    m->kbd_qn--;
    m->kbd_obf = 1;
    m->kbd_next = m->cpu.icount + m->ips / 1000;
    pic_raise(m, 1);
    if (m->log && getenv("F117R_TRACE_KEYS"))
        dos_log(m, "[key] byte %02X to port 60 @%llu (%s)\n", m->port60,
                (unsigned long long)m->cpu.icount, dos_current_program(m));
}

/* ===================================================================== */
/* Mouse                                                                 */
/* ===================================================================== */

/* DOSBox reports X with its low bit masked in modes 0Dh and 13h. */
int mouse_gran_x(const machine_t *m, int x)
{
    return (m->video_mode == 0x0D || m->video_mode == 0x13) ? (x & ~1) : x;
}

static void mouse_apply(machine_t *m, int x, int y, int buttons, int dx, int dy)
{
    /* Driver coordinates in mode 13h are 0..639 across. */
    int vx = x * 2, vy = y;
    if (vx < m->mouse_xmin) vx = m->mouse_xmin;
    if (vx > m->mouse_xmax) vx = m->mouse_xmax;
    if (vy < m->mouse_ymin) vy = m->mouse_ymin;
    if (vy > m->mouse_ymax) vy = m->mouse_ymax;
    m->mouse_x = vx;
    m->mouse_y = vy;
    m->mouse_mickey_x += dx * 2;
    m->mouse_mickey_y += dy * 2;
    for (int b = 0; b < 2; b++) {
        int was = (m->mouse_buttons >> b) & 1, now = (buttons >> b) & 1;
        if (!was && now) { m->mouse_press[b]++; m->mouse_press_x[b] = mouse_gran_x(m, vx); m->mouse_press_y[b] = vy; }
        if (was && !now) { m->mouse_release[b]++; m->mouse_rel_x[b] = mouse_gran_x(m, vx); m->mouse_rel_y[b] = vy; }
    }
    m->mouse_buttons = buttons;
    mouse_draw_cursor(m);
}

/* ===================================================================== */
/* Events                                                                */
/* ===================================================================== */

void pc_reset(machine_t *m)
{
    m->pic_irr = m->pic_isr = 0;
    m->pic_imr = 0xF8;                       /* timer, keyboard, cascade: as GOG's DOSBox leaves it */
    memset(m->pit, 0, sizeof m->pit);
    for (int i = 0; i < 3; i++) { m->pit[i].mode = 3; m->pit[i].access = 3; }
    m->pit[1].reload = 18;                   /* DRAM refresh */
    m->pit[0].epoch_clk = 0;
    pit0_schedule(m);
    m->port61 = 0;
    m->frame_len = muldiv(m->ips, 1000ull, 70086ull);
    m->vsync_next = m->frame_len * 412ull / 449ull;
    m->pel_mask = 0xFF;
    vga_set_mode(m, 3, 1);
    m->mouse_xmin = 0; m->mouse_xmax = 639; m->mouse_ymin = 0; m->mouse_ymax = 199;
    m->mouse_hidden = 1;
    for (int i = 0; i < 4; i++) m->joy_axis[i] = 0x100;
}

/* ---- input -------------------------------------------------------------- */

void machine_input_at(machine_t *m, const machine_input *in)
{
    const int cap = (int)(sizeof m->in_q / sizeof m->in_q[0]);
    if (m->in_qn >= cap) return;
    /* Keep the queue in time order; equal times keep their arrival order. */
    int pos = m->in_qn;
    while (pos > 0 && m->in_q[(m->in_qh + pos - 1) % cap].at > in->at) {
        m->in_q[(m->in_qh + pos) % cap] = m->in_q[(m->in_qh + pos - 1) % cap];
        pos--;
    }
    m->in_q[(m->in_qh + pos) % cap] = *in;
    m->in_qn++;
    m->cpu.stop_at = 0;
}

void machine_key_byte(machine_t *m, uint8_t b)
{
    machine_input in = { 0 };
    in.at = m->cpu.icount;
    in.type = INPUT_KEY;
    in.byte = b;
    machine_input_at(m, &in);
}

void machine_mouse(machine_t *m, int x, int y, int buttons, int dx, int dy)
{
    machine_input in = { 0 };
    in.at = m->cpu.icount;
    in.type = INPUT_MOUSE;
    in.x = (int16_t)x; in.y = (int16_t)y; in.dx = (int16_t)dx; in.dy = (int16_t)dy;
    in.buttons = (uint16_t)buttons;
    machine_input_at(m, &in);
}

void machine_joystick(machine_t *m, int present, const unsigned axis[4], unsigned buttons)
{
    machine_input in = { 0 };
    in.at = m->cpu.icount;
    in.type = INPUT_JOY;
    in.present = (uint8_t)present;
    for (int i = 0; i < 4; i++) in.axis[i] = (uint16_t)(axis ? axis[i] : 0x100);
    in.buttons = (uint16_t)buttons;
    machine_input_at(m, &in);
}

static void input_poll(machine_t *m)
{
    const int cap = (int)(sizeof m->in_q / sizeof m->in_q[0]);
    while (m->in_qn && m->in_q[m->in_qh].at <= m->cpu.icount) {
        const machine_input in = m->in_q[m->in_qh];
        m->in_qh = (m->in_qh + 1) % cap;
        m->in_qn--;
        if (m->on_input) m->on_input(m->on_input_user, &in);
        switch (in.type) {
        case INPUT_KEY: key_apply(m, in.byte); break;
        case INPUT_MOUSE: mouse_apply(m, in.x, in.y, in.buttons, in.dx, in.dy); break;
        case INPUT_JOY: {
            unsigned ax[4] = { in.axis[0], in.axis[1], in.axis[2], in.axis[3] };
            joy_apply(m, in.present, ax, in.buttons);
            break;
        }
        default: break;
        }
    }
}

uint64_t pc_next_event(machine_t *m)
{
    uint64_t t = m->irq0_next;
    if (m->vsync_next < t) t = m->vsync_next;
    if (m->opl_sched_i < m->opl_sched_n && m->opl_sched[m->opl_sched_i].at < t) t = m->opl_sched[m->opl_sched_i].at;
    if (m->video_mode == 0x13 && m->frame_len && m->scan_next < t) t = m->scan_next;
    if (m->kbd_qn && !m->kbd_obf) {
        uint64_t k = m->kbd_next > m->cpu.icount ? m->kbd_next : m->cpu.icount;
        if (k < t) t = k;
    }
    if (m->in_qn && m->in_q[m->in_qh].at < t) t = m->in_q[m->in_qh].at;
    return t;
}

static void vga_scanout(machine_t *m)
{
    if (m->video_mode == 0x13 && m->frame_len) {
        if (m->scan_part == 0) {
            /* VGA_VerticalTimer uses the address latched at retrace. */
            m->scan_start = m->scan_latch;
        } else {
            unsigned first = (m->scan_part - 1u) * 16000u;
            for (unsigned i = first; i < first + 16000u; i++)
                m->scan_work[i] = m->mem[0xA0000u + (uint16_t)(m->scan_start + i)];
            if (m->scan_part == 4) {
                memcpy(m->scan_pixels, m->scan_work, sizeof m->scan_pixels);
                memcpy(m->scan_dac, m->dac_display, sizeof m->scan_dac);
                m->scan_mask = m->pel_mask;
                m->scan_blank = (m->seq[1] & 0x20) != 0;
                m->scan_time = m->scan_next;
                m->scan_valid = 1;
                m->scan_part = 0;
                m->scan_frame += m->frame_len;
                m->scan_next = m->scan_frame;
                return;
            }
        }
        m->scan_part++;
        m->scan_next = m->scan_frame + m->frame_len * (100u * m->scan_part) / 449u;
    }
}

void pc_release_irq0(machine_t *m)
{
    if (!m->irq0_held) return;
    m->irq0_held = 0;
    pic_raise(m, 0);
    wake(m);
}

void pc_events(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint64_t now = c->icount;
    input_poll(m);
    opl_sched_issue(m, now);
    if (m->irq0_held && now >= m->irq0_hold_until) pc_release_irq0(m);
    while (now >= m->irq0_next) {
        pic_raise(m, 0);
        pit0_schedule(m);
        if (m->irq0_next <= now) break;
    }
    /* Preserve display-event order even after HLT or a long BIOS callback. */
    for (;;) {
        if (m->video_mode == 0x13 && m->frame_len &&
            m->scan_next <= now && m->scan_next <= m->vsync_next) {
            vga_scanout(m);
        } else if (m->vsync_next <= now) {
            if (m->video_mode == 0x13)
                m->scan_latch = (uint16_t)(vga_start_address(m) * 4u);
            if (m->hooks.vsync) m->hooks.vsync(m->hooks.user, m->vsync_next);
            m->vsync_next += m->frame_len;
        } else break;
    }
    kbd_poll(m);

    /* Deliver: IF set (or the CPU waiting inside a BIOS call that would
     * have enabled interrupts), not at the shadow of STI/MOV SS. */
    /* (DOSBox's STI has no shadow: its service stubs take the interrupt
     * before the callback.) */
    if (now == c->inhibit_at && c->seg[S_CS] != m->iret_seg) return;
    if (!(c->flags & F_IF) && c->halted != 2) return;
    int n = pic_pending(m);
    if (n < 0) return;
    m->pic_irr &= (uint8_t)~(1u << n);
    m->pic_isr |= (uint8_t)(1u << n);
    c->halted = 0;
    cpu_hw_interrupt(c, (uint8_t)(8 + n));
}

/* Whether an interrupt could be taken at the next boundary - so the run
 * loop knows whether to look again immediately. */
static int deliverable(machine_t *m)
{
    cpu_t *c = &m->cpu;
    if (!(c->flags & F_IF) && c->halted != 2) return 0;
    return pic_pending(m) >= 0;
}

void cpu_irq_state_changed(cpu_t *c)
{
    c->stop_at = 0;
}

/* ===================================================================== */
/* Run loop                                                              */
/* ===================================================================== */

/* Supplied by the recomp runtime (recomp.c). Runs translated code from the
 * current CS:IP until cpu.stop_at; returns 0 if there is no valid
 * translation here, in which case the interpreter takes one step. */
int recomp_run(machine_t *m);
void recomp_note_interp(machine_t *m);
/* Code overrides (recomp_rt.h): how many enabled ones are placed, and the
 * step at CS:IP - 1 one ran, -1 it asked for the original, 0 none here. */
extern int recomp_overrides_live;
int recomp_override_step(machine_t *m);

static int interp_step(machine_t *m)
{
    cpu_t *c = &m->cpu;
    int rc = cpu_step(c);
    m->interp_steps++;
    if (rc == STOP_FAULT) {
        snprintf(m->fault, sizeof m->fault,
                 "invalid instruction at %04X:%04X (%02X %02X %02X %02X) in %s",
                 c->op_cs, c->op_ip,
                 c->mem[phys(c->op_cs, c->op_ip)], c->mem[phys(c->op_cs, (uint16_t)(c->op_ip + 1))],
                 c->mem[phys(c->op_cs, (uint16_t)(c->op_ip + 2))], c->mem[phys(c->op_cs, (uint16_t)(c->op_ip + 3))],
                 dos_current_program(m));
        return RUN_FAULT;
    }
    /* The single-step trap fires after the instruction completes. */
    if (c->flags & F_TF) cpu_interrupt(c, 1);
    return RUN_SLICE;
}

/* An enabled code override at CS:IP: run it, or the original instruction
 * when it declines, and return as a step does; -1 when none is placed here. */
static int override_at(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const int o = recomp_override_step(m);
    if (!o) return -1;
    if (o < 0) {
        recomp_note_interp(m);
        return interp_step(m);
    }
    if (c->flags & F_TF) cpu_interrupt(c, 1);
    return RUN_SLICE;
}

int machine_run(machine_t *m, uint64_t until)
{
    cpu_t *c = &m->cpu;
    for (;;) {
        if (m->exited) return RUN_EXITED;
        if (c->icount >= until) return RUN_SLICE;

        pc_events(m);
        if (m->exited) return RUN_EXITED;

        uint64_t stop = pc_next_event(m);
        if (stop > until) stop = until;
        if (deliverable(m)) {
            /* Pending but held at this boundary (the STI shadow): look
             * again after one instruction. */
            stop = c->icount + 1;
        }
        if (stop <= c->icount) stop = c->icount + 1;

        if (c->halted) {
            /* HLT, or a BIOS call waiting for a key: time passes. */
            c->icount = stop;
            continue;
        }
        c->stop_at = stop;

        if (m->engine == ENGINE_RECOMP && !(c->flags & F_TF)) {
            while (c->icount < c->stop_at && !c->halted) {
                if (recomp_overrides_live) {
                    const int rc = override_at(m);
                    if (rc > RUN_SLICE) return rc;
                    if (rc == RUN_SLICE) {
                        if (m->exited) return RUN_EXITED;
                        continue;
                    }
                }
                if (recomp_run(m)) continue;
                recomp_note_interp(m);
                int rc = interp_step(m);
                if (rc != RUN_SLICE) return rc;
                if (m->exited) return RUN_EXITED;
            }
        } else {
            while (c->icount < c->stop_at && !c->halted) {
                if (recomp_overrides_live) {
                    const int rc = override_at(m);
                    if (rc > RUN_SLICE) return rc;
                    if (rc == RUN_SLICE) {
                        if (m->exited) return RUN_EXITED;
                        continue;
                    }
                }
                recomp_note_interp(m);
                int rc = interp_step(m);
                if (rc != RUN_SLICE) return rc;
                if (m->exited) return RUN_EXITED;
            }
        }
    }
}
