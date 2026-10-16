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
  and variables; `tools/census.py`). Changed code and declined override
  entries fall back to the interpreter.
- **Engine parity.** 45 scripted routes (44 sessions) are identical between
  the interpreter and the recompiled code at every 50-million-clock checkpoint
  and at the end; every translated instruction is held to the interpreter from
  random states (5,718,848 comparisons under each of the two timing profiles,
  0 differences); the interpreter and the translator are held to 8088 and
  80286 silicon vectors; 144 of 144 planted defects are caught; seeded random
  flights are identical. How:
  [architecture.md](architecture.md#verification-why-11-is-a-claim-with-evidence).
- **What the routes cover.** All nine theatres, three tensions and four mission
  categories; all eight primary objective types with normal-input routes and
  strong observers (reconnaissance, ground strike, supply drop with the original
  D5 no-credit behaviour and `cargo_d5_fixed`, secret airstrip, air-to-air types
  5-8), with verified return legs for types 1-4; landing; the speaker and Roland
  driver boots; pilot creation, editing and erasure, KIA and retired dialogs and
  direct maintenance through the original UI; the earned tenth-mission tour
  ribbon, promotion and Airman's Medal; a rank-3 career paired through
  retirement at sortie 99; the rank-6 "General, At Last!" retirement reached
  with a staged roster (`tools/career_rank6.py`; not an earned rank-6 career).
  Per route: [../tools/routes/README.md](../tools/routes/README.md).
- **The emulated PC against GOG's DOSBox 0.74** (`tools/fidelity.py`): 1,210
  comparable answers agree. The machine follows DOSBox where it was measured
  to (memory layout, VGA timing and scanout, DAC, the PIT control-word
  interrupt, ISA I/O delay, the mouse cursor drawn into video memory):
  [architecture.md](architecture.md#time) and
  [The PC](architecture.md#the-pc).
- **Pictures and music against GOG's DOSBox.** `tools/video_compare.py`: 1,329
  exact pictures in order against the saved capture; three one-sample
  logo-transition pictures are unmatched on each side, and a second capture
  matches 1,321 with 11 per side, so the remainder varies by capture. Music:
  all 22,840 AdLib register writes over the logo and intro identical in order
  and value, timing -14 to +1 ms (`tools/dosbox_compare.py`).
- **Other machines as references** (`tools/pc_parity.py`): DOSBox-X matches
  1,237 exact pictures in order (drift up to 0.2 s); 86Box (a 386DX/33 with
  MS-DOS 5.00 and MOUSE.COM 6.26) matches 85 of 86 graphics pictures in 6-bit
  DAC values (the other is START's roster screen sampled mid-draw). The same
  run checks the intro music (GOG envelope 0.963, spectral 0.992; 86Box's first
  2,000 AdLib writes identical) and a scripted START session's saved roster
  (byte-identical on DOSBox-X and 86Box); `tools/fidelity_all.py` holds the
  machine probe to stored baselines. Method:
  [architecture.md](architecture.md#verification-why-11-is-a-claim-with-evidence),
  layer 8; builds: `tools/ref86box/`.
- **Independent GOG DOSBox flights.** Reconnaissance with both photos and the
  raised-deck return completes on GOG's DOSBox and passes the same strict
  gates. The supply mission is reproduced exactly there (the seed is set 275 ms
  early for its autoexec); delivery there is not pursued (Settled, below).
- **The game version verified:** GOG's installer (build 28044) with the 473.04
  update; Steam's release is byte-identical (`tools/verify_install.py`).
- **The gate** (`tools/build_recomp.py`): translate, build, unit tests (122),
  coverage, both engines on every route, instruction lockstep under both
  timing profiles, matched-routine lockstep at three seeds, and the
  routes-only and event-limit checks. About 21-30 minutes when every
  interpreter route is verified fresh (latest 1,532 s); costs and what was
  optimised: [repeated-processes.md](repeated-processes.md).
- **Rendered speaker and speech audio.** PIT counter 2 follows the 8254 model;
  speaker music matches DOSBox-X (envelope 0.87, spectral 0.92-0.93, level
  0.95). Against a silent capture of 86Box's mixer (MS-DOS 5.00, MOUSE.COM
  6.26) the takeoff call's 12,924 non-silence counts are identical and the
  300-3400 Hz waveform correlates at -0.9916 (opposite polarity, as the
  independent speaker models' formulas give), envelope 0.9995, zero lag; no
  renderer change was needed. Commands and limits:
  [build_86box.md](../tools/ref86box/build_86box.md#silent-speech-waveform-reference).
  Listening is Phase 4; sample-for-sample OPL equality is out of scope.
- **A 386DX/33 timing profile** (10 Oct 2026; `f117run --timing 386`,
  `F117R_TIMING=386`; reference
  [timing386.md](../tools/ref86box/timing386.md)). The game steps its flight
  model by its own frame rate and reads the stick once a frame, so the same
  inputs play out differently on a faster or slower PC; this profile makes the
  recompilation play like the original on a period machine. Recorded routes
  stay on the default (DOSBox) profile; results that do not depend on speed
  must match under both.
  - *CPU and services:* the interpreter matches 86Box to the cycle on all 77
    blocks of `probe386.py`; natively answered BIOS, DOS and mouse services
    are charged their measured cost (all 93 probe blocks exact against MS-DOS
    5.00 with MOUSE.COM 6.26; 5.00 and 6.22 cost the same on the game's calls,
    `build_msdos_vm.py`).
  - *Disk:* DOS file services fitted to the reference's period drive; PLAYER,
    DSWAP and START loads charged at their measured disk positions. The intro
    ends +0.04 s from 86Box (+0.43 s uncharged).
  - *Result:* the game's frame rate in flight (S) is 5-9 under the profile and
    7-9 on 86Box, against 14-15 on DOSBox's model; the intro's scenes hold
    86Box's pace to a frame; 21 of 21 held pictures match (duration difference
    at most 0.214 s). Both engines charge the profile: the gate's 386
    instruction lockstep, and 574 identical paired flight checkpoints
    (`stick_response.py --machine machine386`). Matched routines run the
    original translated body under this profile.

### Settled (scope, 8 Oct 2026)

Closed because what is left measures a reference emulator or the test tools,
not the recompilation:

- [x] **Frame parity with DOSBox.** 1,329 of 1,332 pictures exact; the rest
      are one-sample transitions whose frame varies with the capture's phase.
      Frame timing against a period PC is the 386 profile's job.
- [x] **86Box and DOSBox-X as routine checks.** `pc_parity.py` and
      `fidelity_all.py` are the routine checks; the closed-loop supply drop,
      strike-training hit and career sortie pass on both. The test pilot
      passes 7 of 9 over a +-15% range of its stick scale on 86Box: a property
      of the pilot, not of either machine.
- [x] **Timing details still DOSBox's own** (its BIOS and DOS stubs, its
      per-millisecond slicing, the 275 ms boot phase, `--boot-ms`). They
      describe DOSBox, not the original; the 386 profile supersedes them. The
      game's PUSHF sites never read the 286/386 flag bits.
- [x] **Individual mission objectives.** All eight primary objective types
      have gate routes replayed byte-identically by both engines; the
      closed-loop pilots deliver on DOSBox-X and 86Box (GOG's DOSBox replay
      diverges open-loop and is not pursued); the strike-training credit window
      is the original's (80-150 map units, bugs.md D7).
- [x] **The career flow.** Promotion, medals, ribbons, retirement and the
      rank-6 branch are reached and paired (rank 6 from a prepared roster:
      END's promotion code is the same however the roster was made). END's
      tables, read from its memory: totals 300, 1,125, 3,000, 7,000, 16,000,
      27,720; averages 100, 150, 200, 250, 280, 280; sorties 2, 5, 10, 20, 40
      and exactly 99; score multipliers from the pilot-skill form (one sortie:
      168 Green, 231 Regular, 294 Veteran, 288 Regular with Realistic
      Landings). An earned career was flown to sortie 10 at an average of 290
      (`tools/career_chain.py`) and stopped: 89 more scripted sorties would
      test the pilot, not the game.

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
      **Now:** 897 addresses matched, covering 895 of the census functions and
      120,427 of 179,213 bytes (P2 68.84%, overall 89.56%).
      **What is left:** `tools/census_reach.py` separates code with run
      evidence from code without (gate coverage logs only interpreted
      fallback instructions, so it cannot show what ran). A complete
      interpreter `full_cycle` capture (14 Oct, before that day's and 15
      Oct's batches) reached 358 unmatched entries / 43,138 bytes; 121 entries
      / 12,813 bytes (and 6 interiors / 521 bytes) had no evidence in it, so
      new routes are needed for those.
      **Stays original code:** the timer interrupts (VGAME 0x1C9B, START
      0x8D0B, END 0x4BEF) open with STI, after which the run loop admits one
      more instruction before it looks at events, so a matched body could do
      nothing the original does not. The line clipper's divide-error handler
      (`0FB2:04DD`) is fixture-proven but **not integrated**: no input reaches
      it (146,153 real clip divisions across 14 routes stay well under its
      32,767-unit threshold); it is kept on local branch `irq-divide-fault`
      until a route reaches it. Excluded as unequal: START 0x11DB, MPS_LOGO
      0x1ADC and 0x1C82.

### How an entry is proved

Each entry below is compared with the original from random states at several
seeds (zero differences, every instruction reached by the named code unless
stated), rejects deliberately broken bodies ("mutations"), and is run by a
real route with every checkpoint and the final state exact. Every batch then
passed a fresh full gate before its commit. Per-seed state counts and gate
times are in the commits, the named `D:/f117-gate/` evidence directories and
[repeated-processes.md](repeated-processes.md#phase-2-proofs-matched-routines).
Real-route checkpoint counts: `full_cycle` 66, camera-gradient replay 234, F3
replay 57, boot/startup 54, training 133. *Routes only* marks a routine no
random state can compare (interrupt-vector or BIOS calls), proved by the
routes alone; *thin* marks one a real route calls only once or a seed barely
reaches. Candidate drafts can keep private static dossiers
(`matched_draft.py --dossier`):
[repeated-processes.md](repeated-processes.md#phase-2-candidate-reading).

### Matched: VGAME flight, weapons and world

- `3ADA` frame update (the per-frame world update; 848 instructions, all 71
  calls kept): map cell and clamp, first-frame set-up, markers, timers,
  landing field and traffic, tower, field contact, crashes, carrier deck,
  frame counter and frame-rate measure, standing-target count. Replay 8,462
  calls. Declines while D1 or D1REAL is on. `D:/f117-gate/frame-3ada`.
- `1EB0` flight input (the player's flight step; 1,274 instructions, all 121
  calls): keys, stick scaling, holds, trim, recovery, ejection, engine,
  attitude, position, touchdown and crash tests, 16-frame pose history.
  Replay 8,464 calls. `D:/f117-gate/flight-input`.
- `5852` aircraft traffic (998 instructions): formation, pursuit, patrol,
  landing, smoke, markers, staggered firing; divide-error states at four
  sites. Replay 8,464 calls. `D:/f117-gate/frame-5852`.
- `683E` weapons in flight (886 instructions): target selection, seekers,
  steering, gravity, terrain contact, blast, supply objectives, proximity
  damage, markers; four seeds, all 25 callee-return checks. Replay 8,459
  calls. The supply arm (normally bypassed by D5) is reached only by a staged
  fixture; the entry keeps the D5, D8, D1TTL, D1SLOT, D1PROX and D1ACCEL
  hooks. `D:/f117-gate/frame-683e`.
- `5046` ground defences (195 instructions): damage/stealth bars, site reload
  and engagement, incoming marker and arc, warning countdown; the reload's
  staged divide faults hand back at `5122`. Replay 8,465 calls.
  `D:/f117-gate/nav5046`.
- `63F2` `vgame_traffic_lock_and_fire` and `528D` `vgame_ground_site_engage`
  (`full_cycle` 9 and 12 calls), `0D14` `vgame_nearest_object_lookup` (thin;
  declines while D34 is on). `D:/f117-gate/p2-batch3`, `p2-batch4`.
- `066F8` aircraft activation and `072D8` destruction (destroy reaches the
  sound gate's uninitialised driver thunk `0xD414`, so the harness needs an
  entry for it).
- `073C8` `vgame_target_damage` (target scoring): its compound branch
  conditions make most random states incomparable, so the routes are the
  stronger check; planted weapon-lock values in `tests/func_lockstep.c`.
- `04777` projectile motion and gunfire refill, `01007` terrain collision
  probe, `0D3F9` sound priority (engine reassertion kept even when a request
  is rejected), `0D87C` scene finish.
- `3A4E` `vgame_pause`: a real Alt+P / repeated-Alt+P / Space / quit probe
  matches 54 checkpoints (`matched_pause_wait`, route `pause_resume`).
- `1E79` timer tick: camera replay 53,226 calls.
- `01C0E` `vgame_isrclock_init` (timer vector hook) and `01E72`
  `vgame_frame_mission_start` (BIOS clock sample): routes only.
- `0F8EC` `vgame_free` and `0F91A` `vgame_malloc`.
- `0BAA2` `vgame_init_models` (thin at random states; the gate's routes run
  it).
- `09A0` `vgame_load_table_pair` (thin), `F5F2` `vgame_open_file_handle` and
  `F458` `vgame_read_file_handle` (text-mode read; its CR-LF path needs
  scripted DOS answers, `g_dos_script[].put`; `full_cycle` 11 calls).
  `D:/f117-gate/p2-batch5`, `p2-batch6`, `p2-batch7`.
- `0FAE:0002` segment entry binding: all 27 instructions by authored
  fixtures.

### Matched: VGAME scene and rendering

- `2F9D` view composition (938 instructions, all 66 calls): view modes,
  camera, redraw, scene call, overlays, readouts. Replay 8,456 calls.
  `D:/f117-gate/frame-2f9d`.
- `BCEF` camera-image pass (all 49 calls kept, 1,863 census bytes; replay
  7,880 calls) and `983E` moving-object pass (all 38 calls, 1,724 bytes; F3
  replay 550 calls). `D:/f117-gate/camimage`, `moving-objects`.
- `04E6` `vgame_scene_walk` (terrain tile rings and their objects),
  `DC46` `vgame_compose_walk` and `1450` `vgame_horizon_bands` (which calls
  `observe_game_draw` at entry; defects below): `full_cycle` 275-276 calls
  each. `D:/f117-gate/p2-scene`, `p2-compose`, `p2-pair`.
- `BB0D` `vgame_draw_model_in_view`. `D:/f117-gate/p2-batch5`, `p2-batch6`.
- `130D:033F` `vgame_model_edge_clip` (15 Oct; 206 instructions, five seeds,
  64 mutations, `full_cycle` 184,777 calls). Its near-`RET` accept path at
  `031B` (a latent quirk of the original) is handed back to the original
  code, so that path is not independently tested. `D:/f117-gate/p2-vgame-033f`.
- `1974`/`19FD` camera sky/ground strips (including sixteen-band walks;
  camera-gradient replay 7,851 / 1,108 calls; that route checks rendering
  and makes no mission-completion claim).
- `C45C` camera darkening (77x61 inset): reconnaissance with F6 calls it
  1,608 times, all 150 checkpoints (`recon_darken`, `matched_camera_darken`).
- `0FB2:01E6` closed polygon fill (382,365 replay calls,
  `matched_polygon_fill`), `0FB2:06A6` clipped span edge (1,529,189 calls) and
  the span-emitter entries `0FB2:05A4`/`05B3` (inside the already-counted
  `0555` region, so no byte credit).
- `0FB2:084E` span-table divide-error (INT 0) handler, matched as an
  interrupt entry ending in IRET; the lockstep plants the interrupt frame.
  It runs in 28 of the 39 routes gated at the time; a sign-flipped handler
  changes `vietnam_airair`.
- `vgame_outcode` (image `0x0FFDC`), registered at its real `0FB2:04BC`
  entry (dispatch checks CS); the strike route runs it 4,361,362 times.
- `11A8` moving-map downward frame and MPS_LOGO `006D:0678` sprite run
  (including opaque zero-count runs of 256 writes); `full_cycle` reaches
  both.
- Low-detail sky entry (with the batches of 8-9 Oct below).

### Matched: VGAME HUD and cockpit

- `1058:0016` HUD pass (2,488 census bytes), `79BE` cockpit panel frame (2,090
  bytes): F3 replay 550 calls each. `8A90` navigation grid: F3 replay 314
  calls (the `navigation_grid` route). `9515` objectives panel: the F10
  `objectives_panel` route calls it four times.
- `17B4` `vgame_attitude_indicator_draw`: `full_cycle` 275 calls.
  `D:/f117-gate/p2-batch4`.
- `1058:09D8` HUD colour setup and `1058:0A2C` small/full layout: real C/C
  toggles match 54 checkpoints (`matched_hud_layouts`, route
  `hud_layout_toggle`).
- Projected HUD diamond (`B0C2`): the laser-guided training route calls it
  1,051 times.
- `0B171` weapon-lock marker, `8A18` right-aligned number, `881F` HUD line,
  `114A:0280` display fill, `796E` cockpit initialiser, `114A:03AD`
  navigation symbol, `114A:03FD` sprite rectangle, `114A:014F` readout
  outline, `114A:0006` active-block line and `114A:01DB` target box (checked
  with returning driver thunks; the routes use the real driver).
- `0D6DD` cockpit canopy (side frames, posts, landing-approach cue).
- `114A:02CE` `vgame_panel_navigation`: routes only (two call sites run every
  frame of normal flight).

### Matched: START

- `4239` `start_mission_generate` (1,332 instructions once its nine-entry
  switch is resolved; all 339 room claims and 60 calls audited; one
  otherwise-unreachable timing arm staged). The boot route runs it once.
  `D:/f117-gate/start-4239`.
- `40F0` `start_mission_take_out_sites` (theatre load, mission generation,
  random destroyed sites), `7904` `start_choose_world_record`, `0FA3`
  `start_brief_key_loop`, `149C` flight-plan display, `6DB8` pilot options
  dialog, `07EE` armament screen: `full_cycle` one or two calls each.
  `D:/f117-gate/p2-batch7`, `p2-pair`, `p2-start-0fa3`, `flight-plan`,
  `pilot-options`.
- `0963` `start_arms_screen_loop` (thin), `A59A` `start_sopen_file` (its create
  path needs scripted INT 21h answers), `008D` `start_draw_pilot_card`,
  `64E5` `start_draw_pilot_record_sheet` (thin; its span holds six interior census
  entries), `74B1` `start_world_page_objects`, `623E`
  `start_roster_event_loop` (thin) and `6861` `start_draw_pilot_row`.
  `D:/f117-gate/p2-batch3` to `p2-batch6`.
- `39F7` (with END `1CF5`) menu input and pointer update: `full_cycle` 7,015 /
  487 calls. `D:/f117-gate/p2-next`.
- `3641` screen loader with optional overlay callback: `full_cycle` 5 calls.
- `9D67` (with END `56D9`) formatter conversion-state initialiser:
  `full_cycle` 89 / 9 calls. `D:/f117-gate/format-init`.

### Matched: END

- `2BEC` debrief narrative builder (444 instructions). END `206A`, once
  labelled a fatal-error handler, is the "Press Selector to continue" prompt.
  `D:/f117-gate/debrief-narrative`.
- `0714` `end_replay_step` and `2092` `end_wait_answer`: `full_cycle` 1 and 17
  calls. `D:/f117-gate/p2-batch7`.
- `0100D` `end_ribbons_draw` (thin at random states; called from nine sites).
- `1CF5` and `56D9`: with START above.

### Matched: SETUP, PLAYER, MPS_LOGO, DSWAP and the runtime

- SETUP `0010` `setup_questions` (505 instructions): eight real startup cases
  match 320 checkpoints (all four sound choices, calibration, keypad quit, D3
  on and off); four startup regression routes. `D:/f117-gate/setup-0010`.
- MPS_LOGO `006D:0112` `logo_sprite_set_load` (434 instructions): the boot
  route matches all 100 checkpoints of its 10M-clock prefix.
  `D:/f117-gate/logo-07e2`. MPS_LOGO `006D:0002` encoded-sprite recolouring:
  all 63 instructions by authored fixtures. `D:/f117-gate/entry-palette`.
- PLAYER `0244` `player_stream_picture_file`: `full_cycle` 7 calls.
  `D:/f117-gate/p2-batch4`.
- PLAYER `1602` `player_crt_setargv` (15 Oct; the C start-up's argument
  vector, 204 instructions; it leaves by `JMP [1ABC]` with the stack lowered,
  so the lockstep accepts that exit for this routine only): five seeds, 61
  mutations, `full_cycle` 2 calls. `D:/f117-gate/p2-player-1602`.
- SETUP `19B6` `setup_crt_setargv` (204 instructions; the same startup as
  PLAYER `1602`, with its own `JMP [0EBA]` exit): five seeds, 61 mutations,
  `full_cycle` 1 call. MPS_LOGO `0146:1894` `mps_logo_open_file` (163
  instructions; the far-return form of START's runtime open): five seeds,
  59 mutations, `full_cycle` 1 call. Each keeps all 66 checkpoints and the
  final hash. Census credit: 2 functions / 817 bytes, measured against the
  same census before and after; shared tails add no credit.
  `D:/f117-gate/p2-setup-19b6`, `p2-logo-1894`; batch gate and N=1
  presentation evidence: `D:/f117-gate/p2-setup-logo-accept`.
- MPS_LOGO `0006:0072` `mps_logo_play_intro` (248 instructions, all 26 far
  calls retained): music selection, palette/sprite load, timed frames and
  keyboard cancellation. Its runtime allocator returns with SP lowered by
  `0318h`; the local return trap and the harness's escaped-call check both
  account for it. Five seeds, 58 mutations including the wrong-trap negative
  control, `ibm_boot` 26 and `full_cycle` 66 exact checkpoints, 1 call each.
  The watch envelope is 712 bytes including gaps; census credit stays 707.
  MPS_LOGO `0146:08CC` `mps_logo_crt_setargv` (205 instructions) is the
  far-model startup argv builder: five seeds, 62 mutations, the same two
  exact routes and positive counts. Its indirect far return keeps argv on
  the stack; the harness accepts that terminal for this entry.
  Proofs `D:/f117-gate/p2-logo-0072`, `p2-logo-08cc`; batch evidence
  `D:/f117-gate/p2-logo-main-argv-accept`. Census gain: 2 functions / 1,109 bytes.
- Twenty-two support routines: PLAYER's picture open/refill and linear copy,
  the four timer shutdowns, five mouse wrappers and SETUP's mouse probe,
  file-name comparison, BIOS text writer, register-block interrupt call and
  memory free, the logo and debrief record-table fills, logo file
  loading/freeing and START heap resizing; `full_cycle`, `ibm_boot` and
  `setup_calibration` together call every one. `D:/f117-gate/leaf-batch`.
- DSWAP's checked allocator and five Ctrl+Break restore helpers (including
  allocation-error results: this checks the caller when cleanup returns, not
  real out-of-memory termination); five Ctrl+Break installers (VGAME, START,
  END, DSWAP, SETUP) and DSWAP's DOS allocate/free wrappers.
- The batches of 8-9 Oct: 37 + 34 + 47 VGAME, 34 + 40 + 45 START/END and 21 +
  44 + 64 small-program routines, including the picture decoder's RLE row and
  LZW step in VGAME and END (the step is routes only), the front ends'
  retrace re-sync, PLAYER's two tick-rate setters and VGAME's low-detail sky
  entry. The lockstep includes VGAME `1377:00F3`'s loop and RET below its
  entry.

### Defects found in matched code and the harness

Each is fixed; the named regressions keep the failing seed or case.

- **Arguments read late.** `13B9` `vgame_map_project` and `04ABA`
  `vgame_eventlog_add` cached stack arguments the original reads after its own
  stores, which can alias them when DS and SS overlap
  (`matched_map_project_stack_alias`); `13B9` also read `[950E]` before its
  push of BP, which can wrap onto it (`matched_map_project_saved_bp_alias`).
  VGAME `4B8F` (objective place) must read its target after clearing the
  output buffer. The in-place 32-bit shift's stores go through live BX after
  the original helper returns, even when the stack overlaps that helper's
  code (`matched_shift_stack_alias`).
- **Byte and word tests.** `or ax, ax` after a call was a byte test in `BAA2`
  and twice in `73C8` (wrong SF; seed `0xfaf385733f77`, `matched_or_ax_word`).
  `73C8` also stopped 12 instructions short in its slot walk.
- **Wrapping and relocation.** The menu hit test (START `393D`, END `1C3B`)
  assumed eight contiguous bytes for a list at FFFFh (`matched_menu_hit_wrap`).
  A fixed segment in the record-table fills and a constant zero in the five
  Ctrl+Break installers passed zero-base fixtures but diverged after DOS
  relocated the program; fixtures now vary the relocated operand.
- **Code written during a run.** Fuzzed pointer writes from `683E` could
  overwrite a planted callee stub; such states are skipped
  (`matched_projectile_stub_overwrite`). Fuzz inputs that write code are
  watched per instruction (`matched_order_code_alias`,
  `matched_frame_code_alias`). START/END's shadow-text helper keeps PUSH SS's
  memory write when it aliases the stack (`matched_shadow_stack_alias`).
- **Fix sites inside matched bodies.** A named body ran through D1's site
  (`0x441D`, in `3ADA`), so the fix never ran in the recompiled engine. Five
  matched routines contain a fix site after their entry (`3ADA` with D1 and
  D1REAL, flight input with D1REAL, `vgame_scene_set_override` with D34,
  `vgame_detect_evaluate` with D6); each declines while that fix is on. No
  route triggers D34 or D6 yet.
- **Shared hooks.** `1450`'s address is also the presentation observer's
  frame-phase hook; it calls `observe_game_draw` itself and is on
  `code_overrides`' deliberate list.
- **Room and returns.** Four divides in `3ADA` sat outside their room claims;
  `room_audit.py` (private) checks every claim. A missed `RET 8` let the
  original finish every state unseen; the harness now fails any callee return
  the named code misses. `149C` had a stray resume point only a non-default
  seed saw, so every body is tested at several seeds. Nested interrupt runs
  stop at the caller's next event through `support_int`.
- **SETUP event-limit alert (14 Oct).** Not a game-code defect: two atomic
  native service clocks were counted as guest instructions; the ledger now
  tells them apart (`native_service_boundary`;
  [architecture.md](architecture.md#time)).

## Phase 3 - fixes and enhancements (switchable)

- [x] **Code overrides** attach fixes: hand-written C registered for a module
      address, pinned to the shipped file by hash, off unless switched on
      (`--fix ID`, `--list-fixes`). Inert when off. Details:
      [architecture.md](architecture.md#the-run-time).
- [x] **Fixes for the original's bugs** ([bugs.md](bugs.md)), each on a switch:
      D1 (a frame limiter at VGAME 0x441D holds a fast machine to GOG's 11.6
      frames a second; `tools/d1_check.py`, route `d1_fast_machine`), D2
      (speech played from the machine's schedule instead of a busy-wait), D3
      (keypad digits with NumLock off answer SETUP's sound question instead of
      quitting to DOS), D4 (secret airstrips), D5 (supply drops earn credit;
      `cargo_d5_fixed`), D6 (a detection cover of 0 reads as 4; optional, the
      cells are not proven a defect), D8 (the laser-guided bomb's pitch clamp;
      it also fixes D7), D11 (saves written to a temporary file and renamed),
      D12 (END's signed rating), D34 (destroyed-object table), and the
      high-speed combat fixes D1TTL, D1SLOT, D1PROX and D1ACCEL
      (`tools/d1ttl_check.py`, `d1slot_check.py`, `d1physics_check.py`) and
      the optional D1REAL (below). D10, D35 and D36 are closed as not
      established for DOS or not reproduced
      ([legacy-reports.md](legacy-reports.md)). No speculative fix is offered;
      D96 is the Reimp's own UI wait, not a patch for this START. A session
      recorded with fixes on names them in its log and replays with them.
  - *Speed* ([speed-sweep.md](speed-sweep.md)): the app runs at 20 million
    instructions a second with no limiter
    ([why](speed-sweep.md#fast-machine-default-9-october-2026)): the game's
    own clamp holds S at 15, no swings in 214 flights. D1's 11.6 fps cap gives
    a 1.2882x mission clock rather than real time
    ([D1 follow-up](speed-sweep.md#d1-follow-up-9-october-2026)), and a
    [15 fps cap](speed-sweep.md#actual-15-fps-limiter-experiment-9-october-2026)
    gives 1.25x, so no cap restores real-time pacing.
  - *Combat at high S*
    ([measurements](speed-sweep.md#controlled-threat-profile-combat-measurement-9-10-oct-2026)):
    the game's signed hit guard suppresses SA-5 damage at S >= 14 (D1TTL);
    a seeker-cleared lifetime can occupy the launcher's slot (D1SLOT); the
    proximity band and acceleration depend on S (D1PROX, D1ACCEL). All 21
    staged physics cases pass under both engines. Bursts at 20 MIPS remain
    slightly fewer than at 9 (pooled 36 pairs -0.67 a flight, 95% -1.25 to
    -0.08).
  - *D1REAL*
    ([evidence](speed-sweep.md#optional-real-time-cadence-10-oct-controlled-airborne-checks-complete)):
    an optional real-time cadence; seven clock/dialog cases pass at 9, 20 and
    40 MIPS in both engines and airborne clocks run at real time. The
    original's key-poll timing still limits exact whole-flight determinism
    across speeds, so it does not establish a safe speed.
- [ ] **60+ fps and 4K presentation.** Optional and off by default
      (`--present replay|interp|rerun`, `--present-scale N`, `present =` in
      f117a.ini). Design, measurements and history:
      [presentation.md](presentation.md). Rules: the original's logic tick and
      control timing are unchanged; its bitmap text, sprites and artwork are
      kept and integer-scaled; N=1 stays the exact original; an old path is
      retired only when its replacement passes N=1 equality. Target: 4K at
      60 Hz, and any scale and refresh rate, lower or higher. Higher
      resolution and higher frame rates are experimental and keep the
      original renderer's quirks (road and coastline jitter, which the
      original shows too; drawing-order mistakes; lines of one width at any
      distance).
  - *Done:* every mode, rerun included, keeps all 45 N=1 routes exact
    (259,240 logic frames, 259,166 in-between pairs, every guest hash); the
    original executable matches 2,619 fill-style and 3,953 graphics-driver
    fixtures. **Sharp mode** (`--present replay --present-scale N`, verified):
    model polygons re-projected from exact camera snapshots, including
    near-clipped polygons and span-rasteriser fills; footprints the
    original's clipper gets wrong accepted at N>1 (worst-frame fallback 84%
    to 1.7%); lines on the fine grid (360 Reimp reference strokes exact); HUD,
    panel, map arrow, world outlines, dot markers and sky/ground bands drawn
    from game state (29,594 HUD calls, 11,490 panel calls and 714 map blits
    match the original). **In-between frames** (`--present rerun`, integrated
    and gated 15 Oct): the original's unmodified draw re-run on a throwaway
    copy of the machine with camera and object inputs nudged, refined at any
    scale, timed by a smoothed playback clock (`F117_PRESENT_CLOCK=raw` for
    the old one); 1,638,579 copy checks leave real memory, machine and fix
    state unchanged, and unnudged copies reproduce 259,787 frames exactly.
    The earlier pairing interpolation (`--present interp`) remains; identity
    pairing is off (it deformed objects; `F117_PRESENT_IDENT=1`). **Threaded
    presentation:** fullscreen Direct3D 11 3840x2160 at 59.99 Hz with vsync
    at N=9 passed on 13 Oct (one late interval in 7,715 flight pictures);
    hidden 4K throughput 119-150 fps. A palette frozen in threaded `interp`
    and `rerun` at N>1 (gun rounds solid red, fades frozen) was fixed on 15
    Oct. **Scene-input capture** (`F117R_OBSERVE_SCENE=N`,
    `tools/scene_capture.py`, `tools/scene_model.py`) is identical between
    engines and its decoded topology agrees with the original; the project's
    own scene renderer (`src/present/scene_mesh.c`) is an unconnected draft,
    no longer the main path.
  - *Owner verdict (15 Oct):* rerun's motion at 4K accepted "at least for
    now" (fullscreen, apart from the original's jitter).
  - *Open:* clock tuning against picture age; ask before any visible or
    fullscreen test; per-machine observer, runtime and fix state for parallel copies above
    60 Hz; the small `DF03`/`C2C8` vertex group that nudges less well;
    fullscreen pacing of the latest builds;
    line widths by drawing role; weapon and target symbology; long
    cargo/career flights and slower machines. Optional: refine clipped or
    off-screen outlines (6.1% of outlines in the owner's sharp flight still
    use scaled whole-pixel endpoints). Not planned: smoothing the original's
    road and coastline jitter (owner, 15 Oct: N=1 stays authentic) and a
    longer draw distance.

## Phase 4 - checked by people and independent references

What a machine cannot judge by itself: it needs a person, or a reference that
this project did not build. Not scored by `tools/progress.py` (owner, 15 Oct 2026).

- [ ] **A person playing it:** controls, joystick and mouse, saves, the feel.
      Sessions already record themselves for replay.
- [ ] **Roland through Munt** (libmt32emu): the MT-32 music plays in the game
      and renders offline from a MIDI log; the interpreter's and recompiler's
      MIDI logs are byte-identical and offline renders are repeatable. Live
      output is not a stable PCM reference
      ([architecture.md](architecture.md#audio)). Open: independent reference
      PCM and a listening check.

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
- [x] Gate workers: 24 were no faster than 16 with identical verdicts, so 16
      stay; unit tests run on eight workers (`--unit-jobs 1` for serial).
      [repeated-processes.md](repeated-processes.md).
