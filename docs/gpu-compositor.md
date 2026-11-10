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
  our texture is wrapped, no readback), `F117_NO_VSYNC=1` (benchmarks only), `F117_GPU_COMPOSE=1`. Both GPU switches defaulted off until 6 November 2026; since then they are on by default in Remastered with a real display (see the last section). Guards run on the merged build: 901 of 901 composed-image checksums identical
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

1. (Done 5 November, see the last section.) Instrument the worker at 4K by stage (picture-layer redraw, replace pass, GPU begin, GPU finish, in ms each). That says exactly what step 3 must move onto the GPU and what makes Maximum reliable under 16.7.
2. Step 2: a bit-exact GPU compositor (integer edge equations and integer depth on both sides, the CPU compositor as oracle, fixed-cadence checksums identical at Near, Far and Maximum).
3. Step 3: move the per-pixel CPU work that remains at 4K onto the GPU. Then stage 1's pass line, and a `--present-log` from the owner's own 4K window.
4. Stage 2 (the capture per game step). Overlapping the emulator with the copy and threading the far scenery's placing (about 2.5 ms at Maximum; `remaster_live_native` patches the machine's template area and uses a
   scene reader with caches, so each thread would need private copies) only matter for the per-frame copy that stage 2 retires: do them only if the worker measurement (item 1) says the CPU chain still holds 4K back.

Guards for every step: picture checksums (`F117_REPLAY_FIXED_CADENCE=1`, `F117_PRESENT_HASH_FROM=3900000000`, eight workers) identical to the baseline, and the end-of-run machine hash. The scripts are in the repository,
[tools/renderer_bench/](../tools/renderer_bench/README.md): `bench.sh NAME DISTANCE [NAME=value ...]` (timing; `MODE=gpu`, `WIN`, `PSCALE`, `EXE`, `XARGS`) and `guards.sh checksums TAG` / `baseline` / `statehash`. The recorded flight, the ini and the baseline
(taken from the build at `03c6631`) are in `private/renderer-bench/`, which is git-ignored.

## Worker stages and the 4K p95 at all three distances (5 November 2026)

Build at `4f6dba1` and later, hidden 3840x2160 GPU-renderer window (`F117_GPU_PRESENT=1 F117_GPU_COMPOSE=1 F117_NO_VSYNC=1`, `MODE=gpu WIN=3840x2160 PSCALE=9`), runway replay, clocks 3.9 to 4.8 billion, no other load on the machine.

| Distance | p95 work per frame, ms (runs) | Mean, ms |
|---|---|---|
| Near | 13.36, 13.28 | 11.8 |
| Far | 13.20, 13.52 | 11.8 to 11.9 |
| Maximum | 15.28, 15.18, 15.08 | 13.5 to 13.6 |

A fourth Maximum run read 16.92 ms (mean 14.84) while downloads and unzipping were running on the same machine; it is a disturbed run and is not counted. The 16.7 ms line holds at all three distances; step 2's checksum condition (identical to the CPU compositor) is not met yet, so stage 1 is not passed.

Worker stages (`F117_WORKER_TIMING`, elapsed time on the worker thread, whole run from boot, so not the same window as the p95 above; a diagnostic run, kept apart from the timing runs):

| Distance | GPU begin | Picture layer (`drawlive_render`) | Compose | Worker total |
|---|---|---|---|---|
| Near | 1.20 mean, 1.70 p95 | 5.36 mean, 6.10 p95 | 1.86 mean, 2.29 p95 | 8.42 mean, 9.29 p95 |
| Far | 1.33, 1.85 | 5.45, 6.52 | 1.94, 2.62 | 8.73, 9.93 |
| Maximum | 1.35, 1.97 | 5.46, 6.27 | 1.99, 3.01 | 8.80, 9.99 |

The picture layer, the game's own picture redrawn at 4K by the CPU, is about 62% of the worker and does not change with the draw distance. It is the target of step 3. Main-thread and GPU time are not yet split out.

## Step 2 design: exact setup once on the CPU, integer evaluation per pixel (5 November 2026)

Measured on one Near composite from the runway replay (`F117_GPU_DUMP`, 1280 x 600, 604 raster calls: 248 polygons, 32 shaded, 16 logic, 306 lines; 602 primitives, 1,812 vertices):

- Camera-space vertices are integers: |X| up to 9.6e8, |Y| up to 7.4e8 (30 bits), Z from 0.97e6 to 2.03e8 in front of the near plane (28 bits).
- The projection is `x = cx + sx * X / Z`, `y = cy + sy * Y / Z`, `iz = inv_depth_scale / Z` (sx = sy = 768 here, inv_depth_scale 1 to 64). Unclipped screen vertices reach +/-74,000 px (128 vertices past +/-16,384), so edges are clipped to a guard band before any fixed-point step.
- Inverse depth spans 6.2e-9 to 1.6e-5; the largest triangle has twice its area 8.3e9 px^2 before clipping.

What differs between the CPU and GPU compositors today is arithmetic done separately on each side: weights in doubles per pixel on the CPU, in float or double per 16 x 16 tile on the GPU, and float depth compared with factors such as 0.9999 and 0.97. The design that removes the cause rather than matching the rounding:

1. **One setup, on the CPU, in exact integer arithmetic**, per primitive: the vertices snapped to a fixed grid (1/256 px, after clipping to the guard band, so the largest edge value is about 1e12 and fits 64 bits), the three edge equations `A*x + B*y + C` as int64, the top-left fill rule, and the depth plane `iz = a*x + b*y + c` (inverse depth is affine in screen space, so one plane per primitive is exact) as fixed-point int64. A 128-bit intermediate (CPU only) covers the plane's coefficients.
2. **Both compositors evaluate those numbers**, not recompute them: per pixel centre an integer multiply-add for each edge and for depth, tested with integer comparisons. The GPU receives the setup values as constants (int64 as two uint32), so it cannot disagree with the CPU by rounding.
3. **Depth rules in integers:** the tie factors become integer ratios on the fixed depth (for example `iz * 10000 < d * 9999` in 64 bits); the decal, stroke and slope rules read the same fixed depth.
4. **The cost to the baselines:** the CPU compositor then rounds differently from today at pixels whose centre lies within about 1/512 px of an edge, and at depth ties; the 901-checksum baselines per distance are regenerated from the integer CPU compositor after the owner has seen the changed pixels. The end-of-run machine hash does not depend on the picture and must stay `600306c77cb04481`.

Possibly a second reason: the Zig-compiled game (a different compiler and C library) differed from the Microsoft-compiled one in 92 of 901 Remastered pictures per distance while the game state matched exactly. The cause is not established: it may be float arithmetic in the CPU compositor, or the far-scenery sort (`qsort` orders objects tied on distance differently in different C libraries, which the Far object order setting already documents as changing a few edge pixels). Integer setup would remove only the first. Find the cause before attributing it (compare the two builds' pictures at one differing frame, for example clock 4,067,016,397, and see whether the changed pixels sit on the far scenery's tied objects).

Order of work: the polygon fill and depth plane first (CPU, behind a switch, counting the changed pixels against today's output), then the rules that read neighbours (decals, strokes), then the GPU side, then the baselines.

### Step 2, first measurement of exact coverage on both sides (5 November 2026, branch `p3-exact-raster`)

`F117_RASTER_EXACT=1` switches the CPU fills (`remaster_raster.c`) and the GPU polygon and stroke passes (`polys_x.hlsl`, `strokes_x.hlsl`, int64 edge values per tile from `gpu_compose.c`) to the integer coverage test; off by default. One Near frame, 1280 x 600, 604 raster calls:

- CPU alone: the integer coverage changes 34 colour pixels (47 index, 5 owner) from today's output (0.0044%).
- `gpu_chain` against the CPU picture: float 74 pixels differ (0.0096%, 43 by more than 8 levels); exact 55 (0.0072%, 23 by more than 8 levels). The CPU replay of each trace reproduces its own recording (0 differences).

The 55 that remain are not polygon coverage. Listed with `GPU_CHAIN_LIST=N`: about 10 are band-edge pixels (the CPU draws the sky and ground bands with the shaded fill, now exact; `bands_fast.hlsl` is still float coverage), about 35 are one-step colour differences along band boundaries (`FF1C1C1C` against `FF242424`: the CPU steps its colour weights along a row in double with an accumulated sum and rounds; the GPU does float), and about 10 are lights and dots (`FFFFFFB6` on the GPU against the ground colour on the CPU: the stroke and decal rules, float on both sides but computed differently). So bit-exactness needs three more pieces: exact coverage in the band shader, a colour and depth interpolation defined in integers (the plane form in the design above, not an accumulated double), and the stroke and decal rules on the same fixed depth.

### Step 2, bands exact, then the whole-box scan (5 November 2026, branch `p3-exact-raster`)

`src/present/exact.h` now holds the integer definitions both compositors use: the snapped triangle and its edge test, and four integer planes per triangle (inverse depth scaled by 2^52, three colour channels by 2^32: a base value at an origin pixel and a step per pixel in x and y, evaluated in wrapping unsigned 64-bit arithmetic, which is exact whenever the true value fits). The shaded fill that draws the sky and ground bands takes its depth and colour from the planes on the CPU, and `bands_x.hlsl` evaluates the same integers on the GPU (64 words a band triangle).

The first exact build still disagreed at isolated pixels where the CPU left black and the GPU drew a band colour. Cause: the CPU fills visited only the rows and columns `row_span`'s doubles gave, so a triangle tip that snapping moved onto a pixel centre was skipped. In exact mode the CPU now scans the whole bounding box and the integer test decides every pixel.

Fixed-cadence Near frame (`F117_REPLAY_FIXED_CADENCE=1`, clock 4.0 billion, 1280 x 600, one recording per mode), `gpu_chain` against the CPU picture: float 71 pixels differ (40 by more than 8 levels), exact 39 (9 by more than 8 levels); the CPU replay of each trace reproduces its own recording. What is left is in two groups: about 30 pixels where two overlapping polygons tie on depth (`1C1C1C` against `242424`: the polygon passes still take inverse depth from float weights, double on the CPU and float on the GPU) and about 9 lights and dots (the stroke and decal rules). Next: the inverse-depth plane for the polygon and stroke triangles (the same `ex_planes` data, one more block of words per triangle), then the stroke rules on that depth.

## Stage 1, the 4K split into main-thread, worker and GPU time (5 November 2026)

Build at `2c7b837` plus the GPU-wait columns; hidden 3840 x 2160 GPU-renderer window (`F117_GPU_PRESENT=1 F117_GPU_COMPOSE=1 F117_NO_VSYNC=1`), runway replay, clocks 3.9 to 4.8 billion, `F117_LOOP_TIMING=1` and `F117_WORKER_TIMING` on. The diagnostics add work (p95 16.1 to 18.8 ms against 13.2 to 15.3 in the clean timing runs), so read the proportions, not the absolute values. Milliseconds a frame:

| | Near | Far | Maximum |
|---|---|---|---|
| Main thread: waiting for the worker | 6.3 to 6.8 | 6.3 to 6.7 | 6.2 to 6.6 |
| Main thread: the copy | 3.6 to 4.7 | 3.7 to 4.5 | 5.5 to 6.3 |
| Main thread: collect, apply, publish | under 0.02 | under 0.02 | under 0.02 |
| Worker total, mean / p95 | 9.3 / 11.2 | 9.5 / 11.3 | 9.6 / 11.5 |
| Worker: GPU begin | 1.4 / 2.1 | 1.6 / 2.4 | 1.6 / 2.5 |
| Worker: the game's picture layer | 5.6 / 7.5 | 5.5 / 7.4 | 5.6 / 7.5 |
| Worker: compose | 2.3 / 3.6 | 2.4 / 4.0 | 2.4 / 4.0 |
| GPU: scene span, submission to fence | 7.6 / 9.3 | 7.7 / 9.2 | 7.8 / 9.5 |
| GPU: worker blocked on the fence | 1.3 / 2.8 | 1.5 / 3.1 | 1.5 / 3.2 |

The main thread waits about as long for the worker as it spends on the copy, at every distance; the worker's picture layer (the CPU redraw of the game's own picture at 4K) is steady at about 5.6 ms and is its largest part. The GPU is busy about 7.6 to 7.8 ms a scene, mostly overlapping the picture layer, and blocks the worker for 1.3 to 1.5 ms on average. Maximum differs from Near only in the main thread's copy. So step 3 (the picture layer and the replace pass onto the GPU) is what removes the 6.3 ms wait.

Guards with the timing columns on: 901 identical picture checksums at Near, Far and Maximum, and the runway replay's final machine hash `600306c77cb04481` (Near and Maximum).

### Step 2, the depth plane: zero differing pixels on 30 test frames (5 November 2026, branch `p3-exact-raster`)

The polygon, stroke and logic fills now take their inverse depth from the integer depth plane (`exact.h`, `ex_planes`) on the CPU, and the GPU polygon and stroke shaders evaluate the same plane (words 25 to 32 of each triangle, 40 words a triangle). Test: `tools/renderer_bench/exact_frames.sh WORKTREE exact|float TAG`, ten fixed-cadence frames (clock 3.95 billion in steps of 80 million) at each of Near, Far and Maximum, one recording per frame with `F117_REMASTER_THREADS=1`, 1280 x 600 composites, then `gpu_chain` on each with the same mode.

| Mode | GPU against CPU, differing pixels per frame |
|---|---|
| exact | **0 on all 30 frames** |
| float (today) | 15 to 249 per frame (0.002% to 0.032%), up to 230 of them by more than 8 levels |

Controls: the GPU run in float mode on the exact-mode traces differs by 207 to 457 pixels (so the zero is not an artefact of the comparison); the exact CPU compositor replayed on the float traces differs from today's picture by about 180 to 230 colour pixels a frame (the change the owner is to review before any baseline moves).

**Superseded (5 November, late):** the zero results in this section were recorded before the guard-band fix and must be recorded again; see the next section. The 30 frames are also ten distinct frames, not thirty.

Limits to keep in view: (1) the three distances give the same composite on these frames (identical raster-call counts and identical float differences), so the 30 frames are ten distinct frames, not thirty: the far scenery is mostly dropped before the composite, as the earlier Maximum finding said; a Maximum test needs `--dot-distance maximum` to put more primitives in the composite. (2) 1280 x 600 only; 4K composites (3840 x 1800, larger coordinates) are not tested. (3) One GPU (RTX 3060). (4) The whole branch still has to pass the full gate and the all-route presentation check before it merges, and exact mode is off by default.

### Step 2, the 4K test finds an overflow; the guard band (5 November 2026, branch `p3-exact-raster`, `427e429`)

Recorded at 4K (3840 x 1800 composites, `exact_frames.sh` with `WIN=3840x2160 PSCALE=9`), the first three Near frames gave 0, 0 and **4,697** differing pixels, all in seven rows along the horizon. Cause: a stipple polygon at the near plane had a vertex at x = -2,304,193,464 px; with the vertices snapped to 1/256 px the edge products reach about 1e21, past the 9.2e18 of int64. Signed overflow is undefined in C, so the CPU's compiled sign test and the GPU's disagreed. The 1280 x 600 frames happened to hold no such polygon, and the earlier overflow check looked only at band triangles (at most 2.5e5 px).

Fix, shared by both sides in `src/present/exact.h`: every triangle is clipped to a guard band of +/-1,048,576 px (Sutherland-Hodgman, attributes interpolated linearly in screen space, the result clamped onto the band) before it is snapped, so every product stays under about 3e17; the products are computed in unsigned 64-bit (defined wrap); a band vertex outside the band makes the GPU decline the frame. With the fix the same three 4K frames are **0, 0, 0** differing pixels; the control (GPU in float mode on the same frame) differs by 2,491.

Status at the stop: the 1280 x 600 sets (ten Near, ten Maximum with `--dot-distance maximum`, which holds 19,809 raster calls against 577 at Near) must be re-recorded with this build, a 4K Maximum-with-dots set is untested, and the branch has not been through the full gate or the all-route presentation check. Until then the claim is: zero on three 4K Near frames with the final build; unverified elsewhere.
## Step 2 result with the final build, 1280 x 600 (6 November 2026)

Final build (guard-band clip, commit 427e429 code), exact mode, `tools/renderer_bench/exact_frames.sh`, fixed cadence, clocks 3.95 to 4.67 billion in steps of 80 million:

| Set | Frames | GPU versus CPU differing pixels |
|---|---|---|
| Near | 10 | 0 of 768,000 on every frame |
| Maximum with `--dot-distance maximum` | 10 | 0 of 768,000 on every frame |

Sensitivity control (the same traces compared with the GPU in float mode, frames 0, 3 and 9): Near 254, 457, 180 pixels differ; Maximum with dots 262, 459, 189. At 1280 x 600 the Maximum-with-dots composites are almost the same as Near's. Far was not run separately: it gives the same composite as Near in these frames, because the far scenery is dropped before the composite.

## Recording a late frame without replaying the renderer (`F117_SKIP_COMPOSE_BEFORE`)

A trace at clock C normally replays the flight from the start with the renderer drawing every frame, 30 to 60 minutes at 4K. `F117_SKIP_COMPOSE_BEFORE=CLOCK` (test tooling, off unless set, in `src/host/main.c`) makes the host skip the Remastered composite (and the GPU scene begin) until the machine clock reaches CLOCK; the game's own picture is shown meanwhile. The machine and the game's logic are not touched. Used with CLOCK = target minus 300 million, a 4K frame takes about 6 minutes and a 1280 x 600 frame about 2.

Checked against full replays: the recorded trace files (every raster call, the CPU result and the replace inputs) are byte-identical for Near at 1280 x 600 (clocks 3.95 and 4.35 billion) and at 3840 x 2160 (clock 3.95 billion). Never use it for the gate, the lockstep tests, the picture guards, the presentation check or any machine-hash run: those must exercise the renderer on every frame.

## Step 2 result with the final build, 3840 x 2160 (6 November 2026)

Recorded with `F117_SKIP_COMPOSE_BEFORE` (above), exact mode, fixed cadence, clocks 3.95, 4.03 and 4.11 billion, composite 3840 x 1800 (6,912,000 pixels):

| Set | Frame | Exact: GPU versus CPU differing pixels | Float-mode control |
|---|---|---|---|
| Near | 0, 1, 2 | 0, 0, 0 | 2,653 / 2,468 / 2,491 |
| Maximum with `--dot-distance maximum` | 0, 1, 2 | 0, 0, 0 | 2,679 / 2,502 / 2,487 |

Not covered: one GPU (RTX 3060) and one flight (the runway replay); Far not run separately (same composite as Near); the full gate, the presentation check and the 901-checksum guards on this branch are still to do.

## Branch checks (6 November 2026, commit 7450e2c plus tooling)

- **Full gate** (`tools/build_recomp.py --verify-interp-cache`, work folder `D:/f117-dl/gate-exact`): 63 of 63 routes IDENTICAL between the interpreter and the recompiled engine, 845 of 845 unit tests, 0 translated instructions mismatching (both timing profiles), 0 matched-routine mismatches at three seeds, steps 8 and 9 clean.
- **Presentation check** (`tools/presentation_check.py --mode rerun --scales 1 --jobs 16`): all 63 routes print `exact`; a second run with the exit code captured: 63 of 63 `exact`, exit code 0.
- **Exact mode off (the default at the time):** the checksum guards and machine hash first run for this line measured the wrong executable (my mistake: `guards.sh` runs `$F117_REPO/build/f117a.exe`, and `F117_REPO` pointed at the main checkout, whose build was the old float one), so they showed nothing about the branch. Run correctly on the merged build (`build/f117a.exe`, 16:56, 6 November): exact off 901/901 identical to the float baseline at Near, Far and Maximum, machine hash `600306c77cb04481` (section "Exact mode on by default", below).
- Not covered: exact mode on through the gate or the guards (its pictures differ from today's by design until the owner approves the changed pixels); one GPU.

## Step 3 scope, from reading the code (6 November 2026; no code written yet)

Two pieces move, and they differ a great deal in size.

**The replace pass** (`replace_part` in `remaster_live.c`): per pixel, from the game's picture (`src`), its tag buffer (`tag`), the palette (`dac`) and the compositor's buffers (`color`, `inv_z`, `stroke_z`, `line_strong`, `line_tmp_z`), it chooses the composite colour or the game's colour. Its only neighbourhood work is the bright-line test (a 25 x 25 window of tags and palette colours around a pixel) and `nearest_index` (the palette index for `dst`, which the shown picture does not use: `rgb` and `mask` are what is shown). It is data parallel and every input is a buffer the GPU compose already holds or can be given, so it is a compute pass of the same kind as the existing ones; the check is the one used for step 2 (exact equality with the CPU result, here on `wide_out`).

**The picture layer** (`drawlive_render`, `drawfeed.c`, 5.6 ms of the 9.3 ms worker at 4K): it is not one raster but the game's own 2D drawing at the presentation scale, made by `draw_copy` / `draw_rerun` / `draw_list` (the game's fills, clipped spans, scaled RLE sprites, text and fonts, HUD records, bands, and the interpolated in-betweens of `interp_inbetween_ex`), producing `picture_hi` plus `picture_tag`. Porting it means reproducing every primitive the original's resident graphics driver and VGAME's fill selector draw, bit for bit, on the GPU: the existing checks that cover them (`test_present_fills` 2,619 cases, `test_present_driver` 3,953 cases, `test_hud_state`, `test_band_state`, the hires scenes) would each need a GPU counterpart. That is weeks of careful work, not a session, and a mistake shows as a wrong HUD pixel the 901 checksums per distance would catch only at the three test distances.

Plan for the next session, in order: (1) the replace pass on the GPU, exact against the CPU `wide_out` on the 4K frames used for step 2, with the 901 checksums unchanged; that alone removes the replace cost, not the picture layer's; (2) measure what the picture layer costs by primitive class at 4K (fills, sprites, text, HUD, bands, interpolation) to see whether a few classes dominate; (3) port the dominant classes first, each with its component test. The goal's step 3 pass line (p95 under 16.7 ms at 4K) is already met by the current default path at Near and Far and nearly so at Maximum (15.1 to 15.3 ms, 16.9 once with downloads running), so step 3's value is the headroom Stage 2 needs, not the 4K 60 target itself.

## Exact mode on by default (6 November 2026, owner's approval)

The owner reviewed the changed pixels (143 to 263 per 1280 x 600 frame against the float compositor; the largest are edge flips of a runway light and a distant silhouette) and asked for exact mode on. `F117_RASTER_EXACT` now defaults to on in `remaster_raster.c` and `gpu_compose.c`; `F117_RASTER_EXACT=0` restores the double-precision fills. `test_remaster_raster`'s brute-force reference uses the same rule as the fill (the exact.h integer test when exact is on): 0 of 27,648,000 pixels differ in both modes.

- **Correction (6 November, evening).** The first version of this list (machine hash, baselines, "second CPU run") came from `guards.sh` runs that used the main checkout's old float build, not the merged one (`guards.sh` and `bench.sh` run `${EXE:-$F117_REPO/build/f117a.exe}`, and `F117_REPO` pointed at the main checkout). The "regenerated" baseline was byte-identical to the float backup. Re-run with the merged `master` build, `build/f117a.exe` built 16:56 on 6 November (commit `ec2b78e`, plus the `F117_TAB_SHOT_AT` option):
  - **Exact off** (`F117_RASTER_EXACT=0`) against the float baseline (`baseline-float-20261106`): IDENTICAL, 901 of 901 checksums at Near, Far and Maximum.
  - **Baseline regenerated with exact on** (`guards.sh baseline`): it differs from the float baseline at all three distances, as it should.
  - **Exact on, repeat run** against the new baseline: IDENTICAL, 901 of 901 at Near, Far and Maximum.
  - **Machine hash** (`guards.sh statehash`, runway replay, 4.8 billion clocks): `600306c77cb04481` for the old build (the Desktop `ONTests` build) and the new one, at Near and Maximum.
- **GPU compositor live (fixed the same evening: see "The GPU compositor equals the CPU compositor over the whole flight" below).** The first measurement here used the main checkout's old float-mode executable (the same mistake as above), where GPU and CPU are expected to differ by 200 to 450 pixels a frame, so it did not describe exact mode. Run on the merged exact build (`build/f117a.exe`, built 16:56 on 6 November, `MODE=gpu`, `F117_GPU_COMPOSE=1 F117_GPU_COMPOSE_CHECK=1`, Near, fixed cadence, to 4.8 billion clocks, `EXE=` given): **1,735 differing pixels in 3,421 pictures in all** (against 201,755 on the float build), every one of them in the first few hundred pictures of the run and none in the flight. Cause traced offline: a trace at clock 1,290,000,000 (`F117_GPU_DUMP`) gives `gpu_chain` 2 of 768,000 differing pixels, at (180, 207) and (101, 216), CPU `3A3222`, GPU `282828`. Both are the end pixel of the same outline line: `282828` outline lines (records 172 to 174 and 212 to 214 of that trace) of a ground polygon with a vertex behind the near plane, which end one pixel further on the GPU. The line's setup (`add_stroke` in `gpu_compose.c`, `remaster_line` in `remaster_raster.c`) is the same arithmetic on both sides; what differs is the per-pixel decal rule, `decal_stroke_hidden`, which finds the nearest point on the segment in doubles on the CPU and in floats on the GPU and then truncates it to a pixel (`(int)cx`), so a segment end that lies exactly on a pixel boundary can fall in different pixels. Fixed by doing the GPU side in doubles from the same double ends (no CPU change), together with a second cause, tiles skipped at their corner pixel; see the section below.

## GPU composition and presentation on by default (6 November 2026, owner's decision)

The owner's first flight from the setup wizard's install (3840 x 2160, picture detail 9, Remastered, CPU compositor by default) ran at 30 and then 11 pictures a second, with the game slowed to 75 to 89% and the sound stuttering (`--present-log` from the owner's window). The same flight replayed hidden at 3840 x 2160: CPU compositor 18 to 30 pictures a second, then 7 to 8 (122 to 148 ms of work a picture), game speed down to 66%; GPU compositor and presentation 60 a second, then 49 to 54 (16 to 20 ms), game speed 100% throughout.

`F117_GPU_COMPOSE` and `F117_GPU_PRESENT` are therefore on by default (`gpu_wanted` in `src/host/main.c`) in Remastered with a real display; 0 turns either off. They stay off by default with the dummy video driver or the software renderer, which the checksum guards (`guards.sh`) and headless checks use, so the CPU-compositor baselines are unchanged. `bench.sh MODE=gpu` (a hidden Direct3D 11 window) now measures the GPU path unless `F117_GPU_COMPOSE=0 F117_GPU_PRESENT=0` are given. Without a usable GPU device the CPU compositor is used as before. The live GPU-versus-CPU difference is now two pixels in an early scene (previous bullet in the section above).

Found while checking: with GPU presentation the picture goes to a texture without being read back, so Tab saved the last picture the CPU had made (all six of the owner's captures in flight were the same runway picture). Tab now reads the shown picture back first. Test option `F117_TAB_SHOT_AT=CLOCK,...` takes the Tab capture at those clocks in a replay: on the runway replay at 3840 x 2160 with GPU presentation, three captures at 4.0, 4.3 and 4.6 billion are three different pictures of the flight.

## The GPU compositor equals the CPU compositor over the whole flight (6 November 2026, evening)

Two causes found and fixed, each traced from a recorded frame with `gpu_chain` and confirmed offline before the live runs:

1. **The end pixel of near-clipped outline lines** (2 pixels a picture in an early scene, 1,735 in all). The decal rule's nearest point on the segment (`decal_stroke_hidden` on the CPU, `stroke_hidden` in `strokes_x.hlsl`) was found in doubles on the CPU and in floats from float-rounded ends on the GPU, then truncated to a pixel, so a nearest point on a pixel boundary fell in different pixels. The stroke record now carries the segment's ends as doubles (words 16 to 23, 24 words a stroke) and the shader does the same double arithmetic in the same order, casting to float where the CPU does.
2. **Tiles skipped at their corner pixel** (single pixels at x and y multiples of 16, 19 in all). Triangles were binned into 16 x 16 tiles by a float barycentric test on the tile's corners with a 1e-3 margin; exact snapping moves a vertex by up to 1/512 px, so a snapped triangle could cover a tile's first pixel that the unsnapped float test had excluded (a water logic polygon at (864, 112), clock 3,549,000,000). In exact mode the tile test now uses the same int64 edge equations as the shader (`tile_touches_exact`: an edge's best tile corner on the outside means no pixel of the tile is in).

Results, `build/f117a.exe` built 20:06 on 6 November, runway replay, fixed cadence, `EXE=` given:

| Check | Near | Far | Maximum |
|---|---|---|---|
| Live check (`F117_GPU_COMPOSE_CHECK=1`, `MODE=gpu`, 3,421 pictures) | 0 pixels differ | 0 | 0 |
| GPU guards (`MODE=gpu guards.sh checksums`) against the exact CPU baseline | 901/901 identical | 901/901 | 901/901 |
| CPU guards against the exact CPU baseline | 901/901 identical | 901/901 | 901/901 |
| Machine hash (`guards.sh statehash`, 4.8 billion clocks) | `600306c77cb04481` old and new | | `600306c77cb04481` old and new |

The trace sets of step 2 (1280 x 600 and 4K, Near and Maximum with dots) and the two new traces give 0 differing pixels with this build. Not covered: one GPU (RTX 3060) and one flight.

## Plan for step 3 and Stage 2 (6 November 2026, for the owner's approval; nothing started)

**Correction to the step 3 scope note above:** the replace pass already runs on the GPU (`gpu_replace.dxil`, stage 5; the CPU `replace_part` runs only when the GPU path declines a frame). What remains on the CPU every displayed frame at 4K is the game's picture layer (`drawlive_render`, about 5.6 ms on the worker) and the per-frame copy that walks the scene (3.6 to 6.3 ms on the main thread). The owner's 4K flight with GPU composition ran at 49 to 60 pictures a second (16 to 20 ms of work), so those two are what stand between today and 60 everywhere, and between 60 and 144.

**Step 3, the picture layer on the GPU**
1. Measure first (small): split `drawlive_render`'s 5.6 ms at 4K by what it draws (fills and spans, scaled sprites, text, HUD records, bands, the in-between interpolation) with timers, on the runway replay at Near, Far and Maximum.
2. Port the classes that dominate, one at a time, each against the CPU as the reference: a recorded picture-layer input (like `F117_GPU_DUMP` for the composite) and a tool that compares the GPU's `picture_hi` and `picture_tag` byte for byte with the CPU's; the component tests that cover each class (`test_present_fills`, `test_present_driver`, `test_hud_state`, the hires scenes) get a GPU counterpart before a class is switched on.
3. Pass line: p95 under 16.7 ms at 4K, hidden window, Near, Far and Maximum, two clean runs each; the 901 checksums per distance and the machine hash identical.
Size: the fills alone are a known exact port (the step 2 machinery); sprites, text and HUD are each their own piece. Weeks rather than days if every class is ported.

**Stage 2, the scene captured once per game step**
1. Capture: at each game step the copy runs the game's own walk once, with a view wider than the screen by the camera's possible motion until the next step, and keeps the scene (primitives in that step's camera space plus the camera itself).
2. Draw: every displayed frame the GPU transforms the captured scene by the change from the step's camera to the interpolated camera and composes it, with the exact rules of step 2.
3. Tests: frames on a game step byte-identical to today's per-frame copy (the camera is the same); in-between frames against the per-frame copy with static-scenery vertices within half a game pixel and at most 0.5% of pixels differing on average, 2% in any frame; moving objects (aircraft, missiles) reported separately, first kept on the per-frame path if they disagree; p95 under 6.9 ms at 4K, and 1080p reported, two runs each.
4. Owner gate 2 at the first working version: the worst frames beside the copy's; the per-frame copy retired only after the owner approves and has flown it.
Known limits: per-step decisions (which objects are drawn, darken and detail values) still change only at steps, as today. And 6.9 ms at 4K is not reachable while the picture layer costs 5.6 ms on the CPU, so Stage 2's speed target needs step 3's port, or at least its dominant classes.

**Recommended order:** step 3's measurement (1); then Stage 2 (it removes the larger per-frame cost, the copy, and is where 144 comes from); then port the picture layer's dominant classes until 6.9 ms is met. The owner decides the order and whether to start.

## Check of the Model detail build (6 November 2026, 23:42)

`C:\Dev\F-117A Recomp\build\f117a.exe`, built 22:21 on 6 November (commit `ef43ff9`, Model detail Full by default), run with `EXE=` given, runway replay, fixed cadence, no stray `f117a.exe` before or after.

| Check | Near | Far | Maximum |
|---|---|---|---|
| Live check (`MODE=gpu`, `F117_GPU_COMPOSE=1 F117_GPU_COMPOSE_CHECK=1`, 3,421 pictures) | 0 pixels differ | 0 | 0 |
| GPU guards against the exact CPU baseline | 901/901 identical | 901/901 | 901/901 |
| CPU guards against the exact CPU baseline | 901/901 identical | 901/901 | 901/901 |
| Machine hash (4.8 billion clocks, old Desktop `ONTests` build and this one) | `600306c77cb04481` | | `600306c77cb04481` |

Not covered: one GPU (RTX 3060) and one flight.

## Step 4: the 4K frame times on the current picture (7 November 2026)

`C:\Dev\F-117A Recomp\build\f117a.exe`, built 15:58 on 7 November (Model detail Full by default, the margin widenings on, GPU compose and present on), `EXE=` given. Runway replay at 3840 x 2160 (picture detail 9, hidden Direct3D 11 window, `MODE=gpu`), Maximum draw distance, fixed cadence, clocks 3.9 to 4.8 billion (901 pictures), two runs one after the other with nothing else running and no stray `f117a.exe` before or after; `F117_SLOW_FRAME_TRACE=1 F117_REMASTER_TIMING=1`.

| | Run 1 | Run 2 |
|---|---|---|
| Mean, p50, p95, p99, max (ms per picture, the `cost_ns` column) | 20.98, 22.67, 24.99, 25.55, 26.72 | 20.89, 22.52, 24.84, 25.59, 26.45 |
| Pictures over 16.7 ms | 827 of 901 | 819 of 901 |
| Over 20 ms, over 25 ms | 495, 45 | 495, 29 |

**So 4K 60 is not met at Maximum with Model detail Full: about 47 pictures a second on average, and nine in ten pictures miss the 16.7 ms line.** (The 6 November numbers, p95 13.2 to 15.3 ms, were Original model detail, before the margins were filled: both add work. The widenings alone cost about 1.3 ms of 29 ms at 1080p-class in the 7 November tuning runs; not separated at 4K here.)

**Where the time goes** (the slow-frame trace, every picture in the window is over 12 ms; the same in both runs):

| Picture kind | Count | Total | Waiting for the worker | Scene copy (main thread) | Machine step |
|---|---|---|---|---|---|
| Game-step pictures (the machine runs) | 226 | 21.1 ms | 4.6 | 6.8 | 9.6 |
| In-between pictures | 675 | 20.9 ms | 13.9 | 6.8 | 0.1 |

The main thread does these in series: it waits for the worker's previous picture, then runs the scene copy (6.8 ms, every picture), and on game steps also the machine step (about 9.6 ms, of which 1.6 is the draw-feed collection). The worker (picture layer, compositor setup, GPU upload) is on the critical path at about 14 ms on in-between pictures. No picture is slow because of a one-off hitch: the worst frames (26.7, 26.5, 26.3 ms at clocks 4,750,016,397, 4,710,016,397, 4,757,016,397) are ordinary in-between pictures with a slow worker, and the same clocks come out worst in both runs.

**What this means for step 5.** Making the main thread overlap the copy with the worker's job (so a picture costs about the larger of the two, 14 ms, not their sum, 21 ms) is the cheapest route to 16.7 ms, but the copy reads the machine state the worker's picture is made from, so it needs care; shortening the worker (the picture layer across CPU threads, byte-identical) shortens its wait directly; Stage 2 removes the 6.8 ms copy from every picture.

### Step 4, the worker's split, and what it changes (7 November 2026)

One more run at the same settings (`build/f117a.exe` 15:58, 3840 x 2160, Maximum, Model detail Full, `EXE=` given, nothing else running; 21.29 ms mean over 901 pictures, p95 25.41) with `F117_WORKER_TIMING` on, over the last 900 worker jobs that made a picture:

| Worker stage (ms per job) | mean | p50 | p95 | max |
|---|---|---|---|---|
| GPU begin (the scene's setup and submission, CPU) | 3.32 | 3.19 | 4.62 | 5.07 |
| Picture layer (`drawlive_render`, CPU, runs while the GPU works) | 5.31 | 5.27 | 6.03 | 6.43 |
| Compose (`remaster_apply_ex`), of which blocked on the GPU fence | 6.08 (5.15) | 8.57 (7.64) | 9.94 (9.02) | 11.39 (10.43) |
| **Worker total** | **14.70** | 16.58 | 18.39 | 19.70 |
| GPU span, from the scene's submission to its fence | 11.13 | 13.98 | 15.39 | 16.90 |

**The worker is bound by the GPU, not by the picture layer.** The CPU's share before the fence (begin 3.3 + picture 5.3 = 8.6 ms) is hidden behind a GPU span of about 14 ms, and compose then waits 5 to 8 ms for the fence. So the earlier plan of putting the picture layer across CPU threads would cut a cost that is already overlapped and would not shorten a picture. What decides a 4K Maximum picture is, in order: the GPU's scene time (about 14 ms), then the main thread's serial scene copy (6.8 ms, plus 9.6 ms of machine step on a game step) which cannot overlap the worker today (the copy needs the draw-live step ring, which `drawlive_publish` rewrites only while the worker is idle, and its output buffer is swapped only at the end of a copy).

**Revised plan for step 5** (nothing started; each step keeps the 901 checksums and the machine hash identical):
1. Add GPU timestamp queries around each compute pass of `gpu_compose.c` (polygons, strokes, bands, replace, readback) and find which pass owns the 14 ms at 4K Maximum with Full detail (1,829 primitives a frame instead of 554). Candidate causes to check, not yet measured: tile binning with far more triangles, the per-pixel stroke loop's cost, or too few tiles in flight.
2. Make the heaviest pass cheaper without changing a result (the exact-equality check against the CPU compositor is the test, as in step 2).
3. Overlap the main thread's scene copy with the worker's job: double-buffer the step ring and the copy's output so the worker reads a fixed set while the next copy builds; the bound becomes the larger of copy and worker, about 14 ms before step 2 above and lower after it.
4. Stage 2 (capture once per game step) only if 1 to 3 do not reach 16.7 ms at the 95th percentile. Owner gate at its first working version, as before.

### Step 5, first measurements of the GPU passes (7 November 2026, evening)

New diagnostic `F117_GPU_PASS_TIMES=1` (`gpu_compose.c`, off by default): each compute pass of the begin step is submitted and fenced on its own, so each pass's wall time (GPU work plus one submit and fence) is known; the means print every 120 pictures. On `build/f117a.exe` 17:43, 3840 x 2160, Maximum, Model detail Full, one run alone (the passes do not overlap in this mode, so the run is slower: 28.6 ms a picture): **upload 0.6 ms, bands 7.8 ms, polygons 1.2 ms, strokes (points) 0.1 ms, strokes (pixels) 8.4 to 8.9 ms.** The sky-and-ground "bands" pass and the stroke pixel pass are the two heavy ones; the polygons are cheap.

**A change that did not work (reverted):** I took the bands cost for the shader walking every band triangle's box at every pixel and binned the band triangles per 16 x 16 tile (CPU lists, shader reads its tile's list). The recorded traces stayed at 0 differing pixels (so it was exact), but the bands pass only fell from 7.8 to about 7.0 ms while the CPU binning added about 2.3 ms to the worker's start, and the whole picture got slower (21.3 to 23.7 ms mean at 4K Maximum). So the cost is the per-pixel work of the covering triangles (int64 edge and plane arithmetic for the sky and ground, which cover most of the picture, in painter order), not the list walk. Reverted; the code is as before apart from the diagnostic.

**Where this leaves step 5.** The two heavy passes need a cheaper exact method, not a different loop structure: for the bands, test coverage with fewer 64-bit multiplications (one edge value per pixel stepped incrementally instead of three products per triangle), skip triangles a later one will certainly overwrite at that pixel when the depth rule allows it, or evaluate the colour planes only for the triangle that finally wins; for the strokes' pixel pass, find how many stroke entries each tile holds at Maximum with Full detail (about 1,800 primitives against 554) and whether the 16 x 16 tiles should be smaller or the lists sorted. Each needs the exact-equality check against the CPU compositor (0 of 768,000 differing on the recorded traces, then the live check and the guards). Overlapping the 6.8 ms scene copy with the worker (the earlier plan) remains separate and independent.

### Step 5, a second change that did not help, and what the GPU is doing (7 November 2026, night)

**Per-pixel box test in the stroke pixel pass (exact, not kept).** Each stroke triangle got its pixel box (the one the tile binning already relies on, conservative by construction: a half-pixel margin around the vertices) in its spare words, and the shader skipped a triangle at a pixel outside it before any float or 64-bit arithmetic. Exactness held on the recorded 1280 x 600 traces (0 of 768,000 differing). Paired A/B, built copies alternated twice at 4K Maximum with Model detail Full (`F117_GPU_PASS_TIMES`, one run at a time, nothing else of mine running; another program was using about 29% of the GPU, which is why only back-to-back pairs are comparable): baseline strokes (pixels) 8.80 and 8.79 ms, with the box test 8.77 and 8.83 ms; whole pictures 30.29 and 30.28 against 30.14 and 30.18 ms. **No gain, so it was not committed** (the source is as before). With the earlier band-binning result this shows the two heavy passes are not limited by how many triangles a pixel looks at.

**The GPU is not clocked down and is half idle.** Sampled once a second during a 4K Maximum run: power state P0, 1950 of 2130 MHz, memory 7501 MHz, 73 W, utilization 44 to 53%. So over a 20 ms picture the GPU does about 10 ms of work and waits the rest, while the main thread runs the scene copy (6.8 ms) and, on game steps, the machine step (9.6 ms) in series with the worker's wait. The pass times when each pass is fenced on its own (bands 8.3, strokes pixels 8.8, polygons 1.2 ms) add up to more than the GPU's real busy time in normal running because the passes then also wait for their own fences; they show proportions, not the cost of a normal picture.

**Order for the next session's speed work (changed):** (1) overlap the main thread's scene copy and machine step with the worker and the GPU, so a picture costs about the larger of its CPU and GPU halves, not their sum (double-buffer the step ring, `drawlive_publish` and the copy's output, as described above); the GPU's roughly 10 ms of work then fits under 16.7 ms; (2) only then make a pass cheaper if the GPU becomes the larger half (the bands pass is the candidate: cheaper exact edge tests); (3) Stage 2 if (1) falls short, with its owner gate.

### Step 5, why overlapping the scene copy is a design task, not a small edit (7 November 2026, night)

Read in `main.c` (the build step around `g_pw`) and `drawfeed.c`: each loop iteration does `drawlive_collect`, then waits for the worker's previous job, applies its picture, then `drawlive_publish`, `drawlive_plan_interp`, `drawlive_prepare_rerun` (the scene copy, about 6.8 ms at 4K) and releases the next worker job. The copy cannot simply move before the wait, for three reasons found in the code:
1. **The copy's plan depends on the publish.** `drawlive_plan_interp` picks the older and newer steps from the ring as `drawlive_publish` has just left it (`ia`, `im`, `ib`); moving the publish earlier is what lets the copy run earlier.
2. **`drawlive_publish` releases steps the worker may still be reading.** It calls `release_step` on the oldest step as it advances the ring and replaces `r->picture`, `r->picture_hi`, `r->picture_tag`, `r->picture_seq` and `r->picture_end`, which the worker's `drawlive_render` reads. The comment at the call ("the worker is idle: nothing renders from the old steps") is the contract.
3. **The copy writes the frame the worker reads.** `remaster_live_exec` ends a copy by swapping `r->done` and `r->frame` (and copying the band record, origin and window), so the worker's `remaster_live_gpu_begin` and `remaster_apply_ex` read `r->done` that the next copy's end would swap away.

**A design that keeps every picture byte-identical** (so the 901 checksums and the machine hash must not move): (a) let `drawlive_publish` run before the wait but defer its `release_step` calls and the change of `r->picture*` to after the wait, by recording them in a small pending list that is applied at the same point in the iteration as today; give the ring two spare steps so a step the worker reads is never recycled before its release; (b) give the worker its own copy of the `picture*` fields and step indices in the job (`g_pw.plan` already carries the plan) so the worker no longer reads `r->picture*`; (c) defer the `r->done`/`r->frame` swap of a copy to the point after the wait, running the copy into `r->frame` while the worker reads `r->done`. The order of results is then the same as today's; only when the work happens changes. The tests: the 901 checksums at Near, Far and Maximum on CPU and GPU, the live GPU check, host acceptance (its `renderer-workers` scenario compares the parallel result with a single-worker reference on every picture), and a new stress run with `F117_WORKER_COMPOSE_CHECK`, plus the machine hash. Expected gain at 4K Maximum with Full detail: a picture costs about the larger of its CPU half (copy 6.8 + wait for setup) and its GPU half (about 10 ms busy), roughly 14 ms, under 16.7 ms; the machine step on game steps (9.6 ms) remains in the main thread's path and needs the same treatment or Stage 2.
This is a dedicated piece of work (the publish, the worker's reads and the copy's swap all have to change together and be shown byte-identical), not one to rush at the end of a long session.

**Addendum from reading `drawlive_render` (7 November, night):** the worker's picture layer reads the ring indices and the replay fallback from the live `drawlive` at the moment it runs, not from the plan: `older_ix = plan->sa >= 0 ? plan->sa : (r->smooth ? r->im : r->ia)`, `b = plan->sb >= 0 ? plan->sb : r->ib`, and, for a replayed picture, `r->picture`, `r->picture_hi`, `r->picture_tag`. `drawlive_plan_interp` sets `plan->sa` and `plan->sb` only on the smooth rerun path. So step (b) of the design is concrete: make `drawlive_plan_interp` always fill `sa`, `sb` and copies of the `picture*` pointers into the plan, so the worker needs nothing that `drawlive_publish` changes; then defer `release_step` of the oldest step to after the wait (its slot must stay owned while the worker's plan can name it), which needs the step array to have at least two free slots beyond today's peak use (check `DRAWLIVE_STEPS` against the peak ring occupancy first). The worker also writes `r->shown_hi`, `r->shown_tag`, `r->inter_hi`, `r->inter_tag`, which the main thread reads after the wait; those stay on the main-thread side of the wait.

**Overlap, state after sub-step 1 (7 November, night).** Sub-step 1 (the plan carries the step indices and the replayed-picture pointers, so the worker no longer reads them from the live ring) is done, behaviour-neutral and verified (`30aa327`, `build/f117a.exe` 18:55: CPU and GPU guards 901/901 at Near, Far and Maximum with the widenings off, hash `600306c77cb04481`, host acceptance 12/12). Reading `drawlive_prepare_rerun` and the end of `remaster_live_exec` for the next sub-step found that the copy and the worker share **one more structure besides the step ring and `r->done`**: `run_copy` fills `r->rlist` (the copy's primitive records and overlays) and the worker's `draw_copy` / `draw_rerun` read it, so a copy that starts while the worker runs would overwrite what the worker is drawing from. The sub-steps still to do, each verified like sub-step 1 before the next: (2) double-buffer `r->rlist` (the copy writes one, the worker reads the other, switched at the same point in the iteration as today's hand-over); (3) split the copy's end in `remaster_live_exec` so the `r->done`/`r->frame` swap, the band record, origin and window copies and `done_complete` are done by a new `remaster_live_commit` called after the wait; (4) defer `release_step` in `drawlive_publish` to after the wait and enlarge `DRAWLIVE_POOL` (5 today: three published, two staging) by two; (5) give the job its own `ww` (it is set on `g_pw` after the wait today and read by the worker) and skip the overlap on a frame where the wide width changes (`wide_prepare` may resize `g_wpix` the worker writes); (6) only then move `drawlive_publish`, `drawlive_plan_interp` and `drawlive_prepare_rerun` before the wait. Check each against the 901 checksums on CPU and GPU, the live GPU check, host acceptance and the machine hash, and run `F117_WORKER_COMPOSE_CHECK` over a whole flight for a race.

### Step 5, the copy overlap: built, fast, not yet exact (7 November 2026, night)

**What was built** (all behind the plan's switches, default off; sub-steps 1 to 3 are committed and verified neutral: `30aa327`, `d05f905`, `1e0cb79`): the plan carries the step indices and the replayed-picture pointers; the copy's records are double-buffered (`rlist`, `rlist_job`, swapped by `drawlive_commit_rerun`); a copy stages its finished frame and `remaster_live_commit` publishes it; `drawlive_publish` queues the oldest step's release (`defer_release`, `drawlive_release_pending`, `DRAWLIVE_POOL` 7); the main loop can publish and plan before the wait (`F117_OVERLAP_MODE=1`) and run the copy before the wait too (`F117_OVERLAP_MODE=2`); a thread-local flag (`remaster_live_ignore_palette_override`) keeps the copy from using the worker's palette override.

**Measured, paired and repeated, 4K Maximum, Model detail Full, GPU path (`build/f117a.exe` 20:21, `F117_NO_COPY_OVERLAP=1` against the default of that build):** mean picture time **23.45 and 23.48 ms without the overlap, 17.74 and 17.71 ms with it; p95 28.30 and 28.24 against 22.93 and 23.14 ms.** So the overlap is worth about 5.7 ms a picture, as the design predicted (the picture costs about the larger of its halves).

**Exactness (Near, the guards' replay, three runs at once to stress the timing; baseline = the exact baseline with the widenings off):**

| Setting | Differing checksums against the baseline |
|---|---|
| overlap off (the default now) | 0 of 901 |
| mode 1 (publish and plan before the wait), three runs | 0, 0, 0 |
| mode 2 (copy before the wait), two runs | 894 and 896, and the two runs differ from each other in 334: a race |
| mode 2 with the compose moved to the main thread (`F117_NO_WORKER_COMPOSE=1`), two runs | all 901 differ from the baseline and the two runs are identical to each other: systematic |
| mode 2 with one render thread | all 901 differ |

Controls with the overlap off and the same switch: one thread 0 differ (equal to the baseline), no worker compose 2 differ. The sequential guards run showed the differences confined to the first 232 pictures on the CPU compositor and the first three on the GPU path, so the damage depends on timing, which is what a race does.

**What is established.** Publishing, planning and the deferred release are safe (mode 1 is exact under stress). The copy before the wait is what changes the pictures, deterministically when the compose is on the main thread (so not only a thread race), and with a race on top in the default configuration. The palette override was one real shared global (the copy's colours come from `dac_colour`, which takes the worker's palette whenever it is set); ignoring it on the copy's thread did not make the pictures exact, so something else the copy shares with the previous job's picture layer or compose, or depends on from it, is still unfound.

**What to do next.** Find the remaining shared or order-dependent state, by comparing, in the deterministic configuration (`F117_NO_WORKER_COMPOSE=1`, mode 2 against mode 0), the copy's output (`r->frame` primitives, `rlist` records) for the same picture: the first picture whose copy output differs shows what the previous job changed before the copy ran in the old order. Candidates still unchecked: state `drawlive_render` or `draw_recs` leaves in `rerun`'s scratch or in the step snapshots (`rerun_snap`) that a copy reads; the copy machine's incremental sync (`nudge`, `g_copy_mem`); the native templates and `remaster_scene_dropped` that the copy updates and a compose may read; `r->inter_hi` and `r->inter_tag`, which the worker writes and the next plan reads. Until it is exact the overlap must stay off (it is).

### Step 5, the overlap made exact and on by default (7 November 2026, 22:30)

**Cause of the inexactness found above:** `remaster_live_set_deferred(g_rlive_copy, 1)` ran where the worker thread is created, before `g_rlive_copy` exists (it is created when VGAME loads), so the deferred copy was never on: sub-step 3's neutral verification was vacuous, and in mode 2 the early copy's `remaster_live_copy_begin` cleared the done frame while the previous picture's composite still needed it (that composite then declined, `done_prims 0`, and the picture lost its Remastered 3D view). Found by fingerprinting each copy's output (`F117_COPY_FP`: identical in both orders) and each picture's layer, composite and compose decision (`F117_PIC_FP`: the first differing picture was a composite declined with an empty done frame). Fixed by setting the flag where `g_rlive_copy` is created. The palette-override ignore stays (a real shared global, now harmless).

**After the fix:** with the compose on the main thread, mode 2 equals mode 0 in every picture, composite decision and checksum (0 of 3,476 pictures, two runs); in the default configuration four parallel stress replays (mode 0, mode 2 x3) all equal the exact baseline (0 of 901). **Mode 2 is now the default.**

**Speed (`build/f117a.exe` 21:49, 4K 3840 x 2160, Maximum, Model detail Full, GPU path, paired, two rounds):**

| | Mode 0 (old order) | Mode 2 (overlap) |
|---|---|---|
| Mean | 20.87, 20.95 ms | **16.72, 16.72 ms** |
| p95 / p99 / max | 24.9 / 25.6 / 26.9 ms | **17.9 / 18.6 / 19.5 ms** |
| Frames over 16.7 ms | 803, 835 of 901 | 438, 425 |
| Frames over 20 ms | 496, 494 | **0, 0** |

The median is 16.68 ms in both overlap runs, which looks like 60 Hz pacing (vsync on the hidden window) rather than the true cost; the next measurement should run without it to see how far below 16.7 ms the work really is.

**The worker-compose check (`F117_WORKER_COMPOSE_CHECK`) is not usable as a race test on the GPU path.** On `build/f117a.exe` 21:49, GPU path, Maximum, to clock 4.3 billion, it reports the same result with the overlap off and on (275 composites the same, 2,525 different, the first difference at composite 1; on a full run with the overlap on, a difference of 183,366 wide pixels at composite 3,451, with the main thread's recomputation repeatable). So it disagrees in the old order too: the main thread's recomputation is not the same path as the worker's GPU composite. It is a pre-existing problem with the check, not with the overlap; the overlap's exactness rests on the guards (CPU and GPU 901/901), the stress replays and the live GPU check (0 differing pixels at Near, Far and Maximum). Fixing or retiring this check is a small task for the next session.

### R37 review prototype (8 November 2026, owner comparison pending)

Executable: `C:/Dev/F-117A Recomp/build/f117a.exe`, built 8 November
2026 at 12:20:01 local time. `F117_SQUARE_PEN=1` selects a review-only
upright square swept along each segment in the picture layer and both scene
compositors; it is off by default. Scene compositors share the endpoint-square
convex hull and continue the segment's inverse-depth plane across it.
No new baseline is approved or generated.

Completed sequential verification, explicit `EXE` above:

- Machine state: `guards.sh statehash`,
  `OLD=D:/f117-dl/old-ontests/f117a-20261104-2207.exe`, old and new at
  Near and Maximum all give `600306c77cb04481` (square pen off).
- CPU and GPU guards: 901/901 identical at Near, Far and Maximum,
  `F117_FAR_WIDE_ACROSS=0 F117_FAR_WIDE_UP=100 F117_WIDE_UP_ALLOW=0`,
  square pen off. Counts and checksum contents independently inspected.
- Square pen on, widenings on: live CPU/GPU comparison completed through
  clock 4,800,000,000 at all three distances. Each last cumulative report
  covers 3,421 pictures with zero differing pixels. These diagnostic timings
  include both compositors and are not performance evidence.

Evidence: `private/r37-verify/verification.txt`, produced by
`private/r37-verify/verify.sh` (both untracked). Archived executable startup
initially failed because runtime DLLs were not found; the corrected runs above
supersede those failed attempts. The statehash helper now preserves the supplied
new executable, supplies its runtime DLL search directory using Git Bash path
conversion, and fails explicitly when no final hash is produced.

Host acceptance passed 12/12 with one job and square pen off; its saved report was verified against this executable and the current checkers. Evidence
in `private/r37-host` and `private/r37-verify/host-console.txt`. Owner-flight
before/after shots remain required. The owner's original 3840 x 1800 screenshot
was inspected directly: include the teal heading marker and grey frame joins,
using `session-20261107-223638` (recorded AdLib effects enabled).
Owner-flight captures are running sequentially under `private/r37-verify/owner_pen_0` and `owner_pen_1`. No ONTests replacement, commit or push yet. No threading change in R37.

Owner-flight capture progress: the square-pen-off run of
`session-20261107-223638` completed at clock 15,880,000,000, final machine hash
`d209adb69eb1143b` (this is a different flight/end clock from the standard
`600306c77cb04481` guard). Three PNGs were captured at requested clocks
15.5, 15.7 and 15.85 billion. Actual backing images are 4102 x 1800 with
`WIN=3840x1800 PSCALE=9`; both variants use these identical settings.
The first two images were visually inspected and show the flight and HUD joins.
The square-pen-on paired run has started; its final state must equal the off
run above. The review images and state logs remain in the workspace under
`private/r37-verify/owner_pen_0` and `owner_pen_1`, not the owner's folders.

R37 OWNER GATE: both full owner-flight runs completed at clock
15,880,000,000 with identical final state `d209adb69eb1143b`. Their hidden
GPU output widths differ (4102 versus 4576), so the full-width pictures are
not a valid whole-frame A/B. Exact, unscaled 2880 x 1800 centre crops align:
all three pairs have zero differing pixels below row 900 (the cockpit).
Pixel-preserving HUD close-ups in `private/r37-verify/review/flight2_joins.png`
show the teal marker's tilted tips removed and the grey frame join filled.
Full centre crops and two other comparisons are beside it. Review applies to
these line ends/joins; differing margins are excluded from this comparison.
The owner must approve before the visual default or baseline changes.
Windowless probe off produced 4608 x 1800; no full dummy recapture was started.
Commit/push was rejected by automatic approval review, citing an unverified
remote and pending owner-flight gate; remote readback confirms
`https://github.com/TideGear/OpenNighthawk.git`. Nothing was committed/pushed.

ONTests updated with the 8 November 12:20:01 executable, all build DXIL files,
and changed `f117machine_api.dll` (other runtime DLLs already match by SHA-256).
`EXE=C:/Users/Tideg/Desktop/ONTests/f117a.exe` launched headless with dummy
video/audio, private workspace saves, no config, and exit-after 1,000,000:
explicitly waited process exit 0, no remaining game process. Square pen remains
off by default. Owner approval of R37 and the blocked commit/push is pending.

Owner approved R37's shown line ends/joins and commit/push in chat on
8 November ("i approve"). Square pen is now the default; `F117_SQUARE_PEN=0`
retains the prior pen. Rebuilt successfully and started sequential verification
with explicit executable/build header in `private/r37-approved/verification.txt`.
Candidate baseline is isolated in `private/r37-approved/baseline`, with widenings
off. The previous baseline remains untouched until candidate CPU/GPU guards,
live comparisons, machine hashes and host acceptance have been inspected.

R37 approved-default verification complete: `build/f117a.exe`, 8 November
2026 13:58:29, explicit EXE in `private/r37-approved/verification.txt`.
Old/new Near and Maximum hashes all `600306c77cb04481`; CPU and GPU each
901/901 at Near/Far/Maximum against the candidate square-pen baseline,
widenings off; live CPU/GPU checks 3,421 pictures each, zero differing pixels
at all three distances; host acceptance 12/12, saved report verified against
this executable. All six checksum sets independently inspected, 901 entries
and identical contents. Candidate baseline promoted after owner approval;
previous baseline preserved in `private/r37-approved/pre-r37-baseline`.
ONTests refreshed with this executable, DXIL files and changed DLLs. Explicit
EXE headless launch, dummy audio/video, private saves: exit 0, no stray process.
R37's visual gate is approved; owner's flight of this build remains open.
Next item is R36 sea-band smoothing, with its own owner gate. No release
publication was requested or performed.

### R36 investigation (8 November 2026)

Using `EXE=C:/Dev/F-117A Recomp/build/f117a.exe`, build 13:58:29,
owner flight `session-20261107-223638`, AdLib effects as recorded, Maximum
Full. Source reads: `draw_scene_ex` already shades both sky and ground band
quads. `fill_triangle_logic` replaces RGB with `palette[index AND/OR value]`;
GPU `polys_x.hlsl` does the same. Thus palette logic is a candidate for losing
the gradient, not a missing ground-band smoothing switch.

Existing `F117_FLAT_SHOW=1` diagnostic at clock 15,700,000,000 highlights
level model objects magenta but leaves the stepped sea blue (shot under
`private/r36/flat/shots`, inspected). This rules out the highlighted ordinary
flat model fills as the direct cause; logic fills bypass that highlight.
A single-thread raster trace of the same view is running to identify the sea's
operation. `F117_SKIP_COMPOSE_BEFORE=15698000000` skips early composites;
these runs are diagnostic, not timing evidence. No R36 source or baseline
change yet. All outputs and private saves remain in the workspace.

R36 trace finding: `private/r36/sea.trc` (1536 x 600, 76,306 records),
parsed with the current 360-byte record layout and near-plane clipping.
Sea samples (30,300) and (1500,300) are RGB `FF084971`, palette index `FE`,
owner zero. Record 22,765 is an OR fill, value `80`, whose clipped footprint
covers the sea sample: ground-band `7E` becomes `FE`. Thus the OR fill
replaces the interpolated ground RGB with the flat sea palette entry.
The captured scene has 54 AND `6F` and 15 OR `80` fills. Next: preserve
fractional band shading through the OR palette conversion, identically on
CPU/GPU, review-only until the owner's visual gate. No source change yet.

R36 prototype built: `build/f117a.exe`, 8 November 15:01:48.
`F117_SMOOTH_SEA=1` is review-only, default off. For OR `80` on source band
indices `70..7F`, shared integer RGB projection estimates the position between
the original gradient endpoints and maps it between the corresponding sea
palette endpoints. Palette indices and the fill operation remain unchanged.
Verification runs sequentially from `private/r36-verify/verify.sh`, report
`private/r36-verify/verification.txt`. Owner-flight fixed-dimension captures
are prepared in `private/r36/captures.sh`, not started while verification runs.

Owner requested wrap-up: verification deliberately stopped after CPU all distances and GPU Near/Far each 901/901. GPU Maximum incomplete; switch-on live and host acceptance not run. All four statehash comparisons passed. R36 saved as default-off WIP, not accepted. See newest handoff section; CMake dependency approval remains pending.

### R36 verification complete; waiting at the OWNER GATE (8 November 2026, 16:10)

`EXE=C:/Dev/F-117A Recomp/build/f117a.exe`, build 8 November 15:01:48 (unchanged by the
CMake edit: `band_palette.h` added to the `gpu_polys_x.dxil` DEPENDS, owner-approved
8 Nov 15:30; the rebuild found no work to do). Default stays off (`F117_SMOOTH_SEA=1` opts in).

- Machine hash `600306c77cb04481`, old/new, Near and Maximum.
- CPU guards 901/901 and GPU guards 901/901 at Near, Far and Maximum (smoothing off, widenings off). GPU Maximum, interrupted earlier, now passes.
- Live GPU check with `F117_SMOOTH_SEA=1`: 0 differing pixels at Near, Far and Maximum (`private/r36-verify/rest.out`).
- Host acceptance 12/12 (`private/r36-host`; default-off build, so it shows no regression, not the smoothing).
- Owner flight `session-20261107-223638`, clock 15,700,000,000, 4608 x 1800 (3840 x 1800 view plus margins), Maximum Full: off and on end on the same machine hash `c68fad8ffaae6def`. Shots in `private/r36/sea_0/shots` and `sea_1/shots`; comparison `private/r36/r36_before_after_left_margin.png`.
  On/off differ only in the sea: 0 differing pixels above y=425 (sky/HUD) and 0 in the cockpit centre. A margin column goes from 10 to 32 (left) and 11 to 44 (right) distinct colours, in smooth steps.

OWNER GATE: visual approval. Default and baseline are unchanged; after approval the default flips and `guards.sh baseline` is rewritten.

### R36 approved and enabled by default (8 November 2026, 17:10)

Owner approved the before/after shots ("Smoothing looks good"). `F117_SMOOTH_SEA` is now
default on (unset or non-zero; `0` off) in `remaster_raster.c` and `gpu_compose.c`.
`EXE=C:/Dev/F-117A Recomp/build/f117a.exe`, build 8 November 16:15:46
(`private/r36-approved-verification.txt`): machine hash `600306c77cb04481` old/new, Near
and Maximum; new baseline written (differs from the old one at all three distances, as
expected) and CPU and GPU guards 901/901 against it at Near, Far and Maximum, widenings
off; live GPU check 0 differing pixels at all three; host acceptance 12/12, report verified.
Baseline promoted to `private/renderer-bench/baseline`; the previous one is in
`private/r36-approved/pre-r36-baseline`. ONTests holds this build (exe, all DXIL, changed
`f117machine_api.dll`); headless dummy launch exit 0, no stray process.

CI note: runs on `cdd5c7b`, `97f2a2f` and `383f7b5` failed on `presentation_world_lines`,
whose last check still demanded equal diagonal and horizontal stroke area (true for the round
pen, false for the approved square pen). Fixed in `329c663` (checks both pens); CI green.

### R35 far light size, and R38 slope lines (8 November 2026, 20:15)

**R35.** Setting Far light size (`far-light-size`: `original` default, `perspective`). `remaster_prim.shrink` is set in `finish_model`; `far_width` in `remaster_live.c` scales the width passed to `remaster_line`, so the GPU path takes it from the trace record with no shader change. Reach check (`F117_PRIM_HIST`, lines `[prim-z]`, runway replay, Maximum): game-drawn primitives reach z/reach 1.22 at most (object extent); placed one level coarser 1.15 to 3.9. `EXE=C:/Dev/F-117A Recomp/build/f117a.exe`, build 8 November 19:05:25, `private/r35/verification2.txt`: machine hash `600306c77cb04481` old/new at Near and Maximum; CPU and GPU guards 901/901 at all three distances (default off, baseline unchanged); live GPU check 0 differing at all three with `--far-light-size perspective`; flag equal to the environment switch over 901 checksums and different from the baseline; host acceptance 12/12, report verified. Owner flight shots: `private/r35/far_0`, `far_1`, crops `r35_before_after_horizon*.png`: 98,127 pixels differ, all between rows 377 and 536, machine hash equal.

**R38, method.** Owner flight `session-20261107-223638`, clock 15,700,000,000, 3840 x 1800 scale 9, Maximum Full. `F117_GPU_DUMP` traces classified per pixel by `private/r38/analyze.py` (kind, width, footprint visibility); `patch.py` lists a row's pixels and the draw calls over it. Patch (0,360)-(1150,500): no stipple; 37% strokes, 9 px wide with R35 off.

**R38, spotted teeth: cause confirmed.** Row 435 across object 199820 (faces 2048/2049) shows the 2C2C2C edge stroke (`#76352`, width 9) in columns about 4 px apart outside the face. In `decal_stroke_hidden`, a pixel outside the object takes its verdict from the stroke's nearest centre-line pixel: on the object it returns visible; off it, the search over neighbours hides the pixel if any same-object neighbour's depth differs by more than 1% from the stroke's depth, which it compares at a different pixel. The stroke follows its polygon's own edge, so the centre-line pixel flips on and off the object at each stair-step of that edge (the 4 px period). Test without code: `F117_DECAL_TOL=0.50,0.97` removes the teeth (`private/r38/tol`, 1,736 pixels of the frame change, patch 1,506). It also lets the stroke spread about 20 px onto the neighbouring face, so that is a proof, not a fix. Unaffected by far scenery: the teeth remain at Draw distance Original (`private/r38/dd_orig`). Proposed fix: compare depths at the same place, with the stroke's depth moved to the sampled pixel using `line_depth_dx/dy`, on CPU and GPU together. Pictures change, so owner gate first.

**R38, thick: cause.** A/B at the same moment, against current (Maximum, Full), slope patch (864,395)-(2000,500): Draw distance Original changes 53,674 px and removes most of the grey mass; Model detail Original changes 36,988 px, mainly lights, slopes much the same. With R35 on: 32 strokes remain at 9 px in the first patch (R35 leaves them unchanged: game-drawn, or placed but within reach; the trace does not record which), placed ones from 2.5 to 7.4 px by depth. The original 320 x 200 picture at this clock shows coarse dark streaks of the same one-pixel thickness at its own reach (`private/r38/orig_vs_remaster.png`).

**R38, limits.** The flight is almost all over sea (survey sheet `private/r38/survey_sheet.png`), so it cannot show the slopes closer; that needs an owner flight near them.

### Approved defaults verification in progress (8 November 2026)

Owner approved Perspective far light size and Near terrain dots as defaults. `EXE=C:/Dev/F-117A Recomp/build/f117a.exe`, build 21:27:06. Existing benchmark ini pins neither setting. Old baseline preserved in `private/defaults-approved/pre-defaults-baseline`; candidate baseline under `private/defaults-approved/baseline`, not promoted. All three candidate files contain 901 checksums. Old/new Near and Maximum machine hashes all equal `600306c77cb04481`. CPU and GPU guards each pass 901/901 at Near, Far and Maximum with the required widening overrides. Live Near passes 3,421 pictures, zero differing pixels; live Far/Maximum, host acceptance and full CTest remain in the active verification script. Evidence: `private/defaults-approved/verification.txt`.

The owner's revised rendering priority permits hardware rasterisation and depth approximations while keeping machine state and the full selected object set exact. Visible bands/strokes changes require before/after review before enabling. The current bundled SDL GPU public header exposes no timestamp query API; `gpu_compose_times` measures CPU setup/submission/readback, not GPU pass timestamps. Investigate backend instrumentation or an appropriate profiling path before claiming per-pass GPU costs.
Verification update: live Near, Far and Maximum each complete at 3,421 pictures with zero differing pixels. Host acceptance has started (sound passes); full CTest is queued. No baseline promotion yet.

Defaults verification complete: host acceptance 12/12, report verified; full CTest coverage 846/846 (serial tests 1..150, including func_lockstep 1561.84 s; parallel remaining 151..846 all 696 passed in 104.13 s; unique test IDs verified). Candidate baseline promoted with hash-verified copies; previous retained. ONTests exe 21:27:06, DXIL/changed DLLs refreshed, explicit hidden private-save smoke exit 0, no stray game. Evidence logs: serial-verification.txt, remaining-ctest.txt under private/defaults-approved. Performance investigation remains next.

### New-flight trace classification and diagnostic repair (8 November, build 23:30:55)

Owner replay 211031, trace clock10.368G, software scale3,1536x600,76,727 records (`private/owner-211031/late.trc`). This is a comparable late scene, not an exact clock match to the owner's PNGs: captured HUD targets differ. Existing analyze.py classifies land-edge patch (0,80)-(1536,220): zero stipple pixels;7,874 line pixels;750 dot pixels. Sea patch (0,250)-(250,500):119 dark dot pixels, zero lines/stipple. Thus sea specks are real dots, distinct from broken slope strokes. New scene's exact edge-depth failure not yet traced; do not call every spot the earlier R38 teeth. Evidence late-analysis.txt/sea-analysis.txt and shot in trace/shots.

main.c diagnostic-only candidate: F117_WORKER_COMPOSE_CHECK forces GPU texture+readback so g_wpix is populated before comparison. Previously texture-only rendering could leave its CPU buffer stale, while the second main-thread composite wrote a new result. Overlap0 completes standard901 interval and >=3400 comparisons with0 differ; overlap2 still running, >=800 same0 differ. F117_REMASTER_TIMING additionally prints CPU setup time and current record count from begin. Normal rendering switch-off path unchanged. Full CTest running; no new commit/ONTests update yet. Hardware GPU timestamps remain unresolved: split-pass wall times include fences and are not timestamp queries.
### GPU hardware timestamps (9 November 2026, build 11:42:20)

Optional Direct3D 12 timestamp queries around each compute pass: configure option `F117_GPU_TIMESTAMPS` (default OFF), and at run time only when `F117_GPU_TIMESTAMP_LOG=FILE` is set. Begin and finish markers
per pass, one resolve per command list, read after the existing final fence, disabled after an incomplete or failed frame. `tools/renderer_bench/timestamp_report.py` validates a capture (it rejects a bad
sequence, missing boundaries and non-finite samples; the first capture was invalid because of a marker placed after submission, and the validator caught it). No rendering change.

First clean capture, 1080p, picture detail 3, Maximum, Model detail Full, 902 complete frames, mean GPU interval per pass: upload 0.100 ms, bands 2.485, polygons 0.573, points 0.021, stroke pixels 0.780, picture
upload 0.094, replace 0.126, present 0.081, download 0.001. These are hardware intervals at 1080p, **not 4K figures and not whole-frame costs**; the 4K capture is step 2 of the current goal.

Verified on that build (commit `0c270b1`, CI green, in ONTests): CPU guards and timestamp-enabled GPU guards 901/901 at Near, Far and Maximum; machine hash `600306c77cb04481` old and new at Near and Maximum;
live GPU check 0 differing pixels in 3,421 pictures at each distance, each live timestamp file validating 3,473 complete frames; host acceptance 12/12; CTest 846/846 (the suite started before the marker
correction; the corrected app is covered by the guards, live checks and host acceptance). Logs: `private/timestamp-live`.

### R38, teeth on far slope lines: cause, rule, integration and validation (8 to 9 November 2026)

**What the owner saw.** Dark lines along far mountain slopes broken into a row of teeth (R38 in `owner-issues.md`). Thick lines and the real terrain and sea dots are separate matters and are not touched here.

**Cause.** A model's outline stroke lies along the edge of its own polygon. `decal_stroke_hidden` (CPU, `remaster_raster.c`) and `stroke_hidden` (GPU, `gpu/strokes_x.hlsl`) decide whether a stroke pixel
off the object is hidden by looking at the surface under the stroke's centre line and at same-object neighbours. They compared the stroke's depth at one pixel with a surface depth sampled at another, so on a
grazing slope the pixel was rejected at every stair-step of the edge. One traced case: pixel (346, 437), stroke of face `306EA801`, rejected by the neighbouring face `306EA800`, whose plane was being
extrapolated to a point (347.57, 441.85) that its triangle does not cover.

**What was tried and dropped** (all offline, in `private/r38-same-pixel`): loosening the tolerance (`F117_DECAL_TOL=0.50,0.97`) removes the teeth but spreads strokes onto the next face; moving only the line's
depth with `line_depth_dx/dy` changed 156 pixels and left the teeth; estimating the surface slope from neighbouring pixels of the same face (the first candidate, 1,383 pixels) improved the teeth but left
visible breaks, and **the owner rejected it**; extrapolating the true face plane to the stroke pixel alone made teeth on other edges; dropping depth rejection altogether spreads faces. None of these ships.

**The rule that was kept** (the revised candidate; the owner's verdict on its before and after: "much better"). For a line stroke of a model:

1. Both depths are compared at the same place, the nearest point on the stroke's centre line: the stroke's depth is moved there with its own gradient, and the surface's with the exact depth plane of the face
   that owns the sampled pixel.
2. A face of the same object may hide the stroke only if one of its triangles actually covers that centre-line point; a plane continued outside its own triangle hides nothing.
3. Other objects keep the direct depth test, so real occlusion is unchanged. Points (lights) keep their own rule.

**Code** (commit `2c9c01e`). `src/present/decal_faces.h`: a cache of the frame's triangles as rasterised (clipped to the near plane and guard band, snapped to 1/256 pixel), 26 words each (owner, the six snapped
coordinates, signed area, the depth plane's origin, base and two gradients), chained from a 262,144-entry hash table keyed by face owner; duplicates dropped; size and allocation checks; a failed cache turns the
rule off for that frame. CPU: `remaster_raster.c` records faces in `fill_triangle` and `fill_triangle_shaded` (not logic fills, not strokes) and resets the cache in `remaster_clear`; `remaster_faces_use` selects a
caller-owned cache, and `remaster_live.c` gives each worker part its own, restored after each staged call. GPU: `gpu_compose.c` builds the same words from its own clipped triangle list and appends them to the
stroke buffer; `strokes_x.hlsl` evaluates the same rule. Exact mode only: `F117_RASTER_EXACT=0` keeps the float path untouched. **Off by default; `F117_DECAL_FACE=1` enables it.**

**Offline evidence** (trace replays, raw scenes without the picture layer): the integrated CPU output byte-matches the reviewed candidate on five recorded scenes (two 4608 x 1800 traces of
`session-20261107-223638` at clock 15.7 billion, and three 1536 x 600 traces of `session-20261108-211031` at clocks 6.0, 8.4 and about 10.4 billion), changing 1,812, 1,537, 275, 62 and 154 colour pixels and
no owner values; the integrated GPU compositor matches the CPU with 0 differing pixels on all five. Seven 64 x 64 control scenes give the expected result on both sides: a line attached to its surface stays
(260 pixels), a foreground line over another object stays (420), a line outside a same-object face's footprint stays (162), and lines buried behind their own object, another object, a near-plane-clipped
occluder and a guard-band-clipped occluder are hidden (0 each). With the feature off the CPU replay reproduces the recorded trace exactly. The face data is 5.7 MB for the large scene.

**App validation, `build/f117a.exe` 9 November 13:19:19** (scripts and logs in `private/r38-validate`):

| Check | Feature off (against the accepted baseline) | Feature on (against `private/r38-validate/candidate-baseline`) |
|---|---|---|
| Machine hash, Near and Maximum | `600306c77cb04481`, old and new | `600306c77cb04481` |
| CPU guards, 8 threads | 901/901 at Near, Far, Maximum | 901/901 at each (a repeat of the run that wrote the baseline) |
| GPU guards | 901/901 at each | 901/901 at each |
| CPU, one thread | | 901/901 at each |
| CPU, 64-row tiles | | 901/901 at each |
| CPU, 16-row tiles, 5 threads | | 901/901 at each |
| Three replays at once (8 threads; 7 threads, 32-row tiles; 3 threads, 16-row tiles) | | each 901/901 at each distance |
| Live GPU check, 3,421 pictures per distance | 0 differing pixels at each | 0 differing pixels at each |

The candidate baseline differs from the accepted one in 894 of 901 checksums at each distance: nearly every picture of the runway flight has some slope edge. Unit cases for the cache (dedupe, coverage,
allocation failure, a caller-owned context, the float path) are in `tests/test_remaster_raster.c` (`c4d7131`).

**Pictures looked at** (the agent's own inspection; the owner has seen only the first before and after). App shots at 3840 x 1800 backing 4608 x 1800, feature off and on, six along
`session-20261107-223638` (clocks 12.0 to 15.7 billion) and five along `session-20261108-211031` (7.0 to 10.35 billion); 515 to 1,520 pixels change per shot. In the busiest 400 x 120 region of each: the teeth
are gone on every slope edge that had them; nothing spreads onto neighbouring faces; lights, structures and the HUD are unchanged; no new marks. The edges now read as solid dark bars one game pixel wide, so
the separate thickness question is more visible. Crops: `private/r38-validate/review`.

**Not done yet:** GPU-path shots at 4K against the CPU ones; the paired unpaced 4K cost (the cache is reset on every clear call, once per tile and worker in tiled mode, and the GPU path uploads the table and
faces every frame: untimed); the default; host acceptance and the full CTest on the default-on build; promoting the baseline; ONTests.


### R38, tiled-CPU leak, the hangar rule, default on, and the final required set (9 November 2026, evening)

**Tiled CPU rows leaked a paint owner (`87783ef`).** With R38 on, the CPU's default shots at 4K (row tiles of 64 rows, eight workers) differed from the GPU by 12 to 31 pixels on the owner's late flight `session-20261108-211031` (clocks 9.5, 10.2 and 10.35 billion), while the single-thread CPU and the untiled eight-thread CPU matched the GPU. A diagnostic dump of each worker's face cache showed the same triangle count and different contents: 64 sky and ground band triangles were recorded under owner `0x3f3b1803` in the tiled run and owner `0` in the others. `scene_rows_part` reused one target across a worker's tiles, so from the second tile on the bands (which set no owner) inherited the last primitive's `paint_owner`. With R38 off the stale owner never changed a pixel. Fix: each tile starts from the base target (`t = j->base`). On the fixed build all 11 feature-on CPU shots along two flights equal the GPU shots (0 differing pixels each) and all 11 feature-off shots equal the earlier feature-off ones. The 1080p guards never tile (8 parts and 1200 rows are needed), so only a 4K comparison could see this.

**The hangar door line (host acceptance `rear-hangar-occlusion`).** With R38 on by default the host check failed (12 of 15 frames): one dark-yellow door-line pixel (170,146,32) stayed visible at (600,161) on the lower silhouette edge of a hangar viewed from behind, where the old rule hid it. Offline probe on a trace of that frame (`private/r38-validate/hangar`, clock 10,452,330,792 of `session-20261102-141108`): the stroke pixel sits on face 5 of its own object, its centre-line point is 0.136 px outside that face's triangles, so the coverage test returned "uncovered" and showed the stroke. The known teeth case is 0.0012 px outside, so no distance margin separates them. Depth does: over every uncovered same-object case the old rule would hide, the teeth traces have a stroke-to-surface inverse-depth ratio of 0.955 to 0.99 (minimum 0.955 over the five reviewed scenes), the hangar frame 0.77 to 0.99 with 429 of 631 below 0.95 and the door line at 0.913. **Rule now:** a stroke whose centre point no face of its own object covers is hidden if its inverse depth is below 0.95 of the surface's at that pixel, otherwise shown (CPU `remaster_raster.c`, GPU `strokes_x.hlsl`, both `iz < inv_z * 0.95`). All five reviewed scenes stay byte-identical to the result the owner called "much better" (0 differing pixels each); the hangar frame has 0 yellow pixels and differs from R38-off in 305 pixels (was 714), looked at: the ragged edge on the slope is filled, flag and lights unchanged; the GPU chain tool matches the CPU on the teeth trace (0 of 8,294,400), the hangar frame (0 of 768,000) and the late and middle scenes. Two other variants were rejected: dropping the coverage test for strokes on their own object's face (hangar fixed, but 126 of the reviewed edge pixels reverted to the old gaps) and the plain pre-R38 depth there (660 reverted).

**Cost.** Paired unpaced runs, runway replay, 4K Maximum Full, GPU path, `F117_NO_VSYNC=1`, one at a time: first set (build 17:28:36) off 21.75 and 21.87 ms mean, on 23.01 and 22.42 (about +0.9 ms, p95 +1.3); second set (build 20:24:14, with a 16,384-entry head table) off 17.67 and 17.57, on 19.03 and 19.00 (about +1.4 ms). The machine's state differed between the sets (the off runs moved from 21.8 to 17.6 ms), so only the gap within a set means anything. The smaller head table gave no gain and was reverted. A worker-stage split (diagnostic): GPU span 16.4 to 18.9 ms, compose 10.8 to 13.2, CPU setup +1.3. Accepted for now; the stroke pass is the target of the timestamp-guided optimisation in step 7. Unpaced, the runway replay is about 17.6 to 21.9 ms a frame with R38 off: 60 fps is not met.

**R38 is ON by default** (`decal_faces.h`: on unless `F117_DECAL_FACE=0` or `F117_RASTER_EXACT=0`; unit cases in `tests/test_remaster_raster.c`). Final required set on `build/f117a.exe` **9 November 21:10:17** (logs in `private/r38-validate/final2`, run by `final_set2.sh`):

| Check | Result |
|---|---|
| Machine hash, old and new, Near and Maximum | `600306c77cb04481` each |
| CPU guards, default (R38 on), against `candidate-baseline2` (regenerated on this build) | 901/901 at Near, Far, Maximum |
| GPU guards, default, against `candidate-baseline2` | 901/901 at Near, Far, Maximum |
| CPU guards with `F117_DECAL_FACE=0` against the accepted baseline | 901/901 at Near, Far, Maximum |
| Live GPU checks | 3,421 pictures per distance, 0 differing pixels |
| Host acceptance, with `rear-hangar-occlusion` | 12/12, report verified |
| `candidate-baseline2` against the accepted baseline | 891 of 901 checksums differ at each distance; 332 differ from the first candidate (the hangar rule) |

**Full CTest on the same build:** 845 of 845 in the parallel set (`-E func_lockstep`, 190 s), and `func_lockstep` alone 1 of 1 (1,470 s): 846 of 846, 0 failures, `private/r38-validate/ctest_full.txt`.

**Promoted and shipped:** `private/renderer-bench/baseline` is the new candidate (the previous accepted baseline is kept in `private/renderer-bench/baseline-r38-prior`). `Desktop\ONTests` holds the 21:10:17 build (exe, DXIL, SDL3, mt32emu-2; the two Python tooling DLLs are no longer copied); a hidden private-save smoke launch exited 0.

**Not yet done for R38:** 4K GPU timestamps and the GPU-path 4K shots at the late-flight clocks (done for CPU and GPU parity above; no separate GPU-path timed shot set).

### Clean 4K hardware GPU timestamps, runway replay and the owner's late flight (10 November 2026)

`build/f117a.exe` **9 November 21:10:17** (R38 on, timestamp support compiled in, `F117_GPU_TIMESTAMPS=ON`), 3840 x 2160, picture scale 9, Maximum, Model detail Full, GPU compose and present, `F117_NO_VSYNC=1`, nothing else running, one run at a time, `F117_GPU_TIMESTAMP_LOG` set. Scripts and CSVs: `private/timestamp-4k` (`capture.sh`, `runway.csv`, `late.csv`). Both CSVs validate with `tools/renderer_bench/timestamp_report.py` (1,597 and 1,517 complete frames). The captures cover each whole replay, from its first scene; the CSV has no clock column, so the means below are whole-run means, and the tenths further down show how the cost moves. Hardware query intervals, not frame costs (the intervals between submissions can include idle time; the per-scene totals below are the sum of ten intervals).

| Mean GPU interval per scene, ms | Runway replay (clocks 0 to 4.8 billion) | Late flight (`session-20261108-211031`, 0 to 10.4 billion) |
|---|---|---|
| upload | 0.23 (p95 0.75) | 1.25 (p95 2.83) |
| bands | 8.30 (p95 15.66) | 16.45 (p95 24.17) |
| polygons | 0.90 (p95 1.85) | 2.30 (p95 8.97) |
| points | 0.01 | 0.03 |
| strokes | 7.07 (p95 28.51) | 21.32 (p95 43.61) |
| picture upload, replace, present, download | 0.86 | 1.37 |
| whole-frame cost in the same run (`cost_ns`, with timestamps on) | 18.91 mean, p95 23.34 (784 frames of clocks 3.9 to 4.8 billion) | 151.72 mean, p95 220.19 (1,086 frames) |

GPU total per scene by tenth of the run (mean / worst, ms): runway 35.0/50, 35.0/51, 17.3/22, 17.2/22, 10.0/18, 7.9/9, 8.1/9, 11.4/17, 15.3/17, 16.7/21 (554 of 1,597 scenes over 16.7 ms, 1,253 over 8.3 ms; the first two tenths are not explained: early scenes of the replay, possibly a cold GPU clock); late flight 34.1/58, 35.3/59, 35.1/45, 35.0/64, 34.2/78, 38.4/77, 37.5/73, 47.6/88, 65.1/117, 64.9/141 (1,516 of 1,517 over 16.7 ms, every scene over 8.3).

**Reading.** (1) The bands pass and the stroke-pixel pass are 85 to 90% of the GPU time in both flights; polygons, uploads and the picture layer are small. (2) **By the goal's rule the GPU passes alone exceed both lines (16.7 and 8.3 ms): the bands and strokes passes come before the machine thread, the pipeline and the parallel copies.** The gap to 120 fps on the runway take-off stretch is about 8 to 9 ms a scene on the GPU alone at its best (7.9 ms in the best tenth, which is under 8.3 but leaves no room for anything else) and 17 ms at its worst; on the late flight it is 26 to 57 ms. (3) On the late flight the GPU (about 43 ms mean) is only about 28% of the 151.7 ms frame cost, so the CPU side (scene recording and setup for 35,000 to 62,000 primitives, the picture layer, the machine step and copy) is the larger part there; pipelining hides it only up to the slowest stage, and it also needs to be cut. (4) The strokes pass has a long tail (runway p95 28.5 ms against a 7.1 ms mean; late p95 43.6 ms against 21.3). Earlier attempts that did not help and are not to be repeated: a tile-binned band list (exact, slower) and a per-pixel box test in the stroke pass (exact, no change).

**Next, step 6 of the goal:** read the band and stroke shaders (`src/present/gpu/bands_x.hlsl`, `strokes_x.hlsl`) against these numbers, find where the time goes (per-pixel work that could be per-tile, work done for pixels a later pass overwrites, 64-bit integer cost), and cut it with the output byte-identical to the CPU compositor (the guards). If exact rendering cannot reach the budget, the owner gate in step 7 applies, with these figures as the measured baseline.
