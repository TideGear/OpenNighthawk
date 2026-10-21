# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs). State as of 21 October 2026 (end of session).

This file holds only the current state, standing practice and traps, not a
session log: earlier detail is in `git log -p handoff.md`, and per-batch
evidence is in the `D:/f117-gate/` directory named in each roadmap entry.
Consolidate it instead of appending to it.

## Current state

- **Scoreboard:** P1 100.00%, P2 82.84%, P3 100.00%, All 94.76%
  (`py tools/progress.py --title`; P4 and the decided-against P3 items are not
  scored).
- **Phase 2:** 1,189 addresses matched; **165,256 / 201,697 creditable bytes** across
  all 17 game code files (credit by byte; rules in [docs/progress.md](docs/progress.md)).
  Batches a to al are accepted (table in [docs/roadmap.md](docs/roadmap.md); evidence
  `D:/f117-gate/p2-batch-<letter>-accept/`). Gate: 427 CTests, 63 routes run as 62 sessions,
  5,723,136 instruction states per timing profile, all matched routines at three seeds.
- **Where the remaining 36,441 bytes are** (`py tools/census_classify.py --census D:/f117-gate/census.tsv
  --named-cover D:/f117-gate/p2-batch-al-accept/named-cover --route-cover D:/f117-gate/route-cover-20261020b
  --data D:/GOG/F-117A --rows 45`):
  - outside the census rows, about 12,100: 10,301 reviewed static code bytes no route runs
    (`docs/census-code.tsv`) and about 1,800 executed bytes no routine names;
  - inside the census rows, 5,496: 947 `ran` (all declined or staying original: VGAME `0010` main,
    `073C8`'s second lock slot, MPS_LOGO `1ADC`/`1C82` unequal, START `8427` and END `4393` wait loops,
    END `57B0` interior arm, DSWAP `0723`'s failure arm), 4,322 `never_run` (about 2,500 machine-option-only
    error paths, 831 probable-dead, the rest needs combat or very long careers), 222 `unreached`, 5 `slots`.
  - 18,835 validated executed bytes in the launcher, resident graphics library and sound drivers are now in
    scope. Outside the score: 3,939 unreviewed static-discovery bytes and the floating LZEXE decompressor.
- **Phase 2 scope work (21 Oct).** The interrupted analyses below remain private under
  `D:/f117-gate/`; adopt only what you verify. Module scope steps 0/1a/1b are now implemented:
  - `static-route-plan-20261021/` (DONE): no route, damage, argument or probe answer can run any of the 10,301 reviewed-static bytes: 9,197
    bytes (groups A, B, D-I: VGAME CGA/Tandy drivers 1,042; printf modifier states 1,251; SETUP library cluster 2,724; picture/span wrappers
    1,480; INT 23h shims 569; MPS_LOGO mouse library 230; PLAYER report formatter 460; VGAME uncalled code 1,441) have no way in, and the
    1,104 bytes of DSWAP's printf core (group C) need a floppy drive, which the owner declined. `plan.md` and `groups.tsv` there. Certifying
    them dead needs `docs/census-code.tsv` rows removed (the classifier subtracts nothing for `census-dead-ranges.tsv`).
  - `static-route-verify-20261021/` (INTERRUPTED in the prior session): an adversarial re-derivation of groups A, D, E, F, G, H
    (historical agent id a70bf25a1f34ae616; scratch scripts are saved). Do not certify any group dead until it reports.
  - `static-review-20261021/` (INTERRUPTED, agent a2f2492401cab1fda): the code/data review of the 3,939 unreviewed static bytes; stage files
    saved, no `code-rows.tsv` yet.
  - `unmatched-reference-20261021/` (agent a6d52c02c7d0dc047, status unknown when the session ended): the reference table for every range that
    cannot be matched; no `reference.tsv` yet.
  - `modules-scope-20261021/plan.md` (DONE): the ten-step design for bringing the launcher, MGRAPHIC, MISC and the sound drivers into the score
    (18,835 validated bytes; measured byte share 81.93%, weighted P2 82.84%). Steps 0/1a/1b done:
    `measurement.json` verifies per-module counts and captured-record rejection, and the scorer includes all 17
    game code files. Credit remains 165,256; no new dead/original exclusions. Scope validation: 427 standard
    CTests, 57 focused Python tests, 2,235,060 matched states / 0 mismatching (2,000 states, eight shards).
    `scope-verdict.json` retains the byte audit and test evidence. Next: step 2a (harness module keys).
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

1. **Reviewed static code (10,301 bytes):** the route plan found no way to run it. Complete the interrupted verification from its saved scripts, then decide per group: certify dead (with certificates and by removing the rows from `docs/census-code.tsv`) or keep. About 1,800 executed bytes outside the census still need proofs; one cheap extra route is proposed in `D:/f117-gate/static-route-plan-20261021/proposal_err_dswap_keys.args` (`err_dswap_nostart` plus two keys; needs `# expect-` lines).
2. **Reference entries for unmatchable code:** check the agent's `reference.tsv` and coverage check
   (`D:/f117-gate/unmatched-reference-20261021/`), then publish it as a repository doc (one row per routine:
   name, purpose, status, why not matched, evidence).
3. **Unreviewed static bytes:** adopt the review's code rows into `docs/census-code.tsv` (hash, boundaries and
   instruction counts must verify on refresh) and data rows into `docs/census-data.tsv`, then refresh the score.
4. **Drivers and launcher into scope:** implement the Plan agent's step-by-step design (module identification
   for overlays and the COM file, matched-table and lockstep support, census from executed plus reviewed bytes,
   scoring and docs). Measurement and scoring steps 0/1a/1b are complete; next step 2a is module-keying the
   harness, with identical per-routine output at four seeds and a full gate. Small verifiable steps; the gate stays green after each.
5. **Phase 4 stays unscored:** refresh rates above 60 Hz, the hardware-only parts, a person playing.

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

- **Pre-checks before a gate (cheap, adopted 20 Oct):** the commit-keyed seed (`build_recomp.rotating_seed()`;
  the first 12 hex digits of HEAD) through `tools/func_lockstep_par.py --exe build/func_lockstep.exe --jobs 8
  --states 4000 --verbose --seed SEED` (about 4 minutes), and every new route once with `--engine recomp`,
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
`mutations` (four edits at a time), `checkpoints`, `verdict`, `integrate`);
`kit/BRIEF.md`, `kit/AGENT_PROMPT.txt` and `D:/f117-gate/ac_brief_extra.txt` are
the agent brief. Agents do steps 1-7 only, in their own `p2-*` directory, every
command through `kit/pin.py MASK` (CPU set + BelowNormal) with
`mutations --jobs 2`; give each agent its own mask (0x000F0000, 0x00F00000,
0x0F000000). Finish proof workers before starting a gate; run no unrelated
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
