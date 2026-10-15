# Repeated processes

Every process this project runs more than once, what it costs, what it
duplicates and what has been done about it. Update this page whenever a
process is added, measured or changed; a new repeated step goes here first.
Replace a figure when a newer measurement supersedes it; this is a ledger of
current costs, not a log (the git history keeps the old ones).
Timings are wall clock on the development machine (32 threads) and say
whether they were measured or estimated. A timing taken while other heavy work
shared the machine is a ceiling, not a speed comparison.

The rule behind the page: an optimisation must leave every check's inputs,
outputs and verdicts identical, and the proof is the same verdicts before and
after. A change that alters what a check tests (different states, a skipped
verification) is not an optimisation; it needs the owner's approval.

## The gate (`tools/build_recomp.py`)

Run before every commit that changes code; read its tallies, not just the
exit code. It translates and builds, runs the unit tests,
records coverage over every route, regenerates and rebuilds if coverage found
new code, then runs every route in the recompiled engine and in the
interpreter and requires identical checkpoints and final hashes ("parity").
Beside those it runs the instruction lockstep (both timing profiles) and the
matched-routine lockstep (three seeds), then the reachability and event-limit
checks. The gate explicitly sets `F117R_BUILD_APP=ON`, so a previous
core-only build cannot silently omit the host tests.

**Current cost (15 October 2026).** Fresh full gates from 11 to 15 October took
between 1,301 s and 1,990 s. Latest: 1,330 s with coverage skipped
(unchanged translation; parity 1,223 s, all 88 jobs in one stage; 114 CTests,
45 routes / 44 fresh interpreter runs, both instruction profiles, 891 matched
routines at three seeds). Latest with coverage: 1,706 s (initial
translate/build 113 s, coverage 711 s, second translate/build 35 s, parity
848 s; 112 selected tests).

| Stage | Latest measured | What sets it, what was duplicated | Status |
|---|---|---|---|
| 1-2 translate, build | 113 s (15 Oct); 26-344 s with unit tests from 11 to 15 Oct; 7-8 min in a fresh work directory | a fresh work directory builds everything; a CPU-header edit rebuilds all generated C and both matched objects (about six minutes) | **fixed by practice**: reuse the previous run's work directory (`sync_tree` rewrites only changed files, so the build recompiles `matched.c` and relinks); the output is a function of the sources either way. Oversized generated regions are capped at 1,024 instructions (a 6,446-instruction VGAME function had dominated MSVC: 479 s before, 120 s after; both timing profiles keep their instruction verdicts) |
| unit tests | 22.47 s for 75 tests at eight workers (159.76 s serial) | ran serially before the route workers | **fixed (14 Oct)**: up to eight workers; `--unit-jobs 1` restores serial, `--unit-jobs N` changes only this stage. Same executable, inputs, selection, affinity and priority: every test's captured output identical to the serial run. Proof: `D:/f117-gate/leaf-batch/unit-parallel-verdict.json` |
| 3 coverage | 711 s (15 Oct); 487-715 s from 11 to 15 Oct; or skipped | runs every route once only to find untranslated code; as long as its longest job (`career_promotion`, after its `career_serge` seed) | **skipped when provably empty** (7 Oct): a record (`~/f117-recomp-local/coverage-verified.json`) holds the key of a pass that added nothing - translator, sources other than `matched.c`, game files, routes - and the digest of the generated code it ended on; the same key and digest skip the pass. A matched routine only replaces original instructions that already ran, so it cannot add code to cover; a wrong one fails parity or lockstep. Verified: full verdicts identical with and without the skip; 995 s became 422 s. The key hashes `CMakeLists.txt` without `add_test` entries, whole-line comments and blank lines (`build_digest`, 12 Oct); hashing it whole had made every gate that added a test rerun coverage and miss the interpreter cache. A commit adding a matched entry still reruns coverage |
| 4 translate, build again | 35 s (15 Oct); 220 s when coverage added code (SETUP, 13 Oct) | regenerated and rebuilt everything though coverage usually adds nothing | **fixed**: only files whose bytes changed are rewritten; the rebuild is skipped when none did; `--seed-coverage DIR` starts from an earlier run's coverage. A rebuild restarts the locksteps, so their earlier results do not stand for the final build |
| 5 parity | 848 s (with coverage), 1,223 s (coverage skipped); 749-1,223 s from 11 to 15 Oct | as long as `career_promotion`'s interpreter run, which first replays its whole seed chain | **fixed**: longest jobs first, identical sessions run once (found by content, not name; today `recon_career` and `recon_return`) and every route's milestones are checked on the shared run. **Overlap (15 Oct)**: `--overlap-jobs N` (default 8; 0 = as before) starts the interpreter runs during coverage from a private copy of the first build's runner and libraries (`snapshot_runner`); parity then runs only the recompiled half and collects them. Runs, comparisons, cache verification and verdicts unchanged. **Coverage-run reuse (15 Oct)**: when coverage adds no translated code the first build stands, so the coverage runs (now with the parity checkpoints) are the recompiled half; `--check-coverage-reuse` reruns them and requires equality (proved 15 Oct: all 44 equal, every verdict identical), `--no-reuse-coverage-runs` restores the old behaviour. **Measured 15 Oct**: 1,706 s before (coverage 711 + parity 848), 1,556 s with the overlap (coverage 743, parity 687 while the interpreter half finished); the proof gate (full rebuild 284 s, forced coverage, check reruns) 1,853 s. Now the gate ends when `career_promotion`'s interpreter chain does (1,565 s beside coverage, slowed by sharing the CPU), so the reuse saves CPU (513 s of recompiled reruns) rather than wall time; shortening that one chain is the remaining lever |
| 5b interpreter half of parity | all 44 sessions run fresh in the standing command | an interpreter run is a pure function of the game files, the route and the interpreter's own code | **cached**: key over `src/` except `matched.c`, `CMakeLists.txt` (as above), the game files and the route (text, replay files, seed chain); entries expire after 14 days. Measured: parity 522 s to 174 s with 31 of 31 reused, every verdict identical. The standing gate command passes `--verify-interp-cache`, which reruns every session and requires equality, so "0 reused, 44 run" is expected and the cache saves nothing there |
| 6-7 locksteps | add 0 s | waited for parity though they need only the build | **fixed**: start right after the first build, beside coverage and parity; output to files in the work directory. Measured 7 Oct with coverage forced: 778 s to 572 s, the lockstep tail 241 s to 0, every route line and both tallies identical |

Before these changes (6 Oct) the gate took 30, 38 and 32 minutes; after them
the same code (231 matched routines, 32 routes) took 22.8 minutes (1,365 s
against 1,895 s) with every route's clock, hash and verdict and both lockstep
tallies identical.

**What sets the wall time (measured 14 October).** `parallel()` prints each
stage's wall time, work, lower bound (the larger of the longest job and
work / workers) and its four longest jobs; read that before changing worker
counts. `--parity-only --verify-interp-cache`: 88 jobs, 16,736 s of work on 16
workers (1,046 s if perfectly packed), stage 1,160 s, longest job 1,159 s
(`career_promotion/interp`). A seeded route's job first replays its whole seed
chain (`prepare_roster` in `run_route.py`): `career_serge` (790 s as its own
job) plus its own run in sequence. The chain, not the worker count, sets both
long stages. Reusing the seed's roster would save work, not chain length.
The remaining lever, dropping `--verify-interp-cache`, weakens a verification
rather than speeding up the same one: the owner's decision, not yet taken.

**Workers, affinity and priority.** 16 route workers. A 24-worker run (12 Oct,
`D:/f117-gate/gate-jobs24.log`, same executable, translator, harness and
routes as the 16-worker baseline, coverage forced, every interpreter session
fresh) gave identical verdicts and no route speedup; retain 16:

| Stage | 16 workers | 24 workers, BelowNormal |
|---|---:|---:|
| Translation, incremental build, 35 tests | 166 s | 46 s |
| Coverage | 538 s | 557 s |
| Byte-identical regeneration | 19 s | 21 s |
| Fresh parity | 822 s | 830 s |
| Total (rounded laps) | 1,546 s | 1,455 s |

The apparent saving is build time; coverage and parity together were 27 s
slower. Proof: `D:/f117-gate/jobs24-proof/comparison.json`. The gate runs
under affinity mask `0x00FFFFFF` (24 of 32 logical CPUs) and
`BELOW_NORMAL_PRIORITY_CLASS`, inherited by every child, leaving eight CPUs
for the desktop; three eight-shard matched checks plus 16 route workers had
oversubscribed the machine. `D:/f117-gate/affinity_launch.py` sets and reads
back both; an earlier ctypes wrapper failed silently (use pointer-width
signatures and check the result). For actual CPU load use Processor %
Processor Time (about 52% during parity); WMI LoadPercentage reported 96% at
the same time. More balanced lockstep chunks and a shared CPU budget for
route/lockstep workers remain experiments to benchmark first.

**Traps.**
- The third matched seed comes from the gated commit's hash, so it rotates:
  it can fail a commit that did not touch the failing routine. Reproduce with
  `func_lockstep --seed S --only MOD:IP` on a build without the new body, or
  `--shard K/8` to find the routine. `--only` takes an unpadded hexadecimal IP,
  not segment:offset; a wrong filter silently compares nothing, so read the
  per-routine comparison counts. Each failing seed is retained as a CTest.
- A matched body at an address shared with an observer hook fails unit step
  `code_overrides` (the first failure surfaced after 23.8 s, before any route
  work): such a body must call the observer itself.
- `build.cmd` logs inside the checkout's own build directory. With the old
  shared `%TEMP%` log a build in another worktree overwrote it and a scan
  printed BUILD OK for a build that had not linked `func_lockstep.exe`. Check
  the executable's time when other checkouts are building; check the success
  marker and the compiler log as well as the exit code (a failed Windows
  build has returned zero leaving an old executable).
- Parallel command printing once mixed data into the combined console log;
  command printing is now serialized (child arguments, options and results
  unchanged; 12 gate-helper tests). Per-route results are separate files.
- Captures and gate builds must not overlap a running executable they rebuild:
  the running Windows executable locks build outputs.
- The GitHub Windows wall-clock pacing check is timing-sensitive (one failure
  at `7df74bf`: added time 0.109 s against the required 0.45-1.2 s, green on
  rerun of the job; the local check passed at 0.687 s).

## Presentation checks

**After a presentation-code change**, all headless:
- `ctest -E func_lockstep -j 8`: latest 111 selected CTests in 36.56 s
  (15 Oct).
- `tools/presentation_check.py --data DIR --baseline-runs DIR/runs --out DIR
  --scales 1` over the 45 routes: 482.30 s at 16 workers; 1,321.61 s at
  eight workers beside a gate. It keeps 259,240 exact
  logic frames / 259,166 exact interpolation pairs and every guest checkpoint
  and final hash. Default scales are N=4/9 (`--routes STEM...`,
  `--scales N...`, `--jobs N`). Run the full gate first for fresh baselines.
  It requires matching guest checkpoints/final hashes and route milestones,
  every logic frame and interpolation endpoint exact, no unseeded or dropped
  frames, and positive presentation pairs on routes that enter flight (a short
  SETUP-only route may have no 50M-clock checkpoint and no flight frames).
  These concurrent runs check correctness, not refresh-rate performance.
- `boot_to_flight` at `--scales 4 9`: 56 s at two jobs (58.91 s in another
  run), 368 frames / 366 pairs each.
- The retained windows: 96 replay/refinement/interpolation checks across 16
  windows. Run `build/test_drawlist`, `test_hires` and `test_interp` with
  absolute paths: the relative `build/` junction fails under Python's
  `subprocess`.

**The light check** (`tools/present_light_check.py --base REV`,
owner-approved, 15 Oct) replaces the full gate for presentation-only changes.
It refuses unless every file changed since the last full gate's commit is
under `src/present/` and not included from guest-side code (the observer
includes `band_state.h`, `hud_state.h`, `scene_capture.h`), a presentation
test or documentation; then it builds and runs every CTest, the 45-route N=1
check against the last gate's baseline runs, and boot at N=4/9. Observer,
matched, CPU, machine and host changes still need the full gate, and a full
gate is still needed before the next Phase 2 `kit.py checkpoints` (any `src/`
change retires the interpreter cache entry).

**Rules that keep these checks valid.**
- Freeze the runner outside the build and set `F117R_RUN_EXE` to its absolute
  path before an all-route check. Rebuilding it during a run invalidates the
  result even if every route passes; the checker verifies the runner hash at
  the end. Freeze baseline logs and latest saves before a gate can overwrite
  them.
- A capture file with no records fails the checker: seven empty HUD capture
  files once passed as "model success". A diagnostic that counts a bad record
  must also fail: the interpolation provenance audit returned success with 48
  violations over 51 intermediate frames until unknown kinds, and more novel
  records than the generator counted, became fatal (private midpoint-only
  mutants of both pass the old validator and fail the new one; keep this
  check separate from endpoint equality).
- Keep every automated check headless or hidden; ask the owner before any
  visible or fullscreen run.
- An owner replay uses a fresh copy of `save-start` and the input header's 20
  MIPS / time. Leave `F117R_OBSERVE_PAGES` unset for live drawfeed: forcing it
  reseeds every phase, prevents interpolation pairs and loses carried fine map
  terrain. Create the diagnostic output directory first; the writer does not.
  The owner's recorded flight: 69 guest checkpoints, 2,101 logic frames /
  2,099 pairs, final `31c259b992340414`.
- `tools/motion_check.py LOG` measures pairing completeness, deformation of
  paired batches and step-timing irregularity, which the exactness checks
  cannot see ([presentation.md](presentation.md)).

**Component regressions** (all ROM-free unless they need the owner's
generated modules or a private corpus; no original bytes are committed):

| Check | Cost | What it covers |
|---|---|---|
| `test_present_fills` | under a second with the next | 2,619 staged cases against the original VGAME fill selector (style classes, clipping, row/column parity, odd page alignment); `--rows` feeds `tests/test_present_styles.py --original build/test_present_fills.exe` (2,187 rows agree) |
| `test_present_driver` | under a second | 3,953 staged cases against the original resident graphics driver (text, clips, fonts, sprites, scaled RLE, flips, tick scales) |
| `test_present_map`, strip and near-plane regressions | under a second together | orthographic triangle edges, rectangle round trips, cached ownership after a replay copy, stale-source fallback at N=1/4/9; no holes between near-clipped faces |
| flat-cell guard scalar regression | 0.22 s | 8/4-byte group comparison: complete output bytes and all counters at N=1..16, tails, boundaries, protected HUD cells |
| `test_world_lines`, `test_world_jitter LOG N` | - | coordinate/depth continuity, exact coarse replay, stale slots, stroke widths; jitter isolates interpolation rounding only (N=9 strike RMS: outlines 1.680 -> 0.0039, dots 2.756 -> 0.0140, negative model axes 0.496 -> 0.0634 fine px) |
| `test_band_state CORPUS_DIR` | - | 47,516 passes, 1,514,560 quads, 47,313 blend pairs exact (six discrete-state refusals); corpus via `F117R_OBSERVE_BANDS=1`, `F117R_OBSERVE_KINDS=PoUKQ`. `test_band_rules`, `test_band_observer` are ROM-free and check no guest/clock/bus change in both timing profiles |
| `test_hud_state D:/f117-gate/present-v2/hud/corpus` | 0.4 s over 288 passes | ctest `presentation_hud_state`, skipped without the corpus |
| `test_hud_live LOG 4 --leak-check`, `test_hud_jitter LOG 4` | 3 s per 35-74 frame window; windows take 40-120 s to record | record with `F117R_OBSERVE_PAGES=1 F117R_OBSERVE_HUD=1 run_route.py ROUTE.args -- --steps TO --observe LOG:FROM:TO` (fly3 2.79B-2.81B, fly1 2.86B-2.88B, strike 8.53B-8.56B). `--leak-check` copies the fine page around every replaced record; leave it off when timing |
| fine HUD recorded scenes | about 10-15 s for eight windows at N=1/4/9, three workers | reuse `p3-live/logs41/w_*.log` with `test_drawlist` and `test_hires`; older strike/air captures omit colour-replace records and fail the unchanged baseline too. `test_hires --dump PREFIX` renders the last checked frame (about a second per 69-phase N=4 window) |
| model-input decoder | about 2.4 s for 205 detail variants; synthetic 0.05 s; capture parser 0.7 s | original-byte model checks run from bounded private snapshots (192 captured requests), not a whole flight per model; they prove format/geometry, not route reachability |
| draw-list capture windows | 3-4 min per window, four at a time | windows are independent and already run in parallel |

Python pattern and verdict-parser regressions run in ROM-free CI.

**Optimisations proven by identical reports.** Fine rasterisation visits only
the polygon's geometric y range inside its viewport, and rectangle buffers are
reused when replay states are copied (all 96 window reports identical
before/after). The flat-cell guard compares safe unaligned 8/4-byte groups and
tails (full fine-frame checksums, seam-repair tallies and flat/agree/restored
counts preserved; all retained-window checks pass).

**Observer path comparison** (after an observer or matched-dispatch change).
Capture with both engines and compare ordered V and G payloads (all values and
record IDs), record counts, guest checkpoints and final hashes. Perspective V
timestamps can differ by the RET instruction (the matched body records after
charging RET, the original hook before it): a capture boundary, not changed
guest time. Under the 386 profile both engines run the original projection,
so complete logs compare byte for byte. Matching guest hashes alone missed a
shared-address observer bypass that dropped G/E/F records. Measured: boot
capture 217,022 V / 300,637 G payloads identical (interpreter 52.0 s on four
spare CPUs beside a gate); a staged 386 menu script to clock 8,000,000,000
gives a byte-identical 751,862,587-byte log. Evidence:
`D:/f117-gate/observer-original`.

## 4K refresh timing

Capture an isolated application replay with a fresh save directory:
`f117a --no-config --data DIR --save NEW-DIR --no-record --replay
tools/routes/strike.input --exit-after 3500000000 --present interp
--present-scale 9 --fullscreen --present-log FILE --log MACHINE-LOG`. The
replay sets its recorded 9 MIPS clock. Wall time is fixed by the guest clock
(the machine is paced to it): strike to 3.5B 389.8 s, air-to-air type 6 to
5.9B 656.4 s, landing to 10.0B 1,112 s. Compare the final guest hash with an
isolated headless run of the same input, fresh save and stop count; SDL writes
its final hash to the machine log, not stdout.

`py tools/presentation_timing.py FILE --out VERDICT.json` checks 3840x2160 at
60 Hz by default (`--resolution W H`, `--refresh HZ`, `--ips N`). It requires a
GPU, vsync, sufficient fine resolution and at least 600 flight pictures;
median interval within 10% of the refresh period, p99 within 125%, no more
than 1% of intervals above 150%, consistent interpolation time and no more
than 100 ms accumulated guest drift. Malformed, incomplete, wrong-target or
mid-run display-change captures fail (a run that started on a 1080p display
and moved to the 4K one was rejected). Synthetic verdict regressions run in
CI. It measures submitted pictures on the available display, which may be
virtual; tearing and perceived motion on a physical panel need the owner's
eyes. Keep raw timing and game logs private.

Latest (15 Oct, with state-driven bands): strike at 3840x2160 / 59.99 Hz /
vsync / N=9, 7,718 flight pictures, p99 16.7314 ms, maximum 17.0385, no missed
interval; 2,062 exact logic frames, final hash `bc8e3ae5a872405d` equal to a
fresh headless reference. All three scenes and the four-CPU affinity run pass:
[presentation.md](presentation.md). Run with nothing else heavy going; runs
beside the gate are exploratory, not acceptance evidence. A private hidden
3840x2160 Direct3D 11 probe (current library, all state/interpolation flags,
five intermediates per pair) measures strike / mode-2 / small windows at
119.47 / 127.45 / 150.10 fps uncapped, regeneration/raster p99 below 5 ms,
with occasional roughly 100 ms upload stalls; uncapped numbers do not prove
refresh pacing. The owner stopped a visible fullscreen run; it is not
evidence. Evidence: `D:/f117-gate/present-v2/band-finish`,
`D:/f117-gate/present-v2/hud2`.

## Phase 2 proofs (matched routines)

**Process.** Proofs run in parallel agents, each in its own
`D:/f117-gate/p2-*` directory, with `D:/f117-gate/kit/kit.py` (init, build,
lock, seeds, mutations, checkpoints, verdict, integrate; method and traps in
`D:/f117-gate/kit/BRIEF.md`), while one main-tree gate runs. Verify every
verdict yourself, then land a batch with one gate (eight routines plus a
harness fix: 1,743 s to the last lockstep).

| Step | Cost | Note |
|---|---|---|
| private build loop | initial 9.9 s, edits 2.6-3.6 s | a direct candidate matched object linked before frozen generated/core libraries; no generated sources recompile. /Od is development evidence only (it avoided a very large optimizer peak); integration needs /O2, optimized seeds and a full gate. Library hashes kept with the private project |
| private generation in a worktree | 21.9-30.7 s | from frozen coverage, its own generated directory; build with two workers and its own TEMP, after main's matched shards finish |
| targeted seeds (`func_lockstep --only MOD:IP --states 4000`) | 0.4-5 s per seed; 23.3 s for the 1,332-instruction mission generator | run every new body at four or five seeds: the default seed passed a body with a wrong resume point |
| mutations | 37 edits about 8 min serially, 90 s four at a time | identical CAUGHT results; the kit never edits `body.c` |
| isolated unit tests | 29.72-80.52 s (36-44 tests) | |
| real-route replay | 0.8 s (`setup_calibration`) to 190.6 s (camera-image on `camera_gradients`) | needs positive matched invocation counts; per routine below |
| full gate | as above | one per batch |
| census refresh (`reimp_names.py`, then `progress.py --census`) | about a minute | the lead list changes only when the Reimp does; refresh the census each batch |

**Method that worked** (from 1,274-instruction routines up): insert the
`matched_draft.py` instruction transcript first to validate the fixtures,
require per-instruction coverage of the original's run (`COVERED` in
`tests/func_lockstep.c`, a per-routine floor and a skip-reason tally: fault,
long run, bad return, code written, declined, unfinished), plant fixtures
until every instruction is reached, swap in the named body, run the room-claim
audit, the mutations, the real route, then the gate. If the first lockstep
run is already equal, run the mutations before trusting it.

**Traps that still apply.**
- Escaped calls pass vacuously: a callee ending in `RET 8` called as a plain
  near call let the original finish every state. The harness fails such an
  escape where the original returned from the call's stack depth (up to 16
  bytes above it). Check every callee's return form.
- Room claims: compute them from the listing (longest path between resume
  points; `rep movsw` weighted by its iterations), not by hand. The
  lockstep's limit probe tests only the end, so a short mid-routine claim
  passes every seed; `room_audit.py` checks each claim against the
  instructions counted after it. The mid-run probe also checks no-call clock
  overshoot. Every `SM4_NEED` resume address must be the instruction the state
  is actually at; no audit checks this, only other seeds.
- A wait inside a matched routine must stop at `c->stop_at` (`SQ_RUN`), and a
  nested INT run is clamped to the caller's next event (`support_int`):
  otherwise the run overruns the event limit on every route, which the
  lockstep cannot see and step 9 of the gate catches. Private route checkers
  reject overrun reports as well as differing hashes.
- Stubs: write each into the segment its call executes in (data-segment
  writes left the real routines running and skipped 2,815 of 4,000 states).
  Stubs that fold their stack words into a checksum make a wrong pushed
  argument a memory difference; a plain RET stub hid one mutation.
- Relocated operands: a segment immediate is relocated by the loader, not a
  constant; zero-base fixtures miss it (Ctrl+Break installers, the logo record
  builder). Use a nonzero image base or a varied operand.
- Terminal DOS error helpers do not return: replace them in fixtures with a
  jump to the routine's real epilogue so error arguments are compared. Real
  routes keep the original exits.
- Zero-count loops: only the authored states get the 500,000-step guard; a
  skipped zero-count case fails explicitly instead of disappearing into the
  skip tally.
- Divide faults: compare CPU and memory right after the original's DIV
  dispatch (whole interrupt frame, registers, flags, clock, memory) before
  following a random vector. Staged faults are not normal-play evidence.
- A transient code-write watch (whole body) excludes self-modifying fuzz
  states that overwrite and restore code before the final comparison.
- DS=SS stack aliasing: read stack arguments where the original does; a store
  can land on an argument's slot (`vgame_map_project`, objective place,
  `vgame_eventlog_add`).
- A first raw run that compares zero or one state is not evidence; plant
  valid pointers and small records. A real route that does not reach the
  routine (e.g. F5 selects armament, not the camera) is not proof of it.
- Tick-waiting routines resynchronise their timing and mask defects on the
  available routes; START `8427` and END `4393` stay original.
- Keep live-runner executables frozen before rebuilding private candidates.
  Limit large proof builds to one compiler job and a private TEMP/TMP. Never
  copy private development flags or generated sources into the public tree.

**Complete execution inventory.** The gate's `--engine recomp --coverage FILE`
logs only interpreted fallback instructions, so a fully translated or matched
entry can be absent: missing entries never establish unreachability. For a
whole route use a fresh interpreter run: `py tools/run_route.py
tools/routes/full_cycle.args --data DIR --engine interp --out NEW_DIR --
--coverage NEW_DIR/full_cycle.cov`, kept separate from the gate's logs.
`tools/census_reach.py --census PRIVATE_TSV --coverage NEW_DIR --out
PRIVATE_TSV` joins either source and describes absent entries as lacking
evidence. A fresh full-cycle capture (66 checkpoints and final preserved)
records 358 unmatched census entries / 43,138 bytes, overturning the old
fallback-only claim of 72 reached routines. Private inventory:
`D:/f117-gate/format-init/reach-verdict.json`.

**Machine-boundary regressions.** `native_service_boundary` (ROM-free, 0.03 s)
runs a real INT 10h callback at a slice boundary and requires the overrun
report to accept it and reject an added NOP. For SETUP startup, eight
fixed/random slice schedules in both engines compare per-slice records (32
short boots, about 2 s per 16-run batch at eight workers); 24 silent SDL boots
to 3M clocks (four workers, 6.5 s) keep the same final hash. These measure
bookkeeping and state equality, not presentation pacing. Use fresh saves.

**Open.** The private IRET harness baseline across 811 entries compares
42,391 states without differences (three targeted seeds: 23,978 interrupt
states); a normal-driver replay reaching both handlers is still outstanding.

## Phase 2 candidate reading

`tools/matched_draft.py --dossier` writes a private JSON beside the draft,
using the same instruction walk rather than decoding twice. It records the
module image and tool hashes, segmented entry and image offset, disjoint
byte ranges (exclusive ends), encoded call targets, exits and unwalked
successors. Calls are not followed; indirect targets and caller inventory
remain unresolved. This is reusable static evidence, not a parity verdict
or a cache that skips verification. Keep both outputs outside the repo.
Generation takes under one second (the polygon caller: the same
46-instruction / 96-byte draft byte for byte, five call sites). Synthetic
regressions cover disjoint ranges, relocation, bounded walks, indirect exits
and COM segment aliases; Windows CI runs them.

- A walk's byte count is not progress or exclusive ownership: shared flow
  makes walks larger than census extents (span edge 743 bytes against 419;
  mission generator 3,811 against 3,035; armament 373 against 67). Credit
  only the census extent; shared tails are not counted again.
- Resolve indirect jumps before writing fixtures (START `4E39`: the nine
  entries at CS:4E3E lead to 4E14/4E24/4E50).
- Require a real caller or executed address before treating a census lead as
  code: DSWAP `03DF3` begins with 2,791 zero bytes, has no generated entry,
  and its linear draft decodes data as operations (no census adjustment).
  SETUP needed a fresh interpreter prefix to confirm `0000:0010`.
- Draft vocabulary check: `py tests/test_matched_draft.py` also compiles a
  synthetic MOV/ADD/branch/CALL/RET draft against the matched headers in a
  temporary CMake project (no game install, no link). TIMING records belong
  to translated regions and are omitted from instruction-clock drafts. All six
  tests pass in 2.3 s; Windows CI runs the same file.

**Proof costs per routine.** Counts and the routines' behaviour are in
[roadmap.md](roadmap.md); private evidence in the named `D:/f117-gate/`
directory. Targeted seeds are states compared per seed (including partial
stops); replays match every checkpoint and the final hash.

| Routine | Targeted seeds | Real-route replay | Full gate |
|---|---|---|---|
| polygon fill `0FB2:01E6` | 2,214 / 2,172 / 2,214, about 2.5 s each | `camera_gradients` 116.0 s, 382,365 calls | 1,770 s |
| clipped span edge `0FB2:06A6` | 4,825 / 4,815 / 4,821 | `camera_gradients`, 1,529,189 calls | 1,787 s |
| span emitters `0FB2:05A4` / `05B3` | 6,756 / 6,789 / 6,780 | `camera_gradients` 104.1 s, 507,745 / 591,404 calls | 1,709 s |
| timer tick `VGAME 1E79` | 4,565 / 4,601 / 4,610 | `camera_gradients` 117.4 s, 53,226 calls | 1,872 s |
| objectives panel `VGAME 9515` | 2,885 / 2,737 / 2,817 | F10 `objectives_panel` 39.2 s, 4 calls | 1,595 s |
| armament screen `START 07EE` | 2,420 / 2,023 / 2,016 | `full_cycle` 45.4 s, 1 call | 1,617 s |
| navigation grid `VGAME 8A90` | 2,055 / 2,080 / 2,071 | F3 `navigation_grid` 66.6 s, 314 calls | 1,600 s |
| panel frame `VGAME 79BE` | 4,906 / 4,909 / 4,914 | F3 67.8 s, 550 calls | 1,560 s |
| HUD pass `VGAME 1058:0016` | 4,916 / 4,922 / 4,912 | F3 50.5 s, 550 calls | 1,596 s |
| moving objects `VGAME 983E` | 2,710 / 2,707 / 2,654 | F3 51.0 s, 550 calls | 1,799 s |
| camera image `VGAME BCEF` | 4,907 / 4,902 / 4,926 | `camera_gradients` 190.6 s, 7,880 calls | 1,480 s |
| flight input `VGAME 1EB0` | 2,112 / 2,718 / 2,292, about 4 s each | `camera_gradients` 143.4 s, 8,464 calls | 1,745 s |
| frame update `VGAME 3ADA` | 2,931 / 2,914 / 2,912 | `camera_gradients`, 8,462 calls | see roadmap |
| ground defences `VGAME 5046` | 2,696 / 2,679 / 2,685, about 5 s each | `camera_gradients` 117.1 s, 8,465 calls | 1,834 s |
| aircraft traffic `VGAME 5852` | 2,056 / 2,017 / 2,078 | `camera_gradients` 130.7 s, 8,464 calls | 1,963 s |
| weapons in flight `VGAME 683E` | 4,844 / 4,851 / 4,862 / 4,852 | `camera_gradients` 105.8 s, 8,459 calls | 1,772 s |
| mission generator `START 4239` | two optimized seeds 23.3 s each | boot 42.4 s, 1 call | 1,832 s |
| SETUP `0000:0010` | - | eight startup cases 3.9 s on four spare CPUs | 1,608 s |
| logo sprite-set loader | - | logo-load prefix, 1 call | 1,687 s |
| camera gradients `VGAME 1974` / `19FD` | 9,576 / 9,559 / 9,591 | air-to-air probe: recomp 105.8 s, interpreter 220.4 s | 1,497 s |
| sprite run / map wrapper | 9,529 / 9,544 / 9,540 | `full_cycle` 44.7 s, map 1 call, sprite run 1,570 | 1,466 s |
| camera darkening `VGAME C45C` | 7,981 / 7,894 / 7,932 with diamond `B0C2`, about 5.7 s together | reconnaissance 123.3 / 71.4 s (original / isolated), 1,608 calls | 1,420 s |
| 22-routine support batch | four seeds 8.6 s together, 103,554-103,672 each | `full_cycle` 31.0 s, `ibm_boot` 20.7 s, `setup_calibration` 0.8 s | 1,390 s |
| START `3641` screen loader, diamond `B0C2` | 1,615 / 1,579 / 1,645; 4,967 / 4,989 / 4,977 | `full_cycle` 46.7 s; `strike_training` 76.6 s, 1,051 calls | 1,560 s |

The camera-gradient replay's changed drawing cost ends the recorded flight
before landing: the `camera_gradients` route checks rendering, without
mission-completion credit. Small fixture-only matched checks cost little:
`matched_scene_walk` 2.3 s, `matched_start_brief_key_loop` 0.6 s,
`matched_compose_walk` 0.5 s, `matched_horizon_and_record` 7.1 s,
`matched_menu_input` 0.99 s (5,000 states per routine each).

## Other repeated processes

| Process | Trigger | Measured cost | Redundancy and status |
|---|---|---|---|
| Matched-routine lockstep (`func_lockstep --states 4000`) | every gate (three seeds); every new matched routine | single process 21 min for 653 routines beside a gate (9 Oct); 8 shards 3 min 11 s, the same report line for line | **Sharded since 9 Oct 2026** (`tools/func_lockstep_par.py`, `--shard K/N`), with the owner's approval: each routine draws from its own stream (the seed, its module and address), so a shard tests what one run does and a routine's states no longer move when others are added. The states are the same in number and kind, not the old ones. Seeds 0x5EED0F117A, 0xC0FFEE and one from the gated commit's hash. Left: tracking writes instead of copying 1 MB a state |
| Instruction lockstep (`insn_lockstep --states 64`, also `--timing386`) | every gate | runs beside coverage and parity, no wait tail; 5,718,848 states per profile | the 386 check also compares prefetch and held REP state; a second-seed run with `--base-seg 0x9FFF` checks code in VRAM |
| 386 flight engine parity (`stick_response --machine machine386 --engine interp/recomp`) | after a profile or translated timing change | 80 s interpreter, 60 s recomp, beside the gate (8 Oct) | all 574 memory/register/output/scanout checkpoints and stick responses identical; separate processes because the runtime is global |
| MSVC matched-code compilation | every matched-code build | about 30 s for the file with the workaround; the PLAYER decompressor stalled more than 18 min at /O2 (8 Oct) | only `player_unpack` disables MSVC optimisation. Stop an orphan compiler only after checking its source/output paths; otherwise its object stays locked |
| Worktree build and generated code | per worktree | 8 min first, then incremental | give each tree its own TEMP; never share a generated directory between concurrent builds |
| Route recording (adaptive pilots) | after a timing change moves a flight | 10-20 min per flight; try several parameters in parallel | recorded inputs replace them in the gate; a timing change invalidates all of them at once (see `airair_type5-8`, `strike`, `cargo`, `recon`) |
| Targeted lockstep for one VGAME routine with returning driver thunks | before the full gate | 0.5-1.2 s per 4,000-state seed (weapon-lock marker `B171`, canopy `D6DD`, projectiles `4777`; 9 Oct) | enabling thunk substitution changes test inputs to reach drawing branches (marker: 358 to 4,857 comparisons); it is not an identical-input optimisation. Full routes keep the actual driver |
| Staged combat regressions (`d1ttl_check.py`, `d1prox_check.py`, `d1physics_check.py`, `d1slot_check.py`) | after changing that option or its dispatch | 76 s (TTL), 114 s (PROX), about 2 min (physics, slot) for four parallel boots (10 Oct) | both engines with and without the option; compare case hashes across engines and unchanged control hashes (PROX: S=8/9/15, near/far geometry, FFFF suppression; cases for the others in [bugs.md](bugs.md)). Explicitly staged, not natural combat |
| Real-time simulation regression (`d1real_check.py`) | after changing D1REAL or dispatch | 3.8 min for six parallel boots (10 Oct) | 9/20/40 MIPS in both engines, normal inputs only; normal clock, 2x compression, restored rate, pause/resume, quit/cancel |
| Staged seeker-underflow reproduction | next countdown regression | about 120 s for two parallel boots (10 Oct) | private `slot_underflow_probe.py`, interpreter/recomp, S=8/15; the off-axis setup reproduces the defect (exact-rear did not) |
| Raw missile-slot trace | investigate seeker lifetime underflow | 219 s for a parallel 9/20 MIPS pair (10 Oct) | private wrapper around the current pilot; affinity 0x0F000000 beside the gate's 0x00FFFFFF leaves four desktop CPUs. Slot occupancy is not a count of blocked launches |
| Combat cohorts (`threat_profile.py` telemetry v5, `threat_compare.py`) | CPU-speed/combat investigation, after changing pilot, observer or an option | 21.9 min for 96 flights at 12 workers; 12.6 min for 24 flights at eight; held-out 9.9 min for 24; slot-repair 10.4 min for 72 at 12; swept-band 23.7 min at eight beside parity; real-time airborne 4.4 min for 24 at eight (10 Oct) | old cohorts (before v5 fixed life/burst sampling, startup seconds and orbit geometry) cannot be reused for combat conclusions. Reuse an unchanged arm only after a flight-hash check; validate mission identity; keep early exits; bootstrap paired differences. START seed matching does not align VGAME's later BIOS-tick RNG seed: align the final hangar input separately and sample at completed simulation steps. Raw results outside the repo |
| Speed and D1 sweep (`speed_sweep.py`) | evidence for the default-speed decision | about 12 min for 90 flights, 16 workers (9 Oct) | each route's unfixed 9 MIPS baseline runs first to anchor input times; results resume by tag in `runs.jsonl`; compare engagements only on matching missions |
| Isolated 15 FPS cap experiment | does a cap alone keep S at 15 and the mission clock real | about 18 min for 45 flights beside a gate; isolated generated build about 3 min (9 Oct) | one change in a detached checkout (D1_FPS_X10 116 to 150); `D:/f117-gate/speed15/experiment.json`; no default change |
| Control-response sweep (`stick_response.py --ips N [--fix D1]`) | compare controls before choosing a default speed | roughly 43-220 s per flight under load (9 Oct) | repeat the same 30 timed taps; keep mission identifiers, raw samples and early stops so incomplete or differing flights cannot pass as equal |
| START random-seed probe | find boot clocks that generate the same mission across speeds | five front-end boots; wall time not recorded | read START's RNG at DS:AE8C and the BIOS tick count; change only the boot clock, never guest memory (`D:/f117-gate/speedsweep/probe_start_seed.py`) |

## Reference captures

| Process | Trigger | Measured cost | Redundancy and status |
|---|---|---|---|
| PC-parity run (`pc_parity.py`: pictures, music, saved roster on three references) | before a release; after a machine-model change | about 2 min wall expected (96 s for DOSBox-X and 86Box together); 12 min before 7 Oct, when 86Box's runs went one after another | the three references run at once, and 86Box's three runs (picture capture, music and timing, saved roster) run at once too: each stops only its own 86Box (by profile); the traced ones open no VNC port and run on emulated time; ours is replayed per reference. Fast-forward: DOSBox-X's picture capture 11 s instead of 130 s with the same report (fast-forward captures byte-identical; real-time ones differ, since AUTOTYPE and pacing ran on host time); 86Box save run 95 s instead of 277 s, music 49 s instead of 178 s (roster and AdLib write order identical), picture capture 48 s instead of about 3 min. DOSBox-X's save run in fast-forward failed under load (scripted clicks missed, 61 bytes of ROSTER.FIL off), so it runs at DOSBox-X's own speed, 4 min, and ends when the roster's content changes; the picture capture waits for its VNC port |
| Held-picture hashing in PC parity | each reference comparison | about one second on the saved capture | decodes only stationary scenes instead of every changing frame; eight regression tests take 0.13 s |
| DOSBox-X closed-loop flight (`dosbox_cargo_pilot.py`) | per pilot change | 107 s wall in fast-forward (12 min paced); the same pilot on our machine about 1.5 min | fast-forward (`[cpu] turbo=true`) is the default: `result.json` and `flight.csv` identical to a real-time run. Run it beside the machine's flight |
| 86Box closed-loop flight (`b86_cargo_pilot.py`) | per pilot change | about 200 s wall in fast-forward (`B86_FAST`) | deterministic (same `flight.csv` twice), so one flight is a verdict |
| Machine-behaviour probe (`fidelity_all.py`) | after a machine-model change | about 2 min with a saved 86Box sheet; `--86box-run` adds 4 min | the 86Box sheet changes only with the model, so it is reused |
| 86Box boot attempts | per BIOS or config change | 2-5 min per capture series | `hello_floppy.py` answers "does the BIOS boot" in one run first |
| Silent 86Box speech waveform (`tools/ref86box/86box-audio.patch`, `B86_AUDIO=PREFIX`) | speech/sound comparison | incremental 86Box build 23 s; two intro runs together 28 s; takeoff-call flight 94 s; our interpreter replay 50 s; WAV render 1.6 s; comparison about 0.6 s (9 Oct) | records the mixer buffers before host playback: signed 32-bit little-endian stereo `PREFIX.sound.pcm` at 48,000 Hz and `PREFIX.music.pcm` at 49,716 Hz, offscreen video, null host audio. First require identical frame and port traces with capture off and on. Reuse captures while the binary, input and configuration hashes agree. Generated audio and game data stay outside the repo |

## What the gate is checked against

The gate compares the recompiled engine with this project's own interpreter,
so a model error shows in neither. Fidelity to a real PC is checked
separately, against recorded references: GOG's DOSBox 0.74
(`tools/fidelity.py`, saved video and flight captures), DOSBox-X for some
timing traces, and 86Box as the real-hardware-style reference once it runs
the game. Those are recorded or run per question, not per commit.
