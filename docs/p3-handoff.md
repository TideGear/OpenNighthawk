# Phase 3 working notes

The current state of the work is the top of [../handoff.md](../handoff.md) (the renderer plan, numbers and next steps) and the last section of [gpu-compositor.md](gpu-compositor.md). Scores: `py tools/progress.py`
(never copy a figure from an old note). The dated working log of 30 October to 5 November, with every owner round, screenshot clock and ONTests build, is archived as
[archive/p3-handoff-log-20261105.md](archive/p3-handoff-log-20261105.md); the 3 November checklist is [archive/p3-current-work-20261103.md](archive/p3-current-work-20261103.md). This page keeps only what is still true and useful.

## What Phase 3 holds

The Remastered renderer ([remastered-design.md](remastered-design.md), [scenery-distance.md](scenery-distance.md), [remaster-performance.md](remaster-performance.md), [coastline-reconstruction.md](coastline-reconstruction.md)), the settings menu and
Keys tab ([settings.md](settings.md), checked by [host-acceptance.md](host-acceptance.md)), hybrid sound ([sound-board.md](sound-board.md)), controllers and key remapping, radio subtitles, the CRT filter, flight replays
(`--watch`), the colour-blind aid, and the 33 normal-play fixes ([bugs.md](bugs.md)). The owner's reports are tracked one by one in [owner-issues.md](owner-issues.md): read it before changing anything visible.
What is planned and not built: the asset library and import wizard (one library folder for the DOS game, the Mac sounds converted to WAV at import and the Roland ROMs; a first-run wizard that copies and hash-checks
them; every source optional), subtitles under AdLib, a real controller or HOTAS on hardware, other cameras in replays, the CRT improvements the owner asked for (R25), and realistic lighting after the PixelWings
Cockpit & Palette mod (owner, 9 Nov 2026; [roadmap.md](roadmap.md), "Maybe: realistic lighting").

## Rules that hold

- **Original-game parity is a hard boundary.** A Remastered change belongs in the presentation override or the throwaway render copy, never in the live simulation, collision rules, original terrain data or the Original
  renderer. A shoreline or draw-distance change adds no water physics and no collision rule. Compare the simulation state across presentations after any change (the end-of-run machine hash).
- **Settings conveniences use normal guest inputs** (SETUP's answers are typed keys). New controls need a description in the menu.
- **Show, don't claim.** Take screenshots in the replay at the owner's clocks (Tab, or Ctrl+Alt+C, saves `screenshots/<session>-clock<icount>.bmp` beside `sessions`; replay that session to the clock with
  `F117_PRESENT_SHOTS=prefix F117_PRESENT_SHOTS_FROM=clock F117_PRESENT_SHOTS_COUNT=1`, `F117_WINDOW=1920x1080`, `--present-scale 3`) and compare against `--presentation original` before saying a fix works; send
  comparison images (`Desktop\ONShots`, named `YYYYMMDD-HHMMSS-name.png`) with the claim.
- **Held-key release is unaudited** across host overlays and remapping: the menu scripts open shortcuts through modifier flags without preceding physical modifier key-down events. A coverage gap to investigate before
  claiming complete controls coverage, not a verified defect.
- **Full gates** are allowed when a change needs one (the owner lifted the limit on 3 November). Host acceptance and machine parity are separate checks and both are required where they apply.

## Remastered drawing rules worth knowing (all in `src/present`)

- **Depth model** (`remaster_raster.c`; the `owner` buffer is the request serial << 12 | face, bit 11 marks a ground object): lines and dots are decals on their own face (always over it; over their own object's other
  faces unless 1% nearer; behind other objects that are 3% nearer). Ground objects (every polygon level, world axis 1 up) never hide what stands on them and cover it only where 3% nearer. Diagnostics: `F117_LINES=old`
  restores the old rules (50x bias), `F117_DECAL_TOL`, `F117_FLAT_AXIS`, `F117_FLAT_SHOW=1` (level polygons magenta).
- **Aircraft visibility** (`rerun.c`, `wide_air_exec`): a cone three times the canvas; a size-aware test (`F117_AIR_RADIUS`, default 800 units, 0 turns it off) for the aircraft walk's call to `B5DC`.
- **The original's own jitter and whole-object sort are not copied**: Remastered tests depth itself. The game's lines and lights come from projection records one logic step old, so they sit about 3 pixels from
  Remastered's (correct) placement: a flaw of the original, not an error here.

## Tools and diagnostics

`F117_WINDOW=WxH` (a wide window under the dummy driver), `F117_PRESENT_SHOTS=PREFIX` with `_FROM`, `_COUNT`, `_STEP`, `F117_DUMP_RAM=CLOCK:FILE`, `F117_LOOP_TIMING=1` (where each 2 seconds of wall time went),
`F117_PROFILE=1` (the render copy by stretch), `F117_REMASTER_PROBE=X,Y`, `F117_REMASTER_KEPT=2`, `F117_REMASTER_COPYCHECK=N`, `F117_MODEL_LOG=MODEL,CLOCK`, `F117_HUD_ORDER`, `F117_SOUND_LOG=1`,
`F117_MENU_AUTOTEST=1` with `F117_MENU_SHOT=FILE.ppm`, `--replay-fixes cli`, `tools/fix_audit.py`, `tools/verify_flights.py` (with `docs/flight-reference.json`), `tools/audit_routes.py`. Renderer timing and the two
speed guards are in [../tools/renderer_bench/README.md](../tools/renderer_bench/README.md).

**Replaying an owner flight.** A replay applies the fix list in its log header (edit the header to compare fixes) and the sound options of the session (`--adlib-effects` for the Roland bat's sessions, or it diverges). Copy
`save-start` to a private folder, then `f117a.exe --data D:/GOG/F-117A --save COPY --replay SESSION/input.log --presentation remastered --present-scale 3 --sound adlib` under `SDL_VIDEODRIVER=dummy
SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software`. The owner's sessions are in `%APPDATA%\F117Recomp\F-117A\sessions`. The runway flight `20261103-154444` needs `--config` of the ONTests ini or the plane
crashes near clock 3.54 billion (R28). Never stop `f117a` by name (the owner tests from `Desktop\ONTests`) and do not touch `ONTests` while it runs.

## Practical notes

- Python is `py`. A shell `sed` or heredoc turns a backslash-n in a C string into a real newline: use the Edit tool, or the Write tool for patch scripts, and encode a script's output before opening a file for writing.
- Build from PowerShell: `.\build.cmd`. A worktree needs `-DF117R_GEN_DIR=C:/Users/Tideg/f117-recomp-local/gen` (or the lockstep tests are not registered) and, to reuse the fetched SDL,
  `-DFETCHCONTENT_SOURCE_DIR_SDL3="C:/Dev/F-117A Recomp/build/_deps/sdl3-src"`; keep builds at BelowNormal while a gate runs. `tools/build_recomp.py` compiles the main tree, so run a gate from a clean tree.
- `tools/present_light_check.py` refuses changes to `drawfeed.c/.h` and `rerun.c/.h`: those need the full gate.
- Known, not Phase 3's: `func_lockstep` fails four sound routines (RSOUND.LOG `03A2`, RSOUND.117 `0D07` and `0ACE`, ASOUND.117 `005E`: "with N clocks of room the body declined") on a clean checkout too.
