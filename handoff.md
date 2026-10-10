# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs). State as of 10 October 2026.
Earlier session logs are in `git log -p handoff.md`.

## Current work after allowance reset (10 Oct, 07:00 PDT)

Clean starting checkpoint `0174a2c`; the owner resumed after the usage reset.
Current code is uncommitted: optional D1SLOT at VGAME 6CA6 prevents a
seeker-cleared incoming lifetime from underflowing. It uses the shared DEC
semantics for flags, preserves instruction count and declines every
positive-life/player-slot case. Earlier D1TTL/D1PROX behavior is unchanged.
New `tools/d1slot_check.py`: all 15 staged cases pass in both engines +/-
option, all eleven control hashes unchanged (about two minutes, four boots).
Incoming slots 0/7 and player slot 8 are covered. Full CTest also passed all
15 tests (573 s, including its expensive serial 2,000-state matched check).
Use the gate's normal 14-test selection rather than repeating that serial
check unnecessarily.

All 72 natural flights are complete (10.4 min, 12 workers):
`D:/f117-gate/threat_slot.py`, `threat-slot-console.log`, `threat-slot/`.
Four routes x six seed families x (9 TTL+PROX, 9/20 TTL+PROX+SLOT), reusing
the earlier 20 TTL+PROX arm after its control reproduced `7b5cc08d65027187`.
All 24 mission identities match across all four arms. Adding SLOT increases
bursts/flight by 2.1667 at 20 MIPS and 2.375 at 9. With the same three options,
20-minus-9 bursts are -0.9583 (95% paired CI -2.2083..+0.25): not evidence
of equivalence. More damage changes survival/exposure; retain early exits.
Full figures and reproduction are appended to `docs/speed-sweep.md`.

Fixed interpreter/native Middle East 20 MIPS offset2720 matches hash
`1f85212a6ffbf801`, CSV bytes and all missile records: 16 launches, 9 bursts,
20 total damage selections, early exit. Raw fixed 9/20 traces (`ghost-slot/`)
have zero abandoned-slot episodes/slot-seconds and match cohort hashes
`17725de70b98ba07` / `1f85212a6ffbf801`.

Independent constant-speed/heading SA-12 pass (`proximity_pass_probe.py`,
`proximity-pass-{interp,recomp}.json`) still hits at S=8 and misses at S=15
with TTL+PROX+SLOT. Both engines match, hashes `f2a3a1eb93590c5a` /
`a3d3b2e93b8afe35`. Speed28, zero agility, terminal speed frozen28,
lateral22/longitudinal70/altitude100. The PROX S9 floor24 is below closest
slant25.125, while original S8 reach28 hits. First probe allowed acceleration
and both hit; do not confuse it with the corrected frozen-speed result.

REQUIRED FULL GATE PASSED: `D:/f117-gate/gate-d1slot.log`, result
`gate-d1slot-result.txt`, exit 0, 1588.4 s. All 14 CTests, 35 identical route
pairs, both 5,718,912-state instruction profiles and three 765-address
matched seeds passed; every routes-only routine ran and none overran.
Coverage added no code. The attempted ctypes affinity wrapper failed;
corrected parent/descendants early in coverage and verified 0x00FFFFFF.
`affinity_launch.py` now uses pointer-width ctypes signatures, checks native
return values and reads the resulting mask. The earlier cohort's affinity
attempt was likewise ineffective (only 12 workers, so still limited jobs).
Actual Processor % Processor Time was 52% during parity while WMI incorrectly
reported 96%. Prefer the former for load checks.

This checkpoint has passed the required gate and is ready to commit/push.
Then repair remaining proximity
sampling with a physical band plus swept path, rather than raising a floor
to match cohort means. Private `swept_design_check.py` checks exact octagonal
slant minima at piecewise-linear breakpoints: 2,000 random paths, reversal
and subdivision invariant and <=202,000 sampled distances. This is only a
geometry design check, not a game fix. Guidance acceleration increments one
unit on odd frames (7209..722B), with no S normalization; independently
confirmed by `acceleration_probe.py`: normal SA-5 initial1/terminal28,
steering disabled, after two simulated seconds speed9 at S8 versus16 at
S15, both engines hash `c48afd58d9ae6ea2` / `fc9ee748c9424f67`. This
favors high-S acceleration and cannot alone explain weaker combat.
The regenerated native fixed flight (`d1slot-regenerated-native.*`)
retains the pre-regeneration hash, every launch record and every CSV byte.
No probe or gate process is still running.
No claim CPU/combat is solved; do not move to the rest
of the roadmap yet. The Reimp HEAD remains `9e0716dc502cd64501b0d52030ebef041825f907`.

## Previous session checkpoint

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

## Current top priority: CPU speed and enemy effectiveness (10 Oct investigation)

Latest code checkpoint: `5fcdd04`, committed and pushed to `origin/master`.
The full gate and all private cohorts/probes below are complete; no driver
is still running. Continue with the underflow repair below before the
broader roadmap. All generated code, game files and experiment output
remain outside this repository.

The owner wants this resolved before continuing the roadmap goal, and asked
for both a Reimp check and an independent investigation. This checkpoint
adds two optional mitigations and corrected measurements; do not mistake
the earlier matrix for sound combat evidence.

- The Reimp at `9e0716dc502cd64501b0d52030ebef041825f907` decouples simulation
  from rendering, paces simulation from S, starts S at 5, and uses a 60 Hz
  real-time game clock by default. Its `frame_weapons.c` still compares
  incoming lifetime signed. Its simulation-clock tests check display-rate
  independence, not combat. Older catalogue claims about S oscillation are
  not a demonstrated cause in the original's full loop.
- Independently confirmed signed-life bug: ground SA-5 life is `150*S*16`,
  crossing 32767 at S >= 14. VGAME 708D treats nonzero life as active, while
  6F19's signed JLE suppresses damage. Optional `D1TTL` admits valid unsigned
  life through 36000, retaining suppression of abandoned FFFF slots. All
  seven staged cases pass in both engines, fixed/unfixed, with identical
  engine hashes. Natural 600-second fixed Middle East flight at 20 MIPS,
  offset 2720: SA-5 bursts at 293.32 seconds instead of missing; interpreter
  and recomp hash `0559ff3b220737ef`, CSV and launch records identical.
- The old observer hid negative SA-5 lifetimes, missed burst resets between
  polls, and lost damage counters on exit. Seeker 72A5 can clear life before
  move 6CA6 decrements it to FFFF; these abandoned slots are not new launches.
  Current tools read the official launch count and valid unsigned life,
  detect burst crossings, retain early-exit damage, and record S/counters/
  detection inputs. Damage counts selections, not missile impacts.
- The old adaptive pilot rotated at fixed instruction deadlines (8.5 s at
  20 MIPS versus 18.9 s at 9), causing matched fast missions to crash before
  engagement. Startup now keeps authored 9 MIPS seconds. Its orbit also had
  incorrect negative wrapping and outward radius correction; both fixed and
  checked in eight directions. Historical combat conclusions are superseded
  in `docs/speed-sweep.md`.
- Aircraft fire checks are staggered once per 16*S frames at stable S, not
  per frame; ground 5046 runs every frame (visibility skips only repaint).
  Detection has no direct S input. Corrected in `docs/bugs.md`.
- A second mechanism remains: proximity reach `(speed<<3)/S` shrinks with S.
  A staged SA-12 hits at S=8 but misses at S=15 with positive life and equal
  starting geometry. Experimental optional `D1PROX` gives incoming reach an
  S=9 floor, without changing movement/guidance/clocks or limiting frames.
  This is an explicit balance choice, not a recovered original radius.
  Built successfully (108 s). All five staged cases pass under both engines
  +/- D1PROX; engine hashes match and the S=8/9 control hashes are unchanged.

Running private cohort: `D:/f117-gate/threat_aligned_v5.py`, 96 flights,
4 routes x 6 seed families x (9, 16, 20, 20+D1TTL), 600 seconds, 12 workers.
Offsets 110/220 ms at 16/20 align START seed families; verify mission fields
rather than assume alignment. Output `D:/f117-gate/threat-aligned-v5`, console
`threat-aligned-v5-console.log`. All 96 are complete; all 24 mission identities match across arms.
Official launches/flight: 8.5417 / 8.4583 / 7.7500 / 7.7083; bursts:
4.1250 / 3.5000 / 2.7500 / 2.8333. 20-minus-9 launch CI includes zero,
but burst CI is -2.1667..-0.6250. Full report is appended to speed-sweep.md.
Public comparator output: `D:/f117-gate/threat-aligned-v5-summary.txt`. Earlier
v3/v4 experiments are diagnostic history, not the final cohort. Final staged
TTL outputs: `D:/f117-gate/d1ttl-final-{engine}-{False|True}.json`.

Both cohorts are now complete: 96 baseline/TTL flights and 24 combined
D1TTL+D1PROX flights, every mission pair matching. Combined 20 MIPS:
7 early exits, 7.3333 launches, 3.375 bursts, 6.75 damage selections per
flight, 4669.8 orbit seconds. Against 20+D1TTL, burst gain +0.5417
(95% paired CI +0.2083..+0.8750). Against 9, a deficit remains: -0.75
bursts (-1.4167..-0.1667). The options are partial mitigation, NOT a
completed CPU/combat fix. Full report and artifact paths are in
`docs/speed-sweep.md`; comparison tool is `tools/threat_compare.py`.
Combined natural interpreter/recomp flight (Middle East, 20 MIPS,
offset 2720) is identical: hash `7b5cc08d65027187`, all CSV rows and launch
records equal. D1PROX staged five-case checks agree in both engines +/-
option, S=8/9 control hashes unchanged. All 14 C and 97 Python tests pass
(one Python test skipped).

Full required gate PASSED (1,499 seconds):
`py tools/build_recomp.py --data D:/GOG/F-117A --seed-coverage C:/Users/Tideg/f117-recomp-local/coverage --jobs 16`
Log `D:/f117-gate/gate-combat.log`. All 35 routes identical, both
5,718,912-state instruction profiles and all three 765-routine seeds zero
mismatches; 14 C tests passed, every routes-only routine ran, no overruns.
Coverage added no code; 34 fresh interpreter sessions. The gate launches
three eight-shard matched seeds concurrently,
so 16 route workers still oversubscribed 32 threads; restricted the gate
parent and its 49 descendants to affinity mask 0x00FFFFFF (24 logical CPUs),
leaving eight for the desktop. Future children inherit that mask. This is
process-local and ends with the gate, not a persistent machine setting.

This checkpoint includes four new files (`d1ttl_check.py`,
`d1prox_check.py`, `threat_compare.py`, `tests/test_threat_profile.py`).
Do not call the CPU/combat issue solved. Investigate the residual before
moving to the rest of the roadmap. The 9 MIPS cohort has burst-time S mostly
7/8 (23/46 of 99 bursts; mean 8.6465), versus the parked S=9 floor chosen by
D1PROX. Any different calibration should be tested on new seeds, not tuned
until this cohort matches. Continuous swept collision with an explicit
reference radius is another possible design direction; not implemented.
Seeker-abandoned life can still occupy a slot (72A5 clears before 6CA6
underflows); D1TTL guards those FFFF values from damage but does not repair
that separate slot-reclamation defect.

Raw-slot follow-up is complete: `D:/f117-gate/ghost_trace.py`, outputs
`ghost-trace/{9,20}-2500.json`. Middle East hashes reproduce the baseline
cohort exactly. Abandoned slot occupancy over the common first 472.9 s is
1104.8/1295.2 slot-seconds at 9/20 MIPS; whole 20 flight is 1894.0. Both
still launch 11 times. Ground 54B4/air 65C3 select slot index & 7 and require
zero life, so global saturation is unnecessary to block a given launcher.
This defect also remains in Reimp. Test a separate saturating-countdown
option rather than silently changing the already-measured D1TTL behavior.

Held-out cohort complete: `D:/f117-gate/threat_heldout.py`,
console `threat-heldout-console.log`, output `threat-heldout/`. Four routes
x new seeds 15000/17500/20000 x (9 unmodified, 20+D1TTL+D1PROX), 600 s,
eight workers, parent/children affinity 0x00FFFFFF shared with gate; CPU was
49% before launch. Took 9.9 min, no tool errors, all 12 mission pairs match,
three early exits per arm. Bursts 4.5 at 9 versus 4.0 at 20; paired difference
-0.5 (95% -1.75..+0.6667). Pooled with the earlier 24 pairs: -0.6667
(-1.25..-0.0833), so CPU/combat remains open. No calibration changes were
made. See `threat-heldout-summary.txt` and the appended speed-sweep report.

Staged underflow reproduction now passes in both engines:
`D:/f117-gate/slot_underflow_probe.py`, `slot-underflow-{interp,recomp}.json`.
SA-5 class 1, off-axis behind the weapon, positive life 1000 -> FFFF at
both S=8 and 15, no damage, identical per-case engine hashes
`76d07a07e0d330d9` / `7828a2d70478e5a1`. The initial exact-180-degree setup
did not cancel (signed abs edge); corrected to an off-axis bearing.
Next implementation candidate: a separate optional hook at VGAME 6CA6
that keeps incoming life zero when the seeker has cleared it, while
retaining DEC flags and leaving every positive-life countdown untouched.
Test zero/one/positive-life controls, both engines and S values, then natural
flights. Compare 9 and 20 with the SAME option set as well as the original
9 MIPS balance reference. Do not silently change D1TTL: earlier cohort
numbers measure its signed-guard repair alone. A counter bound is not a
permanent identity check; an old abandoned value could decay below 36000.

The active goal remains roadmap completion, with the user's condition to
wrap/commit/push/stop at <=15% five-hour allowance remaining. Local session
rate-limit telemetry last reported 84% used (16% remaining), near the wrap
threshold. Check fresh telemetry before starting another code/build cycle.

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
- **Phase 3**: 12 switches including D1TTL and the D1PROX balance option;
  10 of 14 top-level catalogued defects have a switchable fix. CPU/combat
  remains open; signed life and proximity distance are partial mitigations.
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
