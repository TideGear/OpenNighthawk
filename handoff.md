# Handoff

## Current state: 11 November 2026, 15:20 (read first)

This section is the only current one. Replace it; do not stack a new "read first" section above it. The previous one (9 and 10 November: R38, the 4K timestamps, the
owner gate, the simplified decal rule, the threaded setup) is archived unedited in [docs/archive/handoff-log-20261110.md](docs/archive/handoff-log-20261110.md).
Every owner report is a row in [docs/owner-issues.md](docs/owner-issues.md); renderer measurements are the last sections of [docs/gpu-compositor.md](docs/gpu-compositor.md).

**Top priority (owner, 10 Nov 22:35): "perfecting the margins".** The Remastered wide margins must draw everything the 4:3 area draws, 2D and 3D, at the true
place, with the original's look ("never cut corners"; "it's important we draw everything that should be drawn"). Standing rules from that evening, in memory:
read the matched code before guessing or explaining game behaviour; never hide a limitation; never spend time on what can safely be done faster (replays of a
late clock use `F117_SKIP_COMPOSE_BEFORE`, validated byte-exact on 12 frames).

**Margin dots, built and in the gates (11 Nov; `F117_WIDE_DOTS`, ON by default since the owner approved the look on 11 Nov 16:00, `F117_WIDE_DOTS=0` restores the squashed placement; pushed as `8443226`, default flipped after a full gate with it on: hash, guards 6/6, live 3/3, shots 2/2, host 12/12 alone, CTest 846/846).** `rerun.c`: per call to `B5DC` the game's own matrix, except a call that will draw a 3D model (aircraft or projectile where the reach says model: reach-divided matrix with the widened x row; flare model: widened x row only), so every dot, smoke puff, flare distance and flight-display marker keeps the original's placement, size and cut. `wide_dot_at_entry` predicts the point's true screen position at the entry of the aircraft loop's (`9BDF`) and projectile loop's (`9D89`) calls and, for a dot outside the 320 x 200 picture, adds a margin dot through `rerun_collector.overlay_dot` / `remaster_live_overlay_dot`: exactly one game pixel (a square polygon, not a dot primitive), colour 15 for aircraft, 29h / 13 for projectile slots 0-7 / 8-11, every view (outside views use the camera offset, `air_vector`) and the `[294B]` halving. Checked: with `F117_WIDE_DOTS_LOG=1` every dot call's prediction against the game's own `[4A10]`/`[4A18]`: 45,500 predicted, 0 differ (`F117_WIDE_DOTS_LOG=2` also logs every caller's true position). Hook slots: the table holds the live hooks plus two; the dots work from the one entry hook, and a warning prints if a render-copy hook does not fit (two return hooks once took the far scenery walk's slot and the city lights vanished from the margins: found by measuring the margins' bright pixels, fixed). Owner's flight at 12:58:48 (clock 18.207 billion): the two dots are aircraft at true x 391-392 (past the wide picture's right edge at 373: correctly not drawn) and x 33-35 (behind the left cockpit pillar, with its cyan ring from `AD26`). Not yet in the margins: that ring and the rest of the flight display pass, smoke puffs (blitter `0C95`, scaled run-length sprite, read), `DC46` icon sprites. New issues R43 (draw distance must cover everything: aircraft past 4,800, flares, puffs, far size for all; the Terrain dots default) and R44 (shortcuts found on review) in `docs/owner-issues.md`. Videos: `Desktop\ONShots\20261111-151751-margin-dots-before-after-17.8-18.5G.mp4`. The three symbology answers were given on 11 Nov (recommendations accepted): a plain (unlocked four-sided box, HUD pen) marker for ground and air targets in the margins, no lock indication; the bomb impact point pins to the wide picture's bottom. The game itself shows lock by shape (box searching, hexagon locked, `0B171`) and colour (HUD pen loose, 29h tight, 6 no use); a sketch is `Desktop\ONShots\20261111-153500-lock-markers-plain-vs-locked-from-code.png`. **Gate script from 11 Nov: `bash private/step6/gate.sh NAME`** (about 35-40 min instead of 75; same checks and criteria): stage 1 runs hash, CPU/GPU guards, live checks, shots and `func_lockstep` side by side (all deterministic), stage 2 runs the other 845 CTest tests then host acceptance alone; it writes `private/step6/NAME.txt` ending in per-criterion VERDICT lines and GATE-PASS or GATE-FAIL. Gates for this slice: `private/step6/final10.txt` then host acceptance alone (`host-alone2.txt`) then CTest (`ct-1111c.txt`); commit with explicit paths once all pass.

**Audit reading COMPLETE (11 Nov, `f6de5c7`); the build is next.** Final list of flat things the margins lack: far aircraft and projectile dots, smoke-puff sprites, the `DC46` icon sprites (shape ids below 100h), and the whole flight display pass (`9EFA`). The 3D side has no gap found (terrain walk all round, culls relaxed, range metric measured at 0 rejections). Confirmation runs (turned-view reference, 2D overlay comparison) are still to do. **Build started (11 Nov, `1021f84`):** `F117_WIDE_AIR_PROBE=1` (rerun.c, diagnostic, matrix left alone) hooks the return of the aircraft loop's `B5DC` call (`9BDF`) and recomputes the true x', y', z' and screen x `(x' * 256) / z' + 0A0h` from the unmodified matrix: 437 of 437 on-screen points match the game's `[4A10]`; in clocks 17.9 to 18.5 billion of the owner's flight 507 aircraft points in front of the eye were in the margins (347 at x 320..639, 120 at 640+, 40 below -160) and missing. Screen y (read in matched.c, not yet probed): `q = (y' * 256) / z'`, `q - (q >> 2)`, plus 34h ([294B] 0) or 3Ch with the cockpit flag [368C] set, else plus 64h. Next: the dot overlay: per call, the plain matrix for dots (true placement, game's own cull) and the widened one for models (the interim patch's logic), plus a `SCENE_DOTS` primitive (colour word FF0Fh for an aircraft, 29h/13 for projectiles; `shrink` 0 keeps the width) for each dot the game culled that lies in the wide picture outside the 4:3 window. Build notes learned while designing (`docs/wide-margin-audit.md`, last section): `B5DC` is a NEAR routine, and an exec hook that takes a call ends it with a FAR return (`pc.c`, `taken`), so replacing `B5DC` natively in the copy needs a way to end a near call (hook the callers' return addresses instead, or add a near-return form of the hook: `src/machine` is protected, so prefer the former); the original sets `[4A10]` = FFFFh and keeps the true x in `[952C]` for a point outside 0..13Fh; the picture layer's replay page is 320 wide (`drawlist.c`). Older text of this paragraph follows.  **The audit, in progress:** [docs/wide-margin-audit.md](docs/wide-margin-audit.md). Done: the 2D list from the code (far aircraft and projectile dots, the
smoke-trail puffs and other kind-400h sprites of the scene walk `DC46`, the kind-25h+ marker sprites, and every symbology section, each with its placement and
gate); 3D checklist item 1 (the object cull `E3BF`: the side and up allowances are relaxed in the copy, the range metric `(|x|+|y|)/4+|z|` against `[48E6]` is
NOT; measured 11 Nov with `F117_CULL_WHY=1` over the screenshot window: the metric rejected 0 of 578,234 visible objects, so it is not observed to bind). Next: 3D item 2 (the terrain walker `04E6`: its sector table and whether
the copy's extra walks cover the full wide angle), then items 3 to 10, and proof that no other routine draws flat into the view (the 84 graphics entries).
A runtime tally of 2D draw callers was tried and dropped: the stack top at record time is often inside a helper, so it cannot name the caller.

**Owner decisions pending (asked 10 Nov 23:54, see the audit's symbology table):** in the margins, (1) the ground-target lock marker (4a) and (2) the
air-target lock marker (7): recommended a plain marker with no lock indication, from the real game's state, because the real game offers no lock off its 320
screen; (3) the bomb impact point when it falls off the view (10): recommended pinning to the wide picture's bottom.

**R41 (the white dots that "climb and dive"): cause found.** `wide_air_exec` (`rerun.c`) divides the x row of the `B5DC` matrix by the width factor three times,
once per frame, so every point that routine projects has its x squashed 4 to 5 times towards the centre: far aircraft far to the side show near the crosshair
and swing with the roll; smoke puffs and the symbology are placed the same way. The screenshot moment is clock 18.207 billion of
`session-20261110-125342` (the owner's 12:58:48 shot, matched to 1.1 levels); the dots are absent in the original and in Remastered 4:3. An interim fix
(widen only for calls that draw a 3D model, with an exact prediction of `[DEBE]` checked against the game's own at `EF74`: 25,000 checks, 0 differ; 901 runway
checksums unchanged) puts the middle right but drops far aircraft from the margins, so it is NOT committed: it is saved with its diagnostics
(`F117_WIDE_AIR_CHECK`, `F117_DRAW_AUDIT`) as `private/wide-margin/r41-interim-fix-and-diagnostics.patch` (`git apply` it to reuse the depth predictor).

**The planned true fix (after the audit):** in the render copy, widen `B5DC` by exactly the wide factor (so every point inside the wide picture passes the
game's own tests and its true x is known), hook the game's anchor-taking drawing routines in the copy (the far dot, diamond `B0C2`, lock marker `B171`,
ring and label `B4F5`, sprites `1AE9`/`1B2E`, blast `1A98`, tracer lines) and draw each as a Remastered overlay at the true position, its shape implemented
from the matched code so the 4:3 middle stays pixel-identical; stop the game's own copy of each from drawing in the middle. Several days with verification.

**Paused: 4K 120 fps** (owner: margins first). State: master through `7eb1f70`, CI green on the last code push (`462afea`). The simplified stroke decal rule is ON
by default (owner-approved; `F117_DECAL_SIMPLE=0` restores exact; `guards.sh` pins exact with 0); the CPU setup's classify and pack loops run on a thread
pool (`F117_SETUP_THREADS`, byte-identical, `F117_SETUP_CHECK=1`). Measured at 4K Maximum: late flight worker frame 29.9 ms mean (was 41.1), runway whole
frame about 12.4 ms (p95 about 19); neither target met. Next there: thread binning and the decal face cache (exact), then Stage 2 for the runway's picture
step (owner chose "B then D", C dropped; then measured that neither flight is GPU-bound, so the hardware-rasterised compositor is deferred).

**Other open gates:** ONTests refresh with the data-folder rename build (its first launch moves the owner's saves to `%APPDATA%\OpenNighthawk`; ask first),
the owner's ONTests flight for R30 R31 R32 R36 R38 (R8 R11 R12 R14 R20 R30 R31 were confirmed fixed on 10 Nov), CRT choice, the worktree deletions in
`docs/worktree-audit-20261107.md`, any release. Planned P3 items added on 10 Nov: the Macintosh version's cockpit and other graphics, realistic lighting.
The owner's uncommitted `tools/sound_board_*` edits and `docs/p2-completion-plan.md` are untouched.

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
