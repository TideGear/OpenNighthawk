# Handoff log: the "Current state" section of 9 and 10 November 2026 (archived unedited on 10 Nov 23:58)

## Current state: 9 November 2026, 22:15 (read first)

This section is the only current one. Replace it; do not stack a new "read first" section above it. The dated handoff sections of 5 to 9 November are archived, unedited, in
[docs/archive/handoff-log-20261109.md](docs/archive/handoff-log-20261109.md). The renderer's measurements and the R38 record are in [docs/gpu-compositor.md](docs/gpu-compositor.md) (last sections);
every owner report is a row in [docs/owner-issues.md](docs/owner-issues.md).

**The goal in force** (owner's `/goal`, 9 November; he keeps the text in chat): 4K 120 fps in Remastered without changing game logic, pictures byte-exact except owner-approved visual changes. Work order:

1. R38: DONE (on by default, below).
2. Clean 4K hardware GPU timestamps on the runway replay and the owner's late flight. If the GPU passes alone exceed 16.7 or 8.3 ms, do step 7 before steps 3 to 6 and report the gap.
3. Overlap's true unpaced 4K cost (the worker-checker repair is done; keep its evidence).
4. The emulated machine on its own thread, handing state over at today's points.
5. Pipeline machine, copy, composite and GPU frames; measure the added latency.
6. Parallel copy machines; cut the picture-layer cost (changed-content redraw or the GPU).
7. Cheaper exact bands and strokes, guided by the timestamps.
8. If exact rendering cannot reach the budget: STOP at an owner gate with measured alternatives (hardware rasterisation with bounded edge differences, or exact pixel-shader testing). Stage 2 needs approval.

Pass lines: two runway replays each at 4K Maximum, Model detail Full; 60 fps is zero frames over 16.7 ms, 120 fps zero over 8.3 ms; report Near and Far too. **Neither is met.** Line thickness and the real
terrain and sea dots are separate from R38: show evidence-based options, bundle nothing.

**Where the code is.** `master`, see the git log (CI green through `34b453b`). `Desktop\ONTests` and `build/f117a.exe` are both **10 November 11:13:51** (step 6, both rounds). The guard baseline
(`private/renderer-bench/baseline`) was promoted that evening; the previous one is in `private/renderer-bench/baseline-r38-prior`. Defaults: exact rasterising, GPU composition and presentation, overlap mode 2,
Model detail Full, R37 square pen, R36 smooth sea, R35 Perspective far light size, Near terrain dots, **R38 on**. Optional GPU hardware timestamps are in (`F117_GPU_TIMESTAMPS` configure option, default OFF;
runtime needs `F117_GPU_TIMESTAMP_LOG`). No game process was running at the stop.

**R38 (teeth on far slope lines): finished, ON by default** (`F117_DECAL_FACE=0` turns it off; exact mode only). The full record is the last two R38 sections of `docs/gpu-compositor.md`. In short: the owner
rejected the first candidate (never ask to approve it again) and said "much better" to the revised rule; two defects were found and fixed on the way: tiled CPU rows leaked the previous tile's paint owner into
the band faces (`87783ef`, visible only at 4K on the late flight), and a hangar door-line pixel showed at a silhouette edge (`900fb52`: an uncovered stroke more than 5% behind the surface stays hidden; the five
reviewed scenes stay byte-identical). On build 21:10:17: machine hash `600306c77cb04481`; CPU and GPU guards 901/901 at Near, Far and Maximum; guards with the switch off 901/901 against the previous
baseline; live GPU checks 0 differing pixels in 3,421 pictures per distance; host acceptance 12/12; CTest 846/846. Cost: about 1 ms a frame at 4K Maximum (paired runs). **The owner has not flown an R38
build**: ONTests is ready for it. Scripts and logs: `private/r38-validate`. **Never rerun `private/r38-same-pixel/integrate.py`.**

**Steps 1 and 6 (first part) are done (10 Nov)** (details: `docs/gpu-compositor.md`, the last two sections; scripts and CSVs `private/timestamp-4k`, `private/step6`).
- Clean 4K hardware timestamps: the GPU passes alone were over both lines; bands and strokes were 85 to 90% of the GPU time. On the owner's late flight the CPU side was larger still: the worker's `gpu_begin` grew from 72 to 177 ms.
- Four exact changes, all through the full required set on build 10 Nov 09:21:17 (hash, CPU and GPU guards 901/901 at Near, Far and Maximum, live checks 0 differing, host acceptance 12/12, CPU against GPU 4K shots at late-flight clocks 9.5 and 10.35 billion 0 differing): the bands pass per-group cull (bands 8.2 to 2.6 ms; the runway whole-frame mean 18.8 to 14.35 ms); once-per-picture environment reads in `gpu_compose_begin` (two `getenv` calls per record had made classify 59 of 63 ms of CPU setup); a depth-only plane setup (`ex_plane0_make`, bit-identical); a fast tile binning (`bin_tiles_fast`, identical, small gain). **Late flight whole-frame mean 147 to 54.9 ms after the environment reads; then, paired in one session, the depth-only planes and fast binning take 68 to 56 ms** (runs made at different times drift by about 20% on this machine: only alternating A/B pairs count, with `F117_PLANE_FULL=1` / `F117_BIN_REF=1` for the old behaviour). A float prefilter on the bands edge test was tried first and made it 2.9 times slower: do not repeat.
- A second round (build 10 Nov 11:13:51, same full set; host acceptance only when run alone, see the trap below): `ex_llround` and `ex_pow2` in `exact.h` replace `llround` and `ldexp` (identical on 4e8 test inputs and on every vertex and plane of both replays in the app): depth planes 300 to 85 cycles a triangle, snapped vertices 400 to 69, classify 17-20 to 10-13 ms on the late flight. Two stroke-shader attempts did not help and were reverted (docs).
- Diagnostics left in: `F117_REMASTER_TIMING` also prints `[gpu-phases]`, `[gpu-classify]` (cycle shares by record kind) and `[gpu-prepare]` (scene recording, 2 ms: not a cost); `F117_BIN_CHECK=1` compares the fast binning, the depth-only planes (against `planes_ref`) and the snapped vertices (against `llround`) on every picture; `F117_PLANE_FULL=1` and `F117_BIN_REF=1` select the old behaviour for alternating A/B runs.

**OWNER GATE: DECIDED 10 November 15:14: "B then C... and D if still needed"; 15:20: "actually skip C".** So B (hardware rasterisation with bounded edge differences) is approved as the next step, then D (Stage 2) only if still needed; C (exact pixel-shader testing) is dropped; each needs its own measured tolerance or visual review before it becomes default. Gate text as asked:

**Latest (10 Nov evening): simplified stroke decal rule ON by default (owner decided).** The stroke shader skips the exact rule's neighbourhood walk (`stroke_hidden`); `F117_DECAL_SIMPLE=0` restores the exact rule. Late flight at 4K Maximum: strokes pass 11.9 to 3.0 ms mean (p95 29.6 to 6.5 ms), about 216 of 8.3 million pixels differ from exact at clocks 9.5 and 10.35 billion; the measurements and the dropped variants are in `docs/gpu-compositor.md` (last section). The exact path is frozen: `guards.sh` sets `F117_DECAL_SIMPLE=0` by default, so the 901 checksums and the live checks still prove it. Checks on build 10 Nov 18:39:59: hash `600306c77cb04481`, guards 901/901, live 0 differing, 4K shots 0 differing, host 12/12 (alone), CTest 846/846 on the previous build and the unit tests on this one. **Measured 10 Nov evening (docs/gpu-compositor.md, last two sections): neither flight is GPU-bound now.** The render worker is the critical path. The CPU setup's classify and pack loops now run on a thread pool (`F117_SETUP_THREADS`, byte-identical, checked by `F117_SETUP_CHECK=1`): late flight setup 28.7 to 19.5 ms, worker frame 41.1 to 29.9 ms mean (p95 59.3 to 43.6); runway about 12.6 to 12.4 ms. Remaining late-flight setup: classify 5.8, binning 4.0, packing 3.1 (face cache), copy 1.5, scene preparation 2.5. Runway: the picture step (render copy and picture layer) 7.5 of 11.5 ms. **OWNER GATE for the next step:** thread binning and the face cache (exact), then Stage 2 for the runway's picture step. The hardware-rasterised compositor is deferred. Not done yet: a checksum baseline for the simplified rule itself, and the wider check of the differing-pixel count at 8 to 10 more clocks.

**(10 November, how to go on toward 4K 120 fps.)** Measured, exact rendering, 4K Maximum, Model detail Full, unpaced, on build 10 Nov 11:13:51 (hardware GPU intervals for the GPU figures; frame means from the present log; runs at different times drift by about 20% on this machine):

| | Runway replay | Owner's late flight |
|---|---|---|
| Frame mean (unpaced) | about 14.4 ms, p95 16.7 (earlier run) | about 45 ms (earlier 147 before this work) |
| GPU per scene, hardware timestamps | about 8 to 17 ms (bands 2.6, polygons about 1, strokes about 7) | about 17 to 32 ms (bands 2.7, polygons 1.3, strokes 13; 9 of the 13 are `stroke_hidden`) |
| CPU setup per picture (`gpu_begin`) | not split out | about 23 ms (classify 10 to 13, binning 3, packing 2.5, copy 1.2, scene recording 2) |
| Needed | 60 fps: zero frames over 16.7 ms; 120 fps: zero over 8.3 ms | same |

What the figures say: **the GPU's exact compute passes alone (8 to 17 ms on the runway, 17 to 32 on the late flight) already exceed the 120 fps budget of 8.3 ms, so no amount of CPU-side work or pipelining reaches 120 fps with this renderer.** 60 fps is within reach on the runway replay (frames are already near 16.7 ms) and not on the late flight (GPU alone is 17 to 32 ms, plus the CPU setup, which today runs in series with it in the one worker thread: `begin`, then the picture layer overlapping the GPU, then the compose wait).

Options for the owner (estimates are mine, not measured): **A, stay exact and keep optimising:** pipeline the worker across frames (two GPU contexts in flight, double-buffered scenes: the late flight about 45 to 30 ms, the runway about 14 to 11 ms) and attack the stroke decal logic (the late flight's GPU 17 to 32 might fall by a third at best): 60 fps on the runway likely, the late flight not, 120 fps no; days of work with a heavy verification load (pictures byte-identical, three concurrent replays). **B, hardware rasterisation with bounded edge differences** (the GPU's own triangle setup and depth buffer instead of per-pixel compute over tile lists): large GPU savings expected, but pictures differ slightly at edges, the exact CPU-equals-GPU rule would become a tolerance rule (guards with a bound), and it needs the owner's approval. **C, exact pixel-shader testing with ordered pixel access** (rasterizer ordered views): keeps exactness, depends on hardware support, and the same order-dependence cost may not fall enough. **D, Stage 2** (capture once per game step, draw every displayed frame from the interpolated camera): decouples the display rate from the game's, but in-between pictures are not byte-identical to today's, and it needs the owner's approval. Nothing is removed from the scene in any option.

**Side items, status:** the data-folder rename (owner chose `%APPDATA%\OpenNighthawk`, saves and the 131 flights moved on first run) is written but NOT built or tested (uncommitted: `src/host/main.c` `default_data_folder`, `tools/uninstall.ps1`, docs); test it on a scratch `APPDATA` before the owner runs it (after it runs, the replay scripts under `private/` that name `AppData/Roaming/F117Recomp/F-117A/sessions` need the new path). Realistic-lighting item: `docs/roadmap.md`, waits for the owner.

**Measured so far** (details in `docs/gpu-compositor.md`): runway replay, 4K Maximum Full, overlap mode 2: mean 16.72 ms, p95 17.9, about half the frames over 16.7 ms (7 November, probably paced by vsync:
measure unpaced). Unpaced on 9 November (R38 off): 17.6 to 21.9 ms mean, p95 21.6 to 26.9, in two sessions whose machine state differed; with R38 on about 1 ms more. The owner's late flight `session-20261108-211031`, unpaced 4K Maximum Full: mean 163.69 ms, p95 234.13, worsening from 167 ms at clock 7 billion to 227 ms at 10 billion as the scene grows from
about 35,700 to 62,000 primitives; the slowdown predates the 8 November defaults. A split diagnostic there (with fence overhead, not hardware timestamps) put bands at 19 to 28 ms and stroke pixels at 7 to 17 ms.

**Required checks and rules** (every renderer step):

- Machine hash `600306c77cb04481`: `guards.sh statehash` with `OLD=D:/f117-dl/old-ontests/f117a-20261104-2207.exe`.
- CPU and GPU guards 901/901 at Near, Far and Maximum with `F117_FAR_WIDE_ACROSS=0 F117_FAR_WIDE_UP=100 F117_WIDE_UP_ALLOW=0`; live GPU checks with zero differing pixels; host acceptance 12/12; full CTest
  before pushing a completed step; CI green. Interim builds need only guards and cost; the full set runs once on the build that becomes the default. Partial results are never passes.
- Pass `EXE=` on every guard, bench and hash run and report the executable's build time (from a worktree the scripts otherwise run the main checkout's exe). One timed run at a time, paired A/B; concurrent
  runs are not performance evidence. Benchmark with `F117_NO_VSYNC=1`.
- No stray `f117a.exe` before or after a run, no visible windows, no sound, private saves. Never write to the game install or the owner's folders except an authorised ONTests refresh: copy the exe, every DXIL,
  `SDL3.dll` and `mt32emu-2.dll` (never the two `*_api.dll` tooling libraries), then wait explicitly for a hidden private-save launch to exit 0 (`Start-Process -Wait`; a plain GUI launch returns no exit code).
- Replays of the owner's flights recorded before 7 November 18:06 need `--adlib-effects` when his ini had Roland, or they drift and crash (R28); later logs carry the setting.
- Stop before touching `src/matched`, `src/machine`, `tests/func_lockstep.c` or core CMake. Commit and push each verified step with explicit paths; no AI attribution; nothing from kylofon's projects.
- Preserve the owner's uncommitted `tools/sound_board_page.py`, `tools/sound_board_template.html` and untracked `docs/p2-completion-plan.md`.

**Open owner gates:** his flight of ONTests (R30, R31, R32, R36, the 8 November defaults and R38); the CRT look (`Desktop\ONShots\r25_crt_options.png`, R25); the deletions in
[docs/worktree-audit-20261107.md](docs/worktree-audit-20261107.md); Stage 2; any release (the first, `v2026.11.07`, is published; ask before another). Still owed from the first release: a second PC and
his own fresh-download run.

**Traps met in this stretch** (also: run host acceptance alone, not beside the guards, because `transport-and-screen-edge` is load-sensitive; compare timings only in alternating pairs in one session, because this machine drifts by about 20% between runs) (the general list is under Traps below): a game replay holds `build/f117a.exe` and `SDL3.dll`, so a relink fails while one runs; stopping a task leaves its bash and game
children running, so kill them and confirm; screenshot files are named by wall time, so pair off/on shots by order, not name; a 4K software capture takes minutes per shot; the 1080p guards never tile rows, so compare CPU and GPU at 4K on the owner's late flight; figures from timing sessions hours apart do not compare.

