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

- **Translation.** All 17 code files and the LZEXE decompressor: 89,366
  instruction starts, 96% of the code-area bytes (the rest is strings, tables
  and variables; `tools/census.py`). The coverage pass adds no further
  translated code; changed code and declined override entries can fall back
  to the interpreter.
- **Engine parity.** 35 scripted routes are identical between the interpreter
  and the recompiled code at every 50-million-clock checkpoint and at the end;
  every translated instruction is held to the interpreter from random states
  (5,718,912 comparisons, 0 differences); the interpreter and the translator are
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
  silent, the 386DX/33 with MS-DOS 5.00 and MOUSE.COM) matches 85 of 86 graphics
  pictures exactly in 6-bit DAC values, in order (the other a single sample of
  START's roster screen half drawn while its files load). The same run checks the music (GOG envelope 0.963,
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
  The gate explicitly enables the app build so its host tests are included
  even after a core-only build.
  A retained rotating-seed regression checks the in-place 32-bit shift's
  stores through live BX after the original helper returns, including a
  stack overlapping that helper's code (`matched_shift_stack_alias`).

- **Rendered speaker and speech audio.** PIT counter 2 follows the 8254 model;
  speaker music matches the DOSBox-X reference (envelope 0.87, spectral
  0.92-0.93, level 0.95). The missing speech waveform reference is now captured
  silently from 86Box's mixer on MS-DOS 5.00/MOUSE.COM 6.26 (9 Oct): the
  takeoff call's 12,924 non-silence counts are identical, over 855.626 ms;
  in 300-3400 Hz waveform correlation is -0.9916, envelope 0.9995, zero lag.
  Opposite polarity and 3.17x level follow the independent speaker models'
  amplitude formulas; no renderer/default change was needed. Capture leaves
  all 4,500 frame records and port writes byte-identical. Commands and limits:
  [build_86box.md](../tools/ref86box/build_86box.md#silent-speech-waveform-reference).
  Listening remains Phase 4; OPL mixer sample equality remains out of scope.

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
      INT 16h, 1Ah; INT 21h AH=0Bh and 2Ch; INT 33h): all 93 probe blocks
      exact in the latest run against the saved MS-DOS 5.00 reference with
      Microsoft MOUSE.COM 6.26 (AH=2Ch varies by four cycles between runs)
      (`build_msdos_vm.py`; 5.00 and 6.22 cost the same on the game's calls,
      4.01 10% less, FreeDOS twice on 2Ch; CuteMouse 45% cheaper than 6.26).
      Shown: the game's frame rate in flight (S) is 5-9 under the profile and
      7-9 on 86Box, against 14-15 on DOSBox's model, so the same inputs now fly
      at the period PC's pace (`stick_response.py --machine machine386`); over
      the intro's animation the scenes hold 86Box's pace to a frame
      (`frames386.py`, timing386.md). DOS file services are now charged
      (open, read, write, close, attributes, seek; timing386.md "File
      services"), fitted to the 1989 3500 rpm drive preset, the reference's
      period drive (9 Oct 2026; the RAM-disk preset's fit is recorded there too).
      EXEC and overlay loads are now charged too (9 Oct 2026; by which of three
      measured sizes - 1 KB, 9.5 KB (PLAYER.EXE's), 47 KB (START.EXE's) - a
      load is closest to, not a per-byte formula: a fifteen-size follow-up
      probe came back non-monotonic in size, confirming the cost is seek-
      distance dominated like an ordinary read, not size dominated). On that
      reference the intro's drift at the end falls from +0.43 s to +0.10 s,
      the same 1,237 of 1,275 pictures exact. Reference parity: pictures,
      music and the picture check now all pass (p037's one close picture is a
      confirmed mid-fade capture-timing artifact, not a defect); one 86Box
      check still fails, the longest scene 0.02 s over its 0.35 s limit,
      unmoved by the EXEC/overlay charge. Both engines now charge the same profile: 5,713,152 instruction
      comparisons each in RAM and with code in VRAM, no differences; paired
      flight sessions have 574 identical state checkpoints and stick responses
      (`stick_response.py --machine machine386 --engine interp|recomp`).
      Matched entries use the original translated body under this profile.
      STI and SS loads hold interrupts through the following instruction even
      when it costs zero cycles; four regressions failed before the fix and
      pass after it. Regions are capped at 1,024 instructions to bound MSVC's
      optimisation work (generated rebuild 479 s to 120 s).

### Settled (scope, 8 Oct 2026)

Closed because what is left measures a reference emulator or the test tools,
not the recompilation:

- [x] **Frame parity with DOSBox.** 1,329 of 1,332 pictures exact; the rest
      are one-sample transitions whose frame varies with the capture's phase.
      Frame timing against a period PC is the 386 profile's job.
- [x] **86Box and DOSBox-X as routine checks.** `pc_parity.py` (pictures,
      music, saved data; 86Box 85 of 86 pictures exact in DAC values) and
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
      lead list. 765 addresses are matched (760 of the census functions, 63,917 of
      179,213 bytes); the table is at the end of `src/matched/matched.c` and the
      method is in [architecture.md](architecture.md#matched-routines-phase-2).
      The batches of 8-9 Oct add 37 + 34 + 47 VGAME, 34 + 40 + 45 START/END and 21 + 44 + 64
      small-program routines, each checked at two 4,000-state seeds (the 9 Oct
      VGAME batch also at four more), and the picture decoder's
      RLE row and LZW step in VGAME and END (the step is routes-only: its
      prefix walk checked room once for a chain of any depth, now every turn).
      Three unequal candidates were excluded: START 0x11DB, MPS_LOGO 0x1ADC
      and 0x1C82.
      Follow-up gate repairs (9 Oct): the lockstep now includes VGAME
      1377:00F3's loop and RET below its entry; START/END's shadow-text
      helper preserves PUSH SS's memory write when its source aliases the
      stack, with seed `0xf2a7d2c1bf36` retained as a CTest regression.
      VGAME's weapon-lock marker (`0x0B171`) now names the searching box and
      locked hexagon geometry, preserving the small-HUD argument write and
      the original stack reads for every line call. Its random-state check
      substitutes returning driver thunks on both sides, so the active
      drawing branches return too; the routes use the real driver.
      The cockpit canopy (`0x0D6DD`) now names the compact side frames, shaded
      posts and landing-approach cue, including clearing the old cue before
      painting the new one and preserving the original page-copy arguments.
      Projectile motion and gunfire refill (`0x04777`) now names the twelve-byte
      slots, their frame-rate-dependent walk, signed round-robin shot selection,
      ammunition cost and launch velocity; every loop iteration checks event room.

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
      proven a defect), D3 (keypad digits with NumLock off answer SETUP's
      sound question instead of quitting to DOS). D7 is fixed by D8. Open:
      D10 (the mountain-collision stack corruption, DOS equivalent not
      located; no VGAME routine returns with the stack moved,
      `tools/stack_balance.py`), D35 (not reproduced here; no fix offered) and D36 (not
      reproduced: on 2.0x control ROMs Munt plays the engine 3.4 dB quieter,
      not silent; no fix offered). D96 is the Reimp's
      native-UI wait, not a patch for this translated START. A session recorded
      with fixes on names them in its log and replays with them.
      The [D1 follow-up](speed-sweep.md#d1-follow-up-9-october-2026) adds 90
      flights: no oscillation, D1 inactive hash-for-hash at 9 MIPS, but its
      fast-machine 11.6 fps cap gives a 1.2882x mission clock rather than
      real time. The default speed is now 20 million instructions a second
      with no limiter (9 Oct 2026; [speed-sweep.md](speed-sweep.md#fast-machine-default-9-october-2026)):
      the game's own clamp holds S at 15 and the drawn rate near 16.7, with no
      swings in 214 flights. A follow-up controlled threat-profile measurement
      (10 Oct 2026, [speed-sweep.md](speed-sweep.md#controlled-threat-profile-combat-measurement-9-10-oct-2026)),
      orbiting each flight's primary target instead of flying past it once,
      found a real fall in the enemy launch rate from 9 to 16 to 20 MIPS (9.08,
      6.48, 3.81 launches a flight; bootstrap 95% intervals exclude zero for
      both differences), surviving normalisation for exposure time. Not yet
      explained - the mechanism in the game's listing is the open item, not a
      further sweep. Control-response tooling now accepts speed, fixes,
      program-relative menus and boot-clock overrides and retains early exits.
      Ten control flights now share mission identifiers after boot-clock
      alignment; their progressive input sequences still diverge, so they
      do not establish a safe speed.
      An [isolated 15 FPS experiment](speed-sweep.md#actual-15-fps-limiter-experiment-9-october-2026)
      settles at S 12 with a 1.25x mission clock: the proposed cap does not
      restore real-time pacing. Production D1 is unchanged, and no limiter is
      shipped: the default is the game's own cap at 20 MIPS (above).
- [ ] **60+ fps and 4K presentation**, design and status in
      [presentation.md](presentation.md). The owner chose to observe and replay
      the original's draw path. Stage 0 (the observer) and Stage 1 (draw lists
      that rebuild every phase of the windows tried bit for bit, 0 bytes copied)
      are built. Stage 2 (8 Oct 2026, `tools/hires_subpixel.py`): the model
      polygons re-projected from their camera-space vertices and filled with a
      tiling rule are equal to Stage 1 at N=1, agree on 99.96-100% of flat
      pixels at N=2 and 4 and show 1.5-5.4 times the nearest-neighbour floor's
      change (real edge detail); text, sprites, the HUD and the cockpit art stay
      scaled copies, and 3-6% of polygons (near-clipped, horizon slivers) too.
      Stage 3 (`tools/interp_frame.py`, a 320x200 study): the replay is exact at
      both logic frames, 86-98% of model polygons pair between steps and 1 of 182
      flight steps was a held cut; at 320x200 the in-between frames change under
      1% of pixels, so the gain comes on Stage 2's grid. The live path (9 Oct
      2026, `--present replay|interp`, `present =` in f117a.ini, off by
      default): the observer hands each logic frame's records to the host in
      memory and C ports of the replay and the interpolation draw from them;
      on the strike flight all 9,845 logic frames equal the display at their
      close and all 9,843 pairs are exact at both ends, hashes unchanged; it
      costs 35% (replay) and 54% (interp) of the recompiled run. In-between
      frames also run on Stage 2's grid (`--present-scale N`): all 9,843
      strike pairs match at both coarse and fine endpoints at N = 1/2/4/9,
      with guest hashes unchanged. Stage 4 host pacing, vsync, picture-age
      selection and the original-picture switch are built; dummy-driver
      measurements are in [presentation.md](presentation.md#stage-4-pacing-and-the-pictures-age-9-october-2026).
      Open: the HUD/text source at 4K (the owner's call), near-clipped
      polygons still scaled, and a real-display check of refresh pacing,
      motion and 4K output (Phase 4).

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
