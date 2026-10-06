# Handoff

For the next conversation working on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left - keep it
updated), [docs/architecture.md](docs/architecture.md) (how parity is
built and checked) and [docs/bugs.md](docs/bugs.md) (the original game's
bugs). State as of 5 October 2026, about 23:10 local. Earlier "running" entries
further down are historical; this first section is the current state.

## Current state (5 Oct, ~23:10 local)

All work is committed and pushed (last code commit `399afb8`); the main tree
and the worktree `~/f117-recomp-local/wt` are both at `origin/master`. The
standing goal is still "complete docs/roadmap.md", item by item; this is a
handoff, not completion. Work happened in the worktree (built with
`~/f117-recomp-local/build-p3.cmd` into `p3-build`) while the gate ran in the
main tree.

**Gate.** `tools/build_recomp.py --work ~/f117-recomp-local/p2-pipeline` on
`12d7158` (log `p2-pipeline5.log`): 27/27 routes identical, lockstep 5,709,312
states 0 mismatching, 191 matched routines 0 mismatching. Not rerun since: the
later commits touch the observer (inert unless installed), host audio/config,
CMake (Munt on by default) and docs, not generated or matched code.

**P3 presentation, Stage 1 (current item; owner chose observe-and-replay).**
Everything is in `docs/presentation.md`.
- `src/matched/observe.c` hooks VGAME's fill and outline paths and every
  graphics-library entry (jump slots `1E42:011A+5n`). Record kinds: fills
  `E F B R b a`, outline edges `L`, lines `N`, colour `K`, span fills `Q`,
  blits `C`, text `T` (with its font), sprites `S`, tick scales `H`, page
  copies `D` (44 present, 48 copy, 79 dissolve), other entries `X`; with
  `F117R_OBSERVE_PAGES=1` also page diffs `x`/`n`, the work page `Z` and the
  display `Y` at each `game_draw`.
- `tools/drawlist_frame.py LOG` rebuilds each phase from the draw list:
  **work page 73 of 73 phases exact, 0 bytes copied from the original** (30M
  instructions of strike flight). **Display page 36 of 73**: each 3-D phase
  still differs in ~480 bytes inside the presented window (e.g. `A000:81C1`,
  row 103). Next step: watch one such byte through the replay (wrap the page in
  a `bytearray` subclass that prints writes, as done for the outline-edge
  hunt) - likely the order of the present (entry 44) against the primitives,
  or a second present.
- `tools/drawlist_spans.py LOG` checks primitives one by one (fills, lines,
  span fills, blits).
- Capture: `F117R_OBSERVE_PAGES=1 p3-build/f117run.exe --engine recomp --data
  D:/GOG/F-117A --replay tools/routes/strike.input --steps 8030000000
  --time-us 700000000000000 --save DIR --log FILE --observe
  OUT:8000000000:8030000000` (about 58 MB of log).
- Driver facts: MGRAPHIC at CS `0889` in flight, data segment `06ED` (read from
  its `MOV AX` immediates, which move with the load), page table `cs:[0787]`
  (0 display A000, 1 work page 47BD, 2 cockpit art 09C0), current page
  `cs:[0194]`, origin `cs:[0196]` (entries 24/26), rows `cs:[0004+2y]` = 320y.
- `docs/progress.json`: Stage 0 0.9, Stage 1 0.9. After Stage 1: Stage 2
  (high-resolution re-draw), Stages 3-4 (interpolation, pacing).

**Sound and settings (owner-requested, done and checked).**
- Roland radio calls are PC-speaker digitised sound (counter 2 mode 0, a count
  of 0-80 every 79 PIT clocks). Point-sampling made them a screech; `e2e100c`
  renders mode 0 as GOG DOSBox's "realsound" level, averaged per sample. The
  owner confirmed it sounds right. AdLib plays music, effects and speech on the
  OPL; Roland plays music and effects as MT-32 MIDI (the engine is one held
  note on channel 8 bent with the throttle) and speech on the speaker.
- Munt is in the default build (`FetchContent` at the pinned 2.8.3 commit;
  `mt32emu-2.dll` beside the exe). The main `build\` was reconfigured with
  `-DF117R_WITH_MT32EMU=ON` (an existing cache keeps OFF otherwise).
- `f117a.ini` beside the exe holds the options (`src/host/config.c`, CTest
  `host_config_file`; `f117a.example.ini` is copied there by the build). Roland
  is one setting: `roland = munt|windows|off` plus `mt32-roms = FOLDER`; ROMs
  are recognised by SHA-1 through Munt (`mt32_find_roms`), preferring MT-32
  1.07. README has the supported-ROM table. Measured: control ROMs 1.04 and
  1.07 render this game's MIDI bit-identically; 2.04 renders the flight effects
  ~5 dB quieter and duller (consistent with D36 and DOS Days' "old MT-32").
  The owner's own `build\f117a.ini` points at `D:\GOG\F-117A` and
  `C:\Users\Tideg\f117-recomp-local\roms`. Examples use `C:\GOG Games\F-117A`.
- Owner asked how Roland sound effects work; answered from the MIDI log. Open:
  map each effect to its driver request (trap the sound driver) and check
  whether the startup SysEx uploads custom timbres.

**Still pending from earlier today (unchanged):** the `F117R_PIT_CONTROL_IRQ`
default (policy: correct 0.74 behaviour, but 8 routes need retiming), D12
verification, roster frame parity, MT-32 reference PCM. See the sections below.

## Earlier wrap-up status (5 Oct, 08:52 local)

- The user's immediate objective changed to wrapping up for a new conversation,
  committing and pushing the outstanding project work, then stopping. That
  wrap-up is complete; the project changes are on `origin/master`. The roadmap
  remains open; this is a handoff, not a claim that the full roadmap is
  complete.
- The desktop automation surface currently reports no apps or browsers. The
  saved DOSBox-X run4 trace and local START trace remain the usable evidence;
  no GUI capture was started.
- A closer trace count narrows the pre-screen palette difference. Local START
  reaches the palette writer through caller `0x31EC` eight times, while the
  DOSBox-X run4 trace reaches that caller twice. The full pre-screen totals
  remain 16 local versus four DOSBox-X calls. The caller reads the BIOS timer
  words at `0040:006C`/`006E` and compares them with a stack target before
  repeating. This points to loop/timer state as the next measurement, but the
  traces are separate runs and still do not establish a frame-synchronized
  cause. Next, sample the caller's stack target and BIOS timer values in both
  traces or obtain a guest-event-synchronized reference AVI.
- A read-only attempt to sample those stack locals through `Machine` did not
  run: the temporary probe was launched outside the repository, so Python
  could not import `machine_api`. It created no guest output. To retry, run it
  from the repository or add `tools` to `PYTHONPATH`. The temporary failed
  probe directory is outside the repository.
- The tracked changes are the air-to-air objective routes/controller,
  speaker-event logging and merged offline rendering, live Munt/audio
  diagnostics, related documentation, and a landing observer boundary fix.
  Leave the pre-existing untracked `test.bat` out of the commit. The working
  tree passed `git diff --check`; no tests were run in this wrap-up turn.

## Pending: fix D12 needs a negative rating to verify (5 Oct)

- D12 (END's best-rating/total tally, END 0x00443 cmp/jae and 0x00450 sub
  dx,dx) was written as two overrides (signed compare to 0x044D/0x0449;
  CWD) but NOT committed: no route reaches the tally with a negative rating.
  The tally runs only when the flight record's +30h is 0 (a survived
  mission), and every route scores positive; a crashed cargo return skipped
  it. Verify with a staged negative rating - e.g. a synthetic scorer event
  log as the Reimp's oracle_debrief.py/scorer_check does - before adding it.

## Roster timing ROOT CAUSE found (5 Oct, ~09:40 local)

- START's teardown (`0x8CE6`-`0x8CFE`) writes PIT control word 0x36, reload 0,
  then INT 21h AH=25h restoring INT 8. DOSBox 0.74 (`timer.cpp` `write_p43`)
  raises IRQ0 at once when counter 0's output is low, and its core leaves the
  decoder at the next STI when IF and an IRQ are pending
  (`core_normal/prefix_none.h` STI). The INT 21h stub starts with STI, so the
  game's own timer handler (`0x8D0B`) runs BEFORE the vector change and
  rewrites PIT 0 to ~70 Hz (`0x8DC4`, from `[AE03]`). The BIOS tick then runs
  ~4x faster, so START's 4-tick palette loops (`0x31B3`-`0x31EC`) take about
  a quarter of the time. The saved 0.74 reference video agrees (3 fade
  frames, one VGA frame apart, versus 13 locally). The local machine ignored
  control-word IRQs and its direct INT 21h path ran the service before taking
  any interrupt.
- Implemented opt-in: `F117R_PIT_CONTROL_IRQ=1` (src/machine/pc.c
  `pit_control`, dos.c INT stub entry, pc_events STI-shadow exemption inside
  the service stubs). With it, Roster.Fil -> `rostscrn.pic` is 3.34M clocks
  (was 6.29M) and `tools/video_compare.py --against .../intro-caauys37` gives
  1,321 exact pictures, end drift +14 ms (was -570 ms), no multi-sample
  unmatched. CTests 11/11 and `tools/fidelity.py` 1,210 agree / 0 differ with
  it on.
- It is OFF by default because it moves START's screen timing: with it on,
  `boot_to_flight` is still engine-identical (final `869584dc1fac6f5d`, 54
  checkpoints) but the `career_promotion` prerequisite route fails (its clicks
  are timed against the old slower roster). Turning it on by default means
  re-timing the route inputs, regenerating committed finals and redoing
  the private career/objective evidence chains. That is a policy call (it is
  the correct 0.74 behaviour); decide it, then retime routes and flip the
  default.
- Music check (10:20): `tools/dosbox_compare.py --seconds 130` agrees for 596
  writes / 29.7 s, then differs at write 596 (`0A3` 49 vs 92) both with the
  flag on and off, so it is the known unfiltered random note, not the PIT
  change. Timing offset at the end: +39 ms flag on, +11 ms flag off; a
  random-note-aware comparison of the remainder is still open.
- PIT writers in the intro (to 1.25B clocks, `F117R_TRACE_PIT`): MPS_LOGO 2,
  PLAYER 1+~226 reload writes, START 2 control sequences + ~80-110 handler
  writes, flag on or off. Only START's control word finds the output low and
  changes behaviour; PLAYER/MPS_LOGO counts are unchanged (PLAYER +1 from the
  timing shift). VGAME and END are not covered by this check.
- Routes with the flag on (10:40, `run_route.py`, recomp only, output
  `~/f117-recomp-local/pitflag/`): 19 of 27 pass their milestones
  (boot_to_flight, cargo, cargo_d5_fixed, the six theatre strikes, recon,
  frontend_dialogs, full_cycle, ibm_boot, roland_boot, roster_edit,
  secret_airstrip, vietnam_airair, central_europe_airair, ground/air
  training). 8 fail and need re-timed inputs: career_promotion, career_serge,
  recon_career (saved-roster bytes wrong: the front-end clicks land on other
  screens), landing (VGAME exits at 3.09B, expected >= 7B), cargo_return,
  recon_return, secret_airstrip_return, strike_return (return legs end before
  their minimum clock). The committed finals for the 19 would also change.
- Next: with the flag on, retime the front-end routes (and
  `tools/start_settle`-style scripts), rebuild finals, run
  `tools/dosbox_compare.py` (music) and the frame comparison, then make it the
  default. Check that other control-word writes to PIT 0 (PLAYER, VGAME) behave
  as 0.74 does with the flag on.

## Latest continuation (5 Oct, after 08:36 local)

- Completed a generated Vietnam / Conventional War air-to-air objective
  (objective type 5) through normal inputs. The special aircraft is unit
  slot 0; the loaded station-0 AMRAAM released normally, destroyed it and
  earned the primary event and credit at 410.12 flight seconds. The aircraft
  then landed at home 33 with fuel remaining; parent mission result is 0 and
  pilot status is 3. The last landing sample no longer has the killed bit
  because VGAME clears the unit flags after the kill, so
  `airair_pilot.py` checks the slot-0 flag and primary event on a credit-time
  sample.
- The adaptive run's input log replays under both engines. Recompiler and
  interpreter each have 813 identical flight observations, final hash
  `40a40062ec774095`, no observer errors, and the same successful home
  result. Credit-time state has unit-0 flags `0x27`, one primary air-kill
  event and one launch; station 0 falls from three AMRAAMs to two. Home stop
  is at (14656, 14461), fuel 3050. Runs are outside the repository at
  `C:/Users/Tideg/f117-recomp-local/objective-airair-vietnam-20261005/`.
- New route/controller: `tools/routes/vietnam_airair.front` and
  `tools/airair_pilot.py`. The controller predicts the class-7 seeker choice
  from active air units in the missile's `0x1000` forward cone; it does not
  wait for the unrelated cockpit lock word. Keep its safe 8,000-ft AGL floor:
  the 2,500-ft trial ended before the target. The strict observer and route
  usage are documented in `tools/routes/README.md`; `docs/roadmap.md` now
  records type 5 as covered, not all air-to-air objective variants.
- Rechecked the roster timing with saved image sequences and a full instruction
  trace. The last shared
  transition frame aligns at 106.526 s; the next partial image is 442 ms late,
  and the first list frame is 528 ms late. In the 9 MIPS local log,
  `Roster.Fil`, `rostscrn.pic` and `rostsprt.pic` open at 106.782, 107.481
  and 107.696 s. The stable list appears at 108.124 s; after capture alignment,
  that is 428 ms after the sprite sheet opened. From its open at icount
  969,259,665 through the first stable list at 973,113,714, the trace records
  64,000 RLE90 output-pixel iterations in START `0x08922`-`0x08973`, MGRAPHIC
  page-copy/transparent-blit work, then nine START palette writes at
  `0x08378`. Each writes 256 RGB entries; successive writes are about two VGA
  periods apart and together span 225 ms. The full interval is 428.2 ms.
  Trace: `C:/Users/Tideg/f117-recomp-local/video/roster-trace-full/`.
- A separate 9 MIPS trace from `Roster.Fil` open (icount 961,032,646;
  106.781405 s) to `rostscrn.pic` open (icount 967,325,273; 107.480586 s)
  measures 699.181 ms. It records 16 `0x08378` calls writing 256 entries
  each, with retrace polling over about 452 ms, followed by 17 retrace/PIT
  samples over about 242 ms; setup and short gaps account for about 5 ms.
  The run ends at the same machine hash as the video capture,
  `e4114c0415d4920d`. Private trace:
  `C:/Users/Tideg/f117-recomp-local/video/roster-pre-screen-trace/`.
- Repeated the video comparison in a fresh local run against the saved GOG
  DOSBox capture: 1,319 exact RGB pictures over 130.767 s (1,326 reference,
  1,347 local; seven and 28 unmatched, all one sample), offset
  -285.325 ms, end drift -570.671 ms. Report:
  `C:/Users/Tideg/f117-recomp-local/video/intro-3rytbead/comparison.json`.
- Instrumented a separate, private DOSBox-X 2026.10.01 run using its debugger
  control channel (documented in the
  [DOSBox-X debugger README](https://github.com/joncampbell123/dosbox-x/blob/master/README.debugger)).
  The private run uses the original GOG installation read-only, a private save
  overlay and 9,000 cycles/ms; it is not the packaged GOG DOSBox 0.74 used for
  the saved video captures. The expanded 8,000,000-instruction trace starts
  at `palettes.pal` open, 106.401489 s, and ends at 108.008496 s. It captures
  `requestr.pic` 106.402582, `Roster.Fil` read/write 106.760483/106.761036,
  `rostscrn.pic` 107.177204 and `rostsprt.pic` 107.393696. There are seven
  START `0x08378` entries: four before `rostscrn.pic` at 106.778615,
  106.817953, 106.850761 and 106.893558, then three after `rostsprt.pic` at
  107.571473, 107.598189 and 107.626927. The earlier 4M trace started at the
  sprite open and contained only the final three. Three RLE90 passes of
  64,000 output pixels are recorded; the sprite-sheet pass takes about
  121.4 ms versus 112.7 ms locally. Thus decode execution alone does not
  explain the roster delay, and the seven calls do not form one fade sequence.
- The first DOSBox-X trace used the cloud-save `ROSTER.FIL`, while the saved
  GOG video capture mounts a scratch install and therefore reads the stock
  install-root file. A corrected run 4 used that stock roster file and
  produced the same seven palette entries and matching file-open times. The
  cloud-save difference at nine bytes did not cause the count result. Corrected
  log: `C:/Users/Tideg/f117-recomp-local/dosbox-ref-roster-mcp-20261005/run4/LOGCPU.TXT`.
- The phase counts remain different: 16 local versus four DOSBox-X palette
  calls before `rostscrn.pic`; after `rostsprt.pic`, nine local calls versus
  three DOSBox-X calls. These traces show where local guest time is spent but
  do not synchronize a guest event to a frame in the packaged DOSBox video.
- Within each log, the interval from `Roster.Fil` read to `rostscrn.pic` open
  is 416.721 ms in DOSBox-X and 699.181 ms locally, an excess of 282.460 ms.
  The matching 17-call `0x08EAD` retrace/PIT sampler begins 172.413 ms after
  the read in DOSBox-X and 457.056 ms after locally; its first-call-to-screen
  intervals are 244.308 and 242.125 ms. This places the extra local time
  before the sampling block, alongside the 16-versus-four palette-call phase.
  It narrows the pre-screen execution difference but does not tag a guest
  event to a frame in the GOG DOSBox 0.74 capture.
- A fresh run5 debugger probe at `-break-start` confirmed the live DOSBox-X
  debugger command list has no video-capture start/stop command. The probe
  was stopped before the guest ran; its stock-roster overlay is under
  `C:/Users/Tideg/f117-recomp-local/dosbox-ref-roster-mcp-20261005/run5/`.
  A trace-aligned DOSBox-X AVI still needs its host capture key or menu.
- Across the separate DOSBox-X/local logs, `Roster.Fil` opens differ by about
  21 ms. Anchoring the clocks there estimates that the screen/sprite opens are
  about 282/281 ms earlier in DOSBox-X, and that the reference list phase is
  about 181 ms after its sprite open versus 428 ms locally (~247 ms
  after-open difference). The package video and DOSBox-X trace do not share a
  synchronized event/frame, and the clock offset is anchored at one file
  event, so this is a phase estimate rather than a measured causal split.
  Full frame parity remains open. Keep the default at 9 MIPS; do not apply
  Reimp D96 to translated START. Private debugger/config/log files are under
  `C:/Users/Tideg/f117-recomp-local/dosbox-ref-roster-mcp-20261005/`.
- Continued MT-32 isolation with `--mt32-seed`, `--audio-dump` and
  `--audio-queue-log` in the Munt-enabled executable. Two 12-million-clock
  replays with seed 1 each render 58,800 internal PCM frames, but differ
  starting at frame 16,439 (0.373 s; RMS 3.31, peak 48, correlation
  0.999993). Repeated offline rendering with `audio_render --seed 1` remains
  byte-identical. In both short live runs, SDL disk output matches all 57,776
  overlapping internal frames exactly after a 3,664-frame (83 ms) leading
  offset; neither queue log has an empty-before check, clear or dropped guest
  clocks.
- A 30-second seeded run rendered 1,323,000 internal frames and captured
  1,330,176 SDL frames. Across 1,802 queue updates, 60 began with an empty
  queue, the largest post-update queue was 70.29 ms, and there were no
  backlog clears or discarded guest clocks. The disk capture has a 167-frame
  zero run (3.79 ms) near the first empty-queue check, then local PCM windows
  realign with a 167-frame later offset. The fixed 83 ms alignment first
  breaks at internal frame 270,601; later local windows match exactly with
  changing offsets. This points to brief playback starvation/timing drift in
  addition to same-seed variation that already exists before SDL. The new
  instrumentation and captures are in `src/host/main.c`,
  `tools/audio_render.c`, and
  `C:/Users/Tideg/f117-recomp-local/munt-audit-results/live/queue-diag/`.
  Details are recorded in the Roland sections of `docs/architecture.md` and
  `docs/roadmap.md`.
- Explained the live seeded PCM change from Munt's block-sensitive use of
  global `rand()`: `TVP::nextPitch()` draws once per simulated timer firing,
  while `Synth::produceStreams()` renders partials serially. Different audio
  advance boundaries therefore distribute the seeded draws to partials
  differently. Same-seed live queue logs start with 126 versus 124 produced
  frames. `audio_render` now accepts `--step-clocks` (default remains 10 ms at
  9 MIPS) to isolate that effect. With seed 1 and a 270M-clock render, two
  1 ms runs are byte-identical (PCM SHA-256
  `05cc06e4ae5c803d54bad8536845c79c549ef5004e03298df57ad2f38766f510`);
  the 10 ms run first differs from them at frame 16,791 / 0.381 s and has RMS
  difference 756.52, peak 19,471, and correlation 0.5493. The 10 ms output
  matches the saved `interp-first30.wav` stereo PCM exactly (SHA-256
  `a51690955f052c1edf142eeb93545b3d39160488bb8cf4ff9883e647552cd9b8`). This
  proves the fixed-step offline path is reproducible and explains live
  same-seed variation; it is not independent DOSBox or MT-32 hardware PCM.
  The live `munt-final.wav` versus saved interpreter baseline has 0.707
  envelope correlation, -0.013 waveform correlation and 0.868 median spectral
  cosine after approximate alignment; these values include the measured SDL
  gap. The independent reference capture and a flight-sound listening check
  remain open.
- Captured the completed type-8 air-to-air replay's sound events in
  `C:/Users/Tideg/f117-recomp-local/munt-flight-audio-20261005/`. It ends at
  clock 9,799,924,671 with the existing hash `6c3336ef1c24da17`; the run
  recorded 70,849 OPL writes, no MIDI bytes, and 18 speaker-hook entries.
  Six entries toggle port 61h bit 0, but its audible output gate (bit 1) stays
  clear throughout. The aircraft is airborne by clock 3,131,464,612. Added
  `f117run --speaker-log` and `audio_render --speaker-log` so speaker events
  merge into offline renders by guest clock. The complete speaker-aware host
  mix runs through the final clock and is saved as
  `airair-type8-full-host-mix.wav`; the 0–3.8B prefix is byte-identical to the
  prior OPL-only render. Cropping about flight seconds 29 to 41 produced
  `airair-type8-flight-29-41s.wav`; the 3-second MP3 excerpt is
  `airair-type8-flight-32-35s.mp3`. This sortie has no audible speaker
  contribution; independent DOSBox reference PCM and subjective listening
  validation remain open. The WAVs and logs are outside the repository under
  the directory above.
- Completed generated air-to-air objective types 6–8 in addition to the
  earlier type 5. Types 7 and 8 use Vietnam / Conventional War; type 6 uses
  Central Europe / Cold War. Each sortie releases three station-0 AMRAAMs,
  kills special unit 0, records the primary event and earns credit. They
  return with fuel remaining: types 6 and 7 to home 72 and 33 respectively,
  and type 8 to home 33. Each reports result 0/status 3.
  Type 6 uses `central_europe_airair.front`, the default startup clock and
  `--landing-aim 30`; type 7 uses the +270M briefing-delay route at startup
  clock `700000003000000` and `--landing-aim 50`; type 8 uses the +180M route
  at `700000002000000`. Replays are byte-identical under recomp and interp:
  type 6 has 619 observations/hash `d06a5ccf075b2e80`; type 7 has 780/hash
  `11c82f0235e24265`; type 8 has 741/hash `6c3336ef1c24da17`. Route
  definitions and usage are in `tools/routes/README.md`. `airair_pilot.py`
  now waits for initialized runway state before queueing takeoff controls and
  handles close-to-home objectives by holding cruise altitude until aligned
  with the return runway. It exposes `--landing-aim`. Landing validation
  accepts the exact countdown boundary only when VGAME reports result 0/status
  3 and hands off normally with exit code 129. Private runs and input logs are
  under `C:/Users/Tideg/f117-recomp-local/objective-airair-ce-type6-*/` and
  `C:/Users/Tideg/f117-recomp-local/objective-airair-vn-type{7,8}-*/`.
- Reconciled the remaining generated-objective coverage: type codes 1–4 have
  normal-input routes and returns, and types 5–8 are covered by the air-to-air
  routes above. Type 3 retains original D5's no-credit result;
  `cargo_d5_fixed` earns credit with the switchable fix. `strike_return`
  evidence is in the older paired output at
  `C:/Users/Tideg/f117-recomp-local/parity-audit-20261004/`;
  `cargo_return` also has paired return evidence. Updated the roadmap and
  route README to record the completed strike return and keep the independent
  GOG DOSBox cargo reproduction and wider generated-assignment coverage open.
- Checked the current desktop-control state for the roster AVI step. No
  DOSBox-X window was open; `sky.launch_app` for the private DOSBox-X binary
  failed with `GetCursorPos failed: Access is denied (0x80070005)`, and the
  subsequent window inventory contained only VS Code and GitHub Desktop.
  The event-synchronized capture is still open; do not infer a video match.
- The pre-existing untracked `test.bat` was left untouched. No test suite was
  run; the full flight routes and paired replay checks were run. The new
  objective routes, pilot changes, and speaker-aware audio changes are not
  committed; README, roadmap, architecture, route README, landing validation,
  audio diagnostics and this handoff are modified.

### Next

1. Continue roster frame parity when DOSBox-X can be controlled: synchronize
   a known guest event with a captured reference frame, then resolve the
   estimated ~282 ms before the DOSBox-X screen/sprite opens and ~247 ms
   after-open phase difference. The
   corrected DOSBox-X run uses stock `ROSTER.FIL` and retains seven
   `0x08378` calls (four before the screen picture, three after the sprite
   sheet); local traces show 16 before the screen picture and nine after the
   sprite sheet. The added 17-call retrace/PIT block takes essentially the
   same 242-244 ms in both traces, so investigate the preceding palette/setup
   phase. A new run5 check confirmed there is no video-capture command on the
   debugger channel; if native UI control is available, start an AVI while
   the trace run is paused and match its images to the GOG capture. Keep 9 MIPS
   and leave D96 out of translated START until the timing cause is demonstrated.
2. Continue MT-32/audio validation: compare the in-flight host render with an
   independent DOSBox capture and listen to the flight sound. The Type-8
   speaker-aware render covers the complete sortie; port 61h bit 1 stayed
   clear, so the speaker contributes silence. The live same-seed variation is
   explained by block partitioning, and the SDL capture gap is accounted for
   by empty queue checks and a 167-frame zero run.
3. Complete the remaining objective evidence: independent GOG DOSBox cargo
   reproduction and wider generated-assignment coverage; separately pursue a
   rank-6 career for the "General, At Last!" retirement branch.

## Previous continuation (5 Oct, 01:41 local)

- The rank-3 career chain is now paired through retirement. Sorties 53-98
  completed from the saved sortie-52 roster using
  `career-rank3-photo-01/input.log`: 46 paired sorties, 380 checkpoints per
  sortie, matching flight/career gates, observations, input records, and all
  802 roster bytes. Private batch:
  `C:/Users/Tideg/f117-recomp-local/parity-audit-20261005/career-check-53-98`.
- Sortie 99 also passed under both engines from sortie 98's actual saved
  roster. It earned 207 points, advanced total 20,303 to 20,510, and saved
  status 1 (retired). The final checkpoints, records, reports and all 802
  saved bytes agree. Private result:
  `.../career-check-99/completed.json`.
- A separate recomp rerun sampled END once per second. At 58s END displayed
  "Congratulations on the successful completion of your 99th mission and
  retirement from the Air Force." The remark page at 73s says "Maybe I'll
  write a book, like Schwarzkopf. The lecture circuit sounds nice and
  cushy." Screens are in
  `C:/Users/Tideg/f117-recomp-local/parity-audit-20261005/career-check-99-screentrace/sortie-99-recomp/end-058s.ppm`
  and `.../end-073s.ppm`. This verifies rank-3 retirement, not the separate
  rank-6 "General, At Last!" branch (requires total 27,720, average 280).
- Video comparison now distinguishes the DOSBox roster list-only frame
  (`a0fc2584`, 107.311-107.567 s) from selected-pilot details (`eb49f15a`,
  first at 107.567 s). At the default 9 MIPS, local runs reach the two frames
  at 108.124 and 108.423 s; after median capture alignment they are 0.528
  and 0.571 s late. The list-to-details transition adds only about 43 ms
  beyond DOSBox's 257 ms, so most residual delay accumulates before the
  roster list appears. Local `rostscrn.pic` and `rostsprt.pic` open at
  107.481 and 107.696 s. Diagnostic captures at 4 MIPS put the frames
  1.483/1.498 s late (1,052 exact images, 274 reference/262 local unmatched);
  at 12 MIPS they are 0.385/0.428 s late (1,317 matches, 9/34 unmatched).
  Changing the clock also shifts other frames, so the 9 MIPS default remains.
  Reimp D96 is a wait for its native UI, separate from the translated START
  path, and is not evidence to add a guest-side delay here. Interpreter and
  recompiler captures remain identical.
- `tools/video_compare.py` now accepts `--ips` for clock-sensitivity
  comparisons and records it in run settings; `tests/test_video_compare.py`
  checks VGA sampling cadence and shot-time conversion. The 4 MIPS run is
  `video/intro-bddvev98`; the 12 MIPS run is `video/intro-oj9892eq`; both are
  outside the repository.
- Munt diagnosis: interpreter/recompiler MIDI logs are byte-identical
  (188,505 bytes). A matched replay emits a 4,865-byte prefix; two offline
  renders of it have identical PCM. Independent live SDL captures differ in
  PCM despite identical machine logs. Munt's pinned TVP code uses
  `rand() & 3` for hardware-like pitch variation; the app also primes 60 ms
  of silence and can clear audio backlog above 250 ms. Exact live PCM is
  therefore not a stable oracle yet. Isolate host queue effects and compare
  using controlled randomness or timing/spectral criteria. No runtime code
  changed during these diagnostics.
- The main build uses the fully covered 89,276-instruction generation. The
  rank-3 career chain remains paired through retirement as above.

### Next

1. Trace START's roster-entry timing from `rostscrn.pic` through the first
   list frame against DOSBox. Most measured residual delay is before that
   frame, not in the list-to-details transition; D96 belongs to Reimp's
   native UI and should not be applied to translated START without evidence.
2. Continue MT-32 validation with controlled random variation or
   timing/spectral criteria, and isolate live SDL queue behavior before
   drawing PCM conclusions.
3. Continue other objective types and, separately, a rank-6 career for the
   "General, At Last!" retirement branch.

## Prior wrap-up (4 Oct, ~22:45; superseded by the continuation below)

The user asked for a handoff, commit, push and stop, after a session whose
goal was "complete the plan in roadmap.md". Resume the roadmap when asked.

### Committed and pushed this session

- `d8c6b67` README: leads with "fully **human-driven**, AI-coded
  recompilation", the not-fire-and-forget commitment, why the buggy original
  needs preserving, plus the Reimp README's non-affiliation notice, no-money
  pledge, legal section and evidence rule.
- `cfa8e57` dos.c split into dos_memory/programs/files/keyboard/video.c +
  private dos_internal.h. Token-level check: all 85 top-level items moved
  verbatim (mod `static` and a `dos_` prefix on 29 shared functions, all call
  sites); two log tags the earlier rename pass had changed were restored.
  Six routes x both engines x before/after identical (checkpoints, finals,
  every shot and save file).
- `83b4c8a` code overrides (src/recomp/recomp.c, src/fixes/fixes.c,
  `--fix ID` / `--list-fixes` in f117run, f117a, `fixes=` in the Python
  machine API), first fix D5 (supply drops credited; route
  `cargo_d5_fixed`, both engines 149 checkpoints, final `61fc0505fcee1ba3`),
  `cargo_return` route (delivery then home 51 after hits; 380 checkpoints,
  final `98791aa3b6459905`, strong observer `cargo_check.py --complete`),
  `career_check.py --ahead N` (pipelined engines), landing_pilot `cruise`,
  `aim`, `approach_speed` params (defaults unchanged).
- `9e9eaa2` input logs name the fixes a session ran with
  (`# f117r-fixes D5` second line); replays switch them on.

### Committed in the wrap-up commit (verify CI)

- Recompiler isolates every override address in a one-instruction region,
  reading the sites straight from `OVERRIDES` in src/fixes/fixes.c (regex in
  recompiler/recomp.py `override_sites`; keep each row on one line).
  Regenerated code differs only in START and VGAME; same 89,276
  instructions. NOTE: the interpreted ~750M instructions on cargo routes
  are the emulated BIOS idle stub 0060:00C4, not game code (an earlier doc
  claim that D5 cost a tenth was wrong and was corrected).
- D4 (`--fix D4`): `file_data` machine hook corrects 5 world bytes as read
  (LB.WLD 0x53A, ME.WLD 0x597, NC.WLD 0x607 0->1; NC 0x1F1/0x201 08->09,
  pinned by name, size, shipped byte); override at START entry 0000:8EDC
  widens the 4 airstrip masks (DGROUP 0A95:11DA) to 0027h after LZEXE
  unpacks. Evidence: Libya generator offered supply drop at airstrip
  target 26 (startup 700000036000000, private lb-candidates-d4); VGAME loads
  NC airstrips with flags 09 (d4_nc_check). Flying one remains open.
- D34 (`--fix D34`): five overrides (VGAME entry E6BE empties extension;
  append 0F97, lookup 0FBD, type write 0F7E, type read 0D5E), declining
  until the table holds 30. `tools/d34_check.py --stage [--fix D34]` uses
  the new STAGING-ONLY `stage_write16` API: unfixed, record 31's type 0x4A
  lands on B838 (Reimp's measured value); fixed, count stays 30, log shows
  record 31 kept past the table and found by the game's lookup, both
  engines `13110a036af486ba`; unstaged run unchanged `197c6b398d6fbb9f`.
- `mission_candidates.py --fix`, `F117R_MACHINE_API` env override for the
  DLL, optional binding of new DLL exports, tests: `code_overrides` CTest
  (overrides, log line, data corrections), `tests/test_recompiler_overrides.py`
  (added to CI), cargo verdict regressions.
- Full parity of this 7-site generation (separate build
  `~/f117-recomp-local/isolate2-build`, gen `isolate2-pipeline/gen`,
  `all_routes_parity.py`): ALL 27 ROUTES IDENTICAL under both engines at
  every checkpoint and final (committed finals unchanged, e.g. strike
  `197c6b398d6fbb9f`, career_promotion `2cbd245e873655d5`), lockstep
  89,276 starts / 5,713,152 states / 0 mismatching (`isolate2-parity.log`,
  `isolate2-parity/result.json`). 11 CTests (app build) and 13 Python test
  files pass.
- The main `build/` still holds the pre-override generation
  (theatre-pipeline/gen). Next session: run
  `py tools/build_recomp.py --data "D:/GOG/F-117A"` (or build with
  `-DF117R_GEN_DIR=.../isolate2-pipeline/gen`) so build/ matches master.

### Career toward retirement (in progress, private evidence)

- Sorties 14-15 paired (career-check-14-15); rank 2 -> 3 (Captain) earned at
  15, total 3122. Old rank-2 record then failed at rank 3 (assignment
  changed). New rank-3 record: private `career-rank3-photo-01/input.log`
  (startup 700000009000000, departure 36 / home 35, both photos, landed,
  result 0/3, final e78a0dfa951114ae), front route `career-rank3-photo.front`.
- `career_check.py --ahead 3 --steps 17000000000` batch from sortie 15's
  save PAIRED SORTIES 16..52 (37 sorties; `career-check-16-55/completed.json`,
  log `parity-audit-career-check-16-55.log`), stopped for this handoff.
  Last paired: sortie 52, rank 3, score 207, total 10,781, final
  `b80929026b785d0c` at 17B. Its save:
  `parity-audit-20261004/career-check-16-55/sortie-52-recomp/save-jlej_zhh/Roster.Fil`.
  Resume: `py tools/career_check.py --data D:/GOG/F-117A --initial-roster
  THAT_SAVE --replay .../career-rank3-photo-01/input.log --steps 17000000000
  --count 46 --ahead 3 --out .../career-check-53-98` (about 1.3 min a
  sortie), then handle 99 separately (debrief END pages differ at
  retirement; the tool requires START at the end - check the 99th's screens).
- At 207-219 per sortie the average stays ~210 < 250, so rank stays 3 and
  the record should keep working to 99. Retirement should reach END's
  "Wow, 99 missions!" remark list. The user's screenshot
  (Desktop\328920_screenshots_2015-10-21_00003.jpg) is END's remark page
  `0x039D0` list "- General, At Last!" (Off we go into the wild blue
  General's Office / Now I'll leave the fighting to them / When in doubt,
  get a lieutenant to do it) - shown only for the 99th mission at rank 6,
  which needs total >= 27,720 and average >= 280: a different, higher-scoring
  career. Strings are in END.EXE around image 0xA9F1.

### Other open items noted this session

- Fixes left from the Reimp's set: D2 (speech busy-wait; needs a non-blocking
  speech path on ASOUND 0x2552) and D96 (START page timing). Policy for the
  app's default (Reimp: all fixes on) vs parity default (off) is the user's
  call; currently every fix is off unless named.
- Estimate given to the user: ~35-40% of the whole roadmap by effort,
  ~6-12 weeks left (Phase 2 naming and Phase 3 60fps/4K dominate).
- DISK: C: was full during the wrap-up (a link failed with LNK1180; 29 MB
  free at one point, 13 GB after). Check free space before long runs; the
  scratch builds head-build, dos-split-build and isolate-build were deleted.
- Separate builds used this session (not the repo's build/):
  `dos-split-app-build` (app ON, theatre gen) and `isolate2-build` (app OFF,
  the committed generation); scripts `build-*.cmd`, `route_cli.py`,
  `all_routes_parity.py` in `~/f117-recomp-local`.

## Previous continuation (4 Oct, ~23:15 local; superseded above)

- Resumed the video-parity item. The main build is now configured to use the
  fully covered 89,276-instruction generation at
  `C:/Users/Tideg/f117-recomp-local/isolate2-pipeline/gen`; MSVC/Ninja build
  completed. Its 27-route parity and 5,713,152-state lockstep were already
  completed in `isolate2-parity.log` against this generation.
- Fresh comparison `video/intro-szsaom2z` against the independent DOSBox
  capture `intro-caauys37`: 1,319 exact pictures, seven unmatched reference
  pictures, 28 unmatched shots, all one sample, final drift -570.671 ms.
  Captures against two other independent reference AVIs report -584.938 ms
  (`intro-thjqg7hw`) and -570.671 ms (`intro-z8ncy_nf`).
- An interpreter-only capture against the same AVI (`video/intro-interp-current`)
  has exactly the same shot hashes and timestamps as recomp, and the same
  final hash `e4114c0415d4920d`. Across three distinct DOSBox video captures,
  the stable roster image begins between 107.510 and 107.567 s; both local
  engines begin it between 108.411 and 108.435 s. After global alignment the
  repeatable transition drift is 0.571-0.585 s. This rules out generated
  code as its source; the shared machine timing around START's first roster
  page remains to trace. Do not claim frame parity.
- The initial `build_recomp.py` invocation was stopped during its redundant
  full-route rerun after it regenerated only 89,216 starts. The main app was
  then rebuilt against the verified complete `isolate2-pipeline/gen` above.
  The committed independent 27-route results remain the full parity evidence.
  No tracked source code changed in this continuation; the pre-existing
  untracked `test.bat` was left untouched. No processes remain running.

### Next

1. Continue tracing the stable ~0.58-second START transition difference;
   the interpreter/recompiler comparison rules out generated code.
2. Career chain is paired through sortie 52. Resume 53-98 from the saved
   rank-3 roster with:

   ```powershell
   py tools/career_check.py `
     --data D:/GOG/F-117A `
     --initial-roster C:/Users/Tideg/f117-recomp-local/parity-audit-20261004/career-check-16-55/sortie-52-recomp/save-jlej_zhh/Roster.Fil `
     --replay C:/Users/Tideg/f117-recomp-local/parity-audit-20261004/career-rank3-photo-01/input.log `
     --steps 17000000000 --count 46 --ahead 3 `
     --out C:/Users/Tideg/f117-recomp-local/parity-audit-20261005/career-check-53-98
   ```

3. Then continue Munt's reference PCM check and other mission objectives.

## Previous wrap-up (earlier on 4 Oct; superseded by the section above)

The user requested a handoff, commit, push and stop. No test worker remains
running. Earlier "running" entries below are historical; use this section
as the current state. Resume the full roadmap only when the user asks.

- Commit6dff654 is pushed: secret-strip return, paired earned-career tool,
  actual pilot-status assertions, controller/candidate improvements and docs.
  All73 Python tests pass. The latest Munt interpreter build has9 passing
  CTests. No new machine semantics are committed in this wrap-up.
- Paired earned sorties7..13 completed under both engines. Private outputs
  parity-audit-20261004/career-chain-05-NN-{recomp,interp}, each380 checkpoints,
  identical input/CSV/result/save802 bytes and true saved status800=0.
  Actual sortie13 save is career-chain-05-13-recomp/save/Roster.Fil:
  rank2, score219, total2684, sorties13, hash4cd28bafd25577e6 at19B clocks.
  Sortie14 recomp finished total2903/hash30de30869606bc5e; interpreter was
  interrupted for this handoff, so14 is NOT paired/accepted. Restart from
  actual13 using tools/career_check.py and private career-seven-photo-01/input.log,
  steps19B, count2 to approach expected promotion15. Stop if assignment changes.
  Verified paired summary: career-chain-05-verified.json. Old helper prints
  an incorrect354 count and reads theatre778 as status; its actual saves
  were separately checked at800. Use the corrected public tool going forward.
- User supplied ROMs at C:/Users/Tideg/f117-recomp-local/roms. Use full
  mt32_ctrl_1_07.rom +mt32_pcm.rom; their SHA1 matches Munt's known originals:
  b083518fffb7f66b03c23b7eb4f868e62dc5a987 and
  f6b1eebc4b2d200ec6d3d21d51325d5b48c60252. Other old/new revisions and
  split images are present; no CM32L pair. Prefer first-generation1.07 for
  the game's reported D36 later-hardware engine-sound issue. No ROMs in git.
- Found/fixed a real host bug: main.c loaded Munt, then unconditionally
  destroyed/recreated audio before machine_boot, losing the synth. Read the
  replay clock before creating audio once. Offline original Roland log now
  renders6,370,000 stereo frames at44.1k with MicroProse LCD messages.
  SDL disk-driver live30-second original-game replay produces audible PCM;
  the same replay without synth is exactly silent. Private diagnostics:
  munt-audit-results/live, including final recheck after restoring dos.c.
  Replay header9MHz overrides CLI1MHz; completed at270M clocks in~30.8s.
  Initial analysis wrongly interpreted raw SDL PCM as floats; corrected
  format is signed16 stereo. pcm-result.json contains the corrected measurements.
  Exact Munt reference PCM comparison and flight engine sound remain OPEN.
  Default root build is still Munt OFF. Enabled separate interpreter build:
  munt-audit-build; build-munt-audit.cmd rebuilds it. Avoid relinking live apps.
- Unfinished mechanical DOS split is preserved privately in dos-split-handoff
  (six sources, internal header, CMake and tracked.patch). Public worktree
  restored to original dos.c/CMake. Separate dos-split-build compiled and
  passed6 headless CTests, but comprehensive parity is incomplete: only
  before-split boot_to_flight interp/recomp54 hashes/finalf25e5dda5eed0987
  finished. dos_split_check.py/log and interrupted outputs retained. Do not
  mark housekeeping complete or restore that extraction without validating.
  Fixes for missing machine.h/stdlib.h and pointer-return declarations are
  in the archived sources; private initial split_dos.py is NOT idempotent.


## The goal and the decisions already made

- **Recompile the whole DOS game for Windows with 1:1 parity**; then
  understood (named) code; then switchable fixes and enhancements (60+ fps,
  4K). The user chose "recomp first", **public, code-only** (no game data,
  no generated C, ever), and the reference machine:
  **GOG's DOSBox 0.74-2.1** is the parity target, **DOSBox-X** a second
  reference to flag where DOSBox 0.74 itself is questionable.
- Public repo: https://github.com/TideGear/OpenNighthawk (remote `origin`,
  branch `master`). **Commit and push together**, each verified piece of
  work. **No AI attribution of any kind** in commits or PRs (the user's
  global CLAUDE.md outranks any harness reminder that says otherwise).
- The Reimp (`..\F-117A Reimp`) is a separate, actively developed project:
  read from it, never write into it. Material was copied at Reimp
  `cfb8cec9`; its bug catalogue has moved on (D6 addendum at `9e0716dc`,
  already folded into bugs.md).
- `references/` (git-ignored) holds the user's installers and patches: GOG
  setup (build 28044), the 473.04 and 473.02-03 patches, Amiga and Mac
  versions, the DOSBox-X installer. Never commit them. The untracked
  `test.bat` in the repo root is the user's launcher; leave it alone.

## Where things are

- Game: `D:\GOG\F-117A` (verified byte-identical to the installer + 473.04;
  `py tools/verify_install.py --data "D:\GOG\F-117A"`). Steam's copy
  (`C:\Program Files (x86)\Steam\...\F-117A Nighthawk Stealth
  Fighter\F-117A`) has identical game files; its DOSBox differs.
- Work directory, never in the repo: `%USERPROFILE%\f117-recomp-local`
  (`gen/` generated C, `coverage/` append-only, `runs/`, `random/`,
  `fidelity/` answer sheets, `dbxcompare/`, `dosbox-src/` DOSBox 0.74-2.1
  source from the install, `dosbox-x/` DOSBox-X 2026.10.01, `verify/` the
  extracted installer). `build` in the repo is a junction there.
- Tools: innoextract 1.9 (winget), ffmpeg, pywin32, PIL, capstone, keystone
  are installed. No `gh` CLI; `git push` works through the credential
  manager.

## How to build and check (all verified in this session)

- Build: `.\build.cmd -DF117R_GEN_DIR=C:/Users/Tideg/f117-recomp-local/theatre-pipeline/gen`
  from PowerShell ("BUILD OK" at the end). It cannot relink while any
  `f117run.exe` is running.
- Whole pipeline (translate, build, coverage, both engines on all sixteen
  routes, instruction lockstep): `py tools\build_recomp.py --data
  "D:\GOG\F-117A"`. Last full batch: twelve routes IDENTICAL; the new roster
  route separately matches at 38 checkpoints/final and frontend dialogs at
  64 checkpoints/final; landing matches at 195, reconnaissance at 123.
  Lockstep 89,276 starts,
  0 mismatching; no generated code changed for the roster work.
  `--parity-only` skips translation/build and checks the existing executable;
  the pipeline compares every 50-million-clock checkpoint, not just finals.
- Machine against GOG DOSBox: `py tools/fidelity.py --data "D:/GOG/F-117A"
  [--diffs-only] [--reference dosbox-x]` - last: every comparable answer
  agrees (a 4 KB read's cost within 5%).
- Music against GOG DOSBox: `py tools/dosbox_compare.py --data
  "D:/GOG/F-117A" --seconds 130` (opens a minimised DOSBox; a keypress in
  its window disturbs the timing). Last: 22,687 writes, 100.7 s, identical.
  New runs use unique `dbxcompare/music-*` directories. `--reuse RUN_DIR`
  rechecks saved OPL data. Different writes now exit 1; older tool versions
  printed differences but exited 0. The known random note is not filtered.
- Pictures against GOG DOSBox: `py tools/video_compare.py --data
  "D:/GOG/F-117A" --seconds 130 --diagnostics`. Exit 1 currently means
  pictures differ, not a tool failure. Replay the comparison without a new
  capture: `py tools/video_compare.py --reuse RUN_DIR --diagnostics`.
  `--against RUN_DIR --data INSTALL_DIR` retains its reference video and
  makes fresh current-build shots in a new private directory.
  Requires ffmpeg, ffprobe, Pillow and pywin32. About 2 GB of shots per run.
- Random flights: `py tools/random_flights.py --data "D:/GOG/F-117A"
  --seeds 31,32 --span 4000000000`.
- Coverage of the routes: `py tools/exercised.py --data "D:/GOG/F-117A"
  --run` (56.2%). Translated share: `py tools/census.py --data
  "D:/GOG/F-117A"` (96.2%).
- Planted defects: `py -u tools/mutation_check.py --data "D:/GOG/F-117A"
  --lockstep --random 60 --kinds skip,cf,zf,ax --seed N` (about a minute a
  mutant; keep runs under two hours - background jobs are stopped there).

## In flight at handoff

- Latest verified continuation: secret_airstrip_return.args/input completes
  delivery, normal ground turn/taxi/takeoff, and return to home58. Strong
  observers strip-return-verified-{interp,recomp} input/CSV/result identical,
  errors empty; 372 CLI checkpoints/final c7b303ee1aa60173 at18635798708.
  Stop (2880,11786), ground0, idle/gear/brakes, fuel3000, parent0/status3,
  actual VGAME exit129. Strip intact at delivery, damaged later with player
  8655 units away and zero releases/hits. Report later damage explicitly.
  Route minimum flight bound corrected from an assumed15B to14B; actual
  completed flight spans14858442368 clocks. Coverage139381/230052=60.6%,
  25 routes. 73 Python tests pass; no core semantics changed.
- Public tools/career_check.py self-test career-tool-check-01 earns sortie8
  from actual earned7 roster. Both engines' 380 checkpoints, input/CSV/result
  and 802 save bytes agree, final ce0867bc52a1995b; total1370->1589, score219,
  rank2, sorties7->8. Independently agrees with private career-chain-05-08.
  IMPORTANT: saved pilot status is record+4Eh, Serge file800. Previous
  status778 was actually theatre. Actual active status800 is rechecked0;
  public career fixtures now assert both theatre778 and status800.
- Private career_chain_05.py is running sequential paired earned sorties7
  onward, stopping at next rank promotion. Log parity-audit-career-chain-05.log
  under C:/Users/Tideg/f117-recomp-local. Outputs parity-audit-20261004/
  career-chain-05-NN-{recomp,interp}. Its printed354 hash count is stale:
  actual hashes.json contains380 per leg. Sorties7..11 paired verified;
  rank2->3 expected near15. It copies only preceding actual saved roster.
  At promotion find a normal photo/photo candidate using mission_candidates
  --initial-roster ACTUAL_SAVE --both-objectives, then record recon_pilot
  --time-us NEW_SEED --acquisition level --complete. Strongly pair the new
  record; use public career_check for later earned batches toward99.
  Do not fabricate near-retirement stats. Latest rank2 record is private
  career-seven-photo-01/input.log, startup700000007000000; seven strong
  observers match, final15428427129/97eca1f5d7e391c1. Further debrief replay
  uses19B clocks. Old rank1 record becomes a ground-strike assignment at7.

- Latest controller continuation: optional --acquisition level added to
  recon_pilot and dosbox_flight (defaultnose remains unchanged). Before
  designation it holds positive pitch and requestsN only at550..750range,
  actualpitch>=0, headingerror<1200, usingoriginalconstant640ray. After
  acquisition normalcameraoffsetapplies. Regression coversrange/heading/nose
  gates;59Python pass. Recomp adaptive recon-level-07 completesbothphotos
  and raisedhome36return; replay observers recon-level-07-verified-{interp,
  recomp} input/CSV/result bytes identical, errors empty; finalDSWAP
  13672636250/91b4bdf6f00ff61d. Controller committed/pushed in620bf05;
  CI passed. Independent GOG dosbox-recon-07 completed: errors empty,
  104757 observations, both intact photos/events and raised-home36 return.
  Stops(19199,9452), altitude128, idle, gear/brakes, fuel3718, countdown2/S9,
  parent0/status3 and END observed. Strict gates rechecked from flight.csv.
  Wall-time inputs and different generated mission do not prove exact
  dynamic-state equality. Output dosbox-recon-07 and associated audit log.
- Cargo found in pg-candidates-04 atcase45000000/startup700000019000000:
  primarytype3,target24=(10496,3840),departure58,home51, station0cargo18
  /model38/store1; secondaryphoto. Search03 finished8caseswithnocargo.
  Trials01..05 retained failures: no release, crash, off-area expiry,
  ground-adjacent expiry or off-area obstacle impact. Trial06 succeeds with
  full-throttle approach and steeper release belowpitch-1000: slot11/class38
  /weapon18 impacts(10500,3951),z-5, octagonalrange113<256,time588<1094.
  One release/store1->0, no original8B/4000credit, aircraftairbornefuel7084.
  cargo-verified-{interp,recomp} input/CSV/result identical;149hashes/final
  155c7328edcbe82c at7488092689 agree. Impact cockpit reviewed. New cargo
  fixture extends1.2seconds to check retainedno-credit; retained observers
  input/CSV/result identical,149hashes/final20b78dd06d275670 at7498892689
  agree. Fuel7075/alt444, original no-credit retained. Independent DOSBox
  cargo reproduction and aircraftreturn remain open.
  Strongobserver cargo_check.py and5 regression cases added;64Python pass.
  Coverage139059/230052=60.4%,24routes. No game semantics changed.
- Retirement preparation: oneadditionalnormalSerge sortie replayedfrom
  earned03savedroster (no edits), privatecareer-four-recomp, exec93371done.
  Savedscore217,total500->717,sorties3->4,rank1/status0. No retirement claim.
  Private career_chain.py now running exec52167/logparity-audit-career-chain-02:
  paired sorties4..10 fromactual public-thirdsave, ordinary replay+ENDkeys,
  strictbothphoto/landingparent gates,354hashes and802savecomparison eachleg.
  Sorties4/5/6 passbothengines: strictphotos/homeparent gates,354hashes,
  input/CSV/result and802savebytes agree. Totals717/934/1151; rank1/1/2.
  Seventhfailedstrictflight: rank2 changesassignment, existingrecord notvalid;
  no careeradvancementclaim. Privatefirstseventhoutputretained (run/input logs).
  Initial harness attempt failed because instruction clock can overshoot
  sampletarget; corrected >= sampling, preservedfirstoutput.99
  sorties retire; do not synthesize near-retirement roster to claimearned.

- Optional Munt integration implemented: F117R_WITH_MT32EMU OFF default;
  enable with installedMT32EmuCMakepackage orF117R_MT32EMU_SOURCE path.
  Separate Munt2.8.3 source atprivate munt-src, commit6e7c01fba7e1d50c8fa705834889fd0eac136075.
  --mt32-control/--mt32-pcm userROMpair; sourcebuildsharedDLLcopiedbesideexe.
  MIDI audio advancesonmachineclock, boundedMunt44.1k render/mixbeforeclip.
  Headless--midi-log +audio_render--mt32 supportofflinevalidation. MissingROM,
  incomplete/conflictingoptions, rejectedMIDI/overflow failexplicitly.
  Enabledprivateinterpreterbuild9CTests, defaultgeneratedbuild10CTests,
  64Python pass;7secDBOPL/Nuked308700stereoframes byteidenticalbefore/after.
  Rolandroute26hashes/finalf075fcb3add06eb2 +13788MIDIclock/bytes equal.
  Private munt-audit-build/results; logsparity-audit-munt-*. NoROMs available;
  actualRolandrender/reference/listeningvalidation staysopen.

- Secret-airstrip verified: strip-trial-02 normal adjusted descent aim
  completed primary4 atPGtarget24, (10497,3809), box1/nearest24, ground0,
  idle/gear/brakes, fuel7261, event8B1, primary4000, missionstore1->0,
  strip intact/no ejection. VGAME remains active; no home return claimed.
  New airstrip_check.py strong gate +four regressions;58Python/9CTests pass.
  Strong strip-verified-{recomp,interp}: input.log/flight.csv/result.json
  byte-identical, errors empty. CLI strip-cli-{recomp,interp}167 checkpoints
  and final match8373782617/5762b7e88b22cda9. Credit PNG reviewed.
  Public secret_airstrip.args/input uses ordinaryK/M atstartup700000005000000.
  Coverage secret_airstrip.cov copied toexec-theatres-20261004; union
  138771/230052=60.3%,23routes. First prototype touchedground outside short
  strip box atspeed175 and gotno credit; retain failedstrip-trial-01.
  CI2cdf614 passes after shortpath-test fix (16 route tests, not17).
  GOG dosbox-recon-06 has finished:104607 observations, no credited photos,
  fuel0at1141.688secflight, parentresult1/status1,9acceptanceerrors.
  No live reference run; next controller acquisition should consider the
  original ray's rounded vg_vsin(angle,32)+1 divisor (vgmath.h168, sine
  table2084 and rounded1.15 multiplication). Continuousatan approximation
  drove it low and into circles. A level nose casts a constant640-map-unit
  ray, an alternative ordinary acquisition approach to test, not a model fix.
  Next live cargo search: pg-candidates-03, startup700000006000000 plus
  one second percase, eight frontend delays, stop only ontype3. Log
  parity-audit-pg-candidates-03.log. Candidate02 completed ontype4 at5seconds;
  candidate01 fixed-clock controls all generatedsamephoto/photo. Verify03
  before reusing its ordinaryinput record/clock for a drop test.

- CI follow-up:46151f1,8c997de,316eac4 failed Route milestone regressions;
  build/CTests/pacing passed. Reproduced locally by setting tempfile.tempdir
  to Windows8.3 workspace alias: seed-path assertion compared spelling,
  while helper correctly canonicalized it. Changed assertion to samefile;
  all17 route tests pass under8.3 alias and all54Python pass normally.
  Commit/push this verified fix; check subsequent CI rather than old failures.
- Secret airstrip candidate found: pg-candidates-02 delay450M/startup
  time_us700000005000000: primarytype4 target24=(10496,3840), departure/home58
  (2880,11840), PG Limited War/Strike, cargo18/model38 atstation0, camera
  atstation1. Private strip_trial.py (normal controls, readonly observations)
  uses landing_pilot.control with a local working destination atstrip24;
  no guest state edits. Live exec18479, output strip-trial-01, log
  parity-audit-strip-trial-01.log. Requires original primary4000/8B event,
  stopped inside strip approach box, consumed mission store, fuel/no loss;
  no acceptance claim yet. If successful, formalize dedicated observer and
  replay with its recorded startup clock under both engines before fixture.
  Original secret-strip flag may suppress automaticmission end, so distinguish
  primary completion from home return. Original VGAME041C6 only checks
  type4 objectives inside approach box, speed<=1, every16frames, beforedeadline;
  frame_objective_mark adds8B and setsprimary4000, zerosstore at3672.

- Latest verified public career chain: exec66330 completed0. Fresh public
  interp and recomp-02 agree at all354 hashes/final2cbd245e873655d5/17.7B,
  full saved careers identical. Commit46151f1 pushed, includes chain helper,
  two normal-input career fixtures, tests/docs.53Python pass at that commit.
- Next objective search underway: uncommitted tools/mission_candidates.py
  and tests/test_mission_candidates.py (54Python pass). Uses normal frontend
  inputs and read-only strike_state, records every candidate's input/header.
  Private pg-strike.front selects PG/Limited War/Strike Missions. Candidate
  search01 exec7861 varies briefing input delays at fixed startup clock;
  first two cases identical photo/photo mission, proving the RNG was already
  seeded. Current tool now varies startup hardware clock by1second per case,
  records time_us explicitly; no RNG or guest RAM edits. Search02 private
  pg-candidates-02, parity-audit-pg-candidates-02.log, eight delays, stop on
  either type3(cargo) or4(strip). Check before claiming a usable candidate.
  Replay must preserve its recorded clock; current recon/strike pilots still
  require the fixed700e header, so adapt explicitly for a new clock if used.
  Search02's first four cases now genuinely differ: types1/1,2/1,1/2,2/1
  at startup700e+0/1/2/3seconds. Tool behavior verified; no special candidate
  yet. Candidate search01 (fixed clock) intentionally retained as evidence
  that briefing delay alone does not change generation. All54Python and
  9CTests pass. Utility and regression ready to commit with this evidence.
  GOG dosbox-recon-06 still running without credited photo, wronglock0/20
  around low-altitude primary. No independent complete sortie pass yet.

- Promotion continuation: both03 debrief engines finished,354 hashes/final
  identical2cbd245e873655d5 at17.7B, all802 saved bytes equal. Promotion and
  Airman's Medal pages reviewed in private promotion-contact.png: First
  Lieutenant, rank1,total500,sorties3,medal at758 goes1->2. New public
  career_serge and career_promotion normal K/M input fixtures;22routes.
  run_route # seed-roster earns prerequisite afresh with selected engine,
  validates its gates and copies only802-byte roster into fresh save.
  Pipeline uses same helper; cycles/failed prerequisites rejected;53Python
  pass. Fresh public recomp chain02 exec46827 completed0,354 hashes equal
  verified private interp, final save equal and prerequisite roster equal.
  Initial public recomp failed an incorrectly specified12Bflight minimum;
  actual first flight11.313987B, corrected both routes to11B. Keep failure.
  Fresh public interp exec66330 still live, finished its prerequisite and
  currently promotion flight4.55B. Check parity-audit-career-promotion-public-interp.log
  and output career-promotion-public-interp, save-path.txt names fresh save.
  Compare all354 hashes/final and full save against public recomp-02 after
  completion. Coverage career_serge.cov and career_promotion.cov copied from
  verified private interp to exec-theatres-20261004; union138584/230052=60.2%.
  Independent dosbox-recon-06 still live, ~615secflight, no photo credit,
  circling primarytarget2 atlowaltitude180..255, wronglock0/20. No game model
  defect established. Only controller/replay tool changes, no guest RAM writes.
  Next objective types need Persian Gulf strike missions: original D4 masks
  secret strips/supply to PG; strip needs quality above Green. D5 cargo
  impacts never award credit in473.04, so parity test must preserve that bug.

- Latest continuation: c4070a0 pushed; CI success (also f01e170). Recon
  pilot now distinguishes the physical designation ray from the camera
  0x6EF photo offset. Unit regression added;50Python pass. Fresh normal
  adaptive recon-acquisition-06 earned both photos and landed, but coarse
  sampling missed countdown3/S8. Added the same fine read-only sampling
  used by strike; recon-acquisition-06-verified-{recomp,interp} now both
  pass, final DSWAP13836105984/f4ae7ae4e610850d. All three observer files
  (input/CSV/result) were compared and are byte-identical.
  Optional --initial-roster copies an earned802-byte roster unchanged to a
  fresh private save; no state fabrication. Serge03 seeded from02 debrief
  SHA25605a58bcab9d4a68dbd5d1ea0db2c6d32f9d3c93be8134691ad1e5cccac442438.
  Original END table: total thresholds300/1125/3000/7000/16000/27720,
  average100/150/200/250/280/280, sorties2/5/10/20/40/99, plus training gate.
  Serge02 total283 is17points short;03 normal both-photo return passes,
  DSWAP14558070621/24a4e53ec0a1134d, fuel4449, stop(19204,9455), result0/3.
  Live03 debrief recomp exec59035, private promotion-serge-03-debrief.args
  steps17.7B, output promotion-serge-03-debrief-recomp preseeded from earned02.
  Inspect rank754/score770/total772/sorties776 and promotion screenshot;
  run interp with identical earned initial save and compare hashes/saves.
  UPDATE03 debrief recomp finished: rank0->1, score217,total283->500,
  sorties2->3,status0. Earned promotion saved; screenshot still to review.
  Fresh interp debrief started with02 earned save copied unchanged,
  parity-audit-promotion-serge-03-debrief-interp.log,17.7B budget and coverage
  promotion-serge-03-debrief.cov. Check process/session terminal before
  comparing all354 hashes/final and802-byte save. No public chained route yet.
  Serge02 interp debrief exec81560 has finished:350 hashes and final
  a069f6775d65b94b/17.5B match recomp, all802 save bytes equal.
  Coverage at auditroot/promotion-serge-02-debrief.cov.
  Independent GOG dosbox-recon-06 live exec71596/Python30976/DOSBox4836,
  ~75sec flight, primarytype1 target2=(4768,14144), secondarytype1 target1,
  home36.1900sec limit. Shared controller fix loaded at launch. No photos
  credited yet; do not claim reference pass. Log parity-audit-dosbox-recon-06.log.
- Goal continuation explicitly supplies unbounded budget; earlier repeated
  usageLimited status-only turns made no progress. Continue pending tests
  and the full roadmap; do not stop because the old tool status was stale.

- VERIFIED strike_return: normal primary strike and home33 landing.
  Strong observers strike-return-02 and strike-return-verified-interp have
  identical input.log, flight.csv and result.json, errors empty. Fine
  read-only128-clock sampling observes counter3/S8 before DOS exit, keeping
  the strict threshold. Parent result0/status3, fuel2414, stopped(9792,1536),
  idle/gear/brakes, primary damage and hit credit retained. CLI297 hashes
  and final match14872594982/a4f05231eacf9d75. Stopped PNG reviewed.
  Coverage copied as exec-theatres-20261004/strike_return.cov (underscore
  required to match route stem): union134132/230052=58.3%.20routes/49Python,
  9CTests pass. Recon return/career coverage remains pending.
- Promotion candidate02 selected Serge correctly: header9, both photos and
  return pass. Debrief completed and saved score217,total66->283,sorties1->2,
  rank remains0, status0. Do not claim promotion. Check all three original
  thresholds and training gate in Reimp core/endreport.c; the prior assumed
  eligibility was insufficient. Private promotion-serge-02-debrief-recomp
  and parity-audit-promotion-serge-02-debrief-recomp.log are complete.
  Candidate01's (100,146) click selected Hugo; corrected02 used(100,136),
  then formContinue(258,182). No fabricated save data.
- Independent dosbox-recon-05 is finished at its1900-second limit: no
  credited photos, wrong lock0 while circling primary1(26304,24884).
  No live reference process remains. Preserve as failed controller check.
  Next fix: designation uses physical negative nose pitch; only photo cue
  pointing includes camera offset0x6EF. Current pilot applies it before
  designation too. Test that distinction and start a fresh reference run.

- NEW strike objective: `tools/strike_pilot.py`, `strike.args/strike.input`,
  normal Libya training primary3 destroyed with weapon5=Maverick. Initial
  station0 stores2->0, releaseevents2, matchinghit1, flag5005, damaged1,
  fuel4566, altitude2355, speed610, ejection0. Stops airborne after credit.
  Strong replay observers `strike-verified-{interp,recomp}` pass and input/
  CSV bytes match. CLI175 hashes+final match197c6b398d6fbb9f at8797594941.
  Credit PNG reviewed. Recompiled miss profile contains only outside-module
  BIOS starts; no known-game-module miss coverage file. Interpreter coverage
  auditroot/strike.cov copied toexec-theatres-20261004/strike.cov; unionnow
  133926/230052=58.2%, excludes freshreturn/careercoverage.19routes/48Python.
- Independent attempt04 credited primary but circled moving secondary2 with
  neighboringobject16 designated. Stopped with75428 observations; failed
  result retained. Recon controller now points the sensor at nearby targets
  before designation too; a regression covers that gate. Live attempt05:
  `dosbox-recon-05`, `parity-audit-dosbox-recon-05.log`, execsession45666.
  It generated two photos, target1=(26304,24884), home36.1900secflightlimit.
  Previous session8133/attempt04 is terminated. Check05, not stalehandovers.

- Latest audio probe: new`tools/opl_probe.py` builds original ROM-free16-bit
  COM via fidelity.Asm, capturesWAV with normalCtrlF6 andSpace. It revealed
  DOSBox mixer equal-rate fractional interpolation (14bit remainder16383).
  Fixed `audio_mix.h` applies the reference rounding before2xgain, both
  app and independentrenderer. Probe`opl-tone-05` matches86,524 consecutive
  stereo frames / first44,100 entirely, until firstfrequencychange1.962s.
  BIOS tick phase changes note lengths; fullPCM still fails, preserved.
  Sustained-tone C tests pass;9CTests/44Python. EarlierNuked transient during
  setup produces firstnonzero94 ratherthanactualkey-on44100; do not claim
  Nuked parity. Earlier`opl-tone-03` usednon-sustainedoperators and was only
  a shortattack/release; latest05 usesEG_TYPE21sustained tone.

- Newest work after the initial audit: CI pacing now excludes SDL startup
  and uses1MHz/four seconds to avoid interpreter throughput on shared runners.
  Commit6668230 CI passes; failed CI outputs are uploaded. Corrected recon
  pilot restores power below240 knots; adaptive rerun still reaches the
  established return hash. Independent attempt02 credited one photo then
  crashed at44% throttle. Attempt03 generated a strike secondary and was
  stopped as unsupported, with14768 observations retained. The observer now
  rejects non-photo/photo candidates before takeoff and waits through DSWAP.
  Live attempt04: private`dosbox-recon-04`, log`parity-audit-dosbox-recon-04.log`,
  exec session8133, same normal recon.front route,1900-second flight limit.
- App now defaults to DOSBox DBOPL at44100Hz with2x gain; `--opl nuked`
  retains Nuked, using its resampler at44100Hz. Vendored GPL2+ source is from
  GOG's supplied DOSBox tar; README records hashes and wrapper-only changes.
  C++11 required only when building the app. Nine CTests and43 Python tests
  pass. `audio_render` defaults to app DBOPL; optional fifth arg`nuked`.
  New`dbopl_render` is an independent512-sample renderer. Its full intro
  differs from app sample-sized calls in270 stereo frames of6,394,500 near
  channel silencing, an original chip block behavior. Exact reference PCM
  still differs: RMS0.100662/0.100150, spectral.9826/envelope.9165.
  Current evidence: `dbopl-app.wav`, `dbopl-app-comparison.json`,
  `host-timing-dbopl` under audit root. No new game-machine semantics changed.

- User requested all doubtful parity tests, then resumption of the existing
  autonomous roadmap goal. Goal is active, no budget. New audit evidence is
  documented in `docs/parity-audit.md`; private root is
  `~/f117-recomp-local/parity-audit-20261004/` and `parity-audit-*.log` outputs.
  Sixteen original routes and 5,713,152 lockstep states pass again. Four
  random seeds 71-74 add 19.5 simulated minutes, all 548 hashes/finals equal.
  Input stress: 543 applied events including 143 stress events, 144 hashes,
  final `e88736099423c983`; keys/repeats/mouse and emulated joystick corners.
  GOG DOSBox's roster edit produces exactly the same 802 bytes as both engines.
  Seven CTests and 42 Python tests pass. Real WinMM joystick enumeration is
  empty; MT-32 ROMs are not supplied.
- Host stall pacing fixed (discard excess clock debt, instead of adding it);
  ROM-free deterministic C test and real SDL process-suspension test pass.
  An 800 ms suspension adds ~718 ms. OPL gain corrected to DOSBox's
  `adlib.cpp` `SetScale(2.0)`; RMS 0.100761 vs reference 0.100150 after the fix,
  previously half reference level. Waveform/synthesizer differences remain.
  `audio_render` uses app audio.c; Python comparator is strictly diagnostic
  with failure on unequal PCM. Video unchanged: 1319 matches, 7/28 one-frame
  unmatched images, ~571 ms transition delay (`video/intro-fpfq6g83`).
- `recon_return` and `recon_career` bring the route count to eighteen.
  Raised landing controller now anticipates the deck and keeps adequate
  speed. `recon-return-verified-{interp,recomp}` observers produce identical
  input/CSV bytes and pass: primary event 8A at 345.5 s, secondary event 4A
  at 559.5 s, exactly two frames, both targets intact; touchdown/stop home36
  (19196,9454), height128, gear down, brakes on, idle, fuel5009, countdown2,
  S9, result0/status3. Final END clock13639324268/hashc403d0542430b898.
  Normal record has 4357 lines; rolling replay handles the 4096-slot queue.
  Return hashes match272 checkpoints; career hashes match316 and final
  f8e2e8955bb74467 at15800000000. Its 802-byte saves are identical; score275,
  total2669->2944, sorties9->10, tour ribbon0->1. Award image reviewed.
  Private career outputs: `recon-career-interp`, `recon-debrief-recomp-02`.
  `recon-career-comparison.json` records the equality. Failed landing attempts
  04/05/06 remain private and are not counted as passes.
- Independent observer `tools/dosbox_flight.py` uses only query/read process
  permissions and normal posted input. Static mission fields match16/16;
  trajectory/RNG differences are not exact-clock aligned (`flight-compare-skip`).
  Attempt02 took the primary photo but lost speed at 44% throttle and crashed
  at 710.8 s; actual result1/status1, no successful return. Pilot now restores
  power below240 knots; observer waits through DSWAP for END. Adaptive
  recompiled rerun `recon-power-floor` passes with the existing final hash.
  A fresh full adaptive reference photo/return attempt is running via
  `py -u tools/dosbox_flight.py --data D:/GOG/F-117A --out
  C:/Users/Tideg/f117-recomp-local/parity-audit-20261004/dosbox-recon-03
  --route tools/routes/recon.front --pilot recon --seconds 1900 --skip-intro`.
  Output `parity-audit-dosbox-recon-03.log`; exec session71994. Check process
  status and the log before doing anything; tool is still being verified.
  Windows messages are wall-time scheduled, not original instruction counts.
  Next: finish independent reference run, investigate synthesized audio and
  remaining video drift, strike/drop objectives and promotion/retirement.

- Reconnaissance primary credit now succeeds: `tools/recon_pilot.py`,
  `tools/routes/recon.{args,front,input}`. Normal UI chooses Libya, Cold War,
  Strike Missions; both generated objectives are photos, targets 1 and 2,
  departure/return base 36 at (19200,9472), elevated surface 128. The route
  completes only primary and stops airborne, not a completed return.
  Cold War is transfer **item 10 at (206,149)**; (279,127) is a miss.
  Normal flight uses slash for display 13, F2 to mode 2, camera station 0,
  bay 8, N designation and Enter at the cue. `]`/`[` only step the selected
  display item's bits and cannot open the forward display; the first trial
  `recon-01` circled without credit because it used `]`.
  Successful trial `recon-02`: primary credited at 345.516 s, range 268,
  photo count 1, shutter 3, primary flag 4000h, one event 8A for target 1,
  intact target and C09A=0. Camera 16 retains one store. Credit image
  reviewed (FRAME 1 / PRIMARY PHOTO). Final after another second:
  6,180,124,257 clocks, hash `eb19d05b7369a32b`, still VGAME, photo count 1,
  shutter 0, fuel 7587. `recon-replay-{interp,recomp}.stdout` match all 123
  checkpoints/final; `recon-verified-{interp,recomp}/result.json` pass the
  stronger read-only acceptance gate. Underlying runtime unchanged, no
  interpreted game-module starts/new fallback coverage; 89,276 translated.
  All five CTests and 35 Python regressions pass. Executed coverage is
  129,317 / 230,052 bytes (56.2%), `exercised-recon.log` with the new
  `exec-theatres-20261004/recon.cov`.
  **Next:** secondary photo and return (base 36's elevated/horizontal
  runway needs a different approach from flat home 33), then strike/drop
  objectives. The existing record can be replayed before supplying more
  normal controls; don't synthesize a mission or poke state to resume.
  The prior landing commit `dc16de4` passed Windows CI.
- A landing now succeeds through normal keyboard/mouse input, no state
  writes: `tools/landing_pilot.py`, backed by `f117machine_api` and its
  Python wrapper. `tools/routes/landing.args` replays the committed input
  stream; replay filenames resolve relative to the route. Interpreter and
  recomp match all 195 checkpoints/final hash `9a20d983de7b7f4f` at
  9,787,594,940 clocks. Independent adaptive runs' inputs/CSVs also match.
  Private evidence: `landing-04{,-interp,-observed}/`; screenshots in the
  observed folder show contact and the stop. Final position (9793,1539),
  home 33 at (9792,1600), box half-width 9/length 72; speed/throttle 0,
  gear down, brakes on, fuel 4651, C09A=0, counter 2 with S=11. Parent
  flight block result 0/status 3 confirms the return. DOS exit **129**
  means handoff to END and is separate from this result; failed approaches
  also exit 129. Use `landing_pilot.py --replay tools/routes/landing.input`
  under either engine for the stronger acceptance checks, not exit alone.
  Prior approaches 01/02 landed off-base; 03 overflew while too high.
  The final pilot intercepts via (home_x,home_y+4000), uses 4x lateral
  heading correction, and descends with feed-forward -450. This covers
  shipped Libya training/easy landing, not other runways or objectives.
  API supports read-only observations, normal input, clocks/hashes, record
  and screenshots; one live handle per process. No relinking while a
  Python process holds its DLL. ROM-free API/landing gate tests are in CI.
  Final replay of the stronger checker under the interpreter passes:
  `landing-verified-interp/result.json`, same clock/hash/result. All five
  CTests and 31 Python regressions pass; the full lockstep compares
  5,713,152 states, 512 declined, zero mismatches. Executed coverage now
  125,476 / 230,052 bytes (54.5%), `exercised-landing.log` and existing
  `exec-theatres-20261004/`. Landing introduced no new untranslated starts;
  regeneration still has 89,276. No game/runtime semantics changed.
- `frontend_dialogs.args` covers KIA/retired notices and the direct MAINT
  door before and after briefing; the second page changes station 1 to
  AIM-9 and station 3 to AMRAAM. Both engines match at 64 checkpoints/final
  hash `d9dc8123c860902e`, no interpreted game-module starts. Captures and
  logs: private `frontend-dialogs-final-{recomp,interp}/` and matching
  `.txt` files. KIA OK is (166,124), retired OK is (143,109); Enter outside
  the button leaves the modal up. MAINT's rectangle DS:6F42 item 3 is
  (203,38)..(225,105), clicked at (214,70). `# expect-open` now checks
  successful named-file/program opens and minimum counts; the earlier
  stuck-modal run correctly fails. Twelve route regressions pass.
  Executed coverage: `exercised-dialogs.log`, 123,825 / 230,052 bytes (53.8%).
  CI for this commit `be539e2` and preceding `140d087` is green.
  Earned retirement and award transitions remain open, rather than merely
  displaying a shipped retired pilot's notice.
- `roster_edit.args` creates CHECK, edits a character, cancels a second
  edit, erases the KIA row and saves. Both engines match at 38 checkpoints
  and final hash `57ca3dadb75a4273`; saved rosters are byte-identical.
  Save-byte milestones check names and cleared career counters. Reload
  screenshot confirms CHECK; the 802 bytes remain identical after reload.
  `--move` now sends mouse movement without a click, needed for hovering
  over an editable row. Movement/key recording replays under the interpreter
  at all 38 checkpoints. Nine route regressions and five local CTests pass.
  Private evidence: `roster-edit-{recomp,interp}-1/`, `roster-reload-1/`,
  `roster-pipeline.log` (two clean baselines and replay),
  `build-mouse-move.log`, `boot-after-move.txt` (old boot hash unchanged),
  `exercised-roster.log` (123,025 / 230,052 bytes, 53.5%).
  Pipeline saves are now fresh `save-*` directories inside each run folder;
  previous saves are preserved. `exercised.py --run` likewise creates fresh
  run directories. `run_route.py --out` still reuses its chosen save for
  intentional load/reload tests. Career retirement and awards remain open.
- All nine theatres now have flight coverage: six added routes for CU, NC,
  CE, ME, PG and KU, alongside KO, VN and default LB. All three tension
  levels and four mission categories are selected; individual objective
  completion remains open. See `tools/routes/README.md`. Six new routes
  have airborne screenshots in private `theatres-v2/` (ME in `theatres-v3/`).
  CU/NC start with brakes released, unlike KO's carrier; toggling them
  caused early crashes, corrected before accepting the routes. ME waits
  longer for orders to decode. Milestones now require the correct world
  files, clean VGAME exit and at least a billion flight clocks; both route
  tools enforce them. Five ROM-free regressions pass and the Windows CI
  run for commit `8398f12` is green.
  Full fresh pipeline: `theatre-pipeline.log`, twelve routes at 855
  checkpoints plus final states, 89,276 starts, 5,713,152 lockstep states,
  512 declined and zero mismatches.
  Current build uses `theatre-pipeline/gen`; old `gen/` remains untouched.
  Executed coverage: `exercised-theatres.log`, private
  `exec-theatres-20261004/`, 122,059 of 230,052 bytes (53.1%).
  Final-build fallback checks are in `theatre-final-coverage.log`: all six
  new flights record zero interpreted game-module starts.
- Four-part scanout implemented and verified. Mode 13h stores four groups
  of 50 rows at lines 100/200/300/400 of the 449-line frame; display address
  latches at retrace. Presentation uses completed frames. DAC readback and
  masked render palettes are separate; blue publishes a triplet, changed
  masks update aliases. Scanout and palette state are part of parity hashes.
  Tests: all four ROM-free CTests pass (`vga_scanout` is new); all 1,210
  fidelity answers agree; all six routes agree at 329 checkpoints/finals,
  5.7-million-state lockstep zero mismatching starts. Logs:
  `build-scanout-refined.log`, `fidelity-scanout-refined.log`,
  `scanout-refined-parity.log`. Early music matches 365 writes/15.5 s,
  drift 0..36 ms (`music-scanout-refined.log`).
  Video `intro-p6mrjdbw`: 1,319 matches, seven unmatched reference pictures
  and 28 shots, all one sample; no unmatched roster shots after 110 s.
  Transition still ~0.57 s (`video-scanout-refined.log`). The earlier
  `intro-wv_f0b3k` and `intro-hc7g_vg8` runs have the same counts.
  Screenshot deadlines retain their original periodic schedule.
- `.gitattributes` committed/pushed: LF text, CRLF batch files; existing
  index text was already LF. User's `test.bat` remains untouched.
- GitHub workflow `.github/workflows/windows.yml` builds the full
  interpreter app, headless runner and CPU library on windows-2022, then
  runs four CTests and both comparator suites (nine tests), without game
  files or generated translations. Initial run succeeded:
  https://github.com/TideGear/OpenNighthawk/actions/runs/37192954631
  (`0cbe050`, job 111408818814). Actions are pinned to verified v7 SHAs.
  New checkouts now get separate local build junction targets keyed by
  source path; existing junctions are retained. Fresh public clone
  `~/f117-recomp-local/ci-clone-20261004` builds the interpreter app and
  passes all ROM-free tests. Full recompilation/coverage/parity from it
  also passed: first pass 87,574 starts, after six-route coverage 89,276;
  all six routes identical at 329 checkpoints/finals, lockstep 5,713,152
  states, 512 declined, zero mismatching starts. Log:
  `fresh-clone-pipeline.log`; private generated code/coverage/runs:
  `fresh-clone-pipeline/`. README fresh-clone instructions are verified.
  All local processes finished. The original checkout's existing build
  junction and standard gen directory were retained.
- Theatre list corrected from actual data: the default route opens
  `LB.WLD` / `LB.3DG` (Libya); Kuwait (`KU`) was missing from Left.
  Remaining theatre boxes in START's Transfer Request (DS:6F6E), useful
  for the next route batch: Central America/CU (54,73), North Cape/NC
  (186,7), Central Europe/CE (155,40), Middle East/ME (184,64),
  Kuwait/KU (196,62), Persian Gulf/PG (212,69). Existing Korea/KO is
  (291,58); Vietnam/VN is (270,83). Read from the game's own menu and
  DS:0DE2 mapping; the six routes above now use these boxes. Pilot transfer
  clearance is required for all boxes except default Libya, as in the
  working Korea/Vietnam routes. Individual mission objectives remain open.

- Near-slice-end I/O suppression now follows DOSBox's source: omit the
  delay if fewer than three delays remain before the millisecond/PIT/VGA
  boundary. `pc_slice_left` shares the old DOS transfer cost's event model.
  Three fresh calibration trials (`fadecal/calibration-8hvqvowk`) give
  PLAYER `[5841,5841,5837,5839,5836]`, improved from five 5811s; DOSBox is
  `[5841,5839,5841,5839,5844]`. START/END unchanged. All 1,210 fidelity
  answers agree (`fidelity-io-slices.log`); all three ROM-free CTests pass,
  including exact suppression-boundary tests. Music `music-46h177jn`
  matches 365 writes/15.5 s, drift 0..26 ms (`music-io-slices.log`).
  Saved video `intro-nq0jeekk`: 1,219 matches, 107/74 unmatched pictures,
  all one sample, none on roster after 110 s; transition still ~0.59 s.
  Logs: `fadecal-io-slices.log`, `video-io-slices.log`, build logs with
  `io-slice` in their names. Do not claim exact scheduler timing parity.
  `io-slice-parity.log` verifies all six routes at 329 checkpoints and
  final states, plus the 64-state/5.7-million-state instruction lockstep
  (zero mismatching starts). All checks have finished; nothing running.

- **Roster sampling diagnosis supersedes the colour hypothesis below.**
  The arrow is temporarily erased, revealing background pixels; 70 Hz
  shots beat against 70.086 Hz retrace with an 11.6 s period. Default shots
  now use the runtime's 128,413-clock VGA period. Saved-reference replay
  `video/intro-ek_ltnum`: 1,219 matches, 107 unmatched reference pictures,
  74 unmatched shots, all one sample; no unmatched roster shots after
  110 s. Transition drift still ~0.57 s. Log: `video-native-period.log`.
  `--against` uses current sampling, not the saved old 70 Hz period;
  `--reuse` still reads the original period. No runtime changes for this.

- Music comparator failure/replay checks complete: mismatching and reordered
  synthetic streams exit 1; exactly-one-window matching streams pass.
  Four ROM-free tests: `py tests/test_music_compare.py`. Two fresh short
  captures agree on 365 writes/15.5 s (drift 0..35 ms) and 300/10.4 s
  (drift 0..13 ms). The latter is `dbxcompare/music-y7j935vh`, available
  for `--reuse`; logs are `music-guard-short.log` and `music-guard-unique.log`.
  These validate the comparator and early music, not the full intro.
  Fresh capture-directory check: `dbxcompare/music-b6tnl_qu` also matches
  300 writes/10.4 s, drift 0..9 ms (`music-guard-unique-verified.log`). Both
  the capture and runtime log are inside that run, and `--reuse` passes.

- Original fade calibration diagnostics are now repeatable without the
  Reimp tree: `py tools/fade_calibration.py --data D:/GOG/F-117A --trials 3`.
  Private run `fadecal/calibration-55fc80mv`: all three trials repeat
  START/END `[8,8,7,8,8]` on DOSBox, `[8,8,8,7,8]` here; PLAYER
  `[5841,5839,5841,5839,5844]` on DOSBox, five 5811s here. Seven/eight-step
  range agrees; phase and PLAYER throughput do not. Original PLAYER
  DE4..E0B loops on IN 3DAh / OUT 3C9h until retrace with interrupts off.
  DOSBox's near-slice-end I/O delay suppression is a candidate to measure,
  not yet proven causal. Do not adjust timings just to fit this one probe.

- DAC correction verified: all 1,210 fidelity answers agree, the new
  `vga_dac_ports` ROM-free test passes, all six routes agree at 329 checkpoints
  plus finals, and the 5.7-million-state lockstep has zero mismatching starts.
  BIOS palette calls now use the port device with its state and I/O cost;
  palette blocks wrap index 255 and read linear physical buffers. `3C7h`
  also sets the write address to read address + 1, as DOSBox does.
  Logs: `~/f117-recomp-local/dac-parity.log`, `build-dac-tests.log`.
  Saved-video replay `video/intro-0wp6pl8e` is unchanged (1,217 matches,
  109 unmatched reference pictures, 89 shots). The 130-second music rerun
  (`music-dac.log`) differs at the previously known random channel-3 note
  at 29.7 s; only 596 prefix writes match, timing drift up to 55 ms. Do not
  call that a full music pass. Nothing from this check remains running.

- Nothing running. The video comparison tool is committed, but picture
  parity remains in progress. Two runs live outside the repo at
  `C:/Users/Tideg/f117-recomp-local/video/intro-qlh_wi26` and
  `.../intro-caauys37`. Both captured 9,165 frames (130.767 s) and matched
  1,217 exact RGB pictures in order. First run: 115 unmatched reference
  pictures; second: 109. Both: 89 unmatched shots inside capture time.
  Each report is `comparison.json`; the second has paired diagnostic PNGs
  in `differences/`. Most unmatched pictures last one sample. DOSBox's
  GOG `svga_s3` scanout reads four parts over a frame, unlike instantaneous
  shots: partial borders and sprite draws are visible in diagnostics.
  **Still unresolved:** the roster-entry delay and our mouse cursor's
  colour changes around 117.5 and 129.1 s, absent in both DOSBox captures.
  Paired longer differences are up to 78 pixels inside its 10x15 area at
  (160,77); the pilot names match. START keeps the INT 33h cursor hidden
  and draws its own pointer sprite. Fixing the driver cursor did not change
  these pictures: third capture `.../intro-_hurx9dz` has the same 1,217
  matches, 113 unmatched reference pictures and 89 shots. Investigate
  START's palette handling and timer/transition timing next. Avoid
  misreading the colour difference as pilot text.
  The tool reports these and exits 1; don't call this frame parity.
- The five ROM-free comparator tests pass (`py tests/test_video_compare.py`).
  The shared capture driver's OPL regression also passed: 1,120 writes
  across 30.6 s, identical, timing within 1 ms (`--seconds 30`). No runtime
  or generated code was changed for the initial comparator work; all three
  video runs ended at the same hash.
- The INT 33h cursor now draws into guest VRAM with saved background
  restoration, in `src/machine/mouse.c`; presentation no longer overlays
  it. Fidelity: eight new cursor readback answers agree, 1,193 total,
  zero differences. `test_mouse` checks clipping, guest overwrite, nested
  hide/show, mode changes and text cursor restore. Six routes under both
  engines agree at 329 checkpoints and final states, and the 64-state
  lockstep reports 0 mismatching starts. Detailed outputs:
  `C:/Users/Tideg/f117-recomp-local/cursor-parity/`.
- The last mutation batch (seed 12) finished: 60 of 60 detected,
  recorded in docs/roadmap.md and docs/architecture.md (144 of 144 planted
  defects caught across all runs). Background jobs started in a
  conversation stop when it closes, so finish or record them before a
  handoff.

## Earlier Next list (historical; superseded by the continuation above)

1. Finish the picture comparison: match DOSBox's four-part scanout
   sampling, investigate the roster cursor changes and transition delay.
   `tools/video_compare.py` captures, aligns and reports already; start
   from the saved reports/diagnostics above.
2. Munt (libmt32emu) for the Roland output; the user supplies MT-32 ROMs.
3. Individual mission objectives and earned career transitions. The normal
   landing pilot is now available as a starting point for return legs.
   Pilot editing, roster notices and direct maintenance are covered.
   Creating a pilot removes its transfer clearance; use a separate route.
4. Housekeeping: split `src/machine/dos.c`. Fresh-clone build steps,
   `.gitattributes` and the GitHub Windows build are done.

## Traps that cost time in this session

- **Bash heredocs mangle backslashes** (`\n`, `\\`, `\x00` inside Python or
  C written through a heredoc). Write patch scripts with the Write tool and
  run them; for C edits use the Edit tool. `sed` loses backslashes too.
- Python `open(p, "w")` on Windows writes CRLF; several files are CRLF in
  the working tree (git's autocrlf normalises commits). Patch scripts must
  match the file's own line endings.
- A Git Bash path (`/c/Users/...`) passed inside an argument to a Windows
  program (e.g. `--shots N:/c/...`) is not translated: use `C:/...`.
- DOSBox (SDL 1.2) only sees posted keys with `SDL_VIDEODRIVER=windib`; it
  needs a moment after its window appears before the mapper takes them; a
  redirected stdin makes INT 21h/0Bh report a waiting key, which skips the
  intro; killing DOSBox loses an open capture - stop it and close the
  window.
- DOSBox's raw OPL capture leaves out registers 02h-04h (its timer
  emulation swallows them) and only records changes.
- The game's intro has a random note (channel 3 at 29.7 s) that differs
  between two DOSBox runs: not a machine difference.
- The interpreter's lockstep reference is the machine's `interp_step`
  (cpu_step plus the TF trap), not bare `cpu_step`.
- `f117run` scripted inputs: Space is `\s` in route files (a line loses a
  trailing space); at most 4096 inputs.
- Game keys are on the Key Control Card (Steam's Bonus Content PDF): 0
  brakes, 6 gear, 8 bay, Space select weapon, Enter fire, Backspace cannon,
  B select target, Shift+F10 eject. The carrier start has its brakes on;
  the runway ends about 25 s after full throttle - rotate at about 19 s and
  do not over-climb (it stalls).
