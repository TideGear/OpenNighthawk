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

- **Translation.** All 17 code files and the LZEXE decompressor: 89,365
  instruction starts, 96% of the code-area bytes (the rest is strings, tables
  and variables; `tools/census.py`). The coverage pass adds no further
  translated code; changed code and declined override entries can fall back
  to the interpreter.
- **Engine parity.** 45 scripted routes (44 sessions) are identical between the interpreter
  and the recompiled code at every 50-million-clock checkpoint and at the end;
  every translated instruction is held to the interpreter from random states
  (5,718,848 comparisons under each of the two timing profiles, 0 differences); the interpreter and the translator are
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
  START's roster screen half drawn while its files load). The same run checks the
  music (GOG envelope 0.963, spectral 0.992, level 1.005; DOSBox-X 0.90-0.92 and
  0.947, its mixer 0.74 of the level; 86Box (a 386DX/33 profile): identical first
  2,000 AdLib writes and notes on four of six channels, scene timing within 0.31 s
  per scene and 2.0 s drift over the intro) and a scripted START session's saved
  roster (byte-identical on DOSBox-X and 86Box); `tools/fidelity_all.py` holds the
  machine probe to stored baselines. Builds and notes: `tools/ref86box/`.
- **Independent GOG DOSBox flights.** Reconnaissance with both photos and the
  raised-deck return completes on GOG's DOSBox and passes the same strict
  gates. The supply mission is reproduced exactly on GOG's DOSBox (its autoexec
  consumes about 275 ms of guest time, so the seed is set 275 ms early); delivery
  there is not pursued (Settled, below).
- **The game version verified:** GOG's installer (build 28044) with the 473.04
  update; Steam's release is byte-identical (`tools/verify_install.py`).
- **The gate** (`tools/build_recomp.py`): translate, build, coverage, both
  engines on every route, instruction and matched-routine lockstep; about 7
  minutes after a `matched.c`-only change with cached interpreter results;
  about 22–30 minutes when verifying every interpreter route fresh (15 Oct: 1,330 s with
  coverage skipped, 1,706 s with it). It
  explicitly enables the app build so its host tests are included even after a
  core-only build. Repeated processes, their cost and what was optimised:
  [repeated-processes.md](repeated-processes.md). A retained rotating-seed
  regression checks the in-place 32-bit shift's stores through live BX after
  the original helper returns, including a stack overlapping that helper's
  code (`matched_shift_stack_alias`).
- **Rendered speaker and speech audio.** PIT counter 2 follows the 8254 model;
  speaker music matches the DOSBox-X reference (envelope 0.87, spectral
  0.92-0.93, level 0.95). The speech waveform reference is captured silently
  from 86Box's mixer on MS-DOS 5.00/MOUSE.COM 6.26 (9 Oct): the takeoff call's
  12,924 non-silence counts are identical, over 855.626 ms; in 300-3400 Hz
  waveform correlation is -0.9916, envelope 0.9995, zero lag. Opposite polarity
  and 3.17x level follow the independent speaker models' amplitude formulas; no
  renderer/default change was needed. Capture leaves all 4,500 frame records
  and port writes byte-identical. Commands and limits:
  [build_86box.md](../tools/ref86box/build_86box.md#silent-speech-waveform-reference).
  Listening remains Phase 4; OPL mixer sample equality remains out of scope.
- **A 386DX/33 timing profile** (completed 10 Oct 2026; `f117run --timing 386`,
  `F117R_TIMING=386`, `src/cpu/timing386.c`; reference
  [timing386.md](../tools/ref86box/timing386.md), `ops386.json`). The game steps
  its flight model by its own frame rate ([0x368E]) and reads the stick once a
  frame, so the same inputs play out a little differently on a faster or slower
  PC; this profile makes the recompilation play like the original on a period
  machine. The default machine follows DOSBox's model (one clock an instruction
  plus I/O delays, 9.00 M clocks a second); the profile charges what 86Box's
  386DX/33 charges, from its source: each instruction's cycles and prefetch
  refills, the IBM VGA's 8-bit bus and ISA port costs, at 33.333 MHz. Results
  that do not depend on speed must match under both profiles; recorded routes
  and hashes stay on the DOSBox profile.
  - *CPU and services:* the interpreter matches 86Box to the cycle on all 77
    blocks of `probe386.py` (ALU, memory forms, jumps, calls, INT, REP chunks
    with prefixes, VGA memory, ports); the services answered natively (video
    BIOS mode sets, palette, text; INT 16h, 1Ah; INT 21h AH=0Bh and 2Ch; INT 33h)
    are charged their measured cost: all 93 probe blocks exact against the saved
    MS-DOS 5.00 reference with Microsoft MOUSE.COM 6.26 (AH=2Ch varies by four
    cycles between runs). `build_msdos_vm.py`: 5.00 and 6.22 cost the same on
    the game's calls, 4.01 10% less, FreeDOS twice on 2Ch; CuteMouse 45% cheaper
    than 6.26.
  - *Disk:* DOS file services (open, read, write, close, attributes, seek) are
    fitted to the 1989 3500 rpm drive preset, the reference's period drive
    (timing386.md "File services"; the RAM-disk preset's fit is recorded there
    too). EXEC and overlay loads are charged: PLAYER.EXE, DSWAP.EXE and
    START.EXE, the three loads that dominate the intro's drift, at their own
    measured position on the reference disk (`realexec.py`); any other load by
    the nearest of three synthetic-file sizes. The cost is seek-distance
    dominated, not size dominated (a fifteen-size probe was non-monotonic; the
    real files cost 27-45% more than same-size synthetic stand-ins). The intro's
    drift at the end: +0.43 s uncharged, +0.10 s synthetic sizes, +0.04 s real
    positions, the same 1,237 of 1,275 pictures exact throughout.
  - *Result:* the game's frame rate in flight (S) is 5-9 under the profile and
    7-9 on 86Box, against 14-15 on DOSBox's model, so the same inputs fly at the
    period PC's pace (`stick_response.py --machine machine386`); the intro's
    scenes hold 86Box's pace to a frame (`frames386.py`). Pictures, music and the
    picture check pass against the reference (p037's one close picture is a
    confirmed mid-fade capture-timing artifact). The retained check needs a
    separately recorded 386 capture (comparing the default 9 MIPS capture with
    86Box was a harness error) and pairs held pictures by content: 21 of 21 held
    pictures match, maximum duration difference 0.214 s (limit 0.30), start
    drift 1.113 s (limit 1.5); the final hangar differs by 19 ms after excluding
    reference screen-off gaps. Eight regressions reject wrong profiles, wrong
    pictures, missing holds and excessive duration/drift.
  - *Both engines* charge the same profile: 5,713,152 instruction comparisons
    each in RAM and with code in VRAM, no differences; paired flight sessions
    have 574 identical state checkpoints and stick responses (`stick_response.py
    --machine machine386 --engine interp|recomp`). Matched entries use the
    original translated body under this profile. STI and SS loads hold
    interrupts through the following instruction even when it costs zero cycles
    (four regressions). Regions are capped at 1,024 instructions to bound MSVC's
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

- [ ] **Name the translated routines and data**, with explanations, drawing on
      the Reimp's mapping; matched functions replace translations one at a
      time, each held to the same parity checks. Method:
      [architecture.md](architecture.md#matched-routines-phase-2); the table is
      at the end of `src/matched/matched.c`; how routines are proved now (the
      private kit and parallel agents) and the next candidates:
      [../handoff.md](../handoff.md). `tools/reimp_names.py` joins the Reimp's
      census (1,535 functions) with this project's regions as a private lead
      list.
      **Now:** 891 addresses matched, covering 889 of the census functions and
      117,660 of 179,213 bytes (P2 67.37%, overall 86.67%).
      **What is left:** `tools/census_reach.py` separates code with no run
      evidence from unreachable code (gate coverage logs only interpreted
      fallback instructions, so it cannot show what ran). A complete interpreter
      `full_cycle` capture (14 Oct; all 66 checkpoints and the final state match
      the plain replay) reached 358 unmatched entries / 43,138 bytes; 6
      interiors / 521 bytes and 121 entries / 12,813 bytes had no evidence in
      it, so new routes remain useful for those. These figures predate the 14-15
      Oct batches. The private 47-routine draft batch has 37 positively reached
      entries (two already matched). Private inventory:
      `D:/f117-gate/format-init`.
      **Stays original code:** the timer interrupts (VGAME 0x1C9B, START
      0x8D0B, END 0x4BEF) open with STI, after which the run loop admits one
      more instruction before it looks at events, so a matched body could do
      nothing the original does not. The line clipper's divide-error handler
      (`0FB2:04DD`) is fixture-proven like `084E` below but **not integrated**:
      no input reaches it (all 39 routes, three staged flight scripts and an
      instrumented probe over 146,153 real clip divisions across 14 routes stay
      well under its 32,767-unit threshold); it is kept on local branch
      `irq-divide-fault` and as a patch file, not in `master`, until a route
      reaches it. Three unequal candidates were excluded: START 0x11DB,
      MPS_LOGO 0x1ADC and 0x1C82.

### How an entry is proved

Each entry below is compared with the original from random states at several
seeds (states compared, every instruction reached by the named code, zero
differences unless stated), rejects deliberately broken bodies ("mutations"),
and is run by a real route with every checkpoint and the final state exact.
Every batch then passed a fresh full gate (all routes identical, both
instruction profiles, three matched seeds, reachability and event checks, zero
mismatches) before its commit; per-batch gate figures and census snapshots are
in the commits and evidence directories, the current ones in
[../handoff.md](../handoff.md). Real-route checkpoint counts: `full_cycle` 66,
camera-gradient replay 234, F3 replay 57, boot/startup 54, training 133.
*Routes only* marks a routine no random state can compare (interrupt-vector
or BIOS calls), proved by the routes alone; *thin* marks one a real route
calls only once. Candidate drafts can keep private static dossiers
(`matched_draft.py --dossier`; the synthetic checks, including a C compile
check of the draft tool, run in Windows CI): see
[repeated-processes.md](repeated-processes.md#phase-2-candidate-reading).

### Matched: VGAME flight, weapons and world

- `3ADA` frame update (the per-frame world update): 848 instructions / 2,608
  bytes, all 71 calls kept; map cell and clamp, first-frame set-up, markers,
  timers, landing field and traffic, tower, field contact, crashes, carrier
  deck, frame counter, frame-rate measure, standing-target count. Seeds 2,931 /
  2,914 / 2,912 states (all 848 instructions; 827 at one seed); replay 8,462
  calls. Declines while D1 or D1REAL is on (defects below).
  `D:/f117-gate/frame-3ada`.
- `1EB0` flight input (the player's flight step): 1,274 instructions / 3,543
  bytes, all 121 calls; keys, stick scaling, holds, trim, recovery, ejection,
  engine, attitude, position, touchdown/crash tests, 16-frame pose history.
  Seeds 2,112 / 2,718 / 2,292 (all instructions at the default seed); replay
  8,464 calls. `D:/f117-gate/flight-input`.
- `5852` aircraft traffic: 998 instructions / 2,976 bytes; formation, pursuit,
  patrol, landing, smoke, markers, staggered firing. Seeds 2,056 / 2,017 /
  2,078, plus 226 / 208 / 223 divide-error states at four sites; replay 8,464
  calls. `D:/f117-gate/frame-5852`.
- `683E` weapons in flight: 886 instructions / 2,565 bytes; target selection,
  seekers, steering, gravity, terrain contact, blast, supply objectives,
  proximity damage, markers. Four seeds 4,844 / 4,851 / 4,862 / 4,852, divide
  faults 291 / 271 / 264 / 274, all 25 callee-return checks; replay 8,459 calls.
  The supply arm (normally bypassed by D5) is reached only by a staged fixture.
  The entry guard keeps the D5, D8, D1TTL, D1SLOT, D1PROX and D1ACCEL hooks.
  `D:/f117-gate/frame-683e`.
- `5046` ground defences: 195 instructions / 583 bytes; damage/stealth bars,
  site reload and engagement, incoming marker and arc, warning countdown. Seeds
  2,696 / 2,679 / 2,685; the reload's divide faults (1,006 / 996 / 1,039,
  deliberately staged, not normal play) require hand-back at `5122`;
  `matched_ground_defences`; replay 8,465 calls. `D:/f117-gate/nav5046`.
- `63F2` `vgame_traffic_lock_and_fire` and `528D` `vgame_ground_site_engage`:
  87 and 74 mutations; `full_cycle` 9 and 12 calls. `D:/f117-gate/p2-batch3`.
- `0D14` `vgame_nearest_object_lookup` (declines while D34 is on): 85
  mutations; `full_cycle` once (thin). `D:/f117-gate/p2-batch4`.
- `066F8` aircraft activation and `072D8` destruction: three seeds, about 9,200
  comparable states each. Destroy reaches the sound gate's uninitialised driver
  thunk (`0xD414`), so the harness needs its own entry for it
  (architecture.md).
- `073C8` `vgame_target_damage` (target scoring): 174 instructions; three seeds;
  its compound branch conditions make most random states incomparable, so the
  routes are the stronger check; planted weapon-lock values in
  `tests/func_lockstep.c`.
- `04777` projectile motion and gunfire refill, `01007` terrain collision
  probe (the census's replacement-operation name was misleading), `0D3F9`
  sound priority (engine reassertion kept even when a request is rejected),
  `0D87C` scene finish: two seeds 7,763 / 7,777 states, including partial runs.
- `3A4E` `vgame_pause`: 14,879 states; a real Alt+P / repeated-Alt+P / Space /
  quit probe matches 54 checkpoints; `matched_pause_wait`, route `pause_resume`.
- `1E79` timer tick: 13,776 comparisons; camera replay 53,226 calls; census
  extent 53 bytes (shared walk 95).
- `01C0E` `vgame_isrclock_init` (timer vector hook) and `01E72`
  `vgame_frame_mission_start` (BIOS clock sample): routes only.
- `0F8EC` `vgame_free` (beside the matched `0F91A` `vgame_malloc`): seeds
  66,505 / 4,411 / 4,436.
- `0BAA2` `vgame_init_models`: 17 / 0 / 1 comparable states at three seeds
  (thin random-state evidence; the gate's routes run it).
- `09A0` `vgame_load_table_pair` (thin) and `F5F2` `vgame_open_file_handle` (C runtime
  open), `D:/f117-gate/p2-batch5`/`p2-batch6`; `F458` `vgame_read_file_handle`
  (text-mode read; its CR-LF path needs the lockstep's scripted DOS answers to
  deliver the byte a one-byte read brings, `g_dos_script[].put`; `full_cycle`
  11 calls), `D:/f117-gate/p2-batch7`. The batch-7 routines (`F458`, START
  `40F0`, END `0714`, `2092`) reach every instruction at five seeds with 46-62
  mutations caught.
- `0FAE:0002` segment entry binding: all 27 instructions by authored fixtures.

### Matched: VGAME scene and rendering

- `2F9D` view composition: 938 instructions / 2,595 bytes, all 66 calls; view
  modes, camera, redraw, scene call, overlays, readouts. Seeds 3,199 / 3,216 /
  3,200 (one seed also visits a nonstandard address inside the span, excluded
  from the claim by an audit against the listing); replay 8,456 calls.
  `D:/f117-gate/frame-2f9d`.
- `BCEF` camera-image pass: 14,735 targeted comparisons including 145
  helper-return divide faults, all 32 tracked paths; replay 7,880 calls; all 49
  calls kept; census extent 1,863 bytes. `D:/f117-gate/camimage`.
- `983E` moving-object pass: 8,071 comparisons, all 32 tracked paths; F3
  replay 550 calls; all 38 calls kept; extent 1,724 bytes.
  `D:/f117-gate/moving-objects`.
- `04E6` `vgame_scene_walk` (810 bytes / 276 instructions; terrain tile rings
  and their objects): 37 mutations, `full_cycle` 275 calls.
  `D:/f117-gate/p2-scene`.
- `DC46` `vgame_compose_walk` (867 bytes / 295 instructions): 6,177-6,187 states
  a seed, 26 mutations, `full_cycle` 276 calls. `D:/f117-gate/p2-compose`.
- `1450` `vgame_horizon_bands` (868 bytes / 301 instructions): 11 mutations,
  `full_cycle` 276 calls; it calls `observe_game_draw` at entry (defects
  below). `D:/f117-gate/p2-pair`.
- `BB0D` `vgame_draw_model_in_view`. `D:/f117-gate/p2-batch5`/`p2-batch6`.
- `1974`/`19FD` camera sky/ground strips: 28,726 states including 2,279
  sixteen-band walks; camera-gradient replay 7,851 / 1,108 calls (that route
  checks rendering and makes no mission-completion claim).
- `C45C` camera darkening (77x61 inset): 8,873 states; reconnaissance with F6
  calls it 1,608 times, all 150 checkpoints; `recon_darken`,
  `matched_camera_darken`.
- `0FB2:01E6` closed polygon fill: 6,600 states; camera-gradient replay
  382,365 calls; `matched_polygon_fill`.
- `0FB2:06A6` clipped span edge: 14,461 states; 1,529,189 calls; extent 419
  bytes (shared tails not counted again).
- `0FB2:05A4`/`05B3` span-emitter entries: 20,325 states; 507,745 / 591,404
  executions; inside the already-counted `0555` region, so no byte credit.
- `0FB2:084E` span-table divide-error (INT 0) handler, matched as an interrupt
  entry (kind 3, ending in IRET); the lockstep plants the interrupt frame and
  compares interrupt depth. It runs in 28 of the 39 routes then gated; eight
  (81,235 runs) are identical at all 1,236 checkpoints, and a sign-flipped
  handler changes `vietnam_airair`.
- `vgame_outcode` (image `0x0FFDC`) is registered at its real `0FB2:04BC`
  entry (dispatch checks CS, so the linear address missed rasterizer calls);
  the strike route runs it 4,361,362 times.
- `11A8` moving-map downward frame and MPS_LOGO `006D:0678` sprite run: 28,613
  states, including 1,172 opaque zero-count runs (256 writes) and 595
  transparent runs; `full_cycle` reaches both.
- Low-detail sky entry (with the batch of 8-9 Oct below).

### Matched: VGAME HUD and cockpit

- `1058:0016` HUD pass: 14,750 comparisons including 905 divide faults, all 32
  paths and four fault sites a seed; F3 replay 550 calls; extent 2,488 bytes.
- `79BE` cockpit panel frame: 14,729 comparisons, 32 paths; F3 replay 550
  calls; extent 2,090 bytes.
- `8A90` navigation grid: 6,206 comparisons, 19 paths; F3 replay 314 calls;
  the retained `navigation_grid` input adds the F3 replay to the gate; extent
  1,094 bytes.
- `9515` objectives panel: 8,439 comparisons including partial stops; the
  `objectives_panel` F10 route calls it four times; its 500-byte shared walk
  earns only its 91-byte census extent.
- `17B4` `vgame_attitude_indicator_draw`: 104 mutations, `full_cycle` 275
  calls. `D:/f117-gate/p2-batch4`.
- `1058:09D8` HUD colour setup and `1058:0A2C` small/full layout: 26,500
  states; real C/C toggles match 54 checkpoints; `matched_hud_layouts`, route
  `hud_layout_toggle`.
- Projected HUD diamond: 14,933 states; the laser-guided training route calls
  it 1,051 times (the plain strike's weapon model 1Bh does not enter its
  drawing arms).
- `0B171` weapon-lock marker, `8A18` right-aligned number and `881F` HUD line:
  29,522 states with returning driver thunks (the routes use the real driver).
- `114A:0280` display fill and `796E` cockpit initialiser: 14,971 states.
- `114A:03AD` navigation symbol and `114A:03FD` sprite rectangle: 24,775
  states.
- `114A:014F` readout outline, `114A:0006` active-block line and `114A:01DB`
  target box: 44,398 states.
- `0D6DD` cockpit canopy (side frames, posts, landing-approach cue).
- `114A:02CE` `vgame_panel_navigation`: routes only (two call sites run every
  frame of normal flight, confirmed before the gate).

### Matched: START

- `4239` `start_mission_generate`: a 1,332-instruction / 3,811-byte walk once
  its nine-entry switch is resolved (census ownership 3,035 bytes); all 339
  budgets and 60 calls audited. Seeds 3,858 / 3,859 / 3,858 plus two optimized
  targeted seeds, each with 95 divide-fault frames at both sites; one
  otherwise-unreachable timing arm is staged. The boot route runs it once.
  `D:/f117-gate/start-4239`.
- `40F0` `start_mission_take_out_sites` (theatre load, mission generation,
  random destroyed sites): `full_cycle` 2 calls. `D:/f117-gate/p2-batch7`.
- `7904` `start_choose_world_record` (769 bytes / 272 instructions): 17
  mutations, `full_cycle` 2 calls. `D:/f117-gate/p2-pair`.
- `0FA3` `start_brief_key_loop` (568 bytes / 224 instructions): 46 mutations,
  `full_cycle` 2 calls. `D:/f117-gate/p2-start-0fa3`.
- `149C` flight-plan display (365 instructions / 926 bytes): about 4,970-4,985
  states at five seeds; 20 broken bodies fail; `full_cycle` 2 calls.
  `D:/f117-gate/flight-plan`.
- `6DB8` pilot options dialog (399 instructions / 1,013 bytes): 4,998-5,000
  states at three seeds; 18 broken bodies fail; `full_cycle` once.
  `D:/f117-gate/pilot-options`.
- `07EE` armament screen: 6,459 comparisons; `full_cycle` once; its 373-byte
  walk earns 67 census bytes.
- `0963` `start_arms_screen_loop` (thin), `A59A` `start_sopen_file` (its create
  path, reached when the open fails with AX 2, needs scripted INT 21h answers,
  `g_dos_script`), `008D` `start_draw_pilot_card`, `64E5`
  `start_draw_pilot_record_sheet` (one function whose span holds six census
  entries; thin) and `74B1` `start_world_page_objects`: 66-104 mutations;
  `full_cycle` 1-447 calls. `D:/f117-gate/p2-batch5`, `p2-batch6`.
- `623E` `start_roster_event_loop` (thin) and `6861` `start_draw_pilot_row`:
  70 and 59 mutations. `D:/f117-gate/p2-batch4`, `p2-batch3`.
- `39F7` (with END `1CF5`) menu input and pointer update: all 532 instructions,
  16 mutations; `full_cycle` 7,015 / 487 calls.
  `D:/f117-gate/p2-next/final-gate-verdict.json`.
- Screen loader with optional overlay callback: 4,839 caller states;
  `full_cycle` 5 calls.
- `9D67` (with END `56D9`) formatter conversion-state initializer: all eight
  instructions; `full_cycle` 89 / 9 calls. `D:/f117-gate/format-init`.

### Matched: END

- `2BEC` debrief narrative builder (444 instructions / 1,077 bytes): 4,999-5,000
  states at three seeds; ten broken bodies fail; `full_cycle` once. END `206A`,
  once labelled a fatal-error handler, is the "Press Selector to continue"
  prompt and is renamed. `D:/f117-gate/debrief-narrative`.
- `0714` `end_replay_step` and `2092` `end_wait_answer`: `full_cycle` 1 and 17
  calls. `D:/f117-gate/p2-batch7`.
- `0100D` `end_ribbons_draw`: 33 and 1 comparable states at two of three seeds;
  called from nine sites.
- `1CF5` and `56D9`: with START above.

### Matched: SETUP, PLAYER, MPS_LOGO, DSWAP and the runtime

- SETUP `0010` `setup_questions` (505 instructions / 1,267 bytes): all 122
  budgets and 77 calls audited; seeds 4,484 / 4,484 / 4,489; eight real startup
  cases match 320 checkpoints (all four sound choices, calibration, keypad quit,
  D3 on and off); four startup regression routes. `D:/f117-gate/setup-0010`.
- MPS_LOGO `006D:0112` `logo_sprite_set_load` (434 instructions / 1,164
  bytes): all 78 room claims and 35 calls audited; seeds 3,728 / 3,733 / 3,728;
  the boot route matches all 100 checkpoints of its 10M-clock prefix.
  `D:/f117-gate/logo-07e2`.
- MPS_LOGO `006D:0002` encoded-sprite recolouring: all 63 instructions by
  authored fixtures; seven broken bodies fail (with `0FAE:0002`).
  `D:/f117-gate/entry-palette`.
- PLAYER `0244` `player_stream_picture_file`: 108 mutations, `full_cycle` 7
  calls. `D:/f117-gate/p2-batch4`.
- Twenty-two support routines: PLAYER's picture open/refill and linear copy,
  the four timer shutdowns, five mouse wrappers and SETUP's mouse probe,
  file-name comparison, BIOS text writer, register-block interrupt call and
  memory free, the logo and debrief record-table fills (including the
  65,536-iteration zero-count loops), logo file loading/freeing and START heap
  resizing. Four seeds compare 103,554-103,672 states each; twenty planted
  defects are rejected; `full_cycle`, `ibm_boot` and `setup_calibration`
  together call every routine with zero overruns; six grouped CTests.
  `D:/f117-gate/leaf-batch`.
- DSWAP's checked allocator and five Ctrl+Break restore helpers: 76,014
  states, including 3,709 allocation-error results (this checks the caller
  when cleanup returns, not real out-of-memory termination).
- Five Ctrl+Break installers (VGAME, START, END, DSWAP, SETUP) and DSWAP's DOS
  allocate/free wrappers: 98,171 states.
- The batches of 8-9 Oct: 37 + 34 + 47 VGAME, 34 + 40 + 45 START/END and 21 +
  44 + 64 small-program routines, each checked at two 4,000-state seeds (the 9
  Oct VGAME batch at four more); the picture decoder's RLE row and LZW step in
  VGAME and END (the step is routes only); the front ends' retrace re-sync,
  PLAYER's two tick-rate setters and VGAME's low-detail sky entry. The
  lockstep includes VGAME `1377:00F3`'s loop and RET below its entry.

### Defects found in matched code and the harness

Each is fixed; the named regressions keep the failing seed or case.

- **Arguments read late.** `13B9` `vgame_map_project` and `04ABA`
  `vgame_eventlog_add` cached stack arguments the original reads after its own
  stores, which can alias them when DS and SS overlap; they now read in the
  original's order or decline (`matched_map_project_stack_alias`). VGAME `4B8F`
  (objective place) must read its target after clearing the output buffer.
- **Byte and word tests.** `or ax, ax` after a call was a byte test in `BAA2`
  and twice in `73C8` (wrong SF; a zero low byte took the wrong branch);
  `matched_or_ax_word` keeps seed `0xfaf385733f77`. `73C8` also had a
  12-instruction shortfall in its slot walk (regression keeps the failing seed;
  `D:/f117-gate/gate-sprite-map-retry.log`).
- **Wrapping and relocation.** The menu hit test (START `393D`, END `1C3B`)
  assumed eight contiguous bytes for a list at FFFFh (`matched_menu_hit_wrap`).
  A fixed `00DAh` segment in the record-table fills passed zero-base fixtures
  but corrupted the logo after DOS relocated it; fixtures now vary the
  relocated operand. The five Ctrl+Break installers read the loader-relocated
  segment too (a nonzero-image fixture catches a constant zero).
- **Code written during a run.** Fuzzed pointer writes from `683E` could
  overwrite a planted callee stub; such states are skipped
  (`matched_projectile_stub_overwrite`). Fuzz inputs that write code (END's
  pair swap; two found by the pause gate's seed) are watched per instruction
  (`matched_order_code_alias`, 15,984 states; `matched_frame_code_alias`, 8,944
  states). START/END's shadow-text helper keeps PUSH SS's memory write when it
  aliases the stack (seed `0xf2a7d2c1bf36`).
- **Fix sites inside matched bodies.** A named body ran through D1's site
  (`0x441D`, in `3ADA`), so the fix never ran in the recompiled engine. Five
  matched routines contain a fix site after their entry (`3ADA` with D1 and
  D1REAL, flight input with D1REAL, `vgame_scene_set_override` with D34,
  `vgame_detect_evaluate` with D6); each declines while an enabled fix is
  placed in its body (`recomp_fix_placed_in`). D34 and D6 are not triggered by
  any route yet.
- **Shared hooks.** `1450`'s address is also the presentation observer's
  frame-phase hook; it calls `observe_game_draw` at entry like the three other
  shared placements and is on `code_overrides`' deliberate list.
- **Room and returns.** Four divides in `3ADA` sat outside their room claims,
  invisible to the one-short probe; `room_audit.py` (private) checks every
  claim. A missed `RET 8` on the long divide let the original finish every
  state unseen; the harness now fails any callee return the named code misses.
  `149C` had a stray resume point only a non-default seed saw, so every body
  is tested at several seeds. The mid-run harness checks no-call clock
  overshoots; a radar-decoy fixture guarantees the projectile aspect arm
  (886/886 instructions). Nested interrupt runs stop at the caller's next event
  through `support_int`; removing its clamp is rejected by a route mutation.
- **SETUP event-limit alert (14 Oct).** Not a defect in the game code: its
  native INT 10h callback includes two atomic stub clocks the overrun audit
  took for guest instructions. The ledger now accounts for them without
  changing `icount`; per-slice state records are identical across eight slice
  schedules and both engines, 12 SDL boots per build keep the same final hash,
  the regression still catches an extra NOP, and a ROM-free boundary
  regression fails against the old accounting.

## Phase 3 - fixes and enhancements (switchable)

- [x] **Code overrides** attach fixes: hand-written C registered for a module
      address, pinned to the shipped file by hash, off unless switched on
      (`--fix ID`, `--list-fixes`). Inert when off. Details:
      [architecture.md](architecture.md#the-run-time).
- [x] **Fixes for the original's bugs** ([bugs.md](bugs.md)), each on a switch.
      Done: D1 (a frame limiter at VGAME 0x441D holds a fast machine to GOG's
      11.6 frames a second; `tools/d1_check.py`, route `d1_fast_machine`), D2
      (speech played from the machine's schedule instead of a busy-wait), D4
      (secret airstrips), D5 (supply drops earn credit; `cargo_d5_fixed`), D11
      (saves written to a temporary file and renamed), D12 (END's signed
      rating), D34 (destroyed-object table), D8 (the laser-guided bomb's pitch
      clamp; also what makes D7's training strike hard to complete), D6
      (a detection cover of 0 reads as 4; optional, since the cells are not
      proven a defect), D3 (keypad digits with NumLock off answer SETUP's
      sound question instead of quitting to DOS), D1TTL (unsigned valid
      missile lifetimes restore SA-5 proximity damage at S >= 14 without
      limiting frames; `tools/d1ttl_check.py`), D1SLOT (seeker-cancelled
      incoming life stays zero, releasing its launcher slot;
      `tools/d1slot_check.py`), D1PROX (a swept incoming proximity band using
      an explicit S9 terminal-speed reference) and D1ACCEL (incoming
      acceleration per simulated second; `tools/d1physics_check.py`), and the
      optional D1REAL. D7 is fixed by D8. D10, D35 and D36 are closed after
      review as not established for DOS or not reproduced, with evidence,
      limits and reopening criteria in [legacy-reports.md](legacy-reports.md).
      No speculative fix is offered. D96 is the Reimp's native-UI wait, not a
      patch for this translated START. A session recorded with fixes on names
      them in its log and replays with them.
  - *Speed* ([speed-sweep.md](speed-sweep.md)): the default is 20 million
    instructions a second with no limiter
    ([9 Oct](speed-sweep.md#fast-machine-default-9-october-2026)): the game's
    own clamp holds S at 15 and the drawn rate near 16.7, with no swings in 214
    flights. The [D1 follow-up](speed-sweep.md#d1-follow-up-9-october-2026)
    (90 flights): no oscillation, D1 inactive hash-for-hash at 9 MIPS, but its
    fast-machine 11.6 fps cap gives a 1.2882x mission clock rather than real
    time. An [isolated 15 FPS experiment](speed-sweep.md#actual-15-fps-limiter-experiment-9-october-2026)
    settles at S 12 with a 1.25x mission clock, so a cap does not restore
    real-time pacing; production D1 is unchanged and no limiter is shipped.
  - *Combat at high S* (the first [threat-profile measurement](speed-sweep.md#controlled-threat-profile-combat-measurement-9-10-oct-2026)'s
    observer was faulty and its gap estimates are superseded; the findings below are the corrected ones):
    the game's signed hit guard suppresses SA-5 damage at S >= 14 (D1TTL);
    seeker-cleared lifetime underflows to FFFF and can occupy the launcher's
    chosen slot (D1SLOT; staged S=8/15 cancellation and eleven countdown
    controls pass in both engines with unchanged control hashes); a
    constant-speed straight-pass probe still hits at S=8 and misses at S=15
    with TTL+PROX+SLOT (residual proximity-distance dependence); a
    two-simulated-second SA-5 probe reaches speed 9/16 at S8/15 (acceleration
    is unscaled, which favours high-S acceleration, not weaker combat). All 21
    staged physics cases pass under both engines, with unchanged S9/player
    controls. The 72-flight cohort retains a burst gap across 32 matching
    missions. Burst differences, 20 minus 9 MIPS: 12 held-out pairs -0.5 a
    flight (wide interval); pooled 36 pairs -0.6667 (95% -1.25..-0.0833); a
    24-pair same-option-set cohort -0.9583 (95% -2.2083..+0.25), which does not
    prove equivalence. The tools keep unsigned valid life, the official launch
    counter, burst crossings, timing/detection inputs and damage counters on
    early exit (an earlier observer hid SA-5 launches above signed life 32767
    and missed burst resets; its gap estimates are superseded).
  - *D1REAL* ([evidence](speed-sweep.md#optional-real-time-cadence-10-oct-controlled-airborne-checks-complete)):
    passes seven normal-input clock/dialog cases at 9/20/40 MIPS in both
    engines; corrected airborne clocks run at real time; all 12 seed- and
    step-aligned mission pairs have identical launch counts, 11 match
    burst/damage counts, and the remaining pair takes more damage at 20 MIPS
    after input-phase divergence. The identified CPU/enemy defects are repaired
    in this optional corrected mode; original key-poll timing still limits
    exact whole-flight determinism. Control-response tooling accepts speed,
    fixes, program-relative menus and boot-clock overrides and keeps early
    exits; ten control flights share mission identifiers after boot-clock
    alignment, but their progressive input sequences still diverge, so they do
    not establish a safe speed.
- [ ] **60+ fps and 4K presentation.** Design, measurements, rules and history:
      [presentation.md](presentation.md). Off by default (`--present
      replay|interp`, `present =` in f117a.ini, `--present-scale N`). Rules:
      keep the original's logic tick and simulation/control timing, keep pixel
      art and the original bitmap text/artwork integer-scaled, and retire an
      old path only when its replacement passes N=1 equality. Target: 4K at 60
      Hz (16.67 ms), and any scale and refresh rate, lower or higher.
  - *Done:* the observer and draw lists rebuild every picture bit for bit: all
    45 complete N=1 routes keep 259,240 logic frames, 259,166 interpolation
    pairs and every guest hash; the original executable matches 2,619
    fill-style and 3,953 graphics-driver fixtures. Model polygons are
    re-projected on the fine grid from exact camera snapshots, including
    near-clipped polygons, seams, the map's cockpit-art round trip and polygons
    filled through the span rasteriser (`U` record); polygons whose footprint
    the original's clipper gets wrong are accepted at N>1 (worst-frame fallback
    84% -> 1.7%). Lines follow the Reimp's HUD treatment (fine-grid strokes,
    `--present-line-width`; 360 Reimp reference strokes exact). Drawn from
    game state at N>1: the HUD (all six modes match 29,594 original HUD calls),
    panel (11,490 calls), map arrow (714 blits, eight original 4x4 bitmaps),
    world outlines and dot markers (N=9 RMS 1.680 -> 0.0039 and 2.756 -> 0.0140
    fine pixels) and sky/ground bands (1,514,560 captured quads; horizon RMS
    about 3.3 -> 0.4). `F117_PRESENT_DIAG` shows each pixel's origin. The
    presentation draws the in-between picture one frame ahead on a worker
    thread; the 13 Oct strike build passed fullscreen Direct3D 11 3840x2160 at
    59.99 Hz with vsync at N=9 (7,715 flight pictures, p99 17.23 ms, maximum
    28.32, one late interval, 0.013%). Opt-in scene-input capture
    (`F117R_OBSERVE_SCENE=N`, `tools/scene_capture.py`, `tools/scene_model.py`)
    is integrated: 192 identical requests / 25,214,976 bytes between engines, decoded topology
    agreeing with the original on 2,330 camera vertices, 488 plane tests and
    all 205 reachable detail variants.
  - *Owner decision (15 Oct): stop improving primitive pairing.* Identity
    pairing halved unpaired shapes but deformed the extra pairs (paired
    batches deforming over 100 px 3.78 -> 20.53%), so it is off
    (`F117_PRESENT_IDENT=1` opt-in); `tools/motion_check.py` measures pairing,
    deformation and step timing
    ([presentation.md](presentation.md#identity-pairing-and-the-step-clock-15-october-2026)).
    The safe sharp mode meanwhile is `--present replay --present-scale N`. For
    60+ fps the chosen method is to re-run the original's unmodified draw on a
    throwaway copy of the machine at each in-between moment, with only camera
    and object inputs nudged, plus a smoothed playback clock (designed and
    simulated, not built), at any scale and refresh rate; plan and safety
    proof in [../handoff.md](../handoff.md). Draw distance is not extended.
    The earlier plan to
    [consolidate world geometry](presentation.md#consolidate-world-geometry-before-more-clipping-exceptions-15-october-2026)
    into the project's own scene renderer is no longer the main path:
    `src/present/scene_mesh.c` is on master as part of a gated tree but is
    unconnected to live rendering and not a finished fix.
  - *Open:* the re-run-draw prototype; the owner's road jitter (gray ground
    lines) and jumping building patches (6.4% of polygons and 2.4% of outline
    edges have no partner between logic steps, about 95% of those edge misses
    from the 60 px motion limit or look-alike ordering rather than culling;
    the road-jitter cause is not yet confirmed, see
    [presentation.md](presentation.md#identity-pairing-and-the-step-clock-15-october-2026));
    the uneven step clock (freezes/skips at 24% of step boundaries); coastline
    topology transitions and widths by drawing role; low detail and
    incompatible geometry; width tuning; weapon/target symbology; fullscreen
    refresh pacing for the latest batches (hidden 4K throughput 119-150 fps;
    an interrupted fullscreen run is not acceptance; ask before any
    visible/fullscreen test); the owner's motion review on a physical panel;
    cargo/career flights and slower machines.

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
- [x] Tested 24 gate workers against the same-code 16-worker baseline:
      identical verdicts/checkpoints, no coverage/parity speedup. Retain 16;
      the local launcher now verifies inherited BelowNormal priority too.
      Gate unit tests run on eight workers (`--unit-jobs 1` for serial).
      Measurements and proof: [repeated-processes.md](repeated-processes.md).
