# Repeated processes

Every process this project runs more than once, what it costs, what it
duplicates and what has been done about it. Update this page whenever a
process is added, measured or changed; a new repeated step goes here first.
Replace a figure when a newer measurement supersedes it: this is a ledger of
current costs and practice, not a log (the git history keeps the old ones, and
results live in the docs and `D:/f117-gate/` evidence directories they name).
Timings are wall clock on the development machine (32 threads) and say
whether they were measured or estimated. A timing taken while other heavy work
shared the machine is a ceiling, not a speed comparison.

The rule behind the page: an optimisation must leave every check's inputs,
outputs and verdicts identical, and the proof is the same verdicts before and
after. A change that alters what a check tests (different states, a skipped
verification) is not an optimisation; it needs the owner's approval.

## Current practice by kind of change

- **Original-game bug fix.** Check the faulty and corrected decisions with
  the switch off and on in both engines, then check defaults and replay
  selection before the two whole-table seeds, full gate and presentation
  checks. Q7's staged native check (`build/check_q7 path/to/VGAME.EXE`,
  generated build required) covers 56 runs plus 149 event limits in about
  five seconds; Q11's model-placement fixture covers 108 runs, eight branch
  preservation checks and 358 paired event limits in 19 seconds. The defaults/replay suite takes about six seconds. These
  focused checks supplement the gate. Owner delegated gate frequency on 25 Oct,
  conditional on unchanged P2 accuracy: after Q11, test remaining fixes in order
  within one batch, then run both whole-table seeds and the unchanged full gate,
  defaults/replay and presentation on the final combined source before commit/merge/push. Evidence: `D:/f117-gate/q7-20261025/`, `q11-20261025/`.
- **Progress scoring and census classification.** Run `tests/test_census_classify.py`,
  `tests/test_progress.py` and `tests/test_encoded_code.py` with the installed Python
  interpreter, then print the scoreboard and inspect the saved byte counts. The 80 focused
  tests take one to two seconds (79 took 1.82 s on 24 Oct with whole-table pre-checks
  sharing the machine); the full census refresh takes about ten seconds. The real-image
  refresh validates the reviewed singleton-jump identities and templates, the reference
  range constraints and the encoded SETUP rows; the ordinary rejections must still fail
  (zero captures, wrong file or CS:IP, changed opcodes, unreviewed singletons). No game
  runtime change means no matched-code gate is needed. During a matched batch, defer the
  published byte refresh until the batch's new named coverage passes. Scoring rules are in
  [progress.md](progress.md).
- **Static-code scope review.** Read each candidate range in full, decode every
  instruction independently with Capstone, then let `reviewed_code_bytes`
  verify image hashes, complete instruction boundaries, counts and
  non-overlap before `docs/census-code.tsv` is written (all-or-nothing). Defer
  ranges whose discovery lists overlap, even when isolated lengths agree
  (logo mouse helpers; SETUP `1E4F`). Refresh with the existing census file,
  the latest named coverage and batch-h route coverage. No gate is rerun for a
  scope-table-only change; the classifier, progress and dossier tests are
  enough. Findings: [static-code-review.md](static-code-review.md).
- **Module scope refresh.** Decode and validate the existing route captures against all 17
  game code files, then refresh progress with the current named proofs. The 21 Oct measurement
  took 2.8 s; the complete score refresh took about 9 s. Scope-only tooling changes use the
  progress, classifier and dossier regressions plus the normal gate CTests
  (`ctest --test-dir build -E func_lockstep --output-on-failure -j 8`). The standalone
  serial `func_lockstep` CTest duplicates the gate's separate sharded check; use
  `tools/func_lockstep_par.py --jobs 8` for that comparison. Evidence:
  `D:/f117-gate/modules-scope-20261021/`.
- **Matched routine batch (Phase 2).** Private proofs per routine in
  `D:/f117-gate/p2-*` ([below](#phase-2-proofs-matched-routines)), then one acceptance
  directory per batch, `p2-batch-<letter>-accept/`, made by the accept kit
  (`D:/f117-gate/accept-kit/`, 18 October): `new_batch.py LETTER PREV BASE_COMMIT DIR...`
  writes generic scripts (`accept.py`, `overlap_accept.py`, `optimized.py`,
  `audit_private.py`, `finalize.py`) that read tests, routine totals, mutations and added
  bytes from the gate log, `matched.c` and the verdicts, so nothing is hand-edited
  (far-segment routines use linear addresses; `callfar` kinds and call macros are
  recognised). Run `audit_private.py` right after `new_batch.py`, before the gate.
  `accept.py` runs the gate, the presentation checks, the optimized seeds, the baseline
  group verdict and the credit audit. `finish_all.py DIR[:ROUTE]...` runs each proof
  directory's checkpoints, boot proof and verdict side by side on spare CPUs.
  `optimized.py` passes `--only` with unpadded hex (a padded address once selected zero
  states) and requires positive comparison counts; `finalize.py` validates old coverage
  against the previous batch's own registration table and every new byte against the
  dossiers. A batch's new optimized CTests take two to three seconds (batches at to ax:
  1.82-2.80 s); the census refresh afterwards takes seconds.
- **The presentation overlaps the gate only with a keyed baseline.** With a frozen
  per-route baseline whose interpreter key matches, the N=1 presentation runs beside the
  gate's interpreter tail (batch al: acceptance 1,549 s, N=1 718 s started 793 s into the
  gate); without one (after a route or non-matched `src/` change, or a new CMake target)
  it runs after the gate (batch ai: 1,658 s in all). `new_batch.py` picks the newest
  baseline whose `base_key` equals `build_recomp.interpreter_key` and prints which
  (batches ay to bc copied the previous batch's stale baseline and all fell back to
  sequential). Check the gate's log for "Started" lines and no fallback message. The one
  measured pair shows no saving: bc sequential, acceptance 1,741 s (coverage 766 s,
  parity 343 s, N=1 420 s alone after the gate); bd overlapped, 1,822 s (coverage 635 s,
  parity 879 s, N=1 916 s beside it). One sample each, with different code.
- **Pre-checks before a batch's gate.** Two cheap checks catch most failures: every new
  route once with `--engine recomp`, looking for `[matched] OVERRUN`, and the whole matched
  table at six seeds (4,000 states, eight shards each): the commit-keyed seed,
  `0x27182818`, `0x20261022`, `0x31415926`, `0x1BADB002` and `0x600DF00D`.
  `D:/f117-gate/precheck_parallel.py LOGDIR SEED...` runs three seeds at once (8 workers
  each, 24 of the 32 threads, BelowNormal): batch at's three seeds took 11 minutes against
  18 in series, with identical tallies and per-routine lines. Two rounds of three took
  1,407.72 s for batch ax (`D:/f117-gate/ax-pre/`) and 1,530 s for the load-base
  infrastructure. One commit-keyed seed alone through `tools/func_lockstep_par.py --jobs 8`
  took about 8 minutes (batch as). Every older per-routine report must equal the previous
  batch's at the common seeds, and any change must be explained (batch ax: 1,266 identical,
  two explained). New entries also pass six extra private seeds beyond the kit's five.
  Found this way: a latent clock undercount on VGAME `73C8`'s target-zero path (the
  commit-keyed seed; the body now charges 6, 13 and 6 instructions where it charged 5, 10
  and 6), and an ISOUND.LOG `0253` fixture that reached 301 of 303 instructions with no
  compared state differing (`0x600DF00D`, batch bc).
- **Repairing a fixture that misses instructions.** Keep every existing random draw and
  add the new inputs after them in a fixed share of states, so earlier states are
  unchanged (ISOUND.LOG `0253`: a share of states forces each byte command preset). Reprove
  the routine in its own directory (all private seeds, the original mutation controls, the
  six production seeds at full coverage, its real route), then rerun the six whole-table
  seeds and require every unaffected per-routine report to equal the failed attempt's.
  Evidence: `D:/f117-gate/p2-isound-log-0253-coverage-repair/` and its sibling audit.
- **Harness load-base changes.** A change to how the matched harness places images needs a
  whole-table zero-base control (4,000 states, eight shards) beside sweeps of the affected
  entries at 0400h and 0800h (73 entries; about 610 s together), then the six pre-check
  seeds and the full gate, adding no credit.
  `D:/f117-gate/modules-loadbase-20261022/run-infrastructure-acceptance.py` sequences these;
  `p2-batch-loadbase-accept/` pins every changed code, test and CI file and requires zero
  added credit. The 23 Oct run: core gate 1,369 s, then the presentation after it (N=1
  564 s; boot N=4/9 101 s in parallel), 1,955 s in all. Its new CMake target changed the
  interpreter key, so the old baseline was correctly unused; the closing audit must then
  verify the fresh sequential references, not the old baseline (it once checked the old
  one). Read stage timings for performance, not that verdict's wall time, which includes
  an idle interval before the audit repair.
- **Harness changes after an unexpected rotating-seed failure.** Reproduce on the
  pre-change tree first. An escape-guard or return-recovery repair must keep exact
  original-state evidence (clock, registers, segments, flags) at the recovered return,
  still compare the whole final state and grant no named-code credit; a one-tick negative
  control must still fail. If production source is unchanged, a rerun may reuse the
  freshly verified interpreter cache while tests and registration are the only amendment.
  A recovered state can change later fuzz inputs, because the one-in-four mid-run probe
  draws from the routine's random stream: prove the stream equal up to that point with
  fixed/control traces and keep every iteration. In batch ax SETUP's startup recovered two
  returns; the probe at state 509 changed inputs from state 510 and its comparison count
  went from 649 to 631, with all 4,000 iterations kept
  (`D:/f117-gate/ax-legacy-io-20261023/rng-review.json`).
- **Normal-play default changes.** `tests/test_fix_defaults.py` (ROM-free,
  dummy SDL video/audio, six synthetic COM session cases) must fail against the
  pre-change executable and pass after rebuilding the app and runner; run the
  pacing, input-log, config and playback-clock CTests and the Machine API
  regressions. A default selection does not change the simulation code, so it
  relies on the earlier full gates and validation of the same mode.
- **Cadence / capacity validation.** Paired interpreter vs recompiled runs
  with normal controls only, comparing inputs, timing rows, full telemetry,
  capacity windows and final hashes; clock/dialog/transition cases for
  compression; announced fullscreen display captures only after every hidden
  worker has finished. Results and limits:
  [cadence15-validation.md](cadence15-validation.md). Do not repeat the 922 s
  rerun-engine check for every new input fixture; check that a fixture really
  fires or steers (engine equality of an inactive fixture is not coverage);
  home-orbit attempts give combat, not compression, coverage.
- **Presentation-only changes** (`src/present`, observer, drawlist): the light
  check, or full CTests, every N=1 route and boot N=4/9 against the frozen
  interpreter baselines of the preceding full gate (about 1,450 s in all);
  changes to `drawfeed`/`rerun` need the full gate too
  ([Presentation checks](#presentation-checks)). Diagnostics that replay a frozen
  record (pixel-origin `DT_HUD_FINE` counts, before/after frames) are
  correctness checks, not pacing measurements. Dispatcher or observer
  micro-benchmarks never promote a default.

## Practical traps

- ROM-free tests that group the real route catalogue must provide a synthetic
  install: reading damaged-install route arguments stages their private copy.
  `tests/test_build_recomp.py` supplies one; all twelve checks pass with
  `F117_DATA` pointing at a missing install.
- **`matched.c` must compile with `/Os`.** From batch ab on, MSVC's `/O2` (which includes `/Ot`) took 90+ minutes
  to compile `src/matched/matched.c` (the first batch-ab gate took 108 minutes; later gates only looked fast because the
  object was reused). The slow functions are the big pre-existing VGAME routines (`vgame_draw_model_in_view`,
  `vgame_display_pass` at 125,787 instructions after inlining, ...), not the new ones; batch ab only tipped the optimiser
  over. Measured on 20 October 2026: `/O2 /Ob2` still running at 90 min, `/O2 /Os` 6.5 min, `/O1` 4.5 min, `/O2 /Ob0`
  10 s (the forced-inline CPU helpers become calls), `/O1 /Ot` slow. `CMakeLists.txt` sets `/Os` on that one source file;
  `player_unpack` alone also turns optimisation off (the PLAYER decompressor stalled the compiler more than 18 minutes,
  8 Oct). Find the slow functions of a compile with `cl ... /O1 /d2cgsummary` ("Anomalistic Compile Times"). A stuck
  `cl.exe` is one core at 100%: check CPU time before concluding it is idle, and never run more than two or three of these
  at once (3-4 GB each). Stop an orphan compiler only after checking its source and output paths; otherwise its object
  stays locked.
- PowerShell-redirected pacer logs are UTF-16: decode them explicitly before
  looking for a success line. Restoring a source by copying can keep its old
  timestamp and fool Ninja into reusing an experimental object: touch it and
  confirm a recompile in the build log.
- Offline DbgHelp symbol resolution needs the executable's preferred
  0x140000000 base and `SymSetOptions(0)`.
- The original-reference harness must accept actual stops beyond a requested
  checkpoint and compare those stops (the boot/quit scenario has 54
  checkpoints). Settled windows alone can miss a clock-boundary error: also
  integrate observed compression mode over time across rapid transitions.
- A failed or stopped run is retained as evidence in its own directory and is
  never an acceptance verdict; restart in a fresh directory. Keep failed display
  captures and invalid fixtures immutable and labelled.
- **Bisecting a route divergence between engines.** Narrow the clock window
  with `f117run --hash-every N` and `--hash-from CLOCK`, then run `--trace
  FROM:TO:FILE` over the first differing interval on both engines and diff the
  traces: the first differing line names the instruction. Run route commands
  as an argument list, never through a shell `eval`: one turned the `\r` keys
  of a route into `r`, both engines then agreed on a run that never reached
  the mission, and the divergence looked as if it did not reproduce.
- The synced working folder can lock a file mid-write. Integrating many
  proofs at once therefore restores and retries each routine on its own when a
  write fails, rather than abandoning or half-applying the batch.

## The gate (`tools/build_recomp.py`)

Run before every commit that changes code; read its tallies, not just the
exit code. It translates and builds (always with `F117R_BUILD_APP=ON`, so a
previous core-only build cannot silently omit the host tests), runs the unit
tests, records coverage over every route, translates and rebuilds if coverage
found new code, then runs every route in the recompiled engine and in the
interpreter and requires identical checkpoints and final hashes ("parity").
Beside those it runs the instruction lockstep (both timing profiles, steps 6
and 6b) and the matched-routine lockstep (three seeds, steps 7-7c), then
checks that every routes-only matched routine ran on a route (step 8) and that
no matched routine ran past its event limit (step 9). What each check
establishes is in [architecture.md](architecture.md#verification-why-11-is-a-claim-with-evidence).

**Fix selection.** Normal application play starts with all thirty fixes (Q4, Q11, Q12, Q13a, Q13b, Q14-Q22
added 25 October); the headless runner starts with none, and `build_recomp.py` and
`run_route.py` prepend `--fix none`, so ordinary parity checks original
behaviour and fix-specific route options or recorded headers add the fixes
they test. Do not enable every fix for ordinary parity: original and repaired
behaviour each need their own checks. The defaults are checked by
`tests/test_fix_defaults.py --app APP --runner RUNNER` (ROM-free, also in
Windows CI); by private recording/replay checks of all, none, a recorded subset
and explicit replay additions (old logs retain none); by a combined-default
clock check that reuses `d1real_check.check` with all fifteen fixes at 20 MIPS
in both engines (seven normal/compressed/pause/dialog cases, every case hash
equal, 69.17 s; separate processes, since the Machine API allows one open
machine per process); and by a hidden recorded-session app check (165.69 s:
both presentation modes keep 2,238 exact logic frames and equal
presented/scanned palette counts; rerun draws 7,676 intermediate pictures).
Both timings shared the machine with a gate. Evidence:
`D:/f117-gate/default-fixes-20261017/`.

The standing command (the handoff has the local paths) is `py
tools/build_recomp.py --data INSTALL --seed-coverage WORK/coverage --jobs 16
--verify-interp-cache`, started through `D:/f117-gate/affinity_launch.py`
(below).

**Current figures (25 October 2026, Q11):** 630 CTests; 63 routes identical, run
as 62 sessions, every interpreter session verified fresh; 5,723,136 instruction states under each
timing profile; 1,387 matched routines at three seeds; reachability and event limits clean.
Core gate 1,454 s including a full 766 s coverage pass; acceptance 1,971 s with presentation
after the gate (N=1 516 s; boot N=4/9 89 s). Evidence: `D:/f117-gate/q11-20261025/`.
For the overlap comparison, batch bd's core gate took 1,692 s including a full 635 s coverage pass;
acceptance took 1,822 s with a valid keyed baseline. Batch bc, before the baseline fix: core gate 1,304 s, acceptance
1,741 s with the presentation after the gate (no saving from the overlap, [above](#current-practice-by-kind-of-change)).
Evidence: `D:/f117-gate/p2-batch-bd-accept/`. Typical core gates take about 1,300-1,800 s; the
longest career seed chain sets the route time.

| Stage | Latest measured | What sets it, what was duplicated | Status |
|---|---|---|---|
| 1-2 translate, build | 87-113 s incremental (15 Oct); 7-8 min in a fresh work directory; about 6 min after a CPU-header edit | a fresh work directory builds everything; a CPU-header edit rebuilds all generated C and both matched objects | **fixed by practice**: reuse the previous run's work directory (`sync_tree` rewrites only files whose bytes changed, so the build recompiles `matched.c` and relinks); the output is a function of the sources either way. Generated regions are capped at 1,024 instructions (a 6,446-instruction VGAME function had dominated MSVC: 479 s before, 120 s after; both timing profiles keep their instruction verdicts) |
| unit tests | 22.47 s for 75 tests at eight workers, against 159.76 s serially (14 Oct) | ran serially before the route workers | **fixed**: up to eight workers (`--unit-jobs N`; `--unit-jobs 1` is serial). Same executable, inputs, selection, affinity and priority: every test's captured output identical to the serial run. Proof: `D:/f117-gate/leaf-batch/unit-parallel-verdict.json`. The fresh interpreter sessions now start right after the build, before the unit tests, which run concurrently (a failure still fails the gate) |
| 3 coverage | 711-815 s (15 Oct), or skipped | runs every route once only to find untranslated code; as long as its longest job (`career_promotion`, after its `career_serge` seed) | **skipped when provably empty**: a record (`~/f117-recomp-local/coverage-verified.json`, `--coverage-record`) holds the key of a pass that added nothing - translator, sources other than `matched.c`, game files, routes - and the digest of the generated code it ended on; the same key and digest skip the pass. A matched routine only replaces original instructions that already ran, so it cannot add code to cover; a wrong one fails parity or lockstep. Verified: full verdicts identical with and without the skip. The key hashes `CMakeLists.txt` without `add_test` entries, whole-line comments and blank lines (`build_digest`), so adding a test does not rerun coverage. A commit adding a matched entry still reruns it (the translator isolates each matched entry) |
| 4 translate, build again | 35-41 s; about 220 s when coverage added code | regenerated and rebuilt everything though coverage usually adds nothing | **fixed**: only files whose bytes changed are rewritten; the rebuild is skipped when none did; `--seed-coverage DIR` starts from an earlier run's coverage. A rebuild restarts the locksteps, so their earlier results do not stand for the final build |
| 5 parity | 345 s after overlap and reuse; 1,223 s with coverage skipped | as long as `career_promotion`'s interpreter run, which first replays its whole seed chain | **fixed**: longest jobs first; identical sessions run once (found by content, not name: `recon_career` and `recon_return`), with every route's milestones checked on the shared run. **Overlap**: `--overlap-jobs N` (default 8; 0 = off) starts the interpreter runs during coverage from a private copy of the first build's runner and libraries (`snapshot_runner`); parity then runs only the recompiled half and collects them. **Coverage-run reuse**: when coverage adds no translated code the first build stands, so the coverage runs (made with the parity checkpoints) are the recompiled half; `--check-coverage-reuse` reruns them and requires equality (proved 15 Oct: all 44 equal, every verdict identical), `--no-reuse-coverage-runs` turns reuse off. Measured 15 Oct: 1,706 s before (coverage 711 + parity 848), 1,556 s with the overlap. The gate now ends when `career_promotion`'s interpreter chain does, so reuse saves CPU (about 513 s of recompiled reruns) rather than wall time |
| 5b interpreter half of parity | every session (62) runs fresh in the standing command | an interpreter run is a pure function of the game files, the route and the interpreter's own code | **cached** (`--interp-cache DIR`): key over `src/` except `matched.c`, `CMakeLists.txt` (as above), the game files and the route (text, replay files, seed chain); entries expire after 14 days. Measured: parity 522 s to 174 s with 31 of 31 reused, every verdict identical. The standing command passes `--verify-interp-cache`, which reruns every session and requires equality, so "0 reused, 62 run" is expected and the cache saves nothing there |
| 6-7 locksteps | add 0 s | waited for parity though they need only the build | **fixed**: start right after the first build, beside coverage and parity; output to files in the work directory (`*_lockstep.log`). Measured 7 Oct with coverage forced: 778 s to 572 s, the lockstep tail 241 s to 0, every route line and both tallies identical |

Before these changes (6 Oct) a gate took 30-38 minutes; the first round
alone cut the same code's gate from 1,895 s to 1,365 s with every route's
clock, hash and verdict and both lockstep tallies identical. The overlapped
N=1 presentation also starts speculatively when coverage ends rather than
after the re-translation check (the closing runner-hash comparison turns a
wrong guess into a failed acceptance, never a pass); with the early interpreter
start this was expected to save roughly 100-170 s of ~1,700 s; the one
measured pair (bc against bd, above) showed none. The remaining
floor is the interpreter chain: about 337 s of build and tests, plus the
1,285 s career run.

**Translation preparation (20 October).** Discovery and emission run one
process per module ([architecture.md](architecture.md#the-recompiler)), and
each module's relocation mask is built once during coverage loading instead of
for every captured instruction. The serial and parallel generators produced
identical sets of 58 files, byte for byte, with and without the coverage
corpus: 51.23 s became 5.20 s with coverage; without it, 2.05 s and 1.99 s.
These were sequential runs on four assigned CPUs beside private proof work, so
they establish output equality and an observed saving, not a controlled gate
timing. Evidence: `D:/f117-gate/translate-parallel-20261020/verdict.json`.
Windows CI also compares spawned workers with serial translation on synthetic
modules (relocation, coverage, isolation, mutations) and requires a worker
failure to propagate before a module list is published.

**Overlap worker count (15 Oct).** A batch tried 16 interpreter workers beside
coverage: every one of the 44 sessions then still ran fresh, all 45 route
results were identical, and every other check remained. It took 1,718 s
against 1,553 s for the previous eight-worker batch, with different
source/build work; after the build, 1,329 against 1,257 s. Not a controlled
speed gain, so the default stays at eight (the owner declined changing it).

**Adaptive interpreter pool (16 Oct, private prototype, not integrated).**
`D:/f117-gate/p2-batch-e-accept/adaptive_pool.py` keeps eight interpreter jobs
running during coverage, then permits sixteen; its scheduling tests pass
(`adaptive-pool-verdict.json`). At batch e's interpreter-only tail the
eight-worker pool used 26% of the CPU, but queue analysis
(`schedule-analysis.json`: 1,346 s observed interpreter wall, 1,285 s longest
job) puts the saving at only about 61 s with job durations held fixed,
ignoring changed contention.

**Presentation beside the interpreter tail (16 Oct, adopted batch practice).**
`overlap_accept.py` (first in `D:/f117-gate/p2-batch-f-accept/`) freezes
existing baselines and validates them against the current interpreter/route
cache keys, full checkpoints and final hashes and every route milestone. After
the gate's final build settles, N=1 uses a frozen runner and baselines on 16
workers beside the eight fresh interpreter workers, under the 24-CPU affinity;
boot N=4/9 starts after the gate. Before accepting, every fresh interpreter
session must match the frozen baselines at every checkpoint, final state and
milestone, and every frozen file and runner hash must still match. Five
deliberate faults are rejected (wrong final, wrong checkpoints, missing final,
changed frozen file, stale result claimed as fresh): `overlap-guards-verdict.json`,
`fresh-baseline-verdict.json`, `overlap-verdict.json`. Each route keeps its own
key within a shared session (20 October): `recon_career` and `recon_return`
run the same inputs with different milestone comments, so comparing both to
the lead route's key had rejected a fresh baseline. The wrapper validates
every route's key and the exact session grouping; eight guard checks cover
changed alias milestones, wrong lead/alias keys, changed grouping, expired
references and changed files or interpreter inputs
(`D:/f117-gate/p2-batch-af-accept/baseline-key-guards.json`). When the gate
validates and reuses its coverage record, the presentation starts after the
unit tests instead of waiting for a coverage completion line that will not be
printed. Sequential checking is the fallback when no valid keyed baseline
exists; the gate's worker counts and fresh interpreter verification are
unchanged.

**What sets the wall time.** `parallel()` prints each stage's wall time,
work, lower bound (the larger of the longest job and work / workers) and its
four longest jobs with their start and end offsets; read that before changing
worker counts. Measured 14 Oct (`--parity-only --verify-interp-cache`): 88
jobs, 16,736 s of work on 16 workers (1,046 s if perfectly packed), stage
1,160 s, longest job 1,159 s (`career_promotion/interp`). A seeded route's job
first replays its whole seed chain (`prepare_roster` in `run_route.py`):
`career_serge` (790 s as its own job) plus its own run in sequence. The chain,
not the worker count, sets the end; reusing the seed's roster would save work,
not chain length. Dropping `--verify-interp-cache` or splitting the chain into
checkpoint segments would weaken a verification rather than speed up the same
one; the owner declined both (15 October).

**Workers, affinity and priority.** 16 route workers (`--jobs 16`; the
default would be 24 on this machine). A 24-worker run (12 Oct, coverage
forced, every interpreter session fresh) gave identical verdicts and no route
speedup: coverage and parity together were 27 s slower, and the apparent
saving was build time. Proof: `D:/f117-gate/jobs24-proof/comparison.json`.
The gate runs under affinity mask `0x00FFFFFF` (24 of 32 logical CPUs) and
`BELOW_NORMAL_PRIORITY_CLASS`, inherited by every child, leaving eight CPUs
for the desktop; three eight-shard matched checks plus 16 route workers had
oversubscribed the machine. `D:/f117-gate/affinity_launch.py` sets and reads
back both (a ctypes wrapper must use pointer-width signatures and check the
result; an earlier one failed silently). For actual CPU load read Processor %
Processor Time (about 52% during parity); WMI LoadPercentage reported 96% at
the same moment. More balanced lockstep chunks and a shared CPU budget for
route and lockstep workers remain experiments to benchmark first.

**Traps.**
- The third matched seed comes from the gated commit's hash, so it rotates:
  it can fail a commit that did not touch the failing routine. Reproduce with
  `func_lockstep --seed S --only MODULE.EXE:IP` on a build without the new
  body, or `--shard K/8` to find the routine. The IP is unpadded hexadecimal
  (`D0C`, not `0D0C`); a wrong filter silently compares nothing, so read the
  per-routine comparison counts. Each failing seed is retained as a CTest.
- A matched body at an address shared with an observer hook fails unit test
  `code_overrides` (before any route work): such a body must call the
  observer itself.
- `build.cmd` logs inside the checkout's own build directory (a shared
  `%TEMP%` log once let a build in another worktree print BUILD OK for a build
  that had not linked). Check the executable's time when other checkouts are
  building, and the success marker and compiler log as well as the exit code:
  a failed Windows build has returned zero leaving an old executable.
- Command printing is serialized (it once mixed data into the combined
  console log); per-route results are separate files.
- Captures and gate builds must not overlap a running executable they rebuild:
  the running Windows executable locks build outputs.
- The GitHub Windows wall-clock pacing check is timing-sensitive: one failure
  (`7df74bf`, 0.109 s added against the required 0.45-1.2 s) passed on rerun.

## Presentation checks

**After a presentation-code change**, all headless:
- `ctest -E func_lockstep -j 8`: the gate's unit-test set (625 CTests at batch
  bd; 111 took 36.56 s on 15 Oct).
- `tools/presentation_check.py --data DIR --baseline-runs DIR/runs --out DIR
  --scales 1` over every route: 718 s for 63 routes at 16 workers beside a gate
  (batch al), 420 s alone after a gate (batch bc). It keeps every logic frame
  and interpolation pair exact (260,613 / 260,527 in batch al) and every guest
  checkpoint and final hash.
  Options: `--routes STEM...`, `--scales N...` (default 4 and 9), `--jobs N`
  (default 4), `--mode interp|rerun`. Run the full gate first for fresh
  baselines. It requires matching guest checkpoints, final hashes and route
  milestones, every logic frame and interpolation endpoint exact, no unseeded
  or dropped frames, and presentation pairs on routes that enter flight (a
  short SETUP-only route may have no 50M-clock checkpoint and no flight
  frames). These concurrent runs check correctness, not refresh-rate
  performance.
- `boot_to_flight` at `--scales 4 9`: about 56 s at two jobs (75-101 s in
  recent acceptances), 368 frames / 366 pairs each.
- `--mode rerun` (for `--present rerun`) adds to every check above: the
  unchanged machine copy draws its frame exactly at t = 0, real
  memory/machine/fix state is byte-identical around every copy, no DOS
  operation is blocked, and flight routes actually draw intermediate pictures
  on copies (an all-fallback run cannot pass). Console-status and
  emulated-clock reads use the copy; file operations are blocked. Measured
  15 Oct: the 44 N=1 routes other than `d1_fast_machine` 1,696.92 s at 16
  jobs; `d1_fast_machine` 352.03 s and boot N=4/9 185.44 s, all sharing a
  gate's load; 259,240 frames / 259,166 pairs and every guest hash exact,
  1,638,579 isolated copies in all. Keep `d1_fast_machine` in this pass: it
  caught fix state shared between copies (now saved with each snapshot and
  restored after each copy). The ROM-free CTest
  `presentation_rerun_isolation` checks private memory and devices, silent
  hooks, the restored observer and direct/chained DOS rejection in both
  timing profiles (0.02 s). Threaded app smoke test (offscreen, silent,
  under load): 143.36 s, exit 0, 1,610 exact logic frames, 4,933 original
  in-between draws, 3 fallbacks. Evidence: `D:/f117-gate/present-v2/rerun-accept`.
- The retained windows: 96 replay/refinement/interpolation checks across 16
  windows. Run `build/test_drawlist`, `test_hires` and `test_interp` with
  absolute paths: the relative `build/` junction fails under Python's
  `subprocess`.

**The light check** (`tools/present_light_check.py --base REV --data DIR
--out DIR`, owner-approved 15 Oct; needs `SDL_VIDEODRIVER=dummy`) replaces the
full gate for presentation-only changes. It refuses unless every file changed
since REV (the commit the last full gate passed on) is under `src/present/`
and not included from guest-side code (it finds those headers itself), a
presentation test or analysis tool, or documentation. `drawfeed.c/.h` and
`rerun.c/.h` are refused despite their directory: they snapshot or execute
machines, so changes there need the full gate plus the all-route `--mode
rerun` check. For eligible changes it builds, runs every CTest, the N=1 check
over every route against the last gate's baseline runs, and boot at N=4/9. A
full gate is still needed before the next Phase 2 `kit.py checkpoints`
([below](#phase-2-proofs-matched-routines)).

*App-only changes* (owner-approved 15 Oct): `src/host/main.c` and the example
ini files are built or copied only for `f117a.exe`, which neither the gate nor
the route checks ever run, so the gate cannot check them. The light check
accepts them with `--app-session DIR --app-exit-after CLOCK` (a recorded
session: `input.log` and `save-start/`): it builds, runs every CTest and
replays the session through the app headless in `interp` and `rerun` at
scale 3 (about 8 minutes), requiring every logic frame exact, in-between
pictures drawn by the original, and every scanned-out palette presented
(within the first and last frame). Break-tested: the frozen-palette bug
presented 186 palettes against 209 scanned out and fails; the fix presents
all of them. The route checks run only when presentation code changed too.

**Rules that keep these checks valid.**
- Freeze the runner outside the build and set `F117R_RUN_EXE` to its absolute
  path before an all-route check. Rebuilding it during a run invalidates the
  result even if every route passes; the checker verifies the runner hash at
  the end. Freeze baseline logs and latest saves before a gate can overwrite
  them.
- A capture file with no records fails the checker (seven empty HUD captures
  once passed as success). A diagnostic that counts a bad record must also
  fail: the interpolation provenance audit now treats unknown kinds, and more
  novel records than the generator counted, as fatal (it had returned success
  with 48 violations). Keep that audit separate from endpoint equality.
- Keep every automated check headless or hidden; ask the owner before any
  visible or fullscreen run.
- An owner replay uses a fresh copy of `save-start` and the input header's
  clock and time. Leave `F117R_OBSERVE_PAGES` unset for live presentation:
  forcing it reseeds every phase, prevents interpolation pairs and loses
  carried fine map terrain. Create the diagnostic output directory first; the
  writer does not. The owner's recorded flight: 69 guest checkpoints, 2,101
  logic frames / 2,099 pairs, final `31c259b992340414`.
- `tools/motion_check.py LOG` measures pairing completeness, deformation of
  paired batches and step-timing irregularity, which the exactness checks
  cannot see ([presentation.md](presentation.md)).

**Component regressions** (all ROM-free unless they need the owner's
generated modules or a private corpus; no original bytes are committed):

| Check | Cost | What it covers |
|---|---|---|
| `test_present_fills` | under a second | 2,619 staged cases against the original VGAME fill selector (style classes, clipping, row/column parity, odd page alignment); `--rows` feeds `tests/test_present_styles.py --original build/test_present_fills.exe` (2,187 rows agree) |
| `test_present_driver` | under a second | 3,953 staged cases against the original resident graphics driver (text, clips, fonts, sprites, scaled RLE, flips, tick scales) |
| `test_present_map`, strip and near-plane regressions | under a second together | orthographic triangle edges, rectangle round trips, cached ownership after a replay copy, stale-source fallback at N=1/4/9; no holes between near-clipped faces |
| flat-cell guard scalar regression | 0.22 s | 8/4-byte group comparison: complete output bytes and all counters at N=1..16, tails, boundaries, protected HUD cells |
| `test_world_lines`, `test_world_jitter LOG N` | - | coordinate/depth continuity, exact coarse replay, stale slots, stroke widths; jitter isolates interpolation rounding only (N=9 strike RMS: outlines 1.680 -> 0.0039, dots 2.756 -> 0.0140, negative model axes 0.496 -> 0.0634 fine px) |
| `test_band_state CORPUS_DIR` | - | 47,516 passes, 1,514,560 quads, 47,313 blend pairs exact (six discrete-state refusals); corpus via `F117R_OBSERVE_BANDS=1`, `F117R_OBSERVE_KINDS=PoUKQ`. `test_band_rules`, `test_band_observer` are ROM-free and check no guest/clock/bus change in both timing profiles |
| `test_hud_state D:/f117-gate/present-v2/hud/corpus` | 0.4 s over 288 passes | CTest `presentation_hud_state`, skipped without the corpus |
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
record IDs), record counts, guest checkpoints and final hashes; matching guest
hashes alone once missed a shared-address observer bypass that dropped G/E/F
records. Perspective V timestamps can differ by the RET instruction (the
matched body records after charging RET, the original hook before it): a
capture boundary, not changed guest time. Under the 386 profile both engines
run the original projection, so complete logs compare byte for byte.
Measured: boot capture 217,022 V / 300,637 G payloads identical (interpreter
52.0 s on four spare CPUs beside a gate); a staged 386 menu script to clock
8,000,000,000 gives a byte-identical 751,862,587-byte log. Evidence:
`D:/f117-gate/observer-original`.

## 4K refresh timing

Capture an isolated application replay with a fresh save directory:
`f117a --no-config --data DIR --save NEW-DIR --no-record --replay
tools/routes/strike.input --exit-after 3500000000 --present interp
--present-scale 9 --fullscreen --present-log FILE --log MACHINE-LOG`. The
replay sets its recorded 9 MIPS clock. Wall time is fixed by the guest clock
(the machine is paced to it): strike to 3.5B 389.8 s, air-to-air type 6 to
5.9B 656.4 s, landing to 10.0B 1,112 s. Compare the final guest hash with an
isolated headless run of the same input, fresh save and stop count; the app
writes its final hash to the machine log, not stdout.

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

Run with nothing else heavy going; runs beside the gate are exploratory, not
acceptance evidence, and a visible fullscreen run needs the owner's agreement
(one the owner stopped is not evidence). Results, including the 17 October
air-to-air failure and its 18 October repair, are in
[presentation.md](presentation.md). A private hidden 3840x2160 Direct3D 11
probe (all state/interpolation flags, five intermediates per pair) measured
strike / mode-2 / small windows at 119.47 / 127.45 / 150.10 fps uncapped,
regeneration/raster p99 below 5 ms, with occasional roughly 100 ms upload
stalls; uncapped numbers do not prove refresh pacing. Evidence:
`D:/f117-gate/present-v2/band-finish`, `D:/f117-gate/present-v2/hud2`.

## Phase 2 proofs (matched routines)

**Process.** Each proof uses its own
`D:/f117-gate/p2-*` directory, with `D:/f117-gate/kit/kit.py` (init, build,
lock, seeds, mutations, checkpoints, verdict, integrate; method and traps in
`D:/f117-gate/kit/BRIEF.md`). Finish the proof workers and verify every
verdict before starting the batch's gate; run no other heavy job during it
(owner, 20 October). `kit.py checkpoints` needs the interpreter cache entry for
the current `src/` key, so after any `src/` change run the gate before proof
checkpoints.

The private queue uses three slots, each with one compiler job; larger
routines start first, and a slot carries its routine through mutations, route
checkpoints, boot and verdict before taking another. Batch ax's final pass
took 1,240 s for 18 ready routines and one failed mutation list (that routine
passed after three equivalent edits were replaced with observable carry
defects); this includes repeated proof work, not the batch's whole
development cost. Fresh harness compiles take roughly 80-140 s per private
project, and three compiler processes are the limit. Proof workers can use
separate CPU sets at BelowNormal priority beside the gate, leaving desktop
CPUs free (two isolated lanes, `000FFF` and `FFF000`, refreshed two pending
proofs in 192.62 s on 17 Oct: optimized libraries, five seeds each, all 115
mutations, real-route and boot replays, source/coverage/mutation audits);
keep each worker's mutation builds serial when sharing the machine.

| Step | Cost | Note |
|---|---|---|
| private build loop | initial 9.9 s, edits 2.6-3.6 s | a direct candidate matched object linked before frozen generated/core libraries; no generated sources recompile. /Od is development evidence only (it avoided a very large optimizer peak); integration needs the production options, optimized seeds and a full gate. Library hashes kept with the private project |
| private generation in a worktree | 21.9-30.7 s | from frozen coverage, its own generated directory; build with two workers and its own TEMP, after main's matched shards finish |
| targeted seeds (`func_lockstep --only MODULE.EXE:IP --states 4000`) | 0.4-5 s per seed; 23.3 s for the 1,332-instruction mission generator | run every new body at four or five seeds: the default seed passed a body with a wrong resume point |
| mutations | 37 edits about 8 min serially, 90 s four at a time | identical CAUGHT results; the kit never edits `body.c` |
| isolated unit tests | 29.72-80.52 s (36-44 tests) | |
| real-route replay | 0.8 s (`setup_calibration`) to 190.6 s (camera image on `camera_gradients`) | needs positive matched invocation counts |
| full gate | as above | one per batch |
| census refresh (`reimp_names.py`, then `progress.py --census`) | about a minute for new leads; existing TSV refresh <1 s | the lead list changes only when the Reimp does; refresh the census each batch; explicitly proved interior spans retain credit without double counting |

The census refresh also validates and decodes the interpreter captures and
refreshes the executed-code supplement outside the historical rows (about 4 s
for the 45-route corpus of 17 Oct). The classifier and progress tests cover
four-byte capture windows against actual instruction lengths, relocated
operands, bound vectors, stale hashes, changed code, COM origins, duplicate
records and supplemental credit without double counting. The private
`census-gap-20261017/check_decoders.py` checked every accepted instruction's
length against Capstone: 57,646 agreements, zero differences
(`decoder-verdict.json`). The supplement excludes static-only discoveries and
reports other modules as unscored debt.

For private proofs of routes shorter than 50 million clocks, the kit adds a
fresh interpreter reference at one-million-clock intervals. Its final state
must still match the keyed route result and its milestones must pass; the
candidate must then match every finer checkpoint and that final state, with
positive routine calls and no event overrun. The ordinary gate keeps its
existing routes and intervals.

The kit can carry an optional self-write/return span separately from the
named coverage window, and emit an explicit completion boundary and clock
count, for bounded entries and shared returns. Batch at's prefix proof checks
flags at its one-instruction boundary, then compares the original
continuation's full final state; only two bytes earn credit. Below-entry
returns still need an explicit COV origin. Default kit output is
byte-identical; invalid zero, overflow and out-of-range bounds are rejected.

**Compile cost.** START `1308` expanded four MOVSW/MOVSB palette copies into
one optimizer graph, and the batch's two normal compiles were stopped after
more than 40 minutes (16 Oct). Compiling each new routine alone identified it
(still running at a 60-second cutoff; the other twelve took 1-5 s); a small MSVC-noinline palette-copy helper that keeps the exact read/write and
pointer-update order brought it to 1.36 s, after which the routine repeated
all its proofs and the full gate (`D:/f117-gate/p2-batch-b2-accept`). Both
runtime libraries now share one `f117matched` object target instead of
compiling `matched.c` twice: against both previous objects all 9,372
non-debug sections have identical code, data, unwind tables, characteristics
and relocation targets (`D:/f117-gate/p2-batch-c-accept/shared-object-verdict.json`);
in one build pair the two concurrent compilations took 143.74 / 144.40 s and
the shared one 99.75 s, before the `/Os` change (Practical traps). The private
kit writes generated project files only when their bytes change (15 Oct), so
an unchanged harness and CMake file keep their timestamps across mutations
while the candidate source is rebuilt; an unchanged MPS_LOGO `08CC` proof
build takes 1.59 s and Ninja reports no work
(`D:/f117-gate/kit-write-probe/verification.json`). No proof step is skipped.

Each routine's states, calls and behaviour are in [roadmap.md](roadmap.md);
the private evidence is in its named `D:/f117-gate/` directory. Typical
figures from the 9-15 Oct batches: 1,600-9,600 states compared per seed,
real-route replays reaching the routine from once to 1,529,189 times, and one
gate per batch. The `camera_gradients` route's changed drawing cost ends its recorded
flight before landing, so it checks rendering, not mission completion.
Fixture-only matched checks cost little: `matched_scene_walk` 2.3 s,
`matched_start_brief_key_loop` 0.6 s, `matched_compose_walk` 0.5 s,
`matched_horizon_and_record` 7.1 s, `matched_menu_input` 0.99 s (5,000
states per routine each).

**Method that worked** (from 1,274-instruction routines up): insert the
`matched_draft.py` instruction transcript first to validate the fixtures,
require per-instruction coverage of the original's run (`COVERED` in
`tests/func_lockstep.c`, a per-routine floor and a skip-reason tally: fault,
long run, bad return, code written, declined, unfinished), plant fixtures
until every instruction is reached, swap in the named body, run the room-claim
audit, the mutations, the real route, then the gate. If the first lockstep
run is already equal, run the mutations before trusting it.

Mutation catches require a passing unmodified baseline. The kit refuses
mutations or a verdict unless all five current seed logs show positive
comparisons, zero skipped states for the selected entry, full instruction
coverage and zero mismatches, and logs and mutation results must postdate
their proof inputs. PLAYER `0010` exposed this: its baseline skipped almost
every state, so its apparent mutation catches were invalid and it was
rejected. Integration refuses an automatic span for an IP already registered
in another segment, requiring explicit handling and comparison of both
(`D:/f117-gate/translate-parallel-20261020/seed-guards-verdict.json`).

**Changing an accepted routine.** Remove the routine's inherited named-byte
coverage and collect fresh optimized coverage before computing its credit (a
corrected RNG alias found by the rotating seed in batch j was reproved this
way, with the failed gate kept under `p2-batch-j-accept/attempt-1-failed`).
When replacing a named opening with its whole function, keep the old
registered address, remove only its old wrapper and registration, preserve
shared helpers used elsewhere, and add full span/coverage fixtures and a
dedicated CTest (START `58DF` added 197 bytes this way without a new address).
A census lead such as `5951` can be an interior arm: expand the enclosing
function and its jump-table dossier before introducing a new override. Switch
data are never byte credit. Screening joins actual byte credit: an
unregistered census entry can be fully covered inside an existing named body,
so a FREE lead alone is insufficient.

**Traps that still apply.**
- `SPANS` is keyed by module and IP, without a segment. A one-byte span for
  VGAME `11D8:0155` also shrank `1377:0155` and made it appear untestable;
  removing the redundant span restored 4,992 comparisons for the older entry
  while retaining all 5,000 for the new RET. Check every segment sharing an
  IP after adding a span (`p2-batch-af-accept/segment-alias-verdict.json`).
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
  nested INT run is clamped to the caller's next event (`support_int`);
  otherwise the run overruns the event limit on every route, which the
  lockstep cannot see and step 9 of the gate catches
  ([architecture.md](architecture.md#matched-routines-phase-2)). Private route
  checkers reject overrun reports as well as differing hashes.
- Stubs: write each into the segment its call executes in (data-segment
  writes left the real routines running and skipped 2,815 of 4,000 states).
  Stubs that fold their stack words into a checksum make a wrong pushed
  argument a memory difference; a plain RET stub hid one mutation.
- Relocated operands are read from the loaded instruction
  ([architecture.md](architecture.md#matched-routines-phase-2)). The harness
  keeps the seven ordinary programs at load base zero (`--load-base` moves only
  the launcher, overlays and drivers), where a constant zero segment passes:
  plant a nonzero segment or vary the operand.
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

**Complete execution inventory.** The gate's recompiled `--coverage FILE`
logs only interpreted fallback instructions, so a fully translated or matched
entry can be absent: missing entries never establish unreachability. For a
whole route use a fresh interpreter run: `py tools/run_route.py
tools/routes/full_cycle.args --data DIR --engine interp --out NEW_DIR --
--coverage NEW_DIR/full_cycle.cov`, kept separate from the gate's logs.
`tools/census_reach.py --census PRIVATE_TSV --coverage NEW_DIR --out
PRIVATE_TSV` joins either source and describes absent entries as lacking
evidence. A fresh full-cycle capture (all 66 checkpoints and the final state
preserved) reached 358 unmatched census entries / 43,138 bytes (14 Oct).
Private inventory: `D:/f117-gate/format-init/reach-verdict.json`.

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
Generation takes under one second. Synthetic regressions cover disjoint
ranges, relocation, bounded walks, indirect exits and COM segment aliases;
Windows CI runs them.

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

## Other repeated processes

| Process | Trigger | Measured cost | Redundancy and status |
|---|---|---|---|
| Matched-routine lockstep (`func_lockstep --states 4000`) | every gate (three seeds); every new matched routine | 8 shards about 3 min 11 s for 653 routines (9 Oct; a single process took 21 min), the same report line for line; about 8 min a seed for the whole table by batch as; Q11 (1,387 routines, verbose, eight shards) measured 543 s and 540 s for its two pre-check seeds | **sharded since 9 Oct 2026** (`tools/func_lockstep_par.py`, `--shard K/N`), with the owner's approval: each routine draws from its own stream (the seed, its module and address), so a shard tests what one run does and a routine's states no longer move when others are added. Seeds 0x5EED0F117A, 0xC0FFEE and one from the gated commit's hash. Left: tracking writes instead of copying 1 MB a state |
| Instruction lockstep (`insn_lockstep --states 64`, also `--timing386`) | every gate | runs beside coverage and parity, no wait tail; 5,723,136 states per profile | the 386 check also compares prefetch and held REP state; a second-seed run with `--base-seg 0x9FFF` checks code in VRAM |
| 386 flight engine parity (`stick_response.py --machine machine386 --engine interp/recomp`) | after a profile or translated timing change | 80 s interpreter, 60 s recomp, beside the gate (8 Oct) | all 574 memory/register/output/scanout checkpoints and stick responses identical; separate processes because the runtime is global |
| Worktree build and generated code | per worktree | 8 min first, then incremental | give each tree its own TEMP; never share a generated directory between concurrent builds |
| Route recording (adaptive pilots) | after a timing change moves a flight | 10-20 min per flight; try several parameters in parallel | recorded inputs replace them in the gate; a timing change invalidates all of them at once (see `airair_type5-8`, `strike`, `cargo`, `recon`) |
| Targeted lockstep for one VGAME routine with returning driver thunks | before the full gate | 0.5-1.2 s per 4,000-state seed (9 Oct) | enabling thunk substitution changes test inputs to reach drawing branches (marker: 358 to 4,857 comparisons); it is not an identical-input optimisation. Full routes keep the actual driver |
| Staged combat regressions (`d1ttl_check.py`, `d1prox_check.py`, `d1physics_check.py`, `d1slot_check.py`) | after changing that option or its dispatch | 76 s (TTL), 114 s (PROX), about 2 min (physics, slot) for four parallel boots (10 Oct) | both engines with and without the option; compare case hashes across engines and unchanged control hashes (cases in [bugs.md](bugs.md)). Explicitly staged, not natural combat |
| Real-time simulation regression (`d1real_check.py`) | after changing D1REAL or its dispatch | 3.8 min for six parallel boots (10 Oct) | 9/20/40 MIPS in both engines, normal inputs only; normal clock, 2x compression, restored rate, pause/resume, quit/cancel |
| Staged seeker-underflow reproduction | next countdown regression | about 120 s for two parallel boots (10 Oct) | private `slot_underflow_probe.py`, interpreter/recomp, S=8/15; the off-axis setup reproduces the defect (exact-rear did not) |
| Raw missile-slot trace | investigate seeker lifetime underflow | 219 s for a parallel 9/20 MIPS pair (10 Oct) | private wrapper around the current pilot; affinity 0x0F000000 beside the gate's 0x00FFFFFF leaves four desktop CPUs. Slot occupancy is not a count of blocked launches |
| Combat cohorts (`threat_profile.py` telemetry v5, `threat_compare.py`) | CPU-speed/combat investigation, after changing pilot, observer or an option | 10-24 min for 24-96 flights at 8-12 workers (10 Oct) | cohorts from before telemetry v5 cannot be reused for combat conclusions. Reuse an unchanged arm only after a flight-hash check; validate mission identity; keep early exits; bootstrap paired differences. START seed matching does not align VGAME's later BIOS-tick RNG seed: align the final hangar input separately and sample at completed simulation steps. Raw results outside the repo |
| Speed and D1 sweep (`speed_sweep.py`) | evidence for the default-speed decision | about 12 min for 90 flights, 16 workers (9 Oct) | each route's unfixed 9 MIPS baseline runs first to anchor input times; results resume by tag in `runs.jsonl`; compare engagements only on matching missions |
| Isolated 15 FPS cap experiment | does a cap alone keep S at 15 and the mission clock real | about 18 min for 45 flights beside a gate; isolated generated build about 3 min (9 Oct) | one change in a detached checkout (D1_FPS_X10 116 to 150); `D:/f117-gate/speed15/experiment.json`; no default change |
| Control-response sweep (`stick_response.py --ips N [--fix D1]`) | compare controls before choosing a default speed | roughly 43-220 s per flight under load (9 Oct) | repeat the same 30 timed taps; keep mission identifiers, raw samples and early stops so incomplete or differing flights cannot pass as equal |
| START random-seed probe | find boot clocks that generate the same mission across speeds | five front-end boots; wall time not recorded | read START's RNG at DS:AE8C and the BIOS tick count; change only the boot clock, never guest memory (`D:/f117-gate/speedsweep/probe_start_seed.py`) |

## Reference captures

| Process | Trigger | Measured cost | Redundancy and status |
|---|---|---|---|
| PC-parity run (`pc_parity.py`: pictures, music, saved roster on three references) | before a release; after a machine-model change | estimated at least 4 min wall, set by DOSBox-X's real-time save run (not re-timed since 8 Oct); 12 min before 7 Oct | the three references run at once, and 86Box's three runs (picture capture, music and timing, saved roster) run at once too, each stopping only its own 86Box; the traced runs open no VNC port and run on emulated time. Fast-forward where it gives identical results: DOSBox-X's picture capture 11 s instead of 130 s; 86Box save 95 s instead of 277 s, music 49 s instead of 178 s, pictures 48 s instead of about 3 min. DOSBox-X's save run stays at its own speed (4 min): in fast-forward under load its scripted clicks missed |
| Held-picture hashing in PC parity | each reference comparison | about one second on the saved capture | decodes only stationary scenes instead of every changing frame; eight regression tests (`tests/test_reference_timing.py`) take 0.13 s |
| DOSBox-X closed-loop flight (`dosbox_cargo_pilot.py`) | per pilot change | 107 s wall in fast-forward (12 min paced); the same pilot on our machine about 1.5 min | fast-forward (`[cpu] turbo=true`) is the default: `result.json` and `flight.csv` identical to a real-time run. Run it beside the machine's flight |
| 86Box closed-loop flight (`b86_cargo_pilot.py`) | per pilot change | about 200 s wall in fast-forward (`B86_FAST`) | deterministic (same `flight.csv` twice), so one flight is a verdict |
| Original-game clock on 86Box (private `D:/f117-gate/default-fixes-20261017/clock86.py`) | evidence on the original's mission clock | 30 emulated seconds settling, then 120 emulated seconds captured; wall time not recorded | `b86_cargo_pilot.B86Machine` offscreen on a copied 386DX33/MS-DOS 5 profile; mission time, S, compression and frame counter against 86Box's TSC clock, no flight input or fix, START's mission seed staged by the front-end harness. Reuse `loop.state` for analysis instead of rerunning the VM when an analysis guard is wrong. Result: [speed-sweep.md](speed-sweep.md#original-game-clock-on-86box-17-october-2026) |
| Machine-behaviour probe (`fidelity_all.py`) | after a machine-model change | about 2 min with a saved 86Box sheet; `--86box-run` adds 4 min | the 86Box sheet changes only with the model, so it is reused |
| 86Box boot attempts | per BIOS or config change | 2-5 min per capture series | `hello_floppy.py` answers "does the BIOS boot" in one run first |
| Silent 86Box speech waveform (`tools/ref86box/86box-audio.patch`, `B86_AUDIO=PREFIX`) | speech/sound comparison | incremental 86Box build 23 s; two intro runs together 28 s; takeoff-call flight 94 s; our interpreter replay 50 s; WAV render 1.6 s; comparison about 0.6 s (9 Oct) | records the mixer buffers before host playback (format and procedure in [build_86box.md](../tools/ref86box/build_86box.md#silent-speech-waveform-reference)). First require identical frame and port traces with capture off and on. Reuse captures while the binary, input and configuration hashes agree. Generated audio and game data stay outside the repo |

## What the gate is checked against

The gate compares the recompiled engine with this project's own interpreter,
so a model error shows in neither. Fidelity to a real PC is checked
separately against GOG's DOSBox 0.74, DOSBox-X and 86Box (a 386DX/33 with
MS-DOS 5.00, the 386 timing profile's reference), per question or before a
release (`pc_parity.py`), not per commit: layers 7 and 8 of
[architecture.md](architecture.md#verification-why-11-is-a-claim-with-evidence).
