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

- **Active branch:** `master`. The P3 remaster history is merged here. P2 batch `bx` passed the full gate and its code is committed as `a02efaf`; handoffs were synchronized in `21eb682`, and `master` currently matches `origin/master` (0 ahead / 0 behind). Use `master` for the next loop. The separate `p3-hybrid-sound` work remains excluded.
- **Scoreboard:** P1 100.00%, P2 96.66%, P3 85.42%, All 95.30% (`py tools/progress.py --title`). Refreshed with census `D:/f117-gate/census.tsv`, named cover `D:/f117-gate/p2-batch-bx-accept/named-cover`, route cover `D:/f117-gate/route-cover-20261020b`, and the round-10 PLAYER grayscale reference row. Gameplay is 98.85% (159,398 named bytes plus 2,631 reviewed design-rule bytes of 163,907). The named-code all-code measure is 191,053 / 209,082 bytes (91.38%). Reviewed references cover 47,734 / 47,736 outside-route bytes (1,897 rows; two VGAME bytes remain). There are 1,878 route-executed unmatched bytes: ASOUND.117 376, ISOUND.117 401, VGAME 324, PLAYER 156, ASOUND.LOG 169, RSOUND.LOG 83, START 66, SETUP 57, END 63, ISOUND.LOG 158, MPS_LOGO 18, DSWAP 2, RSOUND.117 5.
- **Latest accepted batch:** `bx` adds SETUP `10B3` (bounded retrace-countdown prefix) and ISOUND.LOG `0545` (timer interrupt handler). It contributes 89 named bytes / 40 fully covered instructions; all 51 private mutants were caught and all ten optimized seeds had positive comparisons. The credit audit found zero claimed-but-uncredited bytes. Evidence is `D:/f117-gate/p2-batch-bx-accept/`.
- **Gate:** `final-gate-verdict.json` passed 823 tests, 63 routes and 62 fresh interpreter sessions. All 1,638 registered routines were checked with zero mismatches; the two instruction timing profiles and three gate seeds also had zero mismatches. The changed interpreter/game inputs required the sequential fresh-baseline audit, which passed all 63 routes. All 63 N=1 presentation results and boot N=4/9 were exact. The full acceptance took 2,074.5 s (core gate 1,531.5 s). The verdict hashes for `matched.c`, `func_lockstep.c` and `CMakeLists.txt` match the accepted source. The independent prechecks are in `D:/f117-gate/p2-remaster-precheck-20261030/`.
- **Saved owner docs:** `stash@{0}` (`preserve owner docs before P3 acceptance merge`) was applied; its round-10 PLAYER reference and decision are included in the documentation commit. The stash remains as a backup and has not been dropped.
- **Still-local owner work:** `tools/sound_board_page.py` and `tools/sound_board_template.html` have unrelated edits, and `4k60.bat` / `original-fixed.bat` are untracked. Preserve them and keep them out of the P2 acceptance commit.
- **P2 gaps and decisions:** ASOUND.117 `09F2` remains excluded pending the D2-fix interaction decision; ISOUND.LOG `0581` needs an exact invalid-range certificate that does not exclude real timer-handler bytes; the 29-Oct protected note is stale on five units: current second-read rows cover 167/169 bytes (including 163 bytes that still need matching/proof and four already covered by original-code rules), leaving only VGAME `04E92-04E93` without a reviewed-reference row. The current-image Q6 audit is under `D:/f117-gate/checklist-full-20261026/round9-q6-cert-20261030/`; review its entry/dead-certificate closure before changing status. PIT waits `0A5B`/`0B01` need split proofs or a reviewed decision. PLAYER `00D5F-00DAF` remains matching work as `needs-route` until a route selects the grayscale arm. Round-10 evidence is under `D:/f117-gate/checklist-full-20261026/round10-q3-needsroute-20261030/`.
- **Remaining naming and closure work:** the 18,029-byte all-code gap includes the 1,878 gameplay bytes (310 census + 1,568 supplemental); 11,652 additional bytes are reviewed static code with no match credit. Reconcile `docs/static-code-review.md`'s 10,301-byte report with the live 11,652-byte count before calling that review complete. The broad indirect-call/dispatch/callback/vector/overlay/self-modifying-code closure still needs a final review. The historical probable-dead tally (12 rows / 831 bytes) is stale: ten rows / 477 bytes were upgraded to dead in the 21-Oct-2026 review, while current reference rows show no probable-dead entries; reconcile the residual 354 bytes against current named coverage.
- **Phase 3:** use [docs/p3-handoff.md](docs/p3-handoff.md) for remaining features. Remastered rendering and hybrid sound are still 0%; the settings, controls, replay and accessibility items remain at the percentages in the roadmap.

## Resume here

The accepted runtime source remains `master` at `21eb682` and matches `origin/master`; P2 is still 96.66%. This stopped loop changed no production or test source and accepted no new bytes. The size survey ran from about 17:15 to 18:00 without finding a proof-ready 1,000-byte route subset. Treat the survey as sufficient for now and resume with proof work rather than another broad inventory pass.

- **Normal batch target:** 30–50 routines and about 1,000–1,500 newly proved route bytes. Count only exact newly route-executed bytes. The current confirmed safe set is smaller; do not pad it with raw census extents or already-covered code. Follow [docs/repeated-processes.md](docs/repeated-processes.md#phase-2-batch-acceptance) and never run private proof builds during a full gate.
- **ISOUND.117 `019C:026C`:** the 10/28 private dossier reports 336 bytes / 111 instructions and 276 newly route-executed bytes, with five positive seed logs and 74/74 mutants caught. It is stale against current master (1,508 vs. 1,638 registered routines), so do not integrate its old snapshots or verdict. A fresh copy at `D:/f117-gate/p2-isound-117-clamp-refresh-20261030/` has current `matched.c` and `func_lockstep.c` snapshots and preserved the reviewed 111-instruction scope. Its `build --func-only` completed at 18:00, but no lock, seeds, mutations, fresh checkpoint/verdict, route precheck, or full gate ran. The private-kit libraries are from `28b34b2`; `src/matched/observe.c` changed after that. Before trusting this refresh, copy the current `build/f117recomp.lib` into the fresh proof's `project/libs/`, rebuild, and run the complete private proof. `f117cpu.lib` and `f117machine.lib` still match the kit copies.
- **ASOUND.117 `0B01`:** the first private proof built and its coverage lock observed all 46/46 instructions (1,922 compared states), but every fixed seed failed with “fixtures reached too little”; the named run had 3,461 long-run skips and fixed seeds had zero comparisons. The fixture scripts only seven port-42 reads despite a randomized 1–6 record count and repeated PIT waits. No mutations ran. A one-record diagnostic copy was being built during handoff; its process was stopped before a completion result, so its build status is unverified and it has no lock/seed evidence. Preserve both the initial failure and diagnostic artifacts; retry only in a fresh copy with a terminating poll script and current libraries.
- **Other sound candidates:** ASOUND.117 `0A5B` (166 bytes) remains an unproved PIT wait; screen/prove it only after the current ISOUND proof progresses. ASOUND.LOG `03F8–042F` earns zero new bytes because its instructions are already covered by accepted `0344`, `0423`, and `042C` scopes. Exclude the ASOUND.LOG `008A` full scope: its existing proof misses 12 mutants. Thus the previously quoted 583-byte candidate pool was provisional; only the 276-byte ISOUND dossier is currently private-ready, and it still needs a current-base proof.
- **Ghidra:** sound-driver headless attempts stalled before import/analysis, and the GUI bridge was unavailable. No Ghidra output confirms the current candidates. Keep Ghidra in the next screening pass: reuse the installed 12.2 setup and existing projects, diagnose headless startup or use the known GUI, and treat any decompilation as a lead that still needs byte, route, lockstep, and mutation checks.
- The 66-byte SETUP static-wrapper pilot also remains incomplete: its builds were stopped without a successful `build --func-only`, and no proof or gate ran. The local owner edits in `tools/sound_board_page.py`, `tools/sound_board_template.html`, `4k60.bat`, and `original-fixed.bat` are intentionally not part of the handoff commit.
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
- **PLAYER grayscale arm (owner, 30 Oct 2026):** `00D5F-00DAF` remains matching work. The palette-loader rule does
  not extend to this exact fragment; keep it `needs-route` until a route selects it, with no census-original split.
- **Parity target is the original on its own hardware, not DOSBox**; gameplay and logic parity outrank rendering.
  Keep all original graphics; no vector replacements.
- **Phase 3 features are off or inert unless asked for:** `--crt`, `--subtitles`, `--sound`, `--remap`, `pad-*`, `--watch` and the
  settings menu (Ctrl+Alt+S) change nothing in the game's logic; the owner has not yet looked at any of them.
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
- Phase 3 generated files (sound sweeps, rendered clips, speech cuts, memory dumps): `C:/Users/Tideg/f117-recomp-local/p3-session-29oct/`;
  MT-32 ROMs: `C:/Users/Tideg/f117-recomp-local/roms`; the sound page: https://claude.ai/artifact/1jdGUvk4aD8bsCSxDvJbta.
- Generated code `C:/Users/Tideg/f117-recomp-local/gen` (never committed); coverage and route outputs beside it.
- Ghidra 12.2 is already installed at `D:/ghidra-trial/ghidra/ghidra_12.2_PUBLIC`; reuse its existing projects (`proj/all_SETUP.gpr`, `proj/rng_SETUP.gpr`) and `out_ranges/` indexes for static routine discovery, exact-boundary leads and call-flow cross-checks. Before opening a candidate, resolve canonical/linear address aliases against existing matched starts and named coverage (VGAME `1132:0002` aliases already-matched `1058:0DA2`; `1132:0052` aliases `1058:0DF2`). Treat decompiler output as a lead only; verify original bytes, route coverage and semantics with the private lockstep/mutation/acceptance checks.
- Existing Ghidra export `out_ranges/SETUP/SETUP_0000_0CB2` decompiles D85/D98/DAE as exact 19/22/25-byte wrappers calling 0E97/0EC2/0F52; these sizes and call targets agree with the Capstone draft dossiers. Its index records all three completed decompilations. Caller exports show CB2→D85 (`f_CB2.c`), CE5→D98 (`f_CE5.c`), and D1B→DAE (`f_D1B.c`). The D1B→DAE evidence conflicts with `reference-reviewed.tsv`'s “no direct call site” wording for DAE, so preserve and review that discrepancy rather than silently changing the reviewed row. Ghidra's D1B prototype is unreliable here: it renders a one-argument call despite DAE's five-word stack contract, so verify raw caller pushes before making any documentation change.
- The private D85/D98/DAE fixture guards now force wrong-stack-slot mutants to distinct values (D85 `SS:SP+6=0800`, D98 `SS:SP+8=9000`, DAE `SS:SP+10=0180`); DAE displacement is now `0300–03FF`, disjoint from its `0200–02FF` count range. These are fixture edits only; the lockstep/mutation/acceptance gate has not yet run.
- The seven string-memory candidates at 1276, 1291, 12AD, 12F8, 130C, 1322 and 1340 are not in the existing Ghidra function indexes; no exact target body was exported. Caller context exists for 1276 (`out_ranges/SETUP/SETUP_0000_0562/f_5A3.c`: 200-byte local destination, two parameters), 1322 (`f_70A.c`: copies a local string then NUL-terminates), and 1340 (`f_5CE.c`: local destination, far source and count, then NUL-terminates). Treat that as argument/use context only. There is no target-specific exported caller for 1291, 12AD, 12F8 or 130C. Nearby 1244/125F bodies are separate routines, not substitutes for these candidates.
- The three wrapper config/mutation JSON files and `body.c` initially carried UTF-8 BOMs. `kit.py`'s plain `json.loads(Path.read_text())` rejects the JSON BOMs, and MSVC rejects the copied C BOMs. Only those leading BOM bytes have been removed from D85/D98/DAE inputs; all six JSON files parse strictly, and all three bodies now begin with `/*`. The subsequent clean `build --func-only` attempts were stopped during compilation; no lockstep, seed, mutation, or verdict evidence exists.
- The current Ghidra START range export only includes `86A8`; there is no `f_54BB.c`. Existing census/route evidence supports the 54BB routine and 54D8 arm, but Ghidra bounds/call-flow for this candidate are still unconfirmed. Fresh headless attempts, including `analyzeHeadless --help`, stalled without output and were stopped; an isolated JDK 25 override did not fix it. The current environment has `JAVA_HOME` set to JDK 8 even though JDK 25 is also on `PATH`; diagnose CLI startup or use the known-working GUI before relying on headless analysis.
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
