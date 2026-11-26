# OpenNighthawk project goal

**Limit this file to 9,000 characters.** Check the UTF-8 character count after edits with `py -c "from pathlib import Path; print(len(Path('docs/goal.md').read_text(encoding='utf-8')))"`. Trim before adding. Keep changing detail in the linked records.

Preserve DOS F-117A Nighthawk Stealth Fighter 2.0 in a lockstep-verified Windows recompilation and decompilation. Keep fixes-off behavior faithful and supported source installs read-only. Ship source-only setup. Keep game data, decoded assets, generated translations, ROMs, and private captures local under the [provenance rules](provenance.md). Follow [README.md](../README.md) and [recomplauncher.json](../recomplauncher.json).

## Current priority

Start at the current stopping point in [handoff.md](../handoff.md). Keep active tasks, settings and evidence there. Reconcile dated notes with current code.

**1. Owner-visible bugs first.** Fix open Remastered defects in [owner-issues.md](owner-issues.md) before performance work: R52, the close missile (menu 3 smooth via a 5 ms machine copy, menu 1 jittery: draw it natively in both, the copy as checker), R51's remaining coast-light shimmer, and every other open row. Fix bugs without adding per-picture machine copies or cost 4K/60 cannot carry. Prove each by "Picture correctness".

**2. Then Remastered at 3840 × 2160 and 60 fps with plenty of headroom** under light load too. The GPU raster takes about 2.5 ms at 4K; the cost is CPU work: the per-step render copy (about 11.5 ms) and per-picture work (2D layer redrawn at 9×, about 6 ms; scene blend and vertex build, 4 to 7 ms). Build the owner-agreed native renderer ([gpu-compositor.md](gpu-compositor.md), 18 Nov): (A) the 2D layer once per step, drawn on the GPU; (B) scene vertex buffers once per step, blended on the GPU; (C) native far scenery off the machine thread, the render copy as oracle. Target on the late flight: main-thread and worker CPU each under about 4 ms per picture and GPU under 8 ms, then the acceptance suite. Add no new patches to per-picture Stage 2 blending, which (B) replaces, unless they fix an owner-visible bug.

**Baseline.** The full DOS loop and Phase 1 translation are established; their parity gates are regression checks. R49 is owner-confirmed; R50 is replay-verified; far fade on by default. The 25 Nov gates failed only the scanner: the 2.263 G HUD tape, the 14.1 G missile and a 6.6 G Vietnam horizon shift. `F117_PREHUD=1` measured worse on 25 Nov.

## Whole-project scope

Use the [roadmap](roadmap.md) and topic records for the full inventory. Use `py tools/progress.py` for current scores and `--stale` to revisit affected estimates. A rounded score never closes a missing proof.

- **P1.** Keep the full DOS game loop, timing profiles, input, saves, devices and audio testable. Prove translated instructions against the interpreter and whole-game behavior against the independent references in the parity audit.
- **P2.** Track all-code naming, gameplay route coverage, and second-read accounting outside routes as separate obligations. Validate census inputs and base every claimed match on the required original-routine lockstep, instruction coverage, caught mutations and applicable route hashes. Fixture-only work earns no route credit. Keep declined bodies, unresolved static ranges, machine-option error paths and probable-dead code open until proved. Apply the original-by-design rules or dead/data certificates; include encoded code, the packed stub and indirect, interrupt, overlay and self-modifying paths. Use [progress.md](progress.md), [static-code-review.md](static-code-review.md), the census and the reviewed reference table for exact closure rules.
- **P3 gameplay and presentation.** Keep switchable fixes, recorded cadence and fix sets, save warnings, and fixes-off parity. Preserve recommended real-time play and explicit opt-in for QUIZ. Keep Presentation and Fixes independent. Preserve Original VGA scanout and N=1 redraw equality on corresponding game frames. Verify Remastered framing, depth, decals, extended scenery, coastlines, interpolation, HUD and MFDs, and CPU fallback. Remastered may differ in rasterization where picture and motion are preserved.
- **P3 host and distribution.** Cover settings, SETUP input, controller/HOTAS and remapping, key release across overlays, replays, screenshots, CRT and accessibility. Verify audio from guest call to mixed output across applicable DOS drivers and hybrid paths, including front end and debrief. Keep the [Phase 3 backlog](p3-handoff.md) in scope, including couch startup/exit, portable data, rotating logs, INI documentation, optional asset import and planned Mac visuals and lighting. Verify upgrade, retry and portable installs for Steam and GOG with isolated data. Test launcher changes through RecompLauncher.
- **P4.** Record human play and listening checks, independent references and physical hardware coverage. Label proxy measurements, missing hardware and open owner judgments.

## What counts as done

**Picture correctness.** Read the original routines and trace their output into the compositor. Reproduce the defect at recorded clocks with known settings and build. Compare against the original and the verified Remastered reference appropriate to the view. Check affected draw order, clipping, depth, palette and timing, including before/during/after short-lived effects and occlusion in both directions. Use distinct presented frames, scanner checks and live pacing where relevant. Require identical machine hashes for presentation-only work and document intentional picture differences.

**4K/60 acceptance.** Run the full fast path in a visible 3840 × 2160 window with vsync enabled and `F117_HIDDEN_WINDOW` unset. Use the seven-flight suite in the current handoff, including takeoff, demanding late flight, long and busy flights, and both theatre routes. Run four quiet runs per flight across two times of day and two under `private/partb/load.sh light`. Reject quiet runs with average background CPU above 3%. Time runs alone with a frozen build and exact settings recorded. For each run, report the first five seconds separately and report each billion-clock stretch afterward. Require zero intervals over 16.7 ms after warm-up and zero over 33 ms including warm-up. Report quiet and loaded separately, with CPU/GPU work and frame-time percentiles as headroom, plus an unsynced throughput run.

**Regression gate.** Run focused checks and the applicable full gates against the candidate build. Check machine parity, picture guards and scanner evidence, and [host acceptance](host-acceptance.md) through the real app. For threading changes, also check copy isolation, repeatability and crashes. If the target proves unreachable, report failing routes, clocks, counts, traced bottlenecks and measured options.

## How to work

- Read the code, capture a baseline, and change one cause at a time. Exercise normal input, including SETUP answers, in end-to-end checks. Diagnose slow frames with `F117_SLOW_FRAME_TRACE` and phase timings. Prefer removing measured work to moving cost or latency between frames.
- Do not waste wall time. Iterate on hidden fixed-cadence runs with per-picture CPU timings (`F117_WORKER_TIMING`, `F117_SLOW_FRAME_TRACE`), seek or skip to the moment, run independent hidden checks side by side, and overlap builds and writing with long jobs. Reuse evidence. Run timing alone; take the owner's screen only for visible timing that counts, and say so first. Keep required coverage intact.
- Build from PowerShell with `cmd.exe /c ".\build.cmd"`. Keep captures on the scratch drive in fresh folders with isolated saves. Run long jobs in the background with a watcher. Check for leftover processes and stop only the PIDs identified as part of the run.
- Record verified findings in the relevant audit, update owner-visible issue status, and keep the current handoff actionable. Record builds, inputs, commands, measurements and what remains unverified. Use `py tools/progress.py --title` for commit prefixes. Stage explicit paths, verify the staged diff, preserve owner edits, and commit and push verified changes under the owner's identity without AI attribution.
- Bring picture comparisons to the owner before changing visual defaults. Release only with explicit owner approval.

## Where the current facts live

| File | What it owns |
| --- | --- |
| [owner-issues.md](owner-issues.md) | Owner and player reports and confirmations |
| [renderer-motion-audit.md](renderer-motion-audit.md) and [wide-margin-audit.md](wide-margin-audit.md) | Picture evidence |
| [gpu-compositor.md](gpu-compositor.md) | Performance evidence |
| [parity-audit.md](parity-audit.md) and [bugs.md](bugs.md) | Original parity and switchable fixes |

Start a new session with `/goal Follow docs/goal.md. Resume from the current stopping point in handoff.md.` Add a specific outcome only when the owner changes the priority or asks for a bounded task.
