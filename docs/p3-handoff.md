# Phase 3 handoff (29 October 2026, end of session)

For the next conversation. Everything below is committed on the branch `p3-settings-menu`
(worktree `D:\f117-wt\p3-settings-menu`, pushed to `origin`), unmerged. The Phase 2 gate has not been run on it.
Scoreboard at the last commit: **P1 100.00%, P2 95.94%, P3 81.41%, All 94.06%** (`py tools/progress.py --title`).

## State of the branch

Host-side only (`src/host/`, `tools/`, `docs/`, `tests/`); no change to `src/machine/`, `src/cpu/`,
`src/matched/` or the recompiler, so parity should be untouched, but merge only after the gate passes.

| Item (weight) | Credit | What exists | What is missing |
|---|---|---|---|
| Settings menu (4) | 60% | Ctrl+Alt+S, a scrolling list of 19 options (presentation, fixes, window, fullscreen, speaker, speed, CRT, subtitles, sound driver, Roland output, game steps, AdLib emulation, 4:3 shape, line width, picture delay, stick dead zone and inversion, CPU engine), Choose fixes, Remap a key, Clear key remaps, Watch last flight and Watch a flight; writes `f117a.ini` keeping its comments; a restart is offered (`settings.c`, `config_set/config_get`) | every option (folders and ROM paths need text entry), the joystick answer, the owner has not looked |
| CRT filter (1) | 70% | `--crt`, `crt = yes`, menu entry (`crt.c`), applied to the displayed copy only | the owner has not looked; no mask or bloom |
| Modern controls (3) | 55% | `pad-deadzone/-invert-x/-invert-y/-fire1/-fire2`; `remap = Q:W` / `--remap` and a Remap a key screen; an on-screen key list (Ctrl+Alt+K, four pages from the manual) | no real controller or HOTAS tried, controller-button remapping, mouse |
| Flight replays (3) | 75% | `--watch latest\|SESSION`, a chooser in the menu, `+`/`-`/`0` speed, `Ctrl+Alt+P` pause, `Ctrl+Alt+R` restart, `]`/`[` jump 30 s, `--seek SECONDS` | other cameras (Remastered), jumping to a typed time |
| Accessibility (2) | 55% | keyboard remapping and its menu screen; radio subtitles (`--subtitles`, Roland and PC speaker drivers, from the speaker counts, `subtitles.c`) | AdLib speech, the PC speaker driver run, a colourblind HUD (Remastered) |
| Fan patches (2) | 100% | debugcom's patch (damsonn) is fix D4; a web search found no other | none, unless the owner finds another |
| Two settings (2) | 50% | Fixes Original / Recommended / Custom (`--fixes custom`, the Choose fixes screen), Presentation Original / Enhanced, saves tagged with their fix set | the Remastered presentation (still refused) |
| Sound device (1) | 80% | `--sound adlib\|roland\|speaker`, `sound =`, a menu row: types SETUP's answers (`docs/setup-autoanswer.md`) | a hybrid choice, joystick yes |
| Remastered (10) | 0% | `docs/remastered-design.md`; decoders for the terrain, world and model files, checked against the game's memory; an offline depth-buffer model sheet; the R3 plan | the renderer in the app (see "R3 plan revised") |
| Hybrid sound (3) | 0% | the sound board page, the sweep tool, the nine speech words | the owner's marks; a second driver fed the same requests |

Diagnostics added: `F117_MENU_AUTOTEST=1` and `F117_MENU_SHOT=FILE.ppm` (window-less menu run).
Tests: `ctest -R host_` in the worktree's `build` (settings menu, CRT, controls, remap, key list, subtitles (needs `F117_SPEECH_117`), config).

## Remastered: R1 done, R2 next

- `tools/world_decode.py` decodes `*.3DG` (terrain tile maps: mark `22`, then 16 + 256 + 3x512 bytes) and `*.3DT`
  (mark `11`, five list sizes, point counts, then points of three words and a word, 8 bytes each in the file)
  from the layouts START's matched loaders read (`src/matched/matched.c`, START `0x07D71` and `0x07E88`). Every byte of all
  nine theatres is accounted for. `tools/world_check.py` compares them with a memory dump: LB, NC, PG and VN match
  START's memory byte for byte (tables at `DS:B3A0..BAB0`, points after). CE, CU, KO, KU, ME decode but were not dumped.
- To dump: `py tools/run_route.py tools/routes/<route>.args --data <copy of the install> --out <dir> -- --steps <N> --dump 0:100000`,
  N just after START opens `<stem>.3dT` (the route's `run.log` shows the clock). The dump is printed as hex text.
- **R2 (30 Oct): `*.WLD` decoded and checked** (see `docs/remastered-design.md` Progress); next the meaning of target and slot fields, `*.3DX`, per-frame objects. (Old note follows.) Find START's matched loader
  (search `matched.c` near the START world loader, `lb.wld` is opened before the 3DG), decode, check against memory.
  Then the model bank `*.3DX` (offset table; `src/present/scene_mesh.c` draft decodes models).
- Design points agreed this session: Remastered draws from state with its own rasteriser at the output size and
  its own frame rate (interpolate between the two latest steps, up to one step of display lag; extrapolate as an
  option); the simulation is untouched and checked equal across presentations. Higher step rates (30/60 a second)
  are an unvalidated experiment: S-dependent fixes (D1TTL, D1PROX, D1ACCEL) would need redoing.

## Sound board (the owner asked for a reference page of all music and effects)

- **Page:** private artifact https://claude.ai/artifact/1jdGUvk4aD8bsCSxDvJbta (version 1; 38 DOS rows x AdLib, Roland,
  PC speaker, Mac; nine speech words; Mac-only and Amiga sounds; marks saved through `db`, collection `marks`,
  one doc per row key). Republish by passing the URL to the Artifact tool. Read the saved marks with `ArtifactData`
  (`list`, collection `marks`) once the owner has marked sounds: they decide hybrid sound's split. The owner's
  stated rule so far: mostly Roland for music, AdLib for everything else.
- **Tools:** `f117run --sound-sweep IDS [--sound-span N] [--sound-repeat N] [--sound-out PREFIX]` (VGAME running at
  the end of `--steps`; copies the machine per id, makes the game's request stub a bare return, calls the driver),
  `tools/sound_board_render.py` (rebase logs, `audio_render`, trim, mp3), `tools/sound_board_page.py` +
  `tools/sound_board_template.html` (one self-contained HTML, audio embedded, `--board-long` overrides ids).
- **Recipe:** `boot_to_flight` with SETUP answer 2 (AdLib), 3 (Roland), 1 (speaker) (variants in the session folder, not
  in `tools/routes`: the gate discovers every `.args` there), `--steps 2450000000`, ids 0-45, span 45000000 (5 s at 9 MIPS).
  MT-32 ROMs: `C:\Users\Tideg\f117-recomp-local\roms` (`mt32_ctrl_1_07.rom`, `mt32_pcm.rom`).
- **Open findings:** (1) the owner heard the AdLib **stall horn glitched**: one request gives a 0.11 s note, the game
  re-requests it every step, so single-request renders of held sounds (warnings 7-9, 11, 12, 14, probably 10 and
  the engine ones) are blips. `--sound-repeat` (re-request every N clocks from the game's own code with IF set) was
  added and works: ids 14, 7, 11 with repeat 700000 and span 18000000 now sustain the full 2 s (id 14: 552 OPL writes,
  against 25 for the single request); the clips are in the session folder (`board_held`), not yet heard by the owner. Next: render
  every id held in the three drivers, show a "held" button beside the single one when it is meaningfully longer.
  (2) ids 3, 36-39 reached the 5 s cap (music or loops): 20 s renders are in `board_long` (the page generator takes
  `--board-long`, not yet republished). (3) ids 41-44: the driver call did not return within 0.1 s, output logged anyway.
  UPDATE 30 Oct: all three are done and the page is version 2 (held buttons for 30 ids, 20 s renders for the music-length ids; page built with `--board-long` and `--board-held`). (4) music played from START and END is not covered (their own driver loads). (5) Mac to DOS id mapping (+300 for
  4-20) is from the notes, unverified by ear.
- **Speech:** `SPEECH.117` is the nine Mac words in one headerless unsigned 8-bit file at 8,000 Hz (3.3 s): primary,
  target, hit, good, nice, landing, cleared, for take off, to land. Cut at 0.52, 0.88, 1.10, 1.34, 1.59, 1.95, 2.30,
  2.76 s; the owner confirmed the cuts by ear. This is the subtitle text.
- **Durable copy of this session's generated files** (the session scratchpad is temporary):
  `C:\Users\Tideg\f117-recomp-local\p3-session-29oct\` (sweeps, rendered mp3s, speech wavs, route variants, the page
  file, the memory dumps for R1, scripts). The game copy used for runs is not saved: copy `D:\GOG\F-117A`
  (never run from the install itself).

## Blocked or waiting

- **SETUP answers from the settings / sound device choice:** needs an injection at SETUP's DOS key reads
  (`docs/setup-autoanswer.md`), which changes `src/machine/dos_files.c`: P2 gate territory, do it after the P2
  integration.
- **Hybrid sound:** needs the owner's marks (the page) and a second hidden AdLib driver fed the same requests, plus
  muting the MT-32 channels of effect ids.
- **Subtitles under AdLib:** the speech is OPL writes there; the detector (`src/host/subtitles.c`, from the speaker counts) covers Roland and PC speaker only.
- **Owner's own checks:** the menu (Ctrl+Alt+S), `--crt`, `--watch latest`, the nine speech clips, the sound page.
- **Merging:** pull `master`, run the full gate (`py tools/build_recomp.py --work ...`) on the merged result, then merge.
- Open question put to the owner: a 30-step-a-second experiment (halves display lag; needs a validation sweep like 15 had).

## Practical notes

- Python is `py`, not `python`. Shell `sed` turns `\n` in a C string into a real newline: use the Edit tool.
- Building in the worktree: `CMAKE_BUILD_PARALLEL_LEVEL=2` and `build.cmd -DFETCHCONTENT_SOURCE_DIR_SDL3="C:/Dev/F-117A Recomp/build/_deps/sdl3-src"`
  (call it as `.\build.cmd`; the bare name is not found). Keep builds at BelowNormal while the other session gates.
  A helper batch file sits outside the repo at `D:\f117-wt\p3-b.cmd` (output `D:\f117-wt\p3-build.out`).
- Window-less runs: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software`; `--no-record`, a copy of the install.

## Update, 30 October 2026 (Remastered progress after the handoff above)

Commits on the branch since: `04ac17a`/`2ca7538` (R2 world files), `1acca1d`/`cf8326a` (mesh decoder on every real
bank, game loads banks unchanged), `ed01de1` (offline depth-buffer model sheets), `ad7697d` (sound page v2).

- **Done and checked:** `*.3DG`/`*.3DT`/`*.WLD` decode (`tools/world_decode.py`, `world_check.py`; LB, NC, PG exact in
  START's memory, VN exact but six runtime-written bytes); the mesh decoder decodes all 3,936 (model x level) cases
  of the 11 `*.3DX` banks (`tools/model_census.c`) and the banks sit unchanged in the game's memory; real models
  draw correctly with a depth buffer (`tools/model_sheet.c`; PNG sheets in the session folder).
- **Next big unit (R3 proper): read the prepared pose from the running game** (see "R3 plan revised" in `docs/remastered-design.md`: no pose-preparation rewrite is needed). Old note: To draw a frame from the scene-input capture (`F117R_OBSERVE_SCENE`,
  `tools/scene_capture.py`) the part matrices and positions must be built independently of the guest: the game does
  it in VGAME `120A:0317` (parts) and `0561` (planes) after the request at `120A:00CF`; `scene_pose_read` currently
  needs a DS snapshot taken AFTER that preparation. Then the camera chain (`model_xform_vertex`, projection `0x129A2`)
  with sub-pixel output, then terrain placement from the `*.3DG` tile maps and the ground-patch models (the first ~30
  models of a theatre bank are flat patches), then widescreen, longer reach, held-out comparison with the original
  frame (`F117_PRESENT_SHOTS`).
- **Not done anywhere:** a renderer in the app, `--presentation remastered`, the settings entry. Remastered stays 0%.

## Merge readiness (30 October 2026, at `a1b12b9`)

- `origin/master` is still `0c2b13a`: the branch is 32 commits ahead and 0 behind, so it fast-forwards.
- The branch changes only `CMakeLists.txt`, `README.md`, `docs/`, `src/host/`, `tests/` and `tools/`; nothing under
  `src/machine/`, `src/cpu/`, `src/matched/`, `src/recomp/`, `src/present/` or `recompiler/`.
- `ctest` in the branch's own build: 44 of 44 pass (the host, presentation, override and boundary tests).
- **Not run:** the full gate (`py tools/build_recomp.py --work ...`: routes, both engines, lockstep). Run it on the merged
  result before pushing to `master`; `headless.c` (f117run) gained the `--sound-sweep` options, which the gate's runner uses.

## Update, 30 October 2026, afternoon

- **`master` moved.** The other session built on the merge: `origin/master` is `d90802e` (a merge of their
  `wip/p2-setup-handoff-20261029` into master; P2 96.60%, All 95.28%, P3 still 85.42% with all credits kept). Their gates
  (`34dd038` and later) ran on their branch, which did not contain the Phase 3 commits; the merge combined the two and nobody
  has gated the result. Their changes are `src/matched/matched.c`, `tests/func_lockstep.c`, `CMakeLists.txt` (new tests),
  docs and census tools; mine touch none of the first two, so a clash is unlikely, but the combination is unproven.
- **Branch `p3-remaster`** (from `d90802e`, pushed, not merged): `src/present/remaster_project.c` (the game's projection,
  exact: 14,718 of 14,718 coordinates of a real flight, plus the sub-pixel form) and `src/present/remaster_raster.c`
  (depth-correct triangle fill: nearer wins in either draw order, near-plane clip, a wider frame moves nothing, sub-pixel
  positions), each with a test (`presentation_remaster_project`, `presentation_remaster_raster`). Neither is wired into
  the app. Both compile standalone with `cl`; they were not built through CMake.
- **Next, needs a full build:** the `pose` observer callback (see "R3 plan revised"). It changes `src/matched/observe.c`
  (and a header), so wait until the memory is free: compiling `matched.c` took about 12.6 GB for the other session.
