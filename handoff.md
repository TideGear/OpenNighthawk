# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs). State as of 10 October 2026.
Earlier session logs are in `git log -p handoff.md`.

## Route-coverage trap, hit twice in a row (10 Oct, 19:30-20:02 PDT)

Two "routes only" VGAME leads in a row (`0x0FB20` sky-detail setter,
then `0x01B9` a cold-start init/sound sequence) passed every lockstep
check at every seed cleanly, then failed the full gate's step 8 ("every
routes-only routine runs on a route") - no scripted route actually
reaches either one. Both reverted after the fact, each costing a full
~16-minute gate run to find out.

For the second one, before building, I traced its single call site
statically (`x86dec`, instruction-aligned, not a byte-scan) to a key-
dispatch branch gated on AX == 0x0B00 (scan code 0x0B, a "0" keypress),
then confirmed a route's recorded input script does send a literal "0"
during VGAME. That still was not reached. **The lesson: a route sending
the right-looking key somewhere is not evidence the exact code path
executes** - the key could be consumed by a different, earlier dispatch
entry, a different game mode/screen than the route is in when it sends
it, or my read of the AX encoding could simply be wrong. Static call-
graph tracing answers "is there a path," not "does any route's actual
play reach it." **The only check that actually answers the real
question is the full gate's own step 8** - there is no cheaper
substitute in this repo (no `callers.py`/`addrmap.py`/`F117_WATCH`
despite older memory notes suggesting otherwise; checked, none exist).

Going forward: prefer candidates that produce **comparable random-fuzz
states** at all (not "routes only") - those get a real verification
signal immediately from targeted lockstep, with no full-gate gamble on
reachability. Save "routes only" leads (DOS interrupt installs, fixed
init sequences gated on rare conditions) for when a route is already
known via other evidence to hit them, or accept the cost of checking
step 8 specifically before writing the next one.

## IMPORTANT CORRECTION: the combat/speed finding further down is stale

The "Combat rate falls with speed" line in "Goal and standing decisions"
below, and most of `docs/speed-sweep.md`'s middle sections, describe a
real effect measured *before* two measurement confounds were found and
corrected (RNG seed drift between CPU speeds; input-poll timing against
the simulation step). **The document's own final, most-corrected cohort
(the "Step-aligned arm" table, `docs/speed-sweep.md`, late in the file)
already found no statistically significant combat difference between 9
and 20 MIPS** once those confounds were fixed on top of the physics
repairs (D1TTL+D1SLOT+D1PROX+D1ACCEL+D1REAL): burst difference +0.1667
(95% CI 0..+0.5), damage selections +0.3333 (95% CI 0..+1.0) - zero
included, point estimate if anything slightly favoring the higher speed.
Quoted verbatim: *"This cohort shows no high-speed enemy weakness."*

This is not a claim that the issue never existed - earlier, less-corrected
cohorts in the same document show a real, solid effect (bursts -1.3 to
-1.5, CI excluding zero). It is a claim that once the *measurement itself*
was fixed, not just the game's physics, the effect was no longer
distinguishable from zero. Read `docs/speed-sweep.md` end to end (not just
grep for a conclusion) before citing this investigation's status to
anyone - this session got it wrong three times in a row in conversation,
each time by answering from an earlier section without reaching the
final, superseding one.

**Correction to the correction:** `docs/roadmap.md` (lines ~305-320) and
`docs/bugs.md` (lines ~167-173, the D1REAL entry) were checked directly
and are ALREADY accurate - both already state the corrected, final
conclusion (11 of 12 seed/step-aligned pairs matching, no high-speed
weakness). Only the "Goal and standing decisions" bullet immediately
below in this same file was actually stale (dated 9 Oct, written before
this correction existed), and it has already been fixed in place. No
further doc propagation is needed. The lesson here is really about
answering from a full re-read rather than a partial one, in conversation,
not about the project's own documentation being out of date - it wasn't.

## Phase 1 threshold tightening (10 Oct, 15:50 PDT)

Followed up on the owner's "are any P1 checks too loose" question: tightened
three numeric thresholds that had 1.75-3x headroom over their single
measured run, down to ~1.4x (`tools/pc_parity.py`'s `gog.max_drift_ms`
100->80 and `gog-music.max_timing_ms` 40->20; `tools/ref86box/
compare_timing86.py`'s `max_duration_diff` 0.35->0.30 and `max_drift`
2.2->1.5). Deliberately left DOSBox-X's `max_drift_ms=350` alone - its
looseness has a stated, specific reason (the comment: "real-time captures
varied; fast-forward ones are identical," i.e. DOSBox-X's own reference
capture isn't perfectly reproducible run-to-run), unlike the other four
which were just round numbers roughly double the measured value with no
recorded justification. Re-verified all three edited checks: the two
`pc_parity.py` ones on a fresh live run (GOG-only, no emulator needed -
`--no-86box --no-dosbox-x --no-save`), `compare_timing86.py`'s on the
existing saved `D:/f117-gate/pc-parity-profile-aware/` capture (reusing it
rather than re-running 86Box, since only the Python constant changed, not
the engine). All eight `tests/test_reference_timing.py` regressions still
pass (they read the module's `LIMITS` dynamically, not a hardcoded copy).
Updated the two doc passages that cited the old numbers literally
(`docs/roadmap.md`, `tools/ref86box/timing386.md`); left the 9 Oct historical
entry in `timing386.md` (the one describing a since-superseded 0.37s FAIL
against the then-current 0.35 limit) untouched, since it's a dated progress
record, not a current claim. This was a values-only change to test tooling,
not engine code - no full gate needed, just the targeted re-verification
above. Single-measurement margins, not variance-backed; if a future clean
run fails one of these for reasons unrelated to the engine, that's the first
thing to suspect.

## CPU/combat: a candidate root cause, investigated and closed (10 Oct, 16:25-16:38 PDT)

The owner redirected priority mid-session from Phase 2 to "perfectly fix"
the CPU-speed/combat issue, then asked whether anything besides combat is
affected by speed - which turned into a proper static audit (see
`docs/speed-sweep.md`'s new final section, "A candidate root cause: S's
own self-calibration", for full detail; this is a summary). Using the
recompiler's own decoder (not grep), every VGAME multiply-by-S was swept
and classified. Most are safe. One is a serious, NOT YET CONFIRMED
candidate: the routine that *measures and corrects S itself* (inside the
large multi-role function at VGAME `0x3ADA`) has a negative-value-into-
unsigned-MUL bug at `0x4447` whose consequence, traced bit-exactly in
Python (not hand math - `D:/f117-gate/audit-s-calibration/
trace_s_calib.py`), sets **S to exactly 0** for every tested starting S
from 16 to 40. If real, this would be upstream of every other S-dependent
system already investigated (missile lifetime, proximity, etc.) - a
single root cause rather than a parallel symptom.

**Follow-up this same session found the reachability answer, and then a
correction to the severity claim.** Traced `0x3ADA`'s one caller in the
reachable graph: VGAME `0x1BCE`, the literal main flight loop (canopy
draw, `game_input`, `0x3ADA`, check the END.EXE hand-off flag, loop while
flying) - this code runs every frame of normal flight, not some rare
mode. `docs/bugs.md`'s existing (pre-dating this audit) D1 writeup
independently corroborates this: `0x441D` is "the controller's entry,
which every frame passes once."

**But that same D1 writeup also records a 90-flight sweep at
9/12/16/20/40 MIPS, with and without the D1 fix - including unprotected
runs at 40 MIPS - with no reported crashes.** If the traced S->0->
divide-fault cascade were accurate as written, those unfixed 40 MIPS
flights should have faulted repeatedly. They did not. This is strong,
already-existing evidence that the "sets S to exactly 0" arithmetic trace
has a wrong precondition somewhere - most likely that the loop-index gate
is actually satisfied in ordinary play, or that the starting-S values fed
to the Python trace (16-40, chosen to span where `[0x43E8]` turns
nonzero) are values the game's own continuous calibration ever actually
reaches. **Both `docs/speed-sweep.md` and this entry have been corrected
in place to flag this as an unconfirmed hypothesis contradicted by
existing data, not a finding** - do not carry forward the earlier,
stronger framing from this same session's first pass.

**Settled the same session, by direct measurement, not further
inference:** `py tools/d1_check.py --data DIR --ips 40000000 --engine
interp --no-fix` (already-existing tooling) measured S directly over a
full 161.66 s flight at 40 MIPS with every fix off: **S=15 for all
71,852 samples - never once above 15**, more than 4x past the raw speed
where the traced bug's precondition (S>15) would first engage. **This
candidate is closed: no fix is warranted.** Whatever this routine's real
behavior is with actual game state, it holds S at a stable ceiling of 15,
not a runaway to 0. The static arithmetic trace was real as an isolated
computation but fed inputs (S=16-40) the game apparently never produces
at this point in its own logic; this is a case of the project's own
`f117a-predict-then-measure` lesson playing out directly - a careful
static reading produced a wrong prediction, and five minutes with existing
tooling settled it where further hand-tracing would not have.

The audit's *other* candidate (`0x2BFE` inside `game_input`, a
terrain-distance value truncated after a signed IMUL - see
`docs/speed-sweep.md`'s audit section) was never pursued this far and
remains open and unweighted if this kind of audit continues later; it
does not inherit this closure.

This investigation, start to finish, is also the clearest argument this
session produced for the project's lockstep + full-gate discipline:
twice today (this finding, and the `vgame_release_count` revert below)
reasoning that looked solid on paper did not survive contact with
real measurement. Keep defaulting to measurement over prediction.

Separately, two of the three P1 threshold-tightening and three doubtable-
investigation asks from earlier this session are already committed (see
below); the audit above was read-only, no code changed for it.

## Bug fix in an already-matched routine: vgame_eventlog_add (10 Oct, 19:35 PDT)

Attempted a new routine, `vgame_sky_low_detail` (VGAME 0x0FB20, far-called,
sets the sky's low-detail flag then calls an unmatched redraw helper).
It passed every lockstep check at every seed (0 comparable states,
correctly classified "routes only" like `vgame_isrclock_init`), but the
full gate's step 8 ("every routes-only routine runs on a route") failed:
**none of the ~34 scripted routes ever call it** - no evidence it's
correct beyond static reading. Reverted; not committed. Lesson: "routes
only" from the lockstep is not sufficient on its own - the gate's own
route-coverage check is the real bar for an unfuzzable routine, and it's
worth confirming informally (e.g. grepping the routes' recorded inputs
for whatever toggles the relevant setting) before investing in a
candidate that may end up with zero coverage.

While the gate was re-running for that attempt, its seed-7c check (this
commit's own derived seed) found a genuine, pre-existing bug in an
**already-matched, already-committed** routine from an earlier session:
`vgame_eventlog_add` (VGAME 0x04ABA). It cached its two stack arguments
(`kind`, `target`) via `arg()` at the top of the function, but the real
original reads them late - `mov al,[bp+4]` and `mov al,[bp+6]`, after
three prior writes to the event-log record. In this fuzzed state DS and
SS happened to alias such that one of those writes landed on the stack
slot holding `target`, and the original's late read observes its own
write while the matched code's early, cached read didn't. Confirmed by
full verbose trace (`AX 00FE vs 0001`, all other registers identical) and
by hand-checking `arg()`'s own doc comment ("whose BP frame is not yet
built") against the point it was actually being called - after the frame
was already built elsewhere in the same function made this easy to miss
on a skim. Fixed by reading both arguments at their correct point in
program order (via `c->r[R_BP]+4`/`+6` directly, not `arg()`, since the
frame is built by then) instead of hoisting them. Reverified on the
exact failing seed (0x4c357fd31ed6: 3,986 states, 0 mismatching), the
default seed (59,911 states) and 0xC0FFEE/0xDEADBEEF (clean), the full
775-routine suite (0 mismatching), and the full gate (0 DIFFERENT routes,
every check clean, exit 0). Committed/pushed. Scoreboard unchanged
(770/1,535 functions, P2 39.83%, All 71.78%) - this fixes an existing
match, it doesn't add one.

**Why this matters beyond the one routine**: this is the second time this
session a gate failure traced back to something *other* than the change
being verified (the first was the stall misdiagnosis, which turned out
to be environmental, not code). Full gate reruns compare against a
*new* per-commit seed every time (`this commit's seed 0x...`, derived
from HEAD), so previously-matched, previously-passing routines are not
permanently proven - each new commit's seed re-rolls the dice on every
one of them. A failure in the full gate always needs its MISMATCH line
checked for which routine actually failed before assuming it's the one
just added.

## Phase 2 checkpoint: vgame_free (10 Oct, 18:38 PDT)

VGAME 0x0F8EC (`vgame_free`) is matched, verified and committed/pushed:
the near-heap `free(ptr)` counterpart to the already-matched `malloc`
(0x0F91A, `vgame_malloc`) - both operate on the same heap descriptor at
DS:922E. Identified (not just guessed) by reading `vgame_malloc`'s own
comment, which names 922E as the heap descriptor, then confirming a real
call site at 0xF1CB (decoded instruction-aligned, not a byte-scan
false-positive) pushes a pointer immediately before calling 0xF8EC. Steps
the pointer back two bytes to its block header, sets the header's low
("free") bit, and advances the descriptor's free-search hint at [922E+8]
past the freed block when it reaches further than the hint already does.

Lockstep caught a bug on the first attempt: both of the routine's two
conditional jumps (`jae`, `jbe`) had their *own* instruction counted as
part of the icount total - I added the manual counts for the instructions
before each Jcc but left the Jcc itself out, undercounting the clock by 1
on whichever path exited early. Textbook missed-Jcc, exactly the trap
flagged in this project's own history. Fixed (folded each Jcc into the
preceding block's count) and reverified clean: 66,505/4,411/4,436
comparable states across three seeds, zero mismatches; full 775-routine
suite 2,509,526 comparisons, zero mismatching. Full gate PASSED clean (0
DIFFERENT routes, both instruction profiles and all three matched-routine
seeds zero mismatches, every routes-only routine ran, exit 0). Scoreboard:
775 addresses matched, 770 of 1,535 census functions, 65,702 of 179,213
bytes; P2 39.83%, All 71.78%.

**Lead-selection trap found along the way**: the census's `size` column
is NOT the function's real instruction/byte length - it's some other
distance metric (a 6-byte census entry turned out to be a 72-instruction
routine; a 7-byte entry was 336 instructions). Sorting unmatched leads by
census size to find "small, quick" routines is not a valid strategy.
Draft each candidate with `tools/matched_draft.py` first and judge size
from its own reported instruction count, not the census row.

## Phase 2 checkpoint: frame_mission_start (10 Oct, 18:08 PDT)

VGAME 0x01E72 (`vgame_frame_mission_start`, samples the BIOS tick count's
low word) is matched, verified and committed/pushed. Four instructions:
zero AH, INT 1Ah (BIOS get-time), `mov ax,dx`, ret. Used the project's
generic `sm4_int` helper (it only cares about the INT instruction's
address and the original SP/return address, not which vector or whether
the service is host-synthesized or real guest code) rather than writing a
one-off dispatch. Routes only - 0 comparable states at any seed, same as
`vgame_isrclock_init`. Full gate PASSED clean (0 DIFFERENT routes, both
instruction profiles and all three matched-routine seeds zero mismatches,
every routes-only routine ran, exit 0). Scoreboard: 774 addresses matched,
769 of 1,535 census functions, 65,699 of 179,213 bytes; P2 39.83%, All
71.77%.

**Trap hit and recovered while picking this lead**: grepped `matched.c`
for the census's `0x00986` (5 hex digits, as the census prints it) and
found nothing, concluded it was unmatched, wrote and registered a full
duplicate `vgame_init_mode` there. The targeted lockstep immediately
showed two entries at the same address ("graphics mode setup" and mine) -
`matched.c`'s own table spells the same address `0x0986` (4 digits, no
leading zero), so the literal-string grep silently missed the existing
entry. Reverted cleanly before any build/gate cost. **Fix for next time**:
cross-reference by parsed/normalized address (int value), never by
grepping the hex string in either direction - a short Python pass that
parses `MATCHED[]`'s own `(binary, bank, offset)` triples and compares
them as integers against the census rows is in this session's history;
worth turning into a real tool (`tools/addrmap.py` is referenced in older
memory notes but does not exist in this checkout - it should be written,
not assumed).

**Also hit and resolved**: the first lockstep run (60,000 states,
background) stalled for ~34 minutes at ~10s of CPU time - not a code bug.
Rerunning the identical command (same binary, same states, same seed)
foreground completed in seconds with a clean result, so it was an
environment glitch (most likely the sync client locking the freshly
linked `func_lockstep.exe` on first launch; this repo's working copy
lives in a synced folder, see `build.cmd`'s own comment on this). If a
lockstep run seems to hang with near-zero CPU use, check `Get-Process`
CPU time against wall time before assuming the new routine is at fault -
a genuine infinite loop in guest code would burn CPU, not sit idle.

## Phase 2 checkpoint: init_models (10 Oct, 17:33 PDT)

VGAME 0x0BAA2 (`vgame_init_models`, copies a model-type's byte table) is
matched, verified and committed/pushed. 40 original instructions, one
data-dependent loop, a found/not-found branch. Lockstep found two real
bugs the draft check missed: a missed `je` on the no-block path left the
clock one instruction short, and the loop's exit test compared a local
C copy of the loop index instead of writing the real `mov ax,[bp-6]`
side effect back to `c->r[R_AX]` first, so AX read stale after the loop
ended. Both fixed; reverified clean at three seeds (17, 0, 1 comparable
states respectively, zero mismatching). Full gate PASSED clean: 35/35
routes IDENTICAL, both instruction profiles and all three matched-routine
seeds (773 routines, 2,505,089/2,504,224/2,505,038 states) zero
mismatches, every routes-only routine ran, no overruns, exit 0. Scoreboard:
773 addresses matched, 768 of 1,535 census functions, 65,692 of 179,213
bytes; P2 39.82%, All 71.77%.

## Phase 2 checkpoint: isrclock_init (10 Oct, 17:00 PDT)

VGAME 0x01C0E (`vgame_isrclock_init`, hooks the timer tick vector) is
matched, verified and committed/pushed. Small (18 instructions), entirely
linear, used the project's established `sm4_int`/`SM4_AT`/`SM4_NEED`
DOS-interrupt helpers (same family as `vgame_restore_vectors`) rather
than a literal instruction transcription. Unfuzzable by random-state
lockstep (0 states comparable at any seed - "routes only," confirmed by
the full gate's own routes-only check that it actually ran). Full gate
PASSED clean: 17/17 unit tests, 35/35 routes IDENTICAL, both instruction
profiles and all three matched-routine seeds zero mismatches. Scoreboard:
772 addresses matched, 767 of 1,535 census functions, 65,585 of 179,213
bytes; P2 39.77%, All 71.76%.

Also confirmed (useful for next time): the census's "implemented_by"
column is unreliable as a lead-filter - at least three addresses this
session (`0xEE0C`, `0x4D5D`, and others checked in passing: `0xE3BF`,
`0xDB3C`, `0x4E5F`) showed as unmatched in a fresh `census.tsv` regen but
were already matched and registered. Always grep `src/matched/matched.c`
directly for `VGAME_47304, 0x0000, 0xNNNN,` before spending time drafting
a "fresh" lead.

## Phase 2 lead attempted and reverted: VGAME 0x4657 (10 Oct, 16:00 PDT)

Extended the already-matched `vgame_release_count` (0x462E) to cover its
declined continuation at 0x4657 (countermeasure dispensing - the census
holds this as a separate "function," but it's really the original's
`jg`-taken continuation of the same routine, reusing its frame; see the
existing comment at 0x462E). Fixed a forward-reference build error, then
three seeds and the full 771-routine suite passed clean (up to 35,212
states at one seed, 0 mismatching). **The full required gate still
failed: 7 routes DIFFERENT.** Random-state fuzzing did not exercise the
actual bug; real gameplay routes did. The change was reverted
(`git checkout -- src/matched/matched.c`) rather than left half-fixed, and
the tree rebuilt clean against the last good commit (`6840bc7`). The
lesson generalizes: this routine's weapon-lock/dispense branches require
specific state combinations (a type argument correlated with specific
table fields) that pure random fuzzing essentially never hits, same as
`vgame_target_damage` earlier this session - the full gate's real routes
are the only check that actually exercises them. 0x4657 remains
unmatched; next attempt should add targeted PLANTS entries for the
countermeasure-type/slot-state correlation (same technique used for
`vgame_target_damage`'s weapon-lock match) before trusting lockstep
alone, and should re-run the full gate before considering it done, not
just the matched-routine suite.

## Phase 2 target-scoring checkpoint (10 Oct, 15:41 PDT)

VGAME 0x073C8 (`vgame_target_damage`, "score and alert a target hit") is
matched, verified and committed/pushed - the densest routine hand-matched
this session (174 original instructions, many branches, calls to both
matched and unmatched helpers). A freshly regenerated `matched_draft.py`
instruction listing was cross-checked opcode-by-opcode and branch-by-branch
before transcribing, which caught no logic errors - but random-state
lockstep testing caught **four separate instruction-clock accounting
bugs** the draft cross-check could not (a missed Jcc instruction's own
clock in three places, and two pop instructions never counted at all), each
found by an exact clock-value mismatch (e.g. "clock 1853 vs 1851") that the
harness's `--verbose` instruction-by-instruction retrace pinpointed
precisely. This is the clearest evidence this session for why the project
requires lockstep verification rather than trusting a careful manual
transcription: dense branchy code is exactly where a careful reread still
misses things. Added two PLANTS/ARG_PLANTS entries to
`tests/func_lockstep.c` targeting the routine's weapon-lock match condition
(E304/E306 fields); confirmed via temporary debug instrumentation (removed
before commit) that most of this routine's incomparable random states are
the *original* interpreter side failing to complete cleanly through its own
many unmatched callees (0x0D14, 0x792E, 0xC436, 0xB9F6, 0xB991, 0x0F3D,
0x8462, 0x56BB) before matched code is even invoked - not a flaw in the
matched C. Three seeds pass clean; the full 771-routine suite passes
(2,505,085 states, 0 mismatching). Required full gate PASSED
(`D:/f117-gate/gate-target-damage.log`, exit 0): 17/17 unit tests, 35/35
routes IDENTICAL, both instruction profiles and all three matched-routine
seeds zero mismatches. Scoreboard: 771 addresses matched, 766 of 1,535
census functions, 65,448 of 179,213 bytes; P2 39.69%, All 71.74%.

The owner asked three follow-up questions about Phase 1 this session,
answered in full in the conversation transcript (not repeated here in
detail, but the findings matter for the next session): (1) what P1=100%
means (parity against emulators, not real hardware - already correctly
scoped); (2) are GOG DOSBox/DOSBox-X/86Box checked equally (no - 86Box is
strictly the most scrutinized: it alone gets the 386-profile cycle-accurate
timing check and register-level AdLib comparison; GOG DOSBox is checked
only via a frozen capture, no live save-roster check; DOSBox-X sits in
between); (3) are any P1 thresholds arguably too loose (yes - the
timing/drift limits in particular have 2-3x headroom over the measured
baseline, while the exact-picture-count checks have none). Concretely:
`tools/ref86box/compare_timing86.py`'s `max_drift=2.2` (measured 1.113, i.e.
room for the measured value to literally double and still pass) and
`tools/pc_parity.py`'s `gog-music.max_timing_ms=40` (measured -14..+1ms,
~3x headroom) were flagged as the two worth tightening, with
`max_duration_diff=0.35` (measured 0.214) and DOSBox-X's `max_drift_ms=350`
(measured 143-200ms, but explicitly justified by the comment as absorbing
DOSBox-X's own real-time capture noise, not engine slack) as secondary
candidates. The owner asked to try tightening them; in progress as of this
handoff - see whether `compare_timing86.py` and `pc_parity.py` show edits
beyond this point, and if so whether they were re-verified against the
*existing* saved captures (`D:/f117-gate/pc-parity-profile-aware/`:
`86box-sound/trace/frames.csv`, `machine386/ours/frames.csv`,
`86box-timing.json`) rather than a fresh emulator run, to avoid re-running
86Box/DOSBox-X unnecessarily while the gate above was still finishing.

## Phase 2 aircraft activate/destroy checkpoint (10 Oct, 14:30 PDT)

The two Phase 2 candidates this handoff left prepared-but-untested
(`ai_activation_candidate.c` at VGAME 0x066F8, `ai_destroy_candidate.c` at
0x072D8) are now matched, verified and committed/pushed. Both were
independently cross-checked instruction-by-instruction against a freshly
regenerated `matched_draft.py` draft before trusting them (every opcode,
operand and branch direction agreed); activate's callees are all
already-matched, driver-free routines (cockpit message, target name, string
copy/concat), so it needed no harness change. Destroy calls the sound-priority
gate (`0xD3F9`) as a callee, and that routine's own far call to the
uninitialised driver thunk at `0xD414` is only stubbed to RETF when
`0xD3F9` itself is the routine under test, not when it is reached as a
callee from elsewhere - so every one of 4,000 random states ran off into the
thunk and 0 were comparable until `0x072D8` was added to
`stub_driver_thunks`'s VGAME ip-list in `tests/func_lockstep.c` (this matches
what the prior handoff had already anticipated: "they need returning driver
thunks in func_lockstep for 66F8/72D8 too"; in fact only destroy needed it).
After the fix: three seeds (default, 0xC0FFEE, 0xDEADBEEF) each pass both
routines cleanly (about 9,200-9,250 comparable states a seed, 0 mismatching),
and the full 770-routine suite passes (2,505,075 states compared, 0
mismatching). Required full gate PASSED (`D:/f117-gate/gate-ai-slots.log`,
exit 0): 17/17 unit tests, 35/35 routes IDENTICAL, both 5,718,912-state
instruction profiles zero mismatches, all three matched-routine seeds zero
mismatches, every routes-only routine ran, none overran. Scoreboard refreshed
(770 addresses matched, 765 of 1,535 census functions, 64,988 of 179,213
bytes; P2 39.45%, All 71.67%). `docs/architecture.md` and `docs/roadmap.md`
updated in the same commit. No further Phase 2 candidates are staged; next
session should pick a fresh lead the same way (`matched_draft.py`, cross-check
against the Reimp's data addresses, lockstep at 2-3 seeds, full gate).

The owner then asked what Phase 1 reaching 100% means, and separately asked
me to think hard about what in Phase 1 is doubtable and how to tackle it. My
answer (reproduced here so it isn't lost): parity is proven only against
emulators (GOG DOSBox, DOSBox-X, 86Box), never real period hardware - the
single biggest standing gap, not independently fixable by an agent, already
correctly scoped as "second reference, not the target" in standing project
guidance. Concrete, addressable residuals: (1) only 4 of 6 AdLib channels are
confirmed matching 86Box's captured audio in `docs/roadmap.md`'s own text -
the other two are never mentioned as matching or mismatching, which reads
like a quietly unresolved question rather than a closed one; (2) 3 of 1,332
DOSBox picture comparisons and 1 of 86 86Box picture comparisons are
unmatched and explained as capture-phase/mid-load timing artifacts, a
judgment call never cross-checked with a second independent capture; (3) the
EXEC file-load timing model is a real file's measured cost for only three
named files (PLAYER.EXE/DSWAP.EXE/START.EXE), a coarse 3-bucket synthetic-size
model for every other load, even though the same investigation proved
size-based costing is wrong in general. The owner asked me to tackle these "when you can." Investigated all three
(read-only; no new captures run) once the gate above freed the heavy-job
window, and all three check out as already properly handled - this was the
right outcome of actually tracing the evidence instead of trusting a terse
roadmap line:
- **AdLib channels 3/4**: `tools/ref86box/build_86box.md` (lines 187-195)
  already has a controlled experiment, not a guess - changing 86Box's own
  VGA speed (`B86_VGA_FAST=1`) shifts the reference's own channel-3 key-on
  count by about the same margin as the ours-vs-86Box gap (8,986 to 9,022 on
  the same reference, against our 8,982), showing the two busy pitch-bend
  channels are paced by the game loop's real-time speed and will not agree
  between any two differently-paced machines. Explicitly marked "not a
  parity target," correctly.
- **Picture residuals**: the 3-of-1,332 DOSBox misses already have the
  repeat-capture check I was going to propose - a second independent
  capture matches 1,321 with a *different* 11 unmatched, proving the
  specific misses are capture-phase noise, not a fixed defect
  (`docs/architecture.md:411`). The 1-of-86 86Box miss (`p085`) has a
  specific, falsifiable mechanism on record: 86Box's capture catches
  START's roster screen mid-load because this machine's file reads finish
  before the first paint while 86Box's don't; tracked explicitly in
  `expected_misses86.txt`, not hand-waved.
- **EXEC synthetic-size bucket**: confirmed it does reach the measured intro
  window (MGRAPHIC.EXE/MISC.EXE/ASOUND.117 load at 5.65-7.56s, inside the
  checked span) rather than being safely out of scope as I'd guessed - but
  `timing386.md` (lines 309-323) shows its net effect is already folded into
  the measured +39.3 ms end-to-end drift, which passes `pc_parity.py`'s
  limit; the one check that still fails (the 0.37 s longest-scene-duration
  gap) is a different metric, already shown unrelated to any load.

No code or doc change was needed for any of the three; this paragraph is the
record so the next session doesn't re-investigate the same ground. The
standing, unfixable-by-agent item remains: parity is proven only against
emulators (GOG DOSBox, DOSBox-X, 86Box), never real period hardware - already
correctly scoped in standing project guidance as "86Box is the second
reference, real hardware is the target," and not actionable here.

## Phase 2 collision/sound/scene checkpoint (10 Oct, 13:37 PDT)

Code checkpoint a18b771 is committed and pushed; origin/master and local
master agree. Final verification found a clean tree and no gate, probe or
reference-emulator processes. This handoff update follows the passing gate.

Profile-aware references checkpoint bbb6337 is committed and pushed; full
gate PASS596sec, all35routes, bothinstructionprofiles, all3matchedseeds,
17selected CTests, routes-only coverage and event limits. Phase1 complete.

Current additions: VGAME1007 vgame_scene_cell_obstacle (nearest eligible
terrain obstacle, signed Manhattan metric, original model probe), D3F9
sound priority gate with unconditional engine reassertion, D87C finish
terrain drawing/reset driver origin/work counters/retain view matrix.
Original instructions independently reread. Census1007 replacement-name
lead was wrong; existing78FD wrapper comment corrected too. All preserve
stack locals, call state, registers, flags and instruction counts; room is
checked at calls and loop boundaries. Returning driver thunks on both sides
make sound/scene random-state checks exercise their post-call paths.

First256-state check caught a missing RET clock in the new code; corrected
all3returns. Corrected256:492comparisons/zero differences. Two4000-state
seeds(default5EED0F117A,C0FFEE) PASS7763/7777comparisons, including partial
stops; 9.7sec concurrently,2workers. Logs scene-target-{256,4000,c0ffee}.log
and result.txt; build-scene-candidates.log,17selectedCTestsPASS. No target
processes remain. Refresh census now768addresses/763censusfunctions,
64423/179213bytes; P239.15%,All71.58%. Private candidates/drafts stay private.

REQUIRED full gate PASS883sec, gate-scene-sound.log and
gate-scene-sound-result.txt(exit0):17selectedCTests,35IDENTICAL route pairs,
both5,718,912-state instruction profiles zero, all three768-address matched
seeds zero; every routes-only routine ran and none overran. Stages92sec
translate/build/tests,495freshcoverage,11byte-identicalregeneration,
284parity;34interpreter sessions legitimately cached. All task processes
kept0x00FFFFFFaffinity. No gate/probe processes remain. Commit+push together.

Usage83% at13:52PDT; wrapping at the owner's85% threshold. Original broad
roadmap goal remains active. Next Phase2 candidates prepared PRIVATELY,
NOT built/tested/installed: D:/f117-gate/ai_activation_candidate.c (66F8)
and ai_destroy_candidate.c (72D8), instruction drafts drafts-next/*.c.
Both independently reread; activation's original coordinates are ZERO
extended before the 32-bit shift (Reimp sign-extends; preserve original).
Destroy masks zero-endurance flags to01C1; don't claim all repeated calls
unconditionally retain the dead bit. Verify every clock/room count before
trusting either candidate; add only after targeted checks and a full gate.
They need returning driver thunks in func_lockstep for66F8/72D8 too; consider
exact SPANS66F8..683D and72D8..73C8 to exclude nearby callees' RETs.

An optional HUD/text style question was sent during this turn; no answer
received as of13:52. No preference implemented. Keep this owner decision
open. Other roadmap work remains; don't mark the broad goal complete.

## Profile-aware references checkpoint (10 Oct, 13:20 PDT)

CPU/combat checkpoint 05bdd27 is committed and pushed. Investigation of the
remaining Phase 1 item found a reference harness error, not a CPU charge
defect: pc_parity fed its default 9 MIPS capture into the 386 held-picture
test. Separate recorded 386 capture now supplies 86Box pictures, AdLib and
timing. Held pictures pair by exact pixel hash in order; all local holds
must match. Screen-off gaps and the final censored reference hold are
handled explicitly. Limits unchanged. Eight new regression tests pass,
alongside nine threat-profile tests and six parity-diagnostic tests; CI
runs the new reference tests and the threat tests.

Fresh full references PASS, exit0, D:/f117-gate/pc-parity-profile-aware:
GOG1329 exact, no multi-sample misses, 57ms drift; DOSBox-X1237 exact,
three multi-sample misses,143ms drift; both sound checks pass. GOG22840
identical AdLib writes, -14..+1ms. 86Box85 exact/one close/no unmatched,
first2000 AdLib writes and four channels identical. All21 local held
pictures match21of22reference, maxduration.214022sec/startdrift1.112912sec.
Both roster saves802bytes/zero differences. Finalhangar6.472724vs6.491997sec
(19.273ms), instead of the mixed-profile .371sec discrepancy. Phase1 item
complete; progress updated, broaderroadmap remains active. No runtime changes.

Required full gate PASS (gate-profile-aware.log/result.txt; exact result
gate-profile-aware-result.txt):17selected CTests,35IDENTICAL route pairs,
both5,718,912-state instruction profiles zero, all three765-address matched
seeds zero; every routes-only routine ran, none overran. Coverage reused its
verified unchanged-input cache;34interpreter sessions reused. Commit and
push this checkpoint together, then continue Phase2 understood code. Keep
0x00FFFFFF affinity, no agents authorized, monitor this thread's rate_limits;
wrap and commit+push when 5h usage reaches85%. Last observed50% at13:18PDT.
Full references log pc-parity-profile-aware-console.log/result.txt (exact
result name pc-parity-profile-aware-result.txt), report86box-timing.json.

## Controlled CPU/combat checkpoint (10 Oct, 13:07 PDT; full gate PASS)

The owner reset usage and resumed. HEAD remains 30f463d, previously pushed
with the full 24.1-minute PASS gate. This checkpoint adds Python pilot/
comparator regressions and documentation; game/fix source is unchanged.
The required full gate PASS in594seconds (9.9min):17selected CTests,
35 IDENTICAL route pairs, both5,718,912-state instruction profiles zero,
all three765-address matched seeds zero, every routes-only routine ran,
none overran. Translation/build21sec, parity573sec; coverage/regeneration
reused the verified unchanged-input/generated-code cache. Logs
D:/f117-gate/gate-combat-alignment.log and result.txt (exact filename
 gate-combat-alignment-result.txt), exit0. No probes/gate processes remain.
Commit and push this checkpoint together before continuing source work.

CPU/enemy investigation now validates the identified optional repairs:
TTL+SLOT+PROX(v2)+ACCEL+REAL(v2). The corrected natural cohort completed
24 flights in 263.2 seconds, all 12 mission identities matching. Active
clock rates are 1.000065/1.000292 at 9/20 MIPS after a diagnostic three-second
terminal exclusion on exited flights; retain all rows/counts for combat.
Every sampled S is 8. Raw full rates include frozen death sequences.
Interpreter/native natural seed2500 hashes, launch lists and every CSV byte
match at both speeds (793c45a47070b53d / 8729c698e4bed4e5).

The residual v5 cohort gap was confounded: START mission alignment does not
align VGAME's later BIOS-tick RNG seed (C880 -> EE1A; stored DS:9540).
Flight seeds differ by 25-33 ticks. Initialization also takes different real
time, and the old 200 ms pilot samples different parts of each 125 ms step.
Normal hangar-delay inputs align the later seed; step-aligned whole-frame
holds remove most feedback timing differences. No guest memory staged.

Final controlled 12-pair result: every launch count identical, 11 pairs also
match burst counts, damage counters and orbit duration exactly. Korea22500
has 7/9 bursts, 16/20 total damage selections and a pilot input divergence
after a partial-frame sample at 288.112 seconds. A mapped 9-input replay at
20 follows the same initial path but crashes before combat: original input
poll timing still matters during slow frames. No high-speed enemy weakness
in this cohort; this is not a proof of universal statistical equivalence or
exact whole-flight CPU determinism. Identified weapon/clock defects are
repaired in the optional corrected mode. Default parity mode stays original.
CPU investigation may now yield to the broader roadmap. Read-only work has
identified the remaining "386" held-picture failure as a harness mix-up:
pc_parity.py passes its default9MIPS DOSBox capture to compare_timing86.py,
not the actual386 run. The picture is the final stationary hangar.
Private held_scene_fast.py matches the saved true386 capture's held pictures
by content (6-bit DAC -> 86Box's RGB/FNV64, alphaFF), not duration:21of21ours
match21of23reference in order, maxdurationdiff.214022sec<.35, maxstartdrift
1.112912sec<2.2. Hangar ours6.472724 vs86Box6.491997sec, difference19.273ms.
Infer screen-off gaps as blanks; raw posted-frame holds count missing scanout
and overstate the reference hold by57ms. Artifacts held-scene-fast.{json,log},
held_scene_fast.py. Source captures frames-realfiles/ours (settings confirms
--timing386) and pc-parity-realfiles/86box-sound/trace/frames.csv. Next retain
this content/profile-aware check, make pc_parity use a separate386 capture
for86Box while DOSBox comparisons keeptheir9MIPSprofile, and rerun the
references/gate. Don't alter CPU cycle charges to fit a mixed-profile test. Do not tune balance to cohort means.

Retained tools: threat_profile.py telemetry v6 records flight_seed, supports
--launch-delay-ms and --align-steps (requires D1REAL). Step pilot is a different
control policy than v5, so don't interpret its means as a before/after fix
comparison. Comparator --match-flight-seed requires the additional matching
seed; +step arms/cache tags prevent silently mixing pilot policies. Nine
unit cases pass. Public MiddleEast seed2500 native/interp replays at both
speeds match the private phase experiment: d4abd70c56500017 (9),
2619b559a5c10b3b (20), every launch record and CSV byte. Four boots146.5sec.

Artifacts D:/f117-gate: threat-realtime/, threat-realtime-summary.txt,
threat-phase-corrected.jsonl and summary.txt, flight-seed-*-600-phase.*,
flight-inputs-*.log, public-phase-*.{json,csv}, replay-korea_strike-22500-20.*.
Initial threat-phase.jsonl has unequal Kuwait seeds: don't use it as final.
Final20 launch delays by family2500/22500/25000: MiddleEast1813/1483/1483,
Korea1813/1813/1813, Kuwait1345/1345/1345, NorthCape1758/1813/1758ms.
Controlled cohort269.9sec/8workers plus three corrected Kuwait repeats.
All task processes use verified 0x00FFFFFF affinity (leave8logicalCPUs).
No agents authorized. Continue until roadmap complete or 5h remaining<=15%;
at that threshold wrap handoff, commit+push and stop. Usage was10% at12:33;
read only rate_limits events from this thread's rollout for current usage.

## Verified physics/clock checkpoint (10 Oct, 09:47 PDT)

Owner's 85% usage threshold was reached at09:22. Work stopped for wrap-up;
the already-running REQUIRED gate completed PASS at09:47 in1447.2seconds
(24.1minutes). All35 route pairs IDENTICAL, both5,718,912-state instruction
profiles zero mismatches, all three765-address matched seeds zero, every
routes-only routine ran and none overran.17selected CTests passed.
Stages: build/tests20sec, coverage508sec, byte-identical regeneration12sec,
parity907sec. Logs D:/f117-gate/gate-physics-realtime.log and result.txt
(the exact result filename is gate-physics-realtime-result.txt).
This source/documentation checkpoint is committed and pushed together.
No experiments or gate processes remain active. Resume only when the owner
resets usage. CPU/combat remains first, before the broader roadmap.

D1SLOT66a8608 is committed/pushed. This checkpoint adds:
D1PROX v2 exact rational continuous octagonal-slant sweep, terminal-speed
band*8/9 (explicit S9 balance choice, not recovered warhead constant).
D1ACCEL integrates9/2 incoming speed units per simulated second, exact
fractions S1..15 with original S9 pattern. Both use per-machine tracks
with epoch/weapon continuity resets. d1physics_check.py: all21 staged cases
both engines +/-options pass, first S9/player hashes unchanged. Legacy
five-case proximity checks pass;20,000-path independent C clipping oracle.

Completed72 natural physics flights,23.7min/8workers. Original24 pairs
match, fresh22500/25000 add8, all four27500 pairs differ and are excluded.
Across32 matching pairs,20-minus-9 bursts-1.5312 (95%CI-2.6250..-0.5312).
Weighted full sampled world-clock rates9=1.2327,20=1.1239. Physics alone
has NOT fixed CPU-dependent combat. Details/reproduction docs/speed-sweep.md
final section; artifacts threat-physics-v2/ and summary files. Natural20
MiddleEastoffset2720 interpreter/native hash3893586487063407 and everyCSVbyte/
launchrecord match; native9offset2500 hash9087c775185e262f. Regenerated
sentinels match pre-gate references exactly. Early exits retained.

New optional D1REAL pins S8 (S4 at2x) at D441 entry so matched override cannot
bypass; original derives all rates.4359 admits eight frames per machine
second, waiting at event boundaries, exact fractional clocks. Lossless
catch-up preserves elapsed time; pause3A4E and quit-dialog2071 explicitly
clear deadline. Per-machine state resets at program/speed changes. Default
off; use --present interp for smooth display (about125ms picture delay).
CPUs unable to execute8frames/sec can still lag. Normal-input public
 d1real_check.py passes all SEVEN cases at9/20/40 MIPS both engines, every
case hash identical:240frames/30world sec normal,80/20 compressed,80/10
restored,0/0paused,24/3resumed,0/0quit-dialog,24/3cancelled. Artifactdir
realtime-checks/, private run_realtime_checks.py. Latest build/tests PASS
154sec,17 selected CTests (build-physics-realtime-v2.log).

IMPORTANT D1REAL airborne validation is still OPEN. First v1 cohort exposed
lost time from >=2period resync (9MIPS orbit7.9534fps). Its gate/cohort were
stopped09:15; logs/data renamed *discarding-v1*. Source now has lossless
catch-up, but fresh natural flights were NOT started because usage hit85%.
Do not claim the overall CPU/enemy issue closed. Next session: first check
final gate/commit below; then run private threat_realtime.py (24flights,
8workers, four routes x seeds2500/22500/25000 x9/20, all five combat options).
Output threat-realtime/ (new, currently empty), metadataREALv2. Verify all12
mission identities; compare baseline physicsv2 same seeds and both CPU arms,
world-clock rates, early exits. Then private realtime_sentinel.py interp9/20
compares natural MiddleEastseed2500 hash/launchrecords/CSV with native cohort.
No builds while DLL probes active. Keep CPU/combat ahead of broader roadmap.

First physics gate FAILED1860.7sec despite35 identical routes and both
5,718,912-state profiles passing: rotating seed66A860898896 falsely counted
SETUP callee1886 RET as walker177C's own return under default300h code range.
Exact near/far walker SPANS and retained matched_initializer_bounds CTest
correct the harness; runtime matched code unchanged. Interrupted v1 REAL
gate already had all three765-address matched seeds zero after correction.
The final gate independently passed every verdict (above).

Reimp HEAD remains9e0716dc502cd64501b0d52030ebef041825f907: separates render
and simulation cadence, initialS5, but signedTTL and lifetime underflow remain.
No agents authorized. Original app default20MIPS/no limiter unchanged.
Private experiments D:/f117-gate; generated/game data never committed.

## Verified D1SLOT checkpoint 66a8608 (10 Oct, 07:00-07:51 PDT)

Clean starting checkpoint `0174a2c`; the owner resumed after the usage reset.
Current code is uncommitted: optional D1SLOT at VGAME 6CA6 prevents a
seeker-cleared incoming lifetime from underflowing. It uses the shared DEC
semantics for flags, preserves instruction count and declines every
positive-life/player-slot case. Earlier D1TTL/D1PROX behavior is unchanged.
New `tools/d1slot_check.py`: all 15 staged cases pass in both engines +/-
option, all eleven control hashes unchanged (about two minutes, four boots).
Incoming slots 0/7 and player slot 8 are covered. Full CTest also passed all
15 tests (573 s, including its expensive serial 2,000-state matched check).
Use the gate's normal 14-test selection rather than repeating that serial
check unnecessarily.

All 72 natural flights are complete (10.4 min, 12 workers):
`D:/f117-gate/threat_slot.py`, `threat-slot-console.log`, `threat-slot/`.
Four routes x six seed families x (9 TTL+PROX, 9/20 TTL+PROX+SLOT), reusing
the earlier 20 TTL+PROX arm after its control reproduced `7b5cc08d65027187`.
All 24 mission identities match across all four arms. Adding SLOT increases
bursts/flight by 2.1667 at 20 MIPS and 2.375 at 9. With the same three options,
20-minus-9 bursts are -0.9583 (95% paired CI -2.2083..+0.25): not evidence
of equivalence. More damage changes survival/exposure; retain early exits.
Full figures and reproduction are appended to `docs/speed-sweep.md`.

Fixed interpreter/native Middle East 20 MIPS offset2720 matches hash
`1f85212a6ffbf801`, CSV bytes and all missile records: 16 launches, 9 bursts,
20 total damage selections, early exit. Raw fixed 9/20 traces (`ghost-slot/`)
have zero abandoned-slot episodes/slot-seconds and match cohort hashes
`17725de70b98ba07` / `1f85212a6ffbf801`.

Independent constant-speed/heading SA-12 pass (`proximity_pass_probe.py`,
`proximity-pass-{interp,recomp}.json`) still hits at S=8 and misses at S=15
with TTL+PROX+SLOT. Both engines match, hashes `f2a3a1eb93590c5a` /
`a3d3b2e93b8afe35`. Speed28, zero agility, terminal speed frozen28,
lateral22/longitudinal70/altitude100. The PROX S9 floor24 is below closest
slant25.125, while original S8 reach28 hits. First probe allowed acceleration
and both hit; do not confuse it with the corrected frozen-speed result.

REQUIRED FULL GATE PASSED: `D:/f117-gate/gate-d1slot.log`, result
`gate-d1slot-result.txt`, exit 0, 1588.4 s. All 14 CTests, 35 identical route
pairs, both 5,718,912-state instruction profiles and three 765-address
matched seeds passed; every routes-only routine ran and none overran.
Coverage added no code. The attempted ctypes affinity wrapper failed;
corrected parent/descendants early in coverage and verified 0x00FFFFFF.
`affinity_launch.py` now uses pointer-width ctypes signatures, checks native
return values and reads the resulting mask. The earlier cohort's affinity
attempt was likewise ineffective (only 12 workers, so still limited jobs).
Actual Processor % Processor Time was 52% during parity while WMI incorrectly
reported 96%. Prefer the former for load checks.

This checkpoint has passed the required gate and is ready to commit/push.
Then repair remaining proximity
sampling with a physical band plus swept path, rather than raising a floor
to match cohort means. Private `swept_design_check.py` checks exact octagonal
slant minima at piecewise-linear breakpoints: 2,000 random paths, reversal
and subdivision invariant and <=202,000 sampled distances. This is only a
geometry design check, not a game fix. Guidance acceleration increments one
unit on odd frames (7209..722B), with no S normalization; independently
confirmed by `acceleration_probe.py`: normal SA-5 initial1/terminal28,
steering disabled, after two simulated seconds speed9 at S8 versus16 at
S15, both engines hash `c48afd58d9ae6ea2` / `fc9ee748c9424f67`. This
favors high-S acceleration and cannot alone explain weaker combat.
The regenerated native fixed flight (`d1slot-regenerated-native.*`)
retains the pre-regeneration hash, every launch record and every CSV byte.
No probe or gate process is still running.
No claim CPU/combat is solved; do not move to the rest
of the roadmap yet. The Reimp HEAD remains `9e0716dc502cd64501b0d52030ebef041825f907`.

## Previous session checkpoint

**This batch (9-10 Oct): four independent pieces of work, three committed and
pushed, one left as an investigated-but-unwritten lead.** The owner asked for
all four to be touched in one session (p037, EXEC/overlay charging, a
controlled threat-profile measurement, and a Phase 2 routine), then mid-
session set the goal to complete the roadmap, then changed it to wrap up for
handoff. Three landed cleanly; the fourth (Phase 2) produced a well-verified
*identification* of a routine worth matching next, not a committed match -
writing one by hand carries real correctness risk (room-claim coverage,
preserving every intermediate memory write across a possible mid-routine
decline) that this session chose not to commit without a lockstep pass, given
the time left. See item 4.

## Current top priority: CPU speed and enemy effectiveness (10 Oct investigation)

Latest code checkpoint: `5fcdd04`, committed and pushed to `origin/master`.
The full gate and all private cohorts/probes below are complete; no driver
is still running. Continue with the underflow repair below before the
broader roadmap. All generated code, game files and experiment output
remain outside this repository.

The owner wants this resolved before continuing the roadmap goal, and asked
for both a Reimp check and an independent investigation. This checkpoint
adds two optional mitigations and corrected measurements; do not mistake
the earlier matrix for sound combat evidence.

- The Reimp at `9e0716dc502cd64501b0d52030ebef041825f907` decouples simulation
  from rendering, paces simulation from S, starts S at 5, and uses a 60 Hz
  real-time game clock by default. Its `frame_weapons.c` still compares
  incoming lifetime signed. Its simulation-clock tests check display-rate
  independence, not combat. Older catalogue claims about S oscillation are
  not a demonstrated cause in the original's full loop.
- Independently confirmed signed-life bug: ground SA-5 life is `150*S*16`,
  crossing 32767 at S >= 14. VGAME 708D treats nonzero life as active, while
  6F19's signed JLE suppresses damage. Optional `D1TTL` admits valid unsigned
  life through 36000, retaining suppression of abandoned FFFF slots. All
  seven staged cases pass in both engines, fixed/unfixed, with identical
  engine hashes. Natural 600-second fixed Middle East flight at 20 MIPS,
  offset 2720: SA-5 bursts at 293.32 seconds instead of missing; interpreter
  and recomp hash `0559ff3b220737ef`, CSV and launch records identical.
- The old observer hid negative SA-5 lifetimes, missed burst resets between
  polls, and lost damage counters on exit. Seeker 72A5 can clear life before
  move 6CA6 decrements it to FFFF; these abandoned slots are not new launches.
  Current tools read the official launch count and valid unsigned life,
  detect burst crossings, retain early-exit damage, and record S/counters/
  detection inputs. Damage counts selections, not missile impacts.
- The old adaptive pilot rotated at fixed instruction deadlines (8.5 s at
  20 MIPS versus 18.9 s at 9), causing matched fast missions to crash before
  engagement. Startup now keeps authored 9 MIPS seconds. Its orbit also had
  incorrect negative wrapping and outward radius correction; both fixed and
  checked in eight directions. Historical combat conclusions are superseded
  in `docs/speed-sweep.md`.
- Aircraft fire checks are staggered once per 16*S frames at stable S, not
  per frame; ground 5046 runs every frame (visibility skips only repaint).
  Detection has no direct S input. Corrected in `docs/bugs.md`.
- A second mechanism remains: proximity reach `(speed<<3)/S` shrinks with S.
  A staged SA-12 hits at S=8 but misses at S=15 with positive life and equal
  starting geometry. Experimental optional `D1PROX` gives incoming reach an
  S=9 floor, without changing movement/guidance/clocks or limiting frames.
  This is an explicit balance choice, not a recovered original radius.
  Built successfully (108 s). All five staged cases pass under both engines
  +/- D1PROX; engine hashes match and the S=8/9 control hashes are unchanged.

Running private cohort: `D:/f117-gate/threat_aligned_v5.py`, 96 flights,
4 routes x 6 seed families x (9, 16, 20, 20+D1TTL), 600 seconds, 12 workers.
Offsets 110/220 ms at 16/20 align START seed families; verify mission fields
rather than assume alignment. Output `D:/f117-gate/threat-aligned-v5`, console
`threat-aligned-v5-console.log`. All 96 are complete; all 24 mission identities match across arms.
Official launches/flight: 8.5417 / 8.4583 / 7.7500 / 7.7083; bursts:
4.1250 / 3.5000 / 2.7500 / 2.8333. 20-minus-9 launch CI includes zero,
but burst CI is -2.1667..-0.6250. Full report is appended to speed-sweep.md.
Public comparator output: `D:/f117-gate/threat-aligned-v5-summary.txt`. Earlier
v3/v4 experiments are diagnostic history, not the final cohort. Final staged
TTL outputs: `D:/f117-gate/d1ttl-final-{engine}-{False|True}.json`.

Both cohorts are now complete: 96 baseline/TTL flights and 24 combined
D1TTL+D1PROX flights, every mission pair matching. Combined 20 MIPS:
7 early exits, 7.3333 launches, 3.375 bursts, 6.75 damage selections per
flight, 4669.8 orbit seconds. Against 20+D1TTL, burst gain +0.5417
(95% paired CI +0.2083..+0.8750). Against 9, a deficit remains: -0.75
bursts (-1.4167..-0.1667). The options are partial mitigation, NOT a
completed CPU/combat fix. Full report and artifact paths are in
`docs/speed-sweep.md`; comparison tool is `tools/threat_compare.py`.
Combined natural interpreter/recomp flight (Middle East, 20 MIPS,
offset 2720) is identical: hash `7b5cc08d65027187`, all CSV rows and launch
records equal. D1PROX staged five-case checks agree in both engines +/-
option, S=8/9 control hashes unchanged. All 14 C and 97 Python tests pass
(one Python test skipped).

Full required gate PASSED (1,499 seconds):
`py tools/build_recomp.py --data D:/GOG/F-117A --seed-coverage C:/Users/Tideg/f117-recomp-local/coverage --jobs 16`
Log `D:/f117-gate/gate-combat.log`. All 35 routes identical, both
5,718,912-state instruction profiles and all three 765-routine seeds zero
mismatches; 14 C tests passed, every routes-only routine ran, no overruns.
Coverage added no code; 34 fresh interpreter sessions. The gate launches
three eight-shard matched seeds concurrently,
so 16 route workers still oversubscribed 32 threads; restricted the gate
parent and its 49 descendants to affinity mask 0x00FFFFFF (24 logical CPUs),
leaving eight for the desktop. Future children inherit that mask. This is
process-local and ends with the gate, not a persistent machine setting.

This checkpoint includes four new files (`d1ttl_check.py`,
`d1prox_check.py`, `threat_compare.py`, `tests/test_threat_profile.py`).
Do not call the CPU/combat issue solved. Investigate the residual before
moving to the rest of the roadmap. The 9 MIPS cohort has burst-time S mostly
7/8 (23/46 of 99 bursts; mean 8.6465), versus the parked S=9 floor chosen by
D1PROX. Any different calibration should be tested on new seeds, not tuned
until this cohort matches. Continuous swept collision with an explicit
reference radius is another possible design direction; not implemented.
Seeker-abandoned life can still occupy a slot (72A5 clears before 6CA6
underflows); D1TTL guards those FFFF values from damage but does not repair
that separate slot-reclamation defect.

Raw-slot follow-up is complete: `D:/f117-gate/ghost_trace.py`, outputs
`ghost-trace/{9,20}-2500.json`. Middle East hashes reproduce the baseline
cohort exactly. Abandoned slot occupancy over the common first 472.9 s is
1104.8/1295.2 slot-seconds at 9/20 MIPS; whole 20 flight is 1894.0. Both
still launch 11 times. Ground 54B4/air 65C3 select slot index & 7 and require
zero life, so global saturation is unnecessary to block a given launcher.
This defect also remains in Reimp. Test a separate saturating-countdown
option rather than silently changing the already-measured D1TTL behavior.

Held-out cohort complete: `D:/f117-gate/threat_heldout.py`,
console `threat-heldout-console.log`, output `threat-heldout/`. Four routes
x new seeds 15000/17500/20000 x (9 unmodified, 20+D1TTL+D1PROX), 600 s,
eight workers, parent/children affinity 0x00FFFFFF shared with gate; CPU was
49% before launch. Took 9.9 min, no tool errors, all 12 mission pairs match,
three early exits per arm. Bursts 4.5 at 9 versus 4.0 at 20; paired difference
-0.5 (95% -1.75..+0.6667). Pooled with the earlier 24 pairs: -0.6667
(-1.25..-0.0833), so CPU/combat remains open. No calibration changes were
made. See `threat-heldout-summary.txt` and the appended speed-sweep report.

Staged underflow reproduction now passes in both engines:
`D:/f117-gate/slot_underflow_probe.py`, `slot-underflow-{interp,recomp}.json`.
SA-5 class 1, off-axis behind the weapon, positive life 1000 -> FFFF at
both S=8 and 15, no damage, identical per-case engine hashes
`76d07a07e0d330d9` / `7828a2d70478e5a1`. The initial exact-180-degree setup
did not cancel (signed abs edge); corrected to an off-axis bearing.
Next implementation candidate: a separate optional hook at VGAME 6CA6
that keeps incoming life zero when the seeker has cleared it, while
retaining DEC flags and leaving every positive-life countdown untouched.
Test zero/one/positive-life controls, both engines and S values, then natural
flights. Compare 9 and 20 with the SAME option set as well as the original
9 MIPS balance reference. Do not silently change D1TTL: earlier cohort
numbers measure its signed-guard repair alone. A counter bound is not a
permanent identity check; an old abandoned value could decay below 36000.

The active goal remains roadmap completion, with the user's condition to
wrap/commit/push/stop at <=15% five-hour allowance remaining. Local session
rate-limit telemetry last reported 84% used (16% remaining), near the wrap
threshold. Check fresh telemetry before starting another code/build cycle.

## What to do first

1. **`git status -sb` first.** By the time this session ends, master should be
   clean and pushed through the real-file EXEC calibration commit (below);
   confirm that landed before doing anything else. If the working tree is
   *not* clean, something interrupted the final commit - read what is staged
   and finish it or roll it back deliberately, do not assume it is abandoned
   work (this session itself found a previous session's uncommitted,
   finished-looking diff sitting in the tree at start - investigated, found
   sound, committed it. The reverse mistake, deleting someone's good work, is
   worse than the delay of checking).

2. **p037.png, investigated and closed** (`66a664e`). A single captured
   instant in the title screen's fade-in: our nearest frame already beats
   both frames next to it, so 86Box's capture simply lands between two of
   ours. Added to `expected_misses86.txt` with that reasoning. Closed.

3. **EXEC and overlay loads, charged under the 386 profile, in two steps**
   (`05b453c`, then a further real-position calibration - confirm its commit
   landed, see item 1). Both `dos_load_program` and `dos_load_overlay` read
   the whole file through `dos_read_whole` with no charge at all before this.
   - Step one (`05b453c`): three measured sizes (1 KB, 9.5 KB, 47 KB -
     `exec1989.py`), charged by which a real load is closest to
     (`T386_EXEC_SMALL/MEDIUM/LARGE`). A fifteen-size follow-up
     (`exec_sizes.py`) came back non-monotonic in size (2 KB cost less than
     1 KB; 40 KB cost almost double 47 KB): EXEC's cost is seek-distance
     dominated, like an ordinary read, not size dominated - the same shape
     problem `T386_FILE_READ_SEEK` already has. Intro end drift: +428.5 ms to
     +96.4 ms, same 1,237 of 1,275 pictures exact. Gate:
     `D:/f117-gate/gate-execfix.log`, 35 routes identical, 0 mismatches.
   - Step two (real positions, 10 Oct): PLAYER.EXE, DSWAP.EXE and START.EXE -
     the three loads that actually dominate the drift - measured directly at
     their own position on the reference disk (`realexec.py`: open, read-
     whole, close, by name, already in place), not as same-size synthetic
     stand-ins. All three cost 27-45% more than their synthetic equivalents.
     `T386_EXEC_PLAYER/DSWAP/START` charge these three named files their own
     cost; any other load still uses the synthetic-size tiers. Drift: +96.4 ms
     to +39.3 ms, same 1,237 of 1,275 exact. `pc_parity.py` now passes the
     86Box picture check too (p037 above); one check still fails, the
     longest-scene timing difference at 0.37 s against a 0.35 s limit - a
     *different* held picture than the measured loads (ours 100.22-106.40 s,
     86Box's counterpart 150.33-156.88 s), unmoved by either EXEC charge.
     **Confirm the gate for this step passed and the commit landed**
     (`D:/f117-gate/gate-realfiles.log`; it was mid-run - specifically its
     verbose MISC.EXE coverage route, which writes one log line a clock while
     genuinely computing, not hung - when this session had to stop watching
     it). If it did not land, the diff is `src/cpu/timing386.h`,
     `src/machine/dos_programs.c` (plus the two doc updates below) and is
     safe to finish: the build already succeeded and `pc_parity.py` already
     passed on it (`D:/f117-gate/pc-parity-realfiles.log`) before the gate was
     started.
   - Next step, not started: identify the held picture behind the 0.37 s
     longest-scene gap (it is not a program load) and what inside its span
     the original spends 0.37 s longer on.

4. **Controlled threat-profile combat measurement, complete** (`77632c6`,
   `da7409a`). The existing typed routes pass the primary target once and
   hold a straight heading after, drawing about a quarter of a launch a
   flight - too sparse to show a speed effect. `tools/threat_profile.py`
   instead approaches and strikes the target as `strike_pilot.py` does, then
   orbits the target's own coordinates (no enemy site position read or
   assumed) for the rest of the flight. 144 flights (four ground-strike
   theatres, three speeds, twelve boot-clock seeds), complete, no errors:
   **a real fall in enemy launch rate from 9 to 16 to 20 MIPS** (9.08, 6.48,
   3.81 launches a flight; bootstrap 95% intervals on the mean exclude zero
   for both differences from 9 MIPS), surviving normalising for orbit
   exposure time (so it is not just shorter flights from earlier bingo fuel
   at higher speed). Misses, bursts and player hits taken all fall by about
   the same proportion too. **This is now the current top priority** - see
   "Current top priority" above for what has been found about the mechanism
   and the concrete next steps. Raw data: `D:/f117-gate/threatmatrix/
   runs.jsonl` and `samples/`.

5. **Phase 2 lead, investigated, not written: VGAME 0x01007.** The census
   (`D:/f117-gate/census.tsv`) names this address `scene_world_replace`, but
   that is wrong - cross-checking the disassembly's data addresses (0xC6B2,
   0xBA42, 0x49AC/AE/B0, 0xE476, 0x8590, and the per-type tables at 0x0960 and
   0x0511/0x0510) against the Reimp's source by *address*, not by the
   census's name guess, finds an exact match to a different function:
   `scene_obstacle_probe` (`../F-117A Reimp/src/core/scene.c:8`), the
   terrain/object collision probe - the routine behind the `0xC6B2 -> 0x9F96
   -> flight_end(2)` chain this project's own notes already reference. Fully
   traced against the disassembly (`matched_draft.py --ip 0x1007`, kept at
   `D:/f117-gate/drafts/vgame_lead_1007.c`, never commit the draft itself) and
   confirmed instruction-by-instruction, including resolving what its three
   callees are: `0x0810` is **already matched** as `vgame_scale_by_level`
   (`scene_cell_scale`), `0x0871` is **already matched** as
   `vgame_scene_cell_index`, and `0xF018` is the runtime's shared unsigned
   32-bit shift (referenced in `vgame_scale_by_level`'s own comment) - DX
   carries through unmodified between a `scale_by_level` call and the
   following `0xF018` call, which is why the routine never touches DX
   explicitly despite shifting a 32-bit value. `0xEE0C` (abs16, one word
   argument) is not yet matched but is trivial (8 instructions, drafted at
   `D:/f117-gate/drafts/vgame_lead_ee0c.c`) and is a good small first step
   next time. A full idiomatic rewrite of 0x1007 itself was drafted
   (not committed - it lives only in this transcript and a scratch file,
   `.../scratchpad/scene_obstacle_probe.c`, not in the repository) but never
   built or lockstep-checked, and should not be trusted without that: the
   room-claim at each of its ~12 call/loop boundaries was counted by hand
   from the instruction draft and has not been verified, and getting a room
   claim wrong is the kind of bug that stays silent until a route runs out of
   budget exactly inside this routine (see `f117r-room-claim-must-cover-
   longest-path` in the project's memory). Before trusting or committing it:
   rebuild, run `func_lockstep` targeted at this address with a few seeds,
   and only then fold it into `src/matched/matched.c` near
   `vgame_scene_cell_index` (both are in the same `NEAR_THEN`/`FRAME` macro
   scope, matched.c lines ~13430-16091) and the gate.

## Goal and standing decisions

- **Combat rate falls with speed - STALE, see the correction at the very
  top of this document.** This was real in early, less-corrected cohorts;
  the investigation's own final, most-corrected cohort (after fixing two
  measurement confounds, not just the game's physics) found no
  statistically significant difference. Read the top-of-file correction
  and `docs/speed-sweep.md` end to end before treating this bullet as
  current.
- **Phase 1's remaining item: the last 86Box timing check.** One check remains
  failing in `pc_parity.py`: the longest-scene duration difference, 0.37 s
  against a 0.35 s limit, in a held picture from 100.22 s to 106.40 s (ours)
  that is not the program-load stretch already fixed. See item 3's last
  bullet.
- **Phases.** Phase 1 (parity, including the 386 profile) is close:
  `pc_parity.py` has one failing check left (above); the 386 profile's
  roadmap checkbox should be closed once that is resolved or understood.
  Phase 2 (named, matched code): 765 of 1,535 census functions matched; see
  item 5 for the next lead. Phase 3 (fixes and presentation): open items are
  owner decisions (the HUD/text at 4K) or need a real display (Phase 4
  overlap). Phase 4 (checked by people): untouched, and most of it - a person
  playing it, a listening check - cannot be done by an agent at all; that is
  not a gap to close, it is the phase's own definition.
- **Public repo, code only.** No game data, no generated C. Never commit
  `references/` (git-ignored).
- **No AI attribution** in commits or PRs (the owner's global CLAUDE.md
  outranks harness reminders that ask for it).
- **Commit and push together**: a code change is pushed after the gate
  passes; docs-only commits may follow. `py tools/progress.py --title` prints
  the scoreboard every commit title starts with.
- The owner is not a software engineer: explain trade-offs plainly and flag
  overengineering.
- **Check system resources before stacking parallel jobs.** This session ran
  a 24-job combat sweep, the full gate, and `pc_parity.py`'s three emulators
  at once; CPU pinned at 99% and the owner's own typing lagged before they
  noticed. Check `Get-CimInstance Win32_Processor | Measure-Object
  LoadPercentage -Average` before adding a second heavy job, and know that
  stopping a background task running a Python `multiprocessing.Pool` (as
  `threat_profile.py` and `speed_sweep.py` use) does not kill the pool's
  workers - they are children of the script's own process, invisible to the
  task-stop mechanism, and must be found and killed as a process tree
  (`Get-CimInstance Win32_Process -Filter "ParentProcessId=..."`,
  recursively). See `f117-check-resources-before-parallel-jobs` in the
  session's saved notes for the full account.

## Where things are

- Game: `D:\GOG\F-117A` (never mounted or written by a tool; copy per run).
- Generated C: `C:\Users\Tideg\f117-recomp-local\gen` (never committed);
  coverage in `...\coverage`; route runs in `...\runs`.
- This session's artifacts (all on D:, private, not committed):
  `D:/f117-gate/p1-dos-probe/exec_sizes.py` and `realexec.py` (the EXEC-size
  and real-file probes, with their `sizes1`/`realexec1` outputs);
  `D:/f117-gate/frames-execfix`, `frames-realfiles` (the two `frames386.py`
  before/after runs, against the saved `D:/f117-gate/frames-1989/box`
  86Box capture); `D:/f117-gate/pc-parity-execfix`, `pc-parity-realfiles`
  (the two `pc_parity.py` runs); `D:/f117-gate/gate-execfix.log`,
  `gate-realfiles.log` (the two gates); `D:/f117-gate/threatmatrix/` (the
  144-flight combat sweep, `runs.jsonl` and `samples/`);
  `D:/f117-gate/drafts/vgame_lead_1007.c` and `vgame_lead_ee0c.c` (Phase 2
  drafts, never commit).
- Research (read only): `D:\f117-gate\macresearch\` (Mac 2.1, 2.3.1 and 2.3.2
  listings, the Amiga listing, `dos_ctl_sim.py`). The archives are in the
  Reimp's `reference\` folder.
- 86Box (`D:\86box`): `vmt386dos500` (the default reference; 1 MB, the 1989
  3500 rpm drive preset), `vmt386dos500h` (4 MB, HIMEM.SYS, DOS=HIGH; not the
  reference), `vmt386` (FreeDOS), `vmt386dos401`, `vmt386dos622`. Built by
  `tools/ref86box/build_msdos_vm.py` from the owner's copies in `references/`.
- Worktrees (`D:\f117-wt`): every Phase 2 and live-path branch is merged into
  master. Remove merged worktrees when convenient (`git worktree remove`).
  **Something else was working in this same checkout concurrently with this
  session** (a handoff commit, `65435f2`, appeared mid-session describing
  this session's own then-uncommitted work as if ending a separate session) -
  if a parallel-agents workflow is in use, confirm it is using its own
  worktree (`D:\f117-wt\...`), not this main checkout, to avoid two sessions
  racing on the same working tree.

## Build and check

- **Build** (PowerShell, not Git Bash's `cmd /c` - that rewrites paths and
  can silently no-op; see "Traps" below): `cd` to the repo in PowerShell,
  then `.\build.cmd -DF117R_BUILD_APP=OFF` (core, `f117run`, tests) or
  `-DF117R_BUILD_APP=ON` (adds the SDL app `f117a`). Look for `BUILD OK`.
- **Gate**: `py tools/build_recomp.py --data D:/GOG/F-117A --seed-coverage
  C:/Users/Tideg/f117-recomp-local/coverage`, about 20-30 minutes, run in the
  background, nothing else heavy at the same time (see "Check system
  resources" above). Read the tally, not the exit code: unit tests (14, 0
  failed), steps 6/6b (every instruction against the interpreter, both
  timing profiles, 0 mismatching), 7/7b/7c (every matched routine at three
  seeds, 0 mismatching), 8 (every routes-only routine ran), 9 (no OVERRUN),
  and the count of routes reported `IDENTICAL` (35, going in).
- **PC parity**: `py tools/pc_parity.py --data D:/GOG/F-117A` (about 12-15
  minutes, runs three emulators at once - its own internal parallelism is
  fine, just do not add a second heavy job alongside it). All checks pass
  except the 86Box timing check (above).
- **Matched lockstep**: `py tools/func_lockstep_par.py --states 4000
  --verbose [--seed S]` (eight shards, about 3 minutes). New routines start
  from `py tools/matched_draft.py --data D:/GOG/F-117A --module VGAME.EXE
  --ip 0xNNNN`; drafts are never committed.
- **Census**: `py tools/reimp_names.py --reimp "../F-117A Reimp" --gen
  C:/Users/Tideg/f117-recomp-local/gen --out D:/f117-gate/census.tsv`, then
  `py tools/progress.py --census D:/f117-gate/census.tsv`. **The census's
  name guess can be wrong** (item 5 found one): verify a lead by cross-
  referencing the disassembly's actual data addresses against the Reimp's
  source, not by trusting the name alone.
- **Threat-profile sweep**: `py tools/threat_profile.py --data D:/GOG/F-117A
  --out DIR --routes R... --speeds 9 16 20 --seconds 600 --offsets-ms
  0 2500 ... --jobs N` (N sized to leave headroom for anything else running;
  resumable - unfinished tags are simply absent from `runs.jsonl`).

## Current state

- **Scoreboard**: P1 97.93%, P2 38.88%, P3 67.00%, P4 42.90%, All 70.63%
  (unchanged this session: nothing new was matched or named, only timing and
  measurement work).
- **Phase 1**: see items 3-4 above. The 386 profile is close to done; one
  86Box timing check remains.
- **Phase 2**: 765 of 1,535 census functions matched (63,917 of 179,213
  bytes). See item 5 for the next lead (`vgame_scene_obstacle_probe`,
  VGAME 0x1007) and `vgame_abs16` (VGAME 0xEE0C, trivial, drafted) as an
  even smaller first step.
- **Phase 3**: 12 switches including D1TTL and the D1PROX balance option;
  10 of 14 top-level catalogued defects have a switchable fix. CPU/combat
  remains open; signed life and proximity distance are partial mitigations.
  Open: D10, D35, D36 (see `docs/bugs.md`). Presentation: the owner's open
  decisions (HUD/text at 4K) and a real-display check remain.
- **Phase 4**: untouched; most of it requires a person.

## Traps that cost time

- **PowerShell, not Git Bash, for `build.cmd`.** `cmd /c ".\build.cmd ..."`
  from the Bash tool gets its path rewritten and can print only the `cmd.exe`
  banner with no actual build - the log then looks like a trivial no-op
  relink (a handful of "Re-checking globbed directories" / "Linking" lines)
  and is easy to mistake for "nothing changed." Confirmed this session: the
  fix is to run `build.cmd` from the PowerShell tool directly, not wrapped in
  `cmd /c` from Bash.
- **A background job can look stalled when it is not.** `build_recomp.py`'s
  coverage step over a route that touches MISC.EXE's self-modifying code
  writes one log line a clock tick - tens of thousands of lines, the same
  few bytes repeated - and a `tail` taken mid-burst looks identical call to
  call. Check CPU delta on the actual process
  (`Get-Process -Name f117run | Select Id,CPU`, twice, a few seconds apart)
  before assuming a hang.
- **Shell and Python escapes**: inline Python heredocs turn `\n`, `\f`, `\a`
  and `\d` into real characters. Use the Edit or Write tool for strings with
  escapes, or a script file.
- **Git Bash and cmd**: see above; `MSYS_NO_PATHCONV=1 cmd /c "C:\...\
  build.cmd" "-DF117R_GEN_DIR=..."` with the absolute path also works from
  Bash if PowerShell is not available, but PowerShell is simpler and was
  what this session used after discovering the problem.
- **Room claim must cover the longest path** (see item 5): a wrong `room(c,
  N)` in a hand-written matched routine hides until a route happens to run
  out of its instruction budget exactly inside that routine, which can be
  billions of instructions in. Count every segment between calls/loop edges
  from the tool-generated draft, not from a mental re-derivation, and when
  in doubt over-claim (always safe) rather than under-claim.
- **A synthetic same-size file is not a stand-in for a real file's EXEC
  cost** (item 3): EXEC's cost is dominated by where the file lands on disk
  relative to the previous access, not by its size. Measure the real file at
  its real position when the file's identity is known (`realexec.py`'s
  approach), rather than inserting a differently-positioned same-size copy.
- **Background work**: a killed run can leave emulator processes holding
  files; check with `tasklist`/`Get-Process` before and after. Run long jobs
  in the background; never sleep in the foreground; use `Monitor` or
  background-task notifications to wait, not chained `sleep`.
- Game keys (Key Control Card): 0 brakes, 6 gear, 8 bay, Space select weapon,
  Enter fire, Backspace cannon, B select target, Shift+F10 eject. `f117run`
  input: Space is `\s` in route files; at most 4096 inputs.
- DOSBox 0.74 sees posted keys only with `SDL_VIDEODRIVER=windib`; DOSBox-X
  does not take posted keys; the release 86Box cannot be typed into (use the
  VNC build, `tools/ref86box/build_86box.md`).
