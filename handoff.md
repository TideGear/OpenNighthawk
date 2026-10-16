# Handoff

For the next conversation on this repository. Read this, then
[docs/roadmap.md](docs/roadmap.md) (what is done and left; keep it current),
[docs/architecture.md](docs/architecture.md) (how parity is built and
checked), [docs/bugs.md](docs/bugs.md) (the original game's bugs),
[docs/presentation.md](docs/presentation.md) (60+ fps and 4K) and
[docs/repeated-processes.md](docs/repeated-processes.md) (every process we run
more than once, what it costs). State as of 15 October 2026.

This file holds only the current state and standing practice, not a
session log. Earlier detail is in `git log -p handoff.md`.

## Current state (15 October)

Scoreboard: P1 100.00%, P2 67.82%, P3 82.75%, All 85.82% (P4 is no longer scored)
(`py tools/progress.py --title`). Phase 2: 891 census functions / 118,501 bytes.

### Presentation: latest (15 October, evening)

**`--present rerun` is integrated and gated.** In-between camera/aircraft/weapon poses are drawn by the original's
own code on a throwaway machine copy, then refined at any scale; a smoothed playback clock (three published steps)
times them (`F117_PRESENT_CLOCK=raw` for the old clock; opt-in knobs `F117_PRESENT_CLOCK_ALPHA`/`_PULL`, defaults
0.2/0.05). Defaults and the owner's settings are unchanged; the owner's local `4k-rerun.bat` runs the 4K preset with
`--present rerun`. Design and figures: [docs/presentation.md](docs/presentation.md).

- **Proof at integration (`fe20913`):** full gate; the 45-route N=1 rerun pass keeps 259,240 frames / 259,166 pairs
  and every guest hash; boot N=4/9 368/366; 1,638,579 copy-isolation checks with no changed real state.
  Evidence `D:/f117-gate/present-v2/rerun-accept/final-gate-verdict.json`, `app-verdict.json`.
- **Threaded palette bug (fixed 15 Oct):** the threaded app path froze the palette at N>1 in `interp` and `rerun`
  (the driver's colour cycling of the gun rounds showed solid dark red; fades froze too). Headless runs never use
  that path, so no route check could see it. `F117_PRESENT_SHOTS` dumps what the app presents; the app summary
  counts distinct presented/scanned palettes, and `present_light_check.py` now checks app-only changes by replaying
  a recorded session through the app (`--app-session`, `--app-exit-after`). Owner's gun session:
  `%APPDATA%/F117Recomp/F-117A/sessions/session-20261015-203305` (burst near 2.83B clocks).
- **Clock tuning (one hidden run each, `D:/f117-gate/present-v2/clock-tune`):** default 6 freezes, 2.5% >20% speed
  changes; alpha 0.4 / pull 0.025: 3 and 1.3%; picture age not measured. Defaults unchanged: the owner's call.
- **Verification scope:** `drawfeed.c/.h` and `rerun.c/.h` execute or snapshot machines, so they need the full gate
  plus `presentation_check.py --mode rerun` over all 45 routes; the light check refuses them.
- **Owner verdict on a real screen (15 Oct):** played `4k-rerun.bat` (fullscreen 4K, rerun) twice: "seems great"
  apart from the original's jitter (20:50), and "looks good" after the palette fix (22:30). Not yet agreed as
  formal motion acceptance for P3 (ask whether it counts before changing `docs/progress.json`).
- **Still open:** clock skips against picture age; formal motion acceptance (above); per-machine
  observer/runtime/fix state for parallel copies above about 120 Hz (one copy costs about 6 ms); the small
  `DF03`/`C2C8` vertex group (`rerun-accept/object-follow-up.txt` is a lead); `thin_strip_stroke` in `hires.c`
  ignores `present-line-width`. P3 stays at 82.75% until motion is judged.
- **Owner decisions (15 Oct):** stop improving primitive pairing (identity pairing is opt-in,
  `F117_PRESENT_IDENT=1`: it halved unpaired shapes but deformed the extra pairs); road and coastline jitter are the
  original's (also in GOG DOSBox and in sharp `replay` mode) and are **not** smoothed at any scale (21:23); the
  higher-resolution and higher-frame-rate modes are experimental and keep the original renderer's quirks (README);
  draw distance is not extended. Owner flights frozen for reference: `D:/f117-gate/present-v2/owner-roads-20261015`
  and `owner-sharp-20261015`. The rerun research harness stays on branch `draw-rerun-probe` for history.

### Presentation: what is integrated

- Baselines every presentation change must keep: all 45 N=1 routes 259,240
  exact frames / 259,166 pairs; live boot N=4/9 368 frames / 366 pairs each;
  the owner's complete N=9 flight 69 checkpoints/final `31c259b992340414` and
  2,101 frames / 2,099 pairs. N=1 remains the exact original.
- HUD, map position, sky/ground and world lines are state-driven where the
  capture was the wrong technique: the HUD's tapes, heading strip, pitch
  ladder, flight-path marker and bars come from game state
  (`F117_PRESENT_HUD=old` switches it off); continuous state moves on the fine
  grid, discrete readouts, switches and the map arrow's heading wait for the
  logic frame. Map-origin subtraction keeps its 16-bit wrap. Models match
  29,594 original HUD calls, 11,490 panel calls and 714 map blits. Evidence
  `D:/f117-gate/present-v2/{diag,fix3d,hud,hud2}`.
- Dots/lights, roads and matching outlines are drawn from exact camera
  geometry (full `256*x/z` projection). One perpendicular-width pen serves
  HUD, outlines and strips; no per-role widths are assigned yet.
- Near-clipped polygons are drawn from geometry; `F117_PRESENT_DIAG` is the
  per-pixel origin diagnostic.
- Threaded presentation (`drawlive_collect` / `drawlive_publish` / worker
  thread, `F117_PRESENT_THREADS_OFF=1` for single-threaded) passed fullscreen
  4K/59.99 Hz/vsync N=9 on 13 October (strike, air-to-air, landing; evidence
  `D:/f117-gate/p3-stream`). Fullscreen pacing of the later batches is
  unchecked. Hidden 3840x2160 D3D11 throughput is 119-150 fps with <5 ms p99
  regeneration/raster but occasional ~100 ms upload stalls.
- Scene-input foundation: opt-in `F117R_OBSERVE_SCENE=N` captures
  pre-visibility requests and complete inputs; `tools/scene_capture.py`
  audits completeness; `tools/scene_model.py` decodes complete topology and
  derives detail selection (agrees with the original on 192 requests, 191
  geometry submissions and 205 reachable detail variants). Shared-vertex
  coverage is synthetic only. The unconnected `src/present/scene_mesh.c`
  draft (mesh/pose/visibility/order/material evaluation, synthetic CTest) is
  on master as part of the gated tree; it is not a finished fix. Evidence and
  private original-execution harnesses: `D:/f117-gate/present-v2/scene`
  (notes `next-step.txt`, `live-integration-notes.txt`).
- The broader world-geometry plan (complete topology before visibility
  filtering, persistent identities; no more clipping exceptions) is recorded
  in
  [docs/presentation.md](docs/presentation.md#consolidate-world-geometry-before-more-clipping-exceptions-15-october-2026);
  it is no longer the main path. The clip-only prototype in
  `D:/f117-gate/present-v2/coast-finish/clip-prototype` is set aside, not
  integrated; its test can be adapted.
- The coarse-interpolation checker fails on unknown novel records and
  regeneration-count overruns.
- Open: width tuning by drawing role, weapon/target symbology, the owner's
  physical-panel motion verdict. Coastline topology work is not planned (no
  jitter smoothing, owner 15 Oct).

### Phase 2

- Next candidates (reached by `full_cycle`, unmatched; ranked list
  `D:/f117-gate/format-init/reached-corrected.tsv` minus the matched table):
  MPS_LOGO `0006:0072`, `0146:08CC`; DSWAP `0010`, `1D66`, `117E`, then the
  rest of that list. Matched 15 Oct: PLAYER `1602`, VGAME `130D:033F`. Proven
  privately and integrated on worktree branch `p2-batch-setup-logo`
  (`D:/f117-wt/gate`), awaiting their gate: SETUP `19B6` (`setup_crt_setargv`,
  the same C start-up as PLAYER `1602`) and MPS_LOGO `0146:1894`
  (`mps_logo_open_file`, the C runtime's open, as START `A59A`); proofs in
  `D:/f117-gate/p2-setup-19b6`, `p2-logo-1894`. Kit libraries refreshed from
  main at 7fa1b62 (old ones in `D:/f117-gate/kit-libs-old-20261015`).
- Other leads: PLAYER `026B:0211` (a distinct timer re-sync) and `026B:0336`
  (tick/callback service; calls original code, so needs returning-helper
  fixtures). Private static drafts are unproven transcripts, not
  integration-ready bodies.
- Deliberately left as original code: the STI-first timer interrupt handlers
  (VGAME `0x1C9B`, START `0x8D0B`, END `0x4BEF`: after STI the run loop admits
  one more instruction, so a matched body cannot do more than the original); START `8427` and
  END `4393` (routes missed their planted defects and lockstep cannot run
  their waits); DSWAP `0x03DF3` (2,791 initial zero bytes, no generated entry,
  the static draft decodes data; evidence
  `D:/f117-gate/dswap-3df3/candidate-review.json`).
- **VGAME 0FB2:04DD** (the line clipper's divide-error handler) is matched and
  fixture-proven but **not integrated**: no route reaches it (146,153 real
  clip divisions across 14 routes all stay well under the 32,767-unit
  threshold). Preserved on local branch `irq-divide-fault` (commit `e15c9ef`)
  and `D:/f117-gate/next-drafts/irq-divide-fault/0002-match-clip-divide-fault-04DD-lockstep-only.patch`.
  Integrate only if a route (or a clearly labelled staged input) reaches it.
- Kit harness features: scripted INT 21h answers (`g_dos_script`, with `.put`
  for the byte a one-byte read brings), and skipping states that overwrite a
  planted callee stub.

### Phase 3

P3 (82.75%) is held down by Stage 3, smooth in-between frames (15%: rerun mode is built, but motion
acceptance and clock tuning remain), Stage 4 infrastructure (90%: fullscreen pacing of the latest
builds, slower machines, cargo/career flights) and Stage 2 (95%: coastline
transitions, widths by role). Details and the method: docs/progress.md.

## Next steps

1. **Gate the Phase 2 batch on branch `p2-batch-setup-logo`** (pushed, worktree `D:/f117-wt/gate`, one commit
   `e11062f` on top of `7fa1b62`): rebase onto `master`, build, ctest, one full gate, `presentation_check --scales 1`,
   refresh `docs/progress.json` (measure the census delta against the same TSV with and without the change:
   PLAYER `1602` + VGAME `033F` added exactly 2 functions / 841 bytes), README/roadmap/architecture figures,
   commit to `master`, push.
2. **De-identify code comments** (found by the 15 Oct docs audit; public-repo rule): `src/cpu/cpu.h:3` names the
   earlier project; `cpu.c`, `x86_sem.h`, `dos.c`, `pc.c`, `tests/cpu_api.c` and `tests/sst286.py` cite private
   `tools/x86oracle/...` paths; `tools/reimp_names.py`'s usage shows a project path. Comment-only, but under `src/`:
   batch it with the next full gate.
3. **Small code-side corrections from the audit:** `tools/pc_parity.py` 86Box miss comment (p037, not the roster
   screen); `tools/dosbox_compare.py:104` should point to `build_dosbox_x.md`; `tools/d1prox_check.py` describes the
   withdrawn S=9-floor version; `secret_airstrip_return.args` steps vs its comment (18,635,798,708); the
   `DRAWLIVE_POOL` comment in `drawfeed.h` says two published steps (rerun publishes three; drawfeed needs the full
   gate); possible 386-model gap: a warm DOS open is charged 50,145 cycles against 3.56 M measured (check before
   changing).
4. **Presentation** (owner verdicts first): the 4K screen/motion verdict (ask before any visible test); whether to
   adopt the clock setting alpha 0.4 / pull 0.025; `thin_strip_stroke` ignoring `present-line-width`; the open items
   above. No jitter smoothing (owner).
5. **Phase 2** with the kit and parallel agents, from the candidate list above.
6. **Phase 3 coverage:** time a long cargo/career flight; weaker machines. Full checksums via `F117_BENCH_HASH=1`
   add work outside frame timers and are diagnostics.
7. **Branch housekeeping:** many local branches are fully merged into `master`. Not merged: `flight-input` (older
   aircraft-traffic draft, superseded by `b41ec9c`; its worktree `D:/f117-wt/span-emitter` has uncommitted edits),
   `irq-divide-fault` (kept on purpose) and `p2-batch-setup-logo` (item 1). The stopped presentation WIP branches
   are preserved for history; do not merge them again. Ask the owner before deleting anything.

## Owner decisions and instructions in force

- **Gate speed (15 Oct):** only optimisations that are totally safe and weaken no check. Declined: splitting the
  long interpreter run into checkpoint segments (only as strong as the saved states are complete) and dropping
  `--verify-interp-cache`. Adopted: the light check's app-only class (`src/host/main.c` and the example ini files
  are built only into `f117a.exe`, which the gate never runs): build, CTests and an app replay of a recorded
  session in `interp` and `rerun` (`--app-session`, `--app-exit-after`), with exact logic frames and every
  scanned-out palette presented; break-tested against the frozen-palette bug.
- **Gameplay and logic parity are the top priority.** Independent rendering
  must preserve original control reads and simulation timing. Latest-state
  rendering avoids interpolation age; smoothing without waiting requires
  strictly visual prediction and measured corrections. Do not silently change
  defaults or the owner's configuration.
- **Keep all original graphics** (15 October): fonts, sprites and bitmap
  artwork stay, including the map aircraft arrow (a 4x4 sprite from eight
  headings). Smooth positioning and render the existing geometry; no vector
  replacements. Widths by reliable drawing role are a tuning task, not a
  fixed four-class list.
- **No screen takeover.** Ask before any visible/fullscreen test; keep
  automated work headless/hidden. The owner stopped a visible fullscreen
  timing run; do not count it as acceptance.
- The owner's untracked `4k60.bat` stays untouched: do not overwrite or stage
  it. Main's executable is rebuilt for `4k60.bat`.
- Dropping `--verify-interp-cache` would weaken a verification, so it stays the
  owner's call ([docs/repeated-processes.md](docs/repeated-processes.md));
  `--overlap-jobs` speeds the gate without weakening it.

## Traps

Presentation:
- Never force `F117R_OBSERVE_PAGES=1` on live presentation: it reseeds every
  phase and prevents pairs.
- Short windows with a previously cached map can show only its original
  bitmap; the full owner replay shows the refined map terrain.
- A timing capture that changes display mid-run logs two display lines and
  the checker rejects it: rerun with nothing else going.
- `tools/presentation_check.py`: use fresh baseline scan runs, canonical
  aliases and a fresh output save for each run. `f117a.4k.ini` is an optional
  preset.
- Original-execution harness return traps use a self-loop; standalone CPU
  stepping does not stop at HLT.

Matching and the gate:
- `1450` is also the presentation observer's frame-phase hook. At one
  address only the first override runs, so a matched body there must call
  the observer itself (`observe_game_draw`) and the address goes on
  `deliberate` in `tests/test_overrides.c`. `kit.py integrate` refuses an
  address with an observer.
- The gate's commit-derived seed rotates with HEAD and can fail a routine the
  commit did not touch. Reproduce with `func_lockstep --seed S --only MOD:IP`,
  or shard the run (`--shard K/8`) to find the failing routine.
- `func_lockstep --only MOD.EXE:HEX`: the IP must have NO leading zeros
  (`D0C`, not `0D0C`), or it silently compares 0 states. "Not testable here
  (routes only)" means only a real route can prove it.
- Run every new body at four or five seeds, not one.
- Charge the clock between every port access (a lump `icount` after several
  IN/OUT made 39 routes DIFFERENT while lockstep passed), stop right after
  each STI, and prove a body on `ibm_boot` first:
  `F117R_MATCHED_LIMIT=N py tools/run_route.py tools/routes/ibm_boot.args
  --engine recomp` (22 s), comparing `final hash` with the gate's interpreter
  run.
- A wait inside a matched routine must clamp `m->run_until` to `c->stop_at`
  (SETUP's `SQ_RUN`; `support_int` bounds nested INT runs at the caller's
  next event). Check the gate's step 9 (event limits) for any routine that
  guest-calls code which can wait on input or a timer; func_lockstep cannot
  see it.
- Matched routines with a switchable fix site after their entry decline
  while that fix is enabled (`fix_inside` in matched.c). When matching,
  check `src/fixes/fixes.c` for sites inside the routine. Check every
  callee's RET form.
- The coverage pass runs for every commit that adds a matched routine (the
  translator isolates each matched entry, changing generated code).
- `kit.py checkpoints` needs the interpreter cache entry for the current
  source tree: after changing anything under `src/` other than `matched.c`
  (including observer work), run a gate before the next proofs'
  checkpoints. Refresh `D:/f117-gate/kit-libs` from a main build after the
  libraries change.
- Reach: the gate's recomp `.cov` files contain only interpreted fallback
  instructions, so absence proves nothing. Use a complete **interpreter**
  `full_cycle` capture (`D:/f117-gate/format-init/full_cycle.cov`,
  `reached.tsv`, `batch-reach.json`; `tools/census_reach.py` states this
  limit). `next-batch-review.json` there warns about static walks past
  terminal DOS exits. Census rows with no region are data (SETUP `0x027AE` is
  strings).
- SETUP's INT 10h at `14EF` runs two native callback/stub clocks atomically;
  `native_stub_time` accounts for it, so an SDL "one-instruction alert" there
  is not extra execution.
- `build.cmd` logs inside the checkout's build directory. Run builds from
  PowerShell; through bash they hang.
- Finished agents can wake on their own timers and resend their report many
  times: stop them (TaskStop) once their proof is verified.
- Gate speed: `parallel()` prints per-stage wall time; coverage and parity
  are each as long as the `career_promotion` seed chain. Keep `--jobs 16`
  (24 workers was not faster). Unit tests run with up to eight workers
  (`--unit-jobs 1` for serial).

## How Phase 2 is done: the kit and parallel agents

`D:/f117-gate/kit/kit.py` runs a private proof for one routine (`init`, write
`body.c` / `fixture.inc` / `mutations.json`, `lock --cover`, `seeds`,
`mutations` (four edits built at a time), `checkpoints`, `verdict`,
`integrate`); `D:/f117-gate/kit/BRIEF.md` is the agent brief with every trap
found so far. Agents work only in their `p2-*` directory. Then:

1. Verify each `private-verdict.json` yourself (counts, mutations, source
   hashes, real-route calls).
2. `kit.py DIR integrate` (set `KIT_ROOT` to integrate into a worktree);
   apply any harness change the agent reports by hand.
3. Build, ctest, ONE full gate per batch, the 45-route
   `presentation_check --scales 1`, docs, commit, push.

Gate batches in a separate worktree (`D:/f117-wt/gate`, branch
`gate-batch7`; build it once with `build.cmd OFF` before the gate, which
needs its `f117cpu_api.dll`) when the owner is testing the main tree's build.

Screening and older method: `tools/matched_draft.py --dossier` (module/seg/ip
from the region name); batch several routines per gate; transcript first to
validate the harness, add the routine to `COVERED`, fixtures until every
instruction is reached (the skip tally says why states drop), then the named
body, tested in sections that hand back to the original; run
`D:/f117-gate/frame-3ada/room_audit.py` on the body; then the real route
(`route-proof.py`). `D:/f117-gate/frame-3ada/swap_body.py` swaps a body into
the proof build in `D:/f117-wt/span-emitter`. `tools/reimp_names.py` has the
private lead list. Never copy an old private matched file wholesale.

## How this project verifies Phase 2 work (read before matching a new routine)

A routine only counts as matched once it passes BOTH:
1. **Fixture/lockstep proof**: `func_lockstep` at three seeds, comparing the
   hand-written C against the original's own bytes from randomized (and for
   routines with real argument/driver dependencies, returning-driver-thunk)
   entry states. Zero mismatches required.
2. **Real-driver proof**: an actual scripted route (`tools/routes/*.args`,
   sometimes replaying `.input`) must execute the routine with a positive
   count, with every checkpoint and the final hash identical between engines.
   Fixture proof alone is not enough: this project has repeatedly found
   routines that pass every fixture seed but were never reached by real play
   (04DD above; see `docs/repeated-processes.md` ("Phase 2 proofs", traps) and
   `docs/roadmap.md`). If no route reaches a new routine, say so plainly and
   keep it out of `master`.

Every change to `src/matched/matched.c` needs a fresh full gate
(`py tools/build_recomp.py --data D:/GOG/F-117A --seed-coverage
C:/Users/Tideg/f117-recomp-local/coverage --jobs 16 --verify-interp-cache`,
via `D:/f117-gate/affinity_launch.py`, which sets and verifies `BelowNormal`
priority and affinity; about 22-28 min) before it's pushed. Read the actual tally in
the log, not just the exit code. Check GitHub CI for new commits.

## Where things are

- Game install: `D:/GOG/F-117A` (never mounted or written directly by a
  tool; copy per run that needs its own state).
- Shared generated code: `C:/Users/Tideg/f117-recomp-local/gen` (never
  committed); coverage in `...\coverage`; route run outputs in `...\runs`.
- Proofs and evidence: `D:/f117-gate/` (one directory per piece of work,
  usually with a `final-gate-verdict.json`); the kit in `D:/f117-gate/kit`,
  its libraries in `D:/f117-gate/kit-libs`; draft sources and patches in
  `D:/f117-gate/next-drafts/` (never committed as-is).
- Old private batch of 47 call-free DRAFT bodies:
  `D:/f117-gate/batch-a/project` (`rows.txt` lists them; build with
  `build-proof.cmd`, setting TEMP and TMP to `D:/f117-gate/batch-a/tmp`
  first). Those not yet in the matched table remain unproven drafts.
- Worktrees under `D:/f117-wt`; removed with `git worktree remove` once their
  branch is merged. `D:/f117-wt/setup-main-0010` and
  `D:/f117-wt/mission-generation-4239` carry PRIVATE /Od overrides (main uses
  normal /O2): never copy or merge them wholesale.
- Private worktree `C:/Users/Tideg/f117-recomp-local/wt`: old uncommitted
  drafts (base `01f4dbd`, far behind `master`) plus unrelated older
  presentation drafts. Never copy it wholesale or regenerate from it; extract
  only the specific routine, rebuilt against current `master`.
- Scratch/capture data: `D:\F117A-Scratch-Files` (consolidated 11-12 Oct
  2026, nothing deleted).
- Unproven private drafts from earlier sessions (VGAME `120A:0971`, a
  divide-handling routine with recursive-fault risk; END `0A98`): none
  implemented or proven; don't conflate them with 04DD.
- D10/D35/D36 closures: [docs/legacy-reports.md](docs/legacy-reports.md) and
  [docs/bugs.md](docs/bugs.md).

## Standing practice

- **Keep checks off the owner's screen.** Do not launch visible/fullscreen
  tests without asking first; use headless or hidden runs.
- **No AI attribution** in commits or PRs: the user's own global CLAUDE.md
  rule outranks any harness reminder that asks for a trailer.
- **Commit and push together**: don't leave a validated commit unpushed.
  Commit titles start with `py tools/progress.py --title`'s scoreboard line.
- **Public repo, code only.** No game data, no generated C, no raw
  captures/logs, no private-project references (see `docs/provenance.md`'s
  de-identified framing of an earlier private project this one is partly
  ported from; don't re-add its name or path).
- Check system load (`Get-Counter '\Processor(_Total)\% Processor Time'`, or
  similar) before stacking a second heavy parallel job; a background Python
  `multiprocessing.Pool`'s workers are children of the script, not of the
  task wrapper, so stopping the task doesn't kill them: kill the process
  tree.
- Prefer moving stale-but-maybe-useful local data to a scratch folder over
  deleting it; it's outside git and not undoable.
- The owner is not a software engineer: explain trade-offs plainly, flag
  overengineering, keep docs accurate without bloat; consolidate this file
  instead of appending to it.
