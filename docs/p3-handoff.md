# Phase 3 handoff (updated night of 1 November 2026; the table and the branch notes further down are from 30 October)

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
`ONTests` while it runs; the P2 session's `5cd50d8` makes `code_overrides` fail and the GitHub runs red (`docs/p2-brief-ci-failure.md`).

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
