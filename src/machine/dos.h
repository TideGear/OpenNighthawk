/* dos.h - enough of DOS and the PC BIOS to run the original game binaries
 * inside the oracle.
 *
 * The scope is set by measurement rather than by guesswork. Recursive descent
 * over the unpacked executables says the phase programs use seventeen INT 21h
 * functions plus INT 10h/16h/1Ah/33h, and disassembly of the F117.COM shell
 * adds the process-control set: EXEC (4B00), overlay load (4B03), and
 * get-exit-code (4D). The shell stays resident, loads MISC.EXE and the
 * graphics driver as overlays, plants a control-block segment in the BIOS
 * inter-application area at 0000:04F0, and EXECs each phase program in
 * turn, choosing the next phase from the exit code of the last. That is the
 * whole surface, and it is small enough to implement exactly.
 *
 * The point is not to be a DOS emulator. It is to let the original code run
 * far enough to be *observed*: to answer questions the static image cannot,
 * and later to serve as the reference the reimplementation is tested
 * against.
 */
#ifndef F117_DOS_H
#define F117_DOS_H

#include "cpu.h"

#include <stdio.h>

#define DOS_MAX_FILES  40
#define DOS_MAX_BLOCKS 64
#define DOS_MAX_PROCS  8

#define DOS_TRACE_ALL   1
#define DOS_TRACE_FILES 2
#define DOS_TRACE_TEXT  4

typedef struct {
    FILE    *fp;
    char     path[520];
    int      in_use;
    int      is_device;      /* handles 0-4 are the standard devices */
    uint16_t owner;          /* PSP of the process that opened it */
} dos_file;

typedef struct {
    uint16_t seg;            /* first paragraph of the block (the PSP, for a program) */
    uint16_t paras;
    uint16_t owner;          /* PSP that owns it; freed when that process exits */
    int      in_use;
} dos_block;

/* One running program. EXEC pushes one of these; terminate pops it and
 * resumes the parent exactly where its INT 21h left off. */
typedef struct {
    uint16_t psp_seg;
    char     name[64];
    /* Parent's CPU state at the moment of the EXEC call. */
    uint16_t r[8];
    uint16_t seg[4];
    uint16_t ret_cs, ret_ip;  /* return address of the parent's INT 21h */
    uint16_t flags;
    uint64_t start_icount;    /* cpu icount when this program began */
} dos_proc;

typedef struct {
    cpu_t   *cpu;

    /* Where DOS paths are resolved. The game asks for names like KU.WLD;
     * this is the directory they are found in. */
    char     data_dir[512];

    dos_file  files[DOS_MAX_FILES];
    dos_block blocks[DOS_MAX_BLOCKS];

    dos_proc  procs[DOS_MAX_PROCS];
    int       nproc;             /* depth of the process stack; root is 1 */

    uint16_t env_seg;            /* master environment, inherited by children */
    uint16_t arena_base_seg;     /* first paragraph available to programs */
    uint16_t arena_end_seg;      /* one past the last */

    uint32_t dta;                /* disk transfer area, linear */

    int      exited;             /* the ROOT process terminated */
    int      exit_code;
    uint8_t  last_child_exit;    /* what INT 21h/4D reports */

    /* Instrumentation */
    int      trace_dos;          /* DOS_TRACE_* bitmask; keep struct snapshot ABI */
    int      trace_ports;        /* log port I/O */
    int      trace_opl;          /* log only OPL2 index/data writes */
    FILE    *log;

    /* Observed hardware state, so the caller can ask what the game did. */
    uint16_t pit_divisor[3];     /* last reload value written per counter */
    int      pit_writes;
    uint8_t  pit_latch_state[3];
    uint16_t pit_latched[3];     /* the value sampled at the latch */
    /* Instructions per IRQ0, from --irq-every, so a counter READ can be
     * derived from where the CPU is inside the current tick. A driver
     * that calibrates machine speed by latching counter 0 across a delay
     * loop needs the counter to MOVE; see dos_io_read's 0x40 case. */
    uint64_t irq_every;
    /* Instructions per second, for the microsecond clock the OPL timers
     * and the game port read. Zero keeps the historical derivation, in
     * which one --irq-every is one PIT period - self-consistent, but it
     * makes the modelled CPU speed depend on counter 0's divisor, so a
     * guest that reprograms the timer appears to change how fast its own
     * instructions run. START's paddle counts moved by a factor of three
     * across one boot for exactly that reason. Setting this fixes the
     * clock to one rate for the whole run. */
    uint64_t ins_per_sec;

    /* The OPL2 as the DRIVER can observe it: the latched index and the
     * two timers behind the status register. Card detection is "start
     * timer 1, wait, expect 0xC0", and a status that always reads 0
     * fails it - which is why ASOUND.LOG's logo driver span its
     * detection forever under this emulator. sndrun.c has modelled this
     * correctly all along; this is the same model. */
    uint8_t  opl_index;
    uint8_t  opl_t1_preset, opl_t2_preset;
    int      opl_t1_run, opl_t2_run;
    int      opl_t1_mask, opl_t2_mask;
    int      opl_t1_flag, opl_t2_flag;
    uint64_t opl_t1_due, opl_t2_due;      /* microseconds */
    /* The analog game port at 201h, and why a constant was not enough.
     *
     * The shared paddle routine - VGAME 0x1124E, START 0x08476, and a copy
     * in every other binary - writes 201h to fire the one-shots and then
     * COUNTS LOOP ITERATIONS while each axis bit stays high; the count is
     * the axis position. Returning a constant 0xF0 made bits 0-1 read zero
     * at the first `in`, so the loop fell straight out and both axes read
     * 0. Every capture this project has ever taken therefore shows the
     * stick jammed into a corner, and no centred or part-deflected path has
     * ever executed under the oracle.
     *
     * Modelled as the hardware behaves: an `out` starts a one-shot per
     * axis whose length is 24.2us + 0.011us per ohm across a 0..100k pot,
     * so 24.2us at joy_axis 0 and 1124.2us at 255. Reads report each axis
     * bit set until its one-shot expires, and buttons in bits 4-7 ACTIVE
     * LOW. joy_present 0 keeps the old constant, which is the right answer
     * for the N (no joystick) SETUP answer every existing recipe uses. */
    int      joy_present;
    unsigned joy_axis[4];            /* 0..255 per axis, 4 = two sticks */
    unsigned joy_buttons;            /* bit 0..3, 1 = pressed */
    uint64_t joy_due[4];             /* microseconds; 0 = not running */

    uint16_t vga_mode;
    /* Port 3DA's read-driven retrace phase is guest-visible machine state
     * and must survive snapshots just like the timer and keyboard queue. */
    unsigned vga_status_reads;
    uint64_t tick_irq_count;
    unsigned key_starved;        /* blocking key reads with nothing queued */

    /* 8259 in-service latch for IRQ0. Once a timer interrupt is delivered no
     * further one can be until the handler writes EOI to port 20h. Without
     * this a handler that runs longer than one tick and enables interrupts
     * early (the game's do) is re-entered without limit. */
    int      irq0_in_service;
    int      irq0_pending;       /* a tick arrived while in service */
    uint64_t irq0_deferred;      /* how many ticks were held back */

    /* VGA DAC as programmed through ports 3C8h/3C9h: 256 x RGB, 6-bit. With
     * the mode-13h framebuffer at A000:0000 this is enough to reconstruct
     * what the guest has on screen. */
    uint8_t  dac[256 * 3];
    int      dac_index;
    int      dac_component;

    /* Emulated mouse, so the point-and-click front end can be driven by
     * exact clicks rather than guessed arrow-key steps. The driver convention
     * for mode 13h is followed on the guest side: x reported doubled
     * (0..639), y 0..199. mouse_x/y here are screen pixels. */
    int      mouse_present;
    int      mouse_x, mouse_y;
    int      mouse_buttons;          /* bit 0 left, bit 1 right */
    int      mouse_shown;
    uint16_t mouse_xmin, mouse_xmax, mouse_ymin, mouse_ymax;
    uint16_t mouse_hnd_seg, mouse_hnd_off, mouse_hnd_mask;   /* INT 33h/0C */
    unsigned mouse_calls;

    /* Emulated keyboard controller. Scancodes queue up with a due tick and
     * are delivered one at a time through IRQ1 to whatever INT 9 handler the
     * guest installed (the flight engine installs its own and never reads
     * the BIOS buffer). While the BIOS vector is still in place the make
     * codes are translated into the ring buffer instead, which is what the
     * BIOS handler would do. */
    /* `shift_set`/`shift_clear` are OR'd into and masked out of the BIOS
     * shift byte at 0040:0017 as the entry is delivered. Alt needs this:
     * a guest that installs its own INT 9 - the flight engine does - reads
     * port 60h and that byte, and never sees an injected 0x38 as a
     * modifier. Worse, 0x38 is inside the range its keypad table
     * translates, so injecting it deflects the virtual joystick. */
    struct {
        uint8_t code; uint8_t ascii; uint64_t due_tick;
        uint8_t shift_set, shift_clear;
    } kbd_q[2048];
    int      kbd_qh, kbd_qn;
    uint64_t kbd_cursor;             /* tick at which the next typed key starts */
    uint8_t  port60, port61;
    uint8_t  port60_ascii;           /* what the BIOS would translate port60 to */
    int      irq1_in_service;
    unsigned kbd_delivered;

    /* The command tail handed to the root program, as a plain string. DOS
     * puts it at PSP:0080 as a length byte, the text, and a CR; a program
     * that takes a filename argument cannot be driven without it. */
    char     args[128];
    /* Accurate BIOS clock mode: BDA time advances only when the guest's
     * INT 8 chain reaches our BIOS stub, not on every physical PIT edge. */
    int      bios_clock;
} dos_t;

/* Type `keys` on the emulated keyboard: make code, hold for `hold_ticks`
 * timer ticks, break code; shifted characters get the shift key around
 * them. Same escapes as dos_push_keys. Keys are typed one after another. */
void dos_kbd_type(dos_t *d, const char *keys, int hold_ticks);

/* Call once per slice: delivers the next due scancode if IRQ1 is free. */
void dos_kbd_poll(dos_t *d);

/* Move the emulated mouse and set its button state. */
void dos_mouse_set(dos_t *d, int x, int y, int buttons);

/* Set up a DOS environment around `cpu` and load `exe_path` as the root
 * program. `data_dir` is where the guest's file opens are resolved. Handles
 * both MZ executables and COM files. */
int dos_boot(dos_t *d, cpu_t *cpu, const char *exe_path, const char *data_dir);
/* Where dos_boot's own log lines go before the caller sets d->log: stdout
 * for dosrun, whose output the tools parse; an embedding host (the shadow
 * oracle) whose stdout is data sets it to stderr first. */
extern FILE *dos_boot_log;

/* Set the root program's command tail. Call **after** dos_boot - it writes
 * straight into the PSP, and dos_boot clears the dos_t. The leading space
 * DOS always puts there is added here, so pass the arguments alone. */
void dos_set_args(dos_t *d, const char *args);

/* Queue a keystroke as the BIOS would: it lands in the BIOS keyboard ring
 * buffer at 0040:001E, where both INT 16h and code that polls the buffer
 * directly will find it. `ascii` may be 0 for an extended key. */
int dos_push_key(dos_t *d, uint8_t ascii, uint8_t scancode);

/* Queue a whole string of keystrokes, with \r, \e and \t understood. */
void dos_push_keys(dos_t *d, const char *s);

/* Name of the program currently running (innermost process). */
const char *dos_current_program(const dos_t *d);

/* Interrupt and I/O hooks; dos_boot installs these on the cpu. */
int      dos_int_hook(cpu_t *c, uint8_t vec);
uint32_t dos_io_read(cpu_t *c, uint16_t port, int width);
void     dos_io_write(cpu_t *c, uint16_t port, uint32_t val, int width);

/* Raise IRQ0, retaining a masked/in-service request. In the default
 * synthetic-clock mode this also advances the BDA tick count; BIOS-clock
 * mode advances it only through the chained handler. Returns non-zero
 * when the interrupt is delivered. */
int dos_raise_timer_irq(dos_t *d);

/* Select BIOS-chained time before running the guest. The default remains
 * the synthetic edge clock used by bounded step/capture fixtures. */
void dos_set_bios_clock(dos_t *d);

/* Call once per slice: deliver a pending IRQ0 after EOI and with IF set. */
void dos_timer_poll(dos_t *d);

void dos_shutdown(dos_t *d);

/* After a snapshot restore the dos_t carries each open file's host path and
 * in-use flag, but its FILE pointer belonged to another process. Reopen
 * handle `h` the way the original open did and seek to `pos` (-1 to leave
 * the position alone). Returns 0 if the file cannot be opened. */
int dos_reopen_file(dos_t *d, int h, long pos);
/* After a snapshot restore: resolve files in data_dir, not the saver's. */
void dos_rebase_data_dir(dos_t *d, const char *data_dir);

/* --vga-time: the timer and the VGA status register run on ins_per_sec,
 * as the hardware does - IRQ0 once per counter-0 period, the counter in
 * mode 3 stepping by two, vertical retrace at 70.086 Hz - so a guest
 * that calibrates its timer against retrace accepts the measurement.
 * Globals, not dos_t fields: dos_t is written whole into snapshots, and
 * a new field would invalidate every one of them. */
extern int     dos_vga_time;
extern uint8_t dos_pit_mode[3];   /* 0x80 | mode once a mode word arrives */
/* The instruction count at counter 0's last reload: a real 8253 starts
 * counting the new value there, which is how the games phase-lock their
 * tick to retrace (they reload at retrace start every twentieth tick). */
extern uint64_t dos_pit_epoch;
/* Whether dos_timer_poll or dos_kbd_poll would deliver an interrupt now:
 * the moment a caller running instructions in bulk must hand back. */
int dos_poll_pending(const dos_t *d);

#endif /* F117_DOS_H */
