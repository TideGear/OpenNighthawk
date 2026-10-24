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

- **Progress scoring changes.** Run `tests/test_progress.py` and `tests/test_census_classify.py`
  with the installed Python interpreter, then print the scoreboard and inspect the saved byte
  counts. Encoded review validation adds `tests/test_encoded_code.py` (five adversarial tests);
  all 74 focused tests take under one second, and the full census refresh about ten seconds.
  No game runtime changes means no matched-code gate is needed. Checklist scoring
  (24 Oct) preserves every byte count and scales existing P2 weights to 90 before adding weight 10.

- **Installer private proofs (24 Oct, awaiting integration).** The ordinary kit/finish/audit
  workflow proved ASOUND.LOG 005A, ISOUND.LOG 060E and RSOUND.LOG 005B plus its separate
  00AD return: 170 bytes, 57 instructions, 80 caught mutants and six seeds per entry.
  Real-route proofs took 39.3 s (full_cycle), 23.6 s (ibm_boot), 27.1 s (Roland installer)
  and 21.6 s (Roland return); IBM controls took 22-28 s. These are proof timings while
  other private compilation shared the machine, not a gate comparison. A literal-claim
  audit independently checks complete/disjoint XB and explicit SM4_NEED claims and pins
  configs, dossiers and source hashes. Evidence: `D:/f117-gate/p2-installers-review-20261024/`.
  No progress credit until the full integration gate passes.

- **Matched routine batch (Phase 2).** Private proofs per routine in
  `D:/f117-gate/p2-*`, then one acceptance directory per batch
  (`p2-batch-<letter>-accept/`: `accept.py`, `overlap_accept.py`,
  `optimized.py`, `audit_private.py`, `finalize.py`). Batch al: acceptance
  1,549 s with the presentation overlapped (427 CTests, 63 routes / 62 fresh
  interpreter sessions, N=1 718 s started 793 s into the gate, boot N=4/9 75 s).
  A keyed per-route baseline is what lets the presentation checks overlap the
  gate; without a valid one (after a route or non-matched `src/` change) they
  run sequentially (batch ai: 1,658 s in all). Before a gate, two cheap
  pre-checks catch most failures: the commit-keyed seed through
  `tools/func_lockstep_par.py --jobs 8` (about 8 minutes per seed in batch as) and every new route
  once with `--engine recomp` for `[matched] OVERRUN`. `optimized.py` passes `--only` with
  unpadded hex (a padded address once selected zero states) and requires
  positive comparison counts; `finalize.py` validates old coverage against the
  previous batch's own registration table and every new byte against the
  dossiers. The census refresh afterwards takes seconds.
- **Extra seed sweep before a matched batch.** Batch at checked all 1,251 entries at 4,000 states,
  eight shards, using the commit-keyed seed, `0x27182818` and `0x20261022`; 1090 s in total.
  The commit-keyed seed first failed on VGAME `73C8` (a latent clock undercount in its target-zero path, fixed in
  batch at: the body now charges 6, 13 and 6 instructions where it charged 5, 10 and 6); 16 seeds at 20,000 states
  then compared equal. Of the 1,248 old per-routine reports, 1,247 were unchanged at the common seed (`73C8` differs). New entries also passed six extra
  private seeds beyond the kit's five, with every planted defect caught. Evidence:
  `D:/f117-gate/at-precheck-verdict.json`; the three new optimized CTests took 2.80 s.
- **Run the pre-check seeds together.** `D:/f117-gate/precheck_parallel.py LOGDIR SEED...` runs the three whole-table seeds at once
  (8 workers each, 24 of the machine's 32 threads, BelowNormal): 11 minutes against 18 in series. Batch at's three seeds
  gave identical tallies and identical per-routine lines either way, so no check changed; the saving is about 7 minutes a batch.
- **Batch au pre-checks.** Three whole-table seeds and three extra seeds, each round through `precheck_parallel.py` (about 12 minutes a round),
  all clean at 1,256 entries; the 1,251 older reports were unchanged. Evidence: `D:/f117-gate/au-pre3/` (final text) and `au-pre2/`.
- **Batch av pre-checks.** Six seeds (the commit-keyed one, `0x27182818`, `0x20261022`, `0x31415926`, `0x1BADB002`, `0x600DF00D`) in two
  `precheck_parallel.py` rounds, all clean at 1,261 entries; the 1,256 older reports were unchanged. Evidence: `D:/f117-gate/av-pre/`.
- **Batch aw checks.** Seven new optimized CTests passed in 2.28 s. Six whole-table seeds at 4,000 states
  passed in two three-seed rounds (`D:/f117-gate/aw-pre/`); acceptance took 1,671 s with 509 CTests.
- **Batch ax checks.** Twenty targeted optimized CTests passed in 1.82 s; six whole-table seeds passed in two three-seed rounds
  (1,407.72 s total, `D:/f117-gate/ax-pre/`). The core gate took 1,584 s and full acceptance 1,795 s: 528 CTests, 63 routes /
  62 fresh interpreter sessions, both timing profiles, event limits, all N=1 presentation and boot N=4/9. All 95 new optimized seed
  checks passed. The first precheck attempt exposed a missed callee-return boundary in the test runner; exact CPU-state recovery keeps
  the final memory/I/O comparison and grants no named completion. Nineteen synthetic cases cover the return, prior interrupt gaps and
  incorrect clocks, registers, flags, memory and I/O. No checks were removed.
- **Relocated fixture infrastructure (23 Oct, accepted locally).** After the normal build, 76 focused CTests take about six seconds;
  metadata, parallel-translation and region tests plus invalid-base CLI controls take about two seconds. A whole-table zero-base
  control (4,000 states, eight shards) runs beside 73-entry sweeps at 0400h and 0800h. The first arena attempt took 613 s:
  all 1,287 zero-base reports were unchanged, while one 0800h state exposed an uninitialized graphics fixture page table.
  The repaired fixture fills disjoint pages without new random draws; fresh controls precede six standard precheck seeds and
  the full gate. `D:/f117-gate/modules-loadbase-20261022/run-infrastructure-acceptance.py` sequences these checks;
  `p2-batch-loadbase-accept/` additionally pins every changed code/test/CI file and requires zero added credit.
  Fresh repaired controls passed in 608 s; six seeds took 1,530 s. The core gate took 1,369 s, followed by sequential presentation
  (N=1 564 s; boot N=4/9 101 s in parallel), 1,955 s for the trial. The new CMake target changed the interpreter key, so the old
  baseline was correctly unused. A closing-audit bug still checked it; the repaired generic audit independently verifies the
  actual fresh sequential references and presentation results. The current baseline is promoted and the old one archived.
  The final verdict's wall time includes the idle interval before that audit repair; use the stage timings for performance.
- **Bounded entries and shared returns.** The private kit carries an optional self-write/return span separately
  from the named coverage window and can emit an explicit completion boundary and clock count. Batch at's
  prefix proof checks flags at its one-instruction boundary, then compares the original continuation's full
  final state; only two bytes earn credit. Below-entry returns still need an explicit COV origin. Default
  kit output is byte-identical; invalid zero, overflow and out-of-range bounds are rejected. Gate acceptance
  and credit audits remain unchanged.
- **Batch acceptance tooling (`D:/f117-gate/accept-kit/`, 18 October).**
  `new_batch.py LETTER PREV BASE_COMMIT DIR...` creates `p2-batch-<letter>-accept/`
  from generic scripts (no hand-editing of routine names, counts or hashes: tests, routine totals,
  mutations and added bytes are read from the gate log, `matched.c` and the verdicts; far-segment
  routines use linear addresses; `callfar` kinds and call macros are recognised); `accept.py` runs the
  gate with the overlapped presentation, the ten optimized seeds, the baseline group verdict and the
  credit audit, then `audit_private.py` is run by hand. `finish_all.py DIR[:ROUTE]...` runs each
  directory's checkpoints, boot proof and verdict side by side on spare CPUs. Wall-time changes that
  keep every check: the gate now starts the fresh interpreter sessions (62 today) (the critical path: the
  career chain is ~1,285 s) right after the build and before the unit tests rather than after them
  (`build_recomp.py`: the same tests run concurrently, a failure still fails the gate); the overlapped
  N=1 presentation starts speculatively when coverage ends instead of after the re-translation check
  (the closing runner-hash comparison turns a wrong guess into a failed acceptance, never a pass).
  Expected saving: roughly 100-170 s of ~1,700 s; the remaining floor is the interpreter chain
  (about 337 s of build and tests, plus the 1,285 s career run).
- **Static-code scope review.** Read each candidate range in full, decode every
  instruction independently with Capstone, then let `reviewed_code_bytes`
  verify image hashes, complete instruction boundaries, counts and
  non-overlap before `docs/census-code.tsv` is written (all-or-nothing). Defer
  ranges whose discovery lists overlap, even when isolated lengths agree
  (logo mouse helpers; SETUP `1E4F`). Refresh with the existing census file,
  the latest named coverage and batch-h route coverage. No gate is rerun for a
  scope-table-only change; the classifier, progress and dossier tests are
  enough (`tests/test_census_classify.py` and the progress tests).
- **Module scope refresh.** Decode and validate the existing route captures against all 17
  game code files, then refresh progress with the current named proofs. The 21 Oct measurement
  took 2.8 s; the complete score refresh took about 9 s. Scope-only tooling changes use the
  progress, classifier and dossier regressions plus the normal gate CTests
  (`ctest --test-dir build -E func_lockstep --output-on-failure -j 8`). The standalone
  serial `func_lockstep` CTest duplicates the gate's separate sharded check; use
  `tools/func_lockstep_par.py --jobs 8` for that comparison. Production and matched code are
  unchanged. Evidence: `D:/f117-gate/modules-scope-20261021/`.
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
- **Presentation-only changes** (`src/present`, observer, drawlist): full
  CTests, all 63 N=1 routes and boot N=4/9 against the frozen interpreter
  baselines of the preceding full gate (about 1,450 s in all); changes to
  `drawfeed`/`rerun` need the full gate too. Diagnostics that replay a frozen
  record (pixel-origin `DT_HUD_FINE` counts, before/after frames) are
  correctness checks, not pacing measurements. Dispatcher or observer
  micro-benchmarks never promote a default.
- **Harness changes after an unexpected rotating-seed failure.** Reproduce on
  the pre-change tree first. An escape-guard repair must keep exact
  original-state evidence (clock, registers, segments, flags) at the destroyed
  return, still compare the whole final state and grant no named-code credit;
  a one-tick negative control must still fail. If production source is
  unchanged, a rerun may reuse the freshly verified interpreter cache while
  tests and registration are the only amendment.
  A recovered state can also change later fuzz inputs: the existing one-in-four
  mid-run probe draws from the routine's random stream. In batch ax's shared-seed
  review, 1,266 old reports are identical, END's error reporter gains one valid
  comparison, and SETUP's startup gains two recovered returns. The latter's new
  probe at state 509 advances the stream, changing inputs from state 510 onward;
  its total comparison count changes from 649 to 631 while all 4,000 iterations
  remain. Fixed/control traces prove the first 510 random-stream values equal
  and retain every check (`D:/f117-gate/ax-legacy-io-20261023/rng-review.json`).

## Practical traps

- ROM-free tests that group the real route catalogue must provide a synthetic
  install: reading damaged-install route arguments stages their private copy.
  `tests/test_build_recomp.py` now supplies one; all twelve checks pass with
  `F117_DATA` pointing at a missing install. This repairs the previous CI
  failure without changing route grouping or its assertions.
- **`matched.c` must compile with `/Os`.** From batch ab on, MSVC's `/O2` (which includes `/Ot`) took 90+ minutes
  to compile `src/matched/matched.c` (the first batch-ab gate took 108 minutes; later gates only looked fast because the
  object was reused). The slow functions are the big pre-existing VGAME routines (`vgame_draw_model_in_view`,
  `vgame_display_pass` at 125,787 instructions after inlining, ...), not the new ones; batch ab only tipped the optimiser
  over. Measured on 20 October 2026: `/O2 /Ob2` still running at 90 min, `/O2 /Os` 6.5 min, `/O1` 4.5 min, `/O2 /Ob0`
  10 s (the forced-inline CPU helpers become calls), `/O1 /Ot` slow. `CMakeLists.txt` sets `/Os` on that one source file.
  Find the slow functions of a compile with `cl ... /O1 /d2cgsummary` ("Anomalistic Compile Times"). A stuck `cl.exe` is
  one core at 100%: check CPU time before concluding it is idle, and never run more than two or three of these at once
  (3-4 GB each).
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
no matched routine ran past its event limit (step 9).

**Fix selection (17 October):** normal application play starts with all fifteen
fixes, including D1REAL and D1PROX. `build_recomp.py` and `run_route.py`
explicitly prepend `--fix none`; subsequent fix-specific route options or
recorded headers select the intended test cases. The headless runner also
starts with fixes off. Do not enable every fix for ordinary parity: both
original behavior and repaired behavior need their separate checks.
`tests/test_fix_defaults.py --app APP --runner RUNNER` is a ROM-free defaults
process check, also in Windows CI. Private recording/replay checks verify all,
none, a recorded subset and explicit replay additions; old logs retain none.
Evidence: `D:/f117-gate/default-fixes-20261017/headers/verdict.json`.
The combined-default check reuses `d1real_check.check` with all fifteen fixes
at 20 MIPS in both engines: seven normal/compressed/pause/dialog cases and
every case hash equal (69.17 s). Use separate processes: the Machine API
allows one open machine per process. Private wrapper and evidence:
`D:/f117-gate/default-fixes-20261017/{all_fixes_clock.py,all-fixes-clock/verdict.json}`.
The hidden recorded-session app check took 165.69 s: both presentation modes
retain 2,238 exact logic frames and equal presented/scanned palette counts;
rerun draws 7,676 intermediate pictures (`app-session/verdict.json`). These
checks shared the machine with the gate and are correctness checks, not
performance acceptance.

The standing command (the handoff has the local paths) is `py
tools/build_recomp.py --data INSTALL --seed-coverage WORK/coverage --jobs 16
--verify-interp-cache`, started through `D:/f117-gate/affinity_launch.py`
(below).

**Current figures (21 October 2026, batch am):** 432 CTests; 63 routes identical, run
as 62 sessions, every interpreter session verified fresh; 5,723,136 instruction states under each
timing profile; 1,193 matched routines at three seeds; reachability and event limits clean.
Gate 1,586 s; acceptance 1,572 s with the presentation checks overlapped (batch al: 63 N=1 routes,
260,613 exact frames / 260,527 pairs; boot N=4/9 736 / 732). Evidence:
`D:/f117-gate/p2-batch-am-accept/`. Typical gates take about 1,500-1,800 s; the longest
career seed chain sets the route time.

| Stage | Latest measured | What sets it, what was duplicated | Status |
|---|---|---|---|
| 1-2 translate, build | 87-113 s incremental (15 Oct); 7-8 min in a fresh work directory; about 6 min after a CPU-header edit | a fresh work directory builds everything; a CPU-header edit rebuilds all generated C and both matched objects | **fixed by practice**: reuse the previous run's work directory (`sync_tree` rewrites only files whose bytes changed, so the build recompiles `matched.c` and relinks); the output is a function of the sources either way. Generated regions are capped at 1,024 instructions (a 6,446-instruction VGAME function had dominated MSVC: 479 s before, 120 s after; both timing profiles keep their instruction verdicts) |
| unit tests | 22.47 s for 75 tests at eight workers, against 159.76 s serially (14 Oct) | ran serially before the route workers | **fixed**: up to eight workers (`--unit-jobs N`; `--unit-jobs 1` is serial). Same executable, inputs, selection, affinity and priority: every test's captured output identical to the serial run. Proof: `D:/f117-gate/leaf-batch/unit-parallel-verdict.json` |
| 3 coverage | 711-815 s (15 Oct), or skipped | runs every route once only to find untranslated code; as long as its longest job (`career_promotion`, after its `career_serge` seed) | **skipped when provably empty**: a record (`~/f117-recomp-local/coverage-verified.json`, `--coverage-record`) holds the key of a pass that added nothing - translator, sources other than `matched.c`, game files, routes - and the digest of the generated code it ended on; the same key and digest skip the pass. A matched routine only replaces original instructions that already ran, so it cannot add code to cover; a wrong one fails parity or lockstep. Verified: full verdicts identical with and without the skip. The key hashes `CMakeLists.txt` without `add_test` entries, whole-line comments and blank lines (`build_digest`), so adding a test does not rerun coverage. A commit adding a matched entry still reruns it (the translator isolates each matched entry) |
| 4 translate, build again | 35-41 s; about 220 s when coverage added code | regenerated and rebuilt everything though coverage usually adds nothing | **fixed**: only files whose bytes changed are rewritten; the rebuild is skipped when none did; `--seed-coverage DIR` starts from an earlier run's coverage. A rebuild restarts the locksteps, so their earlier results do not stand for the final build |
| 5 parity | 345 s after overlap and reuse; 1,223 s with coverage skipped | as long as `career_promotion`'s interpreter run, which first replays its whole seed chain | **fixed**: longest jobs first; identical sessions run once (found by content, not name: `recon_career` and `recon_return`), with every route's milestones checked on the shared run. **Overlap**: `--overlap-jobs N` (default 8; 0 = off) starts the interpreter runs during coverage from a private copy of the first build's runner and libraries (`snapshot_runner`); parity then runs only the recompiled half and collects them. **Coverage-run reuse**: when coverage adds no translated code the first build stands, so the coverage runs (made with the parity checkpoints) are the recompiled half; `--check-coverage-reuse` reruns them and requires equality (proved 15 Oct: all 44 equal, every verdict identical), `--no-reuse-coverage-runs` turns reuse off. Measured 15 Oct: 1,706 s before (coverage 711 + parity 848), 1,556 s with the overlap. The gate now ends when `career_promotion`'s interpreter chain does, so reuse saves CPU (about 513 s of recompiled reruns) rather than wall time |
| 5b interpreter half of parity | all 44 sessions run fresh in the standing command | an interpreter run is a pure function of the game files, the route and the interpreter's own code | **cached** (`--interp-cache DIR`): key over `src/` except `matched.c`, `CMakeLists.txt` (as above), the game files and the route (text, replay files, seed chain); entries expire after 14 days. Measured: parity 522 s to 174 s with 31 of 31 reused, every verdict identical. The standing command passes `--verify-interp-cache`, which reruns every session and requires equality, so "0 reused, 44 run" is expected and the cache saves nothing there |
| 6-7 locksteps | add 0 s | waited for parity though they need only the build | **fixed**: start right after the first build, beside coverage and parity; output to files in the work directory (`*_lockstep.log`). Measured 7 Oct with coverage forced: 778 s to 572 s, the lockstep tail 241 s to 0, every route line and both tallies identical |

Before these changes (6 Oct) a gate took 30-38 minutes; the first round
alone cut the same code's gate from 1,895 s to 1,365 s with every route's
clock, hash and verdict and both lockstep tallies identical.

**Translation preparation (20 October).** Discovery and emission now use one
process per module, up to eighteen (`recomp.py --jobs 1` is serial), with the
module list and console reports kept in load order. The larger saving came
from building each module's relocation mask once during coverage loading,
instead of rebuilding it for every captured instruction. The unchanged serial
generator and the new parallel generator produced identical sets of 58 files,
byte for byte, both without coverage and with the current coverage corpus:
51.23 s became 5.20 s with coverage; without it, 2.05 s and 1.99 s. These were
sequential runs on four assigned CPUs, beside private proof work, so they
establish output equality and an observed saving, not a controlled gate timing.
Evidence: `D:/f117-gate/translate-parallel-20261020/verdict.json`. Windows CI
also compares actual spawned workers with serial translation on synthetic
modules, including relocation, coverage, isolation and mutations, and requires
a worker failure to propagate before a module list is published.

For private proofs of routes shorter than 50 million clocks, the kit adds a
fresh interpreter reference at one-million-clock intervals. Its final state
must still match the keyed route result and its milestones must pass; the
candidate must then match every finer checkpoint and that final state, with
positive routine calls and no event overrun. This adds checkpoints to short
proofs; the ordinary gate retains its existing routes and intervals.

**Overlap worker count (15 Oct).** The latest batch tried 16 interpreter
workers beside coverage: every one of the 44 sessions still ran fresh, all
45 route results were identical, and every other check remained. It took
1,718 s versus 1,553 s for the previous eight-worker batch, with different
source/build work; after the build, 1,329 versus 1,257 s. This is not a
controlled speed gain, so the default stays at eight. Private proof
parallelism and write-if-changed builds remain adopted.

**Adaptive interpreter pool (16 Oct, private prototype).** At batch e's
interpreter-only tail the eight-worker pool used 26% total CPU even though
coverage had ended. `D:/f117-gate/p2-batch-e-accept/adaptive_pool.py` retains
eight running jobs during coverage, then permits sixteen through a semaphore
on the same executor. Scheduling tests require exactly 44 calls, identical
results, the requested before/after concurrency, idempotent expansion and
preserved exceptions. All pass (`adaptive-pool-verdict.json`). This is not
integrated and establishes no real gate speed gain. Queue analysis of this run
(`schedule-analysis.json`) gives 1,346 s observed interpreter wall and a 1,285 s
longest job; keeping measured job durations fixed, eight-to-sixteen scheduling
would save only about 61 s. This estimate ignores changed CPU contention and
second translation. It may not justify adoption. Presentation overlap against
frozen, current-key baselines has now passed the checks below. Every verification
remains mandatory.

**Presentation beside the interpreter tail (16 Oct, adopted batch practice).**
The private acceptance wrapper `D:/f117-gate/p2-batch-f-accept/overlap_accept.py`
freezes existing baselines and validates them against current interpreter/route
cache keys, full checkpoints/final hashes and every route milestone. After the
gate's final build settles, N=1 uses a frozen runner and baselines on 16 workers
beside the eight fresh interpreter workers, under the existing 24-CPU affinity.
Boot N=4/9 starts after the gate. Every command/check is retained. Before accepting,
all fresh interpreter sessions must match the frozen baselines at every
checkpoint/final and milestone; all frozen file and runner hashes must still match.
Five deliberate faults are rejected (wrong final, wrong checkpoints, missing
final, changed frozen file, stale result claimed as fresh). Evidence:
`overlap-guards-verdict.json`, `fresh-baseline-verdict.json`, `overlap-verdict.json`.
Batch f: gate 1,781.97 s, total acceptance 1,836.51 s;
N=1 started at +1,087.12 s. These are observed
concurrent timings, not a controlled speed comparison against batch e. Sequential
checking remains the fallback if no valid keyed baseline is available. No change
to the gate's default worker count or fresh interpreter verification.

The wrapper also stores each route's own key within a shared session (20
October). `recon_career` and `recon_return` run the same inputs but carry
different milestone comments, so their route keys differ. Comparing both to
the lead route's key rejected a freshly frozen baseline. The wrapper now
validates every route's key and the exact session grouping, while retaining
the lead's cached state, all routes' milestone checks, fresh interpreter
comparisons and frozen-file checks. Eight guard checks include changed alias
milestones, wrong lead/alias keys, changed grouping, expired references and
changed files or interpreter inputs; evidence:
`D:/f117-gate/p2-batch-af-accept/baseline-key-guards.json`.
When the gate itself validates and reuses its unchanged coverage record,
presentation starts after the unit tests instead of waiting for a coverage
completion line that will not be printed. The final frozen-runner and fresh
interpreter comparisons still apply.

**What sets the wall time.** `parallel()` prints each stage's wall time,
work, lower bound (the larger of the longest job and work / workers) and its
four longest jobs; read that before changing worker counts. Measured 14 Oct
(`--parity-only --verify-interp-cache`): 88 jobs, 16,736 s of work on 16
workers (1,046 s if perfectly packed), stage 1,160 s, longest job 1,159 s
(`career_promotion/interp`). A seeded route's job first replays its whole
seed chain (`prepare_roster` in `run_route.py`): `career_serge` (790 s as its
own job) plus its own run in sequence. The chain, not the worker count, sets
the end; reusing the seed's roster would save work, not chain length. The
remaining lever, dropping `--verify-interp-cache`, weakens a verification
rather than speeding up the same one: the owner's decision, not yet taken.

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
- `ctest -E func_lockstep -j 8`: the same 142 tests the gate runs (111 took
  36.56 s on 15 Oct).
- `tools/presentation_check.py --data DIR --baseline-runs DIR/runs --out DIR
  --scales 1` over every route: 718 s for 63 routes at eight workers beside a gate
  (batch al; 517.89-579.53 s for 45 routes at 16 workers alone). It keeps 260,613
  exact logic frames / 260,527 exact interpolation pairs and every guest checkpoint
  and final hash.
  Options: `--routes STEM...`, `--scales N...` (default 4 and 9), `--jobs N`
  (default 4), `--mode interp|rerun`. Run the full gate first for fresh
  baselines. It requires matching guest checkpoints, final hashes and route
  milestones, every logic frame and interpolation endpoint exact, no unseeded
  or dropped frames, and presentation pairs on routes that enter flight (a
  short SETUP-only route may have no 50M-clock checkpoint and no flight
  frames). These concurrent runs check correctness, not refresh-rate
  performance.
- `boot_to_flight` at `--scales 4 9`: about 56 s at two jobs, 368 frames /
  366 pairs each.
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
machines, so changes there need the full gate plus the 45-route `--mode
rerun` check. For eligible changes it builds, runs every CTest, the 45-route
N=1 check against the last gate's baseline runs, and boot at N=4/9. A full
gate is still needed before the next Phase 2 `kit.py checkpoints` (any `src/`
change retires the interpreter cache entry).

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

The owner's authorized longer fullscreen air-to-air run (17 October) took
656.44 s, with no other heavy work: 17,241 pictures, p99 28.6976 ms and 3.7022%
above 25 ms, failing both limits. Guest drift 0.005819 s; all 3,679 logic frames
exact and final hash equal to a separate 56.2-second headless run. Evidence:
`D:/f117-gate/present-v2/stage4-airair-long-20261017`. Further visible runs
still need the owner's agreement; this result needs diagnosis, not more samples
to explain away the failure.

## Independent original-game clock

The private `D:/f117-gate/default-fixes-20261017/clock86.py` uses the existing
`b86_cargo_pilot.B86Machine` offscreen on a copied 386DX33/MS-DOS 5 profile.
After 30 emulated seconds settling, capture 120 emulated seconds of mission
time, S, compression and frame counter against 86Box's independent TSC clock.
No flight input or fix is applied; START's mission RNG seed is staged by the
existing front-end harness. Reuse `loop.state` for analysis rather than rerunning
the VM when an analysis guard is wrong. The 17 October capture found 152 mission
seconds in 119.994917 reference seconds at S=5 and normal compression=1;
`analyze_clock86.py` and `finalize_clock86.py` retain samples, hashes and verdict.
This tests original behavior on that profile, not developer intent or host
wall-clock pacing. Interpretation: [speed-sweep.md](speed-sweep.md#original-game-clock-on-86box-17-october-2026).

Latest (15 Oct, with state-driven bands): strike at 3840x2160 / 59.99 Hz /
vsync / N=9, 7,718 flight pictures, p99 16.7314 ms, maximum 17.0385, no missed
interval; 2,062 exact logic frames, final hash `bc8e3ae5a872405d` equal to a
fresh headless reference. All three scenes and the four-CPU affinity run pass:
[presentation.md](presentation.md). Run with nothing else heavy going; runs
beside the gate are exploratory, not acceptance evidence. A private hidden
3840x2160 Direct3D 11 probe (all state/interpolation flags, five
intermediates per pair) measures strike / mode-2 / small windows at
119.47 / 127.45 / 150.10 fps uncapped, regeneration/raster p99 below 5 ms,
with occasional roughly 100 ms upload stalls; uncapped numbers do not prove
refresh pacing. A visible fullscreen run the owner stopped is not evidence.
Evidence: `D:/f117-gate/present-v2/band-finish`,
`D:/f117-gate/present-v2/hud2`.

## Phase 2 proofs (matched routines)

**Process.** Each proof uses its own
`D:/f117-gate/p2-*` directory, with `D:/f117-gate/kit/kit.py` (init, build,
lock, seeds, mutations, checkpoints, verdict, integrate; method and traps in
`D:/f117-gate/kit/BRIEF.md`). Finish the proof workers and verify every
verdict before starting the batch's gate; run no other heavy job during it
(owner, 20 October).

The current private queue uses three slots, each with one compiler job;
larger routines start first, and a slot carries its routine through mutations,
route checkpoints, boot and verdict before taking another. Batch ax's final
pass took 1,240 s for 18 ready routines and one failed mutation list; the
remaining routine passed after three equivalent edits were replaced with
observable carry defects. This timing includes repeated proof work, not the
whole batch's development cost. Fresh harness compiles still take roughly
80-140 s per private project; reuse beyond the existing write-if-changed
behavior needs an equivalence measurement before adoption. Three compiler
processes remain the limit. The 114 extra-seed logs, exact mutation lists,
source freshness and boot hashes were independently audited.

| Step | Cost | Note |
|---|---|---|
| private build loop | initial 9.9 s, edits 2.6-3.6 s | a direct candidate matched object linked before frozen generated/core libraries; no generated sources recompile. /Od is development evidence only (it avoided a very large optimizer peak); integration needs /O2, optimized seeds and a full gate. Library hashes kept with the private project |
| private generation in a worktree | 21.9-30.7 s | from frozen coverage, its own generated directory; build with two workers and its own TEMP, after main's matched shards finish |
| targeted seeds (`func_lockstep --only MODULE.EXE:IP --states 4000`) | 0.4-5 s per seed; 23.3 s for the 1,332-instruction mission generator | run every new body at four or five seeds: the default seed passed a body with a wrong resume point |
| mutations | 37 edits about 8 min serially, 90 s four at a time | identical CAUGHT results; the kit never edits `body.c` |
| isolated unit tests | 29.72-80.52 s (36-44 tests) | |
| real-route replay | 0.8 s (`setup_calibration`) to 190.6 s (camera image on `camera_gradients`) | needs positive matched invocation counts |
| full gate | as above | one per batch |
| census refresh (`reimp_names.py`, then `progress.py --census`) | about a minute for new leads; existing TSV refresh <1 s | the lead list changes only when the Reimp does; refresh the census each batch; explicitly proved interior spans retain credit without double counting |

The census refresh now also validates and decodes the interpreter captures and refreshes the executed-code
supplement outside the historical rows (17 Oct 2026; about 4 s for the current 45-route corpus).
`py tests/test_census_classify.py` and `py tests/test_progress.py` cover classification, four-byte capture
windows versus actual instruction lengths, relocated operands, bound vectors, stale hashes, changed code,
COM origins, duplicate records and supplemental credit without duplicate counting (37 tests together).
The private `census-gap-20261017/check_decoders.py` additionally checks every accepted instruction's length
against Capstone: 57,646 agreements, zero differences; hashes of the capture corpus and tools are retained in
`decoder-verdict.json`. This is a tooling/metric change; guest sources, matched bodies and parity evidence
are unchanged. The supplement excludes static-only discoveries and reports other modules as unscored debt.

**Optimizer stalls (16 Oct).** START `1308` expanded four MOVSW/MOVSB
palette copies into one optimizer graph: the batch's two normal /O2 compiles
were stopped after more than 40 minutes. Compiling each new routine alone
identified `1308` (still running at the 60-second cutoff; the other twelve
took 1-5 seconds). A small MSVC-noinline palette-copy helper keeps the exact
read/write and pointer-update order. The isolated routine then took 1.36 s,
the complete `/d2cgsummary` compile 119.3 s and both normal /O2 matched objects
about 149 s each. All five seeds, 53 mutations and both real routes were
repeated for the revised body, then all 13 routines passed optimized seeds
and the full gate. Evidence: `D:/f117-gate/p2-batch-b2-accept`.

**Shared matched object (16 Oct).** Both runtime libraries used to compile
`matched.c` separately with identical options. The `f117matched` object target
now compiles it once and includes that object in both static archives; each
archive remains self-contained. Normal /O2 settings and non-Windows PIC are
preserved. Against both previous objects, all 9,372 non-debug sections have
identical code, data, unwind tables, section characteristics and relocation
targets (uninitialized BSS compared by size; linker-removed checksum records
excluded). Evidence: `D:/f117-gate/p2-batch-c-accept/shared-object-verdict.json`.
This removes one compiler invocation; a gate wall-time saving is not established.
In one local build pair the two old compilations took 143.74 / 144.40 s
concurrently, and the single shared compilation took 99.75 s (Ninja timings).

**Independent checks (17 Oct).** Two pending proofs were refreshed in two
isolated lanes (`000FFF`, `FFF000`, BelowNormal), leaving eight CPUs for the
desktop. The refresh took 192.62 s: optimized libraries/current bases,
five seeds each, all 115 mutations repeated, real-route and boot replays, and
source/coverage/mutation audits. Bodies, fixtures and library hashes held.
Presentation N=1
over all routes and boot N=4/9 can also overlap using a frozen runner and separate output directories
(16 + 2 workers within the existing 24-CPU affinity). Gate workers and the
default eight interpreter overlap workers are unchanged.

The private kit writes generated project files only when their bytes change
(15 Oct). An unchanged harness and CMake file therefore keep their timestamps
across mutations; the candidate source still changes and is rebuilt. A
mocked-build probe with real SETUP proof inputs requires identical generated
SHA256s and build commands, retained unchanged timestamps, and a mutation
changing only the candidate source. Evidence:
`D:/f117-gate/kit-write-probe/verification.json`. An actual unchanged
MPS_LOGO `08CC` proof build takes 1.59 s on its two assigned CPUs and Ninja
reports no work (`D:/f117-gate/p2-logo-08cc/build-unchanged.log`). Actual
seeds, mutations and route proofs still run normally; no proof is skipped.
Proof workers can use
separate CPU sets at BelowNormal priority beside the gate, leaving desktop
CPUs free; keep each worker's mutation builds serial when sharing the machine.

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

Mutation catches require a passing unmodified baseline. The kit now refuses
mutations or a verdict unless all five current seed logs show positive
comparisons, zero skipped states for the selected entry, full instruction
coverage and zero mismatches. Logs and mutation results must postdate their
proof inputs. PLAYER `0010` exposed this: its baseline skipped almost every
state, so its apparent mutation catches were invalid and it was rejected.
The guard passes all 23 batch-af proofs; ten focused checks cover seed evidence
and span aliases. Integration now refuses an automatic span for an IP already
registered in another segment, requiring explicit handling and comparison of both;
`D:/f117-gate/translate-parallel-20261020/seed-guards-verdict.json`.

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

**Batch j proof notes (17 Oct).** Four new routines retain five seeds each,
full instruction coverage, 203 caught mutations and positive real-route calls;
20 optimized seed runs repeat their checks. A pre-existing RNG alias bug found
by the rotating gate seed was reproduced on main, corrected and independently
proved with five seeds, 28 mutations, boot and `full_cycle`, then six optimized
generic seed runs. The first failed gate is preserved under
`p2-batch-j-accept/attempt-1-failed`; the complete gate and presentation checks
were rerun after the correction. Inherited coverage for the changed RNG was
removed and replaced with fresh optimized coverage before computing byte credit.
Screening also joins actual byte credit: an unregistered census entry can be
fully covered inside an existing named body, so a FREE lead alone is insufficient.

**Existing-entry expansions (17 Oct).** Keep the old registered address
when replacing a named opening with its whole function. Remove only its old
wrapper/registration, preserve shared helpers used elsewhere, and add full
span/coverage fixtures and a dedicated CTest. Retire that entry's inherited
named-byte coverage and collect fresh optimized evidence. START `58DF` adds
197 bytes this way without adding an address. A census lead such as `5951`
can be an interior arm: expand the enclosing function and its jump-table
dossier before introducing a new override. Switch data are never byte credit.

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
| Matched-routine lockstep (`func_lockstep --states 4000`) | every gate (three seeds); every new matched routine | 8 shards about 3 min 11 s for 653 routines (9 Oct; a single process took 21 min), the same report line for line | **sharded since 9 Oct 2026** (`tools/func_lockstep_par.py`, `--shard K/N`), with the owner's approval: each routine draws from its own stream (the seed, its module and address), so a shard tests what one run does and a routine's states no longer move when others are added. Seeds 0x5EED0F117A, 0xC0FFEE and one from the gated commit's hash. Left: tracking writes instead of copying 1 MB a state |
| Instruction lockstep (`insn_lockstep --states 64`, also `--timing386`) | every gate | runs beside coverage and parity, no wait tail; 5,718,848 states per profile | the 386 check also compares prefetch and held REP state; a second-seed run with `--base-seg 0x9FFF` checks code in VRAM |
| 386 flight engine parity (`stick_response.py --machine machine386 --engine interp/recomp`) | after a profile or translated timing change | 80 s interpreter, 60 s recomp, beside the gate (8 Oct) | all 574 memory/register/output/scanout checkpoints and stick responses identical; separate processes because the runtime is global |
| MSVC matched-code compilation | every matched-code build | about 30 s for the file; the PLAYER decompressor stalled more than 18 min at /O2 (8 Oct) | only `player_unpack` disables MSVC optimisation. Stop an orphan compiler only after checking its source/output paths; otherwise its object stays locked |
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
| Machine-behaviour probe (`fidelity_all.py`) | after a machine-model change | about 2 min with a saved 86Box sheet; `--86box-run` adds 4 min | the 86Box sheet changes only with the model, so it is reused |
| 86Box boot attempts | per BIOS or config change | 2-5 min per capture series | `hello_floppy.py` answers "does the BIOS boot" in one run first |
| Silent 86Box speech waveform (`tools/ref86box/86box-audio.patch`, `B86_AUDIO=PREFIX`) | speech/sound comparison | incremental 86Box build 23 s; two intro runs together 28 s; takeoff-call flight 94 s; our interpreter replay 50 s; WAV render 1.6 s; comparison about 0.6 s (9 Oct) | records the mixer buffers before host playback (format and procedure in [build_86box.md](../tools/ref86box/build_86box.md#silent-speech-waveform-reference)). First require identical frame and port traces with capture off and on. Reuse captures while the binary, input and configuration hashes agree. Generated audio and game data stay outside the repo |

## What the gate is checked against

The gate compares the recompiled engine with this project's own interpreter,
so a model error shows in neither. Fidelity to a real PC is checked
separately, against references: GOG's DOSBox 0.74 (`tools/fidelity.py`, saved
video and flight captures), DOSBox-X for some timing traces, and 86Box running
a 386DX/33 with MS-DOS 5.00, the 386 timing profile's reference
([timing386.md](../tools/ref86box/timing386.md)). Those run per question or
before a release (`pc_parity.py`), not per commit.
