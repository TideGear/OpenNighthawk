# Phase 3 handoff (29 October 2026, end of session)

For the next conversation. Everything below is committed on the branch `p3-settings-menu`
(worktree `D:\f117-wt\p3-settings-menu`, pushed to `origin`), unmerged. The Phase 2 gate has not been run on it.
Scoreboard at the last commit: **P1 100.00%, P2 95.94%, P3 81.41%, All 94.06%** (`py tools/progress.py --title`).

## State of the branch

Host-side only (`src/host/`, `tools/`, `docs/`, `tests/`); no change to `src/machine/`, `src/cpu/`,
`src/matched/` or the recompiler, so parity should be untouched, but merge only after the gate passes.

| Item (weight) | Credit | What exists | What is missing |
|---|---|---|---|
| Settings menu (4) | 45% | Ctrl+Alt+S: 12 options, Roland output, Watch last flight row, writes `f117a.ini` keeping comments, restart offered (`settings.c`, `config_set/config_get`) | every option, SETUP answers, mouse; owner has not looked |
| CRT filter (1) | 70% | `--crt`, `crt = yes`, menu entry (`crt.c`), display copy only | owner has not looked; no mask or bloom |
| Modern controls (3) | 25% | `pad-deadzone/-invert-x/-invert-y/-fire1/-fire2`, `remap = Q:W` / `--remap` | physical pad or HOTAS untried, no remap screen, no key-reference overlay, mouse |
| Flight replays (3) | 30% | `--watch latest|SESSION` and a menu row, from a scratch copy of the save | picking among flights, pause/seek, the menu row's restart not run end to end |
| Accessibility (2) | 25% | keyboard remapping | subtitles (the nine words are known, see below), colourblind HUD (Remastered) |
| Fan patches (2) | 100% | debugcom's patch (damsonn) is fix D4; web search found no other | none, unless the owner finds another |
| Remastered (10) | 0% | `docs/remastered-design.md` (staged R1-R6); R1 terrain decoding | R2 onwards, see below |
| Two settings, hybrid sound, sound device | 0% | design only | see "Blocked" |

Diagnostics added: `F117_MENU_AUTOTEST=1` and `F117_MENU_SHOT=FILE.ppm` (window-less menu run).
Tests: `ctest -R host_` in the worktree's `build` (settings menu, CRT, controls, remap, config).

## Remastered: R1 done, R2 next

- `tools/world_decode.py` decodes `*.3DG` (terrain tile maps: mark `22`, then 16 + 256 + 3x512 bytes) and `*.3DT`
  (mark `11`, five list sizes, point counts, then points of three words and a word, 8 bytes each in the file)
  from the layouts START's matched loaders read (`src/matched/matched.c`, START `0x07D71` and `0x07E88`). Every byte of all
  nine theatres is accounted for. `tools/world_check.py` compares them with a memory dump: LB, NC, PG and VN match
  START's memory byte for byte (tables at `DS:B3A0..BAB0`, points after). CE, CU, KO, KU, ME decode but were not dumped.
- To dump: `py tools/run_route.py tools/routes/<route>.args --data <copy of the install> --out <dir> -- --steps <N> --dump 0:100000`,
  N just after START opens `<stem>.3dT` (the route's `run.log` shows the clock). The dump is printed as hex text.
- **R2 next:** `*.WLD` (starts `BN`, then counts; 2-3 KB): object placements and classes. Find START's matched loader
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
  added and builds; its first test (ids 14, 7, 11, repeat 700000, span 18000000) was running at hand-off. Next: render
  every id held in the three drivers, show a "held" button beside the single one when it is meaningfully longer.
  (2) ids 3, 36-39 reached the 5 s cap (music or loops): 20 s renders are in `board_long` (the page generator takes
  `--board-long`, not yet republished). (3) ids 41-44: the driver call did not return within 0.1 s, output logged anyway.
  (4) music played from START and END is not covered (their own driver loads). (5) Mac to DOS id mapping (+300 for
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
- **Subtitles:** need a host hook at the sound request (the same trap as `oracle_frame.py`) to know which words play.
- **Owner's own checks:** the menu (Ctrl+Alt+S), `--crt`, `--watch latest`, the nine speech clips, the sound page.
- **Merging:** pull `master`, run the full gate (`py tools/build_recomp.py --work ...`) on the merged result, then merge.
- Open question put to the owner: a 30-step-a-second experiment (halves display lag; needs a validation sweep like 15 had).

## Practical notes

- Python is `py`, not `python`. Shell `sed` turns `\n` in a C string into a real newline: use the Edit tool.
- Building in the worktree: `CMAKE_BUILD_PARALLEL_LEVEL=2` and `build.cmd -DFETCHCONTENT_SOURCE_DIR_SDL3="C:/Dev/F-117A Recomp/build/_deps/sdl3-src"`
  (call it as `.\build.cmd`; the bare name is not found). Keep builds at BelowNormal while the other session gates.
  A helper batch file sits outside the repo at `D:\f117-wt\p3-b.cmd` (output `D:\f117-wt\p3-build.out`).
- Window-less runs: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software`; `--no-record`, a copy of the install.
