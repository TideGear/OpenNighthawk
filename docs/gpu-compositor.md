# GPU compositor for Remastered (plan, 4 November 2026)

Goal (owner, 4 Nov 2026): 4K at 60 fps with plenty of headroom, and the runway replay's p95 under 12 ms at Near, Far and Maximum.

## Why the GPU, and why not more native scene building first

Measured with the GPU renderer in a hidden window (`F117_HIDDEN_WINDOW=1`, docs/remaster-performance.md "GPU-renderer baseline"):
at 3840 x 2160 the frame is about 35 to 40 ms p95, of which the worker's CPU drawing is 21 ms (raster 15.3 ms on eight threads:
bands 5.5, lines 6.5, polygons 2.0, clear 1.5; the single-threaded replace loop 4.7 ms). Upload and present are about 1 ms together.
The copy (the game's own frame composition, resolution-independent) is 3.8 to 5.4 ms. So at 4K the cost is CPU pixel work; replacing
more of the game's scene building would not touch it.

## Design: a per-pixel compute rasterizer over the same primitive list

The CPU compositor (`remaster_raster.c`, `remaster_live.c`) draws the frame's primitive list (`remaster_frame`, camera space, in the
game's order) into colour, inverse depth, palette index and owner buffers, then replaces the 3D view's pixels of the original picture
by origin tag. The GPU version keeps that list and those rules:

- One compute thread per output pixel walks the frame's primitives in order (binned per screen tile so a thread only visits primitives
  that can cover its tile). Per pixel the order of operations is the CPU's, so painter order, ties ("the later polygon wins within a
  hundredth of a percent") and the AND / OR / stipple palette logic come out the same.
- Rules that read only the pixel itself (polygon depth and owner tests, `fill_triangle`) port directly.
- Rules that read neighbouring pixels (`decal_hidden` through `decal_stroke_hidden`, `point_behind_own_surface`, `surface_slope`) are
  exact only if what they read is fixed while they run. They read the owner and inverse-depth buffers; with lines drawn after the polygons
  and polygon depth read-only (`line_order`), a two-pass GPU version (polygons, then lines reading the polygon pass's buffers) gives the
  same answers. **Verified (4 Nov):** the default decal path draws strokes in a pass of their own after every polygon with `line_order = 1`
  (`remaster_live.c`, the `t->line_strong` block); with `line_order` set `fill_triangle` writes neither `inv_z` nor `owner`, and the stroke
  buffers (`stroke_z`, `stroke_aux_x/y`, `stroke_owner`) are read and written only at the pixel itself. So neighbour reads in the stroke pass see
  only the finished polygon pass, which a two-pass GPU version reproduces.
- The replace loop (origin tags against the original picture) is per pixel and ports directly.
- The result stays a GPU texture and is drawn to the swapchain; no 4K readback or upload.

Precision: the CPU uses doubles for barycentrics and floats for depth. The shader must use the same formulas; where a GPU cannot match
bit for bit, differences are reported, not hidden.

## Verification (the CPU compositor is the oracle)

A check mode renders a frame on both and compares every pixel of colour, index and owner (`F117_GPU_COMPOSE_CHECK`, like
`F117_WORKER_COMPOSE_CHECK`). Each stage below is accepted only with zero differing pixels on the three recorded flights used for the native
options (runway 20261103-154444, banked 20261103-235538, latest 20261104-134053), or with every difference listed and explained to the owner.
Game logic is untouched throughout: the GPU only reads the primitive list the copy already produces.

## Stages

1. Headless harness: an SDL GPU device with no window, the frame's primitives uploaded, a compute pass, readback, compare with the CPU raster.
   **Toolchain done (4 Nov):** `tools/gpu_probe.c` and `src/present/gpu/probe.hlsl` (CTest `gpu_compositor_probe`): dxc to DXIL, a D3D12 device with no
   window (SDL 3.2 needs `SDL_INIT_VIDEO` for that), a 3840 x 2160 compute pass, readback: 0 of 8,294,400 values differ; upload, dispatch and
   readback of 33 MB each way 5.0 ms.
2. Clear and the sky/ground bands (5.5 ms of the 15.3).
3. Polygons: depth, owner and the palette logic.
4. Lines and dots: the decal rules, after the item above is verified.
5. The replace loop, then drawing the result to the swapchain in the app (SDL GPU swapchain; menus and the 2D layers as textures).
6. Timing at 1080p and 4K, Near, Far and Maximum, then the owner's own window.

Toolchain: SDL 3.2.10 (vendored through FetchContent) has the GPU API with D3D12 and Vulkan backends; the Windows SDK's `dxc` compiles HLSL to
DXIL and SPIR-V. Compiled shaders are build outputs, not committed.

Open risk: the SDL renderer the app uses today and an SDL GPU device do not share a window in SDL 3.2, so stage 5 moves the app's whole
presentation to the GPU API (the menus, overlays and CRT filter included), or upgrades SDL. That is the largest single piece.

## Results so far (4 November 2026)

**Stage 1a, the trace** (`F117_GPU_DUMP=FILE,FROM` with `F117_REMASTER_THREADS=1`; `tools/gpu_replay.c`): every raster call of a composite is recorded (`src/present/gpu_trace.h`) together with the
two writes the composite makes between calls (the depth reset after the bands, and the black outside the game's clip window). Replaying the trace through the CPU rasterizer reproduces the
recorded colour, palette-index and owner buffers exactly (0 differing pixels, 604 calls).

**Stage 2, the clear and the sky/ground bands** (`tools/gpu_stage.c`, shaders in `src/present/gpu/`; frames: runway, banked flight (rolled horizon), the owner's latest flight at 1280 x 600, and a
runway frame at 3840 x 1800):
- *Exact form* (`bands.hlsl`, double precision with `-Gis` and `precise`, one thread per row because the CPU steps its weights along the row, one compute pass per triangle): **0 differing pixels in
  colour, index, depth and owner on all four frames.** But 47 ms at 1080p and 170 ms at 4K (compute only; an RTX 3060 runs doubles at a small fraction of float speed): about 30 times slower than
  the CPU's 5.5 ms on eight threads, so exactness by doubles is not the performance path.
- *Fast form* (`bands_fast.hlsl`, single precision, one thread per pixel, one dispatch, triangles walked in the trace's order with the CPU's depth test): 1, 0, 10 and 25 of 768,000, 768,000,
  768,000 and 6,912,000 pixels differ in colour (at most 0.0013%); all but 3 by one level; the 3 large ones (up to 101 levels, at 4K) are pixels on a shared edge between two bands, where float
  rounding picks the neighbour. 0.7 to 1.7 ms at 1080p, 3.2 ms at 4K, each with a readback amortized over 20 runs, so compute is a little less.

**What this means for the remaining stages.** A GPU compositor fast enough to matter has to be float, per pixel, with the CPU compositor as a tolerance oracle: it will not be bit-identical
(edge pixels, one-level colour rounding). The acceptance rule I propose, for the owner's agreement: each stage's differing pixels are counted and bounded (here at most 0.002%), every difference
is either one level or on a primitive's edge, and the frames are shown side by side. Stages 3 to 5 (polygons with the owner and depth rules, strokes, the origin-tag replace) are not built.

**Stage 3, the polygons** (`tools/gpu_polys.c`, `src/present/gpu/polys.hlsl`): the solid fills and the AND / OR / stipple fills of the record sequence between the bands and the first line,
run from the state the bands leave. The CPU clips each polygon to the near plane, projects it, splits it into triangles and bins the triangles into 16 x 16 tiles in the trace's order, with
each triangle's two weights evaluated in double at the tile's first pixel (a huge ground polygon whose vertices are far off the screen would lose its precision in float otherwise); one GPU thread per
pixel walks its tile's list in order carrying the pixel's colour, index, depth and owner in registers, with the CPU's depth, owner (ground over, stand-on) and fill rules. Against the CPU rasterizer on the
recorded frames: 1280 x 600 runway, 39 of 768,000 pixels differ (0.005%, largest channel difference 26); banked flight 0; the owner's latest flight 3; 3840 x 1800 runway 292 of 6,912,000 (0.004%). Every
difference is a pixel on a polygon's edge, where float rounding decides which neighbour covers it (the index, owner and coverage differences are the same pixels). Compute time, 20 runs in one
submission with one upload and readback: 0.18 ms at 1080p and 1.15 ms at 4K (the CPU's polygon stage is about 2 ms on eight workers at 4K).

**What a Maximum frame holds.** The recorder (`F117_GPU_DUMP=FILE,FROM[,MIN_PRIMITIVES]`) shows the largest composite in the runway window has about 510 primitives at Near and about 610 at Maximum with
`--dot-distance maximum` (and 509 at the default Terrain dots setting, which removes the dark dots of far objects before they reach the composite), nowhere near the 9,300 far objects the copy places:
most place nothing the renderer keeps. So the compositor's CPU cost is pixel work (huge ground polygons, the bands, strokes over them), not primitive count, which is the case a per-pixel GPU pass suits.

**Stage 4, the lines and dots** (`tools/gpu_strokes.c`, `src/present/gpu/strokes.hlsl`): each stroke is the CPU's two triangles under the decal rules (hidden where the polygon pass has something clearly nearer:
`decal_hidden`, `decal_stroke_hidden`) and the per-pixel stroke competition (a point against an edge of its own object). With `line_order` set the polygon pass's depth and owner are read-only during
the stroke pass, so the GPU runs a per-stroke kernel first (a point behind its own surface is dropped: `point_behind_own_surface`, which reads the finished polygon buffers) and then a per-pixel pass over the
strokes binned into 16 x 16 tiles in the trace's order, the pixel's colour, index and stroke state held in registers. Against the CPU on the recorded frames: 1280 x 600 runway 9 of 768,000 pixels
differ (0.0012%), banked 0, latest 0, runway at Maximum 12 (0.0016%), 3840 x 1800 35 of 6,912,000 (0.0005%); the differing pixels are stroke edge pixels (the largest channel difference is 219: a stroke
pixel the CPU draws and the GPU does not, or the reverse). Compute time, 20 runs, one upload and readback: 0.15 ms at 1080p, 1.24 ms at 4K (the CPU's line stage is about 6.5 ms on eight workers at 4K).

Stages so far at 4K (3840 x 1800), GPU compute against the CPU compositor's worker maxima: bands about 3 ms (fast form; the CPU 5.5), polygons 1.2 ms (2.0), strokes 1.2 ms (6.5), a clear that is
trivial (1.5): roughly 5.5 ms against 15.3 ms, with the replace loop (4.7 ms single-threaded on the CPU) and the presentation still to move.

**Stage 5, the replace step and the wide picture** (`tools/gpu_replace.c`, `src/present/gpu/replace.hlsl`; the recorder appends the step's inputs and output to a wide trace: the game's picture, its origin
tags, the palette, the window, and the CPU's final picture). In the default (decal) lines the whole-line layer equals the composite's colour (`memcpy(line_strong, color)` when no whole lines are drawn), so
`remaster_live.c`'s replace step is one integer choice per pixel: the composite's colour where the origin tag is a scene or outline tag and the composite drew something there (or the scene is complete, or
it is an outline or span tag), the game's own palette colour elsewhere in the window, and the composite in the margins. It is the CPU's own logic over the CPU's own buffers, and it is **exact**: 0 of
768,000 pixels differ at 1280 x 600 (0.11 ms) and 0 of 6,912,000 at 3840 x 1800 (0.99 ms with the readback amortized over 20 runs). The nearest-palette-index image the CPU also writes (`dst`, for "anything
that wants one", 4.7 ms of the CPU's replace step is mostly its search) is not produced; it is not needed to show the wide picture, but whatever still reads it in wide mode has to be found
before the CPU path is dropped. The bright-line variant (`F117_LINES=old`, whole lines taken beside an original line) is not ported.

Stages 2 to 5 together, GPU compute at 3840 x 1800 (each with its readback amortized): bands about 3 ms, polygons 1.2, strokes 1.2, replace 1.0: about 6.5 ms against the CPU compositor's 15.3 ms of
raster plus 4.7 ms of replace. Not yet done: the stages chained on one frame in one submission, the CPU-side setup (clip, project, bin) measured at 4K, and, above all, showing the result without a
readback, which means moving the app's presentation from SDL's renderer to its GPU API.

**The stages chained** (`src/present/gpu_compose.c/.h`, checked by `tools/gpu_chain.c`): one module takes a composite's raster-call records and the replace step's inputs and returns the wide
picture: the CPU classifies the records (bands, polygons, then lines; anything else, or lines without the decal rules, makes it decline so the CPU compositor is used), clips, projects and bins the
polygons and strokes, uploads everything in one transfer, runs the four shader passes (bands, polygons, strokes in two kernels, replace) in one command buffer and reads the picture back. Against
the CPU's recorded wide picture: 1280 x 600, 28 of 768,000 pixels differ (0.0036%, largest channel difference 68); 3840 x 1800, 156 of 6,912,000 (0.0023%, largest 91); every difference on an edge.
Run synchronously, one frame end to end takes 1.8 ms at 1080p (CPU setup and upload 0.26, GPU compute plus readback and copy 1.5) and 9.2 ms at 4K (setup and upload 1.1, GPU compute plus readback
and copy 7.0; the readback and copy of 27 MB are about 2.5 ms of that, and can overlap the next frame). For comparison the CPU compositor at 4K is about 15 ms of raster and 4.7 ms of replace
on eight workers. Not yet in the app: the raster functions' hook has to be able to skip the CPU's drawing so the same calls are only recorded.

## In the app (4 November 2026)

`F117_GPU_COMPOSE=1` (opt-in) creates the compositor beside the app's own renderer (a device of its own, no window; the shaders are copied beside the executable by the build). In the wide Remastered
picture `remaster_live_compose` runs the same scene drawing with the raster functions' per-thread hook set to record the calls and skip the CPU's fill (the hook returns non-zero), hands the records and the replace
step's inputs to `gpu_compose_run`, and takes the wide picture it returns; any frame it declines (a band after a polygon, lines without the decal rules, no device) goes through the CPU compositor as before.
`F117_GPU_COMPOSE_CHECK=1` also runs the CPU compositor and prints how many pixels differ, every 60th picture: on the runway replay 28 of 768,000 (0.0036%) every time. 42 presentation and host tests pass.

Timing, GPU renderer in a hidden window, runway replay, work per frame p95 ms (CPU compositor against GPU compositor):

| Output | Draw distance | CPU compositor | GPU compositor |
|---|---|---|---|
| 1920 x 1080 | Maximum | 12.2 to 14.9 | 15.4 (two runs 15.4 and 15.5) |
| 3840 x 2160 | Near | 41.3 | 26.5 |
| 3840 x 2160 | Maximum | about 45 (earlier runs) | 30.5 |

So the GPU compositor helps at 4K (about 15 ms of 41) and does not at 1080p, where the CPU compositor was already cheap and the synchronous GPU round trip adds to the worker. In the app at 4K the GPU run is
1.8 ms of CPU setup and upload, 7.7 ms of compute plus readback and 1.2 ms of copy into the picture buffer, after the recording of the calls (1.4 ms; it was 5.3 ms until the CPU's depth-buffer zeroing, which
the GPU does not need, was skipped when recording). The worker still does the Enhanced picture on the CPU first and then the composite, and the main thread waits for it: 4K is now 26 to 30 ms, not 60 fps.
Next: overlap the GPU's raster passes with the Enhanced picture (they do not need it), take the readback off the critical path, and use the GPU only where it wins (large canvases).

**Overlap with the game's own picture** (4 November, evening). The present worker used to make the Enhanced (game) picture on the CPU and then compose. The GPU's bands, polygons and strokes do not need that
picture, so `gpu_compose` is now in two halves (`gpu_compose_begin`: record, upload and run those passes, no wait; `gpu_compose_finish`: upload the game's picture, tags and palette, run the replace step, read back),
and the worker calls `remaster_live_gpu_begin` for a render-copy picture before `drawlive_render` (`F117_GPU_NO_OVERLAP=1` turns it off). Recording also skips the CPU's whole-buffer copies and clears. Check
mode against the CPU compositor is unchanged by it (28 differing pixels per 1080p picture, about 160 per 4K one). Work-per-frame p95, GPU renderer, hidden window, runway replay:

| Output | Draw distance | CPU compositor | GPU, no overlap | GPU, overlapped |
|---|---|---|---|---|
| 3840 x 2160 | Near | 41.3 ms | 24.6 ms | 18.7 ms |
| 3840 x 2160 | Maximum | about 45 ms | 30.5 ms | 21.7 ms |
| 1920 x 1080 | Near | | | 9.3 ms |
| 1920 x 1080 | Maximum | 12.2 to 14.9 ms | 15.4 ms | 13.1 ms (one run) |

4K is now about 46 to 53 fps at Near and about 46 fps at Maximum on this machine and a hidden window, with no vsync pacing and no owner-window measurement. 1080p Maximum is still around the 12 ms line.

**1080p Maximum, three interleaved runs each** (hidden GPU-renderer window, runway replay, work-per-frame p95 ms): CPU compositor 10.9, 13.7, 12.9 (median 12.9); GPU compositor with the overlap 12.9, 14.7, 14.0
(median 14.0); GPU compositor with `F117_PICK_RADIX=1` 10.7, 14.2, 14.1 (median 14.1). At 1080p the GPU compositor is no better than the CPU one and the radix sort's earlier gain (median 11.3 against 14.1 in
CPU-compositor runs) does not show here; the spread between runs of one build is about 3 ms, so these samples cannot separate the effects. The 12 ms line at 1080p Maximum is not reliably held by any of the three.
The floor at 1080p is the main thread's render copy (about 4 to 7 ms) plus the live machine step (about 5 ms on slow frames), which the compositor work does not touch; running the copy beside the step needs the
observer pointer in `src/matched/observe.c` made per-thread (not done: outside this phase's scope without the owner's say-so).
