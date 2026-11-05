# Handoff

## Ready for a new conversation: 5 November, presentation independent of resolution and refresh rate

The owner asked to find a stopping place, hand off, commit and push. Phase 3 remains incomplete; this session was all renderer speed. Full figures, switches and the plan:
[docs/gpu-compositor.md](docs/gpu-compositor.md), last section ("Two-stage plan, stage 1 so far"); the older material below and in [docs/p3-handoff.md](docs/p3-handoff.md) (short Phase 3 working notes; the dated log is archived in [docs/archive/](docs/archive/)) still holds.
**Read this section to the end before changing a default** (the last session flipped three defaults against the newest handoff text once).

**The goal in force when the session ended** (owner's words, 4 Nov 23:13): make the Remastered renderer independent of resolution and refresh rate, so a system like the owner's (Ryzen 9 7950X, RTX) runs
native 4K at 144 fps and 1080p well past 120, without touching game logic. The CPU decides what to draw once per game step; the GPU does all pixel work every displayed frame.
Stage 1 (4K 60, per-frame copy kept): (1) merge the GPU present path and measure; (2) a bit-exact GPU compositor against the CPU one; (3) the remaining per-pixel CPU work onto the GPU. Pass line: hidden 3840x2160
GPU-renderer window, runway replay, p95 work per frame under 16.7 ms at Near, Far and Maximum, two runs each, checksums identical to the CPU compositor, then a `--present-log` from the owner's own 4K window.
Stage 2 (decouple from the display rate): run the game's drawing once per game step, capture the scene as 3D primitives with a view wide enough for the camera's motion, draw it on the GPU every frame from the
interpolated camera, retire the per-frame copy only after: game-step frames byte-identical to the copy's, in-between frames with static-scenery vertices within half a game pixel, at most 0.5% of pixels differing
on average and 2% in any frame, moving objects and per-step detail decisions reported separately, a screenshot review by the owner at the first working version, and the owner flying it and judging the motion
smooth. Pass line: p95 under 6.9 ms (144 fps) at 4K at Near, Far and Maximum, two runs each; 1080p reported the same way. Guards on every step: the end-of-run machine hash and the picture checksums identical,
commit and push each verified step, stop and report rather than ship an approximation. (The owner's decisions behind it: never cut corners, aim for 4K 144 as headroom, higher than 60 Hz is by measurement
since the owner's monitor is 60 Hz.)

**The plan, in order, with status** (details and tables: [docs/gpu-compositor.md](docs/gpu-compositor.md), last section)

1. **Done:** stage 1 step 1, the GPU present path merged to `master` (`e79203e`, SDL 3.4.8, behind `F117_GPU_PRESENT=1`; `F117_GPU_COMPOSE=1` is the GPU compositor; both default off).
2. **Next:** instrument the worker by stage at 4K (the game's picture layer redrawn at 4K, the replace pass, the GPU begin and finish; milliseconds each). It is the critical path at 4K (below), and this says exactly what step 3 must move to the GPU.
3. **Stage 1 step 2:** a bit-exact GPU compositor: integer edge equations and integer depth on both sides, the CPU compositor as the oracle, identical fixed-cadence checksums at Near, Far and Maximum. Today the GPU compositor differs in a bounded number of
   pixels by float rounding (the fast band shader works in float, the exact one in double; weights are evaluated in double on the CPU at each 16 x 16 tile's origin).
4. **Stage 1 step 3:** the per-pixel CPU work that remains at 4K (the game's picture layer, the replace pass) onto the GPU. Then stage 1's pass line and a `--present-log` from the owner's own 4K window.
5. **Stage 2 steps 4 to 6** as in the goal above: one capture of the scene per game step with a wide view, drawn by the GPU every displayed frame from the interpolated camera; the per-frame copy retired only after the checks and the owner's approval.
6. **The native renderer track** (the owner's 3 November target: frames built natively from the game's state, the render copy becoming the oracle) continues alongside, not blocking 4K 60. It is what the stage 2 capture eventually wants, and its gate is the native terrain walk (below).

**Facts that shape the plan** (all measured; the runway replay 20261103-154444, clocks 3.9 to 4.8 billion, unless said)

- **The per-frame copy at Maximum, mean 6.5 ms** (`F117_PROFILE=1`, `F117_WIDE_WALK_STATS=1`): copy start 0.75 ms (192,000 original instructions); far scenery 3.35 ms (choosing about 1.7 ms from 74,005 listed objects, now threaded and exact;
  placing about 10,000 survivors 2.5 to 2.9 ms, one thread: `remaster_live_native` patches the machine's template area and uses a scene reader with caches, so threading it needs private copies per thread); the game's own terrain walk and scene 1.64 ms; the
  game's frame drawing (HUD and cockpit) 0.68 ms (3.46 million instructions, matched C at full speed). Half the copy is my native code, not emulation and not Phase 2 routines.
- **At 4K the frame is a chain of waits:** CPU about 4.7% of the chip (about 1.5 of 32 threads), GPU close to idle; the main thread waits about 6 ms for the worker and then runs the copy about 5 ms. The worker is the critical path, so speeding up the copy
  does not help 4K. Overlapping the emulator with the copy (a thread-local observer in `src/matched/observe.c`, which the owner approved editing) and threading the placing only help the per-frame copy that stage 2 retires: do them only if the step 2 measurement says the CPU chain still holds 4K back.
- **Stage 2 is feasible but not free:** in-between frames made by blending two copies (measured with `F117_BLEND_CHECK`, `F117_BLEND_PICTURE`): primitives pair by key 98.1 to 98.7% across three flights; 91% of paired vertices are within 0.25 game pixel of
  the copy's, 2% off by 4 or more; the blended scene differs from the copy's in 0.35% of pixels. Only 35 to 40% of a copy's primitives exist in the live game step (27% native at Near, 69% at Maximum), so the once-per-step capture must come from a render
  copy with a wide view, not from the live step; that puts the native terrain walk on its critical path. Per-step decisions (detail levels, draw-distance culling) can switch up to one step (1/15 s) earlier or later than in the copy: that is closer to the
  original, but it counts against the 0.5% and 2% caps and is reported separately.
- **The native terrain walk** (`F117_NATIVE_WALK`, off): the picture differs from the game's walk by about 200 pixels (223 and 202 at clocks 4.0 and 4.4 billion), down from about 2,600 after the base-tile cull fix; the later across-test fix (own-level objects now read the original table word: 86% of the game's model-37 objects are chosen) left the difference unchanged. Model counts are still short against the game's: models 37, 39, 40 and 44 at 81, 51, 10 and 20 against 129, 77, 20 and 40. Ruled out, no picture change: cell order, step rules, near plane, deferred drain,
  `C06E`/`C070`. The remaining suspect is the own-cell deferring objects' primitives after the guest placement; **the next measurement is a per-copy count of model-37 primitives by source** (native, game-placed, deferred). Evidence: [docs/scenery-distance.md](docs/scenery-distance.md).
- **The GPU path** (piece 3, merged): 4K Near about 77 fps against about 64 with a readback in throughput runs; the p95 table is in the next section. Presenting without a readback needs SDL 3.4: `SDL_CreateGPURenderer`, `SDL_GetGPURendererDevice` and
  `SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER` to wrap the compositor's texture. With vsync on the GPU renderer blocks in its draw calls, so benchmark with `F117_NO_VSYNC=1`.
- **Owner decisions behind the plan:** never cut corners; the Far object order stays Exact by default (Fast is a setting); the radix sort is opt-in; SDL 3.4.8 on `master` (approved by pasting the goal); the owner's order after the renderer is hybrid sound; higher than 60 Hz is by
  measurement (the owner's monitor is 60 Hz). The owner's own window measured 1080p Maximum p95 about 13 ms, 60 fps locked, before today's threading.

**State at the end** (everything pushed; the renderer commits of 5 November end at `e79203e`, later commits are documentation):

- Done: the GPU present path merged (SDL 3.4.8, behind `F117_GPU_PRESENT=1`; both GPU switches default off); the far scenery's choosing threaded and proven exact; the Far object order setting (Exact default,
  Fast opt-in); the owner's first own-window log (1080p Maximum, p95 about 13 ms, 60 fps locked).
- **Verification owed (5 November):** `src/present/rerun.c` (the threaded far-scenery choosing, the Far object order) and `src/host/main.c` changed today. By [docs/repeated-processes.md](docs/repeated-processes.md) ("The light check") a `rerun.c` change needs the full gate (`tools/build_recomp.py`) plus the all-route `tools/presentation_check.py --mode rerun`, which also proves the machine copies leave real memory and fix state untouched. Neither has been run on these commits; what was run is listed in the next bullet. The owner lifted the gate limit on 3 November, so run both before treating the threaded choosing as accepted, and before the next Phase 2 `kit.py checkpoints`.
- Checks on the merged build: host acceptance passes all twelve cases (two older checker defects fixed: stale menu row counts in `tools/settings_acceptance.py` and a 6-column present-log assumption; [docs/host-acceptance.md](docs/host-acceptance.md)), the in-repo guard scripts reproduce the 901-checksum baseline at all three distances, the quick CTest set passes.
- Measured, p95 work per frame, hidden window: 1080p CPU path 7.9 to 11.2 ms at all three distances (under 12); 4K CPU path 32 to 40 ms; 4K GPU path 14.5 to 17.1 ms (stage 1's 16.7 met in five of six runs,
  checksum-identical to the CPU compositor not yet).
- The finding that sets the next step: at 4K the CPU is about 1.5 of 32 threads busy and the GPU close to idle; the frame is a chain, the main thread waiting on the **worker** about 6 ms and then the copy about 5 ms.
  The worker, not the copy, is the critical path at 4K. **Next: instrument the worker by stage at 4K, then step 2 (bit-exact GPU compositing), then step 3.**
- Not done: the native terrain walk (still about 200 pixels off the game's picture at 4.0 and 4.4 billion clocks; stays off; models 37, 39, 40 and 44 under-counted); threading the placing (about 2.5 ms at
  Maximum; needs private template copies per thread); overlapping the emulator with the copy (needs a thread-local observer in `src/matched/observe.c`: the owner approved editing it); both only matter for the
  per-frame copy that stage 2 retires.
- Owner action pending: an own-window `--present-log` at 4K (and again at 1080p) with a build that carries today's changes. `Desktop\ONTests` still holds the earlier build (before the threading, the Far object order
  setting and the SDL merge) plus the launchers `ON-presentlog.bat` and `ON-presentlog-gpu.bat`; refresh `f117a.exe`, `SDL3.dll` (3.4.8 now) and the `gpu_*.dxil` shader files together, then make a 4K launcher
  (`F117_WINDOW=3840x2160`, `F117_GPU_PRESENT=1`, `F117_GPU_COMPOSE=1`). The backup of the pre-refresh exe is `private/ontests-20261104/`; the two logs are in `private/present-logs-20261104/`.
- Guards and benchmarks (in the repository since the doc pass): [tools/renderer_bench/](tools/renderer_bench/README.md): `bench.sh NAME DISTANCE` (runway-replay timing; `MODE=gpu`, `WIN=3840x2160 PSCALE=9` for 4K) and `guards.sh checksums TAG` / `baseline` / `statehash`. The recorded flight, the ONTests ini it needs and the 901-checksum baselines (taken at `03c6631`) are in `private/renderer-bench/` (git-ignored; if lost, regenerate the baseline from a build at or before `85ec934`; the flight is the owner's session `20261103-154444`). The final machine hash of the replay is `600306c77cb04481`.
- Traps met this session: `ctest` with no filter starts the long machine gates (use `ctest -R "host_|remaster|presentation"`); a benchmark script that passes an option it never reads runs the old setting (check the
  "fast" run really differs); the GPU utilization counter must include the compute engine; with vsync on the GPU renderer blocks in its draw calls, so benchmark with `F117_NO_VSYNC=1`.
- Untouched on purpose: the owner's `tools/sound_board_page.py` and `tools/sound_board_template.html` edits, and the untracked `4k60.bat`, `original-fixed.bat`, `docs/p2-completion-plan.md` and strays.

---

## Standing state (P2, P3, housekeeping)

For the next conversation on this repository. Read the section above, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left, the batch table),
[docs/progress.md](docs/progress.md) (what the scores measure),
[docs/architecture.md](docs/architecture.md) (how parity is built and checked),
[docs/repeated-processes.md](docs/repeated-processes.md) (every repeated process, its command and cost),
[docs/bugs.md](docs/bugs.md) (the original's bugs and our fixes),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and, for Phase 3,
[docs/p3-handoff.md](docs/p3-handoff.md) and
[docs/scenery-distance.md](docs/scenery-distance.md) (the dated Phase 3 logs are in [docs/archive/](docs/archive/)). This file holds only the current state, how to resume, open decisions and traps; dated history is in `git log` and in the
roadmap's batch table. Keep it that way: replace, do not append.
**The owner's reported issues are tracked one by one in [docs/owner-issues.md](docs/owner-issues.md) (status and evidence for each; do not drop an item until the owner agrees). Read it before anything else.**

**Scoreboard:** P1 100.00%, P2 96.93%, P3 94.03%, All 97.55% (`py tools/progress.py --title`; never copy a title from an older commit). Do not claim Phase 3 completion.

**Two threads run side by side** on `master`: Phase 2 (matched code, separate session) and Phase 3 (host and presentation). Phase 3 work stays out of `src/matched`, `src/machine`, `tests/func_lockstep.c` and core
CMake, with one exception the owner approved: making the observer in `src/matched/observe.c` thread-local, for the render copy. `git fetch` before every push and check for a running gate before a heavy job.

**Phase 2 (96.93%, 1,261 route-executed unmatched bytes).** Batches up to `bz` are accepted. The grouped `ca` batch (four sound tails, 80 route bytes, and seven SETUP wrappers, 207 static bytes) is integrated but
unaccepted and earns no credit; the 34 VGAME loader-patched thunks were removed because 29 of them overlap the graphics observers (a matched thunk would stop the observer capturing the entry; see
[docs/p2-brief-ci-failure.md](docs/p2-brief-ci-failure.md)). ASOUND.117 `09F2` stays excluded pending the D2-fix decision; ISOUND.LOG `0581` needs an exact invalid-range certificate. The all-code gap is 17,361 bytes;
reconcile `docs/static-code-review.md`'s 10,301-byte report with the current census before calling that review complete. Known failure on a clean checkout, not from Phase 3: `func_lockstep` fails four sound routines
(RSOUND.LOG `03A2`, RSOUND.117 `0D07` and `0ACE`, ASOUND.117 `005E`: "with N clocks of room the body declined"). A batch aims for 1,000 to 1,500 newly proved route bytes: count only exact new route-executed credit.
Next for `ca`: the combined `func_lockstep` fixture body is too large to compile in the memory available (an MSVC compile was interrupted and left a zero-byte object, since removed): find a
lower-memory compile path or split the fixture body, then run the grouped gate before any credit.

**Phase 3 (94.03%).** In: the in-app Remastered path (`src/present/remaster_live.c`, `rerun.c`, the CPU and GPU compositors), the settings menu and Keys tab ([docs/settings.md](docs/settings.md), checked by
[docs/host-acceptance.md](docs/host-acceptance.md)), hybrid sound, the adjustable draw distance, Terrain dots and Far object order settings. Open: the native renderer (see the section above), interpolation and road motion,
widescreen HUD and cockpit, controller or HOTAS and button remapping, replay cameras, accessibility (a colourblind-safe HUD, subtitles under AdLib), the remaining settings gaps, hybrid-sound listening checks and the
owner's approval of what they have not yet looked at. Remastered changes are presentation overrides; the simulation boundary stays intact. The owner's coastline and taxi tests were on the recomp, not GOG, so they
say nothing about the original's collision behaviour. Renderer shots go to `Desktop\ONShots` named `YYYYMMDD-HHMMSS-name.png`; gradients smooth, no visible bands; depth-correct drawing is the point; read
`src/matched/matched.c` before guessing and say what was read.

**Build and run.** `.\build.cmd` from PowerShell; headless runs with `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`. The bash tool eats backslashes and a Python heredoc turns `\n` in C strings into real
newlines: write patch scripts with the Write tool, fix single lines with Edit, and encode a script's whole output before opening a file for writing (a failed write truncates). A worktree needs
`build.cmd "-DF117R_GEN_DIR=C:/Users/Tideg/f117-recomp-local/gen"` or the lockstep tests are not registered.

**Documentation (read in full on 5 November).** All 37 markdown files were read and corrected against the code and `tools/progress.py`; dated logs were moved to [docs/archive/](docs/archive/) rather than deleted. Two figures are still unreconciled and are flagged in place: the static-review gap (10,301 bytes in the 21 October analysis against 11,601 reviewed static bytes now, [docs/static-code-review.md](docs/static-code-review.md)) and the checklist denominator (47,736 in the round-10 text against 47,952 now, [docs/roadmap.md](docs/roadmap.md)). The owner's untracked `docs/p2-completion-plan.md` (1 November) is a plan for Phase 2 and agrees with the current figures. When a number in a doc and `py tools/progress.py` disagree, the tool is right: fix every copy.

**Housekeeping (nothing here is deleted without the owner).** About 26 worktree folders remain under `D:/f117-wt` on branches already in `master`; `private/` holds the ONTests backups and the own-window logs. The local
`rescue/<name>-20261031` branches are unpushed snapshots of old worktrees (some carry private overrides and the repository is public, so never push them); every one is superseded except that the 31 October `04DD`
job has since landed (`dea8936`) and `rescue/wt-20261031` is also in `D:/f117-archive/orphans-20261031.bundle`. Five stashes exist (`git stash list`), the first `preserve owner docs before P3 acceptance merge`: not
dropped. Old WIP branches (`present-horizon`, `present-hud2`, `wip-view-compose-2f9d`, `draw-rerun-probe`, `origin/p2-batch-setup-logo`, `origin/p2-wt-batch`) are superseded and safe to delete when the owner agrees.
The lockstep hang at `START.EXE:8A32` was a cpu_step prefix runaway, fixed in `tests/func_lockstep.c` (`7967b67`: a 64-prefix guard and `F117_LOCKSTEP_WATCHDOG=N`).

## Owner decisions in force

Scoring decisions with dates are in [docs/progress.md](docs/progress.md); this is what governs daily work.

- **Static code is matchable on lockstep alone (owner, 29 Oct 2026).** Reviewed static ranges in `docs/census-code.tsv` that no
  captured route reaches are accepted without a real-route call: seeds, all mutants, audit, gate and the route-neutrality
  replay (`kit.py DIR checkpoints --static`, or `"static": true` in the proof's config.json so `finish_all.py` does it);
  the kit refuses it for code any capture reaches. Credit is the naming item's, not the gameplay score's.
- **No meaningful game code may be missed.** Launcher, resident graphics library and sound drivers are in Phase 2
  scope; declined routines are work, not exclusions; every range that cannot be matched gets a reviewed reference
  entry. The checklist is done in full by independent blind readers; cost is no object, optimise wall time only.
- **Design rules for code that stays original:** the program launcher (F117.COM `0100..03CA`), hardware wait loops
  only when no lockstep state can complete them (so far none qualifies), STI-first interrupt handlers, the packer
  stub, DSWAP's floppy path (no emulated floppy drive), and presentation-observer graphics slots. Each affected range
  needs its own reviewed reference entry before credit. Dead code leaves the denominator only with a certificate.
- **PLAYER grayscale arm (owner, 30 Oct 2026):** `00D5F-00DAF` remains matching work. The palette-loader rule does
  not extend to this exact fragment; keep it `needs-route` until a route selects it, with no census-original split.
- **Parity target is the original on its own hardware, not DOSBox**; gameplay and logic parity outrank rendering.
  Keep all original graphics; no vector replacements.
- **Phase 3 features are off or inert unless asked for:** `--crt`, `--subtitles`, `--sound`, `--remap`, `pad-*`, `--watch` and the
  settings menu (Ctrl+Alt+S) change nothing in the game's logic; the owner has not yet looked at any of them.
- **Normal play:** 15 Hz / 60 MIPS, all 33 normal-play fixes on (`--fix QUIZ` adds the quiz option, off by default; Q11 was retired on 1 November). Parity
  commands use `--fix none`.
- **Never weaken a check to go faster.** Safe optimisation and parallelism are approved; batches of 30-50 routines
  per gate; no proof agents during a gate.
- **No screen takeover, no sound:** emulators run headless, silent and hidden; ask before anything visible.
- **Fixes to accepted bodies are re-proved and gated before they are committed as accepted.**
- Keep going item by item; ask only on policy. The owner is not a software engineer: explain trade-offs plainly.

## Phase 2 batch workflow

The full steps, commands and costs are in
[repeated-processes.md](docs/repeated-processes.md#phase-2-batch-acceptance) (with the fix-only gate and the checklist
blind-read round). What a resuming session must not skip:

- Proof agents run kit steps 1-7 only (`D:/f117-gate/kit/kit.py`, brief `kit/BRIEF.md` + `kit/AGENT_PROMPT.txt`);
  root verifies every result itself (seeds, `mutation-results.json`, seed-log tallies, file ages) before
  `finish_all.py` (any number of directories, six side by side on CPUs 20-31).
- Hand-apply the harness rows `integrate_many.py` does not carry (list under Traps), build with `.\build.cmd` from
  PowerShell, run only the new CTests, then `route_precheck.py` on every new entry and two whole-table seeds.
  Shared-helper and fix-only repairs require a third pre-gate seed.
- Run `audit_private.py` BEFORE the gate (the `new_batch.py` help text says afterwards: it is wrong); copy it and
  `finalize.py` forward from the previous batch. Check `final-gate-verdict.json`'s `source_sha256` against the tree.
- Commit titles start with `py tools/progress.py --title`; refresh the score with the new batch's named cover.

Running wide: a proof is about 10-15 minutes of compute inside about two hours of agent time, so wall time follows
the number of agents. The current user limit is four agents at once. `kit.py` throttles builds to three
`cl.exe` machine-wide (about 6 GB each of 31 GB). Spare-thread mask during a gate: `0xFF000000`; no proof agents
run while a gate does.

## Traps

Writing bodies:
- **Each block's room claim must cover its longest path, rare early paths included** (bm2: `2F4C`'s `|dx| = 0`
  path). A block followed by a CALL needs `room(c, N + 1)` (`guest_call` charges the CALL); an INT 21h through
  `st4_int21` needs its own claim (`SM4_NEED(1, ip)`). To find a short claim, log each claim's clock and the clock
  at the next one; the gap that exceeds its claim is the culprit.
- Read every argument when the original reads it, through BP (`bp_arg`): DS and SS can overlap. Read segment
  immediates from the code (`seg_read16`); overlays and drivers load at 0800h in the harness.
- Charge the clock between port accesses; stop right after each STI (`x86_sti` before charging it) and after any
  OUT that wakes the run loop. Do not run C after a nested callee that executes STI.
- A wait inside a matched routine clamps `m->run_until` to `c->stop_at`; check every callee's RET form.
- A switchable fix site inside a routine needs a `fix_inside` guard; observer addresses (`1450`, `hook_fill_rows`)
  need the body to call the observer and an entry on `deliberate` in `tests/test_overrides.c`.
- Never mass-edit bodies with a private proof: `finalize.py` requires each proof's `body.c` verbatim in `matched.c`.
- `XW8` declares its own local `a_`: never pass a caller's `a_` to `XW8`/`XW16`.

Lockstep and harness:
- `func_lockstep --only MOD.EXE:HEX`: no leading zeros in the IP (`D0C`), or it silently compares 0 states.
- Harness rows `integrate_many.py` does not carry: SPANS beyond the kit's row, `SPANS_SEG`, IMAGE_PATCHES,
  LIVE_FRAMES, PROGRAM_ENDS, POPF_STOPS, INTERRUPT_FRAMES, `patch_image_for`, `run_to_ret` rules, `g_native_int10`
  / `g_native_int1a`, `g_cov_ip` lines, the `setargv_` pop-distance list. Diff each `tests-base.c` against HEAD.
- Fixtures force each branch outcome in a fixed share of states; prove at six or more seeds. A lone mismatch with
  every line `equal` is a coverage floor (fixture), not a body defect.
- Equivalent mutants (ignored-result CMP-to-SUB, early hand-backs from over-claims) are replaced, and the failed
  run kept as evidence; a mutant that is missed because a path is never exercised is NOT equivalent.
- The commit-derived seed rotates with HEAD and can fail an untouched routine: reproduce with
  `--seed S --only MOD:IP`, then trace the failing state.
- When engines differ on a route, find the first diverging instruction (`--hash-every`/`--hash-from`, then a
  `--trace` diff) before theorising. Bisect with an argument list, never a shell `eval`.
- `kit.py checkpoints` needs the interpreter cache for the current key: after any `src/` change outside
  `matched.c`, run a gate first. A failed gate's directory blocks a rerun: archive it and recreate.
- `matched.c` compiles with `/Os` (at `/O2` it took 90+ minutes).
- The lockstep machine runs at zero IPS unless a fixture sets it (so IO_SLACK is zero) and stubs the production bus delays: a fixture that probes room must set `MACHINE_APP_IPS`, and the clocks it sees are the
  harness's, not production estimates. ASOUND.117 `0A5B` must hand back right after the PIT0 control-word wake at `0AC4`: the old 165-byte claim ran across it; never restore a claim by size.
- Ghidra 12.2's headless launcher needs the installed JDK 25 override, not the default JDK 8. Reuse the existing base-0 exports (`D:/ghidra-trial/sound-analysis-20261031/resume-20261031/exports/ASOUND117-correctmap/`
  and `ASOUNDLOG-correctmap/`) before importing again; the earlier wrongly mapped ASOUND export is not authoritative. Disassembly evidence never replaces lockstep, mutations or acceptance.

Tools and shells:
- `build.cmd` runs from PowerShell (through Bash it hangs; PowerShell `*>` writes UTF-16 logs).
- In Bash or a heredoc, a backslash in an edit (`\n`, a route's `\r`) can be eaten: write it from Python with
  `chr(92)`. Git Bash rewrites `/FI`-style arguments to paths: use PowerShell for `tasklist` filters.
- A background wrapper shell can report "completed" while its job still runs: wait on the result file.
- Stopping a task does not kill a Python `multiprocessing.Pool`'s children: kill the tree.
- `kit.py checkpoints` takes the route from `finish_all.py DIR:ROUTE`; a routine no `full_cycle` capture reaches fails with
  0 calls: find its route in `route-cover-20261020b` first. `integrate_many.py` needs `private-verdict.json`, which only
  `finish_all.py` writes.

Presentation:
- Never force `F117R_OBSERVE_PAGES=1` on live presentation. `drawfeed`/`rerun` changes need the full gate plus
  `presentation_check.py --mode rerun`; app-only changes use `present_light_check.py` with `--app-session`.

## Where things are

- Game install `D:/GOG/F-117A`: never mounted or written; copy per run that needs state.
- Phase 3 generated files (sound sweeps, rendered clips, speech cuts, memory dumps): `C:/Users/Tideg/f117-recomp-local/p3-session-29oct/`;
  MT-32 ROMs: `C:/Users/Tideg/f117-recomp-local/roms`; the sound page: https://claude.ai/artifact/1jdGUvk4aD8bsCSxDvJbta.
- Generated code `C:/Users/Tideg/f117-recomp-local/gen` (never committed); coverage and route outputs beside it.
- Ghidra 12.2 is already installed at `D:/ghidra-trial/ghidra/ghidra_12.2_PUBLIC`; reuse its existing projects (`proj/all_SETUP.gpr`, `proj/rng_SETUP.gpr`) and `out_ranges/` indexes for static routine discovery, exact-boundary leads and call-flow cross-checks. Before opening a candidate, resolve canonical/linear address aliases against existing matched starts and named coverage (VGAME `1132:0002` aliases already-matched `1058:0DA2`; `1132:0052` aliases `1058:0DF2`). Treat decompiler output as a lead only; verify original bytes, route coverage and semantics with the private lockstep/mutation/acceptance checks.
- Existing Ghidra export `out_ranges/SETUP/SETUP_0000_0CB2` decompiles D85/D98/DAE as exact 19/22/25-byte wrappers calling 0E97/0EC2/0F52; these sizes and call targets agree with the Capstone draft dossiers. Its index records all three completed decompilations. Caller exports show CB2→D85 (`f_CB2.c`), CE5→D98 (`f_CE5.c`), and D1B→DAE (`f_D1B.c`). The D1B→DAE evidence conflicts with `reference-reviewed.tsv`'s “no direct call site” wording for DAE, so preserve and review that discrepancy rather than silently changing the reviewed row. Ghidra's D1B prototype is unreliable here: it renders a one-argument call despite DAE's five-word stack contract, so verify raw caller pushes before making any documentation change.
- The private D85/D98/DAE fixture guards now force wrong-stack-slot mutants to distinct values (D85 `SS:SP+6=0800`, D98 `SS:SP+8=9000`, DAE `SS:SP+10=0180`); DAE displacement is now `0300–03FF`, disjoint from its `0200–02FF` count range. These are fixture edits only; the lockstep/mutation/acceptance gate has not yet run.
- The seven string-memory candidates at 1276, 1291, 12AD, 12F8, 130C, 1322 and 1340 are not in the existing Ghidra function indexes; no exact target body was exported. Caller context exists for 1276 (`out_ranges/SETUP/SETUP_0000_0562/f_5A3.c`: 200-byte local destination, two parameters), 1322 (`f_70A.c`: copies a local string then NUL-terminates), and 1340 (`f_5CE.c`: local destination, far source and count, then NUL-terminates). Treat that as argument/use context only. There is no target-specific exported caller for 1291, 12AD, 12F8 or 130C. Nearby 1244/125F bodies are separate routines, not substitutes for these candidates.
- The three wrapper config/mutation JSON files and `body.c` initially carried UTF-8 BOMs. `kit.py`'s plain `json.loads(Path.read_text())` rejects the JSON BOMs, and MSVC rejects the copied C BOMs. Only those leading BOM bytes have been removed from D85/D98/DAE inputs; all six JSON files parse strictly, and all three bodies now begin with `/*`. The subsequent clean `build --func-only` attempts were stopped during compilation; no lockstep, seed, mutation, or verdict evidence exists.
- The current Ghidra START range export only includes `86A8`; there is no `f_54BB.c`. Existing census/route evidence supports the 54BB routine and 54D8 arm, but Ghidra bounds/call-flow for this candidate are still unconfirmed. Fresh headless attempts, including `analyzeHeadless --help`, stalled without output and were stopped; an isolated JDK 25 override did not fix it. The current environment has `JAVA_HOME` set to JDK 8 even though JDK 25 is also on `PATH`; diagnose CLI startup or use the known-working GUI before relying on headless analysis.
- Proofs, gates and evidence: `D:/f117-gate/` (`p2-*` proofs, `p2-batch-*-accept` gates, `kit/`, `accept-kit/`,
  `kit-libs/`, `census.tsv`, `checklist-full-20261026/`, `bn-budget/`).
- Worktrees `D:/f117-wt` (`setup-main-0010` and `mission-generation-4239` carry private /Od overrides: never merge
  them wholesale). Ask the owner before deleting anything.

## Standing practice

- **No AI attribution** in commits or PRs: the user's global CLAUDE.md outranks any harness reminder.
- **Full gates:** the owner lifted the 1 November limit on 3 November ("remove the gate limitation. do gates at will"); run one when a change needs it. A gate compiles the main working tree, so keep it clean while one runs. Unfiltered `ctest` starts the long machine gates; use `ctest -R "host_|remaster|presentation"` for the quick set.
- **Commit and push together**, each verified step; commit titles start with the `progress.py --title` line.
  Stage files by name; never stage the owner's `4k60.bat` or `original-fixed.bat`.
- **Public repo, code only:** no game data, generated C, raw captures or private-project references
  ([docs/provenance.md](docs/provenance.md)).
- Long commands (over about 30 s) run in the background. While waiting, do a doc pass over every `.md`.
