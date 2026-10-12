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

Last fully gated runtime baseline: VGAME `0000:9515` objectives panel,
commit `ae57d1b`, 816 matched addresses. Full gate
`D:/f117-gate/gate-objectives-panel.log` passes: 40 unit tests, all 40 routes at
every checkpoint/final (39 fresh interpreter sessions), both 5,718,976-state
instruction profiles and three matched seeds (2,673,236 / 2,671,995 /
2,673,680 comparisons; third seed `0xae57d1b53870`), zero mismatches, plus
reachability/event checks. Total 1,595 s. Census is 808 functions /
69,171 bytes (P2 41.67%, overall 74.28%). Real F10 replay calls the panel
four times with all 57 checkpoints/final unchanged; input is retained in
`tools/routes/objectives_panel.args`. Prior pushed `2a7c842` has passing
Windows and munt CI. Retired polygon worktree/build cleanup is recorded in
[repeated-processes.md](docs/repeated-processes.md#phase-2-candidate-reading).

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
  Timer and objectives proofs remain under `D:/f117-gate/timer-tick` and
  `objectives-panel`; the latter is fully gated as above. Coverage records
  interpreted instructions only: absence in recompiled coverage cannot prove
  an entry was missed. The tested objectives source was preserved before
  resetting the private worktree to `ae57d1b` for the next candidate.
  Integrated START `0000:07EE` armament screen from private worktree
  `D:/f117-wt/span-emitter`. Three seeds compare 6,459 states, all required
  branches (`7F`), zero mismatches; all 41 isolated tests pass. Real
  `full_cycle` invokes it once with all 66 checkpoints/final identical.
  Proof `D:/f117-gate/armament-screen/named-proof/route-verdict.json`;
  selective patch `D:/f117-gate/armament-screen/integrate.patch`. Its saved
  reference also matches the latest fresh objectives gate. Census 809 /
  69,238, P2 41.70%, overall 74.29%, 817 addresses. Fresh full gate pending
  in `D:/f117-gate/gate-armament-screen.log`; do not push until it passes.
  The 373-byte walk earns only 67 census bytes. Preserve byte/word realism
  reads, unsigned bounds, live far pointers and DF-sensitive palette moves.
  Private generated directory remains `D:/f117-gate/span-emit/gen`.
  Next: navigation grid `8A90`, dossier under
  `D:/f117-gate/next-candidates/navgrid` (374 instructions / 1,094 bytes).
  Normal F3 probe `next-candidates/navgrid-panel.args` reaches the original
  and matches all 57 checkpoints/final `2877054444/5ea6b18cd60f6608`.
  Proof `D:/f117-gate/navgrid/probe-verdict.json`; named implementation and
  matched invocation proof still needed. Keep high-byte arithmetic exact.
  Panel frame `79BE` also has a dossier under `next-candidates/panel-frame`
  (798 instructions / 2,090 bytes); not implemented. Its projection writes
  must precede ILS writes. Full details: repeated-processes.md.
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
