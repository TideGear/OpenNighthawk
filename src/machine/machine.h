/* machine.h - the emulated PC the original program runs on.
 *
 * One machine is a CPU, 1 MiB of memory, DOS and the BIOS (dos.c) and the
 * devices the game touches (pc.c): the 8259 interrupt controller, the 8253
 * timer, the keyboard controller, the VGA, the AdLib, the MPU-401, the game
 * port and the speaker.
 *
 * TIME. The machine's only clock is the CPU's retired-instruction count.
 * `ips` instructions are one second, and every device derives its timing
 * from icount: the timer's interrupts, the retrace bit, the OPL timers, the
 * joystick one-shots, the audio the host renders. The run is therefore a
 * pure function of the program, the files and the inputs with the icount at
 * which each arrived - which is what makes the recompiled engine checkable
 * against the interpreter instruction for instruction, and what makes a
 * recorded session replay exactly.
 *
 * The default speed is 9,000,000 instructions a second: GOG's own DOSBox
 * configuration for this game runs it at cycles=9000.
 *
 * EVENTS. Before every instruction, both engines compare icount with
 * cpu.stop_at; at or past it they hand back to machine_run, which raises
 * due interrupts, delivers the highest-priority deliverable one and
 * computes the next stop. Anything that may make an interrupt deliverable
 * sooner (STI, POPF, IRET, an EOI, a mask change, a timer reload, a port 60
 * read) sets stop_at to 0, so the very next boundary looks. Both engines
 * therefore take every interrupt at the same instruction boundary.
 */
#ifndef F117R_MACHINE_H
#define F117R_MACHINE_H

#include "cpu.h"

#include <stdint.h>
#include <stdio.h>

#define DOS_MAX_FILES  40
#define DOS_MAX_PROCS  8

#define MACHINE_DEFAULT_IPS 9000000ull

typedef struct machine machine_t;

typedef struct {
    FILE    *fp;
    char     path[520];
    int      in_use;
    int      is_device;      /* handles 0-4 are the standard devices */
    uint16_t owner;          /* PSP of the process that opened it */
    uint8_t  sft;            /* its system file table entry: what the PSP's handle table holds */
    char     temp[540];      /* fix D11: written here, renamed over path on close ("" = direct) */
} dos_file;

/* Fix D11: a file the program creates in the save directory is written to
 * a temporary name and renamed over the real one when closed, so an
 * interrupted save leaves the previous file intact. Invisible to the
 * program. Set by fixes_enable. */
extern int dos_atomic_saves;

/* One running program. EXEC pushes one of these; terminate pops it and
 * resumes the parent where its INT 21h left off. */
typedef struct {
    uint16_t psp_seg;
    char     name[64];
    uint16_t r[8];
    uint16_t seg[4];
    uint16_t ret_cs, ret_ip;
    uint16_t flags;
    uint64_t start_icount;
} dos_proc;

/* What kind of image a module-load notification describes. */
enum {
    MODLOAD_EXEC = 1,        /* EXEC 4B00: an MZ program at load_seg = psp+0x10 */
    MODLOAD_COM,             /* EXEC of a .COM: image at psp:0100 */
    MODLOAD_OVERLAY          /* EXEC 4B03: an MZ image at load_seg, relocated by reloc */
};

/* Host hooks. All optional. Every one carries the icount it happened at;
 * none may change guest state. */
typedef struct {
    void *user;
    /* OPL2 register write (index already applied). */
    void (*opl_write)(void *user, uint64_t icount, uint8_t reg, uint8_t val);
    /* One byte written to the MPU-401 data port in UART mode. */
    void (*midi_byte)(void *user, uint64_t icount, uint8_t b);
    /* The speaker's input changed: port 61 bits 0-1 or PIT counter 2. */
    void (*speaker)(void *user, uint64_t icount);
    /* The start of vertical retrace: the moment a VGA scans out a frame. */
    void (*vsync)(void *user, uint64_t icount);
    /* A program or overlay was placed in memory. `file` is the whole file
     * as read from disk. */
    void (*module_load)(void *user, machine_t *m, const char *name,
                        const uint8_t *file, size_t len, int kind,
                        uint16_t load_seg, uint16_t reloc);
    /* Text written to the console by DOS (for logs). */
    void (*console)(void *user, const char *text);
    /* The one hook that may change what the guest sees: the bytes a DOS
     * read of `name` (basename) delivers, `n` of them from file offset
     * `pos` of a file `size` bytes long, before they reach memory - how
     * switchable data fixes correct a file as it is read (docs/bugs.md). */
    void (*file_data)(void *user, machine_t *m, const char *name, long size, long pos,
                      uint8_t *buf, size_t n);
} machine_hooks;

/* One input reaching the machine at a clock count (see machine_input_at). */
enum { INPUT_KEY = 1, INPUT_MOUSE, INPUT_JOY };
typedef struct {
    uint64_t at;
    uint8_t  type;
    uint8_t  byte;                 /* INPUT_KEY: set-1 byte (E0/E1 prefixes as bytes) */
    int16_t  x, y, dx, dy;         /* INPUT_MOUSE: mode-13h pixels, motion */
    uint16_t buttons;              /* mouse or stick buttons */
    uint16_t axis[4];              /* INPUT_JOY: 0..255, 0x100 = no pot */
    uint8_t  present;
} machine_input;

/* Called for every input as it is applied: what a recorder writes down. */
typedef void (*machine_input_fn)(void *user, const machine_input *in);

typedef struct {
    uint16_t reload;         /* 0 means 65536 */
    uint8_t  mode;           /* 0..5 */
    uint8_t  access;         /* 1 lo, 2 hi, 3 lo then hi */
    uint8_t  write_hi_next;  /* access 3: the next write is the high byte */
    uint8_t  read_hi_next;
    uint8_t  latched;        /* a latch command is holding `latch` */
    uint16_t latch;
    uint8_t  null_count;     /* programmed, waiting for the first count */
    uint64_t epoch_clk;      /* PIT clock at which the current count began */
} pit_counter;

struct machine {
    cpu_t    cpu;
    uint8_t *mem;
    uint64_t ips;            /* instructions per emulated second */

    machine_hooks hooks;
    FILE    *log;            /* diagnostics; NULL for none */

    /* ---- DOS ------------------------------------------------------ */
    char     data_dir[512];  /* the user's install: read here */
    char     save_dir[512];  /* written here; read here first. Empty: data_dir */
    dos_file  files[DOS_MAX_FILES];
    dos_proc  procs[DOS_MAX_PROCS];
    int       nproc;
    uint16_t  env_seg;
    uint16_t  first_mcb;     /* the memory control block chain, in guest memory */
    uint8_t   sft_ref[64];   /* references to each system file table entry */
    uint16_t  alloc_strategy;
    uint8_t   return_mode;   /* INT 21h/4Dh AH: 0 normal, 3 resident */
    uint8_t   break_check;   /* INT 21h/33h */
    uint32_t  dta;
    int       exited;        /* the root process terminated */
    int       exit_code;
    uint8_t   last_child_exit;
    uint64_t  boot_time_us;  /* local wall-clock time at boot, counted as if UTC (an RTC) */
    uint16_t  iret_seg;      /* where the BIOS stubs live */

    /* ---- 8259 ----------------------------------------------------- */
    uint8_t  pic_irr, pic_isr, pic_imr;
    uint8_t  pic_icw_step;   /* initialisation sequence in progress */
    uint8_t  pic_read_isr;   /* OCW3: port 20 reads ISR instead of IRR */

    /* ---- 8253 ----------------------------------------------------- */
    pit_counter pit[3];
    uint64_t irq0_next;      /* icount of the next counter-0 interrupt */
    uint64_t irq0_hold_until; /* icount; a control-word IRQ0 waits for an STI or this */
    uint8_t  irq0_held;
    /* A matched routine's call into original code (src/matched): machine_run
     * returns RUN_TRAP when CS:IP and SP reach the call's return. */
    uint8_t  trap_on;
    uint16_t trap_cs, trap_ip, trap_sp;
    uint64_t run_until;      /* the current machine_run's limit, for nested runs */
    uint8_t  pit_control_irq; /* DOSBox 0.74's IRQ0 on a PIT control word; F117R_PIT_CONTROL_IRQ=0 turns it off */
    uint64_t irq0_period_n;  /* edges since epoch, for exact scheduling */

    /* ---- keyboard controller --------------------------------------- */
    uint8_t  kbd_q[512];
    int      kbd_qh, kbd_qn;
    uint64_t kbd_next;       /* earliest icount for the next byte */
    uint8_t  port60;
    int      kbd_obf;        /* a byte is waiting in port 60 */
    uint8_t  con_cache;      /* CON: an extended key's scan code, held for the next read */
    uint8_t  port61;

    /* ---- VGA ------------------------------------------------------ */
    uint8_t  video_mode;
    uint8_t  dac[256 * 3];
    uint8_t  dac_display[256 * 3]; /* masked render palette: publish on blue */
    uint8_t  dac_widx, dac_ridx, dac_comp, dac_state;
    uint8_t  pel_mask;
    uint8_t  seq_idx, seq[8];
    uint8_t  gc_idx, gc[16];
    uint8_t  crtc_idx, crtc[32];
    uint8_t  attr_idx, attr[32], attr_flip;
    uint8_t  misc_out;
    uint64_t frame_len;      /* instructions per VGA frame */
    uint64_t vsync_next;
    /* GOG's svga_s3 renderer reads four groups of 50 mode-13h rows. */
    uint8_t  scan_work[64000], scan_pixels[64000], scan_dac[768];
    uint64_t scan_next, scan_frame, scan_time;
    uint16_t scan_start, scan_latch;
    uint8_t  scan_part, scan_valid, scan_mask, scan_blank;

    /* ---- AdLib (the half a driver can observe) --------------------- */
    uint8_t  opl_index;
    uint8_t  opl_t1_preset, opl_t2_preset;
    int      opl_t1_run, opl_t2_run, opl_t1_mask, opl_t2_mask;
    int      opl_t1_flag, opl_t2_flag;
    uint64_t opl_t1_due, opl_t2_due;     /* microseconds */
    /* A fix's own OPL writes, issued at their icount (fix D2's speech).
     * While any remain, the guest's writes to channel 0 are dropped: that
     * channel is the speech voice. shadow, when non-zero, is the linear
     * address of a byte whose value at the time of issue is written. */
    struct machine_opl_event { uint64_t at; uint32_t shadow; uint8_t reg, val; } *opl_sched;
    uint32_t opl_sched_n, opl_sched_i, opl_sched_cap;
    uint64_t opl_sched_dropped;

    /* ---- MPU-401 --------------------------------------------------- */
    uint8_t  mpu_q[16];
    unsigned mpu_head, mpu_used;
    int      mpu_uart;

    /* ---- game port ------------------------------------------------- */
    int      joy_present;
    unsigned joy_axis[4];    /* 0..255; 0x100 = no pot on the axis */
    unsigned joy_buttons;    /* bit 0..3, 1 = pressed */
    uint64_t joy_due[4];     /* microseconds */

    /* ---- mouse (INT 33h) ------------------------------------------- */
    int      mouse_present;
    int      mouse_x, mouse_y;           /* driver coordinates */
    int      mouse_buttons;
    int      mouse_hidden;               /* 0 shown, >0 hidden (driver counter) */
    int      mouse_xmin, mouse_xmax, mouse_ymin, mouse_ymax;
    unsigned mouse_press[2], mouse_release[2];
    int      mouse_press_x[2], mouse_press_y[2], mouse_rel_x[2], mouse_rel_y[2];
    int      mouse_mickey_x, mouse_mickey_y;
    uint16_t mouse_hnd_seg, mouse_hnd_off, mouse_hnd_mask;
    /* The graphics cursor the driver draws (INT 33h/09): screen mask then
     * cursor mask, 16 words each, and the hot spot. The driver draws into
     * video memory and saves the background for a move or hide. */
    uint16_t mouse_masks[32];
    int16_t  mouse_hot_x, mouse_hot_y;
    int      mouse_driver_installed;    /* a program reset the driver (AX=0) */
    int      mouse_background, mouse_background_text;
    int      mouse_back_x, mouse_back_y;
    uint8_t  mouse_back_pixels[256];
    uint32_t mouse_back_address;
    uint16_t mouse_back_text, mouse_text_and, mouse_text_xor;

    /* ---- pending input, by time ------------------------------------- */
    machine_input in_q[4096];
    int      in_qh, in_qn;
    machine_input_fn on_input;           /* recorder; NULL for none */
    void    *on_input_user;

    /* ---- counters, for reports -------------------------------------- */
    uint64_t opl_writes, midi_bytes, speaker_changes;
    /* Everything the machine sends OUT, folded into one value: each port
     * write (port, value, clock) and each byte DOS writes to a file. Memory
     * and registers do not show what went to the sound card, the palette or
     * the disk; a parity check compares this too. */
    uint64_t io_hash;

    /* ---- engine ---------------------------------------------------- */
    int      engine;                     /* ENGINE_* */
    uint64_t interp_steps;               /* instructions the interpreter ran */
    void    *recomp;                     /* the recomp runtime's state */
    char     fault[256];                 /* why the machine stopped, if it did */
};

enum { ENGINE_INTERP = 0, ENGINE_RECOMP = 1 };

/* Results of machine_run. */
enum { RUN_SLICE = 0, RUN_EXITED, RUN_FAULT, RUN_TRAP };

/* Build a machine and load `program` (F117.COM) from `data_dir`. `mem` is
 * MEM_SIZE bytes, owned by the caller. Returns 0 on failure with the reason
 * in m->fault. */
int  machine_boot(machine_t *m, uint8_t *mem, const char *data_dir,
                  const char *save_dir, const char *program, uint64_t ips,
                  uint64_t boot_time_us, const machine_hooks *hooks);
void machine_shutdown(machine_t *m);

/* The host's local time now, as microseconds counted as if it were UTC:
 * what to pass as boot_time_us for a live session. */
uint64_t machine_local_time_us(void);

/* Run until icount reaches `until` (RUN_SLICE), the program exits
 * (RUN_EXITED) or something unrecoverable happens (RUN_FAULT). */
int  machine_run(machine_t *m, uint64_t until);

/* The clock under F117R_TIMING=386 (src/cpu/timing386.h): CPU cycles of a 33.333 MHz 386DX. */
#define MACHINE_386_IPS 33333333ull

/* Microseconds of emulated time at icount. */
uint64_t machine_now_us(const machine_t *m);

/* Fold output into m->io_hash. */
static inline void machine_io_note(machine_t *m, uint64_t a, uint64_t b)
{
    uint64_t h = m->io_hash ^ (a * 0x9E3779B97F4A7C15ull);
    h = (h ^ b) * 0x100000001B3ull;
    m->io_hash = h ^ (h >> 29);
}

/* The state a parity check compares: all of memory, the registers, and
 * everything sent out (io_hash). */
uint64_t machine_state_hash(const machine_t *m);

/* F117R_INVENTORY=FILE: count the services and ports used (tooling only). */
void machine_inventory_port(uint16_t port, int write);
void machine_inventory_seq(uint8_t index);   /* a read of VGA sequencer data, by register */
void machine_inventory_service(uint8_t vec, uint16_t ax);

/* ---- input, from the host --------------------------------------------
 * Every input carries the icount at which it reaches the machine, and is
 * applied at that instruction boundary like any other event - never
 * "between slices" - so a run is the same however the host slices it, and a
 * recorded session replays exactly. The plain forms stamp the input with
 * the current icount (live play). machine_input is declared above. */
void machine_input_at(machine_t *m, const machine_input *in);
void machine_key_byte(machine_t *m, uint8_t b);
/* Absolute pointer in mode-13h pixels (0..319, 0..199), and buttons. */
void machine_mouse(machine_t *m, int x, int y, int buttons, int dx, int dy);
void machine_joystick(machine_t *m, int present, const unsigned axis[4], unsigned buttons);

/* ---- devices, for dos.c and the engines ------------------------------ */
uint32_t pc_io_read(cpu_t *c, uint16_t port, int width);
void     pc_io_write(cpu_t *c, uint16_t port, uint32_t val, int width);
void     pc_reset(machine_t *m);
void     pc_events(machine_t *m);          /* raise and deliver at this boundary */
uint64_t pc_next_event(machine_t *m);      /* earliest icount needing a look */
/* DOSBox's CPU slice boundary for I/O and DOS transfer costs. */
uint64_t pc_slice_left(const machine_t *m);
/* The PIT's own clock (1,193,182 Hz) at icount. */
uint64_t pc_pit_clock(const machine_t *m);
/* Counter 2's output and gate as the speaker sees them, for the host. */
int      pc_speaker_state(const machine_t *m, uint16_t *reload, int *mode);
/* What the VGA is showing: mode 13h/text, the display start address. */
uint16_t vga_start_address(const machine_t *m);
void     vga_set_mode(machine_t *m, uint8_t mode, int clear);
int      mouse_gran_x(const machine_t *m, int x);
void     mouse_new_video_mode(machine_t *m);
void     mouse_restore_cursor(machine_t *m);
void     mouse_draw_cursor(machine_t *m);
int      mouse_int33(machine_t *m);

/* ---- DOS / BIOS (dos.c) ---------------------------------------------- */
int  dos_int_hook(cpu_t *c, uint8_t vec);
void pc_release_irq0(machine_t *m);
void pc_advance_boot(machine_t *m, uint64_t clocks);   /* start the clock this far in (--boot-ms) */
/* Queue a fix's OPL write (times must not decrease); see opl_sched. */
int  machine_opl_schedule(machine_t *m, uint64_t at, uint8_t reg, uint8_t val, uint32_t shadow);
/* The icount of the last queued write, or 0 when none is pending. */
uint64_t machine_opl_scheduled_until(const machine_t *m);
int  dos_boot(machine_t *m, const char *program);
void dos_shutdown(machine_t *m);
const char *dos_current_program(const machine_t *m);
/* The paragraph after the end of the memory block holding seg (0: none). */
uint16_t dos_block_end(machine_t *m, uint16_t seg);
void dos_log(machine_t *m, const char *fmt, ...);

static inline machine_t *machine_of(cpu_t *c) { return (machine_t *)c->user; }

#endif /* F117R_MACHINE_H */
