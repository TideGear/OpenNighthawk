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

- **Active branch:** `master`. The P3 remaster history is merged here. P2 batch `bx` passed the full gate and its code is committed as `a02efaf`; the local master is not pushed, per the instruction to continue here rather than push and stop. Use `master` for the next loop. The separate `p3-hybrid-sound` work remains excluded.
- **Scoreboard:** P1 100.00%, P2 96.66%, P3 85.42%, All 95.30% (`py tools/progress.py --title`). Refreshed with census `D:/f117-gate/census.tsv`, named cover `D:/f117-gate/p2-batch-bx-accept/named-cover`, route cover `D:/f117-gate/route-cover-20261020b`, and the round-10 PLAYER grayscale reference row. Gameplay is 98.85% (159,398 named bytes plus 2,631 reviewed design-rule bytes of 163,907). The named-code all-code measure is 191,053 / 209,082 bytes (91.38%). Reviewed references cover 47,734 / 47,736 outside-route bytes (1,897 rows; two VGAME bytes remain). There are 1,878 route-executed unmatched bytes: ASOUND.117 376, ISOUND.117 401, VGAME 324, PLAYER 156, ASOUND.LOG 169, RSOUND.LOG 83, START 66, SETUP 57, END 63, ISOUND.LOG 158, MPS_LOGO 18, DSWAP 2, RSOUND.117 5.
- **Latest accepted batch:** `bx` adds SETUP `10B3` (bounded retrace-countdown prefix) and ISOUND.LOG `0545` (timer interrupt handler). It contributes 89 named bytes / 40 fully covered instructions; all 51 private mutants were caught and all ten optimized seeds had positive comparisons. The credit audit found zero claimed-but-uncredited bytes. Evidence is `D:/f117-gate/p2-batch-bx-accept/`.
- **Gate:** `final-gate-verdict.json` passed 823 tests, 63 routes and 62 fresh interpreter sessions. All 1,638 registered routines were checked with zero mismatches; the two instruction timing profiles and three gate seeds also had zero mismatches. The changed interpreter/game inputs required the sequential fresh-baseline audit, which passed all 63 routes. All 63 N=1 presentation results and boot N=4/9 were exact. The full acceptance took 2,074.5 s (core gate 1,531.5 s). The verdict hashes for `matched.c`, `func_lockstep.c` and `CMakeLists.txt` match the accepted source. The independent prechecks are in `D:/f117-gate/p2-remaster-precheck-20261030/`.
- **Saved owner docs:** `stash@{0}` (`preserve owner docs before P3 acceptance merge`) was applied; its round-10 PLAYER reference and decision are included in the documentation commit. The stash remains as a backup and has not been dropped.
- **Still-local owner work:** `tools/sound_board_page.py` and `tools/sound_board_template.html` have unrelated edits, and `4k60.bat` / `original-fixed.bat` are untracked. Preserve them and keep them out of the P2 acceptance commit.
- **P2 gaps and decisions:** ASOUND.117 `09F2` remains excluded pending the D2-fix interaction decision; ISOUND.LOG `0581` needs an exact invalid-range certificate that does not exclude real timer-handler bytes; six protected checklist units still need owner review at `D:/f117-gate/checklist-full-20261026/protected-owner-review-20261029.md`; PIT waits `0A5B`/`0B01` need split proofs or a reviewed decision. PLAYER `00D5F-00DAF` remains matching work as `needs-route` until a route selects the grayscale arm. Only VGAME `04E92-04E93` remains outside reviewed references; keep it `probable-dead` until a current-image certificate closes alternate and indirect entries. Round-10 evidence is under `D:/f117-gate/checklist-full-20261026/round10-q3-needsroute-20261030/`.
- **Phase 3:** use [docs/p3-handoff.md](docs/p3-handoff.md) for remaining features. Remastered rendering and hybrid sound are still 0%; the settings, controls, replay and accessibility items remain at the percentages in the roadmap.

## Resume here

The accepted runtime source and refreshed documentation are committed on `master`; do not rerun the full gate for documentation-only edits. Start the next P2 loop from the refreshed census and named coverage. No gate or proof worker is currently running. For every subsequent runtime batch, follow [docs/repeated-processes.md](docs/repeated-processes.md#phase-2-batch-acceptance); never start private proof agents during a full gate.
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
