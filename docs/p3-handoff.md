# Phase 3 handoff

## Current: 3 November settings and shoreline

The active goal remains **get Phase 3 to 100% safely**. The previous goal turn
made verified progress (installed shoreline correction); this turn completes
the requested settings batch. Neither local batch is completion of Phase 3.

**Verified and installed in ONTests:** SHA-256 `7A50EC412B89FE1B5E5F4B9D3728E478305298F0BF511FAF137E89F042C02974`.
The prior coast build is backed up at `private/settings-20261103/ONTests-before-settings.exe`.
All twelve host acceptance cases pass, including eight real-menu settings
subcases and an actual fresh-process restart. Eleven focused native tests,
twelve ROM-free defaults/precedence tests and 25 Python checker/presentation/
title tests pass. The report verifies against this executable and both checker
files: `private/settings-20261103/settings-release-verified/report.json`.
The gallery includes the actual Audio, Advanced, binding/output-key lists and
reset screens; they were visually reviewed. No full machine gate was run.

**Settings completed in code and integration checks:** active sound survives
saving/restart and SETUP advances through its normal typed N/driver answers;
saved edits supersede conflicting launch arguments; Reset writes every menu
default, clears custom bindings/fixes, cancels without changes and keeps resource
folders. Sound intentionally returns to Ask on factory reset. Advanced and
Audio pages group the requested controls without removing capabilities.
All 101 supported keys are listed with blue nondefault mappings, output-key
selection (including Escape/punctuation), individual restore and paging.
Remap persistence is canonical and supports more than the old 64-pair/400-byte
limits. Custom numeric launch values remain visible/preserved. Prior blue
option values, wrapped fix descriptions and setting help remain intact.
See [settings guide](settings.md). The original code's fix default policy is queried read-only;
Reset does not accidentally enable the optional quiz. Original presentation
also correctly overrides a Remastered configuration; a ROM-free process test
checks that actual parser path.

**Owner shoreline feedback:** the installed 81e0e11 coast build seems fixed in
the owner's quick recomp check. They then tested crashes on: slow dirt and
water taxiing worked, accelerating on either crashed. These were explicitly
**recomp tests, not a GOG gameplay comparison**. They agree with the matched
original taxi-ground branch and its instruction bytes (this off-field check
becomes eligible above 50 knots). There is no water test in that branch.
The Remastered correction changes drawing only. Broader coast/road/mission
review remains open; do not convert this feedback into blanket renderer approval.

**Full scope still open:** renderer throughput (measured native 4K remains
below 60 fps), longer scenery reach, road-motion/terrain transitions, broader
depth/visibility and interpolation checks, widescreen HUD/cockpit work, and
the remaining controls, accessibility, replay-camera and hybrid-audio rows in
`docs/progress.json`/the roadmap. Physical controls, explicit joystick-answer
coverage, original mouse interaction, clipboard/folder interaction, broader
sound-driver listening/reference checks and owner approval remain applicable.
Do not mark 100% from the settings tests. Scores are unchanged pending the
broader requirements; commit titles use `tools/progress.py --title`.

Continue from [current checklist](p3-current-work.md) and the roadmap. The next renderer task is general scenery reach.
A useful performance investigation
is whether the normal true-color Remastered path still computes an unused
paletted image; prove pixel equivalence and measure before claiming a gain.
Scenery work must address which detail-ring cells are requested, not only
raise the far plane. Keep all simulation and original renderer behavior intact.
Honor the standing limit: no additional full machine gate without authorization.

**Preserved owner work:** sound-board tool edits and the existing untracked
files remain excluded. The old ignored `unfinished-snapshot` patches are now
superseded by accepted coast/settings source; do not apply them over this code.

Older sections below are historical; this section takes precedence.

## Latest: renderer performance (2 November 2026)

**Owner clarification:** GOG has the moving coastline too; Remastered must correct it, ideally matching an authoritative gameplay water boundary if one exists. No further GOG reproduction is needed merely to establish that baseline. See [coastline-reconstruction.md](coastline-reconstruction.md) for the twelve screenshots, Halfar departure location and collision/terrain investigation. The shoreline correction remains open; the earlier margin cutoff is a separate installed fix. Older GOG-pending statements below are superseded.

Read [remaster-performance.md](remaster-performance.md) before claiming 4K/60. Large Remastered rasters now use eight workers with 64-row groups on machines with at least 16 logical CPUs; smaller rasters retain four. Two runs per configuration measured 13.36/13.13 fps with four workers versus 18.00/17.83 with eight balanced workers at scale 9, software-renderer 3840x2160. This remains below 60 fps; physical pacing is unmeasured. All 167 moving scale-9 images (airborne, hangar, water and 4:3) match a single-worker redraw exactly. Worker timing counters are now private, with explicit worker-maxima reporting and separate loop build/upload/present timings. Evidence: `D:/f117-gate/remaster-workers-20261102/`. Current build SHA-256: `012D6A61D1FEF5D43F63CE31C3D327F65A760AE82DE8332C811651A389591BD5`.

All ten host cases pass; current report `D:/f117-gate/host-acceptance-coast-scope-20261102/report.json` verifies against this build. The expanded suite adds eight-worker/one-worker comparisons over moving wide and 4:3 frames (42 each, zero differences). Total cost 125.8 seconds at two jobs. Eleven focused native tests and the thirteen/seven Python checker/presentation tests pass. Earlier nine-case reports no longer verify against the current checker/manifest. The verified Phase 3 code/tests are committed with this handoff. No full machine gate or new deployment was performed; ONTests remains the earlier installed coastline/audio build. The engine cutoff and false brown water-margin strip are fixed there, awaiting owner confirmation; hangar/coast alignment remains open. Next performance work must preserve detail and exact pixels when merely eliminating work; reducing the requirement is not completion. Scenery reach, coastline stability and broader fast-turn checks remain open alongside the other Phase 3 requirements.

## Host acceptance (2 November 2026)

The initial prevention suite passed all nine recorded-flight host cases in `D:/f117-gate/host-acceptance-final-20261102/` (116 seconds, two jobs), for build SHA-256 `813a99687f232a5182a6c164c19cd138cff466d30fa0f234c991fe23f1dfd7ea` and the then-current checker/manifest. This historical report is superseded by the expanded ten-case suite above; see [host-acceptance.md](host-acceptance.md) for current commands and coverage. Twelve ROM-free verdict tests, seven presentation-check tests and eleven focused native CTests passed at that point. Two source-level broken builds separately demonstrated rejection of a dropped speed argument and missing continuous-speed routing; the final healthy run followed byte-exact source restoration. Evidence: `D:/f117-gate/host-acceptance-faults-20261102/`.

The suite now runs through the presentation light-check for host/presentation source changes; Windows CI runs its ROM-free verdict tests. This supplements the unchanged machine-parity gates. No new full machine gate, owner/GOG approval, commit, push or deployment is claimed. ONTests still holds the previously installed `507D03C...` coastline/audio build described below. The new suite's diagnostics are off during normal play. GOG hangar/coast alignment, perceived sound/motion, broader viewpoints and remaining renderer features still need their applicable checks.

## Start here: 2 November 2026 stopping point

**Later update:** work resumed on the coastline/sound feedback. Read the final "2 Nov follow-up: coastline and zero-thrust sound" section first. ONTests now has SHA-256 `507D03C37566D8E64852625C5BC5814360C57B82C4A427258F5CDA38155F1516`; the installed-build statements in the original stopping-point notes below are historical.

The owner requested a handoff, commit, push and stop. Renderer work is at a tested, deployed stopping point; there are no renderer jobs to resume. Read this section before the older chronological notes below, several of which describe defects fixed later.

**Installed:** Desktop/ONTests/f117a.exe is the current build, SHA-256 `6C243D476FEF9690DA73FA17734D6214638151F55DD37F34D5732C1F9E4929E9`. Use the owner's usual ON-roland.bat. This session fixed screenshot dispatch/naming/feedback, mixed native depth units (gray geometry through aircraft/buildings and parked-jet/tower ordering), rear yellow hangar strips, attached-light depth/compositing stripes, finite stroke endpoints at flag/pole junctions, and tilde-menu top-bar overflow. The owner said the depth/yellow-strip build was much better; the latest light, flag and menu build still awaits their visual check. Do not record these latest fixes as owner-confirmed.

**Evidence and checks:** the bottom two renderer sections explain the causes and tests. Eight focused menu/presentation CTests pass; the deployed executable matches build/f117a.exe. Seven owner screenshot clocks, moving hangar/transport sequences, and Original flag comparisons are saved. Four-thread vs single-thread checking reported zero differing pictures (at least 121 checked). No full machine gate was run for this host/presentation work; the historical full-gate notes do not cover these edits. Galleries: `C:/Users/Tideg/Desktop/ONShots/20261102-depth-order/index.html` and `C:/Users/Tideg/Desktop/ONShots/20261102-lights-and-flags/index.html`. Scratch/evidence: `D:/f117-gate/depth-20261102/` and `D:/f117-gate/lights-20261102/` (the installed candidate there is caps.exe; earlier candidate/final variants are superseded).

**Next conversation:** first incorporate the owner's visual feedback on the latest build. Remaining renderer scope: scenery draw distance/pop-in (the extra walker experiment is off, not a finished solution), general object-size-aware visibility beyond the initial aircraft change, close-margin visibility confirmation, road/coastline jitter and motion/interpolation validation, and the unresolved/unconfirmed dark hangar band, green overlap patch and trailing-image reports. Broader mission/viewpoint checks and new performance measurements remain. Do not infer all renderer work is complete from the local regressions passing.

**Reproduction:** user's screenshots and recordings are under `C:/Users/Tideg/AppData/Roaming/F117Recomp/F-117A/`; replay session 20261102-141108 with `--adlib-effects` to match the Roland bat or the replay diverges. Use a private save-start copy. For comparison captures use `F117_PRESENT_THREADS_OFF=1`; remove it when checking actual multi-thread composition with `F117_REMASTER_THREADCHECK=1`. Build through `C:/Users/Tideg/f117-recomp-local/build-targets.bat`. Focused check: `ctest --test-dir build -R "presentation_(remaster|scene|rerun)|host_key_reference|host_settings_menu" --output-on-failure`. Check ONTests is closed before replacing its executable. Respect the owner's existing limit on further full gates; do not launch one merely to hand off.

**Preserved unrelated work:** tools/sound_board_page.py and tools/sound_board_template.html were already modified and are excluded from this renderer commit. Untracked `.ppm`, `100`, `4k60.bat`, `docs/p2-completion-plan.md`, `original-fixed.bat`, and `x.slice(0` are left in place. Do not clean or stage them indiscriminately.


## Where this stopped (night of 1 November 2026): start here

**Read first:** [owner-issues.md](owner-issues.md) (every owner report with status and evidence, the owner decisions, the gates) and
[bugs.md](bugs.md) ("What a fix needs", "Fix audit"). P3 is 93.84% (`py tools/progress.py --title`); what is left needs the owner's look, ears
or hardware, or recordings (below). `master` is pushed. The last full gate (11, the owner's limit for 1 Nov) passed on a branch of the P3
commits (`p3-gate` in the `gate-master` worktree, without the P2 session's staged `5cd50d8`); later host-only commits were checked with the
unit tests only (832 of 832).

**Done and confirmed by the owner:** R7 (the parked jets: the Q11 fix culled them, Q11 retired), R10 (cutscene lag: the driver wait loops run as
one step in `machine_run`), R13 (map: kept rectangles survive a reset), the speech stall S9 (same kind of fix), sound level.
**Fixed in code, awaiting the owner:** R16 (hangars and aircraft vanishing in the widescreen margins: the rerun copy's side allowance at
`[E56A]` raised to 18,000, `rerun.c`), R15 (the menu shows what is running), T1 (tilde opens the menu everywhere; the README is the key
documentation: never put documentation in the owner's .bat files). **Open:** R17 (grey horizon line in front of the jets), R18 (flagpole
z-fighting: candidate in the line layer's depth bias, check the original's picture first), R19 (green patch, probably R16), R14 (older margin
report, probably R16), R9 (draw distance: the far plane was ruled out on the takeoff; needs a high-altitude flight over distant targets, the
owner offered their recent flight `session-20261101-224822`), the Remastered depth-correct, interpolation and widescreen-HUD rows, controllers on
real hardware.

**Owner decisions (1 Nov):** the 22 repairs that reach no route stay on by default; Esc stays the game's (it cancels the flight's Alt+Q dialog,
skips PLAYER's animation, cancels START text entry); the menu is the tilde key; no more full gates without approval (they authorised one more
after the R16 to R19 fixes "if you deem it necessary"); no AI attribution in commits.

**Update, 2 November 2026 (morning), after the owner's second look at the build of 23:50:** R16 (hangars) is confirmed better by the owner;
the tower's wind-measure is fine. Done in code since, all in the rerun copy only (`src/present/rerun.c`), awaiting the owner's look in `ONTests`
(10:53 build, 832 of 832 unit tests, not full-gated): **R20** the jets near the tower no longer vanish in the margins (aircraft have their own
visibility test, VGAME `0B5DC`, with a 45-degree cone: an exec hook at its entry divides the x row of its matrix by the view's width factor);
**R9, aircraft part**: far aircraft become models at twice the distance (the same hook divides all three matrix rows by 2, so the size
indicator `[DEBE]` is half and positions are unchanged; before, they were dots or absent until close). **Not done, and why:** R17 (grey horizon
line in front of the jets) and R18 (flagpole z-fighting) are not reproduced in any of my frames of sessions `224822` and `235935`; for R18 the
original shows the dark-red flag on top of the pole, as Remastered does at the moments I could check; both need a clock or a screenshot from the
owner (details in their rows in owner-issues.md). R9 for scenery and ground objects: raising the detail value (`F117_WIDE_REACH=N`, ring sizes
and far plane rewritten as `D4A5` does) changed nothing visible, so the terrain keeps the original's distance; a flight over distant targets would
be the test. Newest owner flight to use: `session-20261101-235935` (aircraft in the sky at the start).

**Tools made tonight:** `tools/fix_audit.py`, `tools/verify_flights.py` (with `docs/flight-reference.json`), `tools/audit_routes.py`; app
diagnostics `F117_PRESENT_SHOTS=PREFIX` with `_FROM`, `_COUNT`, `_STEP`; `F117_WINDOW=1920x1080` (a wide window under the dummy driver);
`F117_REMASTER_LEFT=1`, `F117_MODEL_LOG=MODEL,CLOCK`, `F117_REMASTER_COPYCHECK=N`, `F117_WIDE_VIEW=100` (off), `F117_LOOP_TIMING=1`,
`F117_DUMP_RAM=CLOCK:FILE`, `F117_ESC_AUTOTEST` with `F117_ESC_ANYWHERE`, `--replay-fixes cli`. A replay applies the fix list in its log header
(edit the header to compare fixes). The owner's flights are in `%APPDATA%\F117Recomp\F-117A\sessions`; replay one with
`f117a.exe --data D:/GOG/F-117A --save COPY_OF_save-start --replay SESSION/input.log --presentation remastered --present-scale 3 --sound adlib`
under `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software`, and look at the pictures yourself (the owner asked to have
these issues caught rather than be told). Evidence frame sets are in `D:\f117-gate\remaster-flight-20261031\` (`dense*`, `tl1`).

**Pitfalls met tonight:** build from PowerShell with `cmd /c C:\Users\Tideg\f117-recomp-local\build-targets.bat TARGETS` (a `cmd //c` build from
bash silently does nothing, and a test then "passes" on a stale binary: check the exe's time); a `\n` in a C string written through a Python edit
comes out as a real newline (use the Edit tool, or build the backslash with `chr(92)`); `tools/build_recomp.py` compiles the main tree, so use the
`gate-master` worktree (the `gate-gatemasterN.ps1` pattern); never stop `f117a` by name (the owner tests from `Desktop\ONTests`) and do not touch
`ONTests` while it runs; the unaccepted P2 VGAME thunk family was removed from mainline on 2 November because 29 of its 34 addresses overlap the graphics observers and would bypass Remastered capture; see docs/p2-brief-ci-failure.md.

**Next, in order:** (1) the owner's look at R16, T1 and R15 in the refreshed `ONTests`; (2) reproduce R17 and R18 on their flight (frames near
the jets at the runway's far end, and close to a hangar with its pole; compare `--presentation original`); (3) R9 on the recent flight at high
altitude; (4) the Remastered depth-correct and interpolation rows from crowded and fast-turning frames; (5) flights that reach the 22 untested
repairs.

## State on 1 November 2026

Read [owner-issues.md](owner-issues.md) first: every issue the owner reported, with status and evidence, and "Where this stopped" notes. Scoreboard 93.0% for P3 (`py tools/progress.py --title`). The owner's test copy is `C:/Users/Tideg/Desktop/ONTests` (a copy of `f117a.exe` plus `ON-roland.bat` and `ON-mac.bat`; refresh it from the newest build when nobody is running it). The scratch build tree is `C:/Users/Tideg/f117-recomp-local/clip04dd` (copy the changed files in, then `cmd /c "C:/Users/Tideg/f117-recomp-local/build-app.bat"` from PowerShell and check the exe's timestamp; `ct4.bat` builds everything and runs the host and presentation tests). Evidence and recordings: `D:/f117-gate/remaster-flight-20261031/` (the owner's sessions are under `%APPDATA%/F117Recomp/F-117A/sessions`).

Done in the last two days: the Remastered renderer in the app (widescreen, the line layer, depth-tested lights, the rerun copy as its frame source, the composite on four threads: 60 pictures a second on the takeoff roll, checked pixel for pixel against one thread with `F117_REMASTER_THREADCHECK=1`); hybrid sound (every effect to an AdLib driver beside Roland, the Mac recordings, radio calls from the Mac's words by the driver's own lists, `docs/sound-code-proof.md`); the settings menu (rows, a Folders screen), a colour-blind aid, subtitles by request, a fifth key-list page; the mouse follows the game's cursor.

Open, in the order to take them: the owner's own looks and listens; R7 (the jets at the tower: needs the mission and the moment) and R13 (the map: needs a screenshot); R10 is the P2 session's (the machine runs the DOS-poll stub `0060:00C4` slowly); longer draw distance (R8/R9) through the rerun copy with a wider view; road and coastline jitter (terrain polygons are not model requests yet); the 4K cost of the composite; controller hardware and the other buttons; nothing of the full gate: it passed on `master` at `0243e6d` (`owner-issues.md`, "Full gate on master"); run it again before calling later machine-layer changes accepted. Diagnostics added: `F117_LOOP_TIMING=1` (where each 2 seconds of wall time went), `F117_REMASTER_TIMING`, `F117_REMASTER_KEPT`, `F117_REMASTER_THREADCHECK`, `F117_REMASTER_THREADS_OFF`, `--present-log` (pictures a second).


Phase 3 (host-side features and the Remastered research) is merged into `master`. The merged tree passed the full
acceptance gate during P2 batch `bx` (823 tests, 63 routes, 62 fresh interpreter sessions, all 1,638 matched routines,
both timing profiles, three gate seeds, N=1 presentation on all routes, boot N=4/9; evidence
`D:/f117-gate/p2-batch-bx-accept/`). Continue on `master`. Scoreboard: `py tools/progress.py --title`.

**Gate on `p3-remaster-live` (30 October 2026, commit `d9fc8b8`, about 33 minutes):** `tools/build_recomp.py` passed in full:
824 unit tests; all 63 routes identical between the recompiled engine and the interpreter (62 fresh interpreter sessions);
5,764,032 instruction states under each timing profile, 0 mismatching; all 1,638 matched routines at three seeds, 0
mismatching; every routes-only routine ran on a route; none ran past its event limit. That covers the `src/machine` change (the
execution hook table in `machine.h` and `pc.c`). Not run: the presentation groups of a batch acceptance (N=1 on all routes, boot
N=4/9), which the Remastered composite touches only when `--presentation remastered` is chosen. The log is kept at
`C:\Users\Tideg\f117-recomp-local\p3-session-29oct\gate_p3_remaster_live_d9fc8b8.txt`.

The hybrid sound and Remastered work on `p3-remaster-live` changes `src/machine` (`machine.h`, `pc.c`); the gate above covers it.
Everything else listed here is host-side (`src/host/`, `src/present/`, `tools/`, `docs/`, `tests/`).

## Items

| Item (weight) | Credit | What exists | What is missing |
|---|---|---|---|
| Settings menu (4) | 80% | Ctrl+Alt+S, a scrolling list of 21 options (presentation, fixes, window, fullscreen, speaker, speed, CRT, subtitles, sound driver, music and effects source, Roland output, game steps, AdLib emulation, 4:3 shape, line width, picture delay, stick dead zone and inversion, CPU engine), Choose fixes, Remap a key, Clear key remaps, Controller button as key, Clear controller keys, Watch last flight and Watch a flight; writes `f117a.ini` keeping its comments; a restart is offered (`settings.c`, `config_set/config_get`) | every option (folders and ROM paths need text entry), the joystick answer, the owner has not looked |
| CRT filter (1) | 70% | `--crt`, `crt = yes`, menu entry (`crt.c`), applied to the displayed copy only | the owner has not looked; no mask or bloom |
| Modern controls (3) | 65% | `pad-deadzone/-invert-x/-invert-y/-fire1/-fire2`, `pad-keys = 2:G,3:Left Shift` (a button acts as a key); `remap = Q:W` / `--remap` and a Remap a key screen; an on-screen key list (Ctrl+Alt+K, five pages: four from the manual, one with this program's own keys) | no real controller or HOTAS tried, a menu screen for `pad-keys`, mouse |
| Flight replays (3) | 80% | `--watch latest\|SESSION`, a chooser in the menu, `+`/`-`/`0` speed, `Ctrl+Alt+P` pause, `Ctrl+Alt+R` restart, `]`/`[` jump 30 s, `G` + digits + Enter jumps to a typed second, `--seek SECONDS` | other cameras (Remastered) |
| Accessibility (2) | 55% | keyboard remapping and its menu screen; radio subtitles (`--subtitles`, Roland and PC speaker drivers, from the speaker counts, `subtitles.c`) | AdLib speech, the PC speaker driver run, a colourblind HUD (Remastered) |
| Fan patches (2) | 100% | debugcom's patch (damsonn) is fix D4; a web search found no other | none, unless the owner finds another |
| Two settings (2) | 50% | Fixes Original / Recommended / Custom (`--fixes custom`, the Choose fixes screen), Presentation Original / Enhanced, saves tagged with their fix set | the Remastered presentation (still refused) |
| Sound device (1) | 80% | `--sound adlib\|roland\|speaker`, `sound =`, a menu row: types SETUP's answers ([setup-autoanswer.md](setup-autoanswer.md)) | a hybrid choice, joystick yes |
| Remastered (10) | 0% | `docs/remastered-design.md`; decoders for the terrain, world and model files, checked against the game's memory; an offline depth-buffer model sheet; the R3 plan | the renderer in the app (see "R3 plan revised") |
| Hybrid sound (3) | 0% | the sound board page, the sweep tool, the id census, the owner's rule; on branch `p3-hybrid-sound` (unmerged): the source policy, a Mac recording player, a machine-layer hook ([sound-board.md](sound-board.md)) | a flight that fires a recorded sound, the Roland/AdLib split (a second driver), START and END sounds, the gate |

Diagnostics: `F117_MENU_AUTOTEST=1` and `F117_MENU_SHOT=FILE.ppm` (window-less menu run). Tests: `ctest -R host_` (settings
menu, CRT, controls, remap, key list, subtitles (needs `F117_SPEECH_117`), config, sound policy, Mac sound).

## Branches (30 October 2026)

- `p3-remaster-live` (includes `p3-hybrid-sound` and the docs pass): the machine's execution-hook table (`machine_t.exec_hook`,
  `machine.h`, `pc.c`: the one change under `src/machine`, needs the full gate), the hybrid sound player, and
  `src/present/remaster_live.c`, which draws a recorded flight's frame from the running game
  (`F117_REMASTER_FRAME=N:FILE.ppm`, beside it the original's last picture as `FILE.ppm.orig.ppm`; `F117_REMASTER_DEBUG=1`
  prints the view origin and the band state). Checked: the machine-loop parity (one recorded flight, hook on and off:
  identical exit clock and save files), the rasteriser tests (polygons, lines, dots).
- `docs-pass` and `p3-hybrid-sound`: older tips of the same work; `p3-remaster` is merged to master.

## Remastered: what the live frame does, and the app wiring still to do

The first live frame (recorded takeoff, frame 150) matches the original's horizon, hangars, antennas, runway centre dots and
dusk colours. Missing from it: the face shade in the colours, the runway edge strips close to the eye, hangar detail.
The HUD, cockpit and radar are the original's 2D overlays and are not in it.

**Wiring (31 October 2026, merged into `master`):** `--presentation remastered` is accepted (`src/host/main.c`, and a row in the
settings menu). It is the Enhanced picture with the 3-D view redrawn by `src/present/remaster_live.c`: the model requests of each
frame are captured at the machine's exec hooks, and the redrawn view replaces the fine picture's pixels whose origin tag is a scene
tag, so the original's 2-D layers (cockpit, state-driven HUD, text) stay as Enhanced draws them. Not done: a run of this path in a
real flight compared with the Enhanced baseline (takeoff replay, `--presentation remastered --present-scale 3`, `py tools/remaster_diff.py
ENHANCED.ppm REMASTERED.ppm`); interpolation between the two latest steps; widescreen and longer reach; the fit rule for the cockpit and HUD
(see `docs/remastered-design.md`). The Remastered item stays at 0.0 in `docs/progress.json` until the owner decides.

## Remastered: R1 and R2 done, R3 next

Design and progress: [remastered-design.md](remastered-design.md). Decoders (`tools/world_decode.py`, `world_check.py`)
read `*.3DG`, `*.3DT` and `*.WLD` from the layouts START's matched loaders use (`matched.c`, START `0x07D71` and
`0x07E88`); every byte of all nine theatres is accounted for, and LB, NC, PG and VN match START's memory (VN but six
runtime-written bytes). The mesh decoder decodes all 3,936 (model x level) cases of the 11 `*.3DX` banks
(`tools/model_census.c`); real models draw correctly with a depth buffer (`tools/model_sheet.c`). The projection
(`remaster_project.c`) and the depth-correct triangle fill (`remaster_raster.c`) are used by `remaster_live.c`, which `--presentation remastered` runs.

To dump START's memory for a check: `py tools/run_route.py tools/routes/<route>.args --data <copy of the install> --out
<dir> -- --steps <N> --dump 0:100000`, N just after START opens `<stem>.3dT`.

**Next (R3 proper):** read the prepared pose from the running game rather than rebuilding it (the game prepares part
matrices, translations, eye, light and palette before projecting; see "R3 plan revised" in the design doc), then the
camera chain with sub-pixel output, terrain from the tile maps and the ground-patch models (the first ~30 models of a
theatre bank are flat patches), widescreen, longer reach, and a held-out comparison with the original frame
(`F117_PRESENT_SHOTS`). The machine's execution hooks (`remaster_live.c`, on branch `p3-remaster-live`) read the game without matched code. Remastered stays at 0% until a renderer is in the app (`--presentation remastered`, the
settings entry).

Agreed design: Remastered draws from state with its own rasteriser at the output size and its own frame rate
(interpolate between the two latest steps, up to one step of display lag; extrapolate as an option); the simulation is
untouched and checked equal across presentations. Higher step rates (30 or 60 a second) are an unvalidated experiment:
the S-dependent fixes (D1TTL, D1PROX, D1ACCEL) would need redoing.

## Sound

See [sound-board.md](sound-board.md): the page, the harness, the id census, the owner's rule (speech Mac then PC speaker;
music Mac, Roland, AdLib; sounds Mac then AdLib; overridable; Amiga dropped) and the hybrid design and its tradeoffs.
The generated sweeps and rendered clips are in the session folder
`C:\Users\Tideg\f117-recomp-local\p3-session-29oct\` (the scratchpad is temporary); the game copy used for runs is not
saved: copy `D:\GOG\F-117A` and never run from the install itself.

**In the builds tested here no VGAME matched routine dispatched** (the run's per-routine counters: a seeded full flight,
`airair_type5`, ran none; the VGAME.EXE hash matches the table's, so the cause is not established). A hook that lives in
`matched.c` or in `observe.c` called from it therefore could not be tested. Host hooks went into the machine layer
(`machine_t.exec_hook`); the alternative is the generated-code build (`tools/build_recomp.py`, hours).

## Planned: asset library and setup wizard (owner-approved, 30 October 2026)

One library folder with a subfolder each for the DOS game, the Mac sounds (converted to WAV at import) and the Roland
ROMs. A first-run wizard detects a GOG or Steam install or a GOG installer, copies what it needs into the library (the
app never reads the original install again), checks files by hash (as the existing install check does), and reports what
is present and what each missing piece costs. Every source is optional (the sound-source rule falls back by
availability); nothing from the Mac release or the ROMs goes into the repository; it can be re-run from Settings and has
a command-line mode for tests. Unchecked: whether a GOG installer unpacks without an extra tool, and whether Steam sells
this game. Start after hybrid sound is proven.

## Blocked or waiting

- **Hybrid sound:** done 31 Oct 2026: a real flight fires a recorded sound. The recorded takeoff of the Remastered check
  (handoff.md, item 1) with two cannon bursts added (`K 2500000000 0E` ... `8E`, Backspace), replayed with
  `--mac-sounds DIR` (DIR holds plain `NNN.wav`; the extracted files are named `306_CANNON.8.wav`, so copy them to
  numeric names, which the import wizard must do) and `F117_SOUND_LOG=1`: `[sound] id 6 (has recording)` once, the policy chose the
  Mac source and `macsound_play` started the voice (mixing itself is unit-tested; the run used the dummy audio device, so
  nobody has heard it). Since then: START and END thunks hooked, the AdLib sidecar built (`--adlib-effects`, docs/sound-board.md), and the full gate passed at `59cf216`. Still to do: listen to it, an ini key and menu row, Mac recordings for START and END ids, sidecars for the other drivers if wanted.
- **SETUP answers by injection at the DOS key reads, and the joystick answer:** [setup-autoanswer.md](setup-autoanswer.md);
  changes `src/machine/dos_files.c` (P2 gate territory), after the P2 integration.
- **Subtitles under AdLib:** the speech is OPL writes there; the detector (`src/host/subtitles.c`, from the speaker counts)
  covers Roland and PC speaker only.
- **The owner's own checks:** the menu (Ctrl+Alt+S), `--crt`, `--watch latest`, the nine speech clips, the sound page,
  a real controller or HOTAS.
- **Open question for the owner:** a 30-step-a-second experiment (halves display lag; needs a validation sweep like the one
  for 15).

## Practical notes

- Python is `py`, not `python`. A shell `sed` or heredoc turns a backslash-n in a C string into a real newline: use the
  Edit tool.
- Building in a worktree: `.\build.cmd` (the bare name is not found), `CMAKE_BUILD_PARALLEL_LEVEL=2`, and
  `-DFETCHCONTENT_SOURCE_DIR_SDL3="C:/Dev/F-117A Recomp/build/_deps/sdl3-src"`. Keep builds at BelowNormal while the other
  session gates.
- Window-less runs: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software`; `--no-record`, a copy of the
  install.

Phase 3 (host-side features and the Remastered research) is merged into `master`. The merged tree passed the full
acceptance gate during P2 batch `bx` (823 tests, 63 routes, 62 fresh interpreter sessions, all 1,638 matched routines,
both timing profiles, three gate seeds, N=1 presentation on all routes, boot N=4/9; evidence
`D:/f117-gate/p2-batch-bx-accept/`). Continue on `master`. Scoreboard: `py tools/progress.py --title`.

**Gate on `p3-remaster-live` (30 October 2026, commit `d9fc8b8`, about 33 minutes):** `tools/build_recomp.py` passed in full:
824 unit tests; all 63 routes identical between the recompiled engine and the interpreter (62 fresh interpreter sessions);
5,764,032 instruction states under each timing profile, 0 mismatching; all 1,638 matched routines at three seeds, 0
mismatching; every routes-only routine ran on a route; none ran past its event limit. That covers the `src/machine` change (the
execution hook table in `machine.h` and `pc.c`). Not run: the presentation groups of a batch acceptance (N=1 on all routes, boot
N=4/9), which the Remastered composite touches only when `--presentation remastered` is chosen. The log is kept at
`C:\Users\Tideg\f117-recomp-local\p3-session-29oct\gate_p3_remaster_live_d9fc8b8.txt`.

The hybrid sound and Remastered work on `p3-remaster-live` changes `src/machine` (`machine.h`, `pc.c`); the gate above covers it.
Everything else listed here is host-side (`src/host/`, `src/present/`, `tools/`, `docs/`, `tests/`).

## Items

| Item (weight) | Credit | What exists | What is missing |
|---|---|---|---|
| Settings menu (4) | 80% | Ctrl+Alt+S, a scrolling list of 21 options (presentation, fixes, window, fullscreen, speaker, speed, CRT, subtitles, sound driver, music and effects source, Roland output, game steps, AdLib emulation, 4:3 shape, line width, picture delay, stick dead zone and inversion, CPU engine), Choose fixes, Remap a key, Clear key remaps, Controller button as key, Clear controller keys, Watch last flight and Watch a flight; writes `f117a.ini` keeping its comments; a restart is offered (`settings.c`, `config_set/config_get`) | every option (folders and ROM paths need text entry), the joystick answer, the owner has not looked |
| CRT filter (1) | 70% | `--crt`, `crt = yes`, menu entry (`crt.c`), applied to the displayed copy only | the owner has not looked; no mask or bloom |
| Modern controls (3) | 65% | `pad-deadzone/-invert-x/-invert-y/-fire1/-fire2`, `pad-keys = 2:G,3:Left Shift` (a button acts as a key); `remap = Q:W` / `--remap` and a Remap a key screen; an on-screen key list (Ctrl+Alt+K, five pages: four from the manual, one with this program's own keys) | no real controller or HOTAS tried, a menu screen for `pad-keys`, mouse |
| Flight replays (3) | 80% | `--watch latest\|SESSION`, a chooser in the menu, `+`/`-`/`0` speed, `Ctrl+Alt+P` pause, `Ctrl+Alt+R` restart, `]`/`[` jump 30 s, `G` + digits + Enter jumps to a typed second, `--seek SECONDS` | other cameras (Remastered) |
| Accessibility (2) | 55% | keyboard remapping and its menu screen; radio subtitles (`--subtitles`, Roland and PC speaker drivers, from the speaker counts, `subtitles.c`) | AdLib speech, the PC speaker driver run, a colourblind HUD (Remastered) |
| Fan patches (2) | 100% | debugcom's patch (damsonn) is fix D4; a web search found no other | none, unless the owner finds another |
| Two settings (2) | 50% | Fixes Original / Recommended / Custom (`--fixes custom`, the Choose fixes screen), Presentation Original / Enhanced, saves tagged with their fix set | the Remastered presentation (still refused) |
| Sound device (1) | 80% | `--sound adlib\|roland\|speaker`, `sound =`, a menu row: types SETUP's answers ([setup-autoanswer.md](setup-autoanswer.md)) | a hybrid choice, joystick yes |
| Remastered (10) | 0% | `docs/remastered-design.md`; decoders for the terrain, world and model files, checked against the game's memory; an offline depth-buffer model sheet; the R3 plan | the renderer in the app (see "R3 plan revised") |
| Hybrid sound (3) | 0% | the sound board page, the sweep tool, the id census, the owner's rule; on branch `p3-hybrid-sound` (unmerged): the source policy, a Mac recording player, a machine-layer hook ([sound-board.md](sound-board.md)) | a flight that fires a recorded sound, the Roland/AdLib split (a second driver), START and END sounds, the gate |

Diagnostics: `F117_MENU_AUTOTEST=1` and `F117_MENU_SHOT=FILE.ppm` (window-less menu run). Tests: `ctest -R host_` (settings
menu, CRT, controls, remap, key list, subtitles (needs `F117_SPEECH_117`), config, sound policy, Mac sound).

## Branches (30 October 2026)

- `p3-remaster-live` (includes `p3-hybrid-sound` and the docs pass): the machine's execution-hook table (`machine_t.exec_hook`,
  `machine.h`, `pc.c`: the one change under `src/machine`, needs the full gate), the hybrid sound player, and
  `src/present/remaster_live.c`, which draws a recorded flight's frame from the running game
  (`F117_REMASTER_FRAME=N:FILE.ppm`, beside it the original's last picture as `FILE.ppm.orig.ppm`; `F117_REMASTER_DEBUG=1`
  prints the view origin and the band state). Checked: the machine-loop parity (one recorded flight, hook on and off:
  identical exit clock and save files), the rasteriser tests (polygons, lines, dots).
- `docs-pass` and `p3-hybrid-sound`: older tips of the same work; `p3-remaster` is merged to master.

## Remastered: what the live frame does, and the app wiring still to do

The first live frame (recorded takeoff, frame 150) matches the original's horizon, hangars, antennas, runway centre dots and
dusk colours. Missing from it: the face shade in the colours, the runway edge strips close to the eye, hangar detail.
The HUD, cockpit and radar are the original's 2D overlays and are not in it.

**Wiring (31 October 2026, merged into `master`):** `--presentation remastered` is accepted (`src/host/main.c`, and a row in the
settings menu). It is the Enhanced picture with the 3-D view redrawn by `src/present/remaster_live.c`: the model requests of each
frame are captured at the machine's exec hooks, and the redrawn view replaces the fine picture's pixels whose origin tag is a scene
tag, so the original's 2-D layers (cockpit, state-driven HUD, text) stay as Enhanced draws them. Not done: a run of this path in a
real flight compared with the Enhanced baseline (takeoff replay, `--presentation remastered --present-scale 3`, `py tools/remaster_diff.py
ENHANCED.ppm REMASTERED.ppm`); interpolation between the two latest steps; widescreen and longer reach; the fit rule for the cockpit and HUD
(see `docs/remastered-design.md`). The Remastered item stays at 0.0 in `docs/progress.json` until the owner decides.

## Remastered: R1 and R2 done, R3 next

Design and progress: [remastered-design.md](remastered-design.md). Decoders (`tools/world_decode.py`, `world_check.py`)
read `*.3DG`, `*.3DT` and `*.WLD` from the layouts START's matched loaders use (`matched.c`, START `0x07D71` and
`0x07E88`); every byte of all nine theatres is accounted for, and LB, NC, PG and VN match START's memory (VN but six
runtime-written bytes). The mesh decoder decodes all 3,936 (model x level) cases of the 11 `*.3DX` banks
(`tools/model_census.c`); real models draw correctly with a depth buffer (`tools/model_sheet.c`). The projection
(`remaster_project.c`) and the depth-correct triangle fill (`remaster_raster.c`) are used by `remaster_live.c`, which `--presentation remastered` runs.

To dump START's memory for a check: `py tools/run_route.py tools/routes/<route>.args --data <copy of the install> --out
<dir> -- --steps <N> --dump 0:100000`, N just after START opens `<stem>.3dT`.

**Next (R3 proper):** read the prepared pose from the running game rather than rebuilding it (the game prepares part
matrices, translations, eye, light and palette before projecting; see "R3 plan revised" in the design doc), then the
camera chain with sub-pixel output, terrain from the tile maps and the ground-patch models (the first ~30 models of a
theatre bank are flat patches), widescreen, longer reach, and a held-out comparison with the original frame
(`F117_PRESENT_SHOTS`). The machine's execution hooks (`remaster_live.c`, on branch `p3-remaster-live`) read the game without matched code. Remastered stays at 0% until a renderer is in the app (`--presentation remastered`, the
settings entry).

Agreed design: Remastered draws from state with its own rasteriser at the output size and its own frame rate
(interpolate between the two latest steps, up to one step of display lag; extrapolate as an option); the simulation is
untouched and checked equal across presentations. Higher step rates (30 or 60 a second) are an unvalidated experiment:
the S-dependent fixes (D1TTL, D1PROX, D1ACCEL) would need redoing.

## Sound

See [sound-board.md](sound-board.md): the page, the harness, the id census, the owner's rule (speech Mac then PC speaker;
music Mac, Roland, AdLib; sounds Mac then AdLib; overridable; Amiga dropped) and the hybrid design and its tradeoffs.
The generated sweeps and rendered clips are in the session folder
`C:\Users\Tideg\f117-recomp-local\p3-session-29oct\` (the scratchpad is temporary); the game copy used for runs is not
saved: copy `D:\GOG\F-117A` and never run from the install itself.

**In the builds tested here no VGAME matched routine dispatched** (the run's per-routine counters: a seeded full flight,
`airair_type5`, ran none; the VGAME.EXE hash matches the table's, so the cause is not established). A hook that lives in
`matched.c` or in `observe.c` called from it therefore could not be tested. Host hooks went into the machine layer
(`machine_t.exec_hook`); the alternative is the generated-code build (`tools/build_recomp.py`, hours).

## Planned: asset library and setup wizard (owner-approved, 30 October 2026)

One library folder with a subfolder each for the DOS game, the Mac sounds (converted to WAV at import) and the Roland
ROMs. A first-run wizard detects a GOG or Steam install or a GOG installer, copies what it needs into the library (the
app never reads the original install again), checks files by hash (as the existing install check does), and reports what
is present and what each missing piece costs. Every source is optional (the sound-source rule falls back by
availability); nothing from the Mac release or the ROMs goes into the repository; it can be re-run from Settings and has
a command-line mode for tests. Unchecked: whether a GOG installer unpacks without an extra tool, and whether Steam sells
this game. Start after hybrid sound is proven.

## Blocked or waiting

- **Hybrid sound:** done 31 Oct 2026: a real flight fires a recorded sound. The recorded takeoff of the Remastered check
  (handoff.md, item 1) with two cannon bursts added (`K 2500000000 0E` ... `8E`, Backspace), replayed with
  `--mac-sounds DIR` (DIR holds plain `NNN.wav`; the extracted files are named `306_CANNON.8.wav`, so copy them to
  numeric names, which the import wizard must do) and `F117_SOUND_LOG=1`: `[sound] id 6 (has recording)` once, the policy chose the
  Mac source and `macsound_play` started the voice (mixing itself is unit-tested; the run used the dummy audio device, so
  nobody has heard it). Since then: START and END thunks hooked, the AdLib sidecar built (`--adlib-effects`, docs/sound-board.md), and the full gate passed at `59cf216`. Still to do: listen to it, an ini key and menu row, Mac recordings for START and END ids, sidecars for the other drivers if wanted.
- **SETUP answers by injection at the DOS key reads, and the joystick answer:** [setup-autoanswer.md](setup-autoanswer.md);
  changes `src/machine/dos_files.c` (P2 gate territory), after the P2 integration.
- **Subtitles under AdLib:** the speech is OPL writes there; the detector (`src/host/subtitles.c`, from the speaker counts)
  covers Roland and PC speaker only.
- **The owner's own checks:** the menu (Ctrl+Alt+S), `--crt`, `--watch latest`, the nine speech clips, the sound page,
  a real controller or HOTAS.
- **Open question for the owner:** a 30-step-a-second experiment (halves display lag; needs a validation sweep like the one
  for 15).

## Practical notes

- Python is `py`, not `python`. A shell `sed` or heredoc turns a backslash-n in a C string into a real newline: use the
  Edit tool.
- Building in a worktree: `.\build.cmd` (the bare name is not found), `CMAKE_BUILD_PARALLEL_LEVEL=2`, and
  `-DFETCHCONTENT_SOURCE_DIR_SDL3="C:/Dev/F-117A Recomp/build/_deps/sdl3-src"`. Keep builds at BelowNormal while the other
  session gates.
- Window-less runs: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software`; `--no-record`, a copy of the
  install.

## 2 Nov 11:10 update: the five issues, honest state

1. Jets in margins: fixed in code (R20), awaiting the owner's look.
2. Grey line / flagpole: `line_behind_surface` added (R17 row); the flagpole tie (R18, grey pole line vs dark-red flag line meeting at one vertex) is NOT fixed. Next: a line-vs-line tie rule so the later-drawn flag wins.
3. R9 / 5. Draw distance: aircraft model reach doubled; scenery unchanged. `F117_WIDE_FAR=N` (rerun.c, off by default) writes the far plane `[48E6]` in the copy for the experiment; untested on airborne frames. The walker's ring sizes are code shifts and tile limits, not data.
4. Tower spinner: fine.
"What else might we be missing": unsurveyed: per-ring object lists `[E330+...]`, `compose_draw_model` range tests (0xBC0B/0xBC1B), other B5DC-style tests.

## 2 Nov 11:40: scenery draw distance measured

`F117_WIDE_FAR` (far plane in the copy: 24000 and 30000 against the game's 13332) on the owner's flight 235935 to clock 11.3 G: mean drawn values per rerun copy 253897 (default), 252950 (24000), 243299 (30000). A farther plane draws nothing more (30000 draws less), so the far plane is not what limits scenery. What limits it is the terrain walker `04E6`: the ring count and sizes are shifts in code of routine `0810` and tile-map limits `[0506+2*ring]`, not data. Raising scenery distance needs a longer walk in the render copy (a new walker, or extra rings visited from a copy's own call), which is work in `matched.c`/machine territory the P2 session owns: ask before touching. Not done.

Culling by centre (2 Nov, owner asked): the object cull tests only the object's centre plus a per-detail-class side allowance (`[E56A]`, 18000 in the copy), not its extent; a near, wide object (hangar, tower) with its centre outside the limit can vanish while partly on screen (the owner saw one vanish taxiing close). A fix needs the model's extent (half-width) added to the allowance.

Horizon line and flagpole (commit db91ae2): far lines (> 12000 long) never taken whole; dark lines drawn whole over earlier bright ones. Neither verified on the owner's exact moments; the grey bar across jets on the flight frames is the HUD's own horizon bar (also in front in the original).

## 2 Nov 12:00: near plane by class, far-line rule withdrawn

- The 11:10 "far lines never taken whole" rule is WITHDRAWN: the diagnostic showed it firing on ground lines of models 11 and 30 (coordinates in large fixed-point units), not on a horizon; a length threshold is a guess. The principled reading: the original draws the horizon bands first, the scene next, the HUD last (its grey horizon bar is in front in the original). The owner's bluish line through taxiing jets is still unexplained; it needs a clock from that moment (then F117_REMASTER_PROBE_BOX lists the primitives under those pixels).
- Cull by extent: in the rerun copy the near plane `[48F6+2*class]` moves back by the class's own side allowance (the game's measure of how far the class reaches; capped 12000), so a hangar whose centre is behind the plane but whose near half is in view stays. `F117_WIDE_NEAR=0` turns it off. Per class, not per model; a per-model extent would need the model header's size.
- Scenery distance: `[0506+2*level]` is the map size, not a view limit. Ring reach is in the walker's code and sector tables `[034C]`. The owner's instruction (2 Nov): do not change matched code; override safely for Remastered only. Proposed: in the rerun copy, a second walk over an outer ring (extra cells farther out in the copy's own data), not done.

## 2 Nov 12:15: what is left, and the next lead for draw distance

Left open (owner to judge in ONTests, exe hash 820EDBEB...): jets in margins (R20), near-plane by class (taxiing close), flag-over-pole rule. Still open: the bluish line through taxiing jets (needs a clock; probe the pixels with F117_REMASTER_PROBE_BOX), scenery pop-in.
Next lead for pop-in: not the far plane, not `[0506]` (map size), not the ring words. `1039:011A` (detail level) feeds `[48C4]`, a shade (fog) level, not a visibility test. Candidates not yet checked: (1) the per-tile object lists `[E330+2*(ring*32+tile)]` are filled by the game's logic around the player, so far scenery may not exist in the copy's data until logic loads it (then only a logic change could bring it earlier: P2 territory); (2) ring enables `[04FA+2*ring]`. Test (1) by dumping the list counts `[061E+...]` at a clock where the owner saw pop-in and comparing with the world's objects there.

Lead (1) checked (2 Nov 12:25, owner's flight 235935, RAM dump at 10.4 G, VGAME DS 0x37AE): the per-tile object lists are populated for all four rings (counts at `[061E+2*(ring*32+tile)]`: ring 1 about 1-11 per tile, rings 2-3 5-9, ring 4 one per tile on about 20 tiles), the ring enables `[04FA+2*ring]` are all 1, `[990E]` = 3. So the far scenery's data exists in the copy; it is not drawn because the walker visits at most 16 cells a sector in a cone ahead and E3AC/E3BF cull them (cone, far plane). The next experiment is to log, per rerun copy, which ring-4 cells the walker visits (`E3AC` entries and results) against the 20 populated tiles, at a clock the owner names as pop-in.

Reach found (2 Nov 12:35, from the same dump): the walker's sector table `[034C]` holds cell offsets of only -1..2 (nine cells a sector, the second axis from the sector two along), and `STEP` stops at 9: each ring visits a 3x3 neighbourhood of its own cells around the viewer's cell. Reach is therefore about 1.5-2 cells of the ring's size, and ring 4 (the coarsest) is the limit of scenery distance. Scaling the table's offsets would leave holes (every other cell); more rings or more steps are code. The workable override for Remastered only: a second walk in the rerun copy whose viewer cell (`BASE_X`/`BASE_Y`, stack words -0x16/-0x18 of `04E6`) is displaced one ring-4 cell ahead along the heading, so the cells 3-4 away are visited too (duplicates are harmless under the depth test). It must be an exec hook in `src/present/rerun.c` on `04E6` (call it twice in the copy), not an edit to `matched.c`. Not built.

## 2 Nov 12:05: owner's round of pictures, and what changed

ONTests exe hash EA7C38B1F1BA... (clean P3 branch, 832/832).
- Flagpole (R18): lines now follow the original's draw order among themselves (`line_order` in remaster_raster: a line tests against the polygons' depth only and writes no depth), so a later line covers an earlier one: the flag is on top of its pole in the flight frames. Side effect seen: a light at a pole's foot is partly covered by the pole where the original's order puts the pole later.
- Jets close in the margins (R20): the aircraft cone in the copy is now half again wider than the canvas (the test is on the jet's centre). Not yet confirmed by the owner.
- Grey line: the bar between the two "00" marks is the HUD's, in front in the original too (ONShots `20261102-1200-horizon-bar-original-top-remastered-bottom.png`). The owner says theirs is not the HUD: needs its clock. The bluish strip by a taxiing jet is likely the sea band at the horizon; not reproduced.
- Hangar "glitches" (the owner's picture: yellow T shapes at the doors): the original draws the door's yellow vertical and its foot line too; compare at the owner's clock before changing.
- Scenery distance: a second terrain walk in the copy (nested run from an exec hook at 04E6, sector table moved 3 cells ahead, rings 1-3 off) was built and REMOVED: it drew no more (mean values 254241 vs 254720) and 45 nested runs did not return within their slice. Pop-in is still open.

## 2 Nov 12:25: ONTests 5978ECAD5FE6 (clean P3 branch, 832/832), awaiting the owner's Ctrl+Alt+C shots

- New host key Ctrl+Alt+C: `screenshots/<session>-clock<icount>.bmp` beside `sessions`; replay that session to the clock to reproduce.
- The grey line is scene geometry at the horizon toward the runway's end (owner's GOG shots: behind the jet), not the HUD. Whole lines now follow the original's painting order (`line_by_seq`, `seq` buffer in remaster_raster): over polygons painted before them, behind nearer polygons painted after them. Not yet seen on the owner's moment.
- HUD pass order measured (`F117_HUD_ORDER`): every model of a frame is asked for before the HUD pass.
- Draw distance: unchanged; `F117_WIDE_SCENERY=N` (second walk, uncapped nested run, far plane 30000 for it) is an experiment, off by default.

## 2 Nov 13:05: handoff (start here for the next conversation)

**State.** ONTests had B9FE1AAB25B6 at 12:58 (clean P3 branch `p3-gate` in `C:\Users\Tideg\f117-recomp-local\gate-master`, 832/832): decal lines, the ground rule, Tab screenshots, aircraft cone 3x. The aircraft-size change below is in master; check whether ONTests was refreshed after it (hash in the last commit message or `Get-FileHash`).

**What changed today (Remastered only, all in `src/present`):**
- Screenshots: Tab (menu closed) or Ctrl+Alt+C saves `screenshots/<session>-clock<icount>.bmp` beside `sessions`. Replay that session to the clock to reproduce (`F117_PRESENT_SHOTS=prefix F117_PRESENT_SHOTS_FROM=clock F117_PRESENT_SHOTS_COUNT=1`, `F117_WINDOW=1920x1080`, `--present-scale 3`).
- Depth model (remaster_raster.c, `owner` buffer = request serial << 12 | face, bit 11 = ground object): lines and dots are decals on their own face (always over it; over their own object's other faces unless 1% nearer; behind other objects that are 3% nearer). Ground objects (every polygon level, world axis 1 up) never hide what stands on them and cover it only where 3% nearer. `F117_LINES=old` restores the old rules (50x bias); `F117_DECAL_TOL`, `F117_FLAT_AXIS`, `F117_FLAT_SHOW=1` (level polygons magenta) are diagnostics.
- Aircraft (rerun.c `wide_air_exec`): cone three times the canvas. Size-aware test ON (`F117_AIR_RADIUS`, default 800 units, 0: off): for the aircraft walk's call to `B5DC` (return 9BDF), a jet whose centre is level with the eye or up to the radius behind it is tested at a point 16 units in front (offset pairing from B792: B082+4 with x = C0D0-x, B088+4 with alt = (alt-2DF4)>>5, B08E+4 with y = y-C0DE; z' negative in front). Jets in front are untouched, because the same depth picks model or dot ([DEBE] << cl > -32: model); moving every jet made near ones dots (first two attempts, ONShots `20261102-1305-jet-size-*`). Checked on session 20261102-122500 clocks 6.42-6.70 G: identical to off (that jet never vanished there). NOT yet seen on the owner's vanishing moment: needs their Tab shot of it. Next: the same size-aware test for every object (general cull E3AC/E3BF with each decoded model's radius), which the owner asked for.
- Near plane per class moved back (R16), flags over poles, HUD-pass order diagnostic (`F117_HUD_ORDER`: every model is asked for before the HUD pass).

**Owner's open items (2 Nov 13:00):**
1. Jets vanish too early when very close in the margins: size-aware aircraft test in (see above), unconfirmed on the owner's moment. The owner wants size-aware visibility for all objects.
2. Grey/green horizon line through jets: decals fixed the hangars (clock 2555000000); at 6172000000 the line runs above the jet's back in my replay; the owner still sees it crossing (their image at that moment). Compare original vs Remastered there.
3. Lights "peeking through" a jet (same moment): likely the same question.
4. Hangar door bottoms covered by the ground: ground rule, owner to check.
5. Runway over a parked jet: owner's shot was not saved (maybe a replay: `replay-clock*.bmp` in the working folder); clock 6392270028 is a candidate.
6. Draw distance (scenery pop-in): unchanged. The walker visits a 3x3 cell neighbourhood per ring; `F117_WIDE_SCENERY=N` (second walk) drew nothing more. Needs a new design.
7. The owner asked whether Remastered is too faithful to the original's renderer hacks: agreed direction is own depth tests with decal lines (done), and object-size-aware visibility (aircraft started; scenery objects still centre-based with the per-class near plane).

Owner preference this session: take screenshots in the replay at the owner's clocks and compare before claiming a fix; send comparison images (ONShots) with the claim.

## 2 Nov: screenshot reliability

Fixed host screenshot dispatch in `src/host/main.c`: Tab and Ctrl+Alt+C work during replay as well as live play; the screenshots folder is initialized even with explicit recording, replay or --no-record. Previously these modes could save in the working directory, and replay Tab was ignored. Each non-repeat key press is queued; paused captures at the same clock receive numbered suffixes instead of overwriting. A 2.5-second overlay confirms success or failure, with the SDL error in the console on failure.

Validation: f117a rebuilt successfully; `F117_SHOT_AUTOTEST=1` under SDL dummy video/audio exercised two Tab presses in one frame, another Tab, Ctrl+Alt+C, and a held-key repeat while paused. Both live --no-record and a replay of session-20261102-135147 produced four distinct 768,054-byte BMPs at one clock, ignored the repeat, and exited 0. host_settings_menu passed. No full machine gate run for this host-only change.

## 2 Nov: R17/R21/R22 depth ordering, owner's screenshots from session 141108

The gray line through the transport (`3966919746`), tower and parked jets (`4878976848`, `5045987490`), and blue buildings (`7289133048`) had a common depth-unit defect. `remaster_live.c` now observes `compose_emit_object` (`E2B6`) read-only, retaining each placement's terrain ring and the native scale shift of `compose_draw_model` (caller return `BC59`, adjusted BP+16h). Its model/bank/relative position identifies the later geometry request, including deferred objects. Inverse depth is multiplied by `2^(2*(4-ring)+model_shift)`; screen coordinates and guest state are unchanged. Solid, logical, shaded and line raster paths all use common inverse depth.

The yellow lines from behind the hangars required a separate compositor fix: original outline tags were kept over ground where the new scene had no polygon depth, resurrecting the old, displaced line after Remastered correctly hid it. Outline tags are now replaced throughout the 3D window. A finite-width hidden line's fringe is also checked at its centre and against nearby coverage of its own object, only for uncovered stroke pixels. Same-face ownership no longer exempts a clearly farther line from depth testing.

Evidence: `D:/f117-gate/depth-20261102/` and `Desktop/ONShots/20261102-depth-order/`. The rear hangar-base regions at clocks `10452330792` and `11055367032` contained 152 and 192 yellow pixels before, zero after; the visible front door markings remain at `3522889584`. The transport's crossing gray line is gone and the overlapping parked jet draws in front of the tower base. All seven owner clocks were replayed. Use `--adlib-effects` for these recordings from ON-roland.bat: without its hybrid sound routing the replay diverges in the front end. Controlled comparison captures use `F117_PRESENT_THREADS_OFF=1` to avoid the asynchronous presenter's frame latency.

Validation: f117a and the six focused presentation test targets built; `ctest -R presentation_(remaster|scene|rerun)` passed 6/6. Added regressions for mixed native depth units, same-face hidden lines, and hidden stroke fringes with visible front strokes preserved. No full machine gate for these presentation-only changes. R17, R21 and R22 await the owner's check; do not call them owner-confirmed.
ONTests refreshed after checking it was closed: SHA-256 `92D84AE3BE88B3E62404946A0126EA675B3D90A5E804EB552C101680B5613064`. The seven before/after image pairs are in `C:/Users/Tideg/Desktop/ONShots/20261102-depth-order/index.html`.


## 2 Nov: light flicker, flag junctions and menu overflow

The earlier point/edge depth work alone left the visible roof-light stripe. Raw scene and final-frame comparison at 3522889584 exposed the second cause: lines keep polygon depth read-only, and the composite required polygon depth when replacing original model pixels. An original polygon row therefore survived through a correctly drawn light above the new roof silhouette. Stroke coverage now also authorizes replacement of scene pixels; HUD and cockpit tags retain their existing rules.

Point decorations test their centre against their own surface, reconstructing screen-space inverse depth from same-face neighbours. This keeps a billboard whole on a sloping roof or fin, while truly hidden points remain hidden. Optional stroke buffers resolve same-object point/edge competition at the point centre; ordinary finite strokes retain painter order. Ground marking exceptions are retained. Tests cover both stroke orders, steep slopes through sub-pixel motion, and opaque occluders.

Finite strokes now include square endpoint caps, matching the original line routine's inclusive endpoint pixels. This fixes short flag/pole junctions generally, with no model-specific flag rule. Inverse depth extends linearly through the caps, preserving depth at the original vertices. A regression checks the joined strokes through 16 sub-pixel phases.

All seven owner screenshot clocks replayed with the final candidate; the rear yellow lines remain absent at 10452330792 and 11055367032, and visible front door markings remain. Two moving sequences (hangars and transport) and two frames from session 144914 checked. Four-thread replay comparison logged at least 121 pictures with zero differing pictures against one thread. Eight focused presentation/menu tests pass. No full machine gate run for these presentation/host changes.

The tilde menu top bar is now 560 pixels wide, matching the key panel; shortcut text is aligned to its right inset rather than overflowing a 400-pixel strip.

Evidence: D:/f117-gate/lights-20261102/. Owner gallery: C:/Users/Tideg/Desktop/ONShots/20261102-lights-and-flags/index.html. The gallery includes Original/Remastered flag crops, seven before/after views, and moving comparisons. These fixes await the owner's visual check.

ONTests updated after confirming it was closed: SHA-256 6C243D476FEF9690DA73FA17734D6214638151F55DD37F34D5732C1F9E4929E9. The previous executable is preserved in D:/f117-gate/lights-20261102/installed-before-update.exe.


## 2 Nov follow-up: coastline and zero-thrust sound

Work resumed at the owner's request after the handoff-only feedback update. Evidence is D:/f117-gate/coast-20261102/; session 20261102-154150 was replayed with --adlib-effects and private save copies.

**Margin cutoff:** ground and sea polygons were clipped at the same native near plane (0x10000) as buildings. At low eye height, the clipped floor ends inside the unusually tall side margins, exposing brown background below water. Flat-ground primitives now use 0x1000 in both polygon and line passes; buildings retain 0x10000. Solid and palette-OR regression cases cover this low-camera floor. At 16551899976 the old horizontal brown strip is absent with the fix. At 16283883318 the rectangular cutoff is removed, while the genuine sloping coast remains. Geometry inspection found no captured water face containing the camera's vertical ground projection at 16283883318, and one at 16551899976. Thus these two frames must not be treated as the same side of the crossing.

**Hangar/coast alignment:** matched Original-presentation captures at 12391636686 and 16283883318 also show the coastline relationship reported by the owner. This is evidence it is not introduced solely by Remastered composition, but NOT proof of the GOG baseline. The hangar/coast spatial concern remains open pending that comparison; do not mask it by extending ocean over genuine land. An experiment relaxing coplanar logic-fill depth changed no pixels and was removed. F117_REMASTER_PROBE_VERTICES=1 now includes polygon vertices in the existing probe diagnostic.

**Sound (updated after owner confirmed GOG uses AdLib/Sound Blaster):** two hybrid-routing defects explain the missing continuous engine feed. Request 23 carries airspeed as a second stack word (sound stub SS:SP+6, driver frame BP+8); the sidecar originally forwarded only the ID. More importantly, the per-frame speed calls bypass the library slot watched by the original host hook. The initial argument-only fix therefore did not complete the repair. The host now watches the library slot's bound driver entry and mirrors request 23 into the sidecar with its speed word, leaving the native call, state and timing intact. Other requests retain existing routing, and a speed request swallowed at the library slot cannot reach the driver hook twice.

The real driver probe now verifies sustained engine voice under ongoing nonzero speed updates and release only after explicit engine-off command 25. Calling 25 is NOT equivalent to simply decrementing throttle to zero: the owner's session 154150 uses ordinary minus presses, which do not themselves send it. The earlier interpretation of the isolated command-25 test as proof that zero thrust should always silence AdLib was too broad and is corrected here. The actual replay logs 2,820 speed updates, including 100 at zero selected thrust and nonzero speed during clocks 11981401230..12391636686 (20 down to 15 knots). No gated engine-off command occurs in that interval. This is the sequence the sidecar previously missed. The fix awaits the owner's listening check; no fabricated idle loop was added. GOG driver choice is confirmed by the owner; independent GOG PCM capture has not been made.

**Validation:** ten focused menu/sound/presentation CTests pass, including the new solid/OR floor clipping regression; sidecar_probe ASOUND.117 6 passes its new speed and native zero-thrust checks. Replays of 3522889584 and 11055367032 are pixel-identical to the installed d748bdc build (previous light/flag/hidden-yellow-strip fixes retained). No full machine gate run. Latest work is not committed yet; unrelated sound-board tool edits remain untouched.

Four-thread verification on the low-camera coastline replay logged at least 481 pictures, zero differing pictures against one thread. ONTests was closed before updating it to SHA-256 `74D0E2C823DB09D174456F849896C9E724D03E950C3779FC52A359231E387A58`, verified equal to build/f117a.exe. The prior d748bdc executable is preserved as D:/f117-gate/coast-20261102/f117a.exe. Coastline before/after and Original comparison gallery: C:/Users/Tideg/Desktop/ONShots/20261102-coastline/index.html. No commit or push was made during this resumed work.


**Completed speed-hook validation:** actual replay sound log is `D:/f117-gate/coast-20261102/original-sound-check-12391636686.log`; `sound-ram.bin` (before the new hook) and `sound-after-ram.bin` have identical SHA-256 `c1a431d6b9da36eaa9a2791aaa37be35661ce79bc7958d897f9956dfb796f5fe`. Ten focused tests and the extended real-driver probe pass. ONTests was closed and refreshed to SHA-256 `507D03C37566D8E64852625C5BC5814360C57B82C4A427258F5CDA38155F1516`, verified against build/f117a.exe. Its previous executable is saved as `D:/f117-gate/coast-20261102/before-driver-speed-hook.exe`. Earlier installed hashes above are historical. These resumed changes remain uncommitted.


## 3 Nov: other effects that bypass the sound stub

The S10 repair mirrored only request 23 at the driver's bound entry. Asked whether other effects could bypass the stub too, I audited the owner's flight (session 20261103-145810, to clock 7.8 billion, `F117_SOUND_LOG=1`): the stub saw 31 requests (ids 0, 2, 3, 10, 11, 24, 26, 28, 31, 35); the driver entry saw 1,412 speed updates (23) and one id 19 (a key-triggered effect, clock 4,010,527,871) that the stub never saw, so the AdLib sidecar never played it.

Fix (`src/host/main.c`, `on_exec_hook`): any effect id 4 to 35 reaching the driver entry is mirrored into the sidecar when the policy gives effects to AdLib, except one the stub itself just let through (remembered by id and clock, 20,000-instruction window), so nothing plays twice. Rerun of the same replay: id 19 reaches the sidecar once at the same clock; every other id's count is unchanged (26 x5, 31 x3, 0 x3, 28 x2, 24 x2, 35, 11, 10 once). Only one bypassing id appeared in this flight; effects that bypass in situations it did not exercise are not covered by evidence. `F117_SOUND_LOG` now also logs driver-entry ids and `[sidecar]` requests. Evidence: `private/scenery-20261103/sound-audit/`. 256 sound and host tests pass. No full gate; awaits the owner's listening check with S10.


## 3 Nov, evening: adjustable draw distance (Remastered)

The owner asked for adjustable, effectively infinite draw distance that respects the original code, and said every ground element counts (the lit dot rows
are buildings; "don't leave anything out"). Done in the render copy only: the extra terrain walks feed the walker explicit cells on every ring out to the
16-bit limit of the guest's cell offsets, filtered natively by the map (exact: pixel-identical to the unfiltered walk). Setting: Draw distance in the menu
(Maximum, Far, Near, Original; live), `--draw-distance`, `draw-distance =` in the ini. Design, evidence and cost: [scenery-distance.md](scenery-distance.md).

Open: Maximum costs about 15 pictures a second on the owner's runway replay (54 down to 38 at 1920 x 1080, software output), so 60 fps is not met with it
on; a cone filter was tried and removed (not exact). Ring 1 cannot pass the 16-bit limit (28,672 units). Also this evening: the Keys tab remaps keys and
controller buttons (Settings no longer lists those rows); request ids that bypass the sound stub (id 19) are mirrored into the AdLib sidecar; R18, R23 and
R24 are owner-confirmed. Not gated; presentation and host changes only. Owner to judge Maximum, Far and Near in ONTests.
