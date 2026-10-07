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
Stage times, new run: translate and build 457 s, coverage 173 s, second
build 2 s (skipped), parity 503 s, lockstep tail 229 s.

| Stage | Measured | Duplicates / redundancy | Status |
|---|---|---|---|
| 1-2 translate, build | 7-8 min | a fresh work directory builds everything | kept; the build tool already compiles in parallel |
| 3 coverage | 4-5 min | runs every route once only to find untranslated code | kept: it is the check that the translation is complete |
| 4 translate, build again | 7-8 min | regenerated every file and rebuilt all of it though coverage usually adds nothing | **fixed**: only files whose bytes changed are rewritten; the rebuild is skipped when none did; `--seed-coverage DIR` starts from an earlier run's coverage so step 1 is already complete |
| 5 parity | 8-14 min | 64 runs on 12 slots, started alphabetically; two routes that are one session ran twice per engine | **fixed**: up to 24 slots, longest first, identical sessions run once and every route's milestones are checked on the shared run |
| 5b interpreter half of parity | 5-6 min of the parity stage | an interpreter run is a pure function of the game files, the route and the interpreter's own code, yet reran every gate | **fixed**: results cached by a key over `src/` except `matched.c`, `CMakeLists.txt`, the game files and the route (text, replay files, seed chain); entries expire after 14 days; `--verify-interp-cache` reruns and requires equality. Measured: parity 522 s to 174 s with 31 of 31 results reused and every route verdict identical to the fresh run |
| 6-7 lockstep | 2-3 min | waited for parity though they need only the build | **fixed**: started beside the parity runs (on a busy machine the single-thread matched lockstep can still finish after parity: 229 s tail in the last run) |

Same-session routes (executable lines identical) are found by content, not
by name; today: `recon_career` and `recon_return`.

## Other repeated processes

| Process | Trigger | Measured cost | Redundancy and status |
|---|---|---|---|
| Matched-routine lockstep (`func_lockstep --states 4000`) | every gate; every new matched routine | 3-4.5 min for 246 routines; 12 s at 300 states | single process. **Not sharded, on purpose (analysed 6 Oct):** one random stream runs through every routine and part of it depends on what each routine does, so splitting it changes the test states; per-routine seeding would be a different test, not the same one faster. The time is mostly copying and comparing 1 MB of memory per state (about 0.2 ms x 924,000 states); the safe speed-up is tracking writes instead, which is a larger change to the harness (open, low priority, since the gate now runs it beside parity) |
| Instruction lockstep (`insn_lockstep --states 64`) | every gate | seconds | none found |
| Census refresh (`reimp_names.py`, then `progress.py --census`) | after each batch of matched routines | about a minute | the lead list only changes when the Reimp does; regenerate it then, refresh the census each batch |
| Route recording (adaptive pilots) | after a timing change moves a flight | 10-20 min per flight; try several parameters in parallel | recorded inputs replace them in the gate; a timing change invalidates all of them at once (see `airair_type5-8`, `strike`, `cargo`, `recon`) |
| Draw-list capture windows | per presentation stage | 3-4 min per window, four at a time | windows are independent and already run in parallel |
| Worktree build and generated code | per worktree | 8 min first, then incremental | give each tree its own TEMP (build.cmd shares one log); do not share a generated directory between concurrent builds |
| PC-parity run (`pc_parity.py`: pictures, music, saved roster on three references) | before a release; after a machine-model change | about 12 min wall | the three references run at once; 86Box's capture, music and save run one after another in one thread (each starts and stops the only headless 86Box); ours is replayed per reference, not shared |
| Machine-behaviour probe (`fidelity_all.py`) | after a machine-model change | about 2 min with a saved 86Box sheet; 86Box adds a 4-minute run (`--86box-run`) | the 86Box sheet only changes with the model, so the saved sheet is reused |
| 86Box boot attempts | per BIOS or config change | 2-5 min per capture series | a boot-sector test (`hello_floppy.py`) answers "does the BIOS boot" in one run, before any larger image |

## What the gate is checked against

The gate compares the recompiled engine with this project's own interpreter,
so a model error shows in neither. Fidelity to a real PC is checked
separately, against recorded references: GOG's DOSBox 0.74
(`tools/fidelity.py`, saved video and flight captures), DOSBox-X for some
timing traces, and 86Box as the real-hardware-style reference once it runs
the game. Those are recorded or run per question, not per commit.
