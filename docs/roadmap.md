# Roadmap

What is done, what is in progress, and what is left. The game's own bugs
are tracked separately in [bugs.md](bugs.md); how parity is checked is in
[architecture.md](architecture.md). Updated with every piece of work.

The order of the phases is the project's: **1:1 parity first**, then
understood (named) code, then fixes and enhancements - each switchable,
never instead of parity.

## Phase 1 - recompiled, 1:1 with the original

### Done

- All 17 code files and the LZEXE decompressor translated: 89,216
  instructions, 96.2% of the code-area bytes; the rest is strings, tables
  and variables (`tools/census.py`). On every route and the longest random
  flight the recompiled build interprets no game instruction.
- Every translated instruction held to the interpreter from random states
  (`tests/insn_lockstep.c`, 5.7 million comparisons, 0 differences); the
  translator held to 8088 and 80286 silicon vectors.
- Six scripted routes identical between the interpreter and the recompiled
  code: boot to flight; a full sortie through the debriefing; the speaker
  and Roland drivers; Korea (strike, carrier start); Vietnam (air-to-air,
  runway). The routes execute 52.3% of the code area (`tools/exercised.py`).
- Seeded random flights (`tools/random_flights.py`): every session
  identical at every hash; 30.5 minutes of flight in the 16-session batch.
- Planted defects caught by the lockstep (`tools/mutation_check.py
  --lockstep`): 12 of 12, 72 of 72, 60 of 60 - 144 of 144, of all four
  kinds (removed instruction, flipped CF, ZF, AX bit), most at
  instructions no route runs.
- The emulated PC held to GOG's DOSBox 0.74-2.1 (`tools/fidelity.py`): DOS
  memory, PSP, environment, EXEC and terminate, every DOS/BIOS service the
  game uses and its cost in time, BIOS data, VGA registers, devices,
  clocks. All comparable answers agree. DOSBox-X as a second reference.
- The game's music against GOG's DOSBox (`tools/dosbox_compare.py`): the
  logo and intro, 22,687 AdLib writes over 100.7 s, identical, timing
  within 36 ms.
- The game version verified: GOG's installer (build 28044) with the 473.04
  update; Steam's release byte-identical (`tools/verify_install.py`).
- Mouse driver cursor readback agrees with DOSBox: the cursor is now drawn
  into guest video memory and saves/restores its background, rather than
  being a host overlay. Eight readback probes agree; ROM-free clipping,
  mode-change and text cursor tests pass. All six routes still agree at
  329 checkpoints and final states, plus the 64-state instruction lockstep.
- VGA DAC ports and BIOS palette calls share the same device state and I/O
  costs. Read-address writes update the write index; BIOS blocks wrap the
  palette index and use linear buffers across segment boundaries. All
  1,210 fidelity answers agree; ROM-free DAC tests and six-route parity pass.

### In progress

- **The picture against DOSBox:** `tools/video_compare.py` now captures
  the hands-off logo, intro and roster as ZMBV, decodes every 320x200 frame
  with ffmpeg and compares exact RGB pictures in order against `--shots`.
  Two 130.8-second captures (9,165 frames each) match 1,217 distinct
  consecutive pictures. Differences remain: 115 / 109 unmatched DOSBox
  pictures, 89 here within the capture's time range. Most last one sample;
  DOSBox reads four parts of a frame at different times, while shots read
  all VRAM at once. The roster's mouse cursor also changes colour here
  around 117.5 and 129.1 s, absent in both reference captures. Diagnostic
  pairs differ in up to 78 pixels inside the cursor's 10x15 area; the pilot
  names match. START draws this pointer itself; correcting the INT 33h
  driver did not change it. A third run matches the same 1,217 pictures,
  with 113 unmatched DOSBox pictures and 89 shots. Investigate the game's
  palette handling and roster-entry delay before claiming frame parity.
  Replaying the saved reference against the BIOS DAC correction leaves
  these counts and picture timings unchanged. The full music rerun diverges
  at the known random channel-3 note at 29.7 s; it is not a new full-stream
  pass (the prefix matches 596 writes, with up to 55 ms timing drift).
  Music differences now exit 1. Four ROM-free comparator regressions pass;
  short fresh captures match 365 writes over 15.5 s and 300 over 10.4 s.
  **Sampling diagnosis:** the apparent roster cursor colour changes were
  background pixels exposed during START's pointer erase/redraw, sampled
  by the 70 Hz screenshot clock beating against 70.086 Hz retrace (about
  11.6 s). Sampling every 128,413 clocks, matching the runtime VGA period,
  removes all unmatched roster pictures after 110 s and all multi-sample
  differences. Saved-reference run `intro-ek_ltnum`: 1,219 exact pictures,
  107 unmatched reference pictures and 74 shots, each one sample. The
  transition delay (~0.57 s) and scanout differences remain unresolved.
  **Four-part scanout implemented:** completed frames now retain the four
  groups read during a frame and the displayed palette. Saved-reference
  run `intro-p6mrjdbw` matches 1,319 pictures; seven reference pictures and
  28 shots remain unmatched, each one sample. The roster matches after
  110 s; transition delay remains ~0.57 s. ROM-free temporal scanout,
  address-latch and DAC publication/alias tests pass.
  All 1,210 machine probes and six routes at 329 checkpoints plus final
  states agree; the 5.7-million-state instruction lockstep has no mismatches.

### Left

- [ ] **The picture against DOSBox, frame by frame.** DOSBox's video capture
      (ZMBV) comparison now runs; match scanout sampling and resolve the
      transition timing differences (see In progress). The apparent roster
      cursor changes were diagnosed as screenshot sampling aliasing.
- [ ] **Roland through Munt** (libmt32emu, LGPL-2.1+): the MT-32 music in
      the game itself, and its output rendered and checked automatically.
      Users supply their own MT-32 ROMs.
- [ ] **A landing route:** fly back and land. No route or random flight has
      landed yet.
- [ ] **The other theatres and mission types:** Central America, North Cape,
      Central Europe, Libya, the Middle East, the Persian Gulf (Korea,
      Vietnam and the default theatre are flown).
- [ ] **The rest of the front end:** creating and retiring pilots, the CO's
      office, maintenance, awards, saving and loading a roster.
- [ ] **A person playing it:** controls, joystick and mouse, saves, the feel.
      Sessions record themselves for replay.
- [ ] **Timing details still DOSBox's own:** its stub code at its own
      addresses (vectors F000:xxxx), its per-millisecond slicing, its 386
      against this machine's 286 (flag bits 12-14).
      `tools/fade_calibration.py` now measures the original calibrators:
      START/END's range agrees (seven/eight steps), with different phases;
      PLAYER measures 5,839-5,844 DAC bytes per display period on DOSBox
      versus 5,811 here (three repeat trials). Throughput remains unresolved.
      Matching DOSBox's near-slice-end I/O delay suppression brings the
      runtime to 5,836-5,841 bytes per period; all 1,210 machine probes agree.
      All six routes agree at 329 checkpoints and final states; the
      5.7-million-state instruction lockstep reports zero differences.
      Exact phases and the remaining few-byte difference are still open.
- [ ] **A sound mismatch to explain or accept:** the OPL emulator is Nuked
      OPL3, DOSBox's is its own; the register stream is identical, the
      synthesis is not compared.

## Phase 2 - understood code

- [ ] Name the translated routines and data, with explanations, drawing on
      the Reimp's mapping; matched functions replacing translations one at a
      time, each held to the same parity checks.

## Phase 3 - fixes and enhancements (switchable)

- [ ] Code overrides: hand-written C registered for a module address,
      replacing that address's translation - how the fixes attach.
- [ ] Fixes for the original's bugs in [bugs.md](bugs.md) (D1 frame-rate
      AI, D6 undetectable cells, ...), each on a switch.
- [ ] 60+ fps and 4K presentation.

## Housekeeping

- [ ] Split `src/machine/dos.c` (about 2,300 lines) into memory, programs,
      files, keyboard and video. The mouse driver is now in `mouse.c`.
- [ ] Build-from-scratch steps in the README, tested on a fresh clone.
- [x] `.gitattributes` for line endings: LF text, CRLF Windows batch scripts,
      binary assets excluded from conversion. Existing index text is LF.
- [x] An automated full Windows interpreter build on GitHub, with four
      ROM-free CPU/machine CTests and nine comparator tests. First run
      [passed](https://github.com/TideGear/OpenNighthawk/actions/runs/37192954631).
