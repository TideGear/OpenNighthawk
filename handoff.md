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

- **Scoreboard:** P1 100.00%, P2 94.83%, P3 76.34%, All 92.44% (`py tools/progress.py --title`).
  The full figures: `py tools/progress.py --census D:/f117-gate/census.tsv --named-cover
  D:/f117-gate/p2-batch-bn-accept/named-cover --route-cover D:/f117-gate/route-cover-20261020b --data D:/GOG/F-117A`
  (gameplay 97.50%, naming 89.3%, checklist 45,390 / 47,433 = 95.69%, 4,098 executed bytes unmatched).
- **Accepted runtime:** gate `bn`, passed: 1,913 s, 740 tests, 63 routes / 62 fresh interpreter sessions, 1,496
  matched routines, 287 private mutations, whole table at two seeds 0 mismatching. Evidence
  `D:/f117-gate/p2-batch-bn-accept/`. Nine routines were added (see the roadmap's batch table) and checklist round 5
  was adopted in the same commit.
- **Working copy:** `C:\Dev\F-117A Recomp`. The old `My Drive` copy is stale; do not work there.
- CI is checked through the GitHub Actions API (no `gh` on this machine):
  `curl -s "https://api.github.com/repos/TideGear/OpenNighthawk/actions/runs?per_page=2"`.

## Resume here

1. **Batch bo: proofs finished and checkpointed, not yet integrated** (all under `D:/f117-gate/`, root verified each
   result file, never trust an agent's tally):
   - Checkpointed, ready for `integrate_many.py`: ASOUND.117 `0430` (`p2-asound-117-bn-0430`, 306 bytes, 21,696 calls),
     VGAME `0971` (`p2-vgame-bn-0971`, needs INTERRUPT_FRAMES `{ 0x120A, 0x0971, 3, 0x0A3E }` by hand), DSWAP `0FF8` and
     `2060`, SETUP `1C46`, PLAYER `222A` (3-byte jump thunks, FATAL_JUMPS rows). The `finish.log` of a proof holds two JSON
     records: read the FIRST (checkpoints) for `calls_of_this_routine`; the verdict record shows 0.
   - Finished on disk, checkpoints not yet run: START `2846` and END `36BC` (7 FATAL_JUMPS rows each), PLAYER `02853`
     (POPF_STOPS `{ "PLAYER.EXE", 0x026B, 0x01A3, 0x01B7 }`; check the missed `ss-no-shadow` mutant's reasoning), ASOUND.117
     `025A` prefix (four harness hunks around `run_to_ret`; keep the route pre-check), PLAYER `1255` (run
     `checkpoints --route err_player_assert`), MPS_LOGO `0307` (`p2-mpslogo-bo-0307`, 26/26 and five seeds clean after
     a probe-only decline/overrun check; harness hunks in its `tests-base.c`; entries `030E` and `0315` not yet proved).
   - ISOUND.117 `07E9` (`p2-isound-117-bo-07E9`, 27/27): its only route `err_vgame_no_photo` fails the replay check
     (`lb.3dg` / `lb.3DX` not opened by VGAME). Run the same checkpoint with matched routines off before concluding.
   - DSWAP thunks (`p2-dswap-bo-thunks`, one generic body for all 12 entries, 29/29): proved with a private multi-entry
     `kitm.py` and five hand-carried harness hunks; confirm the route and the observer/fix-site check, then adopt its
     approach for END 29, START 28, VGAME 53, PLAYER 3, SETUP 2.
   - Not accepted: `0468`-style swaps. A missed mutant is never replaced by an "equivalent" one when the path is merely
     never exercised: under-claims (a room claim smaller than the longest path) are real defects; make a probe state.
2. **Checklist round 6** (`D:/f117-gate/checklist-full-20261026/round6/`, packets were being built when the session limit
   hit; resume agent `a77677c8419658767` or rebuild with `round5/build_r5.py` as the model): 15 units where round-5's status
   differed from earlier readers, 3 accept-with-changes, 2 needs-context. The 6 protected units (stays-original,
   probable-dead) need the owner's design-rule and dead-code review, not a reader. Reader rule: the Read tool silently
   truncates files near 800 lines; require each reader to read every packet to its last line and state the line ranges.
3. **Leads not built** (read each directory's notes first): MPS_LOGO `0307`/`030E`/`0315` generic body, SETUP `10B3` and PLAYER
   `0211` (STI in a loop), SETUP `0094` (interrupt-shadow entry), START `8427` and END `4393` (wait handling), the 127
   run-time-patched far-jump thunks, and `D:/f117-gate/kit/DECLINED.tsv`, which is out of date (reconcile it before
   handing out declined routines; the owner ruled 23 Oct that declined routines are work, not exclusions).
4. **Owner decisions to carry out:** probable-dead rows are proved with lockstep inputs, not declared dead on weak
   evidence (recount `PROBABLE` rows of `D:/f117-gate/phase2-final/dead-cert/certificates.tsv` not yet matched);
   machine-option error paths are proved with test inputs (no test-only machine options).
5. **Remaining Phase 2 work beyond these** (definition: [docs/progress.md](docs/progress.md)): the packer stub's
   representation-aware review, the encoded SETUP code, sorting the 27,782 fixture-only matched bytes into routed or
   reviewed, and the closure review of indirect calls, tables, callbacks, vectors and self-modifying code before
   declaring 100%. Details in [docs/roadmap.md](docs/roadmap.md).
6. **Phase 3** re-release features are worktree work beside a gate; Phase 4 stays unscored.

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
