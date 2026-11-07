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
