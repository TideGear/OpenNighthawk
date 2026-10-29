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

## Current state (29 October 2026)

- **Scoreboard:** P1 100.00%, P2 95.47%, P3 76.34%, All 92.64% (`py tools/progress.py --title`).
  The full figures: `py tools/progress.py --census D:/f117-gate/census.tsv --named-cover
  D:/f117-gate/p2-batch-br-accept/named-cover --route-cover D:/f117-gate/route-cover-20261020b --data D:/GOG/F-117A`
  (gameplay 98.35%, naming 89.8%, checklist 45,390 / 47,577 = 95.40%, 2,708 executed bytes unmatched: ASOUND.117 563,
  VGAME 519, ISOUND.117 401, ISOUND.LOG 225, START 213, PLAYER 172, ASOUND.LOG 169, RSOUND.LOG 125, MPS_LOGO 112,
  SETUP 99, END 64, RSOUND.117 44, DSWAP 2).
- **Accepted runtime:** gate `br`, passed: 1,978 s, 776 tests, 63 routes, 1,571 matched routines, 141 private mutations,
  whole table at two seeds 0 mismatching, route precheck of the six new entries identical. Evidence
  `D:/f117-gate/p2-batch-br-accept/` (its `audit_private.py`, `finalize.py` and `multi_proof.py` are the current ones: copy
  them forward from the newest accepted batch with `cp`, never `cp -n`, which kept the generic scripts and failed the audit).
  Earlier today: gate `bq` (43 far-jump thunks, the first multi-entry family).
- **Working copy:** `C:\Dev\F-117A Recomp`. The old `My Drive` copy is stale; do not work there.
- CI is checked through the GitHub Actions API (no `gh` on this machine):
  `curl -s "https://api.github.com/repos/TideGear/OpenNighthawk/actions/runs?per_page=2"`.
- **No agents are running.** Every proof agent of the 28-29 Oct wave has finished or been stopped; their work is in the
  private directories below. The owner caps concurrent agents at 10 (a "completed" agent that waits on its own background
  build still counts), and no proof agent runs while a gate does.

## Resume here

Goal in force when this was written: land Phase 2 bumps safely; a bump is committed only after a full gate passes. The
session ended after gate `br` was committed; nothing is half-integrated.

1. **Proved privately, not yet integrated** (all under `D:/f117-gate/`). The root verifies every result file itself (an
   agent's tally is not evidence: read `mutation-results.json`, `seeds.json`, the lock log's skipped column) and runs
   `py D:/f117-gate/accept-kit/finish_all.py DIR[:ROUTE]` before `integrate_many.py` (which needs `private-verdict.json`).
   Check the route with `grep -l "MODULE .* SEG IP" D:/f117-gate/route-cover-20261020b/*.cov`: PLAYER `1F4D` is run only
   by `err_player_assert`, and `finish_all` on `full_cycle` failed with 0 calls.
   - **Ready after checkpoints and verdict** (numbers read by the root): VGAME `0010` main (`p2-vgame-br-0010`, 73
     instructions, 66/66 mutants, five seeds, 0 skipped; check why the identical `p2-vgame-ta-0010` was never integrated);
     ASOUND.117 `09F2` (`p2-asound-117-br-09F2`, 24/24; owner decision pending on how the D2 fix at the same entry interacts,
     route unconfirmed).
   - **Agent-reported, root has not verified:** ASOUND.117 `005E` (`p2-asound-117-br-005E`, 79/79 mutants; needs six shared
     harness changes including a new `g_strict_limit` overrun check, so it needs a fix-only gate and a re-check of every
     accepted routine); MPS_LOGO `1046`, `103B`, `111D` (`p2-mpslogo-br-*`; `111D` has no route that runs all 63
     instructions, so partial route coverage needs a ruling; IMAGE_PATCHES and FATAL_JUMPS rows to carry); RSOUND.117
     `0D07` and RSOUND.LOG `03A2` (`p2-sndlog-br-*`, 26/26 each, but 10 and 14 drafted mutants were dropped unrun: run the
     full set; POPF_STOPS rows); tiny leaves ISOUND.LOG `060C` and `05D1`, START `8DD7`, END `4CBB`, PLAYER `026B:0306`
     (`p2-tiny-br-*`; they need a shared decline-probe change for one-instruction bodies); thunk families
     `p2-vgame-br-thunks` (53), `p2-player-br-thunks` (3), `p2-setup-br-thunks` (2) (30/30 mutants each; the lock log
     sees only the first entry, so read the per-entry seed logs; the root runs `multi_proof.py` and checkpoints), and START's
     28 (`p2-start-next-thunks`).
   - **Not passing:** PLAYER `1AD4` (2 missed over-claims) and `1ADF` (one long-run state): the cause is missing
     `FATAL_JUMPS` rows for PLAYER `1ADF`/`1AD4` to `1A91` in the shared harness, so a repair-only gate; VGAME `01B9` (3
     missed: two need a probe whose stop lands inside the callee); START `9D5C` (agreed six-byte credit `9D5C..9D62`, 16/18
     caught, the two `claim_ip` mutants are equivalent: replace them with the reason in `NOTES.md`) and `9E9A` (957
     undiagnosed long-run skips, parked); ASOUND.117 `097F` (boundary hand-back at `09F2` never completes under the harness;
     `096D`, `0998`, `09A1` unbuilt); ASOUND.117 `0A17` (`p2-asound-117-br-pit`: lock and seeds pass with a scripted
     port-0x42 hunk, the 29-mutant run never produced a result: `mutations.out` has no CAUGHT or MISSED line); ASOUND.LOG `008A` (needs boundary `00AC` and the POPF row
     dropped) and `05D1` (two unobservable mutants); ISOUND.LOG `0545` and `0138` (blocked); SETUP `10B3` (STI inside the
     retrace loop; proposed split: a prefix proof with boundary `10D3` plus an interior entry at `10D3`); VGAME `1132:0002`,
     `1132:0052` and `1D54` (tails after an STI in matched routines).
   - **Earlier proofs still waiting:** ISOUND.117 `026C` (`p2-isound-117-clamp-cov`, 74/74 mutants; the probe is test-only, but
     its no-earlier-event assumption and the `failed-origin-only` run need review) and `07E9` (`p2-isound-117-bo-07E9`, 27/27).
2. **Owner decisions pending** (recommendations agreed in conversation are in brackets): the PIT wait loops ASOUND.117
   `0A5B`, `0B01`, `0A17` (308 bytes) [scripted multi-port PIT readings in the harness, a repair-only gate, falling back to a
   hardware-wait-loop certificate only if no sound design exists; `0A5B` also needs a ruling on credit at its wake points
   `0AC0`/`0ACB`]; ISOUND.LOG `0581` [certify as data, leave the denominator with a written certificate]; the six protected
   checklist units [prepare evidence for the owner's review]; ISOUND.LOG `0545` [prove in pieces, do not widen the rule]; the D2
   fix interaction at ASOUND.117 `09F2`.
3. **Lessons from this wave** (add to the traps below): a proof's "N of N instructions reached" is not what the body
   executed: `0A33`'s body never ran past `0A46` because the PIC mask write lowers the run limit to 0, which left 13 mutants
   uncatchable until a trace showed it; narrow such a proof and hand back. A mutant is equivalent only by construction
   (for example `SM4_NEED(n, at)` after a passing `room(c, n)` never uses `at`, and `SUB` for `CMP` with a discarded result),
   never because its path is unexercised. A stopped mutation run leaves `project/.mutations-restore`, which blocks `lock`,
   `seeds` and `checkpoints`; restore with `kit._project_restore`. The mutation workers can crash on `vswhere.exe` after a good
   compile: rerun. Several lead rows are tails of routines already matched (a RET after an STI, `1132:0002` is `1058:0DA2`):
   check the parent before proving. A foreground wait loop over 10 minutes is auto-backgrounded; use a background until-loop.
4. **Remaining unmatched executed bytes (2,708) and where the points are:** the ranked lead list is
   `D:/f117-gate/p2-leads-bp/LEADS.tsv` (stale for rows batches bp-br matched). Largest left: ASOUND.117 PIT wait loops,
   ISOUND.117 `026C` (276 executed), the far-jump thunk families (VGAME 53, PLAYER 3, SETUP 2, START 28), ISOUND.LOG
   `0581` (data). The naming bar (weight 31.5, 89.8%) is the biggest pool of points left: it counts reviewed static code and
   error paths no route runs; bodies for those (printf arms need their enclosing function) are worth about three times the
   points per byte of route-executed code.
5. **Checklist** (`D:/f117-gate/checklist-full-20261026/`): round 5 adopted 88 units (3,641 bytes). Round 6 packets were never
   built (rebuild with `round5/build_r5.py` as the model): 15 units where round 5's status differed from earlier readers, 3
   accept-with-changes, 2 needs-context. The 6 protected units need the owner's review, not a reader. Rule: the Read tool
   silently truncates near 800 lines, so every reader must read each packet to its last line and state the ranges. Then the
   closure review of indirect calls, tables, callbacks, vectors and self-modifying code before declaring 100%.
6. **Batch workflow lessons:** the batch audit accepts a block only when the clock charges sum exactly to its instruction
   count; give it the body's own claim macro as the prefix (`XB` for `18C8`), `UNUSED` for macro-free bodies. Trim each
   dossier to the credited window (keep `dossier/full-walk.json`) and set `partial` only for bodies that hand back by design.
   Integrate with `integrate_many.py`, then carry by hand every harness hunk and table row a proof's `tests-base.c` has that
   HEAD lacks (SETUP `0094` needed a `LIVE_FRAMES` row). Never write patch scripts through a Bash heredoc if they contain
   backslashes: use the Write tool. The gate takes about 33 minutes with the machine to itself.
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
- `kit.py checkpoints` takes the route from `finish_all.py DIR:ROUTE`; a routine no `full_cycle` capture reaches fails with
  0 calls: find its route in `route-cover-20261020b` first. `integrate_many.py` needs `private-verdict.json`, which only
  `finish_all.py` writes.

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
