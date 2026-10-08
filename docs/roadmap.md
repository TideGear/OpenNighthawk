# Roadmap

What is done and what is left. The game's own bugs are in [bugs.md](bugs.md);
how parity is built and checked is in [architecture.md](architecture.md); the
commands, current figures and traps are in [../handoff.md](../handoff.md); the
scoreboard is `py tools/progress.py` ([progress.md](progress.md)). Update this
page with every piece of work, and keep it short: one entry per item, the
evidence linked or in the commit, not narrated here.

The order of the phases is the project's: **1:1 parity first** - the same
player actions playing out as on the original's hardware - then understood
(named) code, then fixes and enhancements - each switchable, never
instead of parity - and last the checks only people and outside references can
make (Phase 4).

## Phase 1 - recompiled, 1:1 with the original

### Done

- **Translation.** All 17 code files and the LZEXE decompressor: 89,281
  instruction starts, 96% of the code-area bytes (the rest is strings, tables
  and variables; `tools/census.py`). No game instruction is interpreted on the
  routes.
- **Engine parity.** 34 scripted routes are identical between the interpreter
  and the recompiled code at every 50-million-clock checkpoint and at the end;
  every translated instruction is held to the interpreter from random states
  (5,713,472 comparisons, 0 differences); the interpreter and the translator are
  held to 8088 and 80286 silicon vectors; 144 of 144 planted defects are
  caught; seeded random flights are identical. See
  [architecture.md](architecture.md#verification-why-11-is-a-claim-with-evidence).
- **What the routes cover.** All nine theatres, three tensions and four mission
  categories; all eight primary objective types with normal-input routes and
  strong observers (reconnaissance, ground strike, supply drop with the original
  D5 no-credit behaviour and `cargo_d5_fixed`, secret airstrip, air-to-air types
  5-8), with verified return legs for types 1-4; landing; the speaker and Roland
  driver boots; pilot creation, editing and erasure, KIA and retired dialogs and
  direct maintenance through the original UI; the earned tenth-mission tour
  ribbon, promotion and Airman's Medal; rank-3 career paired through retirement
  at sortie 99; the rank-6 "General, At Last!" retirement branch reached with a
  staged roster (`tools/career_rank6.py`; not an earned rank-6 career). Details
  per route: [../tools/routes/README.md](../tools/routes/README.md).
- **The emulated PC against GOG's DOSBox 0.74** (`tools/fidelity.py`): 1,210
  comparable answers agree. The model follows DOSBox where it was measured to:
  PSP and memory chain, the VGA's rational 70.086 Hz timing and four-part
  scanout, the `svga_s3` screen-off behaviour, the DAC tables, the PIT
  control-word interrupt that START's teardown triggers (the root cause of the
  old roster-entry delay: video drift went from -570 ms to +14 ms), and ISA I/O
  delay with its slice-boundary suppression. The mouse cursor is drawn into guest
  video memory, as DOSBox's driver does.
- **Pictures and music against GOG's DOSBox.** `tools/video_compare.py`: 1,329
  exact RGB pictures in order against the saved reference capture, three
  one-sample logo-transition images unmatched on each side, no end drift; a
  second independent capture matches 1,321 with 11 per side, so the remaining
  differences vary from capture to capture. Music: 22,687 AdLib register writes
  over the logo and intro, identical in order and value, timing within 36 ms
  (measured before the PIT and VGA-timing fixes).
- **Other machines as references** (`tools/pc_parity.py`): DOSBox-X (a patched
  source build that starts its own capture and runs windowless) matches 1,237
  exact pictures in order, drift up to 0.2 s; 86Box (a source build, windowless and
  silent, the 386DX/33 profile) matches 86 of 87 graphics pictures exactly in
  6-bit DAC values, in order (the other a single sample taken mid-draw). The same run checks the music (GOG envelope 0.963,
  spectral 0.992, level 1.005; DOSBox-X 0.90-0.92 and 0.947, its mixer 0.74 of
  the level; 86Box (a 386DX/33 profile): identical first 2,000 AdLib writes and notes on
  four of six channels, scene timing within 0.31 s per scene and 2.0 s drift
  over the intro) and a scripted START session's saved roster (byte-identical on
  DOSBox-X and 86Box); `tools/fidelity_all.py` holds the machine probe to
  stored baselines. Builds and notes: `tools/ref86box/`.
- **Independent GOG DOSBox flights.** Reconnaissance with both photos and the
  raised-deck return completes on GOG's DOSBox and passes the same strict
  gates. The supply mission is reproduced exactly on GOG's DOSBox (its autoexec
  consumes about 275 ms of guest time, so the seed is set 275 ms early); delivery
  there is open (below).
- **The game version verified:** GOG's installer (build 28044) with the 473.04
  update; Steam's release is byte-identical (`tools/verify_install.py`).
- **The gate** (`tools/build_recomp.py`): translate, build, coverage, both
  engines on every route, instruction and matched-routine lockstep; about 7
  minutes after a `matched.c`-only change. Repeated processes and what was optimised:
  [repeated-processes.md](repeated-processes.md).

### Open

- [ ] **A 386DX/33 timing profile** (decided 8 Oct 2026). The game steps its
      flight model by its own frame rate ([0x368E]) and reads the stick once a
      frame, so the same inputs play out a little differently on a faster or
      slower PC: this is what makes the recompilation play like the original on
      a period machine. This machine follows DOSBox's model (one clock an
      instruction plus I/O delays, 9.00 M clocks a second); the profile
      (`f117run --timing 386`, `F117R_TIMING=386`; src/cpu/timing386.c) charges
      what 86Box's 386DX/33 charges, from its source
      ([timing386.md](../tools/ref86box/timing386.md), `ops386.json`): each
      instruction's cycles and prefetch refills, the IBM VGA's 8-bit bus, ISA
      port costs, at 33.333 MHz. Results that do not depend on speed must match
      under both profiles; recorded routes and hashes stay on the DOSBox profile.
      Done: the interpreter matches 86Box to the cycle on all 77 blocks of
      `probe386.py` (ALU, memory forms, jumps, calls, INT, REP chunks with
      prefixes, VGA memory, ports), and the services it answers natively are
      charged their measured 86Box cost (video BIOS mode sets, palette, text;
      INT 16h, 1Ah; INT 21h AH=0Bh and 2Ch, FreeDOS's): 89 of 91 probe blocks
      exact. Frame comparison (`frames386.py`, timing386.md): over the intro's
      animation the scenes now hold 86Box's pace to a frame; the drift left
      (+1.48 s at START's roster screen) is in the loads: DOS file reads and
      writes, program starts and overlays. Open: those, the mouse driver (the
      reference VM loads none), the reference VM on MS-DOS 6.22 (the owner's
      choice, 8 Oct 2026: the last standalone MS-DOS; FreeDOS 1.3's costs stand
      in until it is measured), the recompiled engine charging the same costs.
- [ ] **Rendered sound:** the register stream is exact (every AdLib write of
      the intro in order and value, above), which is what the original sends to
      its chip. How the OPL waveform is synthesised is the emulator's, not the
      game's, so matching DOSBox's mixer sample for sample is not pursued.
      The speaker: the host runs PIT counter 2 itself from the machine's writes,
      clock by clock, as the 8254 data sheet has it (`src/host/speaker.c`), and
      integrates the cone's input over each sample. The speaker driver's music
      rewrites counter 2's count every 3.3 ms; taken at once, as before, that
      restarted the wave and mostly held the cone high. Speech under the speaker
      and Roland drivers is pulse-width modulation (counter 2 mode 0, a count of
      1-72 every 79 clocks, 15.1 kHz); `speaker = realsound` (the default, the
      owner's choice on 8 Oct 2026) averages each carrier period, `pwm` keeps the
      carrier. `tools/speaker_parity.py`
      against DOSBox-X: the intro's 736 gate changes and 440 control words
      identical, its audio envelope 0.87, spectral 0.92-0.93, level 0.95 (before:
      0.69, 0.54, 0.47); the radio call's 12,924 counts identical. Open: an
      audio reference for the speech (DOSBox-X ignores mode 0 counts written
      without a control word and plays nothing), the speaker's timing against
      86Box, and the AdLib speech's in-flight writes against a reference.

### Settled (scope, 8 Oct 2026)

Closed because what is left measures a reference emulator or the test tools,
not the recompilation:

- [x] **Frame parity with DOSBox.** 1,329 of 1,332 pictures exact; the rest
      are one-sample transitions whose frame varies with the capture's phase.
      Frame timing against a period PC is the 386 profile's job.
- [x] **86Box and DOSBox-X as routine checks.** `pc_parity.py` (pictures,
      music, saved data; 86Box 86 of 87 pictures exact in DAC values) and
      `fidelity_all.py` are the routine checks; the closed-loop supply drop,
      strike-training hit and career sortie pass on both (86Box runs
      deterministic; its adaptor sends the stick as whole game frames). The
      86Box intro drift is the VGA's 8-bit bus and the disk path
      (build_86box.md). The test pilot passes 7 of 9 over a +-15% range of its
      stick scale on 86Box: a property of the pilot, not of either machine.
- [x] **Timing details still DOSBox's own** (its BIOS and DOS stubs, its
      per-millisecond slicing, the 275 ms boot phase that lines the intro music
      up with GOG's capture, `--boot-ms`). They describe DOSBox, not the
      original; the 386 profile supersedes them. The game's PUSHF sites never
      read the 286/386 flag bits (14 real, two decoded data).
- [x] **Individual mission objectives.** All eight primary objective types
      have gate routes replayed byte-identically by both engines; the
      closed-loop pilots deliver on DOSBox-X and 86Box (GOG's DOSBox replay
      diverges open-loop and is not pursued); the strike-training credit window
      is the original's (80-150 map units, bugs.md D7).
- [x] **The career flow.** Promotion, medals, ribbons, retirement and the
      rank-6 "General, At Last!" branch are reached and paired (the last from a
      prepared roster: END's promotion code is the same however the roster was
      made). END's tables, read from its memory: totals 300, 1,125, 3,000,
      7,000, 16,000, 27,720; averages 100, 150, 200, 250, 280, 280; sorties 2,
      5, 10, 20, 40 and exactly 99; its score multipliers come from the
      pilot-skill form in the pilot record in memory (one sortie: 168 Green, 231
      Regular, 294 Veteran, 288 Regular with Realistic Landings). An earned
      career was flown to sortie 10 at an average of 290
      (`tools/career_chain.py`, `career_regular_realistic.front`) and stopped:
      flying 89 more scripted sorties would test the pilot, not the game.

## Phase 2 - understood code

- [ ] Name the translated routines and data, with explanations, drawing on the
      Reimp's mapping; matched functions replacing translations one at a time,
      each held to the same parity checks. `tools/reimp_names.py` joins the
      Reimp's census (1,535 functions) with this project's regions as a private
      lead list. 323 addresses are matched (322 of the census functions, 15,303 of
      179,213 bytes); the table is at the end of `src/matched/matched.c` and the
      method is in [architecture.md](architecture.md#matched-routines-phase-2).

## Phase 3 - fixes and enhancements (switchable)

- [x] **Code overrides** attach fixes: hand-written C registered for a module
      address, pinned to the shipped file by hash, off unless switched on
      (`--fix ID`, `--list-fixes`). Inert when off. Details:
      [architecture.md](architecture.md#the-run-time).
- [ ] **Fixes for the original's bugs** ([bugs.md](bugs.md)), each on a switch.
      Done: D1 (a frame limiter at VGAME 0x441D holds a fast machine to GOG's
      11.6 frames a second; `tools/d1_check.py`, route `d1_fast_machine`), D2
      (speech played from the machine's schedule instead of a busy-wait), D4
      (secret airstrips), D5 (supply drops earn credit; `cargo_d5_fixed`), D11
      (saves written to a temporary file and renamed), D12 (END's signed
      rating), D34 (destroyed-object table), D8 (the laser-guided bomb's pitch
      clamp; also what makes D7's training strike hard to complete), D6
      (a detection cover of 0 reads as 4; optional, since the cells are not
      proven a defect). D7 is fixed by D8. Open: D3 (keypad digits with
      NumLock off return to DOS from the sound prompt; the exit is not located),
      D10 (the mountain-collision stack corruption, DOS equivalent not
      located), D35 (not reproduced here; no fix offered) and D36 (later MT-32 ROMs, see
      the README's ROM note). D96 is the Reimp's
      native-UI wait, not a patch for this translated START. A session recorded
      with fixes on names them in its log and replays with them.
- [ ] **60+ fps and 4K presentation**, design and status in
      [presentation.md](presentation.md). The owner chose to observe and replay
      the original's draw path. Stage 0 (the observer) and Stage 1 (draw lists
      that rebuild every phase of the windows tried bit for bit, 0 bytes copied)
      are built; Stage 2's first build (`tools/hires_frame.py`) shows the scaled
      edge walk cracks along shared polygon edges (85% flat-pixel agreement; 100%
      with the nearest-neighbour floor, which is no sharper), so the next step is
      the sub-pixel re-projection of the model polygons; Stages 3 (interpolation) and 4 (pacing and polish) are
      not started.

## Phase 4 - checked by people and independent references

What a machine cannot judge by itself: it needs a person, or a reference that
this project did not build.

- [ ] **A person playing it:** controls, joystick and mouse, saves, the feel.
      Sessions already record themselves for replay.
- [ ] **Roland through Munt** (libmt32emu): the MT-32 music plays in the game
      and renders offline from a MIDI log; the interpreter's and recompiler's
      MIDI logs are byte-identical and offline renders are repeatable. Live
      renders vary with audio block boundaries (a Munt random draw) and show
      brief SDL starvation, so live output is not a stable PCM oracle. Open:
      independent reference PCM and a listening check.

## Housekeeping

- [x] `src/machine/dos.c` split by service (`dos_memory.c`, `dos_programs.c`,
      `dos_files.c`, `dos_keyboard.c`, `dos_video.c`), the mouse driver in
      `mouse.c`; routes agreed before and after at every checkpoint.
- [x] Build-from-scratch steps in the README, tested on a fresh public clone.
- [x] `.gitattributes` for line endings.
- [x] An automated Windows build on GitHub with ROM-free CPU, machine and
      comparator tests and the route milestone tests.
- [x] Docs kept as reference, not diaries: session history lives in git, one
      place per fact ([repeated-processes.md](repeated-processes.md) tracks
      every repeated process).
