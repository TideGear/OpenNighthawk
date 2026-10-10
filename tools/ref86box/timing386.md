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

### Preliminary DOS file-service measurements (9 Oct 2026)

A private COM probe on the MS-DOS 5.00/MOUSE.COM reference opens VGAME.EXE,
seeks to zero before each read, reads into conventional memory, then closes;
eight trials, IRQs unmasked. Debug-port markers bracket each service, including
the marking OUT, MOV AX and result-saving overhead. Both boots returned the
requested byte counts with CF clear. Their complete 400-record port logs were
byte-identical; the second run exited cleanly, the first failed at shutdown
after all measurements and 3,000 frames. These are exploratory measurements,
not service constants installed in the runtime.

| Operation | Typical cycles | Milliseconds at 33.333 MHz |
|---|---:|---:|
| Warm open | 50,145 | 1.50 |
| First open | 509,082 | 15.27 |
| Seek to zero | 1,114 | 0.033 |
| Read 512 bytes | 79,595 | 2.39 |
| Read 4,096 bytes | 109,972 | 3.30 |
| Read 16,384 bytes | 217,180 | 6.52 |
| Read 32,768 bytes | 361,095 | 10.83 |
| Close | 1,800 | 0.054 |

The cold/warm distinction is modelled as described under "File services" below.
The private probes, raw logs and comparison are under `D:/f117-gate/p1-dos-probe/`.
An extended wildcard FCB volume-label search measured 21,300-21,604 cycles
(about 0.65 ms), returning AL=FF (no label). Its direct cost is small; our
DOSBox-style model instead supplies C_DRIVE/AL=0. START's expected name is
F117A-SF (DGROUP:617E); its helper at 804E and branch at 7285 select the
roster-write path for both results, so the label difference does not explain
that step by changing this branch.
Creating, writing and closing a private 802-byte TIMING.BIN (the roster's
size) shows a larger cold cost: first write 10,055,261 cycles (301.66 ms),
later writes 19,582 (0.587 ms), all returning 802 with CF clear. First create
610,313 cycles, second 858,306, then 93,949; first close 123,432, then 47,424.
A 512+290-byte split gives 10,052,946 cycles for the first 512, 5,251 for
the following 290; later 512-byte writes cost 17,267 (clean exit). Splitting
the write does not double its cold cost. This FAT16 disk has four sectors
per cluster.

### File services in the 386 profile (9 Oct 2026)

The profile charges each file call at the call, with interrupts held, as the
video services are (`src/machine/dos_files.c` `t386_file`; constants
`T386_FILE_*` in `src/cpu/timing386.h`). Nothing is charged with the profile
off. Each cost below is in cycles at 33.333 MHz.

START's ROSTER.FIL sequence, from the `roster_edit` route with
`F117R_TRACE_FILES=1`: the read phase is open read-only, get attributes, read
512, read 512, close; the save is open read-write, get attributes, a zero-length
write (the truncate), write 512, write 290, close. The probe
(`D:/f117-gate/p1-dos-probe/roster.py`) runs that sequence on the reference VM
twice in one boot:

| call | cold (first pass) | warm (second) |
|---|---:|---:|
| open read-only | 429,202 | 657,617 |
| get attributes | 44,402 | 44,402 |
| read 512 | 155,974 | 79,605 |
| read 290 (second 512 read) | 80,471 | 4,159 |
| close read-only | 1,800 | 1,800 |
| open read-write | 46,425 | 46,425 |
| zero-length write | 4,249 | 4,249 |
| write 512 | 9,863,479 | 224,577 |
| write 290 | 5,251 | 5,251 |
| close written | 123,428 | 66,642 |

The cold write is the first data write after boot: 9.86 M cycles (296 ms),
the same as the create probe's 10.06 M. Its mechanism is not located (it is
the same on the 1989 drive preset, 9.47 M, so it is not the drive's seek
model). The 1989_3500rpm preset (`hdd_01_speed`), run on the same sequence,
costs more on every call: warm open 3.56 M, warm read 512 1.10 M, close
written 5.08 M (the flush). It is a period-drive experiment, not the reference;
which drive the owner's machine had is unknown.

The RAM-disk fit, recorded for comparison (the reference's preset was
`ramdisk` until 9 Oct 2026):

| call | charge |
|---|---|
| open (AH=3Dh, 3Ch) | 50,145, plus 460,000 for the first open after boot |
| get attributes (AH=43h) | 44,402 |
| seek (AH=42h) | 1,114 |
| read (AH=3Fh) | 74,090 + 8.76 cycles a byte transferred |
| write (AH=40h), n > 0 | 12,000 + 8.76 cycles a byte, plus 9,800,000 for the first one after boot |
| zero-length write | 4,249 |
| close, read only | 1,800 |
| close, written | 60,000 |

Result on the MS-DOS 5.00 reference with the RAM-disk preset (`frames386.py`,
the intro against the same 86Box run, A/B with the charges switched off):

| | drift at the end | exact pictures | roster step | verdict |
|---|---:|---:|---:|---|
| without file charges | +642.5 ms | 1,238 of 1,277 | +623 ms | FAIL |
| with file charges | +228.7 ms | 1,238 of 1,277 | +295 ms | FAIL |

**The reference is now the 1989 3500 rpm preset (9 Oct 2026).** Its sequential
reads behave differently from the RAM-disk preset, so the fit changed. The
sequence probe (`D:/f117-gate/p1-dos-probe/seq.py`, VGAME.EXE, sixteen 4,096-byte
and sixteen 512-byte sequential reads, cold then warm) gives, for the warm pass:

- 4,096-byte reads: 138,319 cycles each, with a spike to about 390,000 to 405,000
  about every fourth read, where the read crosses a 35-sector track (average
  189,523 over the fifteen reads after the first);
- 512-byte reads: 30,909 each (average 31,009), a sector at media speed;
- the first read after the open: about 0.57 M (512 bytes) to 1.26 M (4,096).

So a sequential read costs about 15,400 cycles a sector (the media rate, 0.46 ms
at 3500 rpm) plus the call, and a read that does not follow the previous one on
its handle pays a seek of about 0.55 M. Closing a written file costs about 4.8 M
(the drive's write-behind flush: 4.49 M cold, 5.08 M warm). The cold first data
write after boot is 9.47 M, the same on either preset. A warm first write after a
seek costs about 1.27 M.

The model, fitted to the 1989 probes (`src/cpu/timing386.h`, `T386_FILE_*`):

| call | charge |
|---|---|
| open (AH=3Dh, 3Ch) | 50,145, plus 2,128,293 for the first open after boot (the average of two measured first opens on different files, 1,839,141 and 2,417,445: the cost depends on the file's directory and FAT position, not only on being first) |
| get attributes (AH=43h) | 44,402 |
| seek (AH=42h) | 1,114 |
| read (AH=3Fh) | 15,565 + 22,500 a sector (rounded up), plus 440,493 if the read does not start where the previous read on the handle ended (the average of three measured seeks, 261,244 to 530,173: a flat constant cannot capture the real, distance-dependent seek cost, so this is a rough approximation) |
| write (AH=40h), n > 0 | 8.76 cycles a byte, plus 1,200,000 if the write does not start where the previous one ended; the first data write after boot is 9,470,000 in all |
| zero-length write | 4,249 |
| close, read only | 1,800 |
| close, written | 4,800,000 (the flush) |

The 22,500 a sector averages the track crossings. It fits the 4,096-byte reads
(195,600 against 189,500 measured, 3% high) and over-charges single-sector
sequential reads by about 0.2 ms (38,000 against 31,000).

**EXEC and overlays, charged (9 Oct 2026).** EXEC of child programs of 1 KB, 9.5 KB
and 47 KB (`D:/f117-gate/p1-dos-probe/exec1989.py`, warm): 710,453, 735,408 and
2,465,831 cycles. The 47 KB figure is START's size; PLAYER and DSWAP are 9.5 and
8.6 KB, the intro's overlays 0.7 to 15 KB. The EXEC cost is mostly its fixed
part, a seek and a directory read, plus media time for big images; three sizes
were too few to fit a per-byte model. A follow-up probe across fifteen more
sizes (`exec_sizes.py`, 512 B to 47,100 B) came back non-monotonic in size
(164,590 cycles for 2 KB, 1.7M for 6 KB, 4.4M for 40 KB): EXEC's cost is
dominated by where the loaded file lands relative to the previous disk access
(the same distance-dependent seek this file's T386_FILE_READ_SEEK already
cannot capture with a flat constant), not by its size, so the expanded probe
could not improve on the original three clean points. The model
(`T386_EXEC_SMALL/MEDIUM/LARGE`, `src/cpu/timing386.h`; `dos_programs.c`
`t386_exec`, called once `dos_load_program`/`dos_load_overlay` finish reading
the file) charges each real load by which of the three measured sizes it is
closest to (<=1 KB, <=9,506 bytes, larger) instead of a formula; overlay loads
are charged the same scale since they share the same disk access and no
overlay load has been measured on its own.

Result on the 1989 reference (`frames386.py`, the intro against the saved
86Box run `D:/f117-gate/frames-1989/box`):

| | drift at the end | exact pictures | largest steps (86Box minus ours) |
|---|---:|---:|---|
| without EXEC/overlay charges | +428.5 ms | 1,237 of 1,275 | MPS_LOGO's exit +214 ms, PLAYER's exit to START +248 ms, the roster +280 ms, title loads -128 ms |
| with EXEC/overlay charges | +96.4 ms | 1,237 of 1,275 | the PLAYER-exit/DSWAP/START-exec block +147.7 ms, the roster +280 ms (unaffected: that step has no EXEC in it) |

The same 1,237 exact pictures both ways (no regression); the end drift falls
78%. The roster step's +280 ms is unchanged because nothing in it is a program
load - it is ordinary file access, already charged by the file-service model.

**Reference parity on the 1989 drive** (`py tools/pc_parity.py`, 9 Oct 2026,
after the EXEC/overlay charge): GOG, DOSBox-X, music, sound and roster checks
pass (the roster is byte-identical on both references). The picture check now
passes: p037 (diff 0.028, hash `75c269fac9e7`) is a mid-fade capture-timing
artifact, confirmed and added to `expected_misses86.txt` (the title screen's
brightness ramp rises about 5,400 DAC-units a frame through that stretch, and
our nearest frame already beats both its neighbours). One check still fails:
the timing check's longest-scene duration difference is 0.37 s against a
0.35 s limit (19 of 22 scenes paired, start drift 2.05 s, both within limits).

**PLAYER.EXE/DSWAP.EXE/START.EXE measured at their own real position (10 Oct
2026).** The three sizes above are synthetic test files, inserted at whatever
position the probe happened to leave free; they are not a stand-in for a real
file's own seek distance (the finding above - EXEC's cost is seek-distance
dominated, not size dominated - means a different file of the same size can
cost a very different amount). `realexec.py` measures the three real files
directly: open, read the whole file, close, by name, already in place on the
image (warm): 935,592 (PLAYER.EXE), 1,435,010 (DSWAP.EXE), 3,566,636
(START.EXE) cycles - all higher than the same-size synthetic children
(735,408, 735,408, 2,465,831). `T386_EXEC_PLAYER/DSWAP/START` (`timing386.h`)
charge these three named files their own measured cost; `t386_exec`
(`dos_programs.c`) checks the upper-cased basename first and falls back to
the synthetic-size tiers for any other load (the intro's overlays, and any
other program).

Result (`frames386.py`, same saved 86Box run):

| | drift at the end | exact pictures |
|---|---:|---:|
| synthetic-size EXEC charge | +96.4 ms | 1,237 of 1,275 |
| real-position EXEC charge | +39.3 ms | 1,237 of 1,275 |

A further 59% fall, same exact-picture count. `pc_parity.py`'s "timing 86box"
check still fails at the same 0.37 s: its failing scene (ours holding a
picture 100.22-106.40 s, 86Box 150.33-156.88 s - the pairing's fourth-from-
last row) sits in the same stretch of the intro as the measured loads but is
a different metric (how long one held picture lasts, not when transitions
land); this charge was not expected to move it and did not. Not investigated
further this session - the next step is identifying what that specific held
picture is and what, inside its span, the original spends 0.37 s longer on.

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
not the elements. Measured 8 Oct 2026 on `vmt386` (the instruction blocks are the same
on `vmt386dos500`, 9 Oct 2026):

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
them picture by picture: 86Box traced (`trace_86box.ps1`, a copy of `vmt386dos500`,
the default since 9 Oct 2026, or of `--profile`; `probe86.bare_boot`, fast-forward, a PPM per new picture) and this machine with
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

Result on the FreeDOS VM `vmt386` (8 Oct 2026, 139.8 s from the origin): **FAIL**. 1,238 of 1,275 86Box
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

On the MS-DOS 5.00 VM with MOUSE.COM, the default since 9 Oct 2026 (ours keeps
its INT 33h driver, as the VM loads one): **FAIL** on the same terms, 1,238 of
1,277 picture changes exact in order (991 of the 992 held two frames or more),
272 of them on the same frame, end drift +642.5 ms (+45 frames, before the file charges; with them +228.7 ms, "File services") in 15 steps of
two frames or more; `vmt386dos500h` (HIMEM, DOS=HIGH) 1,237 of 1,273, +728.1 ms.
START's `ROSTER.FIL` step is about +630 ms there (+886 ms under the FreeDOS bare
boot's one buffer); the HIMEM VM falls 86 ms further behind at PLAYER's load
(5.6-13.2 s) and holds it.

The mouse on the FreeDOS VM: it loads no driver (the image's stock `FDAUTO.BAT` and
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

## The game's frame rate in flight

What the profile is for: the game steps its flight model by its own frame rate
(S, [0x368E], frames a second) and reads the stick once a frame, so S decides how
the same inputs fly. `stick_response.py` reads S through the supply-drop route's
first flight minutes on each machine (8 Oct 2026):

| machine | S in flight |
|---|---|
| this machine, DOSBox's model (9 M clocks a second) | 14, 15 |
| DOSBox-X | 13, 15 |
| 86Box 386DX/33, MS-DOS 5.00 and MOUSE.COM (9 Oct 2026; 6-9 on the FreeDOS VM) | 7, 8, 9 |
| this machine, `--timing 386` | 5, 7, 8, 9 |

So under the profile the game runs at the period PC's pace, not DOSBox's. A single
stick tap's effect (the same table) varies with where the tap falls in a frame and
does not compare closely at three repeats a size; S is the figure that does.

## Services this machine answers natively

86Box runs real BIOS and DOS code (the IBM VGA BIOS, the board's AMI BIOS,
and DOS) for the services this machine answers natively. The reference DOS is
MS-DOS 5.00 (`build_msdos_vm.py` builds the VM from the owner's disks; 4.01
and 6.22 VMs too). Their cost per call, measured with `probe386.py` with every
IRQ masked (the BIOS and DOS enable interrupts inside, so an unmasked tick adds
its handler) and charged under the profile (`src/machine/dos.c`
t386_service_cycles, the figures in `src/cpu/timing386.h`): INT 10h mode set
13h about 2.69 million cycles from 13h (80 ms; most of it clearing video memory
at ISA speed), 2.41 million from mode 3, mode 3 1.71 million; a 256-colour
palette block 49,400; cursor, get-mode, teletype and write-character a few
hundred to 4,900; INT 16h key check 250; INT 1Ah tick count 150. The DOS calls
by DOS (cycles a call; the CPU blocks and BIOS calls are identical on all four):

| | FreeDOS 1.3 | MS-DOS 4.01 | MS-DOS 5.00 | MS-DOS 6.22 |
|---|---|---|---|---|
| INT 21h AH=0Bh | 3,526 | 3,121 | 3,483 | 3,483 |
| INT 21h AH=2Ch | 4,104 | 1,833 | 1,956 | 1,952 |

so 5.00 and 6.22 are the same machine for this game.

The mouse: no MS-DOS release includes a driver (it came with the mouse). On the
5.00 VM with 86Box's Microsoft serial mouse on COM1, cycles per INT 33h AX=3
(the position, 2.3 million calls a mission): no driver 63, Microsoft MOUSE.COM
6.26 389, 8.20 370, CuteMouse 2.1 214; 9.01 (1993) hangs while loading on this
board. The reference is 6.26 (1989-90), loaded by the 5.00 VM
(`build_msdos_vm.py --mouse`). With it loaded every other INT 10h call costs 195
more (the driver hooks INT 10h to follow the video mode), and a mode set 6,750
to 16,260 less. The VM's reset (AX=0) waits on a timer tick, so it is not timed
with IRQs masked. 92 of 93 probe blocks are exact against the 5.00 VM with
6.26; the other is INT 21h AH=2Ch by 4 cycles a call, which varies that much
between runs, as mode sets do by a few hundred cycles in 2.7 million. With
4 MB, HIMEM.SYS and DOS=HIGH (`vmt386dos500h`, 9 Oct 2026; two runs identical) 89
are exact: INT 21h AH=0Bh costs about 561 cycles a call more and AH=2Ch 156 more,
the 13h-from-13h mode set 10,980 less and mode 3 from 13h 120 more. With
HIMEM.SYS loaded and DOS low, AH=0Bh is exact and AH=2Ch within its 4 cycles, so
the DOS calls' extra cost is DOS=HIGH's; the profile charges the DOS-low figures. Video BIOS calls are charged at the INT, with interrupts held;
DOS calls pass their time in the stub's LOOP with interrupts on, as FreeDOS
enables them inside INT 21h. Charged at the INT, the intro's 689,000 AH=0Bh
polls held the music driver's interrupts back and its scenes ran up to 140 ms
long; through the loop they hold 86Box's pace to a frame. The DAC block's
split between a fixed part and a part per colour is estimated (one size
measured). The earlier open mouse and file-service work is covered by the
measurements above, including EXEC and overlay loads.

## Corrected reference harness (10 October 2026)

The remaining 0.371-second held-picture discrepancy was measured against
the default 9 MIPS DOSBox-profile capture, not `--timing 386`. It is not
evidence of a 386 cycle-charge defect. `pc_parity.py` now captures the 386
profile separately for 86Box pictures, AdLib and scene timing; DOSBox checks
keep their own profile. `compare_timing86.py` requires recorded 386 settings,
matches held pictures by their 86Box pixel hash in order, and requires every
local held picture to match. Missing reference scanouts delimit screen-off
gaps; the final unclosed reference picture is censored by the capture stop.
Neither duration nor drift limits have been loosened (tightened 10 Oct 2026,
see below).

Fresh full references in `D:/f117-gate/pc-parity-profile-aware` pass: 85 exact
and one close 86Box picture, sound, both 802-byte roster saves, and all 21
local held pictures matched among 22 reference holds. Maximum duration
difference is 0.214022 s (limit 0.30, tightened from 0.35), start drift
1.112912 s (limit 1.5, tightened from 2.2) - both re-verified against this
same saved capture after tightening. The final stationary PLAYER hangar
lasts 6.472724 s locally and 6.491997 s
on 86Box: 19.273 ms apart. The old reference duration included 57 ms with
the screen off. Eight tests in `tests/test_reference_timing.py` guard profile
identity, content/order matching, pixel hashing, duplicate frames, screen-off
gaps, censored endpoints, missing scenes and timing limits.
