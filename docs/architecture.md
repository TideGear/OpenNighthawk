# Architecture

How the recompilation is built, and why it can claim 1:1 parity with the
original DOS game. Written for contributors: people extending the recompiler
or the runtime, or checking a parity claim.

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
src/host/       the window, audio and input (main.c), the headless runner (headless.c),
                presentation (present.c)
recompiler/     the translator: decoder, code discovery, C emitter (Python)
tools/          the pipeline (build_recomp.py), the unpacker, routes, font generation
tests/          the silicon-vector harnesses, for the interpreter and for generated code
third_party/    DOSBox DBOPL (GPL-2+) and Nuked OPL3 (LGPL-2.1)
```

## The CPU and its two engines

`src/cpu/cpu.c` is the Reimp project's oracle interpreter (see
[provenance.md](provenance.md)), an 8086/80186/80286 real-mode core
validated against the SingleStepTests vectors. Its instruction semantics -
the ALU and its flags, shifts, multiply and divide, BCD, string steps,
stack-frame instructions, flag transfers - live in `src/cpu/x86_sem.h`, and
`cpu_step` is now only the decoder that calls them.

The recompiled code calls the same functions. A translated `adc
[bx+si+12h], ax` is `alu_op(c, 2, seg_read16(c, ds, bx+si+0x12), ax, 1)` -
the call `cpu_step` makes for those bytes - so the two engines cannot differ
in an instruction's arithmetic or its flags.

The machine is a 286, as in the Reimp's oracle (VGAME and MPS_LOGO are
compiled with 80186 instructions).

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

- **The default speed is 9,000,000 a second**, GOG DOSBox's `cycles=9000`
  for this game. The game's behaviour depends on machine speed (bug D1);
  this is the speed GOG players have, and `--ips` changes it.
- **ISA I/O delay.** With `cycles = floor(ips/1000)`, a port read costs
  `floor(cycles/1024)` extra clocks and a write `floor(cycles/1365)`:
  eight and six at the default speed. DOSBox suppresses that delay when
  fewer than three delays remain in the CPU slice. The slice model shared
  with DOS file-transfer costs considers millisecond, PIT and VGA events.
  Without bus delay the logo's AdLib driver fails its
  card detection, which polls the status port 200 times expecting an
  80-microsecond timer to expire.
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

## The PC

`src/machine/dos.c` (now split by service into the `dos_*.c` files) began
as the Reimp oracle's DOS: the loader, the
EXEC/overlay/terminate chain that F117.COM drives, the bump allocator and the
file calls. `src/machine/pc.c` holds the devices. Both were extended from
"run a scripted capture" to "play":

- A real 8259 (IRR, ISR, IMR, priorities, EOI forms) and all three 8253
  counters, with counter 2 gated by port 61 (the AdLib driver paces speech on
  it).
- A keyboard controller delivering set-1 bytes one at a time, and a BIOS
  INT 9 translation for a US layout with the shift, lock and E0 states.
- Blocking INT 16h and DOS console reads; INT 10h text services (SETUP draws
  with them); INT 1Ah and DOS date and time from the host clock at boot plus
  emulated time.
- The VGA: mode 13h (the boot-to-flight port trace showed no planar, CRTC or
  graphics-controller programming, only the sequencer's screen-off bit), the
  DAC and the default BIOS palette, the retrace timing, text mode.
- AdLib timers and status (the synthesis is the host's), the MPU-401 in UART
  mode, the game port, the speaker.
- A save directory overlaid on the install: reads look there first, writes
  go there, and the install is never written.
- A software mouse cursor drawn into guest video memory, with clipped
  background save/restore on show, hide, reset, movement and shape changes.
  Frame presentation reads those pixels; it adds no second cursor. A
  mode change discards the saved background and hides the cursor. The
  driver lives in `src/machine/mouse.c`.
- The service vectors (10h, 16h, 1Ah, 21h, 33h) point at stubs, so a program
  that hooks one and chains to the old vector reaches the service.

### Known differences from a real PC

- **The BIOS** is a set of stubs and host routines, not a ROM image. Its
  interrupt handlers execute a handful of instructions where a real BIOS
  executes dozens, a small timing difference.
- **DOS memory layout** follows the Reimp's oracle (programs at 1566 or 18E1
  depending on the sound driver), not any particular DOS version's.
- **The OPL** defaults to GOG DOSBox 0.74-2.1's DBOPL core at its configured
  44,100 Hz, with the reference's 2x mixer gain. `--opl nuked` selects Nuked
  OPL3 in OPL2 mode, resampled to the same output rate. Timestamped writes
  render preceding samples before changing the chip. Shared synthesis code
  removes one source of differences; register timing, mixer scheduling and
  exact PCM agreement with DOSBox still need verification. DBOPL can differ
  briefly when a released channel becomes silent inside different render
  blocks. The host uses sample-sized calls to keep its output independent
  of how often the caller advances it. The standalone `dbopl_render` probe
  also permits comparisons against block rendering.

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
   switch tables (`jmp cs:[bx+table]`, bounded by the guarding compare),
   near pointer tables (`call/jmp [reg+table]` in the code segment or the
   data group: the sound drivers' command handlers, the C library's
   dispatch), far code pointers in relocated data, MSC prologues inside the
   code segments, and coverage - instructions the interpreter executed in
   recorded runs. The data group is found from the C startup's relocated
   `mov di, DGROUP`. Last, every gap left in the code area that decodes
   cleanly to a return or jump is seeded, until none is left: that finds
   interrupt handlers a driver installs by computed address. Data that
   happens to decode costs size, never behaviour, since a region only runs
   from an instruction start whose bytes are verified.
   Every instruction belongs to exactly one region; flow into another
   region's instruction leaves through the dispatcher.

   `tools/census.py` measures the result: the bytes of each module's code
   area that lie inside a translated instruction. The code area is an
   EXE's segments below the data group that hold code (Microsoft C puts far
   data segments, such as VGAME's lookup tables, among them), an overlay's
   image from its base segment, a .COM's image. With the sweep, 96% of
   code-area bytes are translated and no remaining gap decodes like code;
   the rest is strings, tables and variables between routines.
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
the interrupt chains the game patches into its own code (`jmp far 0:0`
operands written at install) just work.

The output - C derived from the original machine code, plus the original
image bytes the run-time verifies against - goes to a work directory outside
the repository.

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
  module (by name and file hash), off until switched on (`--fix ID`). The
  run loop asks for an override before either engine takes an instruction;
  with none enabled that is one counter test. A translated region whose
  bytes include an enabled override's address is refused, because regions
  jump within themselves without returning to the dispatcher, so the
  interpreter reaches the address and the override runs. The override
  leaves CS:IP and the clock where the replaced code would have, or
  declines and the original instruction runs. Fixes attach this way
  (`src/fixes/`). The recompiler reads the fix table and gives each
  override address a region of its own instruction alone, so the refused
  region is that one instruction and the function around it stays
  translated (before that, D5's refused region cost 1.5 million
  interpreted instructions on its route).
- **Data fixes.** The machine's one hook that may change what the guest
  sees, `file_data`, hands each DOS read's bytes to the fixes before they
  reach memory; a correction applies only to its named file, of its size,
  where the shipped byte is. With no fix on nothing changes.
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

Seven layers, each checkable by anyone with their own copy:

1. **The interpreter against silicon.** `tests/sstest.py` (8088,
   3,007,000 vectors, 0 failures) and `tests/sst286.py` (80286 real mode,
   1,429,998 vectors, 0 unexplained; the explained buckets are behaviours the
   game never exercises, each named).
2. **The translator against silicon.** `tests/sst_recomp.py` decodes and
   emits each vector's instruction exactly as game code is translated,
   compiles it, and runs the vector through the generated function with the
   same comparisons. At 300 per file: 8088 90,900 of 90,900; 286 94,200 with
   0 unexplained.
3. **The two engines on whole sessions.** `f117run` runs the same inputs
   under `--engine interp` and `--engine recomp` and hashes all of memory,
   the registers, and everything the machine sent out - every port write
   with its value and clock count (the sound card, the palette, the timer,
   MIDI) and every byte written to a file - at intervals. The routes in `tools/routes/` - boot to
   flight; a sortie flown into the ground, the debriefing and back to the
   front end; boots under the speaker and Roland drivers, which load every
   program at other addresses - are identical at every checkpoint.
   `tools/build_recomp.py` repeats this on every build.
   Theatre routes also declare `# expect-world STEM` and
   `# expect-exit PROGRAM CODE MIN_CLOCKS`. Both the pipeline and
   `tools/run_route.py` check the runtime log for the selected world's
   briefing/flight files, the expected exit code and minimum program
   duration. Equal early crashes or a route stuck at the briefing fail
   these checks even if their hashes agree. Airborne screenshots are
   reviewed when a route is added; these milestones alone do not prove
   takeoff or landing.
   Roster routes use `# expect-save FILE OFFSET HEX` to require committed
   saved bytes, independently of matching hashes. Every pipeline and
   executed-coverage replay uses a new save directory; prior edited careers
   are preserved and cannot contaminate the next baseline. Mouse-only
   moves (`--move WHEN:X,Y`) are scheduled and recorded as machine input,
   like clicks, so hovering over a roster row can enter its name editor.
   `# expect-open FILE PROGRAM MIN_COUNT` requires successful file opens
   from that program, for routes that must reach and return from screens.
   The maintenance route requires two arming pages and four office pages;
   a run stuck in a roster dialog fails instead of passing on equal hashes.
4. **Every translated instruction, routes or not.** The routes run about
   half of the code area (`tools/exercised.py`: 56.2%; error paths,
   individual mission objectives and most setup screens remain).
   `tests/insn_lockstep.c` covers the rest: for each of the 89,276 instruction starts the
   translation has, in every module, it places the module's image in
   memory, puts the machine in random states (registers, flags, segments on
   and off the module, every byte of memory outside the image) and runs one
   instruction through the machine's interpreter step and through the
   generated region entered at that instruction. Registers, segments, IP,
   flags, the clock, the interrupt shadow, every byte written, every port
   read and written and every interrupt raised (with the registers at that
   moment) are compared. At 64 states each, 5,713,152 comparisons: 0
   mismatches. Eight starts are always declined to the interpreter: bytes
   the gap sweep took for code that are invalid opcodes on the 286 (`0F`,
   `63`, `64`, `66`), which fault the same way either way. This is a CTest
   (`insn_lockstep`) and runs in under a second.

   The first run reported 47 mismatches, every one an IRET or POPF loading
   TF: the reference was bare `cpu_step`, while the machine (and the
   generated code, which mirrors it) takes the single-step trap after the
   instruction. The reference was corrected to the machine's own step.
5. **Replay.** Input logs recorded on one engine replay on the other, and in
   the windowed game, to the same clock count and the same final state.
6. **The check can see a defect.** `tools/mutation_check.py` plants one
   wrong instruction at a time in the generated code and requires the
   comparison to notice; a mutant it misses is reported with whether the
   defect ran at all, so an untested path is not mistaken for a pass.

   Results (3 October 2026), each mutant compared against its own binary's
   interpreter: removing an instruction was detected at 7 of the 8 sites
   where the removal ran - VGAME's frame-rate controller and its keyboard
   interrupt handler, START's mission generator, END's timer code, and
   three sites in ASOUND's sequencer and speech code. The eighth, a
   flags-only `test` in START, ran 89 times with the same branch outcomes
   (masked). Two sampled sites never ran on the route and are reported as
   such. Flipping one bit or flag after an instruction was masked at all
   four sites tried: the values were dead there, which the run counts show.

   With `--lockstep` the mutant is judged by the single-instruction
   lockstep (layer 4) and the sites are drawn from all translated
   instructions: 12 of 12 detected (seed 7; removals, flipped CF, flipped
   AX bit 0) in START, VGAME, END, DSWAP and ISOUND.LOG, most of them at
   instructions no route runs. A larger run (seed 11, stopped at the
   two-hour limit after 72 of 150): 72 of 72 detected - 21 removals, 21
   flipped CF, 15 flipped ZF, 15 flipped AX bits - across 14 of the 18
   modules, 33 of them in VGAME. A further batch (seed 12): 60 of 60
   detected - 13 removals, 15 flipped CF, 17 flipped ZF, 15 flipped AX
   bits - across 10 modules, 25 in VGAME and 12 in START. Together with
   the first 12: 144 planted defects, 144 detected. A flipped flag that a route would mask is
   seen here, because the state is compared after the one instruction.

   The mutation check found two weaknesses in this project's own tooling,
   both fixed: removing a branch had been a no-op (so its "masked" verdict
   meant nothing), and the state hash covered memory and registers only, so
   a wrong value sent to the sound card was invisible - which is why it now
   includes everything sent out.

7. **The machine against the reference.** Layers 3-6 compare two engines
   running on one model of a PC, so a difference between that model and the
   machine the game is sold on would pass them all. `tools/fidelity.py`
   measures the model against that machine directly: GOG's DOSBox 0.74-2
   with the install's own `dosboxF117A.conf`. A probe program, assembled by
   the tool (nothing from the game in it), runs as F117.COM from the same
   autoexec GOG uses under DOSBox headless and under `f117run`, and asks
   what the game asks: the memory control block chain, the PSP and
   environment it was given, the registers it starts with, every DOS and
   BIOS service in the inventory (`F117R_INVENTORY=FILE` counts which ones
   the programs call) with every register and flag after the call, the BIOS
   data area, every VGA register after a mode set, the PIC mask, the
   keyboard, mouse and joystick, the AdLib timer detection, the MPU-401
   reset, and the clocks measured against the PIT: the VGA frame period,
   the retrace length, instructions per PIT count, what a port access
   costs, BIOS ticks per frame. The two answer sheets are compared field
   by field.

   The first run found 31 differences, then 211 once every register after
   every call was compared. Each was traced to DOSBox's source (shipped in
   the install as dosbox-0.74-2.1.tar.gz, GPL-2 or later) and the model
   changed to match: the program now loads at the PSP DOSBox gives it
   (0191h, not 0101h, so 2.3 KB less memory is free), out of a real chain
   of memory control blocks run by DOSBox's allocator, with DOSBox's
   environment, PSP fields, entry registers and EXEC/terminate semantics;
   NumLock starts off (it changes what the keypad sends); the equipment
   word reports a game port; an unconnected joystick port reads FFh, not
   F0h; the volume label is C_DRIVE, which START's and SETUP's disk-label
   checks compare against "F117A-SF" and "F117B-SF"; a port read costs 8
   cycles, not 9; and a dozen register-level details of individual
   services. Now: 858 answers agree, 0 differ.

   What remains outside it: the instruction-level timing of DOSBox's own
   BIOS and DOS code (its services are guest-code stubs with a callback
   instruction; here they are serviced in place) and of its event
   scheduler; the 386 that DOSBox emulates where this machine is a 286
   (the game contains no CPU detection; the observable difference is flag
   bits 12-14 after PUSHF); and DOSBox's per-millisecond slicing of I/O and
   transfer costs.

   **Pictures from the game against DOSBox.** `tools/video_compare.py`
   uses the same window driver as the music comparison, with Ctrl+Alt+F5
   for DOSBox's lossless ZMBV AVI. ffmpeg decodes all raw 320x200 RGB frames
   without resizing; `f117run --shots` samples at 70 Hz. Each sequence
   collapses consecutive identical RGB hashes, keeping their sample counts
   and times, and aligns exact pictures in order. Every unmatched reference
   picture is reported, including at the ends; our pictures are counted
   within the reference's estimated time range. A median offset from the
   matching changes removes the unknown capture start. It does not assert
   instruction-accurate or frame-accurate synchronisation. Non-320x200
   captures, including text mode, are excluded explicitly.

   Two 130.767-second runs, each 9,165 VGA frames at 70.086303 Hz, found
   1,217 exact consecutive pictures in order. Unmatched reference pictures:
   115 and 109; unmatched shots within the capture's time range: 89 in both.
   All but two reference differences and six shot differences last one
   sample. These counts are not a parity pass: the tool exits 1 if any
   pictures are unmatched. `--reuse RUN_DIR --diagnostics` writes paired
   DOSBox/shot/difference PNGs, showing the longest differences first.

   DOSBox's source (`vga_draw.cpp`, `VGA_DrawPart`, GOG's `svga_s3` default)
   reads four groups of lines over a frame; the shots read VRAM at once.
   The diagnostic pictures show partially drawn logo borders and moving
   intro sprites, consistent with that difference. The roster transition
   also differs in timing, and our mouse cursor changes colour
   around 117.5 and 129.1 s where both reference captures stay unchanged.
   The paired longer shot differences are confined to up to 78 pixels in
   the cursor's 10x15 area at (160,77); pilot names match. START keeps the
   INT 33h cursor hidden and draws this pointer from its own sprite. Fixing
   the driver's guest-visible cursor did not change this sequence: a third
   run (`intro-_hurx9dz`) matched the same 1,217 pictures, with 113 unmatched
   reference pictures and 89 shots. Investigate the game's palette/cursor
   handling and transition timing; these differences remain to explain.
   Captures, screenshots, reports and diagnostic
   images are kept under `~/f117-recomp-local/video/`, outside the repo.

   **Cursor readback against DOSBox.** The fidelity probe now reads VGA
   memory directly beneath an INT 33h cursor: XOR drawing, a guest write,
   saved background restoration, nested hide/show, movement and reset.
   All eight new answers agree (1,193 total answers, zero differences).
   `tests/test_mouse.c` also checks clipping, mode changes, text cursor
   restoration and that frame capture reads guest pixels without drawing
   an overlay. After the driver change all six routes agree at 329
   checkpoints (every 50 million clocks) and at their final states; the
   64-state instruction lockstep still reports zero mismatching starts.

The DAC probe also checks read/write address state after BIOS palette calls,
blocks crossing palette index 255, and their I/O timing. BIOS calls now use
the port handlers, including their bus cost and output hash, instead of
editing the palette array directly. All 1,210 machine answers agree.
`tests/test_video_ports.c` checks linear BIOS buffers across ES:FFFF and
zero-length blocks too. All six routes still agree at 329 checkpoints and
final states; instruction lockstep reports zero differences. The pipeline's
`--parity-only` mode checks an existing build and now compares checkpoint
streams explicitly, rejecting runner failures as well as different states.

`video_compare.py --against RUN_DIR` copies a saved DOSBox capture into a
new run and generates fresh shots, retaining the old artifacts. Replaying
`intro-caauys37` this way after the BIOS DAC correction (`intro-0wp6pl8e`)
leaves the 1,217 matches, 109 unmatched reference pictures and 89 unmatched
shots unchanged. A fresh full music capture matches only its first 596
writes before the channel-3 note at 29.7 s; it does not verify the whole
music stream. That note is not random between runs: both saved 130-second
DOSBox captures write A3h=49h/B3h=21h there, and this machine 92h/20h at
every startup clock tried. It comes from the sound driver's generator at
0505:0562 (state at 034F:17E4, `ror3(state + 9248h)`, frequency
`(~state & [19FE]) + [1902]`), called through the driver entry 0505:0C17
by PLAYER about once per video frame (1,334 calls here before the note).
DOSBox's value is exactly one generator step ahead, so by 29.7 s DOSBox's
PLAYER has run one more frame. That is a retrace-phase or frame-count
difference, the open "exact phases" timing item, not a sound driver fault.
DOSBox 0.74 does not restart its vertical timer between text mode and
mode 13h (the periods differ by under its 0.0001 ms threshold), so that is
not the cause. Logs live outside the repository.
The music comparator now exits 1 on differing writes and rejects runner
failures. `--reuse RUN_DIR` checks a saved stream; fresh runs retain their
artifacts in separate `dbxcompare/music-*` folders. Four ROM-free stream
tests verify changed values, reordered writes, missing alignment and an
exactly-one-window capture (the last possible alignment is included).

`tools/fade_calibration.py` copies just the original START, PLAYER and END
fade calibrators into private COM probes and runs them on both machines.
It reports the five successive calibration values per trial, without
calling phase-dependent values an exact parity pass. Three fresh trials
per program repeat the same results: START/END return `[8,8,7,8,8]` on
DOSBox and `[8,8,8,7,8]` here; PLAYER returns 5,839-5,844 DAC bytes per
display period on DOSBox and 5,811 here. The seven/eight-step fade range
agrees, but phase and PLAYER throughput still differ. This is evidence
for investigating timing, not proof of the roster delay's cause.

The initial roster "colour changes" were subsequently identified as
pointer erase/redraw sampling: the differing pixels become the underlying
grey background. Their 11.6-second spacing matches the beat between 70 Hz
shots and 70.086 Hz retrace. The comparator now samples every 128,413
clocks, the runtime's VGA period, and `--against` uses the current sampling
period while retaining the reference duration. `--reuse` preserves the
original run's period. Against the same `intro-caauys37` reference, run
`intro-ek_ltnum` matches 1,219 pictures, with 107 unmatched reference
pictures and 74 shots, all one sample. No roster shots after 110 s are
unmatched. The transition still differs by about 0.57 s; the remaining
instantaneous-snapshot/scanout differences still prevent a parity pass.

Matching DOSBox's `IO_USEC_read/write_delay` suppression near a CPU slice
boundary changes PLAYER's calibration from five 5,811s to
`[5841,5841,5837,5839,5836]` (three fresh trials), much closer to DOSBox's
`[5841,5839,5841,5839,5844]`. START/END's seven/eight-step results are
unchanged. This isolates a cause of the throughput discrepancy but does
not prove exact phase or scheduler parity. All 1,210 fidelity answers
agree, and ROM-free tests check both sides of the suppression threshold
at millisecond, PIT and VGA boundaries. A fresh early-music capture matches
365 writes over 15.5 s, drift 0..26 ms. Saved-video run `intro-nq0jeekk`
still has 1,219 matches, 107 unmatched reference pictures and 74 shots,
all one sample; no unmatched roster shots after 110 s. The transition
delay remains (~0.59 s). The shared slice calculation retains the earlier
file-transfer event model; mode-dependent event phases and guest stub
instruction timing remain limitations.
All six routes after this change agree at 329 checkpoints and final states;
the 64-state instruction lockstep compares 5,709,312 states with zero
mismatching instruction starts.

**Mode-13h scanout.** The machine now schedules GOG's four-part VGA draw:
50 logical rows at each of lines 100, 200, 300 and 400 in the 449-line
frame. The display address is latched at retrace for the following frame;
the 64K chain-4 address wraps. Presentation and screenshots use the last
completed indexed frame and its palette, rather than a fresh VRAM copy.
Events are processed in display-time order even after a long callback.
Text mode and graphics before the first completed frame retain snapshots.

The DAC has separate port-readback and displayed colour tables. Red/green
writes affect readback immediately, while blue publishes the triplet to
DOSBox's render palette. Changed PEL masks rebuild its aliases; unchanged
masks do not publish a partial triplet. The display table already includes
the mask, so presentation does not apply it again. Scanout buffers,
palette, address latch and event state are included in engine parity hashes.
`tests/test_scanout.c` verifies temporal writes between groups, retaining
the previous completed frame, address wrapping and retrace latching, mode
changes and hash detection; DAC tests cover partial writes and aliases.

Saved-reference replay `intro-p6mrjdbw` now matches 1,319 of 1,326 reference
pictures, compared with 1,219 before scanout. Seven reference pictures and
28 shots remain unmatched, all one sample; the roster after 110 s matches.
The transition still differs by about 0.57 s. These results improve the
model without claiming full video parity or exact mode/event phase.
All 1,210 fidelity answers agree. All six routes match at 329 checkpoints
and final states with scanout included in the hash, and the 64-state
instruction lockstep reports zero mismatching starts. Fresh early music
matches 365 writes over 15.5 s (drift 0..36 ms). Screenshot deadlines now
advance from their original schedule, avoiding accumulated instruction
overshoot in later samples.

**Transition timing diagnosis.** The main app was rebuilt against the fully
covered generation used by the 27-route parity run (89,276 instruction
starts). A fresh 130.8-second capture still has 1,319 exact pictures, seven
unmatched DOSBox pictures and 28 unmatched shots. Repeating the same inputs
with the interpreter produces byte-identical shot hashes and timestamps and
the same final state (`e4114c0415d4920d`): the transition drift is not caused
by generated code. Three independent DOSBox captures put the stable roster
picture at 107.510-107.567 s; the recompiled and interpreted runs put it at
108.411-108.435 s. A closer comparison separates the DOSBox roster list-only
picture (`a0fc2584`, 107.311-107.567 s) from the selected-pilot details
picture (`eb49f15a`, first at 107.567 s). At the default 9 MIPS, local runs
reach these pictures at 108.124 and 108.423 s; after the comparator's median
capture offset, they are 0.528 and 0.571 s late. Almost all the residual
delay is already present when the roster list first appears; the local
list-to-details transition adds about 43 ms beyond DOSBox's 257 ms. Local
logs open `rostscrn.pic` at 107.481 s and `rostsprt.pic` at 107.696 s.

The default run's last shared transition image (`ea0787f6`) begins at 106.526 s
in DOSBox and 106.811 s locally; applying the measured capture offset of
-0.285325 s puts both at 106.526 s. The next one-sample image is at 107.282 s
in DOSBox and 108.010 s locally (442 ms late after alignment). The stable
roster list is at 107.311 s versus 108.124 s (528 ms late). The delay therefore
accumulates while the roster page is being built, after its visible transition
begins. In the local log, `Roster.Fil` opens at 106.782 s, `rostscrn.pic` at
107.481 s and `rostsprt.pic` at 107.696 s. After alignment the sprite sheet
opens at 107.411 s; the first stable list frame follows 428 ms (about 30 VGA
periods) later. Across the 4, 9 and 12 MIPS runs, sprite-open-to-list times are
673, 428 and 393 ms, while the aligned list residuals are 1,483, 528 and 385
ms. This narrows the remaining trace to `0x03641(7, 0x0615C)`: screen 7's
loader and the Bulletin Board painter, which opens `rostsprt.pic` and draws the
ten pilot rows.

An instruction trace now covers the 9 MIPS interval from the `rostsprt.pic`
open at 969,259,665 through the first stable list picture at 973,113,714
(3,854,049 emulated clocks, 428.2 ms). The 64,000 visits through START's
`0x08922`–`0x08973` are the RLE90 pixel decoder for the 320x200 picture. The
trace also follows MGRAPHIC's page-copy loop (`0x02B35`, `REP MOVSB`) and its
transparent-pixel blitter (`0x0208A`). After the paint, START calls its
`0x08378` palette writer nine times. Each writes 256 RGB entries; calls are
spaced by about 256,800 guest clocks (two VGA periods at this run's clock) and
span 2,026,300 clocks, about 225 ms. The last call is followed by about 39.7
ms to the stable capture. The trace is at
`C:/Users/Tideg/f117-recomp-local/video/roster-trace-full/painter-full.txt`.

The local trace identifies full-picture RLE decoding, page copying and nine
palette-writer calls before the stable list capture. A separate DOSBox-X
2026.10.01 instruction trace at 9,000 cycles/ms now starts at the earlier
`palettes.pal` open (106.401489 s) and covers 8,000,000 instructions through
108.008496 s. Its file-open sequence is `requestr.pic` at 106.402582,
`Roster.Fil` read/write at 106.760483/106.761036, `rostscrn.pic` at
107.177204, and `rostsprt.pic` at 107.393696. The START `0x08378` palette
routine appears seven times: four at 106.778615, 106.817953, 106.850761 and
106.893558 (before `rostscrn.pic` opens), then three at 107.571473,
107.598189 and 107.626927 (after `rostsprt.pic` opens). The earlier
4,000,000-instruction trace began at the sprite-sheet open and therefore saw
only those final three calls, not the complete interval.

A separate 9 MIPS local instruction trace starts at `Roster.Fil` open
(icount 961,032,646; 106.781405 s) and ends when `rostscrn.pic` opens
(icount 967,325,273; 107.480586 s), 699.181 ms later. In that interval,
START calls `0x08378` 16 times. Each call writes 256 palette entries; the
calls span about 427 ms, with retrace polling across about 452 ms. START then
calls its retrace/PIT sampler at `0x08EAD` 17 times over about 242 ms. The
remaining roughly 5 ms is setup and short gaps. This trace accounts for the
local pre-screen delay in guest execution. It ends at the same machine hash as
the saved local video run (`e4114c0415d4920d`). Its trace is at
`C:/Users/Tideg/f117-recomp-local/video/roster-pre-screen-trace/trace.txt`.

The expanded DOSBox-X trace was repeated with the stock install-root
`ROSTER.FIL`, matching the file mounted for the packaged DOSBox video capture.
The prior DOSBox-X run used a cloud-save copy that differs at nine bytes.
With the stock file, run 4 produced the same seven `0x08378` entries and the
same file-open times as the earlier run. The roster-save difference therefore
does not account for the palette-call count or the observed phase comparison.
The corrected trace is at
`C:/Users/Tideg/f117-recomp-local/dosbox-ref-roster-mcp-20261005/run4/LOGCPU.TXT`.

Measuring from `Roster.Fil` read within each trace removes the unrelated
clock origins: DOSBox-X opens `rostscrn.pic` 416.721 ms later, while the local
run takes 699.181 ms, an excess of 282.460 ms. The corresponding 17-call
`0x08EAD` retrace/PIT sampler starts 172.413 ms after the read in DOSBox-X and
457.056 ms after it locally. From the first sampler call to the screen-picture
open, the intervals are 244.308 and 242.125 ms. The extra local time therefore
accumulates before this sampler block, during the preceding palette and setup
phase: that interval contains 16 local `0x08378` calls versus four in
DOSBox-X. This is a comparison of guest paths across two emulator builds, not
proof that the call-count difference alone causes the timing gap. After the
sprite-sheet open, the separate local list-painter trace records nine palette
calls, while the expanded DOSBox-X trace records three. The GOG DOSBox 0.74
video still lacks a guest-event marker, so the frame phase remains open.

The expanded trace records three 64,000-pixel RLE90 passes between 106.408
and 107.521 s. The sprite-sheet decode pass takes about 121.4 ms, versus
112.7 ms in the local trace. Decode execution alone does not explain the
528 ms aligned video residual. The seven DOSBox-X calls are not one
continuous nine-step fade: four precede the screen-picture open, a roughly
678 ms gap separates the fourth and fifth, and three follow the sprite-sheet
open. The local nine calls were measured only from its sprite-sheet open to
the first stable list frame. These are different trace windows.

A fresh local comparison against the saved GOG DOSBox 0.74 video reproduces
1,319 exact RGB pictures over 130.767 s (1,326 reference pictures and 1,347
local shots; seven and 28 unmatched, all one sample). Its measured offset is
-285.325 ms and end drift is -570.671 ms. The list-only image is still
107.311-107.567 s in the reference and 108.124-108.423 s locally; details
start at the end of each interval. A provisional phase estimate combines
that video offset with the DOSBox-X and local file events: `Roster.Fil` opens
within about 21 ms in the two instruction logs. Anchoring the clock origins at
that event puts DOSBox-X's screen and sprite opens about 282 and 281 ms ahead
of the local opens. The reference list would occur roughly 181 ms after the
DOSBox-X sprite open on this adjusted clock comparison, versus 428 ms after
the local sprite open, leaving about 247 ms in the after-open portion. This
split is an inference across separate runs: packaged DOSBox 0.74 video,
DOSBox-X debugger trace, and local trace do not share a synchronized
clock/frame, and the origin offset is anchored at one file event. It is not a
measured causal decomposition. Keep the video-parity item open and capture a
reference frame at a known guest event before assigning the delay to palette
or painter work.
Do not change the default clock or add Reimp D96, which belongs to its
separately implemented UI.

Clock-sensitivity captures against the same DOSBox video put the list/details
residuals at 1.483/1.498 s with `--ips 4000000` (1,052 exact pictures; 274
reference and 262 local pictures unmatched), and 0.385/0.428 s with
`--ips 12000000` (1,317 exact pictures; 9 reference and 34 local pictures
unmatched). The default remains 9 MIPS: changing it also shifts other game
timing and does not establish a parity fix. These probes rule out screenshot
cadence as the main cause but do not isolate the START/emulated-machine
timing responsible for the roster-entry delay. The Reimp's D96 is an
additional wait for its separately implemented native UI; it does not by
itself explain or justify a wait in this translated START path.

## Observing flights through normal controls

With `F117R_BUILD_TESTS=ON`, `f117machine_api` exposes a scalar host API.
`tools/machine_api.py` wraps it: boot, advance to an absolute clock, read
guest memory, queue normal keys/mouse input, record input and capture a
picture. It exposes no memory or register setters. Only one machine may
be live per process because the core's code bitmap is process-global.
ROM-free tests check failed boots, lifetimes, queue limits, readback and
recorded-input replay against the existing headless runner.

`tools/landing_pilot.py` uses the original VGAME flight fields and the
Reimp's diagnostic pilot as a starting point. It flies with released key
pulses, intercepts the runway centreline, then descends, brakes and idles.
The committed `tools/routes/landing.input` contains only keyboard/mouse
events and their clocks. `landing.args` replays them without the controller.
Replay paths in route files resolve relative to the route file.

The Libya return takes about 829 emulated seconds. Interpreter and recomp
agree at all 195 checkpoints and final hash `9a20d983de7b7f4f`; independent
adaptive runs produce byte-identical input logs and observation CSVs.
The aircraft stops at (9793,1539), inside home target 33's box centred on
(9792,1600), with half-width 9 and half-length 72 map units. Speed/throttle
are zero, gear down, brakes on, fuel 4651, no ejection/crash state, and
the original completion counter is 2 with S=11 (greater than 16/S).
Screenshots of contact and the stop were reviewed.

The observer additionally checks the parent flight block: mission result
0 and pilot status 3. **DOS exit 129 is the debriefing handoff**, distinct
from that mission result; it also occurs after failed approaches. Route
milestones alone therefore do not establish a landing. Re-run the stronger
checks with either engine:

```powershell
py tools/landing_pilot.py --data "D:\GOG\F-117A" --engine interp --replay tools/routes/landing.input --out C:/landing-check
```

This proves the shipped training mission's return with its landing setting,
not objective completion, other runway types or physical-controller use.

## Reconnaissance objective acceptance

`tools/recon_pilot.py` drives the original transfer form to Libya, Cold War
and Strike Missions, then accepts the generated loadout and launches.
Cold War is transfer item 10 at (206,149). The flight uses normal released
stick pulses, slash for the forward display, F2 for ground mode, the bay
switch, N for forward-ray designation and Enter for the exposure.
`tools/routes/recon.front` retains the frontend inputs; `recon.input` is the
recorded whole flight and `recon.args` replays it without the controller.
The navigation/camera approach draws on the Reimp's diagnostic photo pilot.

Primary target 1 is photographed at about 345.5 emulated flight seconds,
268 map units away. The sampled credit state has one exposure, shutter 3,
the primary bit set and one event 8A naming target 1. Its damage bit is
clear, the aircraft has no ejection/crash state, and camera 16 retains
one store. The observer also checks these conditions after another second
of flight; the shutter has returned to zero without another exposure.
The credit screenshot shows FRAME 1 and PRIMARY PHOTO. The secondary
photo objective and the return remain unfinished; the route stops airborne.

Both engines agree at 123 checkpoints and final hash `eb19d05b7369a32b`
at 6,180,124,257 clocks. Independent observers pass under both engines.
As with landing, world/file milestones alone do not prove objective credit:

```powershell
py tools/recon_pilot.py --data "D:\GOG\F-117A" --engine interp --replay tools/routes/recon.input --out C:/recon-check
```

Four ROM-free regressions reject missing exposures/events, wrong objective
types, absent credit, destroyed targets, crashes and empty cameras.

## Extended photo return and earned career award

The extended `recon_return` route verifies secondary target 2 at 559.5 seconds
with FRAME 2 and one event 4Ah. It returns to raised base 36, lands at height
128 and stops within the home approach box, retaining 5009 fuel. Both engines'
independent observers produce identical input/flight logs and final hash
`c403d0542430b898`; the route also matches at 272 checkpoints. The complete
gate requires both intact photo targets and credit events, plus the successful
parent return result. Use `recon_pilot.py --complete --replay
tools/routes/recon_return.input` under either engine.

`recon_career` continues through debriefing and the earned tenth-mission tour
ribbon. Both engines agree at 316 checkpoints/final hash `f8e2e8955bb74467`;
all 802 saved bytes match. Save milestones independently require the earned
score, sortie count and ribbon.

`career_serge` selects Serge through the roster UI, completes both photos
and saves his second sortie (score 217, total 283, rank zero). The promotion
table requires total 300, average 100 and two sorties for the next rank;
meeting the average and sortie requirements alone is insufficient.
`career_promotion` declares `# seed-roster career_serge.args`: the runner
earns that prerequisite in a fresh process using the selected engine, checks
its milestones, then copies only the saved 802-byte roster unchanged into
another fresh save directory. Cyclic chains and failed prerequisites fail
before the next sortie. The pipeline and coverage runner use the same path.
The third sortie earns rank 1 and another Airman's Medal: total 500,
sorties 3, medal counter 1 -> 2. Its 354 checkpoints/final and all saved
bytes match between engines, ending at hash `2cbd245e873655d5` at 17.7 billion
clocks. The promotion and medal pages were reviewed. Retirement remains open.

`secret_airstrip` exercises the original type-4 objective at Persian Gulf
target 24. Its observer requires airborne history, ground contact and a
stop inside that strip's approach box, original 8Bh event, primary credit,
store consumption, fuel and no loss/damage. Both engines' observations and
167 checkpoints/final match. `airstrip_check.py` reads the startup clock
from the ordinary input record; it changes no guest state. This route ends
at primary completion with the aircraft still at the strip, rather than
claiming a return to the mission's home.

`airstrip_check.py --complete` additionally requires a second airborne leg,
retained original credit/store consumption, normal home stop/countdown,
successful parent result and VGAME exit 129. Damage at delivery rejects the
flight; later target damage is reported and preserves the original earned
credit. Completion sampling stays fine once home idle is reached: nearest
target changes transiently within the original frame's target search.
Only completion-counter changes trigger full reads during that fine phase.

`career_check.py` chains actual saved rosters without editing their bytes.
Each engine gets a fresh save directory. Strict photo/home acceptance,
normal END inputs, positive saved score, sortie increment, active/retired
status at record offset 4Eh, checkpoints and all 802 saved bytes must pass
before the next leg. The startup clock comes from the recording; promotion
may require a different recording for the new assignment.

The rank-3 photo career was paired through retirement: sorties 53-98 passed
at 380 observations each, and sortie 99 advanced the selected pilot from 98
to 99 sorties, score 207, total 20,303 to 20,510, and active to retired
status. Both engines matched on the retirement sortie's checkpoints and all
802 roster bytes. A private one-second END screen trace confirms the 99th-
mission retirement message and captures the rank-3 remark page. It shows
"Maybe I'll write a book, like Schwarzkopf. The lecture circuit sounds nice
and cushy." The rank-6 "General, At Last!" retirement branch has not been
reached; it needs a separate career averaging 280 points per sortie.

`cargo_check.py` separately observes a normal type-3 supply drop. It tracks
the released player class-26h slot and weapon 18, requires ground penetration
and matching impact globals with a sudden TTL change distinct from expiry,
then applies the original octagonal distance and deadline tests. It requires
an airborne aircraft and another second without primary credit, reproducing
bug D5. No guest memory or saved statistics are edited. Both engines agree
at 149 checkpoints/final and in all input/flight/report bytes. This is a
timely impact with original no-credit behavior, not a completed mission return.

The [independent parity audit](parity-audit.md) distinguishes engine equality
from DOSBox flight/save/sound evidence and documents the remaining differences.

## PIT control-word interrupts and re-recorded routes (5 October 2026)

DOSBox 0.74 raises IRQ0 when a control word reaches PIT counter 0 while its
output is low (`timer.cpp` `write_p43`). Its core takes a pending interrupt
at the next STI (`core_normal/prefix_none.h`), and the INT 21h service stub
begins with STI. START's teardown (`0x8CE6`-`0x8CFE`) writes control word
36h, reload 0, then INT 21h AH=25h to restore INT 8. Under DOSBox the game's
own timer handler (`0x8D0B`) therefore runs before the vector changes and
reloads the PIT from `[AE03]` (about 70 Hz, `0x8DC4`). The BIOS tick then
advances about four times faster, so START's four-tick palette loops
(`0x31B3`-`0x31EC`) take about a quarter as long. The machine now does the
same (`pit_control`, the STI-stub service entry, and no STI shadow inside
the service stubs). `F117R_PIT_CONTROL_IRQ=0` restores the old behaviour.

Against the saved GOG DOSBox capture, `tools/video_compare.py` reports
1,321 exact pictures and +14 ms end drift (it was -570 ms). The roster fade
now has three frames, as DOSBox's capture does, where it had thirteen.
`tools/fidelity.py` still agrees on all 1,210 answers.

Every recorded flight input carries absolute clocks, so the closed-loop
pilots re-recorded them under the new timing. Each passes its strong observer
under both engines with identical observation logs:

| route | observer | final clock / hash |
|---|---|---|
| landing | `landing_pilot.py --replay` | 9,778,413,433 / `1d4ca533f4697474` |
| strike | `strike_pilot.py --replay` | 8,813,613,420 / `4481029f711b1a3e` |
| strike_return | `strike_pilot.py --complete --replay` | 14,469,567,223 / `a2442935828c96cc` |
| recon | `recon_pilot.py --replay` | 7,549,654,902 / `bda4a4f85f274602` |
| recon_return | `recon_pilot.py --complete --replay` (level acquisition) | 14,974,000,296 / `5617058ceb0bd231` |
| cargo | `cargo_check.py` (original no-credit) | 7,182,189,465 / `ba4e2533e68c010d` |
| cargo_d5_fixed | `cargo_check.py --fix D5` (credit) | 7,182,189,465 / `4bc86c6604105145` |
| cargo_return | `cargo_check.py --complete` | 17,215,192,298 / `5e2d1782e45eb471` |
| secret_airstrip | `airstrip_check.py` | 8,283,782,540 / `3cfb9b7fdfba46c3` |
| secret_airstrip_return | `airstrip_check.py --complete` | 18,795,193,513 / `8e069648fc31c226` |

`recon_career` now earns 248 points (total 2917; the sortie count and tour
ribbon are unchanged). `career_serge` keeps every expected saved byte; its
roster click now selects Serge's row at (100,136), since (100,146) lands on
the "Press 'Delete'" line. `cargo_return` is hit again near home on the new
timeline and holds 260 knots on final. `career_promotion`'s
third sortie now generates a strike primary at the old startup clock, so it
starts at `700000001000000` (photo/photo, found with
`mission_candidates.py`); it still earns rank 1 and the second Airman's
Medal, scoring 208 (total 491 rather than 500). Hashes quoted in earlier sections
are from the previous timing.

## Matched routines (Phase 2)

A matched routine is hand-written C in `src/matched/matched.c` that does
what one of the original's routines does, written to be read, and is equal
to it: every register, flag, memory byte and the instruction clock, on every
path. It is a code override with `matched` set: always on, placed only for
the recompiled engine, never for the interpreter. Every route that compares
the two engines therefore compares the matched C with the original
instructions. `F117R_NO_MATCHED=1` leaves them all out.

The routine's arithmetic uses the interpreter's own semantics (`x86_sem.h`,
`x86_shift`), so flags cannot drift. A routine runs whole, so it declines
(and the original instructions run) when it would cross the run loop's next
look at events (`stop_at`); an interrupt due inside it still lands where it
would have.

`tests/func_lockstep.c` (CTest `func_lockstep`, needs the generated code)
holds each one to the original from random states: the module image in
memory, random registers, flags and memory, and a return address outside
the routine. The interpreter runs until it returns there, the matched C runs
once, and registers, segments, IP, flags, clock, ports, interrupts and all
memory are compared. A planted off-by-one clock and a changed constant were
both caught on the first state that reached them.

Half the states put small words (-1, 0, 1, or 0-31) where the arguments
sit, because random words almost never reach a routine's edge cases; an
error planted only on `sign16`'s zero path was caught on the 16th state.
The second half of each routine's states runs on memory of mostly 00, FF
and 01 bytes, so exact tests on memory (a -1 sentinel, a zero count) are
reached too; an error planted on the smoke trail's "no trail" path was
caught there and nowhere in the random half.

Matched so far, from the Reimp's names (`tools/reimp_names.py`), all VGAME:
0x04958 free fall, 0x0D50A waypoint from target, 0x0E289 orientation matrix
transpose, 0x0C863 sign, 0x0C699 clamp, 0x0BA2B "class takes a lock",
0x0EE0C absolute value, the 32-bit shifts 0x0EF68/0x0EF74/0x0F018, 0x0C67A
bar clamp, 104E:008A table sine, 0x0FFDC clipping outcode, the setters
0x0EE1A/0x0D9E7/0x04E4B, 0x01BA7 interrupt-vector read, and the string
helpers 0x0EDC2 strupr, 0x0EB82 strlen, 0x0EB50 strcpy, 0x0EDE0 block copy
and 0x0EDA4 far copy, the moving map's screen x/y 0x085F1/0x08608, the
text pen 0x0886A, weapon effectiveness 0x08A67, the 32-bit multiply
0x0EF36, the divide-error hook pair 120A:027D/029E, 0x0F79D and the axis
spread 1058:0C9F, the event-log append 0x04ABA, the altitude-alert reset
0x049F9, the scene-word decoder 0x0C436 and the key sign extension 0x0C845:
and the renderer's helpers (destroyed-type test 0x0B9F6, polygon row spans
130D:00B6/00D1, screen row offsets 1377:0116, span-table clear 0FB2:051F,
which reads its relocated segment constant from the loaded code, and the
clip-edge test 130D:064A, the 32-bit outcode 130D:0671, the banked row
tables 1377:0132/0155, the model fill's colour setup 1377:01E0, the camera
matrix row 1452:02AC - whose second carry goes into DX, as shipped - and
the plane-table clear 120A:0654), plus the terrain-under-unit test
0x0BA56, the key translation 0x0F0F4 (XLAT through 92A4), plane shading
120A:0674 and the clip polygon collector 130D:00EC; and from START and END:
the shared rectangle hit test (START 0x0393D, END 0x01C3B, one routine at
two addresses), START's clamp 0x059D8, LZW table reset 0x088D4 and cel
start 0x028CE, END's DAC request queue 0x0185C and octagonal distance
0x0452C, and END's copy of the C runtime's 32-bit multiply 0x0539C, which
reuses VGAME's matched version; then VGAME's clipped-edge publisher
130D:0217 and the C runtime's signed 32-bit divide 0x0EE9C (both paths:
two DIVs for a 16-bit divisor, the shift-down estimate and its one-step
correction for a wider one; a zero divisor is declined) and the smoke trail
0x048B8: 62 routines, each equal over 4,000 random states. Twenty more
addresses reuse them: START and END carry byte-identical copies of the C
runtime helpers (string length and copy, block and far copies, the 32-bit
shifts, multiply and divide, absolute value) and of the vector read and the
octagonal distance - code with no relocations or fixed data addresses, so
the same matched C serves every copy. Eleven more START routines follow:
two take the VGAME code with START's own data addresses (the masked sign
test, the word-pair setter), the rest are START's own (pair ordering, a
20-word record load, a byte select, memset and memcpy, a 2.14 fixed-point
multiply, and the formatter's two argument fetchers). END then reuses
START's (pair ordering, record load, byte select, LZW reset, memset, memcpy,
word-pair setter) at its own addresses and adds its report scaling, the
terrain under the replayed aircraft, the replay buffer refill and its
span-table clear; then the runtime copies in DSWAP, SETUP and PLAYER, and a
further round shared between programs (widget-state init, the DAC queue,
span clear, table sine and key translation at START's addresses, a byte
fill, a string table lookup, a record-chain walk and a palette-bank copy):
124 addresses in all. A routine that pushes before it reads must count its
clocks through what it is about to push: in random states the stack can sit
inside the data it walks, which a string lookup's count first missed by
nine clocks.
The original is run until the routine's own near RET (the first taken with
the stack at its entry level), and a state counts only if that RET reaches
the pushed address. Two looser rules failed first: in zeroed memory a
routine that clears its own stack returns to 0 and slides through `00 00`
instructions onto the return address with the stack level correct. On the strike route 38 of them run 13.9 million times in all and
the recompiled engine still reaches the interpreter's final hash.
REP string instructions are stepped as the interpreter steps them (one
clock an iteration, one for a REP that finds CX at 0), and a routine whose
clock depends on the data counts it first with a dry run so it can decline
before changing anything. States where the original writes over its own
code bytes are skipped: no equivalent can follow self-modification, and the
game never does it. The harness also caught a matched routine that
skipped a PUSH/POP pair: the original leaves BP's value in the stack word
below SP, and that word is part of the comparison. On the strike route the
recompiled engine runs them about 4.7 million times and reaches the
interpreter's final hash. Placing an override refuses the translated region
around its address, so nearby code is interpreted (755M to 815M
instructions on that route); the recompiler should isolate matched sites as
it does fix sites. A skipped state is one where the original does not
return within the budget, for instance when the random stack overlaps the
data a routine writes. `recomp_report` prints the call counts
(`[matched] ... ran N times`).



The optional Munt backend is isolated in `src/host/mt32.c` and enabled with
`F117R_WITH_MT32EMU`. It links the public C API, identifies the supplied
control/PCM pair and fails startup on invalid ROMs. Each MPU byte first
advances audio to its machine clock; complete messages enter Munt's MIDI
queue. Rendering uses bounded blocks at the existing 44,100 Hz output rate,
mixed with OPL/speaker before clipping. Queue rejection or oversized SysEx
fails the session/render rather than silently dropping traffic.
`f117run --midi-log` and `audio_render --mt32` provide MIDI capture and offline
rendering without changing the DOS program. Headless `--speaker-log` records
port 61h and PIT2 state at each speaker hook; `audio_render --speaker-log`
merges those events with OPL and MIDI events by guest clock for the same
OPL/speaker/Munt mix used by the host. ROM-free tests cover message
reassembly, real-time interleaving, running status, SysEx limits/recovery,
invalid ROMs and audio chunk invariance. Full ROM-based rendered Roland
parity remains open.

The supplied 1.07 ROM pair is now usable in both live and offline runs. The
interpreter and recompiler MIDI logs are byte-identical (188,505 bytes); a
matched replay emits the same 4,865-byte prefix, and two offline renders of
that stream produce byte-identical PCM. Live diagnostics accept
`--mt32-seed`, `--audio-dump` (internal stereo S16 PCM before SDL), and
`--audio-queue-log`; `audio_render` accepts `--seed` and `--step-clocks` for
offline runs. The latter controls the maximum guest-clock step between audio
advances; it defaults to `ips / 100` (10 ms at 9 MIPS).

Two 12-million-clock live replays with the same seed each rendered 58,800
internal frames, but those dumps first differ at frame 16,439 (0.373 s):
diff RMS 3.31, peak 48, correlation 0.999993. Thus seeding `rand()` does not
make the live synth path repeatable. Repeated offline renders with the same
seed remain byte-identical. In each short live run, the SDL disk capture has
3,664 leading frames (83.0 ms); after that offset, all 57,776 overlapping
frames match the internal dump byte for byte. Both queue logs have 82 updates,
no empty-before checks, no backlog clears and no discarded guest clocks.

A 30-second live replay rendered 1,323,000 internal frames and recorded
1,330,176 SDL frames. Its 1,802 queue updates include 60 checks with an empty
queue before new data was added; queued audio peaked at 70.29 ms. No update
cleared the queue above 250 ms, and no guest clocks were discarded. The fixed
83 ms capture offset stops matching at internal frame 270,601. Local PCM
windows then match exactly with a changing offset, and the raw capture has a
167-frame (3.79 ms) zero run near the first empty-queue check; later samples
resume 167 frames later. This accounts for the capture gap as brief SDL
playback starvation and timing drift. The same-seed internal variation occurs
before SDL receives PCM.

The variation is caused by block partitioning in the Munt renderer. In the
local Munt 2.8.3 source, `TVP::nextPitch()` draws from global `rand()` at each
simulated timer firing. `Synth::produceStreams()` renders partials serially,
and each partial calls `nextPitch()` as it renders samples. A different audio
advance boundary therefore assigns the seeded random sequence to different
partials. Two same-seed live queue logs begin with 126 and 124 produced frames
respectively, showing that host scheduling changes those boundaries. A fixed
offline quantum isolates the effect: at 9 MIPS over 270 million clocks,
two 9,000-clock (1 ms) seed-1 renders are byte-identical (SHA-256
`05cc06e4ae5c803d54bad8536845c79c549ef5004e03298df57ad2f38766f510`). A
90,000-clock (10 ms) render has SHA-256
`a51690955f052c1edf142eeb93545b3d39160488bb8cf4ff9883e647552cd9b8` and
matches the saved `interp-first30.wav` stereo PCM byte for byte. The 1 ms and
10 ms outputs first differ at frame 16,791 (0.381 s); their difference RMS is
756.52, peak is 19,471, and correlation is 0.5493. The same-seed live pair
first differs at frame 16,439 (0.373 s), with RMS 3.31, peak 48, and
correlation 0.999993. Seeding makes a fixed call sequence repeatable, but
does not make this Munt path independent of call boundaries.

The live `munt-final.wav` capture compared with the saved interpreter PCM has
about 0.707 envelope correlation, -0.013 waveform correlation, and 0.868
median spectral cosine after approximate alignment. These measurements mix
synth variation and the measured SDL gaps, so they are not an exact sound
parity verdict. The saved offline match establishes reproducibility for that
captured MIDI prefix; it is not an independent DOSBox or hardware reference.
Independent reference PCM and listening to the flight sound remain open.

For a completed generated type-8 sortie, a headless replay reached the same
final hash `6c3336ef1c24da17` while recording 70,849 OPL writes, no MIDI bytes
and six port 61h bit-0 toggles. Its speaker log contains 18 hook entries,
including PIT2 reprogramming; port 61h bit 1, the audible output gate, remains
clear in every entry. The replay first observed the aircraft airborne at
3,131,464,612 clocks. A speaker-aware offline render now covers the complete
sortie through clock 9,799,924,671 at
`C:/Users/Tideg/f117-recomp-local/munt-flight-audio-20261005/airair-type8-full-host-mix.wav`.
The 0–3.8-billion-clock prefix is byte-identical to the prior OPL-only render,
as expected because the speaker gate is off. The 12-second excerpt from about
flight seconds 29–41 remains at
`C:/Users/Tideg/f117-recomp-local/munt-flight-audio-20261005/airair-type8-flight-29-41s.wav`;
the smaller `airair-type8-flight-32-35s.mp3` is an excerpt for listening.
This is the complete host audio path for this sortie (OPL plus the silent
speaker channel), but no independent DOSBox flight reference or subjective
listening verdict has been recorded.

The open work, in order, is tracked in [roadmap.md](roadmap.md).
