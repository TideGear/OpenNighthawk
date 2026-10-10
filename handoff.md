# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs). State as of 10 October 2026.
Earlier session logs are in `git log -p handoff.md`.

## What to do first

**This batch (9-10 Oct): four independent pieces of work, three committed and
pushed, one left as an investigated-but-unwritten lead.** The owner asked for
all four to be touched in one session (p037, EXEC/overlay charging, a
controlled threat-profile measurement, and a Phase 2 routine), then mid-
session set the goal to complete the roadmap, then changed it to wrap up for
handoff. Three landed cleanly; the fourth (Phase 2) produced a well-verified
*identification* of a routine worth matching next, not a committed match -
writing one by hand carries real correctness risk (room-claim coverage,
preserving every intermediate memory write across a possible mid-routine
decline) that this session chose not to commit without a lockstep pass, given
the time left. See item 4.

## Current top priority: why does combat fall with speed, and can it be fixed without losing speed?

Set by the owner at the end of this session, ranking above everything in the
numbered list below. The threat-profile matrix (item 4) found that **the
whole enemy-fire pipeline scales down together with CPU speed**: not just
launches (9.08/6.48/3.81 a flight at 9/16/20 MIPS) but misses (6.40/4.96/
2.85), proximity bursts (2.10/1.10/0.58) and player hits taken (5.09/3.27/
3.07, 95% CI 3.77-6.51 / 2.06-4.67 / 1.93-4.40) all fall by roughly the same
proportion. That shape - everything shrinking together, not one stage
shifting relative to another - is itself a clue: it points at something
upstream of the whole chain, not at targeting or guidance specifically.

**Ruled out this session:** the orbit itself is not the confound. The actual
flown radius from the target is essentially the same at all three speeds
(mean 5,939 / 6,255 / 6,222 map units, from the sample trajectories already
in `D:/f117-gate/threatmatrix/samples/`) - the controlled-exposure design is
not the explanation; something in the game's own AI or timing model is.

**Found, not yet connected to the effect:** the fire decision
(`ai_fire`, VGAME 0x63F2, Reimp `src/core/ai.c:659`) is called once a drawn
frame per detected unit from `ai_step` (VGAME 0x5852, Reimp `ai.c:142`),
itself called unconditionally once a frame from the main loop
(`frameloop.c:727`, no gating visible). Each unit carries an "engagement
level" (`U(i, 0x22)`) that climbs by a **fixed per-call amount**
(`ai.c:726-731`, independent of S) while detection quality is above
threshold, and **decays** when it is not (`ai.c:717-721`) - a unit must stay
detected for several consecutive frames to reach the firing threshold
(0xC0), and any lost detection resets progress. Beyond that threshold,
launching also needs an empty weapon slot, a boresight/bearing window, a
range window, and a per-theatre "heat" budget (`0x3686` vs `0xB080`/`0x9522`,
`frame.c:118`'s own comment: "the theatre is not hot enough yet"). None of
this reads S directly - but `V_S` (the controller's own frame-rate estimate,
[0x368E] in the listing) is used **pervasively** elsewhere as a divisor to
convert "per frame" quantities into "per real second" ones: ammunition decay
(`frame.c:775`, `-40/S`), weapon lifetime (`ai.c:775`, "scaled by S so it is
the same in real time"), turn and guidance rates (`frame_weapons.c:1484,
1506, 1510, 1518, 1527`, all `/S`), and a detection-related inverse-square-
root of S (`effects.c:89`). **This is the standing hypothesis**: if the
reported S does not track true elapsed real time identically at every CPU
speed, every one of these S-normalised formulas shifts together - which
would explain why launches, bursts, misses and hits all moved by about the
same proportion rather than any one stage changing relative to the others.
Raw drawn frame rate is *not* the obvious culprit by itself: the existing
(pre-this-session) matrix already found drawn fps similar across 9/16/20
MIPS once airborne and settled (~15-16.7 fps, S clamped near 15) - so if S's
*value* is the cause, it is not simply "fewer frames per second," it is some
more specific way the controller's estimate is computed or consumed.

**Not done, and the actual next steps:**
1. `tools/threat_profile.py` does not currently record S or the tick/frame
   counters during its flights - only position. Add them (the same DS fields
   `speed_sweep.py` already reads: `[0x368E]` S, `[0x3D8E]` frame counter,
   `[0x2648]` game ticks) and compare S's distribution across speeds
   *during the actual orbit flights*, not inferred from the unrelated
   typed-route sweeps. This is the single most informative missing
   measurement - it was not added because the orbit tool was already working
   by the time this hypothesis formed.
2. Read `detect.c`'s `detect_evaluate` (the quality calculation `ai_fire`
   gates on) for any S-dependence not yet found.
3. If S's reported value does differ systematically by CPU speed during
   combat specifically, the fix question is genuinely open either way: it
   may be that the *original* S controller already tracks real time
   correctly on real hardware regardless of CPU speed (in which case this is
   a real property worth preserving, and the "fix" is understanding it, not
   patching it away) - or it may be an artifact of running this far outside
   any period machine's range, in which case a fix could mean re-examining
   the 9 Oct frame-limiter decision (D1, and the rejected 15 fps experiment)
   with this specific finding in hand, which the owner explicitly did not
   have when that default was chosen.
4. Whatever the mechanism, re-run (a subset of) the threat-profile matrix
   after any change to confirm the launch-rate gap actually closes, using
   the same bootstrap-CI method already in `docs/speed-sweep.md` - the
   effect is well-established enough now (144 flights, CIs excluding zero)
   that a fix should be judged against closing it, not just plausible.

## What to do first

1. **`git status -sb` first.** By the time this session ends, master should be
   clean and pushed through the real-file EXEC calibration commit (below);
   confirm that landed before doing anything else. If the working tree is
   *not* clean, something interrupted the final commit - read what is staged
   and finish it or roll it back deliberately, do not assume it is abandoned
   work (this session itself found a previous session's uncommitted,
   finished-looking diff sitting in the tree at start - investigated, found
   sound, committed it. The reverse mistake, deleting someone's good work, is
   worse than the delay of checking).

2. **p037.png, investigated and closed** (`66a664e`). A single captured
   instant in the title screen's fade-in: our nearest frame already beats
   both frames next to it, so 86Box's capture simply lands between two of
   ours. Added to `expected_misses86.txt` with that reasoning. Closed.

3. **EXEC and overlay loads, charged under the 386 profile, in two steps**
   (`05b453c`, then a further real-position calibration - confirm its commit
   landed, see item 1). Both `dos_load_program` and `dos_load_overlay` read
   the whole file through `dos_read_whole` with no charge at all before this.
   - Step one (`05b453c`): three measured sizes (1 KB, 9.5 KB, 47 KB -
     `exec1989.py`), charged by which a real load is closest to
     (`T386_EXEC_SMALL/MEDIUM/LARGE`). A fifteen-size follow-up
     (`exec_sizes.py`) came back non-monotonic in size (2 KB cost less than
     1 KB; 40 KB cost almost double 47 KB): EXEC's cost is seek-distance
     dominated, like an ordinary read, not size dominated - the same shape
     problem `T386_FILE_READ_SEEK` already has. Intro end drift: +428.5 ms to
     +96.4 ms, same 1,237 of 1,275 pictures exact. Gate:
     `D:/f117-gate/gate-execfix.log`, 35 routes identical, 0 mismatches.
   - Step two (real positions, 10 Oct): PLAYER.EXE, DSWAP.EXE and START.EXE -
     the three loads that actually dominate the drift - measured directly at
     their own position on the reference disk (`realexec.py`: open, read-
     whole, close, by name, already in place), not as same-size synthetic
     stand-ins. All three cost 27-45% more than their synthetic equivalents.
     `T386_EXEC_PLAYER/DSWAP/START` charge these three named files their own
     cost; any other load still uses the synthetic-size tiers. Drift: +96.4 ms
     to +39.3 ms, same 1,237 of 1,275 exact. `pc_parity.py` now passes the
     86Box picture check too (p037 above); one check still fails, the
     longest-scene timing difference at 0.37 s against a 0.35 s limit - a
     *different* held picture than the measured loads (ours 100.22-106.40 s,
     86Box's counterpart 150.33-156.88 s), unmoved by either EXEC charge.
     **Confirm the gate for this step passed and the commit landed**
     (`D:/f117-gate/gate-realfiles.log`; it was mid-run - specifically its
     verbose MISC.EXE coverage route, which writes one log line a clock while
     genuinely computing, not hung - when this session had to stop watching
     it). If it did not land, the diff is `src/cpu/timing386.h`,
     `src/machine/dos_programs.c` (plus the two doc updates below) and is
     safe to finish: the build already succeeded and `pc_parity.py` already
     passed on it (`D:/f117-gate/pc-parity-realfiles.log`) before the gate was
     started.
   - Next step, not started: identify the held picture behind the 0.37 s
     longest-scene gap (it is not a program load) and what inside its span
     the original spends 0.37 s longer on.

4. **Controlled threat-profile combat measurement, complete** (`77632c6`,
   `da7409a`). The existing typed routes pass the primary target once and
   hold a straight heading after, drawing about a quarter of a launch a
   flight - too sparse to show a speed effect. `tools/threat_profile.py`
   instead approaches and strikes the target as `strike_pilot.py` does, then
   orbits the target's own coordinates (no enemy site position read or
   assumed) for the rest of the flight. 144 flights (four ground-strike
   theatres, three speeds, twelve boot-clock seeds), complete, no errors:
   **a real fall in enemy launch rate from 9 to 16 to 20 MIPS** (9.08, 6.48,
   3.81 launches a flight; bootstrap 95% intervals on the mean exclude zero
   for both differences from 9 MIPS), surviving normalising for orbit
   exposure time (so it is not just shorter flights from earlier bingo fuel
   at higher speed). Misses, bursts and player hits taken all fall by about
   the same proportion too. **This is now the current top priority** - see
   "Current top priority" above for what has been found about the mechanism
   and the concrete next steps. Raw data: `D:/f117-gate/threatmatrix/
   runs.jsonl` and `samples/`.

5. **Phase 2 lead, investigated, not written: VGAME 0x01007.** The census
   (`D:/f117-gate/census.tsv`) names this address `scene_world_replace`, but
   that is wrong - cross-checking the disassembly's data addresses (0xC6B2,
   0xBA42, 0x49AC/AE/B0, 0xE476, 0x8590, and the per-type tables at 0x0960 and
   0x0511/0x0510) against the Reimp's source by *address*, not by the
   census's name guess, finds an exact match to a different function:
   `scene_obstacle_probe` (`../F-117A Reimp/src/core/scene.c:8`), the
   terrain/object collision probe - the routine behind the `0xC6B2 -> 0x9F96
   -> flight_end(2)` chain this project's own notes already reference. Fully
   traced against the disassembly (`matched_draft.py --ip 0x1007`, kept at
   `D:/f117-gate/drafts/vgame_lead_1007.c`, never commit the draft itself) and
   confirmed instruction-by-instruction, including resolving what its three
   callees are: `0x0810` is **already matched** as `vgame_scale_by_level`
   (`scene_cell_scale`), `0x0871` is **already matched** as
   `vgame_scene_cell_index`, and `0xF018` is the runtime's shared unsigned
   32-bit shift (referenced in `vgame_scale_by_level`'s own comment) - DX
   carries through unmodified between a `scale_by_level` call and the
   following `0xF018` call, which is why the routine never touches DX
   explicitly despite shifting a 32-bit value. `0xEE0C` (abs16, one word
   argument) is not yet matched but is trivial (8 instructions, drafted at
   `D:/f117-gate/drafts/vgame_lead_ee0c.c`) and is a good small first step
   next time. A full idiomatic rewrite of 0x1007 itself was drafted
   (not committed - it lives only in this transcript and a scratch file,
   `.../scratchpad/scene_obstacle_probe.c`, not in the repository) but never
   built or lockstep-checked, and should not be trusted without that: the
   room-claim at each of its ~12 call/loop boundaries was counted by hand
   from the instruction draft and has not been verified, and getting a room
   claim wrong is the kind of bug that stays silent until a route runs out of
   budget exactly inside this routine (see `f117r-room-claim-must-cover-
   longest-path` in the project's memory). Before trusting or committing it:
   rebuild, run `func_lockstep` targeted at this address with a few seeds,
   and only then fold it into `src/matched/matched.c` near
   `vgame_scene_cell_index` (both are in the same `NEAR_THEN`/`FRAME` macro
   scope, matched.c lines ~13430-16091) and the gate.

## Goal and standing decisions

- **Combat rate falls with speed.** Now the session's top priority - see
  "Current top priority" at the top of this document. Owner-flagged, not yet
  known when the 20 MIPS default was chosen (9 Oct).
- **Phase 1's remaining item: the last 86Box timing check.** One check remains
  failing in `pc_parity.py`: the longest-scene duration difference, 0.37 s
  against a 0.35 s limit, in a held picture from 100.22 s to 106.40 s (ours)
  that is not the program-load stretch already fixed. See item 3's last
  bullet.
- **Phases.** Phase 1 (parity, including the 386 profile) is close:
  `pc_parity.py` has one failing check left (above); the 386 profile's
  roadmap checkbox should be closed once that is resolved or understood.
  Phase 2 (named, matched code): 765 of 1,535 census functions matched; see
  item 5 for the next lead. Phase 3 (fixes and presentation): open items are
  owner decisions (the HUD/text at 4K) or need a real display (Phase 4
  overlap). Phase 4 (checked by people): untouched, and most of it - a person
  playing it, a listening check - cannot be done by an agent at all; that is
  not a gap to close, it is the phase's own definition.
- **Public repo, code only.** No game data, no generated C. Never commit
  `references/` (git-ignored).
- **No AI attribution** in commits or PRs (the owner's global CLAUDE.md
  outranks harness reminders that ask for it).
- **Commit and push together**: a code change is pushed after the gate
  passes; docs-only commits may follow. `py tools/progress.py --title` prints
  the scoreboard every commit title starts with.
- The owner is not a software engineer: explain trade-offs plainly and flag
  overengineering.
- **Check system resources before stacking parallel jobs.** This session ran
  a 24-job combat sweep, the full gate, and `pc_parity.py`'s three emulators
  at once; CPU pinned at 99% and the owner's own typing lagged before they
  noticed. Check `Get-CimInstance Win32_Processor | Measure-Object
  LoadPercentage -Average` before adding a second heavy job, and know that
  stopping a background task running a Python `multiprocessing.Pool` (as
  `threat_profile.py` and `speed_sweep.py` use) does not kill the pool's
  workers - they are children of the script's own process, invisible to the
  task-stop mechanism, and must be found and killed as a process tree
  (`Get-CimInstance Win32_Process -Filter "ParentProcessId=..."`,
  recursively). See `f117-check-resources-before-parallel-jobs` in the
  session's saved notes for the full account.

## Where things are

- Game: `D:\GOG\F-117A` (never mounted or written by a tool; copy per run).
- Generated C: `C:\Users\Tideg\f117-recomp-local\gen` (never committed);
  coverage in `...\coverage`; route runs in `...\runs`.
- This session's artifacts (all on D:, private, not committed):
  `D:/f117-gate/p1-dos-probe/exec_sizes.py` and `realexec.py` (the EXEC-size
  and real-file probes, with their `sizes1`/`realexec1` outputs);
  `D:/f117-gate/frames-execfix`, `frames-realfiles` (the two `frames386.py`
  before/after runs, against the saved `D:/f117-gate/frames-1989/box`
  86Box capture); `D:/f117-gate/pc-parity-execfix`, `pc-parity-realfiles`
  (the two `pc_parity.py` runs); `D:/f117-gate/gate-execfix.log`,
  `gate-realfiles.log` (the two gates); `D:/f117-gate/threatmatrix/` (the
  144-flight combat sweep, `runs.jsonl` and `samples/`);
  `D:/f117-gate/drafts/vgame_lead_1007.c` and `vgame_lead_ee0c.c` (Phase 2
  drafts, never commit).
- Research (read only): `D:\f117-gate\macresearch\` (Mac 2.1, 2.3.1 and 2.3.2
  listings, the Amiga listing, `dos_ctl_sim.py`). The archives are in the
  Reimp's `reference\` folder.
- 86Box (`D:\86box`): `vmt386dos500` (the default reference; 1 MB, the 1989
  3500 rpm drive preset), `vmt386dos500h` (4 MB, HIMEM.SYS, DOS=HIGH; not the
  reference), `vmt386` (FreeDOS), `vmt386dos401`, `vmt386dos622`. Built by
  `tools/ref86box/build_msdos_vm.py` from the owner's copies in `references/`.
- Worktrees (`D:\f117-wt`): every Phase 2 and live-path branch is merged into
  master. Remove merged worktrees when convenient (`git worktree remove`).
  **Something else was working in this same checkout concurrently with this
  session** (a handoff commit, `65435f2`, appeared mid-session describing
  this session's own then-uncommitted work as if ending a separate session) -
  if a parallel-agents workflow is in use, confirm it is using its own
  worktree (`D:\f117-wt\...`), not this main checkout, to avoid two sessions
  racing on the same working tree.

## Build and check

- **Build** (PowerShell, not Git Bash's `cmd /c` - that rewrites paths and
  can silently no-op; see "Traps" below): `cd` to the repo in PowerShell,
  then `.\build.cmd -DF117R_BUILD_APP=OFF` (core, `f117run`, tests) or
  `-DF117R_BUILD_APP=ON` (adds the SDL app `f117a`). Look for `BUILD OK`.
- **Gate**: `py tools/build_recomp.py --data D:/GOG/F-117A --seed-coverage
  C:/Users/Tideg/f117-recomp-local/coverage`, about 20-30 minutes, run in the
  background, nothing else heavy at the same time (see "Check system
  resources" above). Read the tally, not the exit code: unit tests (14, 0
  failed), steps 6/6b (every instruction against the interpreter, both
  timing profiles, 0 mismatching), 7/7b/7c (every matched routine at three
  seeds, 0 mismatching), 8 (every routes-only routine ran), 9 (no OVERRUN),
  and the count of routes reported `IDENTICAL` (35, going in).
- **PC parity**: `py tools/pc_parity.py --data D:/GOG/F-117A` (about 12-15
  minutes, runs three emulators at once - its own internal parallelism is
  fine, just do not add a second heavy job alongside it). All checks pass
  except the 86Box timing check (above).
- **Matched lockstep**: `py tools/func_lockstep_par.py --states 4000
  --verbose [--seed S]` (eight shards, about 3 minutes). New routines start
  from `py tools/matched_draft.py --data D:/GOG/F-117A --module VGAME.EXE
  --ip 0xNNNN`; drafts are never committed.
- **Census**: `py tools/reimp_names.py --reimp "../F-117A Reimp" --gen
  C:/Users/Tideg/f117-recomp-local/gen --out D:/f117-gate/census.tsv`, then
  `py tools/progress.py --census D:/f117-gate/census.tsv`. **The census's
  name guess can be wrong** (item 5 found one): verify a lead by cross-
  referencing the disassembly's actual data addresses against the Reimp's
  source, not by trusting the name alone.
- **Threat-profile sweep**: `py tools/threat_profile.py --data D:/GOG/F-117A
  --out DIR --routes R... --speeds 9 16 20 --seconds 600 --offsets-ms
  0 2500 ... --jobs N` (N sized to leave headroom for anything else running;
  resumable - unfinished tags are simply absent from `runs.jsonl`).

## Current state

- **Scoreboard**: P1 97.93%, P2 38.88%, P3 67.00%, P4 42.90%, All 70.63%
  (unchanged this session: nothing new was matched or named, only timing and
  measurement work).
- **Phase 1**: see items 3-4 above. The 386 profile is close to done; one
  86Box timing check remains.
- **Phase 2**: 765 of 1,535 census functions matched (63,917 of 179,213
  bytes). See item 5 for the next lead (`vgame_scene_obstacle_probe`,
  VGAME 0x1007) and `vgame_abs16` (VGAME 0xEE0C, trivial, drafted) as an
  even smaller first step.
- **Phase 3**: unchanged. Fixes: 10 of 14 catalogued defects switchable.
  Open: D10, D35, D36 (see `docs/bugs.md`). Presentation: the owner's open
  decisions (HUD/text at 4K) and a real-display check remain.
- **Phase 4**: untouched; most of it requires a person.

## Traps that cost time

- **PowerShell, not Git Bash, for `build.cmd`.** `cmd /c ".\build.cmd ..."`
  from the Bash tool gets its path rewritten and can print only the `cmd.exe`
  banner with no actual build - the log then looks like a trivial no-op
  relink (a handful of "Re-checking globbed directories" / "Linking" lines)
  and is easy to mistake for "nothing changed." Confirmed this session: the
  fix is to run `build.cmd` from the PowerShell tool directly, not wrapped in
  `cmd /c` from Bash.
- **A background job can look stalled when it is not.** `build_recomp.py`'s
  coverage step over a route that touches MISC.EXE's self-modifying code
  writes one log line a clock tick - tens of thousands of lines, the same
  few bytes repeated - and a `tail` taken mid-burst looks identical call to
  call. Check CPU delta on the actual process
  (`Get-Process -Name f117run | Select Id,CPU`, twice, a few seconds apart)
  before assuming a hang.
- **Shell and Python escapes**: inline Python heredocs turn `\n`, `\f`, `\a`
  and `\d` into real characters. Use the Edit or Write tool for strings with
  escapes, or a script file.
- **Git Bash and cmd**: see above; `MSYS_NO_PATHCONV=1 cmd /c "C:\...\
  build.cmd" "-DF117R_GEN_DIR=..."` with the absolute path also works from
  Bash if PowerShell is not available, but PowerShell is simpler and was
  what this session used after discovering the problem.
- **Room claim must cover the longest path** (see item 5): a wrong `room(c,
  N)` in a hand-written matched routine hides until a route happens to run
  out of its instruction budget exactly inside that routine, which can be
  billions of instructions in. Count every segment between calls/loop edges
  from the tool-generated draft, not from a mental re-derivation, and when
  in doubt over-claim (always safe) rather than under-claim.
- **A synthetic same-size file is not a stand-in for a real file's EXEC
  cost** (item 3): EXEC's cost is dominated by where the file lands on disk
  relative to the previous access, not by its size. Measure the real file at
  its real position when the file's identity is known (`realexec.py`'s
  approach), rather than inserting a differently-positioned same-size copy.
- **Background work**: a killed run can leave emulator processes holding
  files; check with `tasklist`/`Get-Process` before and after. Run long jobs
  in the background; never sleep in the foreground; use `Monitor` or
  background-task notifications to wait, not chained `sleep`.
- Game keys (Key Control Card): 0 brakes, 6 gear, 8 bay, Space select weapon,
  Enter fire, Backspace cannon, B select target, Shift+F10 eject. `f117run`
  input: Space is `\s` in route files; at most 4096 inputs.
- DOSBox 0.74 sees posted keys only with `SDL_VIDEODRIVER=windib`; DOSBox-X
  does not take posted keys; the release 86Box cannot be typed into (use the
  VNC build, `tools/ref86box/build_86box.md`).
