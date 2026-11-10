# Handoff

## Current state: 9 November 2026, afternoon (read first)

This section is the only current one. Replace it; do not stack a new "read first" section above it. The dated handoff sections of 5 to 9 November are archived, unedited, in
[docs/archive/handoff-log-20261109.md](docs/archive/handoff-log-20261109.md). The renderer's measurements and the R38 record are in [docs/gpu-compositor.md](docs/gpu-compositor.md) (last sections);
every owner report is a row in [docs/owner-issues.md](docs/owner-issues.md).

**The goal in force** (owner's `/goal`, 9 November; he keeps the text in chat): 4K 120 fps in Remastered without changing game logic, pictures byte-exact except owner-approved visual changes. Work order:

1. Finish R38 (below).
2. Clean 4K hardware GPU timestamps on the runway replay and the owner's late flight. If the GPU passes alone exceed 16.7 or 8.3 ms, do step 7 before steps 3 to 6 and report the gap.
3. Overlap's true unpaced 4K cost (the worker-checker repair is done; keep its evidence).
4. The emulated machine on its own thread, handing state over at today's points.
5. Pipeline machine, copy, composite and GPU frames; measure the added latency.
6. Parallel copy machines; cut the picture-layer cost (changed-content redraw or the GPU).
7. Cheaper exact bands and strokes, guided by the timestamps.
8. If exact rendering cannot reach the budget: STOP at an owner gate with measured alternatives (hardware rasterisation with bounded edge differences, or exact pixel-shader testing). Stage 2 needs approval.

Pass lines: two runway replays each at 4K Maximum, Model detail Full; 60 fps is zero frames over 16.7 ms, 120 fps zero over 8.3 ms; report Near and Far too. **Neither is met.** Line thickness and the real
terrain and sea dots are separate from R38: show evidence-based options, bundle nothing.

**Where the code is.** `master`, pushed through `c4d7131` (CI green on `2c9c01e`). `build/f117a.exe` is **9 November 13:19:19**. `Desktop\ONTests` holds the accepted **11:42:20** build; the accepted guard
baseline (`private/renderer-bench/baseline`) is unchanged. Accepted defaults: exact rasterising, GPU composition and presentation, overlap mode 2, Model detail Full, R37 square pen, R36 smooth sea, R35
Perspective far light size, Near terrain dots. Optional GPU hardware timestamps are in (`F117_GPU_TIMESTAMPS` configure option, default OFF; runtime needs `F117_GPU_TIMESTAMP_LOG`).

**R38 (teeth on far slope lines): ON by default since build 9 Nov 21:10:17 (`docs/gpu-compositor.md`, last section).** `F117_DECAL_FACE=0` turns it off (exact mode only; `F117_RASTER_EXACT=0` keeps the float path). Full set passed on that build: hash, CPU and GPU guards against the new baseline, switch-off guards, live checks, host acceptance 12/12 (after the hangar fix), full CTest 846/846; baseline promoted (previous kept in `private/renderer-bench/baseline-r38-prior`); ONTests refreshed. The owner rejected the
first candidate (never ask to approve it again) and said "much better" to the revised one, which was taken as approval to integrate and test. Rule and code: `docs/gpu-compositor.md`, R38 section. Scripts and
logs: `private/r38-validate` (`off.sh`, `on.sh`, `stress.sh`, `shots.sh`, `perf.sh`). **Never rerun `private/r38-same-pixel/integrate.py`** (a one-time source-edit script).

Passed on the 13:19:19 build:

- Feature off: machine hash `600306c77cb04481` (old and new, Near and Maximum); CPU and GPU guards 901/901 at Near, Far and Maximum against the accepted baseline; live GPU check 0 differing pixels in 3,421
  pictures at each distance.
- Feature on, against the candidate baseline in `private/r38-validate/candidate-baseline` (894 of 901 checksums differ from the accepted one at each distance): CPU repeat, GPU, one thread, 64-row tiles, and
  16-row tiles with five threads, each 901/901 at all three distances; live GPU check 0 differing pixels at all three; machine hash unchanged.
- Three concurrent stress replays (8 threads; 7 threads with 32-row tiles; 3 threads with 16-row tiles): each 901/901 at all three distances.
- Unit cases in `tests/test_remaster_raster.c` (`c4d7131`): cache dedupe, coverage lookup, allocation failure, a caller-owned context, the float path. Run standalone only, not yet in a full CTest.
- Own inspection of off/on app shots at 3840 x 1800 along the owner's flights `session-20261107-223638` and `session-20261108-211031` (11 pairs; for each, the 400 x 120 region with the most changed pixels, crops in `private/r38-validate/review`; 515 to 1,520 pixels change per shot): the teeth are gone
  on every slope edge that had them; no spreading onto neighbouring faces, no lost lights or structures, no new marks. With the gaps filled, those edges now read as solid dark bars one game pixel wide, which
  makes the separate line-thickness issue easier to see. Faceted model shapes are unchanged. Regions with fewer changes were not looked at one by one.

Still to do for R38, in order: compare the GPU-path 4K shots with the CPU ones; the paired unpaced 4K cost (`perf.sh`, nothing else running; the cache is cleared per clear call and the GPU uploads the face
table every frame, so fix that first if the cost is measurable); enable by default; on that build run the required set once (hash, CPU and GPU guards, live checks, host acceptance 12/12, full CTest with
`func_lockstep`); promote the candidate baseline (keep the old one); refresh ONTests; push; CI. On a GPU-path cache failure `gpu_compose_begin` returns 0 and the frame falls back to the CPU compositor (read
from the code, not tested in the app).

**Measured so far** (details in `docs/gpu-compositor.md`): runway replay, 4K Maximum Full, overlap mode 2: mean 16.72 ms, p95 17.9, about half the frames over 16.7 ms (7 November, probably paced by vsync:
measure unpaced). The owner's late flight `session-20261108-211031`, unpaced 4K Maximum Full: mean 163.69 ms, p95 234.13, worsening from 167 ms at clock 7 billion to 227 ms at 10 billion as the scene grows from
about 35,700 to 62,000 primitives; the slowdown predates the 8 November defaults. A split diagnostic there (with fence overhead, not hardware timestamps) put bands at 19 to 28 ms and stroke pixels at 7 to 17 ms.

**Required checks and rules** (every renderer step):

- Machine hash `600306c77cb04481`: `guards.sh statehash` with `OLD=D:/f117-dl/old-ontests/f117a-20261104-2207.exe`.
- CPU and GPU guards 901/901 at Near, Far and Maximum with `F117_FAR_WIDE_ACROSS=0 F117_FAR_WIDE_UP=100 F117_WIDE_UP_ALLOW=0`; live GPU checks with zero differing pixels; host acceptance 12/12; full CTest
  before pushing a completed step; CI green. Interim builds need only guards and cost; the full set runs once on the build that becomes the default. Partial results are never passes.
- Pass `EXE=` on every guard, bench and hash run and report the executable's build time (from a worktree the scripts otherwise run the main checkout's exe). One timed run at a time, paired A/B; concurrent
  runs are not performance evidence. Benchmark with `F117_NO_VSYNC=1`.
- No stray `f117a.exe` before or after a run, no visible windows, no sound, private saves. Never write to the game install or the owner's folders except an authorised ONTests refresh: copy the exe, every DXIL
  and each changed DLL, then wait explicitly for a hidden private-save launch to exit 0 (`Start-Process -Wait`; a plain GUI launch returns no exit code).
- Replays of the owner's flights recorded before 7 November 18:06 need `--adlib-effects` when his ini had Roland, or they drift and crash (R28); later logs carry the setting.
- Stop before touching `src/matched`, `src/machine`, `tests/func_lockstep.c` or core CMake. Commit and push each verified step with explicit paths; no AI attribution; nothing from kylofon's projects.
- Preserve the owner's uncommitted `tools/sound_board_page.py`, `tools/sound_board_template.html` and untracked `docs/p2-completion-plan.md`.

**Open owner gates:** his flight of ONTests (R30, R31, R32, R36 and the 8 November defaults); the CRT look (`Desktop\ONShots\r25_crt_options.png`, R25); the deletions in
[docs/worktree-audit-20261107.md](docs/worktree-audit-20261107.md); Stage 2; any release (the first, `v2026.11.07`, is published; ask before another). Still owed from the first release: a second PC and
his own fresh-download run.

**Traps met in this stretch** (the general list is under Traps below): a game replay holds `build/f117a.exe` and `SDL3.dll`, so a relink fails while one runs; stopping a task leaves its bash and game
children running, so kill them and confirm; screenshot files are named by wall time, so pair off/on shots by order, not name; a 4K software capture takes minutes per shot; C: had about 12 GB free on 8 November.

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
[docs/archive/p2-brief-ci-failure.md](docs/archive/p2-brief-ci-failure.md)). ASOUND.117 `09F2` stays excluded pending the D2-fix decision; ISOUND.LOG `0581` needs an exact invalid-range certificate. The all-code gap is 17,361 bytes;
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

**Documentation (read in full on 5 November).** All 37 markdown files were read and corrected against the code and `tools/progress.py`; dated logs were moved to [docs/archive/](docs/archive/) rather than deleted. Two figures are still unreconciled and are flagged in place: the static-review gap (10,301 bytes in the 21 October analysis against 11,601 reviewed static bytes now, [docs/static-code-review.md](docs/static-code-review.md)) and the checklist denominator (47,736 in the round-10 text against 47,952 now, [docs/roadmap.md](docs/roadmap.md)). The owner's untracked `docs/p2-completion-plan.md` (1 and 3 November) is the Phase 2 plan: it agrees with the current figures (its ledger: 17,356 distinct uncredited bytes, of them reviewed static code 11,601, never-run 4,267, route-executed 1,261, other 227) and schedules the two reconciliations above as ledger work (section 2). Its "no full machine gate without the owner's authorization" constraint dates from 3 November before the owner lifted the limit that evening; check with the owner before treating it as current. When a number in a doc and `py tools/progress.py` disagree, the tool is right: fix every copy.

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

### Owner revised rendering goal (8 November, current)

Game logic and the complete selected object set remain exact; improved visuals may use hardware rasterisation, depth buffers and approximations. Owner reviews every visible change before enabling it. Adding shader build rules to CMake is approved; other core CMake edits and protected machine/matched/lockstep files stop for owner review.

After defaults: new-flight paired performance, unpaced overlap/check repair, GPU pass timestamps; then hardware gradient bands preserving R36 behind a switch (timing and before/after owner gate); then hardware stroke quads with depth/bias and close-line options behind a switch (owner gate). Follow with machine thread, pipeline/latency, parallel copies, picture caching/GPU. Stage 2 requires approval if still over budget. Keep current exact GPU checks for speed-only changes; only replace CPU-equals-GPU with measured tolerance after hardware rendering approval. No objects removed for speed.
