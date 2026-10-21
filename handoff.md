# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs). State as of 20 October 2026.

This file holds only the current state, standing practice and traps, not a
session log: earlier detail is in `git log -p handoff.md`, and per-batch
evidence is in the `D:/f117-gate/` directory named in each roadmap entry.
Consolidate it instead of appending to it.

## Current state

- **Scoreboard:** P1 100.00%, P2 90.04%, P3 100.00%, All 96.96%
  (`py tools/progress.py --title`; P4 and the decided-against P3 items are not
  scored).
- **Phase 2:** 1,189 addresses matched. Scope is the expanded seven-program
  one: **165,256 / 184,601 creditable bytes** (credit by byte since 16 Oct;
  rules in [docs/progress.md](docs/progress.md)). Remaining outside the score:
  3,939 static-discovery bytes (speculative table/padding decodes and decoder
  disagreements; never add them blindly) and 18,584 executed bytes in other
  modules (launcher, resident graphics and sound drivers; they need
  module/segment support). `docs/census-code.tsv` has 37 reviewed static ranges;
  SETUP `1E4F` (142 bytes, opaque mixed range) stays deferred.
- **Batches r to ad (all accepted, evidence `D:/f117-gate/p2-batch-<letter>-accept/`; table and counts in
  [docs/roadmap.md](docs/roadmap.md)):** r, t, u (VGAME `9EFA` display pass), v, w, x (START `11DB`/`1C2F`), aa (63 key-dispatcher
  and dialog routines), ab (37 small routines and the clock default), ac (5: END `4160` `2430` `36D6`, MPS_LOGO `0030`, PLAYER
  `01F6`), ad (routes only: seven new routes and `tools/route_damage.py`), ae (the quiz option and its two routes; 58 routes / 57 sessions). Scripts:
  `D:/f117-gate/accept-kit/` (`new_batch.py LETTER PREV BASE_COMMIT [--no-baseline] DIR[:PREFIX[:partial]]...`: mark hand-back routines
  `partial`, give `--no-baseline` whenever `CMakeLists.txt` outside tests, `src/` or a route changes (the interpreter key moves and the old
  baseline is rejected at the end); `integrate_many.py DIR...` merges proofs and their FATAL_JUMPS/SHARED_ENDINGS rows; `finish_all.py
  DIR:ROUTE...` runs checkpoints, boot proof and verdict; `prefill_cache.py ROUTE...` refills the interpreter cache after a non-matched `src/`
  edit; `finalize.py` derives the instruction-state count from the gate log). Traps met on the way (each cost hours):
  agents miss harness hunks (an arm that joins code below its entry needs a SPANS `from` and a `g_cov_ip` line; PROGRAM_ENDS, POPF_STOPS and
  LIVE_FRAMES rows): diff each proof's `tests-base.c` against the repo's CURRENT `tests/func_lockstep.c`; a body must read a segment immediate
  from the code (`seg_read16`), never hard-code it (END `4160` passed lockstep and failed `landing`); **`matched.c` compiles with `/Os`**
  (at `/O2` MSVC took 90+ minutes from batch ab on; `CMakeLists.txt` sets it); never run more than two or three `cl.exe` at once;
  `kit checkpoints` fails with "result.json missing" when the interpreter cache lacks the current tree key.
  Not integrated: END `23D6` and `3682` (whole-function proofs that would replace existing fragment rows `st4_end_ribbon_layout` and
  `st4_end_debrief_switch_open`: remove those rows, fixtures and CTests when integrating), MPS_LOGO `01D3`/`01DA` (exit family, incomplete
  mutation set, `01DA` never entered), VGAME `0010` (main, declined), `12A11`, END `03D9` (fix D12 sites inside), `57B0`, MPS_LOGO `067C`.
- **Damaged-install routes and compressed boot (batch ad):** a route's `--damage` / `ACTION:FILE` lines build a private hard-linked damaged
  copy of the install (`tools/route_damage.py`, called from `route_args`; the install is never written). Routes `err_player_assert`,
  `err_player_divide`, `err_boot_series_palette`, `err_driver_overlay`, `err_dswap_nostart` run the original's own error handlers;
  `libya_tour_eject` and `korea_release_eject` use a compressed boot (Esc skips PLAYER's animations: START at 80M clocks, not 957M; keep one
  unskipped boot such as `boot_to_flight`). Coverage `D:/f117-gate/route-cover-20261020/`; the 128 rows they run that no routine covers yet
  (3,246 bytes) are in `D:/f117-gate/phase2-final/ran-rows.tsv`.
- **Quiz option (owner, 20 Oct: functional, off by default):** `--fix QUIZ` (`src/fixes/fixes.c` `fix_quiz`: an override at START `001D` pushes
  `0020` and runs the quiz routine `0703`, the call GOG patched to `90 90 90`); `--fix all` and normal play never switch it on, `--fix none`
  clears it; test `test_the_quiz_option_is_off_unless_named`. Routes `quiz_right` (clicks the A-10 shown: START `073A`) and `quiz_wrong` (`074B`),
  `# expect-open planes2.pic`; probes and screenshots in `D:/f117-gate/phase2-final/quiz/`. The quiz rows (START `0583`, `06E0`, `0703`, `072E`, `0788`) are credited since batch ah.
- **Batch al (21 Oct, accepted):** eight PLAYER routines (`1A7E` `18C4` `1476` `1839` `15BA` `223A` `026B:0244` `0010`; 495 bytes, 228 instructions,
  255 mutations; table in `docs/roadmap.md`); 427 CTests, gate 1,579 s, acceptance 1,549 s. Evidence `D:/f117-gate/p2-batch-al-accept/` (its
  `audit_private.py` is the newest: copy it forward). Pre-checks used again; first-attempt pass.
- **Batch ak (21 Oct, accepted):** DSWAP `023E` `03A7` `0AD5` `0BCF`, MPS_LOGO `0006:0044` `0006:000C` `0146:01D3`, END `3B9A` `03D9` (875 bytes, 379
  instructions, 283 mutations; table in `docs/roadmap.md`); 419 CTests, 63 routes / 62 sessions, gate 1,719 s, acceptance 1,606 s with overlap.
  Evidence `D:/f117-gate/p2-batch-ak-accept/`. Pre-checks used: the commit-keyed seed through `func_lockstep_par.py` before the gate. Batch-copy
  audit allowances: `NOBLOCKS` prefix in `batch.json` for bodies without `XB`, and `audit_private.py` accepts a hook collision when a covering
  `fix_inside` guard is in the body (copy both from `p2-batch-ak-accept/` into the next batch directory, `new_batch.py` copies stock ones).
  Not integrated: PLAYER ran-row proofs still in progress (`p2-player-vj-*`); MPS_LOGO `_exit` `01DA`/`163A` and `27C2` (no route enters them).
- **Batch aj (20 Oct, accepted):** DSWAP `0754`, MPS_LOGO `1364`, VGAME `0CDD`, END `41CA` (277 bytes, 116 instructions, 122 mutations; table in
  `docs/roadmap.md`); 410 CTests, 63 routes / 62 sessions, gate 1,770 s, acceptance 1,753 s with overlap (keyed baseline for 63 routes frozen,
  so `new_batch.py` needs no `--no-baseline` again unless `CMakeLists.txt` outside tests, non-matched `src/` or a route changes). Evidence
  `D:/f117-gate/p2-batch-aj-accept/`. **Cheap pre-checks before a gate (adopted 20 Oct):** (1) the commit-keyed seed
  (`py tools/build_recomp.py` prints it; it is the first 12 hex digits of HEAD) through `tools/func_lockstep_par.py --jobs 8 --states 4000
  --verbose --seed SEED` (about 4 minutes) catches a thin fixture or an old argument-order bug; run it on the working tree before the gate and
  note that committing changes the seed; (2) each new route once with `--engine recomp`, grepping `[matched] OVERRUN`. A fixture must force each
  branch outcome in a fixed share of states, not leave it to chance.
- **Batch ai (20 Oct, accepted, routes only):** five new damaged-install routes (`err_start_no_world`, `err_start_terrain_files`,
  `err_vgame_no_photo`, `err_dswap_no_swap_palette`, `err_end_no_tass`; table in `tools/routes/README.md`): 63 routes / 62 sessions, 406 CTests,
  gate and acceptance 1,658 s, run without the keyed baseline (new routes), so no presentation overlap. They run 581 never-run census bytes that
  now wait for proofs (list: `D:/f117-gate/route-cover-20261020b/` against the census). Also the `D9E7` argument-order fix
  (`matched_scene_origin_stack_alias`), `NO_FLIGHT_ROUTES` in `tools/presentation_check.py`, and the VGAME `0EBEA` dead-row removal.
  Traps: a route that leaves a matched `getch` waiting trips gate step 9 (OVERRUN): type the key before the wait (`/tmp`-style probe:
  run the route with `--engine recomp` and grep `[matched] OVERRUN`); a rotating seed (7c) keyed by the commit hash can expose an old
  argument-order bug in any matched body: reproduce with `func_lockstep --states 4000 --only MOD:IP --seed SEED`. `finalize.py` now
  accepts any "declined" count (it grows when new routes add translated code). Evidence `D:/f117-gate/p2-batch-ai-accept/`; failed attempts
  are `...-attempt1-d9e7`, `...-attempt2-overrun`, `...-attempt3-overrun2`.
- **Batch ah (20 Oct, accepted):** START quiz routines `057E`/`0703`/`0786` (proved on `quiz_right`; the census rows `0583`, `06E0`, `072E`, `0788`
  are mid-instruction pieces of them), PLAYER `1BB6` (printf dispatch) and VGAME `1058:000A`/`1058:0012`; 328 instructions, 275 mutations,
  405 CTests, gate 1,783 s. Evidence `D:/f117-gate/p2-batch-ah-accept/` (attempt 1, with a duplicate VGAME `14C2:00B7`, is `...-attempt1-dup14cd7`).
  Traps: `kit-libs` were stale (a private build said `no fix QUIZ`): refresh them from the main build after changing `src/` (a copy of the old set is in
  `kit-libs-before-<commit>`); `kit.py integrate` refuses same-IP span aliases unless `KIT_ALLOW_ALIAS=1`, and then the new span goes in the
  segment-keyed `SPANS_SEG` table, not `SPANS`; an agent's "new" address can already be matched under another segment spelling
  (`14C2:00B7` = `1452:07B7`): compare linear addresses, the finalize tally catches it; agents do not run `--fix` routes, the root does.
  Open: MPS_LOGO `27C2` and `163A` (no route call found).
- **Batch ag (20 Oct, accepted):** 14 routines from the ran-rows list (table in `docs/roadmap.md`; 645 named bytes,
  313 covered instructions, 341 caught mutations, 70 optimized seeds); 399 CTests, 58 routes / 57 sessions, gate 1,604 s,
  acceptance 1,731 s. Evidence `D:/f117-gate/p2-batch-ag-accept/`. Traps met: `integrate_many.py` carries only FATAL_JUMPS and
  SHARED_ENDINGS rows: SPANS, IMAGE_PATCHES, LIVE_FRAMES, `patch_image_for` lines and `run_to_ret` hunks in each proof's
  `harness-hunks.diff` / `tests-base.c` must be applied by hand (three CTests failed until the SPANS rows went in);
  `finalize.py`/`audit_private.py` need a block-macro prefix (`NOBLOCKS` in batch.json for bodies without `XB`), and mark
  hand-back or `XN`/computed-count bodies `partial`. Not integrated (proofs private-ready or needing work): MPS_LOGO `27C2`, `163A`
  (no route call found), VGAME `14C2:00B7`, `1058:000A`, `1058:0012` (SPANS is keyed by IP: add a segment key first).
- **Batch af (20 Oct, accepted):** 23 saved drafts: five DSWAP, seven END, three PLAYER, SETUP's DOS printer,
  three START hooks and four VGAME entries (table in `docs/roadmap.md`). 363 fully covered instructions,
  550 caught mutations, 115 optimized seed checks; 385 CTests, 58 identical routes / 57 fresh interpreter
  sessions, both timing profiles, all three full matched seeds and event limits pass. Gate 1,555 s, complete
  acceptance 1,689 s, presentation overlap valid. Evidence `D:/f117-gate/p2-batch-af-accept/`.
  The raw audit adds 690 distinct bytes against the older route set; current captures add DSWAP `111E`/`111F`,
  and five overlapping census positions give 697 census credits from 692 distinct bytes. Same prior registry
  refreshed alone gains zero; `credit-reconciliation.json` records this without changing the scoring method.
  Translation now uses up to eighteen module processes and one relocation mask per module: whole-install output
  is byte-identical to the frozen serial generator, with and without coverage; 51.23 to 5.20 s with coverage
  (`D:/f117-gate/translate-parallel-20261020/verdict.json`). CI's scheduling test now uses a synthetic install.
  Deferred: PLAYER `0010` (failing baseline; mutation catches invalid), DSWAP `03A7` (unclamped nested calls),
  MPS_LOGO `0146:15FA` (needs detailed room/caller review). Do not integrate these on mutation counts alone.
- **Never-run code (research done 19 Oct, `D:/f117-gate/neverrun-research/`):** of 12,972 never-run census bytes,
  5,201 are dead, 3,657 error paths, 2,321 player-reachable, 1,306 setup/hardware, 487 unknown; most of the
  player-reachable part is now credited (batch aa). Left from that research: the `ejection` route (END's rescue page
  `3B9A`, built, not adopted), career thresholds (a hand-made roster), the ILS panel's last 23 bytes
  (`97E9`..`97FF`: needs an approach that enters the ILS window), `0CB64`, `0CA1A`, `073C8`'s second lock slot.
  Owner decision (19 Oct): dead code leaves the denominator only when certain: `docs/census-dead.tsv`, 67 rows /
  2,666 bytes; error paths stay in and are run on damaged-install routes (below); the START quiz is an option
  (`--fix QUIZ`), not dead code (owner, 20 Oct).
- **Stage 3/4 (owner approved 18 Oct):** Stage 4 is done at 1.0 for what can be tested here: hidden capacity
  measurements plus the announced 24-minute fullscreen landing run (p99 16.95 ms, zero missed; docs/presentation.md);
  Stage 3 is closed at 1.0 (19 Oct): clock default 0.4 / 0.025 adopted, slower machines measured, the owner's A/B looked the same.
  Hardware-only parts are a Phase 4 maybe.
- **Normal-play defaults (owner, 18 Oct):** **15 Hz / 60 MIPS**, all fifteen
  fixes on. S15, 15 normal / 30 compressed updates, phase-correct transitions,
  recorded per-machine cadence; legacy recordings keep eight. Do not promote
  15/40 (fails active compressed-flight capacity). The previous mode is
  `--sim-hz 8 --ips 20000000`. Validation and limits:
  [docs/cadence15-validation.md](docs/cadence15-validation.md). Core, API and
  headless runs start with fixes off and parity commands prepend `--fix none`;
  `--fix none` clears them and further IDs select a subset.
- **Phase 3 (100.00%, 19 Oct):** Stage 3 closed at 1.0: the 0.4 / 0.025 clock default is adopted and measured (hidden replays, 8/4/2/1 cores, simulation plateau) and the owner's A/B looked the same. The `DF03`/`C2C8` group is fixed (`rerun.c` attitude spelling; gate, all 46
  routes in rerun mode and boot exact). Stage 2's open parts became a Phase 4 maybe; Stage 4 is complete for what
  can be tested here. Refresh rates above 60 Hz are unscored Phase 4. Details:
  [docs/progress.md](docs/progress.md), [docs/presentation.md](docs/presentation.md).

### Presentation facts worth keeping

- `--present rerun` (in-between poses drawn by the original's own code on a
  throwaway machine copy, smoothed playback clock) is integrated and gated;
  the owner's `4k-rerun.bat` uses it. Owner verdicts (15 Oct): motion on a real
  4K screen "looks good"; road and coastline jitter are the original's and are
  **not** smoothed; identity pairing is opt-in (`F117_PRESENT_IDENT=1`); draw
  distance is not extended; the high-resolution modes keep the original
  renderer's quirks.
- 18 Oct repairs, all in [docs/presentation.md](docs/presentation.md) and
  gated: override dispatch uses a sorted binary lookup (was two linear scans,
  46% of main-thread samples); the observer copies wrap-safe contiguous rows
  and skips unchanged 64-byte blocks (`test_observer_rows`; also fixed a
  span-record overflow at 220 rows); the playback clock keeps the first usable
  pair (no startup reversal); normal-width strokes are clipped to the original
  endpoint cells (map-edge specks). 4K60 fullscreen passes unchanged limits
  (p99 16.92 ms, zero intervals over 25 ms).
- Baselines every presentation change must keep: all 58 N=1 routes 260,480
  exact frames / 260,396 pairs (259,741 / 259,661 over the earlier 49; 259,240 / 259,166 over the original 45); boot N=4/9 736 frames / 732 pairs (two runs);
  the owner's complete N=9 flight 69 checkpoints / final `31c259b992340414`.
- Verification scope: `drawfeed.c/.h` and `rerun.c/.h` execute or snapshot
  machines, so they need the full gate plus
  `presentation_check.py --mode rerun` over all 45 routes (the light check
  refuses them). App-only changes (`src/host/main.c`, example inis) use
  `present_light_check.py` with `--app-session` / `--app-exit-after`: the
  headless gate never runs the threaded app path (a frozen palette once passed
  everything).
- Original-hardware references for cadence: 486/ET4000 reaches 16.47/16.57
  updates/s with S about 13; 86Box 386DX25/33/40 gives clock ratios
  1.306/1.272/1.167 at S=4/5/6, so no exact common multiplier. These support
  fifteen as an attainable light-scene rate, not recovered developer intent.
  Evidence under `D:/f117-gate/{clock486*,clock-matrix}-20261017/`, summary in
  [docs/speed-sweep.md](docs/speed-sweep.md).

## Owner decisions and instructions in force

- **Phase 2 first**; safe optimisation and parallelisation are authorised, but
  no check may be removed or weakened. Declined: splitting the long
  interpreter run into checkpoint segments, dropping `--verify-interp-cache`,
  silently changing the default overlap of eight workers (a 16-worker trial
  showed no speed gain). The private kit writes generated files only when the
  bytes differ.
- **Parity target is the original on its own hardware, not DOSBox**; do not copy
  identified DOSBox quirks; label behaviours by evidence. Player actions play
  out as on the original; emulator- and pilot-only work is settled.
- **Gameplay and logic parity outrank rendering.** Independent rendering must
  preserve original control reads and simulation timing. Do not silently change
  defaults or the owner's configuration.
- **Keep all original graphics** (fonts, sprites, the map arrow); smooth and
  re-render existing geometry, no vector replacements.
- **Bug fixes are on by default for normal play** (all fifteen, including
  D1REAL and D1PROX, which are project choices, not recovered developer
  targets). Parity gates keep the original reference.
- **No screen takeover.** Ask before any visible/fullscreen test and finish
  hidden workers first; the owner stopped a visible run once. Emulators run
  headless, silent, hidden.
- The owner's untracked `4k60.bat`, `4k-rerun.bat` and `4k-sharp.bat` stay
  untouched: do not overwrite or stage them.
- **Machine-option-only error paths stay unscored original code (owner, 20 Oct):** out-of-memory arms, DOS write/read/close errors with the
  carry flag set, the `_C_FILE_INFO` environment arm and the like (about 2,500 bytes) are not worth test-only machine options (smaller memory,
  injected DOS failures, extra environment variables). Do not build such options; do not count these bytes as work left. They are not certified
  dead either (the "only when certain" rule stands), so they stay in the denominator.
- The roadmap goal stays active: keep going item by item; ask only on policy.

## Next steps

Goal in force: finish Phase 2 (90.04% now; see Current state). Everything below is private work under `D:/f117-gate/` plus repo edits that wait for a gate.

1. **Batch af is accepted; next is ag.** The approved gate changes are implemented: parallel translation has the byte-identity proof;
   no proof worker or unrelated heavy job runs during a gate; the keyed baseline (af's, 58 routes) is stale since batch ai added five routes: the next matched batch runs `new_batch.py ... --no-baseline` once and freezes a new one for 63 routes / 62 sessions.
   Keep it valid by running matched batches back to back without changing `CMakeLists.txt` outside tests, non-matched `src/`, or routes.
   `new_batch.py ag af <HEAD> DIR...` can reuse it. The wrapper stores each alias route's own key (milestones can differ within one session),
   validates grouping and all fresh results, and overlaps presentation even when the gate validates its unchanged coverage record.
   Do not split or shorten routes, drop cache verification, or change the default eight overlap workers. Splitting `matched.c` stays an
   optional large refactor only if compilation becomes a problem again.
2. **Continue the newly runnable rows** (original lead list: 128 rows / 3,246 bytes, `D:/f117-gate/phase2-final/ran-rows.tsv`; af's 23 entries are done).
   Saved drafts are in `D:/f117-gate/p2-{dswap,mpslogo,player,end,setup,vgame,start}-v?-*`. Review every body, fixture, seed log and mutation
   against `kit/BRIEF.md` and `D:/f117-gate/ac_brief_extra.txt`; many remaining drafts have failed or stale evidence. The kit now refuses
   mutations/verdicts without five current passing seed logs and current mutation results, and refuses automatic integration where SPANS
   would alias another segment. Original group masks were 0x0000F000, 0x000F0000, 0x00F00000, 0x0F000000, 0xF0000000; use at most two or
   three compilers in all. Complete private proofs before the gate. Then `finish_all.py DIR:ROUTE` (cap its six-worker default to two or three
   when it compiles), `integrate_many.py`, local build and new CTests, the `partial`-claim audit, full acceptance, score, docs and commit/push.
   The START quiz rows are done (batch ah).
3. **What is left after that (5,305 never-run bytes, `D:/f117-gate/phase2-final/classify-after-routes.txt`):** error paths no file damage can reach
   (out of memory, DOS write errors, other open errors, the `_C_FILE_INFO` environment arm: about 2,500 bytes; the agent's list is in
   `phase2-final/err-routes`); rows the dead certificates call PROBABLE (1,315 bytes, `phase2-final/dead-cert/summary.txt`: they need evidence, not a decision);
   player-reachable leftovers that need combat or very long careers (END icon kinds and death causes, VGAME `073C8` second lock slot, `0CB64`, `0CA1A`,
   career thresholds, `phase2-final/reach-routes/missing.txt`). The owner's rule: code leaves the denominator only when certain it is not needed
   (`docs/census-dead.tsv`, a certificate per row).
4. **Phase 4 stays unscored (owner, 20 Oct):** refresh rates above 60 Hz, the hardware-only parts, a real older CPU or GPU. Long-flight and
   loaded-machine tests were run hidden on 20 Oct (`docs/presentation.md`): 31.7 minutes of `cargo_return`, flat memory, no drift; a starved host
   slows the game and thins the in-between pictures without breaking anything.
5. **Branch housekeeping (reviewed 19 Oct):** merged branches are retained. Two unmerged branches were reviewed
   and not worth merging: `flight-input` is superseded (its VGAME `5852` and `683E` routines are on master as
   `vgame_air_traffic` and `vgame_projectiles`; its only unique piece, the draft generator's `RET n` fix,
   was cherry-picked with its test; the worktree `D:/f117-wt/span-emitter` holds an older uncommitted draft
   of `683E` that master has surpassed). `irq-divide-fault`: its span-clipper handler `084E` is on master
   (`117201c`); the rest is `e15c9ef`, the line clipper's handler `04DD`, lockstep-only because only a line
   longer than 32,767 units overflows its divide and every caller bounds its lines below that (a probe of
   146,153 clip divisions peaked at a quotient of 9,047), so a player cannot reach it and it stays in
   `docs/census-original.tsv`. Both branches and the span-emitter worktree were deleted on the owner's
   say-so (19 Oct); the commits (`e15c9ef`, `7aab7e5`) and the worktree's uncommitted diff are archived as
   patches in `D:/f117-gate/archive-branches/`. Stopped presentation WIP branches are history; do not merge
   them. Ask the owner before deleting anything.

## Left as original code, declined, or stopped

- STI-first timer handlers (VGAME `1C9B`, START `8D0B`, END `4BEF`: after STI the
  run loop admits one more instruction), the C start-up (START `8EDC` and six
  copies: stops after its own STI, never returns; fixes D4 and D34 hook two of
  those entries), START `8427`, END `4393`, DSWAP `3DF3` (data, no generated
  entry). Recorded in `D:/f117-gate/kit/DECLINED.tsv` (also PLAYER `2853`, PLAYER
  `0211`, VGAME `14C2:0000`, END `57B0`, START `1285`, interior arms END `3100`
  and `3078`). END `03D9` is stopped (fix D12 sites may lie inside).
- Unproven private drafts (VGAME `120A:0971`, END `0A98`): not implemented; do
  not conflate with `04DD`.

## How Phase 2 is done: the kit and parallel agents

`D:/f117-gate/kit/kit.py` runs a private proof for one routine (`init`, write
`body.c` / `fixture.inc` / `mutations.json`, `lock --cover`, `seeds`,
`mutations` (four edits at a time), `checkpoints`, `verdict`, `integrate`);
`kit/BRIEF.md` and `kit/AGENT_PROMPT.txt` are the agent brief with every trap
found so far. Agents do steps 1-7 only, in their own `p2-*` directory, every
command through `kit/pin.py MASK` (CPU set + BelowNormal) with
`mutations --jobs 2`. Finish proof workers before starting a gate; run no unrelated heavy job beside it.
Then:

1. Verify each `private-verdict.json` yourself (counts, mutations, source
   hashes, real-route calls); stop finished agents (TaskStop): they wake on
   their own timers and resend reports.
2. `kit.py checkpoints`, `kit/boot_proof.py` (`KIT_ROOT` at the gated worktree),
   `verdict`, `audit_proofs.py` (batch helper: `finish.sh DIR`). Then
   `kit.py DIR integrate` (set `KIT_ROOT` for a worktree) and apply any harness
   hunk the agent reports by hand.
3. Build, ctest, ONE full gate per batch, the 45-route `presentation_check
   --scales 1`, docs, commit, push. Keep `kit-libs` and the kit bases
   (`matched-base.c`, `tests-base.c`) fresh after each batch; freeze a keyed
   presentation baseline before integrating so the overlap wrapper
   (`overlap_accept.py`) keeps its overlap.

Gate batches in a separate worktree (`D:/f117-wt/gate-h`) when the owner is
testing the main tree's build; build it once with
`build.cmd -DF117R_BUILD_APP=OFF` first. Method details, screening and the
older proof method: [docs/repeated-processes.md](docs/repeated-processes.md).

## How this project verifies Phase 2 work

A routine counts as matched only after BOTH:

1. **Fixture/lockstep proof:** `func_lockstep` at several seeds comparing the
   hand-written C with the original's bytes from randomized entry states (with
   returning driver stubs where the routine depends on them): zero mismatches,
   full instruction coverage, every deliberate mutation caught.
2. **Real-driver proof:** a scripted route (`tools/routes/*.args`) runs the
   routine with a positive count and every checkpoint and the final hash equal
   between engines. A routine no route reaches stays out of `master` (04DD).

Every change to `src/matched/matched.c` needs a fresh full gate
(`py tools/build_recomp.py --data D:/GOG/F-117A --seed-coverage
C:/Users/Tideg/f117-recomp-local/coverage --jobs 16 --verify-interp-cache`, via
`D:/f117-gate/affinity_launch.py`; about 22-30 min) before it is pushed. Read
the tally (`N ok, 0 failed`), not the exit code. Check GitHub CI after pushing.

## Lockstep harness: misaligned fuzzed returns (fixed 19 Oct)

A rotating seed (`0x904ceaac77b9`) failed step 7c with `projectiles: 1 calls escaped their return`
(VGAME `683E`, state 1799). A fuzzed write had altered the code of the `abs` helper (`EE0C`), so the
original decoded on misaligned and reached a `ret 0xC22B` that was really the tail of `xor ax,dx`: a
return at the wrong stack depth whose own bytes were pristine, which `changed_return` (bytes of the RET
only) could not see. `tests/func_lockstep.c` now marks a side dirty when it executes an instruction whose
first byte differs from the pristine image, and from then on every RET takes the changed-return path
(accepted only through the exact original state recorded at that RET; no named-region credit); the
record table is 4,096 entries. Old and new harness agree on all 1,016 routines at three seeds except this
state. Permanent CTest: `matched_projectile_misaligned_return`.

## Traps

Matching and the gate:
- `func_lockstep --only MOD.EXE:HEX`: the IP has NO leading zeros (`D0C`, not
  `0D0C`) or it silently compares 0 states. "Not testable here (routes only)"
  means only a real route can prove it.
- The commit-derived seed rotates with HEAD and can fail an untouched routine;
  reproduce with `func_lockstep --seed S --only MOD:IP` or `--shard K/8`. A lone
  `1 mismatching` with every line `equal` is a coverage floor: reproduce with
  `--only` and `F117_COVER`. Run every new body at four or five seeds.
- Charge the clock between every port access, stop right after each STI, and
  prove a body on `ibm_boot` first (`F117R_MATCHED_LIMIT=N py tools/run_route.py
  tools/routes/ibm_boot.args --engine recomp`, 22 s).
- A wait inside a matched routine must clamp `m->run_until` to `c->stop_at`;
  step 9 of the gate (event limits) checks routines that guest-call code that
  can wait. Check every callee's RET form.
- Matched routines with a switchable fix site after their entry decline while
  that fix is enabled (`fix_inside`); check `src/fixes/fixes.c` for sites inside.
- `1450` is also the presentation observer's frame-phase hook: at one address
  only the first override runs, so a matched body there must call
  `observe_game_draw` itself and go on `deliberate` in `tests/test_overrides.c`;
  `kit.py integrate` refuses an address with an observer.
- The coverage pass runs for every commit that adds a matched routine. Cache
  keys include raw source bytes (CRLF vs LF changes them); use `KIT_ROOT` at the
  gated worktree, never rename old cache metadata. `kit.py checkpoints` needs the
  interpreter cache entry for the current source tree: after changing anything
  under `src/` other than `matched.c`, run a gate first.
- Use `recon_career`, not its alias `recon_return`, for the keyed baseline.
- Reach: the gate's recomp `.cov` files hold only interpreted fallback
  instructions, so absence proves nothing; use a complete interpreter capture
  (`D:/f117-gate/p2-batch-d-accept/allroutes/`, `tools/census_reach.py`). Census
  rows with no region are data or interior entries routes run.
- SETUP's INT 10h at `14EF` runs two native callback/stub clocks atomically
  (`native_stub_time`): an SDL one-instruction alert there is not extra execution.
- `build.cmd` must run from PowerShell (through bash it hangs); it logs in the
  checkout's build directory.
- Gate speed: keep `--jobs 16` (24 was not faster); unit tests use up to eight
  workers. Coverage and parity are each as long as the `career_promotion` seed
  chain.

Presentation:
- Never force `F117R_OBSERVE_PAGES=1` on live presentation (it reseeds every
  phase and prevents pairs).
- Short windows with a cached map can show only the original bitmap.
- A timing capture that changes display mid-run logs two display lines and the
  checker rejects it: rerun with nothing else going.
- `presentation_check.py`: fresh baseline scan runs, canonical aliases, a fresh
  output save per run. Original-execution harness return traps use a self-loop;
  standalone CPU stepping does not stop at HLT.

## Where things are

- Game install: `D:/GOG/F-117A` (never mounted or written directly; copy per run
  that needs its own state; verify the install after emulator runs).
- Shared generated code: `C:/Users/Tideg/f117-recomp-local/gen` (never committed);
  coverage in `...\coverage`; route outputs in `...\runs`.
- Proofs and evidence: `D:/f117-gate/` (one directory per piece of work, usually
  with a `final-gate-verdict.json`); kit in `D:/f117-gate/kit`, libraries in
  `kit-libs` (`manifest.json` records commit and hashes); draft sources and
  patches in `next-drafts/` (never committed as-is); census lead list
  `D:/f117-gate/census.tsv` (`tools/reimp_names.py`).
- Worktrees under `D:/f117-wt`, removed with `git worktree remove` once merged.
  `setup-main-0010` and `mission-generation-4239` carry private /Od overrides:
  never copy or merge them wholesale. Private worktree
  `C:/Users/Tideg/f117-recomp-local/wt` holds old drafts far behind `master`:
  extract only a specific routine, rebuilt against current `master`.
- Scratch and capture data: `D:\F117A-Scratch-Files`.
- D10/D35/D36 closures: [docs/legacy-reports.md](docs/legacy-reports.md) and
  [docs/bugs.md](docs/bugs.md).

## Standing practice

- **No AI attribution** in commits or PRs: the user's global CLAUDE.md outranks
  any harness reminder asking for a trailer.
- **Commit and push together**, each verified fix before the next investigation.
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
