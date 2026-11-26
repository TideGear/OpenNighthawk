# OpenNighthawk project goal

**Limit this file to 9,000 characters.** Check the UTF-8 character count after edits with `py -c "from pathlib import Path; print(len(Path('docs/goal.md').read_text(encoding='utf-8')))"`. Trim before adding. Keep changing detail in the linked records.

Preserve DOS F-117A Nighthawk Stealth Fighter 2.0 in a lockstep-verified Windows recompilation and decompilation. Keep fixes-off behavior faithful and supported source installs read-only. Ship source-only setup. Keep game data, decoded assets, generated translations, ROMs, and private captures local under the [provenance rules](provenance.md). Follow [README.md](../README.md) and [recomplauncher.json](../recomplauncher.json).

## Current priority

Make Remastered sustain **3840 × 2160 at 60 fps with measurable headroom**, including under light load, alongside visual correctness work. Prove 60 Hz first.

Start at the current stopping point in [handoff.md](../handoff.md). Keep active tasks, settings and evidence there. Reconcile dated notes with current code, tools and owner decisions.

**Established baseline.** The full DOS loop and Phase 1 translation are established. Keep their parity gates as regression checks. Phase 2 closure and Phase 3 acceptance remain open. R49 is owner-confirmed; R50 is replay-verified. The 25 Nov combined gate failed only the scanner: the 2.263 G HUD tape, the 14.1 G flash (opt-in off in the scan) and an unclassified 6.6 G Vietnam horizon shift.

**Order of work.** Measure the late flight alone and visible with the full fast path, including `F117_PREHUD=1` (try_fastpath menus 4-5). If it misses, cut the traced cost in the native renderer's order (gpu-compositor.md, 18 Nov): the 2D layer once per step, scene vertex buffers blended on the GPU, then native far scenery (copy as oracle). Run the full suite only once the late flight holds. Between timing runs: the scanner failures (bisect 6.6 G against 6295ab4), flash depth proof and live cost, live takeoff and bank captures, a second combined gate, and the copy-thread A/B. Owner to choose: far fade (R47/R51), start-up hold. Then picture review.

## Whole-project scope

Use the [roadmap](roadmap.md) and topic records for the full inventory. Use `py tools/progress.py` for current scores and `--stale` to revisit affected estimates. A rounded score never closes a missing proof. Record measured changes with the evidence date.

- **P1.** Keep the full DOS game loop, timing profiles, input, saves, devices and audio testable. Prove translated instructions against the interpreter and whole-game behavior against the independent references in the parity audit.
- **P2.** Track all-code naming, gameplay route coverage, and second-read accounting outside routes as separate obligations. Validate census inputs and base every claimed match on the required original-routine lockstep, instruction coverage, caught mutations and applicable route hashes. Fixture-only work earns no route credit. Keep declined bodies, unresolved static ranges, machine-option error paths and probable-dead code open until proved. Use the approved original-by-design rules or strict dead/data certificates where applicable. Include encoded code and the separate packed-stub review. Check observer collisions, indirect entries, dispatch tables, callbacks, interrupts, overlays and self-modifying code. Use [progress.md](progress.md), [static-code-review.md](static-code-review.md), the census and the reviewed reference table for exact closure rules.
- **P3 gameplay and presentation.** Keep switchable fixes, recorded cadence and fix sets, save warnings, and fixes-off parity. Preserve recommended real-time play and explicit opt-in for QUIZ. Keep Presentation and Fixes independent. Preserve Original VGA scanout and N=1 redraw equality on corresponding game frames. Verify Remastered framing, depth, decals, extended scenery, coastlines, interpolation, HUD and MFDs, and CPU fallback. Use CPU and compute output as diagnostic references. Remastered acceptance allows rasterization differences that preserve the intended picture and motion.
- **P3 host and distribution.** Cover settings, SETUP input, controller/HOTAS and remapping, key release across overlays, replays, screenshots, CRT and accessibility. Verify audio from guest call to mixed output across applicable DOS drivers and hybrid paths, including front end and debrief. Track owner picture and listening reviews separately. Keep the [Phase 3 backlog](p3-handoff.md) in scope, including couch startup/exit, portable data, rotating logs, INI documentation, optional asset import and planned Mac visuals and lighting. Verify clean and affected upgrade, retry or portable install paths with isolated data for supported Steam/GOG installs. Test launcher changes through RecompLauncher.
- **P4.** Record human play and listening checks, independent references and physical hardware coverage. Label proxy measurements, missing hardware and open owner judgments.

## What counts as done

**Picture correctness.** Read the original routines and trace their output into the compositor. Reproduce the defect at recorded clocks with known settings and build. Compare against the original and the verified Remastered reference appropriate to the view. Check affected draw order, clipping, depth, palette and timing, including before/during/after short-lived effects and occlusion in both directions. Use distinct presented frames, scanner checks and live pacing where relevant. Require identical machine hashes for presentation-only work and document intentional picture differences.

**4K/60 acceptance.** Run the full fast path in a visible 3840 × 2160 window with vsync enabled and `F117_HIDDEN_WINDOW` unset. Use the seven-flight suite in the current handoff, including takeoff, demanding late flight, long and busy flights, and both theatre routes. Run four quiet runs per flight across two times of day and two under `private/partb/load.sh light`. Reject quiet runs with average background CPU above 3%. Time runs alone with a frozen build and exact settings recorded. For each run, report the first five seconds separately and report each billion-clock stretch afterward. Require zero intervals over 16.7 ms after warm-up and zero over 33 ms including warm-up. Report quiet and loaded results separately. Record CPU/GPU work and frame-time percentiles alongside delivered intervals to establish headroom. Use a separate higher-refresh or throughput run to measure capacity beyond the vsynced rate.

**Regression gate.** Run focused checks and the applicable full gates against the candidate build. Check machine parity, picture guards and scanner evidence, and [host acceptance](host-acceptance.md) through the real app. For threading changes, also check copy isolation, repeatability and crashes. If the target proves unreachable, report failing routes, clocks, counts, traced bottlenecks and measured options.

## How to work

- Read the code, capture a baseline, and change one cause at a time. Exercise normal input, including SETUP answers, in end-to-end checks. Diagnose slow frames with `F117_SLOW_FRAME_TRACE` and phase timings. Prefer removing measured work to moving cost or latency between frames.
- Parallelize independent correctness checks and run timing checks alone. Before each build, replay, gate, or wait, look for reusable evidence, safe seek or unthrottled execution, and independent work that can overlap. Keep required coverage intact and verify that shortcuts preserve the result.
- Build from PowerShell with `cmd.exe /c ".\build.cmd"`. Keep captures on the scratch drive and use fresh output directories and isolated save folders. Run long jobs in the background with a watcher. Check for leftover processes and stop only the PIDs identified as part of the run.
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
