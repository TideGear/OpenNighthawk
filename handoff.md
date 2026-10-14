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

## Owner priority (13 October)

Prioritize Phase 3 to completion, targeting **4K at 60 Hz** (16.67 ms per
host frame). The pacing target now passes on two scenes (next section); what
is left of Phase 3 is the owner's motion review on a physical panel, other
scenes and slower machines. Phase 2's private routine batch stays parked
until Phase 3 is closed out.

## Latest Phase 3 integration: threaded 4K/60 presentation (13 October)

Fullscreen Direct3D 11, 3840x2160 at 59.99 Hz, vsync, N=9 passes
`tools/presentation_timing.py`: strike to 3.5B (7,719 pictures, p99 16.71 ms,
max 17.06, none missed, 2,062/2,062 logic frames exact, hash
`bc8e3ae5a872405d`) and air-to-air type 6 to 5.9B (19,000 pictures, p99
16.71 ms, max 21.31, none missed, 3,679/3,679 exact, hash `b050542466c9a616`,
equal to a fresh headless run) and landing to 10.0B (50,440 pictures, p99
16.71 ms, max 17.25, none missed, 13,408/13,408 exact, hash
`efe080faa364f8f4`, equal to a headless run). The single-threaded build had failed at p99
24.30 ms: logic-frame closes cost 21-25 ms and ordinary frames used 89% of the
budget. Now `drawlive_collect` replays records as they arrive and stages closed
frames into a pool of five steps, `drawlive_publish` makes them current (pointer
changes only), a worker thread draws the in-between picture one frame ahead
(`drawlive_plan_interp` / `drawlive_render`) and three helper threads convert
the palette. Scratch tables both threads reach are thread-local (`tls.h`). The
picture is one host frame older (extrapolate mode plans a frame ahead
instead). `F117_PRESENT_THREADS_OFF=1` runs single-threaded. Design, results
and limits: [docs/presentation.md](docs/presentation.md).

Verified: 60 tests, all 45 routes at N=1 (259,240 logic frames / 259,166 pairs,
unchanged; 481 s at `--jobs 16`), `boot_to_flight` N=4/9 (368 frames / 366
pairs each) and the 66 retained-window checks. P3 **93.52%**, overall
**83.23%**; Stage 3/4 is scored 80%, not 100%, because the panel motion review,
cargo/career flights and a slower CPU clock or weaker GPU are unchecked (the
strike flight also passes pinned to four logical CPUs: p99 16.71, max 16.82 ms). A capture that changes
display mid-run (a window starting on the 1080p monitor, then the 4K one) logs
two display lines and the checker rejects it: rerun with nothing else going.
Evidence: `D:/f117-gate/p3-stream` (`thr2`, `air6`, `chk2`, `windows`).

Next for Phase 3: time a long cargo/career flight, look
at the SETUP `0000:0010` event-limit overrun seen once in an SDL boot log
(two instructions past its limit under 1 ms slices; the headless gate has
none), and get the owner's motion verdict.

## Latest Phase 3 integration: fill styles and replay coverage (13 October)

All model fill styles replay by rule; dither/stipple refine and interpolate
with original-sized cells, without copying post-fill output. Staged original
instruction fixtures pass 2,619 fill and 3,953 driver cases; Python matches
2,187 original rows. All 45 complete routes pass at N=1: 259,240 exact logic
frames / 259,166 exact endpoint pairs, unchanged guest checkpoints/finals,
zero dropped/unseeded frames. Live boot N=4/9 and all 66 retained-window
checks pass. Stage 1 complete; Stage 2 95%.

Fresh full gate (fill styles): 58 tests, 45 routes / 44 fresh interpreter
sessions, both 5,718,848-state instruction profiles, three 831-address matched
seeds (2,725,058 / 2,723,998 / 2,725,465), zero mismatches; reachability/event
checks pass (1,969 s). Proof: `D:/f117-gate/p3-styles`. The fill-style milestone
(`571a490`) used frozen runner SHA256
`b1b99911ed22476aa1e83d925a2e0411a5f27124d8ef1e02933442bdc82862d3`.
`tools/presentation_check.py` retains the full-route checker; use fresh
baseline scan runs, canonical aliases and a fresh output save for each run.
The safe 8/4-byte flat-guard comparisons are integrated (scalar equivalence
at N=1..16, all 66 windows pass); optional preset `f117a.4k.ini`.

## Latest Phase 2 integration: five reached routines (13 October)

Matched and gated: the front ends' retrace re-sync (START `0x8D71`, END `0x4C55`),
PLAYER's two timer tick-rate setters (`026B:0073`, `026B:008B`) and VGAME's
low-detail sky far entry (`0FB2:0000`). Three seeds each compare 2,300-4,700
states with zero mismatches. Fresh full gate: 60 tests, 45 routes IDENTICAL
(44 fresh interpreter sessions), both 5,718,848-state instruction profiles,
three 836-address matched seeds (2,745,183 / 2,744,149 / 2,743,525), zero
mismatches, routes-only reachability and event-limit checks pass (1,264 s).
Census: 828 functions / 98,993 bytes; P2 57.48%, overall 83.59%. Proof:
`D:/f117-gate/p3-stream/gate2.log`.

The first gate failed (39 routes DIFFERENT) although the random-state lockstep
passed at three seeds: the re-sync helper charged `icount` in one lump after
several IN/OUT instructions, so the PIT and CRT-status accesses happened at a
stale clock, which the harness cannot see. Charge the clock between every port
access, stop right after each STI, and prove a body on `ibm_boot` first
(`F117R_MATCHED_LIMIT=N py tools/run_route.py tools/routes/ibm_boot.args
--engine recomp`, 22 s, compare `final hash` with the gate's interpreter run).
The timer interrupt handlers (VGAME `0x1C9B`, START `0x8D0B`, END `0x4BEF`) open
with STI and are deliberately left as original code: after STI the run loop
admits one more instruction, so a matched body cannot do more than the original.

**Phase 2 is bounded by route coverage, not effort.** `py tools/census_reach.py
--census census.tsv --coverage DIR` shows that of 487 unmatched code entries only
72 (1,954 bytes) are entered by any route, 4 only partway, and 411 (53,214 bytes)
never run. The next Phase 2 step is new routes that enter unreached code (START
and END screens, other missions, error paths), then matching what they reach.

## Latest Phase 2 integration: logo sprite-set loader (13 October)

MPS_LOGO 006D:0112 (`logo_sprite_set_load`) is fully gated: 434 instructions /
1,164 bytes, 78 room claims, 35 calls (33 far, two near calls to RETF helpers via
`guest_call_pop(...,2)`). Three optimized seeds compare 3,728 / 3,733 / 3,728
states, all 434 reached, zero mismatches; the boot route's real load matches 100
checkpoints/final on both engines. Fresh full gate: 56 tests, 45 routes, both
5,718,848-state instruction profiles, three 831-address matched seeds
(2,725,058 / 2,723,998 / 2,723,885), zero mismatches, reachability/event checks
pass (1,687 s). Census: 823 functions / 98,833 bytes; P2 57.39%, P3 85.52%,
overall 81.31%. Proof: `D:/f117-gate/logo-07e2`.

SETUP questions (`0000:0010`) landed just before it (`cf7fde3`, 1,608 s gate). Its
first gate failed step 9: nested keyboard waits ran to the host slice and went
62,168 instructions past an event limit on every route, which func_lockstep
cannot see. A wait inside a matched routine must clamp `m->run_until` to
`c->stop_at` (SETUP's `SQ_RUN`); check the gate's step 9 for any routine that
guest-calls code which can wait on input or a timer. Proof:
`D:/f117-gate/setup-0010`.

Main uses normal /O2. Private worktrees `D:/f117-wt/setup-main-0010` and
`D:/f117-wt/mission-generation-4239` retain PRIVATE /Od overrides; do not copy or
merge them wholesale. Check GitHub CI for the new commits.

## Batch of call-free leaf routines (private, started 13 October)

`D:/f117-gate/batch-a/project` is a fast fixture loop (direct candidate object
linked before frozen libs copied from the main build; /Od; build with
`build-proof.cmd`, setting TEMP and TMP to `D:/f117-gate/batch-a/tmp` first).
`matched.c` there is the main file plus 47 unreviewed DRAFT bodies
(`draft_<mod>_<seg>_<ip>`, from `matched_draft.py`, instruction-transcript style)
and table rows; `rows.txt` lists them: call-free, plain-RET, 15-160 instructions,
from VGAME/START/END/PLAYER/MPS_LOGO/DSWAP/SETUP. Drafts are not yet named or
explained, and no reach proof exists. All 47 ran at 4,000 states (logs in
`batch-a/ls`): 32 compare equal on thousands of states; four compare equal on few
(SETUP `0A9F` 1, SETUP `14BE` 127, END `45AD` 319, MPS_LOGO `00DA:0491` 603), so
they need fixtures before they count; eleven report "not testable here (routes
only)" (DSWAP `0FF8`, `10F4`; END `4393`, `44B9`, `44E7`; MPS_LOGO `00DA:0602`;
PLAYER `0F47`; START `8427`, `8545`, `8573`; VGAME `120A:0971`), so only a real
route can prove them. Run any with
`func_lockstep.exe --only MOD.EXE:HEX --states 4000 --verbose`; the IP must have NO
leading zeros (`D0C`, not `0D0C`, or it silently compares 0 states). A passing
draft still needs a real route that runs it (the gate's step 8 reports the ones
that never run: drop those), a named and explained body, and one full gate for
the whole batch. Nothing of this is in the repository. Reach check (13 Oct,
`tools/census_reach.py`): only 3 of its 47 entries are entered by any route (START
`8D71` and END `4C55` are now matched; PLAYER `026B:0211` is a different
variant), so the rest cannot count until new routes reach them: do not spend a
session on this batch.

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

- **Phase 3 first, 4K/60 target (owner, 13 Oct).** Finish presentation
  coverage and performance before returning to routine matching. Keep
  physical refresh/motion verdicts separate from hidden-window throughput.
  Private throughput prototype:
  `D:/f117-gate/p3-styles/bench` (`profile` versus `profile_guard`). Full fine
  checksums and all repair/guard tallies agree across strike/landing/air-to-air
  at N=9 and landing N=4. Guard is now integrated; the isolated actual-app
  p99 still fails (see latest integration). `F117_BENCH_HASH=1` enables the
  full frame checksum, adding work outside the per-frame timers. A grouped
  seam-repair draft (`profile_fast`) barely helped and is deferred.

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
  Next: 487 census entries with a translated region (56.5 KB) remain; census
  rows with no region are data (SETUP `0x027AE` is strings), so filter them out.
  **Only 72 of them (1,954 bytes) are entered by any of the 45 routes**;
  `py tools/census_reach.py --census census.tsv --coverage DIR` (the gate's
  `*.cov` files) lists them. The other 411 (53,214 bytes) never run, so matching
  them needs new routes first (screens START/END never visit, other missions),
  and the private leaf-routine batch (`D:/f117-gate/batch-a`) is almost all in
  that group: 3 of its 47 entries are reached. The reached set is small glue;
  an interrupt handler that opens with STI cannot be matched past its first
  instruction (the timer interrupts at VGAME 0x1C9B, START 0x8D0B, END 0x4BEF).
  Screen with `tools/matched_draft.py --dossier` (module/seg/ip from the
  region name). Batch several routines per gate. DSWAP `0x03DF3` is deferred:
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
