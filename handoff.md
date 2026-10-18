# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs). State as of 17 October 2026.

17 October: `p2-batch-o` remains the latest matching batch. The owner approved progressive static-code
scope review, the longer fullscreen air-to-air test, and all fifteen fixes on for normal play.
**Start here:** the owner requested **wrap up, hand off, commit, push and stop**.
All running validation processes and their queued presentation launcher were stopped for that request.
Do not restart them in this conversation. The opt-in `--sim-hz 15` implementation is committed as
experimental; the normal default remains **8 Hz /20 MIPS**. It uses 15 normal /30 compressed, S15,
per-machine phase handling and recorded cadence; legacy logs retain eight.

**Passed:** build; 239 CTests (also passed inside the interrupted full gate); all-fixes clock/dialog,
six compression round trips and 120 rapid transitions in both engines at 8 Hz/20 MIPS and 15 Hz/40 MIPS;
record/replay/API guards. Eight's seven original clock-case hashes are unchanged. Evidence:
`D:/f117-gate/cadence15-production-20261017/{clock-verdict.json,replay-verdict.json}`;
initial unit log: `D:/f117-gate/clock486-20261017/candidate-unit.log`.

**Not accepted:** full gate was stopped during coverage/fresh interpreter routes. Long compression,
hidden app and candidate presentation checks were stopped before verdicts. Complete 45-route N=1
rerun and boot N=4/9 were queued but never started. No new visible/fullscreen test occurred.
Partial logs and frozen source archive remain under `D:/f117-gate/cadence15-production-20261017/`;
`stopped-for-handoff.json` distinguishes stopped work from failures. Use a **fresh evidence directory**
when restarting wrappers; do not reuse partial results or an old cancelled gate marker.

**Combat:** four Middle East runs (normal/compression-requested, both engines) completed, engines
agree exactly. About 692 observed seconds, 34 enemy launches. Normal's raw windows pass;
compression-requested runs have one failed raw 10-second window (142 rather than 150 frames)
including impact/zero altitude/full damage. All 66 fully airborne windows pass. Compression did
not stay active in combat: the original enemy-awareness/weapon code restores normal time.
`flight-analysis.json` retains every raw failure and the explicitly qualified airborne analysis.
Korea runs were interrupted; Kuwait/North Cape did not start. Do not call this full combat acceptance.

**Original hardware references completed:** 486DX25/33/50 and DX2-50/66 with basic VGA, plus
DX2-50/66 with ET4000AX. Same scene and original executable, all eleven scene fields agree.
ET4000 gives 16.47/16.57 actual updates/s, **S around 13**, clock ratios 1.272/1.278.
Period hardware can supply about fifteen actual updates in this light scene; this does **not**
establish S15 intent, a hardware ceiling, combat capacity or 40-MIPS equivalence.
Evidence: `D:/f117-gate/clock486-20261017/`, `clock486dx2-20261017/`, `clock486-et4000-20261017/`.
ET4000 BIOS was added from the official 86Box ROM repository to the private ROM collection only.

**Next:** finish full gate, long compression, candidate replay/app and full presentation checks,
then the authorized display assessment before considering any default change. Audit Python recording
consumers too: cargo/strike tools currently require 9-MIPS headers and reject normal 40-MIPS candidate
logs, but their direct Machine replay paths do not yet restore the new cadence metadata. Main/headless
replay does restore it. Do not silently replay an explicitly fifteen-step 9-MIPS test through those tools.
The copied-draw override-dispatch bottleneck remains a separate lead; batch p is prepared.
The authorized longer fullscreen test is complete and failed pacing limits; further visible tests need agreement.

**Owner's clarified target:** the best stable responsiveness the original could
provide, at consistent intended world speed across systems. Legitimate bug
repairs are allowed to benefit the player. Do not interpret the earlier
discussion as a preference to restore hardware-dependent adaptive clock drift.
The further fifteen-step experiment passes the tested cases with constant-S
compression and corrected phase handling at 40 MIPS; production defaults
have not been changed. Evidence is under
`D:/f117-gate/cadence15-followup-20261017/` (findings below).

**Process notes (16 October):** screen leads with `p2-batch-g-accept/screen_leads.py` before any
agent starts (five of the first twelve picks were blocked or already inline); agents run `pin.py` with
an absolute path to the program; `--shard K/N` is 0-based; a stray `dis.py` in the Temp folder shadows
the standard library (moved to scratch); route proofs can finish in four parallel lanes
(`p2-batch-g-accept/lanes.sh`). Census leads: `D:/f117-gate/p2-batch-g-accept/reached-fixed.tsv`;
free leads after screening: `screened-g.tsv` (298 free, most under 150 bytes); filter with
`kit/DECLINED.tsv` first. The owner's untracked `4k*.bat` files are still untouched.

This file holds only the current state and standing practice, not a
session log. Earlier detail is in `git log -p handoff.md`.

## Current state (17 October)

Batches n and o are landed and CI-green. The earlier census correction is committed and pushed
(`7221e50`, empty-capture guard `f51040f`); CI: https://github.com/TideGear/OpenNighthawk/actions/runs/39078907127.
The reviewed static-code tranche (`b0a10f0`) and normal-play fix defaults (`d0fa4a4`) are committed and pushed.
The fresh full gate
passed in 1,557.30 s from `D:/f117-wt/gate-h` on `fix-defaults-20261017`: 238 CTests, 45 identical routes,
44 fresh interpreter sessions, 5,718,848 instruction states under each timing profile, 1,006 matched
routines at three seeds, no mismatches, reachability and event limits clean. Private evidence:
`D:/f117-gate/default-fixes-20261017/{gate-verdict.json,gate.log,gate-source-manifest.json}`.
The exact gated files are archived in `gated-source.zip`. The gate worktree now has private profiling
instrumentation on `airair-profile-20261017` (main.c and rerun.c); archived sources/exe/PDB are under
`D:/f117-gate/present-v2/airair-profile-20261017/archive/`. Restore those two files before using it for a gate.
The private cadence patch to fixes.c has been restored and the API rebuilt; experimental DLLs stay in evidence only.
All private runs are finished. Both Windows and Munt CI passed for `d0fa4a4`:
https://github.com/TideGear/OpenNighthawk/actions/runs/39083815431.
Local census/credit regressions: 45 pass; default-selection process checks: six pass.

Scoreboard: P1 100.00%, P2 81.48%, P3 97.22%, All 93.64% (P4 and the decided-against P3 items are no longer scored)
(`py tools/progress.py --title`). Phase 2: 1,006 addresses matched; named code runs
147,448 of 169,933 creditable historical census bytes, plus 846 of 14,268 proven bytes outside its rows:
**148,294 / 184,201 in the expanded seven-program scope** (credit by byte since 16 October).
**The denominator was split on 17 October (owner):** 9,280 of the 179,213 census bytes are data or code that stays original and no longer count
(`tools/census_classify.py`, `docs/census-data.tsv`, `docs/census-original.tsv`; rule in docs/progress.md).
The jump from 83.02% to 87.28% at the change was the metric, not new matching. The 842 bytes of unused
entry-vector slots (SETUP `2D65`, PLAYER `3BFA`...) stay in the denominator: checked 17 Oct, they are live jump
vectors filled by each program's segment binder (VGAME `0FAE:0002` is `vgame_bind_segment_entries`; the same
binder is in SETUP, PLAYER, DSWAP, START and END), and 47 of VGAME's 84 run. An earlier memory-dump comparison
that suggested otherwise used a misaligned match; locate a program in a dump by many aligned chunks.
After any batch, rerun `progress.py --census ...` as before: it now also stores the class counts.

### The census scope was expanded (17 Oct; owner left this decision to me)

Keep the historical census (1,535 rows, 179,213 bytes) for comparison; add instructions actually executed
outside its rows in those seven programs. `tools/census_classify.py` computes a separate supplement and
`progress.py --census ...` refreshes it automatically. No game source or matched body changed.

- **Corrected the initial audit:** `recomp.c` writes four-byte windows, not instruction lengths. The earlier
  `gap.py`/`gap.json` route-byte figures must not be used. The classifier now decodes the image, checks the
  file hash and captured instruction bytes, masks relocation words and accepts the known bound jump vectors.
  All 57,646 accepted unique records agree in length with Capstone; 124 changed and 3 undecodable records are
  rejected and reported. ROM-free regressions plant short/long instructions, stale hashes, changed code,
  relocation/vector cases, duplicate records and COM origins. Evidence:
  `D:/f117-gate/census-gap-20261017/{check_decoders.py,decoder-verdict.json,validated-ran.json}`.
- **Added:** 5,524 validated route-run bytes plus 403 additional fixture-proven named bytes = **5,927**, of
  which **846** are credited. By module, bytes/credited: VGAME 4,060/311, START 1,186/291, SETUP 288/51,
  END 171/24, DSWAP 135/98, MPS_LOGO 64/49, PLAYER 23/22. The old census's uncredited classes also change
  under actual instruction lengths (roadmap); one formerly excluded byte returns, denominator 169,933.
  P2 87.43% -> 85.11%, overall 95.46% -> 94.75% is a scope correction, not lost matching.
- **Static-code review, first tranche:** `docs/census-code.tsv` records 16 ranges across all seven programs,
  adding 8,341 bytes with no matching credit. Hash, complete instruction boundaries, count and overlapping
  reviews are checked at every refresh; all lengths independently agree with Capstone. Private evidence:
  `D:/f117-gate/census-gap-20261017/static-audit/{approve.py,reviewed-verdict.json,inventory.json}`.
  Scope is now 148,294 / 184,201, P2 81.48%, overall 93.64%; the reduction records more work, not lost matching.
- **Still outside the score:** 7,089 static-discovery bytes remain unreviewed, including speculative table/padding
  decodes and decoder disagreements; do not blindly add them. Also **18,469 validated executed bytes in other
  modules**, individually reported by the classifier. Matching the launcher, resident graphics and sound drivers needs module/segment support.
  The score explicitly describes the expanded seven-program scope, not all game code.
- **Batch p lead screened:** VGAME `09EFA` (`prepass_display_body`) has a prepared private dossier at
  `D:/f117-gate/p2-vgame-9efa/`: **1,628 instructions / 4,552 bytes**, RET at `B0C1`, length must enclose
  4,552 bytes. Validated routes run 3,487 bytes; 154 call sites have 46 targets, no existing matched entry
  lies inside the body (`candidate-review.json`). Its zero-size census row no longer hides its executed code.
  No observer/fix at the entry.
  Body, fixtures, seeds, mutations and route proof are **not done**; config still has placeholder names.

### Normal-play defaults and original timing

- All fifteen fixes, including D1REAL and D1PROX, now start on in normal `f117a` play (owner).
  `--fix none` clears them; follow with selected IDs for a subset. Core/API/headless start off, and parity
  commands explicitly prepend `--fix none`; fix-specific checks enable their own cases. Replays restore
  recorded sets; legacy logs without a fix header retain none. Private six-case recording/header probe passed.
- Main's normal /O2 app build passed. Hidden owner-session app replays passed in both `interp` and `rerun`:
  2,238 exact logic frames each, all presented/scanned palette counts equal; rerun drew 7,676 intermediate
  pictures. Evidence: `D:/f117-gate/default-fixes-20261017/app-session/verdict.json`.
- All fifteen fixes together pass the seven normal/compressed/pause/dialog clock cases at 20 MIPS in
  both engines, every case hash identical (69.17 s). Evidence: `all-fixes-clock/verdict.json` under
  the same private directory. The Machine API permits one open machine per process, so use separate processes.
- D1REAL's eight steps/second are a project choice, not a recovered developer target or a requirement for 60 Hz.
  D1PROX's terminal-speed x 8/9 band uses the GOG-speed parked S=9 baseline as a compatibility calibration,
  not an authoritative warhead radius. No numeric canonical radius was recovered from the inspected code/manual.
- **Owner requested 8 vs 15 Hz; complete, with 14 as an even-divisor control.** Private build only;
  no production timing/default change. Forty-six consistency checks pass (engine agreement/control integrity,
  not candidate acceptance). At 20 MIPS, 8 and 14 pass all seven clock/dialog cases; 15 fails 2x:
  150 mission seconds in 70 seconds, because S truncates to 7. All give 1:1 normal time. At 9 MIPS,
  higher targets reach only 11.6 updates/s in the parked scene; eight still passes. Median/max normal-input
  throttle response over 24 pulses: 8 Hz 64.5/112 ms; 14 Hz 31.5/68; 15 Hz 35/67 (guest state, not display latency).
  Missile pass offsets 20/22/28 give hit/miss/miss at all rates; tested acceleration speed 1->10 in two seconds.
  Same takeoff keys produce different speed/climb. Six exploratory flights preserve each mission/combat seed
  but vary in path/exposure/outcomes; no balance equivalence claim. Cannon probes failed to fire and a staged
  guidance probe did not steer, so those do not provide coverage. Initial airborne pass/roll-latency probes
  are also excluded; use separate parked physics and throttle checks. D1's independent 11.6 Hz cap confounds
  higher targets; comparison excludes D1, leaves other repairs on, and proves eight unchanged by that exclusion.
  **Recommendation given:** keep eight now; fourteen is the stronger higher-rate candidate, with more work before
  shipping. Evidence/scripts/archived binaries: `D:/f117-gate/cadence-20261017/verdict.json`, `without-d1/`.
  Full details in docs/speed-sweep.md. No new visible/fullscreen test was run.
- **Fifteen-step follow-up complete (owner requested):** normal 15 Hz/S15 is viable in the tested cases.
  First 15-normal/14-compressed prototype passes steady clocks at 20/40 MIPS but fails 120 rapid toggles:
  82 mission seconds vs 72.469 integrated mode-seconds. Constant S15 with 15 Hz normal /30 Hz compressed
  avoids divisor changes; at 20 MIPS it cannot supply 30 updates/s (1,636 updates/109 mission seconds in
  70 compressed seconds), then catches up after switching back. At 40 MIPS steady cases pass, but retaining
  the old-rate deadline loses 1.957 seconds over rapid toggles. **Final phase-corrected candidate** scales
  remaining deadline/debt at the existing rate-change hook: at 40 MIPS both engines agree on all seven
  clock/dialog cases, six round trips/twelve settled windows and 120 rapid transitions. Rapid clock:
  68 mission seconds vs 68.012 integrated seconds, within the counter resolution. Twelve final checks pass:
  `D:/f117-gate/cadence15-followup-20261017/constant-step/phase-corrected/verdict.json`.
  Normal-only physics/24-pulse throttle/two flight scenarios retain prior 15-Hz hashes in
  the first prototype (35 ms median, 67 ms max throttle response at 20 MIPS); both flight engines agree.
  Each scenario requested 600 seconds; Middle East ends early, Korea reaches the limit.
  Later prototypes preserve initial normal clock hashes, not a new physical-display test. Fixes-off boot/quit
  retains all 54 checkpoints and final exit/hash before/after each candidate and between engines. D1 excluded,
  other fourteen fixes on. Failed candidates retained; notes in docs/speed-sweep.md.
  Private fixes.c patch has been restored byte-for-byte from production and the normal API rebuilt.
  **Adoption not done:** needs explicit per-machine cadence state (prototype packs metadata), full gate,
  presentation checks and display assessment. Default remains 8 Hz/20 MIPS. Fast original 486 reference
  tests have not been run; fifteen is not established developer intent or an original-hardware ceiling.
- Independent original game on 86Box OPTi 495SX / 386DX33, MS-DOS 5.00: after 30 seconds settling,
  152 mission seconds in 119.994917 native emulated seconds (1.26672x), S=5, normal compression=1.
  VGAME on the private VM disk hashes identically to the GOG file. This confirms a mismatch on that profile,
  not developer intent or every historical PC. The earlier sweep's `386` is our model, not 86Box itself.
  Capture initially asserted normal compression was 0; corrected analysis uses the retained samples (normal is 1).
  Evidence: `D:/f117-gate/default-fixes-20261017/clock86/{loop.state,samples.json,verdict.json}`;
  interpretation in docs/speed-sweep.md. The owner emailed the director; no reply is needed to continue testing.
- **Controlled original 86Box follow-up:** same parked scene, chipset/VGA/DOS and mission, changing only
  386DX25/33/40 speed. After 30 seconds settling, three consecutive 60-second windows give ratios
  1.30551 / 1.27218 / 1.16662 at S=4/5/6. All eleven scene fields agree; compression stays normal.
  This rules out an exact common multiplier on that hardware family, not every period PC or developer intent.
  Evidence: `D:/f117-gate/clock-matrix-20261017/verdict.json`. Owner reconsidered intentional acceleration;
  recommendation remains D1REAL on, separating the uncertain cadence choice from 1:1 time.
- Online primary-source check: F-19's 1988 designers' notes explicitly describe real-time simulation
  independent of CPU speed, with only graphics detail/smoothness changing. This strengthens real-time intent
  for the family, but is not an exact F-117A timing target. Intentional faster simulation did exist as travel
  compression and pilot-training research (1970s onward); no source links that to this game's normal-speed drift.
  Sources and limits are recorded in docs/speed-sweep.md, "Historical intent and faster-than-real-time simulation".
- Longer fullscreen air-to-air test completed on the owner's 4K/59.99 Hz display: 17,241 timed pictures,
  p99 28.6976 ms, 638 intervals over 25 ms (3.7022%), **fails both pacing limits**. All 3,679 logic frames
  exact, drift 0.005819 s, final hash `b050542466c9a616` equal to a separate headless replay. Evidence:
  `D:/f117-gate/present-v2/stage4-airair-long-20261017`. No score increase.
- **Hidden profile locates a CPU bottleneck:** two original-fix-off replay windows retain gate endpoint hashes.
  Main work median/p99 grows 11.804/14.561 -> 18.020/21.702 ms, copied redraw preparation 6.984 ->12.348 ms;
  simulation stays about 2.5 ms. Deep timers put cost in copied execution, not copying/packing; draw records
  1,900->6,000, interpreted instructions only 1,179->1,752. Of 7,986 main-thread samples, 46.01% land in
  `recomp_override_step`. Next lead: replace its two linear placement scans with an index preserving registration
  order, fix priority, CS aliases, observer declines, 386 skips and load/enable invalidation; no implementation yet.
  Needs meaningful override tests, full gate and presentation checks, then isolated timing. Hidden CPU evidence
  does not exclude fullscreen GPU cost or accept pacing. Details: docs/presentation.md and private
  `D:/f117-gate/present-v2/airair-profile-20261017/`.

### Presentation: latest (15 October, evening)

**`--present rerun` is integrated and gated.** In-between camera/aircraft/weapon poses are drawn by the original's
own code on a throwaway machine copy, then refined at any scale; a smoothed playback clock (three published steps)
times them (`F117_PRESENT_CLOCK=raw` for the old clock; opt-in knobs `F117_PRESENT_CLOCK_ALPHA`/`_PULL`, defaults
0.2/0.05). Defaults and the owner's settings are unchanged; the owner's local `4k-rerun.bat` runs the 4K preset with
`--present rerun`. Design and figures: [docs/presentation.md](docs/presentation.md).

- **Proof at integration (`fe20913`):** full gate; the 45-route N=1 rerun pass keeps 259,240 frames / 259,166 pairs
  and every guest hash; boot N=4/9 368/366; 1,638,579 copy-isolation checks with no changed real state.
  Evidence `D:/f117-gate/present-v2/rerun-accept/final-gate-verdict.json`, `app-verdict.json`.
- **Threaded palette bug (fixed 15 Oct):** the threaded app path froze the palette at N>1 in `interp` and `rerun`
  (the driver's colour cycling of the gun rounds showed solid dark red; fades froze too). Headless runs never use
  that path, so no route check could see it. `F117_PRESENT_SHOTS` dumps what the app presents; the app summary
  counts distinct presented/scanned palettes, and `present_light_check.py` now checks app-only changes by replaying
  a recorded session through the app (`--app-session`, `--app-exit-after`). Owner's gun session:
  `%APPDATA%/F117Recomp/F-117A/sessions/session-20261015-203305` (burst near 2.83B clocks).
- **Clock tuning (one hidden run each, `D:/f117-gate/present-v2/clock-tune`):** default 6 freezes, 2.5% >20% speed
  changes; alpha 0.4 / pull 0.025: 3 and 1.3%; picture age not measured. Defaults unchanged: the owner's call.
- **Verification scope:** `drawfeed.c/.h` and `rerun.c/.h` execute or snapshot machines, so they need the full gate
  plus `presentation_check.py --mode rerun` over all 45 routes; the light check refuses them.
- **Owner verdict on a real screen (15 Oct):** played `4k-rerun.bat` (fullscreen 4K, rerun) twice: "seems great"
  apart from the original's jitter (20:50), "looks good" after the palette fix (22:30), and accepted the motion
  "at least for now" (22:32): Stage 3 raised from 0.15 to 0.9, P3 96.33%.
- **Still open (Stage 3's last 0.067):** clock skips against picture age; the small
  `DF03`/`C2C8` vertex group (`rerun-accept/object-follow-up.txt` is a lead); slower machines.
  Refresh rates above 60 Hz (per-machine observer/runtime/fix state for parallel copies; one copy
  costs about 6 ms) moved to the unscored Phase 4 on 17 Oct: the owner cannot test above 60 Hz.
- **Owner decisions (15 Oct):** stop improving primitive pairing (identity pairing is opt-in,
  `F117_PRESENT_IDENT=1`: it halved unpaired shapes but deformed the extra pairs); road and coastline jitter are the
  original's (also in GOG DOSBox and in sharp `replay` mode) and are **not** smoothed at any scale (21:23); the
  higher-resolution and higher-frame-rate modes are experimental and keep the original renderer's quirks (README);
  draw distance is not extended. Owner flights frozen for reference: `D:/f117-gate/present-v2/owner-roads-20261015`
  and `owner-sharp-20261015`. The rerun research harness stays on branch `draw-rerun-probe` for history.

### Presentation: what is integrated

- Baselines every presentation change must keep: all 45 N=1 routes 259,240
  exact frames / 259,166 pairs; live boot N=4/9 368 frames / 366 pairs each;
  the owner's complete N=9 flight 69 checkpoints/final `31c259b992340414` and
  2,101 frames / 2,099 pairs. N=1 remains the exact original.
- HUD, map position, sky/ground and world lines are state-driven where the
  capture was the wrong technique: the HUD's tapes, heading strip, pitch
  ladder, flight-path marker and bars come from game state
  (`F117_PRESENT_HUD=old` switches it off); continuous state moves on the fine
  grid, discrete readouts, switches and the map arrow's heading wait for the
  logic frame. Map-origin subtraction keeps its 16-bit wrap. Models match
  29,594 original HUD calls, 11,490 panel calls and 714 map blits. Evidence
  `D:/f117-gate/present-v2/{diag,fix3d,hud,hud2}`.
- Dots/lights, roads and matching outlines are drawn from exact camera
  geometry (full `256*x/z` projection). One perpendicular-width pen serves
  HUD, outlines and strips; no per-role widths are assigned yet.
- Near-clipped polygons are drawn from geometry; `F117_PRESENT_DIAG` is the
  per-pixel origin diagnostic.
- Threaded presentation (`drawlive_collect` / `drawlive_publish` / worker
  thread, `F117_PRESENT_THREADS_OFF=1` for single-threaded) passed fullscreen
  4K/59.99 Hz/vsync N=9 on 13 October (strike, air-to-air, landing; evidence
  `D:/f117-gate/p3-stream`). Fullscreen pacing of the later batches is
  unchecked. Hidden 3840x2160 D3D11 throughput is 119-150 fps with <5 ms p99
  regeneration/raster but occasional ~100 ms upload stalls.
- Scene-input foundation: opt-in `F117R_OBSERVE_SCENE=N` captures
  pre-visibility requests and complete inputs; `tools/scene_capture.py`
  audits completeness; `tools/scene_model.py` decodes complete topology and
  derives detail selection (agrees with the original on 192 requests, 191
  geometry submissions and 205 reachable detail variants). Shared-vertex
  coverage is synthetic only. The unconnected `src/present/scene_mesh.c`
  draft (mesh/pose/visibility/order/material evaluation, synthetic CTest) is
  on master as part of the gated tree; it is not a finished fix. Evidence and
  private original-execution harnesses: `D:/f117-gate/present-v2/scene`
  (notes `next-step.txt`, `live-integration-notes.txt`).
- The broader world-geometry plan (complete topology before visibility
  filtering, persistent identities; no more clipping exceptions) is recorded
  in
  [docs/presentation.md](docs/presentation.md#consolidate-world-geometry-before-more-clipping-exceptions-15-october-2026);
  it is no longer the main path. The clip-only prototype in
  `D:/f117-gate/present-v2/coast-finish/clip-prototype` is set aside, not
  integrated; its test can be adapted.
- The coarse-interpolation checker fails on unknown novel records and
  regeneration-count overruns.
- Open: width tuning by drawing role, weapon/target symbology, the owner's
  physical-panel motion verdict. What the owner decided against (jitter smoothing,
  outline refinement, more pairing work, a longer draw distance) is not scored since 17 Oct; the
  roadmap lists it as possible future work. The coastline transitions are an issue of the original
  game and sit among the unscored Phase 4 items.

### Phase 2

- Latest batch (17 Oct): **`p2-batch-o` is gated and landed**: three new addresses
  (1,006 addresses; 147,448 bytes of the 169,932 creditable, +268). Five seeds each,
  full instruction coverage, all 104 mutations caught, exact `recon_career` (END, 1 call) and
  `full_cycle` (VGAME 276 calls, START 89 calls) routes, boot26, fifteen optimized seed runs.
  Gate 1,894.81 s: 238 CTests, 45 identical routes / 44 fresh interpreter sessions, 1,006
  matched routines at three seeds, both timing profiles, reachability and event limits clean.
  N=1 259,240 exact frames / 259,166 pairs; boot N=4/9 368/366. All acceptance processes
  1,965.26 s. Evidence `D:/f117-gate/p2-batch-o-accept/final-gate-verdict.json`.
  Main's normal /O2 application build passed (244.56 s); proof-kit libraries were refreshed from
  `aa8991b` (hashes in `kit-libs/manifest.json`). CI of `aa8991b` failed in "Phase 2 census credit
  regressions": the split-denominator change had made `progress.refresh_census` load the game's programs,
  which that test does not have; fixed in `e35d1f9` (classification is its own step,
  `tests/test_census_classify.py` added to CI). Both Windows and Munt CI passed on `bfafc31`:
  https://github.com/TideGear/OpenNighthawk/actions/runs/39076427990.
  All batch-o acceptance/build processes are finished.
  END `26A9` report page text, VGAME `D826` view and sky, START `9E3E` formatter dispatch
  (harness: any `FATAL_JUMPS` row of a routine ends it). The integrated-source audit ignores
  trailing whitespace (one comment line of `9E3E` had some). END `400F` (DOS open, fatal tail at
  `41CA`) is proved privately in `p2-end-400f` but not integrated: its tail is a stand-in, one
  mutation (the hand-over target) is not caught, and its coverage figure counts 21 of 39 instructions.
- Previous batch (17 Oct): **`p2-batch-n` is gated and landed**: one new address
  and one existing-entry expansion (1,003 addresses; 147,180 / 179,213 bytes,
  +1,239). Five seeds each, full instruction coverage, all 199 mutations
  caught, exact `recon_career` (3 calls) and `full_cycle` (2 calls) routes,
  boot26, fifteen optimized seed runs. Gate 1,838.95 s: 235 CTests,
  45 identical routes / 44 fresh interpreter sessions, 1,003 matched routines
  at three seeds, both timing profiles, reachability and event limits clean.
  N=1 259,240 exact frames / 259,166 pairs; boot N=4/9 368/366.
  All acceptance processes 1,922.80 s.
  Evidence `D:/f117-gate/p2-batch-n-accept/final-gate-verdict.json`.
  END `3021` complete debrief scorer (579 instructions, existing `34EA`
  alias kept); VGAME `11B8:0008` packed picture opener with a terminal DOS
  exit (harness: `PROGRAM_ENDS` entry, native INT 10h predicate).
  Main's normal /O2 application build passed (212.16 s); proof-kit
  libraries were refreshed from `ef6fb93` (hashes in `kit-libs/manifest.json`).
  Both Windows and Munt CI passed: https://github.com/TideGear/OpenNighthawk/actions/runs/39070726821.
  All batch-n acceptance/build processes are finished.
- Previous batch (17 Oct): **`p2-batch-m` is gated and landed**: one new address
  and one existing-entry expansion (1,002 addresses; 145,941 / 179,213 bytes,
  +262). Five seeds each, full instruction coverage, all 74 mutations caught,
  exact 344-checkpoint `recon_career` routes with one call each, boot26 and
  ten optimized seed runs. Gate 1,811.97 s: 233 CTests, 45 identical
  routes / 44 fresh interpreter sessions, 1,002 matched routines at three
  seeds, both timing profiles, reachability and event limits clean.
  N=1 259,240 exact frames / 259,166 pairs; boot N=4/9 368/366.
  All acceptance processes 1,865.48 s.
  Evidence `D:/f117-gate/p2-batch-m-accept/final-gate-verdict.json`.
  END `2200` awards-page callback: 52 instructions / 28 mutations.
  END `22EB` complete ribbon collector: 86 instructions / 46 mutations;
  replaces its old opening. Fresh coverage replaces the inherited entry's
  evidence (+161 bytes); the new callback adds 101. Same census denominator.
  Main's normal /O2 application build passed (125.70 s); proof-kit
  libraries were refreshed from `6489da3` (hashes in `kit-libs/manifest.json`).
  Both Windows and Munt CI passed: https://github.com/TideGear/OpenNighthawk/actions/runs/39067339405.
  All batch-m acceptance/build processes are finished.
- Previous batch (17 Oct): **`p2-batch-l` is gated and landed**: one new address
  plus an existing-entry expansion (1,001 addresses; 145,679 / 179,213 bytes,
  +314). Five seeds each, full instruction coverage, all 115 mutations caught,
  exact route/boot proofs and ten optimized seed runs. Gate 1,847.11 s:
  231 CTests, 45 identical routes / 44 fresh interpreter sessions, 1,001
  matched routines at three seeds (0 mismatching), both timing profiles,
  reachability and event limits clean. N=1 259,240 exact frames / 259,166
  pairs; boot N=4/9 368/366. All acceptance processes 1,900.11 s.
  Evidence `D:/f117-gate/p2-batch-l-accept/final-gate-verdict.json`.
  VGAME `11D8:000A` far picture wrapper: 40 instructions, 41 mutations,
  `full_cycle` 2 calls / 66 checkpoints; live relocated operands and all 200 rows.
  START `58DF` complete grid formatter: 91 instructions, 74 mutations,
  `korea_strike` 2 calls / 76 checkpoints; replaces the old six-instruction
  opening at the same address. END's shared helper is preserved.
  Jump-table data `5963..5974` and VGAME `11DFF` remain uncredited.
  Main's normal /O2 application build passed (168.61 s); proof-kit
  libraries were refreshed from `354cf5c` (hashes in `kit-libs/manifest.json`).
  Both Windows and Munt CI passed: https://github.com/TideGear/OpenNighthawk/actions/runs/39061808384.
  All batch-l acceptance/build processes are finished.
- Previous batch (17 Oct): **`p2-batch-k` is gated and landed**, five routines /
  802 census bytes (1,000 addresses; 145,365 of 179,213 bytes). Five seeds
  each, full instruction coverage, all 267 mutations caught, exact real-route
  and boot proofs; 25 optimized seed runs. Gate 1,833.81 s:
  229 CTests, 45 identical routes / 44 fresh interpreter sessions, 1,000
  matched routines at three seeds (0 mismatching), both timing profiles,
  reachability and event limits clean. N=1 259,240 exact frames / 259,166
  pairs; boot N=4/9 368/366. All acceptance processes 1,888.80 s.
  Evidence `D:/f117-gate/p2-batch-k-accept/final-gate-verdict.json`.
  END `3BF4`, SETUP `111A`, END `2265`, END `1BAA`, START `3EA5`.
  SETUP's gap `1182` stays uncredited; denominator unchanged.
  Main's normal /O2 application build passed (186.70 s); proof-kit
  libraries were refreshed from `7e16a9f` (hashes in `kit-libs/manifest.json`).
  Both Windows and Munt CI passed: https://github.com/TideGear/OpenNighthawk/actions/runs/39038100875.
  All batch-k acceptance/build processes are finished.
- Previous batch (17 Oct): **`p2-batch-j` is gated and landed**, four routines /
  1,349 census bytes by byte credit (995 addresses; 144,563 of 179,213 bytes).
  Five seeds each, full instruction coverage, 203 mutations caught, exact boot
  and real-route proofs; 20 optimized seed runs. Gate 1,726.66 s:
  224 CTests, 45 identical routes / 44 fresh interpreter sessions, 995 matched
  routines at three seeds (0 mismatching), both timing profiles, routes-only
  and event limits clean. N=1 259,240 exact frames / 259,166 pairs; boot N=4/9
  368/366. All acceptance processes 1,779.95 s with checked overlap.
  Evidence `D:/f117-gate/p2-batch-j-accept/final-gate-verdict.json`.
  Routines: START `6AB3` (`roster_edit`), END `2703` (`recon_career`),
  START `0010` and END `0010` (`full_cycle`). END's final two bytes stay uncredited.
  Corrected the pre-existing START `76C0` RNG bound read order: the generator
  can update the stacked bound through DS/SS aliasing. Five private seeds,
  all nine instructions, 28 caught mutations, exact boot/route and six optimized
  generic seed runs prove it. Permanent CTest keeps the failing seed
  `0x4ebc42e1d00b`; it reproduces on batch i too. First failure preserved under
  `attempt-1-failed`; the full gate and presentation checks were rerun.
  Main's normal /O2 application build passed (180.86 s); proof-kit
  libraries were refreshed from `07b0897` (hashes in `kit-libs/manifest.json`).
  Both Windows and Munt CI passed: https://github.com/TideGear/OpenNighthawk/actions/runs/39034858973.
  All batch-j acceptance/build processes are finished.
- Previous batch (17 Oct): **`p2-batch-i` is gated and landed**, six routines /
  602 census bytes by byte credit (991 addresses; 143,214 of 179,213 bytes).
  Five seeds each, full instruction coverage, 207 mutations caught, exact boot
  and `full_cycle` route proofs; 30 optimized seed runs. Gate 1,915.56 s:
  219 CTests, 45 identical routes / 44 fresh interpreter sessions, 991 matched
  routines at three seeds (0 mismatching), both timing profiles, routes-only
  and event limits clean. N=1 259,240 exact frames / 259,166 pairs; boot N=4/9
  368/366. All acceptance processes 1,969.70 s with checked
  presentation overlap (baseline frozen before integration).
  Evidence `D:/f117-gate/p2-batch-i-accept/final-gate-verdict.json`.
  Routines: END `3F44`, START `8782`, PLAYER `019A` (last two bytes
  uncredited; the returning fixture covers six of the eight tail bytes), END
  `4648`, VGAME `0FB2:0246` (linear `FD66`), START `9094`
  (new `PROGRAM_ENDS` row and alternate-entry return rule).
  Main's normal /O2 application build passed (219.81 s); the proof-kit
  libraries were refreshed from `258c74d` (hashes in `kit-libs/manifest.json`).
  Both Windows and Munt CI passed:
  https://github.com/TideGear/OpenNighthawk/actions/runs/39013542862.
  All local acceptance/build/proof processes are finished.
- Previous batch (16 Oct, late): **`p2-batch-h` is gated and landed**, 18 routines /
  3,894 census bytes by byte credit (985 addresses; 142,612 of 179,213 bytes).
  Five seeds each, full instruction coverage, 803 mutations caught in all, exact
  boot and real-route proofs; 90 optimized seed runs. Gate 1,623.75 s: 213 CTests,
  45 identical routes / 44 fresh interpreter sessions, 985 matched routines at
  three seeds (0 mismatching), both timing profiles, routes-only and event limits
  clean. N=1 259,240 exact frames / 259,166 pairs; boot N=4/9 368/366.
  All acceptance processes finished in 2,185.69 s (presentation sequential: no
  valid keyed baseline for the new tree, so the overlap fell back; freeze a
  baseline before integrating next time to keep the overlap).
  Evidence `D:/f117-gate/p2-batch-h-accept/final-gate-verdict.json`.
  Main's normal /O2 application build passed (177.95 s), and proof-kit libraries
  were refreshed from `eab3cc0`; hashes are in `D:/f117-gate/kit-libs/manifest.json`.
  Both Windows and Munt CI passed for `eab3cc0`:
  https://github.com/TideGear/OpenNighthawk/actions/runs/39010544583.
  All local acceptance/build/proof processes are finished.
- Previous batch (16 Oct, late): **`p2-batch-g` is gated and landed**, 21 routines /
  6,181 census bytes by byte credit (967 addresses; 138,718 of 179,213 bytes).
  Five seeds each, full instruction coverage, 1,017 mutations caught in all, exact
  boot and real-route proofs; 105 optimized seed runs. Gate 1,803.14 s: 195 CTests,
  45 identical routes / 44 fresh interpreter sessions, 967 matched routines at
  three seeds (0 mismatching), both timing profiles, routes-only and event limits
  clean. N=1 259,240 exact frames / 259,166 pairs; boot N=4/9 368/366.
  All acceptance processes finished in 1,856.14 s using the checked presentation
  overlap. Evidence `D:/f117-gate/p2-batch-g-accept/final-gate-verdict.json`.
  Main's normal /O2 application build passed (173.41 s), and proof-kit libraries
  were refreshed from `8e42b41`; hashes are in `D:/f117-gate/kit-libs/manifest.json`.
  Both Windows and Munt CI passed for the merge commit:
  https://github.com/TideGear/OpenNighthawk/actions/runs/39004209030
  (`D:/f117-gate/p2-batch-g-accept/ci-run.json`). All local acceptance/build/proof
  processes are finished; no test session needs resuming.
  The first gate attempt failed step 7c at seed `0x70ca1542de1f`: PLAYER `0738`'s
  coverage floor (its busy early exit `073F` unreached; the fixture planted the
  busy word only on a 1-in-10 random draw). Every tenth state now plants one
  outright, and `matched_player_frame_tick_busy_seed` retains the seed. Note the
  harness prints a routine's `equal` line before the floor check adds its failure,
  so a lone `1 mismatching` with all lines equal means a coverage floor: reproduce
  with `--only` and `F117_COVER`.
- **Declined (recorded in `kit/DECLINED.tsv`):** PLAYER `2853`, PLAYER `0211`
  re-sync, VGAME `14C2:0000` (STI in its colour loop; the `set_dac` family).
- **Next leads:** rescreen with current registrations and actual byte credit;
  `p2-batch-j-accept/ranked.json` predates recent batches. END `3100` and
  `3078` are interior arms of the now-named `3021` scorer: do not add them.
  Current registration/byte-credit screening: `p2-next-after-l/ranked.json`.
  `p2-end-2200` and `p2-end-22eb` landed in batch m;
  `p2-end-2373` is superseded by the enclosing `22EB` expansion.
  `p2-vgame-11cd3/RESEARCH.txt` identifies a 512-byte picture read and its
  terminal DOS error path; its default draft walks past exit into data and
  another function, so it needs a bounded dossier before implementation.
  `p2-batch-n-accept/ranked-partials.json` additionally ranks incomplete
  registered routines (screen before selecting; some remaining bytes are data).
- **Stopped draft:** END `03D9` (D12 sites 0443/0450 may lie inside: decline
  while D12 is on). VGAME `7663` was superseded by batch g.
- **Routes other than full_cycle:** `kit.py DIR checkpoints --route NAME` (and `ROUTE=NAME` for
  `finish.sh`) proves a routine on another cached route; `audit_proofs.py` reads the route from the
  checkpoint verdict. Rank with `D:/f117-gate/p2-batch-d-accept/reached-allroutes.tsv` (all 45 routes,
  region-less rows included; column 6 lists the routes) and pick the shortest route that runs it.
- **Blocked, stays original:** the C start-up (START `8EDC` and six copies) must stop after its own STI
  and never returns to its caller's stack, so no lockstep state can complete it
  (`D:/f117-gate/p2-start-8edc/BLOCKED.txt` lists the harness rules that would unblock it). Fixes D4
  and D34 are hooked at two of those entries; an enabled fix runs instead of any override there.
- **Kit practice (16 Oct, evening):** agents run every kit command through
  `D:/f117-gate/kit/pin.py MASK` (CPU set + BelowNormal) with `mutations --jobs 2`; while a gate runs
  (affinity `0xFFFFFF`), agents use the CPU pairs at bits 24-29. Root finishes each proof with
  `D:/f117-gate/p2-batch-d-accept/finish.sh DIR` (checkpoints, boot proof, verdict, audit). Traps found:
  preparers must write LF (`write_text(..., newline='\n')`); kit bases (`matched-base.c`,
  `tests-base.c`) go stale when a batch lands - refresh them before rebuilding an old draft; a body edit
  after the mutation run (even a comment) fails the audit until seeds and mutations are rerun; an IRET in
  a fixture's interrupt handler stops the body (use RETF 2); the lockstep DOS handler sets DX on some
  scripted INT 21h answers; a span that encloses a matched helper reports more instructions than the
  routine owns (set `insns` to the span, rebuild, rerun seeds; credit stays the census row).
- **Coverage for new routes:** a fresh interpreter `--coverage` capture of all 45 routes is in
  `D:/f117-gate/p2-batch-d-accept/allroutes/` (`interp_cover.py`); run `tools/census_reach.py` on it to
  see which census entries any route runs before designing new routes (done 16 Oct: 208 rows /
  18,901 bytes have no evidence, data included; see the roadmap).
- Ranked leads: `D:/f117-gate/p2-batch-d-accept/reached-allroutes.tsv` (minus the routines above).
- Kit libraries are refreshed from main's normal /O2 build after each batch;
  `D:/f117-gate/kit-libs/manifest.json` records the source commit and hashes.
- Other leads: PLAYER `026B:0336` (tick/callback service; calls original code,
  so needs returning-helper fixtures). The four older `cinit` wrappers
  (VGAME, PLAYER, DSWAP, SETUP) have no
  lockstep coverage entry; their fatal arm stays original code.
- Deliberately left as original code: the STI-first timer interrupt handlers
  (VGAME `0x1C9B`, START `0x8D0B`, END `0x4BEF`: after STI the run loop admits
  one more instruction, so a matched body cannot do more than the original); START `8427` and
  END `4393` (routes missed their planted defects and lockstep cannot run
  their waits); DSWAP `0x03DF3` (2,791 initial zero bytes, no generated entry,
  the static draft decodes data; evidence
  `D:/f117-gate/dswap-3df3/candidate-review.json`).
- **VGAME 0FB2:04DD** (the line clipper's divide-error handler) is matched and
  fixture-proven but **not integrated**: no route reaches it (146,153 real
  clip divisions across 14 routes all stay well under the 32,767-unit
  threshold). Preserved on local branch `irq-divide-fault` (commit `e15c9ef`)
  and `D:/f117-gate/next-drafts/irq-divide-fault/0002-match-clip-divide-fault-04DD-lockstep-only.patch`.
  Integrate only if a route (or a clearly labelled staged input) reaches it.
- Kit harness features: scripted INT 21h answers (`g_dos_script`, with `.put`
  for the byte a one-byte read brings), skipping states that overwrite a
  planted callee stub, `PROGRAM_ENDS` for terminal DOS arms.

### Phase 3

P3 (97.22%) is held down by Stage 3, smooth in-between frames (93.3%: accepted by the owner for now;
clock tuning and slower machines remain; refresh rates above 60 Hz are unscored Phase 4), Stage 4 infrastructure (90%: fullscreen pacing of the latest
builds, slower machines, cargo/career flights) and Stage 2 (96.7%: widths by
role, the panel review; the coastline and the other decided-against items left the score on 17 Oct). Details and the method: docs/progress.md.

## Next steps

0. **Open questions for the owner** (do not assume): whether to run a
   longer fullscreen air-to-air timing test on his 4K screen (strike and landing passed, air-to-air missed p99 on a
   short sample; the run script is `D:/f117-gate/present-v2/stage4-fullscreen-20261017/stage4.py`, live input is
   ignored during a replay, but other load on the machine would disturb the timing); whether Stage 3's remaining skips and slower machines, and
   Stage 4's slower CPUs/GPUs and long career flights, are worth pursuing (see the P3 table in this conversation:
   Stage 3 93.3%, Stage 4 90%, Stage 2 96.7%).
1. **Phase 2 first (owner).** `p2-batch-o` is gated and landed. END `400F` is proved privately (`p2-end-400f`)
   but held back (stand-in fatal tail, one uncaught mutation, coverage counts 21 of 39); new screened leads are in
   `D:/f117-gate/p2-next-after-n/screened.tsv` (FREE rows; `kit/DECLINED.tsv` now also lists END `57B0` and START
   `01285` as interior arms).
   Continue down the ranked list
   (`D:/f117-gate/p2-batch-d-accept/reached-allroutes.tsv`) with the private kit and parallel agents (agents do steps 1-7 only; root runs
   `kit.py checkpoints` and `D:/f117-gate/kit/boot_proof.py` with `KIT_ROOT` at the gated worktree, then
   `verdict` and `audit_proofs.py`). The brief's "Traps found on 16 October" lists today's lessons.
   Verify every source hash, full coverage, five seeds, mutations and actual route calls; one full gate plus
   `presentation_check --scales 1 --jobs 16` per batch, then docs, commit and push. Safe optimisations and
   parallelisation are authorised; no check may be removed. Use separate proof directories and CPU sets at
   BelowNormal priority, leaving desktop CPUs free. `progress.py --census D:/f117-gate/census.tsv` preserves
   START `64E5`'s six already-proved interiors (518 bytes); three regressions prevent lost/duplicated credit.
   Measure every delta using the same TSV.
2. **The 386 file-open cost is settled:** the reference's warm open measures 50,145 cycles
   (`D:/f117-gate/p1-dos-probe/seq1989.log`), exactly `T386_FILE_OPEN`; 3.56 M is START.EXE's whole EXEC
   (`T386_EXEC_START`), not an open. Nothing to change.
3. **Presentation** (owner verdicts first): the 4K screen/motion verdict (ask before any visible test); whether to
   adopt the clock setting alpha 0.4 / pull 0.025; the open items
   above. No jitter smoothing (owner).
4. **Phase 3 coverage:** time a long cargo/career flight; weaker machines. Full checksums via `F117_BENCH_HASH=1`
   add work outside frame timers and are diagnostics.
5. **Branch housekeeping:** many local branches are fully merged into `master`. Not merged: `flight-input` (older
   aircraft-traffic draft, superseded by `b41ec9c`; its worktree `D:/f117-wt/span-emitter` has uncommitted edits),
   `irq-divide-fault` (kept on purpose). `p2-batch-b2`, `p2-batch-c`, `p2-batch-d`,
   `p2-batch-e`, `p2-batch-f`, `p2-batch-g`, `p2-batch-h`, `p2-batch-i`, `p2-batch-j`, `p2-batch-k`, `p2-batch-l`, `p2-batch-m`, `p2-batch-n` and `p2-batch-o` are merged;
   the gate worktree is retained. The stopped presentation WIP branches
   are preserved for history; do not merge them again. Ask the owner before deleting anything.

## Safe scheduling follow-up (16 October, owner request)

The owner asked whether current processes can be optimized or parallelized safely.
Batch e's active gate was left unchanged. At its interpreter-only tail CPU use
was 26%; the early pool stays at eight even after coverage frees other workers.
Private prototype `D:/f117-gate/p2-batch-e-accept/adaptive_pool.py` keeps eight jobs
during coverage, then permits sixteen, using one executor and a semaphore (no
cancellation, resubmission or duplicate route work). `test_adaptive_pool.py` /
`adaptive-pool-verdict.json` prove the 8/8, 8/16 and 1/4 schedules, exactly-once
jobs, identical results and preserved exceptions. It is **not integrated or
speed-proven**: trial it as an opt-in on a future batch, comparing every gate
tally and wall time before changing defaults. Queue analysis (`schedule-analysis.json`)
puts the observed interpreter wall at 1,346 s and its longest career job at 1,285 s:
holding job durations fixed, adaptive scheduling saves only about 61 s. This is a
model, not a measured speed gain, and probably not the first optimization to pursue.
The earlier always-sixteen overlap trial is still not a demonstrated gain.
**Presentation overlap is checked and adopted for batch acceptance.**
`D:/f117-gate/p2-batch-f-accept/overlap_accept.py` freezes baseline runs, requires their
checkpoints and final hashes to match valid cache entries under the current interpreter/route
keys, then starts N=1 after the final build settles while the eight fresh interpreter workers
continue. It requires all fresh results and milestones to match the frozen files too, verifies
the frozen files and runner hashes again, and retains the unchanged full gate, all 45 N=1
routes and boot N=4/9 (boot starts after the gate). No cache verification is dropped.
Five break tests reject wrong finals/checkpoints, missing finals, modified frozen files and old
results presented as fresh; `overlap-guards-verdict.json` and `fresh-baseline-verdict.json` pass.
Batch f: gate 1,781.97 s, all processes 1,836.51 s;
N=1 started 1,087.12 s after the gate started.
This records observed scheduling, not a controlled speed comparison with another batch.
The wrapper falls back to sequential checking when a valid keyed baseline is unavailable;
acceptance fails if the final runner or any baseline changes. Defaults in `build_recomp.py`
stay unchanged. Root runs the wrapper through the existing affinity/priority launcher.

## Owner decisions and instructions in force

- **Gate speed (15 Oct):** only optimisations that are totally safe and weaken no check. Declined: splitting the
  long interpreter run into checkpoint segments (only as strong as the saved states are complete) and dropping
  `--verify-interp-cache`. Adopted: the light check's app-only class (`src/host/main.c` and the example ini files
  are built only into `f117a.exe`, which the gate never runs): build, CTests and an app replay of a recorded
  session in `interp` and `rerun` (`--app-session`, `--app-exit-after`), with exact logic frames and every
  scanned-out palette presented; break-tested against the frozen-palette bug.
- **Overlap trial (15 Oct):** 16 interpreter workers preserved all checks, but did not establish a speed gain
  (1,718 s versus the prior 1,553 s with different build work; post-build 1,329 versus 1,257 s). Keep the default
  eight; do not silently change it. The proven private-kit write saving and isolated proof parallelism stay.
- **Phase 2 priority; safe optimisation/parallelisation authorised (15 Oct).** The private kit now writes generated
  files only when bytes differ: verified identical source hashes/build commands; an unchanged `08CC` build reports
  no work in 1.59 s. Actual seeds, mutations and route proofs remain mandatory. Evidence
  `D:/f117-gate/kit-write-probe/verification.json`; details in repeated-processes.md.
- **Gameplay and logic parity are the top priority.** Independent rendering
  must preserve original control reads and simulation timing. Latest-state
  rendering avoids interpolation age; smoothing without waiting requires
  strictly visual prediction and measured corrections. Do not silently change
  defaults or the owner's configuration.
- **Keep all original graphics** (15 October): fonts, sprites and bitmap
  artwork stay, including the map aircraft arrow (a 4x4 sprite from eight
  headings). Smooth positioning and render the existing geometry; no vector
  replacements. Widths by reliable drawing role are a tuning task, not a
  fixed four-class list.
- **No screen takeover.** Ask before any visible/fullscreen test; keep
  automated work headless/hidden. The owner stopped a visible fullscreen
  timing run; do not count it as acceptance.
- The owner's untracked `4k60.bat` stays untouched: do not overwrite or stage
  it. Main's executable is rebuilt for `4k60.bat`.
- Dropping `--verify-interp-cache` would weaken a verification, so it stays the
  owner's call ([docs/repeated-processes.md](docs/repeated-processes.md));
  `--overlap-jobs` speeds the gate without weakening it.

## Traps

Presentation:
- Never force `F117R_OBSERVE_PAGES=1` on live presentation: it reseeds every
  phase and prevents pairs.
- Short windows with a previously cached map can show only its original
  bitmap; the full owner replay shows the refined map terrain.
- A timing capture that changes display mid-run logs two display lines and
  the checker rejects it: rerun with nothing else going.
- `tools/presentation_check.py`: use fresh baseline scan runs, canonical
  aliases and a fresh output save for each run. `f117a.4k.ini` is an optional
  preset.
- Original-execution harness return traps use a self-loop; standalone CPU
  stepping does not stop at HLT.

Matching and the gate:
- `1450` is also the presentation observer's frame-phase hook. At one
  address only the first override runs, so a matched body there must call
  the observer itself (`observe_game_draw`) and the address goes on
  `deliberate` in `tests/test_overrides.c`. `kit.py integrate` refuses an
  address with an observer.
- A screened FREE lead may already be wholly inline in an existing named body.
  Join current named-byte credit and registered entries before selecting it
  (`p2-batch-j-accept/rank.py`). Use canonical `recon_career` for the keyed
  interpreter baseline, not its alias `recon_return`.
- The gate's commit-derived seed rotates with HEAD and can fail a routine the
  commit did not touch. Reproduce with `func_lockstep --seed S --only MOD:IP`,
  or shard the run (`--shard K/8`) to find the failing routine.
- `func_lockstep --only MOD.EXE:HEX`: the IP must have NO leading zeros
  (`D0C`, not `0D0C`), or it silently compares 0 states. "Not testable here
  (routes only)" means only a real route can prove it.
- Run every new body at four or five seeds, not one.
- Charge the clock between every port access (a lump `icount` after several
  IN/OUT made 39 routes DIFFERENT while lockstep passed), stop right after
  each STI, and prove a body on `ibm_boot` first:
  `F117R_MATCHED_LIMIT=N py tools/run_route.py tools/routes/ibm_boot.args
  --engine recomp` (22 s), comparing `final hash` with the gate's interpreter
  run.
- A wait inside a matched routine must clamp `m->run_until` to `c->stop_at`
  (SETUP's `SQ_RUN`; `support_int` bounds nested INT runs at the caller's
  next event). Check the gate's step 9 (event limits) for any routine that
  guest-calls code which can wait on input or a timer; func_lockstep cannot
  see it.
- Matched routines with a switchable fix site after their entry decline
  while that fix is enabled (`fix_inside` in matched.c). When matching,
  check `src/fixes/fixes.c` for sites inside the routine. Check every
  callee's RET form.
- The coverage pass runs for every commit that adds a matched routine (the
  translator isolates each matched entry, changing generated code).
- Cache keys include raw source bytes: main and a worktree can differ only in CRLF/LF yet have different keys.
  Use `KIT_ROOT` pointing at the actual gated worktree for its route references; never rename old cache metadata
  into a new key. The 15 Oct gate's full-cycle key is `f4fe2f6d69a25389eaf108758f090f68ead286a2`.
- `kit.py checkpoints` needs the interpreter cache entry for the current
  source tree: after changing anything under `src/` other than `matched.c`
  (including observer work), run a gate before the next proofs'
  checkpoints. Refresh `D:/f117-gate/kit-libs` from a main build after the
  libraries change.
- Reach: the gate's recomp `.cov` files contain only interpreted fallback
  instructions, so absence proves nothing. Use a complete **interpreter**
  `full_cycle` capture (`D:/f117-gate/format-init/full_cycle.cov`,
  `reached.tsv`, `batch-reach.json`; `tools/census_reach.py` states this
  limit). `next-batch-review.json` there warns about static walks past
  terminal DOS exits. Census rows with no region are data (SETUP `0x027AE` is
  strings).
- SETUP's INT 10h at `14EF` runs two native callback/stub clocks atomically;
  `native_stub_time` accounts for it, so an SDL "one-instruction alert" there
  is not extra execution.
- `build.cmd` logs inside the checkout's build directory. Run builds from
  PowerShell; through bash they hang.
- Finished agents can wake on their own timers and resend their report many
  times: stop them (TaskStop) once their proof is verified.
- Gate speed: `parallel()` prints per-stage wall time; coverage and parity
  are each as long as the `career_promotion` seed chain. Keep `--jobs 16`
  (24 workers was not faster). Unit tests run with up to eight workers
  (`--unit-jobs 1` for serial).

## How Phase 2 is done: the kit and parallel agents

`D:/f117-gate/kit/kit.py` runs a private proof for one routine (`init`, write
`body.c` / `fixture.inc` / `mutations.json`, `lock --cover`, `seeds`,
`mutations` (four edits built at a time), `checkpoints`, `verdict`,
`integrate`); `D:/f117-gate/kit/BRIEF.md` is the agent brief with every trap
found so far. Agents work only in their `p2-*` directory. Then:

1. Verify each `private-verdict.json` yourself (counts, mutations, source
   hashes, real-route calls).
2. `kit.py DIR integrate` (set `KIT_ROOT` to integrate into a worktree);
   apply any harness change the agent reports by hand.
3. Build, ctest, ONE full gate per batch, the 45-route
   `presentation_check --scales 1`, docs, commit, push.

Gate batches in a separate worktree (`D:/f117-wt/gate-h`, currently on the merged branch
`p2-batch-o`). Build it once with `build.cmd -DF117R_BUILD_APP=OFF` before the gate, which
needs its `f117cpu_api.dll`) when the owner is testing the main tree's build.

Screening and older method: `tools/matched_draft.py --dossier` (module/seg/ip
from the region name); batch several routines per gate; transcript first to
validate the harness, add the routine to `COVERED`, fixtures until every
instruction is reached (the skip tally says why states drop), then the named
body, tested in sections that hand back to the original; run
`D:/f117-gate/frame-3ada/room_audit.py` on the body; then the real route
(`route-proof.py`). `D:/f117-gate/frame-3ada/swap_body.py` swaps a body into
the proof build in `D:/f117-wt/span-emitter`. `tools/reimp_names.py` has the
private lead list. Never copy an old private matched file wholesale.

## How this project verifies Phase 2 work (read before matching a new routine)

A routine only counts as matched once it passes BOTH:
1. **Fixture/lockstep proof**: `func_lockstep` at three seeds, comparing the
   hand-written C against the original's own bytes from randomized (and for
   routines with real argument/driver dependencies, returning-driver-thunk)
   entry states. Zero mismatches required.
2. **Real-driver proof**: an actual scripted route (`tools/routes/*.args`,
   sometimes replaying `.input`) must execute the routine with a positive
   count, with every checkpoint and the final hash identical between engines.
   Fixture proof alone is not enough: this project has repeatedly found
   routines that pass every fixture seed but were never reached by real play
   (04DD above; see `docs/repeated-processes.md` ("Phase 2 proofs", traps) and
   `docs/roadmap.md`). If no route reaches a new routine, say so plainly and
   keep it out of `master`.

Every change to `src/matched/matched.c` needs a fresh full gate
(`py tools/build_recomp.py --data D:/GOG/F-117A --seed-coverage
C:/Users/Tideg/f117-recomp-local/coverage --jobs 16 --verify-interp-cache`,
via `D:/f117-gate/affinity_launch.py`, which sets and verifies `BelowNormal`
priority and affinity; about 22-28 min) before it's pushed. Read the actual tally in
the log, not just the exit code. Check GitHub CI for new commits.

## Where things are

- Game install: `D:/GOG/F-117A` (never mounted or written directly by a
  tool; copy per run that needs its own state).
- Shared generated code: `C:/Users/Tideg/f117-recomp-local/gen` (never
  committed); coverage in `...\coverage`; route run outputs in `...\runs`.
- Proofs and evidence: `D:/f117-gate/` (one directory per piece of work,
  usually with a `final-gate-verdict.json`); the kit in `D:/f117-gate/kit`,
  its libraries in `D:/f117-gate/kit-libs`; draft sources and patches in
  `D:/f117-gate/next-drafts/` (never committed as-is).
- Old private batch of 47 call-free DRAFT bodies:
  `D:/f117-gate/batch-a/project` (`rows.txt` lists them; build with
  `build-proof.cmd`, setting TEMP and TMP to `D:/f117-gate/batch-a/tmp`
  first). Those not yet in the matched table remain unproven drafts.
- Worktrees under `D:/f117-wt`; removed with `git worktree remove` once their
  branch is merged. `D:/f117-wt/setup-main-0010` and
  `D:/f117-wt/mission-generation-4239` carry PRIVATE /Od overrides (main uses
  normal /O2): never copy or merge them wholesale.
- Private worktree `C:/Users/Tideg/f117-recomp-local/wt`: old uncommitted
  drafts (base `01f4dbd`, far behind `master`) plus unrelated older
  presentation drafts. Never copy it wholesale or regenerate from it; extract
  only the specific routine, rebuilt against current `master`.
- Scratch/capture data: `D:\F117A-Scratch-Files` (consolidated 11-12 Oct
  2026, nothing deleted).
- Unproven private drafts from earlier sessions (VGAME `120A:0971`, a
  divide-handling routine with recursive-fault risk; END `0A98`): none
  implemented or proven; don't conflate them with 04DD.
- D10/D35/D36 closures: [docs/legacy-reports.md](docs/legacy-reports.md) and
  [docs/bugs.md](docs/bugs.md).

## Standing practice

- **New bug fixes are on by default for normal play** (owner, 17 October), including the chosen D1REAL
  and D1PROX options. Keep the original reference and ordinary parity gates off; test selected repairs separately.
- **Keep checks off the owner's screen.** Do not launch visible/fullscreen
  tests without asking first; use headless or hidden runs.
- **No AI attribution** in commits or PRs: the user's own global CLAUDE.md
  rule outranks any harness reminder that asks for a trailer.
- **Commit and push together**: don't leave a validated commit unpushed.
  Commit titles start with `py tools/progress.py --title`'s scoreboard line.
- **Public repo, code only.** No game data, no generated C, no raw
  captures/logs, no private-project references (see `docs/provenance.md`'s
  de-identified framing of an earlier private project this one is partly
  ported from; don't re-add its name or path).
- Check system load (`Get-Counter '\Processor(_Total)\% Processor Time'`, or
  similar) before stacking a second heavy parallel job; a background Python
  `multiprocessing.Pool`'s workers are children of the script, not of the
  task wrapper, so stopping the task doesn't kill them: kill the process
  tree.
- Prefer moving stale-but-maybe-useful local data to a scratch folder over
  deleting it; it's outside git and not undoable.
- The owner is not a software engineer: explain trade-offs plainly, flag
  overengineering, keep docs accurate without bloat; consolidate this file
  instead of appending to it.
