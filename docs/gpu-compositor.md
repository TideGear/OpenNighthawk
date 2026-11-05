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
