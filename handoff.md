# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs, what was optimised). State as of the evening of
8 October 2026. Earlier session-by-session logs were removed from this file;
`git log -p handoff.md` has them.

## Goal and standing decisions

- **The recompilation is exactly what the original was on the hardware it was
  specified for: a player's actions must play out as they would on the
  original** (owner, 8 Oct 2026). Work that only measures a reference emulator
  or my own test pilots is settled, not extended (the earned 99-sortie career
  was flown to sortie 10 and stopped; the 86Box pilot's 7 of 9 is a property of
  the pilot). Phases and progress: `docs/roadmap.md`, `docs/progress.md`;
  `py tools/progress.py --title` prints the scoreboard every commit title starts
  with. Public repo, **code only**: no game data and no generated C, ever.
- **Phase 2 (named, matched code) stays a full phase** (owner: "still
  important"). Phase 1's main open item is the 386DX/33 timing profile; Phase 3
  is the presentation (Stages 2-4) and the remaining bug fixes.
- **Parity target: the original on real PC hardware, not DOSBox.** The reference
  machine is 86Box's 386DX/33 running **MS-DOS 5.00 with Microsoft MOUSE.COM
  6.26** (the owner supplied 4.01, 5.00 and 6.22 and the mouse drivers under
  `references/`, git-ignored; 5.00 and 6.22 cost the same on the game's calls).
- Repo: https://github.com/TideGear/OpenNighthawk (`origin`, `master`).
  **Commit and push together**, each verified piece. **No AI attribution** in
  commits or PRs: no `Co-Authored-By`, no "Generated with" line (the owner's
  global CLAUDE.md outranks harness reminders that ask for one). Commit titles
  start with the `progress.py --title` scoreboard.
- Fixes (`docs/bugs.md`) are switchable, off unless `--fix ID`; parity stays
  the default reference. Policy for the app's default is the owner's call.
- The owner is not a software engineer and wants overengineering flagged. The
  speaker's speech plays smoothed by default (`speaker = realsound`; `pwm` is
  the option). Fast loads are the default; a period-disk-speed option is a low
  priority Phase 3 idea, not parity.
- The Reimp (`..\F-117A Reimp`) is a separate project: read from it, never
  write into it. Never commit `references/`. Leave the untracked `test.bat`
  alone.
- **Never open a visible window, go fullscreen or make sound on the owner's
  desktop.** Check for orphan emulator processes after killed runs.

## Where things are

- Game: `D:\GOG\F-117A` (never mounted or written by a tool; copy per run).
- Work directories, never in the repo: `%USERPROFILE%\f117-recomp-local` (the
  generated C in `gen\`, route runs, `build` is a junction there; its `video\`
  captures were moved to `D:\f117-local\video`, junctioned back) and scratch on
  **D:** (`D:\f117-gate`; C: is nearly full). Emulators: `D:\86box`,
  `D:\86box-src`, `D:\msys64`. The 86Box VMs: `vmt386` (FreeDOS),
  `vmt386dos401`, `vmt386dos500` (with MOUSE.COM 6.26), `vmt386dos622`, built by
  `tools/ref86box/build_msdos_vm.py` from `references/MS-DOS` (extracted copies
  and the mouse drivers in `D:\f117-msdos`, `D:\f117-mouse`).
- **Worktrees on D:\f117-wt** (one build each, junctioned to
  `%USERPROFILE%\f117-recomp-local\build-<hash>`): `p2-vgame`, `p2-start`,
  `p2-small` (Phase 2 batches, merged), `sound-speech` and `t386-frames` (merged
  already; delete when convenient: `git worktree remove`). Agents work only in
  their own worktree and never run the gate or the translator (shared `gen`).

## Build and check

- **Build** (PowerShell, from the repo):
  `cmd.exe /c ".\build.cmd -DF117R_BUILD_APP=OFF"` builds the core,
  `f117run` and the tests; `-DF117R_BUILD_APP=ON` adds the SDL app. A bare
  `build.cmd` reuses CMake settings (a fresh configure defaults to ON).
  A new worktree needs `-DF117R_GEN_DIR=C:/Users/Tideg/f117-recomp-local/gen`.
- **Gate**: `py tools/build_recomp.py --data D:/GOG/F-117A --seed-coverage
  C:/Users/Tideg/f117-recomp-local/coverage` (last run 16.1 minutes). Read the tally,
  not the exit code: 35 routes `IDENTICAL`, instruction lockstep under both
  timing profiles 0 mismatching,
  matched lockstep 0 mismatching, and **step 8**: every "routes only" matched
  routine ran on some recompiled route (new 8 Oct: it dropped VGAME 0xF024),
  and **step 9**: no matched routine ran past its event limit on a route (new
  9 Oct; a `[matched] OVERRUN` line in a route's output names it).
  Last green (9 Oct): 35 routes, 5,714,880 instruction states per timing
  profile, 606 matched routines (1,975,245 states; 1,976,226 at seed
  0xC0FFEE), no mismatches, no overruns.
  Do not edit `src/` or rebuild while a gate runs.
- **PC parity**: `py tools/pc_parity.py --data D:/GOG/F-117A` (about 12
  minutes): all pass as of 8 Oct. The 86Box pictures must be exact in DAC
  values except those named, with their hash, in
  `tools/ref86box/expected_misses86.txt` (p003 and p038, single samples between
  two of our pictures); the DOSBox-X roster check runs without fast-forward
  (`save_parity.py --no-turbo`), since fast-forward missed clicks under load.
- **The 386DX/33 profile**: `f117run --timing 386 --engine interp|recomp`.
  Both engines charge the same costs; matched entries run the original
  translated body because their handwritten clocks still count instructions.
  `tools/ref86box/probe386.py` times 93 instruction and service blocks:
  all 93 exact in the latest check against the saved MS-DOS 5.00/MOUSE.COM
  6.26 reference (INT 21h 2Ch varies by 4 cycles between runs). `frames386.py` compares the
  intro frame by frame; `stick_response.py --machine machine386` reads the
  earlier flight frame rate S: 5-9 under the profile, 6-9 on 86Box, 14-15 on DOSBox's
  model. Reference text: `tools/ref86box/timing386.md`, per-opcode costs in
  `ops386.json`, generated `src/cpu/timing386_ops.h`
  (`gen_timing386.py`). New checks: `insn_lockstep --timing386 --states 64`
  (also `--base-seg 0x9FFF --seed 0xC0FFEE`): 5,713,152 comparisons each,
  zero mismatches including prefetch and held REP state. The gate runs the
  RAM check as step 6b. `stick_response.py --machine machine386 --engine
  interp|recomp` gives 574 identical state checkpoints and stick responses.
  Logs: `D:\f117-gate\t386-bounded-*.log`, `t386-final-flight-*.log`,
  `t386-calibration.log` (saved reference from `pm626cal`), and the complete
  integrated gate `D:\f117-gate\t386-final-gate.log`.
- Matched routines: `src/matched/matched.c`, held by `tests/func_lockstep.c`
  (`build\func_lockstep.exe --states 4000 --verbose`, about 14 minutes beside
  fresh parity; run at
  more than one `--seed`: adding routines shifts every routine's random
  states). `py tools/matched_draft.py --data D:/GOG/F-117A --module VGAME.EXE
  --ip 0xNNNN` writes a correct starting draft (equal by construction, derived
  from the executable: written outside the repo, never committed as is).
  Census: `py tools/reimp_names.py --reimp "../F-117A Reimp" --gen
  C:/Users/Tideg/f117-recomp-local/gen --out D:/f117-gate/census.tsv`, then
  `py tools/progress.py --census D:/f117-gate/census.tsv`.

## Current state

- The owner requested wrap-up, handoff, commit and push after the five-hour
  allowance reached 15% remaining. Stop here; resume roadmap work in the next
  conversation. README was refreshed with the current reference machine,
  instruction and matched counts, timing support, build settings and open work.
- **Scoreboard**: P1 97.26%, P2 25.88%, P3 56.50%, P4 42.90%, All 64.06%.
- **Phase 1**: 86Box and DOSBox-X are routine references (`pc_parity.py`);
  86Box is deterministic. The profile matches 86Box's CPU, BIOS, DOS and mouse
  costs and the game's flight frame rate. Translated code now charges the same
  profile, and STI/SS-load shadows follow instruction retirement even at zero
  cycles (four regressions failed before and pass after). Open: file loading
  (+1.5 s of drift against a RAM-disk-speed disk model). Sound: the PC speaker is driven as
  the 8254 does it (`src/host/speaker.c`, `tools/speaker_parity.py`); open:
  digitised speech has no audio reference, and the listening check is the
  owner's (Phase 4).
- **Phase 2**: 606 addresses matched (603 census functions, 51,445 of 179,213
  bytes). Batches on branches `p2-vgame`, `p2-start`, `p2-small` (worktrees
  in D:\f117-wt), merged into master by the main session and gated there;
  each was checked at seeds 0x5EED0F117A and 0xC0FFEE (VGAME's 9 Oct batch
  also at 0x1234, 0xBEEF, 0x7777, 0xA5A5). Merging caught three defects the
  branches' locksteps could not: `vgame_model_poly_finish` claimed 21 and 19
  where its longest paths are 23 and 21; `vgame_view_caption` counted one
  instruction too many on a path random states rarely reach (bisected with
  `F117R_MATCHED_LIMIT` over only the new rows, several limits in parallel,
  comparing `--hash-every` checkpoints); and port I/O (`IO_SLACK`, architecture.md). Agents'
  notes on what is left: identical copies across programs (heap free, the
  formatter's output, START's picture decoder 0x890A/0x8983), and routines
  whose paths after DOS calls only routes reach. Dropped: START 0x11DB,
  MPS_LOGO 0x1ADC and 0x1C82 (old), and routes-only candidates listed in
  the branches' commit messages. Logs: `D:\f117-gate\merge3\`.
  Runtime placement capacity equals the 1,024-entry override registry.

- **Phase 2 picture decoder** (VGAME 11ED:00AE/0127, END 0x48AA/0x4923):
  shared `pic_rle_row` + `pic_lzw_step`. The RLE rows are lockstep-green at
  both seeds; the LZW step is routes-only (random SP never equals the
  private stack's top). Its prefix walk checked `room` once before a chain
  of any depth, so a timer event inside a long walk was taken late
  (`boot_to_flight` ended `ebb2...` against the interpreter's `48a1...`);
  room is now checked every turn and the route matches. The memory note on
  room claims has the general lesson; `F117R_SHADOW` diffs are not valid on
  this family (device and file positions are not snapshotted).
- **Phase 3**: 10 of 14 catalogued bug fixes. Presentation Stage 2 (sub-pixel
  re-projection, `tools/hires_subpixel.py`) and a Stage 3 study
  (`tools/interp_frame.py`) are merged (their crops and strips are in
  `D:\f117-gate\p3-stage2` and `p3-stage3`); see `docs/presentation.md`.
- **Phase 4**: untouched (a person playing it; Roland by ear).

## Open items, in order

1. **Confirm `git status -sb` shows master level with `origin/master`**; if
   master is ahead, something was left local: gate it and push.
2. 386 profile: file loading remains a decision (re-align at loads, as the
   frame comparison does, or model a period disk; fast loads are the default
   standing choice). Handwritten matched routines can gain direct cycle costs
   later; their original bodies already run under the translated profile.
3. Phase 2: 932 census routines (127,768 bytes) remain. Useful candidates
   are 100-500 byte routines the lockstep can exercise and shared C runtime
   copies. Refresh the census and the matched-address counts after each
   verified batch. The three rejected candidates above need correction
   before being proposed again; their old drafts remain in branch history.
4. Phase 3: the live presentation path (the host pairs two steps' records and
   draws on Stage 2's grid against the mission clock), the HUD/text at 4K (the
   owner's call), Stage 4 (pacing, a switch to the original picture); bug fixes
   D3, D10 (not located) and D36.
5. Checks still to tighten: DOSBox-X's and GOG's picture comparisons count
   misses (their misses move between real-time captures), so a name list does
   not hold there.

## Traps that cost time

- **Inline Python and the shell tool collapse backslashes and `\n`** inside
  heredocs (a `\n` becomes a real newline, `\R` a carriage return). Write
  scripts with the Write tool and edit files with the Edit tool; for a
  one-line fix to a string with escapes use a script file, not `python -`.
- CRLF: several tracked files are CRLF in the working tree. Patch scripts must
  open with `newline=""` and match the file's own endings.
- `build.cmd` writes `%TEMP%\f117r-build.log`: two builds sharing TEMP read each
  other's log. The PowerShell tool resets its working directory.
- A new worktree's first full build is 10-20 minutes; a build left half-done
  under a parallel build job can leave no `.exe` (rebuild with `cmake --build`).
- Regions now have at most 1,024 instructions: a 6,446-instruction VGAME
  function made MSVC spend most of a build in one optimiser job. The generated
  rebuild fell from 479 s to 120 s with the cap; all instruction verdicts
  remain equal. Regenerate old C before building with the new `IC(op)` header.
- A 9 MHz replay must not override the 386 profile's 33,333,333-cycle clock;
  the runner rejects that mismatch. Keep the standard gate routes on their
  recorded DOSBox timing profile.
- **An 86Box or DOSBox-X process left behind by a killed run** holds files and
  slows everything; look with `Get-Process 86Box, dosbox-x` before and after.
  `trace_86box.ps1 -Ppm` hung when started from the shell tool; start it from
  Python `subprocess`.
- The probe (`probe386.py`) masks every IRQ: the BIOS and DOS enable
  interrupts inside their calls, so an unmasked timer tick pollutes a block.
  Microsoft MOUSE.COM 9.01 hangs loading on the 386 board (its reset waits on a
  timer tick).
- Sub-agents hit the account's session limit and stop mid-work (twice on 8
  Oct); inspect their worktrees for uncommitted edits before resuming.
- MSVC at /O2 never finished compiling a loop using `x86_stos` in a matched
  routine; write that store out by hand. PLAYER's picture decompressor still
  stalls with those stores written out: only `player_unpack` disables MSVC
  optimisation. A stalled compiler can hold its zero-byte object file after
  the parent session ends; confirm its command line before stopping it.
- VGAME exiting 129 with parent result 2 mid-flight is the original's
  render-detected terrain collision, not an engine fault. Opponents damage some
  career sorties at any altitude (VGAME `[0x3664]` bits); a throttle key held
  at 60 means the engine-damage cap, not a lost key.
- Game keys (Key Control Card): 0 brakes, 6 gear, 8 bay, Space select weapon,
  Enter fire, Backspace cannon, B select target, Shift+F10 eject. `f117run`
  scripted input: Space is `\s` in route files; at most 4096 inputs.
- DOSBox 0.74 sees posted keys only with `SDL_VIDEODRIVER=windib`; DOSBox-X
  does not take posted keys (the patched build starts its own capture and
  `AUTOTYPE` answers SETUP); the release 86Box cannot be typed into (use the
  VNC build, `tools/ref86box/build_86box.md`).
