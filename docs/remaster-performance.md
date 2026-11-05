# Remastered throughput

## Current measurement: balanced workers (2 November 2026)

Remastered is not yet proven to meet 60 fps at native 4K detail. The older
Enhanced/rerun 4K timing results do not establish that requirement for the
Remastered compositor. Its performance and fast-turn validation remain open.

Large Remastered rasters now use eight workers on machines with at least
16 logical CPU cores, sharing 64-row groups across the picture. Smaller rasters
and machines retain four workers. The busy horizon no longer falls mostly in
one worker's contiguous band. Every stage still completes everywhere before
the next begins; depth, geometry, shading and detail are unchanged. Helpers
are created only for the selected count, and only for Remastered.

Two repeated runs per configuration, in four/eight/eight/four order, on the
Ryzen 9 7950X (16 cores, 32 logical CPUs), with the same airborne recording,
window, scale and software output:

| Scale-9 3840 x 2160 window | FPS, run 1 / run 2 | p95 interval, run 1 / run 2 |
|---|---:|---:|
| Four workers, contiguous row bands | 13.36 / 13.13 | 80.72 / 82.05 ms |
| Eight workers, 64-row groups | 18.00 / 17.83 | 58.91 / 60.68 ms |

The mean throughput increased about 35% in this test; this is evidence for
this machine and scene, not a hardware-wide guarantee. Compositor wall time
fell from about 39 ms to 22 ms. Eight workers without smaller row groups
managed about 15.2 fps; 128-row groups about 16.6 fps. These alternatives were
measured rather than assuming more workers alone would solve the imbalance.

Evidence: `D:/f117-gate/remaster-workers-20261102/final/report.json`, with
`measure-final.py` for reproduction. It pins build SHA-256
`012D6A61D1FEF5D43F63CE31C3D327F65A760AE82DE8332C811651A389591BD5`,
the recording, commands and diagnostic environment. The protocol below still
applies: isolated sequential runs, startup excluded and no picture capture.

All 167 scale-9 moving pictures across airborne, hangar, water and 4:3 scenes
matched the single-worker render pixel for pixel. These comparisons include
the wide output, centre, palette indices and coverage mask for the same frame;
evidence is `moving/report.json` in that directory. An eight-worker moving
comparison in wide and 4:3 windows is now mandatory in the host acceptance
suite; thirteen ROM-free verdict tests include wrong-worker, missing-frame,
changed-pixel and frozen-motion failures. These checks do not establish GOG
fidelity or owner approval.

The expanded ten-case host suite passes in 125.8 seconds at two jobs;
`D:/f117-gate/host-acceptance-coast-scope-20261102/report.json` verifies against
the current binary and checker/manifest. Its worker case compares another
42 wide and 42 4:3 frames with zero differences. Thirteen checker tests,
seven presentation-check tests and eleven focused native CTests pass.
ONTests was not replaced, and no new full machine gate was run.

The old stage counters had workers writing shared floating-point accumulators.
Workers now own their counters, combined only after stage barriers, and no
stage clocks/counters are updated when timing is off. The report labels stage
values as worker maxima; they are not additive wall-clock phases. The loop
diagnostic separately measures image build, conversion/upload and SDL present.
With eight workers, scale-9 composition still costs about 22 ms, image build
about 27 ms and software SDL present about 19 ms per picture. Both CPU work
and output cost remain above the final budget.

Diagnostics: `F117_REMASTER_THREADS=1..8` chooses a worker count;
`F117_REMASTER_ROW_TILE=0` restores contiguous bands, or 16..1024 chooses
row-group height. `F117_REMASTER_TIMING=1` records component timings;
`F117_LOOP_TIMING=1` records the app loop. Keep these diagnostic overrides out
of normal player presets. None substitutes for physical display pacing.

## Earlier baseline: unused upload removal (2 November 2026)

A recorded airborne segment (`session-20261101-235935`, clocks 2.06 to
2.60 billion, 60 million clocks/second) was measured in isolation, one app
process at a time, using dummy SDL video/audio and the software renderer.
Four compositor threads were enabled. Seek/startup and the last second were
excluded; no screenshot capture or pixel comparison ran during measurement.
The output is a host throughput proxy, not physical-display pacing evidence.

| Window / picture detail | Before | After unused 4:3 upload removal | After p50 / p95 frame interval |
|---|---:|---:|---:|
| 1920 x 1080 / scale 4 | 48.77 fps | 53.13 fps | 17.94 / 22.83 ms |
| 3840 x 2160 / scale 9 | 12.26 fps | 12.86 fps | 75.34 / 88.06 ms |

These are single paired runs, not a statistically established speedup.
At scale 9 the internal wide canvas is 3840 x 1800, displayed at 3840 x 2160
to account for the game's pixel shape. Scale 4 is 1706 x 800 internally;
calling a window "1080p" does not mean its internal raster is 1920 x 1080.
Both windows kept approximately real-time simulation during the measured
interval, but many fewer pictures were delivered at scale 9. Distinct display
plans were counted, not image hashes; this is not proof of visual smoothness.

The optimization removes conversion and upload of the separate 4:3 texture
when a complete wide composite is already available. The composed wide pixels
are unchanged; the 4:3 conversion remains the fallback. Raster rules, detail,
simulation and frame timing policy are unchanged.

Four exact-clock before/after comparisons (wide hangar, wide transport,
airborne scale 9, and the 4:3 fallback) are byte-identical; evidence is
`pictures/report.json` beneath the measurement directory. All nine host
acceptance cases also pass for the rebuilt candidate, with a verified report
at `D:/f117-gate/host-acceptance-wide-upload-20261102/report.json`. Eleven
focused native CTests, twelve host-verdict tests and seven presentation-check
tests pass. No new full machine gate or physical display test was run.

Evidence and reproduction script: `D:/f117-gate/remaster-throughput-20261102/`.
`measure.py` and `report.json` describe the baseline; `measure-after.py` and
`after/report.json` describe the candidate. Each report pins its executable and
recording SHA-256 and records the full command. Private save copies are used.
The script selects its analysis interval from `--present-log`, not total boot
wall time. These private recordings and raw captures are not distributed.

The scale-9 compositor alone took approximately 39 ms in both runs, versus
about 8 ms at scale 4. The remaining time includes the finer original redraw,
composition outside that timer, texture operations and software SDL output.
The reported per-stage timer sums are not independent wall-clock timings:
workers accumulate them concurrently. Use the overall compositor timer before
attributing the gap to a specific stage.

## Next work and acceptance

Separate CPU scene/composite cost from texture/display cost, then optimize the
measured bottleneck with the same detail and scene. Repeat paired measurements
before claiming a stable improvement. Preserve exact output for changes that
only eliminate work; retain the host regression suite and appropriate parity
checks. Include fast turns and crowded ground scenes, not only takeoff.

Completion needs sustained 60 distinct pictures/second at the required detail
and a physical 4K display pacing check. Lowering detail or labelling the
Enhanced results as Remastered acceptance would not complete this requirement.

## GPU-renderer baseline (4 November 2026)

`F117_HIDDEN_WINDOW=1` (new, diagnostic) creates the window hidden, so the real renderer can be timed without anything on screen. Runway session
20261103-154444, clocks 3.9 to 4.8 billion, ONTests ini via `--config`, `SDL_RENDER_DRIVER=direct3d11`, default native options:

| Output / detail | Draw distance | p95 work per frame | Where it goes (per frame) |
|---|---|---|---|
| 1920 x 1080 / 3 | Maximum | 13.2 ms | the copy 5.4 ms on the main thread; the composite overlaps on the worker |
| 3840 x 2160 / 9 | Near | 40.0 ms | waiting for the worker 21.0 ms: CPU raster 15.3 ms (eight threads: bands 5.5, lines 6.5, polygons 2.0, clear 1.5) and the single-threaded replace loop 4.7 ms; the copy 3.8 ms; upload about 1.1 ms, present about 0.1 ms |
| 3840 x 2160 / 9 | Maximum | 34.1 ms | as Near |

With the GPU renderer the 4K upload and present are no longer the problem (about 1 ms together); the CPU compositor is. 4K at 60 fps with headroom
needs the compositor's pixel work on the GPU. Toolchain present: SDL 3.2.10's GPU API (D3D12, Vulkan) and the Windows SDK's `dxc`.

Clean GPU-renderer runs (hidden direct3d11 window, 1920 x 1080 / scale 3, same replay, no timing switches; the exe was rebuilt with a NULL trace hook
partway through the batch, which costs one pointer test per raster call), p95 (p99) ms, two runs: Near 9.20 (10.73) and 10.21 (11.72); Far 11.88 (14.03)
and 9.76 (11.50); **Maximum 12.12 (14.46) and 13.00 (14.70)**. So with the GPU renderer Maximum misses 12 ms at 1080p, unlike the software-renderer
runs (11.1 to 11.3): the present call and vsync wait shape the tail, and run-to-run spread is about 1 ms. This is the 1080p finish line the GPU
compositor work (docs/gpu-compositor.md) has to move.
