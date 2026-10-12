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

Last fully gated runtime baseline: VGAME `0000:983E` moving-object pass,
commit `b811414`, 821 matched addresses. Full gate
`D:/f117-gate/gate-moving-objects.log` passes: 45 unit tests, all 41 routes at
every checkpoint/final (40 fresh interpreter sessions), both 5,718,976-state
instruction profiles and three matched seeds (2,690,243 / 2,688,636 / 2,689,943
comparisons; third seed `0xb8114142076c`), zero mismatches, plus
reachability/event checks. Total 1,799 s. Census is 813 functions /
76,634 bytes (P2 45.62%, overall 75.42%). Real F3 replay calls the named pass
550 times with all 57 checkpoints/final unchanged. Retained input:
`tools/routes/navigation_grid.args`. Prior HUD baseline/evidence is pushed
as `7fb10be`.

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

- **Gate speed experiment completed; retain `--jobs 16`.** The full
  24-worker gate at `7df74bf` passes with identical inputs and verdicts,
  but coverage/parity take 557/830 s against the latest same-code
  16-worker baseline's 538/822 s. Its 1,455 s total versus 1,546 s comes
  from the faster incremental build, not faster routes. The earlier
  1,466 s baseline had 809 addresses; it is not the direct comparator.
  Proof and limits: [docs/repeated-processes.md](docs/repeated-processes.md).
  `D:/f117-gate/affinity_launch.py` now sets and verifies `BelowNormal`
  priority as well as affinity; workers were verified to inherit both.
- Continue Phase 2 (naming/matching routines) — `tools/reimp_names.py` has
  the private lead list; current progress is in `docs/roadmap.md`.
  `matched_draft.py --dossier` also writes private static evidence beside a
  draft; its five synthetic regressions pass and the polygon draft is
  byte-identical. The clipped edge and shared emitters are integrated and
  fully gated; private proofs/patches remain in `D:/f117-gate/span-edge` and
  `D:/f117-gate/span-emit`.
  Earlier proofs and tested source snapshots remain under `D:/f117-gate/`;
  history is in [docs/repeated-processes.md](docs/repeated-processes.md).
  HUD is integrated and fully gated. Preserve its original
  byte-DIV fault frames, repeated single-bit shifts and call event guards.
  Moving objects are integrated and fully gated as above. Preserve target
  cycling before drawing, live frame slots, signed comparisons, byte flag
  updates and all 38 calls. Tested snapshots and verdicts remain in
  `D:/f117-gate/moving-objects`.
  Next camera-image pass `0000:BCEF`: 658 instructions / 1,863 bytes,
  49 calls. The private worktree `D:/f117-wt/span-emitter` was reset to
  `b811414` only after all three moving-object snapshots were verified
  against private and main sources. It now holds three camera changes,
  backed up in `D:/f117-gate/camimage`: complete named source, tested
  snapshots, selective patch, scripts and `targeted-verdict.json`.
  Three seeds compare 4,907 / 4,902 / 4,926 states, zero mismatches; all 32 tracked
  paths per seed and 46 isolated tests pass (41 core plus five host tests).
  145 divide-fault comparisons deliberately force a helper return of
  FFFF on both sides; other states execute real range/trig math. This proves
  the caller's fault frame, not a normal-game fault claim.
  Actual camera-gradient replay invokes the named pass 7,880 times with
  all 234 checkpoints/final identical. The saved interpreter reference
  matches the fresh moving-object gate (`fresh-reference-crosscheck.json`).
  Integrate only after pushing this moving-object baseline, refresh census
  and require another full gate. Preserve backgrounds before model/terrain,
  live guest locals, nested render-global restoration and call guards.
  Next dossier: `next-candidates/flight-input`, VGAME `0000:1EB0`,
  1,274 instructions / 3,543 bytes / 121 calls. Partial external named
  draft `D:/f117-gate/flight-input/named-flight-input.c` covers initialization
  and keyboard collection, command dispatch, modal quit/blank pauses and raw
  axis scaling, nonlinear controls, altitude hold, trim and low-speed recovery
  through `23B4`; uninserted and untested. Ejection dynamics, thrust and
  movement remain.
- 04DD above, if a real route to it is ever found.

## How this project verifies Phase 2 work (read before matching a new routine)

A routine only counts as matched once it passes BOTH:
1. **Fixture/lockstep proof**: `func_lockstep` at three seeds, comparing the
   hand-written C against the original's own bytes from randomized (and for
   routines with real argument/driver dependencies, returning-driver-thunk)
   entry states. Zero mismatches required.
2. **Real-driver proof**: an actual scripted route (`tools/routes/*.args`, sometimes replaying `.input`)
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
  risk; END `0A98`): none
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
