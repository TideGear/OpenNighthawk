# Architecture

How the recompilation is built, and why it can claim 1:1 parity with the
original DOS game. Written for contributors: people extending the recompiler
or the runtime, or checking a parity claim. Current figures and open work are
in [roadmap.md](roadmap.md); the commands are in [../handoff.md](../handoff.md).

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
src/cpu/        the CPU: state, the interpreter (cpu.c), the shared semantics (x86_sem.h)
src/machine/    the PC: DOS and BIOS (dos.c dispatch and BIOS stubs; dos_memory.c,
                dos_programs.c, dos_files.c, dos_keyboard.c, dos_video.c),
                devices (pc.c), mouse driver (mouse.c)
src/recomp/     the recompiled code's run-time (recomp.c) and the generated-code contract
src/matched/    hand-written equivalents of original routines (matched.c) and the
                draw-list observer (observe.c)
src/fixes/      the switchable fixes (fixes.c)
src/host/       the window, audio and input (main.c), the headless runner (headless.c),
                presentation (present.c), the MT-32 backend (mt32.c)
recompiler/     the translator: decoder, code discovery, C emitter (Python)
tools/          the gate (build_recomp.py), pilots and checks, reference-machine
                harnesses (ref86box/), routes (routes/)
tests/          silicon-vector harnesses, lockstep harnesses, ROM-free unit tests
third_party/    DOSBox DBOPL (GPL-2+) and Nuked OPL3 (LGPL-2.1)
```

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

- **The 386DX/33 profile** (`f117run --timing 386`, `F117R_TIMING=386`;
  `src/cpu/timing386.c`) makes the clock count CPU cycles at 33,333,333 a
  second instead: each instruction costs what 86Box's 386DX/33 charges for it
  (its cycles, prefetch refills, prefixes, REP chunks), VGA memory 32 cycles a
  byte, and ports 86Box's costs in place of DOSBox's delays
  ([timing386.md](../tools/ref86box/timing386.md)). `probe386.py` holds the
  interpreter to 86Box block by block; the recompiled engine does not charge
  these costs yet and `--timing 386` refuses it. Recorded routes run without it.
- **The default speed is 9,000,000 a second**, GOG DOSBox's `cycles=9000`
  for this game. The game's behaviour depends on machine speed (bug D1);
  this is the speed GOG players have, and `--ips` changes it.
- **ISA I/O delay.** With `cycles = floor(ips/1000)`, a port read costs
  `floor(cycles/1024)` extra clocks and a write `floor(cycles/1365)`:
  eight and six at the default speed. DOSBox suppresses that delay when
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
  (`F117R_PIT_CONTROL_IRQ=0` restores the old behaviour). This was the root
  cause of the roster-entry delay (-570 ms drift to +14 ms). It is one of the
  places the machine follows DOSBox 0.74 rather than known real hardware; 86Box
  is the reference that can overrule it.

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
  detection and the one observable difference is flag bits 12-14 after PUSHF.
- **The OPL** defaults to GOG DOSBox's DBOPL core at 44,100 Hz with its 2x
  mixer gain; `--opl nuked` selects Nuked OPL3 in OPL2 mode. Timestamped writes
  render preceding samples before changing the chip. Register timing, mixer
  scheduling and exact PCM agreement with DOSBox are not yet verified.

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
3. **Emission** (`emit.py`). One C function per region, mirroring
   `cpu_step` case for case: operand evaluation order, divide faults
   (the 286 pushes the faulting IP), REP iterations as separate instruction
   boundaries, INT hooks that may wait or switch programs, the TF trap after
   POPF and IRET.

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
lose code an earlier capture got translated.

## The run-time

`src/recomp/recomp.c`.

- **Instances.** Every program and overlay DOS loads is announced before its
  image is written; the run-time matches it to a module by file hash and
  registers it at its load segment. A module it has no translation for is
  registered anyway, so coverage can name it.
- **Dispatch.** CS:IP inside an instance, an entry for that image offset, a
  region that expects this CS, and a region whose bytes are verified: then
  the region runs. Otherwise the interpreter runs one instruction.
- **Verification.** A region's bytes (minus the run-time-read operands) are
  compared with memory before it first runs; the verdict holds until a
  write changes one of them. A bitmap of translated bytes makes every write
  check one bit; a write that changes a translated byte bumps the instance's
  generation and makes running code stop at the next boundary.
- **Code overrides.** Hand-written C registered for one address of one
  module (by name and file hash). Fixes are overrides off until switched on
  (`--fix ID`); matched routines are overrides always on for the recompiled
  engine. The run loop asks for an override before either engine takes an
  instruction; with none enabled that is one counter test. A translated
  region whose bytes include an enabled override's address is refused, so
  the recompiler gives each override address a region of its own (one
  instruction) and the function around it stays translated. An override
  leaves CS:IP and the clock where the replaced code would have, or declines
  and the original instruction runs. Under the 386 timing profile, matched
  entries use the original body until their handwritten clocks charge cycles.
  Each loaded module can place all 1,024
  registry entries; matched routines and observer hooks share that capacity.
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

Eight layers, each checkable with your own copy of the game. Figures are
current as of the last gate (see [../handoff.md](../handoff.md)).

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
   its value and clock, every file write) every 50 million clocks. The 35
   routes in `tools/routes/` must be identical at every checkpoint;
   `tools/build_recomp.py` (the gate) repeats this on every change.
   Routes also declare milestones - `# expect-world`, `# expect-exit`,
   `# expect-open`, `# expect-save`, `# seed-roster` - so two engines that
   crash identically, or stall at a briefing, still fail. Every replay uses a
   fresh save directory. Milestones alone do not prove a takeoff or landing:
   the strong observers below do.
4. **Every translated instruction, routes or not.** The routes run about
   half the code area. `tests/insn_lockstep.c` places each module's image in
   memory, puts the machine in random states (registers, flags, segments on
   and off the module, every byte of memory outside the image) and runs one
   instruction through the machine's interpreter step and through the
   generated region entered there, comparing registers, segments, IP, flags,
   the clock, the interrupt shadow, every byte written, every port access
   and every interrupt raised. 89,303 instruction starts at 64 states each:
   5,714,880 comparisons, 0 mismatches (512 states are declined to the
   interpreter: bytes the gap sweep took for code that are invalid 286
   opcodes). The reference is the machine's own step (`cpu_step` plus the TF
   trap), not bare `cpu_step`.
   The gate repeats this under the 386 profile, comparing the prefetch queue
   and held REP chunk as well. States exercise cached and uncached memory,
   partly filled queues and unfinished chunks. A diagnostic instruction
   budget stops after one operation even when its clock cost is zero.
5. **Replay.** Input logs recorded on one engine replay on the other, and in
   the windowed game, to the same clock count and final state.
6. **The check can see a defect.** `tools/mutation_check.py` plants one wrong
   instruction at a time in the generated code and requires the comparison to
   notice. With the single-instruction lockstep as the judge, 144 planted
   defects (removals, flipped CF, ZF and AX bits) in START, VGAME, END, DSWAP,
   ISOUND.LOG and other modules were all detected, most at instructions no
   route runs. It found two
   weaknesses in this project's own tooling, both fixed: removing a branch
   had been a no-op, and the state hash had not covered what was sent to the
   sound card.
7. **The machine against GOG's DOSBox.** Layers 3-6 compare two engines on
   one model of a PC, so a difference between that model and the real machine
   would pass them all. `tools/fidelity.py` assembles a probe (nothing from
   the game in it), runs it under GOG's DOSBox 0.74-2 with the install's own
   `dosboxF117A.conf` and under `f117run`, and compares what the game asks:
   the memory chain, PSP and environment, the registers it starts with, every
   DOS and BIOS service in the inventory with every register and flag after
   the call, the BIOS data area, every VGA register after a mode set, the
   PIC, keyboard, mouse and joystick, the AdLib and MPU-401 detection, and
   clocks measured against the PIT. All 1,210 comparable answers agree. Its
   first run found 31 differences, each traced to DOSBox's source (shipped in
   the install) and fixed: the PSP at 0191h, a real memory-block chain,
   DOSBox's environment and entry registers, NumLock off, a game port in the
   equipment word, an unconnected stick reading FFh, the volume label
   `C_DRIVE` (START and SETUP compare it with "F117A-SF"), and a port read
   costing 8 cycles. What stays outside it: the instruction timing of
   DOSBox's own BIOS and DOS code, its event scheduler, the 386, and its
   per-millisecond slicing.
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
   intro audio, the radio call's counts). The machine-behaviour probe
   (`tools/fidelity_all.py`, about 1,200 answers) is held to stored baselines
   of known differences in `tools/fidelity_baseline.json`: none for GOG's
   DOSBox (held to every answer, DOS memory layout included, since this
   machine follows it), 40 for DOSBox-X and 95 for 86Box; a new difference
   fails. For the other two the answers that describe how a DOS and BIOS lay
   memory out (segment values, PSP, memory chain, the EXEC'd child's frame)
   are left out, so the lists hold behaviour: the DOS version, BDA words,
   a few VGA/DAC registers, the PIC mask, the MPU port, 86Box's missing mouse
   driver, speed-dependent counts. Which of those the game can see:
   `tools/port_reads.py` replays every route with the machine's port
   inventory and lists the ports its own code reads. It reads 0x21 (55
   times), the PIT, 0x60/0x61, the Sound Blaster probe ports 0x226-0x22E and
   0xB8B (unanswered, as on every reference), 0x388, the VGA retrace status
   0x3DA, DAC data 0x3C9, MPU 0x330/0x331 (one route) and the sequencer data
   port 0x3C5 - only register 1 (59,605 reads). It never reads 0x3C2, 0x3C4,
   0x3C7, 0x3C8 or 0x3CE/0x3CF, so the differences there (input status 0,
   sequencer registers 0 and 4, DAC index readback, graphics-controller
   register 7) cannot reach it, and register 1 agrees. The one difference
   on a port it reads, the PIC mask at start (F8 here as under GOG's DOSBox,
   B8 on 86Box and a real AT BIOS, which leaves the floppy line unmasked), was
   tested by starting the model at B8: routes end on the same clock count and
   behave the same; only the hash differs, because the game writes back a
   value derived from the one it read. Kept at F8, so no recorded hash moves.
   Builds and notes: `tools/ref86box/`. Pictures are compared exactly as RGB, collapsing
   identical consecutive pictures and aligning them in order; unmatched
   pictures are reported, never hidden by a tolerance. Results:
   - GOG DOSBox: 1,329 exact pictures in order; three one-sample logo
     transition images unmatched on each side; drift 57 ms at worst, none at
     the end. A second independent capture matches 1,321 with 11 one-sample
     differences per side: the remaining differences vary from capture to
     capture, so they are not a model defect.
   - DOSBox-X: 1,237 exact pictures; timing drift up to about 0.2 s over the
     intro. DOSBox-X draws scanline by scanline, so a palette write can land
     between the two scanlines of a doubled line (its 640x400 capture of mode
     13h); the tool reads the first scanline and reports how many frames
     differ.
   - 86Box (the 386DX/33 profile): 86 of 87 graphics pictures exact in 6-bit
     DAC values, in order, the other a single sample taken mid-draw. The 8-bit
     captures differ by up to 1 level because the two expand the DAC
     differently (86Box floor(v*255/63), ours v<<2|v>>4), so
     `compare_intro.py` compares v>>2. Timing differs by design: 86Box models
     a 386DX/33's instruction timings, VGA bus and disk, this machine DOSBox's.

   Other comparisons: `tools/dosbox_compare.py` captures the AdLib register
   writes in DOSBox and here over the logo and intro: all 22,840 over 106.0 s
   identical in order and value, timing -14 to +1 ms, with this machine
   started 275 ms in as GOG's DOSBox is when F117 starts (`--boot-ms`). Started
   at 0 the sound driver's per-frame generator is one step off from the note at
   29.7 s on (the same generator step in all three GOG captures), 86% of the
   writes match; the match holds for starts of 5-20, 120-230 and 260-400 ms, so
   it is a start phase, not a clock error;
   `tools/fade_calibration.py` runs START's, PLAYER's and END's fade
   calibrators on both machines (START and END return the same range;
   PLAYER's DAC throughput is within a few bytes of DOSBox's after matching
   its I/O-delay suppression at slice boundaries).

   Open: exact frame phase against DOSBox (the intro's partial-logo
   transitions depend on capture timing) and exact PCM agreement. Reimp's D96
   is an extra wait in its own UI and is not evidence for a wait in the
   translated START.

## Observing flights through normal controls

With `F117R_BUILD_TESTS=ON`, `f117machine_api` exposes a scalar host API and
`tools/machine_api.py` wraps it: boot, advance to an absolute clock, read guest
memory, queue normal keys and mouse input, record input, capture a picture. It
has no memory or register setters. Only one machine may be live per process.

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
saved rosters, never edited ones; the rank-6 case stages one cause (rank and
total) and says so.

Per-route recordings and their parameters are in
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
the first N (bisecting a divergence to one routine).

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

**The harness** (`tests/func_lockstep.c`, needs the generated code): for each
routine it places the module image in memory (at segment 0, where the
unrelocated image is also the relocated one), sets random registers, flags and
memory and a return address outside the routine, runs the interpreter until the
routine's own RET reaches it and the matched C once, and compares registers,
segments, IP, flags, clock, ports, interrupts and all memory. Details that
caught real defects:

- Half the states put small words (-1, 0, 1, 0-31) where the arguments sit,
  and the other half use memory of mostly 00, FF and 01 bytes, so edge cases
  and exact tests on memory are reached.
- START and END's uninitialised graphics-driver thunks are replaced with
  RETF on both sides of the check, so drawing wrappers can return and their
  surrounding work is compared. The routes still check the real driver.
  END's DAC-loader states clear the BIOS gray-scale flag that overlaps the
  module image at segment 0; some START map-caption states plant the exact
  argument kinds 64h and 65h. `--only MODULE:IP,...` selects routines for
  diagnosis; the full gate checks the whole table.
- A state counts only when the original returns to the pushed address with the
  stack at its entry level (a looser rule let a routine that clears its own
  stack slide to the return address through `00 00`). States where the original
  writes over its own code are skipped.
- The routine's own RET lies within 0x300 bytes above its entry, or for the
  entries in `CODE_BELOW` (second entries into the model fills that jump back
  to a shared exit) as far below it as the table says; for those the
  original's code is also watched after every step, since a row table over the
  code (DS = CS) can change an instruction, run it and write the bytes back.
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
  executing it there is no evidence at all, and the routine is dropped (VGAME
  0xF024, the run-time termination messages, was, 8 Oct 2026).
- Room on the routes: the dispatcher (`recomp_override_step`) checks every
  matched routine it runs. One whose instructions carry the clock past an
  event limit nothing moved claimed too little room, and the run's output
  ends with `[matched] OVERRUN ...`; time charged beyond instructions (a
  port's bus delay, a DOS transfer, kept in `cpu.charged`) may cross a limit
  as it does in the original and is left out. Step 9 of the gate fails on any
  such line. It is the only room check a routes-only routine meets: the
  picture decoder's LZW step walked a prefix chain of any depth after one
  room check (9 Oct 2026), and with that bug put back step 9 names it.
- Port accesses: the bus delay (`io_delay`) can carry the clock across the
  limit inside one IN or OUT, and the original then stops right after that
  instruction. The machine notes the clock of such a crossing
  (`cpu.io_cross`) and the dispatcher reports a matched routine that went
  on past it. A stretch with k port accesses therefore claims
  `IO_SLACK(k)` (k times the read delay, ips/1000/1024) beside its
  instructions, so no delay can cross inside a matched routine; when that
  room is not there the routine parks and the original runs the stretch.
  Ten routines do port I/O; three of them shifted a checkpoint by two
  clocks before the rule (9 Oct 2026).

`F117R_SHADOW=FROM:TO` re-runs matched routines against the original on the
live game in a window (a diagnostic, not a gate: device state is not in the
snapshot). `f117run --dump LINEAR:LENGTH` prints registers and memory at the
end of a run for comparing two engines at a chosen clock.

653 addresses are matched in all seven programs. The programs carry
byte-identical copies of the C runtime helpers (string and block copies, the
32-bit shifts, multiply and divide), so one matched routine serves several
addresses. MPS_LOGO carries the same library built for a larger model: its
calls are far (9A), or PUSH CS / CALL to a far routine, where the others' are
near, and some of its routines return far with the arguments a word higher.
The shared routines read each call's kind from the code (`sm3_call`) and take
a far flag. Candidates come from `tools/reimp_names.py`; the list is the table
at the end of `matched.c`.

## Audio

The OPL is synthesised on the host (above). The Roland path uses Munt
(libmt32emu), isolated in `src/host/mt32.c` and enabled with
`F117R_WITH_MT32EMU` (on by default; Munt fetched at a pinned commit). It
links the public C API, identifies the supplied control/PCM pair and fails
startup on invalid ROMs. Each MPU byte first advances audio to its machine
clock; complete messages enter Munt's MIDI queue, rendered in bounded blocks
at 44,100 Hz and mixed with OPL and speaker before clipping. A rejected queue
entry or oversized SysEx fails the session rather than dropping traffic.
`f117run --midi-log` and `--speaker-log` capture MIDI and speaker state;
`audio_render` merges them by guest clock for an offline mix.

Status: the interpreter's and recompiler's MIDI logs are byte-identical, and
offline renders of one stream are byte-identical. Live renders with the same
seed differ because Munt 2.8.3's `TVP::nextPitch()` draws from global `rand()`
while partials render serially, so a different audio-advance boundary assigns
the random sequence to different partials; a fixed offline quantum
(`audio_render --step-clocks`) is repeatable. Live SDL playback also shows brief
starvation gaps. Live output is therefore not a stable PCM oracle. Independent
reference PCM and a listening check for Roland, and exact OPL PCM agreement,
remain open ([roadmap.md](roadmap.md)).

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
