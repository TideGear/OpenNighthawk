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
| Matched-routine lockstep (`func_lockstep --states 4000`) | every gate; every new matched routine | 3-4.5 min for 250 routines; 8.5-10.2 min for 415-429 routines with three branches checked together; about 14 min for 484 routines beside fresh parity (8 Oct); 12 s at 300 states | single process. **Not sharded, on purpose (analysed 6 Oct):** one random stream runs through every routine and part of it depends on what each routine does, so splitting it changes the test states; per-routine seeding would be a different test, not the same one faster. The time is mostly copying and comparing 1 MB of memory per state (about 0.2 ms x 924,000 states); the safe speed-up is tracking writes instead, which is a larger change to the harness (open, low priority, since the gate now runs it beside parity) |
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

## What the gate is checked against

The gate compares the recompiled engine with this project's own interpreter,
so a model error shows in neither. Fidelity to a real PC is checked
separately, against recorded references: GOG's DOSBox 0.74
(`tools/fidelity.py`, saved video and flight captures), DOSBox-X for some
timing traces, and 86Box as the real-hardware-style reference once it runs
the game. Those are recorded or run per question, not per commit.
