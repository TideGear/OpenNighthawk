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

## Current state (30 October 2026)

- **Scoreboard:** P1 100.00%, P2 96.52%, P3 76.34%, All 92.96% (`py tools/progress.py --title`). Refreshed with
  census `D:/f117-gate/census.tsv`, named cover `D:/f117-gate/p2-batch-bw-accept/named-cover` and route cover
  `D:/f117-gate/route-cover-20261020b`. Gameplay 98.80%, naming 91.33%, checklist 47,265 / 47,740 = 99.01%,
  1,870 reviewed-reference entries; 475 checklist bytes remain. 1,967 executed bytes remain unmatched: ASOUND.117 376, VGAME 324, ISOUND.117 401, ISOUND.LOG 215, PLAYER 156,
  ASOUND.LOG 169, RSOUND.LOG 83, MPS_LOGO 18, SETUP 89, START 66, END 63, RSOUND.117 5, DSWAP 2.
- **Latest accepted runtime:** batch `bw` passed: eight static SETUP routines, 1,257 named bytes, 509 fully covered
  instructions, 281 private mutations caught, 40 optimized-seed checks, 811 CTests, 63 routes / 62 fresh interpreter
  sessions. All routes were identical; both timing profiles and the whole table at three seeds had zero mismatches;
  route-only entries ran and none exceeded event limits. All 63 N=1 presentation cases and boot N=4/9 were exact.
  The credit audit found 0 claimed-but-uncredited bytes. Acceptance took 1,912 s and used the sequential fresh-baseline
  audit because the cached interpreter key was stale. Evidence: `D:/f117-gate/p2-batch-bw-accept/`.
- **Classifier repair:** the owner-approved DSWAP floppy-path and presentation-observer graphics-slot rules are now
  recognized by the census classifier and documented with focused positive tests. No current reviewed entry uses
  either rule, so this repair changes no score. The fresh round-6 reviews raised P2 to 96.52%. The focused scoring suite
  passes (77 tests).
- **Acceptance scripts:** `audit_private.py`, `finalize.py` and `multi_proof.py` are copied forward from the previous
  accepted batch. `new_batch.py` does not copy `multi_proof.py`; carry all three forward for the next runtime batch.
- **Latest fix-only gate:** `bt` passed with no runtime changes or score credit. Commit `fe273e1` is pushed; GitHub Actions
  run 40975806272 passed. Evidence: `D:/f117-gate/p2-batch-bt-accept/`.
- **Working copy:** `C:\Dev\F-117A Recomp`. The old `My Drive` copy is stale. Do not stage the owner's untracked
  `4k60.bat` or `original-fixed.bat`.
- **SETUP proofs:** The idle `18A2` watcher was stopped; its `p2-setup-bt-*` proof directories are preserved.
  The latest eight-range SETUP tranche was accepted as batch `bw`.
- CI is checked through GitHub Actions API (no `gh` on this machine):
  `curl -s "https://api.github.com/repos/TideGear/OpenNighthawk/actions/runs?per_page=2"`.

## Resume here

Goal in force: reach 100% of Phase 2 safely. Batch `bw` passed its full acceptance gate; the round-6 checklist adoption and progress data are now refreshed. No runtime changed in this checklist tranche.

The working branch is `wip/p2-setup-handoff-20261029`. Batch `bw` was prepared from accepted HEAD
`0c2b13a69ce0262567c89ad241031f5de0cb463a`; pushed commit `34dd038` records that acceptance. The current
scoreboard is P1 100.00%, P2 96.52%, P3 76.34%, All 92.96%.

Eight static SETUP ranges (`0562`, `0582`, `05A3`, `05CE`, `070A`, `0811`, `0851`, `0979`) passed private proof,
root audit, static route-neutrality replay and `finish_all.py`. Their preserved proof directories are
`D:/f117-gate/p2-setup-reproof-{0562,0582,05A3,05CE,070A,0811,0851,0979}-20261029`. The changes are integrated in
`CMakeLists.txt`, `src/matched/matched.c` and `tests/func_lockstep.c`; the custom `05CE` return-code probe was applied
by hand. `build.cmd` succeeded and all eight focused CTests passed. Individual route prechecks found no captured
routes reaching these reviewed static ranges, as expected.

The prior whole-table checks disagreed: rotating seed `0x0c2b13a69ce0` passed, while fixed seed `0xA25E20261029`
found one mismatch at `START.EXE:9430` (original AX `D768`, matched AX `0000`). The mismatch also reproduces on
accepted base `0c2b13a69ce0262567c89ad241031f5de0cb463a`, so it was a latent parity defect. Its cause is now traced:
the guest formatter overflow path changes live BP before the caller spills and reloads `[BP-4]`, while matched
`crt_printf` used its entry BP. `src/matched/matched.c` now uses live BP and SS for those BP-relative accesses.
The exact fixed-seed reproduction passes, as do the same-seed five-site shared-printf check and all 28 SETUP CTests;
`build.cmd` succeeds. A route precheck covering all eight new entries found no captured route reaches them, consistent
with the approved static-code policy. An independent code review mapped each changed access to the corresponding
guest BP-relative operand and found no remaining cached entry BP/SS use in `crt_printf`; the trace does not record
transient BP/SS, so the whole-table sweeps remain necessary. These results and current runs are under
`D:/f117-gate/p2-setup-precheck-20261030-livebp/`.

The three whole-table prechecks passed at fixed seed `0xA25E20261029`, rotating seed `0xe1afc9bb8d07` and third seed
`0x0c2b13a69ce0`; each compared all 1,636 routines with zero mismatches. The full `bw` gate, sequential 63-route
baseline audit, 63 N=1 presentation cases, boot N=4/9, optimized seeds and private credit audit all passed. Detailed
logs are under `D:/f117-gate/p2-setup-precheck-20261030-livebp/` and `D:/f117-gate/p2-batch-bw-accept/`.

The owner's untracked `4k60.bat` and `original-fixed.bat` remain untouched and must stay out of commits. The separate
P3 route worker/worktree was left untouched. Use `py tools/progress.py --title` for future accepted commit titles.

Other remaining work is still in scope: ASOUND.117 `09F2` is excluded pending the owner's D2-fix interaction decision;
ISOUND.LOG `0581` needs an exact invalid-range data certificate without excluding real timer-handler bytes; six
protected checklist units need the owner's review (`D:/f117-gate/checklist-full-20261026/protected-owner-review-20261029.md`);
PIT waits `0A5B`/`0B01` need split lockstep pieces across their wake boundaries or a reviewed decision; ISOUND.LOG `0545`
needs proof in pieces; SETUP `10B3` needs the proposed prefix/interior split. MPS_LOGO `111D` partial route coverage was
accepted in `bu`; do not reopen it absent new evidence.

Round 6 is complete against accepted commit `34dd038` and the `bw` named cover. Its fresh set had 36 units / 2,181
bytes across eight groups. Root reconciliation verified all 2,181 original bytes and 9,837 disassembly lines with no
gaps, duplicates, overlaps or reviewed-byte collisions. Three fresh readers returned 36 verdicts; 22 units / 1,875
bytes were staged, validated and appended to `docs/reference-reviewed.tsv`. The checklist is now 47,265 / 47,740
bytes (99.01%); P2 is 96.52%, up 0.39 percentage points. Of the 14 unadopted units / 306 bytes, 10 / 202 need correction, context or a fresh independent read; four / 104 were withheld for protected statuses and need separate owner or design-rule review. Six additional protected units / 169 bytes still need owner review. Private round evidence is under
`D:/f117-gate/checklist-full-20261026/round6-bw-34dd038-refresh/`. Continue by building a fresh next round from the
current live scope; do not reuse round-5 packet counts or round-6 source identities. Run at most three reader agents
beside root (four participants total). The route-independent credit pool and open rules are in
`docs/roadmap.md` and `docs/progress.md`. Current largest
executed-code gaps are ASOUND.117 PIT waits, ISOUND.117 `026C`, presentation-observer VGAME thunks, and ISOUND.LOG `0581`;
regenerate the lead list from the refreshed census/named cover before choosing the next body. Static naming and reviewed
reference entries also contribute materially to P2. Protect private game data and captures from the public repository.

## Owner decisions in force

Scoring decisions with dates are in [docs/progress.md](docs/progress.md); this is what governs daily work.

- **Static code is matchable on lockstep alone (owner, 29 Oct 2026).** Reviewed static ranges in `docs/census-code.tsv` that no
  captured route reaches are accepted without a real-route call: seeds, all mutants, audit, gate and the route-neutrality
  replay (`kit.py DIR checkpoints --static`, or `"static": true` in the proof's config.json so `finish_all.py` does it);
  the kit refuses it for code any capture reaches. Credit is the naming item's, not the gameplay score's.
- **No meaningful game code may be missed.** Launcher, resident graphics library and sound drivers are in Phase 2
  scope; declined routines are work, not exclusions; every range that cannot be matched gets a reviewed reference
  entry. The checklist is done in full by independent blind readers; cost is no object, optimise wall time only.
- **Design rules for code that stays original:** the program launcher (F117.COM `0100..03CA`), hardware wait loops
  only when no lockstep state can complete them (so far none qualifies), STI-first interrupt handlers, the packer
  stub, DSWAP's floppy path (no emulated floppy drive), and presentation-observer graphics slots. Each affected range
  needs its own reviewed reference entry before credit. Dead code leaves the denominator only with a certificate.
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
- Hand-apply the harness rows `integrate_many.py` does not carry (list under Traps), build with `.\build.cmd` from
  PowerShell, run only the new CTests, then `route_precheck.py` on every new entry and two whole-table seeds.
  Shared-helper and fix-only repairs require a third pre-gate seed.
- Run `audit_private.py` BEFORE the gate (the `new_batch.py` help text says afterwards: it is wrong); copy it and
  `finalize.py` forward from the previous batch. Check `final-gate-verdict.json`'s `source_sha256` against the tree.
- Commit titles start with `py tools/progress.py --title`; refresh the score with the new batch's named cover.

Running wide: a proof is about 10-15 minutes of compute inside about two hours of agent time, so wall time follows
the number of agents. The current user limit is four agents at once. `kit.py` throttles builds to three
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
