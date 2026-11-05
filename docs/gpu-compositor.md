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

Toolchain: SDL 3.2.10 when this plan was written (3.4.8 since 5 November; both vendored through FetchContent) has the GPU API with D3D12 and Vulkan backends; the Windows SDK's `dxc` compiles HLSL to
DXIL and SPIR-V. Compiled shaders are build outputs, not committed.

Open risk (resolved, 4 to 5 November: SDL 3.4's `SDL_CreateGPURenderer` and a wrapped GPU texture let the app keep its renderer and still present without a readback, "Piece 3" below): the SDL renderer the app uses today and an SDL GPU device do not share a window in SDL 3.2, so stage 5 moves the app's whole
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
observer pointer in `src/matched/observe.c` made per-thread (not done; the owner approved editing `observe.c` for this on 4 November, so the only thing missing is the work).

## Goal of 4 November, late: presentation independent of the emulated code (plan and first findings)

Goal (owner): 4K holds 60+ fps with headroom (p95 under 12 ms at 3840 x 2160, Near, Far and Maximum, two runs each, hidden GPU-renderer window; 1080p reported too) by (1) capturing the scene once per 15 Hz
game step, (2) interpolating it on the render side, (3) presenting the GPU compositor's output without a readback, (4) running the emulator on its own thread; each piece checked against the copy-based renderer.

**Measured before starting (4K, runway replay, `--present interp`: no per-frame copy; the frame is the live game's own step, composed with no interpolation).** CPU compositor 31.0 ms p95; GPU compositor 21.0 ms
at Near and at Maximum. So taking the copy away alone does not reach 12 ms: the main thread still spends about 11 ms a frame applying the picture, which is the GPU compositor's synchronous round trip (CPU setup and
upload 1.8 ms, compute plus readback 7.7 ms, copy into the picture buffer 1.2 ms; the compute itself is about 3 to 5 ms) plus 4 ms waiting for the worker. The readback is the biggest single item, so **piece 3 (no
readback) is the enabling piece and goes first**; pieces 1 and 2 (no copy) follow, then 4.

**Piece 3 feasibility.** The pinned SDL (3.2.10) has no way to put its 2D renderer on a GPU device we own. SDL 3.4 does: `SDL_CreateGPURenderer(device, window)`, `SDL_GetGPURendererDevice`, and an `SDL_Texture` can wrap
an `SDL_GPUTexture` we draw into (`SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER`; checked in the 3.4.8 header, release tags 3.4.4 to 3.4.20 exist). Plan: upgrade SDL to 3.4.x on a branch, create the app's renderer through
`SDL_CreateGPURenderer` on the compositor's device when `F117_GPU_COMPOSE` is set, have the replace pass write a storage texture instead of a buffer, and wrap that texture so the menus, overlays and CRT filter
keep working unchanged on the same renderer. Fallback if that fails: present the compositor's texture through our own SDL_GPU swapchain and draw the menus into it.

## Piece 1 and 2 feasibility: how well consecutive logic steps pair (4 November, night)

`F117_STEP_PAIR=1` (diagnostic, live instance) compares each completed 15 Hz step's primitive list (`remaster_frame`, camera space, what the live collector already records) with the previous step's. Runway
replay (20261103-154444, to clock 4.4 billion, 761 steps): **98.7% of primitives pair by key** (model, kind, vertex count and colour word, greedy and in order) and 94% pair by plain list index; steps hold 170 to 250
primitives. So the per-step capture already exists (the live collector's frame), and in-between frames can be built by blending paired primitives' camera-space vertices, with the roughly 1% unpaired (objects entering or
leaving) taken from whichever step has them. Still to establish: the banked and latest flights (turns, other objects), how blending the vertices compares with the copy-based picture at t between 0 and 1,
and the camera and light state the compositor needs beside the vertices.

Confirmed on the other two flights: the banked flight (20261103-235538, 2,481 steps) 98.1% by key and 92.3% by index; the owner's latest flight (20261104-134053, 1,561 steps) 98.7% by key and 95.5% by index. Steps there
hold 65 to 290 primitives; the unpaired share is highest when a step gains objects (65 to 286 in one case). Pairing by key therefore holds across takeoff, banking and approach.

## Piece 2: how faithful is a render-side blend? (4 November, night)

`F117_BLEND_CHECK=1` (diagnostic, app; `remaster_blend_report` in `remaster_live.c`): for each presented in-between frame the game's copy is run at t = 0 and t = 1 and at the presented t; the primitives of the
first two are paired (same model, kind, vertex count and colour word; for each the unused candidate whose first vertex is nearest on screen) and blended in camera space; the blended vertices are compared, as
screen positions (256 x / z, in game pixels), with the third copy's. Runway replay, Near, 2,821 frames, about 4.2 million vertices in front of the camera:

| Error against the copy's own frame at t | Share of vertices |
|---|---|
| under 0.25 game pixel | 91.2% |
| 0.25 to 0.5 | 2.2% |
| 0.5 to 1 | 1.8% |
| 1 to 2 | 1.5% |
| 2 to 4 | 1.2% |
| 4 or more | 2.0% |

99.3% of a frame's primitives pair in all three copies. Vertices more than four near-plane distances out give the same figures. The 4-or-more errors are 1.1% of vertices at t under 0.15 (where the blend is almost
the first step, so this is a floor from wrong pairings among identical primitives), 2.2% from 0.15 to 0.85, and 1.9% above 0.85; they are spread over many models in proportion to their vertex counts (model 37 3%,
30 2%, 26 3%, 51 2%, 11 6%, 39 1%), not concentrated in the lagging lights and lines. Pairing by nearest first vertex instead of first-unused cut the 4-or-more share from 3.7% to 2.0%.

Reading: a plain camera-space blend reproduces about 93% of the copy's vertices to within half a game pixel (1.5 fine pixels at scale 3, 4.5 at scale 9) and fails grossly on about 2%, which in a
frame of 500 primitives is several wrong primitives. That is not yet a picture the owner would accept as equal to the copy's; what would raise it: pairing by the game's object identity (the request and face ids the collector
already records) rather than appearance, blending rotation as rotation where an object turns between steps, and drawing unpaired primitives from the nearer step. The picture-space comparison against the
copy-based frame is not done.

**Picture-space check of the blend, and what the live steps cover** (4 November, night). `F117_BLEND_PICTURE=1` (with `F117_BLEND_CHECK=1`, 1920 x 1080 window): the scene the copy produced at t and the scene built from the blend
of the copy at t = 0 and t = 1 are each drawn by the CPU scene renderer into the wide canvas and compared pixel for pixel. Runway replay, Near, 1,501 frames: the blended scene differs from the copy's in **0.35% of pixels** over
all frames (0.10% by more than 32 levels); a typical frame differs in 2,000 to 4,000 of 768,000 pixels; 35% of frames differ in under 0.1%. Scene only: the sky and ground bands use the copy's own record in both (blending
the band record's angles is not tested), the HUD and cockpit are not part of it, and pairing uses `F117_BLEND_NO_FACE`-switchable face numbers beside model, kind, vertex count, colour word and nearest first vertex (adding the
face number changed the vertex statistics by under 0.1 point).

**Piece 1 is not just the live collector.** The copy at t = 0 must draw step a's picture; compared with the live game's own recorded steps (the live instance's completed frames, newest eight kept under
`F117_BLEND_CHECK`) only about 35 to 40% of the copy's primitives exist in the newest live step vertex for vertex (236 of 649, 244 of 561, 218 of 531, 189 of 527), and no copy frame equals a whole live step. The rest is
what the copy adds that the live game does not draw: the native far scenery (up to about 9,000 survivors placed each copy, most culled or drawn as nothing) and objects in the wider view. So the per-step capture has two
parts: the live step's own objects (recorded once per 15 Hz step by the live collector, as now) and the far scenery and margins, which come from the static world table and the camera and can be placed natively for each
displayed frame at the interpolated camera (they need no machine code: that is `far_scenery` in `rerun.c`, about 4 ms at Maximum on the main thread today, itself a candidate to move to the GPU or to cache per step).

**Where a copy frame's primitives come from** (`F117_SRC_SPLIT=1`, runway replay, 2,881 copy frames). Near: 594 primitives a frame, of which 162 (27%) are placed natively (176 objects) and the other 432 (73%) are drawn by
the game's own code in the copy. Maximum: 1,415 primitives, 979 (69%) native from about 6,960 objects a frame, and 436 drawn by the game's code. Put beside the earlier figure (only 35 to 40% of a copy frame's
primitives also exist in the live game's own step), this fixes the shape of piece 1: the primitives the live collector records (the game's own objects, about 240 of 594 at Near) are not enough, because the copy also asks the
game's own walk for objects in the widened view (the copy sees 30% wider than the live game culls), about 200 more at Near, and those are placed by the game's code. Dropping the copy therefore needs those placed
without it: **the native terrain walk is on the critical path to piece 1**, not a side project, and its remaining difference from the game's walk (about 200 pixels, the ring-1 objects models 37, 39, 40 and 44 short by
about half, not the across, up, metric or near-plane tests) is the thing to close next. The far scenery (the native 27% at Near) is already native and does not depend on it.

## The owner's own window (4 November 2026, 22:10): first real measurement

`--present-log` from the owner's launcher (`Desktop\ONTests\ON-presentlog.bat` and `ON-presentlog-gpu.bat`): Remastered, 1920 x 1080 window, Direct3D 11 renderer, vsync on at 59.99 Hz, scale 3, the ONTests ini
(draw distance Maximum, Terrain dots Original), build at `03c6631`; a free flight of about a minute each (not the runway replay). Logs: `private/present-logs-20261104/`.

| | CPU compositor (default) | GPU compositor (`F117_GPU_COMPOSE=1`) |
|---|---|---|
| Flight segment | 73.6 s, 4,410 frames | 65.5 s, 3,931 frames |
| Presented rate | 60.0 fps (vsync-locked) | 60.0 fps |
| Work per frame (`cost_ns`) p50 / p95 / p99 / max | 7.55 / **12.96** / 15.60 / 28.7 ms | 7.15 / **13.29** / 16.38 / 42.2 ms |
| Interval between presented frames p95 / p99 / max | 16.70 / 16.75 / 33.3 ms | 16.70 / 16.87 / 42.2 ms |
| Frames over 20 ms apart | 0.3% | 0.0% |
| Frames over 33 ms apart | 0.00% | 0.03% |

Reading: at 1080p Maximum the owner's machine holds the 60 Hz display with essentially no dropped frames, but the work per frame has a p95 of about 13 ms, so the goal's 12 ms p95 (80 fps headroom) is missed by about
1 ms; the GPU compositor does not help at 1080p (13.29 against 12.96), as the hidden-window runs said. The hidden-window proxies were close (12.9, 14.0 and 14.1 ms medians of p95 on the runway replay). The 4K figures are
still hidden-window only.

## Piece 3 (4 November, night; built on the experiment branch `copy-overlap`, merged into `master` on 5 November as `e79203e`): present without a readback

SDL is built at 3.4.8 (on `master` since the merge; the earlier pinned 3.2.10 cannot put its renderer on a GPU device we own). `F117_GPU_COMPOSE=1 F117_GPU_PRESENT=1`: the window's renderer is `SDL_CreateGPURenderer(NULL, window)`, the
compositor runs on that renderer's own device (`gpu_compose_create_on`), a new compute pass (`src/present/gpu/present_tex.hlsl`) writes the finished picture into one of two RGBA textures, and the app wraps
both as renderer textures (`SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER`, `SDL_PIXELFORMAT_RGBA32`) and draws the one just written: no readback, no CPU copy, no upload. Double-buffered so the worker's next write cannot
land in the texture the main thread is sampling. The CRT and colour-blind filters, frame hashes and `F117_PRESENT_SHOTS` read the pixels, so they switch on a readback into the pixel buffer too (mode 2); the Tab-key
screenshot does not yet (it would read stale pixels in this mode). A frame the GPU compositor declines still goes through the CPU compositor and the old streaming texture.

Checks: `F117_GPU_PRESENT_CHECK=1` with `F117_GPU_COMPOSE_CHECK=1` compares the wrapped texture with the compositor's own readback: 0 of 768,000 pixels differ per picture. (Compared against the app's pixel
buffer instead it showed about 1,000 differing pixels, because the CPU compositor on another thread also writes that buffer; the texture itself is exact.) The write and download path was also checked with a position
pattern: 0 mismatches at 1280 x 600.

**Measurement note.** With vsync on, the GPU renderer blocks for the next swapchain image inside its draw calls, which the work-per-frame column counts, so every figure sits at 16.7 ms or above. `F117_NO_VSYNC=1`
turns vsync off for benchmarks. Unpaced, the loop presents more frames per game step and the repeats are cheap, so frame-time percentiles flatter it; frames per second over the same replay window (3.9 to 4.8
billion clocks, 15 game seconds) is the comparable number. Runway replay, hidden window, `F117_GPU_COMPOSE=1`:

| Output / distance | Path | Frames in the window | Rate |
|---|---|---|---|
| 3840 x 2160 Near | readback (SDL 3.4.8, D3D11 renderer) | 958 | about 64 fps |
| 3840 x 2160 Near | GPU present, no readback | 1,149 | about 77 fps |
| 3840 x 2160 Maximum | GPU present | 776 | about 52 fps |
| 1920 x 1080 Maximum | GPU present | 1,671 | about 111 fps |

Piece 3 on its own moves 4K Near from about 64 to about 77 fps; it is the enabler, not the finish. What remains on the critical path is the per-frame copy and the main thread's machine step (pieces 1, 2 and 4).

## Two-stage plan, stage 1 so far (4 to 5 November 2026)

Goal in force at the end of the session (owner, 4 Nov 23:13; the full text is in handoff.md): make the Remastered renderer independent of resolution and refresh rate, native 4K at 144 fps and 1080p well
past 120 on a system like the owner's (Ryzen 9 7950X, RTX), game logic untouched. Principle: the CPU decides what to draw once per game step; the GPU does all pixel work every displayed frame.
Stage 1 (4K 60, the per-frame copy kept): merge the GPU present path, make the GPU compositor bit-exact with the CPU one, move the remaining per-pixel CPU work to the GPU. Stage 2 (decouple from the
display rate): capture the scene once per game step, the GPU draws it from the interpolated camera every frame, retire the per-frame copy. Stage 2's in-between frames are held to: static-scenery vertices within half a game pixel
of the per-frame copy, at most 0.5% of pixels differing on average and 2% in any frame, a screenshot review by the owner at the first working version, and the owner judging the motion smooth.

**Done (committed and pushed)**

- Step 1, the merge: the `copy-overlap` branch is on master (`e79203e`): SDL 3.4.8 (`release-3.4.8` in CMakeLists.txt, so a fresh build fetches it), `F117_GPU_PRESENT=1` (the window is an SDL GPU renderer and
  our texture is wrapped, no readback), `F117_NO_VSYNC=1` (benchmarks only), `F117_GPU_COMPOSE=1`. Both GPU switches default off. Guards run on the merged build: 901 of 901 composed-image checksums identical
  to the pre-merge build at Near, Far and Maximum (fixed cadence); the machine's final state hash after the replay, `600306c77cb04481`, identical between the pre-threading build and the merged one at Near and
  Maximum (it is the same at every distance: the renderer does not touch game logic); the 42 host, remaster and presentation tests pass.
- The far scenery's choosing runs on several threads (`85ec934`): `far_choose_row` in rerun.c, one row of cells per job, each job writing its own list, the lists joined in row order, so the pick list and the sort of
  it are the same as the single loop's. The pool is the copy's own (`g_cpar` in main.c, `F117_CHOOSE_THREADS`, default 4, 1 turns it off), apart from the composite's helpers. 901 of 901 checksums identical at Near,
  Far and Maximum. `F117_FAR_WHY`, `F117_CULL_WHY` and `F117_NATIVE_FORCE_BASE` keep the choosing on one thread.
- Far object order setting (`5c7e030`): Video settings row `pick-order`, `--pick-order exact|fast`, default Exact (qsort of the picks; qsort's order for objects tied on distance is the library's, so Exact means
  "what every comparison so far was made against", not "the game's"). Fast is the radix sort. It saves about 0.8 ms of the mean at 1080p Maximum; the p95 moves more run to run than that.

**Measured (runway replay 3.9 to 4.8 billion, hidden window, work per frame `cost_ns`, p95 ms, two runs each)**

| | 1080p CPU, before threading | 1080p CPU, threaded choosing | 4K CPU path | 4K GPU path (`F117_GPU_COMPOSE=1 F117_GPU_PRESENT=1 F117_NO_VSYNC=1`) |
|---|---|---|---|---|
| Near | 9.7, 8.3 | 7.9, 9.5 | 32.0, 31.9 | 14.6, 14.5 |
| Far | 8.9, 10.7 | 9.8, 9.1 | 31.8, 32.0 | 14.9, 15.2 |
| Maximum | 13.3, 9.5 | 11.2, 10.2 | 40.4, 33.5 | 17.1, 15.3 |

The 4K GPU path meets stage 1's 16.7 ms line in five of six runs (Maximum run 1 is 17.1); it does not yet meet its other condition, checksums identical to the CPU compositor (the GPU differs in a bounded number
of pixels by float rounding: step 2). The owner's own window (free flight, 1080p Maximum, before the threading) measured p95 12.96 ms (CPU) and 13.29 (GPU), 60.0 fps locked: see above.

**Utilization, 4K Maximum on the GPU path (typeperf during one replay):** CPU 4.7% of the whole chip (about 1.5 of 32 threads busy); GPU 3D engine 0.5% mean, copy engine 4.4% mean, no compute-engine
column seen (the 3D engine counter alone would miss compute work, so treat the GPU figure as "close to idle"). The frame is a chain of waits, not compute: `F117_LOOP_TIMING=1 F117_REMASTER_TIMING=1` shows the main
thread per frame waiting for the worker about 6.0 to 6.4 ms and then running the copy about 4.8 to 5.6 ms. The worker (the game's picture layer redrawn at 4K, the replace pass, the GPU begin and finish)
takes longer than the copy, so **the worker is the critical path at 4K**, not the copy.

**Next, in this order**

1. Instrument the worker at 4K by stage (picture-layer redraw, replace pass, GPU begin, GPU finish, in ms each). That says exactly what step 3 must move onto the GPU and what makes Maximum reliable under 16.7.
2. Step 2: a bit-exact GPU compositor (integer edge equations and integer depth on both sides, the CPU compositor as oracle, fixed-cadence checksums identical at Near, Far and Maximum).
3. Step 3: move the per-pixel CPU work that remains at 4K onto the GPU. Then stage 1's pass line, and a `--present-log` from the owner's own 4K window.
4. Stage 2 (the capture per game step). Overlapping the emulator with the copy and threading the far scenery's placing (about 2.5 ms at Maximum; `remaster_live_native` patches the machine's template area and uses a
   scene reader with caches, so each thread would need private copies) only matter for the per-frame copy that stage 2 retires: do them only if the worker measurement (item 1) says the CPU chain still holds 4K back.

Guards for every step: picture checksums (`F117_REPLAY_FIXED_CADENCE=1`, `F117_PRESENT_HASH_FROM=3900000000`, eight workers) identical to the baseline, and the end-of-run machine hash. The scripts are in the repository,
[tools/renderer_bench/](../tools/renderer_bench/README.md): `bench.sh NAME DISTANCE [NAME=value ...]` (timing; `MODE=gpu`, `WIN`, `PSCALE`, `EXE`, `XARGS`) and `guards.sh checksums TAG` / `baseline` / `statehash`. The recorded flight, the ini and the baseline
(taken from the build at `03c6631`) are in `private/renderer-bench/`, which is git-ignored.
