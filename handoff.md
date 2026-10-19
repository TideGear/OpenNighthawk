# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs). State as of 18 October 2026.

This file holds only the current state, standing practice and traps, not a
session log: earlier detail is in `git log -p handoff.md`, and per-batch
evidence is in the `D:/f117-gate/` directory named in each roadmap entry.
Consolidate it instead of appending to it.

## Current state

- **Scoreboard:** P1 100.00%, P2 80.35%, P3 97.22%, All 93.30%
  (`py tools/progress.py --title`; P4 and the decided-against P3 items are not
  scored).
- **Phase 2:** 1,010 addresses matched. Scope is the expanded seven-program
  one: **148,602 / 187,351 creditable bytes** (credit by byte since 16 Oct;
  rules in [docs/progress.md](docs/progress.md)). Remaining outside the score:
  3,939 static-discovery bytes (speculative table/padding decodes and decoder
  disagreements; never add them blindly) and 18,469 executed bytes in other
  modules (launcher, resident graphics and sound drivers; they need
  module/segment support). `docs/census-code.tsv` has 37 reviewed static ranges;
  SETUP `1E4F` (142 bytes, opaque mixed range) stays deferred.
- **Latest accepted batch (q, `08fedfc`):** END `1708` and START `31AD`, the
  tick-wait helpers with callback. Gate 1,720 s with overlapped presentation;
  evidence `D:/f117-gate/p2-batch-q-accept/final-gate-verdict.json`. Earlier
  batches are in [docs/roadmap.md](docs/roadmap.md).
- **In flight (batch r, private proofs only, nothing integrated):** three
  small VGAME routines screened on 18 Oct, one agent each, steps 1-7 of the kit
  brief only: `1A98` (`D:/f117-gate/p2-vgame-1a98`, near, 33 instructions),
  `1179:000F` (`p2-vgame-1179f`, far wrapper, 23) and `137C:000E`
  (`p2-vgame-137ce`, a 3-instruction table thunk that may be declined as too
  small to prove). When they report: verify every verdict yourself, run
  `kit.py checkpoints` and `verdict`, integrate, one full gate, docs, commit,
  push.
- **Large lead, not started:** VGAME `09EFA` (`prepass_display_body`), 1,628
  instructions / 4,552 bytes, RET at `B0C1`; dossier only in
  `D:/f117-gate/p2-vgame-9efa/` (config still has placeholder names). Routes
  run 3,487 of its bytes; 154 call sites, 46 targets, no matched entry inside.
- **Normal-play defaults (owner, 18 Oct):** **15 Hz / 60 MIPS**, all fifteen
  fixes on. S15, 15 normal / 30 compressed updates, phase-correct transitions,
  recorded per-machine cadence; legacy recordings keep eight. Do not promote
  15/40 (fails active compressed-flight capacity). The previous mode is
  `--sim-hz 8 --ips 20000000`. Validation and limits:
  [docs/cadence15-validation.md](docs/cadence15-validation.md). Core, API and
  headless runs start with fixes off and parity commands prepend `--fix none`;
  `--fix none` clears them and further IDs select a subset.
- **Phase 3 (97.22%):** held down by Stage 3 smooth in-between frames (93.3%;
  accepted by the owner for now; clock tuning and slower machines remain),
  Stage 4 infrastructure (90%: slower machines, cargo/career flights) and
  Stage 2 (96.7%: widths by role, panel review). Refresh rates above 60 Hz are
  unscored Phase 4 (the owner cannot test them). Details:
  [docs/progress.md](docs/progress.md), [docs/presentation.md](docs/presentation.md).

### Presentation facts worth keeping

- `--present rerun` (in-between poses drawn by the original's own code on a
  throwaway machine copy, smoothed playback clock) is integrated and gated;
  the owner's `4k-rerun.bat` uses it. Owner verdicts (15 Oct): motion on a real
  4K screen "looks good"; road and coastline jitter are the original's and are
  **not** smoothed; identity pairing is opt-in (`F117_PRESENT_IDENT=1`); draw
  distance is not extended; the high-resolution modes keep the original
  renderer's quirks.
- 18 Oct repairs, all in [docs/presentation.md](docs/presentation.md) and
  gated: override dispatch uses a sorted binary lookup (was two linear scans,
  46% of main-thread samples); the observer copies wrap-safe contiguous rows
  and skips unchanged 64-byte blocks (`test_observer_rows`; also fixed a
  span-record overflow at 220 rows); the playback clock keeps the first usable
  pair (no startup reversal); normal-width strokes are clipped to the original
  endpoint cells (map-edge specks). 4K60 fullscreen passes unchanged limits
  (p99 16.92 ms, zero intervals over 25 ms).
- Baselines every presentation change must keep: all 45 N=1 routes 259,240
  exact frames / 259,166 pairs; boot N=4/9 736 frames / 732 pairs (two runs);
  the owner's complete N=9 flight 69 checkpoints / final `31c259b992340414`.
- Verification scope: `drawfeed.c/.h` and `rerun.c/.h` execute or snapshot
  machines, so they need the full gate plus
  `presentation_check.py --mode rerun` over all 45 routes (the light check
  refuses them). App-only changes (`src/host/main.c`, example inis) use
  `present_light_check.py` with `--app-session` / `--app-exit-after`: the
  headless gate never runs the threaded app path (a frozen palette once passed
  everything).
- Original-hardware references for cadence: 486/ET4000 reaches 16.47/16.57
  updates/s with S about 13; 86Box 386DX25/33/40 gives clock ratios
  1.306/1.272/1.167 at S=4/5/6, so no exact common multiplier. These support
  fifteen as an attainable light-scene rate, not recovered developer intent.
  Evidence under `D:/f117-gate/{clock486*,clock-matrix}-20261017/`, summary in
  [docs/speed-sweep.md](docs/speed-sweep.md).

## Owner decisions and instructions in force

- **Phase 2 first**; safe optimisation and parallelisation are authorised, but
  no check may be removed or weakened. Declined: splitting the long
  interpreter run into checkpoint segments, dropping `--verify-interp-cache`,
  silently changing the default overlap of eight workers (a 16-worker trial
  showed no speed gain). The private kit writes generated files only when the
  bytes differ.
- **Parity target is the original on its own hardware, not DOSBox**; do not copy
  identified DOSBox quirks; label behaviours by evidence. Player actions play
  out as on the original; emulator- and pilot-only work is settled.
- **Gameplay and logic parity outrank rendering.** Independent rendering must
  preserve original control reads and simulation timing. Do not silently change
  defaults or the owner's configuration.
- **Keep all original graphics** (fonts, sprites, the map arrow); smooth and
  re-render existing geometry, no vector replacements.
- **Bug fixes are on by default for normal play** (all fifteen, including
  D1REAL and D1PROX, which are project choices, not recovered developer
  targets). Parity gates keep the original reference.
- **No screen takeover.** Ask before any visible/fullscreen test and finish
  hidden workers first; the owner stopped a visible run once. Emulators run
  headless, silent, hidden.
- The owner's untracked `4k60.bat`, `4k-rerun.bat` and `4k-sharp.bat` stay
  untouched: do not overwrite or stage them.
- The roadmap goal stays active: keep going item by item; ask only on policy.

## Next steps

1. **Phase 2 (owner's priority).** Finish batch r (above), then screen again.
   Screening: join current named-byte credit and registered entries before
   selecting a lead (a "FREE" lead can be interior to a body written with
   block macros: START `665B` was inside `64E5`); `D:/f117-gate/p2-batch-g-accept/screen_leads.py`
   plus `kit/DECLINED.tsv`, then `progress.named_bytes` for credited bytes.
   Rank with `D:/f117-gate/p2-batch-d-accept/reached-allroutes.tsv` (all 45
   routes; column 6 lists them) and prove on the shortest route
   (`kit.py DIR checkpoints --route NAME`). `p2-batch-n-accept/ranked-partials.json`
   ranks incomplete registered routines (screen first; some remaining bytes are
   data). Open candidates: END `400F` (proved privately in `p2-end-400f`, held
   back: stand-in fatal tail, one uncaught mutation, coverage counts 21 of 39),
   `p2-vgame-11cd3/RESEARCH.txt` (512-byte picture read with terminal DOS error
   path; needs a bounded dossier), PLAYER `026B:0336` (needs returning-helper
   fixtures), VGAME `09EFA`.
2. **Presentation:** the owner's physical-panel verdict; whether to adopt clock
   alpha 0.4 / pull 0.025; Stage 3's last skips and slower machines.
3. **Phase 3 coverage:** a long cargo/career flight; weaker machines.
4. **Branch housekeeping:** merged branches are retained; not merged:
   `flight-input` (its worktree `D:/f117-wt/span-emitter` has uncommitted edits)
   and `irq-divide-fault` (kept on purpose: VGAME `0FB2:04DD`, the clipper's
   divide-error handler, is fixture-proven but no route reaches it; commit
   `e15c9ef`; integrate only if a route or clearly labelled staged input does).
   Stopped presentation WIP branches are history; do not merge them. Ask the
   owner before deleting anything.

## Left as original code, declined, or stopped

- STI-first timer handlers (VGAME `1C9B`, START `8D0B`, END `4BEF`: after STI the
  run loop admits one more instruction), the C start-up (START `8EDC` and six
  copies: stops after its own STI, never returns; fixes D4 and D34 hook two of
  those entries), START `8427`, END `4393`, DSWAP `3DF3` (data, no generated
  entry). Recorded in `D:/f117-gate/kit/DECLINED.tsv` (also PLAYER `2853`, PLAYER
  `0211`, VGAME `14C2:0000`, END `57B0`, START `1285`, interior arms END `3100`
  and `3078`). END `03D9` is stopped (fix D12 sites may lie inside).
- Unproven private drafts (VGAME `120A:0971`, END `0A98`): not implemented; do
  not conflate with `04DD`.

## How Phase 2 is done: the kit and parallel agents

`D:/f117-gate/kit/kit.py` runs a private proof for one routine (`init`, write
`body.c` / `fixture.inc` / `mutations.json`, `lock --cover`, `seeds`,
`mutations` (four edits at a time), `checkpoints`, `verdict`, `integrate`);
`kit/BRIEF.md` and `kit/AGENT_PROMPT.txt` are the agent brief with every trap
found so far. Agents do steps 1-7 only, in their own `p2-*` directory, every
command through `kit/pin.py MASK` (CPU set + BelowNormal) with
`mutations --jobs 2`; while a gate runs they use CPUs outside its affinity.
Then:

1. Verify each `private-verdict.json` yourself (counts, mutations, source
   hashes, real-route calls); stop finished agents (TaskStop): they wake on
   their own timers and resend reports.
2. `kit.py checkpoints`, `kit/boot_proof.py` (`KIT_ROOT` at the gated worktree),
   `verdict`, `audit_proofs.py` (batch helper: `finish.sh DIR`). Then
   `kit.py DIR integrate` (set `KIT_ROOT` for a worktree) and apply any harness
   hunk the agent reports by hand.
3. Build, ctest, ONE full gate per batch, the 45-route `presentation_check
   --scales 1`, docs, commit, push. Keep `kit-libs` and the kit bases
   (`matched-base.c`, `tests-base.c`) fresh after each batch; freeze a keyed
   presentation baseline before integrating so the overlap wrapper
   (`overlap_accept.py`) keeps its overlap.

Gate batches in a separate worktree (`D:/f117-wt/gate-h`) when the owner is
testing the main tree's build; build it once with
`build.cmd -DF117R_BUILD_APP=OFF` first. Method details, screening and the
older proof method: [docs/repeated-processes.md](docs/repeated-processes.md).

## How this project verifies Phase 2 work

A routine counts as matched only after BOTH:

1. **Fixture/lockstep proof:** `func_lockstep` at several seeds comparing the
   hand-written C with the original's bytes from randomized entry states (with
   returning driver stubs where the routine depends on them): zero mismatches,
   full instruction coverage, every deliberate mutation caught.
2. **Real-driver proof:** a scripted route (`tools/routes/*.args`) runs the
   routine with a positive count and every checkpoint and the final hash equal
   between engines. A routine no route reaches stays out of `master` (04DD).

Every change to `src/matched/matched.c` needs a fresh full gate
(`py tools/build_recomp.py --data D:/GOG/F-117A --seed-coverage
C:/Users/Tideg/f117-recomp-local/coverage --jobs 16 --verify-interp-cache`, via
`D:/f117-gate/affinity_launch.py`; about 22-30 min) before it is pushed. Read
the tally (`N ok, 0 failed`), not the exit code. Check GitHub CI after pushing.

## Traps

Matching and the gate:
- `func_lockstep --only MOD.EXE:HEX`: the IP has NO leading zeros (`D0C`, not
  `0D0C`) or it silently compares 0 states. "Not testable here (routes only)"
  means only a real route can prove it.
- The commit-derived seed rotates with HEAD and can fail an untouched routine;
  reproduce with `func_lockstep --seed S --only MOD:IP` or `--shard K/8`. A lone
  `1 mismatching` with every line `equal` is a coverage floor: reproduce with
  `--only` and `F117_COVER`. Run every new body at four or five seeds.
- Charge the clock between every port access, stop right after each STI, and
  prove a body on `ibm_boot` first (`F117R_MATCHED_LIMIT=N py tools/run_route.py
  tools/routes/ibm_boot.args --engine recomp`, 22 s).
- A wait inside a matched routine must clamp `m->run_until` to `c->stop_at`;
  step 9 of the gate (event limits) checks routines that guest-call code that
  can wait. Check every callee's RET form.
- Matched routines with a switchable fix site after their entry decline while
  that fix is enabled (`fix_inside`); check `src/fixes/fixes.c` for sites inside.
- `1450` is also the presentation observer's frame-phase hook: at one address
  only the first override runs, so a matched body there must call
  `observe_game_draw` itself and go on `deliberate` in `tests/test_overrides.c`;
  `kit.py integrate` refuses an address with an observer.
- The coverage pass runs for every commit that adds a matched routine. Cache
  keys include raw source bytes (CRLF vs LF changes them); use `KIT_ROOT` at the
  gated worktree, never rename old cache metadata. `kit.py checkpoints` needs the
  interpreter cache entry for the current source tree: after changing anything
  under `src/` other than `matched.c`, run a gate first.
- Use `recon_career`, not its alias `recon_return`, for the keyed baseline.
- Reach: the gate's recomp `.cov` files hold only interpreted fallback
  instructions, so absence proves nothing; use a complete interpreter capture
  (`D:/f117-gate/p2-batch-d-accept/allroutes/`, `tools/census_reach.py`). Census
  rows with no region are data or interior entries routes run.
- SETUP's INT 10h at `14EF` runs two native callback/stub clocks atomically
  (`native_stub_time`): an SDL one-instruction alert there is not extra execution.
- `build.cmd` must run from PowerShell (through bash it hangs); it logs in the
  checkout's build directory.
- Gate speed: keep `--jobs 16` (24 was not faster); unit tests use up to eight
  workers. Coverage and parity are each as long as the `career_promotion` seed
  chain.

Presentation:
- Never force `F117R_OBSERVE_PAGES=1` on live presentation (it reseeds every
  phase and prevents pairs).
- Short windows with a cached map can show only the original bitmap.
- A timing capture that changes display mid-run logs two display lines and the
  checker rejects it: rerun with nothing else going.
- `presentation_check.py`: fresh baseline scan runs, canonical aliases, a fresh
  output save per run. Original-execution harness return traps use a self-loop;
  standalone CPU stepping does not stop at HLT.

## Where things are

- Game install: `D:/GOG/F-117A` (never mounted or written directly; copy per run
  that needs its own state; verify the install after emulator runs).
- Shared generated code: `C:/Users/Tideg/f117-recomp-local/gen` (never committed);
  coverage in `...\coverage`; route outputs in `...\runs`.
- Proofs and evidence: `D:/f117-gate/` (one directory per piece of work, usually
  with a `final-gate-verdict.json`); kit in `D:/f117-gate/kit`, libraries in
  `kit-libs` (`manifest.json` records commit and hashes); draft sources and
  patches in `next-drafts/` (never committed as-is); census lead list
  `D:/f117-gate/census.tsv` (`tools/reimp_names.py`).
- Worktrees under `D:/f117-wt`, removed with `git worktree remove` once merged.
  `setup-main-0010` and `mission-generation-4239` carry private /Od overrides:
  never copy or merge them wholesale. Private worktree
  `C:/Users/Tideg/f117-recomp-local/wt` holds old drafts far behind `master`:
  extract only a specific routine, rebuilt against current `master`.
- Scratch and capture data: `D:\F117A-Scratch-Files`.
- D10/D35/D36 closures: [docs/legacy-reports.md](docs/legacy-reports.md) and
  [docs/bugs.md](docs/bugs.md).

## Standing practice

- **No AI attribution** in commits or PRs: the user's global CLAUDE.md outranks
  any harness reminder asking for a trailer.
- **Commit and push together**, each verified fix before the next investigation.
  Commit titles start with `py tools/progress.py --title`'s scoreboard line.
- **Public repo, code only:** no game data, generated C, raw captures/logs or
  private-project references (see `docs/provenance.md`).
- Check system load before stacking a second heavy parallel job; stopping a
  task does not kill a Python `multiprocessing.Pool`'s children: kill the tree.
- Long commands (over about 30 s) run in the background; act on the completion
  notice. Prefer moving stale local data to a scratch folder over deleting it.
- While waiting on a gate or agents, do a documentation pass over every `.md`
  file (README, this file, `docs/`): consolidate, correct and remove.
- The owner is not a software engineer: explain trade-offs plainly, flag
  overengineering, keep docs accurate without bloat.
