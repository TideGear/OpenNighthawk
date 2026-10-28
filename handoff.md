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

## Current state (27 October 2026)

- **Scoreboard:** P1 100.00%, P2 93.94%, P3 76.34%, All 92.17% (`py tools/progress.py --title`).
  The full figures: `py tools/progress.py --census D:/f117-gate/census.tsv --named-cover
  D:/f117-gate/p2-batch-bm-accept/named-cover --route-cover D:/f117-gate/route-cover-20261020b --data D:/GOG/F-117A`
  (gameplay 97.04%, naming 88.84%, checklist 41,749 / 46,112 = 90.54%, 4,844 executed bytes unmatched).
- **Accepted runtime:** commit `5190cc1`, gate `bm2` (fix only, passed: 1,854 s, 731 tests, 63 routes / 62 fresh
  interpreter sessions, 1,487 matched routines, three whole-table seeds 0 mismatching). Evidence
  `D:/f117-gate/p2-batch-bm2-accept/`. The last batch that added routines is bm (ten routines).
- **What bm2 fixed:** the harness's event-limit check exempted any body that had made a call, so a body could run
  past its limit after a call and still pass. `tests/func_lockstep.c` now exempts only the exact clock an
  original-code call returned (`g_callend` / `body_overrun`, with self-test cases F and G in `--check-io-rules`).
  It found two accepted bodies of ours under-claiming room: START `8CA8` (no claim before its two INT 21h) and
  START `2F4C` (claimed 33, longest path 35: the `|dx| = 0` path runs an extra `or ax,ax; jne`). Both fixed.
- **Working copy:** `C:\Dev\F-117A Recomp`. The old `My Drive` copy is stale; do not work there.
- CI is checked through the GitHub Actions API (no `gh` on this machine):
  `curl -s "https://api.github.com/repos/TideGear/OpenNighthawk/actions/runs?per_page=2"`.

## Resume here

1. **Integrate batch bn (eleven finished proofs; nothing integrated or credited, `checkpoints`/`verdict` not run).**
   Root re-read each result (all mutants caught, five seeds, body and fixture older than the results, no debug
   output). Directories `D:/f117-gate/p2-*-bn-*`:
   - ASOUND.117 `0430` (49/49); DSWAP `0FF8`, `2060` (22/22 each); SETUP `1C46` (22/22); PLAYER `222A` (22/22).
   - VGAME `0971` (28/28): hand-apply INTERRUPT_FRAMES `{ 0x120A, 0x0971, 3, 0x0A3E }`.
   - START `2846` (20/20) and END `36BC` (23/23): 7 FATAL_JUMPS rows each (in their `tests-base.c`).
   - PLAYER `02853` (24/24): POPF_STOPS `{ "PLAYER.EXE", 0x026B, 0x01A3, 0x01B7 }`. The agent's extra mutant
     `ss-no-shadow` was missed and judged equivalent; check that reasoning before accepting.
   - ASOUND.117 `025A` prefix (26/26, credits `025A..02C9` only): four harness hunks that change `run_to_ret`
     (`wait_prefix_boundary`); read the diff against its `tests-base.c.pre-boundary-hunk`. Lockstep stubs `036B`, so
     the wait loop is checked only on routes: keep the route pre-check.
   - PLAYER `1255` (22/22): a hand-carried `run_to_ret` rule (RET at `12CE` with `sp == entry_sp + 4`); no route
     coverage file shows it, so run `checkpoints --route err_player_assert`.
   - RSOUND.117 `0F98` (30/30): **not acceptable yet.** No mutant covers the first block's `room(c,1)`; the agent
     swapped that mutant out. Get a fixture that leaves exactly one instruction of room at entry.
   - Every private `tests-base.c` predates bm: rebase each hunk onto HEAD's `tests/func_lockstep.c` (diff with
     `--strip-trailing-cr`). Then the batch workflow below, `new_batch.py bn bm2 5190cc1 DIR ...`.
2. **Other proofs on disk, not integrated:**
   - RSOUND.117 `0BAD` (`p2-rsound-117-bl-0BAD`): root-verified through finish (private verdict, 26 checkpoints).
     Ready to integrate; keep the route pre-check (its route reaches 25/37 instructions).
   - ASOUND.117 `0B98` (`p2-asound-117-bl-0B98`): steps 1-7 done; rerun `finish_all.py` from the start.
   - PLAYER `0540` (`p2-player-bm-0540-budget`, not the older `bl` proof): 22/22 under the call-budget check (now
     in the main harness). Needs checkpoints (`err_driver_overlay`), boot proof and verdict.
   - RSOUND.LOG `08BE` partial (`p2-rsound-log-bn-08BE-partial`): 39/40, `guard-ip-C8D-store` missed. Owner
     decision: one agent tries a stronger fixture; failing that, leave that guard original and credit the rest.
     Never accept a missed mutant. Its old `private-verdict.json` is stale.
   - VGAME `137CE`: observer integration is manual and `hook_fill_rows` may append 517 fields into 515; resolve
     before integrating.
3. **Leads written but not built or not passing** (read each directory's notes first): ISOUND.LOG `0675`, `0468`
   (bounded prefixes); ISOUND.117 `07E9`; MPS_LOGO `1ADC` (`strstr`) and `1C82`; PLAYER `028C1`
   (POPF_STOPS `{ ..., 0x0211, 0x0231 }`); START `8427` and END `4393` (wait handling, open decision on
   `tick_inject`); MPS_LOGO `0307`/`030E`/`0315` (census rows of size 0: check credit first); SETUP `10B3` and
   PLAYER `0211` (STI in a loop); SETUP `0094` (interrupt-shadow entry); VGAME `D569`; ASOUND.LOG `00FE`
   (withdrawn: do not run C after a nested callee that executes STI). Lead list `D:/f117-gate/p2-leftovers-bl-LEADS.md`;
   the 127 five-byte run-time-patched far-jump thunks should be one generic body registered for many entries.
   `D:/f117-gate/kit/DECLINED.tsv` is out of date: it still lists END `057B0` (matched in bm) and PLAYER `02853` as
   STI-first original (a bn proof now covers its prefix); reconcile it before handing out declined routines.
4. **Owner decisions of 27 Oct to carry out:** START `08F94` (`8F94..8FC2`, the C run time's divide-error
   handler) is split out of the start-up census row, proved and counted (its PROGRAM_ENDS row
   `{ "START.EXE", 0, 0x8F94, 0xF5F2 }`); the probable-dead rows are proved with lockstep inputs, not declared dead on weak evidence
   (831 bytes at the 21 Oct review; recount first: `PROBABLE` rows of `D:/f117-gate/phase2-final/dead-cert/certificates.tsv`
   not yet matched); machine-option error paths are proved with test inputs (no test-only machine
   options).
5. **Checklist round 5** (no gate, almost no CPU): round 4 (`D:/f117-gate/checklist-full-20261026/round4/`)
   left 111 units unadopted: 36 where the reader's status differed from the structural proposal (a fresh reader
   must confirm the status), 25 range-wording, 6 missing fields, 33 accept-with-changes (apply the reader's
   correction, then a DIFFERENT reader), 11 needs-context (add what the reader asked for, e.g. START's stream
   helper at `0x9992`). Build packets with `round4/build_r4.py` as the model, one fresh blind reader per group,
   stage with `stage_r4.py`, validate with `validate_stage_r4.py`, then apply to `docs/reference-reviewed.tsv`.
6. **Remaining Phase 2 work beyond these** (definition: [docs/progress.md](docs/progress.md)): the declined
   routines, the driver leads, the packer stub's representation-aware review, the encoded SETUP code, sorting the
   27,782 fixture-only matched bytes into routed or reviewed, and the closure review of indirect calls, tables,
   callbacks, vectors and self-modifying code before declaring 100%. Details in [docs/roadmap.md](docs/roadmap.md).
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
