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

Last fully gated runtime baseline: `1337acf`, flight input (VGAME
`0000:1EB0`), 823 matched addresses, census 815 / 82,040 bytes. Gate
`D:/f117-gate/gate-flight-input.log`: 48 tests, all 40 routes, both
instruction profiles and three matched seeds, 1,745 s.

Frame update (VGAME `0000:3ADA`, `vgame_frame_update`) is integrated on top
of it: 824 matched addresses, census 816 functions / 84,648 bytes (P2
49.87%, overall 76.66%). Proof: three seeds zero mismatches with every
instruction reached, isolated tests, actual camera-gradient replay
identical at all 234 checkpoints with no overrun after a room-claim fix
([docs/roadmap.md](docs/roadmap.md), private evidence
`D:/f117-gate/frame-3ada`). Its first gate (`gate-frame-update.log`)
failed `d1_fast_machine`: a fix inside a matched body never ran. Matched
routines with a fix site after their entry now decline while that fix is
on (`fix_inside` in matched.c). Re-gate `D:/f117-gate/gate-fix-inside.log`
(1,746 s): 49 unit tests pass, all 40 routes IDENTICAL (d1_fast_machine
included), 824 matched routines at three seeds, 0 mismatching.
When matching a routine, check `src/fixes/fixes.c` for sites inside it.
The coverage pass still runs for every commit that adds a matched routine:
the translator isolates each matched entry, so the generated code changes
and the skip's generated-code guard declines (the test-entry key fix only
helps commits that add none).

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

## In progress: VGAME 0x5046 (branch `wip-ground-defences-5046`, `e27c270`)

Not in `master`: ungated, so it must not be pushed there until the gate passes.
Named C `vgame_ground_defences` (ground threat reload timers, the two cockpit
damage/stealth bars, the incoming-weapon marker and arc; 583 bytes, 195
instructions, read against the Reimp's `frame_ground_defences` in
`src/core/frame.c`). Lockstep at three seeds (default, `0xC0FFEE`, `0x5046a11`),
4,000 states: zero mismatches, all 195 instructions reached by states the named
routine finished itself (`COVERED` entry, floor 190). Fixtures stub the five
callees that draw or engage (`0x528D`, `0x8F71`, `0x8625`, `0x8719`, far
`114A:033C`); the clamp (`C67A`) and 32-bit divide (`EE9C`) run for real. No
switchable fix lies inside its range. Private draft/dossier:
`D:/f117-gate/nav5046`.

Open before it can merge:
- **The divide-fault hand-back is untested.** The reload `idiv cx` at `0x5122`
  can fault (defence value FFC0h gives a zero divisor; FFBCh with S = 80h
  overflows); the routine stops there and lets the original take it. A break test
  (hand-back removed) still passes lockstep, even with plants for both cases, so
  no fixture reaches it. Find out why (the plants may be overwritten, or the
  served-site gate / timer test not met) and make a state that reaches it, then
  confirm the break test fails.
- Real-driver proof: a route must run it with a positive count (the strike and
  camera routes call it every frame via `3ADA`), all checkpoints identical.
- The full gate (command below), then merge, update roadmap.md and the census.

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
  the private lead list; current progress is in `docs/roadmap.md`, which
  has the full history of integrated-and-gated passes (span-edge/emit, HUD,
  moving objects, camera-image, flight input, frame update). Earlier proofs
  and tested source snapshots remain under `D:/f117-gate/`; history is in
  [docs/repeated-processes.md](docs/repeated-processes.md).
  Next candidates, all reached every flight frame from 3ADA: VGAME
  `0x05852` (2,976 bytes), `0x0683E` (2,565, warnings/lamp timers),
  `0x05046` (583, navigation display); also VGAME `0x02F9D` (2,595, frame
  compose), START `0x04239` (3,035), DSWAP `0x03DF3` (2,924).
  Method (repeated-processes.md): transcript first to validate the harness,
  add the routine to `COVERED`, fixtures until every instruction is
  reached (the skip tally says why states drop), then the named body,
  tested in sections that hand back to the original; check every callee's
  RET form; run `D:/f117-gate/frame-3ada/room_audit.py` on the body; then
  the real route (`route-proof.py`), which is what caught the room-claim
  defect. Private worktree `D:/f117-wt/span-emitter` has the proof build;
  `D:/f117-gate/frame-3ada/swap_body.py` swaps a body into it.
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
