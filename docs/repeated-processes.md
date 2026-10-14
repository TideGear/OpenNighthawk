# Repeated processes

Every process this project runs more than once, what it costs, what it
duplicates and what has been done about it. Update this page whenever a
process is added, measured or changed; a new repeated step goes here first.
Timings are wall clock on the development machine (32 threads) and say
whether they were measured or estimated. The rule behind the page: an
optimisation must leave every check's inputs, outputs and verdicts
identical, and the proof is the same verdicts before and after.

## Presentation styles and full-route replay checks (13 October 2026)

`test_present_fills` compares replay rules with the original VGAME fill
selector on synthetic pages: 2,619 staged cases, including all style classes,
clipping, logical row/column parity and odd page alignment. Optional `--rows`
exports synthetic before/after rows for `tests/test_present_styles.py
--original build/test_present_fills.exe`: 2,187 Python rows agree too.
`test_present_driver` compares 3,953 staged cases with the original resident
graphics driver: opaque/transparent text, row/left/width/all-side clips,
variable/fixed fonts, transparent clipped sprites, scaled RLE clipping and
both flips, and both tick-scale variants. Together these take under a second
on an idle machine. They need generated modules from the owner's install;
no original bytes are committed. The near-polygon regression separately
checks fine pattern cells at N=1/4/9 and interpolation endpoints. Python
pattern and verdict-parser regressions run in ROM-free CI.

`tools/presentation_check.py --data DIR --baseline-runs DIR/runs --out DIR`
replays gate routes at N=4/9 by default (`--routes STEM...`, `--scales N...`,
`--jobs N`). First run the full gate for fresh scan baselines. The checker
requires matching guest checkpoints/final hashes and route milestones,
every logic frame and interpolation endpoint exact, no unseeded or dropped
frames, and positive presentation pairs on routes that enter flight. A
short SETUP-only route may have no 50M-clock checkpoint and no flight frames;
its final state and milestones still count. Raw output stays outside git.
These concurrent runs check correctness, not refresh-rate performance.

13 October result: all 45 routes at N=1 pass, with 259,240 exact closed
logic frames, 259,166 exact interpolation pairs and matching guest hashes
(1,489.86 s, eight workers beside the gate). Live boot N=4/9 and 66 checks
across 11 retained windows pass too. The full gate takes 1,969 s: initial
translation/build/tests 174, coverage 715, identical regeneration 39 and
fresh parity 1,041. All 58 tests, 45 routes, both instruction profiles,
three matched seeds and reachability/event checks pass; 44 interpreter
sessions run fresh, none reused. Evidence: `D:/f117-gate/p3-styles`.

Private 4K throughput investigation: a hidden Direct3D 11 target verifies
3840x2160 output using the carried replay and five intermediate pictures per
pair. Profiling separates regeneration/raster, palette conversion, upload
and GPU submission. Runs beside the gate are exploratory: they include
driver stalls and are not isolated 60 Hz acceptance evidence. A byte-group
flat-guard draft preserves full fine-frame checksums, seam-repair tallies
and flat/agree/restored counts across strike/landing/air-to-air. It was
subsequently integrated with the scalar regression below. Keep its
evidence separate from the fill-style milestone. Isolated N=9 strike
throughput subsequently measures 84.36 fps / 15.120 ms p99 with current
code, and 90.58 fps / 13.803 ms p99 with the draft; both have an occasional
roughly 100 ms upload stall. Short landing and air-to-air runs also exceed
60 fps on average but their p99 includes upload stalls. This does not pass
the steady-refresh check. Private sources and
logs: `D:/f117-gate/p3-styles/bench` and `guard-before/after-*`.

## GPU refresh and 4K presentation timing (13 October 2026)

Capture an isolated application replay with a fresh save directory, using
`f117a --no-config --data DIR --save NEW-DIR --no-record --replay
tools/routes/strike.input --exit-after 3500000000 --present interp
--present-scale 9 --fullscreen --present-log FILE --log MACHINE-LOG`.
The replay sets its recorded 9 MIPS clock. This takes about 389 seconds of
wall time, including menus and over two minutes of flight; keep other heavy
jobs stopped. The timing log includes the actual GPU renderer, output size,
refresh rate, vsync and fine scale. Compare the final guest hash with an
isolated headless run using the same input, fresh save and stop count.

`py tools/presentation_timing.py FILE --out VERDICT.json` checks 3840x2160
at 60 Hz by default (`--resolution W H`, `--refresh HZ`, `--ips N`). It
requires a GPU, vsync, sufficient fine resolution and at least 600 flight
pictures; median interval within 10% of the refresh period, p99 within
125%, no more than 1% of intervals above 150%, consistent interpolation
time and no more than 100 ms accumulated guest drift. Malformed, incomplete
or wrong-target captures fail. Synthetic verdict regressions run in CI.
This measures submitted pictures on the available display, which may be
virtual. Physical-panel tearing and perceived motion require the owner's
eyes. Keep raw timing and game logs private.

The flat-cell guard now compares safe unaligned 8/4-byte groups and tails.
Its scalar regression checks complete output bytes and all counters at
N=1..16, including modified tails, non-flat boundaries and protected HUD
cells (0.22 s). All 66 retained-window checks pass after the optimization.
The final guard-only build passes 59 CTests in 142.83 s (excluding the
unchanged full func_lockstep already covered by the preceding gate), seven
Python verdict regressions and a ROM-free preset/metadata smoke check.

Threaded presentation, isolated fullscreen captures (13 October): strike to
3.5B takes 389.8 s, air-to-air type 6 to 5.9B 656.4 s and landing to 10.0B
1,112 s of wall time
(the machine is paced to the guest clock, so they cannot be shortened). All
three pass `presentation_timing.py` at 3840x2160/59.99 Hz/vsync/N=9: p99 16.71 ms,
maximum 17.06, 21.31 and 17.25 ms, no missed interval, all logic frames exact, final
hashes equal to fresh headless runs. Before the threaded draw the strike
capture failed p99 at 24.30 ms. Per-stage timers (a temporary build; not kept)
found the cause: logic-frame closes cost 21-25 ms and ordinary frames used
89% of the budget on one thread. Details: [presentation.md](presentation.md).
Run these with nothing else heavy going: a first attempt that started on a
1080p display and moved to the 4K one logged two display lines and the checker
rejected it. Evidence: `D:/f117-gate/p3-stream` (`thr2`, `air6`, `chk2`).

After a presentation-code change: `ctest -E func_lockstep` (60 tests, 19 s at
`-j 8`), `presentation_check.py --scales 1 --jobs 16` over the 45 routes
(481 s; 1,062 s at eight workers, same 259,240 frames / 259,166 pairs), the
`boot_to_flight` sentinel at `--scales 4 9`, and the retained windows (66
checks; run `build/test_drawlist`, `test_hires` and `test_interp` with absolute
paths, the relative `build/` junction fails under Python's `subprocess`).

## Observer path comparison (13 October 2026)

Use the existing draw-list capture process with both engines, not just the
matched path. Compare ordered V and G payloads (all values and record IDs),
record counts, guest checkpoints and final hashes. Perspective V timestamps
can differ by the RET instruction: the matched body records after charging
RET, the original hook reads immediately before RET. This is a capture
boundary difference, not changed guest time. Under the 386 profile both
engines execute the original projection, so the complete logs can be compared
byte for byte. Keep raw captures and private comparison scripts outside git.

The default boot capture repeats 54 checkpoints and final hash from the
prior live baseline, with 217,022 V and 300,637 G payloads identical across
engines (interpreter 52.0 s on four spare CPUs while the gate runs).
A staged 386 menu script uses normal inputs, skips the intros with Enter,
and captures through clock 8,000,000,000. Its first comparison found a
shared-address observer bypass: matching guest hashes alone did not catch
missing G/E/F records. The corrected dispatch passes its shared-address
regression and produces a byte-identical 751,862,587-byte log: 446,978 V,
618,966 G, 80 checkpoints and final hash equal to the retained interpreter
run. The final full gate passes in 1,661 s: build/tests 146, coverage 588,
identical regeneration 40, fresh parity 887. All 53 tests and 41 routes pass
with 40 fresh interpreter sessions, both instruction profiles, three matched
seeds and reachability/event checks. Retain the earlier failing evidence.
An isolated build overlapped this run, so its timing is not a speed comparison. Local bundle:
`D:/f117-gate/observer-original`.

## Phase 2 candidate reading

`tools/matched_draft.py --dossier` writes a private JSON beside the draft,
using the same instruction walk rather than decoding twice. It records the
module image and tool hashes, segmented entry and image offset, disjoint
byte ranges (exclusive ends), encoded call targets, exits and unwalked
successors. Calls are not followed; indirect targets and caller inventory
remain unresolved. This is reusable static evidence, not a parity verdict
or a cache that skips verification. Keep both outputs outside the repo.
On the polygon caller, generation takes under one second, produces the
same 46-instruction / 96-byte draft byte for byte, and exposes its five
call sites. Synthetic regressions cover disjoint ranges, relocation,
bounded walks, indirect exits and COM segment aliases; Windows CI runs them.
The next span-edge candidate's walk reaches 743 bytes across eight ranges,
larger than its 419-byte census extent because of shared flow: do not treat
the walk's byte count as progress or exclusive function ownership.

Draft vocabulary check (13 October): `py tests/test_matched_draft.py` now
also compiles a synthetic MOV/ADD/branch/CALL/RET draft against the matched
headers in a temporary CMake project. The previous tool's IC() macro rejects
the emitter's opcode argument (MSVC C4002); TIMING records also belong to
translated regions and are omitted from these instruction-clock drafts.
All six tests pass in 2.3 s. CTest/Python need not inherit a developer shell;
the check uses the installed Windows compiler environment without logging it.
This compiles a synthetic fixture, not original game bytes; no link or game
install is needed. Windows CI runs the same test file.

Mission-generator proof (13 October): resolve the
indirect jump at START 4E39 before writing its fixtures. The nine entries at
CS:4E3E lead to 4E14/4E24/4E50, expanding the static walk to 1,332 instructions
and 3,811 code bytes; its census extent is 3,035. Synthetic coherent worlds
and returning helpers bound searches while retaining the original caller's
registers, flags, memory and clocks. Retry/timing cases are explicitly staged;
normal routes keep all actual helpers. DS=SS and clear DF are part of the
coherent C caller; unconstrained controls keep other states. Both main divide
faults are compared before following a random vector; the final divisor 150
cannot fault. All 339 budgets and 60 calls audit exactly. Three development
seeds reach all 1,332 starts, with 95 fault frames each and no mismatches;
two optimized main-tree seeds repeat that coverage in 23.3 s each while
sharing four spare CPUs. A development boot replay reaches the routine and
matches 54 checkpoints/final in 42.4 s. The optimized full gate passes all
54 tests, 41 routes (40 fresh interpreter sessions), both instruction profiles
and three 829-address matched seeds, zero mismatches and passing reachability/
event checks. Its fresh boot route invokes the routine once. Total 1,832 s:
build 326, coverage 658, second build 25, parity 823. These timings include
single-job compilation and overlapping targeted probes; they are not a gate
speed comparison. Local proof: `D:/f117-gate/start-4239`.

Next-candidate review: DSWAP 03DF3's census lead begins with 2,791 zero bytes
and has no generated entry. Its linear static draft decodes data as operations;
require a real caller or executed address before treating it as code. No
census adjustment or matched credit is taken. SETUP 0010 has a 505-instruction / 1,267-byte walk; its integrated proof is
described below. Private dossiers: `D:/f117-gate/dswap-3df3`
and `D:/f117-gate/setup-0010`.

For fixture development only, /Od avoided a very large transcript optimizer
peak. Production validation restores /O2. Limit large proof builds to one
compiler job and use a private TEMP/TMP, since build.cmd otherwise shares a
temporary log. Check the success marker and the compiler log as well as the
exit code; a failed Windows build has returned zero while leaving an old
executable. No transcript or development build flag is committed.

SETUP proof (13 October): the coverage pass records only interpreted misses,
so a fully translated entry can be absent from its files. A fresh interpreter
prefix confirms SETUP 0000:0010 and dumps its patched keyboard/button targets:
MISC base +7 paragraphs, both RETF with no argument pops. Synthetic fixtures
use DS=SS, real string copies, returning device/exit helpers and a staged
sound-question skip. At image base zero, BIOS 04F0 aliases the original code;
use its existing word as the synthetic settings segment without patching an
instruction. The 122-budget / 77-call room audit and all 505 instruction starts
pass three development seeds and two optimized seeds. Eight real startup cases
pass 320 paired checkpoints plus final hashes in 3.9 s on four spare CPUs.
Calibration needs enough time for the original PIT measurement between button
transitions; the early 6M-clock script stopped inside measurement, before setup
finished. The retained 150M-clock route completes both-button calibration.
The D3 guard is verified with keypad accepted and another extended key still
quitting. These input-only regression routes join the full gate.
The full gate passes 55 tests, 45 routes, 44 fresh interpreter sessions, both
5,718,848-state instruction profiles and three 830-address matched seeds,
zero mismatches and passing reachability/event checks (1,608 s). Build/tests
344 s, coverage 585 s, second translation/build 220 s; use the log's final
parity timing. Calibration contributes 97 observed SETUP starts, displacing
99 heuristic starts; the final translation has 89,365 instructions. The rebuild
restarts the locksteps, so results before it do not stand for the final build.
The first gate failed step 9: the named routine's nested keyboard waits ran to
the host slice (62,168 instructions past an event limit, on every route),
which the lockstep cannot see. A wait inside a matched routine must stop at
`c->stop_at` (`SQ_RUN`); the seeds, startup cases and gate were rerun after it.
Private source-frozen proof: `D:/f117-gate/setup-0010`.

Logo sprite-set loader (13 October): the mini-project loop (direct candidate
object linked before frozen libraries, 2.6-3.6 s per edit) proved the body; the
integration was then three mechanical steps: append the named body and table
row, apply only the harness additions, add one CTest. Optimized seeds, the
real logo-load prefix (one call, 100 checkpoints/final identical on both
engines) and a fresh full gate (56 tests, 45 routes, 1,687 s) followed.
No event overrun this time: the routine has no unbounded waits. Proof:
`D:/f117-gate/logo-07e2`.

Private sprite-loader fixture iteration now links a direct candidate matched
object before frozen generated/core libraries. The archive's old matched object
is not selected. This avoids recompiling generated sources for each body or
fixture edit: initial mini build 9.9 s, edits 2.6-3.6 s. A SETUP control repeats
its 36 comparisons / 504 reached instructions at 32 states, and the sprite
loader's real route matches 100 checkpoints. Library hashes are retained with
the private project (`D:/f117-gate/logo-07e2/library-provenance.json`). This is
/Od development evidence only; integration still needs a normal /O2 build,
optimized checks and a fresh full gate. Never copy private development flags
or generated sources into the public tree.

Clipped span edge (`0FB2:06A6`, 12 Oct): the named clipper preserves
the temporary INT 0 vector, overflow halving, signed intersections and
remainder rounding, boundary-column calls, live endpoints and tail jumps to
shared emitters `05A4`/`05B3`. The harness watches shared flow from `0555`.
Three 4,000-state seeds compare 4,825 / 4,815 / 4,821 states (including
partial stops), zero mismatches; all 37 isolated tests pass in 42.12 s.
The real camera replay executes the new entry 1,529,189 times with all 234
checkpoints/final identical to the saved fresh interpreter result. Private
proof: `D:/f117-gate/span-edge/route-verdict.json`. The initial reachability
checker accidentally read the old polygon output; correcting its path and
checking the saved runner, milestones and hashes passes without rerunning.
Full gate at `1a26b2c` passes in `D:/f117-gate/gate-span-edge.log`:
37 unit tests, all 39 routes IDENTICAL, both 5,718,976-state instruction
profiles and three matched seeds (2,659,030 / 2,657,868 / 2,657,992
comparisons, third seed `0x1a26b2c7fed2`), zero mismatches, passing
reachability/event checks. All 38 interpreter sessions were run fresh.
Total 1,787 s: build/tests 198, coverage 629, identical regeneration 22,
parity 938. No additional heavy candidate work ran alongside this gate;
these different-source timings do not establish a speedup.

Shared span emitters (`0FB2:05A4` / `05B3`, 12 Oct), targeted proof:
151-line selective patch adds named boundary contribution and shallow/steep
raster loops, preserving live endpoint/table reads, unsigned bounds, signed
half-step errors, instruction counts and event resume positions. The harness
watches the boundary entry's tail jump into the existing `0555` column store.
Three 4,000-state seeds (default, `0xC0FFEE`, `0x831e74036ce2`) compare
6,756 / 6,789 / 6,780 states, zero mismatches (11,030 finish in original
code; remaining comparable states include partial stops). Isolated 38-test
suite passes in 39.54 s. Real camera replay takes 104.1 s, executes the two
entries 507,745 / 591,404 times and matches all 234 checkpoints/final hash
against the fresh interpreter output from the `1a26b2c` full gate, without
event overruns. Private proof/patch: `D:/f117-gate/span-emit/route-verdict.json`
and `integrate.patch`; implementation worktree `D:/f117-wt/span-emitter`.
Initial build caught an undefined NEG helper; the tested implementation uses
the existing subtraction helper. Integrated and fully gated at `eda53ee`:
`D:/f117-gate/gate-span-emit.log` passes 38 tests, all 39 routes IDENTICAL
at every checkpoint/final (38 fresh interpreter sessions, no cache reuse),
both 5,718,976-state instruction profiles and three matched seeds with zero
mismatches (2,665,786 / 2,664,657 / 2,663,454 comparisons; commit seed
`0xeda53ee148d7`). Routes-only reachability and event-limit checks pass.
Total 1,709 s: build/tests 205, coverage 598, identical regeneration 23,
parity 882. Light candidate reading and a retired-build archive move ran
alongside; this different-source timing is not a controlled speedup result.
The retired polygon worktree's three source changes were verified against
`1a26b2c` and backed up as `D:/f117-gate/polygon-fill/preserved-worktree.patch`;
its build moved to `D:/F117A-Scratch-Files/worktree-builds/polygon-fill-1a26b2c`
before the worktree and merged local branch were removed.
Both entries lie in the already-counted 324-byte `0555` census region, so
integration must add no census functions or bytes.

Timer tick (`VGAME 0000:1E79`, 12 Oct), targeted proof: the named
implementation preserves the three counters, graphics driver call, seven-
phase poll, and positive/negative/zero rate-request branches into the shared
`1C6D` / `1C85` tails. Returning driver thunks on both fixture sides expose
its own work around the calls; CODE_BELOW includes the shared rate code.
Three 4,000-state seeds (default, `0xC0FFEE`, `0x52d67e50d9d4`) compare
4,565 / 4,601 / 4,610 states, including partial stops, zero mismatches.
All 39 isolated unit tests pass in 40.86 s. The real camera replay takes
117.4 s, executes it 53,226 times and matches all 234 checkpoints/final
against the fresh interpreter capture from the `eda53ee` full gate, without
event overruns. Private proof records source, executable, replay input,
runner and reference hashes: `D:/f117-gate/timer-tick/route-verdict.json`.
Selective patch: `D:/f117-gate/timer-tick/integrate.patch`; active worktree
`D:/f117-wt/span-emitter`, reset to `52d67e5` after its emitter changes were
verified on main and the full tested source snapshotted in
`D:/f117-gate/span-emit/tested-matched.c`. Generation reuses this worktree's
own private generated directory and takes 22.3 s; build uses two workers.
Integrated and fully gated at `b253f3a` in
`D:/f117-gate/gate-timer-tick.log`: 39 unit tests, all 39 routes IDENTICAL
at every checkpoint/final, 38 fresh interpreter sessions, both
5,718,976-state instruction profiles and all three matched seeds, zero
mismatches. Matched comparisons: 2,670,351 / 2,669,258 / 2,671,209;
third seed `0xb253f3af283f`. Reachability/event checks pass. Total 1,872 s:
build/tests 207, coverage 663, identical regeneration 23, parity 979.
Two private objectives-panel probe replays overlapped parity after matched
shards finished; this is not a controlled speed comparison.
Its census extent is 53 bytes;
the 95-byte static walk includes shared tails and is not exclusive credit.

Objectives panel (`VGAME 0000:9515`), candidate only: manually resolving the
bounded eight-way table at `95AE` extends the static walk to 200 instructions
/ 500 bytes; census extent is only 91 bytes. A private normal-input F10 route
reaches the original entry and matches both engines at all 57 checkpoints
and final `2877054667/c465c5fd46862b46`. Saved proof:
`D:/f117-gate/objectives-panel/probe-verdict.json`. The initial verifier
incorrectly required the entry in both coverage files: coverage records only
interpreted instructions, excluding generated and matched execution. The
corrected verifier checks saved raw results and original-entry coverage;
positive matched invocation proof was still needed at this stage.

Named panel implementation (12 Oct): 200 lines of explained C preserve the
live guest frame, shared buffer and call arguments, instruction counts,
resume positions and live CS phrase table. Unknown table targets resume the
original code. Returning driver thunks enable randomized fixtures; actual
string/format/display helpers still execute. A quarter of states plant small
objective records, including mid-run-stop states; original branch reporting
requires all eight phrase targets, both completed objectives, deadline and
overdue paths, empty/nonempty places and an invalid kind (mask `7FFF`).
Three seeds (default, `0xC0FFEE`, `0x2a7c842ff547`) compare 2,885 / 2,737 /
2,817 states with zero mismatches, each with the full branch mask. All 40
isolated tests pass in 39.92 s. The real F10 replay takes 39.2 s and executes
the named entry four times, all 57 checkpoints/final identical, no event
overruns. Source/executable/input/runner/reference hashes are retained in
`D:/f117-gate/objectives-panel/named-proof/route-verdict.json`; selective
patch `D:/f117-gate/objectives-panel/integrate.patch`. The input is now
retained publicly as `tools/routes/objectives_panel.args`, giving 40 gate
routes. Generation takes 21.9 s; build uses two workers and its own TEMP.
Integrated with census 808 / 69,171 (P2 41.67%, overall 74.28%). Full gate
at `ae57d1b` passes in `D:/f117-gate/gate-objectives-panel.log`: all 40 tests,
40 routes at every checkpoint/final, 39 fresh interpreter sessions, both
5,718,976-state profiles and three matched seeds (2,673,236 / 2,671,995 /
2,673,680 comparisons, third seed `0xae57d1b53870`), zero mismatches, passing
reachability/event checks. Total 1,595 s: build/tests 183, coverage 542,
identical regeneration 25, parity 846. Private armament build, fixtures and
replays overlapped after matched shards finished; this different-source
duration is not a controlled speed comparison. Audited verdict and log hash:
`D:/f117-gate/objectives-panel/full-gate-verdict.json`.

Armament screen (`START 0000:07EE`), private candidate at base `ae57d1b`:
the 145-line named implementation preserves settings/mission far pointers,
unavailable stations, sixteen hit boxes, DF-sensitive gray palette copies,
picture-load arguments, station icons and stores text. The initial 200-state
run compared only one state, so fixtures now plant valid pointers and small
settings. Half use returning drawing/format/mouse helpers, half retain actual
helpers; the picture loader returns in both. Three seeds compare 2,420 /
2,023 / 2,016 states including partial stops, zero mismatches, all required
branches (`7F`); 220 full states retain actual helpers. All 41 isolated tests
pass in 61.98 s. Real `full_cycle` replay takes 45.4 s, invokes the entry once
and matches all 66 checkpoints/final against an immutable snapshot from the
fresh timer gate, no event overruns. Source/executable/input/runner/reference
hashes: `D:/f117-gate/armament-screen/named-proof/route-verdict.json`; selective
patch `D:/f117-gate/armament-screen/integrate.patch`. Generation takes 25.2 s
from frozen complete coverage lines; build uses two workers and its own TEMP.
The 373-byte walk earns only 67 census bytes. The reference also matches the
latest fresh objectives-panel gate. Integrated selectively: 817 addresses,
census 809 / 69,238 (P2 41.70%, overall 74.29%). Fresh full gate at `fc98297`
passes in
`D:/f117-gate/gate-armament-screen.log`: 41 tests, all 40 routes at every
checkpoint/final (39 fresh interpreter sessions), both 5,718,976-state
instruction profiles and three matched seeds (2,675,656 / 2,674,018 / 2,674,510
comparisons; rotating seed `0xfc9829737559`), zero mismatches and passing
reachability/event checks. Total 1,617 s. Audited verdict/log hash:
`D:/f117-gate/armament-screen/full-gate-verdict.json`. Private navigation-grid
generation/build, fixtures and F3 replay overlapped after matched shards
finished; this is not a controlled speed comparison.

Larger next candidates: panel frame `VGAME 0000:79BE`, 798 instructions /
2,090 bytes, and navigation grid `0000:8A90`, 374 / 1,094. Their static walks
have no unresolved successors; dossiers remain under
`D:/f117-gate/next-candidates/panel-frame` and `navgrid`. A normal-input F3
probe reaches original `8A90` and matches both engines at all 57 checkpoints
and final `2877054444/5ea6b18cd60f6608`. Proof:
`D:/f117-gate/navgrid/probe-verdict.json`. Panel frame was initially unimplemented.
Preserve high-byte-only grid arithmetic and the order of projection stores
versus the panel's ILS.

Navigation grid named implementation is proven privately at base `fc98297`:
three 4,000-state seeds compare 2,055 / 2,080 / 2,071 states including partial
stops, zero mismatches, all 19 tracked paths. Fixtures plant small worlds,
zoom settings, marker types and target highlights; projection/trig helpers
remain original, graphics calls return. A transient code-write watch covers
the entire 1,094-byte body. All 42 isolated tests pass in 77.31 s. Real F3
replay takes 66.6 s, invokes the named entry 314 times and matches all 57
checkpoints/final, without event overruns. Audited source/executable/input/
runner/reference and fixture hashes: `D:/f117-gate/navgrid/targeted-verdict.json`;
selective patch `integrate.patch`. Generation takes 30.7 s from frozen
coverage; private build uses two workers after main's matched shards finish.
These overlapping workloads are not a controlled speed comparison. After
pushing the validated armament baseline (`7651db5`), the selective grid patch
is integrated with the retained `tools/routes/navigation_grid.args` input.
Census: 810 functions / 70,332 bytes, P2 42.28%, overall 74.45%, 818 matched
addresses. Fresh full gate at `f1ecb55` passes in
`D:/f117-gate/gate-navigation-grid.log`: 42 tests, all 41 routes at every
checkpoint/final (40 fresh interpreter sessions), both 5,718,976-state
instruction profiles and three matched seeds (2,677,711 / 2,676,098 / 2,677,743
comparisons; rotating seed `0xf1ecb55f79bf`), zero mismatches and passing
reachability/event checks. Total 1,600 s. Audited verdict/log hash:
`D:/f117-gate/navgrid/full-gate-verdict.json`. Private panel-frame build,
fixtures and replay overlapped after matched shards finished; these timings
do not establish a speedup.

Cockpit panel frame (`VGAME 0000:79BE`) is proven privately at base `f1ecb55`.
The named implementation preserves all 80 call sites, accepted-page flag,
unsigned altitude rounding, mode-change erases, warning copies, shared text
buffers, byte-only HUD masks, waypoint advance, message countdown and the
left-altitude inset. Navigation projection still precedes ILS projection.
The initial fixture caught a missing RET count; corrected three-seed tests
compare 4,906 / 4,909 / 4,914 states including partial stops, zero mismatches,
all 32 tracked paths. Graphics/string/navigation helpers return in these
fixtures; original trig/distance/bearing/clamp helpers execute. Code-write
watch covers all 2,090 bytes. All 43 isolated tests pass in 80.52 s.
Real F3 replay uses the actual drivers/helpers, takes 67.8 s, calls the named
entry 550 times and matches all 57 checkpoints/final without event overruns.
Audited artifacts and selective patch: `D:/f117-gate/panel-frame/targeted-verdict.json`
and `integrate.patch`; tested source is snapshotted there. Patch preflight
passes on main. After pushing the validated grid baseline (`c8f262b`), the
panel patch is integrated: census 811 / 72,422, P2 43.39%, overall 74.78%,
819 matched addresses. Fresh full gate at `ae4b50f` passes in
`D:/f117-gate/gate-panel-frame.log`: 43 tests, all 41 routes at every
checkpoint/final (40 fresh interpreter sessions), both 5,718,976-state
instruction profiles and three matched seeds (2,682,617 / 2,681,007 / 2,683,132
comparisons; rotating seed `0xae4b50f791f6`), zero mismatches and passing
reachability/event checks. Total 1,560 s. Audited verdict/log hash:
`D:/f117-gate/panel-frame/full-gate-verdict.json`.
Its saved interpreter reference also matches all 57 checkpoints/final in the
fresh grid gate (`panel-frame/fresh-reference-crosscheck.json`).
Private build uses two workers after matched shards finish. These overlapped
timings do not establish a speedup. Next HUD pass `1058:0016` has a private
dossier under `next-candidates/hud-pass`: 806 instructions / 2,488 bytes,
45 call sites, no unresolved successors. Its complete handwritten draft
is inserted in the private worktree, backed up as
`D:/f117-gate/hud-pass/named-hud-pass.c`. After pushing the validated
panel baseline/evidence (`cb50ccf`), integrated the selective patch on main;
all three runtime/fixture/CMake texts match the tested snapshots. Census
812 / 74,910, P2 44.71%, overall 75.16%, 820 matched addresses. Fresh full
gate at `de1189e` passes in `D:/f117-gate/gate-hud-pass.log`: 44 tests,
all 41 routes at every checkpoint/final (40 fresh interpreter sessions),
both 5,718,976-state instruction profiles and three matched seeds
(2,687,533 / 2,685,929 / 2,685,258 comparisons; rotating seed `0xde1189e2aeae`),
zero mismatches and passing reachability/event checks. Total 1,596 s.
Audited verdict/log hash: `D:/f117-gate/hud-pass/full-gate-verdict.json`.
Final three seeds compare 4,916 / 4,922 / 4,912 states including 303 / 298 /
304 divide faults, zero mismatches. Each reaches all 32 tracked paths and
four fault sites; all 44 isolated tests pass (62.41 s). Actual-driver F3
replay invokes the HUD 550 times, with all 57 checkpoints/final identical
in 50.5 s. Audited verdict, selective patch and tested snapshots:
`D:/f117-gate/hud-pass/targeted-verdict.json`. The initial broad run caught
a call starting at an exhausted event limit; explicit guards now precede
every far call. Fault fixtures compare CPU and memory immediately after
original DIV dispatch, before interpreting a random vector target.
Preserve byte-DIV fault addresses and flags, separate one-bit shifts,
live coordinate reads and guest stack pushes during fixed-point rotation.
The saved F3 reference matches all checkpoints/final of the fresh panel
gate (`hud-pass/fresh-reference-crosscheck.json`).

Moving-object pass (`VGAME 0000:983E`) is proven privately at `de1189e`:
564 instructions / 1,724 bytes, all 38 calls retained. Named C preserves
target cycling before drawing, detail reduction, shadows, distant dots,
projectile slots, flare scales and exterior-player placement. Guest frame
slots and helper scratch remain live across calls; byte flag updates and
signed/unsigned comparisons retain the original order. Fixtures return
projection/drawing helpers with controlled depth while original range,
bearing, absolute-value, scaling and trail-size math executes. Code-write
watch covers the whole body. Three seeds compare 2,710 / 2,707 / 2,654 states,
zero mismatches, all 32 tracked paths; all 45 isolated tests pass. Actual
F3 replay invokes the named pass 550 times and matches all 57 checkpoints/
final in 51.0 s. Audited artifacts, patch and tested snapshots:
`D:/f117-gate/moving-objects/targeted-verdict.json`. Its saved interpreter
reference matches the fresh HUD gate (`fresh-reference-crosscheck.json`).
After pushing HUD baseline/evidence as `7fb10be`, integrated the selective
patch; all three main texts match the tested snapshots. Census: 813 /
76,634, P2 45.62%, overall 75.42%, 821 addresses. Fresh full gate at
`b811414` passes in `D:/f117-gate/gate-moving-objects.log`: 45 tests,
all 41 routes at every checkpoint/final (40 fresh interpreter sessions),
both 5,718,976-state instruction profiles and three matched seeds
(2,690,243 / 2,688,636 / 2,689,943 comparisons; rotating seed `0xb8114142076c`),
zero mismatches and passing reachability/event checks. Total 1,799 s.
Audited verdict/log hash: `D:/f117-gate/moving-objects/full-gate-verdict.json`.
A two-worker private camera build and replay ran alongside this gate;
these timings do not establish a controlled speed difference.

Camera-image pass (`VGAME 0000:BCEF`) is proven privately at `b811414`:
658 instructions / 1,863 bytes, all 49 original calls retained. Named C
preserves pitch folding, two-word height clamping, flat/gradient background
order, target models, nested terrain state, blast overlays, F6 darkening/
lamp changes, labels and zoom/compact-HUD restoration. Render/string
helpers return in fixtures while original range, trig and bearing execute.
A controlled helper-return subset forces FFFF to prove the caller's own
IDIV fault frame; it does not establish a normal-play fault. Whole-body
code-write watch remains. Three seeds compare 4,907 / 4,902 / 4,926 states,
including 145 faults, zero mismatches and all 32 tracked paths per seed.
All 46 isolated checks pass: 41 core tests, then five rebuilt host targets
under the restored app configuration. Actual camera-gradient replay invokes
the named pass 7,880 times and matches all 234 checkpoints/final in 190.6 s.
Audited artifacts, selective patch and tested snapshots:
`D:/f117-gate/camimage/targeted-verdict.json`. The immutable interpreter
reference matches the fresh moving-object gate (`fresh-reference-crosscheck.json`).
After pushing moving-object baseline/evidence as `e9aa11a`, integrated
the selective patch; all three main texts match the tested snapshots.
Census: 814 / 78,497, P2 46.61%, overall 75.71%, 822 addresses. Fresh full
gate `D:/f117-gate/gate-camimage.log` passed: 47 tests, all 40 routes, both
instruction profiles, three matched seeds, 1,480 s.

That gate's own rotating seed (`0x2fa6695118ca`) caught a stack-aliasing
bug in the unrelated, already-matched `vgame_map_project` (`VGAME 0000:13B9`):
cached `y`/`py` stack arguments where the original reads both late, after a
store that can land on either slot when DS and SS alias - the same bug class
as `vgame_eventlog_add`. Fixed by declining whenever the store could overlap
either slot. Re-gate `D:/f117-gate/gate-map-project-fix.log` passed clean:
47 tests, all 40 routes, both instruction profiles, three matched seeds
(including the same commit seed), 1,480 s (build 94, coverage 584,
regeneration 25, parity 777). Regression `matched_map_project_stack_alias`
retains the seed.

Flight input (`VGAME 0000:1EB0`, 12 Oct), proven privately at `cc2bddc`.
Method that worked for a 1,274-instruction routine: insert the
`matched_draft.py` transcript first to validate the harness (equal on every
compared state), add per-instruction coverage of the original's run, plant
fixtures until every instruction is reached, then swap in the named body -
a partial body that hands back to the original at a fixed IP can be tested
the same way. The first named run passed vacuously: two callees (EE9C,
EF36) end in `RET 8`, the draft called them as plain near calls, so the
return was never trapped and the original finished every state. The harness
now counts such escapes as failures, but only where the original came back
to that address from the call's stack depth (up to 16 bytes above it): in
the 2,000-state suite run a random state recursed in both engines and
reached the address only deeper, a false alarm under an address-only test.
Break test: restoring one plain call fails at 2,000 and 4,000 states. Fixture
classes: planted key poll/read stubs (each command, Y/N quit answers),
post-ejection counters, low-speed recovery, braking on the ground,
touchdown (frame rate 1, contact flag, aircraft record +40h) and crash;
the game-port reads, the crash pause and the quit prompt's page/text
helpers return at once where they would outrun the 100,000-step limit.
Three seeds compare 2,112 / 2,718 / 2,292 states, zero mismatches, about
four seconds each; the default seed runs all 1,274 instructions inside
the named code, the others 1,260. 49 isolated tests pass. Real
camera-gradient replay: 143.4 s, 8,464 calls, all 234 checkpoints/final
identical. Audited verdict: `D:/f117-gate/flight-input/named-proof/route-verdict.json`.
Full gate at `1337acf` (`D:/f117-gate/gate-flight-input.log`): 48 tests,
all 40 routes, both instruction profiles, three matched seeds, 1,745 s
(build 241, coverage 620, regeneration 24, parity 860 with 40 fresh
interpreter runs). Its coverage pass recorded the new key.

Frame update (`VGAME 0000:3ADA`, 12 Oct), same method. The coverage and
escape tracking now serve a table of routines (`COVERED` in
`tests/func_lockstep.c`) with a per-routine floor, and a skip-reason tally
(fault, long run, bad return, code written, declined, unfinished) shows
why states are dropped: here every skip was a long run, ended inside the
frame's sub-passes (5852, 683E, scene, AI, palette), which now return at
once in this routine's fixtures. The routine picks the nearest field and
derives the map cell itself, so airfield fixtures plant the 32-bit position
and flagged field records, not the cell. Transcript and named body both
equal at three seeds (2,931 / 2,914 / 2,912 states); two planted constant
errors fail at once. The real replay showed one event overrun by one
instruction in 8,462 calls: four divides after their setup were outside the
room claim. The lockstep's limit probe tests only the limit at the very
end, so a mid-routine claim can be short and still pass every seed; the
route found it. `D:/f117-gate/frame-3ada/room_audit.py` walks a named body
and checks each claim against the instructions counted after it (it found
all four in the unfixed text; two flags it raises are if/else and
table-claim artifacts, checked by reading).

Ground-defence update (`VGAME 0000:5046`, 12 Oct): 195 instructions / 583
bytes. Returning draw/engagement fixtures retain the real clamp and long
divide. Independent sentinel plants had not proven reload faults: the
harness waited for a normal RET and discarded the fault states. Correlated,
explicitly staged one-site worlds now force zero divisors (defence FFC0h)
and quotient overflow (FFBCh, S=80h). Require the named routine to hand back
at `5122`, step the original IDIV once, and compare the entire interrupt
frame, registers, flags, clock and memory before following its random vector.
Both fault kinds must appear. Removing hand-back fails at state 1; a
transient code-write watch excludes random site loops that overwrite their
own instructions. Three seeds compare 2,696 / 2,679 / 2,685 states, zero
mismatches, with 1,006 / 996 / 1,039 faults and all 195 instructions reached
inside the named routine at each seed (about five seconds per seed).
`matched_ground_defences` retains the check. Room audit has one reviewed
scope artifact: the unconditional RET-8 call block claims three instructions
at `51D2`; the parser loses that claim on leaving its braces. Actual camera
replay: 117.1 s, 8,465 calls, all 234 checkpoints/final identical and no
event overrun. Private evidence: `D:/f117-gate/nav5046`.
Fresh full gate (`gate-ground-defences.log`): 50 tests, all 41 routes
(40 fresh interpreter sessions; one shared reconnaissance replay), both
instruction profiles, three matched seeds and reachability/event checks
pass, 1,834 s. Audited verdict: `nav5046/full-gate-verdict.json`.

Aircraft traffic (`VGAME 0000:5852`, 12 Oct): 998 instructions / 2,976
bytes. Rebuild the preserved named draft against current main; correct its
map-marker and mission-clock explanations. Bounded traffic/field records,
returning driver thunks and one obstacle-probe fixture class reach all 998
instructions at three seeds (2,056 / 2,017 / 2,078 comparisons, no mismatches).
Vary the staged pursuit range across 200h to exercise the shooting decision
consistently. Fault-aware execution compares 226 / 208 / 223 divide-error
states at `5B99`, `5E0D`, `5E89` and `6280`, including the interrupt frame;
the other four divides are reached normally, not proven to fault. The private
instruction transcript passes the same harness (2,079 comparisons at the
default seed). Changing the fault return address at `6280` is caught.
`matched_air_traffic` retains the fixture and coverage checks. Room claims
pass the static audit without exceptions. All 16 callees' return forms
are checked: only `EF36` pops arguments (RET 8), handled explicitly.
The real camera replay takes 130.7 s: 8,464 calls, all 234 checkpoints/final
identical, no event overrun.
Private evidence: `D:/f117-gate/frame-5852`.
The fresh full gate (`gate-air-traffic.log`) passes 51 unit tests, all
41 routes (40 fresh interpreter sessions; one shared reconnaissance
replay), both instruction profiles and three matched seeds with zero
mismatches and passing reachability/event checks, 1,963 s.
Audited verdict: `frame-5852/current-full-gate-verdict.json`.
Matched comparisons: 2,704,945 / 2,703,866 / 2,704,280;
third seed `0x06c1dfe9140d`. Stage seconds (build/tests, coverage,
regeneration, parity): 253 / 634 / 27 / 1048.

Weapons in flight (`VGAME 0000:683E`, 12 Oct): complete the preserved
partial draft through guidance, motion and impacts. Correct its slot ownership,
scratch-target explanation and a one-instruction suffix count after `717F`.
The old fixture needed up to 240 words in a 200-word array; use 256 with a
capacity check before collecting fresh proof. `matched_projectiles` requires
all 886 instructions at 4,000 states. Four seeds (default, `0xC0FFEE`,
`0x683Ea11`, `0xb41ec9c960e4`) compare 4,844 / 4,851 / 4,862 / 4,852
states and 291 / 271 / 264 / 274 divide-error frames, no mismatches.
Fault masks `3D` / `3D` / `BD` / `BD` cover five or six of the eight divides;
the others execute normally but are not proven to fault. The transcript
passes the same harness; changing `6C94`'s fault return address is caught.
The supply arm uses an explicitly staged retyping label fixture because the
original dispatch normally skips it (D5). All 25 callees return with plain
near RET; room audit passes. Actual placement tests show all six internal
fix options decline without changing CPU/memory and are removed when disabled.
Fresh camera replay: 8,459 calls, all 234 checkpoints/final identical,
no event overrun, 105.8 s. Full gate: 52 tests, all 41 routes
(40 fresh interpreter sessions), both 5,718,976-comparison profiles and
three matched seeds, zero mismatches, passing reachability/event checks.
Total: 1,772 s. Private evidence: `D:/f117-gate/frame-683e`.
Matched tallies: 2,709,789 / 2,708,717 / 2,708,223.
Stage seconds (build/tests, coverage, regeneration, parity):
254 / 639 / 26 / 853. No heavy candidate work ran alongside this gate;
these different-source timings do not establish a speedup.

## The gate (`tools/build_recomp.py`)

Polygon caller (12 Oct): isolated current-main worktree, separate generated
directory and two build workers; initial build about six minutes, all 36
unit tests 29.72 s. Three 4,000-state seeds compare 2,214 / 2,172 / 2,214
states, zero mismatches (about 2.5 s per seed while concurrent). Returning
span-reset, clipped-edge and driver fixtures prove the caller; the real
`camera_gradients` replay takes 116.0 s, executes `0FB2:01E6` 382,365 times,
and matches all 234 checkpoints/final `bb1c0cffd145907a` against the existing
fresh interpreter capture. Full gate `D:/f117-gate/gate-polygon-fill.log`
passes at commit `723c46a`: 36 tests, all 39 routes IDENTICAL, both
5,718,976-state instruction profiles and three matched seeds with zero
mismatches (2,654,205 / 2,653,053 / 2,653,554 comparisons; third seed
`0x723c46a9db96`), passing reachability/event checks. All 38 interpreter
sessions are fresh. Total 1,770 s: build/tests 189, coverage 604, identical
regeneration 22, parity 955. Private clipping builds, tests and a replay ran
alongside it; this duration is not a controlled performance comparison.

**24-worker experiment (12 Oct): correct, no route speedup; retain 16.**
`D:/f117-gate/gate-jobs24.log`, HEAD `7df74bf`, has the same executable,
translator, harness and route sources as the 810-address `117201c` baseline.
The private comparison wrapper retains that baseline's third seed
`0x117201ca17d6` deliberately; this is a scheduling comparison, not a new
commit-seed claim. Coverage is forced with `--coverage-record=` and every
interpreter session runs fresh with `--verify-interp-cache`.

| Stage | 16 workers, latest same-code baseline | 24 workers, BelowNormal |
|---|---:|---:|
| Translation, incremental build, 35 tests | 166 s | 46 s |
| Coverage | 538 s | 557 s |
| Byte-identical regeneration | 19 s | 21 s |
| Fresh parity | 822 s | 830 s |
| Total (rounded laps) | 1,546 s | 1,455 s |

The apparent total saving is build time: coverage and parity together are
27 s slower. One run does not establish a small regression, but gives no
evidence for adopting more workers. The older 1,466 s sprite/map run has
809 addresses and is not the direct comparator. All 87 verdict/tally lines
are identical: 35 tests, 39 routes, both 5,718,976-state instruction profiles,
three matched seeds, reachability and event-limit checks. A pre-run snapshot
also confirms identical final clocks/hashes, 10,612 checkpoints and 18,202
matched execution reports across 72 completed engine results, including all
38 recompiled sessions (one serves two routes). Four interpreter snapshots
were unavailable after an interrupted prior run; their fresh results are
held to the corresponding recompiled session and the baseline's final hash.
Stale non-leader session files are excluded. Private proof:
`D:/f117-gate/jobs24-proof/comparison.json` and `compare.py`.

The local affinity launcher now sets and reads back both mask `0x00FFFFFF`
and `BELOW_NORMAL_PRIORITY_CLASS`; all 48 active route/lockstep workers
were observed inheriting both. Parity used about 76% of total CPU, but higher
utilization did not shorten the phase. Retain the lower priority for desktop
responsiveness; subjective typing latency was not measured.

The GitHub Windows job at `7df74bf` initially failed its wall-clock pacing
check (normal/stalled runs 4.688/4.797 s, difference 0.109 s, required
0.45–1.2 s; both exited normally). The unchanged local check passes at
0.687 s added time; rerunning only the failed GitHub job makes both jobs
green, [run 38217956592](https://github.com/TideGear/OpenNighthawk/actions/runs/38217956592).
This is evidence of timing variability, not proof the pacing test is robust.

Span divide-fault gate (12 Oct, 00:25 Pacific) **passes**:
`D:/f117-gate/gate-divide-fault-084e.log`, commit `117201c` (VGAME 0FB2:084E,
810 addresses). 35 unit tests, both instruction profiles (0 mismatching) and
three matched seeds all pass with 0 mismatching, including this commit's own
rotating seed `0x117201ca17d6` (2,651,991 / 2,650,881 / 2,649,857 states
compared). All 39 routes IDENTICAL, 38 fresh interpreter runs (0 reused),
every routes-only routine reached, no event-limit overruns. Duration 1,546 s:
build/tests 166, coverage 538, identical regeneration 19, parity 822.

Sprite/map retry gate (11 Oct, 21:42 Pacific) **passes**:
`D:/f117-gate/gate-sprite-map-retry.log`, commit `ae9fbca` (809 addresses).
34 unit tests, both 5,718,976-state instruction profiles (0 mismatching) and
three matched seeds all pass with 0 mismatching, including this commit's own
rotating seed `0xae9fbca4da0b` (2,647,992 / 2,646,888 / 2,648,997 states
compared). All 39 routes IDENTICAL, 38 fresh interpreter runs (0 reused),
every routes-only routine reached, no event-limit overruns. Duration 1,466 s:
build/tests 49, coverage 559, identical regeneration 19, parity 839. The
earlier FAILED run below is superseded; the checkpoint is now fully validated.

Sprite/map gate outcome (11 Oct, 21:07 Pacific): full gate FAILED at the
third matched seed: existing target scoring VGAME 73C8 missed 12 original
instructions. Default/C0FFEE seeds pass 2,647,992 / 2,646,888 states; the
rotating seed compares 2,651,451 with one difference. All 33 unit tests,
39 replay routes and both 5,718,976-state instruction profiles pass.
Duration 1,438 s: initial build 149, coverage 488, parity 782; 38 fresh
interpreter runs. The console strictly decodes with no NUL/non-ASCII data.
The private fix restores live loop reads, flags and every loop/join count;
all three targeted seeds pass, including the original failing case. Main
selectively integrates it and a retained-seed regression (34 unit tests).
A fresh full gate remains required. The user requested a handoff checkpoint
commit before that gate completes. The corrected run was stopped during
initial build, and the private strike probe was stopped without a verdict;
all owned processes were terminated for handoff. Do not treat the checkpoint
as fully validated.

Private IRET harness baseline across 811 entries compares 42,391 states
without differences. Three targeted seeds compare 23,978 interrupt states,
including 11,985 special-site selections. These are fixture proofs;
normal-driver replay reaching both handlers remains outstanding.

Camera gradients: original VGAME 1974 and 19FD build four-point
sky/ground strips at x=169..245, subtracting/adding a table of horizon
offsets and stopping at y=110/169 or 16 bands. A shared named helper retains
the guest frame, original fill calls, signed clipping, live loop reads and
event stops. After verified main regeneration, the private two-worker build
passes (`build-wt-camera-gradients.log`), recompiling 31 generated files
whose discovery boundaries changed. Returning-fill fixtures and selected
zero-offset tables compare 9,576/9,559/9,591 states exactly, including 2,279
completed 16-band walks. These prove caller behavior with a returning fill.
The private unit subset passes all 27 tests in 19.28 s. Real-rasterizer
reconnaissance finishes in 96.2 s with all 150 hashes/final unchanged, but
reaches neither helper: the default camera takes a nested terrain walk.
Normal Alt+W selects the gradient path. Paired recon probes match all 150
checkpoints/final but reach ground only (2,074 calls). An air-to-air rendering
probe reaches sky 7,851 times and ground 1,108 times: recomp 105.8 s,
fresh interpreter 220.4 s, all 234 checkpoints/final bb1c0cffd145907a equal.
Its changed drawing cost ends the recorded flight before landing; the retained
`camera_gradients` route checks rendering, without mission-completion credit.
Main now selectively integrates the two helpers and fixtures, one focused
regression and that replay: 807 addresses, 32 unit tests, 39 routes. Fresh full
gate `D:/f117-gate/gate-camera-gradients.log` passes under affinity
0x00FFFFFF with 16 workers and fresh interpreter-cache verification.
Coverage passes in 538 s and regeneration adds no code; three 807-entry
matched seeds compare 2,638,463 / 2,637,344 / 2,634,993 states with zero
differences. Both instruction profiles compare 5,718,976 states, no
mismatches. All 32 unit tests and 39 routes pass, as do reachability/event
checks. Total 1,497 s: first translate/build 149, coverage 538, identical
regeneration 19, parity 791. 38 fresh interpreter runs, zero reused.
Census: 801 functions, 68,294 bytes. The combined command log contains a
corrupted section; independent route results and the final gate tallies
remain intact. The private logging regression/fix below addresses that.

Sprite/map follow-up: the logo's 006D:0678 sprite pair repeats a colour or
skips transparent pixels; opaque count zero wraps to 256 writes. Explicit
AX boundary cases retain that asset-unreached behavior. VGAME 11A8 wraps
the downward scene frame used by the moving map. Returning-call fixtures
isolate its live argument setup; normal-driver replay checks the full path.
The two-worker private build starts only after main's regeneration is
verified finished; `D:/f117-gate/build-wt-sprite-map.log`. Main source stays
fixed while its gate runs. Build passes (25 steps). Targeted smoke compares
152 states, zero differences; three seeds compare 9,529 / 9,544 / 9,540
states, including 1,172 opaque zero-count and 595 transparent sprite runs.
Full-cycle replay calls map once and sprite-run 1,570 times in 44.7 s; all
66 checkpoints/final 6b61d89b95e7b562 match the prior interpreter capture.
Main's fresh full-cycle interpreter capture (20:34:51) now also matches all
66 checkpoints and final state, completing real-driver replay proof without
duplicating the original run already required by the gate. Both additions
are selectively integrated in main, with one focused regression: 809
addresses, 33 unit tests, 39 routes. Full gate
`D:/f117-gate/gate-sprite-map.log` runs with 16 workers under affinity
0x00FFFFFF, requiring fresh interpreter comparisons.

The running gate's combined console log showed mixed-in data during parallel
command printing; independent per-route results remain separate. A private
regression with yielding stdout reproduces interleaved command records.
Serializing only command printing fixes it while retaining child arguments,
options and results; all 12 gate-helper tests pass, including after integration
in main. The running sprite/map gate also retains this logging fix; verify
its finished console stream has complete command records and no mixed-in data.

Diamond and screen-loader integration (11 Oct, 19:37 Pacific): the two
privately verified entries and their intentional fixtures are now in main,
with two focused regressions. Main has 805 addresses, 31 gate unit tests
and 38 routes. Full gate log `D:/f117-gate/gate-diamond-screen.log`, 16
workers under affinity 0x00FFFFFF. The full gate passes all 38 routes,
31 tests, both instruction profiles (5,718,976 states) and three 805-entry
seeds (2,628,887 / 2,627,785 / 2,628,392 states), reachability and event
checks. Duration 1,560 s: build 163, coverage 525, identical regeneration
19, parity 852 (rounded stage times). 37 fresh interpreter runs, zero reuse.
Census: 799 functions, 68,002 bytes.

Private follow-up (11 Oct): START 3641 wraps fade-out, picture/palette load,
full-page copy, optional overlay callback, fade-in and present. Initial raw
64-state test compares zero states (callee completion limits); do not count
that as parity evidence. Returning fade/load/palette and optional-overlay
fixtures compare 1,615/1,579/1,645 caller states across three seeds, no
differences. Normal full-cycle replay calls the helper five times and
preserves 66 checkpoints/final hash in 46.7 s. The previously unreached
diamond is reached by laser-guided `strike_training`: 1,051 calls, all 133
checkpoints/final hash identical in 76.6 s. The plain strike uses model 1Bh,
outside the diamond's 1Ch/1Dh/1Eh branches. Private build logs are
`build-wt-start-screen{,-fixture}.log`; implementation remains private while
main's allocation/restore gate runs. The private unit subset passes all 27
tests in 19.68 s while sharing CPU with the gate. Fresh diamond comparisons
pass 4,967/4,989/4,977 states with zero differences; all private jobs are closed.

The checked allocator and five restore entries are now integrated in main
(803 addresses), with two focused regressions (29 gate unit tests).
The diamond stays private pending integration. Full verification passes in
`D:/f117-gate/gate-checked-restore.log`, 16 workers under affinity
0x00FFFFFF: 178 s build, 540 s coverage, 19 s identical regeneration,
787 s parity, 1,524 s total. Both instruction profiles compare 5,718,976
states; three 803-address seeds compare 2,622,305 / 2,621,217 / 2,618,194
states, with zero differences. All 38 routes and reachability/event checks
pass; 37 fresh interpreter runs, zero reuse. Census: 797 functions,
67,798 bytes. The harness records 1,263/1,247/1,283 completed allocation-error
cases across the full seeds, all equal.

Next isolated DSWAP wrapper (11 Oct): checked byte allocation at 0494 keeps
the DOS result in the guest frame, compares it against segment 10h, reports
an error through formatted output, and invokes the original failure path.
The implementation preserves calls, flags, live frame reads and event stops.
Build `build-wt-dswap-checked.log` waits until the runtime gate's second
regeneration finishes; the two-worker build is now live and recompiles
shared generated units changed by the seven runtime entries. No build or
random-state verification was claimed while that build ran. The first build
fails because frame/call macros were out of scope; the corrected build uses
the available helpers and passes in `build-wt-dswap-checked-restore.log`.
The private batch now also contains five matching Ctrl+Break restore entries;
their original bodies differ only in verified saved-vector addresses. Three
seeds compare 78,713 states exactly, 27 tests pass in 10.66 s, and a 46.1 s
full-cycle replay preserves all 66 hashes, with VGAME/SETUP restore calls.
The replay does not reach the checked allocator. A controlled fixture supplies
low/error allocation replies, returning graphics thunks and failure cleanup,
checking the caller when cleanup returns. Three seeds then compare 76,014
states exactly, including 3,709 completed error results; 27 tests pass in
9.61 s. This does not prove real out-of-memory termination. Logs are
`checked-restore-error-lockstep-{0,1,2}.log`. Main stays fixed at 797 entries.

Next isolated runtime batch (11 Oct): five Ctrl+Break installers share an
18-instruction original body in VGAME, START, END, DSWAP and SETUP. Byte
comparison confirms only the saved-vector and handler addresses differ;
all configuration fields are checked against the original. Two DSWAP DOS
memory wrappers have byte-identical bodies to the already matched START/END
helpers, so reuse those helpers at DSWAP's entries. Returning DOS fixtures
cover these seven entries. Implementation is private; build and random-state
verification waited until camera gate regeneration finished. Initial three
seeds passed 98,171 states, but the full-cycle real-driver probe differs from
the original starting at 2.35B: the installers' MOV AX,segment immediate is
relocated, not a constant zero. A new nonzero-image fixture catches all five
broken installers (64-state request, five immediate mismatches). The fix
reads the loaded operand; `build-wt-break-relocation-fixed.log` passes.
Repeated seeds with the relocation fixture compare 32,723/32,696/32,752
states exactly; the 27-test unit subset passes in 7.46 s. A fresh full-cycle
probe takes 30.2 s, preserving all 66 checkpoints/final hash; SETUP, VGAME
and END installers each run once. These seven candidates are now integrated
under `gate-break-memory.log`: 797 addresses, 27 unit tests and 38 routes,
16 workers under affinity 0x00FFFFFF. The first translation now has 89,367
instruction starts (one additional static entry). The full gate passes: 172 s
build, 522 s coverage, 18 s identical regeneration and 778 s parity, 1,490 s
total. Both instruction profiles compare 5,718,976 states; three matched seeds
compare 2,596,946 / 2,595,902 / 2,593,952 states, all without differences.
All 38 routes and reachability/event checks pass; 37 interpreter runs are
fresh, with zero reuse. Census: 791 functions and 67,670 bytes.
The initial broad CTest run passes 27 tests but its full 2,000-state matched
test is terminated after this known defect, so that run is not a suite pass.
Use the gate's unit exclusion for broad locksteps in the repeat; targeted
seeds already cover the changed routines and the full gate checks all later.

Next isolated drawing helpers (11 Oct): projected-point diamond B0C2 and
camera inset darkening C45C are rewritten privately while main stays fixed
under the combined pause/HUD gate. The two-worker build waits until main
regeneration ends and parity starts, avoiding shared generated-file writes
during compilation. No verification/integration claim yet; drafts and build
log remain outside the repository (`build-wt-diamond-camera.log`). The build
passes after regeneration. Three targeted seeds compare 7,981/7,894/7,932
states exactly in about 5.7 s concurrently; driver thunks return on both
sides. The isolated suite passes all 24 tests in 11.95 s. A private
F5/F6/F6 probe preserves all 54 hashes but reaches neither helper (F5
selects armament, not the camera), so it is not proof of these entries.
A reconnaissance replay with F6 darkening enabled passes all 150 hashes
and final state, 123.3/71.4 s original/isolated, C45C called 1,608 times.
The isolated strike replay takes 81.6 s against the combined gate's fresh
232.3 s interpreter control: all 177 hashes/final match, but B0C2 never
runs. Keep that candidate isolated. Camera darkening alone is integrated
under `gate-camera-darken.log`, 25 tests, 38 routes, 790 matched addresses;
Full verification passes: 25 unit tests, all 38 routes, both instruction
profiles, three matched seeds and reachability/event checks. The gate takes
1,420 s (build 137, coverage 494, identical regeneration 18, parity 771),
37 fresh interpreter runs, no cache reuse. New `recon_darken` retains the successful
real-driver path rather than the non-reaching F5/F6 probe.

Combined pause/HUD gate (11 Oct) passes in `gate-pause-hud-2.log`:
24 unit tests, 37 routes, 789 matched addresses, affinity 0x00FFFFFF,
16 route workers. Total 1,453 s: build 130, coverage 537, identical
regeneration 18, parity 768. All 37 routes are identical (36 fresh
interpreter runs, zero cache reuse); both instruction profiles, all three
matched seeds and reachability/event checks pass. First `gate-pause.log` passes 22 unit tests and both
instruction profiles but is aborted after its rotating seed finds two
changed-code inputs: PLAYER's stack aliases/restores its RET, and VGAME's
edge midpoint code lies below the watched entry. The repaired per-step
watch/range passes that exact seed (8,944 comparisons, about one second),
retained as `matched_frame_code_alias`. This is not a passing first gate.
HUD real-driver C/C probes take 58.7/55.2 s concurrently, preserving all 54
checkpoints and final hash; colour/layout handlers run once/twice.

Isolated HUD colour/layout candidates (11 Oct) use two census entries:
1058:09D8 falls through to 1058:0A2C. Three compact-mode-planted seeds
compare 8,828/8,845/8,827 states exactly, about one second each concurrently;
21 unit tests pass in 9.20 s. Normal-input C/C layout toggles are prepared
in private `hud-layout.args` for interpreter/matched real-driver probes.
These candidates are now integrated in the combined gate above; their
earlier isolated checks do not replace that gate. `--only` takes an unpadded hexadecimal
IP, not segment:offset; an incorrect filter silently compares no routines.
Always inspect the per-routine comparison counts.

Pause-handler matching (11 Oct) first probes Alt+P, repeated Alt+P, Space,
then quit through normal input, using the original interpreter and isolated
matched binary: 38.9/40.0 s concurrently with a two-worker build. All 54
checkpoints and final hash agree; the matched handler runs once. Without
returning sound-driver thunks the random harness skips every state. Enabling
its existing driver fixture for the pause lets the existing DOS/BIOS answers
exercise polling too: three targeted seeds compare 4,966/4,959/4,954 states,
zero differences, about 0.4 s each. No keyboard code is patched. The focused
CTest and normal-input `pause_resume` route retain this coverage. Main gate
is pending in `gate-pause.log`, affinity 0x00FFFFFF, 16 route workers.

Camera-plane/seam integration (11 Oct) reuses the existing observer captures:
the final isolated 20-test run takes 5.98 s. Fine endpoints at N=4/9 pass
for strike (444), landing (62), and air-to-air (54); three intermediate
pictures per pair are exercised too. Independent C/Python repair counts
match at N=4 (10,010/1,534/2,941 respectively). Frozen-binary live N=4/9
sentinels take 53.2/73.7 s concurrently, each preserving 368 logic frames,
366 endpoint pairs and 54 checkpoints/final hash. Main full gate passes
in `gate-near-seams.log`: 21 tests, all 35 routes, both instruction profiles,
three matched seeds, reachability and event limits. Total 1,411 s: build/tests
107, coverage 513, byte-identical regeneration 17, parity 775 (rounded).
All 34 interpreter sessions run fresh; affinity 0x00FFFFFF, 16 route workers.

The first cockpit drawing-helper gate (11 Oct, `gate-panel-drawing.log`)
takes 1,059 s: build/tests 127, coverage 603, byte-identical regeneration 17,
parity 312. All 35 route pairs and both instruction profiles pass, with 34
interpreter cache hits and none fresh. Its third matched seed exposes a
transient self-modifying fuzz state in END's pair swap: code is overwritten
and restored before the final memory comparison. The existing per-step code
watch now covers all four pair-order helpers too. Retained CTest
`matched_order_code_alias` runs the failing seed in about 1.3 s and compares
15,984 states, zero mismatches, skipping the invalid code-alias state.
The complete second gate (`gate-panel-drawing-2.log`) passes all 35 routes,
19 tests, both 5,718,912-state instruction profiles and three 786-entry
matched seeds (2,547,415/2,546,484/2,547,057 comparisons), zero differences,
with reachability and event-room checks. Total 1,426 s: build/tests 26,
coverage 574, byte-identical regeneration 17, parity 810 (rounded stages).
The CMake regression changes the interpreter cache key: 34 fresh sessions,
none reused. Affinity 0x00FFFFFF, 16 route workers; isolated presentation
builds and two two-flight observer probes share the machine.

The isolated near-plane metadata probe records a 200M-clock strike window
and a same-binary observer-off control (`record-near-metadata.py`), preserving
all 174 checkpoints and final hash. `near-crossing-check.py` checks 4,120
crossings exactly; four ambiguous records motivate an entry-observer repair.
Carried replay and N=1/4/9 checks cover 447 work/display phases and take
about 4.5 s concurrently. Initial isolated 18-test run takes 6.82 s. These
are investigation runs; no production near-plane rendering rule is enabled.

The cockpit sprite gate (11 Oct, `gate-panel-blits.log`) passes all 35
routes, 18 tests, both 5,718,912-state instruction profiles and three
783-entry matched seeds (2,532,630/2,531,689/2,533,435 comparisons), with
reachability and event limits checked. Raw wall time 16,480 s includes
15,015 s suspended at the owner's allowance threshold: about 1,465 s
active, split 148 build/tests, 550 coverage, 18 byte-identical regeneration
and 749 parity (rounded). All 34 interpreter sessions ran fresh: final
host-comment changes after the HUD gate changed the cache key. Affinity
0x00FFFFFF, 16 route workers; a two-worker isolated build shared the run.
Three fresh targeted main seeds compare 24,775 sprite-helper states in
about 4.6 s concurrently. The next isolated readout/block-line helpers
compare 29,593 states in about 5.2 s concurrently, and the target box
14,805 in about 0.9 s; no differences, no full gate for those three yet.

The Reimp-style HUD gate (11 Oct, `gate-hud-reimp.log`) took 1,444 seconds:
translation/build and 18 tests 27, fresh coverage 487, byte-identical
regeneration 24, parity 906 (rounded stages). Host/presentation/CMake
changes invalidated interpreter cache keys: 34 fresh sessions, 0 reused.
All 35 routes, both 5,718,912-state instruction profiles and three
781-entry matched seeds pass (2,524,384/2,523,430/2,525,667 comparisons,
no differences), with reachability and event limits checked. Affinity
0x00FFFFFF, 16 route workers. Separate live N=4/9 sentinels also match all
54 checkpoints and final hashes of both freshly checked scan engines.

The cockpit startup gate (10 Oct, `gate-panel-init.log`) took 898 seconds:
translation/build and 17 tests 86, fresh coverage 506, byte-identical
regeneration 16, parity 290. All 35 routes, both instruction profiles and
three 781-entry matched seeds pass (2,524,384/2,523,430/2,525,425 routine
comparisons, no differences), with reachability and event limits checked.
34 interpreter sessions were reused, affinity 0x00FFFFFF, 16 route workers.
Three targeted seeds compare 14,971 states in about 6.7 seconds per seed
concurrently. A two-worker worktree build overlapped coverage and parity.

The two-panel-helper gate (10 Oct, `gate-panel-helpers.log`) took 1,010
seconds: translation/build and 17 unit tests 91, fresh coverage 559,
byte-identical regeneration 17, parity 342 (rounded stages). All 35 routes,
both instruction profiles and three 779-entry matched seeds passed, with
2,519,363/2,518,465/2,522,480 routine comparisons and no differences.
Reachability and event-room checks passed; 34 interpreter sessions were
reused. Affinity 0x00FFFFFF, 16 route workers. A two-worker build in the
separate worktree overlapped final parity. Targeted panel checks compared
29,522 states over three concurrent seeds in about a second per seed;
the next display-fill candidate compared 6,281 states in about three
seconds per seed concurrently, without differences (not yet fully gated).

The clipping-entry registration gate (10 Oct, `gate-outcode-bank.log`)
took 959 seconds: translation/build and 17 unit tests 125, fresh coverage
547, byte-identical regeneration 15, parity 272. All 35 routes, both
5,718,912-state instruction profiles and three 777-entry matched seeds
passed; every routes-only routine ran and no routine overran. It used
16 route workers with verified inherited affinity 0x00FFFFFF. Three
targeted 4,000-state seeds took about 0.4 seconds each concurrently,
comparing 3,990/3,999/3,998 states without differences. An earlier
duplicate-implementation gate was deliberately interrupted during unit
tests after discovering the existing routine; it is not a passing run.

The collision/sound/scene full gate PASS (10 Oct, `gate-scene-sound.log`)
took 883 seconds: translation/build/tests 92, fresh coverage 495,
byte-identical regeneration 11, parity 284. All 35 routes were identical,
both instruction profiles and all three 768-address matched seeds had zero
mismatches; every routes-only routine ran and none overran. Interpreter
results reused 34 verified cached sessions. The locksteps finished during
coverage, leaving no tail. Affinity remained 0x00FFFFFF throughout.

The 10 Oct collision/sound/scene batch's two targeted 4,000-state seeds ran
concurrently in 9.7 seconds (two workers, affinity 0x00FFFFFF), comparing
7,763 and 7,777 states without differences. Rebuilding the large matched.c
translation unit took roughly 2.5 minutes per edit, including both libraries
and linking; the unchanged-source profile-aware gate took 596 seconds, with
34 interpreter sessions and coverage legitimately reused from verified caches.

The profile-aware reference run (10 Oct, `pc-parity-profile-aware`) adds one
independent local 386 capture alongside the existing DOSBox captures. Fresh
GOG/DOSBox-X/86Box pictures, sound, timing and both saves pass. Held-picture
hashing decodes only stationary scenes, taking about one second on the saved
capture instead of decoding every changing frame. Eight regression tests
take 0.13 seconds. Captures and gate builds must remain sequential because
the running Windows executable locks build outputs.

Run before every commit that changes code. Before the changes below it took
30, 38 and 32 minutes (three runs on 6 Oct 2026, some alongside other work).
After them, the same code (231 matched routines, 32 routes) took **22.8
minutes** (1,365 s against 1,895 s), with every route's clock, hash and
verdict, and both lockstep tallies, identical to the earlier run; that run
shared the machine with a large compile, so it is a ceiling, not a best case.
The 484-routine, 35-route gate on 8 Oct took 14.8 minutes (886 s): build
7 s, coverage 237 s, second translation 3 s, fresh parity 638 s, no lockstep
tail. Both locksteps had zero mismatches and every routes-only routine ran.
The subsequent translated-386 gate took **16.1 minutes** (964 s): build
7 s, coverage 277 s, second translation 4 s, fresh parity 676 s, no lockstep
tail. All 35 routes were identical, both 5,713,152-state instruction checks
and the 484-routine check had zero mismatches, and every routes-only routine ran.

The 9 Oct gate with 762 matched addresses and the shadow-text regression
(`D:\f117-gate\merge10\gate.log`) took **19.3 minutes** (1,157 s): build and
13 unit tests 12 s, coverage 415 s, second translation 7 s (no code added),
fresh parity 723 s (34 interpreter sessions for 35 routes), no lockstep tail.
Both instruction profiles and all three matched seeds had zero mismatches;
all routes were identical, all routes-only routines ran, and no route overran
an event limit.

The gate explicitly sets `F117R_BUILD_APP=ON` (9 Oct), so a previous
core-only build cannot silently omit the five host tests. With generated
code, the unit step now runs all 14 tests, including the retained shift
stack-alias regression; the matched lockstep is checked
separately at three 4,000-state seeds.

The combat-option gate (`D:/f117-gate/gate-combat.log`, 10 Oct) took
1,499 s: build and 14 unit tests 16 s, coverage 537 s, byte-identical second
translation 11 s, fresh parity 934 s (34 interpreter sessions). All 35
route pairs, both 5,718,912-state instruction profiles and all three matched
seeds passed; every routes-only routine ran and none overran. Three
eight-shard matched checks plus 16 route workers initially oversubscribed
the machine. Restricted the gate parent and descendants to affinity
0x00FFFFFF (24 of 32 logical CPUs), inherited by later children. Raw-slot
traces and the held-out cohort shared the machine near the end; this is a
measured shared-load cost, not an isolated performance comparison. The
process-local limit leaves eight CPUs available to the desktop.

The D1SLOT gate (`D:/f117-gate/gate-d1slot.log`, 10 Oct) took **26.5
minutes** (1,588 s): translation/build and 14 tests 80 s, coverage 581 s,
byte-identical second translation 11 s, fresh parity 917 s. All 35 route
pairs, both 5,718,912-state instruction profiles and all three 765-address
matched seeds passed; every routes-only routine ran and none overran.
The attempted ctypes affinity wrapper failed silently. Corrected the parent
and all live descendants to 0x00FFFFFF early in coverage, verified the masks;
future native calls must use pointer-width signatures and check the result.
A later two-worker acceleration probe and one natural replay shared parity.
`D:/f117-gate/affinity_launch.py` now checks both setting and reading the mask.
Use Processor % Processor Time (about 52% during parity) for actual CPU load;
WMI LoadPercentage reported 96% at the same time.

The 763-address gate with this setting (`D:/f117-gate/merge14/gate.log`)
took 555 s: build and 13 unit tests 14 s, parity 541 s with 34 unchanged
interpreter sessions reused. All 35 routes were identical, both instruction
profiles and all three matched seeds had zero mismatches, every routes-only
routine ran, and no route overran an event limit.

The 764-address canopy gate (`D:/f117-gate/canopy/gate.log`) took 887 s:
translation/build 84 s, fresh coverage 490 s, byte-identical second
translation 7 s, parity 306 s. It reused 34 interpreter sessions; all 13
unit tests, 35 route pairs, both 5,718,912-state instruction profiles and
three matched seeds passed. Coverage added nothing; no event-limit overruns.

The 765-address projectile gate (`D:/f117-gate/projectiles/gate.log`) took
845 s: translation/build 78 s, fresh coverage 476 s, byte-identical second
translation 8 s, parity 283 s. It reused 34 interpreter sessions; all 13
unit tests, 35 route pairs, both instruction profiles and three matched
seeds passed, with no event-limit overruns. Coverage added nothing.

The speech-reference/shift-repair gate (`D:/f117-gate/p1-speech/gate-fixed.log`)
took 1,269 s: translation/build 14 s, coverage 522 s, byte-identical second
translation 9 s, parity 724 s. All 14 unit tests, 35 route pairs, both
5,718,912-state instruction profiles and three matched seeds passed, with no
event-limit overruns. It ran 34 fresh interpreter sessions; coverage added
nothing. The initial 595 s gate caught the shift regression at the rotating
seed; that seed is now retained as a unit regression.

Stage times, earlier run: translate and build 457 s, coverage 173 s, second
build 2 s (skipped), parity 503 s, lockstep tail 229 s.

| Stage | Measured | Duplicates / redundancy | Status |
|---|---|---|---|
| 1-2 translate, build | 7-8 min fresh, 6 s reused | a fresh work directory builds everything | **fixed by practice**: reuse the previous run's work directory (`sync_tree` rewrites only changed files, so the build recompiles `matched.c` and relinks); the output is a function of the sources either way |
| 3 coverage | 3-4 min, or skipped | runs every route once only to find untranslated code | **skipped when provably empty** (7 Oct): a record (`~/f117-recomp-local/coverage-verified.json`) holds the key of a pass that added nothing - translator, sources other than `matched.c`, game files, routes - and the digest of the generated code it ended on; the same key and digest skip the pass. A matched routine only replaces original instructions that already ran, so it cannot add code to cover; a wrong one fails parity or lockstep. Verified: the full verdicts (32 routes with hashes, lockstep tallies) are identical with and without the skip; 995 s became 422 s. **It had stopped working** (12 Oct): `CMakeLists.txt` was hashed whole, and each of the last 15 matched passes added an `add_test` entry, so every gate reran coverage (about 580 s) and, by the same key, missed the interpreter cache. The key now hashes the build file without `add_test` entries, whole-line comments and blank lines (`build_digest`): equal across the last four passes' build files, different for a changed build setting |
| 4 translate, build again | 7-8 min | regenerated every file and rebuilt all of it though coverage usually adds nothing | **fixed**: only files whose bytes changed are rewritten; the rebuild is skipped when none did; `--seed-coverage DIR` starts from an earlier run's coverage so step 1 is already complete |
| 5 parity | 8-14 min | 64 runs on 12 slots, started alphabetically; two routes that are one session ran twice per engine | **fixed**: up to 24 slots, longest first, identical sessions run once and every route's milestones are checked on the shared run |
| 5b interpreter half of parity | 5-6 min of the parity stage | an interpreter run is a pure function of the game files, the route and the interpreter's own code, yet reran every gate | **fixed**: results cached by a key over `src/` except `matched.c`, `CMakeLists.txt`, the game files and the route (text, replay files, seed chain); entries expire after 14 days; `--verify-interp-cache` reruns and requires equality. Measured: parity 522 s to 174 s with 31 of 31 results reused and every route verdict identical to the fresh run |
| 6-7 lockstep | 2-3 min | waited for parity though they need only the build | **fixed**: started right after the first build, beside coverage and parity (stopped and restarted if step 4 rebuilds, which 18 of 21 logged gates did not need); output to files in the work directory. Measured 7 Oct with coverage forced: 778 s to 572 s, the lockstep tail 241 s to 0, every route line and both tallies identical |

Same-session routes (executable lines identical) are found by content, not
by name; today: `recon_career` and `recon_return`.

The physics/real-time gate (`D:/f117-gate/gate-physics-realtime.log`,10Oct)
took24.1minutes (1447seconds),16workers with verified24-CPU affinity:
build and17tests20seconds, coverage508, byte-identical regeneration12,
parity907. All35pairs, both5,718,912-state profiles and all three765-address
matched seeds passed; every routes-only routine ran and none overran.
Earlier physics gate failed on the initializer-range harness error; the
first real-time gate was interrupted after an airborne pacing defect.
These are recorded separately and are not passing gates.

## Other repeated processes

| Process | Trigger | Measured cost | Redundancy and status |
|---|---|---|---|
| Targeted weapon-lock marker lockstep | check the named box/hexagon geometry before the full gate | about 0.5 s per 4,000-state run at the two follow-up seeds (9 Oct); 4.2 s before returning driver thunks were enabled | `func_lockstep --only VGAME.EXE:B171 --states 4000 --verbose`; the existing thunk substitution is enabled for this VGAME routine too, increasing the fixed-seed comparisons from 358 to 4,857 (17 skipped); this changes test inputs to reach drawing branches, rather than being an optimisation with identical inputs; full routes retain the actual driver |
| Targeted cockpit-canopy lockstep | check canopy posts, compact side frames and landing cue before the full gate | 0.7 s per 4,000-state run, three seeds concurrently (9 Oct) | `func_lockstep --only VGAME.EXE:D6DD --states 4000 --verbose`; returning thunks on both sides allow graphics calls to return; 4,491 / 4,482 / 4,476 comparisons, 15 / 9 / 7 skipped, zero mismatches; full routes retain the actual driver |
| Targeted projectile lockstep | check slot motion and gunfire refill before the full gate | 1.0-1.2 s per 4,000-state seed, two concurrently (9 Oct) | `func_lockstep --only VGAME.EXE:4777 --states 4000 --verbose`; small-count/firing plants and returning driver thunks reach more active states: 2,823 / 2,886 / 2,891 comparisons, zero mismatches. A private observer confirms 18 full refills among the fixed-seed comparisons. Full routes retain the actual drivers |
| Corrected paired threat cohort | CPU-speed/combat investigation, after changing pilot or observer | 21.9 min for 96 flights, 12 workers (10 Oct); 600 guest seconds, four routes, six seed families, four arms | `threat_profile.py` telemetry v5 fixes unsigned life/burst sampling, startup seconds and orbit geometry; +110/+220 ms boot shifts at 16/20 MIPS, mission identities checked. Old cohorts cannot be reused for combat conclusions. `threat_compare.py` retains early exits and bootstraps paired differences; raw results outside repo |
| Staged incoming-lifetime regression | after changing D1TTL or its dispatch | 76 s for four boots in parallel (10 Oct) | `d1ttl_check.py`, both engines +/- D1TTL, seven cases per boot; compare case hashes, positive-life controls and FFFF/final-4*S suppression. Explicitly staged, not natural combat |
| Staged proximity-distance regression | after changing D1PROX or its dispatch | 114 s for four boots in parallel (10 Oct) | `d1prox_check.py`, both engines +/- D1PROX; S=8/9/15, near/far geometry and FFFF suppression; S and weapons explicitly staged |
| Combined combat-option cohort | after changing the proximity option | 12.6 min for 24 flights, eight workers (10 Oct) | Same matched seed families as the 96-flight cohort, 20 MIPS with D1TTL+D1PROX; reuse the baseline arms, validate mission identity and compare paired differences |
| Raw missile-slot trace | investigate seeker lifetime underflow | 219 s for a parallel 9/20 MIPS pair (10 Oct) | Read-only private wrapper around current pilot; both cohort hashes reproduced. Two processes share affinity mask 0x0F000000 while the gate uses 0x00FFFFFF, leaving four desktop CPUs. Compare common observation windows; slot occupancy is not a count of blocked launches |
| Held-out combat cohort | validate options without changing calibration | 9.9 min for 24 flights, eight workers (10 Oct) | Four routes, three new seed families, 9 unmodified versus 20+D1TTL+D1PROX; all 12 mission identities match. Shares the gate's 24-CPU affinity limit; three early exits per arm retained |
| Staged seeker-underflow reproduction | establish the next countdown regression | About 120 s for two boots in parallel (10 Oct) | Private `slot_underflow_probe.py`, interpreter/recomp, S=8/15; positive life becomes FFFF after lost lock, case hashes identical. First exact-rear setup failed its staging expectation; off-axis setup reproduces the defect |
| Staged weapon physics regression | after changing D1PROX/D1ACCEL or dispatch | about 2 min for four boots (10 Oct) | `d1physics_check.py`, both engines +/- options; 21 cases: S=9/player controls, equal-time and changing-S acceleration, frozen straight passes; incoming slots 0/7; every case hash agrees across engines, both control hashes unchanged |
| Real-time simulation regression | after changing D1REAL or dispatch | 3.8 min for six parallel boots (10 Oct) | `d1real_check.py`, 9/20/40 MIPS in both engines, normal inputs only; normal clock, 2x compression, restored rate, pause/resume and quit/cancel; compare case hashes per speed |
| Corrected real-time airborne cohort | validate D1REAL during combat | 4.4 min for 24 flights with eight workers (10 Oct); two interpreter replays run alongside | four routes x seeds 2500/22500/25000 x 9/20 MIPS, TTL+PROX(v2)+SLOT+ACCEL+REAL(v2); verify missions, full telemetry and replay hashes. START seed matching does not align VGAME's later BIOS-tick RNG seed; align the final hangar input separately for controlled combat, and sample at completed simulation steps |
| Initializer walker bounds regression | each gate | included in the17-test unit selection | `ctest -R matched_initializer_bounds`, retained seed0x66A860898896; exact walker code spans prevent an unrelated callee return from being mistaken for the walker's own RET |
| Staged slot-reclamation regression | after changing D1SLOT or dispatch | about 2 min for four parallel boots (10 Oct) | `d1slot_check.py`, both engines +/- D1SLOT; S=8/15, zero/one/signed/unsigned controls before lost-lock cases; all 15 case hashes agree across engines and all eleven control hashes are unchanged; includes incoming slot 7 and player slot 8 |
| Swept-band/acceleration cohort | measure current physics changes at both CPU speeds, including held-out seeds | 23.7 min, eight workers beside parity (10 Oct) | Four routes x nine seed families x 9/20 MIPS, all four combat options, early exits retained; fresh seeds22500..27500 held out;32 matching pairs, four27500 pairs excluded; check regenerated native 9/20 sentinels against pre-gate/interpreter references |
| Slot-repair combat cohort | assess D1SLOT separately from earlier hit mitigations | 10.4 min for 72 new flights, 12 workers (10 Oct) | Four routes x six seed families x (9 with TTL+PROX, 9/20 with TTL+PROX+SLOT), reuse prior 20 TTL+PROX arm after unchanged-flight hash check; all 24 mission identities match across all four arms, early exits retained |
| Isolated 15 FPS cap experiment | test whether a cap alone keeps S at 15 and the mission clock real | about 18 min for 45 flights, eight workers beside a full gate (9 Oct); isolated generated build about 3 min | one source change in a detached checkout at 28f2a50: D1_FPS_X10 116 to 150; 45 unfixed baseline rows retained from the preceding sweep; separate output and DLL hash in `D:/f117-gate/speed15/experiment.json`; compare two 40 MIPS flights under the interpreter too; no default or production D1 change |
| Speed and D1 sweep (`speed_sweep.py`) | evidence for the default-speed decision | about 12 min for 90 flights, 16 workers beside control research (9 Oct): nine routes, 300 guest seconds each, at five speeds with and without D1 | each route's unfixed 9 MIPS baseline runs first to anchor input times; results resume by tag in `runs.jsonl`; compare engagements only on matching missions; keep fresh results separate from the earlier mixed-version launch counts |
| Control-response speed sweep (`stick_response.py --ips N [--fix D1]`) | compare controls before choosing a default speed | roughly 43-220 s per flight under concurrent load (9 Oct); ten aligned DOSBox-profile flights, four workers | repeat the same 30 timed taps; retain mission identifiers, raw pitch/roll samples and early stops, so incomplete flights and differing missions cannot pass for equal responses; absolute and common-clock exploration runs also include the 386 profile |
| START random-seed probe | find boot clocks that generate the same mission across speeds | five front-end boots; separate wall time not recorded | read START's RNG at DS:AE8C and the BIOS tick count; change only the boot clock for any follow-up, never guest memory; scratch probe in `D:/f117-gate/speedsweep/probe_start_seed.py` |
| Shadow-text stack-alias regression (`ctest -R matched_shadow_stack_alias`) | every gate's unit-test step, with generated code | about 2 s (9 Oct) | seed `0xf2a7d2c1bf36`, START 36C4 and END 19C3/112A only; preserves the state that caught an omitted PUSH SS memory write in the shared matched helper; 14,752 comparisons, zero mismatches after the fix |
| Matched-routine lockstep (`func_lockstep --states 4000`) | every gate (three seeds); every new matched routine | single process 21 min for 653 routines beside a gate (9 Oct); 8 shards 3 min 11 s, the same report line for line | **Sharded since 9 Oct 2026** (`tools/func_lockstep_par.py`, `--shard K/N`), with the owner's approval: each routine draws from its own stream (the seed, its module and address), so a shard tests what one run does, and a routine's states no longer move when other routines are added (before, one stream ran through the table and merges met paths a branch never drew). The states are the same in number and kind, not the old ones. The gate runs seeds 0x5EED0F117A and 0xC0FFEE and one from the gated commit's hash, so successive gates draw new states. Left: tracking writes instead of copying 1 MB a state |
| Instruction lockstep (`insn_lockstep --states 64`, also `--timing386`) | every gate | no wait tail in the latest gate; 5.7 million states per profile (8 Oct) | runs beside coverage and parity; the 386 check also compares prefetch and held REP state; a second-seed run with `--base-seg 0x9FFF` checks code in VRAM |
| 386 flight engine parity (`stick_response --machine machine386 --engine interp/recomp`) | after a profile or translated timing change | 80 s interpreter, 60 s recomp, alongside the gate (8 Oct) | all 574 memory/register/output/scanout checkpoints and stick responses identical; use separate processes because the runtime is global |
| Oversized generated regions | every generated build | 479 s before bounding regions; 120 s after (8 Oct) | a 6,446-instruction VGAME function dominated MSVC compilation; cap regions at 1,024 instructions, preserving each decode and live operand and dispatching across pieces. Both timing profiles retain the 5,713,152-state instruction verdicts; whole-route gate required |
| Census refresh (`reimp_names.py`, then `progress.py --census`) | after each batch of matched routines | about a minute | the lead list only changes when the Reimp does; regenerate it then, refresh the census each batch |
| Route recording (adaptive pilots) | after a timing change moves a flight | 10-20 min per flight; try several parameters in parallel | recorded inputs replace them in the gate; a timing change invalidates all of them at once (see `airair_type5-8`, `strike`, `cargo`, `recon`) |
| Draw-list capture windows | per presentation stage | 3-4 min per window, four at a time | windows are independent and already run in parallel |
| Fine HUD recorded-scene checks | after HUD/replay changes | about 10-15 s for eight corrected windows at N=1/4/9, three workers (10 Oct) | reuse `p3-live/logs41/w_*.log`; run carried `test_drawlist` and `test_hires`; older strike/air captures omit colour-replace records and fail the unchanged baseline too. `test_hires --dump PREFIX` renders the last checked frame, about a second per 69-phase N=4 window |
| Fine HUD live sentinel | after presentation changes | 146.9/112.8 s at N=4/9, two workers beside the gate (10 Oct) | `run_route.py boot_to_flight.args -- --present interp --present-scale N --hash-every 50000000`; 368 exact logic frames and 366 exact pairs per scale, 54 matching guest checkpoints and final hash; separate outputs and inherited 24-CPU affinity. These concurrent runs do not measure isolated performance |
| MSVC matched-code compilation | every matched-code build | PLAYER decompressor stalled for more than 18 min at /O2; about 30 s for the file with the workaround (8 Oct) | only `player_unpack` disables MSVC optimisation; the rest of the file keeps the build's flags. It runs during picture loading. Stop an orphan compiler only after checking its source/output paths; otherwise its object stays locked |
| Worktree build and generated code | per worktree | 8 min first, then incremental | give each tree its own TEMP (build.cmd shares one log); do not share a generated directory between concurrent builds |
| PC-parity run (`pc_parity.py`: pictures, music, saved roster on three references) | before a release; after a machine-model change | about 2 min wall expected (96 s for DOSBox-X and 86Box together; 3.8 min before 86Box's picture capture was traced, 5 min before fast-forward, 12 min before 7 Oct, when 86Box's runs went one after another) | the three references run at once, and 86Box's three runs (picture capture, music and timing, saved roster) run at once too: each stops only its own 86Box (by profile), the traced ones open no VNC port and run on emulated time; ours is replayed per reference, not shared. The DOSBox-X save run ends when the roster's content changes (its inputs run on emulated time, so a wall-clock limit failed under load); the picture capture waits for its VNC port instead of a fixed 6 s. Fast-forward (7 Oct): DOSBox-X's picture capture 11 s instead of 130 s and the same report (two fast-forward captures byte-identical; two real-time ones differ in 3,826 frames, since AUTOTYPE and the pacing ran on host time); its save run 54 s instead of 227 s on an idle machine, which failed under load (8 Oct: the scripted clicks missed, 61 bytes of ROSTER.FIL off), so the check now runs at DOSBox-X's own speed, 4 min; 86Box's save run 95 s instead of 277 s and its music run 49 s instead of 178 s, the saved roster identical and the AdLib writes identical in order, their timing within 86Box's own run-to-run spread. 86Box's picture capture is traced too (48 s instead of about 3 min, two runs identical) |
| DOSBox-X closed-loop flight (`dosbox_cargo_pilot.py`) | per pilot change | 107 s wall in fast-forward (12 min paced to real time, 7 Oct); the same pilot on our machine takes about 1.5 min | fast-forward (`[cpu] turbo=true`) is the default: 1,642 ticks, `result.json` and `flight.csv` identical to a real-time run. Run it beside the machine's flight |
| 86Box closed-loop flight (`b86_cargo_pilot.py`) | per pilot change | about 200 s wall in fast-forward (`B86_FAST`) | run beside the machine's and DOSBox-X's flights; runs are deterministic (same `flight.csv` twice), so one flight is a verdict |
| Machine-behaviour probe (`fidelity_all.py`) | after a machine-model change | about 2 min with a saved 86Box sheet; 86Box adds a 4-minute run (`--86box-run`) | the 86Box sheet only changes with the model, so the saved sheet is reused |
| 86Box boot attempts | per BIOS or config change | 2-5 min per capture series | a boot-sector test (`hello_floppy.py`) answers "does the BIOS boot" in one run, before any larger image |

## Silent 86Box speech waveform reference

`tools/ref86box/86box-audio.patch` records the mixer buffers before host
playback, enabled only by `B86_AUDIO=PREFIX`: signed 32-bit little-endian
stereo `PREFIX.sound.pcm` at 48,000 Hz and `PREFIX.music.pcm` at 49,716 Hz.
The reference runner retains offscreen video and null/dummy host audio.
Before using a capture, run the same input with capture disabled and enabled
and require identical frame and port traces. Measured on 9 Oct: incremental
86Box build 23 s, two intro runs concurrently 28 s, short takeoff-call flight
94 s, our interpreter replay 50 s, WAV render 1.6 s, comparison about 0.6 s.
Reuse captures when the recorded binary, input and configuration hashes agree.
Generated audio and game data stay outside the repository.

The combat-alignment tooling gate (10 Oct, `gate-combat-alignment.log`)
took 9.9 minutes (594 seconds): translation/build/tests 21 seconds,
parity 573; unchanged game source, inputs and generated code reused the
verified coverage/regeneration cache. All 17 selected CTests, 35 routes,
both instruction profiles and three matched seeds passed.

## What the gate is checked against

The gate compares the recompiled engine with this project's own interpreter,
so a model error shows in neither. Fidelity to a real PC is checked
separately, against recorded references: GOG's DOSBox 0.74
(`tools/fidelity.py`, saved video and flight captures), DOSBox-X for some
timing traces, and 86Box as the real-hardware-style reference once it runs
the game. Those are recorded or run per question, not per commit.
