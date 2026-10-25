# Architecture

How the recompilation is built, and why it can claim 1:1 parity with the
original DOS game. Written for contributors: people extending the recompiler
or the runtime, or checking a parity claim. Current figures and open work are
in [roadmap.md](roadmap.md); building and running are in the
[README](../README.md); the current working state is in
[../handoff.md](../handoff.md).

## The idea in one paragraph

The original game is several small real-mode DOS programs (F117.COM, SETUP,
MPS_LOGO, PLAYER, DSWAP, START, VGAME, END) plus overlays and drivers
(MGRAPHIC, MISC, the `.117` and `.LOG` sound drivers). The recompiler
translates their machine code, instruction by instruction, into C. The
result runs on an emulated PC: memory, DOS, the BIOS and the devices the
game touches. Every translated instruction calls the same semantic helpers
as a reference interpreter that is validated against real silicon, stops for
interrupts at the same instruction boundaries, and is checked against that
interpreter on whole play sessions. Anything not translated, or whose bytes
no longer match what was translated, is interpreted. Correctness never
depends on how much was translated; only speed does.

## Components

```
src/cpu/        the CPU: state, the interpreter (cpu.c), the shared semantics (x86_sem.h),
                the 386DX/33 timing profile (timing386.c)
src/machine/    the PC: DOS and BIOS (dos.c dispatch and BIOS stubs; dos_memory.c,
                dos_programs.c, dos_files.c, dos_keyboard.c, dos_video.c),
                devices (pc.c), mouse driver (mouse.c)
src/recomp/     the recompiled code's run-time (recomp.c) and the generated-code contract
src/matched/    hand-written equivalents of original routines (matched.c) and the
                draw-list observer (observe.c)
src/fixes/      the switchable fixes (fixes.c)
src/present/    the optional presentation: draw lists, finer re-draw, in-between frames
src/host/       the window, input and presentation driver (main.c, present.c), the
                headless runner (headless.c), audio (audio.c, speaker.c), the MT-32
                backend (mt32.c), the scripting API (machine_api.c)
recompiler/     the translator: decoder, code discovery, C emitter (Python)
tools/          the gate (build_recomp.py), pilots and checks, reference-machine
                harnesses (ref86box/), routes (routes/)
tests/          silicon-vector harnesses, lockstep harnesses, ROM-free unit tests
third_party/    DOSBox DBOPL (GPL-2+) and Nuked OPL3 (LGPL-2.1)
```

The optional presentation (finer pictures, in-between frames) observes the
original's draw path and never changes the machine; its design is in
[presentation.md](presentation.md). Its `rerun` mode is the one part that runs
guest code: it re-runs the original's draw on throwaway copies of the machine.
The gate's routes do not exercise those copies, so `presentation_check.py
--mode rerun` checks them on every route: guest checkpoints and final hashes
against the plain run, the unnudged redraw, and real memory, machine state
and the fixes' host state before and after each extra draw. Memory alone was
not enough: an early copy advanced D1's frame limiter, which lives outside
guest memory, so fix state now travels with the snapshot. For the same reason
the light check for presentation-only changes (`tools/present_light_check.py`)
refuses changes to `drawfeed.c/.h` and `rerun.c/.h`; those need the full gate.

## The CPU and its two engines

`src/cpu/cpu.c` is the Reimp project's oracle interpreter (see
[provenance.md](provenance.md)), an 8086/80186/80286 real-mode core
validated against the SingleStepTests vectors. Its instruction semantics -
the ALU and its flags, shifts, multiply and divide, BCD, string steps,
stack-frame instructions, flag transfers - live in `src/cpu/x86_sem.h`, and
`cpu_step` is only the decoder that calls them.

The recompiled code calls the same functions. A translated `adc
[bx+si+12h], ax` is `alu_op(c, 2, seg_read16(c, ds, bx+si+0x12), ax, 1)` -
the call `cpu_step` makes for those bytes - so the two engines cannot differ
in an instruction's arithmetic or its flags. The machine is a 286, as in the
Reimp's oracle (VGAME and MPS_LOGO are compiled with 80186 instructions).

### What a translated instruction looks like

```c
L_1234: CHECK(0x1234);          /* event due? stop here; else note op_ip */
        { const uint16_t s_ = c->seg[S_DS]; const uint16_t o_ = (uint16_t)(c->r[R_BX] + 0x12);
          const uint16_t r_ = (uint16_t)alu_op(c, 0, seg_read16(c, s_, o_), c->r[R_AX], 1);
          seg_write16(c, s_, o_, (uint16_t)(r_)); }
        IC(); goto L_1238;      /* retire, continue */
```

It is the interpreter's loop unrolled: look at events before the
instruction, execute it, count it. Every instruction has a label, so
execution can resume anywhere - after an interrupt, after a far return,
after the interpreter ran something.

## Time

The machine's only clock is `cpu.icount`. Each instruction costs one, each
port access costs extra (below), and `ips` of them make an emulated second.
Every device reads time from it: the PIT's counters and interrupts, the VGA
retrace bit, the OPL's timers, the joystick one-shots, and the audio the
host renders. A run is therefore a pure function of the program, its files,
and the inputs with the clock counts at which they arrived.

- **Speed.** The headless reference default is 9,000,000 a second, GOG
  DOSBox's `cycles=9000` for this game; the app defaults to 60,000,000 with
  the fifteen-step simulation cadence (the owner's 18 October decision).
  `--ips` changes either. The game's behaviour depends on speed (bug D1);
  optional corrected combat and real-time pacing are documented in
  [speed-sweep.md](speed-sweep.md).
- **The 386DX/33 profile** (`--timing 386`, `F117R_TIMING=386`;
  `src/cpu/timing386.c`) makes the clock count CPU cycles at 33,333,333 a
  second instead: each instruction costs what 86Box's 386DX/33 charges for it
  (its cycles, prefetch refills, prefixes, REP chunks), VGA memory 32 cycles a
  byte, and ports 86Box's costs in place of DOSBox's delays
  ([timing386.md](../tools/ref86box/timing386.md)). `probe386.py` holds the
  interpreter to 86Box block by block; both engines charge these costs and
  instruction lockstep checks them. DOS file services and program loads are
  charged the reference drive's measured costs. Recorded routes run on the
  default DOSBox profile.
- **ISA I/O delay.** With `cycles = floor(ips/1000)`, a port read costs
  `floor(cycles/1024)` extra clocks and a write `floor(cycles/1365)`:
  eight and six at the 9 MIPS reference speed. DOSBox suppresses that delay when
  fewer than three delays remain in the CPU slice; the slice model, shared
  with DOS file-transfer costs, considers millisecond, PIT and VGA events.
  Without bus delay the logo's AdLib driver fails its card detection, which
  polls the status port 200 times expecting an 80-microsecond timer to
  expire.
- **Events.** Before every instruction both engines compare `icount` with
  `cpu.stop_at`. At or past it they return to `machine_run`, which raises
  due interrupts (PIT edges, keyboard bytes, input), delivers the
  highest-priority deliverable one through the 8259 model, and computes the
  next stop. Anything that could make an interrupt deliverable sooner - STI,
  POPF, IRET, EOI, an IMR write, a timer reload, reading port 60, a write to
  translated code - sets `stop_at` to 0 so the next boundary looks. So both
  engines take every interrupt at the same boundary.
- **Atomic native service clocks.** A natively answered BIOS/DOS interrupt
  includes the callback stub's clocks in the same atomic step as the INT.
  These remain in `icount`; `native_stub_time` distinguishes them in the
  matched-overrun ledger. A callback ending just past a slice is therefore
  allowed, while an extra guest instruction after it is still reported.
  `native_service_boundary` checks both cases without game data.
- **The interrupt shadow.** STI, MOV SS and POP SS hold off interrupts for
  one instruction, as the CPU does.
- **Inputs are machine events.** Keys, mouse and stick arrive with a clock
  count and are applied at that boundary, never "between slices", so a run
  does not depend on how the host slices it. This is what makes record and
  replay exact.
- **Waiting.** HLT, and a BIOS keyboard read with nothing typed, let time
  pass to the next event; the BIOS wait takes interrupts whatever the
  caller's IF, as the ROM routine does on its own stack frame.
- **The wall clock.** DOS and the BIOS report the local time at boot plus
  emulated time, and the BIOS tick count starts at the time of day, as a PC
  reads it from its real-time clock. The boot time is stored as local time
  counted as if it were UTC (what an RTC holds), so a recorded session
  replays with the same DOS clock in any time zone.
- **PIT control-word interrupts.** DOSBox 0.74 raises IRQ0 when a control
  word reaches PIT counter 0 while its output is low and takes it at the next
  STI, which every INT 21h stub begins with. START's teardown writes control
  word 36h, reload 0, then INT 21h AH=25h to restore INT 8, so under DOSBox
  the game's own timer handler runs first and reloads the PIT to about 70 Hz;
  the BIOS tick then runs about four times faster and START's four-tick
  palette loops take a quarter as long. The machine does the same
  (`F117R_PIT_CONTROL_IRQ=0` turns it off); it was the cause of an old
  delay entering the roster (the video drift against GOG's capture went from
  -570 ms to +14 ms when it was modelled). It is one of the places the
  machine follows DOSBox 0.74 rather than known real hardware; 86Box is the
  reference that can overrule it.

## The PC

`src/machine/dos*.c` began as the Reimp oracle's DOS: the loader, the
EXEC/overlay/terminate chain that F117.COM drives, the bump allocator and the
file calls. `src/machine/pc.c` holds the devices. Both were extended from
"run a scripted capture" to "play":

- A real 8259 (IRR, ISR, IMR, priorities, EOI forms) and all three 8253
  counters, with counter 2 gated by port 61 (the AdLib driver paces speech on
  it).
- A keyboard controller delivering set-1 bytes one at a time, and a BIOS
  INT 9 translation for a US layout with the shift, lock and E0 states.
- Blocking INT 16h and DOS console reads (an extended key reads as 0 and its
  scan code is held for the next read, as DOSBox's CON device does); INT 10h
  text services (SETUP draws with them); INT 1Ah and DOS date and time.
- The VGA: mode 13h, the DAC and the default BIOS palette, the retrace
  timing, text mode (below).
- AdLib timers and status (the synthesis is the host's), the MPU-401 in UART
  mode, the game port, the speaker.
- A save directory overlaid on the install: reads look there first, writes
  go there, and the install is never written.
- A software mouse cursor drawn into guest video memory, with clipped
  background save/restore on show, hide, reset, movement and shape changes.
  Frame presentation reads those pixels and adds no second cursor
  (`src/machine/mouse.c`).
- The service vectors (10h, 16h, 1Ah, 21h, 33h) point at stubs, so a program
  that hooks one and chains to the old vector reaches the service.

### The VGA, as GOG's DOSBox draws it

Mode 13h follows DOSBox's `svga_s3` path, which the GOG configuration selects:

- The frame is 449 lines at a 25.175 MHz pixel clock divided by 8 and 100
  horizontal clocks: a rational 70.086 Hz period (128,413.108 guest cycles at
  9 MHz). Frame starts, line parts, retrace and status reads use it, each
  event rounded up to the first whole guest instruction at or after its
  deadline, as DOSBox's PIC queue runs a fractional delay. `pc_slice_left()`
  budgets DOS I/O and file transfers to the next such deadline.
- Scanout is DOSBox's four-part draw: 50 logical rows at each of lines 100,
  200, 300 and 400. The display address is latched at retrace for the next
  frame and the 64K chain-4 address wraps. Presentation and screenshots use
  the last completed indexed frame and its palette.
- `VGA_DrawPart()` in the `svga_s3` path ignores the sequencer screen-off
  bit that other DOSBox paths honour; START toggles it about every 128,400
  clocks, so the presenter does too (otherwise those intervals render solid
  black).
- The DAC has separate readback and displayed tables: red and green writes
  affect readback at once, and blue publishes the triplet to the render
  palette; PEL-mask changes rebuild its aliases. The BIOS palette calls go
  through the port handlers, so their bus cost and output hash are included.

Scanout buffers, palette, address latch and event state are in the engine
parity hashes. `tests/test_scanout.c`, `test_video_ports.c` and `test_mouse.c`
cover them.

### Known differences from a real PC

- **The BIOS** is a set of stubs and host routines, not a ROM image. Its
  interrupt handlers execute a handful of instructions where a real BIOS
  executes dozens, a small timing difference.
- **DOS memory layout** follows the Reimp's oracle with DOSBox's PSP (0191h)
  and memory chain (programs at 1566 or 18E1 depending on the sound driver),
  not any particular DOS version's.
- **The CPU** is a 286 where DOSBox emulates a 386; the game has no CPU
  detection and the one observable difference is flag bits 12-14 after PUSHF,
  which the game's PUSHF sites never read.
- **The OPL** defaults to GOG DOSBox's DBOPL core at 44,100 Hz with its 2x
  mixer gain; `--opl nuked` selects Nuked OPL3 in OPL2 mode. Timestamped writes
  render preceding samples before changing the chip. The register writes match
  DOSBox's in order, value and timing (below); sample-for-sample agreement with
  DOSBox's mixer is not checked.

## The recompiler

`recompiler/recomp.py` turns the user's install into C.

1. **Modules** (`modules.py`). Each code file becomes the image the CPU will
   execute, before relocation: the unpacked load module of an LZEXE program
   (recovered by running the program's own decompressor on the validated
   core and loading it at two segments to find the relocations,
   `tools/unpack.py`), the load module of a plain MZ overlay, or a .COM. The
   file's hash identifies it at run time.
2. **Discovery** (`discover.py`). Regions are closures under near control
   flow from seeds: declared entries (program entry, the MicroProse overlay
   descriptor's entry table), call and far-branch targets, Microsoft C
   switch tables, near pointer tables (the sound drivers' command handlers,
   the C library's dispatch), far code pointers in relocated data, MSC
   prologues, and coverage - instructions the interpreter executed in
   recorded runs. The data group is found from the C startup's relocated
   `mov di, DGROUP`. Last, every gap in the code area that decodes cleanly
   to a return or jump is seeded, until none is left: that finds interrupt
   handlers a driver installs by computed address. Data that happens to
   decode costs size, never behaviour, since a region only runs from an
   instruction start whose bytes are verified. Every instruction belongs to
   exactly one region; flow into another region's instruction leaves through
   the dispatcher. `tools/census.py` measures the result: about 96% of
   code-area bytes are translated and no remaining gap decodes like code.
   Regions are capped at 1,024 instructions to bound the C compiler's
   optimisation work.
3. **Emission** (`emit.py`). One C function per region, mirroring
   `cpu_step` case for case: operand evaluation order, divide faults
   (the 286 pushes the faulting IP), REP iterations as separate instruction
   boundaries, INT hooks that may wait or switch programs, the TF trap after
   POPF and IRET.

Discovery and emission run in separate processes per module (up to eighteen;
`recomp.py --jobs 1` keeps the serial path). Each worker writes only its own
module's files; the parent writes the module list in the original load order
after every worker succeeds. Serial and parallel output are checked byte for
byte. Coverage validation builds each module's relocation mask once and still
checks every captured instruction's hash, bounds and unrelocated bytes.

**Operands the loader patches are read at run time.** Relocated words (the
segment of a far pointer, `mov ax, seg DGROUP`) and the operands of far
jumps and calls are read from memory when the instruction runs, exactly as
the CPU fetches them. One translation therefore serves every load address
(the programs load at different segments depending on the sound driver), and
the interrupt chains the game patches into its own code just work.

The output - C derived from the original machine code, plus the original
image bytes the run-time verifies against - goes to a work directory outside
the repository and is never committed. Coverage files are append-only: a
build records only what it had to interpret, so dropping an old file would
lose code an earlier capture got translated. For the same reason the gate's
coverage files are not a reach inventory: they hold only interpreted
fallback instructions, not everything that ran.

## The run-time

`src/recomp/recomp.c`.

- **Instances.** Every program and overlay DOS loads is announced before its
  image is written; the run-time matches it to a module by file hash and
  registers it at its load segment. A module it has no translation for is
  registered anyway, so coverage can name it. An instance stays live until a
  later load overlaps its range, even if its memory is reused meanwhile (a
  buffer, a later program's data).
- **Dispatch.** CS:IP inside an instance, an entry for that image offset, a
  region that expects this CS, and a region whose bytes are verified: then
  the region runs. Otherwise the interpreter runs one instruction.
- **Verification.** A region's bytes (minus the run-time-read operands) are
  compared with memory before it first runs; the verdict holds until a
  write changes one of them. A bitmap of translated bytes makes every write
  check one bit; a write that changes a translated byte bumps the instance's
  generation and makes running code stop at the next boundary.
- **Code overrides.** Hand-written C registered for one address of one
  module (by name and file hash). Fixes start off in the core and headless
  runner; the application enables all eighteen for normal play (`--fixes
  recommended`; `--fixes original` turns them all off). `--fix none`
  clears the set and `--fix ID` adds a selection. Ordinary parity routes
  explicitly start with none; fix-specific routes enable their tested fixes.
  Replays restore their recorded set (no header means none). Matched routines
  are overrides always on for the recompiled engine, and so are observer
  hooks. A switched-on fix takes the place of the
  matched routine (and any observer) at its address, whatever the order they
  were registered in, because the matched routine reproduces the original and
  the fix is the deviation that was asked for; a matched routine with a fix
  site inside it (after its entry) declines while that fix is on
  (`fix_inside` in `matched.c`). At one address only the first override runs,
  so two parity routines or observers there leave one silently idle: the unit
  test `code_overrides` lists such pairs and fails on any not named as
  deliberate (an observer a matched routine calls itself, such as
  `observe_game_draw`). The run loop asks for an override before either
  engine takes an instruction; with none enabled that is one counter test. A
  translated region whose bytes include an enabled override's address is
  refused, so the recompiler gives each override address a region of its own
  (one instruction) and the function around it stays translated. An override
  leaves CS:IP and the clock where the replaced code would have, or declines
  and the original instruction runs. Verification protects regions from
  reused memory; overrides have their own check: an override runs only while
  the byte at its entry equals the module's expected byte there, an opcode,
  which relocations and run-time patches never change (they change operands).
  Otherwise the original instruction, whatever is there now, runs. The
  expected byte comes from the module's unpacked image where the translator
  holds one (`mod->image`, the same image `verify()` compares regions
  against), because a packed program's file bytes are not what runs: START and
  the other LZEXE programs unpack in place, and a check against the file would
  decline every override in them, including the QUIZ fix (found by the
  `quiz_right` and `quiz_wrong` routes). A module with no translation falls
  back to a copy of the loaded file's bytes (the MZ image after its header, a
  .COM as is), taken from the file because the module is announced before its
  bytes are written. Without this check matched routines kept firing on foreign
  bytes once a module's memory was reused (found at ISOUND.LOG `060D`, run
  at 169B:060D). `stale_override` (`tests/test_stale_override.c`) tests it,
  and `code_overrides` writes module images into memory as the loader does.
  Each loaded module can place all 4,096
  registry entries; matched routines and observer hooks share that capacity,
  and the registry warns on stderr if it fills (a dropped entry is a lost
  routine). Placements are sorted by physical address when modules load or
  fixes change; dispatch uses a binary address lookup and checks the exact CS
  within that address's entries, keeping registration order for ties and the
  precedence of enabled fixes, so no nested draw call scans every module
  entry.
- **Data fixes.** The machine's one hook that may change what the guest
  sees, `file_data`, hands each DOS read's bytes to the fixes before they
  reach memory; a correction applies only to its named file, of its size,
  where the shipped byte is. With no fix on, nothing changes.
- **Staging.** A fix for a state normal play takes hours to reach (a full
  destroyed-object table) is checked by staging that state: the Python
  machine API's `stage_write16` is its only guest write. Pilots and parity
  observers never call it.

Data inside code regions costs speed, not correctness: F117.COM keeps
variables after code that the walk reaches, VGAME patches its far-call thunk
table in DGROUP at start, and MGRAPHIC uses part of MISC's image as a
buffer. Those regions verify, get invalidated by the writes, and fall back
to the interpreter.

## Verification: why "1:1" is a claim with evidence

Eight layers, each checkable with your own copy of the game. Current totals
are in [roadmap.md](roadmap.md).

1. **The interpreter against silicon.** `tests/sstest.py` (8088, 3,007,000
   vectors, 0 failures) and `tests/sst286.py` (80286 real mode, 1,429,998
   vectors, 0 unexplained; the explained buckets are behaviours the game
   never exercises, each named).
2. **The translator against silicon.** `tests/sst_recomp.py` emits each
   vector's instruction exactly as game code is translated, compiles it, and
   runs the vector through the generated function with the same comparisons
   (8088 90,900 of 90,900; 286 94,200 with 0 unexplained at 300 per file).
3. **The two engines on whole sessions.** `f117run` runs the same inputs
   under `--engine interp` and `--engine recomp` and hashes all of memory,
   the registers and everything the machine sent out (every port write with
   its value and clock, every file write) every 50 million clocks. Every
   route in `tools/routes/` (63, run as 62 sessions) must be identical at
   every checkpoint; `tools/build_recomp.py` (the gate) repeats this on every
   change. Routes also declare milestones - `# expect-world`, `# expect-exit`,
   `# expect-open`, `# expect-save`, `# seed-roster` - so two engines that
   crash identically, or stall at a briefing, still fail. Every replay uses a
   fresh save directory. Milestones alone do not prove a takeoff or landing:
   the strong observers below do. `tools/random_flights.py` adds seeded
   random pilots (stick, throttle, weapons, views, systems keys) under both
   engines, hashed every 50 million instructions.
4. **Every translated instruction, routes or not.** The routes run about
   half the code area (`tools/exercised.py`). `tests/insn_lockstep.c` places
   each module's image in memory, puts the machine in random states
   (registers, flags, segments on and off the module, every byte of memory
   outside the image) and runs one instruction through the machine's
   interpreter step and through the generated region entered there,
   comparing registers, segments, IP, flags, the clock, the interrupt shadow,
   every byte written, every port access and every interrupt raised. 89,433
   instruction starts at 64 states each: 5,723,136 comparisons per timing
   profile, 0 mismatches (576 states are declined to the interpreter: bytes
   the gap sweep took for code that are invalid 286 opcodes). The reference
   is the machine's own step (`cpu_step` plus the TF trap), not bare
   `cpu_step`. The gate repeats this under the 386 profile, comparing the
   prefetch queue and held REP chunk as well; `insn_lockstep --base-seg`
   places the code in VGA memory for a separate run outside the gate.
   States exercise cached and uncached memory, partly filled queues and
   unfinished chunks. A diagnostic instruction budget stops after one
   operation even when its clock cost is zero.
5. **Replay.** Input logs recorded on one engine replay on the other, and in
   the windowed game, to the same clock count and final state.
6. **The check can see a defect.** `tools/mutation_check.py` plants one wrong
   instruction at a time in the generated code and requires the comparison to
   notice. With the single-instruction lockstep as the judge, 144 planted
   defects (removals, flipped CF, ZF and AX bits) in START, VGAME, END, DSWAP,
   ISOUND.LOG and other modules were all detected, most at instructions no
   route runs. It also caught two weaknesses in this project's own tooling,
   both fixed: removing a branch had been a no-op, and the state hash had not
   covered what was sent to the sound card.
7. **The machine against GOG's DOSBox.** Layers 3-6 compare two engines on
   one model of a PC, so a difference between that model and the real machine
   would pass them all. `tools/fidelity.py` assembles a probe (nothing from
   the game in it), runs it under GOG's DOSBox 0.74-2 with the install's own
   `dosboxF117A.conf` and under `f117run`, and compares what the game asks:
   the memory chain, PSP and environment, the registers it starts with, every
   DOS and BIOS service in the inventory with every register and flag after
   the call, the BIOS data area, every VGA register after a mode set, the
   PIC, keyboard, mouse and joystick, the AdLib and MPU-401 detection, and
   clocks measured against the PIT. All 1,210 comparable answers agree (a
   4 KB file read's cost within 5%, the clock rates within 2%). The 31
   differences it found were each traced to DOSBox's source (shipped in the
   install) and fixed: the PSP at 0191h, a real memory-block chain, DOSBox's
   environment and entry registers, NumLock off, a game port in the equipment
   word, an unconnected stick reading FFh, the volume label `C_DRIVE` (START
   and SETUP compare it with "F117A-SF"), and a port read costing 8 cycles.
   `--reference dosbox-x` runs the same probe against DOSBox-X, for where
   DOSBox 0.74 itself is questionable. What stays outside it: the
   instruction timing of DOSBox's own BIOS and DOS code, its event
   scheduler, the 386, and its per-millisecond slicing.
8. **The model against other PCs.** `tools/pc_parity.py` plays the intro on
   GOG's DOSBox (a saved lossless capture), on DOSBox-X (a patched source
   build that starts its own capture and runs with no window) and on 86Box
   (a source build with its VNC renderer, windowless and silent), and checks
   the pictures against limits measured on 6 October 2026. The same run
   judges the intro's music (GOG's and DOSBox-X's captured audio against the
   application's own render of this machine's AdLib log, `tools/sound_parity.py`;
   86Box's AdLib writes and scene timing in emulated time on a 386DX/33,
   `tools/ref86box/compare_opl86.py`, `compare_timing86.py`) and
   a scripted START session's saved `ROSTER.FIL` byte for byte on DOSBox-X and
   86Box (`tools/save_parity.py`). The speaker is held to DOSBox-X apart
   from it (`tools/speaker_parity.py`: the speaker driver's port writes and
   intro audio, the radio call's counts).

   The machine-behaviour probe (`tools/fidelity_all.py`, about 1,200
   answers) is held to stored baselines of known differences in
   `tools/fidelity_baseline.json`: none for GOG's DOSBox (held to every
   answer, DOS memory layout included, since this machine follows it), 40
   for DOSBox-X and 73 for 86Box; a new difference fails. For the other two
   the answers that describe how a DOS and BIOS lay memory out (segment
   values, PSP, memory chain, the EXEC'd child's frame) are left out, so the
   lists hold behaviour: the DOS version, BDA words, a few VGA/DAC registers,
   the PIC mask, the MPU port, a few mouse-driver answers, speed-dependent
   counts.

   Which of those the game can see: `tools/port_reads.py` replays every
   route with the machine's port inventory and lists the ports its own code
   reads. It reads 0x21 (55 times), the PIT, 0x60/0x61, the Sound Blaster
   probe ports 0x226-0x22E and 0xB8B (unanswered, as on every reference),
   0x388, the VGA retrace status 0x3DA, DAC data 0x3C9, MPU 0x330/0x331 (one
   route) and the sequencer data port 0x3C5 - only register 1 (59,605
   reads). It never reads 0x3C2, 0x3C4, 0x3C7, 0x3C8 or 0x3CE/0x3CF, so the
   differences there (input status 0, sequencer registers 0 and 4, DAC index
   readback, graphics-controller register 7) cannot reach it, and register 1
   agrees. The one difference on a port it reads is the PIC mask at start
   (F8 here as under GOG's DOSBox, B8 on 86Box and a real AT BIOS, which
   leaves the floppy line unmasked). Starting the model at B8, routes end on
   the same clock count and behave the same; only the hash differs, because
   the game writes back a value derived from the one it read. It is kept at
   F8, so no recorded hash moves. Builds and notes: `tools/ref86box/`.

   Pictures are compared exactly as RGB, collapsing identical consecutive
   pictures and aligning them in order; unmatched pictures are reported,
   never hidden by a tolerance. Results:
   - GOG DOSBox: 1,329 exact pictures in order; three one-sample logo
     transition images unmatched on each side; drift 57 ms at worst, none at
     the end. A second independent capture matches 1,321 with 11 one-sample
     differences per side: the remaining differences vary from capture to
     capture, so they are not a model defect. `tools/video_compare.py`
     compares completed four-part scanout frames at the native VGA period.
     Roster cursor changes it once appeared to show were screenshot
     aliasing.
   - DOSBox-X: 1,237 exact pictures; timing drift up to about 0.2 s over the
     intro. DOSBox-X draws scanline by scanline, so a palette write can land
     between the two scanlines of a doubled line (its 640x400 capture of mode
     13h); the tool reads the first scanline and reports how many frames
     differ.
   - 86Box (the 386DX/33 profile, MS-DOS 5.00 with Microsoft MOUSE.COM
     6.26): 85 of 86 graphics pictures exact in 6-bit DAC values, in order,
     the other a single sample of START's roster screen taken mid-draw; the
     check rejects any difference other than the documented samples. The 8-bit captures differ
     by up to 1 level because the two expand the DAC differently (86Box
     floor(v*255/63), ours v<<2|v>>4), so `compare_intro.py` compares v>>2.
     Timing differs by design: 86Box models a 386DX/33's instruction
     timings, VGA bus and disk, this machine DOSBox's.

   Other comparisons: `tools/dosbox_compare.py` captures the AdLib register
   writes in DOSBox and here over the logo and intro: all 22,840 over 106.0 s
   identical in order and value, timing -14 to +1 ms, with this machine
   started 275 ms in as GOG's DOSBox is when F117 starts (`--boot-ms`).
   Started at 0, the sound driver's per-frame generator is one step off from
   the note at 29.7 s on (the same generator step in all three GOG captures)
   and 86% of the writes match; the match holds for starts of 5-20, 120-230
   and 260-400 ms, so it is a start phase, not a clock error.
   `tools/fade_calibration.py` runs START's, PLAYER's and END's fade
   calibrators on both machines (START and END return the same range;
   PLAYER's DAC throughput is within a few bytes of DOSBox's after matching
   its I/O-delay suppression at slice boundaries).

   Not pursued (out of scope, [roadmap.md](roadmap.md)): exact frame phase
   against DOSBox (the intro's partial-logo transitions depend on capture
   timing) and sample-for-sample PCM agreement.

## Observing flights through normal controls

With `F117R_BUILD_TESTS=ON`, `f117machine_api` exposes a scalar host API and
`tools/machine_api.py` wraps it: boot, advance to an absolute clock, read guest
memory, queue normal keys and mouse input, record input, capture a picture. It
has no register setters, and its one memory write, `stage_write16`, exists
only for staging fix checks ([the run-time](#the-run-time)); pilots and
observers never call it. Only one machine may be live per process.

The pilots fly with released key pulses and record an input log of keys and
mouse events only; the gate replays the log without the controller. Each
route has a strong observer that checks what the milestones cannot:

| Route(s) | Observer | What it requires |
|---|---|---|
| landing | `landing_pilot.py` | ground contact inside the home approach box, stop at idle, gear and brakes, the original countdown, parent flight block result 0 / status 3 |
| recon, recon_return, recon_career | `recon_pilot.py`, `career_check.py` | exposure count and photo-credit events, intact targets, camera retained, both photos for the return, the saved ribbon bytes |
| strike, strike_return, strike_training, strike_training_d8 | `strike_pilot.py` | weapon release, matching hit, credit, landing at home |
| cargo, cargo_d5_fixed, cargo_return | `cargo_check.py` | the released crate's ground impact and delivery area; no credit without `--fix D5`, credit with it |
| cargo_pilot | `cargo_check.py` | the same delivery from a closed-loop pilot (`tools/cargo_pilot.py`): the crate released by the aircraft's own state |
| secret_airstrip(_return) | `airstrip_check.py` | strip approach box, 8Bh event, store consumption, return leg |
| airair_type5-8 | `airair_pilot.py` | kill of the special aircraft, primary event and credit, consumed stores, landing |
| career_serge, career_promotion | `career_check.py` | positive saved score, sortie increment, all 802 roster bytes, promotion and medal |
| (rank 6) | `career_rank6.py` | a staged rank-6 roster reaches END's rank-6 retirement remark |
| d1_fast_machine | `d1_check.py` | S, mission clock and frame rate at 40 MIPS equal GOG-speed values |

**DOS exit 129 is the debriefing handoff**, distinct from the mission result;
it also occurs after failed approaches, and after the original's
render-detected terrain collision (parent result 2), so a route that "reaches
END" has not necessarily completed anything. A career is chained from actual
saved rosters, never edited ones: `career_promotion` replays its
`# seed-roster` prerequisite (`career_serge`) in a fresh process and carries
only the resulting roster forward. The rank-6 case stages one cause (rank
and total) and says so.

Per-route recordings, their parameters and the observer commands are in
[../tools/routes/README.md](../tools/routes/README.md). A recording is tied to
the timing model it was made under: a change to VGA, PIT or slice timing
invalidates them all and they are re-recorded (the pilots adapt, so this is
mechanical).

## Matched routines (Phase 2)

A matched routine is hand-written C in `src/matched/matched.c` that does what
one of the original's routines does, written to be read, and is equal to it:
every register, flag, memory byte and the instruction clock, on every path.
It is a code override with `matched` set: always on, placed only for the
recompiled engine, never for the interpreter, so every route that compares
the two engines compares the matched C with the original instructions.
`F117R_NO_MATCHED=1` leaves them all out; `F117R_MATCHED_LIMIT=N` turns on only
the first N (bisecting a divergence to one routine). Under the 386 timing
profile, matched entries use the original translated body until their
handwritten clocks charge cycles.

A routine counts as matched only with both kinds of proof: the lockstep
harness below at several seeds, and a real route that runs it (positive
count, every checkpoint and the final hash identical). Fixture proof alone
is not enough.

Registration must use the original code segment and instruction offset,
not just an equivalent linear image address: dispatch checks CS as well.
VGAME's clipping outcode, for example, is called at `0FB2:04BC`; registering
it at `0000:FFDC` passes isolated lockstep at those bytes but misses the
actual calls. When vetting census leads, compare both the bank/offset in
`our_region` and the linear image address against the existing table. An
alias can identify an existing implementation whose registration needs
investigation, rather than a new routine to rewrite.

A routine's arithmetic uses the interpreter's own semantics, so flags cannot
drift. A routine runs whole, so it declines (and the original instructions
run) when it would cross the run loop's next look at events; an interrupt due
inside it still lands where it would have. A routine that pushes before it
reads must count its clocks through what it is about to push, and REP string
instructions are stepped as the interpreter steps them.

**Routines that call original code.** `guest_call` pushes the original return
address, points CS:IP at the callee and runs the machine until a return trap
fires (`trap_cs:trap_ip` with SP back at the caller's level). The callee may
be translated, interpreted or matched itself. The interpreter checks the trap
after every instruction; `recomp_run` polls it at every region dispatch and
reports at once. That poll is load-bearing: without it a recomp batch runs to
`stop_at` past the return point and re-enters matched routines, a C nesting
level per call, and a long single run over dense drawing overflowed the C
stack. `tests/test_run_slicing.py` pins the underlying promise: one long
`run_until` reaches the same hash as fine slicing. The guest state at every
call must be the original's, stack frame included. If the nested run reaches
the outer limit before the callee returns, or the code after a call would not
fit before the next event, the routine sets IP to the original instruction
after the call and returns, and the original code finishes the routine.
`guest_call_far` does the same for a far `CALL ptr16:16` (the target is read
from the loaded instruction, so relocation is already in it).

Relocated immediate words must also come from the loaded instruction, even
when the unpacked executable shows zero. For example, each Ctrl+Break
installer loads its handler segment from a relocated MOV AX operand. A
constant zero passes the segment-zero harness but diverges in normal flight.
The retained installer fixtures plant a nonzero segment on both sides. Keep
`CODE16` operands from a private draft as loaded-word reads when rewriting
its logic.

**The harness** (`tests/func_lockstep.c`, needs the generated code): for each
routine it places the module image in memory, applies its loader relocations,
sets random registers, flags and
memory and a return address outside the routine, runs the interpreter until the
routine's own RET reaches it and the matched C once, and compares registers,
segments, IP, flags, clock, ports, interrupts and all memory. Details that
caught real defects:

- The seven ordinary programs retain their historical zero load base. The
  launcher, graphics and miscellaneous overlays, and six sound drivers use
  0800h; `--load-base` provides a zero control and alternatives from 0400h to
  0800h, all below the graphics fixtures' RAM buffers. Generated test metadata
  identifies each image by name and file hash and records image-relative
  relocation words; the loader validates bounds and overlapping sites before
  copying or patching memory. Both image reload paths apply the fixups, and
  named coverage compares against the relocated image. The production module
  descriptor is unchanged.
- Half the states put small words (-1, 0, 1, 0-31) where the arguments sit,
  and the other half use memory of mostly 00, FF and 01 bytes, so edge cases
  and exact tests on memory are reached.
- Stack writes matter even for a PUSH/POP pair that only transfers a segment
  register. START's shadow-text source can alias the temporary PUSH SS slot
  through the 20-bit wrap; replacing PUSH SS / POP ES with an assignment
  changed the record it copied. The seed `0xf2a7d2c1bf36` catches this;
  `matched_shadow_stack_alias` keeps that regression in the unit-test step.
- Uninitialised graphics-driver thunks are replaced with RETF on both sides
  of the check, so drawing wrappers can return and their surrounding work is
  compared: START's and END's, and VGAME's when checking the marker at
  `0xB171`, canopy at `0xD6DD`, projectiles at `0x4777`, sound gate at
  `0xD3F9`, scene finish at `0xD87C`, destroy at `0x72D8`, right-aligned
  number at `0x8A18`, HUD line at `0x881F`, cockpit initialiser at `0x796E`
  and display fill at `114A:0280`. The marker checks reach 4,857
  comparisons per 4,000-state fixed-seed run (358 without the stubs),
  including partial-run checks; 17 states are skipped. The routes still
  check the real driver. A routine that reaches such a thunk through a
  callee needs an entry too: `vgame_ai_destroy` (`0x72D8`) calls into the
  sound gate's far-called driver thunk (`0xD414`), and without it 0 of
  4,000 states were comparable; `vgame_ai_activate` (`0x66F8`) calls only
  already-matched, driver-free callees and needs none. END's DAC-loader
  states clear the BIOS gray-scale flag that overlaps the module image at
  segment 0; some START map-caption states plant the exact argument kinds
  64h and 65h. `--only MODULE:IP,...` selects routines for diagnosis (IP
  without leading zeros); the full gate checks the whole table.
- A state counts only when the original returns to the pushed address with the
  stack at its entry level (a looser rule let a routine that clears its own
  stack slide to the return address through `00 00`). States where the original
  writes over its own code are skipped. The RET must lie in the routine's own
  code, the 0x300 bytes from its entry unless `SPANS` names its range: a long
  routine entered at its first piece (START's formatter, END's scorer), or
  one that ends in a shared tail placed before it (the C runtime's DOS returns).
- DOS: for the routines named in `DOS_STUBS` the harness answers INT 21h itself
  (AX, sometimes DX, from the side's record so far; AL 0 and CF set one time in
  four each), so the code after a DOS call is compared on every state;
  `g_dos_script` scripts exact answers where a routine needs them (`.put` for
  the byte a one-byte read brings). Elsewhere INT 21h goes through a vector
  that the image's bytes make up and seldom comes back. The machine's DOS is
  held by the routes.
- `CODE_BELOW` gives the same kind of range for second entries into the model
  fills that jump back to a shared exit below them: the code starts that many
  bytes under the entry. VGAME `1377:00F3` also needs it: its 640-wide row-table
  loop starts at `00C2` and returns at `00F2`, below the entry. With the range
  about 3,990 of 4,000 states per seed are compared (341 without it, which
  missed a row table overwriting the loop displacement); a one-step defect in
  the branch fails on its first state. For those the original's code is also
  watched after every step, since a row table over the code (DS = CS) can
  change an instruction, run it and write the bytes back. The model command
  stream at `120A:0CE9` also owns the condition tail and RET at `0CC5-0CE8`;
  its instruction-coverage origin includes that earlier code.
- OVERRUN: given one instruction less room than the original takes, a routine
  must decline. Mid-run stop: stopped at a random clock inside its path, the
  state it leaves must equal the original's at that clock. It may never start a
  call into original code with the clock already at the limit.
- Calls that cannot be followed from random states (a BIOS call; a far call
  through the graphics driver's slot at 1E42:0188, which holds `JMP FAR 0:0` in
  the file) make a routine "not testable here (routes only)": its evidence is
  the routes, not the lockstep. Step 8 of the gate (`routes_only_unrun` in
  `tools/build_recomp.py`) fails when such a routine ran on no recompiled route
  (each route's output lists `[matched] ... ran N times`): with no route
  executing it there is no evidence at all, and the routine is dropped (as
  VGAME 0xF024, the run-time termination messages, was).
- Counter attribution survives a nested program exit or load: the dispatcher
  keeps the registered override index before calling its body. A nested DOS
  call can clear or reuse the module instance before the body returns; reading
  its placement index afterwards lost DSWAP `0010`'s call and could attribute
  an event overrun to a different routine. `code_overrides` replaces its own
  module inside a callback and checks both reports; the old runtime fails
  both assertions. This changes reporting, not guest execution.
- A fuzzed write can change a callee's RET into RET n: VGAME `683E` does
  this to the sine helper at `C830` at seed `0x3ad6ecf4e53b`. An earlier
  iteration returned at the normal stack depth, so the old harness falsely
  reported the later changed return as an escape. The harness now records
  changed returns from the original. Only the first return to the requested
  IP can qualify, and its clock, source address, registers, segments and
  flags must exactly match. The resulting state is still compared; original
  completion earns no named-region coverage. Ordinary missed traps still
  fail (including a deliberately wrong stack-depth trap on this seed).
- Room on the routes: the dispatcher (`recomp_override_step`) checks every
  matched routine it runs. One whose instructions carry the clock past an
  event limit nothing moved claimed too little room, and the run's output
  ends with `[matched] OVERRUN ...`; time charged beyond instructions (a
  port's bus delay, a DOS transfer, kept in `cpu.charged`) may cross a limit
  as it does in the original and is left out. Step 9 of the gate fails on any
  such line. It is the only room check a routes-only routine meets (it names
  the picture decoder's LZW step, which walked a prefix chain of any depth
  after one room check, when that bug is put back). A wait inside a matched
  routine must clamp `m->run_until` to `c->stop_at`, or a nested keyboard or
  timer wait runs to the host slice and step 9 fails.
- Port accesses: the bus delay (`io_delay`) can carry the clock across the
  limit inside one IN or OUT, and the original then stops right after that
  instruction. The machine notes the clock of such a crossing
  (`cpu.io_cross`) and the dispatcher reports a matched routine that went
  on past it. A stretch with k port accesses therefore claims
  `IO_SLACK(k)` (k times the read delay, ips/1000/1024) beside its
  instructions, so no delay can cross inside a matched routine; when that
  room is not there the routine parks and the original runs the stretch.
  The clock must be charged between port accesses, not in one lump after
  them: a stale clock at a PIT or CRT-status read changes what the routes
  read, and the lockstep records the clock of every port access
  (`matched_io_rules` checks that it sees a lump). An interrupt handler that
  opens with STI cannot be matched past that instruction (the run loop admits one more instruction
  after STI), so the timer handlers stay original code. A port write that
  makes the run loop look at interrupts (`io_write8`: an EOI or mask to the
  PIC, a counter 0 control word or reload to the PIT) lowers the limit to 0
  as STI does: the routine stops at the boundary right after it and the
  original finishes the routine. The lockstep requires each such write to
  lie within the run's limit and the body to stop exactly there (`WAKE
  OVERRUN` otherwise; `matched_wake_stops` keeps the eight timer routines
  that once ran on past their control word), and counts a body stopped
  after its own such write as completing its named region, as it does one
  stopped after its own POPF or STI (`POPF_STOPS`).
- DOS and BIOS calls: a matched routine runs its INT instruction as
  original code (`sm4_int`: the machine at the INT, a return trap after
  it), so the DOS services, their stubs and time and any interrupt taken
  inside are the original's, and its own instructions before and after
  are the C; it checks room after the call, since a DOS transfer charges
  time. A call that ends the program (DOS 4Ch) is left to the
  original: the routine stops at the INT. `PROGRAM_ENDS` marks SETUP
  `0A9F`'s error exit at `0BA0`, MPS_LOGO `00DA:0352`'s at `043B`, and
  START `9094`'s at `910F` (AH = 4Ch) as completing the named region;
  the original INT still runs and the resulting state is compared.
  `FATAL_JUMPS` likewise marks only the fatal-hook jump targets of START
  `8FC6`, END `4EBC` and MPS_LOGO `0146:010E` as named-region completion;
  the original fatal-exit code still runs and its resulting state is compared.
  In the harness INT 21h and 16h answer as port reads do (AX from the call
  count, a random carry and zero flag); through the vector table, which is
  the image's first bytes there, a DOS call used to lose the state.
  Interrupt timestamps have their own full-record hash, so checking their
  order does not change the synthetic DOS answers derived from the existing
  record. A contradictory event already observed before an unfinished
  continuation is a mismatch; an unfinished run with no contradictory event
  keeps its skip (a late INT whose final instruction count is unchanged, a
  late 65th INT beyond the diagnostic log).
- Direct returns past the caller: a callee that pops extra argument words
  and returns straight to the routine's caller (as in END `53DA` and SETUP
  `1600`) is
  recognised only when the completed original's clock, registers, segments,
  flags, interrupt shadow, depth and halt state agree exactly; all memory and
  I/O are still compared and no named-region completion is granted. Without
  this a correct return ran into synthetic caller bytes merely because its
  stack differed from the usual RET level. `matched_io_rules` holds 19
  synthetic cases for the port-clock, wake, interrupt-record and
  direct-return rules, including wrong clocks, registers, flags, memory and
  I/O.
- Shared code: the C runtime's DOS calls end with a JMP to endings the
  routines share, and itoa and ltoa end in one conversion, outside the
  routine's 0x300 bytes. `SHARED_ENDINGS` in the harness names those
  ranges: a RET there at the caller's level is the routine's own, and a
  state that writes into them is skipped as a write into the routine's
  code is. The near stackavail returns by JMP CX with its address already
  popped, and that counts as its return. A call into original code can end
  the routine itself (_write's flush leaves through _write's ending when a
  write fails): its run stops at the routine's return, as the original
  side's does.

`F117R_SHADOW=FROM:TO` re-runs matched routines against the original on the
live game in a window (a diagnostic, not a gate: device state is not in the
snapshot). `f117run --dump LINEAR:LENGTH` prints registers and memory at the
end of a run for comparing two engines at a chosen clock.

Operand reads keep their original order across calls. START's `rnd(n)`
reads its stacked bound after the generator updates its seed: DS and SS can
alias, so an early C local can change the divisor. The generic regression
`matched_start_rnd_stack_alias` preserves the seed that exposed this case.

1,383 addresses are matched across the game's 17 code files (the census
totals are in [roadmap.md](roadmap.md)). The programs carry byte-identical copies of the C
runtime helpers (string and block copies, the 32-bit shifts, multiply and
divide), so one matched routine serves several addresses. MPS_LOGO carries
the same library built for a larger model: its calls are far (9A), or PUSH
CS / CALL to a far routine, where the others' are near, and some of its
routines return far with the arguments a word higher. The shared routines
read each call's kind from the code (`sm3_call`) and take a far flag.
Candidates come from `tools/reimp_names.py`; the list is the table at the
end of `matched.c`.

## Audio

The OPL is synthesised on the host (above). The Roland path uses Munt
(libmt32emu), isolated in `src/host/mt32.c` and enabled with
`F117R_WITH_MT32EMU` (on by default; Munt fetched at a pinned commit). It
links the public C API, identifies the supplied control/PCM pair and fails
startup on invalid ROMs. Each MPU byte first advances audio to its machine
clock; complete messages enter Munt's MIDI queue, rendered in bounded blocks
at 44,100 Hz and mixed with OPL and speaker before clipping. A rejected queue
entry or oversized SysEx fails the session rather than dropping traffic.

**Offline rendering.** `f117run --midi-log FILE` records each MPU byte with
its instruction clock. `f117run --speaker-log FILE` records each speaker hook
event with the guest clock, port 61h low bits, PIT2 reload and mode, PIT2
epoch in PIT clocks and PIT2's null-count flag (1 after a control word).
`audio_render` merges OPL, speaker and optional MIDI events by guest clock.
For example, a complete OPL/speaker replay through a known end clock, and a
Roland render:

```bat
build\audio_render.exe opl.log flight.wav 9000000 9799924671 --speaker-log speaker.log
build\audio_render.exe midi.log roland.wav 9000000 1300000000 --mt32 "C:\ROMs\CONTROL.ROM" "C:\ROMs\PCM.ROM"
```

`--speaker-model pwm` renders the speaker's digitised speech with its 15 kHz
carrier (`realsound`, the default, without it).

Status: the interpreter's and recompiler's MIDI logs are byte-identical, and
offline renders of one stream are byte-identical. Live renders with the same
seed differ because Munt's `TVP::nextPitch()` draws from global `rand()`
while partials render serially, so a different audio-advance boundary assigns
the random sequence to different partials; a fixed offline quantum
(`audio_render --step-clocks`) is repeatable. Live SDL playback also shows brief
starvation gaps. Live output is therefore not a stable PCM reference.
Independent reference PCM and a listening check for Roland remain open
(Phase 4 in [roadmap.md](roadmap.md)).

The PC speaker is the cone driven by PIT counter 2's OUT through port 61h
bit 1, with bit 0 the counter's gate. The machine reports each control word,
count and change of those bits (the speaker hook; which one it was follows
from counter 2's null-count flag), and `src/host/speaker.c` runs counter 2
from them one PIT clock at a time as the 8254 data sheet describes it, with
86Box's `pit.c` as the executable reference: in modes 2 and 3 a count written
while counting waits for the end of the period or half-cycle; in mode 0 a count
drives OUT low at once and high N+1 clocks after. The cone's input is
integrated over each output sample, one PIT clock late so every clock is
complete. The machine's own counter 2 model (port 61h bit 5, counter reads)
is unchanged; only the sound comes from this one.

The game uses the speaker three ways. Under the speaker (IBM) driver
ISOUND.117 the music and effects are counter 2 in mode 3, gated on and off at
port 61h, the count rewritten for vibrato and noise every 3.3 ms without a
control word. Under the speaker and Roland drivers the radio calls are
"realsound": the driver (RSOUND.117 file offset 0x2C23, ISOUND.117 0x21A0
onward) sets counter 0 to mode 2 with count 79 (15.1 kHz) and counter 2 to
mode 0 (control word 90h), and its timer handler writes each SPEECH.117 sample,
prescaled to 1-72 by 0x29EF, as counter 2's count, twice. So OUT is low for
N+1 of every 79 clocks: pulse-width modulation. `speaker = realsound` (the
default; `--speaker`, `audio_render --speaker-model`) passes the train through
a moving average one carrier period long, measured from the writes, which
removes the carrier and leaves the level GOG DOSBox 0.74 and 86Box derive from
the count (with the hardware's polarity and the full 10,000 swing tones have);
`pwm` keeps the 15.1 kHz carrier, as the PC sends it. Under AdLib the speech
is OPL writes (bug D2) and the speaker stays off (port 61h bit 1 clear).
`tools/speaker_parity.py` holds the port-level stream and the intro's speaker
audio to DOSBox-X ([roadmap.md](roadmap.md)).
