# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs). State as of 24 October 2026 (batch bc accepted; census refreshed; committed locally, not yet published).

This file holds only the current state, standing practice and traps, not a
session log: earlier detail is in `git log -p handoff.md`, and per-batch
evidence is in the `D:/f117-gate/` directory named in each roadmap entry.
Consolidate it instead of appending to it.

## Current state

- **Scoreboard:** P1 100.00%, P2 81.41%, P3 100.00%, All 94.33%
  (`py tools/progress.py --title`; P4 and the decided-against P3 items are not
  scored).
- **Phase 2:** 1,359 addresses matched. Gameplay score (owner, 21 Oct; scores the matched item): **148,948 /
  163,907 route-executed bytes plus 2,631 reviewed design-rule original bytes, 92.48%**. `docs/reference-reviewed.tsv` holds 41
  second-read entries (the C start-up copies, the STI-first timer and keyboard handlers, the STI palette loaders; evidence
  `D:/f117-gate/reference-reviewed-20261021/`). Rejected by both readers and now remaining work: PLAYER `02853` (an interrupt handler
  whose STI is its 13th instruction, not STI-first) and `028C1` (a retrace re-sync), both taken off `docs/census-original.tsv`, and
  START `08F94` (the run time's divide-error entry, inside the C start-up row). The scoring check now requires every
  stays-original entry to name its design rule. The accounted-for checklist is 585 / 44,654. Previous
  all-code measure (scores the naming item): **175,324 / 205,967 creditable bytes** across
  all 17 game code files (credit by byte; rules in [docs/progress.md](docs/progress.md)).
  Batches a to bc are accepted (table in [docs/roadmap.md](docs/roadmap.md); evidence
  `D:/f117-gate/p2-batch-<letter>-accept/`). Latest gate: 597 CTests, 63 routes run as 62 sessions,
  5,723,136 instruction states per timing profile, all matched routines at three seeds.
- **Remaining work:** 12,819 route-executed bytes are unmatched; the all-code measure has 31,586 uncredited bytes.
  Largest executed groups: ASOUND.117 4,662, ASOUND.LOG 1,226, RSOUND.117 1,778, RSOUND.LOG 1,574,
  ISOUND.117 905, ISOUND.LOG 563, F117.COM 249, VGAME 549, PLAYER 268, END 261, MPS_LOGO 255 and START 251.
  There are 13,817 uncredited reviewed static bytes that no route runs (`docs/census-code.tsv` plus the two encoded rows), about 2,500
  machine-option-only error-path bytes and 831 probable-dead bytes still retained. Outside the score: about 495
  held static-discovery bytes (the remaining plain-code/data follow-up rows) and the floating LZEXE decompressor.
  Refresh using batch ax's `named-cover` and `D:/f117-gate/route-cover-20261020b`.
- **What P2 100% still requires:** 10,708 unmatched executed bytes are in the six sound drivers, 249 in F117.COM outside the launcher (lead: executed bytes of the `03CB` loader region the named cover does not claim),
  and 1,862 in the seven ordinary programs. The previous all-code measure also has unexecuted static/error-path work: no byte leaves
  scope without its existing design rule or a strict dead certificate. Old private mapping counts are leads, not proofs or current
  inventories. Reviewed-original credit still requires a second reader; do not convert declined routines into exclusions.
- **Batch ax accepted (23 Oct).** Nineteen leaf routines: START cel steps, ticks, mouse position, flush count and free-or-exit;
  END ticks; MISC key input; DSWAP flush count, current drive and empty picture hook; SETUP calibration counters; MPS_LOGO video mode,
  exit checksum and formatter readers; PLAYER picture close/end flag and vector get/set. All 141 instructions covered, 344 mutants
  caught, 95 optimized seed checks. Added 271 named bytes: 230 executed, 41 fixture-only. Gate: 528 CTests, 63 routes / 62 fresh
  interpreter sessions, both timing profiles, event limits and all routes-only entries; presentation N=1 all routes and boot N=4/9.
  Core gate 1,584 s; acceptance 1,795 s. Evidence: `D:/f117-gate/p2-batch-ax-accept/final-gate-verdict.json` and `private-audit.json`.
  Six whole-table precheck seeds passed in 1,407.72 s (`D:/f117-gate/ax-pre/`); 1,266 old reports identical, two changes explained.
  Private audit verifies all 114 extra-seed logs, exact mutation names, freshness and boot/source hashes; explicit INT claims are now
  recognized by the generic accept-kit auditors without relaxing completeness, disjointness, clocks or real-route requirements.
  **Verification fixes:** unfinished continuations now reject already contradictory I/O, and a separate full interrupt-clock hash
  preserves synthetic DOS answers. The first precheck attempt then exposed a return-tracking gap: callees in END `53DA` and SETUP
  `1600` can consume extra stack words and return directly to the caller. The runner now recognizes only the exact completed
  original CPU/clock state, still compares all memory/I/O, and grants no named completion. All 19 ROM-free cases pass. Pinned
  controls/traces: `D:/f117-gate/harness-unfinished-io-20261022/` and `D:/f117-gate/ax-legacy-io-20261023/`. The latter includes
  `rng-review.json`: SETUP recovers states 366/509; the existing mid-run probe at 509 consumes rnd(), changing later inputs from
  state 510. All 4,000 iterations remain; comparison totals change from 649 to 631. END gains one comparison. Failed attempts remain
  archived, including `ax-pre-attempt-1-legacy-return/`. No game-logic defect is claimed by these harness repairs.
  START `2A7E` means cel-animation steps left; PLAYER `0BDC` is the stream-end flag (including the corrected old open comment).
  Unfinished private `p2-player-vc-0540` still has two missed mutants and only `err_driver_overlay` reach; do not integrate it as proved.
- **Relocation infrastructure locally accepted (23 Oct), no new credit.** `D:/f117-gate/p2-batch-loadbase-accept/final-gate-verdict.json`
  and `infrastructure-verdict.json` pin all ten changed code/test/CI files plus unchanged `matched.c`. Deterministic generated metadata
  identifies images by name/hash and lists relocation words; all 60 old generated C files remain identical. Both harness reload paths
  apply fixups, named coverage uses the relocated image, and the COM fixture uses actual CS. Production module ABI is unchanged.
  The nine launcher/overlay/driver modules default to 0800h; historical programs remain at zero. CLI controls permit zero or
  0400h..0800h, reserving code below fixed fixture RAM without changing its aliases. Generic loader tests still cover 1000h/3000h,
  word wrap, COM origin, malformed metadata/no writes, and a DS=0 mutant invisible at zero but caught at both nonzero bases.
  Fresh controls: all 1,287 zero-base reports exactly match ax; all 73 affected entries pass at both 0400h and 0800h with zero skips.
  Six whole-table seeds (4,000 states, eight shards, two rounds of three) passed in 1,530 s; all 1,214 historical-program reports
  are unchanged. Full gate: 529 CTests, 63 routes / 62 fresh interpreter sessions, both timing profiles, three matched seeds,
  routes-only reach and event limits; all N=1 presentation and boot N=4/9 exact. Core gate 1,369 s; sequential gate/presentation trial 1,955 s.
  Two fixture-layout failures remain archived in `D:/f117-gate/modules-loadbase-20261022/`: `attempt-1-code-ram-overlap/` (3000h
  code collides with page RAM) and `attempt-2-empty-page-table/` (MGRAPHIC 019C:02A1, seed 27182818h, state 574 writes through ES=0
  over relocated instruction 099C:067A). The latter fixture now initializes all five renderer page entries to disjoint buffers,
  rotating by state with no added random draws. This is a fixture repair, not a claimed game bug.
  **Closing-audit repair:** adding the CMake loader target invalidated the inherited interpreter key. The gate correctly fell back
  to sequential presentation against its fresh runs, but `group_verdict.py` still tried the unused old baseline. Its repaired path
  independently verifies the actual 62 fresh sessions and all presentation results; `closing-audit-recovery.json` preserves the
  original failure. The current-key baseline is promoted in this acceptance directory; the stale one is archived there. Generic
  `accept-kit/group_verdict.py` carries the same fix. The original pipeline log still records its closing-audit failure; the
  recovered `modules-loadbase-20261022/pipeline-verdict.json` and final verdicts are authoritative. Do not rerun the successful gate.
  **Publication/kit:** CI for the pushed revision (`adb6a42`) was green; the private kit libraries were refreshed to it on 23 Oct
  (`D:/f117-gate/kit-libs`, backup `kit-libs-before-adb6a42`, `manifest.json`).
- **Batch ay accepted (23 Oct): first sound-driver overlay routines.** RSOUND.LOG `021B`, ASOUND.LOG `0344` and ISOUND.LOG `0253`
  (details in the roadmap row). Evidence: `D:/f117-gate/p2-batch-ay-accept/final-gate-verdict.json` (532 CTests, 1,290 routines, 199
  mutations, acceptance 1,896 s), proofs `p2-rsound-log-021b`, `p2-asound-log-0344`, `p2-isound-log-0253`. `0344` and `0253` are
  `:partial` (they stop at the counter-0 control write and the original finishes). Three recurring traps: driver LOG routines can start
  below their entry (`g_cov_ip` lines in `tests/func_lockstep.c`, keyed by module and IP), a call counter stored in the code window
  needs a self-write exemption (ISOUND.LOG `0251`), and `integrate_many.py` carries neither. `finish_all.py DIR:ROUTE` must run before
  `audit_private.py` (the audit needs `ibm-boot-proof`; running `checkpoints` and `verdict` by hand is not enough).
- **Batches az and ba accepted (23 Oct): 13 and 19 routines per gate** (roadmap rows; evidence `D:/f117-gate/p2-batch-az-accept/` and
  `p2-batch-ba-accept/final-gate-verdict.json`: 564 CTests, 1,322 routines, 595 mutations, acceptance 1,991 s). Three agents, each taking a family of
  routines one after another (not one routine per agent), is the pattern to keep (owner, 23 Oct: safe improvements only, larger batches that save
  time overall are approved; no proof agents while the gate runs). The root runs `finish_all.py DIR:ROUTE` for every proof (it makes `ibm-boot-proof`
  that the audit needs), then `integrate_many.py`, hand-applies the harness hunks it does not carry (`g_cov_ip` lines for routines that begin below
  their entry, the self-write exemption for a call counter inside the code window), builds, runs the whole-table seeds, `new_batch.py`, the audit, the gate.
  **Defects fixed from the commit-keyed seed:** (1) START `28CE` read its four stack arguments up front; the original reads `count` after storing `x`,
  so a stack overlapping the data segment differed (read through BP in the original's order, with `bp_arg`, never `arg()` after `push bp`).
  (2) END `100D` checked `room(c, 8)` for a block followed by a CALL, but `guest_call` charges the CALL itself: a stop limit landing exactly there ran the
  CALL one instruction late (`OVERRUN ... +101 with the limit at +100`, `g_callover`). A block followed by a call needs `room(c, N + 1)` (new bodies claim the
  call separately with `SM4_NEED(1, ip)`). A scan of `matched.c` found 39 more bodies with the shape; all now reserve `+ 1` (it can only make a body
  decline sooner; many were false positives that claim the call separately, harmless). Do not run such a scan over bodies with a private proof:
  `finalize.py` requires each proof's `body.c` verbatim in `matched.c` (it failed once on ASOUND.LOG `019A`, restored). The class cannot be seen at
  ordinary seeds (the limit must land exactly on the boundary and the routine must complete in lockstep), so run two whole-table seeds before every gate.
  **Audit notes:** a body that hands back, calls original code or whose first room claim is an expression (`14 + 3 + IO_SLACK(2)`) is marked `partial`
  in `batch.json` (with a `note`); carry `p2-batch-az-accept/audit_private.py` forward (it also accepts lines between `if (!room(c, N)) return 0;` and
  `c->icount += N;`, never across a call). `new_batch.py` needs a fresh directory: a failed gate's `runner/` blocks a rerun, so archive the directory
  (`p2-batch-X-accept-run1`) and recreate it with `new_batch.py`, then restore `batch.json` and the two copied scripts.
  **Next:** ASOUND.LOG `0640` `06AA` and caller `0BE2`, the ASOUND.117 voice-reset group (body at `05E7`, a jump target: check that a matched entry can
  dispatch there first) and its `0E5E` family, the RSOUND.117 Roland voice-start family (`0EBD`..), RSOUND.117 `0444` `046D` `03D6`, wrapper/dispatch
  entry points (relocated `mov ds`, indirect calls), the installers that end `CLI ... STI; RET` (stop after the STI), and the large routines
  (`D:/f117-gate/ay-next-leads/leads.md`). ISOUND.LOG `0581` needs separately verified bounded-handler/resume support for its EOI-before-IRET arm.
  Plan: two proof rounds per gate, three agents per round on masks 0x000F0000, 0x00F00000, 0x0F000000.
- **Phase 2 scope work (21 Oct).** The interrupted analyses below remain private under
  `D:/f117-gate/`; adopt only what you verify. Module scope steps 0/1a/1b are now implemented:
  - `static-route-plan-20261021/` (route-planning lead) and `static-route-verify-20261021/` (resumed review DONE):
    no additional route identified for the 10,301 reviewed-static bytes. **No new dead certificate.** The review
    retains all 27 requested ranges / 6,505 bytes; indirect/callback/vector and writer closure is incomplete.
    The old claim that none can run is superseded: VGAME `1377:0204..0356` includes the M pixel routine `0287`,
    and the old MPS_LOGO pointer scan used the wrong DGROUP (`041A`, actual `0332`). Details and per-group
    decisions: [docs/static-code-review.md](docs/static-code-review.md); private `resumed-report.md` and
    `group-a-closure.md`. B/I remain held too; C needs declined floppy support or machine-option-only errors.
  - `static-review-20261021/` (DONE, independently audited): 117 code ranges and one 70-byte data range adopted on 21 Oct (`adoption-audit.md`);
    held for a strict validator: 20 follow-up data rows, the plain follow-up code rows (`followup-review-report.md`); encoded SETUP code is now adopted with strict validation.
  - `unmatched-reference-20261021/` (DONE, checked, NOT published): `reference.tsv` has 793 rows / 43,362 bytes with a per-byte coverage proof
    against the old 201,697 scope (`coverage-check.json`, `independent-check.json`). Before publishing it as a repository doc: regenerate it
    against the current 205,509 scope, fix VGAME `ED10` `vgame_int86x_carry` (it says the DOS call never fails: too strong; see
    `reference-adoption-review.md`), strengthen the names of ASOUND.LOG `04E5` and RSOUND.LOG `028E` (two new original-code candidates), and
    note that 297 rows have the status `pending-match-purpose-unreviewed` and 153 `unresolved-review`: those are inventory, not claims that
    the code cannot be matched.
  - `modules-scope-20261021/plan.md` (DONE): the ten-step design for bringing the launcher, MGRAPHIC, MISC and the sound drivers into the score
    (18,835 validated bytes; measured byte share 81.93%, weighted P2 82.84%). Steps 0/1a/1b done:
    `measurement.json` verifies per-module counts and captured-record rejection, and the scorer includes all 17
    game code files. Credit remains 165,256; no new dead/original exclusions. Scope validation: 427 standard
    CTests, 57 focused Python tests, 2,235,060 matched states / 0 mismatching (2,000 states, eight shards).
    `scope-verdict.json` retains the byte audit and test evidence. Step 2a (harness module keys: module-keyed rules, 37 `PLANTS` keyed by module, synthetic collision test `matched_module_rules`) is done: 428 CTests, gate 1,272 s, per-routine reports identical at four seeds for all 1,189 routines (`D:/f117-gate/modules-harness-keys-20261021/final-gate-verdict.json`; its `accept.py` failed only on its own text check and `finish_after_gate.py` completed the steps). Step 3 (private kit and accept-kit widened for .COM/.117/.LOG names, module tokens, overlay segments, default checkpoint routes, driver traps in `BRIEF.md`; `kit.py X defines` prints the nine `#define` lines for matched.c) is done and dry-run on MISC, MGRAPHIC, ASOUND.117, ASOUND.LOG and F117.COM (`D:/f117-gate/kit-widening-20261021/`). The step 4 pilot (batch `am`) is done too: MISC `0000` `000A` `0018` and MGRAPHIC `023A`, gate 1,586 s, 432 CTests; credit +114 bytes. Next steps: see Next steps, item 4.
  Owner decisions this session: bring these modules in scope; no floppy drive (DSWAP's disk-swap path stays original).
- **Dead code** (owner rule: only when certain): `docs/census-dead.tsv` (73 census rows, 2,927 bytes;
  certificates `D:/f117-gate/phase2-final/dead-cert/`, the 21 Oct evidence review `dead-review-20261021/`) and
  `docs/census-dead-ranges.tsv` (byte ranges that are not census rows: 868 bytes of SETUP and PLAYER jump slots
  no program binds or names, `slots-review-20261021/`, and 367 bytes of code no walk decodes,
  `unreached-review-20261021/`). Held back on weaker evidence: MPS_LOGO `00754` (a `LABSLOGO.SS` byte our
  damage routes can poke), `01B3C`, `02ECA` (single-writer arguments), VGAME `12E4B` `11CF4` `11DFF`, START
  `081C6`, MPS_LOGO `02ECF` (only "no reference found"), SETUP slot 47 at `02E51`; 12 rows / 831 bytes stay
  PROBABLE. `docs/census-data.tsv` gained 183 padding and table ranges (257 bytes) on 21 Oct.
- **Routes:** 63. Damaged-install routes (`--damage ACTION:FILE`, `tools/route_damage.py`: a private hard-linked
  copy; the install is never written) run the original's own error handlers: `err_player_assert`,
  `err_player_divide`, `err_boot_series_palette`, `err_driver_overlay`, `err_dswap_nostart`, `err_start_no_world`,
  `err_start_terrain_files`, `err_vgame_no_photo`, `err_dswap_no_swap_palette`, `err_end_no_tass`.
  `libya_tour_eject` and `korea_release_eject` use a compressed boot (Esc skips PLAYER's animations; keep one
  unskipped boot such as `boot_to_flight`). Full interpreter coverage of all 63: `D:/f117-gate/route-cover-20261020b/`.
- **Quiz option (owner, 20 Oct):** `--fix QUIZ` restores the aircraft-identification quiz GOG patched out (an
  override at START `001D` runs `0703`); off by default, `--fix all` and normal play never switch it on. Routes
  `quiz_right`/`quiz_wrong`; the quiz routines are matched (batch ah).
- **Normal-play defaults (owner, 18 Oct):** **15 Hz / 60 MIPS**, all fifteen fixes on. S15, 15 normal / 30
  compressed updates, recorded per-machine cadence; legacy recordings keep eight. Do not promote 15/40. The
  previous mode is `--sim-hz 8 --ips 20000000`. Validation: [docs/cadence15-validation.md](docs/cadence15-validation.md).
  Core, API and headless runs start with fixes off; parity commands prepend `--fix none`.
- **Phase 3 (100.00%):** Stage 3 closed 19 Oct (clock default 0.4 / 0.025, the owner's A/B looked the same);
  Stage 4 done for what can be tested here. Stage 2's open parts and the hardware-only parts are a Phase 4 maybe.

### Presentation facts worth keeping

- `--present rerun` (in-between poses drawn by the original's own code on a
  throwaway machine copy, smoothed playback clock) is integrated and gated;
  the owner's `4k-rerun.bat` uses it. Owner verdicts (15 Oct): motion on a real
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

- **No meaningful game code may be missed (21 Oct).** Approved: bring the launcher, the resident graphics
  library and the sound drivers into the Phase 2 scope (the percentage drops at first; accepted); a route
  campaign for the reviewed static code; a reference entry (name and one-line purpose) for every range that
  cannot be matched; a review of the unreviewed static-discovery bytes.
- **Phase 2 first**; safe optimisation and parallelisation are authorised, but no check may be removed or
  weakened. Declined: splitting the long interpreter run into checkpoint segments, dropping
  `--verify-interp-cache`, changing the default overlap of eight workers.
- **Program launcher stays original (23 Oct):** F-117.COM `0100`..`03CA` (715 bytes) is a fifth design rule ("program launcher"), reviewed by an
  independent second reader (no copy protection, no checks beyond child exit codes); the overlay loader `03CB` stays matched. Owner chose it over
  matching the launcher in nested-run proofs. **Hardware wait loops (owner, 23 Oct, option C):** a sixth design rule, "hardware wait loop", lets a wait or
  calibration loop stay original only if no lockstep state can complete it, each with a second reader; the STI installers (18 routines, about 1,280 bytes)
  and the OPL write primitives (8, about 361 bytes) must be matched. A first read (`D:/f117-gate/wait-loops-first-read/first-read.md`) found that none of
  the 8 candidate loops qualifies: ASOUND.LOG `00FE` `0140`, RSOUND.LOG `012D` `03A2`, RSOUND.117 `0AFE` `0D07`, ASOUND.117 `0A17` `0B01` can all be completed
  by a fixture (three as the harness stands, five once the callee that executes STI is stubbed, as SETUP `111A`'s fixture does). Match them; the rule has no users.
- **Dead code leaves the denominator only when certain** (19 Oct), with a certificate per row.
- **Machine-option-only error paths stay original code (20 Oct):** out-of-memory arms, DOS read/write/close
  errors, the `_C_FILE_INFO` environment arm (about 2,500 bytes). Do not build test-only machine options for
  them; they are not dead either, so they stay in the denominator (and get reference entries).
- **Parity target is the original on its own hardware, not DOSBox**; do not copy identified DOSBox quirks;
  label behaviours by evidence. Gameplay and logic parity outrank rendering.
- **Keep all original graphics** (fonts, sprites, the map arrow); smooth and re-render existing geometry, no
  vector replacements.
- **Bug fixes are on by default for normal play** (all fifteen); parity gates keep the original reference.
- **No screen takeover.** Ask before any visible or fullscreen test; emulators run headless, silent, hidden.
- The owner's untracked `4k60.bat`, `4k-rerun.bat` and `4k-sharp.bat` stay untouched: never stage them.
- Keep going item by item; ask only on policy.

## Next steps

**Publication pending:** scoring/encoded-review changes are verified in local commit `9478498`. Automatic approval review
rejected the combined commit/push because this chat has no direct external-publication authorization;
an approval question is pending. Do not retry publication until answered. All 74 focused tests pass,
the full census refresh succeeded, and commit 9478498 preserved the three batch-bb runtime source hashes.
The tooling/docs commit needed no full game gate; the installer changes below do require one. The owner's three untracked launchers are untouched.

**Installer family integrated locally (24 Oct, validation pending; no new credit yet):** `D:/f117-gate/p2-installers-review-20261024/`
has a passing copied batch-bb private audit and an additional literal-claim/source-hash review of four proofs:
`p2-asoundlog-005a` (48 bytes/16 instructions/26 mutations, full_cycle 66 exact checkpoints),
`p2-isoundlog-060e` (39/14/22, ibm_boot 26), `p2-rsoundlog-005b` (82/26/26, roland_boot 26),
and `p2-rsoundlog-00ad` (1/1/6, roland_boot 26). Each has five kit seeds plus fresh 31415926,
zero skips, complete instruction coverage, every mutation caught, positive own-route calls and passing
IBM-boot controls. All 170 bytes / 57 instructions remain uncredited until the full gate. The four bodies, fixtures,
CMake tests and three POPF_STOPS rows are integrated locally (1359 registered entries, 1355 accepted).
The main harness carries each POPF_STOPS hunk (005A->0089, 060E->0634, 005B->00AD).
All hand-backs are `:partial`; the one-instruction 00AD return uses `NOBLOCKS`, not partial.
Important distinction: RSOUND.LOG's already-installed branch targets STI at 00AC, not RET; both paths
must enable interrupts. Its bounded dossier ends at 00AD (full dossier retained), and the separate
return proof covers that final byte. ASOUND.LOG 005A and ISOUND.LOG 060E instead skip STI on their
already-installed branch. The first ASOUND draft also exposed the STI clock trap: call x86_sti before
charging its own instruction, so inhibit_at names the correct boundary. The original install was
reverified after route runs: all 18 game files and all three optional GOG reference files match.
`p2-asoundlog-008a` is only an initialized lead, not a proof: its first PIT control OUT wakes the machine;
cover its following continuations explicitly rather than claiming unreachable fixture instructions.

**Resume the live repaired prechecks before starting anything heavy:** the first six-seed attempt
passed five seeds but 600DF00D exposed an older ISOUND.LOG 0253 fixture's coverage floor: 4856 equal
comparisons, zero skipped, 301/303 instructions (missing 04FA and 0522 preset assignments). The terminal
failure is preserved in `D:/f117-gate/bc-pre-attempt-1-coverage-floor/`; session 73187 is closed.
The fixture now forces every 65h..6Ch byte command in one sixteenth of states after preserving every
original RNG draw; other states stay unchanged. Source/body comparisons prove that only this fixture
changed since the failed attempt, not matched.c or CMake. Separate reproof in
`p2-isound-log-0253-coverage-repair` passes all five private seeds, all 96 original mutants, all six
production 4000-state seeds (303/303), and real IBM boot (26 exact checkpoints, 464 calls) plus boot
control. Its audit is `p2-isound-log-0253-coverage-repair-audit`; the original proof is untouched.
`p2-batch-bc-accept/fixture-repair-verdict.json` pins the reproof; it is not a new routine/credit entry.
The production test runner rebuilt successfully; five new/repaired CTests passed in 1.11 s.

Unified execution session `61709` is running `D:/f117-gate/bc-pre/run_repaired.py`: six whole-table
seeds in two rounds, then an assertion that all 1358 unaffected primary per-routine reports are
identical to the archived attempt. Re-poll this same handle; do not restart on observation timeout
or trust status.json alone. Round 1: `0x94784986bfbe`, `0x27182818`, `0x20261022`; round 2:
`0x31415926`, `0x1BADB002`, `0x600DF00D`. No other heavy proof work is active. Source manifests,
logs and the final `precheck-verdict.json` (only on success) are in `D:/f117-gate/bc-pre/`.
Batch `D:/f117-gate/p2-batch-bc-accept/` is already prepared (prev bb, base 9478498), with latest bb
private audit/finalize scripts and a passing four-new-proof audit (57 instructions/80 mutants).
Do not recreate it. Coordinator PID `100424`, verified live with its exact command line, runs
`continue_after_prechecks.py` in that acceptance directory. It holds a native handle to precheck
PID `100752`, requires its successful exit and six unchanged-report checks, revalidates the private
proofs and measurement inputs, then runs accept.py through affinity_launch.py automatically.
Do not start a second acceptance. Its `coordinator-status.json` is advisory; verify live processes
and the terminal gate verdict. After acceptance, refresh the census using bc named-cover.
Do not change matched.c/tests/CMake or the five files pinned in `measurement-input-manifest.json`
during validation. Batch bc was accepted (gate 1,741 s, 601 CTests, 1,359 routines, 80 mutants, +170 named bytes; `D:/f117-gate/p2-batch-bc-accept/final-gate-verdict.json`) and progress.json now reads 81.31%.
Published through `51a70f1`.

**Measurement repairs queued with bc (24 Oct):** the COM launcher reference was wrongly keyed by
file offset 00000. It now uses the already approved guest range 00100..003CA (715 bytes), resolving
249 misclassified executed bytes; loader 03CB remains separate. COM references below 0100 reject.
`docs/census-runtime-jumps.tsv` and the new strict parser validate nine exact singleton EA templates
by file identity, CS:IP and original writer review (17 records / 45 executed bytes): six interrupt
chains and MPS_LOGO 00DA:0307/030E/0315 sound thunks. No generic changed-code allowance was added.
The original three timer and keyboard reference spans now include their final five-byte chain;
SETUP 104D..10B3 has a new second-read STI-first-handler entry based on its earlier static review
(102 bytes/51 instructions; 10B3 and other callees are excluded from this entry). PLAYER's handler
still starts with PUSH AX and remains work; the logo thunks also remain work. All 79 focused tests
pass. On the old bb named coverage, the corrected read-only measurement is P2 81.20%, All 94.26%,
gameplay 148778 matched + 2631 original / 163907 bytes, remaining 12498, checklist 590 / 44644.
Do not publish those as bc's result: use bc's accepted named coverage for the final refresh.
Saved progress.json still holds the previous 81.09% until bc acceptance. After the runtime-patch
validation, exactly 300 records remain rejected: 112 bootstrap, 187 zero-driver captures, one
VGAME/START cross-record. Preserve all rejection rules.

The F117.COM loader review also accounts for 128 already-matched fixture-only bytes in six
error arms. The successful FFFF-paragraph allocation arm remains `unreached-held`; allocation
and resize failures are `machine-option-only`; overlay-load/header failures and the common exit
are `needs-route`. No dead certificate is claimed. The final INT 21h at `04E3..04E5` adds two
strictly validated static code bytes, with no matched credit; the old draft's fallthrough into
string data after this nonreturning exit is rejected. These six reference entries raise the
table to 48 rows and add 130 checklist-reviewed bytes. The earlier read-only measurement above
predates these 130 bytes and the two-byte scope addition; refresh after the gate, never infer its
final score. A launcher-level missing resident overlay is a route lead for these error paths.

**Naming denominator (owner decided 24 Oct):** second-read `stays-original` reference entries outside the census leave the naming
denominator (790 bytes: the 688 launcher bytes and the 102-byte SETUP timer handler); all other statuses stay in. Naming moved
84.80% -> 85.12% by this change alone (P2 81.31% -> 81.41%), not by matching work.

**0. P2 completion scope (owner-approved 23-24 Oct; scoring implemented).** The accounted-for checklist
is now a fourth measured P2 item, weight 10. Existing weights were scaled proportionally to 90
(tooling 4.5, naming 31.5, gameplay 54). This intentionally lowers P2 from 90.04% to 81.04%:
no matching credit was removed. After encoded-code adoption and its second-read reference entries,
585 / 44,654 checklist bytes are reviewed and current P2 is 81.09%. The full completion definition
is in `docs/progress.md`. Remaining scope tasks:
- **Packer bootstrap measured (24 Oct):** the `packer stub` design-rule name is now recognized, but no
  reference entry or score credit is added. All seven packed programs have 16 raw-source-validated route
  records covering 29 bytes at their original packed CS:000E..002A: 112 instructions / 203 bytes by file
  occurrence (29 identical instruction bytes). These records are rejected correctly (109 changed,
  3 undecodable against the unpacked image). They belong to the packed representation, so do not add
  their addresses as original code in the unpacked image: that would alias unrelated code/data.
  A separate source-representation-aware review/score path is still needed. The full floating
  decompressor remains outside the score. After the singleton-jump repairs above, the audit accepts
  64,529 of 64,829 records; 289 changed and 11 decode rejects remain.
  Read-only source plan `D:/f117-gate/packer-representation-plan-20261024/` verifies identical
  packed code in all seven files: 163 instructions / 325 bytes in `000E..00F7` and `00FC..0158`,
  excluding the five-byte signature between them. The copied core adds 147 instructions / 296
  bytes per source file beyond the bootstrap, but has no complete existing route capture.
  First-read semantics and exact file/code hashes are saved privately; no independent adoption,
  scope change or credit is claimed. A copied runtime instance must not count its source twice.
- **Encoded SETUP adopted locally (24 Oct):** `tools/encoded_code.py` validates the two code rows in
  `docs/census-encoded-code.json`, with raw-file, image, bootstrap, whole-source/decoded-block and per-row
  hashes, fixed transform bounds, no relocations, exact instruction counts and disjoint reviews. All 585
  bytes add workload, zero matching credit; independent 21 Oct transform review is retained. Five
  synthetic adversarial tests pass; CI includes them. No decoded bytes are written. Two second-read
  `unreached-held` reference entries account for all 585 bytes; their matching/entry-path work stays open.
- Match the ~2,500 machine-option-only error-path bytes (OOM, DOS read/write/close errors, the `_C_FILE_INFO` environment arm) with
  test-input proofs instead of leaving them in permanent limbo; no test-only machine options needed (owner, 20 Oct, unchanged).
- Resolve the 831 probable-dead bytes: either a strict dead certificate or a proof, not "probable" forever.
- Before declaring P2 100%, run a closure review ("leave no stone unturned", owner's phrase, 23-24 Oct) of indirect calls, jump/dispatch
  tables, callbacks, interrupt vectors, overlay entry tables and self-modifying code, to catch code no discovery walk or route has found.
  This cannot be scored (you can't measure what you haven't found) — record what it covered as a closing step, not a percentage.
- The full "P2 is done when ..." definition is now in `docs/progress.md`; keep it current.

1. **Reviewed static code (10,301 bytes):** resumed verification retains every range; no additional route or dead certificate is established ([review](docs/static-code-review.md)). Any later exclusion needs exact per-range closure of indirect and callback paths. About 1,800 executed bytes outside the census still need proofs; one cheap extra route is proposed in `D:/f117-gate/static-route-plan-20261021/proposal_err_dswap_keys.args` (`err_dswap_nostart` plus two keys; needs `# expect-` lines).
2. **Reference entries for unmatchable code:** regenerate and fix `D:/f117-gate/unmatched-reference-20261021/reference.tsv` as listed above, then publish it as a repository doc (one row per routine: name, purpose, status, why not matched, evidence); no private paths or binary dumps in the public copy.
3. **Unreviewed static bytes:** the 117 audited code rows and the 70-byte VGAME data row are adopted (21 Oct, `D:/f117-gate/static-review-20261021/adoption-audit.md`). Still to adopt: the 20 follow-up data rows (`followup-data-rows.tsv`), the plain follow-up code rows (`followup-code-rows.tsv`). The encoded SETUP code is now adopted locally with the strict validator (585 code bytes within the 1,300-byte transform, no matching credit).
4. **Drivers and launcher: matching, steps 4 onward of `D:/f117-gate/modules-scope-20261021/plan.md`.** Done: steps 0, 1a, 1b (scope),
   2a (module-keyed harness), 3 (kit widened) and the step 4 pilot (batch `am`, `261381c`: MISC `0000` `000A` `0018`, MGRAPHIC `023A`;
   module tokens are defined in `matched.c`), then MGRAPHIC batches `ao` (14), `ap` (6), `aq` (9) and `ar` (9: the large drawing routines). 0 executed MGRAPHIC bytes remain unmatched: all 3,237 route-executed MGRAPHIC bytes are matched (474 more matched bytes run only in fixtures).
   Batches `at` (sprite clipper `0BC0`, glyph renderer `061A`, span prefix `0F3B`), `au` (clip helpers `02B9` and `037F`, tail `03D1`, set-up `03E8`, front end
   `03C9`; its first gate failed the private audit) and `av` (clip B `031D`, front ends `02A1`, `02B0`, `0314`, `0376`) are accepted. Batch av: 51
   implemented instructions, 211 caught mutations, 502 CTests, acceptance 1,727 s (`D:/f117-gate/p2-batch-av-accept/`; proofs
   `p2-mgraphic-av-<ip>`; batch au's first run is `p2-batch-au-accept-run1/`). Conventions that worked for the text cluster: helpers whose reject arm does `pop ax` /
   `jmp 03D1` either run the tail inline (`02B9`, `03E8`: tail unscored) or hand back at `03D1` with a `FATAL_JUMPS` row (`037F`, `031D`, the front ends);
   RETF-at-entry+6 and JMP-SI exits need `run_to_ret` rules (hand-applied to `tests/func_lockstep.c`), and `03E8` also needs the bad-return skip test to accept
   SP-2. A wrong call-depth value never fires a trap, so mutations of it are invisible: use post-trap mutants and judge such calls by the real-route counts.
   A route must run the rare arms: `full_cycle` missed `02B9`'s loop and `031D`'s cut arm (`persian_gulf_air_training` runs it).
   Step 5 was already completed in batch an: F117.COM `03CB` is matched, and MISC `0029` too. Batch ax adds MISC `0005`;
   check the registry and current named coverage before selecting any old lead. Step 2b relocation infrastructure now passes its
   separate full gate, including the planted DS=0 controls and zero-base report equivalence described above. The first three driver routines are matched (batch ay).
   Continue with ASOUND.117, the remaining .LOG routines, ISOUND/RSOUND.117 and the launcher EXEC chain,
   using the plan only for leads and traps; STI-first handlers remain original by design. The existing COM fixture needs explicit
   adaptation from segment 0 to cs. Its helper already reads live CS, as do most MGRAPHIC fixtures.
5. **The hard tail: declined routines are Phase 2 work, not exclusions** (owner, 23 Oct: put them in). They are in the gameplay denominator today (they are
   part of the unmatched executed bytes: VGAME 549, PLAYER 268, END 261, MPS_LOGO 255, START 251, SETUP 210, DSWAP 68) and P2 cannot reach 100% without them.
   List (from `D:/f117-gate/kit/DECLINED.tsv`, the roadmap and the rejected reviewed entries); each needs the stated unit before it can be proved:
   - VGAME `0x00010` `main()` (the whole game): a nested-run proof or a decision; check before assigning.
   - MPS_LOGO `1ADC` and `1C82`, START `11DB` (with its interior arm `1285`, an 86-byte fragment of a 131-byte tail): earlier proofs found the draft unequal;
     find the difference first (start from the saved private proofs named in the roadmap), then prove the whole function.
   - START `8427` and END `4393`: earlier routes missed planted defects and the routines wait; needs routes and inputs that reach the arms and wait handling.
   - END `57B0`: an interior arm of the 795-byte printf-like function (RET at `5B4C`, 11 other census rows); the whole function is the unit.
   - VGAME `137CE`: a 9-byte far table thunk at the observer hook's address (`hook_fill_rows`); the body must call the observer itself and join the
     `deliberate` list in `tests/test_overrides.c`.
   - PLAYER `02853` (interrupt handler whose STI is its 13th instruction) and `028C1` (retrace re-sync, STI inside its loop): rejected as original by both
     readers; match with the stop-after-STI hand-back.
   - START `08F94` (the run time's divide-error entry, inside the C start-up row).
   Not on the list: DSWAP `3DF3` (2,791 zero bytes of data, already excluded) and the design-rule originals. Add these to a batch once the driver leaves run low.
6. **Sort the matched code no route runs** (owner, 21 and 23 Oct): about 26,196 fixture-only bytes (about 88 whole routines no route reaches, plus
   unrouted arms such as ASOUND.LOG `0344`'s voice sweeps and ISOUND.LOG `0253`'s stream readers). They are proven by lockstep and credited by the naming
   score, but no real play confirms them and the gameplay score does not count them. Classify each group as player-reachable (write a route, which also
   moves it into the gameplay table already credited) or not reachable (error path, hardware option, dead: a checklist entry in
   `docs/reference-reviewed.tsv`). Mostly read-only work plus route writing; run it between proof batches. The same sorting applies to the 13,817
   reviewed static bytes no route runs: each ends matched (a fixture proof is enough for the naming score), routed, or certified dead or data.
7. **Phase 4 stays unscored:** refresh rates above 60 Hz, the hardware-only parts, a person playing.

## Phase 2 batch workflow and its traps

Per batch: proofs (agents, steps 1-7 of the kit) -> `finish_all.py DIR:ROUTE ...` (checkpoints, boot proof,
verdict; at most three at once) -> `integrate_many.py DIR ...` -> hand-apply the harness hunks it does not carry
-> build (`build.cmd` from PowerShell) and the new CTests -> pre-checks -> `new_batch.py LETTER PREV HEAD
[--no-baseline] DIR ...` -> `affinity_launch.py py p2-batch-LETTER-accept/accept.py` -> `finalize.py`,
`audit_private.py` -> score refresh (`py tools/progress.py --census ... --named-cover
p2-batch-LETTER-accept/named-cover --route-cover D:/f117-gate/route-cover-20261020b --data D:/GOG/F-117A`) ->
docs -> commit (title from `py tools/progress.py --title`) -> push -> CI. Scripts are in
`D:/f117-gate/accept-kit/`; copy `audit_private.py` and `finalize.py` forward from the newest accepted batch
(they carry the allowances below), since `new_batch.py` copies stock ones.

- **Run `audit_private.py` BEFORE the gate.** It reads only the proof directories and takes seconds; batch au's failed after a 27-minute
  gate and forced a second gate. Mark `:partial` at `new_batch.py` time for bodies that hand back, inline a tail, leave a dead arm unwritten or
  call original code (`partial` is read only by the audit). Claimed blocks must be disjoint: an instruction reached by two paths (a shared `RET`)
  is claimed once, in a shared label.
- **A rerun of the same coverage inputs gets slower, not faster (lead, unfixed).** The second batch-au gate skipped coverage (nothing new to add),
  so the interpreter parity runs no longer overlapped it; parity then took 1,666 s (longest: `career_promotion/interp` 1,663 s) and the N=1
  presentation check started at 173 s instead of 939 s. Acceptance was 1,920 s against 1,600 s for the first run. Untested ideas, both
  verdict-neutral because each route runs on its own virtual clock: start the interpreter runs early even when coverage is skipped, and start N=1 after
  the interpreter runs. The gate log now prints start and end offsets of its longest jobs per stage.
  Latest acceptance (aw), including presentation and the final audits, took 1,671 s.
- **Pre-checks before a gate (cheap, adopted 20 Oct):** the commit-keyed seed (`build_recomp.rotating_seed()`;
  the first 12 hex digits of HEAD) through `tools/func_lockstep_par.py --exe build/func_lockstep.exe --jobs 8
  --states 4000 --verbose --seed SEED` (about 8 minutes per seed in batch as), and every new route once with `--engine recomp`,
  grepping `[matched] OVERRUN`.
- **Fixtures force each branch outcome** in a fixed share of states (by the state index), and every proof is
  run at six or more seeds: a chance-only arm fails `fixtures reached too little` at some rotating seed.
- **Harness hunks `integrate_many.py` does not carry:** SPANS (beyond the kit's own row), IMAGE_PATCHES,
  LIVE_FRAMES, PROGRAM_ENDS, POPF_STOPS, `patch_image_for` lines, `run_to_ret` rules, the `setargv_` pop-distance
  list and the `g_native_int10` list. Diff each proof's `tests-base.c` against the repo's current
  `tests/func_lockstep.c`.
- **Same-IP routines in different segments:** SPANS is keyed by IP; `kit.py integrate` refuses an alias unless
  `KIT_ALLOW_ALIAS=1`, and the new span then goes in the segment-keyed `SPANS_SEG`.
- **Duplicates:** an agent's "new" address can already be matched under another segment spelling
  (`14C2:00B7` = `1452:07B7`); compare linear addresses. The finalize tally catches it.
- **Audit allowances:** `NOBLOCKS` prefix in `batch.json` for bodies without an `XB` block macro; mark
  hand-back, computed-count and inlined-callee bodies `partial`; a hook inside a routine is accepted only with
  a covering `fix_inside` guard in the body (END `03D9` declines while D12 is on).
- **Baselines:** `--no-baseline` whenever `CMakeLists.txt` outside tests, non-matched `src/` or a route changes;
  the next batch then freezes a new keyed baseline (the current one is for 63 routes).
- **Kit libraries:** refresh `D:/f117-gate/kit-libs` from the main build after any `src/` change (a stale set
  once lacked `--fix QUIZ`); keep the old set as `kit-libs-before-<commit>`.
- **Waits and events:** a route that leaves a matched `getch` waiting trips gate step 9 (OVERRUN): press the key
  before the wait (`err_vgame_no_photo` types at VGAME+600000).
- **Bodies:** read every argument at the moment the original reads it (DS and SS can overlap: VGAME `D9E7`);
  read segment immediates from the code (`seg_read16`), never hard-code them (END `4160`).
- **`matched.c` compiles with `/Os`** (at `/O2` MSVC took 90+ minutes); never more than two or three `cl.exe`.
- **Do not chain a commit after a doc-edit script:** a failed edit once let a commit go out without its docs.

## How Phase 2 is done: the kit and parallel agents

`D:/f117-gate/kit/kit.py` runs a private proof for one routine (`init`, write
`body.c` / `fixture.inc` / `mutations.json`, `lock --cover`, `seeds`,
`mutations --jobs 1`, `checkpoints`, `verdict`, `integrate`);
`kit/BRIEF.md`, `kit/AGENT_PROMPT.txt` and `D:/f117-gate/ac_brief_extra.txt` are
the agent brief. Agents do steps 1-7 only, in their own `p2-*` directory, every
command through `kit/pin.py MASK` (CPU set + BelowNormal) with
`mutations --jobs 1` in each of at most three proof slots; the tool's default
of four jobs is unsuitable for the shared queue. Use a separate mask per slot
(0x000F0000, 0x00F00000, 0x0F000000), with at most three `cl.exe` processes
across all work. Main builds use `CMAKE_BUILD_PARALLEL_LEVEL=3`.
Finish proof workers before starting a gate; run no unrelated
heavy job beside it. Verify each `private-verdict.json` yourself (counts,
mutations, source hashes, real-route calls) before integrating. Routines that
stay original or were declined are listed in `D:/f117-gate/kit/DECLINED.tsv`.

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

## Fault replay inside a body's nested original code (22 Oct, fixed in the harness)

`tests/func_lockstep.c` compares a divide fault at the state where the original takes it. A matched body that
calls original code (`guest_call`) runs that callee in `step_runner_run`; a fuzzed stack can make the callee wander
into the body's own code, fault there exactly as the original does, and then keep stepping through the random fault
vector to the clock limit, so the two sides were compared at different moments (VGAME 0x5852, seed `0xd10dd4e2005e`,
`--states 4000`, state 3187: matched `vgame_bearing` call at 5AF6 returned to 619A; both sides faulted at 5E89 with
AX 1000, DX 0). The routine was right. `step_runner_run` now returns at the first interrupt entry for the mission,
ground, projectile and traffic fixtures, as `run_to_ret` does. When a fault-replay state mismatches with the same
fault site on both sides, print the matched side's ip after `o->fn` and the nested call log before suspecting the body.

## Two latent defects the extra seeds found (22 Oct, fixed)

Before the batch ar gate a 13-seed sweep (`py tools/func_lockstep_par.py --exe build/func_lockstep.exe --jobs 8 --states 4000 --seed S`, about
2 min a seed) found two older mismatches the gate's four seeds never hit. (1) VGAME `1377:0874`, `0886`, `08A3`, `094A` (planar fill): after each of
four `OUT dx,al` to the graphics controller the body went on although the write wakes the run loop; each now stops after the OUT with its clock
charged (`if (!room(c, 1)) { c->icount++; c->ip = next; return 1; }`). (2) VGAME `120A:0815` (vertex transform), seed `0x27182818`, state 1606: a
harness gap, not the body. The part-switch escape `07C8-0812` lies below the entry, so the default self-write window missed a fuzzed stack that
aliases it; `SPANS_SEG` now has `{VGAME.EXE, 0x120A, 0x0815, 0x07C8, 0x00F0}`. The tell: the original's per-instruction trace showed one-byte
instructions where the dossier draft has a four-byte one (the executed bytes differed from the image). Run an extra-seed sweep before every gate; it is
cheap and runs in the background. Debug recipe: `--only MODULE:HEXIP` takes the IP without leading zeros; print `g_state_index` in `compare()` to get
the failing state, then trace that state's original run (`g_trace`, with registers) and compare it with the draft.

## A third latent defect: instruction counts on rare paths (22 Oct, fixed)

The commit-keyed seed failed VGAME `73C8` (`vgame_target_damage`) at the batch at pre-check: its target-zero path (`792E` reports
something) charged 5, 10 and 6 instructions where the original runs 6, 13 and 6 (plus 2 on the skipped `B2DC` load arm), a gap that was only
6 compared states of 4,000 at that seed. Found by reading the original's per-instruction clocks against the matched side's callee list:
key both by clock, and the first clock whose (ip, sp) differs is where the count is short. Counting instructions by branch arm is
the error-prone part; verify each arm with `--verbose --only` and extra seeds (16 at 20,000 states now compare equal). `--only`
reports tens of compared states per seed for this routine, so a low "states compared" figure means a rare arm may still be unchecked.
The machine is a Ryzen 9 7950X (16 cores, 32 threads); `affinity_launch.py` uses mask `0xffffff` (24 threads) and leaves 8 free.

## Traps

Matching and the gate:
- `func_lockstep --only MOD.EXE:HEX`: the IP has NO leading zeros (`D0C`, not
  `0D0C`) or it silently compares 0 states. "Not testable here (routes only)"
  means only a real route can prove it.
- The commit-derived seed rotates with HEAD and can fail an untouched routine;
  reproduce with `func_lockstep --seed S --only MOD:IP`. A lone
  `1 mismatching` with every line `equal` is a coverage floor (`fixtures
  reached too little`): reproduce with `--only` and `--verbose`.
- Charge the clock between every port access, stop right after each STI, and
  prove a body on `ibm_boot` first (`F117R_MATCHED_LIMIT=N py tools/run_route.py
  tools/routes/ibm_boot.args --engine recomp`, 22 s).
- A wait inside a matched routine must clamp `m->run_until` to `c->stop_at`;
  gate step 9 (event limits) checks routines that guest-call code that can
  wait. Check every callee's RET form.
- Matched routines with a switchable fix site after their entry decline while
  that fix is enabled (`fix_inside`); check `src/fixes/fixes.c` for sites inside.
- `1450` is also the presentation observer's frame-phase hook: a matched body
  there must call `observe_game_draw` itself and go on `deliberate` in
  `tests/test_overrides.c`; `kit.py integrate` refuses an address with an observer.
- Cache keys include raw source bytes (CRLF vs LF changes them). `kit.py
  checkpoints` needs the interpreter cache entry for the current source tree:
  after changing anything under `src/` other than `matched.c`, run a gate first.
- Use `recon_career`, not its alias `recon_return`, for the keyed baseline.
- Reach: the gate's recomp `.cov` files hold only interpreted fallback
  instructions, so absence proves nothing; use a complete interpreter capture.
- SETUP's INT 10h at `14EF` runs two native callback/stub clocks atomically
  (`native_stub_time`): an SDL one-instruction alert there is not extra execution.
- `build.cmd` must run from PowerShell (through bash it hangs).
- PowerShell redirection (`*>`) writes UTF-16 on this host: read those logs with an explicit encoding, or have Python write UTF-8 logs itself.
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
  never copy or merge them wholesale. Archived branch patches:
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
