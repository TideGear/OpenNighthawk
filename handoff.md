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

Last fully gated runtime baseline: VGAME `1058:0016` HUD pass,
commit `de1189e`, 820 matched addresses. Full gate
`D:/f117-gate/gate-hud-pass.log` passes: 44 unit tests, all 41 routes at
every checkpoint/final (40 fresh interpreter sessions), both 5,718,976-state
instruction profiles and three matched seeds (2,687,533 / 2,685,929 / 2,685,258
comparisons; third seed `0xde1189e2aeae`), zero mismatches, plus
reachability/event checks. Total 1,596 s. Census is 812 functions /
74,910 bytes (P2 44.71%, overall 75.16%). Real F3 replay calls the named HUD
550 times with all 57 checkpoints/final unchanged. Retained input:
`tools/routes/navigation_grid.args`. Pushed panel baseline `cb50ccf`
has passing Windows and munt CI.

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
  HUD is integrated and fully gated as above. Preserve its original
  byte-DIV fault frames, repeated single-bit shifts and call event guards.
  Next moving-object pass `0000:983E`: dossier under
  `next-candidates/moving-objects`, 564 instructions / 1,724 bytes, 38 calls.
  The private worktree `D:/f117-wt/span-emitter` was reset to `de1189e`
  after all three tested HUD snapshots were verified and preserved.
  It now holds three moving-object changes, backed up in
  `D:/f117-gate/moving-objects`: named source, tested snapshots, selective
  patch, scripts and `targeted-verdict.json`. Three seeds compare 2,710 / 2,707 / 2,654
  states, zero mismatches; all 32 tracked paths and 45 isolated tests pass.
  Actual-driver F3 replay invokes the named pass 550 times with
  all 57 checkpoints/final equal. The immutable interpreter reference also
  matches the fresh HUD gate (`fresh-reference-crosscheck.json`).
  Integrate only after pushing this HUD baseline, refresh census and run
  another full gate. Preserve target cycling before drawing, live guest
  frame slots, signed comparisons, byte flag updates and all 38 calls.
  Next camera-image dossier: `next-candidates/camimage`, VGAME `BCEF`,
  658 instructions / 1,863 bytes / 49 calls. Partial handwritten source
  `D:/f117-gate/camimage/named-camera-image.c` is outside the worktree and
  untested; it covers entry/class/page setup, weapon pose, range/trig,
  forward-camera setup, horizon/ground-class selection and world-position
  selection through `C043`. Viewport/rendering remains an explicit
  placeholder; complete before insertion.
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
