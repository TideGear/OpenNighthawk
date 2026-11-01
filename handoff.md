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

## Current state (31 October 2026)

- **Active branch:** `master` is the main branch and is the branch for this P2 work. Batch `by` has passed acceptance; this commit records the accepted code and updated progress/docs. The Phase 3 branches are merged (2adbdad); see "Resume here" before continuing that work.
- **Scoreboard:** P1 100.00%, P2 96.81%, P3 85.42%, All 95.34% (`py tools/progress.py --title`). Refreshed with census `D:/f117-gate/census.tsv`, named cover `D:/f117-gate/p2-batch-by-accept/named-cover`, and route cover `D:/f117-gate/route-cover-20261020b`. Gameplay is 99.08% (159,771 named bytes plus 2,631 reviewed design-rule bytes of 163,907). The named-code all-code measure is 191,693 / 209,298 bytes (91.59%). Reviewed references cover 47,734 / 47,952 outside-route bytes. There are 1,505 route-executed unmatched bytes: ASOUND.117 376, ASOUND.LOG 169, ISOUND.117 28, ISOUND.LOG 158, VGAME 324, PLAYER 156, RSOUND.117 5, RSOUND.LOG 83, START 66, SETUP 57, END 63, MPS_LOGO 18, DSWAP 2.
- **Latest accepted batch:** `by` adds ISOUND.117 `019C:026C` and `019C:07E9`, plus the Ghidra-informed static SETUP `0000:0CB2` wrapper. It contributes 640 named bytes (373 newly route-executed, 267 fixture-only), 175 fully covered instructions and 125 caught private mutants; the bodies total 478 bytes. The credit audit found zero claimed-but-uncredited bytes. Evidence is `D:/f117-gate/p2-batch-by-accept/`.
- **Gate:** `final-gate-verdict.json` passed 826 tests, 63 routes and 62 fresh interpreter sessions. All 1,641 registered routines were checked with zero mismatches; both timing profiles, both whole-table prechecks, all 15 optimized seeds, and the sequential fresh-baseline audit passed. All 63 N=1 presentation results and boot N=4/9 were exact. The full acceptance took 3,370.2 s (core gate 2,557.5 s). The verdict hashes for `matched.c`, `func_lockstep.c` and `CMakeLists.txt` match the accepted source. Ghidra's SETUP export and the independent prechecks are under `D:/f117-gate/p2-static-ghidra-20261030/` and `D:/f117-gate/p2-batch-by-precheck-20261031/`.
- **Saved owner docs:** `stash@{0}` (`preserve owner docs before P3 acceptance merge`) was applied; its round-10 PLAYER reference and decision are included in the documentation commit. The stash remains as a backup and has not been dropped.
- **Still-local owner work:** `tools/sound_board_page.py` and `tools/sound_board_template.html` have unrelated edits. Preserve these and the untracked `.ppm`, `100`, `4k60.bat`, `original-fixed.bat`, and `x.slice(0` files; they are excluded from the P2 acceptance commit.
- **P2 gaps and decisions:** the route-executed unmatched total is now 1,505 bytes (module breakdown above). ASOUND.117 `09F2` remains excluded pending the D2-fix interaction decision; ISOUND.LOG `0581` needs an exact invalid-range certificate that does not exclude real timer-handler bytes; the 29-Oct protected note is stale on five units. The current-image Q6 audit is under `D:/f117-gate/checklist-full-20261026/round9-q6-cert-20261030/`; review its entry/dead-certificate closure before changing status. PIT waits `0A5B`/`0B01` need split proofs or a reviewed decision. PLAYER `00D5F-00DAF` remains matching work as `needs-route` until a route selects the grayscale arm. Round-10 evidence is under `D:/f117-gate/checklist-full-20261026/round10-q3-needsroute-20261030/`.
- **Remaining naming and closure work:** the all-code gap is 17,605 bytes, including the 1,505 route-executed bytes above; additional static-code naming and closure work remains. Reconcile `docs/static-code-review.md`'s 10,301-byte report with the current census before calling that review complete. The broad indirect-call/dispatch/callback/vector/overlay/self-modifying-code closure still needs a final review. The historical probable-dead tally (12 rows / 831 bytes) is stale: ten rows / 477 bytes were upgraded to dead in the 21-Oct-2026 review, while current reference rows show no probable-dead entries; reconcile the residual 354 bytes against current named coverage.
- **Phase 3:** use [docs/p3-handoff.md](docs/p3-handoff.md) for remaining features. Remastered rendering and hybrid sound are still 0%; the settings, controls, replay and accessibility items remain at the percentages in the roadmap.

## Resume here

Two threads. `master` is the active main branch. P2 batch `by` is accepted at 96.81%; P3 remains 85.42%.

**Phase 3 (merged into `master` on 31 October 2026, at `2adbdad`).** `master` now contains the in-app Remastered work (branch `p3-remaster-live`), hybrid sound (`p3-hybrid-sound`) and the docs pass; the branches stay as history. Read [docs/remastered-design.md](docs/remastered-design.md) and [docs/p3-handoff.md](docs/p3-handoff.md).
- **What is in:** `--presentation remastered` (`src/host/main.c` and a settings-menu row): the Enhanced picture with the 3-D view redrawn by `src/present/remaster_live.c` from the model requests, composited by origin tag; the machine-loop exec-hook table (`machine_t.exec_hook`, `src/machine/pc.c` and `machine.h`: one comparison per instruction when off); hybrid sound (`src/host/macsound.c`, `--mac-sounds DIR`, `--music-source`, `--sound-source`, `tests/test_macsound.c`).
- **Acceptance of the merge:** gate `D:/f117-gate/p3-merge-gate2-20261031/` (827 tests; 63 routes identical with 62 fresh interpreter sessions; 5,764,032 translated-instruction states under both timing profiles, 0 mismatching; 1,641 matched routines at three seeds, about 6.87 million states each, 0 mismatching; reachability and event limits clean) and the presentation checks beside it (`present-n1.log`: N=1 exact on 63 routes; `present-boot.log`: boot_to_flight N=4 and N=9 exact). The first gate attempt (`gate-attempt1-hung.log`) hung on a latent lockstep defect (below), not on the merge.
- **Score unchanged:** P3 85.42%; Remastered is still 0.0 in `docs/progress.json`. The owner asked whether to raise it; the advice was not yet (the design doc credits a stage only after its proof), or about 20% with evidence. The owner decides.
- **The headless tool I built is retired** (`remaster_view`, `f117run --remaster-shot`, `tools/remaster_wide_check.py`): code at git tag `remaster-view-tool-20261031`, findings in the design doc ("Findings from the headless shot tool"). Ported into `remaster_scene.c`: a fallback for edge sets that do not chain (angular order) and a three-vertex rule. Not in the live code: skipping requests made while the viewport (`DS:85FA..8600`) is under 300 wide (the cockpit's map display), and the frame boundary from the flip bit `DS:0346`.
- **Next, in order:** (1) run the merged in-app path in a real flight against the Enhanced baseline, never done (takeoff replay, `--presentation remastered --present-scale 3`, `F117_PRESENT_SHOTS`, `py tools/remaster_diff.py ENHANCED.ppm REMASTERED.ppm`); (2) the live branch's open list: face shade `[7D8A]`, validator mismatches (models 3, 30, 51, 50), interpolation between logic steps, widescreen, longer reach (R6); (3) the fit rule (cockpit and HUD scaled to fit uncropped, crosshair centred, 3-D view filling the window); (4) hybrid sound (the owner's stated order, 31 Oct: finish it right after the remastered renderer, before anything else in Phase 3; the `p3-hybrid-sound` branch is kept for this): a second driver (Roland music with AdLib effects, MT-32 effect channels muted), the START and END thunks (`0A95:6BB1`, `05FA:25F3`), a generated-code build check, the owner's marks on the sound-board page, the id mapping heard by ear; (5) the owner's credit decision.
- **Owner instructions to keep:** renderer shots go to `Desktop\ONShots` named `YYYYMMDD-HHMMSS-name.png` (leave the `Prenaming` folder alone); gradients smooth with no visible bands; depth-correct drawing is the point; read `src/matched/matched.c` before guessing and say what was read.
- **Build and run:** `.\build.cmd` from PowerShell; headless runs with `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`; a Python heredoc in Bash turns `\n` in C strings into real newlines, so use the Edit tool for such lines. Research switches on the live path: `F117_REMASTER_FRAME=N:FILE.ppm`, `F117_REMASTER_DEBUG=1`, `F117_REMASTER_VALIDATE`.

**Phase 2.** Batch `by` is fully accepted on `master`. It integrates ISOUND.117 `019C:026C` and `019C:07E9`, and the static SETUP `0000:0CB2` wrapper. Routine-body sizes are 336, 91 and 51 bytes; total accepted credit is 640 named bytes (373 newly route-executed, 267 fixture-only), with 175 fully covered instructions and 125/125 private mutants caught. Ghidra helped inspect the SETUP wrapper; its export is `D:/f117-gate/p2-static-ghidra-20261030/SETUP-0000-0CB2/`. The full acceptance packet is `D:/f117-gate/p2-batch-by-accept/`. Follow [docs/repeated-processes.md](docs/repeated-processes.md#phase-2-batch-acceptance) for the next candidate batch, and never run private proof builds during a full gate.

- **Normal batch target:** aim for 1,000-1,500 newly proved route bytes. Count only exact new route-executed credit; bundle independently proven small routines when they pass the same lockstep, mutation, audit and gate standards. Do not pad batches with raw census extents or already-covered code.
- **Accepted ISOUND.117 `019C:026C`:** 336 bytes, 111/111 instructions, five lockstep seeds and all 74 mutants passed. It passed whole-table and route prechecks and the full acceptance as part of `by`; credit audit attributes 276 bytes as newly route-executed, with the rest overlapping existing named credit. Evidence is under `D:/f117-gate/p2-batch-by-accept/`.
- **ASOUND.117 `0B01`:** the first private proof built and its coverage lock observed all 46/46 instructions (1,922 compared states), but every fixed seed failed with “fixtures reached too little”; the named run had 3,461 long-run skips and fixed seeds had zero comparisons. The fixture scripts only seven port-42 reads despite a randomized 1–6 record count and repeated PIT waits. No mutations ran. A one-record diagnostic copy was being built during handoff; its process was stopped before a completion result, so its build status is unverified and it has no lock/seed evidence. Preserve both the initial failure and diagnostic artifacts; retry only in a fresh copy with a terminating poll script and current libraries.
- **Other sound candidates:** ASOUND.117 `0A5B` (166 bytes) remains an unproved PIT wait; ASOUND.LOG `03F8-042F` earns zero new bytes because its instructions are already covered by accepted `0344`, `0423`, and `042C` scopes. Exclude ASOUND.LOG `008A`: its existing proof misses 12 mutants. The failed `0B01` evidence remains useful for a later fixture redesign; keep candidates separate until their own lockstep and mutation evidence passes.
- **Ghidra:** Ghidra 12.2 assisted the accepted static SETUP `0000:0CB2` wrapper. Its export agrees with the raw instruction boundaries and helper calls; the wrapper still passed byte, lockstep, mutation and acceptance checks. Keep using the installed setup and existing projects to screen static candidates, and treat decompilation as a lead rather than proof.
- **SETUP pilot:** `0000:0CB2` is accepted. SETUP `0CE5` and `0D1B` remain excluded because their mutation proofs failed. The unrelated local owner edits in `tools/sound_board_page.py`, `tools/sound_board_template.html`, and untracked `.ppm`, `100`, `4k60.bat`, `original-fixed.bat`, and `x.slice(0` files are intentionally excluded from the P2 commit.
## Unmerged branches and uncommitted worktrees (scan of 30 October 2026, 23:45)

Nothing below was changed or deleted; ask the owner before deleting anything. `p3-remaster-live` and `p3-hybrid-sound` were live Phase 3 work and are now merged into `master` (`p3-hybrid-sound` keeps one moot cleanup commit, `4e2f274`). The rest:
- **Old WIP branches, one commit each, not in `master`:** `present-horizon` and `present-hud2` (14 Oct, "stopped mid-task for a conversation handoff, unverified"), `wip-view-compose-2f9d` (12 Oct), `origin/p2-batch-setup-logo` (15 Oct, WIP SETUP `19B6`), `origin/p2-wt-batch` (10 Oct, a draft `vgame_camimage_background_raster`), `draw-rerun-probe` (4 commits, 15 Oct, an experiment). `docs-pass` has 3 commits dated 30 Oct that are not in `master` (docs text: "state what was ob...") worth a look before the doc pass.
- **Worktrees with uncommitted changes (the work exists only on disk):** `C:/Users/Tideg/f117-recomp-local/wt` (detached HEAD, **24 files, +1958/-103**, touching `CMakeLists.txt`, `config.c`, `headless.c`, `main.c`, `matched.c` and more: the largest unrecorded change, inspect first); `D:/f117-wt/credit` (4 files: `progress.py`, `progress.md`, tests, `func_lockstep.c`); `D:/f117-wt/gate-h` (`main.c`, `rerun.c`, +76); `D:/f117-wt/experimental-livebp-20261030` and the two `.claude/worktrees/agent-*` worktrees (a few lines each in `src/matched/matched.c`, probably experiments); also `D:/f117-wt/wake-bug` (detached, 6 files), `D:/f117-wt/mission-generation-4239` (5 files) and `D:/f117-wt/setup-main-0010` (3 files; both carry the private /Od overrides: never merge wholesale), `D:/f117-wt/thunk-candidate-check` (detached, 3 files) and `D:/f117-wt/harness-rules` (detached, 1 file). No detached-HEAD commit is stranded: every one is reachable from a branch. One registered worktree, `C:/Users/Tideg/My Drive/Development/F-117A Recomp/.claude/worktrees/agent-ada5a0330bc7d4569` (locked), no longer exists on disk.
- **Stashes:** five (`git stash list`), the first being `preserve owner docs before P3 acceptance merge`; not dropped.

## Orphan triage (31 October 2026): what each rescue snapshot held

Every worktree with uncommitted changes was pinned, without touching it, as a local `rescue/<name>-20261031` branch (not pushed:
some carry private overrides and the repository is public). Each was then measured (share of its added lines already on the tree) and
read. Result: nothing is lost by tossing any of them except the one P2 candidate below. Deleting a worktree or branch is the owner's call.

| Snapshot (worktree) | Already on the tree | What is left, and the verdict |
|---|---|---|
| `agent-a7d0...`, `agent-ace3...`, `experimental-livebp-20261030` | 100% | nothing: toss |
| `mission-generation-4239`, `setup-main-0010` | 99-100% | only the private `/Od /Ob0` override and fixture-iteration rows: toss (never merge wholesale) |
| `thunk-candidate-check` | 99% | the private ISOUND `026C` clamp-probe harness; the routine was accepted in `48869f4`: toss |
| `harness-rules` | 97% | the origin of `body_overrun` (the tree has the refined form): toss |
| `wake-bug` | 99% | an early draft of the stale-override entry-opcode check; the tree has it (`recomp.c` `in->img` check, `tests/test_stale_override.c`): toss |
| `credit` | 59% | the 16 Oct byte-credit design of `tools/progress.py`; the tree has the evolved form (`--named-cover`, `--route-cover`): toss |
| `gate-h` | 1% | private 4K stage-timing instrumentation (Windows-only calls, hidden 4K window, refuses to start without `F117_PROFILE_LOG`): toss; the supported route is `--present-log` plus `tools/presentation_timing.py`. Idea worth remembering: per-stage columns (sim, wait, worker, convert, publish, plan, prepare, upload, present) |
| `wt` (detached, base 10 Oct) | 89% | 35 of the 36 routines it added are on the tree. **Keep: `vgame_clip_divide_fault`, VGAME `0FB2:04DD`** (the clipper's INT 0 saturation handler; a one-line wrapper `vgame_raster_divide_fault(m, 0)` registered with level 3, plus lockstep rows `{ 0x0FB2, 0x04DD, 4, 0x046E }` and planted states that clear IF and TF and set `int_depth`; see the `matched.c` and `func_lockstep.c` hunks in `rescue/wt-20261031`). It exists nowhere else in git history. The tree has the sibling `084E`. `docs/progress.md` calls it "the line-clipper divide-error handler that no route reaches"; under the 29 Oct rule static code is matchable on lockstep alone, so it is a proof-and-gate candidate for the next P2 batch. The rest (model-seam repair `drawlist_finish_models`, a 17-field `'G'` snapshot record) is superseded presentation work from before the Enhanced consolidation (still in history: `5361129`, `571a490`). Its five untracked files (`nearclip.c/h` and three tests) are early drafts of what the tree has, copied to `C:/Users/Tideg/f117-recomp-local/rescue-20261031/wt-untracked/` |

Old WIP branches (not in `master`, nothing running on them): `present-horizon`, `present-hud2` (their band and HUD tests exist on the tree and pass), `wip-view-compose-2f9d` (`0x2F9D` is matched and hooked), `draw-rerun-probe` (the rerun path exists), `origin/p2-batch-setup-logo`, `origin/p2-wt-batch`. All superseded; safe to delete when the owner agrees. `docs-pass`, `p3-remaster-live` and (except one moot cleanup commit) `p3-hybrid-sound` are contained in the merge.

### Done: the `0FB2:04DD` job (31 October 2026; the owner asked that no useful work be lost)

`04DD` (the clipper's INT 0 saturation handler: `push bp; mov bp,sp; cmp word [bp+2],046Eh; mov ax,[228Dh]; je; mov ax,[228Bh];
xor dx,ax; mov ax,7F00h; jns; neg ax; add word [bp+2],4; sub dx,dx; pop bp; iret`, 33 bytes, 14 instructions) has the same shape as the
accepted `084E`. It is now registered as `vgame_clip_divide_fault` (`vgame_raster_divide_fault(m, 0)`, level 3) with the
`INTERRUPT_FRAMES` row `{ 0x0FB2, 0x04DD, 4, 0x046E }`; no new body was written, the shared body already existed. Evidence:
`func_lockstep --only VGAME.EXE:4DD` at six seeds, 3,992-4,000 states each, 0 mismatching (`084E` runs the same size); four mutants
of the `spans = 0` arm (compare constant, denominator, IP advance, clock charge) each give 1 mismatching; full gate on the tree
(827 tests, 63 routes identical, 1642 routines at three seeds, 0 mismatching, steps 8 and 9 clean). Landed from branch
`p2-clip-04dd` (`dea8936`). The percentage was not refreshed: run `tools/progress.py --census` with the next batch to credit the 33 bytes.
The old snapshot `rescue/wt-20261031` is now redundant (its body used a replaced trap-flag line) and is also in
`D:/f117-archive/orphans-20261031.bundle`.

### Fixed: the lockstep hang at `START.EXE:8A32`, state 1627 (31 October 2026)

The first full gate of the merge (commit `863898e`, seed `0x863898e40efa`) never finished: matched-routine lockstep shard 7/8 sat
on one `func_lockstep.exe` for over two hours. Reproduction was `func_lockstep.exe --states 1628 --seed 0x863898e40efa --only
START.EXE:8A32` (`1627` finished, `1628` hung). It was not caused by the merge: the `master` build (`48869f4`) hung the same way.
**Root cause** (an earlier note here blamed `st2_fill_rect`'s 64K-row run; that was wrong): the planted state made the original code
slide along a chain of instruction-prefix bytes, and `cpu_step`'s prefix loop (`src/cpu/cpu.c`) has no bound, so a single step never
returned and the per-state step limit never fired. **Fix** (commit `7967b67`, `tests/func_lockstep.c` only; the core was left alone,
it would need the full gate for no gain): `prefix_runaway()` stops a state whose bytes at CS:IP are more than 64 consecutive prefixes
(no instruction can complete), counted and reported as `prefix-run guard: N state(s) stopped as long runs`; plus an opt-in watchdog,
`F117_LOCKSTEP_WATCHDOG=N` (seconds), that prints the stuck state and exits 3. Evidence: the full table at the hanging seed finished in
15 minutes (1641 routines, 6,873,385 states, 0 mismatching, guard stopped a handful of states per shard); the full gate on the fix
tree passed (827 tests, 63 routes identical, 1641 routines at three seeds, 0 mismatching).

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
