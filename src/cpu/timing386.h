/* timing386.h - the 386DX/33 timing profile: what 86Box's 386DX/33 charges.
 *
 * With the profile on, an instruction advances the clock by the cycles 86Box's exec386_2386 core
 * charges for it (tools/ref86box/timing386.md): its CLOCK_CYCLES, the prefetch queue's refills
 * (prefetch_run), the prefixes, and the device cycles its memory and port accesses cost. The
 * machine's clock then counts CPU cycles at 33,333,333 a second. Off (the default), an instruction
 * costs one clock, as every recorded route assumes.
 *
 * The interpreter records what the cost depends on while it executes (t386_* fields of cpu_t) and
 * calls t386_step at the end of each step. */
#ifndef F117R_TIMING386_H
#define F117R_TIMING386_H

#include "cpu.h"

enum {
    T386_NORMAL, T386_JCC, T386_LOOP, T386_JCXZ, T386_REPSTR, T386_INTO, T386_HLT,
    T386_ENTER, T386_UD, T386_SHIFTN, T386_NOTIMED
};

typedef struct {
    int16_t has, instr, bytes, modrm, reads, reads_l, writes, writes_l;
} t386_pf_t;

typedef struct {
    uint8_t   kind;
    int16_t   cycles[2];     /* register form, memory form */
    t386_pf_t pf[2];
    uint8_t   flush;         /* 1 always, 2 when the branch is taken */
    int16_t   per;           /* cycles per string element */
} t386_op_t;

/* Memory and prefetch wait states: 2 with the board's external cache on, 6 off (cpu.c:4477-4519). */
#define T386_MEM_CACHED 2
#define T386_MEM_UNCACHED 6

/* Natively answered services, cycles per call beyond this machine's INT, stub and IRET (src/machine/
 * dos.c t386_service_cycles), calibrated with tools/ref86box/probe386.py against the MS-DOS 5.00
 * reference VM (build_msdos_vm.py), every IRQ masked while it measures. */
#define T386_SVC_SET13_FROM13      2690185
#define T386_SVC_SET13_FROM3       2414511
#define T386_SVC_SET3              1705359
#define T386_SVC_CURSOR            355
#define T386_SVC_WRITE_CHAR_TEXT   481
#define T386_SVC_TELETYPE_13       4847
#define T386_SVC_TELETYPE_TEXT     728
#define T386_SVC_GET_MODE          207
#define T386_SVC_DAC_BASE          167   /* measured at 256 colours only: the split is an estimate */
#define T386_SVC_DAC_EACH          192
#define T386_SVC_MOUSE_POSITION    347     /* Microsoft MOUSE.COM 6.26, serial mouse on COM1 */
#define T386_SVC_MOUSE_SET_POSITION 569
#define T386_SVC_MOUSE_RANGE       639     /* AX=7 measured; 8 and 0Fh taken as the same */
#define T386_SVC_MOUSE_INT10_HOOK  195     /* each INT 10h through MOUSE.COM's hook */
/* A mode set with MOUSE.COM loaded costs less than without it on the reference VM (measured,
 * every IRQ masked; the driver's hook takes its own path to the BIOS): these are subtracted. */
#define T386_SVC_MOUSE_SET13_FROM13 16262
#define T386_SVC_MOUSE_SET13_FROM3 6752
#define T386_SVC_MOUSE_SET3        9054
#define T386_SVC_KEY_CHECK         188
#define T386_SVC_TICKS             89
#define T386_SVC_DOS_STDIN_STATUS  3416    /* MS-DOS 5.00 (6.22 the same) */
#define T386_SVC_DOS_GET_TIME      1885    /* MS-DOS 5.00 (6.22 the same; it varies by 4 between runs) */

/* File services on the reference VM's disk (XT-IDE, 1989 3500 rpm preset: 35 sectors a track;
 * timing386.md, "File services"), charged at the call with interrupts held, as the video services
 * are. A read or write that does not follow the previous one on its handle pays a seek. Sequential
 * reads cost a sector each (T386_FILE_SECTOR, the media rate, with the average track crossing);
 * sequential writes cost the IDE word transfer, T386_FILE_BYTE_HUND/100 cycles a byte. Closing a
 * written file flushes the drive's write-behind cache. The first open after boot and the first
 * data write after boot cost the reference's cold start. A zero-length write (the truncate in
 * START's roster save) costs T386_FILE_TRUNC. */
#define T386_FILE_OPEN             50145
#define T386_FILE_OPEN_FIRST       2128293    /* average of two measured first opens, 1.84M/2.42M */
#define T386_FILE_ATTR             44402
#define T386_FILE_SEEK             1114
#define T386_FILE_CLOSE_READ       1800
#define T386_FILE_CLOSE_WRITE      4800000
#define T386_FILE_READ_CALL        15565
#define T386_FILE_READ_SEEK        440493    /* average of 3 measured seeks, 261k-530k (see timing386.md) */
#define T386_FILE_SECTOR           22500
#define T386_FILE_WRITE_SEEK       1200000
#define T386_FILE_WRITE_FIRST      9470000    /* the whole first data write after boot */
#define T386_FILE_TRUNC            4249
#define T386_FILE_BYTE_HUND        876

/* EXEC (INT 21h/4B00h) and overlay loads (4B03h), charged once the whole file is read
 * (dos_load_program/dos_load_overlay, dos_programs.c), as a file open+read+close would be.
 * Measured on the 1989 drive preset by EXEC-ing minimal children of known size
 * (D:/f117-gate/p1-dos-probe/exec1989.py: warm, 1 KB/9.5 KB/47 KB). A follow-up probe across
 * fifteen more sizes (exec_sizes.py) came back non-monotonic in size (164,590 cycles for 2 KB,
 * 1.7M for 6 KB, 4.4M for 40 KB): EXEC's cost is dominated by where the file lands relative to
 * the previous disk access, the same distance-dependent seek timing386.md's T386_FILE_READ_SEEK
 * already documents, not by its size. A flat per-byte or per-sector fit is therefore the wrong
 * shape here even more than for an ordinary read.
 *
 * PLAYER.EXE, DSWAP.EXE and START.EXE - the three loads that dominate the intro's drift - are
 * each measured directly at their own real position on this disk image instead
 * (D:/f117-gate/p1-dos-probe/realexec.py: open, read-whole, close, warm; a same-size synthetic
 * file inserted elsewhere, as the three sizes above are, is not a stand-in for a real file's real
 * seek distance, and in fact reads substantially less here: 935,592/1,435,010/3,566,636 measured
 * against 735,408/735,408/2,465,831 for same-size synthetic children). Any other load (the
 * intro's overlays, 0.7-15 KB, and any other program) is charged by which of the three synthetic
 * sizes it is closest to - a worse approximation, but the best available for a file whose own
 * position was not separately measured. Overlay loads are charged the same scale as EXEC: the
 * measured cost is almost entirely the disk access both kinds of load share. */
#define T386_EXEC_SMALL            710453     /* <= 1 KB: the 1,024-byte synthetic child */
#define T386_EXEC_MEDIUM           735408     /* <= 9,506 bytes: the 9.5 KB synthetic child */
#define T386_EXEC_LARGE            2465831    /* above that: the 47 KB synthetic child */
#define T386_EXEC_PLAYER           935592     /* PLAYER.EXE, measured at its real position */
#define T386_EXEC_DSWAP            1435010    /* DSWAP.EXE, measured at its real position */
#define T386_EXEC_START            3566636    /* START.EXE, measured at its real position */

/* The overhead stub's cost under the profile at a LOOP count of n: T386_DOS_LOOP_BASE +
 * T386_DOS_LOOP_EACH * n beyond the call's own INT (calibrated with probe386.py). */
#define T386_DOS_LOOP_BASE         6
#define T386_DOS_LOOP_EACH         13

/* Turn the profile on: memory at `mem_wait` cycles (T386_MEM_*), VGA memory [vga_lo, vga_lo +
 * vga_size) at `vga_byte` cycles a byte, the prefetch queue empty. */
void t386_enable(cpu_t *c, int mem_wait, uint32_t vga_lo, uint32_t vga_size, uint32_t vga_byte);

/* A translated instruction has already decoded these fields. Prepare the same
 * timing inputs as cpu_step, including every repeated prefix. */
void t386_begin(cpu_t *c, uint16_t next, int modrm, int seg, int rep,
                int seg_prefixes, int rep_prefixes, int lock_prefixes);

/* Called by cpu_step after the instruction ran: the cycles it costs, which the caller adds to the
 * clock in place of one. */
uint32_t t386_step(cpu_t *c, uint8_t op, int stop);

#endif
