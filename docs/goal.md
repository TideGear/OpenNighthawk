# OpenNighthawk project goal

**Limit this file to 9,000 characters.** Check the UTF-8 character count after edits with `py -c "from pathlib import Path; print(len(Path('docs/goal.md').read_text(encoding='utf-8')))"`. Trim before adding. Keep changing detail in the linked records.

Preserve DOS F-117A Nighthawk Stealth Fighter 2.0 in a lockstep-verified Windows recompilation and decompilation. Keep fixes-off behavior faithful and supported source installs read-only. Ship source-only setup. Keep game data, decoded assets, generated translations, ROMs, and private captures local under the [provenance rules](provenance.md). Follow [README.md](../README.md) and [recomplauncher.json](../recomplauncher.json).

## Current priority

Start at the stopping point in [handoff.md](../handoff.md), which keeps tasks, settings and evidence; reconcile dated notes with code.

**1. Owner-visible bugs and missed effects first.** Fix open rows in [owner-issues.md](owner-issues.md) before performance work, adding no per-picture cost 4K/60 cannot carry; prove each by "Picture correctness". Order: (a) R58, the missed-effects audit: confirm Remastered shows the original's colour 0Dh cycling, census palette and registers (`F117_VGA_CENSUS=1`) on the other suite flights, and list the original's hardware writes statically; (b) R57, screen shake on a hit: find what sets it (MGRAPHIC 019C:1251), script a flight that triggers it, add it; (c) the flat-overlay audit: every flat-drawn item (weapon box, tracers, puffs, blast sprites, dots) placed per picture at fine precision, opt-in, shown to the owner beside the original. The precise box draws once at 4K but sits off the near missile after the hit: explain that before its clip. (d) R55 (far scenery lights through a bank), R52, R51, R53. Opt-in until the owner's look: `F117_PREHUD=auto`, `F117_FAR_AIR_FINE`, `F117_WIDE_BOX_PRECISE`, `F117_STAGE2_HIT_PROJECTILES`. Review clips are headless.

**2. Then Remastered at 3840 × 2160 and 60 fps with plenty of headroom**, under light load too. At the missile hit (long flight, 14.1 G) the default shows 52-54 pictures a second: threads half busy, GPU span (submit to fence) is 15-18 ms vs 4-5 ms of GPU work: find why (vsync off, hit and steady flight). CPU options only after that ([gpu-compositor.md](gpu-compositor.md)). `F117_STEPA`, `F117_GPU_HUD` stay opt-in. Target, late flight and through the hit: every thread and the GPU span under about 10 ms, under 0.5% of frames over 20.9 ms, 60 pictures a second.

**Baseline.** The DOS loop and Phase 1 translation are established; their parity gates are regression checks.

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

- Read the code, capture a baseline, and change one cause at a time. Exercise normal input, including SETUP answers, in end-to-end checks. Diagnose slow frames with `F117_SLOW_FRAME_TRACE` and `F117_WORKER_TIMING`. Prefer removing measured work to moving cost or latency between frames.
- Do not waste wall time. Iterate on hidden runs skipped to the moment, run independent checks side by side, overlap builds with long jobs and reuse evidence. Run timing alone; take the owner's screen only for visible timing that counts, and say so first.
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
