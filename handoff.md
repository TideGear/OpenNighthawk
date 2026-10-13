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

## Latest integration: mission generation (13 October)

START 0000:4239 (`start_mission_generate`) is fully gated: named stages cover
objectives, bases, special air units, map window, variants and handover.
The resolved nine-entry switch has three targets, 4E14/4E24/4E50: 1,332
instructions / 3,811 code bytes; census ownership is 3,035 bytes.
339 room claims and all 60 calls audit exactly. Three development seeds
compare 3,858 / 3,859 / 3,858 states; two optimized targeted seeds compare
3,859 / 3,858. Every canonical instruction reached, zero mismatches; each
includes 95 divide-fault frames at both possible sites (499D, 4A7C).
Fixtures use synthetic worlds and staged returning helpers; mode 7 changes
secondary timing after an earlier guard, proving code rather than normal play.
Coherent fixtures use DS=SS and clear DF; random controls retain other states.
Production retains all actual helpers and original rejection loops.

Fresh full gate passes 54 tests, 41 routes, 40 fresh interpreter sessions,
both 5,718,976-state instruction profiles and three 829-address matched seeds
(2,716,846 / 2,715,774 / 2,715,365 comparisons), zero mismatches, reachability/
event checks pass (1,832 s). The fresh optimized boot route executes the new
routine once, all 54 checkpoints/final identical (`48a10e901505e2a9`).
Census: 821 functions / 96,402 bytes; P2 56.10%, P3 85.52%, overall 80.94%.
Proof: `D:/f117-gate/start-4239/final-gate-verdict.json` and adjacent audits.

`D:/f117-wt/mission-generation-4239`, branch `wip-mission-generation-4239`,
retains development work and a PRIVATE /Od CMake override. Do not merge or
copy it wholesale; main uses /O2. Large proof builds use one compiler job
and private TEMP/TMP. Check BUILD OK as well as exit status: a failed Windows
build has returned zero with an old executable. Published predecessor
`04c9668` passed GitHub CI run 38421875283; check CI for the new integration.

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

Prior view-composition proof (`VGAME 0000:2F9D`,
`vgame_frame_compose`), 828 matched addresses, census 820 functions /
93,367 bytes (P2 54.49%, P3 85.52%, overall 80.47%). Three targeted seeds
compare 3,199 / 3,216 / 3,200 states, zero mismatches; all 938 canonical
instruction starts reached.
Room audit: 208 claims, 938 instructions, 66 calls. Actual camera replay:
8,456 calls, all 234 checkpoints and final identical, no event overrun.
Private proof: `D:/f117-gate/frame-2f9d`.

Fresh full gate `D:/f117-gate/gate-view-compose-render.log`: 53 tests,
41 routes, 40 fresh interpreter sessions, both 5,718,976-state instruction
profiles and three 828-address matched seeds, zero mismatches; reachability
and event checks pass (1,768 s). After observer/presentation refinements,
`gate-orthographic-final.log` repeats build/tests and fresh route parity,
with unchanged translation/matched coverage reused (1,271 s). Audits:
`frame-2f9d/current-full-gate-verdict.json` and
`render-fallbacks/final-gate-verdict.json` under `D:/f117-gate`.

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
  Next: SETUP `0000:0010` (1,267 census bytes); private static dossier
  `D:/f117-gate/setup-0010` resolves 505 instructions with near/far calls.
  Its real-driver entry proof is still needed. DSWAP `0x03DF3` is deferred:
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
