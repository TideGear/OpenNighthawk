# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs, what was optimised). State as of 6 October
2026. Earlier session-by-session logs were removed from this file on that
date; `git log -p handoff.md` has them.

## Goal and standing decisions

- **Recompile the whole DOS game for Windows with 1:1 parity**, then
  understood (named) code, then switchable fixes and enhancements (60+ fps,
  4K). Phases and progress: `docs/roadmap.md`, `docs/progress.md`;
  `py tools/progress.py --title` prints the scoreboard every commit title ends
  with. Public repo, **code only**: no game data and no generated C, ever.
- **Parity target: the original game on real PC hardware, not DOSBox.**
  Identified DOSBox quirks are not copied; label behaviour by evidence. The
  machine still follows GOG's DOSBox 0.74 in a few places (PIT control-word
  IRQ0 behind the roster timing, DBOPL OPL, VGA timing); those are the places
  86Box can overrule.
- Repo: https://github.com/TideGear/OpenNighthawk (`origin`, `master`).
  **Commit and push together**, each verified piece. **No AI attribution** in
  commits or PRs (the owner's global CLAUDE.md outranks harness reminders).
  Commit titles end with the `progress.py --title` scoreboard.
- Fixes (`docs/bugs.md`) are switchable, off unless `--fix ID`; parity stays
  the default reference. Policy for the app's default is the owner's call.
- The Reimp (`..\F-117A Reimp`) is a separate project: read from it, never
  write into it. `references/` (git-ignored) holds the owner's installers and
  patches; never commit them. Leave the untracked `test.bat` alone.
- **Never open a visible window, go fullscreen or make sound on the owner's
  desktop.** GOG's `dosboxF117A.conf` asks for fullscreen; any config layered
  over it must set `fullscreen=false` and `mixer nosound=true`.

## Where things are

- Game: `D:\GOG\F-117A` (byte-identical to the installer plus 473.04;
  `py tools/verify_install.py --data "D:\GOG\F-117A"`).
- Work directories, never in the repo: `%USERPROFILE%\f117-recomp-local`
  (saved references under `video\`, e.g. `intro-qlh_wi26`, the GOG DOSBox
  intro capture; `dosbox-x\`; `build` in the repo is a junction there) and
  scratch on **D:** (C: has often had under 1 GB free, keep TEMP and work
  directories on D:). 86Box, DOSBox-X and MSYS2: `D:\86box`, `D:\86box-src`,
  `D:\msys64`. Evidence cited in docs: `D:\f117-gate\genmap`,
  `dosbox-cargo-m275`, `cargo-m275.input`.
- A second worktree at `C:\Users\Tideg\f117-recomp-local\wt` lets code work
  continue while a gate compiles the main tree (call its `build.cmd` by
  absolute path; give each tree its own TEMP; they share nothing else).

## Build and check

- **Gate** (translate, build, coverage, both engines on every route, instruction
  lockstep, matched-routine lockstep): `py tools/build_recomp.py --data
  D:/GOG/F-117A --work D:/f117-gate/workN` (add `--seed-coverage` the previous
  work's `coverage` directory so the second build is skipped). About 23
  minutes. Read the tally, not the exit code: every route `IDENTICAL`,
  instruction lockstep 0 mismatching, matched lockstep 0 mismatching. Last
  green: 32 routes identical, 5,713,472 instruction states, 248 matched
  routines (806,801 states). Interpreter results are cached across gates
  (`--interp-cache`, default `~/f117-recomp-local/interp-cache`; the key ignores
  `src/matched/matched.c`), cutting parity from about 9 to 3 minutes when only
  matched routines changed; `--verify-interp-cache` proves it equal.
- **PC parity against other machines**: `py tools/pc_parity.py --data
  D:/GOG/F-117A` runs the intro on GOG's DOSBox (saved capture), DOSBox-X
  (patched build, headless) and 86Box (VNC build, headless) at once and checks
  each against measured limits: pictures, the intro's music, and a scripted
  START session's saved roster (DOSBox-X and 86Box). `py tools/fidelity_all.py
  --data D:/GOG/F-117A` holds the machine probe to `tools/fidelity_baseline.json`.
  Details and builds: `tools/ref86box/` (`build_86box.md`, `build_dosbox_x.md`).
  Other references: `tools/fidelity.py`, `tools/dosbox_compare.py` (OPL),
  `tools/video_compare.py`.
- Routes: `tools/routes/*.args` with a README; `py tools/run_route.py ROUTE
  --data DIR` is only the weak milestone check, so also run the route's strong
  observer (`strike_pilot`, `recon_pilot`, `cargo_check`, `airstrip_check`,
  `airair_pilot`, `career_check`, `d1_check`, `career_rank6`). Adaptive pilots
  record an input; the gate replays it. A timing change invalidates every
  recording at once.
- Matched routines (Phase 2): `src/matched/matched.c`, held by
  `tests/func_lockstep.c`; candidates from `py tools/reimp_names.py --reimp
  "../F-117A Reimp" --gen GEN --out FILE.tsv`, then `py tools/progress.py
  --census FILE.tsv` after a batch. Routines that call the far library or do
  port I/O show "not testable here" and are held by the routes only.
- ROM-free tests: `py tests/test_*.py` and `ctest` (13). CI is
  `.github/workflows/windows.yml`.

## Current state

- Phase 1 (recompiled, 1:1): translation, both engines and the gate are
  green; 32 routes. All eight primary objective types have gate-replayed
  routes (recon, strike, cargo/D5, airstrip, air-to-air 5-8, each with a
  return leg for types 1-4). Rank-3 career paired through retirement; the
  rank-6 "General, At Last!" branch is reached with a staged roster
  (`tools/career_rank6.py`), not an earned rank-6 career.
- Fixes done: D1 (frame limiter at VGAME 0x441D), D2, D4, D5, D11, D12, D34.
  Open: D6, D7 (inconclusive probe), D8 (needs an LGB route).
- Phase 2: 248 addresses matched (247 of 1,535 census functions, 10,868 of
  179,213 bytes; P2 10.76%).
- Phase 3 presentation Stage 1: draw lists rebuild every phase exactly on the
  routes and windows tried (`docs/presentation.md`); Stages 2-4 open.
- References: GOG DOSBox intro 1,329 exact pictures with 3 unmatched each side
  and no end drift; DOSBox-X 1,237 exact, drift up to +200 ms; 86Box scenes
  and order match, 104 of 105 pictures within 3 levels (the VM is a 6 MHz 286,
  so frame-exact timing is not meaningful yet).

## Open items, in order

1. Keep `pc_parity.py` and `fidelity_all.py` in the routine checks; extend the
   saved-data check beyond the roster (a career) and to flight.
2. Interpreter-result cache in the gate (key excludes `matched.c`), then
   fast-forward and parallel reference runs (`docs/repeated-processes.md`).
3. 86Box (now a 386DX/33, `make_profile386.py`): flight under script (the mouse
   and START already are); the remaining 2 s intro drift is disk-interface
   pacing; channels 3 and 4's pitch-bend writes land in different places
   (speed-paced), which `compare_opl86.py` reports without judging.
4. Roadmap Phase 1 leftovers: picture residuals, Munt listening checks,
   sound parity, DOSBox-specific timing details, a closed-loop cargo pilot for
   independent delivery, an earned rank-6 career.
5. Phase 2 batches (the remaining small routines are far-segment or I/O);
   Phase 2 naming; Phase 3 stages 2-4.

## Traps that cost time

- **The shell tool halves backslashes** and turns `\n` into real newlines
  inside heredocs and Python it runs. Write scripts with the Write tool and
  edit with the Edit tool; avoid escapes in inline Python.
- CRLF: several tracked files are CRLF in the working tree (git normalises).
  Patch scripts must match the file's own endings; open with `newline=""`.
- `build.cmd` writes `%TEMP%\f117r-build.log`: two builds sharing TEMP read each
  other's log. The PowerShell tool resets its working directory, so
  `cd X; .\build.cmd` builds the main tree.
- `rm -rf` with a relative glob after `cd` is refused by the safety check; use
  explicit absolute paths. A file-removal on a project folder via PowerShell
  may be refused too.
- DOSBox 0.74 (SDL 1.2) sees posted keys only with `SDL_VIDEODRIVER=windib`;
  DOSBox-X does not take posted keys at all, which is why the patched build
  starts its own capture and `AUTOTYPE` answers SETUP. The release 86Box cannot
  be typed into at all; use the VNC build (`tools/ref86box/build_86box.md`).
- VGAME exiting 129 with parent result 2 mid-flight is the original's
  render-detected terrain collision, not an engine fault.
- A staged poke the game overwrites is the wrong poke; stage the cause, and
  check the shipped default first. A port-only copy of an original record
  desyncs silently. Segment cells stay native in lockstep.
- Game keys (Key Control Card): 0 brakes, 6 gear, 8 bay, Space select weapon,
  Enter fire, Backspace cannon, B select target, Shift+F10 eject. The carrier
  start has its brakes on; rotate about 19 s after full throttle and do not
  over-climb.
- `f117run` scripted input: Space is `\s` in route files; at most 4096 inputs.
- DOSBox's raw OPL capture omits registers 02h-04h and records changes only;
  the intro's channel-3 note at 29.7 s is random between runs.
