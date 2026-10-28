# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/progress.md](docs/progress.md) (what the scores measure),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs). **Current state, 27 October 2026 (evening): the call-budget harness repair is applied and two under-claiming bodies (START `8CA8`, `2F4C`) are fixed (item 0); eleven batch-bn proofs are ready for checkpoints but not integrated (item 0b); checklist round 4 is committed (90.54%). Earlier today: batch bm accepted by the gate (ten routines); with checklist round 4, P2 93.94%, All 92.17%; checklist 41,749 / 46,112 bytes (90.54%) after round 4 (the 90.20% to 87.84% fall was only the scope growing by 1,206 bytes; no entry was removed). Commit: see `git log`.**
The working copy is **`C:\Dev\F-117A Recomp`**; the old `My Drive` copy is stale: do not use it.
The accepted runtime is batch bm (`D:/f117-gate/p2-batch-bm-accept/`, the commit that adds it: see `git log`); batch bl was `9e06249`.
There are 34 normal-play switches. Kit libraries (`D:/f117-gate/kit-libs`, manifest) were built at `28b34b2`;
the only `src/` changes since are matched bodies, which each private proof replaces itself, so the libraries remain valid.
This machine has no `gh`; check the pushed commit's CI through the GitHub Actions API.

### Resume state

0. **Call-budget harness repair applied; two accepted bodies were under-claiming (27 Oct; gate `bm2` PASSED: 1,854 s, 731 tests, 63 routes / 62 fresh sessions, 1,487 routines, three whole-table seeds 0 mismatching; `D:/f117-gate/p2-batch-bm2-accept/final-gate-verdict.json`; the accepted runtime is now this commit, see `git log`).** The root-reviewed patch
   (`D:/f117-gate/bm-root-review/pending-call-budget.patch`: `g_callend` / `body_overrun`, which exempts only the exact clock an original-code call returned, never later C instructions; plus the F/G self-test cases) is now in `tests/func_lockstep.c`;
   `--check-io-rules` passes with it. Under it the whole table showed two defects in code the old gate had accepted, both OUR matched bodies (not the original game), both fixed in `src/matched/matched.c`:
   START `8CA8` `start_install_clock_hook` made each INT 21h (`8CC7`, `8CDD`) without first claiming room (`SM4_NEED(1, ...)` added before each `HINT`); START `2F4C` `start_route_leg` claimed `NEED(33, 0x2F6A)` but the path
   through `2F7B` when |dx| is 0 and |dy| is not runs the extra `or ax,ax; jne` and is 35 instructions (now `NEED(35, ...)`; found by logging each claim's clock, the instruction counts of every other block checked out).
   Two whole-table seeds (`0x5EED0C01`, `0x20261027`; 1,487 routines, about 5.88 M states each) show 0 mismatching and no OVERRUN (before the fix: 1 mismatching, `2F4C`); `8CA8` and `2F4C` alone are equal.
   Evidence `D:/f117-gate/bn-budget/`. **Lesson:** the old exemption (any body that had made a call could overrun) hid under-claims after calls; a body's per-block claim must cover its longest path, including rare early paths. If a future seed or gate shows another OVERRUN, fix the claim and re-prove that routine.
0b. **Batch bn proofs: eleven agents ran 27 Oct (nothing integrated, credited or gated; checkpoints and verdicts not run).** Root re-read each result file (mutants caught, five seeds, body and fixture older than the mutation results, no debug printf):
   ASOUND.117 `0430` 49/49 (no harness hunk); VGAME `0971` 28/28 (hand-apply INTERRUPT_FRAMES `{ 0x120A, 0x0971, 3, 0x0A3E }`); DSWAP `0FF8` and `2060` 22/22 each; SETUP `1C46` 22/22; START `2846` 20/20 (7 FATAL_JUMPS rows);
   PLAYER `02853` 24/24 (POPF_STOPS `{ "PLAYER.EXE", 0x026B, 0x01A3, 0x01B7 }`; one extra mutant `ss-no-shadow` missed and judged equivalent by the agent, needs a second look); ASOUND.117 `025A` prefix 26/26 (four hand-merged harness hunks that change `run_to_ret`: read the diff against its `tests-base.c.pre-boundary-hunk`; lockstep stubs `036B`, keep the route pre-check);
   END `36BC` 23/23 (7 FATAL_JUMPS rows); PLAYER `222A` 22/22 and PLAYER `1255` 22/22 (`1255` needs a hand-carried `run_to_ret` rule, and probably runs only on `err_player_assert`/`err_player_divide`: pass `--route` to `checkpoints`); RSOUND.117 `0F98` 30/30 (the first-block `room(c,1)` claim is not covered by any mutant: ask for a fixture that leaves exactly one instruction of room, do not accept the swapped-in mutant).
   **Not for this batch:** RSOUND.LOG `08BE` partial (39/40: `guard-ip-C8D-store` missed, guard paths unexercised; retry with a stronger fixture or exclude that guard, never accept a missed mutant) and START `8CA8` (already matched; its proof directory `p2-start-bn-8CA8-cb` only confirmed the fix above, nothing to integrate).
   Every private `tests-base.c` predates batch bm: rebase each hunk onto HEAD's `tests/func_lockstep.c`. Then the recipe in item 2 (`finish_all.py`, `integrate_many.py`, hand-applied rows, build, new CTests, pre-checks, `new_batch.py bn bm2 <commit>`, gate).
0c. **Checklist round 4 done (27 Oct, commit `bbf2d0a`): 41,749 / 46,112 bytes (90.54%), P2 93.94%.** See item 4 for round 5.
1. **Batch bm accepted by the gate (27 Oct; gate 1,797 s, 731 tests, 63 routes identical).** Ten routines: RSOUND.117 `1225`, ASOUND.117 `1BCC` `1BAF` `1360`, ISOUND.117 `0023` `07CF`, END `57B0` (the 92-byte dispatch arm, its 11 `FATAL_JUMPS` rows added), ISOUND.LOG `00B8` (dispatch logic only; its arms are stubbed), ASOUND.LOG `0804` and `0D1A` (`0D1A` partial: `107C` unclaimed). +3,018 named bytes, 1,036 fixture instructions, 313 mutations caught. Six bytes are claimed but uncredited: the two exits from END `57B0` into other arms (`580C`, `583A`). Pre-checks: `route_precheck` identical for every entry, and two whole-table seeds (the second run with `1360` included: 1,487 matched routines, 0 mismatching). Evidence `D:/f117-gate/p2-batch-bm-accept/`. Next, held for owner decisions: ASOUND.117 `08F94` (its bytes sit in an unreviewed gap in the C start-up coverage: `8F94`..`8FC2`, the divide-error handler the C start-up installs; needs a census decision before integration; its `PROGRAM_ENDS` row `{ "START.EXE", 0, 0x8F94, 0xF5F2 }` must be added by hand) and RSOUND.LOG `08BE` partial (guard branches untested; a second reader should review them first).
1b. **Batch bl accepted and pushed (`9e06249`).** 33 routines, +892 named bytes, 334 fixture instructions, 900 mutations caught, 165 optimized seeds; gate 2,078 s, 721 tests,
   63 identical routes / 62 fresh interpreter sessions. Pre-checks: whole-table seeds `0xa1bc7fe770f2` (the gate's) and `0x5EED0B12` (5.83 M states each, zero mismatches) and
   `route_precheck.py` on all 33 entries (49 reached routes, all identical with matched routines disabled, about 10 minutes at `--jobs 6`). Evidence `D:/f117-gate/p2-batch-bl-accept/`,
   `D:/f117-gate/bl-prechecks/`. CI for `9e06249`: green (Windows build and ROM-free tests). Contents: Roland chains, ISOUND.117 `0079` `0045`, ASOUND.117 `0BD4` `0C08` `0B76` `197B` `018F` `0210` and 17 song/voice-start wrappers.
   **Blocked: ASOUND.117 `025A`** (its RET is behind a loop polling six voice bytes that only the timer interrupt clears, so no lockstep state reaches it: prove the prefix `025A..02C9`
   with a boundary at `02CA`, or declare it; `D:/f117-gate/p2-asound-117-bl-025A/BLOCKED.txt`). Audit flags used: six bodies full (`0390 0CF5 0C2D 03FA 0C08 0B76`), 27 `:partial`
   (the static audit cannot read per-port clock charging, calls without `SM4_NEED`, or a first claim written as an expression; same precedent as batch bk), `0E70` prefix `XB`, `0079` and `018F` `NOBLOCKS`.
   Harness hunks applied by hand because `integrate_many.py` carries only FATAL_JUMPS/SHARED_ENDINGS rows: 0E70's outer-RET recognition (the `0E4C` line widened), the INT 1A function 2 answer plus
   `g_native_int1a` rows for ISOUND.117 `0045` and ASOUND.117 `0BD4`, and a `g_cov_ip = 0x0838` row for ASOUND.117 `089C`. They are in `tests/func_lockstep.c` now.
2. **Earlier batch-bm stop (27 Oct, before the gate; its proof notes still apply, but item 1 supersedes its status lines).** Three proof agents resumed part of the earlier twelve-agent queue; a fourth agent reviewed the harness. All are stopped and no proof/build child processes remain. Existing `p2-*-bl-*` directories feed batch bm; the new repaired directories use `bm`. **Nothing below is integrated or credited.** Read each directory's `NOTES.md`; interrupted runs require a complete rerun, not a tally of surviving logs.
   - **(Superseded 27 Oct: the patch below is applied and reviewed, see item 0; kept for its detail.) Validate the saved harness repair.** PLAYER `0540` exposed a real gap: both event-limit checks exempted the whole body once it had made any call, so a tail could overrun and still finish in the correct state. The proposed `g_callend` / `body_overrun` check permits only the exact clock returned by original code (including an atomic service crossing the limit), then rejects subsequent charged C instructions. Full patch: `D:/f117-gate/bm-root-review/pending-call-budget.patch` (SHA256 `8ba8afa8221c1fabc1c440752961800848cf3328522231affa22a7783cd844b1`); full source snapshot `pending-func_lockstep.c`. **The tracked harness was restored to HEAD for this handoff-only commit.** Three initial synthetic call/tail cases passed, as did PLAYER's five seeds and all 22 mutants under the new check. Independent review found no blocking issue; it added two atomic-service control cases, but their main build was stopped before validation. Apply the patch, rebuild, run `--check-io-rules`, then whole-table seeds before accepting it. Preserve private proof inputs; changing their `tests-base.c` invalidates their evidence.
   - **RSOUND.117 `0BAD` (`p2-rsound-117-bl-0BAD`), root verified through finish:** five fresh seeds, 37/37 instructions, 43/43 actual mutant mismatches, 26 identical `roland_boot` checkpoints and final hash, 8,288 calls; IBM boot unchanged. `private-verdict.json` exists; root witness `D:/f117-gate/bm-root-review/rsound-117-0BAD.json`. Ready for integration and the usual prechecks/gate, not credited yet. The route reaches only 25/37 instructions; callees `0B9C`/`050A` are stubbed, so keep the route precheck. No harness hunk.
   - **ASOUND.117 `0B98` (`p2-asound-117-bl-0B98`), steps 1-7 complete:** 14/14 instructions, five fresh seeds (6,098-6,121 comparisons), 25/25 meaningful mutants caught. Equivalent ignored-result CMP-to-SUB and zeroing XOR-to-SUB edits were replaced; failed evidence preserved. Root started `finish_all.py ...:full_cycle`, then stopped its entire tree during the build: no checkpoint, boot or private-verdict result. Rerun finish from the start. `resume-audit.json` and `D:/f117-gate/p2-driver-bm-audit.py` support mechanical validation; no harness hunk.
   - **PLAYER `0540`: use `p2-player-bm-0540-budget`, not the older `bl` proof.** Unchanged body/fixture, 15/15 instructions, five fresh seeds (6,170 / 6,165 / 6,152 / 6,180 / 6,166), all 22 mutants caught, including the CALL and tail budget underclaims. Root read the actual overrun lines. Its private harness contains the new call-end check and the initial three synthetic cases; carry the complete pending main patch above. Checkpoints (`err_driver_overlay`), IBM boot, verdict and integration remain. The old `p2-player-bl-0540` has a 21/22 result: its tail underclaim is a genuine missed defect. Initial room-increase mutants were equivalent early handbacks, preserved in `attempt-1-overclaim-mutants/`.
   - **RSOUND.117 `1225` (`p2-rsound-117-bl-1225`):** five seeds pass, 18/18 instructions. One equivalent ignored-result CMP-to-SUB mutant replaced. Corrected run stopped after 7/26 fresh catches; the other logs include the earlier attempt. Stale results renamed `mutation-results.pre-correction.json`; no current result file. Rerun all 26, then `finish_all :roland_boot`.
   - **ASOUND.117 `1BCC` / `1BAF`, ISOUND.117 `0023` / `07CF`:** all now have five fresh passing seeds, full coverage and zero skips/mismatches. Mutation definitions audited (22 / 26 / 21 / 20 respectively), but none run. Several equivalent edits replaced before launch; see notes. Callees stubbed; `0023`/`07CF` run only on `err_vgame_no_photo`; `1BAF` reaches only 5/12 instructions on real routes.
   - **ASOUND.LOG `0D1A`: use `p2-asound-log-bm-0D1A-partial`.** Full independent jump-table dossier is 348 instructions; the named body implements 347 and explicitly hands back at `107C` without claiming its unreachable range-check jump. No dead-byte exclusion or denominator reduction. Fresh build, lock and five seeds pass (6,190 / 6,183 / 6,181 / 6,198 / 6,182); full 37-mutation list restored, 14 caught before stop, no complete results. Rerun all 37. `SOURCE-REVIEW.md` / `input-sha256.json` record hashes and limits; old `p2-asound-log-bl-0D1A` untouched as evidence. Self-write guard paths remain untested because the harness skips original code-writing states. `116E` is already accepted in bk.
   - **RSOUND.LOG `08BE`: do not integrate the old `p2-rsound-log-bl-08BE` despite its private verdict.** Root finished it (26 route checkpoints, 2,676 calls, IBM boot unchanged), but review found missing after-callee code snapshots: real callees can write aliased globals/slots onto the body. Also the record may alias and overwrite its own stack argument. New `p2-rsound-log-bm-08BE-partial` adds snapshots after all 13 calls, rechecks the record, and guards code/argument overlap for records, fixed cells, F2 and chord/slot stores. **Never built or tested.** Full dossier 626 instructions; intended named 625, explicit unclaimed handback at `0DAC`, no dead exclusion. Read source review/notes before building; no guard-branch coverage is claimed.
   - **END `57B0` (`p2-end-bl-57B0`):** root confirmed the 92-byte dispatch arm / 41 instructions as the unit, matching the accepted START `9E3E` arm (the larger 795-byte function overlaps installed bodies). Five seeds pass; 19 mutant logs survive, no result file: rerun all 24. Needs 11 FATAL_JUMPS rows for targets `580C 583A 584D 58B0 58B3 5948 594C 5952 5958 5979 59F7` (the integration helper's FATAL_JUMPS merge should carry these; inspect it).
   - **VGAME `137CE` (`p2-vgame-bl-137CE`):** seeds pass, 3/3 instructions; actual mutation list has 21 edits, none run. Observer integration remains manual (`observe_fill_rows`, declaration, body without private stub, deliberate override row). Static review found `hook_fill_rows` allocates 515 fields but can append 517 for 256 rows; do not integrate until resolved and the real observer path is checked. The private observer is a no-op. An attempted boundary probe produced no executable and exercised nothing. **Automatic safety review rejected that probe and the large-log focused guard probes for possible cybersecurity risk; they remain stopped and were never compiled/run.** No review bypass or dynamic validation is claimed.
   - **START `11DB` is already fully matched:** root confirmed `start_map_hover_light`, SPANS length 301, COVERED 127 and the batch-x roadmap entry; `1285`/`12DB` are its interior arms. The duplicate `p2-start-bl-11DB` proof must not be integrated; private DECLINED rows remain stale.
   - **Written, never built:** ISOUND.LOG `00B8`, `0675`, `0468` (bounded prefixes with hand-backs), ISOUND.117 `07E9`, ASOUND.117 `0430` (105 instructions, 49 mutants), ASOUND.LOG `0804`, MPS_LOGO `1ADC` (the C run-time `strstr`, 96 bytes), PLAYER `02853` (built, nothing run; POPF_STOPS row `{ "PLAYER.EXE", 0x026B, 0x01A3, 0x01B7 }`) and `028C1` (covers `0211..0230` only; POPF_STOPS row `{ ..., 0x0211, 0x0231 }`; whether the harness accepts its hand-backs is unverified), START `08F94` (the true routine is `8F94..8FC2`, 47 bytes, 15 instructions; PROGRAM_ENDS row `{ "START.EXE", 0, 0x8F94, 0xF5F2 }`; no census row, it sits inside the `8EDC` row, so credit is a decision), and the leftovers list below.
   - **ASOUND.117 `1360` group (`p2-asound-117-bl-1360`):** one routine of 408 instructions (all 14 entries, minus the dead jump at `172E`); body and final fixture written, but the final fixture was not rebuilt or run; the `seeds.out` and `seed-*.log` files there are stale (do not use them); 82 mutants written, none run.
   - **Partial or failing:** RSOUND.117 `0F98` (10 instructions; lockstep fine but seeds fail with 266 long-run skips because the original continuing at `0E95` runs the real `0E4C`/`0E70` MPU waits; the last fixture edit, stubs for those two at fixed addresses, was never built); START `8427` (built, lockstep skipped all 5,000 states as unfinished; the agent added a synthetic-tick hunk (`tick_inject`) that would have to be carried into `tests/func_lockstep.c`, and the spin would still run as original code: an open decision) and END `4393` (never built); MPS_LOGO `0307`/`030E`/`0315` (far-jump thunks; `0307` passed lock and seeds, `030E` lock only, `0315` unbuilt; their census rows have size 0, so they may earn no named bytes: check first; two harness hunks, `program_end_reached` and `run_to_ret`, in each `tests-base.c`).
   - **Blocked:** ASOUND.117 `025A` (loop waits on bytes only the timer clears; prove the prefix `025A..02C9` with a boundary at `02CA`, or declare it), SETUP `10B3` (STI on every pass of its retrace loop; the agent's untried idea is a prefix with boundary `10D3` plus a separate interior-entry proof of `10D3..10E3`), PLAYER `0211` (on `DECLINED.tsv` for STI in its loop; `028C1` above is the stop-after-STI attempt).
   - **ASOUND.LOG `00FE` (`p2-asound-log-bl-00FE`): nothing built.** The agent's reading: the matched `00DA` stops after its STI at `00FA` (stop_at is 0), and the withdrawn body then runs more C after the nested call using that changed `stop_at`. Suggestion (unverified): never run C after a nested callee that executes STI; split `00FE` into stub-free entries `00FE..0106`, the call at `0107`, `010A`, `0110..013F` that hand back to the original, or keep it original. The one test that could confirm it (the `setup_calibration` route, recomp against `F117R_NO_MATCHED=1`, with the withdrawn proof's private `f117run.exe`) was denied to the agent by the permission classifier and has not been run.
   - **Leftovers written, never built** (`D:/f117-gate/p2-leftovers-bl-LEADS.md`, census script `p2-leftovers-bl-work/unmatched.py`): VGAME `0971` (renderer INT 0 handler, kind 3, 27 bytes, INTERRUPT_FRAMES row `{ 0x120A, 0x0971, 3, 0x0A3E }`), SETUP `0094` (joystick poll tail after STI, LIVE_FRAMES row `{ "SETUP.EXE", 0x0237, 0x0094, 0x000E }`, `setup_calibration` route only; the entry is in an interrupt shadow, a decision for root), VGAME `D569` (the fixture stubs `11B8:00C4`, which is itself matched: check the stub runs), END `36BC` (7 FATAL_JUMPS rows), START `2846` (7 FATAL_JUMPS rows), PLAYER `1255`, and four 3-byte near JMP thunks (DSWAP `0FF8`, DSWAP `2060`, PLAYER `222A`, SETUP `1C46`). Not written: 127 five-byte run-time-patched far-jump thunks (table `p2-leftovers-bl-work/thunk_entries.tsv`): prove one generic thunk body registered for many entries rather than 127 proofs.
   - **Builds were the bottleneck.** The kit's three build slots were all held by other agents, so one agent waited about 40 minutes for a slot and never compiled, and each build took about 10-20 minutes under load. With 12 agents the throttle (RAM, 6 GB per `cl.exe`) became a queue. Options: measure a `--func-only` build's RAM and raise `BUILD_SLOTS` (4 would use about 24 of 31 GB: untested), stagger the agents, or fewer agents on build-heavy units.
   - **Harness diffs:** `D:/f117-gate/head_lock.c` is stale (about 20,000 diff lines): diff each `tests-base.c` against `git show HEAD:tests/func_lockstep.c`. Use `py`, not `python3` (absent). Lockstep now loads code at base 0x800 (cs=0x0A3E), so fixtures must use `cs`, not the encoded `0x023E`.
   - **Recipe per finished proof (what batch bl followed):** read `seeds.json`, `mutation-results.json` and the seed-log tallies yourself (0 skipped, 0 mismatching, every mutant caught, `body.c` and `fixture.inc` older than the mutation results),
     then `py D:/f117-gate/accept-kit/finish_all.py DIR:ROUTE ...` (three directories per invocation; it uses CPUs 20-31), read each `finish.log` for `calls_of_this_routine` above 0, `integrate_many.py DIR ...`, apply the harness
     hunks by hand, rebuild with `.\build.cmd OFF` from PowerShell (`cmd` cannot find a bare `build.cmd`; redirect inside `cmd /c`, since PowerShell's
     `*>` writes UTF-16), run only the new CTests, `route_precheck.py` on every new entry plus two whole-table seeds (HEAD's `rotating_seed()` first), `new_batch.py bm bl 9e06249 DIR ...`, copy `audit_private.py` and `finalize.py`
     forward from `p2-batch-bl-accept`, run `audit_private.py` and set `partial`/`prefix` in `batch.json` per item 1, then `affinity_launch.py py p2-batch-bm-accept/accept.py`, the `tools/progress.py` refresh
     (`--census D:/f117-gate/census.tsv --named-cover D:/f117-gate/p2-batch-bm-accept/named-cover --route-cover D:/f117-gate/route-cover-20261020b --data D:/GOG/F-117A`), docs (README, roadmap table, this file), a commit titled by `py tools/progress.py --title`, push, CI.
3. **Run wide: how to use many agents (owner, 27 Oct: there is no agent limit to speak of).** Measured on 27 Oct: a proof is about 10-15 minutes of compute inside about two hours of agent time (seeds 2-6 s, 40-60 mutants 2-3 min), so wall time follows the number of agents
   working, not CPU. Agent reasoning does not use this PC; only tool commands do, and one `cl.exe` holds about 6 GB of the 31 GB RAM (0.4 GB was free with three compiles plus a gate). **`D:/f117-gate/kit/kit.py` now
   throttles builds to 3 machine-wide** with file locks (`BUILD_SLOTS`, `kit/buildslots/`, backup `kit.py.bak-20261027`, tested: a fourth caller waits); with it, 12 agents ran together. Keep every slot full: when an agent reports, give it the next unit
   (or `SendMessage` it). During a gate agents use the 8 spare threads (mask `0xFF000000`) and keep drafting; the gate's own verdicts are unaffected. Start the long critical-path units first (`1360`, `0D1A`, `08BE` groups, then the declined
   routines of the seven programs); one big routine is one chain that more agents cannot shorten. Verification stays with root and is the part that does not scale: use sub-agents for mechanical reads, never for the decision.
   Batches of about 30-50 routines are best (a gate costs about the same at any size); prove batch N+1 while batch N is in its gate. Never weaken a check to go faster.
   Use the actual session limit: this Codex session allowed three concurrent subagents plus root, so three proof agents and later a separate reviewer were used. Keep the machine-wide three-compiler throttle. Measured in this continuation: first private harness build about 180-196 s; unchanged-harness mutant builds about 1.1-1.6 s, not three minutes per mutant (`project/build/.ninja_log`).
4. **Checklist round 4 done (27 Oct); round 5 is next and needs no gate:** 41,749 / 46,112 bytes (90.54%), 4,363 bytes without entries. Round 4: 143 packets / 29 groups, 143 reads (99 ACCEPT, 33 ACCEPT-WITH-CHANGES, 11 NEEDS-CONTEXT); 32 units / 1,245 bytes / 37 rows adopted by `round4/stage_r4.py` (same rule as round 3: ACCEPT, status equals the structural proposal, exact range, no name correction) and `round4/validate_stage_r4.py` (checklist 40,504 to 41,749, gameplay unchanged). Round 5 packets: the 36 status-differs units (readers judged e.g. unreached-held where the proposal said needs-route: a fresh reader must confirm the status), 25 range-wording, 6 missing-field, 33 with-changes (use the reader's corrected name or range, give it to a DIFFERENT reader) and 11 NEEDS-CONTEXT (add the context the reader named, e.g. the stream helper at START `0x9992`). Work: `D:/f117-gate/checklist-full-20261026/round4/` (build_r4.py, validate_r4.py, units.tsv, reads/, staged-round4.report.tsv). The proposed names are structural placeholders (`..._checklist_fragments`), as in round 3.
5. **ASOUND.LOG `00FE` is still withdrawn** (its lockstep stubs the PIT sampler `00DA`; the real `setup_calibration` route takes an IRQ at a different boundary after the nested call). One of the batch-bm agents has the re-proof; otherwise keep it original.
   Debug with recomp versus `F117R_NO_MATCHED=1`, `F117R_MATCHED_LIMIT` bisect and `--dump 0:10` at budgets; `--trace` forces the interpreter.
6. **Process notes:** run only new CTests before the full gate; keep the commit-derived seed and one independent whole-table seed; `route_precheck.py` on every new entry (not only the stubbed ones: about 10 minutes for 33 entries).
   Pending: `--overlap-jobs` timing, `career_promotion` profiling (805 s, the gate's floor); second PC deferred. Check whitespace before starting proofs: formatting changes invalidate the kit's source hashes.
   The owner's untracked `4k60.bat` and `original-fixed.bat` stay untouched (never stage them; stage files by name).

This file holds only the current state, standing practice, open decisions and
traps, not a session log: earlier detail is in `git log -p handoff.md`, and
per-batch evidence is in the `D:/f117-gate/` directory named in each roadmap
entry. Consolidate it instead of appending to it.

## Current state

- **Scoreboard:** P1 100.00%, P2 93.94%, P3 76.34%, All 92.17%
  (`py tools/progress.py --title`; P4 and the decided-against items are not
  scored). P3 is below 100% only because of the re-release features (roadmap,
  weight 31): the first part of the two settings is built but none is credited
  yet; the presentation stages are complete.
- **Phase 2 figures** (`docs/progress.json` after batch bl, with checklist round 3b; `py tools/progress.py` prints them;
  what they mean: [docs/progress.md](docs/progress.md)):
  - 1,487 addresses matched; batches a to bm are accepted (table in [docs/roadmap.md](docs/roadmap.md);
    evidence `D:/f117-gate/p2-batch-<letter>-accept/`).
  - Gameplay score (scores the matched item): **(154,617 matched + 2,631 reviewed design-rule original) /
    163,907 route-executed bytes = 95.94%**. 26,576 matched bytes run only in fixtures and earn no gameplay credit.
  - **6,659 executed bytes are unmatched** (20 design-rule original bytes awaiting a reviewed entry).
    Sound drivers 4,869: ASOUND.117 2,379, RSOUND.117 289, RSOUND.LOG 728, ASOUND.LOG 649, ISOUND.117 483, ISOUND.LOG 341.
    Seven programs 1,790: VGAME 549, PLAYER 273, MPS_LOGO 270, END 261, START 251, SETUP 118, DSWAP 68.
    F117.COM, MGRAPHIC and MISC: none.
  - Naming item (previous all-code measure): **181,258 / 206,232** creditable bytes; 24,974 uncredited,
    including 13,809 reviewed static code bytes no route runs.
  - Accounted-for checklist: **41,749 / 46,112 bytes (90.54%)**; `docs/reference-reviewed.tsv` has 1,636 second-read entries
    (evidence for the first 41: `D:/f117-gate/reference-reviewed-20261021/`; the 10 trial entries: `D:/f117-gate/unmatched-reference-20261026/trial-reads/`;
    the 523 round-2 rows: `D:/f117-gate/checklist-full-20261026/round2/`; the round 3 and 3b rows: `round3/`, `round3b/`; the 792 round-1 rows from the full blind pass: `D:/f117-gate/checklist-full-20261026/` (reads/, staged-round1.tsv, reconcile-result.json)). Every `stays-original` entry
    must name its design rule.
  - Still in the denominators: about 2,500 machine-option-only error-path bytes and 831 probable-dead bytes.
    Outside every score: about 495 held static-discovery bytes and the floating LZEXE decompressor.
- **Batch bl acceptance (27 Oct):** `D:/f117-gate/p2-batch-bl-accept/final-gate-verdict.json` passed in 2,078 s: 721 tests, 63 routes / 62 fresh interpreter sessions,
  1,477 routines, 900 private mutations, 165 optimized seeds, +892 named bytes, 334 new fully covered instructions, 0 claimed-but-uncredited bytes. Two whole-table pre-check seeds
  and 49 paired real-route pre-checks passed (resume state 1).
- **Batch bk acceptance (27 Oct):** `D:/f117-gate/p2-batch-bk-accept/final-gate-verdict.json` passed in 1,659 s:
  688 tests, 63 routes / 62 fresh interpreter sessions, 1,444 routines, 228 private mutations,
  40 optimized seed checks, +418 named bytes. Three whole-table pre-check seeds and 53 paired real-route pre-checks passed.
- **Batch bh acceptance (26 Oct):** `D:/f117-gate/p2-batch-bh-accept/final-gate-verdict.json` passed in 1,723 s: 674 tests, 63 routes / 62 fresh interpreter sessions,
  1,430 matched routines, 188 private mutations, 25 optimized seeds, +1,410 named bytes (all route-executed), 614 new fully covered instructions. The five
  bg4 routines: ASOUND.117 `1048` `123E`, ASOUND.LOG `0BFC`, ISOUND.117 `062E` `0443`. Three pre-check seeds (`0x5295f6b75bad`, `0x5EED0BF3`,
  `0x20261027`; about 5.59 M states each, zero mismatches; `D:/f117-gate/p2-batch-bh-prechecks/`). Harness: `g_cov_ip` lines for `062E`/`0443`. `0BFC` has
  guard arms (decline/hand back if its own code is overwritten) that the harness cannot test: lockstep skips self-writing states. **Fixture hazard found:**
  `XW8` declares its own local `a_`; passing a caller's `a_` captured it (value derived from the address, not random). Fixed in 123E (its own bug) and in the
  accepted VGAME `4973` and ASOUND.117 `0C2E` fixtures (renamed; both re-proved, no body defect). No other `XW8`/`XW16` call in the repository passes `a_`.
- **Previous acceptance (batch bg, 26 Oct):** `final-gate-verdict.json`: 669 tests, 63 routes / 62 fresh interpreter sessions, 1,425 matched routines,
  427 private mutations, 125 optimized seeds, +473 named bytes (all route-executed), 255 new fully covered instructions. Two whole-table
  pre-check seeds (`0xaebbf86cfffe`, `0x5EED0BF2`; 5.57 M states each, zero mismatches; logs `D:/f117-gate/p2-batch-bg-prechecks/`). Audit: every entry
  is `:partial` and `NOBLOCKS`/`XB` per body (the stock audit rejects hand-back and hand-claimed blocks). Merged harness hunks: probe-tail and
  bit-7 status forcing, port-keyed `g_io_force[2]` and `g_cap_hint`, three LIVE_FRAMES and three POPF_STOPS rows. The six MPU/OPL wait-loop
  fixtures force timeout/success and LOOP counts 0/1/FFFF by state index. Thin real-route reach: `0C43`, `02D4`, `0B74`, `0B75` run once.
- **Previous acceptance (batch bf, 25 Oct):** `D:/f117-gate/p2-batch-bf-accept/final-gate-verdict.json` passed in 1,642 s: 644 CTests,
  63 routes / 62 fresh interpreter sessions, 1,400 matched routines at three seeds, +61 named bytes (all route-executed),
  46 mutations, 20 optimized seeds; two whole-table pre-check seeds (`0x4fc6ba6c9f0d`, `0x5EED0BF1`) zero mismatches; presentation overlap
  passed; it froze a new keyed baseline (`--no-baseline`). The 0251 prefix stops at 027C after the PIT control write; 027C stops after the
  reload high byte (the wake) at 0289; 0289 stops after STI at 028D (POPF_STOPS); 028D is the RET. 027C and 0289 are LIVE_FRAMES
  entries (the saved AX sits under the return address). `new_batch.py` took `NOBLOCKS` for bodies without XB.
- **Previous combined acceptance (batch be, 25 Oct):**
  `D:/f117-gate/p2-batch-be-accept/combined-final-verdict.json` passed.
  Both whole-table pre-check seeds (4,000 states, eight shards each):
  `0x03c761a5484c` 5,431,624 compared / `0x5EED0BE1` 5,432,816, zero mismatches,
  494 s together. Full gate: 640 CTests, 63 identical routes / 62 fresh
  interpreter sessions; 1,396 matched routines at three seeds; reachability
  and event limits clean. N=1 260,613 exact frames / 260,527 pairs; boot N=4/9
  736 frames / 732 pairs. Core 1,338 s; batch acceptance 1,802 s; combined
  acceptance with all focused/default checks repeated afterward 1,948 s.
- **Earlier matched batch, be:** eight ASOUND.117 jump thunks (`0899` `08C2`
  `0892` `08D0` `090E` `08AD` `08BB` `088B`) and RSOUND.LOG `00AE` timer
  uninstall, including its hand-applied `POPF_STOPS` row to `00FC`. Audit before
  gate: 39 instructions, 212 mutants; 45 optimized seed checks. +73 named bytes,
  all route-executed (+24 ASOUND.117 / +49 RSOUND.LOG), no fixture-only increase.
  `new_batch.py be bd2 03c761a --no-baseline` used fresh sequential references.
- **Measured fixes:** D101's unwritten Transfer local staged as 11 before the
  handler prologue, then an unchanged-tension click: 12 visible/RGB pixel
  changes off, zero on in both engines. It initializes FFFFh and erases only
  valid remembered slots. Q12b: 174,256 call traps at VGAME `707B` across 63
  routes (18 reach it), 77 zero saved-SI words: 99.956% nonzero. All 63 final
  guest states equal the prior accepted reference. It explicitly passes 1
  (2x2). Focused checks: D101 118 runs, Q12b 44, including every event boundary.
  Evidence: `D:/f117-gate/d101-q12-measure-20261025/`.
- **EDC2 was its body, not fixture/harness.** Clean-master executable without
  be reproduces seed `0xfe825bd272ab`, state 911. The string at C49F:0001
  aliases PUSH BP at B82F:C70A: that push inserts a zero at physical C49FA.
  The pre-scan ran 111 clocks instead of 74 and uppercased the return `6DC2`
  to `4DC2`. The body now declines a scan reaching either pushed BP byte,
  like the other string helpers; original execution handles the alias.
  No comparison relaxed; six isolated seeds and `matched_strupr_stack_alias`
  pass. Evidence: `D:/f117-gate/edc2-master-20261025/`, be's `prechecks/`.
- **Prior engine repair, bd2** (`fe825bd`): matched overrides run only while
  the entry still holds its module's unpacked opcode byte, preventing a stale
  instance from firing on reused memory. Regression `tests/test_stale_override.c`;
  evidence `D:/f117-gate/p2-batch-bd2-accept/`. Earlier PIT/region-refusal theories
  were superseded by the first-instruction trace.
- **Routes:** 63. Damaged-install routes (`--damage ACTION:FILE`, `tools/route_damage.py`: a private hard-linked
  copy; the install is never written) run the original's own error handlers: `err_player_assert`,
  `err_player_divide`, `err_boot_series_palette`, `err_driver_overlay`, `err_dswap_nostart`, `err_start_no_world`,
  `err_start_terrain_files`, `err_vgame_no_photo`, `err_dswap_no_swap_palette`, `err_end_no_tass`.
  `libya_tour_eject` and `korea_release_eject` use a compressed boot (Esc skips PLAYER's animations; keep one
  unskipped boot such as `boot_to_flight`). Full interpreter coverage of all 63: `D:/f117-gate/route-cover-20261020b/`.
- **Dead code** (owner rule: only when certain): `docs/census-dead.tsv` (73 census rows, 2,927 bytes;
  certificates `D:/f117-gate/phase2-final/dead-cert/`, the 21 Oct evidence review `dead-review-20261021/`) and
  `docs/census-dead-ranges.tsv` (byte ranges that are not census rows: 868 bytes of SETUP and PLAYER jump slots
  no program binds or names, `slots-review-20261021/`, and 367 bytes of code no walk decodes,
  `unreached-review-20261021/`). Held back on weaker evidence: MPS_LOGO `00754` (a `LABSLOGO.SS` byte our
  damage routes can poke), `01B3C`, `02ECA` (single-writer arguments), VGAME `12E4B` `11CF4` `11DFF`, START
  `081C6`, MPS_LOGO `02ECF` (only "no reference found"), SETUP slot 47 at `02E51`; 12 rows / 831 bytes stay
  PROBABLE. `docs/census-data.tsv` gained 183 padding and table ranges (257 bytes) on 21 Oct.
- **Quiz option (owner, 20 Oct):** `--fix QUIZ` restores the aircraft-identification quiz GOG patched out (an
  override at START `001D` runs `0703`); off by default, `--fix all` and normal play never switch it on. Routes
  `quiz_right`/`quiz_wrong`; the quiz routines are matched (batch ah).
- **Normal-play defaults (owner, 18 Oct):** **15 Hz / 60 MIPS**, all thirty-four fixes on (D101 and Q12b accepted 25 Oct). S15, 15 normal / 30
  compressed updates, recorded per-machine cadence; legacy recordings keep eight. Do not promote 15/40. The
  previous mode is `--sim-hz 8 --ips 20000000`. Validation: [docs/cadence15-validation.md](docs/cadence15-validation.md).
  Core, API and headless runs start with fixes off; parity commands prepend `--fix none`.
- **Phase 3:** Stage 3 closed 19 Oct (clock default 0.4 / 0.025, the owner's A/B looked the same); Stage 4 done
  for what can be tested here. Stage 2's open parts and the hardware-only parts are a Phase 4 maybe.
  Re-release features (24 Oct): `--presentation original|enhanced`, `--fixes original|recommended` and
  fix-set-tagged saves are built (`ab4256b`; what exactly: the roadmap's Phase 3 entry); SETUP auto-answer and
  the sound board are design notes only. The settings' progress item is still 0%: raise it only with evidence,
  and only for what is built.
- **Other versions:** [docs/other-versions.md](docs/other-versions.md) (Macintosh and Amiga ports; evidence for
  D1 and its sub-fixes). Both later ports kept frame counting (Mac cap 60, Amiga cap 10), so D1 is
  not a DOS-only quirk. Mac-version improvements are an unscored Phase 4 maybe.

### Presentation facts worth keeping

- `--present rerun` (in-between poses drawn by the original's own code on a
  throwaway machine copy, smoothed playback clock) is integrated and gated;
  the owner's `4k60.bat`, the repository's `f117a.4k.ini` preset and
  `--presentation enhanced` use it. Owner verdicts (15 Oct): motion on a real
  4K screen "looks good"; road and coastline jitter are the original's and are
  **not** smoothed; identity pairing is opt-in (`F117_PRESENT_IDENT=1`); draw
  distance is not extended; the high-resolution modes keep the original
  renderer's quirks.
- Baselines every presentation change must keep: all 63 N=1 routes 260,613
  exact frames / 260,527 pairs (batch al); boot N=4/9 736 frames / 732 pairs;
  the owner's complete N=9 flight 69 checkpoints / final `31c259b992340414`.
  `tools/presentation_check.py` names the routes that start VGAME but end at a
  file error before a flight frame (`NO_FLIGHT_ROUTES`).
- Verification scope: `drawfeed.c/.h` and `rerun.c/.h` execute or snapshot
  machines, so they need the full gate plus
  `presentation_check.py --mode rerun` over all routes (the light check
  refuses them). App-only changes (`src/host/main.c`, example inis) use
  `present_light_check.py` with `--app-session` / `--app-exit-after`: the
  headless gate never runs the threaded app path (a frozen palette once passed
  everything).
- Original-hardware references for cadence: 486/ET4000 reaches 16.47/16.57
  updates/s with S about 13; 86Box 386DX25/33/40 gives clock ratios
  1.306/1.272/1.167 at S=4/5/6. These support fifteen as an attainable
  light-scene rate, not recovered developer intent
  ([docs/speed-sweep.md](docs/speed-sweep.md)).

## Owner decisions and instructions in force

Scoring decisions and their dates are recorded in [docs/progress.md](docs/progress.md); this list is what
governs day-to-day work.

- **No meaningful game code may be missed (21 Oct).** Approved: bring the launcher, the resident graphics
  library and the sound drivers into the Phase 2 scope (the percentage drops at first; accepted); a route
  campaign for the reviewed static code; a reference entry (name and one-line purpose) for every range that
  cannot be matched; a review of the unreviewed static-discovery bytes. **No emulated floppy drive** (21 Oct):
  DSWAP's disk-swap path stays original.
- **P2 completion scope (23-24 Oct):** the accounted-for checklist is a scored item (weight 10); the full
  "P2 is done when" definition is in [docs/progress.md](docs/progress.md#phase-2-completion-requirements-owner-23-24-october-2026).
  Declined routines are Phase 2 work, not exclusions (23 Oct).
- **Naming denominator (24 Oct):** second-read `stays-original` reference entries outside the census leave the
  naming denominator (790 bytes: 688 launcher bytes and the 102-byte SETUP timer handler); all other statuses
  stay in. That change alone moved naming 84.80% -> 85.12% (P2 81.31% -> 81.41%), not matching work.
- **Program launcher stays original (23 Oct):** F117.COM `0100`..`03CA` (715 bytes) is the fifth design rule
  ("program launcher"), reviewed by an independent second reader (no copy protection, no checks beyond child exit
  codes); the overlay loader `03CB` stays matched.
- **Hardware wait loops (23 Oct, option C):** the sixth design rule lets a wait or calibration loop stay original
  only if no lockstep state can complete it, each with a second reader; the STI installers (18 routines, about
  1,280 bytes) and the OPL write primitives (8, about 361 bytes) must be matched. The first read
  (`D:/f117-gate/wait-loops-first-read/first-read.md`) found that none of the 8 candidate loops qualifies:
  ASOUND.LOG `00FE` `0140`, RSOUND.LOG `012D` `03A2`, RSOUND.117 `0AFE` `0D07`, ASOUND.117 `0A17` `0B01` can all
  be completed by a fixture (three as the harness stands, five once the callee that executes STI is stubbed, as
  SETUP `111A`'s fixture does). Match them; the rule has no users.
- **Dead code leaves the denominator only when certain** (19 Oct), with a certificate per row.
- **Machine-option-only error paths stay original code (20 Oct):** out-of-memory arms, DOS read/write/close
  errors, the `_C_FILE_INFO` environment arm (about 2,500 bytes). Do not build test-only machine options for
  them; they are not dead either, so they stay in the denominator (and get reference entries).
- **Phase 2 first**; safe optimisation and parallelisation are authorised, but no check may be removed or
  weakened. Declined: splitting the long interpreter run into checkpoint segments, dropping
  `--verify-interp-cache`, changing the default overlap of eight workers. Larger batches that save time
  overall are approved (23 Oct); no proof agents while the gate runs.
- **Parity target is the original on its own hardware, not DOSBox**; do not copy identified DOSBox quirks;
  label behaviours by evidence. Gameplay and logic parity outrank rendering.
- **Keep all original graphics** (fonts, sprites, the map arrow); smooth and re-render existing geometry, no
  vector replacements.
- **Bug fixes are on by default for normal play** (all thirty-four); parity gates keep the original reference.
- **No screen takeover.** Ask before any visible or fullscreen test; emulators run headless, silent, hidden.
- The owner's untracked `4k60.bat` and `original-fixed.bat` (settings live in `build/f117a.ini` and
  `build/f117a.4k60.ini`, outside the repository) stay untouched: never stage them.
- **Owner decisions 27 Oct (accepted all recommendations):** START `08F94` is split out of the start-up row, proved and counted as its own routine; the 831 probable-dead bytes are proved with lockstep inputs, not declared dead on weak evidence; the machine-option error paths follow the 20 Oct rule (prove with test inputs, no fake machine options); RSOUND.LOG `08BE` gets one agent on a stronger fixture, else the unexercised guard stays original and only the rest is credited (never accept a missed mutant); fixes to accepted bodies are re-proved, and a fix-only change is gated before it is committed as accepted; round-5 checklist readers next.
- Keep going item by item; ask only on policy.

## Next steps

**Plan (owner agreed 26 Oct): fewer, larger gates, and a checklist push alongside.**
- **Process changes agreed 26 Oct (all keep every check, input and verdict identical; do these next batch).** Before the gate run only the new
  CTests, not the full suite (the gate repeats it; bg's full run cost 21 min, 1,252 s of it one `func_lockstep` test). Keep the standing pre-check
  (this commit's seed, so the gate cannot fail on it, plus at least one other whole-table seed) and start the seeds beside the CTests once the
  build finishes; add a third seed the gate does not use when the machine is free. Time `--overlap-jobs` (interpreter re-run beside coverage) on one gate and keep it only if the gate log shows it faster; never
  drop `--verify-interp-cache`. Between gates use six proof slots on disjoint masks with at most three `cl.exe`; none while a gate runs.
  Profile `career_promotion` (805 s, the gate's floor) and adopt only speedups that leave every route hash identical. Start the checklist
  second-read trial with many parallel fresh reader agents. The second PC is deferred (owner, 26 Oct). Combined proof route runs were
  rejected: they drop the per-routine proof before the gate. `D:` is a spinning disk (`D:/f117-gate`, worktrees); moving proof builds to the
  NVMe is an unmeasured idea (C: had 50 GB free).
- **Batch size** (the three-slot and three-agent figures below are superseded on 27 Oct: see resume state 3; batch bl had 33 routines and was fine). A gate costs about 45-60 minutes (gate, two pre-check seeds, audit, score refresh, docs, CI) whether it holds
  4 routines or 40, while proofs cost about one agent-hour per 4-5 small routines with only three slots. So run several proof
  rounds back to back (three agents, masks 0x000F0000 / 0x00F00000 / 0x0F000000, `finish_all.py` on each round, no gate between),
  then integrate and gate once at about 25-40 routines. Merge the agents' harness hunks into one form before integrating (they
  overlap in `io_r`). A failed gate costs more with a big batch: keep the audit and both pre-check seeds before every gate.
- **Expected value (updated 26 Oct, after batch bh and the full checklist pass).** What was left on 26 Oct after batch bh: about 3.1 points in matched functions, about 4.2 in naming, about 2.2 in the accounted-for checklist.
  Measured since: checklist round 3 (4,476 bytes) +1.0, batch bi (1,393 named bytes) +0.56, batch bj (283) +0.13, checklist round 3b (1,344 bytes) +0.30.
  The bulk reference work gave the big early jumps; the matching pool is now small, partial hand-backs credit little and each batch costs a full gate.
- **The accounted-for checklist (owner decision 26 Oct: do the whole checklist, every entry blind-read by an independent
  reader, cost is no object, optimise wall time only).** `D:/f117-gate/checklist-full-20261026/` holds 912 units that partition exactly the 43,920
  then-unreviewed bytes (`reconcile.py`: 0 missing, 0 extra, 0 overlaps), `READER-BRIEF.md`, `AGENT-PROMPT.md`, per-group manifests (`groups/R###.txt`) and
  `adopt.py` (tabulates `reads/`, stages only ACCEPT units whose reader-judged status equals the proposal, never stays-original/dead/probable-dead, and
  validates on a COPY of the repository TSV). **Round 1 (156 fresh readers, 912 verdicts): 444 units / 19,928 bytes adopted** (`staged-round1.tsv`, applied in
  the batch bh commit: 780 -> 20,708 of 44,714 bytes, 46.31%, P2 82.14% -> 87.27%); 368 accept-with-changes, 21 needs-context, 13 reject and 66 status
  mismatches are NOT adopted. Reader names and ranges were corrected where they over-claimed (a retry named a failure, an arm named a whole function, a
  range cutting a function mid-instruction). **Round 2** (`round2/`) re-cut those 468 units into 588 packets / 79 groups: all read, 367 units / 13,976 bytes adopted.
  The checklist is now 41,749 / 46,112 (90.54%) after rounds 3, 3b and 4 (resume state item 4); round 4 covers what is left,
  with corrected blind reads and no unsupported exclusions. Use the actual available reader slots; this session has at most three concurrent subagents.
- **Driver code stays in scope (owner, 26 Oct; settled, do not reopen).** Drivers are 7,666 of the 9,456 unmatched executed bytes (after bh): the
  `.117` overlays (headers "F117A AdLib/Roland/IBM 10-23-91", MicroProse copyright 1991: F-117A builds) and the `.LOG` drivers ("AdLib Logo 7-18-91",
  "RolandMT-32 3-25-91", "IBM Logo 7-12-91": earlier dates and logo names, plausibly a reusable MicroProse driver; whether other games shipped them is
  unverified and does not matter for scope). They keep counting in the P2 denominators, as decided on 21 Oct.
0. **Batch bh accepted (the bg4 five). What is left of the private bg work:** nothing proved. Archive `D:/f117-gate/bg-handoff-20261026/`; blocked
   ISOUND.LOG `05A1` `066D` `0715` `071D` remain false leads. Next matching leads: see item 1.
   - **Integration traps that still apply:** integrate with `safe_integrate.py`, diff each proof's `tests-base.c` against HEAD (`diff --strip-trailing-cr`:
     the bg3 copies had CRLF), merge every `io_r` control and reset each per state, keep the default-disabled RNG stream, run `audit_private.py` from the
     newest accepted batch (stock is identical), and put `NOBLOCKS` on bodies with no `XB` macro. Extra seeds use `extra-*.log`: the kit requires exactly five
     standard logs. `new_batch.py` takes `name::partial` (two colons) for a partial entry with an auto-detected prefix.
1. **Match the rest of the sound drivers** (7,666 of the 9,456 unmatched executed bytes after bh). The RSOUND.LOG timer
   installer is done. Check the registry and current named coverage before selecting any lead: `D:/f117-gate/ay-next-leads/leads.md`
   predates batches bb to bf. Still open there: wrapper/dispatch entry points (relocated `mov ds`, indirect calls), the
   remaining installers that end `CLI ... STI; RET` (stop after the STI; bc and bf matched five), the hardware wait loops above,
   and the large routines (ISOUND.117 and ASOUND.LOG, 90-320 instructions each). ISOUND.LOG `0581` needs separately verified
   bounded-handler/resume support for its EOI-before-IRET arm. `p2-asoundlog-008a` is an initialized lead, not a proof: its first PIT
   control OUT wakes the machine, so cover its following continuations explicitly. Plan: see resume state 3 (many agents, build throttle in the kit, spare-thread masks during a gate). STI-first handlers stay original by design. Precedent for a PIT
   installer tail: only the counter-0 reload's second write wakes the machine (ISOUND.LOG `0603`, RSOUND.LOG `027C`).
   All original selected fixes are done (34 normal-play switches). The bugfix worktree stays until the owner authorizes removal.
   `D:/f117-gate/modules-scope-20261021/plan.md` is a source of leads and traps only.
2. **The hard tail: declined routines are Phase 2 work, not exclusions** (owner, 23 Oct). They are part of the
   unmatched executed bytes of the seven programs, and P2 cannot reach 100% without them. List (from
   `D:/f117-gate/kit/DECLINED.tsv`, the roadmap and the rejected reviewed entries); each needs the stated unit
   before it can be proved:
   - VGAME `0x00010` `main()` (the whole game): a nested-run proof or a decision; check before assigning.
   - MPS_LOGO `1ADC` and `1C82` (`1ADC` is the C run-time `strstr`; first drafts exist, resume state 2); START `11DB` is **already matched** as
     `start_map_hover_light` (batch x): root confirmed the whole 301-byte / 127-instruction body, including `1285` and `12DB`; do not prove it again.
   - START `8427` and END `4393`: earlier routes missed planted defects and the routines wait; needs routes and
     inputs that reach the arms and wait handling.
   - END `57B0`: use the 92-byte dispatch arm, approved against accepted START `9E3E`; the 795-byte enclosing function overlaps already matched bodies (resume state 2).
   - VGAME `137CE`: a 9-byte far table thunk at the observer hook's address (`hook_fill_rows`); the body must
     call the observer itself and join the `deliberate` list in `tests/test_overrides.c`.
   - PLAYER `02853` (interrupt handler whose STI is its 13th instruction, so not STI-first) and
     `028C1` (retrace re-sync, STI inside its loop): rejected as original by both readers and taken off
     `docs/census-original.tsv`; match with the stop-after-STI hand-back.
   - START `08F94` (the run time's divide-error entry, inside the C start-up row).
   - SETUP `10B3` (the retrace helper after the reviewed timer handler) and the three MPS_LOGO sound-overlay
     thunks `00DA:0307`/`030E`/`0315` whose runtime-patched jumps are now validated: matching work.
   - Unfinished private `p2-player-vc-0540` still has two missed mutants and only `err_driver_overlay` reach;
     do not integrate it as proved.
   Not on the list: DSWAP `3DF3` (2,791 zero bytes of data, already excluded) and the design-rule originals.
   Add these to a batch once the driver leads run low.
3. **Reference/checklist: trial, round 1 and round 2 are done and credited; round 3 is frozen and fresh reads are underway (resume state above).** Background kept for reference:
   `D:/f117-gate/unmatched-reference-20261026/` replaces the stale 21 Oct inventory for planning. Its current
   classifier-based refresh against accepted bf has 741 rows / 37,032 distinct unmatched reference bytes
   (29,576 distinct naming-workload bytes plus 7,456 labeled original/dead exclusions); no duplicate, omitted
   or already matched target bytes. Its separate checklist preserves all 44,641 bytes: 715 reviewed, 43,926
   missing second reads, including 26,183 already matched fixture-only bytes. Do not drop those checklist duties
   merely because matching credit exists. The old annotations remain proposals, with uncertainty retained.
   `vgame_int86x_carry` is corrected to the generic interrupt carry helper's exact actions; a DOS-read-only
   prerequisite is not established. `trial-proposal.tsv` and 24 blind disassembly packets cover 247 candidate
   bytes. **Trial run 26 Oct (four fresh readers, six packets each, ~72-78k tokens per reader, one to two minutes of wall time):
   12 accept, 10 accept-with-changes, 2 needs-context, 0 reject.** Ten entries (65 bytes) are adopted (`machine-option-only`: packets
   1 7 9 10 11 13 17 18 22 24; 715 -> 780 bytes, P2 82.14%). Held: packet 14 (`end_stackavail`, a whole library function: the readers judged
   name and range, not whether `machine-option-only` is the right status) and 21 (stack-probe overflow arm: the reader could not verify
   that exit path 1549 never returns). The ten accept-with-changes (2 3 4 5 8 15 16 19 20 23) share one failure: a name or range that
   claimed a failure where the bytes are a retry, a stub or an entry shim. Correct each (reader's suggested name or range), then give it
   to a DIFFERENT reader; the proposer must not credit its own revision. Packets 6 and 12 (`mov ax,2; jmp` after the entry's `ret`, probably
   the C run time's R6002 floating-point stub reached from elsewhere; PLAYER `015BA` (packet 16) is the same shape) need their real callers
   found first. The pass rate is for the easiest tiny arms: expect larger routines to cost more per byte. Per-reader cost to
   scale from: about 12k tokens per packet. The packets contain private game bytes and stay outside the public repo. No second-read
   score may be raised by the author of an entry. See the private `notes.md` and `coverage-check.json`.
4. **Static code no route runs.** Reviewed static code: the resumed review (`D:/f117-gate/static-route-plan-20261021/`,
   `static-route-verify-20261021/`) found no additional route and no new dead certificate for the 10,301 bytes it
   covered and retains all 27 requested ranges / 6,505 bytes; indirect/callback/vector and writer closure is
   incomplete, so any later exclusion needs exact per-range closure. VGAME `1377:0204..0356` includes the M pixel
   routine `0287`, and the old MPS_LOGO pointer scan used the wrong DGROUP (`041A`, actual `0332`). Per-group
   decisions: [docs/static-code-review.md](docs/static-code-review.md); private `resumed-report.md` and
   `group-a-closure.md`. B/I remain held too; C needs declined floppy support or machine-option-only errors.
   One cheap extra route is proposed in `static-route-plan-20261021/proposal_err_dswap_keys.args`
   (`err_dswap_nostart` plus two keys; needs `# expect-` lines). Unreviewed static bytes
   (`D:/f117-gate/static-review-20261021/`): the 117 audited code rows and the 70-byte VGAME data row are adopted
   (`adoption-audit.md`); still to adopt with a strict validator: the 20 follow-up data rows
   (`followup-data-rows.tsv`) and the plain follow-up code rows (`followup-code-rows.tsv`,
   `followup-review-report.md`). About 1,800 executed bytes outside the census still need proofs.
5. **Sort the matched code no route runs** (owner, 21 and 23 Oct): 26,311 fixture-only bytes (about 88 whole
   routines no route reaches, plus unrouted arms such as ASOUND.LOG `0344`'s voice sweeps and ISOUND.LOG
   `0253`'s stream readers). Lockstep proves them and the naming score credits them, but no real play confirms
   them and the gameplay score does not count them. Classify each group as player-reachable (write a route,
   which also moves it into the gameplay table already credited) or not reachable (error path, hardware option,
   dead: a checklist entry in `docs/reference-reviewed.tsv`). The F117.COM loader's six error arms are an example:
   allocation/resize failures are machine-option-only, overlay-load/header failures and the common exit need
   routes (a launcher-level missing resident overlay is a route lead), the successful FFFF-paragraph allocation
   arm stays `unreached-held`. Mostly read-only work plus route writing; run it between proof batches. The same
   sorting applies to the 13,809 reviewed static bytes: each ends matched (a fixture proof is enough for the
   naming score), routed, or certified dead or data.
6. **Remaining P2 completion tasks** (definition in [docs/progress.md](docs/progress.md#phase-2-completion-requirements-owner-23-24-october-2026)):
   - **Packer stub:** the `packer stub` design-rule name is recognized but earns nothing yet. All seven packed
     programs have 16 raw-source-validated route records at their packed CS:000E..002A (rejected correctly
     against the unpacked image); do not add their addresses as original code in the unpacked image, which would
     alias unrelated code or data. A separate source-representation-aware review/score path is needed. The
     read-only plan `D:/f117-gate/packer-representation-plan-20261024/` verifies identical packed code in all
     seven files (163 instructions / 325 bytes in `000E..00F7` and `00FC..0158`, excluding the five-byte
     signature between them); the copied core adds 147 instructions / 296 bytes per file but has no complete
     route capture. A copied runtime instance must not count its source twice.
   - **Encoded SETUP code:** adopted with strict validation (`tools/encoded_code.py`, 585 bytes, two
     `unreached-held` reference entries); its matching and entry-path work stays open.
   - Match the ~2,500 machine-option-only error-path bytes with test-input proofs; no test-only machine options.
   - Resolve the 831 probable-dead bytes: a strict dead certificate or a proof, not "probable" forever.
   - Before declaring P2 100%, run the closure review ("leave no stone unturned", owner, 23-24 Oct) of indirect
     calls, jump/dispatch tables, callbacks, interrupt vectors, overlay entry tables and self-modifying code.
     It cannot be scored; record what it covered as a closing step.
7. **Phase 3 re-release features** (roadmap; Phase 2 comes first, but worktree work beside a gate is fine): the
   built settings are the start of the first item. **Phase 4 stays unscored:** refresh rates above 60 Hz, the
   hardware-only parts, a person playing, Mac-version improvements.


## Phase 2 batch workflow and its traps

Per batch: proofs (agents, steps 1-7 of the kit) -> `finish_all.py DIR:ROUTE ...` (checkpoints, boot proof,
verdict; at most three at once) -> `integrate_many.py DIR ...` (or `safe_integrate.py DIR ...`, which restores and
retries a routine when the synced folder locks a file mid-write, but carries no table rows) -> diff each proof's
`tests-base.c` table rows against `tests/func_lockstep.c` and hand-apply the ones not carried
-> build (`build.cmd` from PowerShell) and the new CTests -> pre-checks -> `new_batch.py LETTER PREV HEAD
[--no-baseline] DIR ...` -> `audit_private.py` -> `affinity_launch.py py p2-batch-LETTER-accept/accept.py` ->
`finalize.py` -> score refresh (`py tools/progress.py --census ... --named-cover
p2-batch-LETTER-accept/named-cover --route-cover D:/f117-gate/route-cover-20261020b --data D:/GOG/F-117A`) ->
docs -> commit (title from `py tools/progress.py --title`) -> push -> CI. Scripts are in
`D:/f117-gate/accept-kit/`; copy `audit_private.py` and `finalize.py` forward from the newest accepted batch
(they carry the allowances below), since `new_batch.py` copies stock ones.

- **The keyed baseline must match the current interpreter key.** The kit's `new_batch.py` picks the newest
  baseline whose `base_key` equals `build_recomp.interpreter_key` and prints which (until 24 Oct it copied the
  previous batch's, so batches ay to bc ran the sequential fallback unnoticed); `overlap_accept.py` validates it
  and falls back to sequential if invalid ("No valid keyed baseline" in `accept.out`, expected after a `src/`
  change, as in bd2). With a valid baseline the presentation check overlaps the gate (no time saved: next bullet).
- **Optimization pass 2 (owner, 24 Oct), first finding: the presentation overlap does not save time.** Comparable
  gates, both with a full coverage pass: bc sequential (coverage 766 s, parity 343 s, N=1 420 s alone) accepted in
  1,741 s; bd overlapped (coverage 635 s, parity 879 s, N=1 916 s beside parity) in 1,822 s. Parity and N=1 each
  run ~16 workers on 24 threads, so overlapping roughly doubles both. One sample each; do not change the default
  on this alone, but stop expecting the overlap to save time, and measure the next two gates the same way (bd2
  runs sequentially). Still to examine: the 25-minute six-seed precheck (two rounds of three seeds x 8 shards
  fill the machine). Read bd's `gate.log`, precheck logs and stage timings for where wall time
  goes (precheck sharding, proof build/mutation loop, repeated audit/finalize/verdict work, idle CPU in the gate).
  Propose or apply only changes that leave every check's inputs, outputs and verdicts identical; measure on the
  gate's own logs, never run extra heavy jobs beside a gate.
- **Proof checkpoints need a gate after any `src/` change.** `kit.py checkpoints` reads the interpreter-cache entry
  for the current key (`src/` except `matched.c`, `CMakeLists.txt` minus tests, the game files); after an engine or
  host change it fails with `FileNotFoundError ... interp-cache/<key>/result.json` for every routine. Run the gate
  first (it stores the new entries), then the batch's `finish_all.py` (24 Oct: batch be after the bd2 engine fix).
  A new CMake target changes the key too, and so do line endings (the key hashes raw source bytes).
- **`finish_all.py DIR:ROUTE` before `audit_private.py`:** the audit needs `ibm-boot-proof`; running `checkpoints`
  and `verdict` by hand is not enough.
- **Run `audit_private.py` BEFORE the gate.** It reads only the proof directories and takes seconds; batch au's
  failed after a 27-minute gate and forced a second gate. Mark `:partial` at `new_batch.py` time for bodies that
  hand back, inline a tail, leave a dead arm unwritten, call original code, or whose first room claim is an
  expression (`14 + 3 + IO_SLACK(2)`); `partial` is read only by the audit. The audit accepts lines between
  `if (!room(c, N)) return 0;` and `c->icount += N;`, never across a call. Claimed blocks must be disjoint: an
  instruction reached by two paths (a shared `RET`) is claimed once, in a shared label.
- **A failed gate's directory blocks a rerun:** its `runner/` stays, so archive the directory
  (`p2-batch-X-accept-run1`), recreate it with `new_batch.py`, then restore `batch.json` and the two copied scripts.
- **Pre-checks before a gate:** the commit-keyed seed (`build_recomp.rotating_seed()`; the first 12 hex digits of
  HEAD) and at least one more whole-table seed through `tools/func_lockstep_par.py --exe build/func_lockstep.exe
  --jobs 8 --states 4000 --verbose --seed SEED` (about 8 minutes a seed), and every new route once with
  `--engine recomp`, grepping `[matched] OVERRUN`. Call-slot and argument-order defects show only at a seed whose
  stop limit lands exactly on the boundary, so two whole-table seeds before every gate are the minimum; a wider
  sweep (13 seeds, in the background) found two latent defects before batch ar.
- **A seed failure in an older routine is often its fixture, not its body.** A lone `1 mismatching` with every
  line `equal` is a coverage floor (`fixtures reached too little`). Batch bc's precheck at `0x600DF00D` failed
  ISOUND.LOG `0253` at 301/303 instructions; the fixture was repaired to force the missing byte commands in one
  sixteenth of states after preserving every original RNG draw, reproved separately
  (`p2-isound-log-0253-coverage-repair`, audit `-audit`; the original proof untouched; failure in
  `D:/f117-gate/bc-pre-attempt-1-coverage-floor/`) and pinned in the acceptance directory, without new credit.
- **Harness changes can expose return-tracking gaps, not game defects.** Batch ax's precheck found callees in END
  `53DA` and SETUP `1600` that consume extra stack words and return directly to the caller; the runner recognizes
  only the exact completed original CPU/clock state, still compares all memory and I/O, and grants no named
  completion. Unfinished continuations reject already contradictory I/O, and a separate interrupt-clock hash
  preserves synthetic DOS answers. A mid-run probe that consumes `rnd()` changes later inputs (SETUP, state 510).
  Controls: `D:/f117-gate/harness-unfinished-io-20261022/`, `ax-legacy-io-20261023/` (`rng-review.json`).
- **A rerun of the same coverage inputs gets slower, not faster (lead, unfixed).** The second batch-au gate skipped
  coverage, so the interpreter parity runs no longer overlapped it; parity took 1,666 s and acceptance 1,920 s
  against 1,600 s. Untested verdict-neutral ideas: start the interpreter runs early even when coverage is skipped,
  and start N=1 after them. The gate log prints start and end offsets of its longest jobs per stage.
- **Fixtures force each branch outcome** in a fixed share of states (by the state index), and every proof is
  run at six or more seeds: a chance-only arm fails `fixtures reached too little` at some rotating seed.
- **Harness hunks `integrate_many.py` does not carry:** SPANS (beyond the kit's own row), IMAGE_PATCHES,
  LIVE_FRAMES, PROGRAM_ENDS, POPF_STOPS, `patch_image_for` lines, `run_to_ret` rules, the `setargv_` pop-distance
  list, the `g_native_int10` list, the `g_cov_ip` lines for driver routines that start below their entry (keyed
  by module and IP), and the self-write exemption for a call counter stored in the code window (ISOUND.LOG
  `0251`). Diff each proof's `tests-base.c` against the repo's current `tests/func_lockstep.c`.
- **Same-IP routines in different segments:** SPANS is keyed by IP; `kit.py integrate` refuses an alias unless
  `KIT_ALLOW_ALIAS=1`, and the new span then goes in the segment-keyed `SPANS_SEG`.
- **Duplicates:** an agent's "new" address can already be matched under another segment spelling
  (`14C2:00B7` = `1452:07B7`); compare linear addresses. The finalize tally catches it.
- **Audit allowances:** `NOBLOCKS` prefix in `batch.json` for bodies without an `XB` block macro (a
  one-instruction return uses it, not `partial`); a hook inside a routine is accepted only with a covering
  `fix_inside` guard in the body (END `03D9` declines while D12 is on).
- **Baselines:** `--no-baseline` whenever `CMakeLists.txt` outside tests, non-matched `src/` or a route changes;
  the next batch then freezes a new keyed baseline (the current one is for 63 routes).
- **Kit libraries:** refresh `D:/f117-gate/kit-libs` from the main build after any `src/` change (a stale set
  once lacked `--fix QUIZ`); keep the old set as `kit-libs-before-<commit>`.
- **Measure only from the accepted batch's named cover.** A read-only measurement against an older named cover is a
  lead; refresh after the gate and never infer the final score from it.
- **Do not chain a commit after a doc-edit script:** a failed edit once let a commit go out without its docs.

## How Phase 2 is done: the kit and parallel agents

`D:/f117-gate/kit/kit.py` runs a private proof for one routine (`init`, write
`body.c` / `fixture.inc` / `mutations.json`, `lock --cover`, `seeds`,
`mutations --jobs 1`, `checkpoints`, `verdict`, `integrate`);
`kit/BRIEF.md`, `kit/AGENT_PROMPT.txt` and `D:/f117-gate/ac_brief_extra.txt` are
the agent brief. Three agents, each taking a family of routines one after
another (not one routine per agent), is the pattern to keep. Agents do steps
1-7 only, in their own `p2-*` directory, every command through `kit/pin.py MASK`
(CPU set + BelowNormal) with `mutations --jobs 1` in each of at most three proof
slots; the tool's default of four jobs is unsuitable for the shared queue. Use a
separate mask per slot (0x000F0000, 0x00F00000, 0x0F000000), with at most three
`cl.exe` processes across all work. Main builds use `CMAKE_BUILD_PARALLEL_LEVEL=3`.
Finish proof workers before starting a gate; run no unrelated heavy job beside
it. Verify each `private-verdict.json` yourself (counts, mutations, source
hashes, real-route calls) before integrating. Routines that stay original or
were declined are listed in `D:/f117-gate/kit/DECLINED.tsv`. The machine is a
Ryzen 9 7950X (16 cores, 32 threads); `affinity_launch.py` uses mask `0xffffff`
(24 threads) and leaves 8 free.

## How this project verifies Phase 2 work

A routine counts as matched only after BOTH:

1. **Fixture/lockstep proof:** `func_lockstep` at several seeds comparing the
   hand-written C with the original's bytes from randomized entry states (with
   returning driver stubs where the routine depends on them): zero mismatches,
   full instruction coverage, every deliberate mutation caught.
2. **Real-driver proof:** a scripted route (`tools/routes/*.args`) runs the
   routine with a positive count and every checkpoint and the final hash equal
   between engines. A routine no route reaches stays out of `master`.

Every change to `src/matched/matched.c` needs a fresh full gate
(`py tools/build_recomp.py --data D:/GOG/F-117A --seed-coverage
C:/Users/Tideg/f117-recomp-local/coverage --jobs 16 --verify-interp-cache`, via
`D:/f117-gate/affinity_launch.py`; about 26-30 min) before it is pushed. Read
the tally (`N ok, 0 failed`), not the exit code. Check GitHub CI after pushing
(a run shows "cancelled" when the next push supersedes it).

## Traps

Writing bodies:
- Read every argument at the moment the original reads it, through BP in the
  original's order (`bp_arg`, never `arg()` after `push bp`): DS and SS can
  overlap (VGAME `D9E7`, START `28CE`). Read segment immediates from the code
  (`seg_read16`), never hard-code them (END `4160`); the launcher, overlays and
  drivers load at 0800h in the harness (the seven programs at zero), and a DS=0
  mutant passes at zero base.
- A block followed by a CALL needs `room(c, N + 1)`: `guest_call` charges the
  CALL itself, so a stop limit landing exactly there ran it one instruction late
  (END `100D`, `OVERRUN ... +101 with the limit at +100`). New bodies may claim
  the call separately with `SM4_NEED(1, ip)`. Never mass-edit bodies that have a
  private proof: `finalize.py` requires each proof's `body.c` verbatim in
  `matched.c`.
- Charge the clock between every port access, stop right after each STI (call
  `x86_sti` before charging its own instruction, so `inhibit_at` names the right
  boundary), and stop after any OUT that wakes the run loop (graphics controller,
  PIT/PIC writes) with its clock charged: `if (!room(c, 1)) { c->icount++; c->ip =
  next; return 1; }`. Prove a body on `ibm_boot` first (`F117R_MATCHED_LIMIT=N py
  tools/run_route.py tools/routes/ibm_boot.args --engine recomp`, 22 s).
- Count instructions per branch arm and verify each arm with `--verbose --only`
  and extra seeds: VGAME `73C8`'s target-zero path was short by a few
  instructions in 6 of 4,000 states. Key the original's per-instruction clocks
  and the matched side's callee list by clock; the first clock whose (ip, sp)
  differs is where the count is short. A low "states compared" figure means a
  rare arm may still be unchecked.
- A wait inside a matched routine must clamp `m->run_until` to `c->stop_at`;
  gate step 9 (event limits) checks routines that guest-call code that can
  wait. Check every callee's RET form. A route that leaves a matched `getch`
  waiting trips step 9: press the key before the wait (`err_vgame_no_photo`
  types at VGAME+600000).
- Matched routines with a switchable fix site after their entry decline while
  that fix is enabled (`fix_inside`); check `src/fixes/fixes.c` for sites inside.
- `1450` is also the presentation observer's frame-phase hook: a matched body
  there must call `observe_game_draw` itself and go on `deliberate` in
  `tests/test_overrides.c`; `kit.py integrate` refuses an address with an observer.
- Helpers whose reject arm does `pop ax` / `jmp TAIL` either run the tail inline
  (tail unscored) or hand back at the tail with a `FATAL_JUMPS` row (MGRAPHIC
  `02B9`/`03E8` against `037F`/`031D`). RETF-at-entry+6 and JMP-SI exits need
  `run_to_ret` rules. A wrong call-depth value never fires a trap, so its
  mutants are invisible: use post-trap mutants and judge such calls by the
  real-route counts. A route must run the rare arms (`full_cycle` missed
  `02B9`'s loop and `031D`'s cut arm; `persian_gulf_air_training` runs it).
- An already-installed branch can differ between sibling installers: RSOUND.LOG
  `005B`'s targets the STI at `00AC` (both paths enable interrupts), while
  ASOUND.LOG `005A` and ISOUND.LOG `060E` skip STI there.

Lockstep and harness:
- `func_lockstep --only MOD.EXE:HEX`: the IP has NO leading zeros (`D0C`, not
  `0D0C`) or it silently compares 0 states. "Not testable here (routes only)"
  means only a real route can prove it.
- The commit-derived seed rotates with HEAD and can fail an untouched routine;
  reproduce with `func_lockstep --seed S --only MOD:IP`, print `g_state_index`
  in `compare()` to get the failing state, then trace that state's original run
  (`g_trace`, with registers) against the draft.
- When the original's per-instruction trace shows instruction lengths that
  differ from the dossier draft, the executed bytes differ from the image: a
  self-write window gap, not the body. Code below the entry needs a `SPANS_SEG`
  window (VGAME `120A:0815`, escape `07C8-0812`).
- A fault-replay mismatch with the same fault site on both sides: print the
  matched side's ip after `o->fn` and the nested call log before suspecting the
  body. `step_runner_run` returns at the first interrupt entry for the mission,
  ground, projectile and traffic fixtures (VGAME `5852`, 22 Oct).
- Fixtures must initialize every page/table entry a routine may write through:
  an empty renderer page table let MGRAPHIC `019C:02A1` write through ES=0 over
  relocated code (`D:/f117-gate/modules-loadbase-20261022/attempt-2-empty-page-table/`).
- When the engines differ on a route after routines are registered, find the
  first diverging instruction before theorising: `--hash-every`/`--hash-from`
  narrow it to about 1,000 clocks, then a `--trace` diff of both engines names
  it (bd's held-back routines: two wrong theories, gate load and region refusal,
  before the trace showed a stale module instance, fixed in bd2). Bisect with an
  argument list, never a shell `eval`: `\r` keys silently vanish and both engines
  then "agree" on a run that never started the mission.
- Use `recon_career`, not its alias `recon_return`, for the keyed baseline.
- Reach: the gate's recomp `.cov` files hold only interpreted fallback
  instructions, so absence proves nothing; use a complete interpreter capture.
- SETUP's INT 10h at `14EF` runs two native callback/stub clocks atomically
  (`native_stub_time`): an SDL one-instruction alert there is not extra execution.
- `matched.c` compiles with `/Os` (at `/O2` MSVC took 90+ minutes); never more
  than two or three `cl.exe`.

Tools and shells:
- `build.cmd` must run from PowerShell (through bash it hangs).
- PowerShell redirection (`*>`) writes UTF-16 on this host: read those logs with
  an explicit encoding, or have Python write UTF-8 logs itself.
- In Bash, a backslash in an edit (a route's `\r` key) can be eaten: write such
  lines from Python with `chr(92)`.

Presentation:
- Never force `F117R_OBSERVE_PAGES=1` on live presentation (it reseeds every
  phase and prevents pairs).
- Short windows with a cached map can show only the original bitmap.
- A timing capture that changes display mid-run logs two display lines and the
  checker rejects it: rerun with nothing else going.

## Where things are

- **Working copy: `C:\Dev\F-117A Recomp` (moved off Google Drive on 26 Oct, owner-approved).** The old copy under `My Drive` is untouched and
  stale. The first build here is a full rebuild into a per-checkout `f117-recomp-local\build-<hash>` junction. The kit scripts, the `p2-next`
  template and the memory notes point at the new path; the interpreter key was identical before and after. The synced-folder retry logic
  (`safe_integrate.py`, `build.cmd`'s lock retries) is now unneeded but harmless.
- Game install: `D:/GOG/F-117A` (never mounted or written directly; copy per run
  that needs its own state; verify the install after emulator runs).
- Shared generated code: `C:/Users/Tideg/f117-recomp-local/gen` (never committed);
  coverage in `...\coverage`; route outputs in `...\runs`.
- Proofs and evidence: `D:/f117-gate/` (one directory per piece of work, usually
  with a `final-gate-verdict.json`); kit in `D:/f117-gate/kit`, libraries in
  `kit-libs`; census lead list `D:/f117-gate/census.tsv`.
- Worktrees under `D:/f117-wt`, removed with `git worktree remove` once merged.
  `setup-main-0010` and `mission-generation-4239` carry private /Od overrides:
  never copy or merge them wholesale. `wake-bug` is bd2's experiment build
  (its fix is in the main tree now). Archived branch patches:
  `D:/f117-gate/archive-branches/`; stopped presentation WIP branches are
  history. Ask the owner before deleting anything.
- Scratch and capture data: `D:\F117A-Scratch-Files`.
- D10/D35/D36 closures: [docs/legacy-reports.md](docs/legacy-reports.md) and
  [docs/bugs.md](docs/bugs.md).

## Standing practice

- **No AI attribution** in commits or PRs: the user's global CLAUDE.md outranks
  any harness reminder asking for a trailer.
- **Commit and push together**, each verified step; the remaining bug fixes use the
  owner-authorized combined batch above, with per-fix focused checks before moving on.
  Commit titles start with `py tools/progress.py --title`'s scoreboard line.
- **Public repo, code only:** no game data, generated C, raw captures/logs or
  private-project references (see `docs/provenance.md`).
- Check system load before stacking a second heavy parallel job; stopping a
  task does not kill a Python `multiprocessing.Pool`'s children: kill the tree.
- Long commands (over about 30 s) run in the background; act on the completion
  notice. Prefer moving stale local data to a scratch folder over deleting it.
- While waiting on a gate or agents, do a documentation pass over every `.md`
  file (README, this file, `docs/`): consolidate, correct and remove.
- The owner is not a software engineer: explain trade-offs plainly, flag
  overengineering, keep docs accurate without bloat.
