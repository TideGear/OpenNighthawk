# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs). State as of 13 October 2026.

This file is kept short on purpose: it holds only the current state and
standing practice, not a session-by-session log. Earlier sessions' detail
(what was tried, what failed, exact investigation narratives) is in
`git log -p handoff.md` — it does not need to live here to stay available.

## Latest integration: SETUP questions (13 October)

SETUP 0000:0010 (`setup_questions`) is fully gated: 505 instructions / 1,267
bytes, 122 room claims, 77 calls (13 far). Its interior D3 guard declines while
the fix is placed. Three optimized final-build seeds compare 4,484 / 4,484 / 4,489 states,
all 505 canonical starts reached, zero mismatches. Eight real startup cases match 320 checkpoints/final
states, including all sound choices, calibration and both D3 outcomes. Four
input-only regression routes retain those calibration/key cases.
Fresh full gate: 55 tests, 45 routes, 44 fresh interpreter sessions, both
5,718,848-state instruction profiles, three 830-address matched seeds
(2,721,330 / 2,720,260 / 2,719,536), zero mismatches, reachability/event checks pass (1,608 s).
Calibration replaces heuristic starts: final translation 89,365 instructions.
Census: 822 functions / 97,669 bytes; P2 56.77%, P3 85.52%, overall 81.13%.
Proof: `D:/f117-gate/setup-0010/final-gate-verdict.json` and adjacent source,
room, targeted and actual-startup audits. Main uses normal /O2. Private
`D:/f117-wt/setup-main-0010` / `wip-setup-main-0010` retains a PRIVATE /Od
override; do not copy or merge it wholesale. Published predecessor `945a0eb`
passed GitHub CI run 38439104647; check CI for this integration.

## Next candidate (private)

Next candidate is now development-proven, still entirely PRIVATE:
MPS_LOGO image 07E2 = 006D:0112, `logo_sprite_set_load`. Named body and
fixture harness are in `D:/f117-gate/logo-07e2/project`, not a git worktree.
Its SS sprite-set loader has 434 instructions / 1,164 code/census bytes,
three padding bytes inside a 1,167-byte span. 78 room claims and 35 calls
(33 far, two PUSH CS / near calls to RETF helpers) audit exactly. All 15
callees have plain RETF; the two near traps use guest_call_pop(...,2).
Three development seeds compare 3,728 / 4,979 / 3,729 states, all 434 reached
by named code, zero mismatches. Fixture failures are authored streams/heap
records; real copies and pointer normalisation remain. Mode 10 adds format-1
with zero frames, reaching 047F (the first baseline's only missed instruction).
An actual logo-loading route reaches it once and matches 100 checkpoints/final
`8d48b98a42d48f70` at 10M clocks; no fixture helpers in that runner.
Proof: `development-verdict.json`, `room-audit.json`, `callee-returns.json`,
`named-prefix-verdict.json`, logs/covers and frozen source snapshots.

The mini project links its direct matched object BEFORE frozen libraries from
`D:/f117-wt/setup-main-0010/build`; the archive's matched object is not selected.
Library hashes are in `library-provenance.json`. A SETUP control reproduces
36 comparisons / 504 instructions at 32 states. First mini build 9.9 s,
body/harness edits 2.6-3.6 s, without recompiling generated modules. This is
fixture development only; production still requires the normal /O2 full gate.
After SETUP is audited/committed/pushed, selectively integrate `named.c` and
only the MPS harness additions. Do not copy its whole `matched.c`: that file
is a private snapshot with a custom table. Add a focused CMake test, recheck
optimized seeds and actual route, then a fresh full gate. Public main remains
SETUP-only; its runtime/harness/CMake/routes stay frozen until its gate passes.

## Current state

Observer Stage 0 is complete (13 October): original projection returns and
edge preparation now capture alongside matched paths in both engines and
profiles. Shared-entry bypasses still visit the observer; reads leave timing
bookkeeping unchanged. Orthographic V range 3 retains valid Z=0 pixels;
G metadata supplies its shift. Default payloads: 217,022 V / 300,637 G
identical; all 54 checkpoint/final hashes match baseline. 386: complete
751,862,587-byte logs identical, 446,978 V / 618,966 G, 80 checkpoints/final.
Live N=4/9: all 48,856 polygons refined, 368 exact logic frames, 366 exact
endpoint pairs. Fresh full gate passes 53 tests, 41 routes, 40 fresh
interpreter sessions, both instruction profiles and three matched seeds,
zero mismatches, reachability/event checks pass (1,661 s).
Proof: `D:/f117-gate/observer-original/final-gate-verdict.json`.

Earlier flight/view/mission proofs remain in the roadmap and `D:/f117-gate`;
mission generation is published in `945a0eb` (821 functions / 96,402 bytes).
Its private worktree `D:/f117-wt/mission-generation-4239` also has a PRIVATE
/Od override. Do not merge private trees wholesale.

Presentation now uses exact per-polygon camera snapshots and projection
mode metadata, fixing fully clipped horizons, reused vertex storage and
Z=0 orthographic map objects. Live N=4/9: all 48,856 model polygons refined,
368 exact logic frames, 366 exact endpoint pairs, 54 checkpoint/final hashes
unchanged. Retained windows pass N=1/4/9; interpolation passes N=4/9.
One extreme original integer-clipping overflow artifact in air-to-air stays
scaled to preserve its original silhouette; incomplete legacy metadata also
falls back safely. Original bitmap lettering/artwork remains the owner's
choice. Real-display refresh/motion/4K checks remain. Details:
[docs/presentation.md](docs/presentation.md); private evidence
`D:/f117-gate/render-fallbacks`.

D10/D35/D36 were investigated deeply and closed as not established for DOS
or not reproduced, with evidence and reopening criteria in
[docs/legacy-reports.md](docs/legacy-reports.md) and bugs.md. D10's recovered
MicroProse release notes establish a Macintosh defect. D35 passes all 18
speed/engine/sound probes; D36's engine plays on all seven available MT-32
control ROMs. These closures are not new patches; the separate scope/progress
commit reduces their weight and credits D7 through D8. Phase 3 still includes
presentation coverage and real-display polish; human play/listening checks
remain Phase 4.

Weapons in flight (`683E`) and aircraft traffic (`5852`) remain integrated
and fully gated; retained proofs: `D:/f117-gate/frame-683e` and `frame-5852`.

Ground defences (`5046`) is integrated and fully gated too; its retained
test requires zero-divisor and overflow faults and hand-back at `5122`.
Proof: `D:/f117-gate/nav5046`; details are in the roadmap.

Frame update (`3ADA`) and flight input (`1EB0`) are integrated and fully
gated too. Matched routines with a switchable fix site after their entry
decline while that fix is enabled (`fix_inside` in matched.c); this keeps
D1/D1REAL and the other internal fixes reachable. When matching a routine,
check `src/fixes/fixes.c` for sites inside it. The coverage pass still runs
for every commit that adds a matched routine: the translator isolates each
matched entry, changing generated code and invalidating the skip guard
(the test-entry key fix only helps commits that add none).

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

- **Branch housekeeping (12 Oct).** 24 local branches are fully merged into
  `master` and safe to delete; `flight-input` (older draft of aircraft
  traffic, superseded by `b41ec9c`, its worktree `D:/f117-wt/span-emitter`
  has uncommitted edits) and `irq-divide-fault` (kept on purpose, see below)
  are not merged. Nothing has been deleted yet: ask the owner first.

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
  moving objects, camera-image, flight input, frame update, ground defences,
  aircraft traffic, weapons in flight, view composition and mission generation). Earlier proofs and tested source
  snapshots remain under `D:/f117-gate/`; history is in
  [docs/repeated-processes.md](docs/repeated-processes.md).
  Next: the development-proven sprite-set loader above. DSWAP `0x03DF3` is deferred:
  2,791 initial zero bytes, no generated entry; the static draft decodes data.
  Require an actual caller or execution before matching, and keep the census
  unchanged meanwhile. Evidence: `D:/f117-gate/dswap-3df3/candidate-review.json`.
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
via `D:/f117-gate/affinity_launch.py`, ~30 min) before it's pushed. Read the
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
