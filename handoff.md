# Handoff

For the next conversation working on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left - keep it
updated), [docs/architecture.md](docs/architecture.md) (how parity is
built and checked) and [docs/bugs.md](docs/bugs.md) (the original game's
bugs). State as of 4 October 2026: the parity audit and expanded recon/career
routes are verified; an independent DOSBox recon flight is running (below).

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

- Latest controller continuation: optional --acquisition level added to
  recon_pilot and dosbox_flight (defaultnose remains unchanged). Before
  designation it holds positive pitch and requestsN only at550..750range,
  actualpitch>=0, headingerror<1200, usingoriginalconstant640ray. After
  acquisition normalcameraoffsetapplies. Regression coversrange/heading/nose
  gates;59Python pass. Recomp adaptive recon-level-07 completesbothphotos
  and raisedhome36return; replay observers recon-level-07-verified-{interp,
  recomp} input/CSV/result bytes identical, errors empty; finalDSWAP
  13672636250/91b4bdf6f00ff61d. Controller change ready tocommit/push.
  Independent GOG dosbox-recon-07 exec9548 nowearnedbothphotos, primary1
  (28736,13840), secondary2, bothintact; at438secfuel7475/alt2460/speed301,
  flags6205, returninghome36. Do not claimfullreturn beforeitsstrictresult.
  Liveoutput dosbox-recon-07, parity-audit-dosbox-recon-07.log,1900seclimit.
- Cargo found in pg-candidates-04 atcase45000000/startup700000019000000:
  primarytype3,target24=(10496,3840),departure58,home51, station0cargo18
  /model38/store1; secondaryphoto. Search03 finished8caseswithnocargo.
  New private cargo_trial.py normalsamefrontend/startup andshortstrip
  approach, releasescargo at<400range withnose<-400/alt>128/bayopen;
  tracksactualownedslot8..11 class38 andTTLtoimpact, readsimpactcoordinates
  anddeliverydeadline. Liveexec92416, cargo-trial-01, logparity-audit-cargo-trial-01.log.
  Verifygroundimpactwithinoriginaloctagonalrange256 andbeforedeadline,
  one release/storeconsumption, andno originalprimary8B/4000credit (D5)
  before claimingbug-compatiblearrival. No statewrites, no save fabrication.
- Retirement preparation: oneadditionalnormalSerge sortie replayedfrom
  earned03savedroster (no edits), privatecareer-four-recomp, exec93371done.
  Savedscore217,total500->717,sorties3->4,rank1/status0. No retirement claim.
  Possible next task: a guarded earned-career replay loop starting with the
  public seed route and carrying only eachprior actualsave unchanged; verify
  normalphoto/landingresultseachleg andpairedcheckpoint/saveequality.99
  sorties retire; do not synthesize near-retirement roster to claimearned.

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

## Next (from docs/roadmap.md, in the suggested order)

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
