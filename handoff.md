# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs). State as of 11 October 2026.
Earlier session logs are in `git log -p handoff.md`.

## Runtime gate passed (11 Oct, 17:57 Pacific)

Gate 26448 has closed exit 0. `D:/f117-gate/gate-break-memory.log` passes
27 unit tests, all 38 routes, both instruction profiles (5,718,976 states
at 89,367 instruction starts), three 797-address matched seeds
(2,596,946 / 2,595,902 / 2,593,952 states), reachability and event checks.
Duration 1,490 s: build 172, coverage 522, identical regeneration 18,
parity 778; 37 fresh interpreter runs, zero reuse.
Census refreshed: 791/1,535 functions, 67,670/179,213 bytes;
P2 40.87%, All 74.05%. No owned build/probe/gate job remains live.

Visible allowance at 17:57 Pacific is 100% five-hour, reset 22:57,
seven-day 55%. Continue the standing five-minute checks; suspend owned
work at <=20% and check every 30 minutes until >20% before resuming.
The old gate PID/handle instructions below are historical; do not suspend
or resume those exited processes.

Next: commit/push this verified seven-entry batch, then integrate ONLY the
six privately verified allocation/restore entries and their deliberate
harness fixtures. Exclude the unreached diamond. Run a fresh full gate
before committing that batch. Private verification is recorded below.

## Runtime helpers integrated; full gate running (historical)

Latest next-batch proof (14:47): isolated checked allocator plus FIVE
Ctrl+Break restore entries (VGAME 1B96, START 85DB, END 457A, DSWAP 0546,
SETUP 0C58) bring private count to 804 (main 797 + these six + diamond).
The first private build 94554 fails: FRAME/SETFRAME/NEAR_THEN macros were
out of scope. Corrected allocator uses bp_get/bp_put and explicit guest_call
stops. `build-wt-dswap-checked-restore.log` passes, session 99809 closed.
Three raw seeds pass 26,274/26,187/26,252 states; 27 unit tests pass in 10.66 s.
Probe 40215 closed exit 0, 46.1 s: all 66 hashes/final match the copied
full-cycle interpreter control; VGAME and SETUP restore each run once.
The checked allocator does not run on that replay.

Extra private error fixture (`build-wt-checked-error-fixture.log` BUILD OK):
DSWAP 0494 gets low/error allocation replies, planted empty error format,
returning graphics thunks and a returning failure-exit stub at 0E36.
This tests caller behavior after cleanup returns, not real OOM termination.
Three seeded comparisons pass 25,359/25,315/25,340 states, including
1,263/1,247/1,199 completed error results; 27 tests pass in 9.61 s.
Logs `checked-restore-error-lockstep-{0,1,2}.log` and
`test-wt-checked-restore-error-unit.log`. All private build/probe/test handles
are closed; only main gate 26448 remains live. Do not suspend the old
already-exited private-build root 6632. Main source remains unchanged.

Latest turn (14:38): runtime gate 26448 is live in parity. Coverage passes
in 522 s (694 s since start); a live interpreter route confirms regeneration
has finished even when the parent log is buffered. Both instruction profiles
pass 5,718,976 states at 89,367 instruction starts; three 797-address matched
seeds pass 2,596,946 / 2,595,902 / 2,593,952 states, zero mismatches.

Next isolated candidate is DSWAP 0000:0494, `dswap_alloc_checked` (43 census
bytes), wrapping existing 04E0 with error reporting and original failure path.
Private source/harness now has 799 addresses (main 797, diamond, checked
allocator); `DOS_STUBS` adds DSWAP 0494. Build session 94554 is live in
`D:/f117-gate/build-wt-dswap-checked.log`, two workers, private TEMP and
affinity 0x00FFFFFF. It recompiles updated generated files (42 steps). No
build/test claim yet. Once BUILD OK, run three 4,000-state seeds with
`--only DSWAP.EXE:494`; inspect real comparison counts and failure-path
coverage before accepting, then a normal full-cycle probe if useful.
The original instruction draft is private `next-drafts/dswap_timer_install.c`
(old provisional name; do not copy or commit that draft).

Latest visible allowance 22% at 14:38:10, next read by 14:43:10 (planned
14:42:45). At <=20% suspend BOTH live owned trees: gate root 35048 and
private-build PowerShell root 6632 (created 14:36:24; revalidate before use).
Use separate new identity records with the prepared helper and appropriate
`--expected` log names. No suspension has yet occurred in this batch.

Current gate 26448 is confirmed live; all 27 unit tests pass (LastTest ends
14:26), with coverage/locksteps running. Previous goal turn made progress:
camera commit plus repaired runtime integration. Current turn adds the
relocated-operand rule to `docs/architecture.md` and verifies this exact job.

Latest visible allowance 22% at 14:30:25 Pacific, reset 17:01, seven-day 59%.
Next read planned 14:35:10 (no later than 14:35:25). At <=20%, suspend the
owned tree before doing further roadmap work. Prepared private helper:
`py D:/f117-gate/allowance_gate_tree.py suspend --root 35048 --created 1791753820.200343 --record D:/f117-gate/allowance-paused-20261011-runtime.json`.
It verifies root command/creation identity, suspends parents before children,
and records each process's creation time and command hash. This record has
not been created yet; no suspension has occurred as of 14:28.
After an actual suspension, read allowance every 30 minutes until >20%.
Resume with the same helper's `resume --record` mode, then poll gate 26448;
never use the older already-resumed record or restart the gate.

Camera darkening is committed and pushed as `15d6c05`; HEAD equals
origin/master. Seven runtime candidates and their relocation/DOS fixtures
are now integrated in main, with two focused CTests. Main has 797 matched
addresses, 27 unit tests and the existing 38 routes. Main is fixed under
`D:/f117-gate/gate-break-memory.log`, session 26448; initial translation
reports 89,367 instruction starts, one additional static entry. Poll this
handle without restarting or editing source; no code commit until full gate
passes. Census/scoreboard remain at camera's verified values, All 74.01%.
Private worktree 798 includes the unreached diamond; never copy it whole.

Latest allowance 26% at 14:23:57 Pacific, reset 17:01, seven-day 59%; next
visible read by 14:28:57. At <=20% suspend owned jobs/work, then check every
30 minutes until >20%. Current owned gate wrapper process is PowerShell
35048, launcher py 26204 -> Python 6276 -> py 49552 -> gate Python 2364
(identities confirmed 14:24). Revalidate command/creation identities before
suspending its tree. Do not reuse the older already-resumed pause record.
There are no other live private probe/build/test jobs from this turn.

## Camera gate passed; runtime candidates verified privately (11 Oct, 14:22 Pacific)

Main camera gate `D:/f117-gate/gate-camera-darken.log` passes, session 37707
closed exit 0. All 25 unit tests, 38 routes, both instruction profiles,
three 790-address matched seeds and reachability/event checks pass. Duration
1,420 s: 137 build, 494 coverage, 18 identical regeneration, 771 parity;
37 fresh interpreter runs, zero cache reuse. Census refreshed to 785/1,535
functions, 67,463/179,213 bytes, P2 40.76%, All 74.01%. Ready to commit.

Runtime repair is verified privately. `build-wt-break-relocation-fixed.log`
passes, session 6777 closed (wrapper exit 1, actual BUILD OK). Three targeted
seeds with relocated-operand fixtures pass 32,723/32,696/32,752 states, zero
mismatches (`break-memory-fixed-lockstep-{0,1,2}.log`); 27 unit tests pass in
7.46 s (`test-wt-break-memory-fixed-unit.log`, excludes only full func_lockstep).
Fresh full-cycle probe 55346 closed exit 0, 30.2 s: all 66 checkpoints and
final `6b61d89b95e7b562` equal the copied interpreter control. SETUP, VGAME,
END installers each run once. Earlier mismatching probe and aborted broad
unit run remain failures, superseded only by these fixed checks.
Next, after committing main camera, integrate ONLY seven runtime candidates
and their DOS/relocation fixtures plus two focused CTests from the isolated
worktree. Keep B0C2 diamond out: all three normal probes fail to reach it.
Run a full gate before committing runtime code. Main 790 becomes 797
addresses; private 798 still includes diamond. Do not copy the worktree whole.

## Camera gate and runtime preparation (superseded by verified results above)

Latest follow-up (14:19 Pacific): main gate 37707 is live in parity; coverage
passes in 494 s, second regeneration is byte-identical (18 s), total 649 s
before parity. Both 5,718,912-state instruction profiles and three 790-address
seeds pass (2,564,223 / 2,563,206 / 2,561,119 states). Routes remain pending.

Isolated runtime build `build-wt-break-memory.log` passes (40641 closed).
Initial targeted seeds pass 32,723 / 32,696 / 32,752 states, but these
**do not prove correctness**: normal `full_cycle` differs at 2.35B and final
`913853da6bd71abf`, versus original `6b61d89b95e7b562`. The shared installer
mistakenly used constant zero for a MOV AX,segment immediate the loader
relocates. It now reads CS:[entry+12h]. `IMAGE_PATCHES` plants nonzero
segment 197Ah at each installer's operand on both sides; before the fix this
catches all five entries (`break-relocation-before.log`). No main changes.
Fixed rebuild `build-wt-break-relocation-fixed.log` session 6777 is live;
poll and require BUILD OK, then repeat three 4,000-state seeds and normal
full_cycle in a NEW output/save directory. Original completed control is
`D:/f117-gate/break-memory-cycle-control.txt`; compare every checkpoint.
The first private CTest run 88052 is not a pass: 27 tests pass, but the broad
2,000-state func_lockstep is deliberately terminated after the normal-driver
defect is found (PID 44148 command verified). For repeat unit run use
`-E ^func_lockstep$`, as the main gate does, retaining the quick instruction
test and all focused regressions. Isolated source
currently registers 28 tests, so the repeat unit subset has 27.

Air-to-air diamond probe 3616 closed exit 0: all 234 checkpoints/final
`896858d151aebc76` equal the last gate's interpreter control (copied to
`diamond-airair-control.txt`), but B0C2 never runs. Keep diamond isolated.
Latest visible allowance 32% at 14:19:18, reset 17:01, seven-day 60%; next
read by 14:24:18. Suspend owned jobs/work at <=20% per the standing rule.

Current follow-up adds seven candidates ONLY to the isolated worktree:
An additional isolated air-to-air type-5 replay is live, session 3616,
`D:/f117-gate/diamond-airair-matched.log`, output directory
`D:/f117-gate/diamond-airair-matched`. It uses the existing private binary
(791 addresses), aiming to exercise B0C2 after strike failed to reach it.
Wait for this runner to exit before relinking the private build. Compare
all hashes to the main gate's fresh airair_type5 interpreter when available;
require an actual B0C2 counter before claiming normal-driver proof.

Ctrl+Break installers VGAME 1B73 / START 85B8 / END 4557 / DSWAP 0523 /
SETUP 0C35, with one named shared body and per-program saved-vector/handler
configuration. Original byte checks prove those are the only differences.
DSWAP 04E0/0506 reuse the existing DOS allocate/free helpers after full
body byte comparisons. Their DOS fixture entries are added privately.
Isolated build now has 798 addresses (includes main's camera and the
unreached diamond). No build/lockstep claim yet. Wait until main enters
parity after second regeneration before rebuilding the private worktree;
use two workers and affinity launcher, then three 4,000-state seeds with
`--only VGAME.EXE:1B73,START.EXE:85B8,END.EXE:4557,DSWAP.EXE:523,SETUP.EXE:C35,DSWAP.EXE:4E0,DSWAP.EXE:506`.
All original instruction drafts stay private in `D:/f117-gate/next-drafts`.
The camera gate has passed all 25 tests (unit log ends 14:00 Pacific) and
is collecting coverage. Session 37707 confirmed live at 14:02:51.
Latest visible allowance 74% at 14:01:31, next read by 14:06:31.

Previous batch is committed and pushed as `9bec607`; local HEAD equals
origin/master and the working tree was clean before this batch.
Camera darkening alone (`VGAME 0000:C45C`) is now integrated in main, with
returning-driver lockstep fixtures, `matched_camera_darken` CTest and
`tools/routes/recon_darken.args`. Fixed main has 790 addresses, 25 tests and
38 routes. New full gate `D:/f117-gate/gate-camera-darken.log`, session 37707,
is running its build. Poll that handle; do not restart or change main source.
Code remains uncommitted until the full gate passes; census/scoreboard stay
at verified `9bec607` values (784 functions, 67,369 bytes, All 74.00%).

Private reconnaissance darkening probes are finished, exit 0, sessions
18121/76646 closed. All 150 checkpoints and final `2aa0648449d98887`
at 7,549,654,902 match, 123.3/71.4 s; C45C ran 1,608 times through the real
graphics driver. Its three seeded comparisons total 8,873 states exactly.
Private strike session 52070 is also closed exit 0, 81.6 s: all 177
checkpoints and final `aa8fca6ac6f373f0` match the main gate's fresh control,
but B0C2 never ran. Diamond remains ONLY in the isolated worktree, not main.
Next find normal controls selecting class 1D/1E stores with a projected
target (Maverick strike did not reach the marker), or choose another census
candidate. Do not conflate camera's proven reach with diamond's missing one.
Shared generation must remain untouched by the private worktree.

Latest visible allowance 74% at 13:57:48 Pacific, reset 17:01; seven-day 67%.
Next visible read due by 14:02:48. Human Phase 4 question is still pending.

## Pause/HUD gate passed; next drawing helpers isolated (11 Oct, 13:56 Pacific; superseded above)

Full gate `D:/f117-gate/gate-pause-hud-2.log` has finished, exit 0;
session 33892 is closed. All 24 unit tests, 37 route comparisons, both
5,718,912-state instruction profiles, all three 789-address matched seeds
(2,561,209 / 2,560,263 / 2,560,055 comparisons), reachability and event-room
checks pass. Duration 1,453 s: build 130, coverage 537, identical regeneration
18, parity 768. There are 36 fresh interpreter runs and no cache reuse.
The pause/HUD batch is ready to commit. Census refreshed: 784/1,535 functions,
67,369/179,213 bytes; P2 40.71%, All 74.00%. Earlier pending notes below
describe preparation and are superseded by this result.

Next work is ONLY in `C:/Users/Tideg/f117-recomp-local/wt`: projected-point
diamond `VGAME 0000:B0C2` (122 bytes) and camera darkening `0000:C45C`
(94 bytes). Main remains at 789 addresses; isolated build has 791.
Build `build-wt-diamond-camera.log` passes, targeted three seeds compare
7,981 / 7,894 / 7,932 states exactly, and 24 unit tests pass in 11.95 s.
Logs `diamond-camera-lockstep-{0,1,2}.log` and
`test-wt-diamond-camera-unit.log` are private in `D:/f117-gate`.
The initial F5/F6/F6 probe preserves all 54 hashes but reaches neither
new helper; F5 selects armament, not camera. Do not claim that as proof.
Recon replay plus F6 at 100M reaches C45C 1,608 times in isolated recomp,
final `2aa0648449d98887` at 7,549,654,902 (71.4 s); interpreter session
18121 is still running, recomp 76646 closed exit 0. Compare all hashes before
accepting. Logs `recon-darken-{original,matched}.log`, output directories
of the same names. Isolated strike replay session 52070 is live in
`diamond-strike-matched.log`; compare with the main gate's fresh
`C:/Users/Tideg/f117-recomp-local/runs/strike_interp` result, and require
the B0C2 execution counter. No main integration/full-gate claim for these two.

Latest visible allowance 74% at 13:54 Pacific, reset 17:01; seven-day 67%.
Continue five-minute visible reads while above 20%, suspend owned work at
or below 20% and check every thirty minutes until above 20%. Human Phase 4
playtest question remains unanswered; keep those items open.

## Combined pause/HUD gate preparation (11 Oct, 13:32 Pacific; passed above)

Current turn is progress: HUD candidates pass random/real-driver checks and
are now integrated with pause; repaired two invalid self-modifying fuzz cases.
Main fixed sources are under fresh full gate `D:/f117-gate/gate-pause-hud-2.log`,
session 33892. Poll this exact handle; do not restart on observation timeout.
Code is uncommitted until full gate passes. Main unit log passes 24/24 at 13:33:50;
coverage is live with 16 workers at 13:34. Main now has 789 addresses,
24 unit CTests and 37 routes, with `hud_layout_toggle` added beside pause.
Scoreboard/census remain at the last committed values until verification.

The first pause gate `gate-pause.log` is **not a pass**: both instruction
profiles and the first two matched seeds pass, but rotating seed
`0x5361129204e7` exposes PLAYER 0000:1E90 and VGAME 130D:0577 mismatches.
Preserved raw traces: `D:/f117-gate/pause-failure/func3_lockstep.log`.
PLAYER SS:F38B + BP:E5E3 + 8 wraps to code 1E9B, replacing its RET; the
original enters adjacent instructions, recurses and restores the bytes.
VGAME DI:0561 aliases the bisection midpoint code below entry 0577;
the original later executes 055F/056B. Its watch window omitted those bytes.
The harness now watches PLAYER's reader per step and extends VGAME's range
down 0x18 bytes. Retained CTest `matched_frame_code_alias` runs this seed.
Targeted repaired proof: 8,944 states, zero mismatches, 23 invalid states
skipped (`code-alias-fixed.log`). No matched implementation was changed by
this harness repair. First gate's exact process tree was terminated after
preserving evidence; session 76816 is closed, exit 1, not restarted.

HUD real-driver C/C probes both pass world/exit milestones, all 54 hashes,
final `89dfc62167b7ae04` at clock 2,727,067,963
(`hud-layout-{original,matched}.log`, 58.7/55.2 s). Matched colour setup runs
once, layout selector twice. Probe sessions 20907/30476 are closed, exit 0.
Copies of the intentional matched/harness deltas are integrated; main's
newer regressions preserved. The code below describes prior preparation.
Latest visible allowance 74% at 13:33:58 Pacific, reset 17:01; seven-day 67%.

## Isolated HUD colour/layout candidates (11 Oct, 13:28 Pacific; integrated above)

Current turn makes progress while the pause gate runs: implements two new
functions ONLY in `C:/Users/Tideg/f117-recomp-local/wt/src/matched/matched.c`.
`vgame_hud_colours_init` 1058:09D8 (84 census bytes) falls through to
`vgame_hud_layout_apply` 1058:0A2C (564). Both compact/full settings are named
data; byte writes preserve AH, common font stores use one AX load, full
layout calls the actual graphics helper at BC4 for its window origin.
Do not replace that return with the Reimp's fixed 5200 assumption. Matched
counts now 789 isolated only; main still 787 under its pause gate.

Isolated build passes `build-wt-hud-layout-plants.log`; harness adds these
entries to returning driver fixtures and plants DS:294B=1 to exercise compact
mode. Three targeted seeds compare 8,828/8,845/8,827 states, zero mismatches,
including mid-run boundaries (`hud-layout-lockstep-{0,1,2}.log`). First two
filter syntaxes selected zero routines; those were not proof. Correct syntax
is `--only VGAME.EXE:9D8,VGAME.EXE:A2C`, unpadded IP, no segment. Check the
per-routine lines. Unit run `test-wt-hud-layout-unit.log` passes 21 tests in
9.20 s. Private C/C toggle route real-driver probes are next; no integration
or full-gate claim. Main sources stay fixed for pause gate session 76816.
Asked owner asynchronously for controls/save/feel playtest results; no answer
yet, so Phase 4 stays open. Latest allowance 79% at 13:23:49 Pacific.

## Pause-handler integration under gate (11 Oct, 13:16 Pacific)

Previous goal turn made progress: camera-plane/seam rendering committed and
pushed as `5361129`, main clean before this batch. Current turn adds
`vgame_pause` at VGAME `0000:3A4E`: sound off, save DS:2646 in guest frame,
poll input while empty or Alt+P, restore sound and the saved timer. Calls,
stack aliasing and instruction/event boundaries are preserved. Main sources
are fixed for `D:/f117-gate/gate-pause.log`, session 76816; do not restart
or commit code until the gate passes. Main unit log shows 22/22 passed;
coverage is live with 16 workers as of 13:17 Pacific. Adds `matched_pause_wait` CTest and
normal-input `tools/routes/pause_resume.args` (36 routes total).

Isolated build passes (`build-wt-pause.log`/`build-wt-pause-thunks.log`).
Without sound thunks, 4,000 random states all skip. Enabling the existing
returning driver fixture for this handler allows the harness's normal
DOS/BIOS answers to reach its polling/return paths; no keyboard code is
patched. Targeted three seeds compare 4,966/4,959/4,954 states, zero
mismatches, including mid-run limits (`pause-lockstep-thunk-{0,1,2}.log`).
Real-input original/matched probes (`pause-{original,matched}.log`) both
pass world/exit milestones, all 54 hashes and final `cdca1b902f63d18b` at
2,727,068,014 clocks. Matched handler runs once. Host time 38.9/40.0 s.
No full-gate claim yet; census/scoreboard not advanced before verification.
Remaining Phase 2 lead: HUD colour setup private draft in `next-drafts`.
Latest visible allowance 86% at 13:13:29 Pacific, reset 17:01; 7-day 69%.

## Camera-plane rendering and seam repair gated (11 Oct, 12:50 Pacific)

Continued the roadmap. Main now contains the isolated camera snapshots,
exact near-plane reconstruction, atomic interpolation metadata, and geometry-aware
seam repair. The full gate passes on fixed main sources in
`D:/f117-gate/gate-near-seams.log`, terminal exit 0: 21 tests, all 35 route
pairs identical, both 5,718,912-state instruction profiles and three matched
seeds (2,547,415/2,546,484/2,546,742 comparisons), zero differences, with
reachability and event-room checks. 1,411 s total: build/tests 107, coverage
513, byte-identical regeneration 17, parity 775 (rounded stages). All 34
interpreter sessions ran fresh. Gate session 2166 is closed; no jobs remain.
Main's transient code-alias harness and CTest are preserved. Stage 2 estimate
advances from 0.70 to 0.85 for verified clipped geometry and seam handling;
unsupported geometry and visual polish remain. P3 75.20%, All 73.89%.

The shared-edge fixture below now passes without removing its missing-pixel
assertions: exactly six N=9 holes are restored to their original coarse owner's
colour, zero at N=4. Coverage tracks true geometry, actual painted fine pixels
(including black), and coarse footprint unions. Repair only fills unpainted
pixels inside both unions, respecting HUD masks. Coverage is deep-copied,
finalized before replacing primitives/blits, and reset on reseed. Reading a
picture is idempotent and retains coverage for subsequent adjacent faces.
Passive `a`/`x` observer diagnostics must not close the model layer: doing so
caused recorded/interpolated endpoint differences and was fixed with a fixture.

Final isolated unit run: 20/20, 5.98 s (`test-wt-seam-final-unit.log`). Strike,
landing and air-to-air fine checks pass at N=4 and 9: respectively 444/62/54
endpoints and 666/93/81 intermediate pictures per scale
(`seam-interp-*-fixed.log`). C/Python N=4 repair counts agree exactly:
10,010 strike, 1,534 landing, 2,941 air-to-air (`seam-c-*`/`seam-python-*`).
Earlier 5,014 original crossing checks and N=1 identity evidence still apply.
Frozen repaired binary `near-seam-bin` live N=4/9 checks both pass:
368/368 logic frames, 366/366 exact fine endpoint pairs, all 54 checkpoints
and final hash `48a10e901505e2a9` at clock 2,727,068,014
(`seam-live-{4,9}.log`; 53.2/73.7 s host time).

Latest visible allowance: 94% at 12:46:40 Pacific, reset 17:01; seven-day 70%.
Continue five-minute reads while above 20%, otherwise suspend owned work and
check every 30 minutes until above 20%. No subagents are authorized.

Next Phase 2 leads: private drafts in `D:/f117-gate/next-drafts`: VGAME
`0000:3A4E` pause begin (19 instructions, polling loop; confirm route reach),
`1058:09D8` HUD colour setup (219 instructions across its branches, despite
the census's 84-byte entry). No new matched implementation or verification.
Main retains 786 addresses, 781/1,535 census functions, 66,674/179,213 bytes.

## Shared-edge test exposed a seam limitation (11 Oct, 06:18 Pacific; repaired above)

Previous goal turn was progress (interpolation binding, oracle and scene/live
checks). Current turn adds shared-edge tests to isolated
`tests/test_near_polygons.c`; **the new test currently fails**. Do not integrate
or run the main gate yet. Main remains source-clean at `6d3abaa` before these
documentation updates; no owned jobs run. Isolated build reports BUILD OK
(`build-wt-near-shared-4.log`), but six N=9 seam assertions fail. Earlier green
unit results predate this new fixture and do not prove shared-edge tiling.

Fixture: two adjacent camera-clipped triangles, front `(20,20)`, shared
crossing `(24,26)`, outer crossings `(16,26)`/`(32,26)`, colours 7/9. Camera
front `(0,0,0x20000)`; behind endpoints `x=-1024/1024/3072`, y=1536,
z=-0x10000; origin `(20,20)`. Original edge-walk span rows are in
`shared_spans`. Both faces choose overshoot 0.5. Reversed edge endpoints
produce identical individual pictures; reversing face order preserves the
union and changes only the expected border overlap (at most 1.667/N from
the shared edge). Nothing appears outside the union of original footprints.
N=4 has no interior holes. N=9 has exactly six, at fine indices
`(187,189),(188,189),(188,190),(205,216),(206,216),(206,217)`.
Private independent Python `shared-seam-study.py` reproduces these exact
coordinates using `fine_cover`/`choose_grow` on the known geometry and spans.
This is a limitation of the existing per-polygon footprint rule, not different
C/Python clipping math: those coarse cells belong only to the left face,
while their true fine geometry belongs to the right; neither face paints them.
The flat-colour guard cannot in general repair differently coloured neighbours.
The first fixture assertion mistakenly included pixels outside both original
footprints; correcting that still leaves these six genuine interior holes.

Next action: implement a geometry-aware seam repair without loosening the
original footprint union, erasing outer-edge detail, or returning all clipped
polygons to nearest scaling. A promising precise rule is to track per-page
fine geometry coverage, actual model paint coverage, and the union of coarse
model footprints; an unpainted fine pixel inside both unions can retain its
coarse owner's colour. This preserves each original colour's footprint while
closing internal gaps. This is a design lead, not implemented or verified.
Any coverage state must survive drawlist copies/blits, be cleared by replacing
primitives, respect fine HUD masks, avoid stale scene/page metadata, and keep
the existing N=1 identity and fine endpoint checks. Do not substitute a general
3x3 palette guard: it misses the holes near the shared apex and can erase
legitimate outer detail. Add a passing seam test before claiming integration
ready; retain original missing-pixel assertions rather than weakening them.
Then repeat the recorded-scene/live checks and full gate on fixed main sources.
Allowance still visibly 85% at 06:17:20; freshness unconfirmed; next read 06:22:20.

## Camera-plane interpolation verified in isolation (11 Oct, 06:06 Pacific)

The previous goal turn made progress: implemented interpolation metadata and
added synthetic endpoint tests. This turn found 3,466 reused G slots in the
strike capture, 959 with changed vertex identities. Worktree `interp.c` now
binds each E to its preceding G within the batch, pairs those bindings through
the paired polygon, and updates all of a polygon's snapshots/crossings together
only when its coarse polygon moved and every snapshot is compatible. Reused-slot
and changed-origin/marker/identity/legacy regressions pass. `test_interp --fine N`
compares fine endpoints to each neighbour's own fine rendering and exercises
three intermediate pictures; its default coarse report is unchanged. It also
frees each rendered drawlist, avoiding the old per-render allocation leak.

Strike (`near-metadata-entry/w_strike.log`): 444 exact fine endpoints at both
N=4 and N=9, 666 intermediate pictures each, 648,695 regenerated records.
Default coarse replay/endpoints are exact for 221 pairs, zero provenance
violations. Logs `interp-near-fine-{4,9}.log`, `interp-near-coarse.log`.
The isolated Python `tools/hires_subpixel.py` now independently rebuilds the
camera-plane midpoint and closing edge, preserving legacy fallback. It matches
all 4,124 retained crossings and C's exact polygon counts, per-overshoot counts
and painted footprint totals on this window (`near-python-hires-4.log`). Its
whole-picture statistics differ because Python still uses the old scaled HUD.

Fresh frozen-binary normal-input landing 9.70..9.73b and type-5 air-to-air
6.793..6.823b windows are under `D:/f117-gate/near-scenes/{landing,air5}`.
Observer on/off hashes agree at 194/136 checkpoints and finals
`d6fb96881fd7f84a`/`61593284e035ebbd`; all 272/618 additional retained crossings
pass in Python and C, no ambiguous/missing/mismatched pairs. The initial wrappers
exit 1 only because full-route debrief milestones are inapplicable to these
partial windows. `verify-near-scenes.py` checks the original inputs/world
milestones, exact stop clocks, all checkpoints and these specific wrapper errors
without rerunning flights; `near-scenes/result.json` records the limitation.
Private partial route files preserve normal inputs and world checks. Both
windows pass static N=1/4/9 and 62/54 fine N=4 endpoints plus 93/81 intermediate
pictures. C/Python polygon counts, overshoot counts and footprint totals match
exactly for both (`hires-4.out`, `python-hires-4.out`). Three footprint slivers
remain nearest in landing; air5 has 219 slivers and four unrelated behind cases.

Live frozen binary `near-live-{4,9}.log`: each 368 exact logic frames, 366 exact
interpolation pairs, 48,828/48,856 refilled model polygons and two guard-restored
flat cells; same 54 checkpoints/final `48a10e901505e2a9` at clock 2,727,068,014.
Full isolated CTest session 59604 finished exit 0, 21/21 in 634.92 s, including
the 629.33 s full function sweep (`test-wt-near-interp-all.log`). After the
presentation binding/checker changes, all 20 unit tests excluding that sweep
pass in 5.54 s (`test-wt-near-final-unit.log`). Latest isolated build is BUILD OK
(`build-wt-near-binding-final.log`). All capture/test sessions are closed;
no owned jobs remain. No phase-3 source changes are in main yet.

Next: shared-edge tiling cases, then integrate only nearclip, hires, interp,
observer snapshot/hook and their tests/Python oracle into current main. Preserve
main's newer transient-code harness regression. Run the full gate on fixed
main sources before any code commit. Phase-3 progress is not yet advanced.
Allowance text still reads 85% at 06:05:43; Windows input remains unavailable,
so freshness is unconfirmed. Next five-minute read due 06:10:43 Pacific.

## Cockpit drawing helpers verified (11 Oct 2026)

Integrated only `vgame_panel_readout_outline` (`114A:014F`),
`vgame_raster_line_block` (`114A:0006`) and `vgame_panel_box` (`114A:01DB`)
from the owned worktree into main after `c5ce512`. The main matched source
and driver-thunk harness are fixed during the running full gate:
`D:/f117-gate/gate-panel-drawing.log`. Its 18 tests pass; coverage takes
603 s, second translation 17 s and adds no code; initial build/tests 127 s.
All 35 route pairs and both instruction profiles pass; parity takes 312 s,
34 interpreter sessions reused, none fresh. Two matched seeds pass
2,547,415/2,546,484 comparisons. The commit seed finds one END 1CB1 mismatch:
the random pair swap temporarily overwrites its own epilogue, falls into
the adjacent ascending helper and restores the code before the final memory
check. This is a self-modifying fuzz input, not ordinary guest behavior.
Extended the existing per-step self-code watcher to the four START/END
pair-order routines and retained seed `0xc5ce5129a4d9` in CTest
`matched_order_code_alias`. Targeted 15,984 comparisons pass, 16 skipped,
including the invalid transient-code state. BUILD OK; no sorting implementation
change. The complete second gate **passes**, terminal exit 0, in
`D:/f117-gate/gate-panel-drawing-2.log`: 19 unit tests, all 35 route pairs,
both 5,718,912-state instruction profiles and three matched seeds
(2,547,415/2,546,484/2,547,057 comparisons), zero mismatches, every routes-only
routine reached and no event overruns. Main sources were fixed throughout.
Initial build/tests 26 s, coverage 574 s, second translation 17 s; byte
identical generated code. Parity takes 810 s, total 1,426 s (rounding stages).
CMakeLists.txt participates in the interpreter cache key, so the new
regression invalidates all reference results: 34 fresh interpreter flights,
zero reused. Both gate sessions are closed (80357 exit 1, 59127 exit 0).
No owned build/capture/test jobs remain running.
Census 786 addresses, 781 functions, 66,674 bytes;
P2 40.34%, All 73.32%. Targeted evidence remains 44,398 states, no differences.
README, roadmap and progress counts have been refreshed.

Phase 3 investigation remains isolated: worktree `src/matched/observe.c/.h`
extend `G` from three to seventeen values, retaining the first three and
adding raw origin words and each endpoint's camera xyz, projected xy and
exact x-high-word-8000 behind marker. Existing readers ignore the extra
fields. `tests/test_overrides.c` checks snapshots, independent camera and
projection offset wraps, physical wrap, VGA timing and full CPU/memory
immutability. Both builds report BUILD OK (`build-wt-edge-snapshot[-2].log`);
all 18 isolated unit tests pass in 6.82 s. The first draft test accidentally
overlapped two wrapped records; separate cases fixed the fixture. No main
observer or rendering changes yet. A frozen isolated binary is recording
`D:/f117-gate/near-metadata/w_strike.log` over 8.53..8.73 billion clocks;
`record-near-metadata.py` compares observer on/off flights at every 50M
checkpoint and final hash. Both match all 174 checkpoints and final
`0ef657e8aa7a7fb4` at 8,730,000,000 clocks. All 4,120 reconstructible near
crossings match exactly; four ambiguous edges remain. Carried drawlist replay
is exact for all 447 work and display phases, and N=1/4/9 checks pass.

The four ambiguous records appear stale when model_prepare_edge declines
for event room before emitting G. Worktree matched.c now emits G at entry
before either room decision (read-only, original instructions still run).
`build-wt-edge-entry.log` reports BUILD OK. A fresh observer-on/off pair with
this change in `D:/f117-gate/near-metadata-entry`, console log
`record-near-metadata-entry.log`, matches all 174 checkpoints and the same
final hash. All 4,124 near crossings now match exactly, no ambiguous or
missing pairs. Carried replay is exact for 447 work and display phases;
N=1/4/9 checks pass (`replay.log`, `hires-{1,4,9}.log`). The frozen binary and first capture
in `near-metadata` are preserved. No phase-3 edits were merged into main.

Further isolated work (not ready to merge): `src/present/nearclip.c/.h`
preserve wrapped midpoint rounding, DEC-DX sign and the original projection,
retaining the discarded low byte. `test_nearclip` checks boundary cases and
accepts a private eight-column camera/original-crossing TSV; all 4,124
captured cases pass (`near-metadata-entry/crossing-cases.tsv`). The isolated
hires builder now keeps exact G snapshots separately from its legacy V map,
verifies retained crossings, drops wholly behind edges and adds the closing
edge; N=1 remains exact for 447 display phases. At N=4, all 1,061 previously
nearest near polygons refill; total refilled 16,979/17,058, 69 footprint
slivers and ten unrelated behind-vertex cases still nearest. Flat guard
restores 70 cells, fine-versus-scaled share 1.086% (baseline 1.060%). Logs
`near-metadata-entry/near-hires-{1,4}.log`. New synthetic
`test_near_polygons.c` checks the sloped closing edge, entirely behind edge,
N=1, footprint containment, fine detail and legacy/mismatched-crossing
fallback, including both clipping orientations. All 20 isolated tests pass
in 6.11 s (`test-wt-near-all.log`). N=9 also passes, with 1.106% fine versus
scaled change and 140 flat cells restored by the guard
(`near-metadata-entry/near-hires-9.log`). Builds: `build-wt-near-crossing[-2].log`,
`build-wt-near-polygons.log`, `build-wt-near-polygon-test.log`, all BUILD OK.
Before integration: finish interpolation metadata/crossings, update the Python
oracle, test more scenes and fine endpoints, add shared-edge tiling cases.
`build-wt-near-final.log` reports BUILD OK with the fixture warning removed.
At 05:51 Pacific, isolated `interp.c/.h` also moves compatible seventeen-field
G camera snapshots and regenerates their retained E crossings under
INTERP_VERTICES; coarse-only and legacy modes retain their prior behavior.
`test_near_polygons.c` now includes a moving clipped triangle with two pixels
of endpoint translation, both clipping orientations, five fractional times,
quantized crossing assertions, fine refill and byte-exact N=4 pictures near
both endpoints. Changed origins, markers, identities and legacy records hold
their skeleton snapshot. BUILD OK (`build-wt-near-interp-test-6.log`), all
20 isolated unit tests excluding the full function sweep pass in 5.29 s
(`test-wt-near-interp-unit.log`); the 4,124 original retained-crossing cases
still pass. The first synthetic fixture omitted the R destination segment
and used incorrect border spans; those fixture errors were corrected.
Full isolated CTest is separately running in session 59604, log
`test-wt-near-interp-all.log`, currently in `func_lockstep --states 2000`
(PID 47512, creation 05:49:38). Check completion before launching another.
Do not merge yet: inspect whole-polygon holding when only some G records
change topology, repeated edge slots within batches and fine endpoints on
recorded scenes; Python oracle, scene diversity and shared-edge tests remain.
Main cockpit batch is committed and pushed as `638e2d6`; main sources remain
unchanged by this presentation prototype. Worktree CMake/harness remain older than
main's new code-alias regression; copy only intentional presentation changes.

`D:/f117-gate/near-crossing-check.py` improves the earlier study: compare
the reconstructed midpoint against the retained E crossing at slot+10/14,
even when the visible edge was viewport-clipped. In legacy logs, 522 of
3,662 near edges have enough origin information: 520 exact, two mismatches
(strike windows 853 and 870), ten ambiguous markers and two missing pairs.
Do not enable fine near polygons on this evidence alone. Use the richer
snapshot to resolve these cases and check every retained crossing. Preserve
the guest's DEC-DX sign test (including high word 8000), midpoint wrap and
projection rounding. Before consuming extended G in hires, update interpolated
G snapshots too: `interp.c` currently moves V records but copies G unchanged.
Keep legacy three-field G fallback, N=1 identity, footprint/flat guards,
fine endpoints and the closing near edge tests.

Latest on-screen allowance read 05:26 Pacific: 85%, reset 09:32. The Status
panel has kept that value since 04:51 and may be stale. At 05:16..05:20,
attempts to close/reopen it via supported Sky mouse and keyboard APIs both
fail with `GetCursorPos failed: Access is denied (0x80070005)`. Text-only
reads still work. Asked the owner asynchronously for a freshly opened Status
reading; no answer yet. Do not present the unchanged panel as confirmed fresh.
Continue checking
every five minutes; at <=20 suspend all owned job trees, including any new
recording job, until a live reading exceeds 20. The older paused-job identity
file has already been resumed and must not be reused.

## Cockpit sprite helpers checkpoint (11 Oct 2026)

Continued the full roadmap after the HUD commit `10257f5`. Integrated ONLY
`vgame_panel_symbol` (`114A:03AD`) and `vgame_panel_blit` (`114A:03FD`)
from the owned worktree, preserving main's display fill and initialiser.
The first constructs a centred 7x7 navigation-symbol sheet blit; the second
constructs a sprite rectangle with live full-word mask/copy selection.
Both preserve the original frame reads, writes, flags, calls and clocks.
Driver-thunk allowances preserve all existing cockpit helpers.

Three fresh targeted main-build seeds (default, 12345678, C0FFEE) compare
8,246/8,270/8,259 states, 24,775 total, no differences. Logs:
`D:/f117-gate/main-panel-blits-{0,1,2}.log`. The `--only` filter uses
un-padded IP hex (`VGAME.EXE:3AD,VGAME.EXE:3FD`); padded `03AD` silently
selects zero routines, so check nonzero comparison counts.
Census now 783 addresses, 778 functions and 66,263 of 179,213 bytes;
P2 40.13%, All 73.25%. **Full gate passed:**
`D:/f117-gate/gate-panel-blits.log`, all 35 routes identical, 18 tests,
both 5,718,912-state instruction profiles and three 783-entry matched
seeds (2,532,630/2,531,689/2,533,435 comparable states), reachability and
event limits checked. Terminal exit 0, no remaining gate/build processes.
34 fresh interpreter sessions, 0 reused (host comments changed after the
HUD gate). Raw elapsed 16,480 s includes the owner's 15,015 s allowance
suspension; about 1,465 s active (148 build/tests, 550 coverage, 18 second
translation, 749 parity, rounded). The navigation symbol runs 2,723 times
on central_europe_airair; sprite rectangle 31,059 on strike_training.

Owner's ongoing allowance rule: read live on-screen 5h remaining every
five minutes above 20%; at or below 20% suspend owned jobs and roadmap
work, then check every thirty minutes until it exceeds 20%. The owner
requested resume at 04:00 Pacific; this thread next continued at 04:32,
verified 100% on-screen and resumed all 38 saved process identities
without restarting either job. Latest check 04:46 Pacific: 87% remaining,
reset 09:32. Sky screenshot capture timed out; supported text-only
`get_window_state` reads the visible VS Code Status panel successfully.
The private process identity record is `D:/f117-gate/allowance-paused-jobs.json`;
all entries there have already been resumed, so do not resume them again.

Next candidates are isolated in `C:/Users/Tideg/f117-recomp-local/wt`:
`vgame_panel_readout_outline` (`114A:014F`) and
`vgame_raster_line_block` (`114A:0006`) and the nine-by-seven target box
`vgame_panel_box` (`114A:01DB`). Their original drafts are in
`D:/f117-gate/drafts/`. The worktree's matched.c and func_lockstep.c now
include current main's startup and sprite helpers as well as those three new
candidates; other worktree files remain based on `01f4dbd` plus the HUD.
Builds: `D:/f117-gate/build-wt-panel-lines.log` and
`build-wt-panel-box.log`, both BUILD OK, two workers and private TEMP.
Three seeds pass 29,593 comparable states for readout/block-line and 14,805
for the box, 44,398 total, no differences. Logs: `target-panel-lines-{0,1,2}.log`
and `target-panel-box-{0,1,2}.log`. The IP-only filter for `VGAME.EXE:6`
also selects sine and picture-row aliases; those extra states are excluded
from the candidate tally. No full gate for these three candidates yet.
Use `sm3_call` for PUSH CS / near CALL returning far, so the stack trap
includes the extra segment pop. Never copy the worktree wholesale.

### Near-plane presentation lead (investigation only)

`D:/f117-gate/near-clip-study.py` and its output `near-clip-study.json`
inspect the eight corrected flight windows. They count 1,831 near-clipped
polygon records and 3,662 near edges: 3,650 have reconstructible vertex
pairs (midpoint convergence within ten turns), ten have ambiguous behind
markers and two lack a pair. Only 522 visible endpoints permit an origin
inference; none has a wholly unclipped near crossing for a direct endpoint
check. This is metadata evidence, NOT proof of a rendering rule.
The original `120A:098C` halves 32-bit endpoint sums rounded toward positive
until the midpoint z high word is unsigned 0 or 1; its crossing projection
uses `(coordinate >> 8) * 2`, not an ordinary perspective divide. A fine
rule must retain that rounding and the closing near edge. The current `G`
record has only slot/projected vertex IDs, before edge preparation; consider
recording actual endpoint camera coordinates/origin and the computed
crossing to avoid stale vertex identities. Keep the current nearest copy
until replay, N=1, fine endpoints and captured clipped geometry are proved.

## Reimp-style HUD checkpoint (11 Oct 2026)

The owner explicitly instructed **"Do the HUD like the Reimp."** Implemented
in the main tree after `3fd77e9`: fine-grid HUD lines/ticks with the Reimp's
square nib and clipped end planes; scale-proportional width by default,
`--present-line-width 1..20` to choose output pixels (0 restores default).
Original bitmap text, sprites and cockpit art stay enlarged pixel blocks.
N=1 retains the exact original replay. Colour operations preserve fine
pixels; whole-page, rectangle and overlapping self-copies carry the stroke
mask, and the flat-pixel guard excludes deliberately painted HUD cells.
CLI, config, example INI and presentation docs updated. No guest writes or
timing changes. P3 estimate 72.80%, All 73.23%; P2 unchanged at 781 entries,
776 census functions and 66,083 bytes.

Verification: all 18 unit tests pass; `presentation_hud_lines` covers
coarse identity, octants, reversal, dots, width, origins, guard, colour
operations, artwork and copies. Eight corrected `p3-live/logs41/w_*.log`
windows pass carried replay and N=1/4/9 checks: 447 exact work phases and
447 exact display phases. N=4/9 live `boot_to_flight` sentinels each pass
368 logic frames and 366 pairs at coarse/fine endpoints; their 54 guest
checkpoints and final hash `48a10e901505e2a9` match. Times 146.9/112.8 s
alongside the gate, not isolated performance measurements. Both hosts'
width parsers pass seven boundary/malformed-value cases each; use
Start-Process -Wait for the GUI executable's exit status (PowerShell's
direct GUI invocation leaves LASTEXITCODE unchanged).

Private direct comparison against the Reimp's actual `hires.c` finds 360
strokes byte-identical at N=2/4/9, widths 0/1/2/5/20. Reproduction files:
`D:/f117-gate/hud-reimp-oracle.c`, `build-hud-oracle.cmd`,
`hud-oracle/oracle.exe`. Inspected previews: `hud-default-fine.png`,
`hud-thin-fine.png`, `hud-wide-fine.png`, and `hud-comparison.png` on D:.
The test harness can render them with `test_hires LOG N --carry
--line-width WIDTH --dump PREFIX` (coarse/fine PPMs with captured palette).

**Full gate passed:** `D:/f117-gate/gate-hud-reimp.log`, 1,444 seconds,
affinity 0x00FFFFFF, 16 route workers. All 18 tests, 35 identical routes,
both 5,718,912-state instruction profiles and three matched seeds
(2,524,384/2,523,430/2,525,667 comparisons) pass, with routes-only
reachability and event-room checks. Host/presentation/CMake changes
invalidated interpreter caches: 34 fresh interpreter sessions, 0 reused;
parity took 906 s. The N=4/9 sentinels' 54 checkpoints and final hashes
also match BOTH fresh scan engines after gate completion. No remaining
gate/build processes. The gate overwrites mutable `runs/*/runner.txt`
during coverage/parity: wait for completion before comparing against it.

**Next Phase 2 candidates remain isolated:** the `wt` worktree is still
based on `01f4dbd`, with the same HUD code plus symbol `114A:03AD` and
sprite blit `114A:03FD`. Three targeted seeds pass 24,775 comparable
states, no full gate for those two yet. It lacks main's display-fill and
startup helpers: bring ONLY the two named functions/registrations and
combine their harness allowances, never copy its whole matched.c or
func_lockstep.c over main. Keep the full roadmap goal active: near-clipped
fine polygons, remaining Phase 2 code and human/reference checks remain.

## Cockpit startup checkpoint (10 Oct 2026)

Continued from `01f4dbd`. Matched display fill `114A:0280` and cockpit
initialisation `0000:796E`: both pen colours, display bounds and active
page, four startup elements, invalidated legend cache and objectives-page
selection. 781 addresses, 776 census functions, 66,083 bytes; P2 40.03%,
All 71.83%. Three targeted seeds compare 5,021/4,985/4,965 states without
differences. Full gate `D:/f117-gate/gate-panel-init.log` passes in 898 s:
17 tests, 35 identical routes, both 5,718,912-state instruction profiles,
three matched seeds (2,524,384/2,523,430/2,525,425 comparisons),
reachability and event-room checks. No main-tree gate remains.

**Latest owner instruction: "Do the HUD like the Reimp."** Implementing
in `C:/Users/Tideg/f117-recomp-local/wt`: fine-grid HUD lines and ticks,
scale-proportional width with `--present-line-width 0..20`, original pixel
text/artwork. Colour operations preserve fine pixels; page copies carry
the stroke mask so the flat-pixel guard retains intentionally wide strokes.
New `test_present_lines` passes; corrected recorded-flight scene checks
are in progress. Presentation changes are not yet in the main tree.
The old `p3-stage3/strike1.log` and air logs lack colour-replace records
and fail on the unchanged baseline too; use `p3-live/logs41/w_*.log`.

The same worktree also contains the NEXT Phase 2 candidates, symbol blit
`114A:03AD` and sprite rectangle blit `114A:03FD`, **not merged or fully
gated**. Three targeted seeds pass 8,246/8,270/8,259 comparable states
(`D:/f117-gate/target-panel-blits-{0,1,2}.log`). Do not copy its whole
matched.c/harness over the main tree: that worktree is based on `01f4dbd`
and lacks this checkpoint's two startup helpers. Bring presentation files
forward separately after this checkpoint, then run its full gate.

## Panel helper checkpoint (10 Oct 2026)

Continued from `a91d716`. Added `vgame_panel_number_right` (`0000:8A18`)
and `vgame_panel_hud_line` (`0000:881F`): unsigned decimal formatting with
four-pixel character alignment, and compact/full HUD clipping bounds.
Their random-state checks use the existing symmetric returning-driver-thunk
mechanism; routes still use the real graphics driver. The scoreboard is
779 addresses, 774 census functions, 65,925 bytes, P2 39.95%, All 71.81%.

Three targeted seeds compare 9,843/9,850/9,829 states, zero differences.
Full gate `D:/f117-gate/gate-panel-helpers.log` passed in 1,010 seconds:
17 unit tests, 35 identical routes, both 5,718,912-state instruction
profiles, three matched seeds (2,519,363/2,518,465/2,522,480 comparisons),
routes-only reachability and event-room checks. Affinity 0x00FFFFFF,
16 route workers; no remaining gate or build processes.

**Next candidate prepared, not merged or fully gated:** the detached
`C:/Users/Tideg/f117-recomp-local/wt` worktree is based on `a91d716` and
contains `vgame_panel_display_fill` (`114A:0280`, 27 instructions/78 bytes)
plus its driver-stub harness allowance. It sets both pen colours and fills
the left/right cockpit display rectangle. Worktree build passed; three
targeted seeds compare 2,109/2,069/2,103 states without differences
(`D:/f117-gate/target-panel-fill-{0,1,2}.log`). No full gate yet. Combine
its harness allowance with the main tree's new 8A18/881F allowances when
bringing it forward; do not replace the main harness wholesale. The
two-worker worktree build had to rebuild 36 targets against the refreshed
shared generated code and ran alongside final parity. Its log is
`D:/f117-gate/build-wt-panel-fill.log`, with separate TEMP at
`D:/f117-gate/wt-temp`; BUILD OK is authoritative despite PowerShell's
stderr wrapper returning 1. No translator ran in that worktree.

**Presentation choice remains open.** In response to the optional HUD
question the owner asked how the Reimp does it. The local code confirms
fine-grid HUD lines with scale-proportional, configurable stroke width;
original text glyphs, sprites and cockpit art enlarged in N x N pixel
blocks. Explained this and recorded the reference in presentation.md.
This was a question, not a selection of a new presentation mode.

## Clipping-entry registration checkpoint (10 Oct 2026)

Continued from `ab1d517`. Corrected the existing `vgame_outcode`
registration from `0000:FFDC` to `0FB2:04BC` (same image address). The
runtime also requires matching CS, so the old registration missed the
rasterizer's actual near calls (confirmed at `0FB2:0729` and `0733`).
The final strike parity run now calls it 4,361,362 times. No new routine
or census bytes: the scoreboard remains 777 entries, 772 census functions,
65,795 bytes, P2 39.88%, All 71.79%.

Verification: targeted seeds default/0x12345678/0xC0FFEE compare
3,990/3,999/3,998 states, zero differences. Full gate
`D:/f117-gate/gate-outcode-bank.log` passes all 17 unit tests, 35 identical
routes, both 5,718,912-state instruction profiles, three matched seeds
(2,509,520/2,508,636/2,509,145 comparisons), routes-only reachability and
event-room checks. Measured 959 seconds, affinity 0x00FFFFFF, 16 route
workers. No gate processes remain.

**Lead-vetting correction:** compare BOTH bank/offset and linear image
address against MATCHED. Bank-corrected census matching alone can list an
already-implemented routine registered under an alias, as happened here.
I initially wrote a duplicate, then removed it and reused the original
implementation before the final gate. The interrupted
`gate-raster-outcode.log` is not a passing gate.

Seven further alias-only census mappings were found, **not yet verified
as registration bugs**: VGAME `0FB2:0320` (registered `0000:FE40`),
`1132:0002/0052` (`1058:0DA2/0DF2`), `14C2:00B7` (`1452:07B7`);
PLAYER `026B:0307` (`0000:29B7`); SETUP `0237:003A/00A4`
(`0000:23AA/2414`). Trace actual call sites before changing any of these.
Ordinary unmatched lead: VGAME `0000:8A18`, right-aligned number display,
21 instructions, calls EBBA (format), EB82 (length), 895E (draw).
Private instruction draft: `D:/f117-gate/drafts/vgame_panel_number_right.c`;
no implementation or verification yet. The two smallest banked leads
(`120A:0971`, `0FB2:084E`) are divide-fault handlers, not ordinary
division functions; treat them accordingly.

## Session end-of-shift handoff (10 Oct, 22:31 PDT)

**Scoreboard at session end**: 777 addresses matched (772 of 1,535 census
functions, 65,795 of 179,213 bytes). `(P1 100.00%, P2 39.88%, P3 67.00%,
P4 42.90%, All 71.79%)`. `git status` clean, `master` pushed, no leftover
gate/build processes in the main tree or the `wt` worktree.

**Matched and committed this session** (in order; see each commit for
detail): `vgame_init_models`, `vgame_frame_mission_start`, `vgame_free`
(plus a missed-Jcc bug found and fixed), a standalone fix for a
pre-existing aliasing bug in `vgame_eventlog_add` (surfaced by an
unrelated gate's per-commit seed, not introduced this session),
`vgame_panel_navigation`, `end_ribbons_draw`.

**Reverted this session, each cleanly, with no trace left in `matched.c`**:
`vgame_sky_low_detail`, `vgame_cold_start_init`, `vgame_camimage_background_raster`
(all three: clean lockstep, but the full gate's step 8 found no route
actually reaches them - "routes only" is necessary, not sufficient,
evidence), `vgame_joystick_save`/`vgame_joystick_load` (near-miss: both
addresses were already matched under different names before any build
cost - the bug was in my own lead-vetting, not the game: the census's
flat `entry` column isn't a usable address for banks beyond 0xFFFF, only
`our_region`'s `R_<BIN>_EXE_<bank>_<offset>` is), `pic_decode_screen_last`
(abandoned before the gate - hung 90+ seconds under fuzzing, likely an
infinite loop reachable only through its ~800 nested calls into
genuinely unverified driver/decoder code), and `start_pit_restore_unhook`
(clean lockstep at thousands of states, but failed **all 35 routes** in
the full gate - a real correctness bug in PIT/interrupt-vector-touching
code, whose blast radius a per-call fuzz check cannot see; not yet
root-caused, do not re-attempt without more time budgeted).

**The accumulated lesson, read these four reverts together**: a clean
lockstep pass answers only "is this one call, by itself, correct" - it
does not answer "does any route reach this," "does an unmatched nested
callee misbehave under truly random input," or "does this change
anything outside its own call frame." Four different failure shapes this
session, from four different gaps in what lockstep alone can see. Keep
checking route-coverage via static call-site tracing (`x86dec`, bank from
`our_region`, not the raw census address) before investing in a full
build+gate cycle, and treat PIT/interrupt-vector and heavily-nested-call
candidates as their own higher-risk categories regardless of how clean
the lockstep looks.

**Workflow change adopted and still in effect**: the `C:\Users\Tideg\f117-recomp-local\wt`
worktree (detached HEAD) is reused for drafting the next Phase 2
candidate while the main tree's gate runs, per `[[f117r-worktree-while-gate-runs]]`.
A fresh worktree build needs `build.cmd -DF117R_GEN_DIR=C:/Users/Tideg/f117-recomp-local/gen -DF117R_BUILD_APP=ON`
or `func_lockstep`/`insn_lockstep` silently don't get built (`tests/CMakeLists.txt`
gates them on `F117R_GEN_DIR`) while `build.cmd` still reports `BUILD OK`.
Batching 2 verified candidates per gate run was tried once (one survived,
one didn't - see the "First batch-of-2 result" entry below); worth
continuing, but check which of the batch actually failed via the
MISMATCH/NOT-RUN line before assuming it's the newest addition (the
`eventlog_add` incident was an UNRELATED pre-existing bug surfaced by an
otherwise-unrelated gate run).

**Loose end**: a disposable branch `p2-wt-batch` is still pushed to
origin, containing only the now-abandoned `camimage_background_raster`
commit. It was never merged into `master` (the function was reverted
from the main tree directly instead). Safe to delete whenever convenient;
not referenced by anything else.

**Next steps for Phase 2**: pick the next lead from the bank-corrected
unmatched list (see the Python snippet used throughout this session's
transcript - parses `MATCHED[]`'s own bank/offset triples and cross-
references against `census.tsv`'s `our_region` column, not raw `entry`).
Prefer candidates that show real comparable fuzz states over "routes
only" ones, and when a "routes only" candidate is otherwise attractive,
require multiple call sites inside functions already proven to run every
frame (like `game_input`/`0x3ADA`) before investing in the build/gate
cycle - one call site inside a plausible-sounding function name was wrong
three times this session.

## Reverted: start_pit_restore_unhook (START 0x08CE6) - correct fuzz signal, wrong blast-radius read (10 Oct, 22:00 PDT)

Picked this one deliberately to avoid the "routes only, unreachable" trap:
small (15 instructions), no far calls into unmatched driver code, and it
produced thousands of real comparable random-fuzz states cleanly at all
three seeds (64,233/4,262/4,285, zero mismatching). That instinct - prefer
candidates likely to show real comparable states - was right as far as it
went. But the full gate found **all 35 routes DIFFERENT**, not one edge
case: a correctness bug, not a rare interaction (clock counts matched
exactly per route; only the state hashes diverged, so timing/icount was
right and some actual value or address was wrong).

The routine reprograms the 8253 PIT (channel 0 back to its default BIOS
rate) and reinstalls interrupt vector 8 (the IRQ0 timer tick) via DOS
AH=25h, reading the far pointer to restore from a fixed CS-relative
address. Checked before reverting: AH=25h *is* properly host-serviced
(`src/machine/dos_files.c:541`, writes the real IVT slot correctly), and
the far-pointer read matches the exact pattern an already-passing routine
(`vgame_isrclock_init`) uses successfully. The bug is real and somewhere
in this code, just not found yet given the time already spent - reverted
rather than debug further at cost, per standing policy.

**The real lesson, sharper than "prefer comparable states"**: a routine
that reprograms hardware timer state or an interrupt vector has a blast
radius that spans the entire rest of every route after it runs, because
every route's timing and every later interrupt delivery depends on this
state being exactly right - unlike a routine that only reads/writes its
own data structure, where a bug stays local to whatever uses that
structure. Comparable-fuzz-state verification checks per-call register/
memory correctness; it says nothing about downstream systemic effects of
installing wrong timer/interrupt state, because the fuzz harness's random
single-call check can't see "what happens to every subsequent frame of
every route." **Treat PIT/interrupt-vector-touching candidates as a
distinct, higher-risk class regardless of how clean their lockstep looks**,
on par with the nested-unverified-call risk already noted for
`pic_decode_screen_last` below - both are classes where a clean lockstep
pass is necessary but not sufficient, for different reasons.

## First batch-of-2 result: one survived, one didn't (10 Oct, 21:07 PDT)

The new batched workflow's first real test. Merged two lockstep-verified
candidates (`vgame_panel_navigation` from the main tree,
`vgame_camimage_background_raster` drafted in the `wt` worktree) and ran
one combined gate. Result: **`panel_navigation` passed everything clean**
(including step 8 - it does run on a route); **`camimage_background_raster`
failed step 8 again** - same "routes only, lockstep-clean, but no route
actually reaches it" trap as the two solo attempts earlier tonight,
despite a call-site trace this time too (one call site, inside the
`camimage_background_raster` census-named region - evidently that
region's own reachability was the wrong thing to check; a single call
site is weaker evidence than panel_navigation's three, and this session
has now seen three "routes only" leads fail step 8 against one that
passed). Reverted just that one function + its registration line from
the main tree (clean diff, no entanglement with `panel_navigation`);
rebuilt, reconfirmed 776/776 routines clean, and - since this exact
post-revert state was byte-identical to `panel_navigation`'s own earlier
*standalone* gate pass - committed directly without re-running the gate
a third time on unchanged code.

**Running tally on "routes only" leads this session: 1 kept (`panel_navigation`,
3 call sites in proven every-frame functions), 3 reverted (`sky_low_detail`,
`cold_start_init`, `camimage_background_raster`, 1 call site each).** A
single call site inside a plausibly-per-frame-sounding function name is
not strong enough evidence on its own; multiple call sites inside
functions *already proven* to run every frame (by their own call-graph
position, like `game_input`/`0x3ADA`) is what worked. Default to that
higher bar before investing in the build+lockstep+gate cycle for the
next "routes only" candidate, or just prefer candidates that produce
real comparable fuzz states and skip this whole class of risk.

Also abandoned without even reaching the gate: `pic_decode_screen_last`
(VGAME 11D8:000A) - lockstep-tested in the worktree and hung for 90+
seconds with zero output (a real hang, not the sync-lock false alarm
from earlier - confirmed by near-zero CPU after the fact was NOT the
signature here; this one simply never returned). Likely cause: the
routine loops 200 times, each iteration making up to four nested
`guest_call_far`/`guest_call` calls into *completely unmatched, unverified*
driver/decoder code, under genuinely random fuzzed memory - 800 nested
interpreted calls into logic that has never been exercised by real
game data has much higher odds of hitting a real infinite loop or
unbounded path in that unmatched code than the simpler 1-3-external-call
routines matched earlier tonight. Reverted before any build/gate cost.
Lesson: scale caution with the number of *nested, unverified* calls a
candidate makes under fuzzing, not just its own static instruction count.

## Parallel Phase 2 workflow adopted (10 Oct, 20:13 PDT onward)

Owner asked for safe optimizations/parallelization for Phase 2. Adopted two
changes, both matching patterns already in project memory
([[f117r-worktree-while-gate-runs]], [[f117-parallel-agents-workflow]]):

1. **Draft the next candidate in the worktree while the main tree's gate
   runs.** The existing `C:\Users\Tideg\f117-recomp-local\wt` worktree
   (detached HEAD, stale at an unrelated old commit) is the one to reuse -
   `git fetch && git reset --hard origin/master` there first. Commit the
   verified candidate to a disposable branch (e.g. `p2-wt-batch`) and
   `git push origin HEAD:refs/heads/<name>`, then in the main tree
   `git diff origin/master origin/<branch> -- src/matched/matched.c` and
   apply it (a raw `git apply` of that diff can fail on context drift if
   the main tree already has its own uncommitted addition nearby; apply
   by hand with Edit in that case - it is always a small, isolated
   function-plus-registration-line diff).

2. **Batch 2 verified candidates per full gate run** instead of one each,
   roughly halving the number of ~15-20 minute gate runs for the same
   progress. Tradeoff, confirmed: a batched gate failure needs the
   MISMATCH line checked to isolate which candidate (or neither) caused
   it, same discipline as the `eventlog_add` incident below.

**Worktree build trap hit setting this up**: a fresh `build.cmd` in the
worktree reported `BUILD OK` but silently produced no `func_lockstep.exe`
and no error - `tests/CMakeLists.txt` only defines that target
`if(F117R_GEN_DIR)`, which a bare `build.cmd` with no arguments never
sets. Fix: `build.cmd -DF117R_GEN_DIR=C:/Users/Tideg/f117-recomp-local/gen
-DF117R_BUILD_APP=ON` (the shared `gen/` directory every worktree should
point at, per [[f117-parallel-agents-workflow]] - never run the
translator in a worktree, it would regenerate the shared directory other
sessions rely on). Also hit: the reused worktree's old build directory
(a junction to `f117-recomp-local/build-<hash>`, keyed by the worktree's
own path so it persists across `git reset --hard`) had a stale ninja log
from Oct 6 that reported "no work to do" even for a target file I had
just deleted by hand; wiping that build directory and letting `build.cmd`
recreate it fixed it. If a worktree's build looks suspiciously fast or a
expected binary is simply missing after "BUILD OK", check both of these
before trusting the build.

## Route-coverage trap, hit twice in a row (10 Oct, 19:30-20:02 PDT)

Two "routes only" VGAME leads in a row (`0x0FB20` sky-detail setter,
then `0x01B9` a cold-start init/sound sequence) passed every lockstep
check at every seed cleanly, then failed the full gate's step 8 ("every
routes-only routine runs on a route") - no scripted route actually
reaches either one. Both reverted after the fact, each costing a full
~16-minute gate run to find out.

For the second one, before building, I traced its single call site
statically (`x86dec`, instruction-aligned, not a byte-scan) to a key-
dispatch branch gated on AX == 0x0B00 (scan code 0x0B, a "0" keypress),
then confirmed a route's recorded input script does send a literal "0"
during VGAME. That still was not reached. **The lesson: a route sending
the right-looking key somewhere is not evidence the exact code path
executes** - the key could be consumed by a different, earlier dispatch
entry, a different game mode/screen than the route is in when it sends
it, or my read of the AX encoding could simply be wrong. Static call-
graph tracing answers "is there a path," not "does any route's actual
play reach it." **The only check that actually answers the real
question is the full gate's own step 8** - there is no cheaper
substitute in this repo (no `callers.py`/`addrmap.py`/`F117_WATCH`
despite older memory notes suggesting otherwise; checked, none exist).

Going forward: prefer candidates that produce **comparable random-fuzz
states** at all (not "routes only") - those get a real verification
signal immediately from targeted lockstep, with no full-gate gamble on
reachability. Save "routes only" leads (DOS interrupt installs, fixed
init sequences gated on rare conditions) for when a route is already
known via other evidence to hit them, or accept the cost of checking
step 8 specifically before writing the next one.

## IMPORTANT CORRECTION: the combat/speed finding further down is stale

The "Combat rate falls with speed" line in "Goal and standing decisions"
below, and most of `docs/speed-sweep.md`'s middle sections, describe a
real effect measured *before* two measurement confounds were found and
corrected (RNG seed drift between CPU speeds; input-poll timing against
the simulation step). **The document's own final, most-corrected cohort
(the "Step-aligned arm" table, `docs/speed-sweep.md`, late in the file)
already found no statistically significant combat difference between 9
and 20 MIPS** once those confounds were fixed on top of the physics
repairs (D1TTL+D1SLOT+D1PROX+D1ACCEL+D1REAL): burst difference +0.1667
(95% CI 0..+0.5), damage selections +0.3333 (95% CI 0..+1.0) - zero
included, point estimate if anything slightly favoring the higher speed.
Quoted verbatim: *"This cohort shows no high-speed enemy weakness."*

This is not a claim that the issue never existed - earlier, less-corrected
cohorts in the same document show a real, solid effect (bursts -1.3 to
-1.5, CI excluding zero). It is a claim that once the *measurement itself*
was fixed, not just the game's physics, the effect was no longer
distinguishable from zero. Read `docs/speed-sweep.md` end to end (not just
grep for a conclusion) before citing this investigation's status to
anyone - this session got it wrong three times in a row in conversation,
each time by answering from an earlier section without reaching the
final, superseding one.

**Correction to the correction:** `docs/roadmap.md` (lines ~305-320) and
`docs/bugs.md` (lines ~167-173, the D1REAL entry) were checked directly
and are ALREADY accurate - both already state the corrected, final
conclusion (11 of 12 seed/step-aligned pairs matching, no high-speed
weakness). Only the "Goal and standing decisions" bullet immediately
below in this same file was actually stale (dated 9 Oct, written before
this correction existed), and it has already been fixed in place. No
further doc propagation is needed. The lesson here is really about
answering from a full re-read rather than a partial one, in conversation,
not about the project's own documentation being out of date - it wasn't.

## Phase 1 threshold tightening (10 Oct, 15:50 PDT)

Followed up on the owner's "are any P1 checks too loose" question: tightened
three numeric thresholds that had 1.75-3x headroom over their single
measured run, down to ~1.4x (`tools/pc_parity.py`'s `gog.max_drift_ms`
100->80 and `gog-music.max_timing_ms` 40->20; `tools/ref86box/
compare_timing86.py`'s `max_duration_diff` 0.35->0.30 and `max_drift`
2.2->1.5). Deliberately left DOSBox-X's `max_drift_ms=350` alone - its
looseness has a stated, specific reason (the comment: "real-time captures
varied; fast-forward ones are identical," i.e. DOSBox-X's own reference
capture isn't perfectly reproducible run-to-run), unlike the other four
which were just round numbers roughly double the measured value with no
recorded justification. Re-verified all three edited checks: the two
`pc_parity.py` ones on a fresh live run (GOG-only, no emulator needed -
`--no-86box --no-dosbox-x --no-save`), `compare_timing86.py`'s on the
existing saved `D:/f117-gate/pc-parity-profile-aware/` capture (reusing it
rather than re-running 86Box, since only the Python constant changed, not
the engine). All eight `tests/test_reference_timing.py` regressions still
pass (they read the module's `LIMITS` dynamically, not a hardcoded copy).
Updated the two doc passages that cited the old numbers literally
(`docs/roadmap.md`, `tools/ref86box/timing386.md`); left the 9 Oct historical
entry in `timing386.md` (the one describing a since-superseded 0.37s FAIL
against the then-current 0.35 limit) untouched, since it's a dated progress
record, not a current claim. This was a values-only change to test tooling,
not engine code - no full gate needed, just the targeted re-verification
above. Single-measurement margins, not variance-backed; if a future clean
run fails one of these for reasons unrelated to the engine, that's the first
thing to suspect.

## CPU/combat: a candidate root cause, investigated and closed (10 Oct, 16:25-16:38 PDT)

The owner redirected priority mid-session from Phase 2 to "perfectly fix"
the CPU-speed/combat issue, then asked whether anything besides combat is
affected by speed - which turned into a proper static audit (see
`docs/speed-sweep.md`'s new final section, "A candidate root cause: S's
own self-calibration", for full detail; this is a summary). Using the
recompiler's own decoder (not grep), every VGAME multiply-by-S was swept
and classified. Most are safe. One is a serious, NOT YET CONFIRMED
candidate: the routine that *measures and corrects S itself* (inside the
large multi-role function at VGAME `0x3ADA`) has a negative-value-into-
unsigned-MUL bug at `0x4447` whose consequence, traced bit-exactly in
Python (not hand math - `D:/f117-gate/audit-s-calibration/
trace_s_calib.py`), sets **S to exactly 0** for every tested starting S
from 16 to 40. If real, this would be upstream of every other S-dependent
system already investigated (missile lifetime, proximity, etc.) - a
single root cause rather than a parallel symptom.

**Follow-up this same session found the reachability answer, and then a
correction to the severity claim.** Traced `0x3ADA`'s one caller in the
reachable graph: VGAME `0x1BCE`, the literal main flight loop (canopy
draw, `game_input`, `0x3ADA`, check the END.EXE hand-off flag, loop while
flying) - this code runs every frame of normal flight, not some rare
mode. `docs/bugs.md`'s existing (pre-dating this audit) D1 writeup
independently corroborates this: `0x441D` is "the controller's entry,
which every frame passes once."

**But that same D1 writeup also records a 90-flight sweep at
9/12/16/20/40 MIPS, with and without the D1 fix - including unprotected
runs at 40 MIPS - with no reported crashes.** If the traced S->0->
divide-fault cascade were accurate as written, those unfixed 40 MIPS
flights should have faulted repeatedly. They did not. This is strong,
already-existing evidence that the "sets S to exactly 0" arithmetic trace
has a wrong precondition somewhere - most likely that the loop-index gate
is actually satisfied in ordinary play, or that the starting-S values fed
to the Python trace (16-40, chosen to span where `[0x43E8]` turns
nonzero) are values the game's own continuous calibration ever actually
reaches. **Both `docs/speed-sweep.md` and this entry have been corrected
in place to flag this as an unconfirmed hypothesis contradicted by
existing data, not a finding** - do not carry forward the earlier,
stronger framing from this same session's first pass.

**Settled the same session, by direct measurement, not further
inference:** `py tools/d1_check.py --data DIR --ips 40000000 --engine
interp --no-fix` (already-existing tooling) measured S directly over a
full 161.66 s flight at 40 MIPS with every fix off: **S=15 for all
71,852 samples - never once above 15**, more than 4x past the raw speed
where the traced bug's precondition (S>15) would first engage. **This
candidate is closed: no fix is warranted.** Whatever this routine's real
behavior is with actual game state, it holds S at a stable ceiling of 15,
not a runaway to 0. The static arithmetic trace was real as an isolated
computation but fed inputs (S=16-40) the game apparently never produces
at this point in its own logic; this is a case of the project's own
`f117a-predict-then-measure` lesson playing out directly - a careful
static reading produced a wrong prediction, and five minutes with existing
tooling settled it where further hand-tracing would not have.

The audit's *other* candidate (`0x2BFE` inside `game_input`, a
terrain-distance value truncated after a signed IMUL - see
`docs/speed-sweep.md`'s audit section) was never pursued this far and
remains open and unweighted if this kind of audit continues later; it
does not inherit this closure.

This investigation, start to finish, is also the clearest argument this
session produced for the project's lockstep + full-gate discipline:
twice today (this finding, and the `vgame_release_count` revert below)
reasoning that looked solid on paper did not survive contact with
real measurement. Keep defaulting to measurement over prediction.

Separately, two of the three P1 threshold-tightening and three doubtable-
investigation asks from earlier this session are already committed (see
below); the audit above was read-only, no code changed for it.

## Bug fix in an already-matched routine: vgame_eventlog_add (10 Oct, 19:35 PDT)

Attempted a new routine, `vgame_sky_low_detail` (VGAME 0x0FB20, far-called,
sets the sky's low-detail flag then calls an unmatched redraw helper).
It passed every lockstep check at every seed (0 comparable states,
correctly classified "routes only" like `vgame_isrclock_init`), but the
full gate's step 8 ("every routes-only routine runs on a route") failed:
**none of the ~34 scripted routes ever call it** - no evidence it's
correct beyond static reading. Reverted; not committed. Lesson: "routes
only" from the lockstep is not sufficient on its own - the gate's own
route-coverage check is the real bar for an unfuzzable routine, and it's
worth confirming informally (e.g. grepping the routes' recorded inputs
for whatever toggles the relevant setting) before investing in a
candidate that may end up with zero coverage.

While the gate was re-running for that attempt, its seed-7c check (this
commit's own derived seed) found a genuine, pre-existing bug in an
**already-matched, already-committed** routine from an earlier session:
`vgame_eventlog_add` (VGAME 0x04ABA). It cached its two stack arguments
(`kind`, `target`) via `arg()` at the top of the function, but the real
original reads them late - `mov al,[bp+4]` and `mov al,[bp+6]`, after
three prior writes to the event-log record. In this fuzzed state DS and
SS happened to alias such that one of those writes landed on the stack
slot holding `target`, and the original's late read observes its own
write while the matched code's early, cached read didn't. Confirmed by
full verbose trace (`AX 00FE vs 0001`, all other registers identical) and
by hand-checking `arg()`'s own doc comment ("whose BP frame is not yet
built") against the point it was actually being called - after the frame
was already built elsewhere in the same function made this easy to miss
on a skim. Fixed by reading both arguments at their correct point in
program order (via `c->r[R_BP]+4`/`+6` directly, not `arg()`, since the
frame is built by then) instead of hoisting them. Reverified on the
exact failing seed (0x4c357fd31ed6: 3,986 states, 0 mismatching), the
default seed (59,911 states) and 0xC0FFEE/0xDEADBEEF (clean), the full
775-routine suite (0 mismatching), and the full gate (0 DIFFERENT routes,
every check clean, exit 0). Committed/pushed. Scoreboard unchanged
(770/1,535 functions, P2 39.83%, All 71.78%) - this fixes an existing
match, it doesn't add one.

**Why this matters beyond the one routine**: this is the second time this
session a gate failure traced back to something *other* than the change
being verified (the first was the stall misdiagnosis, which turned out
to be environmental, not code). Full gate reruns compare against a
*new* per-commit seed every time (`this commit's seed 0x...`, derived
from HEAD), so previously-matched, previously-passing routines are not
permanently proven - each new commit's seed re-rolls the dice on every
one of them. A failure in the full gate always needs its MISMATCH line
checked for which routine actually failed before assuming it's the one
just added.

## Phase 2 checkpoint: vgame_free (10 Oct, 18:38 PDT)

VGAME 0x0F8EC (`vgame_free`) is matched, verified and committed/pushed:
the near-heap `free(ptr)` counterpart to the already-matched `malloc`
(0x0F91A, `vgame_malloc`) - both operate on the same heap descriptor at
DS:922E. Identified (not just guessed) by reading `vgame_malloc`'s own
comment, which names 922E as the heap descriptor, then confirming a real
call site at 0xF1CB (decoded instruction-aligned, not a byte-scan
false-positive) pushes a pointer immediately before calling 0xF8EC. Steps
the pointer back two bytes to its block header, sets the header's low
("free") bit, and advances the descriptor's free-search hint at [922E+8]
past the freed block when it reaches further than the hint already does.

Lockstep caught a bug on the first attempt: both of the routine's two
conditional jumps (`jae`, `jbe`) had their *own* instruction counted as
part of the icount total - I added the manual counts for the instructions
before each Jcc but left the Jcc itself out, undercounting the clock by 1
on whichever path exited early. Textbook missed-Jcc, exactly the trap
flagged in this project's own history. Fixed (folded each Jcc into the
preceding block's count) and reverified clean: 66,505/4,411/4,436
comparable states across three seeds, zero mismatches; full 775-routine
suite 2,509,526 comparisons, zero mismatching. Full gate PASSED clean (0
DIFFERENT routes, both instruction profiles and all three matched-routine
seeds zero mismatches, every routes-only routine ran, exit 0). Scoreboard:
775 addresses matched, 770 of 1,535 census functions, 65,702 of 179,213
bytes; P2 39.83%, All 71.78%.

**Lead-selection trap found along the way**: the census's `size` column
is NOT the function's real instruction/byte length - it's some other
distance metric (a 6-byte census entry turned out to be a 72-instruction
routine; a 7-byte entry was 336 instructions). Sorting unmatched leads by
census size to find "small, quick" routines is not a valid strategy.
Draft each candidate with `tools/matched_draft.py` first and judge size
from its own reported instruction count, not the census row.

## Phase 2 checkpoint: frame_mission_start (10 Oct, 18:08 PDT)

VGAME 0x01E72 (`vgame_frame_mission_start`, samples the BIOS tick count's
low word) is matched, verified and committed/pushed. Four instructions:
zero AH, INT 1Ah (BIOS get-time), `mov ax,dx`, ret. Used the project's
generic `sm4_int` helper (it only cares about the INT instruction's
address and the original SP/return address, not which vector or whether
the service is host-synthesized or real guest code) rather than writing a
one-off dispatch. Routes only - 0 comparable states at any seed, same as
`vgame_isrclock_init`. Full gate PASSED clean (0 DIFFERENT routes, both
instruction profiles and all three matched-routine seeds zero mismatches,
every routes-only routine ran, exit 0). Scoreboard: 774 addresses matched,
769 of 1,535 census functions, 65,699 of 179,213 bytes; P2 39.83%, All
71.77%.

**Trap hit and recovered while picking this lead**: grepped `matched.c`
for the census's `0x00986` (5 hex digits, as the census prints it) and
found nothing, concluded it was unmatched, wrote and registered a full
duplicate `vgame_init_mode` there. The targeted lockstep immediately
showed two entries at the same address ("graphics mode setup" and mine) -
`matched.c`'s own table spells the same address `0x0986` (4 digits, no
leading zero), so the literal-string grep silently missed the existing
entry. Reverted cleanly before any build/gate cost. **Fix for next time**:
cross-reference by parsed/normalized address (int value), never by
grepping the hex string in either direction - a short Python pass that
parses `MATCHED[]`'s own `(binary, bank, offset)` triples and compares
them as integers against the census rows is in this session's history;
worth turning into a real tool (`tools/addrmap.py` is referenced in older
memory notes but does not exist in this checkout - it should be written,
not assumed).

**Also hit and resolved**: the first lockstep run (60,000 states,
background) stalled for ~34 minutes at ~10s of CPU time - not a code bug.
Rerunning the identical command (same binary, same states, same seed)
foreground completed in seconds with a clean result, so it was an
environment glitch (most likely the sync client locking the freshly
linked `func_lockstep.exe` on first launch; this repo's working copy
lives in a synced folder, see `build.cmd`'s own comment on this). If a
lockstep run seems to hang with near-zero CPU use, check `Get-Process`
CPU time against wall time before assuming the new routine is at fault -
a genuine infinite loop in guest code would burn CPU, not sit idle.

## Phase 2 checkpoint: init_models (10 Oct, 17:33 PDT)

VGAME 0x0BAA2 (`vgame_init_models`, copies a model-type's byte table) is
matched, verified and committed/pushed. 40 original instructions, one
data-dependent loop, a found/not-found branch. Lockstep found two real
bugs the draft check missed: a missed `je` on the no-block path left the
clock one instruction short, and the loop's exit test compared a local
C copy of the loop index instead of writing the real `mov ax,[bp-6]`
side effect back to `c->r[R_AX]` first, so AX read stale after the loop
ended. Both fixed; reverified clean at three seeds (17, 0, 1 comparable
states respectively, zero mismatching). Full gate PASSED clean: 35/35
routes IDENTICAL, both instruction profiles and all three matched-routine
seeds (773 routines, 2,505,089/2,504,224/2,505,038 states) zero
mismatches, every routes-only routine ran, no overruns, exit 0. Scoreboard:
773 addresses matched, 768 of 1,535 census functions, 65,692 of 179,213
bytes; P2 39.82%, All 71.77%.

## Phase 2 checkpoint: isrclock_init (10 Oct, 17:00 PDT)

VGAME 0x01C0E (`vgame_isrclock_init`, hooks the timer tick vector) is
matched, verified and committed/pushed. Small (18 instructions), entirely
linear, used the project's established `sm4_int`/`SM4_AT`/`SM4_NEED`
DOS-interrupt helpers (same family as `vgame_restore_vectors`) rather
than a literal instruction transcription. Unfuzzable by random-state
lockstep (0 states comparable at any seed - "routes only," confirmed by
the full gate's own routes-only check that it actually ran). Full gate
PASSED clean: 17/17 unit tests, 35/35 routes IDENTICAL, both instruction
profiles and all three matched-routine seeds zero mismatches. Scoreboard:
772 addresses matched, 767 of 1,535 census functions, 65,585 of 179,213
bytes; P2 39.77%, All 71.76%.

Also confirmed (useful for next time): the census's "implemented_by"
column is unreliable as a lead-filter - at least three addresses this
session (`0xEE0C`, `0x4D5D`, and others checked in passing: `0xE3BF`,
`0xDB3C`, `0x4E5F`) showed as unmatched in a fresh `census.tsv` regen but
were already matched and registered. Always grep `src/matched/matched.c`
directly for `VGAME_47304, 0x0000, 0xNNNN,` before spending time drafting
a "fresh" lead.

## Phase 2 lead attempted and reverted: VGAME 0x4657 (10 Oct, 16:00 PDT)

Extended the already-matched `vgame_release_count` (0x462E) to cover its
declined continuation at 0x4657 (countermeasure dispensing - the census
holds this as a separate "function," but it's really the original's
`jg`-taken continuation of the same routine, reusing its frame; see the
existing comment at 0x462E). Fixed a forward-reference build error, then
three seeds and the full 771-routine suite passed clean (up to 35,212
states at one seed, 0 mismatching). **The full required gate still
failed: 7 routes DIFFERENT.** Random-state fuzzing did not exercise the
actual bug; real gameplay routes did. The change was reverted
(`git checkout -- src/matched/matched.c`) rather than left half-fixed, and
the tree rebuilt clean against the last good commit (`6840bc7`). The
lesson generalizes: this routine's weapon-lock/dispense branches require
specific state combinations (a type argument correlated with specific
table fields) that pure random fuzzing essentially never hits, same as
`vgame_target_damage` earlier this session - the full gate's real routes
are the only check that actually exercises them. 0x4657 remains
unmatched; next attempt should add targeted PLANTS entries for the
countermeasure-type/slot-state correlation (same technique used for
`vgame_target_damage`'s weapon-lock match) before trusting lockstep
alone, and should re-run the full gate before considering it done, not
just the matched-routine suite.

## Phase 2 target-scoring checkpoint (10 Oct, 15:41 PDT)

VGAME 0x073C8 (`vgame_target_damage`, "score and alert a target hit") is
matched, verified and committed/pushed - the densest routine hand-matched
this session (174 original instructions, many branches, calls to both
matched and unmatched helpers). A freshly regenerated `matched_draft.py`
instruction listing was cross-checked opcode-by-opcode and branch-by-branch
before transcribing, which caught no logic errors - but random-state
lockstep testing caught **four separate instruction-clock accounting
bugs** the draft cross-check could not (a missed Jcc instruction's own
clock in three places, and two pop instructions never counted at all), each
found by an exact clock-value mismatch (e.g. "clock 1853 vs 1851") that the
harness's `--verbose` instruction-by-instruction retrace pinpointed
precisely. This is the clearest evidence this session for why the project
requires lockstep verification rather than trusting a careful manual
transcription: dense branchy code is exactly where a careful reread still
misses things. Added two PLANTS/ARG_PLANTS entries to
`tests/func_lockstep.c` targeting the routine's weapon-lock match condition
(E304/E306 fields); confirmed via temporary debug instrumentation (removed
before commit) that most of this routine's incomparable random states are
the *original* interpreter side failing to complete cleanly through its own
many unmatched callees (0x0D14, 0x792E, 0xC436, 0xB9F6, 0xB991, 0x0F3D,
0x8462, 0x56BB) before matched code is even invoked - not a flaw in the
matched C. Three seeds pass clean; the full 771-routine suite passes
(2,505,085 states, 0 mismatching). Required full gate PASSED
(`D:/f117-gate/gate-target-damage.log`, exit 0): 17/17 unit tests, 35/35
routes IDENTICAL, both instruction profiles and all three matched-routine
seeds zero mismatches. Scoreboard: 771 addresses matched, 766 of 1,535
census functions, 65,448 of 179,213 bytes; P2 39.69%, All 71.74%.

The owner asked three follow-up questions about Phase 1 this session,
answered in full in the conversation transcript (not repeated here in
detail, but the findings matter for the next session): (1) what P1=100%
means (parity against emulators, not real hardware - already correctly
scoped); (2) are GOG DOSBox/DOSBox-X/86Box checked equally (no - 86Box is
strictly the most scrutinized: it alone gets the 386-profile cycle-accurate
timing check and register-level AdLib comparison; GOG DOSBox is checked
only via a frozen capture, no live save-roster check; DOSBox-X sits in
between); (3) are any P1 thresholds arguably too loose (yes - the
timing/drift limits in particular have 2-3x headroom over the measured
baseline, while the exact-picture-count checks have none). Concretely:
`tools/ref86box/compare_timing86.py`'s `max_drift=2.2` (measured 1.113, i.e.
room for the measured value to literally double and still pass) and
`tools/pc_parity.py`'s `gog-music.max_timing_ms=40` (measured -14..+1ms,
~3x headroom) were flagged as the two worth tightening, with
`max_duration_diff=0.35` (measured 0.214) and DOSBox-X's `max_drift_ms=350`
(measured 143-200ms, but explicitly justified by the comment as absorbing
DOSBox-X's own real-time capture noise, not engine slack) as secondary
candidates. The owner asked to try tightening them; in progress as of this
handoff - see whether `compare_timing86.py` and `pc_parity.py` show edits
beyond this point, and if so whether they were re-verified against the
*existing* saved captures (`D:/f117-gate/pc-parity-profile-aware/`:
`86box-sound/trace/frames.csv`, `machine386/ours/frames.csv`,
`86box-timing.json`) rather than a fresh emulator run, to avoid re-running
86Box/DOSBox-X unnecessarily while the gate above was still finishing.

## Phase 2 aircraft activate/destroy checkpoint (10 Oct, 14:30 PDT)

The two Phase 2 candidates this handoff left prepared-but-untested
(`ai_activation_candidate.c` at VGAME 0x066F8, `ai_destroy_candidate.c` at
0x072D8) are now matched, verified and committed/pushed. Both were
independently cross-checked instruction-by-instruction against a freshly
regenerated `matched_draft.py` draft before trusting them (every opcode,
operand and branch direction agreed); activate's callees are all
already-matched, driver-free routines (cockpit message, target name, string
copy/concat), so it needed no harness change. Destroy calls the sound-priority
gate (`0xD3F9`) as a callee, and that routine's own far call to the
uninitialised driver thunk at `0xD414` is only stubbed to RETF when
`0xD3F9` itself is the routine under test, not when it is reached as a
callee from elsewhere - so every one of 4,000 random states ran off into the
thunk and 0 were comparable until `0x072D8` was added to
`stub_driver_thunks`'s VGAME ip-list in `tests/func_lockstep.c` (this matches
what the prior handoff had already anticipated: "they need returning driver
thunks in func_lockstep for 66F8/72D8 too"; in fact only destroy needed it).
After the fix: three seeds (default, 0xC0FFEE, 0xDEADBEEF) each pass both
routines cleanly (about 9,200-9,250 comparable states a seed, 0 mismatching),
and the full 770-routine suite passes (2,505,075 states compared, 0
mismatching). Required full gate PASSED (`D:/f117-gate/gate-ai-slots.log`,
exit 0): 17/17 unit tests, 35/35 routes IDENTICAL, both 5,718,912-state
instruction profiles zero mismatches, all three matched-routine seeds zero
mismatches, every routes-only routine ran, none overran. Scoreboard refreshed
(770 addresses matched, 765 of 1,535 census functions, 64,988 of 179,213
bytes; P2 39.45%, All 71.67%). `docs/architecture.md` and `docs/roadmap.md`
updated in the same commit. No further Phase 2 candidates are staged; next
session should pick a fresh lead the same way (`matched_draft.py`, cross-check
against the Reimp's data addresses, lockstep at 2-3 seeds, full gate).

The owner then asked what Phase 1 reaching 100% means, and separately asked
me to think hard about what in Phase 1 is doubtable and how to tackle it. My
answer (reproduced here so it isn't lost): parity is proven only against
emulators (GOG DOSBox, DOSBox-X, 86Box), never real period hardware - the
single biggest standing gap, not independently fixable by an agent, already
correctly scoped as "second reference, not the target" in standing project
guidance. Concrete, addressable residuals: (1) only 4 of 6 AdLib channels are
confirmed matching 86Box's captured audio in `docs/roadmap.md`'s own text -
the other two are never mentioned as matching or mismatching, which reads
like a quietly unresolved question rather than a closed one; (2) 3 of 1,332
DOSBox picture comparisons and 1 of 86 86Box picture comparisons are
unmatched and explained as capture-phase/mid-load timing artifacts, a
judgment call never cross-checked with a second independent capture; (3) the
EXEC file-load timing model is a real file's measured cost for only three
named files (PLAYER.EXE/DSWAP.EXE/START.EXE), a coarse 3-bucket synthetic-size
model for every other load, even though the same investigation proved
size-based costing is wrong in general. The owner asked me to tackle these "when you can." Investigated all three
(read-only; no new captures run) once the gate above freed the heavy-job
window, and all three check out as already properly handled - this was the
right outcome of actually tracing the evidence instead of trusting a terse
roadmap line:
- **AdLib channels 3/4**: `tools/ref86box/build_86box.md` (lines 187-195)
  already has a controlled experiment, not a guess - changing 86Box's own
  VGA speed (`B86_VGA_FAST=1`) shifts the reference's own channel-3 key-on
  count by about the same margin as the ours-vs-86Box gap (8,986 to 9,022 on
  the same reference, against our 8,982), showing the two busy pitch-bend
  channels are paced by the game loop's real-time speed and will not agree
  between any two differently-paced machines. Explicitly marked "not a
  parity target," correctly.
- **Picture residuals**: the 3-of-1,332 DOSBox misses already have the
  repeat-capture check I was going to propose - a second independent
  capture matches 1,321 with a *different* 11 unmatched, proving the
  specific misses are capture-phase noise, not a fixed defect
  (`docs/architecture.md:411`). The 1-of-86 86Box miss (`p085`) has a
  specific, falsifiable mechanism on record: 86Box's capture catches
  START's roster screen mid-load because this machine's file reads finish
  before the first paint while 86Box's don't; tracked explicitly in
  `expected_misses86.txt`, not hand-waved.
- **EXEC synthetic-size bucket**: confirmed it does reach the measured intro
  window (MGRAPHIC.EXE/MISC.EXE/ASOUND.117 load at 5.65-7.56s, inside the
  checked span) rather than being safely out of scope as I'd guessed - but
  `timing386.md` (lines 309-323) shows its net effect is already folded into
  the measured +39.3 ms end-to-end drift, which passes `pc_parity.py`'s
  limit; the one check that still fails (the 0.37 s longest-scene-duration
  gap) is a different metric, already shown unrelated to any load.

No code or doc change was needed for any of the three; this paragraph is the
record so the next session doesn't re-investigate the same ground. The
standing, unfixable-by-agent item remains: parity is proven only against
emulators (GOG DOSBox, DOSBox-X, 86Box), never real period hardware - already
correctly scoped in standing project guidance as "86Box is the second
reference, real hardware is the target," and not actionable here.

## Phase 2 collision/sound/scene checkpoint (10 Oct, 13:37 PDT)

Code checkpoint a18b771 is committed and pushed; origin/master and local
master agree. Final verification found a clean tree and no gate, probe or
reference-emulator processes. This handoff update follows the passing gate.

Profile-aware references checkpoint bbb6337 is committed and pushed; full
gate PASS596sec, all35routes, bothinstructionprofiles, all3matchedseeds,
17selected CTests, routes-only coverage and event limits. Phase1 complete.

Current additions: VGAME1007 vgame_scene_cell_obstacle (nearest eligible
terrain obstacle, signed Manhattan metric, original model probe), D3F9
sound priority gate with unconditional engine reassertion, D87C finish
terrain drawing/reset driver origin/work counters/retain view matrix.
Original instructions independently reread. Census1007 replacement-name
lead was wrong; existing78FD wrapper comment corrected too. All preserve
stack locals, call state, registers, flags and instruction counts; room is
checked at calls and loop boundaries. Returning driver thunks on both sides
make sound/scene random-state checks exercise their post-call paths.

First256-state check caught a missing RET clock in the new code; corrected
all3returns. Corrected256:492comparisons/zero differences. Two4000-state
seeds(default5EED0F117A,C0FFEE) PASS7763/7777comparisons, including partial
stops; 9.7sec concurrently,2workers. Logs scene-target-{256,4000,c0ffee}.log
and result.txt; build-scene-candidates.log,17selectedCTestsPASS. No target
processes remain. Refresh census now768addresses/763censusfunctions,
64423/179213bytes; P239.15%,All71.58%. Private candidates/drafts stay private.

REQUIRED full gate PASS883sec, gate-scene-sound.log and
gate-scene-sound-result.txt(exit0):17selectedCTests,35IDENTICAL route pairs,
both5,718,912-state instruction profiles zero, all three768-address matched
seeds zero; every routes-only routine ran and none overran. Stages92sec
translate/build/tests,495freshcoverage,11byte-identicalregeneration,
284parity;34interpreter sessions legitimately cached. All task processes
kept0x00FFFFFFaffinity. No gate/probe processes remain. Commit+push together.

Usage83% at13:52PDT; wrapping at the owner's85% threshold. Original broad
roadmap goal remains active. Next Phase2 candidates prepared PRIVATELY,
NOT built/tested/installed: D:/f117-gate/ai_activation_candidate.c (66F8)
and ai_destroy_candidate.c (72D8), instruction drafts drafts-next/*.c.
Both independently reread; activation's original coordinates are ZERO
extended before the 32-bit shift (Reimp sign-extends; preserve original).
Destroy masks zero-endurance flags to01C1; don't claim all repeated calls
unconditionally retain the dead bit. Verify every clock/room count before
trusting either candidate; add only after targeted checks and a full gate.
They need returning driver thunks in func_lockstep for66F8/72D8 too; consider
exact SPANS66F8..683D and72D8..73C8 to exclude nearby callees' RETs.

An optional HUD/text style question was sent during this turn; no answer
received as of13:52. No preference implemented. Keep this owner decision
open. Other roadmap work remains; don't mark the broad goal complete.

## Profile-aware references checkpoint (10 Oct, 13:20 PDT)

CPU/combat checkpoint 05bdd27 is committed and pushed. Investigation of the
remaining Phase 1 item found a reference harness error, not a CPU charge
defect: pc_parity fed its default 9 MIPS capture into the 386 held-picture
test. Separate recorded 386 capture now supplies 86Box pictures, AdLib and
timing. Held pictures pair by exact pixel hash in order; all local holds
must match. Screen-off gaps and the final censored reference hold are
handled explicitly. Limits unchanged. Eight new regression tests pass,
alongside nine threat-profile tests and six parity-diagnostic tests; CI
runs the new reference tests and the threat tests.

Fresh full references PASS, exit0, D:/f117-gate/pc-parity-profile-aware:
GOG1329 exact, no multi-sample misses, 57ms drift; DOSBox-X1237 exact,
three multi-sample misses,143ms drift; both sound checks pass. GOG22840
identical AdLib writes, -14..+1ms. 86Box85 exact/one close/no unmatched,
first2000 AdLib writes and four channels identical. All21 local held
pictures match21of22reference, maxduration.214022sec/startdrift1.112912sec.
Both roster saves802bytes/zero differences. Finalhangar6.472724vs6.491997sec
(19.273ms), instead of the mixed-profile .371sec discrepancy. Phase1 item
complete; progress updated, broaderroadmap remains active. No runtime changes.

Required full gate PASS (gate-profile-aware.log/result.txt; exact result
gate-profile-aware-result.txt):17selected CTests,35IDENTICAL route pairs,
both5,718,912-state instruction profiles zero, all three765-address matched
seeds zero; every routes-only routine ran, none overran. Coverage reused its
verified unchanged-input cache;34interpreter sessions reused. Commit and
push this checkpoint together, then continue Phase2 understood code. Keep
0x00FFFFFF affinity, no agents authorized, monitor this thread's rate_limits;
wrap and commit+push when 5h usage reaches85%. Last observed50% at13:18PDT.
Full references log pc-parity-profile-aware-console.log/result.txt (exact
result name pc-parity-profile-aware-result.txt), report86box-timing.json.

## Controlled CPU/combat checkpoint (10 Oct, 13:07 PDT; full gate PASS)

The owner reset usage and resumed. HEAD remains 30f463d, previously pushed
with the full 24.1-minute PASS gate. This checkpoint adds Python pilot/
comparator regressions and documentation; game/fix source is unchanged.
The required full gate PASS in594seconds (9.9min):17selected CTests,
35 IDENTICAL route pairs, both5,718,912-state instruction profiles zero,
all three765-address matched seeds zero, every routes-only routine ran,
none overran. Translation/build21sec, parity573sec; coverage/regeneration
reused the verified unchanged-input/generated-code cache. Logs
D:/f117-gate/gate-combat-alignment.log and result.txt (exact filename
 gate-combat-alignment-result.txt), exit0. No probes/gate processes remain.
Commit and push this checkpoint together before continuing source work.

CPU/enemy investigation now validates the identified optional repairs:
TTL+SLOT+PROX(v2)+ACCEL+REAL(v2). The corrected natural cohort completed
24 flights in 263.2 seconds, all 12 mission identities matching. Active
clock rates are 1.000065/1.000292 at 9/20 MIPS after a diagnostic three-second
terminal exclusion on exited flights; retain all rows/counts for combat.
Every sampled S is 8. Raw full rates include frozen death sequences.
Interpreter/native natural seed2500 hashes, launch lists and every CSV byte
match at both speeds (793c45a47070b53d / 8729c698e4bed4e5).

The residual v5 cohort gap was confounded: START mission alignment does not
align VGAME's later BIOS-tick RNG seed (C880 -> EE1A; stored DS:9540).
Flight seeds differ by 25-33 ticks. Initialization also takes different real
time, and the old 200 ms pilot samples different parts of each 125 ms step.
Normal hangar-delay inputs align the later seed; step-aligned whole-frame
holds remove most feedback timing differences. No guest memory staged.

Final controlled 12-pair result: every launch count identical, 11 pairs also
match burst counts, damage counters and orbit duration exactly. Korea22500
has 7/9 bursts, 16/20 total damage selections and a pilot input divergence
after a partial-frame sample at 288.112 seconds. A mapped 9-input replay at
20 follows the same initial path but crashes before combat: original input
poll timing still matters during slow frames. No high-speed enemy weakness
in this cohort; this is not a proof of universal statistical equivalence or
exact whole-flight CPU determinism. Identified weapon/clock defects are
repaired in the optional corrected mode. Default parity mode stays original.
CPU investigation may now yield to the broader roadmap. Read-only work has
identified the remaining "386" held-picture failure as a harness mix-up:
pc_parity.py passes its default9MIPS DOSBox capture to compare_timing86.py,
not the actual386 run. The picture is the final stationary hangar.
Private held_scene_fast.py matches the saved true386 capture's held pictures
by content (6-bit DAC -> 86Box's RGB/FNV64, alphaFF), not duration:21of21ours
match21of23reference in order, maxdurationdiff.214022sec<.35, maxstartdrift
1.112912sec<2.2. Hangar ours6.472724 vs86Box6.491997sec, difference19.273ms.
Infer screen-off gaps as blanks; raw posted-frame holds count missing scanout
and overstate the reference hold by57ms. Artifacts held-scene-fast.{json,log},
held_scene_fast.py. Source captures frames-realfiles/ours (settings confirms
--timing386) and pc-parity-realfiles/86box-sound/trace/frames.csv. Next retain
this content/profile-aware check, make pc_parity use a separate386 capture
for86Box while DOSBox comparisons keeptheir9MIPSprofile, and rerun the
references/gate. Don't alter CPU cycle charges to fit a mixed-profile test. Do not tune balance to cohort means.

Retained tools: threat_profile.py telemetry v6 records flight_seed, supports
--launch-delay-ms and --align-steps (requires D1REAL). Step pilot is a different
control policy than v5, so don't interpret its means as a before/after fix
comparison. Comparator --match-flight-seed requires the additional matching
seed; +step arms/cache tags prevent silently mixing pilot policies. Nine
unit cases pass. Public MiddleEast seed2500 native/interp replays at both
speeds match the private phase experiment: d4abd70c56500017 (9),
2619b559a5c10b3b (20), every launch record and CSV byte. Four boots146.5sec.

Artifacts D:/f117-gate: threat-realtime/, threat-realtime-summary.txt,
threat-phase-corrected.jsonl and summary.txt, flight-seed-*-600-phase.*,
flight-inputs-*.log, public-phase-*.{json,csv}, replay-korea_strike-22500-20.*.
Initial threat-phase.jsonl has unequal Kuwait seeds: don't use it as final.
Final20 launch delays by family2500/22500/25000: MiddleEast1813/1483/1483,
Korea1813/1813/1813, Kuwait1345/1345/1345, NorthCape1758/1813/1758ms.
Controlled cohort269.9sec/8workers plus three corrected Kuwait repeats.
All task processes use verified 0x00FFFFFF affinity (leave8logicalCPUs).
No agents authorized. Continue until roadmap complete or 5h remaining<=15%;
at that threshold wrap handoff, commit+push and stop. Usage was10% at12:33;
read only rate_limits events from this thread's rollout for current usage.

## Verified physics/clock checkpoint (10 Oct, 09:47 PDT)

Owner's 85% usage threshold was reached at09:22. Work stopped for wrap-up;
the already-running REQUIRED gate completed PASS at09:47 in1447.2seconds
(24.1minutes). All35 route pairs IDENTICAL, both5,718,912-state instruction
profiles zero mismatches, all three765-address matched seeds zero, every
routes-only routine ran and none overran.17selected CTests passed.
Stages: build/tests20sec, coverage508sec, byte-identical regeneration12sec,
parity907sec. Logs D:/f117-gate/gate-physics-realtime.log and result.txt
(the exact result filename is gate-physics-realtime-result.txt).
This source/documentation checkpoint is committed and pushed together.
No experiments or gate processes remain active. Resume only when the owner
resets usage. CPU/combat remains first, before the broader roadmap.

D1SLOT66a8608 is committed/pushed. This checkpoint adds:
D1PROX v2 exact rational continuous octagonal-slant sweep, terminal-speed
band*8/9 (explicit S9 balance choice, not recovered warhead constant).
D1ACCEL integrates9/2 incoming speed units per simulated second, exact
fractions S1..15 with original S9 pattern. Both use per-machine tracks
with epoch/weapon continuity resets. d1physics_check.py: all21 staged cases
both engines +/-options pass, first S9/player hashes unchanged. Legacy
five-case proximity checks pass;20,000-path independent C clipping oracle.

Completed72 natural physics flights,23.7min/8workers. Original24 pairs
match, fresh22500/25000 add8, all four27500 pairs differ and are excluded.
Across32 matching pairs,20-minus-9 bursts-1.5312 (95%CI-2.6250..-0.5312).
Weighted full sampled world-clock rates9=1.2327,20=1.1239. Physics alone
has NOT fixed CPU-dependent combat. Details/reproduction docs/speed-sweep.md
final section; artifacts threat-physics-v2/ and summary files. Natural20
MiddleEastoffset2720 interpreter/native hash3893586487063407 and everyCSVbyte/
launchrecord match; native9offset2500 hash9087c775185e262f. Regenerated
sentinels match pre-gate references exactly. Early exits retained.

New optional D1REAL pins S8 (S4 at2x) at D441 entry so matched override cannot
bypass; original derives all rates.4359 admits eight frames per machine
second, waiting at event boundaries, exact fractional clocks. Lossless
catch-up preserves elapsed time; pause3A4E and quit-dialog2071 explicitly
clear deadline. Per-machine state resets at program/speed changes. Default
off; use --present interp for smooth display (about125ms picture delay).
CPUs unable to execute8frames/sec can still lag. Normal-input public
 d1real_check.py passes all SEVEN cases at9/20/40 MIPS both engines, every
case hash identical:240frames/30world sec normal,80/20 compressed,80/10
restored,0/0paused,24/3resumed,0/0quit-dialog,24/3cancelled. Artifactdir
realtime-checks/, private run_realtime_checks.py. Latest build/tests PASS
154sec,17 selected CTests (build-physics-realtime-v2.log).

IMPORTANT D1REAL airborne validation is still OPEN. First v1 cohort exposed
lost time from >=2period resync (9MIPS orbit7.9534fps). Its gate/cohort were
stopped09:15; logs/data renamed *discarding-v1*. Source now has lossless
catch-up, but fresh natural flights were NOT started because usage hit85%.
Do not claim the overall CPU/enemy issue closed. Next session: first check
final gate/commit below; then run private threat_realtime.py (24flights,
8workers, four routes x seeds2500/22500/25000 x9/20, all five combat options).
Output threat-realtime/ (new, currently empty), metadataREALv2. Verify all12
mission identities; compare baseline physicsv2 same seeds and both CPU arms,
world-clock rates, early exits. Then private realtime_sentinel.py interp9/20
compares natural MiddleEastseed2500 hash/launchrecords/CSV with native cohort.
No builds while DLL probes active. Keep CPU/combat ahead of broader roadmap.

First physics gate FAILED1860.7sec despite35 identical routes and both
5,718,912-state profiles passing: rotating seed66A860898896 falsely counted
SETUP callee1886 RET as walker177C's own return under default300h code range.
Exact near/far walker SPANS and retained matched_initializer_bounds CTest
correct the harness; runtime matched code unchanged. Interrupted v1 REAL
gate already had all three765-address matched seeds zero after correction.
The final gate independently passed every verdict (above).

Reimp HEAD remains9e0716dc502cd64501b0d52030ebef041825f907: separates render
and simulation cadence, initialS5, but signedTTL and lifetime underflow remain.
No agents authorized. Original app default20MIPS/no limiter unchanged.
Private experiments D:/f117-gate; generated/game data never committed.

## Verified D1SLOT checkpoint 66a8608 (10 Oct, 07:00-07:51 PDT)

Clean starting checkpoint `0174a2c`; the owner resumed after the usage reset.
Current code is uncommitted: optional D1SLOT at VGAME 6CA6 prevents a
seeker-cleared incoming lifetime from underflowing. It uses the shared DEC
semantics for flags, preserves instruction count and declines every
positive-life/player-slot case. Earlier D1TTL/D1PROX behavior is unchanged.
New `tools/d1slot_check.py`: all 15 staged cases pass in both engines +/-
option, all eleven control hashes unchanged (about two minutes, four boots).
Incoming slots 0/7 and player slot 8 are covered. Full CTest also passed all
15 tests (573 s, including its expensive serial 2,000-state matched check).
Use the gate's normal 14-test selection rather than repeating that serial
check unnecessarily.

All 72 natural flights are complete (10.4 min, 12 workers):
`D:/f117-gate/threat_slot.py`, `threat-slot-console.log`, `threat-slot/`.
Four routes x six seed families x (9 TTL+PROX, 9/20 TTL+PROX+SLOT), reusing
the earlier 20 TTL+PROX arm after its control reproduced `7b5cc08d65027187`.
All 24 mission identities match across all four arms. Adding SLOT increases
bursts/flight by 2.1667 at 20 MIPS and 2.375 at 9. With the same three options,
20-minus-9 bursts are -0.9583 (95% paired CI -2.2083..+0.25): not evidence
of equivalence. More damage changes survival/exposure; retain early exits.
Full figures and reproduction are appended to `docs/speed-sweep.md`.

Fixed interpreter/native Middle East 20 MIPS offset2720 matches hash
`1f85212a6ffbf801`, CSV bytes and all missile records: 16 launches, 9 bursts,
20 total damage selections, early exit. Raw fixed 9/20 traces (`ghost-slot/`)
have zero abandoned-slot episodes/slot-seconds and match cohort hashes
`17725de70b98ba07` / `1f85212a6ffbf801`.

Independent constant-speed/heading SA-12 pass (`proximity_pass_probe.py`,
`proximity-pass-{interp,recomp}.json`) still hits at S=8 and misses at S=15
with TTL+PROX+SLOT. Both engines match, hashes `f2a3a1eb93590c5a` /
`a3d3b2e93b8afe35`. Speed28, zero agility, terminal speed frozen28,
lateral22/longitudinal70/altitude100. The PROX S9 floor24 is below closest
slant25.125, while original S8 reach28 hits. First probe allowed acceleration
and both hit; do not confuse it with the corrected frozen-speed result.

REQUIRED FULL GATE PASSED: `D:/f117-gate/gate-d1slot.log`, result
`gate-d1slot-result.txt`, exit 0, 1588.4 s. All 14 CTests, 35 identical route
pairs, both 5,718,912-state instruction profiles and three 765-address
matched seeds passed; every routes-only routine ran and none overran.
Coverage added no code. The attempted ctypes affinity wrapper failed;
corrected parent/descendants early in coverage and verified 0x00FFFFFF.
`affinity_launch.py` now uses pointer-width ctypes signatures, checks native
return values and reads the resulting mask. The earlier cohort's affinity
attempt was likewise ineffective (only 12 workers, so still limited jobs).
Actual Processor % Processor Time was 52% during parity while WMI incorrectly
reported 96%. Prefer the former for load checks.

This checkpoint has passed the required gate and is ready to commit/push.
Then repair remaining proximity
sampling with a physical band plus swept path, rather than raising a floor
to match cohort means. Private `swept_design_check.py` checks exact octagonal
slant minima at piecewise-linear breakpoints: 2,000 random paths, reversal
and subdivision invariant and <=202,000 sampled distances. This is only a
geometry design check, not a game fix. Guidance acceleration increments one
unit on odd frames (7209..722B), with no S normalization; independently
confirmed by `acceleration_probe.py`: normal SA-5 initial1/terminal28,
steering disabled, after two simulated seconds speed9 at S8 versus16 at
S15, both engines hash `c48afd58d9ae6ea2` / `fc9ee748c9424f67`. This
favors high-S acceleration and cannot alone explain weaker combat.
The regenerated native fixed flight (`d1slot-regenerated-native.*`)
retains the pre-regeneration hash, every launch record and every CSV byte.
No probe or gate process is still running.
No claim CPU/combat is solved; do not move to the rest
of the roadmap yet. The Reimp HEAD remains `9e0716dc502cd64501b0d52030ebef041825f907`.

## Previous session checkpoint

**This batch (9-10 Oct): four independent pieces of work, three committed and
pushed, one left as an investigated-but-unwritten lead.** The owner asked for
all four to be touched in one session (p037, EXEC/overlay charging, a
controlled threat-profile measurement, and a Phase 2 routine), then mid-
session set the goal to complete the roadmap, then changed it to wrap up for
handoff. Three landed cleanly; the fourth (Phase 2) produced a well-verified
*identification* of a routine worth matching next, not a committed match -
writing one by hand carries real correctness risk (room-claim coverage,
preserving every intermediate memory write across a possible mid-routine
decline) that this session chose not to commit without a lockstep pass, given
the time left. See item 4.

## Current top priority: CPU speed and enemy effectiveness (10 Oct investigation)

Latest code checkpoint: `5fcdd04`, committed and pushed to `origin/master`.
The full gate and all private cohorts/probes below are complete; no driver
is still running. Continue with the underflow repair below before the
broader roadmap. All generated code, game files and experiment output
remain outside this repository.

The owner wants this resolved before continuing the roadmap goal, and asked
for both a Reimp check and an independent investigation. This checkpoint
adds two optional mitigations and corrected measurements; do not mistake
the earlier matrix for sound combat evidence.

- The Reimp at `9e0716dc502cd64501b0d52030ebef041825f907` decouples simulation
  from rendering, paces simulation from S, starts S at 5, and uses a 60 Hz
  real-time game clock by default. Its `frame_weapons.c` still compares
  incoming lifetime signed. Its simulation-clock tests check display-rate
  independence, not combat. Older catalogue claims about S oscillation are
  not a demonstrated cause in the original's full loop.
- Independently confirmed signed-life bug: ground SA-5 life is `150*S*16`,
  crossing 32767 at S >= 14. VGAME 708D treats nonzero life as active, while
  6F19's signed JLE suppresses damage. Optional `D1TTL` admits valid unsigned
  life through 36000, retaining suppression of abandoned FFFF slots. All
  seven staged cases pass in both engines, fixed/unfixed, with identical
  engine hashes. Natural 600-second fixed Middle East flight at 20 MIPS,
  offset 2720: SA-5 bursts at 293.32 seconds instead of missing; interpreter
  and recomp hash `0559ff3b220737ef`, CSV and launch records identical.
- The old observer hid negative SA-5 lifetimes, missed burst resets between
  polls, and lost damage counters on exit. Seeker 72A5 can clear life before
  move 6CA6 decrements it to FFFF; these abandoned slots are not new launches.
  Current tools read the official launch count and valid unsigned life,
  detect burst crossings, retain early-exit damage, and record S/counters/
  detection inputs. Damage counts selections, not missile impacts.
- The old adaptive pilot rotated at fixed instruction deadlines (8.5 s at
  20 MIPS versus 18.9 s at 9), causing matched fast missions to crash before
  engagement. Startup now keeps authored 9 MIPS seconds. Its orbit also had
  incorrect negative wrapping and outward radius correction; both fixed and
  checked in eight directions. Historical combat conclusions are superseded
  in `docs/speed-sweep.md`.
- Aircraft fire checks are staggered once per 16*S frames at stable S, not
  per frame; ground 5046 runs every frame (visibility skips only repaint).
  Detection has no direct S input. Corrected in `docs/bugs.md`.
- A second mechanism remains: proximity reach `(speed<<3)/S` shrinks with S.
  A staged SA-12 hits at S=8 but misses at S=15 with positive life and equal
  starting geometry. Experimental optional `D1PROX` gives incoming reach an
  S=9 floor, without changing movement/guidance/clocks or limiting frames.
  This is an explicit balance choice, not a recovered original radius.
  Built successfully (108 s). All five staged cases pass under both engines
  +/- D1PROX; engine hashes match and the S=8/9 control hashes are unchanged.

Running private cohort: `D:/f117-gate/threat_aligned_v5.py`, 96 flights,
4 routes x 6 seed families x (9, 16, 20, 20+D1TTL), 600 seconds, 12 workers.
Offsets 110/220 ms at 16/20 align START seed families; verify mission fields
rather than assume alignment. Output `D:/f117-gate/threat-aligned-v5`, console
`threat-aligned-v5-console.log`. All 96 are complete; all 24 mission identities match across arms.
Official launches/flight: 8.5417 / 8.4583 / 7.7500 / 7.7083; bursts:
4.1250 / 3.5000 / 2.7500 / 2.8333. 20-minus-9 launch CI includes zero,
but burst CI is -2.1667..-0.6250. Full report is appended to speed-sweep.md.
Public comparator output: `D:/f117-gate/threat-aligned-v5-summary.txt`. Earlier
v3/v4 experiments are diagnostic history, not the final cohort. Final staged
TTL outputs: `D:/f117-gate/d1ttl-final-{engine}-{False|True}.json`.

Both cohorts are now complete: 96 baseline/TTL flights and 24 combined
D1TTL+D1PROX flights, every mission pair matching. Combined 20 MIPS:
7 early exits, 7.3333 launches, 3.375 bursts, 6.75 damage selections per
flight, 4669.8 orbit seconds. Against 20+D1TTL, burst gain +0.5417
(95% paired CI +0.2083..+0.8750). Against 9, a deficit remains: -0.75
bursts (-1.4167..-0.1667). The options are partial mitigation, NOT a
completed CPU/combat fix. Full report and artifact paths are in
`docs/speed-sweep.md`; comparison tool is `tools/threat_compare.py`.
Combined natural interpreter/recomp flight (Middle East, 20 MIPS,
offset 2720) is identical: hash `7b5cc08d65027187`, all CSV rows and launch
records equal. D1PROX staged five-case checks agree in both engines +/-
option, S=8/9 control hashes unchanged. All 14 C and 97 Python tests pass
(one Python test skipped).

Full required gate PASSED (1,499 seconds):
`py tools/build_recomp.py --data D:/GOG/F-117A --seed-coverage C:/Users/Tideg/f117-recomp-local/coverage --jobs 16`
Log `D:/f117-gate/gate-combat.log`. All 35 routes identical, both
5,718,912-state instruction profiles and all three 765-routine seeds zero
mismatches; 14 C tests passed, every routes-only routine ran, no overruns.
Coverage added no code; 34 fresh interpreter sessions. The gate launches
three eight-shard matched seeds concurrently,
so 16 route workers still oversubscribed 32 threads; restricted the gate
parent and its 49 descendants to affinity mask 0x00FFFFFF (24 logical CPUs),
leaving eight for the desktop. Future children inherit that mask. This is
process-local and ends with the gate, not a persistent machine setting.

This checkpoint includes four new files (`d1ttl_check.py`,
`d1prox_check.py`, `threat_compare.py`, `tests/test_threat_profile.py`).
Do not call the CPU/combat issue solved. Investigate the residual before
moving to the rest of the roadmap. The 9 MIPS cohort has burst-time S mostly
7/8 (23/46 of 99 bursts; mean 8.6465), versus the parked S=9 floor chosen by
D1PROX. Any different calibration should be tested on new seeds, not tuned
until this cohort matches. Continuous swept collision with an explicit
reference radius is another possible design direction; not implemented.
Seeker-abandoned life can still occupy a slot (72A5 clears before 6CA6
underflows); D1TTL guards those FFFF values from damage but does not repair
that separate slot-reclamation defect.

Raw-slot follow-up is complete: `D:/f117-gate/ghost_trace.py`, outputs
`ghost-trace/{9,20}-2500.json`. Middle East hashes reproduce the baseline
cohort exactly. Abandoned slot occupancy over the common first 472.9 s is
1104.8/1295.2 slot-seconds at 9/20 MIPS; whole 20 flight is 1894.0. Both
still launch 11 times. Ground 54B4/air 65C3 select slot index & 7 and require
zero life, so global saturation is unnecessary to block a given launcher.
This defect also remains in Reimp. Test a separate saturating-countdown
option rather than silently changing the already-measured D1TTL behavior.

Held-out cohort complete: `D:/f117-gate/threat_heldout.py`,
console `threat-heldout-console.log`, output `threat-heldout/`. Four routes
x new seeds 15000/17500/20000 x (9 unmodified, 20+D1TTL+D1PROX), 600 s,
eight workers, parent/children affinity 0x00FFFFFF shared with gate; CPU was
49% before launch. Took 9.9 min, no tool errors, all 12 mission pairs match,
three early exits per arm. Bursts 4.5 at 9 versus 4.0 at 20; paired difference
-0.5 (95% -1.75..+0.6667). Pooled with the earlier 24 pairs: -0.6667
(-1.25..-0.0833), so CPU/combat remains open. No calibration changes were
made. See `threat-heldout-summary.txt` and the appended speed-sweep report.

Staged underflow reproduction now passes in both engines:
`D:/f117-gate/slot_underflow_probe.py`, `slot-underflow-{interp,recomp}.json`.
SA-5 class 1, off-axis behind the weapon, positive life 1000 -> FFFF at
both S=8 and 15, no damage, identical per-case engine hashes
`76d07a07e0d330d9` / `7828a2d70478e5a1`. The initial exact-180-degree setup
did not cancel (signed abs edge); corrected to an off-axis bearing.
Next implementation candidate: a separate optional hook at VGAME 6CA6
that keeps incoming life zero when the seeker has cleared it, while
retaining DEC flags and leaving every positive-life countdown untouched.
Test zero/one/positive-life controls, both engines and S values, then natural
flights. Compare 9 and 20 with the SAME option set as well as the original
9 MIPS balance reference. Do not silently change D1TTL: earlier cohort
numbers measure its signed-guard repair alone. A counter bound is not a
permanent identity check; an old abandoned value could decay below 36000.

The active goal remains roadmap completion, with the user's condition to
wrap/commit/push/stop at <=15% five-hour allowance remaining. Local session
rate-limit telemetry last reported 84% used (16% remaining), near the wrap
threshold. Check fresh telemetry before starting another code/build cycle.

## What to do first

1. **`git status -sb` first.** By the time this session ends, master should be
   clean and pushed through the real-file EXEC calibration commit (below);
   confirm that landed before doing anything else. If the working tree is
   *not* clean, something interrupted the final commit - read what is staged
   and finish it or roll it back deliberately, do not assume it is abandoned
   work (this session itself found a previous session's uncommitted,
   finished-looking diff sitting in the tree at start - investigated, found
   sound, committed it. The reverse mistake, deleting someone's good work, is
   worse than the delay of checking).

2. **p037.png, investigated and closed** (`66a664e`). A single captured
   instant in the title screen's fade-in: our nearest frame already beats
   both frames next to it, so 86Box's capture simply lands between two of
   ours. Added to `expected_misses86.txt` with that reasoning. Closed.

3. **EXEC and overlay loads, charged under the 386 profile, in two steps**
   (`05b453c`, then a further real-position calibration - confirm its commit
   landed, see item 1). Both `dos_load_program` and `dos_load_overlay` read
   the whole file through `dos_read_whole` with no charge at all before this.
   - Step one (`05b453c`): three measured sizes (1 KB, 9.5 KB, 47 KB -
     `exec1989.py`), charged by which a real load is closest to
     (`T386_EXEC_SMALL/MEDIUM/LARGE`). A fifteen-size follow-up
     (`exec_sizes.py`) came back non-monotonic in size (2 KB cost less than
     1 KB; 40 KB cost almost double 47 KB): EXEC's cost is seek-distance
     dominated, like an ordinary read, not size dominated - the same shape
     problem `T386_FILE_READ_SEEK` already has. Intro end drift: +428.5 ms to
     +96.4 ms, same 1,237 of 1,275 pictures exact. Gate:
     `D:/f117-gate/gate-execfix.log`, 35 routes identical, 0 mismatches.
   - Step two (real positions, 10 Oct): PLAYER.EXE, DSWAP.EXE and START.EXE -
     the three loads that actually dominate the drift - measured directly at
     their own position on the reference disk (`realexec.py`: open, read-
     whole, close, by name, already in place), not as same-size synthetic
     stand-ins. All three cost 27-45% more than their synthetic equivalents.
     `T386_EXEC_PLAYER/DSWAP/START` charge these three named files their own
     cost; any other load still uses the synthetic-size tiers. Drift: +96.4 ms
     to +39.3 ms, same 1,237 of 1,275 exact. `pc_parity.py` now passes the
     86Box picture check too (p037 above); one check still fails, the
     longest-scene timing difference at 0.37 s against a 0.35 s limit - a
     *different* held picture than the measured loads (ours 100.22-106.40 s,
     86Box's counterpart 150.33-156.88 s), unmoved by either EXEC charge.
     **Confirm the gate for this step passed and the commit landed**
     (`D:/f117-gate/gate-realfiles.log`; it was mid-run - specifically its
     verbose MISC.EXE coverage route, which writes one log line a clock while
     genuinely computing, not hung - when this session had to stop watching
     it). If it did not land, the diff is `src/cpu/timing386.h`,
     `src/machine/dos_programs.c` (plus the two doc updates below) and is
     safe to finish: the build already succeeded and `pc_parity.py` already
     passed on it (`D:/f117-gate/pc-parity-realfiles.log`) before the gate was
     started.
   - Next step, not started: identify the held picture behind the 0.37 s
     longest-scene gap (it is not a program load) and what inside its span
     the original spends 0.37 s longer on.

4. **Controlled threat-profile combat measurement, complete** (`77632c6`,
   `da7409a`). The existing typed routes pass the primary target once and
   hold a straight heading after, drawing about a quarter of a launch a
   flight - too sparse to show a speed effect. `tools/threat_profile.py`
   instead approaches and strikes the target as `strike_pilot.py` does, then
   orbits the target's own coordinates (no enemy site position read or
   assumed) for the rest of the flight. 144 flights (four ground-strike
   theatres, three speeds, twelve boot-clock seeds), complete, no errors:
   **a real fall in enemy launch rate from 9 to 16 to 20 MIPS** (9.08, 6.48,
   3.81 launches a flight; bootstrap 95% intervals on the mean exclude zero
   for both differences from 9 MIPS), surviving normalising for orbit
   exposure time (so it is not just shorter flights from earlier bingo fuel
   at higher speed). Misses, bursts and player hits taken all fall by about
   the same proportion too. **This is now the current top priority** - see
   "Current top priority" above for what has been found about the mechanism
   and the concrete next steps. Raw data: `D:/f117-gate/threatmatrix/
   runs.jsonl` and `samples/`.

5. **Phase 2 lead, investigated, not written: VGAME 0x01007.** The census
   (`D:/f117-gate/census.tsv`) names this address `scene_world_replace`, but
   that is wrong - cross-checking the disassembly's data addresses (0xC6B2,
   0xBA42, 0x49AC/AE/B0, 0xE476, 0x8590, and the per-type tables at 0x0960 and
   0x0511/0x0510) against the Reimp's source by *address*, not by the
   census's name guess, finds an exact match to a different function:
   `scene_obstacle_probe` (`../F-117A Reimp/src/core/scene.c:8`), the
   terrain/object collision probe - the routine behind the `0xC6B2 -> 0x9F96
   -> flight_end(2)` chain this project's own notes already reference. Fully
   traced against the disassembly (`matched_draft.py --ip 0x1007`, kept at
   `D:/f117-gate/drafts/vgame_lead_1007.c`, never commit the draft itself) and
   confirmed instruction-by-instruction, including resolving what its three
   callees are: `0x0810` is **already matched** as `vgame_scale_by_level`
   (`scene_cell_scale`), `0x0871` is **already matched** as
   `vgame_scene_cell_index`, and `0xF018` is the runtime's shared unsigned
   32-bit shift (referenced in `vgame_scale_by_level`'s own comment) - DX
   carries through unmodified between a `scale_by_level` call and the
   following `0xF018` call, which is why the routine never touches DX
   explicitly despite shifting a 32-bit value. `0xEE0C` (abs16, one word
   argument) is not yet matched but is trivial (8 instructions, drafted at
   `D:/f117-gate/drafts/vgame_lead_ee0c.c`) and is a good small first step
   next time. A full idiomatic rewrite of 0x1007 itself was drafted
   (not committed - it lives only in this transcript and a scratch file,
   `.../scratchpad/scene_obstacle_probe.c`, not in the repository) but never
   built or lockstep-checked, and should not be trusted without that: the
   room-claim at each of its ~12 call/loop boundaries was counted by hand
   from the instruction draft and has not been verified, and getting a room
   claim wrong is the kind of bug that stays silent until a route runs out of
   budget exactly inside this routine (see `f117r-room-claim-must-cover-
   longest-path` in the project's memory). Before trusting or committing it:
   rebuild, run `func_lockstep` targeted at this address with a few seeds,
   and only then fold it into `src/matched/matched.c` near
   `vgame_scene_cell_index` (both are in the same `NEAR_THEN`/`FRAME` macro
   scope, matched.c lines ~13430-16091) and the gate.

## Goal and standing decisions

- **Combat rate falls with speed - STALE, see the correction at the very
  top of this document.** This was real in early, less-corrected cohorts;
  the investigation's own final, most-corrected cohort (after fixing two
  measurement confounds, not just the game's physics) found no
  statistically significant difference. Read the top-of-file correction
  and `docs/speed-sweep.md` end to end before treating this bullet as
  current.
- **Phase 1's remaining item: the last 86Box timing check.** One check remains
  failing in `pc_parity.py`: the longest-scene duration difference, 0.37 s
  against a 0.35 s limit, in a held picture from 100.22 s to 106.40 s (ours)
  that is not the program-load stretch already fixed. See item 3's last
  bullet.
- **Phases.** Phase 1 (parity, including the 386 profile) is close:
  `pc_parity.py` has one failing check left (above); the 386 profile's
  roadmap checkbox should be closed once that is resolved or understood.
  Phase 2 (named, matched code): 765 of 1,535 census functions matched; see
  item 5 for the next lead. Phase 3 (fixes and presentation): open items are
  owner decisions (the HUD/text at 4K) or need a real display (Phase 4
  overlap). Phase 4 (checked by people): untouched, and most of it - a person
  playing it, a listening check - cannot be done by an agent at all; that is
  not a gap to close, it is the phase's own definition.
- **Public repo, code only.** No game data, no generated C. Never commit
  `references/` (git-ignored).
- **No AI attribution** in commits or PRs (the owner's global CLAUDE.md
  outranks harness reminders that ask for it).
- **Commit and push together**: a code change is pushed after the gate
  passes; docs-only commits may follow. `py tools/progress.py --title` prints
  the scoreboard every commit title starts with.
- The owner is not a software engineer: explain trade-offs plainly and flag
  overengineering.
- **Check system resources before stacking parallel jobs.** This session ran
  a 24-job combat sweep, the full gate, and `pc_parity.py`'s three emulators
  at once; CPU pinned at 99% and the owner's own typing lagged before they
  noticed. Check `Get-CimInstance Win32_Processor | Measure-Object
  LoadPercentage -Average` before adding a second heavy job, and know that
  stopping a background task running a Python `multiprocessing.Pool` (as
  `threat_profile.py` and `speed_sweep.py` use) does not kill the pool's
  workers - they are children of the script's own process, invisible to the
  task-stop mechanism, and must be found and killed as a process tree
  (`Get-CimInstance Win32_Process -Filter "ParentProcessId=..."`,
  recursively). See `f117-check-resources-before-parallel-jobs` in the
  session's saved notes for the full account.

## Where things are

- Game: `D:\GOG\F-117A` (never mounted or written by a tool; copy per run).
- Generated C: `C:\Users\Tideg\f117-recomp-local\gen` (never committed);
  coverage in `...\coverage`; route runs in `...\runs`.
- This session's artifacts (all on D:, private, not committed):
  `D:/f117-gate/p1-dos-probe/exec_sizes.py` and `realexec.py` (the EXEC-size
  and real-file probes, with their `sizes1`/`realexec1` outputs);
  `D:/f117-gate/frames-execfix`, `frames-realfiles` (the two `frames386.py`
  before/after runs, against the saved `D:/f117-gate/frames-1989/box`
  86Box capture); `D:/f117-gate/pc-parity-execfix`, `pc-parity-realfiles`
  (the two `pc_parity.py` runs); `D:/f117-gate/gate-execfix.log`,
  `gate-realfiles.log` (the two gates); `D:/f117-gate/threatmatrix/` (the
  144-flight combat sweep, `runs.jsonl` and `samples/`);
  `D:/f117-gate/drafts/vgame_lead_1007.c` and `vgame_lead_ee0c.c` (Phase 2
  drafts, never commit).
- Research (read only): `D:\f117-gate\macresearch\` (Mac 2.1, 2.3.1 and 2.3.2
  listings, the Amiga listing, `dos_ctl_sim.py`). The archives are in the
  Reimp's `reference\` folder.
- 86Box (`D:\86box`): `vmt386dos500` (the default reference; 1 MB, the 1989
  3500 rpm drive preset), `vmt386dos500h` (4 MB, HIMEM.SYS, DOS=HIGH; not the
  reference), `vmt386` (FreeDOS), `vmt386dos401`, `vmt386dos622`. Built by
  `tools/ref86box/build_msdos_vm.py` from the owner's copies in `references/`.
- Worktrees (`D:\f117-wt`): every Phase 2 and live-path branch is merged into
  master. Remove merged worktrees when convenient (`git worktree remove`).
  **Something else was working in this same checkout concurrently with this
  session** (a handoff commit, `65435f2`, appeared mid-session describing
  this session's own then-uncommitted work as if ending a separate session) -
  if a parallel-agents workflow is in use, confirm it is using its own
  worktree (`D:\f117-wt\...`), not this main checkout, to avoid two sessions
  racing on the same working tree.

## Build and check

- **Build** (PowerShell, not Git Bash's `cmd /c` - that rewrites paths and
  can silently no-op; see "Traps" below): `cd` to the repo in PowerShell,
  then `.\build.cmd -DF117R_BUILD_APP=OFF` (core, `f117run`, tests) or
  `-DF117R_BUILD_APP=ON` (adds the SDL app `f117a`). Look for `BUILD OK`.
- **Gate**: `py tools/build_recomp.py --data D:/GOG/F-117A --seed-coverage
  C:/Users/Tideg/f117-recomp-local/coverage`, about 20-30 minutes, run in the
  background, nothing else heavy at the same time (see "Check system
  resources" above). Read the tally, not the exit code: unit tests (14, 0
  failed), steps 6/6b (every instruction against the interpreter, both
  timing profiles, 0 mismatching), 7/7b/7c (every matched routine at three
  seeds, 0 mismatching), 8 (every routes-only routine ran), 9 (no OVERRUN),
  and the count of routes reported `IDENTICAL` (35, going in).
- **PC parity**: `py tools/pc_parity.py --data D:/GOG/F-117A` (about 12-15
  minutes, runs three emulators at once - its own internal parallelism is
  fine, just do not add a second heavy job alongside it). All checks pass
  except the 86Box timing check (above).
- **Matched lockstep**: `py tools/func_lockstep_par.py --states 4000
  --verbose [--seed S]` (eight shards, about 3 minutes). New routines start
  from `py tools/matched_draft.py --data D:/GOG/F-117A --module VGAME.EXE
  --ip 0xNNNN`; drafts are never committed.
- **Census**: `py tools/reimp_names.py --reimp "../F-117A Reimp" --gen
  C:/Users/Tideg/f117-recomp-local/gen --out D:/f117-gate/census.tsv`, then
  `py tools/progress.py --census D:/f117-gate/census.tsv`. **The census's
  name guess can be wrong** (item 5 found one): verify a lead by cross-
  referencing the disassembly's actual data addresses against the Reimp's
  source, not by trusting the name alone.
- **Threat-profile sweep**: `py tools/threat_profile.py --data D:/GOG/F-117A
  --out DIR --routes R... --speeds 9 16 20 --seconds 600 --offsets-ms
  0 2500 ... --jobs N` (N sized to leave headroom for anything else running;
  resumable - unfinished tags are simply absent from `runs.jsonl`).

## Current state

- **Scoreboard**: P1 97.93%, P2 38.88%, P3 67.00%, P4 42.90%, All 70.63%
  (unchanged this session: nothing new was matched or named, only timing and
  measurement work).
- **Phase 1**: see items 3-4 above. The 386 profile is close to done; one
  86Box timing check remains.
- **Phase 2**: 765 of 1,535 census functions matched (63,917 of 179,213
  bytes). See item 5 for the next lead (`vgame_scene_obstacle_probe`,
  VGAME 0x1007) and `vgame_abs16` (VGAME 0xEE0C, trivial, drafted) as an
  even smaller first step.
- **Phase 3**: 12 switches including D1TTL and the D1PROX balance option;
  10 of 14 top-level catalogued defects have a switchable fix. CPU/combat
  remains open; signed life and proximity distance are partial mitigations.
  Open: D10, D35, D36 (see `docs/bugs.md`). Presentation: the owner's open
  decisions (HUD/text at 4K) and a real-display check remain.
- **Phase 4**: untouched; most of it requires a person.

## Traps that cost time

- **PowerShell, not Git Bash, for `build.cmd`.** `cmd /c ".\build.cmd ..."`
  from the Bash tool gets its path rewritten and can print only the `cmd.exe`
  banner with no actual build - the log then looks like a trivial no-op
  relink (a handful of "Re-checking globbed directories" / "Linking" lines)
  and is easy to mistake for "nothing changed." Confirmed this session: the
  fix is to run `build.cmd` from the PowerShell tool directly, not wrapped in
  `cmd /c` from Bash.
- **A background job can look stalled when it is not.** `build_recomp.py`'s
  coverage step over a route that touches MISC.EXE's self-modifying code
  writes one log line a clock tick - tens of thousands of lines, the same
  few bytes repeated - and a `tail` taken mid-burst looks identical call to
  call. Check CPU delta on the actual process
  (`Get-Process -Name f117run | Select Id,CPU`, twice, a few seconds apart)
  before assuming a hang.
- **Shell and Python escapes**: inline Python heredocs turn `\n`, `\f`, `\a`
  and `\d` into real characters. Use the Edit or Write tool for strings with
  escapes, or a script file.
- **Git Bash and cmd**: see above; `MSYS_NO_PATHCONV=1 cmd /c "C:\...\
  build.cmd" "-DF117R_GEN_DIR=..."` with the absolute path also works from
  Bash if PowerShell is not available, but PowerShell is simpler and was
  what this session used after discovering the problem.
- **Room claim must cover the longest path** (see item 5): a wrong `room(c,
  N)` in a hand-written matched routine hides until a route happens to run
  out of its instruction budget exactly inside that routine, which can be
  billions of instructions in. Count every segment between calls/loop edges
  from the tool-generated draft, not from a mental re-derivation, and when
  in doubt over-claim (always safe) rather than under-claim.
- **A synthetic same-size file is not a stand-in for a real file's EXEC
  cost** (item 3): EXEC's cost is dominated by where the file lands on disk
  relative to the previous access, not by its size. Measure the real file at
  its real position when the file's identity is known (`realexec.py`'s
  approach), rather than inserting a differently-positioned same-size copy.
- **Background work**: a killed run can leave emulator processes holding
  files; check with `tasklist`/`Get-Process` before and after. Run long jobs
  in the background; never sleep in the foreground; use `Monitor` or
  background-task notifications to wait, not chained `sleep`.
- Game keys (Key Control Card): 0 brakes, 6 gear, 8 bay, Space select weapon,
  Enter fire, Backspace cannon, B select target, Shift+F10 eject. `f117run`
  input: Space is `\s` in route files; at most 4096 inputs.
- DOSBox 0.74 sees posted keys only with `SDL_VIDEODRIVER=windib`; DOSBox-X
  does not take posted keys; the release 86Box cannot be typed into (use the
  VNC build, `tools/ref86box/build_86box.md`).
