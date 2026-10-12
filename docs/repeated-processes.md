# Repeated processes

Every process this project runs more than once, what it costs, what it
duplicates and what has been done about it. Update this page whenever a
process is added, measured or changed; a new repeated step goes here first.
Timings are wall clock on the development machine (32 threads) and say
whether they were measured or estimated. The rule behind the page: an
optimisation must leave every check's inputs, outputs and verdicts
identical, and the proof is the same verdicts before and after.

## The gate (`tools/build_recomp.py`)

Private follow-up (11 Oct): START 3641 wraps fade-out, picture/palette load,
full-page copy, optional overlay callback, fade-in and present. Initial raw
64-state test compares zero states (callee completion limits); do not count
that as parity evidence. Returning fade/load/palette and optional-overlay
fixtures compare 1,615/1,579/1,645 caller states across three seeds, no
differences. Normal full-cycle replay calls the helper five times and
preserves 66 checkpoints/final hash in 46.7 s. The previously unreached
diamond is reached by laser-guided `strike_training`: 1,051 calls, all 133
checkpoints/final hash identical in 76.6 s. The plain strike uses model 1Bh,
outside the diamond's 1Ch/1Dh/1Eh branches. Private build logs are
`build-wt-start-screen{,-fixture}.log`; implementation remains private while
main's allocation/restore gate runs. The private unit subset passes all 27
tests in 19.68 s while sharing CPU with the gate. Fresh diamond comparisons
pass 4,967/4,989/4,977 states with zero differences; all private jobs are closed.

The checked allocator and five restore entries are now integrated in main
(803 addresses), with two focused regressions (29 gate unit tests).
The diamond stays private pending integration. Full verification passes in
`D:/f117-gate/gate-checked-restore.log`, 16 workers under affinity
0x00FFFFFF: 178 s build, 540 s coverage, 19 s identical regeneration,
787 s parity, 1,524 s total. Both instruction profiles compare 5,718,976
states; three 803-address seeds compare 2,622,305 / 2,621,217 / 2,618,194
states, with zero differences. All 38 routes and reachability/event checks
pass; 37 fresh interpreter runs, zero reuse. Census: 797 functions,
67,798 bytes. The harness records 1,263/1,247/1,283 completed allocation-error
cases across the full seeds, all equal.

Next isolated DSWAP wrapper (11 Oct): checked byte allocation at 0494 keeps
the DOS result in the guest frame, compares it against segment 10h, reports
an error through formatted output, and invokes the original failure path.
The implementation preserves calls, flags, live frame reads and event stops.
Build `build-wt-dswap-checked.log` waits until the runtime gate's second
regeneration finishes; the two-worker build is now live and recompiles
shared generated units changed by the seven runtime entries. No build or
random-state verification was claimed while that build ran. The first build
fails because frame/call macros were out of scope; the corrected build uses
the available helpers and passes in `build-wt-dswap-checked-restore.log`.
The private batch now also contains five matching Ctrl+Break restore entries;
their original bodies differ only in verified saved-vector addresses. Three
seeds compare 78,713 states exactly, 27 tests pass in 10.66 s, and a 46.1 s
full-cycle replay preserves all 66 hashes, with VGAME/SETUP restore calls.
The replay does not reach the checked allocator. A controlled fixture supplies
low/error allocation replies, returning graphics thunks and failure cleanup,
checking the caller when cleanup returns. Three seeds then compare 76,014
states exactly, including 3,709 completed error results; 27 tests pass in
9.61 s. This does not prove real out-of-memory termination. Logs are
`checked-restore-error-lockstep-{0,1,2}.log`. Main stays fixed at 797 entries.

Next isolated runtime batch (11 Oct): five Ctrl+Break installers share an
18-instruction original body in VGAME, START, END, DSWAP and SETUP. Byte
comparison confirms only the saved-vector and handler addresses differ;
all configuration fields are checked against the original. Two DSWAP DOS
memory wrappers have byte-identical bodies to the already matched START/END
helpers, so reuse those helpers at DSWAP's entries. Returning DOS fixtures
cover these seven entries. Implementation is private; build and random-state
verification waited until camera gate regeneration finished. Initial three
seeds passed 98,171 states, but the full-cycle real-driver probe differs from
the original starting at 2.35B: the installers' MOV AX,segment immediate is
relocated, not a constant zero. A new nonzero-image fixture catches all five
broken installers (64-state request, five immediate mismatches). The fix
reads the loaded operand; `build-wt-break-relocation-fixed.log` passes.
Repeated seeds with the relocation fixture compare 32,723/32,696/32,752
states exactly; the 27-test unit subset passes in 7.46 s. A fresh full-cycle
probe takes 30.2 s, preserving all 66 checkpoints/final hash; SETUP, VGAME
and END installers each run once. These seven candidates are now integrated
under `gate-break-memory.log`: 797 addresses, 27 unit tests and 38 routes,
16 workers under affinity 0x00FFFFFF. The first translation now has 89,367
instruction starts (one additional static entry). The full gate passes: 172 s
build, 522 s coverage, 18 s identical regeneration and 778 s parity, 1,490 s
total. Both instruction profiles compare 5,718,976 states; three matched seeds
compare 2,596,946 / 2,595,902 / 2,593,952 states, all without differences.
All 38 routes and reachability/event checks pass; 37 interpreter runs are
fresh, with zero reuse. Census: 791 functions and 67,670 bytes.
The initial broad CTest run passes 27 tests but its full 2,000-state matched
test is terminated after this known defect, so that run is not a suite pass.
Use the gate's unit exclusion for broad locksteps in the repeat; targeted
seeds already cover the changed routines and the full gate checks all later.

Next isolated drawing helpers (11 Oct): projected-point diamond B0C2 and
camera inset darkening C45C are rewritten privately while main stays fixed
under the combined pause/HUD gate. The two-worker build waits until main
regeneration ends and parity starts, avoiding shared generated-file writes
during compilation. No verification/integration claim yet; drafts and build
log remain outside the repository (`build-wt-diamond-camera.log`). The build
passes after regeneration. Three targeted seeds compare 7,981/7,894/7,932
states exactly in about 5.7 s concurrently; driver thunks return on both
sides. The isolated suite passes all 24 tests in 11.95 s. A private
F5/F6/F6 probe preserves all 54 hashes but reaches neither helper (F5
selects armament, not the camera), so it is not proof of these entries.
A reconnaissance replay with F6 darkening enabled passes all 150 hashes
and final state, 123.3/71.4 s original/isolated, C45C called 1,608 times.
The isolated strike replay takes 81.6 s against the combined gate's fresh
232.3 s interpreter control: all 177 hashes/final match, but B0C2 never
runs. Keep that candidate isolated. Camera darkening alone is integrated
under `gate-camera-darken.log`, 25 tests, 38 routes, 790 matched addresses;
Full verification passes: 25 unit tests, all 38 routes, both instruction
profiles, three matched seeds and reachability/event checks. The gate takes
1,420 s (build 137, coverage 494, identical regeneration 18, parity 771),
37 fresh interpreter runs, no cache reuse. New `recon_darken` retains the successful
real-driver path rather than the non-reaching F5/F6 probe.

Combined pause/HUD gate (11 Oct) passes in `gate-pause-hud-2.log`:
24 unit tests, 37 routes, 789 matched addresses, affinity 0x00FFFFFF,
16 route workers. Total 1,453 s: build 130, coverage 537, identical
regeneration 18, parity 768. All 37 routes are identical (36 fresh
interpreter runs, zero cache reuse); both instruction profiles, all three
matched seeds and reachability/event checks pass. First `gate-pause.log` passes 22 unit tests and both
instruction profiles but is aborted after its rotating seed finds two
changed-code inputs: PLAYER's stack aliases/restores its RET, and VGAME's
edge midpoint code lies below the watched entry. The repaired per-step
watch/range passes that exact seed (8,944 comparisons, about one second),
retained as `matched_frame_code_alias`. This is not a passing first gate.
HUD real-driver C/C probes take 58.7/55.2 s concurrently, preserving all 54
checkpoints and final hash; colour/layout handlers run once/twice.

Isolated HUD colour/layout candidates (11 Oct) use two census entries:
1058:09D8 falls through to 1058:0A2C. Three compact-mode-planted seeds
compare 8,828/8,845/8,827 states exactly, about one second each concurrently;
21 unit tests pass in 9.20 s. Normal-input C/C layout toggles are prepared
in private `hud-layout.args` for interpreter/matched real-driver probes.
These candidates are now integrated in the combined gate above; their
earlier isolated checks do not replace that gate. `--only` takes an unpadded hexadecimal
IP, not segment:offset; an incorrect filter silently compares no routines.
Always inspect the per-routine comparison counts.

Pause-handler matching (11 Oct) first probes Alt+P, repeated Alt+P, Space,
then quit through normal input, using the original interpreter and isolated
matched binary: 38.9/40.0 s concurrently with a two-worker build. All 54
checkpoints and final hash agree; the matched handler runs once. Without
returning sound-driver thunks the random harness skips every state. Enabling
its existing driver fixture for the pause lets the existing DOS/BIOS answers
exercise polling too: three targeted seeds compare 4,966/4,959/4,954 states,
zero differences, about 0.4 s each. No keyboard code is patched. The focused
CTest and normal-input `pause_resume` route retain this coverage. Main gate
is pending in `gate-pause.log`, affinity 0x00FFFFFF, 16 route workers.

Camera-plane/seam integration (11 Oct) reuses the existing observer captures:
the final isolated 20-test run takes 5.98 s. Fine endpoints at N=4/9 pass
for strike (444), landing (62), and air-to-air (54); three intermediate
pictures per pair are exercised too. Independent C/Python repair counts
match at N=4 (10,010/1,534/2,941 respectively). Frozen-binary live N=4/9
sentinels take 53.2/73.7 s concurrently, each preserving 368 logic frames,
366 endpoint pairs and 54 checkpoints/final hash. Main full gate passes
in `gate-near-seams.log`: 21 tests, all 35 routes, both instruction profiles,
three matched seeds, reachability and event limits. Total 1,411 s: build/tests
107, coverage 513, byte-identical regeneration 17, parity 775 (rounded).
All 34 interpreter sessions run fresh; affinity 0x00FFFFFF, 16 route workers.

The first cockpit drawing-helper gate (11 Oct, `gate-panel-drawing.log`)
takes 1,059 s: build/tests 127, coverage 603, byte-identical regeneration 17,
parity 312. All 35 route pairs and both instruction profiles pass, with 34
interpreter cache hits and none fresh. Its third matched seed exposes a
transient self-modifying fuzz state in END's pair swap: code is overwritten
and restored before the final memory comparison. The existing per-step code
watch now covers all four pair-order helpers too. Retained CTest
`matched_order_code_alias` runs the failing seed in about 1.3 s and compares
15,984 states, zero mismatches, skipping the invalid code-alias state.
The complete second gate (`gate-panel-drawing-2.log`) passes all 35 routes,
19 tests, both 5,718,912-state instruction profiles and three 786-entry
matched seeds (2,547,415/2,546,484/2,547,057 comparisons), zero differences,
with reachability and event-room checks. Total 1,426 s: build/tests 26,
coverage 574, byte-identical regeneration 17, parity 810 (rounded stages).
The CMake regression changes the interpreter cache key: 34 fresh sessions,
none reused. Affinity 0x00FFFFFF, 16 route workers; isolated presentation
builds and two two-flight observer probes share the machine.

The isolated near-plane metadata probe records a 200M-clock strike window
and a same-binary observer-off control (`record-near-metadata.py`), preserving
all 174 checkpoints and final hash. `near-crossing-check.py` checks 4,120
crossings exactly; four ambiguous records motivate an entry-observer repair.
Carried replay and N=1/4/9 checks cover 447 work/display phases and take
about 4.5 s concurrently. Initial isolated 18-test run takes 6.82 s. These
are investigation runs; no production near-plane rendering rule is enabled.

The cockpit sprite gate (11 Oct, `gate-panel-blits.log`) passes all 35
routes, 18 tests, both 5,718,912-state instruction profiles and three
783-entry matched seeds (2,532,630/2,531,689/2,533,435 comparisons), with
reachability and event limits checked. Raw wall time 16,480 s includes
15,015 s suspended at the owner's allowance threshold: about 1,465 s
active, split 148 build/tests, 550 coverage, 18 byte-identical regeneration
and 749 parity (rounded). All 34 interpreter sessions ran fresh: final
host-comment changes after the HUD gate changed the cache key. Affinity
0x00FFFFFF, 16 route workers; a two-worker isolated build shared the run.
Three fresh targeted main seeds compare 24,775 sprite-helper states in
about 4.6 s concurrently. The next isolated readout/block-line helpers
compare 29,593 states in about 5.2 s concurrently, and the target box
14,805 in about 0.9 s; no differences, no full gate for those three yet.

The Reimp-style HUD gate (11 Oct, `gate-hud-reimp.log`) took 1,444 seconds:
translation/build and 18 tests 27, fresh coverage 487, byte-identical
regeneration 24, parity 906 (rounded stages). Host/presentation/CMake
changes invalidated interpreter cache keys: 34 fresh sessions, 0 reused.
All 35 routes, both 5,718,912-state instruction profiles and three
781-entry matched seeds pass (2,524,384/2,523,430/2,525,667 comparisons,
no differences), with reachability and event limits checked. Affinity
0x00FFFFFF, 16 route workers. Separate live N=4/9 sentinels also match all
54 checkpoints and final hashes of both freshly checked scan engines.

The cockpit startup gate (10 Oct, `gate-panel-init.log`) took 898 seconds:
translation/build and 17 tests 86, fresh coverage 506, byte-identical
regeneration 16, parity 290. All 35 routes, both instruction profiles and
three 781-entry matched seeds pass (2,524,384/2,523,430/2,525,425 routine
comparisons, no differences), with reachability and event limits checked.
34 interpreter sessions were reused, affinity 0x00FFFFFF, 16 route workers.
Three targeted seeds compare 14,971 states in about 6.7 seconds per seed
concurrently. A two-worker worktree build overlapped coverage and parity.

The two-panel-helper gate (10 Oct, `gate-panel-helpers.log`) took 1,010
seconds: translation/build and 17 unit tests 91, fresh coverage 559,
byte-identical regeneration 17, parity 342 (rounded stages). All 35 routes,
both instruction profiles and three 779-entry matched seeds passed, with
2,519,363/2,518,465/2,522,480 routine comparisons and no differences.
Reachability and event-room checks passed; 34 interpreter sessions were
reused. Affinity 0x00FFFFFF, 16 route workers. A two-worker build in the
separate worktree overlapped final parity. Targeted panel checks compared
29,522 states over three concurrent seeds in about a second per seed;
the next display-fill candidate compared 6,281 states in about three
seconds per seed concurrently, without differences (not yet fully gated).

The clipping-entry registration gate (10 Oct, `gate-outcode-bank.log`)
took 959 seconds: translation/build and 17 unit tests 125, fresh coverage
547, byte-identical regeneration 15, parity 272. All 35 routes, both
5,718,912-state instruction profiles and three 777-entry matched seeds
passed; every routes-only routine ran and no routine overran. It used
16 route workers with verified inherited affinity 0x00FFFFFF. Three
targeted 4,000-state seeds took about 0.4 seconds each concurrently,
comparing 3,990/3,999/3,998 states without differences. An earlier
duplicate-implementation gate was deliberately interrupted during unit
tests after discovering the existing routine; it is not a passing run.

The collision/sound/scene full gate PASS (10 Oct, `gate-scene-sound.log`)
took 883 seconds: translation/build/tests 92, fresh coverage 495,
byte-identical regeneration 11, parity 284. All 35 routes were identical,
both instruction profiles and all three 768-address matched seeds had zero
mismatches; every routes-only routine ran and none overran. Interpreter
results reused 34 verified cached sessions. The locksteps finished during
coverage, leaving no tail. Affinity remained 0x00FFFFFF throughout.

The 10 Oct collision/sound/scene batch's two targeted 4,000-state seeds ran
concurrently in 9.7 seconds (two workers, affinity 0x00FFFFFF), comparing
7,763 and 7,777 states without differences. Rebuilding the large matched.c
translation unit took roughly 2.5 minutes per edit, including both libraries
and linking; the unchanged-source profile-aware gate took 596 seconds, with
34 interpreter sessions and coverage legitimately reused from verified caches.

The profile-aware reference run (10 Oct, `pc-parity-profile-aware`) adds one
independent local 386 capture alongside the existing DOSBox captures. Fresh
GOG/DOSBox-X/86Box pictures, sound, timing and both saves pass. Held-picture
hashing decodes only stationary scenes, taking about one second on the saved
capture instead of decoding every changing frame. Eight regression tests
take 0.13 seconds. Captures and gate builds must remain sequential because
the running Windows executable locks build outputs.

Run before every commit that changes code. Before the changes below it took
30, 38 and 32 minutes (three runs on 6 Oct 2026, some alongside other work).
After them, the same code (231 matched routines, 32 routes) took **22.8
minutes** (1,365 s against 1,895 s), with every route's clock, hash and
verdict, and both lockstep tallies, identical to the earlier run; that run
shared the machine with a large compile, so it is a ceiling, not a best case.
The 484-routine, 35-route gate on 8 Oct took 14.8 minutes (886 s): build
7 s, coverage 237 s, second translation 3 s, fresh parity 638 s, no lockstep
tail. Both locksteps had zero mismatches and every routes-only routine ran.
The subsequent translated-386 gate took **16.1 minutes** (964 s): build
7 s, coverage 277 s, second translation 4 s, fresh parity 676 s, no lockstep
tail. All 35 routes were identical, both 5,713,152-state instruction checks
and the 484-routine check had zero mismatches, and every routes-only routine ran.

The 9 Oct gate with 762 matched addresses and the shadow-text regression
(`D:\f117-gate\merge10\gate.log`) took **19.3 minutes** (1,157 s): build and
13 unit tests 12 s, coverage 415 s, second translation 7 s (no code added),
fresh parity 723 s (34 interpreter sessions for 35 routes), no lockstep tail.
Both instruction profiles and all three matched seeds had zero mismatches;
all routes were identical, all routes-only routines ran, and no route overran
an event limit.

The gate explicitly sets `F117R_BUILD_APP=ON` (9 Oct), so a previous
core-only build cannot silently omit the five host tests. With generated
code, the unit step now runs all 14 tests, including the retained shift
stack-alias regression; the matched lockstep is checked
separately at three 4,000-state seeds.

The combat-option gate (`D:/f117-gate/gate-combat.log`, 10 Oct) took
1,499 s: build and 14 unit tests 16 s, coverage 537 s, byte-identical second
translation 11 s, fresh parity 934 s (34 interpreter sessions). All 35
route pairs, both 5,718,912-state instruction profiles and all three matched
seeds passed; every routes-only routine ran and none overran. Three
eight-shard matched checks plus 16 route workers initially oversubscribed
the machine. Restricted the gate parent and descendants to affinity
0x00FFFFFF (24 of 32 logical CPUs), inherited by later children. Raw-slot
traces and the held-out cohort shared the machine near the end; this is a
measured shared-load cost, not an isolated performance comparison. The
process-local limit leaves eight CPUs available to the desktop.

The D1SLOT gate (`D:/f117-gate/gate-d1slot.log`, 10 Oct) took **26.5
minutes** (1,588 s): translation/build and 14 tests 80 s, coverage 581 s,
byte-identical second translation 11 s, fresh parity 917 s. All 35 route
pairs, both 5,718,912-state instruction profiles and all three 765-address
matched seeds passed; every routes-only routine ran and none overran.
The attempted ctypes affinity wrapper failed silently. Corrected the parent
and all live descendants to 0x00FFFFFF early in coverage, verified the masks;
future native calls must use pointer-width signatures and check the result.
A later two-worker acceleration probe and one natural replay shared parity.
`D:/f117-gate/affinity_launch.py` now checks both setting and reading the mask.
Use Processor % Processor Time (about 52% during parity) for actual CPU load;
WMI LoadPercentage reported 96% at the same time.

The 763-address gate with this setting (`D:/f117-gate/merge14/gate.log`)
took 555 s: build and 13 unit tests 14 s, parity 541 s with 34 unchanged
interpreter sessions reused. All 35 routes were identical, both instruction
profiles and all three matched seeds had zero mismatches, every routes-only
routine ran, and no route overran an event limit.

The 764-address canopy gate (`D:/f117-gate/canopy/gate.log`) took 887 s:
translation/build 84 s, fresh coverage 490 s, byte-identical second
translation 7 s, parity 306 s. It reused 34 interpreter sessions; all 13
unit tests, 35 route pairs, both 5,718,912-state instruction profiles and
three matched seeds passed. Coverage added nothing; no event-limit overruns.

The 765-address projectile gate (`D:/f117-gate/projectiles/gate.log`) took
845 s: translation/build 78 s, fresh coverage 476 s, byte-identical second
translation 8 s, parity 283 s. It reused 34 interpreter sessions; all 13
unit tests, 35 route pairs, both instruction profiles and three matched
seeds passed, with no event-limit overruns. Coverage added nothing.

The speech-reference/shift-repair gate (`D:/f117-gate/p1-speech/gate-fixed.log`)
took 1,269 s: translation/build 14 s, coverage 522 s, byte-identical second
translation 9 s, parity 724 s. All 14 unit tests, 35 route pairs, both
5,718,912-state instruction profiles and three matched seeds passed, with no
event-limit overruns. It ran 34 fresh interpreter sessions; coverage added
nothing. The initial 595 s gate caught the shift regression at the rotating
seed; that seed is now retained as a unit regression.

Stage times, earlier run: translate and build 457 s, coverage 173 s, second
build 2 s (skipped), parity 503 s, lockstep tail 229 s.

| Stage | Measured | Duplicates / redundancy | Status |
|---|---|---|---|
| 1-2 translate, build | 7-8 min fresh, 6 s reused | a fresh work directory builds everything | **fixed by practice**: reuse the previous run's work directory (`sync_tree` rewrites only changed files, so the build recompiles `matched.c` and relinks); the output is a function of the sources either way |
| 3 coverage | 3-4 min, or skipped | runs every route once only to find untranslated code | **skipped when provably empty** (7 Oct): a record (`~/f117-recomp-local/coverage-verified.json`) holds the key of a pass that added nothing - translator, sources other than `matched.c`, game files, routes - and the digest of the generated code it ended on; the same key and digest skip the pass. A matched routine only replaces original instructions that already ran, so it cannot add code to cover; a wrong one fails parity or lockstep. Verified: the full verdicts (32 routes with hashes, lockstep tallies) are identical with and without the skip; 995 s became 422 s |
| 4 translate, build again | 7-8 min | regenerated every file and rebuilt all of it though coverage usually adds nothing | **fixed**: only files whose bytes changed are rewritten; the rebuild is skipped when none did; `--seed-coverage DIR` starts from an earlier run's coverage so step 1 is already complete |
| 5 parity | 8-14 min | 64 runs on 12 slots, started alphabetically; two routes that are one session ran twice per engine | **fixed**: up to 24 slots, longest first, identical sessions run once and every route's milestones are checked on the shared run |
| 5b interpreter half of parity | 5-6 min of the parity stage | an interpreter run is a pure function of the game files, the route and the interpreter's own code, yet reran every gate | **fixed**: results cached by a key over `src/` except `matched.c`, `CMakeLists.txt`, the game files and the route (text, replay files, seed chain); entries expire after 14 days; `--verify-interp-cache` reruns and requires equality. Measured: parity 522 s to 174 s with 31 of 31 results reused and every route verdict identical to the fresh run |
| 6-7 lockstep | 2-3 min | waited for parity though they need only the build | **fixed**: started right after the first build, beside coverage and parity (stopped and restarted if step 4 rebuilds, which 18 of 21 logged gates did not need); output to files in the work directory. Measured 7 Oct with coverage forced: 778 s to 572 s, the lockstep tail 241 s to 0, every route line and both tallies identical |

Same-session routes (executable lines identical) are found by content, not
by name; today: `recon_career` and `recon_return`.

The physics/real-time gate (`D:/f117-gate/gate-physics-realtime.log`,10Oct)
took24.1minutes (1447seconds),16workers with verified24-CPU affinity:
build and17tests20seconds, coverage508, byte-identical regeneration12,
parity907. All35pairs, both5,718,912-state profiles and all three765-address
matched seeds passed; every routes-only routine ran and none overran.
Earlier physics gate failed on the initializer-range harness error; the
first real-time gate was interrupted after an airborne pacing defect.
These are recorded separately and are not passing gates.

## Other repeated processes

| Process | Trigger | Measured cost | Redundancy and status |
|---|---|---|---|
| Targeted weapon-lock marker lockstep | check the named box/hexagon geometry before the full gate | about 0.5 s per 4,000-state run at the two follow-up seeds (9 Oct); 4.2 s before returning driver thunks were enabled | `func_lockstep --only VGAME.EXE:B171 --states 4000 --verbose`; the existing thunk substitution is enabled for this VGAME routine too, increasing the fixed-seed comparisons from 358 to 4,857 (17 skipped); this changes test inputs to reach drawing branches, rather than being an optimisation with identical inputs; full routes retain the actual driver |
| Targeted cockpit-canopy lockstep | check canopy posts, compact side frames and landing cue before the full gate | 0.7 s per 4,000-state run, three seeds concurrently (9 Oct) | `func_lockstep --only VGAME.EXE:D6DD --states 4000 --verbose`; returning thunks on both sides allow graphics calls to return; 4,491 / 4,482 / 4,476 comparisons, 15 / 9 / 7 skipped, zero mismatches; full routes retain the actual driver |
| Targeted projectile lockstep | check slot motion and gunfire refill before the full gate | 1.0-1.2 s per 4,000-state seed, two concurrently (9 Oct) | `func_lockstep --only VGAME.EXE:4777 --states 4000 --verbose`; small-count/firing plants and returning driver thunks reach more active states: 2,823 / 2,886 / 2,891 comparisons, zero mismatches. A private observer confirms 18 full refills among the fixed-seed comparisons. Full routes retain the actual drivers |
| Corrected paired threat cohort | CPU-speed/combat investigation, after changing pilot or observer | 21.9 min for 96 flights, 12 workers (10 Oct); 600 guest seconds, four routes, six seed families, four arms | `threat_profile.py` telemetry v5 fixes unsigned life/burst sampling, startup seconds and orbit geometry; +110/+220 ms boot shifts at 16/20 MIPS, mission identities checked. Old cohorts cannot be reused for combat conclusions. `threat_compare.py` retains early exits and bootstraps paired differences; raw results outside repo |
| Staged incoming-lifetime regression | after changing D1TTL or its dispatch | 76 s for four boots in parallel (10 Oct) | `d1ttl_check.py`, both engines +/- D1TTL, seven cases per boot; compare case hashes, positive-life controls and FFFF/final-4*S suppression. Explicitly staged, not natural combat |
| Staged proximity-distance regression | after changing D1PROX or its dispatch | 114 s for four boots in parallel (10 Oct) | `d1prox_check.py`, both engines +/- D1PROX; S=8/9/15, near/far geometry and FFFF suppression; S and weapons explicitly staged |
| Combined combat-option cohort | after changing the proximity option | 12.6 min for 24 flights, eight workers (10 Oct) | Same matched seed families as the 96-flight cohort, 20 MIPS with D1TTL+D1PROX; reuse the baseline arms, validate mission identity and compare paired differences |
| Raw missile-slot trace | investigate seeker lifetime underflow | 219 s for a parallel 9/20 MIPS pair (10 Oct) | Read-only private wrapper around current pilot; both cohort hashes reproduced. Two processes share affinity mask 0x0F000000 while the gate uses 0x00FFFFFF, leaving four desktop CPUs. Compare common observation windows; slot occupancy is not a count of blocked launches |
| Held-out combat cohort | validate options without changing calibration | 9.9 min for 24 flights, eight workers (10 Oct) | Four routes, three new seed families, 9 unmodified versus 20+D1TTL+D1PROX; all 12 mission identities match. Shares the gate's 24-CPU affinity limit; three early exits per arm retained |
| Staged seeker-underflow reproduction | establish the next countdown regression | About 120 s for two boots in parallel (10 Oct) | Private `slot_underflow_probe.py`, interpreter/recomp, S=8/15; positive life becomes FFFF after lost lock, case hashes identical. First exact-rear setup failed its staging expectation; off-axis setup reproduces the defect |
| Staged weapon physics regression | after changing D1PROX/D1ACCEL or dispatch | about 2 min for four boots (10 Oct) | `d1physics_check.py`, both engines +/- options; 21 cases: S=9/player controls, equal-time and changing-S acceleration, frozen straight passes; incoming slots 0/7; every case hash agrees across engines, both control hashes unchanged |
| Real-time simulation regression | after changing D1REAL or dispatch | 3.8 min for six parallel boots (10 Oct) | `d1real_check.py`, 9/20/40 MIPS in both engines, normal inputs only; normal clock, 2x compression, restored rate, pause/resume and quit/cancel; compare case hashes per speed |
| Corrected real-time airborne cohort | validate D1REAL during combat | 4.4 min for 24 flights with eight workers (10 Oct); two interpreter replays run alongside | four routes x seeds 2500/22500/25000 x 9/20 MIPS, TTL+PROX(v2)+SLOT+ACCEL+REAL(v2); verify missions, full telemetry and replay hashes. START seed matching does not align VGAME's later BIOS-tick RNG seed; align the final hangar input separately for controlled combat, and sample at completed simulation steps |
| Initializer walker bounds regression | each gate | included in the17-test unit selection | `ctest -R matched_initializer_bounds`, retained seed0x66A860898896; exact walker code spans prevent an unrelated callee return from being mistaken for the walker's own RET |
| Staged slot-reclamation regression | after changing D1SLOT or dispatch | about 2 min for four parallel boots (10 Oct) | `d1slot_check.py`, both engines +/- D1SLOT; S=8/15, zero/one/signed/unsigned controls before lost-lock cases; all 15 case hashes agree across engines and all eleven control hashes are unchanged; includes incoming slot 7 and player slot 8 |
| Swept-band/acceleration cohort | measure current physics changes at both CPU speeds, including held-out seeds | 23.7 min, eight workers beside parity (10 Oct) | Four routes x nine seed families x 9/20 MIPS, all four combat options, early exits retained; fresh seeds22500..27500 held out;32 matching pairs, four27500 pairs excluded; check regenerated native 9/20 sentinels against pre-gate/interpreter references |
| Slot-repair combat cohort | assess D1SLOT separately from earlier hit mitigations | 10.4 min for 72 new flights, 12 workers (10 Oct) | Four routes x six seed families x (9 with TTL+PROX, 9/20 with TTL+PROX+SLOT), reuse prior 20 TTL+PROX arm after unchanged-flight hash check; all 24 mission identities match across all four arms, early exits retained |
| Isolated 15 FPS cap experiment | test whether a cap alone keeps S at 15 and the mission clock real | about 18 min for 45 flights, eight workers beside a full gate (9 Oct); isolated generated build about 3 min | one source change in a detached checkout at 28f2a50: D1_FPS_X10 116 to 150; 45 unfixed baseline rows retained from the preceding sweep; separate output and DLL hash in `D:/f117-gate/speed15/experiment.json`; compare two 40 MIPS flights under the interpreter too; no default or production D1 change |
| Speed and D1 sweep (`speed_sweep.py`) | evidence for the default-speed decision | about 12 min for 90 flights, 16 workers beside control research (9 Oct): nine routes, 300 guest seconds each, at five speeds with and without D1 | each route's unfixed 9 MIPS baseline runs first to anchor input times; results resume by tag in `runs.jsonl`; compare engagements only on matching missions; keep fresh results separate from the earlier mixed-version launch counts |
| Control-response speed sweep (`stick_response.py --ips N [--fix D1]`) | compare controls before choosing a default speed | roughly 43-220 s per flight under concurrent load (9 Oct); ten aligned DOSBox-profile flights, four workers | repeat the same 30 timed taps; retain mission identifiers, raw pitch/roll samples and early stops, so incomplete flights and differing missions cannot pass for equal responses; absolute and common-clock exploration runs also include the 386 profile |
| START random-seed probe | find boot clocks that generate the same mission across speeds | five front-end boots; separate wall time not recorded | read START's RNG at DS:AE8C and the BIOS tick count; change only the boot clock for any follow-up, never guest memory; scratch probe in `D:/f117-gate/speedsweep/probe_start_seed.py` |
| Shadow-text stack-alias regression (`ctest -R matched_shadow_stack_alias`) | every gate's unit-test step, with generated code | about 2 s (9 Oct) | seed `0xf2a7d2c1bf36`, START 36C4 and END 19C3/112A only; preserves the state that caught an omitted PUSH SS memory write in the shared matched helper; 14,752 comparisons, zero mismatches after the fix |
| Matched-routine lockstep (`func_lockstep --states 4000`) | every gate (three seeds); every new matched routine | single process 21 min for 653 routines beside a gate (9 Oct); 8 shards 3 min 11 s, the same report line for line | **Sharded since 9 Oct 2026** (`tools/func_lockstep_par.py`, `--shard K/N`), with the owner's approval: each routine draws from its own stream (the seed, its module and address), so a shard tests what one run does, and a routine's states no longer move when other routines are added (before, one stream ran through the table and merges met paths a branch never drew). The states are the same in number and kind, not the old ones. The gate runs seeds 0x5EED0F117A and 0xC0FFEE and one from the gated commit's hash, so successive gates draw new states. Left: tracking writes instead of copying 1 MB a state |
| Instruction lockstep (`insn_lockstep --states 64`, also `--timing386`) | every gate | no wait tail in the latest gate; 5.7 million states per profile (8 Oct) | runs beside coverage and parity; the 386 check also compares prefetch and held REP state; a second-seed run with `--base-seg 0x9FFF` checks code in VRAM |
| 386 flight engine parity (`stick_response --machine machine386 --engine interp/recomp`) | after a profile or translated timing change | 80 s interpreter, 60 s recomp, alongside the gate (8 Oct) | all 574 memory/register/output/scanout checkpoints and stick responses identical; use separate processes because the runtime is global |
| Oversized generated regions | every generated build | 479 s before bounding regions; 120 s after (8 Oct) | a 6,446-instruction VGAME function dominated MSVC compilation; cap regions at 1,024 instructions, preserving each decode and live operand and dispatching across pieces. Both timing profiles retain the 5,713,152-state instruction verdicts; whole-route gate required |
| Census refresh (`reimp_names.py`, then `progress.py --census`) | after each batch of matched routines | about a minute | the lead list only changes when the Reimp does; regenerate it then, refresh the census each batch |
| Route recording (adaptive pilots) | after a timing change moves a flight | 10-20 min per flight; try several parameters in parallel | recorded inputs replace them in the gate; a timing change invalidates all of them at once (see `airair_type5-8`, `strike`, `cargo`, `recon`) |
| Draw-list capture windows | per presentation stage | 3-4 min per window, four at a time | windows are independent and already run in parallel |
| Fine HUD recorded-scene checks | after HUD/replay changes | about 10-15 s for eight corrected windows at N=1/4/9, three workers (10 Oct) | reuse `p3-live/logs41/w_*.log`; run carried `test_drawlist` and `test_hires`; older strike/air captures omit colour-replace records and fail the unchanged baseline too. `test_hires --dump PREFIX` renders the last checked frame, about a second per 69-phase N=4 window |
| Fine HUD live sentinel | after presentation changes | 146.9/112.8 s at N=4/9, two workers beside the gate (10 Oct) | `run_route.py boot_to_flight.args -- --present interp --present-scale N --hash-every 50000000`; 368 exact logic frames and 366 exact pairs per scale, 54 matching guest checkpoints and final hash; separate outputs and inherited 24-CPU affinity. These concurrent runs do not measure isolated performance |
| MSVC matched-code compilation | every matched-code build | PLAYER decompressor stalled for more than 18 min at /O2; about 30 s for the file with the workaround (8 Oct) | only `player_unpack` disables MSVC optimisation; the rest of the file keeps the build's flags. It runs during picture loading. Stop an orphan compiler only after checking its source/output paths; otherwise its object stays locked |
| Worktree build and generated code | per worktree | 8 min first, then incremental | give each tree its own TEMP (build.cmd shares one log); do not share a generated directory between concurrent builds |
| PC-parity run (`pc_parity.py`: pictures, music, saved roster on three references) | before a release; after a machine-model change | about 2 min wall expected (96 s for DOSBox-X and 86Box together; 3.8 min before 86Box's picture capture was traced, 5 min before fast-forward, 12 min before 7 Oct, when 86Box's runs went one after another) | the three references run at once, and 86Box's three runs (picture capture, music and timing, saved roster) run at once too: each stops only its own 86Box (by profile), the traced ones open no VNC port and run on emulated time; ours is replayed per reference, not shared. The DOSBox-X save run ends when the roster's content changes (its inputs run on emulated time, so a wall-clock limit failed under load); the picture capture waits for its VNC port instead of a fixed 6 s. Fast-forward (7 Oct): DOSBox-X's picture capture 11 s instead of 130 s and the same report (two fast-forward captures byte-identical; two real-time ones differ in 3,826 frames, since AUTOTYPE and the pacing ran on host time); its save run 54 s instead of 227 s on an idle machine, which failed under load (8 Oct: the scripted clicks missed, 61 bytes of ROSTER.FIL off), so the check now runs at DOSBox-X's own speed, 4 min; 86Box's save run 95 s instead of 277 s and its music run 49 s instead of 178 s, the saved roster identical and the AdLib writes identical in order, their timing within 86Box's own run-to-run spread. 86Box's picture capture is traced too (48 s instead of about 3 min, two runs identical) |
| DOSBox-X closed-loop flight (`dosbox_cargo_pilot.py`) | per pilot change | 107 s wall in fast-forward (12 min paced to real time, 7 Oct); the same pilot on our machine takes about 1.5 min | fast-forward (`[cpu] turbo=true`) is the default: 1,642 ticks, `result.json` and `flight.csv` identical to a real-time run. Run it beside the machine's flight |
| 86Box closed-loop flight (`b86_cargo_pilot.py`) | per pilot change | about 200 s wall in fast-forward (`B86_FAST`) | run beside the machine's and DOSBox-X's flights; runs are deterministic (same `flight.csv` twice), so one flight is a verdict |
| Machine-behaviour probe (`fidelity_all.py`) | after a machine-model change | about 2 min with a saved 86Box sheet; 86Box adds a 4-minute run (`--86box-run`) | the 86Box sheet only changes with the model, so the saved sheet is reused |
| 86Box boot attempts | per BIOS or config change | 2-5 min per capture series | a boot-sector test (`hello_floppy.py`) answers "does the BIOS boot" in one run, before any larger image |

## Silent 86Box speech waveform reference

`tools/ref86box/86box-audio.patch` records the mixer buffers before host
playback, enabled only by `B86_AUDIO=PREFIX`: signed 32-bit little-endian
stereo `PREFIX.sound.pcm` at 48,000 Hz and `PREFIX.music.pcm` at 49,716 Hz.
The reference runner retains offscreen video and null/dummy host audio.
Before using a capture, run the same input with capture disabled and enabled
and require identical frame and port traces. Measured on 9 Oct: incremental
86Box build 23 s, two intro runs concurrently 28 s, short takeoff-call flight
94 s, our interpreter replay 50 s, WAV render 1.6 s, comparison about 0.6 s.
Reuse captures when the recorded binary, input and configuration hashes agree.
Generated audio and game data stay outside the repository.

The combat-alignment tooling gate (10 Oct, `gate-combat-alignment.log`)
took 9.9 minutes (594 seconds): translation/build/tests 21 seconds,
parity 573; unchanged game source, inputs and generated code reused the
verified coverage/regeneration cache. All 17 selected CTests, 35 routes,
both instruction profiles and three matched seeds passed.

## What the gate is checked against

The gate compares the recompiled engine with this project's own interpreter,
so a model error shows in neither. Fidelity to a real PC is checked
separately, against recorded references: GOG's DOSBox 0.74
(`tools/fidelity.py`, saved video and flight captures), DOSBox-X for some
timing traces, and 86Box as the real-hardware-style reference once it runs
the game. Those are recorded or run per question, not per commit.
