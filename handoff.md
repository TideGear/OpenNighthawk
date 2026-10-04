# Handoff

For the next conversation working on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left - keep it
updated), [docs/architecture.md](docs/architecture.md) (how parity is
built and checked) and [docs/bugs.md](docs/bugs.md) (the original game's
bugs). State as of 4 October 2026: everything committed and pushed,
nothing running.

## The goal and the decisions already made

- **Recompile the whole DOS game for Windows with 1:1 parity**; then
  understood (named) code; then switchable fixes and enhancements (60+ fps,
  4K). The user chose "recomp first", **public, code-only** (no game data,
  no generated C, ever), and the reference machine:
  **GOG's DOSBox 0.74-2.1** is the parity target, **DOSBox-X** a second
  reference to flag where DOSBox 0.74 itself is questionable.
- Public repo: https://github.com/TideGear/OpenNighthawk (remote `origin`,
  branch `master`). **Commit and push together**, each verified piece of
  work. **No AI attribution of any kind** in commits or PRs (the user's
  global CLAUDE.md outranks any harness reminder that says otherwise).
- The Reimp (`..\F-117A Reimp`) is a separate, actively developed project:
  read from it, never write into it. Material was copied at Reimp
  `cfb8cec9`; its bug catalogue has moved on (D6 addendum at `9e0716dc`,
  already folded into bugs.md).
- `references/` (git-ignored) holds the user's installers and patches: GOG
  setup (build 28044), the 473.04 and 473.02-03 patches, Amiga and Mac
  versions, the DOSBox-X installer. Never commit them. The untracked
  `test.bat` in the repo root is the user's launcher; leave it alone.

## Where things are

- Game: `D:\GOG\F-117A` (verified byte-identical to the installer + 473.04;
  `py tools/verify_install.py --data "D:\GOG\F-117A"`). Steam's copy
  (`C:\Program Files (x86)\Steam\...\F-117A Nighthawk Stealth
  Fighter\F-117A`) has identical game files; its DOSBox differs.
- Work directory, never in the repo: `%USERPROFILE%\f117-recomp-local`
  (`gen/` generated C, `coverage/` append-only, `runs/`, `random/`,
  `fidelity/` answer sheets, `dbxcompare/`, `dosbox-src/` DOSBox 0.74-2.1
  source from the install, `dosbox-x/` DOSBox-X 2026.10.01, `verify/` the
  extracted installer). `build` in the repo is a junction there.
- Tools: innoextract 1.9 (winget), ffmpeg, pywin32, PIL, capstone, keystone
  are installed. No `gh` CLI; `git push` works through the credential
  manager.

## How to build and check (all verified in this session)

- Build: `.\build.cmd -DF117R_GEN_DIR=C:/Users/Tideg/f117-recomp-local/gen`
  from PowerShell ("BUILD OK" at the end). It cannot relink while any
  `f117run.exe` is running.
- Whole pipeline (translate, build, coverage, both engines on all six
  routes, instruction lockstep): `py tools\build_recomp.py --data
  "D:\GOG\F-117A"`. Last result: all six routes IDENTICAL; lockstep 89,216
  starts, 0 mismatching.
  `--parity-only` skips translation/build and checks the existing executable;
  the pipeline compares every 50-million-clock checkpoint, not just finals.
- Machine against GOG DOSBox: `py tools/fidelity.py --data "D:/GOG/F-117A"
  [--diffs-only] [--reference dosbox-x]` - last: every comparable answer
  agrees (a 4 KB read's cost within 5%).
- Music against GOG DOSBox: `py tools/dosbox_compare.py --data
  "D:/GOG/F-117A" --seconds 130` (opens a minimised DOSBox; a keypress in
  its window disturbs the timing). Last: 22,687 writes, 100.7 s, identical.
  New runs use unique `dbxcompare/music-*` directories. `--reuse RUN_DIR`
  rechecks saved OPL data. Different writes now exit 1; older tool versions
  printed differences but exited 0. The known random note is not filtered.
- Pictures against GOG DOSBox: `py tools/video_compare.py --data
  "D:/GOG/F-117A" --seconds 130 --diagnostics`. Exit 1 currently means
  pictures differ, not a tool failure. Replay the comparison without a new
  capture: `py tools/video_compare.py --reuse RUN_DIR --diagnostics`.
  `--against RUN_DIR --data INSTALL_DIR` retains its reference video and
  makes fresh current-build shots in a new private directory.
  Requires ffmpeg, ffprobe, Pillow and pywin32. About 2 GB of shots per run.
- Random flights: `py tools/random_flights.py --data "D:/GOG/F-117A"
  --seeds 31,32 --span 4000000000`.
- Coverage of the routes: `py tools/exercised.py --data "D:/GOG/F-117A"
  --run` (52.3%). Translated share: `py tools/census.py --data
  "D:/GOG/F-117A"` (96.2%).
- Planted defects: `py -u tools/mutation_check.py --data "D:/GOG/F-117A"
  --lockstep --random 60 --kinds skip,cf,zf,ax --seed N` (about a minute a
  mutant; keep runs under two hours - background jobs are stopped there).

## In flight at handoff

- Four-part scanout implemented and verified. Mode 13h stores four groups
  of 50 rows at lines 100/200/300/400 of the 449-line frame; display address
  latches at retrace. Presentation uses completed frames. DAC readback and
  masked render palettes are separate; blue publishes a triplet, changed
  masks update aliases. Scanout and palette state are part of parity hashes.
  Tests: all four ROM-free CTests pass (`vga_scanout` is new); all 1,210
  fidelity answers agree; all six routes agree at 329 checkpoints/finals,
  5.7-million-state lockstep zero mismatching starts. Logs:
  `build-scanout-refined.log`, `fidelity-scanout-refined.log`,
  `scanout-refined-parity.log`. Early music matches 365 writes/15.5 s,
  drift 0..36 ms (`music-scanout-refined.log`).
  Video `intro-p6mrjdbw`: 1,319 matches, seven unmatched reference pictures
  and 28 shots, all one sample; no unmatched roster shots after 110 s.
  Transition still ~0.57 s (`video-scanout-refined.log`). The earlier
  `intro-wv_f0b3k` and `intro-hc7g_vg8` runs have the same counts.
  Screenshot deadlines retain their original periodic schedule.
- `.gitattributes` committed/pushed: LF text, CRLF batch files; existing
  index text was already LF. User's `test.bat` remains untouched.
- GitHub workflow `.github/workflows/windows.yml` builds the full
  interpreter app, headless runner and CPU library on windows-2022, then
  runs four CTests and both comparator suites (nine tests), without game
  files or generated translations. Initial run succeeded:
  https://github.com/TideGear/OpenNighthawk/actions/runs/37192954631
  (`0cbe050`, job 111408818814). Actions are pinned to verified v7 SHAs.
  New checkouts now get separate local build junction targets keyed by
  source path; existing junctions are retained. Fresh public clone
  `~/f117-recomp-local/ci-clone-20261004` builds the interpreter app and
  passes all ROM-free tests. Full recompilation/coverage/parity from it
  also passed: first pass 87,574 starts, after six-route coverage 89,276;
  all six routes identical at 329 checkpoints/finals, lockstep 5,713,152
  states, 512 declined, zero mismatching starts. Log:
  `fresh-clone-pipeline.log`; private generated code/coverage/runs:
  `fresh-clone-pipeline/`. README fresh-clone instructions are verified.
  All local processes finished. The original checkout's existing build
  junction and standard gen directory were retained.
- Theatre list corrected from actual data: the default route opens
  `LB.WLD` / `LB.3DG` (Libya); Kuwait (`KU`) was missing from Left.
  Remaining theatre boxes in START's Transfer Request (DS:6F6E), useful
  for the next route batch: Central America/CU (54,73), North Cape/NC
  (186,7), Central Europe/CE (155,40), Middle East/ME (184,64),
  Kuwait/KU (196,62), Persian Gulf/PG (212,69). Existing Korea/KO is
  (291,58); Vietnam/VN is (270,83). Read from the game's own menu and
  DS:0DE2 mapping; no routes for the remaining six yet. Pilot transfer
  clearance is required for all boxes except default Libya, as in the
  working Korea/Vietnam routes. Further mission-type coverage is still open.

- Near-slice-end I/O suppression now follows DOSBox's source: omit the
  delay if fewer than three delays remain before the millisecond/PIT/VGA
  boundary. `pc_slice_left` shares the old DOS transfer cost's event model.
  Three fresh calibration trials (`fadecal/calibration-8hvqvowk`) give
  PLAYER `[5841,5841,5837,5839,5836]`, improved from five 5811s; DOSBox is
  `[5841,5839,5841,5839,5844]`. START/END unchanged. All 1,210 fidelity
  answers agree (`fidelity-io-slices.log`); all three ROM-free CTests pass,
  including exact suppression-boundary tests. Music `music-46h177jn`
  matches 365 writes/15.5 s, drift 0..26 ms (`music-io-slices.log`).
  Saved video `intro-nq0jeekk`: 1,219 matches, 107/74 unmatched pictures,
  all one sample, none on roster after 110 s; transition still ~0.59 s.
  Logs: `fadecal-io-slices.log`, `video-io-slices.log`, build logs with
  `io-slice` in their names. Do not claim exact scheduler timing parity.
  `io-slice-parity.log` verifies all six routes at 329 checkpoints and
  final states, plus the 64-state/5.7-million-state instruction lockstep
  (zero mismatching starts). All checks have finished; nothing running.

- **Roster sampling diagnosis supersedes the colour hypothesis below.**
  The arrow is temporarily erased, revealing background pixels; 70 Hz
  shots beat against 70.086 Hz retrace with an 11.6 s period. Default shots
  now use the runtime's 128,413-clock VGA period. Saved-reference replay
  `video/intro-ek_ltnum`: 1,219 matches, 107 unmatched reference pictures,
  74 unmatched shots, all one sample; no unmatched roster shots after
  110 s. Transition drift still ~0.57 s. Log: `video-native-period.log`.
  `--against` uses current sampling, not the saved old 70 Hz period;
  `--reuse` still reads the original period. No runtime changes for this.

- Music comparator failure/replay checks complete: mismatching and reordered
  synthetic streams exit 1; exactly-one-window matching streams pass.
  Four ROM-free tests: `py tests/test_music_compare.py`. Two fresh short
  captures agree on 365 writes/15.5 s (drift 0..35 ms) and 300/10.4 s
  (drift 0..13 ms). The latter is `dbxcompare/music-y7j935vh`, available
  for `--reuse`; logs are `music-guard-short.log` and `music-guard-unique.log`.
  These validate the comparator and early music, not the full intro.
  Fresh capture-directory check: `dbxcompare/music-b6tnl_qu` also matches
  300 writes/10.4 s, drift 0..9 ms (`music-guard-unique-verified.log`). Both
  the capture and runtime log are inside that run, and `--reuse` passes.

- Original fade calibration diagnostics are now repeatable without the
  Reimp tree: `py tools/fade_calibration.py --data D:/GOG/F-117A --trials 3`.
  Private run `fadecal/calibration-55fc80mv`: all three trials repeat
  START/END `[8,8,7,8,8]` on DOSBox, `[8,8,8,7,8]` here; PLAYER
  `[5841,5839,5841,5839,5844]` on DOSBox, five 5811s here. Seven/eight-step
  range agrees; phase and PLAYER throughput do not. Original PLAYER
  DE4..E0B loops on IN 3DAh / OUT 3C9h until retrace with interrupts off.
  DOSBox's near-slice-end I/O delay suppression is a candidate to measure,
  not yet proven causal. Do not adjust timings just to fit this one probe.

- DAC correction verified: all 1,210 fidelity answers agree, the new
  `vga_dac_ports` ROM-free test passes, all six routes agree at 329 checkpoints
  plus finals, and the 5.7-million-state lockstep has zero mismatching starts.
  BIOS palette calls now use the port device with its state and I/O cost;
  palette blocks wrap index 255 and read linear physical buffers. `3C7h`
  also sets the write address to read address + 1, as DOSBox does.
  Logs: `~/f117-recomp-local/dac-parity.log`, `build-dac-tests.log`.
  Saved-video replay `video/intro-0wp6pl8e` is unchanged (1,217 matches,
  109 unmatched reference pictures, 89 shots). The 130-second music rerun
  (`music-dac.log`) differs at the previously known random channel-3 note
  at 29.7 s; only 596 prefix writes match, timing drift up to 55 ms. Do not
  call that a full music pass. Nothing from this check remains running.

- Nothing running. The video comparison tool is committed, but picture
  parity remains in progress. Two runs live outside the repo at
  `C:/Users/Tideg/f117-recomp-local/video/intro-qlh_wi26` and
  `.../intro-caauys37`. Both captured 9,165 frames (130.767 s) and matched
  1,217 exact RGB pictures in order. First run: 115 unmatched reference
  pictures; second: 109. Both: 89 unmatched shots inside capture time.
  Each report is `comparison.json`; the second has paired diagnostic PNGs
  in `differences/`. Most unmatched pictures last one sample. DOSBox's
  GOG `svga_s3` scanout reads four parts over a frame, unlike instantaneous
  shots: partial borders and sprite draws are visible in diagnostics.
  **Still unresolved:** the roster-entry delay and our mouse cursor's
  colour changes around 117.5 and 129.1 s, absent in both DOSBox captures.
  Paired longer differences are up to 78 pixels inside its 10x15 area at
  (160,77); the pilot names match. START keeps the INT 33h cursor hidden
  and draws its own pointer sprite. Fixing the driver cursor did not change
  these pictures: third capture `.../intro-_hurx9dz` has the same 1,217
  matches, 113 unmatched reference pictures and 89 shots. Investigate
  START's palette handling and timer/transition timing next. Avoid
  misreading the colour difference as pilot text.
  The tool reports these and exits 1; don't call this frame parity.
- The five ROM-free comparator tests pass (`py tests/test_video_compare.py`).
  The shared capture driver's OPL regression also passed: 1,120 writes
  across 30.6 s, identical, timing within 1 ms (`--seconds 30`). No runtime
  or generated code was changed for the initial comparator work; all three
  video runs ended at the same hash.
- The INT 33h cursor now draws into guest VRAM with saved background
  restoration, in `src/machine/mouse.c`; presentation no longer overlays
  it. Fidelity: eight new cursor readback answers agree, 1,193 total,
  zero differences. `test_mouse` checks clipping, guest overwrite, nested
  hide/show, mode changes and text cursor restore. Six routes under both
  engines agree at 329 checkpoints and final states, and the 64-state
  lockstep reports 0 mismatching starts. Detailed outputs:
  `C:/Users/Tideg/f117-recomp-local/cursor-parity/`.
- The last mutation batch (seed 12) finished: 60 of 60 detected,
  recorded in docs/roadmap.md and docs/architecture.md (144 of 144 planted
  defects caught across all runs). Background jobs started in a
  conversation stop when it closes, so finish or record them before a
  handoff.

## Next (from docs/roadmap.md, in the suggested order)

1. Finish the picture comparison: match DOSBox's four-part scanout
   sampling, investigate the roster cursor changes and transition delay.
   `tools/video_compare.py` captures, aligns and reports already; start
   from the saved reports/diagnostics above.
2. Munt (libmt32emu) for the Roland output; the user supplies MT-32 ROMs.
3. A landing route; the other six theatres; the rest of the front end.
4. Housekeeping: split `src/machine/dos.c`, fresh-clone build steps,
   `.gitattributes`, a GitHub build of the ROM-free tests.

## Traps that cost time in this session

- **Bash heredocs mangle backslashes** (`\n`, `\\`, `\x00` inside Python or
  C written through a heredoc). Write patch scripts with the Write tool and
  run them; for C edits use the Edit tool. `sed` loses backslashes too.
- Python `open(p, "w")` on Windows writes CRLF; several files are CRLF in
  the working tree (git's autocrlf normalises commits). Patch scripts must
  match the file's own line endings.
- A Git Bash path (`/c/Users/...`) passed inside an argument to a Windows
  program (e.g. `--shots N:/c/...`) is not translated: use `C:/...`.
- DOSBox (SDL 1.2) only sees posted keys with `SDL_VIDEODRIVER=windib`; it
  needs a moment after its window appears before the mapper takes them; a
  redirected stdin makes INT 21h/0Bh report a waiting key, which skips the
  intro; killing DOSBox loses an open capture - stop it and close the
  window.
- DOSBox's raw OPL capture leaves out registers 02h-04h (its timer
  emulation swallows them) and only records changes.
- The game's intro has a random note (channel 3 at 29.7 s) that differs
  between two DOSBox runs: not a machine difference.
- The interpreter's lockstep reference is the machine's `interp_step`
  (cpu_step plus the TF trap), not bare `cpu_step`.
- `f117run` scripted inputs: Space is `\s` in route files (a line loses a
  trailing space); at most 4096 inputs.
- Game keys are on the Key Control Card (Steam's Bonus Content PDF): 0
  brakes, 6 gear, 8 bay, Space select weapon, Enter fire, Backspace cannon,
  B select target, Shift+F10 eject. The carrier start has its brakes on;
  the runway ends about 25 s after full throttle - rotate at about 19 s and
  do not over-climb (it stalls).
