# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left, the batch table),
[docs/progress.md](docs/progress.md) (what the scores measure),
[docs/architecture.md](docs/architecture.md) (how parity is built and checked),
[docs/repeated-processes.md](docs/repeated-processes.md) (every repeated process, its command and cost),
[docs/bugs.md](docs/bugs.md) (the original's bugs and our fixes) and
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K).
This file holds only the current state, how to resume, open decisions and traps; history is in `git log`
and in the roadmap's batch table. Keep it that way: replace, do not append.

## Latest: Remastered throughput (2 November 2026)

**Latest owner direction:** the owner confirmed the coastline issue also happens in GOG and wants Remastered to fix it. An independent GOG reproduction is no longer a prerequisite for that diagnosis. The desired shoreline should align with the game's water collision boundary if one exists; do not invent one or alter simulation behavior. All twelve latest screenshots are the previously identified session 154150 sequence. Current evidence and investigation: [docs/coastline-reconstruction.md](docs/coastline-reconstruction.md). The moving shoreline itself remains unfixed; only the separate wide-margin cutoff is fixed. This supersedes the older pending-GOG-comparison wording below.

Continuing the active Phase 3 goal, large Remastered rasters now use eight workers on machines with at least 16 logical CPUs, sharing 64-row groups instead of leaving the busy horizon in one worker band. Smaller rasters retain four workers. Two repeated runs per configuration at scale 9 in a 3840x2160 software-renderer window measured 13.36/13.13 fps with four workers versus 18.00/17.83 with eight balanced workers; compositor cost fell from about 39 to 22 ms. This is still below 60 fps and is not physical-display pacing evidence. The prior unused 4:3 upload removal remains. All 167 moving scale-9 pictures across airborne, hangar, water and 4:3 views match the single-worker render pixel for pixel. Shared timer accumulators were replaced with per-worker counters, combined after stage barriers; diagnostics are off in normal play. Details: [docs/remaster-performance.md](docs/remaster-performance.md), evidence `D:/f117-gate/remaster-workers-20261102/`.

The rebuilt candidate SHA-256 is `012D6A61D1FEF5D43F63CE31C3D327F65A760AE82DE8332C811651A389591BD5`. All ten host acceptance cases passed in 125.8 seconds at two jobs; the bound report verifies at `D:/f117-gate/host-acceptance-coast-scope-20261102/report.json`. The new mandatory worker case compares 42 moving wide and 42 moving 4:3 frames against one worker, with zero differing pixels. Thirteen checker tests, seven presentation-check tests and eleven focused native CTests pass. Older nine-case reports are historical and cannot verify against the expanded checker/manifest. This handoff accompanies the verified Phase 3 code/test commit. No full machine gate or new deployment was performed; ONTests retains the earlier installed coastline/audio build. Commit 1560096 installs the scored-title guard; 16 earlier unscored titles were audited and left unchanged. Phase 3 remains active and incomplete: improve measured renderer throughput, complete scenery reach/coastline stability and broader motion coverage, then the other feature requirements and owner's final checks; do not shrink the completion scope to these local tests. The engine cutoff and false brown water-margin strip are fixed in ONTests, awaiting owner confirmation; the moving shoreline is an owner-confirmed original/GOG issue that Remastered must correct.

## Host acceptance implemented (2 November 2026)

The owner authorized prevention work after the hybrid engine-sound regression escaped the earlier gates. The new [host integration suite](docs/host-acceptance.md) supplements existing machine parity; neither its inputs nor its requirements were weakened. Windows CI runs the ROM-free verdict tests, and `present_light_check.py` now requires the recorded-flight suite for host/presentation source changes. The suite checks actual sound forwarding, OPL pitch and rendered/mixed PCM; moving hangar/aircraft/coast scenes; and real screenshot/menu event dispatch. Reports bind the executable, checker, manifest and recording hashes; missing evidence and partial suites cannot certify deployment.

The final healthy run passed all nine cases in 116 seconds at two jobs: `D:/f117-gate/host-acceptance-final-20261102/report.json` and `index.html`. Tested build SHA-256: `813a99687f232a5182a6c164c19cd138cff466d30fa0f234c991fe23f1dfd7ea`. Twelve checker tests, seven presentation-check tests and eleven focused native CTests passed. Two actual faulty builds (dropped speed argument; removed continuous-speed routing) both failed the sound acceptance, with diagnostics still enabled; evidence is `D:/f117-gate/host-acceptance-faults-20261102/`. Source was restored and the healthy build retested afterward. Observer-on/off replay RAM hashes match. These results are host integration evidence, not a new full machine gate or a GOG fidelity verdict.

No full machine gate was launched, and no commit, push or ONTests replacement was performed for this prevention work. ONTests retains the previously installed coastline/audio fixes at SHA-256 `507D03C37566D8E64852625C5BC5814360C57B82C4A427258F5CDA38155F1516`. Owner listening/visual acceptance and the GOG hangar/coast comparison remain pending; broader renderer work is still open. The feedback-only restriction quoted below was superseded by the owner's later continuation and prevention requests.

## Earlier owner feedback (2 November 2026, after renderer commit d748bdc)

The owner reports that the latest Remastered build looks much better. This update supersedes older renderer/culling notes below. Detailed implementation, installed build hash and prior validation are in [docs/p3-handoff.md](docs/p3-handoff.md), under "Start here: 2 November 2026 stopping point". The latest feedback is not confirmation that every individual renderer issue is resolved.

- **Priority: coastline motion and its relationship to buildings.** In the latest flight the backs of the hangars appear to stand in water; as the player moves forward, the coastline moves underneath them and farther away. Inspect the changing coastline relative to fixed hangar geometry, rather than treating this only as ordinary pixel jitter. Once the player enters the water, land/coastline remains at the bottom of the widescreen margins until takeoff. The owner thinks crashes were disabled; that setting is not yet verified. Both behaviors remain OPEN, with no established cause or fix.
- **Screenshot evidence:** `C:/Users/Tideg/AppData/Roaming/F117Recomp/F-117A/screenshots/session-20261102-154150-clock*.bmp` (12 captures, clocks 12391636686 through 16896927060). Representative captures were inspected: 12391636686 and 13296691572 show blue water against/under the hangar backs; 14733785897 shows the shoreline farther down the view as the hangars pass to the right; 16283883318 shows water filling the forward view while brown land remains low in the side margins; 16896927060 shows the later airborne view. These stills support the reported spatial inconsistency; they do not establish its rendering cause. Use the corresponding `sessions/session-20261102-154150/input.log` and a private save-start copy for a moving comparison with Original and Remastered. Retain `--adlib-effects` when reproducing the Roland launcher's routing. Compare the center and the wide extensions, terrain/sea geometry and composition at the same recorded clocks before proposing a fix.
- **Engine noise at zero thrust: OPEN sound/parity question.** The owner reports that engine noise in the recomp stops after a moment at 0 thrust, whereas it continues in the GOG release. They ask why. Cause is not investigated or established; do not describe the cutoff as intended behavior or claim a fix. A future investigation should compare sustained zero-thrust behavior with GOG, matching sound driver/routing and relevant settings, then distinguish the game's engine-sound requests from host playback/loop lifetime. Screenshots alone cannot verify this audio difference.
- **Aircraft culling baseline corrected by the owner:** parked aircraft disappearing too early when very close and near the screen edge also happens in the original GOG release. It is therefore not a widescreen-only recomp regression. Keep the original behavior and any desired Remastered visibility improvement distinct when assessing this issue. General object-size-aware visibility may still be an enhancement, but do not use this observation as proof that our widescreen renderer introduced the culling defect.

The owner explicitly limited this turn to reviewing screenshots and updating `handoff.md`, with very little five-hour ChatGPT usage remaining. No implementation, replay, build, test, deployment, commit or push was requested for this feedback-only update. Continue investigation only when requested in the next conversation.

## Follow-up work resumed here (2 November 2026)

The owner's later "Continue work here" supersedes the feedback-only restriction above. See the final "2 Nov follow-up: coastline and zero-thrust sound" section in [docs/p3-handoff.md](docs/p3-handoff.md) for current evidence and limits. The margin's horizontal water cutoff has a tested ground near-clipping fix; genuine sloping coast is retained. Hangar/coast alignment was also reproduced in our Original presentation, but a GOG comparison is still needed. ONTests now has the fixes (SHA-256 `507D03C37566D8E64852625C5BC5814360C57B82C4A427258F5CDA38155F1516`); ten focused tests and the real-driver probe pass, with zero differences in at least 481 four-thread comparison pictures. The owner confirmed GOG uses AdLib/Sound Blaster. The hybrid engine repair now includes both the missing speed argument and interception of continuous speed calls at the bound driver entry (they bypass the old library-slot hook). Actual replay preserves nonzero speed updates while coasting at zero throttle; ordinary minus presses do not themselves issue engine-off command 25. The fix is installed and awaits the owner's listening check. Before/after guest RAM is identical. The aircraft edge-culling observation remains inherited GOG behavior, not a newly introduced widescreen defect.

## Current state (1 November 2026)

- **P2 procedure update:** use the revised [proof work order](docs/repeated-processes.md#order-work-to-avoid-repeated-proof-cycles-1-november-2026): review proof inputs before compiling, smoke-test at 64 states across five seeds, retain every full proof and acceptance check, time-box stalled diagnosis, bundle independent ready candidates, and check for stale named-coverage registry entries before the expensive gate. Preserve accepted coverage and validate refreshes as separate hashed addenda. No proof workers during the gate.

- **Active branch:** `master` remains the main branch. Batch `bz` passed acceptance and its code/docs are integrated from a clean worktree based on fetched `origin/master` at `b40dbba`; the shared checkout's P3 edits remain separate. The Phase 3 branches are merged (2adbdad); see "Resume here" before continuing that work.
- **Scoreboard:** P1 100.00%, P2 96.93%, P3 93.84%, All 97.51% (`py tools/progress.py --title`). Refreshed with census `D:/f117-gate/census.tsv`, named cover `D:/f117-gate/p2-batch-bz-accept/named-cover`, and route cover `D:/f117-gate/route-cover-20261020b`. Gameplay is 99.23% (160,015 named bytes plus 2,631 reviewed design-rule bytes of 163,907). The named-code all-code measure is 191,937 / 209,298 bytes (91.71%). Reviewed references cover 47,734 / 47,952 outside-route bytes. There are 1,261 route-executed unmatched bytes: ASOUND.117 166, ASOUND.LOG 135, ISOUND.117 28, ISOUND.LOG 158, VGAME 324, PLAYER 156, RSOUND.117 5, RSOUND.LOG 83, START 66, SETUP 57, END 63, MPS_LOGO 18, DSWAP 2.
- **Latest accepted batch:** `bz` bundles ASOUND.117 `01B6:0A48`, `0A5B`, `0B01`, and the ASOUND.LOG `0000:008A` timer-uninstall prefix. It contributes 244 new route-executed named bytes, 118 fully covered instructions, and 88 private mutations caught, with zero claimed-but-uncredited bytes. Evidence is `D:/f117-gate/p2-batch-bz-accept/`; prior batch `by` remains documented below.
- **Gate:** the `bz` final verdict passed: 831 tests, 63 routes, 62 fresh interpreter sessions; 1,646 routines checked at three seeds; both timing profiles, the whole-table checks, 20 optimized seeds, and sequential fresh-baseline audit passed. Route parity covered 90,168 instruction starts / 5,764,032 states with zero mismatches. All 63 N=1 presentation results and boot N=4/9 were exact. The clean mainline integration build and all five targeted tests also passed. Core gate 1,555.53 s; full acceptance 2,503.42 s. The finalizer recorded 244 added named bytes and zero claimed-but-uncredited bytes. Its separate refresh addendum covers `VGAME.EXE 0FB2:04DD`, registered after the accepted `by` base; `by`'s original coverage was left byte-for-byte unchanged. Evidence and logs: `D:/f117-gate/p2-batch-bz-accept/` and `D:/f117-gate/p2-accept-master-2e4287e/acceptance-logs/`.
- **Saved owner docs:** `stash@{0}` (`preserve owner docs before P3 acceptance merge`) was applied; its round-10 PLAYER reference and decision are included in the documentation commit. The stash remains as a backup and has not been dropped.
- **Still-local owner work:** `tools/sound_board_page.py` and `tools/sound_board_template.html` have unrelated edits. Preserve these and the untracked `.ppm`, `100`, `4k60.bat`, `original-fixed.bat`, and `x.slice(0` files; they are excluded from the P2 acceptance commit.
- **P2 gaps and decisions:** the route-executed unmatched total is 1,261 bytes (module breakdown above). ASOUND.117 `09F2` remains excluded pending the D2-fix interaction decision; ISOUND.LOG `0581` needs an exact invalid-range certificate that does not exclude real timer-handler bytes; the 29-Oct protected note is stale on five units. The current-image Q6 audit is under `D:/f117-gate/checklist-full-20261026/round9-q6-cert-20261030/`; review its entry/dead-certificate closure before changing status. PLAYER `00D5F-00DAF` remains matching work as `needs-route` until a route selects the grayscale arm. Round-10 evidence is under `D:/f117-gate/checklist-full-20261026/round10-q3-needsroute-20261030/`.
- **Remaining naming and closure work:** the all-code gap is 17,361 bytes, including the 1,261 route-executed bytes above; additional static-code naming and closure work remains. Reconcile `docs/static-code-review.md`'s 10,301-byte report with the current census before calling that review complete. The broad indirect-call/dispatch/callback/vector/overlay/self-modifying-code closure still needs a final review. The historical probable-dead tally (12 rows / 831 bytes) is stale: ten rows / 477 bytes were upgraded to dead in the 21-Oct-2026 review, while current reference rows show no probable-dead entries; reconcile the residual 354 bytes against current named coverage.
- **Phase 3:** use [docs/p3-handoff.md](docs/p3-handoff.md) for remaining features. P3 is 93.84%; the settings, controls, Remastered rendering, replay, hybrid-sound and accessibility work remains at the percentages in the roadmap.

## Resume here

**P2 status (1 November 2026, evening Pacific):** accepted batch `bz` remains P2 96.93% with 1,261 route-executed unmatched bytes. The candidate integration is based on `fed0186` (`28728bf` is the tested full-gate base); `origin/master` matched `fed0186` before this handoff commit. The unaccepted `ca` candidate adds no P2 credit yet.
The grouped ca batch now retains 11 private-ready entries: four sound tails (80 route bytes) and seven SETUP wrappers (207 static bytes), totaling 287 body bytes. The 34 VGAME loader-patched thunks were parked on 2 November because 29 overlap graphics observers; they remain unaccepted and earn no credit. The remaining-entry private audit passed for 135 instructions and 256 mutations, and the parity-only route check passed all 62 groups (41 cached and 21 fresh interpreter runs) with no event overruns. The 276-byte ISOUND.117 019C:026C result belongs to accepted batch by, so it is not counted again here.
The remaining sound/SETUP candidate source and CTest registrations are integrated but unaccepted; the combined func_lockstep target has not been freshly compiled and the grouped ca gate has not run. The VGAME thunk family and its candidate test were removed to preserve observer capture. The P2 session's isolated harness extraction and revised acceptance packet are separate preparatory work and remain unbuilt/unaccepted. No candidate credit is claimed. Do not run private proof builds during another session's full gate. P3's separate steps below remain intact. Retain the owner's sound-board edits and five untracked files listed above.


Two threads. `master` is the active main branch. P2 batch `bz` is accepted at 96.93%; P3 is 93.03% after the latest progress commits.

**Phase 3 (merged into `master` on 31 October 2026, at `2adbdad`).** `master` now contains the in-app Remastered work (branch `p3-remaster-live`), hybrid sound (`p3-hybrid-sound`) and the docs pass; the branches stay as history. Read [docs/remastered-design.md](docs/remastered-design.md) and [docs/p3-handoff.md](docs/p3-handoff.md).
- **What is in:** `--presentation remastered` (`src/host/main.c` and a settings-menu row): the Enhanced picture with the 3-D view redrawn by `src/present/remaster_live.c` from the model requests, composited by origin tag; the machine-loop exec-hook table (`machine_t.exec_hook`, `src/machine/pc.c` and `machine.h`: one comparison per instruction when off); hybrid sound (`src/host/macsound.c`, `--mac-sounds DIR`, `--music-source`, `--sound-source`, `tests/test_macsound.c`).
- **Acceptance of the merge:** gate `D:/f117-gate/p3-merge-gate2-20261031/` (827 tests; 63 routes identical with 62 fresh interpreter sessions; 5,764,032 translated-instruction states under both timing profiles, 0 mismatching; 1,641 matched routines at three seeds, about 6.87 million states each, 0 mismatching; reachability and event limits clean) and the presentation checks beside it (`present-n1.log`: N=1 exact on 63 routes; `present-boot.log`: boot_to_flight N=4 and N=9 exact). The first gate attempt (`gate-attempt1-hung.log`) hung on a latent lockstep defect (below), not on the merge.
- **Score split (owner decision, 31 Oct 2026):** the Remastered item (weight 10) and the hybrid sound item (weight 3) were split into sub-items in `docs/progress.json`, each credited only for what is built, run and (where it applies) gated: Remastered 5.5 of 10, hybrid sound 2.11 of 3, P3 85.42% to 91.23%. The sub-items for the owner's own look (Remastered 1.0, hybrid sound 0.3), longer draw distance, road and coastline jitter and interpolation stay at 0.
- **The headless tool I built is retired** (`remaster_view`, `f117run --remaster-shot`, `tools/remaster_wide_check.py`): code at git tag `remaster-view-tool-20261031`, findings in the design doc ("Findings from the headless shot tool"). Ported into `remaster_scene.c`: a fallback for edge sets that do not chain (angular order) and a three-vertex rule. Not in the live code: skipping requests made while the viewport (`DS:85FA..8600`) is under 300 wide (the cockpit's map display), and the frame boundary from the flip bit `DS:0346`.
- **Next, in order:** (1) run the merged in-app path in a real flight against the Enhanced baseline (**first run done 31 Oct 2026**: the scripted `boot_to_flight` route recorded with `f117run --record` (inputs in `D:/f117-gate/remaster-flight-20261031/rec/`, start save `save-start`), replayed in the app with `--fixes original --presentation enhanced|remastered --present-scale 3`, frames at clock 2.45 billion. Result: no region fails `remaster_diff.py` (largest 108 fine pixels); 2,931 of 576,000 pixels differ (all inside the 3D view; the HUD and cockpit are identical); the sky column has 74 distinct colours against Enhanced's 10 bands (smooth); the sea strip is redrawn. Not yet judged: hangar detail, depth ordering in motion, a flight with real flying inputs. Shots are in `Desktop/ONShots`; the recorded sessions in `%APPDATA%/F117Recomp/F-117A/sessions` are all front-end browsing, not flights, so a takeoff must be recorded this way), original recipe (takeoff replay, `--presentation remastered --present-scale 3`, `F117_PRESENT_SHOTS`, `py tools/remaster_diff.py ENHANCED.ppm REMASTERED.ppm`); (2) the live branch's open list: face shade `[7D8A]` (**closed 31 Oct: the original never reads it; see docs/remastered-design.md**), validator mismatches (models 3, 30, 51, 50: **explained 31 Oct, a stale-record artifact of the game's lazy vertex projection, not a placement error**; docs/remastered-design.md), interpolation between logic steps, widescreen, longer reach (R6); (3) the fit rule (**built 31 Oct: widescreen Remastered, `remaster_live_wide`; the cockpit and HUD block keeps its size in the middle, the world fills the window, crosshair centred; checked at 1920 x 1080, docs/remastered-design.md; the mouse maps through the wide view (worked out and built, not tried with a real mouse); still to do: objects outside the game's own field of view**); (4) hybrid sound (the owner's stated order, 31 Oct: finish it right after the remastered renderer, before anything else in Phase 3; the `p3-hybrid-sound` branch is now recorded as merged (`bd58bc2`, no content change): **done 31 Oct 2026, gated at `59cf216` (827 tests, 63 routes, 1,642 routines at three seeds, 0 mismatching): the AdLib sidecar (`src/host/sidecar.c`, `--sound roland --adlib-effects`: flight effects ids 4-20 and 23-25 on a second AdLib driver with its own chip, Roland keeps the music and everything the front end polls) and the START and END thunk hooks (`docs/sound-board.md`)**. What is left: listening to it (every run used the dummy audio device), an ini key and a menu row for `--adlib-effects` (and the owner's call on making it the default with Roland), Mac recordings for START's and END's ids (none maps today), sidecars for RSOUND/ISOUND if wanted, a generated-code build check of the exec hook, the owner's marks on the sound-board page, the id mapping heard by ear; (5) the owner's credit decision.
- **Owner instructions to keep:** renderer shots go to `Desktop\ONShots` named `YYYYMMDD-HHMMSS-name.png` (leave the `Prenaming` folder alone); gradients smooth with no visible bands; depth-correct drawing is the point; read `src/matched/matched.c` before guessing and say what was read.
- **Build and run:** `.\build.cmd` from PowerShell; headless runs with `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`; a Python heredoc in Bash turns `\n` in C strings into real newlines, so use the Edit tool for such lines. Research switches on the live path: `F117_REMASTER_FRAME=N:FILE.ppm`, `F117_REMASTER_DEBUG=1`, `F117_REMASTER_VALIDATE`.

**Phase 2.** Batches `by` and `bz` are accepted on `master`. The latest batch `bz` adds four sound routines totaling 244 newly route-executed named bytes (all four bodies are bundled because no other candidate was private-ready), 118 fully covered instructions and 88 caught mutants. P2 is 96.93% with 1,261 route-executed bytes remaining. The full acceptance packet is `D:/f117-gate/p2-batch-bz-accept/`; the VGAME baseline refresh is recorded as a separate addendum without altering `by` coverage. Ghidra exports reused for the batch are in `D:/ghidra-trial/sound-analysis-20261031/resume-20261031/exports/ASOUND117-correctmap/` and `ASOUNDLOG-correctmap/`. Follow [docs/repeated-processes.md](docs/repeated-processes.md#phase-2-batch-acceptance) for the next batch, and never run private proof builds during a full gate.

- **Normal batch target:** aim for 1,000-1,500 newly proved route bytes. Count only exact new route-executed credit; bundle independently proven small routines when they pass the same lockstep, mutation, audit and gate standards. Do not pad batches with raw census extents or already-covered code.
- **Accepted ISOUND.117 `019C:026C`:** 336 bytes, 111/111 instructions, five lockstep seeds and all 74 mutants passed. It passed whole-table and route prechecks and the full acceptance as part of `by`; credit audit attributes 276 bytes as newly route-executed, with the rest overlapping existing named credit. Evidence is under `D:/f117-gate/p2-batch-by-accept/`.
- **ASOUND.117 `0B01`:** the first proof attempt had a fixture nontermination, later traced to using randomized `seg[S_CS]`; the repaired fixture captures `cs` explicitly and the 86-byte routine passed all five seeds, coverage, mutations and `bz` acceptance. Preserve the earlier failed evidence as history; it is superseded by the accepted proof in `D:/f117-gate/p2-asound-117-0b01-refresh-9fce9a0/`.
- **Other sound candidates:** ASOUND.117 `0A5B` is accepted as the 105-byte prefix ending at the PIT wake `0AC4`; the continuation remains original code. ASOUND.LOG `008A` is accepted only as `[008A,00AC)`, handing back immediately after the PIT control-word OUT. ASOUND.LOG `03F8-042F` still earns zero new bytes because its instructions are already covered by accepted `0344`, `0423`, and `042C` scopes.
- **Ghidra:** Ghidra 12.2 assisted the accepted SETUP `0000:0CB2` wrapper and the sound batch used the corrected base-0 ASOUND.117 / ASOUND.LOG exports to review instruction boundaries, wake points and the LOG caller. Keep the exports at `D:/ghidra-trial/sound-analysis-20261031/resume-20261031/exports/`; decompilation remains a lead, not proof.
- **SETUP pilot:** `0000:0CB2` is accepted. SETUP `0CE5` and `0D1B` have complete private proofs and are integrated with the unaccepted `ca` group; they receive no credit until grouped acceptance. The unrelated local owner edits in `tools/sound_board_page.py`, `tools/sound_board_template.html`, and untracked `.ppm`, `100`, `4k60.bat`, `original-fixed.bat`, and `x.slice(0` files are intentionally excluded from the P2 commit.
**The owner's reported issues are tracked one by one in [docs/owner-issues.md](docs/owner-issues.md) (status and evidence for each; do not drop an item until the owner agrees). Read it before anything below.**

## Open work list (31 October 2026, late evening; the owner's priority is the Remastered renderer first, then hybrid sound)

**P2 runs in a separate session at the same time**: this Phase 3 session touches only `src/host`, `src/present`, `docs`, P3 tools; never `src/matched`, `src/machine`, `tests/func_lockstep.c` or core CMake; `git fetch` before every push; check for a running gate before a heavy job; build in worktrees under `C:/Users/Tideg/f117-recomp-local`; stop only processes you started (by PID or path), never `Stop-Process -Name f117a` (the owner may be testing).

*Owner's live test, 31 Oct (Desktop `ONTests\ON-roland.bat`, `ON-mac.bat`; a copy of a build plus `f117a.ini` with the MT-32 ROM path; the copy needs refreshing from a newer build):*
1. **Intro and launch cutscenes lag.** Being measured: `D:/f117-gate/remaster-flight-20261031/lag2.sh` times the same replay (boot to the flight, `rec/input-go.log`) in the original, Enhanced and Remastered presentations (`lag2.txt`). Cause unknown; suspects: the Remastered path's per-pixel origin tags, the fine picture at scale 3, the sidecar's tick loop.
2. **A dark bar across the hangars** (screenshot in the owner's message; not reproduced). Trace with `F117_REMASTER_PROBE=X,Y` and `F117_REMASTER_KEPT=2`.
3. **A second image lagging behind when moving.** Believed to be the missing interpolation: the composite takes its mask from Enhanced's interpolated picture and its fill from the last completed logic frame. Fix is the "interpolation between logic steps" item.
4. **Mac sounds did not play at the hangar**: the tested build predates the Mac mappings added since (intro music 3 to 200, weapons page 29 to 323 and 30 to 325, the flight program's 31 to 323, `macsound.c`) and the PLAYER thunk hook (`PLAYER.EXE 02A3:13B9`).
5. Opening jingle: heard once `f117a.ini` pointed at the ROMs (`roland = munt`, `mt32-roms`). Confirmed by the owner.
6. Hangar-door lights: now full squares; they sit about 3 px from the original's because the original's lines and lights use projection records that lag one logic step (the polygons' do not): Remastered draws them where the geometry puts them (a flaw of the original fixed, not an error). Two black dots and some blue bits behind the doors showed through the "whole line" layer; the fix (bright lines only, `remaster_live.c` composite) is written but was not yet built and gated at the time of this note.

*Remastered renderer, open:* interpolation (3 above); longer draw distance (R6); objects wholly outside the game's own view are not drawn in the widescreen margins (the game culls before asking; fix by running the scene walk on a hidden render-only machine copy with a wider view, the same job as R6; the old `draw-rerun-probe` branch, `8849ce4` and its three children in `D:/f117-archive/orphans-20261031.bundle`, tried re-running the view compose on a machine copy); about 1,000 original pixels a frame still kept (thin lines around the distant aircraft, taxiway edges: `F117_REMASTER_KEPT=1`); the leftover-pixel check and the zoomed-crop review on other scenes (air-to-air, ground targets, other theatres, night): so far one takeoff at dusk; lines and dots have no per-model accounting yet (polygons do); a fresh full gate (the last full gate predates the door fixes, the line layer and the logic-fill change).

*Hybrid sound, open:* the code proof of ids across the drivers is `tools/sound_id_proof.py` and `docs/sound-id-table.md` (all three drivers have a handler for every id 0 to 45; the PC speaker's id 39 is a bare RET; it does not prove the sounds are alike); the Mac-to-DOS mapping is still partly by ear or by the Mac code's own calls (`mac-extract/dis`, calls through `jsr $54a`): unproven: hangar door 324, engine loop 500 (Mac plays one loop for 311 and 312), START's 31 (launch animation), speech phrases 41 to 45 (the Mac has nine separate words), the Mac's own ids 27 and 28; the owner has to listen (Mac recordings, the AdLib effects beside Roland).

*Other Phase 3 items:* settings menu gaps (custom fix sets, folders and ROM paths, speed numbers, the joystick answer, mouse use); a real controller or HOTAS and button remapping; replays (jump to a time or marker, other cameras in Remastered); accessibility (a colourblind-safe HUD option, subtitles under AdLib).

*Housekeeping:* the three old presentation branches (`draw-rerun-probe`, `present-horizon`, `present-hud2`) and their worktrees were removed on the owner's say-so (31 Oct); about 26 other worktree folders remain under `D:/f117-wt` on branches already contained in `master` (ask before removing them); the app copy in `ONTests` is about 64 MB (`f117a.exe` 60 MB, SDL, Munt); the two tooling DLLs (`f117machine_api.dll`, `f117cpu_api.dll`, 55 MB and 0.2 MB) are not needed to play.

## Unmerged branches and uncommitted worktrees (scan of 30 October 2026, 23:45)

Nothing below was changed or deleted; ask the owner before deleting anything. `p3-remaster-live` and `p3-hybrid-sound` were live Phase 3 work and are now merged into `master` (`p3-hybrid-sound` was recorded as merged in `bd58bc2`; its last commit, `4e2f274`, removed a hook edit master never had). The rest:
- **Old WIP branches, one commit each, not in `master`:** `present-horizon` and `present-hud2` (14 Oct, "stopped mid-task for a conversation handoff, unverified"), `wip-view-compose-2f9d` (12 Oct), `origin/p2-batch-setup-logo` (15 Oct, WIP SETUP `19B6`), `origin/p2-wt-batch` (10 Oct, a draft `vgame_camimage_background_raster`), `draw-rerun-probe` (4 commits, 15 Oct, an experiment). `docs-pass` has 3 commits dated 30 Oct that are not in `master` (docs text: "state what was ob...") worth a look before the doc pass.
- **Worktrees with uncommitted changes (the work exists only on disk):** `C:/Users/Tideg/f117-recomp-local/wt` (detached HEAD, **24 files, +1958/-103**, touching `CMakeLists.txt`, `config.c`, `headless.c`, `main.c`, `matched.c` and more: the largest unrecorded change, inspect first); `D:/f117-wt/credit` (4 files: `progress.py`, `progress.md`, tests, `func_lockstep.c`); `D:/f117-wt/gate-h` (`main.c`, `rerun.c`, +76); `D:/f117-wt/experimental-livebp-20261030` and the two `.claude/worktrees/agent-*` worktrees (a few lines each in `src/matched/matched.c`, probably experiments); also `D:/f117-wt/wake-bug` (detached, 6 files), `D:/f117-wt/mission-generation-4239` (5 files) and `D:/f117-wt/setup-main-0010` (3 files; both carry the private /Od overrides: never merge wholesale), `D:/f117-wt/thunk-candidate-check` (detached, 3 files) and `D:/f117-wt/harness-rules` (detached, 1 file). No detached-HEAD commit is stranded: every one is reachable from a branch. One registered worktree, `C:/Users/Tideg/My Drive/Development/F-117A Recomp/.claude/worktrees/agent-ada5a0330bc7d4569` (locked), no longer exists on disk.
- **Stashes:** five (`git stash list`), the first being `preserve owner docs before P3 acceptance merge`; not dropped.

## Orphan triage (31 October 2026): what each rescue snapshot held

Every worktree with uncommitted changes was pinned, without touching it, as a local `rescue/<name>-20261031` branch (not pushed:
some carry private overrides and the repository is public). Each was then measured (share of its added lines already on the tree) and
read. Result: nothing is lost by tossing any of them except the one P2 candidate below. Deleting a worktree or branch is the owner's call.

| Snapshot (worktree) | Already on the tree | What is left, and the verdict |
|---|---|---|
| `agent-a7d0...`, `agent-ace3...`, `experimental-livebp-20261030` | 100% | nothing: toss |
| `mission-generation-4239`, `setup-main-0010` | 99-100% | only the private `/Od /Ob0` override and fixture-iteration rows: toss (never merge wholesale) |
| `thunk-candidate-check` | 99% | the private ISOUND `026C` clamp-probe harness; the routine was accepted in `48869f4`: toss |
| `harness-rules` | 97% | the origin of `body_overrun` (the tree has the refined form): toss |
| `wake-bug` | 99% | an early draft of the stale-override entry-opcode check; the tree has it (`recomp.c` `in->img` check, `tests/test_stale_override.c`): toss |
| `credit` | 59% | the 16 Oct byte-credit design of `tools/progress.py`; the tree has the evolved form (`--named-cover`, `--route-cover`): toss |
| `gate-h` | 1% | private 4K stage-timing instrumentation (Windows-only calls, hidden 4K window, refuses to start without `F117_PROFILE_LOG`): toss; the supported route is `--present-log` plus `tools/presentation_timing.py`. Idea worth remembering: per-stage columns (sim, wait, worker, convert, publish, plan, prepare, upload, present) |
| `wt` (detached, base 10 Oct) | 89% | 35 of the 36 routines it added are on the tree. **Keep: `vgame_clip_divide_fault`, VGAME `0FB2:04DD`** (the clipper's INT 0 saturation handler; a one-line wrapper `vgame_raster_divide_fault(m, 0)` registered with level 3, plus lockstep rows `{ 0x0FB2, 0x04DD, 4, 0x046E }` and planted states that clear IF and TF and set `int_depth`; see the `matched.c` and `func_lockstep.c` hunks in `rescue/wt-20261031`). It exists nowhere else in git history. The tree has the sibling `084E`. `docs/progress.md` calls it "the line-clipper divide-error handler that no route reaches"; under the 29 Oct rule static code is matchable on lockstep alone, so it is a proof-and-gate candidate for the next P2 batch. The rest (model-seam repair `drawlist_finish_models`, a 17-field `'G'` snapshot record) is superseded presentation work from before the Enhanced consolidation (still in history: `5361129`, `571a490`). Its five untracked files (`nearclip.c/h` and three tests) are early drafts of what the tree has, copied to `C:/Users/Tideg/f117-recomp-local/rescue-20261031/wt-untracked/` |

Old WIP branches (not in `master`, nothing running on them): `present-horizon`, `present-hud2` (their band and HUD tests exist on the tree and pass), `wip-view-compose-2f9d` (`0x2F9D` is matched and hooked), `draw-rerun-probe` (the rerun path exists), `origin/p2-batch-setup-logo`, `origin/p2-wt-batch`. All superseded; safe to delete when the owner agrees. `docs-pass`, `p3-remaster-live` and (except one moot cleanup commit) `p3-hybrid-sound` are contained in the merge.

### Done: the `0FB2:04DD` job (31 October 2026; the owner asked that no useful work be lost)

`04DD` (the clipper's INT 0 saturation handler: `push bp; mov bp,sp; cmp word [bp+2],046Eh; mov ax,[228Dh]; je; mov ax,[228Bh];
xor dx,ax; mov ax,7F00h; jns; neg ax; add word [bp+2],4; sub dx,dx; pop bp; iret`, 33 bytes, 14 instructions) has the same shape as the
accepted `084E`. It is now registered as `vgame_clip_divide_fault` (`vgame_raster_divide_fault(m, 0)`, level 3) with the
`INTERRUPT_FRAMES` row `{ 0x0FB2, 0x04DD, 4, 0x046E }`; no new body was written, the shared body already existed. Evidence:
`func_lockstep --only VGAME.EXE:4DD` at six seeds, 3,992-4,000 states each, 0 mismatching (`084E` runs the same size); four mutants
of the `spans = 0` arm (compare constant, denominator, IP advance, clock charge) each give 1 mismatching; full gate on the tree
(827 tests, 63 routes identical, 1642 routines at three seeds, 0 mismatching, steps 8 and 9 clean). Landed from branch
`p2-clip-04dd` (`dea8936`). The percentage was not refreshed: run `tools/progress.py --census` with the next batch to credit the 33 bytes.
The old snapshot `rescue/wt-20261031` is now redundant (its body used a replaced trap-flag line) and is also in
`D:/f117-archive/orphans-20261031.bundle`.

### Fixed: the lockstep hang at `START.EXE:8A32`, state 1627 (31 October 2026)

The first full gate of the merge (commit `863898e`, seed `0x863898e40efa`) never finished: matched-routine lockstep shard 7/8 sat
on one `func_lockstep.exe` for over two hours. Reproduction was `func_lockstep.exe --states 1628 --seed 0x863898e40efa --only
START.EXE:8A32` (`1627` finished, `1628` hung). It was not caused by the merge: the `master` build (`48869f4`) hung the same way.
**Root cause** (an earlier note here blamed `st2_fill_rect`'s 64K-row run; that was wrong): the planted state made the original code
slide along a chain of instruction-prefix bytes, and `cpu_step`'s prefix loop (`src/cpu/cpu.c`) has no bound, so a single step never
returned and the per-state step limit never fired. **Fix** (commit `7967b67`, `tests/func_lockstep.c` only; the core was left alone,
it would need the full gate for no gain): `prefix_runaway()` stops a state whose bytes at CS:IP are more than 64 consecutive prefixes
(no instruction can complete), counted and reported as `prefix-run guard: N state(s) stopped as long runs`; plus an opt-in watchdog,
`F117_LOCKSTEP_WATCHDOG=N` (seconds), that prints the stuck state and exits 3. Evidence: the full table at the hanging seed finished in
15 minutes (1641 routines, 6,873,385 states, 0 mismatching, guard stopped a handful of states per shard); the full gate on the fix
tree passed (827 tests, 63 routes identical, 1641 routines at three seeds, 0 mismatching).

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
- **Normal play:** 15 Hz / 60 MIPS, all 34 fixes on (`--fix QUIZ` restores the quiz, off by default). Parity
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
- **Commit and push together**, each verified step; commit titles start with the `progress.py --title` line.
  Stage files by name; never stage the owner's `4k60.bat` or `original-fixed.bat`.
- **Public repo, code only:** no game data, generated C, raw captures or private-project references
  ([docs/provenance.md](docs/provenance.md)).
- Long commands (over about 30 s) run in the background. While waiting, do a doc pass over every `.md`.
