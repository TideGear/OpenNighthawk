# Repeated processes

Every process this project runs more than once, what it costs, what it
duplicates and what has been done about it. Update this page whenever a
process is added, measured or changed; a new repeated step goes here first.
Timings are wall clock on the development machine (32 threads) and say
whether they were measured or estimated. The rule behind the page: an
optimisation must leave every check's inputs, outputs and verdicts
identical, and the proof is the same verdicts before and after.

## The gate (`tools/build_recomp.py`)

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

## Other repeated processes

| Process | Trigger | Measured cost | Redundancy and status |
|---|---|---|---|
| Targeted weapon-lock marker lockstep | check the named box/hexagon geometry before the full gate | about 0.5 s per 4,000-state run at the two follow-up seeds (9 Oct); 4.2 s before returning driver thunks were enabled | `func_lockstep --only VGAME.EXE:B171 --states 4000 --verbose`; the existing thunk substitution is enabled for this VGAME routine too, increasing the fixed-seed comparisons from 358 to 4,857 (17 skipped); this changes test inputs to reach drawing branches, rather than being an optimisation with identical inputs; full routes retain the actual driver |
| Targeted cockpit-canopy lockstep | check canopy posts, compact side frames and landing cue before the full gate | 0.7 s per 4,000-state run, three seeds concurrently (9 Oct) | `func_lockstep --only VGAME.EXE:D6DD --states 4000 --verbose`; returning thunks on both sides allow graphics calls to return; 4,491 / 4,482 / 4,476 comparisons, 15 / 9 / 7 skipped, zero mismatches; full routes retain the actual driver |
| Targeted projectile lockstep | check slot motion and gunfire refill before the full gate | 1.0-1.2 s per 4,000-state seed, two concurrently (9 Oct) | `func_lockstep --only VGAME.EXE:4777 --states 4000 --verbose`; small-count/firing plants and returning driver thunks reach more active states: 2,823 / 2,886 / 2,891 comparisons, zero mismatches. A private observer confirms 18 full refills among the fixed-seed comparisons. Full routes retain the actual drivers |
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

## What the gate is checked against

The gate compares the recompiled engine with this project's own interpreter,
so a model error shows in neither. Fidelity to a real PC is checked
separately, against recorded references: GOG's DOSBox 0.74
(`tools/fidelity.py`, saved video and flight captures), DOSBox-X for some
timing traces, and 86Box as the real-hardware-style reference once it runs
the game. Those are recorded or run per question, not per commit.
