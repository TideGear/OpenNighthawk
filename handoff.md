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

- **Scoreboard:** P1 100.00%, P2 95.65%, P3 76.34%, All 92.69% (`py tools/progress.py --title`).
  The full figures: `py tools/progress.py --census D:/f117-gate/census.tsv --named-cover
  D:/f117-gate/p2-batch-bs-accept/named-cover --route-cover D:/f117-gate/route-cover-20261020b --data D:/GOG/F-117A`
  (gameplay 98.57%, naming 89.8%, checklist 45,390 / 47,587 = 95.38%, 2,342 executed bytes unmatched: ASOUND.117 563,
  VGAME 324, ISOUND.117 401, ISOUND.LOG 225, PLAYER 157, ASOUND.LOG 169, RSOUND.LOG 125, MPS_LOGO 112, SETUP 89,
  START 67, END 64, RSOUND.117 44, DSWAP 2).
- **Accepted runtime:** gate `bs`, passed: 2,005 s, 781 tests, 63 routes, 1,606 matched routines, 174 private mutations,
  whole table at three seeds 0 mismatching, route precheck identical. Evidence `D:/f117-gate/p2-batch-bs-accept/` (its
  `audit_private.py`, `finalize.py` and `multi_proof.py` are the current ones: copy them forward from the newest accepted
  batch with `cp`, never `cp -n`). Earlier today: gates `bq` (43 far-jump thunks) and `br` (six routines).
- **Latest fix-only gate:** `bt` passed after a harness-only correction; runtime and P2 credit are unchanged. The gate
  ran 781 tests and 63 routes, the three whole-table seeds had 0 mismatches, and the final verdict reports 0 new
  instructions or named bytes. Both presentation checks and the baseline group verdict passed. Evidence:
  `D:/f117-gate/p2-batch-bt-accept/`; its source hashes match the working tree.
- **Working copy:** `C:\Dev\F-117A Recomp`. The old `My Drive` copy is stale; do not work there.
- CI is checked through the GitHub Actions API (no `gh` on this machine):
  `curl -s "https://api.github.com/repos/TideGear/OpenNighthawk/actions/runs?per_page=2"`.
- **Background proof pipelines were found running on resumption**, contrary to the earlier pause note. Their five process
  trees were stopped before the fix-only gate, preserving their private directories; SETUP `0979` and VGAME `1452:0556`
  advanced to later entries before stopping. Recheck process trees and any interrupted proof results before reuse. The
  current user limit is four concurrent agents, and no proof agent runs while a gate does.

## Resume here

Goal in force: reach 100% of Phase 2 safely. A batch is committed only after its full gate passes.

The `bt` fix-only batch is fully verified and passed. The rotating-seed precheck originally exposed a VGAME `5046`
fixture assumption at state 2060; the older accepted binary reproduced it, so it was not caused by the pending harness
edits. The trace showed the nested original-code runner could already execute the same divide at `5122`. The fixture now
accepts either a hand-back immediately before that instruction or an already-taken divide fault only when both sides
are at the same `CS:IP`, interrupt depth and operation boundary; it then compares the complete machine state, memory and
I/O. Nothing is skipped. The targeted test, both full precheck seeds, all three gate seeds, and the two ground-defence
CTest cases passed. The gate ran 781 tests and 63 routes; baseline, presentation-n1 and presentation-boot checks passed.
`final-gate-verdict.json` reports `passed`, zero new bytes and matching source hashes. No runtime body changed and P2
remains 95.65%.

No gate or proof job is running. The next step is to commit and push the verified harness fix, then continue with the
root-verified private proofs and static checklist ranges below. Do not stage the owner's untracked `4k60.bat` or
`original-fixed.bat`.

On resumption, `026C`'s earlier origin-only run was confirmed as a coverage-range error: 5,943 equal states,
zero skipped, but 102/111 instructions counted because the shifted tracking window ended early. The corrected tracker
counts 111/111 in the entry window while tracking lower shared tails; its 74/74 mutants and five seeds passed, and
`ibm_boot` called it 8,293 times with 26 exact checkpoints. Its test-only call-clamp probe assumes no earlier event;
the controlled runner sets the nested limit directly, and the probe passed. The fresh route gate also passed; its
`ibm_boot` session called the far entry 8,120 times. The tracker and probe are included in the passed fix-only gate.
The PIT loops can be driven by finite scripted reads; `0A5B` needs
separate proof pieces at its port-wake boundaries, so neither loop is certified original. The six protected units'
evidence is prepared at `D:/f117-gate/checklist-full-20261026/protected-owner-review-20261029.md`; no credit or owner
decision is recorded by that note. The observer-slot design rule is now documented.
Route choices for the pending batch are in `D:/f117-gate/pending-bs-routes.txt`: the three MPS_LOGO entries need a
damaged-install route and the Roland entries need `roland_boot`. For ISOUND.LOG `0581`, do not classify the entire
range as data merely because the error route also runs off-alignment zeroes: the capture includes the real timer
handler's instruction stream as well as changed zero-filled execution. Establish exactly which records are invalid
before making the requested data certificate or changing score accounting.
(a) the owner accepted my recommendations on the decision list: bundle the pending shared-harness edits into one reviewed
fix-only gate (005E's `g_ipf`/`g_strict_limit`/`POPF_STOPS`, 0A17's `g_port_script`, the one-instruction decline probe,
PLAYER `1ADF`/`1AD4` `FATAL_JUMPS` rows, the Roland `POPF_STOPS` rows); try a multi-port PIT script for `0A5B`/`0B01`, else
certify; use the reviewed `026C` evidence above; keep the D2 fix at `09F2` and credit the fixes-off lockstep; credit
`111D`'s routed part plus lockstep; `0581` as data certificate; `0545` in pieces; `10B3` split; present the six protected
units' prepared evidence for the owner's review. (b) These private proofs are
root-verified (mutants all caught, five seeds ok, from the result files) and need `finish_all.py` then a batch: 005E (79/79),
0A17 in `p2-asound-117-br-pit` (29/29), RSOUND.117 `0D07` (36/36) and RSOUND.LOG `03A2` (40/40) on `roland_boot`, MPS_LOGO
`1046` (22/22), `103B` (22/22), `111D` (25/25), tiny leaves ISOUND.LOG `060C` (17/17) and `05D1` (21/21), START `8DD7`,
END `4CBB`, PLAYER `026B:0306` (12/12 each), ASOUND.117 `09F2` (24/24). (c) The static-range pilot (Ghidra reading aid at
`D:/ghidra-trial/out_all` and `out_ranges`, 16 largest unregistered ranges of `docs/census-code.tsv`) was stopped mid-build:
dirs `p2-setup-bt-*` (17B0 family drafted, 0562 family set up) and `p2-vgame-bt-1452-*` (0063 and 04D9 families fixtured);
resume those agents or start fresh from the directories, set `"static": true` in each config.json once steps 1-7 are green,
and run `finish_all.py` (the kit's `checkpoints --static` accepts code no capture reaches). The whole static pool is worth
about +2.1 points at 0.00015 points a byte; the pilot's four ranges (2,660 bytes) about +0.4.

1. **Proved privately, not yet integrated** (all under `D:/f117-gate/`). The root verifies every result file itself (an
   agent's tally is not evidence: read `mutation-results.json`, `seeds.json`, the lock log's skipped column) and runs
   `py D:/f117-gate/accept-kit/finish_all.py DIR[:ROUTE]` before `integrate_many.py` (which needs `private-verdict.json`).
   Check the route with `grep -l "MODULE .* SEG IP" D:/f117-gate/route-cover-20261020b/*.cov`: PLAYER `1F4D` is run only
   by `err_player_assert`, and `finish_all` on `full_cycle` failed with 0 calls.
   - **Ready after checkpoints and verdict** (numbers read by the root): ASOUND.117 `09F2` (`p2-asound-117-br-09F2`, 24/24;
     owner decision pending on how the D2 fix at the same entry interacts, route unconfirmed). VGAME `0010`, START `9D5C` and
     the START, PLAYER and SETUP thunks were accepted in gate `bs`.
   - **Root checked the seed logs, result files and input ages; not yet integrated:** ASOUND.117 `005E` (`p2-asound-117-br-005E`, 79/79 mutants; needs shared
     harness changes including a new `g_strict_limit` overrun check, so it needs a fix-only gate and a re-check of every
     accepted routine); MPS_LOGO `1046`, `103B`, `111D` (`p2-mpslogo-br-*`; `111D` has no route that runs all 63
     instructions, so partial route coverage needs a ruling; IMAGE_PATCHES and FATAL_JUMPS rows to carry); RSOUND.117
     `0D07` (36/36) and RSOUND.LOG `03A2` (40/40) (`p2-sndlog-br-*`, root-verified full mutant sets; POPF_STOPS rows
     `0D07 -> 0D3A` and `03A2 -> 03D8` to carry); tiny leaves ISOUND.LOG `060C` and `05D1`, START `8DD7`, END `4CBB`, PLAYER
     `026B:0306` (`p2-tiny-br-*`; they need a shared decline-probe change for one-instruction bodies). **VGAME thunk family**
     (`p2-vgame-br-thunks`, 53 entries, 30/30 mutants, five seeds): left out of gate `bs` because 28 of its 34 non-obvious
     entries share an address with a presentation observer (`code_overrides` lists them; `screen_leads.py`'s hook screen is
     not enough, it missed observers registered in table form); a plain thunk there would displace the draw capture. The
     presentation-observer graphics slots stay original under the owner's decision; review the remaining entries separately and
     run the presentation checks (`present_light_check.py`, `presentation_check.py --mode rerun`) beside any gate that
     changes presentation behavior.
   - **Not passing:** PLAYER `1AD4` (2 missed over-claims) and `1ADF` (one long-run state): the cause is missing
     `FATAL_JUMPS` rows for PLAYER `1ADF`/`1AD4` to `1A91` in the shared harness, so a repair-only gate; VGAME `01B9` (3
     missed: two need a probe whose stop lands inside the callee); START `9E9A` (957
     undiagnosed long-run skips, parked); ASOUND.117 `097F` (boundary hand-back at `09F2` never completes under the harness;
     `096D`, `0998`, `09A1` unbuilt); ASOUND.LOG `008A` (needs boundary `00AC` and the POPF row
     dropped) and `05D1` (two unobservable mutants); ISOUND.LOG `0545` and `0138` (blocked); SETUP `10B3` (STI inside the
     retrace loop; proposed split: a prefix proof with boundary `10D3` plus an interior entry at `10D3`); VGAME `1132:0002`,
     `1132:0052` and `1D54` (tails after an STI in matched routines).
   - **Earlier proofs still waiting:** ISOUND.117 `026C` (`p2-isound-117-clamp-cov`, 74/74 mutants; review and route
     evidence above) and `07E9` (`p2-isound-117-bo-07E9`, 27/27).
2. **Owner decisions pending** (recommendations agreed in conversation are in brackets): the PIT wait loops ASOUND.117
   `0A5B`, `0B01`, `0A17` (308 bytes) [scripted multi-port PIT readings in the harness, a repair-only gate, falling back to a
   hardware-wait-loop certificate only if no sound design exists; `0A5B` needs proof pieces across wake boundaries
   `0AC4` and `0ACD` (entry starts `0AC0` and `0ACB` respectively), plus the STI at `0B00`]; ISOUND.LOG `0581` [certify the
   off-alignment/junk execution as data only with exact range evidence; keep real handler bytes in the denominator]; the six protected
   checklist units [prepare evidence for the owner's review]; ISOUND.LOG `0545` [prove in pieces, do not widen the rule]; the D2
   fix interaction at ASOUND.117 `09F2`.
3. **Lessons from this wave** (add to the traps below): run the whole non-lockstep unit set
   (`ctest --test-dir build -E func_lockstep --parallel 8`, about 160 s) after the build and before the gate; it caught the
   observer collision (`code_overrides`) that the gate would have found 15 minutes in. The pre-check seeds (the gate's
   rotating seed is the first 12 hex digits of HEAD, so it changes each commit) found two latent defects in accepted bodies,
   VGAME `73C8` and `peek_over`/`F06C`; test an older build on the same seed to tell latent from new. Launch background
   jobs with `CREATE_NO_WINDOW` (0x08000200), never `DETACHED_PROCESS` (it opens a console window per child). a proof's "N of N instructions reached" is not what the body
   executed: `0A33`'s body never ran past `0A46` because the PIC mask write lowers the run limit to 0, which left 13 mutants
   uncatchable until a trace showed it; narrow such a proof and hand back. A mutant is equivalent only by construction
   (for example `SM4_NEED(n, at)` after a passing `room(c, n)` never uses `at`, and `SUB` for `CMP` with a discarded result),
   never because its path is unexercised. A stopped mutation run leaves `project/.mutations-restore`, which blocks `lock`,
   `seeds` and `checkpoints`; restore with `kit._project_restore`. The mutation workers can crash on `vswhere.exe` after a good
   compile: rerun. Several lead rows are tails of routines already matched (a RET after an STI, `1132:0002` is `1058:0DA2`):
   check the parent before proving. A foreground wait loop over 10 minutes is auto-backgrounded; use a background until-loop.
4. **Remaining unmatched executed bytes (2,342) and where the points are:** the ranked lead list is
   `D:/f117-gate/p2-leads-bp/LEADS.tsv`, stale: `py D:/f117-gate/accept-kit/screen_leads.py LEADS.tsv --min-bytes 8` shows
   only 5 of 60 leads clean (all hard), so regenerate the list from the current census and named cover after each batch.
   Largest left: ASOUND.117 PIT wait loops, ISOUND.117 `026C` (276 executed), the VGAME thunk family (170 bytes, observers),
   ISOUND.LOG `0581` (data). The naming bar (weight 31.5, 89.8%) is the biggest pool of points left: it counts reviewed static code and
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
- Hand-apply the harness rows `integrate_many.py` does not carry (list under Traps), build with `.uild.cmd` from
  PowerShell, run only the new CTests, then `route_precheck.py` on every new entry and two whole-table seeds.
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
