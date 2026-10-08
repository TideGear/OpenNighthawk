# 86Box's 386DX/33: where emulated time goes

The reference for the 386DX/33 timing profile (roadmap Phase 1). Read from the
86Box source (`D:/86box-src/src/src`, the tree `86box-trace.patch` applies to)
for the profile `D:\86box\vmt386` (`make_profile386.py`): machine `ami495`
(OPTi 495SX), i386DX at 33,333,333 Hz, no FPU, 1 MB, IBM VGA, AdLib,
XT-IDE AT disk (`hdc_1 = xtide_at`, `hdd_01_speed = ramdisk`), dynarec off.
File and line numbers are that tree's (8 Oct 2026). 1 us = 33.333 cycles.

## The CPU

- Core: the `exec386_2386` interpreter (`cpu/386.c`; chosen at cpu.c:1855-1868
  for a 386DX without the dynarec). Each instruction adds its cycles to `tsc`
  and due timers run after every instruction (386.c:406-418).
- Costs are constants in each `ops_2386_*` handler; the shared ones are set at
  cpu.c:1047-1081: ALU r,r 2; r,m 6; m,r 7; m,m 6 (32-bit the same); Jcc taken
  3+4, not taken 3; INT n 37, IRET 22, CALL far 17, RETF 18, JMP far 12.
  Examples: MOV r,m8 4, MOV m,r 2 (x86_ops_mov.h:559, 661); LOOP 11, JCXZ 5
  (+4 taken), JMP/CALL near 7, RET 10 (x86_ops_jump.h); MUL 13/21, IMUL
  14/22/38, DIV 14/22/38, IDIV 19/27/43 (x86_ops_misc.h); IMUL r,rm 14/17;
  MOV sreg,r 2, MOV sreg,m 5, LDS/LES 7; INT3 59, exceptions 70; HLT 100 per
  pass; IN 12, OUT imm 10, OUT DX 11 (x86_ops_io.h).
- A hardware interrupt's entry in real mode costs nothing (386.c:388-401).
- Prefetch (386_common.c:515-567): a 16-byte queue filled 4 bytes at a time.
  An instruction whose bytes drive the queue negative refills it at
  `cpu_prefetch_cycles` per 4 bytes; data reads and writes cost nothing
  directly and only use up cycles that would otherwise refill the queue
  (`mem_cycles`). A taken branch flushes the queue.
- REP string instructions (x86_ops_rep_2386.h): the prefix costs 2 each time
  it is dispatched; per element MOVS 4, STOS 5, LODS 5, CMPS 9, SCAS 8, INS 15,
  OUTS 14. MOVS, STOS, LODS and SCAS run until about 100 cycles are spent
  (`cycles_end = cycles - 100`, line 217) and are dispatched again, paying the
  2 again (REPNE SCASB: 13 elements a dispatch, measured); CMPS, INS and OUTS
  do one element per dispatch.

## Memory

- `cpu_update_waitstates` (cpu.c:4477-4519): with the external cache flag set,
  2 cycles per 4-byte prefetch and 2 per read or write; without it the CPU
  table's 6. The flag ends up set by the BIOS: OPTi register 0x21 bit 4
  (opti495.c:159-161; ports 0x22/0x24), from the CMOS setup in `vmt386\nvr`.
  Read it from the running VM.
- ROM code costs what RAM code costs under this core. The 33-cycle ROM rate
  (`cpu_rom_prefetch_cycles`, cpu.c:579-582) is installed only by
  `getpccache()` (mem.c:637-660), and the core's fetch helpers, built with
  `OPS_286_386`, never call it (386_common.h:300-345).

## Each instruction in detail

The per-opcode table is `ops386.json` (513 entries: one-byte opcodes with
group opcodes split by ModRM reg, `0F xx`, and what follows REP; each with its
`CLOCK_CYCLES` and `PREFETCH_RUN` arguments for the register and memory forms,
flushes, data-dependent costs and the source line), read from a preprocessed
`386.c` and checked by hand; macro definitions and constants are in
`ops386_meta.json`. The rules around it (386.c:226-427, 386_common.c:503-573):

- A step: `ins_cycles = cycles`, fetch, dispatch; prefixes are handlers that
  call the next handler, so a prefixed instruction is one step. Segment
  override 4, REP/REPNE 2, LOCK 4, 66/67 2, `0F` 0, each also counted in
  `prefetch_prefixes`. Then `tsc += ins_cycles - cycles` once, at the end:
  devices that read the clock during an instruction see its start.
- `prefetch_run(instr, bytes, modrm, reads, reads_l, writes, writes_l, ea32)`:
  `mem = reads * read + ... + writes_l * write_l`; `instr = max(instr, mem)`;
  the queue loses the prefixes, `bytes` and the ModRM displacement (mod 0
  rm 6: 2; mod 1: 1; mod 2: 2); while it is below 0 it gains 4 bytes and
  `cycles -= cpu_prefetch_cycles` (the only time the queue costs anything);
  then `instr - mem` refills it free, 4 bytes per `cpu_prefetch_cycles`, the
  remainder dropped; cap 16. `prefetch_flush()` empties it: taken jumps,
  CALL, RET, RETF, IRET, far JMP and CALL; INT n, exceptions and IRQ entry do
  not flush. A handler without `PREFETCH_RUN` (a prefix alone, WAIT, MOV SS, a
  zero-count shift) leaves its prefixes to the next instruction's run. Some
  byte counts are as coded, not as encoded (RET imm16 5, ENTER 3, `0F` forms
  count the `0F` as a prefix).
- Device cycles (VGA, an AdLib read) go straight into `cycles`; `prefetch_run`
  still counts the access at `cpu_cycles_write`, so they never feed the free
  refill, except in handlers that pass `cycles_old - cycles` as the
  instruction's cycles (far CALL and JMP, RETF, IRET, INT).
- An unfinished REP string instruction sets `pc` back to its first prefix and
  ends the step after more than 100 cycles (device cycles included): STOSB to
  RAM 21 elements a step, MOVSB 26, STOSB to the VGA 3, STOSW 2. Each step
  pays the prefixes again, counts as an instruction and lets an interrupt in.
  CMPS, INS and OUTS do one element a step; CX = 0 costs the prefix and
  `PREFETCH_RUN(0, 1, ...)`.
- POP SS and MOV SS execute the next instruction inside themselves: one step,
  no interrupt check between; MOV SS itself charges nothing.
- A real-mode hardware interrupt's entry costs 0 and does not flush. Faults:
  `x86_int` (#DE, #UD, BOUND, single step, NMI) 70; a word at offset FFFF
  (#GP/#SS through `abrt`) 0. Misaligned accesses cost nothing on the 386.

## The VGA

- ISA timing 8/16/32 (vid_vga.c:32) times `isa_cycles` = 4 (cpu.c:577): a byte
  read or write in A0000-BFFFF costs 32 cycles, a word 64, a dword 128, added
  to the instruction (vid_svga.c:1836, 2024, 2253-2440). REP STOSW to A000 is
  5 + 64 a word. The VGA's ports cost nothing extra. (`B86_VGA_FAST`, a local
  test switch, makes them 1/1/2.)
- Mode 13h: a line is 1,059.26 cycles (847.4 displayed), 449 lines a frame,
  475,610 cycles (70.086 Hz). Vertical retrace is set at `vsyncstart`
  (vid_svga.c:1557) and cleared at the CRTC 0x11 line match (1420).

## I/O and timers

- Unclaimed ports and port 0x84 cost `io_delay` = 11 (io.c:384, 442). The PIT,
  PIC, port 0x61, keyboard controller, VGA and IDE ports cost nothing extra;
  CMOS 0x70/0x71 32 each.
- AdLib (Nuked OPL2): a read of 0x388/0x389 costs `isa_timing * 8` with
  `isa_timing = cpuclock / cpu_isa_speed` (snd_opl2_nuked.c:1383, pit.c:1254):
  47 cycles at the chipset's default bus/6 ISA clock (opti495.c:283), 32, 24 or
  20 if the BIOS sets OPTi register 0x25 to /4, /3 or 2/5. Writes cost nothing.
  OPL timers count 80 us and 320 us of emulated time.
- PIT: 8254, 27.9365 cycles a PIT clock, stepped in `tsc`; IRQ0 taken at the
  next instruction boundary.
- DRAM refresh and DMA take no CPU time.

## The disk (XT-IDE AT, a RAM-disk speed preset)

- Every seek or read: 50 us (hdd.c:33). PIO mode 3: 512 bytes in 46.08 us.
- READ SECTORS: 96.08 us to the first DRQ and between sectors (hdc_ide.c:
  1876-1884, 2076-2079), plus the host's transfer loop (REP INSW: 17 cycles a
  word, 4,352 a sector, slower when run from unshadowed ROM).
- READ MULTIPLE 2,000 us before the first data, WRITE 2,000 us before the
  first DRQ then 96.08 us a sector; SEEK and recalibrate 1,000 us.

## Measured: `probe386.py`

`tools/ref86box/probe386.py OUT` runs blocks of one instruction class each
(interrupts off, mode 13h) and reads 86Box's cycle counter at a debug-port
write before each (`B86_PORTLOG`, build_86box.md); this machine logs its
clock the same way (`F117R_PORTLOG`). A block's count includes its mark
(`mov al,n` / `out 0E9h,al`), its setup and the three-instruction restore of
DS and ES (the empty block: 29 cycles, 5 instructions). 86Box counts a REP
instruction once per dispatch, so its instruction count there is the chunks,
not the elements. Measured 8 Oct 2026 on `vmt386`:

| block | instructions timed | 86Box cycles | 86Box instructions counted |
|---|---|---|---|
| empty (the mark itself) | 0 | 29 | 5 |
| nop x64 | 64 | 221 | 69 |
| add r16,r16 x64 | 64 | 157 | 69 |
| mov r16,imm16 x64 | 64 | 157 | 69 |
| mov r16,[si] x64 | 64 | 287 | 70 |
| mov [si],r16 x64 | 64 | 217 | 70 |
| add [si],r16 x64 | 64 | 537 | 70 |
| cmp r16,[bx+si] x64 | 64 | 417 | 71 |
| mov r16,[si+disp16] x64 | 64 | 287 | 70 |
| push/pop x32 | 64 | 221 | 69 |
| lodsb x64 | 64 | 351 | 70 |
| shl r16,1 x64 | 64 | 221 | 69 |
| shl r16,cl(5) x32 | 32 | 127 | 38 |
| jmp short +0 x64 | 64 | 605 | 69 |
| jnz not taken x64 | 64 | 223 | 70 |
| jz taken +0 x64 | 64 | 607 | 70 |
| loop x64 | 64 | 861 | 70 |
| mul r16 x32 | 32 | 703 | 38 |
| div r16 x32 | 32 | 739 | 40 |
| les r16,[si] x32 | 32 | 255 | 38 |
| mov ds,r16 x32 | 32 | 95 | 38 |
| rep movsb 200 | 200 | 853 | 16 |
| rep stosb 200 | 200 | 1053 | 17 |
| rep movsw 200 | 200 | 853 | 16 |
| in al,40h x32 | 32 | 413 | 37 |
| in al,61h x32 | 32 | 413 | 37 |
| in al,dx(388h) x32 | 32 | 1439 | 38 |
| in al,dx(3DAh) x32 | 32 | 415 | 38 |
| mov es:[di],al (A000) x32 | 32 | 1293 | 40 |
| mov al,es:[di] (A000) x32 | 32 | 1315 | 40 |
| rep stosb A000 200 | 200 | 7571 | 76 |
| rep stosw A000 200 | 200 | 14037 | 109 |
| rep movsb A000->A000 200 | 200 | 14383 | 110 |

The figures agree with the source above: 2 cycles a register ALU op, a taken
jump 7 plus the queue refill, MUL 21, DIV 22, IN 12, an AdLib read 12 + 32
(so the BIOS runs the ISA bus at /4, not the chipset's default /6, which
would make it 47), 32 extra a VGA byte and 64 a word.

## Frame comparison: `frames386.py`

`tools/ref86box/frames386.py OUT` runs the intro on both machines and compares
them picture by picture: 86Box traced (`trace_86box.ps1`, a copy of `vmt386`,
`probe86.bare_boot`, fast-forward, a PPM per new picture) and this machine with
`f117run --timing 386 --engine interp --shots-vga --shots-changed --frame-log`
(each VGA frame's scan-out clock, steps and screen-off bit; only changed
pictures written). SETUP is answered the same way on both (n, then 2 300 frames
later). Pictures are compared in 6-bit DAC values and aligned in order on exact
equality; 86Box posts no frame while the screen is off, so a gap of more than
1.5 frame periods is a blank picture, matched against our screen-off frames.
The origin is the first picture both show; drift is 86Box's time since it minus
ours. The report gives scene 0 (SETUP's last key to the origin), every step of
two frames or more between consecutive exact pictures with the events of our
log in the interval, and the drift per scene. PASS needs every picture exact and
on the same frame. `--box DIR`/`--ours DIR` reuse a run (rerun only ours after a
profile change: about 15 s); `--only box|ours` makes one run. Both runs are
deterministic: two runs gave byte-identical `frames.csv` on each side.

Result (8 Oct 2026, 139.8 s from the origin): **FAIL**. 1,238 of 1,275 86Box
picture changes exact in order (992 of the 993 held two frames or more); the 37
others are 36 single-frame pictures (mid-draw or mid-fade: 86Box scans line by
line, this machine in four 100-line parts) and START's mode-set blank, which this
machine does not have. Drift at the end +1,571 ms (+110 frames), all of it in
seven steps, each at a load:

| where (86Box s) | added | our events in the interval |
|---|---|---|
| scene 0, key to MPS logo | +215 ms | SETUP exits, EXEC MPS_LOGO, ASOUND.LOG overlay, INT 10h mode 13h (86Box: 85.6 ms screen-off) |
| 0.00-0.39 | +130 ms | MPS_LOGO reads `labslogo.SS` in 24 DOS reads of 512 bytes |
| 5.65-7.56 | +171 ms | MPS_LOGO exits; ASOUND.117, MISC and MGRAPHIC overlays; EXEC PLAYER |
| 7.56-8.49 (blank) | +71 ms | PLAYER opens and reads the three title pictures (23 reads, 154 KB) |
| 102.1-108.6 | +270 ms | PLAYER exits; EXEC DSWAP, then START; INT 10h mode set; two reads |
| 108.6-109.9 | +886 ms | START reads and rewrites `ROSTER.FIL` (2 reads, 3 writes) |
| 109.9-111.4 | +86 ms | START reads `rostscrn.pic`, `rostsprt.pic` |

Between those steps the drift stays put: over PLAYER's 95 s of animation,
credits and panning pictures (scenes from 8.5 to 102 s) it moves only between 24
and 28 frames, with no trend, and each scene keeps one or two adjacent values (the
game's scene changes wait on timer ticks, so the phase between the tick and the
frame decides between two neighbouring frames). A steady 0.109-frame offset is
where each machine takes its picture (86Box at vertical sync start, this machine
at line 400). So the CPU, VGA and port costs hold 86Box's pace. What is missing
is the disk and DOS path (`EXEC`, overlay loads, reads and writes go through
FreeDOS, the AMI BIOS's INT 13h and the IDE model on 86Box, and cost nothing
here) and the BIOS's INT 10h mode set (screen off and the video memory cleared
at ISA speed). Two 86Box runs attribute them: with `B86_BUFFERS=20` (FreeDOS's
sector buffers) the `ROSTER.FIL` step goes and the end drift falls to +300 ms;
with `B86_VGA_FAST=1` scene 0 falls from +215 to +101 ms, which puts about
114 ms of it on the mode set's video memory. The first step (24 reads of 512
bytes, +130 ms with 1 buffer, +116 ms with 20) is about 5 ms a DOS read.

The mouse: the VM loads no driver (the image's stock `FDAUTO.BAT` and
`FDCONFIG.SYS` load none, `FREEDOS\BIN` has none, `86box.cfg` has
`mouse_type = none`, and `bare_boot` replaces the boot files anyway), so INT
33h reaches the BIOS's dummy handler. This machine always provided a driver;
`f117run --no-mouse` (INT 33h answers as without one) is now the default here.
SETUP resets the mouse once; PLAYER polls INT 33h AX=3 and INT 21h AH=0Bh in
its wait loop (591,735 times each over the intro). The intro's pictures and
timing are the same either way, on both machines. 86Box with CuteMouse loaded
(`--mouse-driver`, `mouse_type = msserial`) gives the same 1,275 pictures and the
same steps as without (frame times within 108 cycles); START draws its own
pointer on the roster screen in both. With our driver the roster screen's
pointer area changes about every six frames (150 more picture changes in its
last 28 s, against none on 86Box with CuteMouse): a lead for INT 33h, not for
the profile.

## Services this machine answers natively

86Box runs real BIOS and DOS code (the IBM VGA BIOS, the board's AMI BIOS,
FreeDOS 1.3) for the services this machine answers natively. Their cost per
call, measured with `probe386.py` and charged under the profile
(`src/machine/dos.c` t386_service_cycles, the figures in
`src/cpu/timing386.h`): INT 10h mode set 13h about 2.66 million cycles from
13h (80 ms; most of it clearing video memory at ISA speed), 2.41 million from
mode 3, mode 3 1.70 million; a 256-colour palette block 49,800; cursor,
get-mode, teletype and write-character a few hundred to 4,900; INT 16h key
check 250; INT 1Ah tick count 150; INT 21h AH=0Bh 3,500 and AH=2Ch 4,100
(FreeDOS's). Video BIOS calls are charged at the INT, with interrupts held;
DOS calls pass their time in the stub's LOOP with interrupts on, as FreeDOS
enables them inside INT 21h. Charged at the INT, the intro's 689,000 AH=0Bh
polls held the music driver's interrupts back and its scenes ran up to 140 ms
long; through the loop they hold 86Box's pace to a frame. The DAC block's
split between a fixed part and a part per colour is estimated (one size
measured). Open: the mouse driver (86Box's VM loads none), file reads and
writes, program starts and overlay loads.
