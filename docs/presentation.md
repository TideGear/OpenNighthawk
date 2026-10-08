# 60+ fps and 4K: design and status

Roadmap Phase 3 asks for 60+ frames a second and 4K output. This page says
what constrains each, what has been measured, how far the draw-list capture
has got, and the order of the remaining work. The plan is staged so that each
stage is useful alone and none can change the emulated machine.

## The constraint that matters: the presentation must not touch the machine

Phase 1's proof is that the recompiled game is the original, instruction for
instruction. A presentation path that changes a single guest write or clock
tick breaks that, and the routes would say so. So every stage is an
**observer**: it reads what the machine already does and draws something from
it. It never writes guest memory, never changes the instruction count and
never feeds back into the machine. The parity routes and `func_lockstep`
keep their meaning, and a presentation bug can only draw the wrong picture.

## What is known

- **The original runs at a low, speed-dependent rate.** The flight model
  steps once per drawn frame and divides every per-second rate by
  S = `[0x368E]`, which the frame-rate controller clamps to at most 15
  ([bugs.md](bugs.md) D1). At the default 9 MIPS, S is 12-13 early in the
  strike flight and 9 from about 5 billion instructions on: about 9 steps a
  second (8.6 measured from the step routine's entries). A faster emulated
  CPU raises it towards 15, which is why `--fix D1` paces frames at GOG's
  rate. 60 fps cannot come from the original drawing faster; it must come
  from *interpolating between its states*, 4 to 7 times. Raising S is not an
  option: the flight model is 16-bit integer and rates smaller than S round to
  zero.
- **The host shows the last frame the VGA scanned out** and scales it with SDL
  (`--scale N`, `--fullscreen`, aspect correction). A large window or a 4K
  display already works as a scaled 320x200 picture; every scene edge is
  still on the 320x200 grid.
- **Time is one real second per game second** (the mission-clock invariant),
  so interpolating game state against the host clock is meaningful.
- **One logic step is two `game_draw` phases** (`game_draw` is `0x01450`):
  one just after the step and one about half a step later. They alternate
  between two buffers in the library's 64 KB page segment (origin 0 and
  8A29h), draw different work (the scene is walked, transformed and cut into
  edges in one; spans are built and filled in the other) and repeat the same
  pattern step after step. An observer must collect across both before it has
  a frame's polygons.
- **The frame has two rasterisers.** VGAME's own model rasteriser (transform
  `0x128B5`, projection `0x129A2`, edge and span tables, the row loops at
  `0x1418C` and `0x1420C`) draws the 3-D scene; the resident graphics library
  in segment `0889`, reached through the `1E42:0188...` jump slots (each
  `JMP FAR 0:0` in the file, patched at load), draws sprites, text, lines,
  span fills and the HUD. About a fifth of a flight's instructions run in the
  library.
- **The scene is flattened to integer pixels early.** `model_xform_vertex`
  gives 32-bit camera-space coordinates; the projection divides into an
  integer pixel, and the clip, edge accumulation, span tables and fill all
  work in whole pixels. Sub-pixel information exists only before the divide.

## The decision: observe the draw path (5 October 2026, owner)

Two ways to get a high-resolution, interpolated picture were considered.
**A**, chosen: capture the original's own primitives in this project and
re-draw them. **B**, not taken: render from the data segment with the Reimp's
renderer, which would reuse code but make this project depend on the Reimp's
renderer and assets, and on a renderer held to its own oracle rather than to
this machine's instruction-level one.

## Stages

Each stage is judged by a check, as in the rest of the project.

| Stage | What | Check | Status |
|---|---|---|---|
| 0 | Observer: log every call to the drawing primitives per frame | the log is identical across runs; hashes unchanged with it on | done |
| 1 | Draw lists: a frame's primitives as a list, replayed at 320x200 | the replay reproduces the original's work and display pages bit for bit, every phase of every route | done for the windows below; unexercised branches listed below |
| 2 | Re-draw the list at N times the resolution | N = 1 is Stage 1 exactly; at N > 1 every N x N block agrees with the N = 1 pixel wherever no edge crosses it | first build run (`hires_frame.py`): the scaled walk cracks along shared edges; the clip stage must be redone |
| 3 | Interpolate between consecutive draw lists to the host display rate | at a logic frame the output is exactly that frame; no in-between primitive absent from both neighbours | not started |
| 4 | Pacing, vsync, a picture-age setting, HUD handling, a switch to the original picture | - | not started |

Interpolation shows the picture one logic step behind (about 110 ms at 9
steps a second, 67 ms at the clamp of 15), the standard price. Stage 1 needs
the drawing path understood, which is what the matched routines in Phase 2
supply, so the two phases are not independent.

## Stage 0 and 1: the observer and the draw list

`src/matched/observe.c` hooks `game_draw`'s entry, VGAME's fill and outline
paths, the matched projection and every graphics-library entry. It is a pure
reader: with it on, every checkpoint and final hash is unchanged.
`f117run --observe FILE:FROM:TO` writes the log; with `F117R_OBSERVE_PAGES=1`
it also dumps the library's whole page segment (`Z`, work page) and the
display (`Y`, A000) at each `game_draw`.

Record kinds: `P` phase, `V` projected vertex (camera-space x, y, z and the
pixels the original made of it), `G` edge prepared, `E` edge of a filled
polygon, `F`/`R` the polygon's fill and row spans, `b`/`a` fill rows (solid
or AND/OR/discard styles; `a` is dither/stipple), `B`/`L` outline polygon and
its edges, `N` library line, `K` colour, `Q` span fill, `C` blit, `T` text
(with its font, so the replay needs no game data), `S` sprite, `W` scaled RLE
sprite, `H` tick scale, `D` page copies (44 present, 48 copy, 79 dissolve),
`X` any other entry, `x` changed byte deltas.

`tools/drawlist_frame.py LOG` rebuilds each phase from the previous page dump
plus every captured primitive in order and compares it, byte for byte over
the whole 64 KB page, with the dump at the phase's end; `drawlist_spans.py`
checks primitives one by one; `hires_spans.py` is the Stage 2 check. Facts the
replay rules rest on:

- **Model polygons** replay to the original's pixels bit for bit: the model
  edge walk (`1377:072B`), the polygon accumulator (`130D:004A`), the near-clip
  join and border runs, and fill styles from the colour word the fill really
  uses (`[8606]`: FF solid, FE AND, FD OR, FB discard). Outline polygons use
  VGAME's own line routine (`1377:046F`, via the style table at `[85F2]`).
- **Library entries**: lines (31), span fills (37, 40), blits (42; a page is an
  index into the page table at `cs:[0787]`: 0 display, 1 work page, 2 cockpit
  art), text (entries 1-6: a parameter block and a NUL-terminated string with
  width and row clips and a left skip; glyph rows from the driver's font table),
  sprites (73 and 18; 71 and 19 clip first), tick scales (11), the scaled
  flippable RLE sprite (22, with Bresenham-style column and row stepping), the
  page switches (12-16, so lines and span fills carry the page and origin they
  were drawn at) and the present (44). Entry 46 writes only the DAC and CRTC
  start, so its byte deltas are the fill's own rows. Entries 24 and 26 set the
  origin.
- **Capture traps found on the way:** a nested state setter can split a parent
  entry that resumes drawing, and an entry can write to A000 directly even when
  the active page points elsewhere, so both page snapshots restart after every
  graphics entry; a pending line or span capture closes at the next entry but a
  model fill's does not (the fill calls entry 46 while it paints).

### Coverage (30M-instruction windows)

With 0 bytes copied from the original, every phase rebuilds exactly on both
pages on these routes (phases in brackets) at the windows tried:

- Flights from `ordnance.pic` onward: strike (73), recon (34), Central Europe
  air-to-air (29), landing (38), cargo (38), Korea, Kuwait, Middle East and
  North Cape strikes (39, 33, 28, 27), Central America ground training (27),
  Persian Gulf air training (32), Vietnam air-to-air (27), secret airstrip (32).
- Later in flights: cargo at 7.30B (45), strike at 8.53B (69) and at the impact
  8.72B (56), landing at 9.90B (18), `recon_return` at 14.0B (69),
  `strike_return` on the landing approach at 14.99B (45), and the AMRAAM launch
  and kill windows of `airair_type6` (5.092B, 5.240B: 59, 57 phases) and
  `airair_type5` (6.793B, 6.914B: 57, 66).

Command per route: `F117R_OBSERVE_PAGES=1 py tools/run_route.py
tools/routes/NAME.args --data DIR --out OUT -- --observe LOG:FROM:TO`, with
FROM just after the flight's `ordnance.pic` open, then `drawlist_frame.py LOG`.

**Not yet exercised:** dithered and stipple fills (no window used one), the
opaque text branch and the width clip, sprite clip edge cases, the tick scale's
CL variant, and windows in the rest of each flight (weapon release on other
weapons, the cockpit's other displays).

## Stage 2: what the list holds and what it needs

The edge records carry integer screen endpoints, already rounded to the
320x200 grid by the original's projection and clip, so scaling them by N and
running the same edge walk gives only an N x N nearest-neighbour picture.
Real sub-pixel detail needs the camera-space vertices before the divide (the
`V` records), and the clip (which drops a whole edge that leaves the window
rather than clipping it) and the span stage must be redone at the new
resolution.

`hires_spans.py` re-walks every filled polygon's edges on an N-times grid and
tests the stage's rule on the 3,332 coarse pixels whose eight neighbours are
inside the polygon (so no edge crosses them): N = 1 agrees on all, **N = 2 on
88.8%, N = 4 on 85.8%**. Every disagreement is in 36 four-edge polygons whose
edges carry the clipper's statuses 0x80 and 0x41 (an edge rejected to borders
only, or near-clipped): the original clips whole edges in integer coordinates
and records their borders from y ranges, and those rules do not scale. So the
plain scaled walk is right for unclipped polygons and the clip stage is what
has to be redone. The first Stage 2 build is the scaled N x N renderer with
that check; the sub-pixel re-projection follows only if that picture is
judged too blocky.

### First Stage 2 build (8 October 2026)

`tools/hires_frame.py LOG [N] [OUT_DIR|-] [--floor]` replays a log with the Stage 1 replay
(`drawlist_frame.main`, which gained three hooks and is otherwise unchanged: 25 of 25 phases
exact as before) and keeps beside each page a picture N times finer. Every write the replay
makes is mirrored as an N x N block, except a model polygon's fill: its edges are re-walked on the
fine grid, the coarse pixels no edge can cross (all eight neighbours inside) are covered whole, a
fine row the walk does not reach takes the plain scaled span, and the fill's style is applied to
the fine rows. A page copy or blit between replayed pages carries the fine rows across, so the
present shows the fine picture.

Result on the strike window (25 phases, 3,125 polygons, N = 2): 84.9% of the coarse pixels whose
3x3 neighbourhood is one value keep that value on all four fine pixels, and 8.4% of the fine
picture differs from the plain scaled copy (N = 4: the same 84.9%, 8.6%). The disagreements are
not edges: they are cracks where two polygons meet. Each polygon's fine walk puts its boundary at
the top-left corner of a coarse pixel, and a polygon that ends there (the ground triangle's lower
side, clipped edges with statuses 0x80 and 0x41) stops a fine row short of its neighbour, so an
older colour shows through along the shared edge (a strip of the sky colour 127 in a ground region
of 238 in the first sample). Covering each polygon's interior whole and extending its far sides by
N - 1 fine pixels does not close them, because the shared edge is not on one fine row for both
polygons.

With `--floor` each fine fill is laid over the nearest-neighbour fill and never takes coverage
from it: the flat-pixel agreement is 100%, and only 0.13% of the fine picture differs from the
scaled copy, so the picture is correct and no sharper. The two modes bracket the method. What the
integer edge walk cannot give is a rule that tiles neighbouring polygons on a finer grid (the
original's inclusive spans overdraw on both sides at 1x, and nothing in the list says where the
true shared edge lies); it needs the camera-space vertices and a fill rule that tiles (top-left),
which is the sub-pixel re-projection this stage's earlier text held back. The next Stage 2 step is
therefore that re-projection for the model polygons, judged by this tool's two numbers: agreement
at 100% and the fine picture differing from the scaled copy by more than 0.13%.

## Open questions and risks

- **Pairing for interpolation.** Interpolation needs object A in frame n paired
  with object A in frame n+1. The two phases of a step draw different sets of
  vertices, each the same size from step to step, and 87-99% of vertices are
  in the same slot within 40 pixels from one step to the next, so slot pairing
  works for most of a frame; where a count changes, the slots after it shift and
  need a smarter match.
- **Things that are not smooth** (explosions, tracers, the HUD, a cut between
  views) must be left alone: hold the previous frame or switch on the logic
  frame.
- **The wall.** If the original's rasteriser depends on state not in the draw
  list (a clip window held in a register, a dither phase), the replay misses
  pixels; that is a finding that says what else to record, not a failure of
  method.
- **Time base.** Interpolating against the host clock needs one agreed time
  base with the machine; the mission-clock invariant provides it.
- **Is sub-pixel "4K" worth its cost** against the cheaper scaled picture? The
  data exists; the clip and span stages would be redone.
