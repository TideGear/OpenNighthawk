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
  mere expiry or store consumption. The return to home 51 is now verified in
  `cargo_return` under both engines; an independent GOG DOSBox cargo
  reproduction remains open.
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
  the transition remains 0.571-0.585 s late. A fresh local run against the
  same saved video reproduces 1,319 exact RGB pictures over 130.767 s (1,326
  reference frames, 1,347 local shots; seven and 28 unmatched, all one
  sample), offset -285.325 ms and end drift -570.671 ms. Frame inspection separates the
  roster list-only image (`a0fc2584`, DOSBox 107.311-107.567 s) from selected
  pilot details (`eb49f15a`, first at 107.567 s). At 9 MIPS the local frames
  begin at 108.124 and 108.423 s; after capture alignment they are 0.528 and
  0.571 s late. Nearly all the delay is before the roster list appears; the
  list-to-details transition itself adds about 43 ms. Clock probes put these
  residuals at 1.483/1.498 s at 4 MIPS and 0.385/0.428 s at 12 MIPS, while
  changing many other frames too. The default stays 9 MIPS. Screenshot
  cadence is not the main cause. The last shared transition image aligns at
  106.526 s; the next partial is 442 ms late and the list is 528 ms late.
  Local START opens `Roster.Fil` at 106.782 s, `rostscrn.pic` at 107.481 s
  and `rostsprt.pic` at 107.696 s. A 9 MIPS trace from sprite open through
  the first stable list frame spans 3,854,049 guest clocks (428.2 ms): 64,000
  RLE90 pixel-decode iterations, MGRAPHIC page-copy and transparent-blit
  work, then nine START palette writes at `0x08378`, from 163.4 to 388.5 ms
  after the file open. An expanded DOSBox-X 2026.10.01 log at 9,000 cycles/ms
  starts at `palettes.pal` open (106.401489 s) and covers 8,000,000
  instructions through 108.008496 s. It records `Roster.Fil` read/write at
  106.760483/106.761036, `rostscrn.pic` at 107.177204 and `rostsprt.pic` at
  107.393696. Seven `0x08378` entries occur: four at 106.778615, 106.817953,
  106.850761 and 106.893558, before `rostscrn.pic`; then three at 107.571473,
  107.598189 and 107.626927, after `rostsprt.pic`. The earlier 4M-instruction
  trace began at the sprite open and saw only the latter three. The expanded
  trace was repeated with the stock install-root `ROSTER.FIL` used by the
  packaged video capture. Although the earlier trace used a cloud-save copy
  that differs at nine bytes, the corrected run 4 has the same seven palette
  calls and file-open times. Its log is
  `C:/Users/Tideg/f117-recomp-local/dosbox-ref-roster-mcp-20261005/run4/LOGCPU.TXT`.
  A separate local pre-screen trace starts at `Roster.Fil` open (icount
  961,032,646; 106.781405 s) and ends at `rostscrn.pic` open (icount
  967,325,273; 107.480586 s), a 699.181 ms guest interval. It records 16
  `0x08378` palette calls spanning about 427 ms, retrace polling over about
  452 ms, followed by 17 retrace/PIT samples over about 242 ms; setup and
  short gaps account for the remaining roughly 5 ms. The trace reaches the
  same final machine hash as the video run and is at
  `C:/Users/Tideg/f117-recomp-local/video/roster-pre-screen-trace/trace.txt`.
  In the pre-screen phase, the local trace has 16 palette calls versus four
  in DOSBox-X; after `rostsprt.pic`, the existing local list-painter trace
  has nine calls versus three in the expanded DOSBox-X trace. The stock-file
  rerun confirms the DOSBox-X count was not caused by using the cloud roster.
  Measuring relative to `Roster.Fil` read within each trace, DOSBox-X opens
  `rostscrn.pic` 416.721 ms later and the local run takes 699.181 ms, an
  excess of 282.460 ms. The corresponding 17-call `0x08EAD` retrace/PIT
  sampler starts 172.413 ms after the read in DOSBox-X and 457.056 ms after
  it locally; from its first call to the screen-picture open, the interval is
  244.308 versus 242.125 ms. The extra local time accumulates before this
  sampler, alongside the 16-versus-four palette-call difference. These
  event-relative guest timings do not depend on aligning the two log clock
  origins, but they do not synchronize either event to a frame in the GOG
  DOSBox 0.74 video. The expanded trace also records three 64,000-pixel RLE90
  passes. The sprite-sheet decode takes about 121.4 ms versus 112.7 ms locally.
  The saved DOSBox list-only
  interval is 107.311-107.567 s; local is 108.124-108.423 s, with details
  beginning at each interval's end. A provisional comparison suggests that
  DOSBox-X opens the screen/sprite files about 302 ms earlier than local and
  reaches the reference-list phase roughly 202 ms after its sprite open,
  compared with 428 ms locally. These values combine separate runs and clock
  origins. Anchoring the two instruction-log clocks at their `Roster.Fil`
  opens (21 ms apart) estimates the screen/sprite opens about 282/281 ms
  earlier in DOSBox-X; it puts the reference-list phase about 181 ms after
  the DOSBox-X sprite open versus 428 ms locally, or about 247 ms of
  after-open difference. This phase estimate is not a proven cause. The fresh
  video comparison confirms the prior timing,
  but a frame capture synchronized to a guest event is still needed. Keep the
  frame-parity item open, retain 9 MIPS, and do not add Reimp D96 to translated
  START.

### Left

- [ ] **The picture against DOSBox, frame by frame.** DOSBox's video capture
      (ZMBV) comparison now runs; match scanout sampling and resolve the
      transition timing differences (see In progress). The apparent roster
      cursor changes were diagnosed as screenshot sampling aliasing.
      Root cause of the roster-entry delay found: DOSBox 0.74 raises IRQ0 on
      a PIT control word and takes it at the INT 21h stub's STI, so START's
      own timer handler restores a ~70 Hz tick before the INT 8 vector is
      swapped; its 4-tick palette loops then run about 4x shorter. This is now
      the default (`F117R_PIT_CONTROL_IRQ=0` turns it off): video drift
      against the saved 0.74 capture falls from -570 ms to +14 ms (1,321
      exact pictures, no multi-sample unmatched picture). Every recorded
      flight route was re-recorded by its pilot under the new timing and
      passes its strong observer under both engines (see
      architecture.md, "PIT control-word interrupts"). The private career
      chain evidence predates the change. Remaining: 5 single-sample
      DOSBox pictures and 16 here.
- [ ] **Roland through Munt** (libmt32emu, LGPL-2.1+): the MT-32 music in
      the game itself, and its output rendered and checked automatically.
      Optional integration now builds against an installed library or a
      separate Munt source checkout. MIDI bytes advance the existing audio
      clock; Munt is mixed at 44,100 Hz before clipping. Headless MIDI capture
      and offline rendering are available. `--speaker-log` captures PC speaker
      state and `audio_render --speaker-log` merges it by guest clock with OPL
      and optional MIDI events. Default/Munt builds and MIDI
      reassembly tests pass; existing DBOPL/Nuked PCM is unchanged. Supplied
      MT-32 1.07 ROMs now render original game MIDI offline and live; a host
      startup bug that discarded the synth was fixed. The
      interpreter and recompiler MIDI logs match byte-for-byte (188,505
      bytes); a matched 4,865-byte MIDI prefix renders to identical PCM in
      two offline runs. Optional live diagnostics record internal PCM,
      queue depth and a fixed Munt seed; `audio_render --seed` repeats offline
      PCM exactly. Two 12-million-clock live runs with seed 1 differ inside
      the internal PCM at 0.373 s (RMS 3.31, peak 48, correlation 0.999993),
      before SDL. In each short capture, SDL output matches all 57,776
      overlapping internal frames byte-for-byte after an 83 ms lead. A
      30-second capture has 60 empty-before queue checks among 1,802 updates,
      reaches 70.29 ms maximum queued audio, and has no backlog clear or
      discarded guest clocks. Its output contains a 167-frame zero run near
      the first empty-queue check and then realigns 167 frames later, consistent
      with brief playback starvation and timing drift. The live same-seed
      variation is explained by Munt 2.8.3: `TVP::nextPitch()` draws from
      global `rand()` while `Synth::produceStreams()` renders partials
      serially, so changing audio block boundaries reassigns random draws.
      Same-seed queue logs start with 126 and 124 produced frames. A new
      `audio_render --step-clocks` diagnostic reproduces the effect: fixed
      1 ms renders repeat byte-for-byte, while 1 ms and 10 ms PCM first differ
      at 0.381 s. The 10 ms, seed-1 offline stereo render matches the saved
      `interp-first30.wav` byte-for-byte. That is a saved interpreter baseline,
      not an independent DOSBox or hardware reference. Live output remains
      unsuitable for exact PCM checks until SDL gaps are removed or accounted
      for. A completed type-8 sortie replay reached hash `6c3336ef1c24da17`
      and recorded 70,849 OPL writes, no MIDI bytes and six port 61h bit-0
      toggles. Its 18 speaker-hook entries all have audible-enable bit 1 clear.
      The speaker-aware offline host mix now renders the full sortie through
      clock 9,799,924,671 at
      `C:/Users/Tideg/f117-recomp-local/munt-flight-audio-20261005/airair-type8-full-host-mix.wav`;
      its prefix through 3.8 billion clocks is byte-identical to the prior
      OPL-only render because the speaker is gated off. A 12-second excerpt
      from about flight seconds 29–41 remains saved at
      `C:/Users/Tideg/f117-recomp-local/munt-flight-audio-20261005/airair-type8-flight-29-41s.wav`;
      independent DOSBox flight reference PCM and subjective listening
      validation remain open.
- [ ] **Individual mission objectives:** all nine theatres and all four
      mission categories now have flight routes. All eight primary objective
      type codes have dedicated normal-input routes with independent event
      and state gates: reconnaissance type 1, ground strike type 2, supply
      drop type 3, secret airstrip type 4, and air-to-air types 5–8. Types
      1–4 also have verified return legs; type 1 earns both photo credits.
      Type 3's normal route confirms the original D5 no-credit behavior, while
      `cargo_d5_fixed` earns credit when that switchable fix is enabled.
      Types 5, 7 and 8 use Vietnam / Conventional War; type 6 uses Central
      Europe / Cold War. Type 5 uses one AMRAAM (station 0 stores 3 -> 2); the
      special slot-0 aircraft kill, primary event and credit are verified,
      followed by a home-33 landing. Both engines produced 813 identical
      flight observations and final hash `40a40062ec774095`, with parent
      result 0/status 3. Types 6–8 each use three station-0 AMRAAMs, kill the
      special slot-0 aircraft, earn primary credit and return to their home
      base with fuel remaining. Replays match byte-for-byte across both
      engines: type 6 has 619 flight observations and hash `d06a5ccf075b2e80`;
      type 7 has 780 and hash `11c82f0235e24265`; type 8 has 741 and hash
      `6c3336ef1c24da17`. Independent GOG DOSBox reproduction of the cargo
      behavior and wider generated-assignment coverage remain open. The
      transfer routes exercise generation and controls, then quit; they do
      not establish objective completion.
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
      The intro music's channel-3 note at 29.7 s shows the phase gap in
      sound: it comes from the sound driver's per-frame generator, and both
      DOSBox captures are exactly one generator step (one PLAYER frame)
      ahead of this machine.
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
      Started: `tools/reimp_names.py` joins the Reimp's census (1,535
      functions in seven programs) and its address citations, read-only,
      with this project's generated regions. 317 functions have an
      implementing Reimp function (the doc comment above a definition cites
      the address first), 58 of them with more than one candidate; VGAME has
      203 (47% of its census bytes), START 69, END 41. Caller citations
      name 719. The table is a private lead list; each name still needs
      checking against the code before it is used.
      Matched routines have a path now: `src/matched/matched.c`, placed for
      the recompiled engine only, held to the original by
      `tests/func_lockstep.c` from random states and by every route.
      59 routines are matched, 51 in VGAME and 8 in START and END (free fall, waypoint, orientation
      transpose, sign, abs, clamps, 32-bit shifts and multiply, table sine,
      clipping outcode, map projections, weapon effectiveness, setters,
      string and block copies); the strike route runs 38
      of them 13.9 million times with an unchanged final hash. Next: have the recompiler isolate matched sites
      so their neighbours stay translated.

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
      overrides) are done, the Reimp's fixes; D2 (speech hang) is done too:
      the driver's speech writes are played from the machine's schedule
      instead of a busy-wait, value for value. D96
      is an optional wait for the Reimp's separately implemented native UI,
      not a patch for this translated START path. The measured roster-entry
      timing difference is tracked under Phase 1. A session recorded with
      fixes on names them (`# f117r-fixes D5`) and its replay switches them
      on again.
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
