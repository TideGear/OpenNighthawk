# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs). State as of 15 October 2026.

This file is kept short on purpose: it holds only the current state and
standing practice, not a session-by-session log. Earlier sessions' detail
(what was tried, what failed, exact investigation narratives) is in
`git log -p handoff.md` — it does not need to live here to stay available.

## Current state (15 October)

**Latest (15 October, afternoon): identity pairing, a `BAA2` fix, motion metric, gate tooling.** The owner's
recorded flight `session-20261015-103956` (road jitter, jumping building patches) is frozen privately in
`D:/f117-gate/present-v2/owner-roads-20261015` (inputs, save, 633 MB draw capture 2.45B-2.85B; `ident/draw.log` has
identity records 2.75B-2.85B; `analysis/` the scans). Findings and changes (details in
[docs/presentation.md](docs/presentation.md#identity-pairing-and-the-step-clock-15-october-2026)):
- Identity pairing (the observer's compact `I` record: model, ring, world cell at `120A:00CF`) was integrated, then
  turned OFF after the owner saw more jitter: it halves unpaired shapes (polygons 7.78 -> 3.98%, edges 2.51 -> 0.78%)
  but the extra pairs join different visible-vertex sets, and paired batches deforming over 100 px between steps rose
  3.78 -> 20.53% (`motion_check.py`). `F117_PRESENT_IDENT=1` keeps it as an experiment. Lesson: judge pairing by
  deformation, not by unpaired counts; real identity needs per-vertex identity, i.e. the scene renderer.
- The step clock is a second, separate cause: steps take 42-86 ms for a fixed distance, and `drawlive_plan_interp`'s
  t freezes or skips at uneven boundaries. A smoothed playback clock (simulated) cuts freezes/skips 35 -> 2 and
  >20% speed changes 21.4% -> 1.6% for ~17 ms more age. Not implemented: needs a third live step and its pairing;
  make it switchable (owner has not set defaults) and judge it with `tools/motion_check.py`.
- `tools/motion_check.py LOG` reports pairing completeness, deformation of paired batches and step-timing irregularity
  (the exactness checks cannot see any of them).
- Matched-code defect found by the rotating seed `0xfaf385733f77`: `or ax, ax` after a call was a byte test in VGAME
  `BAA2` and twice in `73C8` (wrong SF, and a zero low byte would take the wrong branch). Fixed; CTest
  `matched_or_ax_word` retains the seed.
- Gate: `build_recomp.py --overlap-jobs` (default 8) starts interpreter runs during coverage from a private runner copy;
  not yet timed, because the gate skipped coverage (unchanged translation). Time it with `--coverage-record ''`.
  `tools/present_light_check.py --base REV` is the owner-approved light check for presentation-only changes.
- Verification of the merged tree: fresh full gate 1,330 s (114 CTests, 45 routes IDENTICAL / 44 fresh interpreter
  runs, both instruction profiles 0 mismatches, 891 matched routines at three seeds incl. `0xfaf385733f77`,
  reachability/event checks); 45-route N=1 259,240 frames / 259,166 pairs; boot N=4/9 368/366.
  Evidence `D:/f117-gate/present-v2/identity-*`.
- Owner decisions today: no longer draw distance for now (resident world is four rings of 32 tile slots); make the
  state-driven scene renderer the main path for world geometry (no primitive pairing), then the smoothed clock.
  The unconnected `src/present/scene_mesh.c` draft (mesh/pose/visibility/order/
  material evaluation, synthetic CTest) is on master as part of the gated tree; it is not a finished fix. Private
  notes: `D:/f117-gate/present-v2/scene/next-step.txt`, `live-integration-notes.txt`.
- Next, in order: implement the smoothed clock as an option; time the gate overlap; then the scene renderer (prepared
  poses + identity capture in one observer batch, one gate). Keep original graphics, N=1 and gameplay/input timing.
  No visible/fullscreen tests without asking.

The remaining HUD/map-position follow-up is integrated on top of sky/ground (`f57f9cf`) and world lines (`84f3edc`).
All original fonts/sprites remain, including the eight-heading 4x4 aircraft arrow. Continuous state moves on the fine
grid; discrete readouts, switches and arrow heading wait for the logic frame. Map-origin subtraction preserves its
16-bit wrap. Models match 29,594 original HUD calls, 11,490 panel calls and 714 map blits across 17 fresh windows.
111 CTests, fresh full gate (45 routes / 44 fresh interpreter sessions, both instruction profiles, three 891-address
matched seeds, reachability/event checks), all 45 N=1 routes (259,240 frames / 259,166 pairs), boot N=4/9 (368/366 each),
96 retained checks and 34 new live checks pass. The complete owner's N=9 flight preserves 69 checkpoints/final
`31c259b992340414` and 2,101 frames / 2,099 pairs. Evidence: `D:/f117-gate/present-v2/hud2`.

**No screen takeover.** The owner stopped the visible fullscreen timing run; do not count it as acceptance. Hidden
3840x2160 D3D11 throughput is 119-150 fps with <5 ms p99 regeneration/raster but occasional roughly 100 ms upload
stalls. Fullscreen refresh pacing for this batch is unchecked. Ask before a visible/fullscreen test; keep automated
work headless/hidden. Main's executable is rebuilt from this content for `4k60.bat`; the owner's untracked batch
file stays untouched. Phase 2 counts are unchanged.

**Next 4K work (owner review, 15 October): a broader world-geometry fix.** Do not keep adding clipping exceptions.
Raw camera positions for visible edges already exist; the missing inputs are complete model topology before
visibility filtering and persistent instance/face identities. The original model request `120A:00CF` selects
parts/planes, then `06B5` skips invisible vertices and `0CA8` skips hidden faces. Observe upstream model/scene
state; decode original meshes/materials and share pose interpolation, projection and clipping across terrain,
roads, outlines and dots. Preserve sprites/fonts and the original simulation. Detailed steps and acceptance:
[docs/presentation.md](docs/presentation.md#consolidate-world-geometry-before-more-clipping-exceptions-15-october-2026).
The clip-only prototype in `D:/f117-gate/present-v2/coast-finish/clip-prototype` passes its analytic regression and
112 selected tests, but is set aside and not integrated. Its test can be adapted to the broader path.

**Scene-input foundation is integrated.** Opt-in `F117R_OBSERVE_SCENE=N` captures pre-visibility requests and complete inputs;
`tools/scene_capture.py` audits completeness; `tools/scene_model.py` decodes complete topology and derives detail
selection. Two paired owner replays preserve all 69 baseline checkpoints/final `31c259b992340414`, with 192 identical
requests / 25,214,976 bytes. Original-byte checks agree on all 192 selections and the 191 geometry submissions:
stream boundaries/counts, 2,330 camera vertices, 488 plane tests and 349 visible shades. Controlled fixtures agree
on all 205 reachable detail variants, including six articulated and nine zero-vertex variants. Shared-vertex coverage
is synthetic only. No persistent identity, independent pose preparation or live scene renderer is implemented yet.
112 selected CTests and 18 Python capture/model tests pass. Fresh gate passes (1,706 s): all 45 routes / 44 fresh
interpreter sessions, both instruction profiles, three 891-address seeds, reachability/event limits. All 45 N=1 routes
retain 259,240 frames / 259,166 pairs; boot N=4/9 retains 368/366 each. Evidence/private original-execution harnesses:
`D:/f117-gate/present-v2/scene` (foundation-verdict.json, gate.log, *-verdict.json,
flight/*-verdict.json). Harness return traps use a self-loop; standalone CPU stepping does not stop at HLT.

**Gameplay and logic parity remain the owner's top priority.** Independent rendering must preserve original control
reads and simulation timing. Latest-state rendering avoids interpolation age; smoothing without waiting requires
strictly visual prediction and measured corrections. Do not silently change defaults or the owner's configuration.

The full owner replay confirms refined map terrain; short windows with a previously cached map can show only
its original bitmap. Never force `F117R_OBSERVE_PAGES=1` on live presentation: it reseeds every phase and prevents
pairs. The stopped WIP branches remain preserved, not second integration candidates. The scene gate refreshed the
source-keyed interpreter cache; new observer work will require another gate before P2 checkpoints. Refresh the kit
libraries from main after its build. Width tuning and weapon/target symbology remain open.

**Validation audit:** coarse interpolation now accepts its implemented `U` generator and fails on unknown novel
records or regeneration-count overruns. Both midpoint-only mutants fail the corrected checker and escaped the old
one. All 111 selected tests and 96 retained checks pass. No presentation runtime changed in this audit.

**Phase 2:** 889 census functions / 117,660 bytes (P2 67.37%, overall 86.67%). Today added
24 routines in seven gated commits (proofs `D:/f117-gate/p2-*`; gates `p2-pair`, `p2-compose`,
`p2-batch2`..`p2-batch7`).
Two harness changes came with them: scripted INT 21h answers (`g_dos_script`, with `.put`
for the byte a one-byte read brings), and skipping states that overwrite a planted
callee stub. Next candidates (reached by `full_cycle`, unmatched; ranked list
`D:/f117-gate/format-init/reached-corrected.tsv` minus the matched table): MPS_LOGO
`0006:0072`, `0146:1894`, `0146:08CC`; DSWAP `0010`, `1D66`, `117E`; VGAME `130D:033F`;
SETUP `19B6`; PLAYER `1602`, then the rest of that list. Agents were stopped mid-proof
on VGAME `130D:033F`, MPS_LOGO `0146:1894` and PLAYER `1602` (their directories hold a
draft `body.c`/`fixture.inc`, unverified) and before starting SETUP `19B6` (empty).

**How Phase 2 is done now: the kit and parallel agents.** `D:/f117-gate/kit/kit.py` runs a
private proof for one routine (`init`, write `body.c` / `fixture.inc` / `mutations.json`,
`lock --cover`, `seeds`, `mutations` (four edits built at a time), `checkpoints`,
`verdict`, `integrate`); `D:/f117-gate/kit/BRIEF.md` is the agent brief with every trap
found so far. Agents work only in their `p2-*` directory. Then: verify each
`private-verdict.json` yourself (counts, mutations, source hashes, real-route calls),
`kit.py DIR integrate` (set `KIT_ROOT` to integrate into a worktree), apply any harness
change the agent reports by hand, build, ctest, ONE full gate per batch, the 45-route
`presentation_check --scales 1`, docs, commit, push. Gate batches in a separate worktree
(`D:/f117-wt/gate`, branch `gate-batch7`; build it once with `build.cmd OFF` before the
gate, which needs its `f117cpu_api.dll`) when the owner is testing the main tree's
build. `kit.py checkpoints` needs the interpreter cache entry for the current source tree:
after changing anything under `src/` other than `matched.c`, run a gate before the next
proofs' checkpoints. Refresh `D:/f117-gate/kit-libs` from a main build after the libraries
change (the kit recopies newer libraries into each proof).

**Presentation (owner decision 14 October: state-driven where the capture is the wrong
technique).** Merged and gated (`dfe8cf4`): the per-pixel origin diagnostic
`F117_PRESENT_DIAG`; near-clipped polygons drawn from geometry and polygons the original's
clipper mis-draws accepted at N>1 (worst-frame fallback 84% -> 1.7%, no holes); the HUD's
tapes, heading strip, pitch ladder, flight-path marker and bars drawn from game state
(exact on 12,921 original HUD calls; ladder-label jitter rms 3.78 -> 0.01 fine px;
`F117_PRESENT_HUD=old` switches it off). Evidence `D:/f117-gate/present-v2/{diag,fix3d,hud}`,
analysis `present-v2/hud-analysis.md`. The horizon follow-up from `present-horizon` is now finished and integrated
(above). The original WIP branch is preserved, not a second integration candidate. The HUD/map follow-up from `present-hud2` is now finished and checked (above). Both original WIP branches are
preserved for history; neither should be merged again. Weapon/target symbology, coastline topology changes and the
owner's physical-panel verdict remain open. Preserve original sprites and artwork.

**Owner's 4K priorities (14 October, 23:20; follow-up 15 October).** Dots/lights and roads are
now drawn from exact camera geometry when their captured endpoints match it, and matching
outlines interpolate in camera space. Full `256*x/z` projection removes positive-modulo
jumps at negative coordinates and the earlier rounded-depth steps. One perpendicular-width
pen serves HUD, outlines and strips; no per-class widths have been assigned yet. N=9 strike
RMS: outlines 1.680 -> 0.0039 fine px, dots 2.756 -> 0.0140. The owner's recorded flight also
passes 19 frames / 17 pairs; its negative model-axis RMS falls 0.877 -> 0.00049. **Coastline
motion remains open**: sampled visible shoreline RMS improves little, and those probes omit
boundaries that disappear or change topology. Next investigate those transitions. The horizon and HUD/map-position follow-ups are now integrated; coastline transitions remain open.

**Owner instruction (15 October): keep all original graphics.** The map aircraft arrow is a
4x4 sprite selected from eight headings; keep it and the rest of the sprite/bitmap artwork.
Smooth positioning and render the existing geometry; do not replace it with a vector icon.
Different widths by reliable drawing role remain a tuning task, not a fixed four-class list.

Traps found today:
- `1450` is also the presentation observer's frame-phase hook. At one address only
  the first override runs, so a matched body there must call the observer itself
  (`observe_game_draw`) and the address goes on `deliberate` in
  `tests/test_overrides.c`. `kit.py integrate` refuses an address with an observer.
- The gate's commit-derived seed (rotates with HEAD) can fail a routine the commit
  did not touch: it found the segment-wrap defect in `hit_test` (START `393D`, END
  `1C3B`) and the overwritten stubs of VGAME `683E`. Reproduce with `func_lockstep --seed
  S --only MOD:IP`, or shard the whole run (`--shard K/8`) to find which routine fails.
- `build.cmd` now logs inside the checkout's build directory; before that, a build in
  another worktree overwrote the shared log and printed BUILD OK for a failed link. Run
  builds from PowerShell; through bash they hang.
- Finished agents can wake on their own timers and resend their report many times:
  stop them (TaskStop) once their proof is verified.
- Gate speed: `parallel()` prints per-stage wall time; coverage and parity are each as long
  as the `career_promotion` seed chain; the owner has not decided on the two
  verification-weakening speedups ([docs/repeated-processes.md](docs/repeated-processes.md)).

The 22-routine support batch is integrated and fully gated. Proof:
`D:/f117-gate/leaf-batch/final-gate-verdict.json`. Fresh full gate: 75 tests,
all 45 routes / 44 fresh interpreter sessions, both 5,718,848-state profiles,
three 865-address seeds (2,883,318 / 2,881,648 / 2,881,497), zero mismatches,
reachability/event checks pass (1,390 s). Four seeds pass (103,554-103,672
comparisons
each), all three private routes match and every routine runs on at least one.
The real route exposed a hardcoded segment in logo `00DA:0491`; the body now
reads the relocated operand at CS:04AD and the fixture varies it. Terminal
error helpers now return through real epilogues so error arguments are checked.
All 20 private mutations are rejected. Both zero-count fixtures must return
intact (END uses a separate stack and zero source words); skipping its full
fill is rejected. Six grouped CTests and instruction floors
retain the proof, including PLAYER `026B:004D` and both zero-count record loops.
Census: 857 functions / 103,349 bytes; P2 59.78%, overall 84.47%.
`support_int` bounds every new nested INT run at the caller's next event.
The first complete gate matched all routes and locksteps but failed step 9
for three SETUP wrappers; that log is `gate-event-limit-failed.log` in the
batch directory. The clamp fixes all three private routes with current
generated libraries; removing it fails the route audit despite equal hashes.
The private route checker now rejects overruns too. This is a 21st mutation,
separate from the 20 fixture mutants.
Gate unit tests now run with up to eight workers (`--unit-jobs 1` for serial).
All 75 captured test outputs are identical: 159.76 s serial, 22.47 s parallel.
Proof: `unit-parallel-verdict.json` in the batch directory. The final full
gate reused verified unchanged coverage; its unit stage was still serial.

The batch covers PLAYER `0B84`, `0BA2`, `0F47`, `05F6`, `026B:004D`; START
`8CE6`, `8545`, `8573`, `A8C2`; END `4BCA`, `44B9`, `44E7`, `45AD`; SETUP
`0FFE`, `0237:0004`, `0C84`, `14BE`, `1387`, `0BFB`; MPS_LOGO `00DA:0491`,
`04C4`, `043D`. START `8427` and END `4393` stay original: available routes
missed their planted defects, and lockstep cannot run their waits. The two
`6DB8` comments now correctly describe queued palette updates and the tick
delay that applies them. Run every new body at four or five seeds, not one.
Only the owner's `f117a.example.ini` edit and untracked `4k60.bat` remain
outside the task's files; do not overwrite or stage them.

What keeps P3 below 100%: only Stage 3/4 (85%): the owner's panel motion
verdict, long cargo/career flight timing and weaker CPU/GPU checks.

## Owner priority (13 October)

Fix the remaining 4K uprez issues in the owner's screenshots: the map, toothy
triangle edges and blocky terrain. The owner authorizes our own in-flight
captures. **Once the uprez looks right and the map is fixed, return to Phase 2.**
Do not wait for all remaining physical-panel and weaker-machine Phase 3
checks before resuming P2. Match code with positive real-route reach evidence;
the complete interpreter capture below supplies new leads for the private batch.

## Phase 2 reach inventory corrected (14 October)

**Latest integration:** VGAME `0FAE:0002` binds segment entry slots;
MPS_LOGO `006D:0002` shifts encoded sprite colours. Authored fixtures cover all
27 / 63 instructions, header/stack and stream/frame aliases, table overlap,
wraparound, palette bases and the full 65,536-iteration zero-count LOOP.
Full-cycle invocations: 3 / 82, with 66 checkpoints/final exact. Seven private
mutations are rejected, including a five-clock claim for a six-instruction
loop; the public mid-run audit now checks no-call clock overshoots too.
An explicit live radar decoy removes a seed-dependent projectile coverage gap
(870/886 -> 886/886); the floor stays 886. Fresh full gate passes: 66 tests,
45 routes / 44 fresh interpreter sessions, both 5,718,848-state profiles,
three 840-address seeds (2,760,609 / 2,759,527 / 2,756,481), zero mismatches,
reachability/event checks pass (1,391 s). Verified unchanged coverage reused.
Proof: `D:/f117-gate/entry-palette/final-gate-verdict.json`; prior coverage-only
failure retained there. Census: 832 functions / 99,221 bytes; P2 57.60%,
overall 83.84%. Owner edits in `f117a.example.ini` and `4k60.bat` stay untouched.

**Earlier formatter integration:** two eight-instruction conversion initializers
(START `9D67`, END `56D9`) are implemented, fixture-proven at three seeds and
real-driver-proven by `full_cycle` (89 / 9 calls, 66 exact checkpoints/final).
The first full gate passes all 45 routes and both instruction profiles, but
rotating seed `0x376cc4116689` exposes an existing VGAME `4B8F` alias bug:
the named code read the argument before the original clears the output byte.
The read now follows the clear through the live frame; the failing seed and
both fixed seeds pass. CTest retains the failing seed. The fresh full gate
passes: 64 unit tests, all 45 routes / 44 fresh interpreter sessions, both
5,718,848-state profiles, three 838-address seeds (2,753,443 / 2,752,408 /
2,752,308), zero mismatches, reachability/event checks pass (1,301 s).
Coverage reuses the first gate's identical-input, unchanged-translation proof.
Evidence: `D:/f117-gate/format-init/final-gate-verdict.json`; first failure
retained in `gate.log` / `failed-func3.log`. Census: 830 functions / 99,033
bytes, P2 57.50%, overall 83.81%. The owner's edited `f117a.example.ini` and
untracked `4k60.bat` must remain untouched.

The old "only 72 reached / 411 never run" conclusion was wrong: the gate's
recomp `.cov` files contain only interpreted fallback instructions, not all
execution. Absence says nothing about already-translated code. A fresh
**interpreter** `full_cycle` capture alone reaches 358 then-unmatched code entries
(43,138 bytes), plus 6 interiors (521 bytes); 121 entries (12,813 bytes) lack
evidence in that capture. It preserves all 66 checkpoints and final state
`3300000000/6b61d89b95e7b562`. `tools/census_reach.py` now states this limit.
Private proof and ranked leads: `D:/f117-gate/format-init/full_cycle.cov`,
`reached.tsv`, `batch-reach.json`. Of the old 47 private draft entries,
**37 have positive reach in this route** (including the two already matched
front-end re-sync helpers). They can be reviewed, named, fixture-strengthened
and real-driver-proven without first inventing new routes. The other ten
still need reach evidence. Largest new leads include END `2BEC`, START
`6DB8`/`149C`, VGAME `1450`/`DC46`/`04E6`; do not assume a lead is proven C.

Retained prototypes and mutations are in `D:/f117-gate/format-init`.
`next-batch-review.json` there records six other readings and warns about static
walks past terminal DOS exits. Never copy an old private matched file wholesale.

END `2BEC` was this inventory's largest lead and is now integrated (see
above). Its fixture is in `tests/func_lockstep.c`.

## Phase 2 resumed: SETUP's native BIOS boundary alert (14 October)

The SDL alert is a false positive, not extra execution: SETUP's INT 10h at
`14EF` runs two native callback/stub clocks atomically. A 4,096-clock boot
reproduces it at 102,400 -> 102,401 on **both** engines with identical state.
`native_stub_time` preserves the original clock cost and accounts for native service
clocks in the overrun ledger. The new ROM-free regression allows the atomic
callback and still rejects an extra NOP; it fails with the old accounting.
Eight slice schedules x both engines x before/after: all 16 sets of per-slice
records identical. Twelve SDL boots per build: same final hash, one old alert,
zero new alerts. Fresh full gate: 63 unit tests, all 45 routes / 44 fresh
interpreter sessions, both 5,718,848-state profiles, three 836-address matched
seeds (2,745,183 / 2,744,149 / 2,745,748), zero mismatches, reachability and
event checks pass (1,745 s). Proof: `D:/f117-gate/p2-resume/final-gate-verdict.json`.

That native-boundary correction counted no new routine. The two formatter
conversion initializers are now integrated above. Remaining private dossiers
identify PLAYER `026B:0211`
as its distinct timer re-sync and `026B:0336` as tick/callback service. The
latter calls original code, so needs returning-helper fixtures. The private
static drafts are unproven transcripts, not integration-ready bodies.

## Latest Phase 3 integration: map and smooth terrain (13 October)

The map's fine rectangles now survive the round trip through the unseeded
cockpit-art page, guarded by matching logged coarse bytes, and repeated
`x` display-blit bytes no longer erase them. Its one-cell displacement was
orthographic alignment: the rounded integer names a cell centre. Ordinary
verified model polygons now clip to the viewport instead of each coarse
stair, removing triangle teeth; the guard preserves verified fine geometry.
Narrow span-rasteriser regions retain both geometric edges; only actual
zero-area strips use a stroke. Separate HUD and guard masks keep near-plane
seam repair working after refined sky/terrain. N=1 remains the exact original.

Verified: 62 selected CTests, all 96 retained-window checks, live boot N=4/9
(368 exact frames / 366 pairs each), and all 45 N=1 routes (259,240 exact
frames / 259,166 pairs, guest hashes unchanged, 669 s). The frozen runner is
`dde819d160efa08869e69b289b6d3e2e235e2871a0708328a284ab5abe7d6cbb`.
Fresh captures show the runway, smooth map diagonals and banked terrain.
Isolated 4K/59.99 Hz/vsync/N=9 strike to 3.5B passes: 7,715 flight pictures,
p99 17.23 ms, max 28.32, one late interval (0.013%), 2,062/2,062 exact logic
frames, hash `bc8e3ae5a872405d`, equal to a fresh headless run. That SDL boot
reproduced SETUP's one-instruction alert; the P2 follow-up above identifies
and corrects its native BIOS accounting cause. Evidence is in
`D:/f117-gate/map-refill`. Details:
[docs/presentation.md](docs/presentation.md#joined-strokes-outline-edges-and-span-rasteriser-polygons-13-october-2026).

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

Next for Phase 3: time a long cargo/career flight and get the owner's
motion verdict. The SETUP alert is resolved by the native-boundary
accounting follow-up above.

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

**Reach inventory:** use complete interpreter captures, not the gate's
fallback-only coverage, to choose remaining routines. The corrected inventory
above finds substantial reached code; new routes are needed only where positive
evidence is still missing.

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
`matched.c` there is an older main file plus 47 unreviewed DRAFT bodies
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
`tools/census_reach.py`) was fallback-only and incomplete. The fresh interpreter
`full_cycle` capture now proves entry reach for 37 of 47 (see above). START
`8D71` and END `4C55` are already matched; PLAYER `026B:0211` is a different
variant. Rebase individual candidates onto current code and retain positive
matched invocation counts and exact route hashes before counting them.

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

- **Phase 2 resumed after the visual fixes (owner, 13 Oct).** The corrected
  interpreter inventory supplies 358 reached unmatched entries and 37 private
  batch leads. Review and prove those before adding routes for entries still
  lacking evidence. The private dossiers remain unproven transcripts.
- **Remaining Phase 3 coverage.** Physical-panel motion review, cargo/career
  timing and weaker machines remain. The current strike build passes 4K/60;
  the older single-threaded p99 failure is superseded. Full checksums via
  `F117_BENCH_HASH=1` add work outside frame timers and are diagnostics.

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
  Next: use the complete interpreter inventory above; census rows with no
  region are data (SETUP `0x027AE` is strings), so filter them out. The gate's
  fallback coverage is not a complete reach inventory. The private batch has
  37 positively reached entries, and larger reached routines are listed too.
  An interrupt handler that opens with STI cannot be matched past its first
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

- **Keep checks off the owner's screen.** Do not launch visible/fullscreen tests without asking first. Use headless or hidden runs for automated work (owner instruction, 15 October).

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
