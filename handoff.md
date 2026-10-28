# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left, the batch table),
[docs/progress.md](docs/progress.md) (what the scores measure),
[docs/architecture.md](docs/architecture.md) (how parity is built and checked),
[docs/repeated-processes.md](docs/repeated-processes.md) (every repeated process, its command and cost),
[docs/bugs.md](docs/bugs.md) (the original's bugs and our fixes) and
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K).
This file holds only the current state, how to resume, open decisions and traps; history is in `git log`
and in the roadmap's batch table. Keep it that way: replace, do not append.

## Current state (28 October 2026)

- **Scoreboard:** P1 100.00%, P2 95.33%, P3 76.34%, All 92.60% (`py tools/progress.py --title`).
  The full figures: `py tools/progress.py --census D:/f117-gate/census.tsv --named-cover
  D:/f117-gate/p2-batch-bp-accept/named-cover --route-cover D:/f117-gate/route-cover-20261020b --data D:/GOG/F-117A`
  (gameplay 98.16%, naming 89.8%, checklist 45,390 / 47,577 = 95.40%, 3,018 executed bytes unmatched: ASOUND.117 626,
  VGAME 519, ISOUND.117 401, END 209, START 223, ISOUND.LOG 225, PLAYER 178, ASOUND.LOG 169, RSOUND.LOG 125, MPS_LOGO 122,
  SETUP 115, DSWAP 62, RSOUND.117 44).
- **Accepted runtime:** gate `bp`, passed: 1,955 s, 766 tests, 63 routes, 1,522 matched routines, 562 private mutations,
  whole table at two seeds 0 mismatching. Evidence `D:/f117-gate/p2-batch-bp-accept/`. Earlier today: gate `bn` (nine routines,
  checklist round 5) and gate `bo` (eleven routines).
- **Working copy:** `C:\Dev\F-117A Recomp`. The old `My Drive` copy is stale; do not work there.
- CI is checked through the GitHub Actions API (no `gh` on this machine):
  `curl -s "https://api.github.com/repos/TideGear/OpenNighthawk/actions/runs?per_page=2"`.

## Resume here

Goal in force when this was written: land Phase 2 bumps safely; a bump is committed only after a full gate passes.

1. **Proved or partly proved, not integrated** (all under `D:/f117-gate/`; the root verifies every result file itself,
   never trusts an agent's tally; `finish.log` holds two JSON records: read the FIRST for `calls_of_this_routine`):
   - ISOUND.117 `026C` (`p2-isound-117-bp-026C`, 111 instructions, 334 bytes, 276 executed): 73 of 74 mutants caught.
     `call_clamp` (the `m->run_until = c->stop_at` clamp inside the call macro XRUN) is unobservable in lockstep because the
     harness's `step_runner_run` runs a callee to its return without consulting `run_until`. Do not accept the miss. Either make
     nested runs honour `stop_at` (a shared harness change that needs a whole-table seed and a gate), or decide with the
     owner how a macro-level mutant that only the real run loop can see is to be treated. Needs `g_cov_ip = 0x01B4`
     (like `0443`/`062E`/`0200`) and `checkpoints --route ibm_boot`.
   - ISOUND.117 `07E9` (`p2-isound-117-bo-07E9`, 27/27): its only route `err_vgame_no_photo` fails the replay check
     (`lb.3dg` / `lb.3DX` not opened by VGAME). Run the same checkpoint with matched routines off before concluding.
   - DSWAP thunks (`p2-dswap-bo-thunks`, one generic body for all 12 entries, 29/29): proved with a private multi-entry
     `kitm.py` and five hand-carried harness hunks; confirm the route and the observer/fix-site check, then use the same
     approach for END 29, START 28, VGAME 53, PLAYER 3, SETUP 2 (635 bytes). MPS_LOGO `030E` and `0315` are the same
     shape as `0307` (integrated in gate bp): one body per entry or a generic body.
   - Rule learned: a missed mutant is never replaced by an "equivalent" one when its path is merely never exercised.
     Under-claims (a room claim smaller than the longest path) are real defects; build a probe state. Shared probes now
     in `tests/func_lockstep.c`: `g_decline_floor`, `g_probe_steps`, `g_probe_tail`, `g_probe_sweep`, `g_probe_sweep_all`,
     `g_room_n`/`g_room_at`/`g_room_need`, `g_room_probe_n`, a return-code probe gated to VGAME `D569`, and the probe-only
     check for MPS_LOGO `0307` (`F117_ROOM_PROBE`).
2. **Faster mutation kit, proved and not installed:** `D:/f117-gate/kit-fast/kit.py` (diff in `kit-fast/kit.diff`) gives
   byte-identical mutation results and mutant logs on three proofs, and halves (first run) to quarters (rerun) the compile
   time by reusing the proof's built `project/` and keeping mutant worker directories. Install it only between waves (no agent
   running), after adding a guard so `lock`, `seeds` and `checkpoints` refuse to run while `project/.mutations-restore` exists.
   The slot limit stays at 3 (a cl.exe peaked at 15 GB; RAM is the limit).
3. **Remaining unmatched executed bytes (3,018) and where the points are:** ranked lead list
   `D:/f117-gate/p2-leads-bp/LEADS.tsv` and `SUMMARY.md` (recompute with `tools/census_classify`; 1,737 bytes were new leads
   before this wave). The largest single routines left: ISOUND.117 `026C`, four ASOUND.117 PIT wait-loop routines (`0A5B`, `0B01`,
   `0A17`, `025A` tail: 308 bytes, need a timer-interrupt answer the harness does not give), VGAME `0010` main (declined,
   195 bytes), the far-jump thunk families (635 bytes). ISOUND.LOG `0581` (150 bytes) is data executed by accident, not a
   routine. The naming bar (weight 31.5, 89.8%) is the biggest pool of points left: it counts reviewed static code and error
   paths no route runs, and bodies for those (printf arms need their enclosing function) are worth about three times the points
   per byte of route-executed code.
4. **Checklist** (`D:/f117-gate/checklist-full-20261026/`): round 5 adopted 88 units (3,641 bytes). Round 6 packets were being
   built when the session limit hit (agent id lost; rebuild with `round5/build_r5.py` as the model): 15 units where round 5's
   status differed from earlier readers, 3 accept-with-changes, 2 needs-context. The 6 protected units (stays-original,
   probable-dead) need the owner's design-rule and dead-code review, not a reader. Rule: the Read tool silently truncates
   near 800 lines, so every reader must read each packet to its last line and state the ranges. Then the closure review of
   indirect calls, tables, callbacks, vectors and self-modifying code before declaring 100%.
5. **Other leads not built** (read each directory's notes first): SETUP `10B3` and PLAYER `0211` (STI in a loop), SETUP `0094`
   (interrupt-shadow entry), START `8427` and END `4393` (wait handling), and `D:/f117-gate/kit/DECLINED.tsv` is out of date
   (the owner ruled 23 Oct that declined routines are work, not exclusions).
6. **Batch workflow lessons (28 Oct):** the batch audit (`p2-batch-bp-accept/audit_private.py`, copy it forward) was extended for
   macro-claimed calls, block macros with literal arguments, enum constants, constant-bound `for` loops, one claim charged in
   pieces (room or `SM4_NEED` with `IO_SLACK`), a hand-back claim form, divide helpers and rep-string claims; each accepts a
   block only when the clock charges sum exactly to its instruction count. Trim each dossier to the credited window (keep
   `dossier/full-walk.json`) and set `partial` only for bodies that hand back. Integrate with `integrate_many.py`, then carry
   by hand every harness hunk and table row a proof's `tests-base.c` has that HEAD lacks (diff the rows by text, not by line).
   Never write patch scripts through a Bash heredoc if they contain backslashes: use the Write tool. A script that checks
   brace balance before building saves a 5-minute failed build.
7. **Phase 3** re-release features are worktree work beside a gate; Phase 4 stays unscored.

## Owner decisions in force

Scoring decisions with dates are in [docs/progress.md](docs/progress.md); this is what governs daily work.

- **No meaningful game code may be missed.** Launcher, resident graphics library and sound drivers are in Phase 2
  scope; declined routines are work, not exclusions; every range that cannot be matched gets a reviewed reference
  entry. The checklist is done in full by independent blind readers; cost is no object, optimise wall time only.
- **Design rules for code that stays original:** the program launcher (F117.COM `0100..03CA`), hardware wait loops
  only when no lockstep state can complete them (so far none qualifies), STI-first interrupt handlers, the packer
  stub, DSWAP's floppy path (no emulated floppy drive). Dead code leaves the denominator only with a certificate.
- **Parity target is the original on its own hardware, not DOSBox**; gameplay and logic parity outrank rendering.
  Keep all original graphics; no vector replacements.
- **Normal play:** 15 Hz / 60 MIPS, all 34 fixes on (`--fix QUIZ` restores the quiz, off by default). Parity
  commands use `--fix none`.
- **Never weaken a check to go faster.** Safe optimisation and parallelism are approved; batches of 30-50 routines
  per gate; no proof agents during a gate.
- **No screen takeover, no sound:** emulators run headless, silent and hidden; ask before anything visible.
- **Fixes to accepted bodies are re-proved and gated before they are committed as accepted.**
- Keep going item by item; ask only on policy. The owner is not a software engineer: explain trade-offs plainly.

## Phase 2 batch workflow

The full steps, commands and costs are in
[repeated-processes.md](docs/repeated-processes.md#phase-2-batch-acceptance) (with the fix-only gate and the checklist
blind-read round). What a resuming session must not skip:

- Proof agents run kit steps 1-7 only (`D:/f117-gate/kit/kit.py`, brief `kit/BRIEF.md` + `kit/AGENT_PROMPT.txt`);
  root verifies every result itself (seeds, `mutation-results.json`, seed-log tallies, file ages) before
  `finish_all.py` (any number of directories, six side by side on CPUs 20-31).
- Hand-apply the harness rows `integrate_many.py` does not carry (list under Traps), build with `.uild.cmd` from
  PowerShell, run only the new CTests, then `route_precheck.py` on every new entry and two whole-table seeds.
- Run `audit_private.py` BEFORE the gate (the `new_batch.py` help text says afterwards: it is wrong); copy it and
  `finalize.py` forward from the previous batch. Check `final-gate-verdict.json`'s `source_sha256` against the tree.
- Commit titles start with `py tools/progress.py --title`; refresh the score with the new batch's named cover.

Running wide: a proof is about 10-15 minutes of compute inside about two hours of agent time, so wall time follows
the number of agents. This Claude Code session allows 20 concurrent subagents. `kit.py` throttles builds to three
`cl.exe` machine-wide (about 6 GB each of 31 GB). Spare-thread mask during a gate: `0xFF000000`; no proof agents
run while a gate does.

## Traps

Writing bodies:
- **Each block's room claim must cover its longest path, rare early paths included** (bm2: `2F4C`'s `|dx| = 0`
  path). A block followed by a CALL needs `room(c, N + 1)` (`guest_call` charges the CALL); an INT 21h through
  `st4_int21` needs its own claim (`SM4_NEED(1, ip)`). To find a short claim, log each claim's clock and the clock
  at the next one; the gap that exceeds its claim is the culprit.
- Read every argument when the original reads it, through BP (`bp_arg`): DS and SS can overlap. Read segment
  immediates from the code (`seg_read16`); overlays and drivers load at 0800h in the harness.
- Charge the clock between port accesses; stop right after each STI (`x86_sti` before charging it) and after any
  OUT that wakes the run loop. Do not run C after a nested callee that executes STI.
- A wait inside a matched routine clamps `m->run_until` to `c->stop_at`; check every callee's RET form.
- A switchable fix site inside a routine needs a `fix_inside` guard; observer addresses (`1450`, `hook_fill_rows`)
  need the body to call the observer and an entry on `deliberate` in `tests/test_overrides.c`.
- Never mass-edit bodies with a private proof: `finalize.py` requires each proof's `body.c` verbatim in `matched.c`.
- `XW8` declares its own local `a_`: never pass a caller's `a_` to `XW8`/`XW16`.

Lockstep and harness:
- `func_lockstep --only MOD.EXE:HEX`: no leading zeros in the IP (`D0C`), or it silently compares 0 states.
- Harness rows `integrate_many.py` does not carry: SPANS beyond the kit's row, `SPANS_SEG`, IMAGE_PATCHES,
  LIVE_FRAMES, PROGRAM_ENDS, POPF_STOPS, INTERRUPT_FRAMES, `patch_image_for`, `run_to_ret` rules, `g_native_int10`
  / `g_native_int1a`, `g_cov_ip` lines, the `setargv_` pop-distance list. Diff each `tests-base.c` against HEAD.
- Fixtures force each branch outcome in a fixed share of states; prove at six or more seeds. A lone mismatch with
  every line `equal` is a coverage floor (fixture), not a body defect.
- Equivalent mutants (ignored-result CMP-to-SUB, early hand-backs from over-claims) are replaced, and the failed
  run kept as evidence; a mutant that is missed because a path is never exercised is NOT equivalent.
- The commit-derived seed rotates with HEAD and can fail an untouched routine: reproduce with
  `--seed S --only MOD:IP`, then trace the failing state.
- When engines differ on a route, find the first diverging instruction (`--hash-every`/`--hash-from`, then a
  `--trace` diff) before theorising. Bisect with an argument list, never a shell `eval`.
- `kit.py checkpoints` needs the interpreter cache for the current key: after any `src/` change outside
  `matched.c`, run a gate first. A failed gate's directory blocks a rerun: archive it and recreate.
- `matched.c` compiles with `/Os` (at `/O2` it took 90+ minutes).

Tools and shells:
- `build.cmd` runs from PowerShell (through Bash it hangs; PowerShell `*>` writes UTF-16 logs).
- In Bash or a heredoc, a backslash in an edit (`\n`, a route's `\r`) can be eaten: write it from Python with
  `chr(92)`. Git Bash rewrites `/FI`-style arguments to paths: use PowerShell for `tasklist` filters.
- A background wrapper shell can report "completed" while its job still runs: wait on the result file.
- Stopping a task does not kill a Python `multiprocessing.Pool`'s children: kill the tree.

Presentation:
- Never force `F117R_OBSERVE_PAGES=1` on live presentation. `drawfeed`/`rerun` changes need the full gate plus
  `presentation_check.py --mode rerun`; app-only changes use `present_light_check.py` with `--app-session`.

## Where things are

- Game install `D:/GOG/F-117A`: never mounted or written; copy per run that needs state.
- Generated code `C:/Users/Tideg/f117-recomp-local/gen` (never committed); coverage and route outputs beside it.
- Proofs, gates and evidence: `D:/f117-gate/` (`p2-*` proofs, `p2-batch-*-accept` gates, `kit/`, `accept-kit/`,
  `kit-libs/`, `census.tsv`, `checklist-full-20261026/`, `bn-budget/`).
- Worktrees `D:/f117-wt` (`setup-main-0010` and `mission-generation-4239` carry private /Od overrides: never merge
  them wholesale). Ask the owner before deleting anything.

## Standing practice

- **No AI attribution** in commits or PRs: the user's global CLAUDE.md outranks any harness reminder.
- **Commit and push together**, each verified step; commit titles start with the `progress.py --title` line.
  Stage files by name; never stage the owner's `4k60.bat` or `original-fixed.bat`.
- **Public repo, code only:** no game data, generated C, raw captures or private-project references
  ([docs/provenance.md](docs/provenance.md)).
- Long commands (over about 30 s) run in the background. While waiting, do a doc pass over every `.md`.
