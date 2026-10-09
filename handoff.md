# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs). State as of the afternoon of 9 October 2026.
Earlier session logs are in `git log -p handoff.md`.

## What to do first

1. `git status -sb`: the gate repairs are on master and `lockstep-code-below`,
   pushed together after the full gate passed (item 3). Leave any
   pre-existing untracked files alone.
2. The app's default frame rate is the open decision. The owner's rule (9 Oct):
   the default is the highest frame rate at which nothing breaks, with parity
   first and the developers' intent second. Read `docs/speed-sweep.md` (merged
   from branch `speed-sweep`) and `docs/bugs.md` D1, then decide with the owner.
   The current default is `timing = dosbox` (GOG's 9 million instructions a
   second). The speed study has not finished: the 15 frames a second limiter
   candidate, the pilots' engagement results, the control-response measurements
   and the `--fix D1` sweep are still to run.
3. **Green and pushed.** `merge9` tested HEAD `f2a7d2c`:
   all 35 routes identical, both 5,718,912-state instruction checks and both
   fixed matched seeds passed. The rotating seed `0xf2a7d2c1bf36` caught one
   real matched-code bug in START `0000:36C4`: the shared shadow-text helper
   replaced PUSH SS / POP ES with an assignment. Its source record can alias
   the temporary stack write through the 20-bit wrap, changing the copied
   shadow colour. Restoring the push/pop passes that seed on START 36C4 and
   END 19C3/112A (14,752 comparisons). CTest now retains it as
   `matched_shadow_stack_alias` (13 tests in the gate's unit step).
   The full rerun, `D:\f117-gate\merge10\gate.log`, passed in 1,157 s:
   13 unit tests; 35 identical routes (34 fresh interpreter sessions); both
   instruction profiles 5,718,912 states, zero mismatches; matched seeds
   `0x5EED0F117A`, `0xC0FFEE`, `0xf2a7d2c1bf36` compared 2,475,889,
   2,474,978 and 2,474,931 states, zero mismatches. Every routes-only routine
   ran and no route reported an event-limit overrun. Coverage added nothing.
   The preceding `CODE_BELOW` fix for VGAME `1377:00F3` remains:
   its 640-wide loop and RET lie below its entry (`00C2` and `00F2`), and the
   harness now compares about 3,990 states instead of 341. It passed the old
   failing seed `0x303c384910a7` and both fixed seeds; a one-step mutant fails.

## Goal and standing decisions

- **Parity first.** The recompilation is what the original did on the hardware
  of its time (owner, 8 Oct 2026). Where the developers' intent can be
  established, it guides the defaults (owner, 9 Oct: "1:1 parity with the
  original DOS version is our priority, but where we can figure out the
  developers' intent, that is important too"). Work that only measures a
  reference emulator or the test pilots is settled, not extended.
- **Frame rate.** The owner wants the highest frame rate the game runs at
  without anything breaking. The original caps its frame-rate estimate S at 15
  (verified in the VGAME listing at `0x0D479`), so above 15 frames a second the
  mission clock runs at frames divided by 15 times real time. A limiter at
  exactly 15 frames a second (four ticks per frame) is the leading hypothesis
  and is under test. The owner decides the default.
- **Phases.** Phase 1 (parity, including the 386 profile), then Phase 2 (named,
  matched code), Phase 3 (fixes and presentation), Phase 4 (checks only people
  can make).
- **Fixes** (`docs/bugs.md`) are switchable, off unless `--fix ID`. A switched-on
  fix takes the place of the parity routine at its address, in both engines.
- **Reference machine.** The 86Box tools default to the MS-DOS 5.00 machine
  `vmt386dos500` (MS-DOS 5.00 with MOUSE.COM 6.26; owner, 9 Oct). FreeDOS
  `vmt386` is reached with `--profile` / `--profile86`.
- **Matched lockstep.** Each routine draws its states from its own random
  stream, so shards test exactly what one run does (owner approved, 9 Oct: the
  same number and kind of states, not the old ones).
- **Public repo, code only.** No game data, no generated C. Never commit
  `references/` (git-ignored).
- **No AI attribution** in commits or PRs (the owner's global CLAUDE.md
  outranks harness reminders that ask for it).
- **Commit and push together**: a code change is pushed after the gate passes;
  docs-only commits may follow. `py tools/progress.py --title` prints the
  scoreboard every commit title starts with; it also rewrites
  `docs/progress.json`, so commit that with the docs.
- The owner is not a software engineer: explain trade-offs plainly and flag
  overengineering.
- The Reimp (`..\F-117A Reimp`) is read only. Never open a visible window, go
  fullscreen or make sound on the owner's desktop. Check for orphan emulator
  processes after killed runs.

## Where things are

- Game: `D:\GOG\F-117A` (never mounted or written by a tool; copy per run).
- Generated C: `C:\Users\Tideg\f117-recomp-local\gen` (never committed);
  coverage in `...\coverage`; route runs in `...\runs`.
- Scratch and logs on D: (`D:\f117-gate\...`). The current gate's log is
  `D:\f117-gate\merge10\gate.log`; earlier gates are in `merge3` to `merge9`.
- Research (read only): `D:\f117-gate\macresearch\` (Mac 2.1, 2.3.1 and 2.3.2
  listings, the Amiga listing, `dos_ctl_sim.py`). The archives are in the
  Reimp's `reference\` folder.
- 86Box (`D:\86box`): `vmt386dos500` (the default reference; 1 MB),
  `vmt386dos500h` (4 MB, HIMEM.SYS, DOS=HIGH; built, not the reference),
  `vmt386` (FreeDOS), `vmt386dos401`, `vmt386dos622`. They are built by
  `tools/ref86box/build_msdos_vm.py` (`--mem-kb`, `--himem`) from the owner's
  copies in `references/`. The patched 86Box with the trace options (`B86_OPL`,
  `B86_SPKLOG`, ...) is `D:\86box-src\build\src\86Box.exe`.
- Worktrees (`D:\f117-wt`): every Phase 2 and live-path branch is merged into
  master: `p2-vgame`, `p2-start`, `p2-small`, `p3-live`, `ref-vm`,
  `lockstep-par`, `sound-speech`, `t386-frames`. `speed-sweep` holds the speed
  study is merged: `tools/speed_sweep.py`, `tools/d1_check.py --route`, the write-up `docs/speed-sweep.md`, and the 199 raw flights in `D:\f117-gate\speedsweep\main\runs.jsonl` (with `table.txt`). `p3-stage2` and
  `p3-stage3` are old. Remove merged worktrees when convenient
  (`git worktree remove`).

## Build and check

- **Build** (PowerShell, from the repo):
  `cmd.exe /c ".\build.cmd -DF117R_BUILD_APP=OFF"` builds the core, `f117run`
  and the tests; `-DF117R_BUILD_APP=ON` adds the SDL app `f117a`. A new
  worktree needs `-DF117R_GEN_DIR=C:/Users/Tideg/f117-recomp-local/gen`. Check
  for `BUILD OK` in `%TEMP%\f117r-build.log`; parallel builds need a private
  TEMP.
- **Gate**: `py tools/build_recomp.py --data D:/GOG/F-117A --seed-coverage
  C:/Users/Tideg/f117-recomp-local/coverage`, about 20 minutes, run in the
  background. Do not edit `src/` or rebuild while it runs. Read the tally, not
  the exit code:
  - after the build, the unit tests (`ctest -E func_lockstep`, 13 tests with
    generated code, including the shadow-text stack-alias regression);
  - steps 6 and 6b: every translated instruction (89,366 starts, 5,718,912
    states per timing profile) against the interpreter, 0 mismatching;
  - steps 7, 7b and 7c: every matched routine at three seeds (0x5EED0F117A,
    0xC0FFEE, and one from HEAD's hash), run as eight shards, 0 mismatching;
  - step 8: every routes-only routine ran on some route; step 9: no
    `[matched] OVERRUN` line on any route;
  - the 35 routes identical between the engines (end hash and every
    checkpoint).
- **Quick route checks** (reference end hashes):
  `tools/routes/boot_to_flight.args` `48a10e901505e2a9` (also with `--fix D34`);
  `strike.args` `aa8fca6ac6f373f0`; `career_serge.args` `6ea4f6612f4775da`;
  `frontend_dialogs.args` `20ab75f97ab041c8`. Run with
  `py tools/run_route.py ROUTE.args --data D:/GOG/F-117A --engine recomp --out DIR`.
- **CI**: `.github/workflows/windows.yml` runs ctest on each push. This machine
  has no `gh`; read the run list with the public API:
  `curl https://api.github.com/repos/TideGear/OpenNighthawk/actions/runs?per_page=5`.
  The job logs need sign-in, so reproduce a failure locally with ctest. The
  last three pushes were green.
- **PC parity**: `py tools/pc_parity.py --data D:/GOG/F-117A` (about 12 minutes).
  All checks pass on the MS-DOS 5.00 reference (9 Oct). The 86Box pictures must
  be exact except the one listed by hash in
  `tools/ref86box/expected_misses86.txt` (p085, START's roster while 86Box is
  still loading files). The DOSBox-X roster check runs without fast-forward
  (`save_parity.py --no-turbo`).
- **Matched lockstep**: `py tools/func_lockstep_par.py --states 4000 --verbose
  [--seed S]` (eight shards, about 3 minutes; `build\func_lockstep.exe` alone
  takes about 20). A routine's DOS and keyboard calls get deterministic answers
  (AX from the call count). `DOS_STUBS`, `SHARED_ENDINGS`, `SPANS` and
  `CODE_BELOW` in `tests/func_lockstep.c` give the ranges and the listed
  routines' stubs. New routines start from `py tools/matched_draft.py --data
  D:/GOG/F-117A --module VGAME.EXE --ip 0xNNNN`; drafts are never committed as
  they are.
- **Room and call claims**: the rules are in `docs/architecture.md` ("Room on
  the routes", "Port accesses": each port access in a stretch claims
  `IO_SLACK(k)`). `tools/stack_balance.py` checks stack balance across a
  module's census.
- **Census**: `py tools/reimp_names.py --reimp "../F-117A Reimp" --gen
  C:/Users/Tideg/f117-recomp-local/gen --out D:/f117-gate/census.tsv`, then
  `py tools/progress.py --census D:/f117-gate/census.tsv`.
- **Speed study**: `py tools/d1_check.py --data D:/GOG/F-117A --ips N --no-fix
  [--route R.args]` (boot_to_flight by default; busier missions with `--route`).
- **The app's options**: `f117a --timing dosbox|386`, or `timing = ` in
  `f117a.ini` (default `dosbox`). `--present replay|interp`,
  `--present-scale N`, `--present-age interp|extrapolate` and `--present-log
  FILE` are off by default.
- **386 profile**: `insn_lockstep --timing386 --states 64`;
  `tools/ref86box/probe386.py` (90 of 93 blocks equal to the MS-DOS reference);
  `frames386.py` (a known end-drift failure, +642.5 ms on the MS-DOS VM);
  `stick_response.py --machine machine386`.

## Current state

- **Scoreboard** (the last pushed title): P1 97.71%, P2 38.40%, P3 67.00%,
  P4 42.90%, All 70.40%.
- **Phase 1 (parity)**
  - Translation: 89,366 instruction starts (96% of the code bytes). 35 routes
    identical between the engines. The interpreter and the translator are
    checked against silicon vectors and planted defects (`docs/roadmap.md`).
  - 386 profile: cycle-accurate against 86Box's 386DX/33 on the MS-DOS 5.00
    reference (probe 90 of 93 blocks exact; the other three are mode-set
    spreads).
  - Sound: the AdLib takeoff call on the MS-DOS machine matches this build in
    order and value (6,464 register-43h writes, the same rate). The speaker's
    counter writes match exactly (13,046 counts, 15.1 a millisecond on both).
    Open: a reference audio for the speech (DOSBox-X plays none of it).
  - **The original's frame controller** (verified in the VGAME listing):
    - S is clamped to [4 - accel, 15] (`0x0D479`).
    - A per-frame wait at `0x0409` calls `0x04C7`, which waits `[0x43E8]` ticks.
    - `0x0D441` sets `[0x43E8]` to (9 + (-120 / S)) / 2, rounded down, limited to
      1 to 4, when the measured S is above 15, and to 0 otherwise.
    - So the wait's frame cap is 60/N frames a second: 60 for measured S 16-23,
      30 for 24-35, 20 for 40 and up. It does not cap at 15.
    - Above 15 frames a second the mission clock runs at frames divided by 15
      times real time: 1.11 measured at 16 MIPS (16.7 / 15 predicts it); 1.29 at
      9 MIPS (S 9 against 11.6 frames a second).
    - No 15-to-3 swing appeared in any run here. The earlier D1 text said it
      would; `docs/bugs.md` D1 is corrected.
  - Speed study (`docs/speed-sweep.md`): the original's wait runs in this build
    (traced at 40 MIPS); 199 flights at 3-40 MIPS and on the 386 profile never
    swung. Game ticks: 70.08 a second at 7-8.5 MIPS, 67.9-68.2 at 9-10, 66.75
    from 11 MIPS up (one tick lost in 21 frames), 69.2 on the 386 profile. The
    controller assumes 60 ticks a second. The mission clock per emulated second,
    over nine typed routes: 1.28 on the 386 profile, 1.17 at 9 MIPS, 1.11 at 40;
    no speed gives 1.00. The manual (p. 177, as read by the agent) puts the top
    detail level at 'average 386 and above'.
  - Open (Phase 1): file loading drift (about +0.6 s by START's roster on the
    MS-DOS machine; the disk model is RAM-disk speed).
- **Reference machine**
  - All 86Box tools default to `vmt386dos500`. Re-baselined: `pc_parity` all
    pass; the pictures are 85 exact plus p085 listed by hash; music, timing and
    the roster hold.
  - Six register differences between the VM and our DOS (AX after INT 21h 3Eh
    and 49h, an EXEC child's AX/BX/DX, the entry FLAGS) are recorded in
    `tools/fidelity_baseline.json`. They are not copied; copy them only if a
    route is shown to read one.
  - `vmt386dos500h` is built and reproducible; DOS=HIGH makes INT 21h 0Bh and
    2Ch cost more, so it is not the reference.
- **Phase 2 (named, matched code)**
  - 762 addresses matched (757 of 1,535 census functions, 62,999 of 179,213
    bytes). 778 functions (116,214 bytes) remain.
  - Batches this session, each merged after a gate: VGAME (34 and 47), START/END
    (40 and 45), the small programs (21 and 44), and the C runtime's DOS layer
    (64; the lockstep now answers INT 21h and 16h for every routine).
  - Defects the merged gate caught that the branches' locksteps could not:
    `vgame_model_poly_finish` claimed 21 and 19 where its longest paths are 23
    and 21; `vgame_view_caption` was one clock late on a rare path; port bus
    delays crossing an event limit (`IO_SLACK`). The picture decoder's prefix walk
    checked room once for any depth (fixed; boot_to_flight matches again).
  - Ready leads, triaged equal and not yet written: VGAME 0x0B171, 0x0D6DD,
    0x04777, 0x01007, 0x00D14, 0x05046 and 130D:033F. Identical copies across
    programs were listed in the batches' commit messages; check the table before
    adding a row.
  - Open: START 0x11DB (a one-clock error, not retried); MPS_LOGO 0x1ADC and
    0x1C82; routes-only candidates (see the commit messages). The stack checks
    START 0xA4C6 and END 0x5B4E run only inline in the formatter scans.
- **Phase 3 (presentation and fixes)**
  - Fixes with a switch: 10 of 14 catalogued defects (D1, D2, D3, D4, D5, D6,
    D8, D11, D12, D34; D7 is fixed by D8). D3 was fixed on 9 Oct: keypad digits
    answer SETUP's sound question instead of quitting to DOS. Open: D10 (not
    located; no VGAME routine returns with the stack moved), D35 (not
    reproduced here), D36 (not reproduced: Munt's 2.0x ROMs play the engine
    3.4 dB quieter, not silent).
  - Live path (merged, off by default). `--present replay` redraws each frame
    from the original's draw records: exact on 447 phases, with graphics entry 41
    decoded as a colour replace and no page snapshots. `--present interp` shows
    in-between frames at vsync (headless: at most one missed refresh in 23,336
    frames). `--present-scale N` draws polygons at N times resolution (N = 9 is
    2880 x 1800 for 4K). `--present-age extrapolate` is an option that mispredicts
    where interp does not; interp stays the default.
  - Open: the HUD and text at 4K (the owner's call; the options and their costs
    are in `docs/presentation.md`); Stage 4 steadiness at 60 and 144 Hz on a
    real display (the owner's eyes).
- **Phase 4**: untouched (a person playing it; Roland by ear).

## Open items, in order

1. **The default frame rate** (the owner's decision). The clock runs faster than
   real time at every speed measured, so no setting is real time as the game
   stands. Candidates: GOG's DOSBox pace (the current default), the 386 profile
   (the hardware the manual names), and a limiter at 15 frames a second (the
   hypothesis that keeps the clock real; not written yet). Before choosing,
   measure what speed does to the enemy: launches and hits on the player, our
   hits, and control response, over many missions at each speed with the same
   mission (fixed seeds: the front end's timing changes the mission below 9
   MIPS and on some routes above it).
   The tick question, which the owner's decision depends on: the controller
   assumes 60 ticks a second, while the game's timer is calibrated to the 70 Hz
   VGA retrace (`0x1D01`, reload 17024). If the original's timer is 70 Hz on a
   real VGA, its world runs about 70/60 = 1.17 times real time by design, even
   with a perfect frame controller. This is the speed study's reading of the
   listing, not measured on hardware (none is available here); settle it with
   the owner, since it decides whether parity means that speed too.
2. **Fixes under `--timing 386`**: a fix counts instructions, not 386 cycles,
   when it runs under the 386 profile (matched routines are run as the original
   body there; fixes are not). Decide: refuse fixes with `--timing 386`, or
   charge them cycles. The default is `dosbox`, so this is not reached by
   default.
3. **Mac and Amiga research** (`D:\f117-gate\macresearch\`): the Mac 2.1 port keeps
   the DOS controller with the cap raised to 60, so its TickDelay is effectively
   0; Mac 2.3 adds Quick, Fast and Warp modes that break real time, and its
   mission clock ticks every 8 frames (unknown whether that was deliberate); the
   Amiga caps frames at about 10.7 with a 6-tick floor and keeps S. No port keeps
   a fixed simulation step separate from drawing. Record this in
   `docs/roadmap.md` and `docs/bugs.md` once the default is decided. The Mac 2.0
   version is not in the archives.
4. **Phase 2**: continue from the ready leads with the sharded lockstep. Keep
   the gate's routes-only and OVERRUN checks green. After each batch, refresh the
   census and every count in README, roadmap, handoff and architecture.
5. **Phase 3**: the owner's decisions (the HUD and text at 4K; the Stage 4 display
   check). D10 and D35 remain unlocated or unreproduced.
6. **Reference machine**: keep `vmt386dos500`. Decide with the owner whether to
   copy the six DOS register differences.
7. **Checks still to tighten**: DOSBox-X's and GOG's picture comparisons count
   misses that move between captures, so a name list does not hold there. The
   lockstep's DOS answers are deterministic rather than the real DOS; the routes
   check the real one.

## Traps that cost time

- **Shell and Python escapes**: inline Python heredocs turn `\n`, `\f`, `\a` and
  `\d` into real characters (a `\f` in a path became a form feed; `"\aq"` became
  a bell). Use the Edit or Write tool for strings with escapes, or a script file.
- **Stale binaries**: a usage message that lacks a new option means the build
  predates the edit. Rebuild before testing. The old mutation-check binary
  was replaced by the merge9 gate and the shadow-text fix build.
- **Git Bash and cmd**: `cmd /c` in Git Bash is rewritten as a path and starts an
  interactive shell. Use `MSYS_NO_PATHCONV=1 cmd /c "C:\...\build.cmd"
  "-DF117R_GEN_DIR=C:/Users/Tideg/f117-recomp-local/gen"` with the absolute path,
  because `cmd` does not start in Bash's directory.
- **Merge conflicts**: `git checkout --theirs <file>` replaces the whole file,
  not the conflict. To recreate a conflict, use `git checkout -m <file>` and
  resolve the hunks. Check for duplicate matched rows with
  `grep -oE '\{ "matched", ...' | sort | uniq -d`.
- **Registration order** no longer decides which override runs: a switched-on
  fix runs first at its address. Observers return 0, so the original
  instruction runs after them. Observers at a matched routine's address run
  under the interpreter as well.
- **Unit tests in the gate**: the gate runs them after the build now, as CI
  does. CI caught an override-registry capacity failure the local gate had
  missed (the registry is 4,096 entries and warns when full).
- **Code below an entry**: the lockstep watches a routine's own code from its
  entry and accepts its RET only inside that span; `CODE_BELOW` widens the span.
  A routine whose RET or code lies below its entry is skipped silently unless
  listed, and a random DS can put a table over that code unseen. A low count of
  states compared is the tell: VGAME 1377:00F3 compared 341 states before its row.
- **The MS-DOS result was misattributed**: `b86_cargo_pilot.py` defaulted to
  FreeDOS `vmt386`. Check which profile a tool actually runs before blaming a
  machine.
- `d1_check.py` flies `boot_to_flight` by default: a parked jet, not a fight.
  Use `--route` for busy missions.
- `progress.py --title` rewrites `docs/progress.json`: commit it with the docs.
- **Engagement results across speeds need the same mission.** The front end's
  timing seeds the mission generator, so below 9 MIPS and on some routes above
  it the missions differ. `tools/speed_sweep.py` flags runs whose mission differs
  from the 9 MIPS run; compare hits and launches only on matching missions.
- **Background work**: a killed run can leave `86Box.exe` or `f117run.exe`
  holding files; look with `tasklist` before and after. Run long jobs in the
  background; never sleep in the foreground.
- The probe (`probe386.py`) masks every IRQ, because the BIOS and DOS enable
  interrupts inside their calls. Microsoft MOUSE.COM 9.01 hangs on the 386 board.
- MSVC at /O2 stalls on `x86_stos`, `x86_lods` and `x86_cmps` in matched
  routines: write those out by hand (`sm4_stosb`, `sm4_step`). A normal
  matched.c compile takes about 4.5 minutes.
- VGAME exiting 129 with parent result 2 mid-flight is the original's
  render-detected terrain collision, not an engine fault.
- Game keys (Key Control Card): 0 brakes, 6 gear, 8 bay, Space select weapon,
  Enter fire, Backspace cannon, B select target, Shift+F10 eject. `f117run`
  input: Space is `\s` in route files; at most 4096 inputs.
- DOSBox 0.74 sees posted keys only with `SDL_VIDEODRIVER=windib`; DOSBox-X does
  not take posted keys; the release 86Box cannot be typed into (use the VNC
  build, `tools/ref86box/build_86box.md`).
