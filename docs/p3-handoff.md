# Phase 3 handoff (updated 30 October 2026)

Phase 3 (host-side features and the Remastered research) is merged into `master`. The merged tree passed the full
acceptance gate during P2 batch `bx` (823 tests, 63 routes, 62 fresh interpreter sessions, all 1,638 matched routines,
both timing profiles, three gate seeds, N=1 presentation on all routes, boot N=4/9; evidence
`D:/f117-gate/p2-batch-bx-accept/`). Continue on `master`. Scoreboard: `py tools/progress.py --title`.

The hybrid sound work on `p3-hybrid-sound` changes `src/machine` (`machine.h`, `pc.c`), so it needs the full gate before it
merges. Everything else listed here is host-side (`src/host/`, `tools/`, `docs/`, `tests/`).

## Items

| Item (weight) | Credit | What exists | What is missing |
|---|---|---|---|
| Settings menu (4) | 60% | Ctrl+Alt+S, a scrolling list of 19 options (presentation, fixes, window, fullscreen, speaker, speed, CRT, subtitles, sound driver, Roland output, game steps, AdLib emulation, 4:3 shape, line width, picture delay, stick dead zone and inversion, CPU engine), Choose fixes, Remap a key, Clear key remaps, Watch last flight and Watch a flight; writes `f117a.ini` keeping its comments; a restart is offered (`settings.c`, `config_set/config_get`) | every option (folders and ROM paths need text entry), the joystick answer, the owner has not looked |
| CRT filter (1) | 70% | `--crt`, `crt = yes`, menu entry (`crt.c`), applied to the displayed copy only | the owner has not looked; no mask or bloom |
| Modern controls (3) | 55% | `pad-deadzone/-invert-x/-invert-y/-fire1/-fire2`; `remap = Q:W` / `--remap` and a Remap a key screen; an on-screen key list (Ctrl+Alt+K, four pages from the manual) | no real controller or HOTAS tried, controller-button remapping, mouse |
| Flight replays (3) | 75% | `--watch latest\|SESSION`, a chooser in the menu, `+`/`-`/`0` speed, `Ctrl+Alt+P` pause, `Ctrl+Alt+R` restart, `]`/`[` jump 30 s, `--seek SECONDS` | other cameras (Remastered), jumping to a typed time |
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

**Wiring (not started):** `--presentation remastered` needs (1) a per-step capture of the frame's primitives and band state
(what `remaster_live.c` does at the per-frame entry), (2) a display loop that draws every refresh by interpolating the
camera and objects between the two latest steps, (3) the 2D layers composited over it (the cockpit and HUD from the
original's frame or from the state-driven HUD of `hud_state.h`), (4) widescreen and longer reach, (5) the settings entry.
Step 3 needs a decision on how to separate the original's 2D layers from its 3D picture; nothing here has been tried.

## Remastered: R1 and R2 done, R3 next

Design and progress: [remastered-design.md](remastered-design.md). Decoders (`tools/world_decode.py`, `world_check.py`)
read `*.3DG`, `*.3DT` and `*.WLD` from the layouts START's matched loaders use (`matched.c`, START `0x07D71` and
`0x07E88`); every byte of all nine theatres is accounted for, and LB, NC, PG and VN match START's memory (VN but six
runtime-written bytes). The mesh decoder decodes all 3,936 (model x level) cases of the 11 `*.3DX` banks
(`tools/model_census.c`); real models draw correctly with a depth buffer (`tools/model_sheet.c`). The projection
(`remaster_project.c`) and the depth-correct triangle fill (`remaster_raster.c`) are standalone and not wired into the app.

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

- **Hybrid sound:** a flight that fires a recorded sound (the cannon, id 6, Mac 306) to prove the hook plays the WAV;
  the second driver for the Roland/AdLib split; the full gate.
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
