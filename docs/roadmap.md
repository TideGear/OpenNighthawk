# Roadmap

What is done and what is left. The game's own bugs are in [bugs.md](bugs.md);
how parity is built and checked is in [architecture.md](architecture.md); the
commands, current figures and traps are in [../handoff.md](../handoff.md); the
scoreboard is `py tools/progress.py` ([progress.md](progress.md)). Update this
page with every piece of work, and keep it short: one entry per item, the
evidence linked or in the commit, not narrated here.

The order of the phases is the project's: **1:1 parity first**, then
understood (named) code, then fixes and enhancements - each switchable, never
instead of parity.

## Phase 1 - recompiled, 1:1 with the original

### Done

- **Translation.** All 17 code files and the LZEXE decompressor: 89,281
  instruction starts, 96% of the code-area bytes (the rest is strings, tables
  and variables; `tools/census.py`). No game instruction is interpreted on the
  routes.
- **Engine parity.** 32 scripted routes are identical between the interpreter
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
  exact pictures in order, drift up to 0.2 s; 86Box (a source build with its VNC
  renderer, windowless and silent) shows the same scenes, order and colours,
  its VM being a 6 MHz 286. The same run checks the music (GOG envelope 0.963,
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
  engines on every route, instruction and matched-routine lockstep; about 23
  minutes. Repeated processes and what was optimised:
  [repeated-processes.md](repeated-processes.md).

### Open

- [ ] **Exact frame parity with DOSBox.** The remaining unmatched pictures are
      partial logo and aircraft transitions whose exact frame varies with the
      capture's phase. A frame capture synchronised to a guest event would
      settle it.
- [ ] **A person playing it:** controls, joystick and mouse, saves, the feel.
      Sessions already record themselves for replay.
- [ ] **86Box and DOSBox-X as routine checks.** `tools/pc_parity.py` is the
      routine PC-parity check (pictures, music and saved data, against limits
      measured on 6 Oct 2026) and `tools/fidelity_all.py` the machine probe.
      Open: script flight and more saved data (a career) on both; find what
      paces the remaining 2 s of intro drift on 86Box (the disk interface) and
      the pitch-bend channels' different write positions; then decide whether DOSBox-X plus 86Box replace GOG's
      DOSBox for routine checks (the saved GOG capture stays as a frozen
      regression reference). Parity target: the original on real hardware, not
      DOSBox.
- [ ] **Roland through Munt** (libmt32emu): the MT-32 music plays in the game
      and renders offline from a MIDI log; the interpreter's and recompiler's
      MIDI logs are byte-identical and offline renders are repeatable. Live
      renders vary with audio block boundaries (a Munt random draw) and show
      brief SDL starvation, so live output is not a stable PCM oracle. Open:
      independent reference PCM and a listening check.
- [ ] **Rendered sound parity:** the OPL defaults to GOG DOSBox's DBOPL core at
      its rate and 2x gain. A tone probe matches exactly; the captured intro
      still differs in RMS by about 0.5%, and a flight comparison against a GOG
      mixer WAV is approximate (envelope correlation 0.60). Register timing, mixer
      block scheduling, digitised speech, audible speaker output and a listening
      check remain.
- [ ] **Timing details still DOSBox's own:** its BIOS and DOS stubs at their own
      addresses, its per-millisecond slicing, its 386 against this machine's 286
      (flag bits 12-14). PLAYER's fade calibration is within a few bytes of
      DOSBox's (5,836-5,841 against 5,839-5,844 DAC bytes per display period); the
      channel-3 note at 29.7 s, re-measured on 7 Oct after the PIT and VGA fixes
      against the saved GOG capture: 596 AdLib writes identical in order and
      value over 29.7 s with timing within 26 ms, then the pitch-glide write
      there takes a different value (A3=92 here, 49 there, 11 ms apart). 86Box
      (386DX/33) differs at the same note, so the glide is paced by the loop and
      its sample instant depends on small clock phase; not yet traced to a
      cause.
- [ ] **Individual mission objectives:** independent GOG DOSBox cargo delivery
      needs a closed-loop pilot (open-loop replay diverges across machines);
      wider generated-assignment coverage; the strike-training mission (bugs.md
      D7) is unresolved (the recon pilot never designated its target).
- [ ] **The remaining career flow:** an earned rank-6 career (the branch is
      reached only with a staged roster).

## Phase 2 - understood code

- [ ] Name the translated routines and data, with explanations, drawing on the
      Reimp's mapping; matched functions replacing translations one at a time,
      each held to the same parity checks. `tools/reimp_names.py` joins the
      Reimp's census (1,535 functions) with this project's regions as a private
      lead list. 246 addresses are matched (245 of the census functions, 10,822 of
      179,213 bytes); the table is at the end of `src/matched/matched.c` and the
      method is in [architecture.md](architecture.md#matched-routines-phase-2).
      The remaining small routines are mostly far-segment or do port I/O, which
      the lockstep cannot hold, so they are held by the routes only. Progress
      is measured in census bytes ([progress.md](progress.md)).

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
      rating), D34 (destroyed-object table). Open: D6 (cause not proven), D7
      (not located), D8 (needs a laser-guided-bomb route). D96 is the Reimp's
      native-UI wait, not a patch for this translated START. A session recorded
      with fixes on names them in its log and replays with them.
- [ ] **60+ fps and 4K presentation**, design and status in
      [presentation.md](presentation.md). The owner chose to observe and replay
      the original's draw path. Stage 0 (the observer) and Stage 1 (draw lists
      that rebuild every phase of the windows tried bit for bit, 0 bytes copied)
      are built; Stage 2's first check shows the clip stage must be redone at
      the new resolution; Stages 3 (interpolation) and 4 (pacing and polish) are
      not started.

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
