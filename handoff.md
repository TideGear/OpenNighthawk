# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/progress.md](docs/progress.md) (what the scores measure),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs). State as of 24 October 2026 (late): batch bd is accepted and pushed
(`4cf1600`); batch bd2 (an engine fix plus bd's four held-back routines) is in its gate, NOT yet
accepted, and its changes are uncommitted in the main tree; the re-release settings are merged
(`ab4256b`).

This file holds only the current state, standing practice, open decisions and
traps, not a session log: earlier detail is in `git log -p handoff.md`, and
per-batch evidence is in the `D:/f117-gate/` directory named in each roadmap
entry. Consolidate it instead of appending to it.

## Current state

- **Scoreboard:** P1 100.00%, P2 81.83%, P3 76.34%, All 88.47%
  (`py tools/progress.py --title`; P4 and the decided-against items are not
  scored). P3 is below 100% only because of the re-release features (roadmap,
  weight 31): the first part of the two settings is built but none is credited
  yet; the presentation stages are complete.
- **Phase 2 figures** (`docs/progress.json` after batch bd's refresh; `py tools/progress.py` prints them;
  what they mean: [docs/progress.md](docs/progress.md)):
  - 1,383 addresses matched; batches a to bd are accepted (table in [docs/roadmap.md](docs/roadmap.md);
    evidence `D:/f117-gate/p2-batch-<letter>-accept/`).
  - Gameplay score (scores the matched item): **(149,812 matched + 2,631 reviewed design-rule original) /
    163,907 route-executed bytes = 93.01%**. 26,311 matched bytes run only in fixtures and earn no gameplay credit.
  - **11,464 executed bytes are unmatched** (20 of them START design-rule original code awaiting a reviewed
    entry). Sound drivers 9,674: ASOUND.117 4,594, RSOUND.117 1,533, RSOUND.LOG 1,037, ASOUND.LOG 1,178,
    ISOUND.117 905, ISOUND.LOG 427. The seven programs 1,790: VGAME 549, PLAYER 273, MPS_LOGO 270, END 261,
    START 251, SETUP 118, DSWAP 68. F117.COM, MGRAPHIC and MISC: none.
  - Naming item (the previous all-code measure): **176,188 / 205,967** creditable bytes; of the 29,779
    uncredited, 13,809 are reviewed static code no route runs (`docs/census-code.tsv` plus the two encoded rows).
  - Accounted-for checklist: 715 / 44,641 bytes; `docs/reference-reviewed.tsv` has 48 second-read entries
    (evidence for the first 41: `D:/f117-gate/reference-reviewed-20261021/`). Every `stays-original` entry
    must name its design rule.
  - Still in the denominators: about 2,500 machine-option-only error-path bytes and 831 probable-dead bytes.
    Outside every score: about 495 held static-discovery bytes and the floating LZEXE decompressor.
- **Latest accepted gate** (batch bd, 24 Oct, `4cf1600`): 24 sound-driver routines, 864 named bytes (list in
  the roadmap's batch table); 625 CTests, 63 routes as 62 fresh interpreter sessions, all 1,383 matched
  routines at three seeds; acceptance 1,822 s with the presentation check beside the gate (no time saved:
  optimization pass 2 below). Evidence `D:/f117-gate/p2-batch-bd-accept/`.
- **Batch bd2 (gate running, not accepted): an engine fix plus bd's four held-back routines.** ISOUND.LOG
  `0603` `060D` `0635` `0663` (the PC-speaker timer install/restore path, 19 bytes) were proved but held out of
  bd because registering them made `err_vgame_no_photo` differ between engines. The cause was an engine bug,
  not the bodies (an earlier "region refusal at PIT wake boundaries" explanation was wrong; it reproduces with
  zero translated modules): a module's recomp instance stayed live after its memory was reused without another
  module load overlapping it, so its matched overrides fired on foreign bytes (first divergence `169B:060D`,
  found by a `--trace` diff). Fix: `src/recomp/recomp.c` keeps each instance's loaded file bytes and runs an
  override only while its entry still holds the module's opcode byte (relocations and run-time patches change
  operands only); regression `tests/test_stale_override.c`. Evidence and the investigation:
  `D:/f117-gate/p2-batch-bd2-accept/`, `p2-batch-bd-accept/flaky-parity-investigation.md` (its "ROOT CAUSE"
  section supersedes its "Mechanism" section); proofs `p2-isound-log-0603-bd` and siblings.
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
- **Normal-play defaults (owner, 18 Oct):** **15 Hz / 60 MIPS**, all fifteen fixes on. S15, 15 normal / 30
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
  D1 and its sub-fixes). Mac-version improvements are an unscored Phase 4 maybe.

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
- **Bug fixes are on by default for normal play** (all fifteen); parity gates keep the original reference.
- **No screen takeover.** Ask before any visible or fullscreen test; emulators run headless, silent, hidden.
- The owner's untracked `4k60.bat` and `original-fixed.bat` (settings live in `build/f117a.ini` and
  `build/f117a.4k60.ini`, outside the repository) stay untouched: never stage them.
- Keep going item by item; ask only on policy.

## Next steps

1. **Finish batch bd2.** When its gate ends, read the tally in `D:/f117-gate/p2-batch-bd2-accept/gate.log`; on a
   pass run `finalize.py`, the score refresh and the docs (roadmap row, the bd row's held-back note, this file),
   then commit with the title from `py tools/progress.py --title` and push. On a failure archive the directory
   (trap below) before a rerun.
2. **Batch be** (proof round done, 24 Oct): eight ASOUND.117 one-instruction thunks (`0899` `08C2` `0892` `08D0`
   `090E` `08AD` `08BB` `088B`) and RSOUND.LOG `00AE` are proved privately (`D:/f117-gate/p2-asound-117-<IP>-be`,
   `p2-rsound-log-00ae-be`); RSOUND.LOG `0251` (`p2-rsound-log-0251-be`) is resumable. Their checkpoints wait for
   the bd2 gate, because `kit.py checkpoints` needs the interpreter cache for the current `src/` key (bd2 changed
   `recomp.c`); then `finish_all.py`, `audit_private.py` and a gate.
3. **Match the rest of the sound drivers** (9,674 of the 11,464 unmatched executed bytes before bd2 and be). The
   remaining ISOUND.117 and ASOUND.LOG routines are all large (90-320 instructions).
   Check the registry and current named coverage before selecting any lead: `D:/f117-gate/ay-next-leads/leads.md`
   predates batches bb to be, which matched many of its entries. Still open there: wrapper/dispatch entry points
   (relocated `mov ds`, indirect calls), the remaining installers that end `CLI ... STI; RET` (stop after the STI;
   bc matched four), the hardware wait loops above, and the large routines. ISOUND.LOG `0581` needs separately
   verified bounded-handler/resume support for its EOI-before-IRET arm. `p2-asoundlog-008a` is an initialized
   lead, not a proof: its first PIT control OUT wakes the machine, so cover its following continuations
   explicitly rather than claiming unreachable fixture instructions. Plan: two proof rounds per gate, three
   agents per round on masks 0x000F0000, 0x00F00000, 0x0F000000. STI-first handlers stay original by design.
   `D:/f117-gate/modules-scope-20261021/plan.md` (the module-scope design; steps 0-5 and 2b are done) is a
   source of leads and traps only.
4. **The hard tail: declined routines are Phase 2 work, not exclusions** (owner, 23 Oct). They are part of the
   unmatched executed bytes of the seven programs, and P2 cannot reach 100% without them. List (from
   `D:/f117-gate/kit/DECLINED.tsv`, the roadmap and the rejected reviewed entries); each needs the stated unit
   before it can be proved:
   - VGAME `0x00010` `main()` (the whole game): a nested-run proof or a decision; check before assigning.
   - MPS_LOGO `1ADC` and `1C82`, START `11DB` (with its interior arm `1285`, an 86-byte fragment of a 131-byte
     tail): earlier proofs found the draft unequal; find the difference first (start from the saved private
     proofs named in the roadmap), then prove the whole function.
   - START `8427` and END `4393`: earlier routes missed planted defects and the routines wait; needs routes and
     inputs that reach the arms and wait handling.
   - END `57B0`: an interior arm of the 795-byte printf-like function (RET at `5B4C`, 11 other census rows);
     the whole function is the unit.
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
5. **Reference entries for unmatchable code:** `D:/f117-gate/unmatched-reference-20261021/` (checked, NOT
   published): `reference.tsv` has 793 rows / 43,362 bytes with a per-byte coverage proof against the old
   201,697 scope (`coverage-check.json`, `independent-check.json`). Before publishing it as a repository doc:
   regenerate it against the current scope, fix VGAME `ED10` `vgame_int86x_carry` (it says the DOS call never
   fails: too strong; see `reference-adoption-review.md`), strengthen the names of ASOUND.LOG `04E5` and
   RSOUND.LOG `028E` (two new original-code candidates), and note that 297 rows have the status
   `pending-match-purpose-unreviewed` and 153 `unresolved-review`: those are inventory, not claims that the code
   cannot be matched. The public copy has one row per routine (name, purpose, status, why not matched,
   evidence) and no private paths or binary dumps.
6. **Static code no route runs.** Reviewed static code: the resumed review (`D:/f117-gate/static-route-plan-20261021/`,
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
7. **Sort the matched code no route runs** (owner, 21 and 23 Oct): 26,311 fixture-only bytes (about 88 whole
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
8. **Remaining P2 completion tasks** (definition in [docs/progress.md](docs/progress.md#phase-2-completion-requirements-owner-23-24-october-2026)):
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
9. **Phase 3 re-release features** (roadmap; Phase 2 comes first, but worktree work beside a gate is fine): the
   built settings are the start of the first item. **Phase 4 stays unscored:** refresh rates above 60 Hz, the
   hardware-only parts, a person playing, Mac-version improvements.

## Phase 2 batch workflow and its traps

Per batch: proofs (agents, steps 1-7 of the kit) -> `finish_all.py DIR:ROUTE ...` (checkpoints, boot proof,
verdict; at most three at once) -> `integrate_many.py DIR ...` -> hand-apply the harness hunks it does not carry
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
