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
src/machine/    the PC: DOS and BIOS (dos.c), devices (pc.c), mouse driver (mouse.c)
src/recomp/     the recompiled code's run-time (recomp.c) and the generated-code contract
src/host/       the window, audio and input (main.c), the headless runner (headless.c),
                presentation (present.c)
recompiler/     the translator: decoder, code discovery, C emitter (Python)
tools/          the pipeline (build_recomp.py), the unpacker, routes, font generation
tests/          the silicon-vector harnesses, for the interpreter and for generated code
third_party/    Nuked OPL3 (LGPL-2.1)
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
- **ISA I/O delay.** A port read costs `ips/1,000,000` extra clocks and a
  write `ips/1,333,000`: DOSBox's `IODELAY_READ_MICROS` 1.0 and
  `IODELAY_WRITE_MICROS` 0.75. Without it the logo's AdLib driver fails its
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

`src/machine/dos.c` began as the Reimp oracle's DOS: the loader, the
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
- **The OPL** is Nuked OPL3 in OPL2 mode, the most accurate emulator
  available. DOSBox 0.74 uses DBOPL; small timbre differences against a GOG
  DOSBox recording are expected.

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
4. **Every translated instruction, routes or not.** The routes run about
   half of the code area (`tools/exercised.py`: 47%; error paths, other
   theatres, most setup screens are not on them). `tests/insn_lockstep.c`
   covers the rest: for each of the 89,216 instruction starts the
   translation has, in every module, it places the module's image in
   memory, puts the machine in random states (registers, flags, segments on
   and off the module, every byte of memory outside the image) and runs one
   instruction through the machine's interpreter step and through the
   generated region entered at that instruction. Registers, segments, IP,
   flags, the clock, the interrupt shadow, every byte written, every port
   read and written and every interrupt raised (with the registers at that
   moment) are compared. At 64 states each, 5,709,312 comparisons: 0
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

## Where this goes next

The open work, in order, is tracked in [roadmap.md](roadmap.md).
