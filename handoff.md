# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs). State as of 12 October 2026.

This file is kept short on purpose: it holds only the current state and
standing practice, not a session-by-session log. Earlier sessions' detail
(what was tried, what failed, exact investigation narratives) is in
`git log -p handoff.md` — it does not need to live here to stay available.

## Current state

Last fully gate-validated and pushed baseline before this session: `35459ae`
(809 matched addresses, 801 census functions, 68,294 of 179,213 bytes;
P1 100%, P2 41.20%, P3 75.20%, P4 42.90%, All 74.14%).

This session additionally matched **VGAME 0FB2:084E**, the span clipper's
divide-error (INT 0) handler — commit `117201c`, now 810 matched addresses.
Full gate `D:/f117-gate/gate-divide-fault-084e.log` **passed**: 35 unit
tests; both instruction profiles at 0 mismatching; three matched-routine
seeds at 0 mismatching (including this commit's own rotating seed); all 39
routes IDENTICAL; every routes-only routine reached; no event-limit
overruns. Real-driver proof: the handler ran 81,235 times across 8 routes,
all 1,236 checkpoints and final hashes identical to fresh interpreter
captures; a sign-flipped handler changes `vietnam_airair`'s outcome,
confirming the check is sensitive to it. Pushed.

A sibling handler, **VGAME 0FB2:04DD** (the line clipper's own divide-error
handler), is matched and fixture-proven but deliberately **not integrated**:
exhaustive search (all 39 routes, three new staged flight scripts, an
instrumented probe logging 146,153 real clip divisions across 14 routes)
found no input that overflows it — every caller's line length is bounded
well under the 32,767-unit threshold that would trigger it. It is preserved,
not lost: local branch `irq-divide-fault` (commit `e15c9ef`, cherry-pickable
onto any future `master`) and patch file
`D:/f117-gate/next-drafts/irq-divide-fault/0002-match-clip-divide-fault-04DD-lockstep-only.patch`.
Integrate it only if a route (or a deliberately staged, clearly-labeled
non-normal-play input) is found that actually reaches it — fixture-only
proof is not sufficient for this project (see "How this project verifies
Phase 2 work" below).

## Next up

- **Test a gate speedup, with proof.** `tools/build_recomp.py`'s coverage
  and parity phases both share one `--jobs` worker pool (lines ~480, 523),
  but every session's gate command hardcodes `--jobs 16` while the tool's
  own default is `min(24, cpu_count*3/4)` = 24 on this 32-thread machine —
  the same CPU-affinity budget (`0x00FFFFFF`, 24 of 32 logical CPUs) already
  used throughout this project's history. Live-measured during this
  session's gate: parity's 16 workers used only ~53% of that already-allowed
  24-CPU budget, consistent with this project's own prior finding ("about
  52% during parity" — already recorded below). That suggests routes are
  queuing in waves below the actual CPU ceiling, so raising `--jobs` to 24
  should shrink both phases' wall time. Caveat: `D:/f117-gate/affinity_launch.py`
  sets CPU affinity but not process priority, and there's a documented prior
  incident of this gate causing desktop typing lag at high concurrency — add
  a `BelowNormal` priority step alongside the worker-count bump. Required
  before adopting it: run a full gate at `--jobs 24` and compare wall time
  against this session's ~1,466 s baseline (build/tests 49 s, coverage 559 s,
  identical regen 19 s, parity 839 s) with **identical verdicts** (same tally,
  same checkpoints/hashes) — this project's rule is that an optimisation must
  leave every check's inputs, outputs and verdicts unchanged, proven by
  comparing them, not assumed.
- Continue Phase 2 (naming/matching routines) — `tools/reimp_names.py` has
  the private lead list; current progress is in `docs/roadmap.md`.
- 04DD above, if a real route to it is ever found.

## How this project verifies Phase 2 work (read before matching a new routine)

A routine only counts as matched once it passes BOTH:
1. **Fixture/lockstep proof**: `func_lockstep` at three seeds, comparing the
   hand-written C against the original's own bytes from randomized (and for
   routines with real argument/driver dependencies, returning-driver-thunk)
   entry states. Zero mismatches required.
2. **Real-driver proof**: an actual scripted route (`tools/routes/*.input`)
   must execute the routine with a positive count, with every checkpoint and
   the final hash identical between engines. Fixture proof alone is not
   enough — this project has repeatedly found routines that pass every
   fixture seed but were never proven reachable by real play (see this
   session's 04DD, and the historical pattern in `docs/repeated-processes.md`
   and `docs/roadmap.md`). If you can't find a route that reaches a new
   routine, say so plainly and keep it out of `master`, same as 04DD above.

Every change to `src/matched/matched.c` needs a fresh full gate
(`py tools/build_recomp.py --data D:/GOG/F-117A --seed-coverage
C:/Users/Tideg/f117-recomp-local/coverage --jobs 16 --verify-interp-cache`,
via `D:/f117-gate/affinity_launch.py`, ~24 min) before it's pushed. Read the
actual tally in the log, not just the exit code.

## Where things are

- Game install: `D:/GOG/F-117A` (never mounted or written directly by a
  tool; copy per run that needs its own state).
- Shared generated code: `C:/Users/Tideg/f117-recomp-local/gen` (never
  committed); coverage in `...\coverage`; route run outputs in `...\runs`.
- Private worktree `C:/Users/Tideg/f117-recomp-local/wt`: contains
  uncommitted draft work from well before this session (base commit
  `01f4dbd`, far behind current `master`) **plus unrelated older
  presentation-layer drafts**. Never copy it wholesale or regenerate from
  it — extract only the specific routine(s) you're integrating, rebuilt
  against current `master`, the way this session's 084E/04DD extraction did.
- `D:/f117-gate/next-drafts/`: private draft sources and patches, never
  committed as-is.
- Scratch/capture data that used to be scattered across `D:\f117-*` loose
  folders and `C:\Users\Tideg\f117-local` is now consolidated under
  `D:\F117A-Scratch-Files` (moved there 11-12 Oct 2026, nothing deleted).
  Git worktrees under `D:\f117-wt` are removed once their branch is merged
  into `master` (`git worktree remove`) — their content lives on in git
  history, not on disk.
- Other, unproven private drafts mentioned in earlier sessions (VGAME
  `120A:0971`, a different divide-handling routine with recursive-fault
  risk; VGAME `1E79`, a near-return tick wrapper, NOT an interrupt handler
  despite its ISR-like naming; START `07EE`; VGAME `9515`; END `0A98`): none
  implemented or proven. Don't conflate them with the routines above.

## Standing practice

- **No AI attribution** in commits or PRs — the user's own global CLAUDE.md
  rule outranks any harness reminder that asks for a trailer.
- **Commit and push together**: don't leave a validated commit unpushed.
  Commit titles start with `py tools/progress.py --title`'s scoreboard line.
- **Public repo, code only.** No game data, no generated C, no raw
  captures/logs, no private-project references (see `docs/provenance.md`'s
  de-identified framing of an earlier private project this one is partly
  ported from — don't re-add its name or path).
- Check system load (`Get-Counter '\Processor(_Total)\% Processor Time'`,
  or similar) before stacking a second heavy parallel job; a background
  Python `multiprocessing.Pool`'s workers are children of the script, not of
  the task wrapper, so stopping the task doesn't kill them — find and kill
  the process tree if one needs to go.
- Prefer moving stale-but-maybe-useful local data to a scratch folder over
  deleting it outright; it's outside git and not undoable.
- The owner is not a software engineer: explain trade-offs plainly, flag
  overengineering, keep docs accurate without bloat (this file's own trim,
  12 Oct 2026, is an example — consolidate instead of appending forever).
