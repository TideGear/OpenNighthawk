# Roadmap

What is done, what is in progress, and what is left. The game's own bugs
are tracked separately in [bugs.md](bugs.md); how parity is checked is in
[architecture.md](architecture.md). Updated with every piece of work.

The order of the phases is the project's: **1:1 parity first**, then
understood (named) code, then fixes and enhancements - each switchable,
never instead of parity.

## Phase 1 - recompiled, 1:1 with the original

### Done

- [Parity audit](parity-audit.md): sixteen routes and the full instruction
  lockstep pass again; independent DOSBox roster saves match all 802 bytes.
  Overlapping input, extended keys and emulated joystick extremes pass.
  Host stall pacing and the missing 2x AdLib mixer gain were found and fixed.
  GOG's DBOPL synthesizer is now the default, with Nuked selectable; both
  use the reference's 44,100 Hz rate. Chip and host audio tests pass, while
  exact reference PCM agreement remains open.

- All 17 code files and the LZEXE decompressor translated: 89,276
  instructions, 96.2% of the code-area bytes; the rest is strings, tables
  and variables (`tools/census.py`). On every route and the longest random
  flight the recompiled build interprets no game instruction.
- Every translated instruction held to the interpreter from random states
  (`tests/insn_lockstep.c`, 5.7 million comparisons, 0 differences); the
  translator held to 8088 and 80286 silicon vectors.
- Twenty-four scripted routes identical between the interpreter and the recompiled
  code: boot to flight; a full sortie through the debriefing; the speaker
  and Roland drivers; eight transfer-and-flight routes covering all nine
  theatres, three tensions and four mission categories. The routes execute
  at least 60.6% of the code area (`tools/exercised.py`, including the new
  promotion routes). Theatre routes also require
  the expected world files, a clean VGAME exit and at least a billion clocks
  in flight; airborne screenshots were reviewed for the six added routes.
- Pilot creation, Backspace editing, Escape cancellation and erasure through
  the original roster UI: the new route matches at 38 checkpoints/final.
  Saved names and cleared career counters are checked independently; both
  802-byte rosters match. CHECK survives a fresh-process reload. Recorded
  mouse/key input replays identically under the interpreter. Pipeline and
  coverage replays now start with fresh saves, preserving previous outputs.
- KIA/retired roster dialogs and direct maintenance navigation before and
  after briefing: screens reviewed, AIM-9/AMRAAM station changes visible,
  both engines identical at 64 checkpoints/final. Required screen-open
  counts reject routes stuck in dialogs or missing a maintenance visit.
- A normal-input Libya landing route: takeoff, return to base 33, runway
  alignment, contact and a stop inside its approach box. Both engines
  agree at 195 checkpoints/final; adaptive runs also produce identical
  input logs and flight observations. The stronger observer checks zero
  speed/throttle, gear down, brakes on, fuel remaining, no ejection/crash,
  the completed countdown and the parent's mission result 0/status 3.
  DOS exit 129 is a separate debriefing handoff, not a landing result.
  Contact/stop screenshots reviewed. This covers the shipped training
  mission and its landing setting; other runways and objectives remain.
- Generated reconnaissance primary completed through normal controls:
  Libya/Cold War/Strike Missions, forward display, ground mode, camera,
  bay, target designation and exposure. Both engines match at 123
  checkpoints/final. Independent observers require one exposure and one
  photo-credit event for target 1, primary credit, no target damage or
  ejection, and the retained camera/store. Credit screenshot reviewed.
  The route stops airborne after credit; its return is not established.
- Extended reconnaissance completes both photos and returns to raised home
  base 36. Independent observers' input/flight logs and final states match.
  Two exposures, primary 8Ah and secondary 4Ah events, intact targets, deck
  height 128, gear/brakes/idle, fuel remaining, countdown and parent result
  0/status 3 all pass. The route agrees at 272 checkpoints/final hash
  `c403d0542430b898`. Contact/stop and FRAME 2 screenshots reviewed.
- Independent GOG DOSBox reconnaissance also completes both photos and the
  raised home-36 return, reaches END and passes the same strict flight gates:
  intact targets/events, idle stop inside the home box, fuel, countdown and
  parent result 0/status 3. Its 104,757 observations use wall-time controls
  and a different generated mission; exact dynamic-state equality is open.
- Earned Overseas Long Tour Ribbon for the tenth mission: `recon_career`
  continues into END, displays the award and returns to the office. Both
  engines agree at 316 checkpoints/final and all 802 saved bytes. Independent
  milestones require score 275, total 2669 -> 2944, sorties 9 -> 10 and the
  tour ribbon 0 -> 1. Award screenshot reviewed.
- Earned promotion and Airman's Medal: Serge's second sortie saves total
  283 and rank zero; the third passes all promotion thresholds, saving rank
  1, total 500, sorties 3 and medal counter 1 -> 2. Both engines agree at
  354 checkpoints/final and all 802 saved bytes. Promotion/medal pages
  reviewed. The route earns its starting roster through a prerequisite
  sortie using normal inputs, with no committed or fabricated save data.
- Ground-strike training primary destroyed by normally released Mavericks:
  target 3 damaged, primary flag, one matching hit event, two releases and
  station stores 2 -> 0; aircraft airborne with fuel and no ejection/crash.
  Independent observers' input logs and CSVs match under both engines;
  176 checkpoints/final agree, hash `197c6b398d6fbb9f`. Screenshot reviewed.
  The route stops airborne after credit and does not establish a return.
- Maverick strike return to home 33: primary damage and hit credit retained,
  normal landing, zero speed/throttle, gear/brakes, fuel remaining and the
  completed countdown. Parent result 0/status 3; both independent observers'
  input/CSV/result files match exactly. All 297 checkpoints and final hash
  `a4f05231eacf9d75` agree. Stopped cockpit screenshot reviewed.
- Secret-airstrip primary completed in Persian Gulf Limited War: normal
  landing at target 24, (10497,3809), inside its short approach box.
  Original event 8Bh, primary flag, mission store 1 -> 0, idle/gear/brakes,
  fuel 7261 and intact strip pass. Both observers' input/CSV/result files
  match; 167 checkpoints/final agree, hash `5762b7e88b22cda9`. Cargo-delivered
  cockpit reviewed. Aircraft remains at the strip; home return is not claimed.
- Secret-airstrip return now departs the short strip after a normal ground
  turn and taxi, then stops at home 58 with fuel 3000 and parent result
  0/status 3. Credit and consumed stores persist through the second flight.
  The strip is intact at delivery and damaged later while the aircraft is
  elsewhere; this is reported explicitly. Both strong observers agree;
  372 checkpoints/final match, hash `c7b303ee1aa60173`.
- Supply drop reaches Persian Gulf target 24's delivery area before the
  deadline through normal release controls. The tracked player cargo crosses
  ground altitude with matching impact coordinates; one store/release and
  no primary credit reproduce original bug D5. The strong observer rejects
  mere expiry or store consumption. Independent DOSBox cargo reproduction
  and the aircraft's return remain open.
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
  **Transition diagnosis:** the main app now uses the fully covered
  89,276-instruction generation. A fresh 130.8-second comparison has the
  same 1,319 matches, seven unmatched DOSBox pictures and 28 unmatched shots.
  An interpreter capture has byte-identical shot hashes and timestamps and
  the same final state (`e4114c0415d4920d`), so the delay is shared by both
  engines rather than caused by generated regions. Against three independent
  DOSBox captures, the stable roster picture appears at 107.510-107.567 s;
  here it appears at 108.411-108.435 s. After the comparator's global offset,
  the transition remains 0.571-0.585 s late. Frame inspection separates the
  roster list-only image from the selected-pilot details image; the latter is
  late locally after `rostsprt.pic` loads. This points to START's shared
  post-page/menu timing (including D96), not scanout sampling; the timing
  cause is still open.

### Left

- [ ] **The picture against DOSBox, frame by frame.** DOSBox's video capture
      (ZMBV) comparison now runs; match scanout sampling and resolve the
      transition timing differences (see In progress). The apparent roster
      cursor changes were diagnosed as screenshot sampling aliasing.
- [ ] **Roland through Munt** (libmt32emu, LGPL-2.1+): the MT-32 music in
      the game itself, and its output rendered and checked automatically.
      Optional integration now builds against an installed library or a
      separate Munt source checkout. MIDI bytes advance the existing audio
      clock; Munt is mixed at 44,100 Hz before clipping. Headless MIDI capture
      and offline rendering are available. Default/Munt builds and MIDI
      reassembly tests pass; existing DBOPL/Nuked PCM is unchanged. Supplied
      MT-32 1.07 ROMs now render original game MIDI offline and live; a host
      startup bug that discarded the synth was fixed. The
      interpreter and recompiler MIDI logs match byte-for-byte (188,505
      bytes); a matched 4,865-byte MIDI prefix renders to identical PCM in
      two offline runs. Independent live SDL captures still differ despite
      identical MIDI logs. Munt models hardware pitch variation with
      `rand() & 3`, and the host queues 60 ms of startup silence and may clear
      backlogs above 250 ms. Live PCM therefore needs controlled randomness
      or timing/spectral criteria, plus queue-capture diagnosis, before it
      can serve as an exact oracle. Exact reference PCM, flight sound and
      listening validation remain open.
- [ ] **Individual mission objectives:** all nine theatres and all four
      mission categories now have flight routes (air combat, ground strike,
      and both training categories). Complete dedicated objective types.
      Timely cargo impact reproduces the original no-credit bug, and the
      supply-drop return leg is verified (`cargo_return`: home 51 after hits
      on the way, 380 checkpoints under both engines); secret-strip
      delivery and home return are verified. Reconnaissance
      primary/secondary credits and raised-runway return are now verified.
      The existing transfer routes exercise generation and controls, then
      quit; they do not establish objective completion.
- [ ] **The remaining career flow:** higher-rank awards and transfer
      transitions. Promotion, Airman's Medal, the ten-mission tour ribbon,
      and retirement are now earned and saved. A rank-3 (Captain) photo
      record was paired from sorties 16 through 99 under both engines.
      Sorties 53-98 each matched at 380 checkpoints and in all 802 saved
      roster bytes; sortie 99 retired the pilot normally at rank 3 with
      total 20,510. Timed END captures show its 99th-mission retirement
      message and the rank-3 remark "Maybe I'll write a book, like
      Schwarzkopf. The lecture circuit sounds nice and cushy." The
      rank-6 "General, At Last!" branch remains open; it requires the 99th
      mission at rank 6 (total 27,720 and an average of 280). CO transfer
      requests, briefing/arming, pilot creation/editing/erasure and roster
      save/reload, direct maintenance and retired/KIA dialogs are covered.
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
- [ ] **Rendered sound parity:** the default now uses GOG DOSBox's own
      DBOPL synthesis core, rate and 2x gain; Nuked remains selectable.
      Timestamped host output passes chunk-boundary and speaker-gate tests.
      A ROM-free tone probe found and fixed equal-rate mixer rounding;
      86,524 stereo frames now match exactly up to the frequency change.
      The captured intro waveform still differs, with RMS within about
      0.5%. Resolve register timing, mixer block scheduling and capture
      alignment; digitized speech and speaker output need reference PCM.

## Phase 2 - understood code

- [ ] Name the translated routines and data, with explanations, drawing on
      the Reimp's mapping; matched functions replacing translations one at a
      time, each held to the same parity checks.

## Phase 3 - fixes and enhancements (switchable)

- [x] Code overrides: hand-written C registered for a module address,
      replacing that address's code under either engine - how the fixes
      attach. Pinned to the shipped file by hash, off unless switched on
      (`--fix ID`, `--list-fixes`; `fixes=` in the Python machine API).
      Inert when off: the cargo return's 380 checkpoints match the build
      without them. ROM-free `code_overrides` CTest. The recompiler reads
      the fix table and isolates each override address in a
      one-instruction region, so a fix costs no translated code around it.
- [ ] Fixes for the original's bugs in [bugs.md](bugs.md) (D1 frame-rate
      AI, D6 undetectable cells, ...), each on a switch. D5 (supply drops
      earn credit) is done and checked through normal input under both
      engines (`cargo_d5_fixed`). D4 (secret airstrips in Libya, North Cape
      and the Middle East: world bytes corrected as read, START's masks
      widened at its entry) and D34 (destroyed-object table extension, five
      overrides) are done, the Reimp's fixes; D2 (speech hang) and D96 (menu
      timing) are its remaining ones. A session recorded with fixes on names
      them (`# f117r-fixes D5`) and its replay switches them on again.
- [ ] 60+ fps and 4K presentation.

## Housekeeping

- [x] Split `src/machine/dos.c` (about 2,300 lines) into memory, programs,
      files, keyboard and video (`dos_memory.c`, `dos_programs.c`,
      `dos_files.c`, `dos_keyboard.c`, `dos_video.c`; `dos.c` keeps the
      dispatch, BIOS stubs and boot). A token-level check shows every one of
      the original's 85 top-level items moved unchanged apart from `static`
      and a `dos_` prefix on 29 functions now shared through
      `dos_internal.h`, all of them call sites. Six routes agree under both
      engines before and after at every checkpoint, final state, screenshot
      and saved file. The mouse driver is in `mouse.c`.
- [x] Build-from-scratch steps in the README, tested on a fresh public clone:
      full interpreter build, coverage/recompilation, six-route parity at
      329 checkpoints/finals and 5,713,152 lockstep states (zero mismatches).
- [x] `.gitattributes` for line endings: LF text, CRLF Windows batch scripts,
      binary assets excluded from conversion. Existing index text is LF.
- [x] An automated full Windows interpreter build on GitHub, with four
      ROM-free CPU/machine CTests, nine comparator tests and twelve route
      milestone tests. First run
      [passed](https://github.com/TideGear/OpenNighthawk/actions/runs/37192954631).
