# 60+ fps and 4K: design

Roadmap Phase 3 asks for 60+ frames a second and 4K output. This page says
what constrains each, what is known and what is not, and the order of work.
Nothing here is built yet; the plan is staged so that each stage is useful
alone and none can change the emulated machine.

## The constraint that matters: the presentation must not touch the machine

Phase 1's proof is that the recompiled game is the original, instruction for
instruction, held to GOG DOSBox. A presentation path that changes a single
guest write or clock tick breaks that, and the routes would say so. So every
stage below is an **observer**: it reads what the machine already does and
draws something from it. It never writes guest memory, never changes the
instruction count and never feeds back into the machine. The parity routes
and `func_lockstep` therefore keep their meaning, and a presentation bug can
only draw the wrong picture, never change the game.

## What is known

- **The original runs at a low, speed-dependent rate.** The flight model
  steps once per drawn frame and divides every per-second rate by
  S = `[0x368E]`, which the frame-rate controller clamps to at most 15
  ([bugs.md](bugs.md) D1). **Measured** on the strike route at the default
  9 million instructions a second (`f117run --peek 3B24E`, the word at
  `[368E]`, every 250 million instructions): S is 12-13 in the first part of
  the flight and **9** from about 5 billion instructions to the end of the
  route. So a GOG-speed machine runs the flight at about 9 steps a second,
  and a faster emulated CPU raises it towards 15. 60 fps cannot come from
  the original drawing faster: it must come from *interpolating between its
  states*, a jump of 4 to 7 times. Raising S is not an option: the flight
  model is 16-bit integer, and rates smaller than S round to zero (that is
  D1's missed missiles).
- **The host shows the last frame the VGA scanned out** (`present_capture`
  on each vertical retrace, `src/host/main.c`) and scales it to the window
  with SDL (`--scale N`, `--fullscreen`, aspect correction). So a large
  window or a 4K display already works as *a scaled 320x200 picture*. That
  is not 4K rendering: every scene edge is still on the 320x200 grid.
- **Time is one real second per game second** (the mission-clock invariant),
  so interpolating game state against the host clock is meaningful.

## Stage 0, first measurement: what draws

A trace of 1.5 million instructions of the strike route in flight
(`f117run --trace`, the registers before every instruction; the stores
decoded offline from the original's bytes, each attributed to a routine by
the Reimp's census) shows the 3-D scene drawn in stages, each a routine
this project already has or is matching:

1. **Transform and prepare**: `model_xform_vertex` (`0x128B5`, the most
   store-heavy routine), `model_prepare_edge` (`0x12BDB`), the camera
   matrix routines (`0x1473B`, `0x14850`), clipping (`mclip_*`,
   `raster_clip_model_edge32`).
2. **Edge and span tables**: `edge_column;raster_spans_col` (`0x10075`),
   `raster_spans_clear_used` (`0x1003F`, which alone stores 1.4 MB in the
   window by clearing tables) and `raster_spans_edge_clipped` (`0x101C6`),
   all into one 64K region (linear `0x3xxxx`: the span tables).
3. **Fill**: `raster_model_fill` (`0x1420C`, `0x1418C`, `0x140E6`) and the
   fill at `0x142BF` write pixels into a second region (`0x5xxxx`, the back
   page): 29 KB of pixels against 2.3 MB of table traffic.

The picture is made *from* projected polygons through span tables, so the
polygon list (colour, vertices) exists in the middle of the path and is the
natural place to observe.

**One logic step is two `game_draw` phases.** The step itself (`frame_effect_timers`,
`0x04738`, then free fall `0x04958` 218 instructions later) was entered at
instructions 514,934 and 1,295,143 of the window: 780,000 apart, which is
8.6 steps a second at 9 million instructions a second, agreeing with the
measured S = 9. `game_draw` (`0x01450`) was entered at 142,435, 522,343,
921,618 and 1,303,588: twice per step, about half a step apart, one just
after each step (7,400 and 8,400 instructions after it) and one midway.
Splitting the window at those entries, the two halves of a step are
different work, and the same from one step to the next:

| slice (instructions) | length | work |
|---|---|---|
| first `game_draw` to the second (contains the step) | 380,000 | 4,662 vertex transforms, 3,208 edge preparations, 1,538 fill stores |
| second to the next first | 399,000 | 7,590 fill stores, 7,167 span-column stores, 6,644 table-clearing stores |
| the same again, one step later | 382,000 | 4,662, 3,207 and 1,538: the first row to within one store |

So a step is two **phases** of one picture: the scene is walked, transformed
and cut into edges, and then spans are built and filled. Both belong to the
same frame, and an observer must collect across both before it has the
frame's polygons. The step's physics runs between them.

A fifth of the traced window (21%) runs outside the unpacked VGAME image, in
segment `0889`. A memory dump of it (`f117run --dump 08890:1400`) shows it is
the **resident graphics library** that VGAME reaches through the
`1E42:0188...` driver slots (each `JMP FAR 0:0` in the file, patched at
load): a polygon **span fill** (for each row, left and right taken from two
tables on the stack, a `REP STOSW` into the target page at the row's offset
plus the page base), a **transparent sprite blit** (`LODSB; OR AL,AL; JE
skip; STOSB`, so colour 0 is transparent), and further entry points. So the
frame has *two* rasterisers: VGAME's own model rasteriser (transform, span
tables, and the row loops at `0x1418C` and `0x1420C` that AND, OR or store a
pattern) and this library, which draws what the model rasteriser does not
(sprites, the HUD, panels). An observer on the driver slots sees every
library call with its arguments before it becomes pixels, which is the
right place for sprites and the HUD.

**The observer (Stage 0, built).** `src/matched/observe.c`/`observe.h`: a
hook on `game_draw`'s entry and one in the matched projection, both pure
readers. `f117run --observe FILE:FROM:TO` logs `P icount` for each phase of
a picture and `V icount x y z px py range` for each projected vertex (the
32-bit camera-space coordinates and the pixels the original made of them).
Checks, on the strike route over 3 million instructions of flight: the
final hash is identical with and without it; two logged runs are
byte-identical; the eight phase entries land on exactly the instruction
counts the trace found; the unit test (`test_overrides`) decodes a vertex
and checks it wrote nothing.

What the log shows: the two phases of a step draw **different sets** of
vertices, 258 in one and 220-223 in the other, and each set is the same
size from step to step (the 258 are 150 near, 93 mid-range and 15 behind the
eye). Comparing the same phase of consecutive steps, **87% to 99% of the
vertices are in the same slot, within 40 pixels** (225, 256, 220 of 258; 216
of 223; 94 of 220 in the one pair where the count changed from 220 to 223).
The original therefore draws in a stable order, and a pairing by slot works
for most of a frame; where a count changes, the slots after the change shift
and need a smarter match.

## What is not known (and has to be measured next)

1. **Where the scene is drawn.** The matched routines already name part of
   the path: the model fill setup (`1377:01E0`), the polygon row spans
   (`130D:00B6`, `130D:00D1`), the span tables (`0FB2:051F`), the clip
   outcode (`130D:0671`), plane shading (`120A:0674`) and, for sprites, the
   graphics driver's slot at `1E42:0188`. Text, the HUD, the map and the
   cockpit panels go elsewhere. A census of every routine that writes the
   frame buffer, by caller, is the first piece of work.
2. **What precision the scene has before it is flattened. (Answered from
   the Reimp's documented routines; to be confirmed against the trace.)**
   `model_xform_vertex` (`0x128B5`) turns each 16-bit model vertex into
   *32-bit camera-space coordinates*: three 16 x 16 products per axis,
   summed at 32 bits with the part's translation. The projection
   (`0x129A2`) then divides `(x >> 8)` by the high word of z into an
   *integer pixel*, and everything after that - the 32-bit clip arm (a
   point is on the boundary exactly, in whole pixels), edge accumulation,
   the span tables, the fill - works in whole pixels. So the edge stage has
   no sub-pixel information to offer; the camera-space vertices before the
   divide do. A real high-resolution re-draw therefore has to observe the
   transformed vertices at `0x128B5`, redo the projection at N times the
   resolution with the fraction kept, and redo the clip and the span
   generation at that resolution. It cannot simply scale the original's
   spans. (A cheaper "4K" that scales the integer-pixel edges draws the
   same shapes sharper, with the 320x200 staircase intact.)
3. **Whether successive frames draw the same objects in the same order.**
   Interpolating between two states needs a pairing: object A in frame n
   with object A in frame n+1. If the draw order is stable (the original
   sorts by depth, so it can reorder) or every object carries an id, the
   pairing is easy; otherwise it needs a matching step.

## A fork to decide before Stage 1: observe the draw path, or render from state

Two ways to get a high-resolution, interpolated picture, and they differ in
what they depend on.

**A. Observe the original's draw path (what Stages 0-3 describe).** Capture
the original's own primitives (camera-space vertices, polygons, library
calls) and re-draw them. Everything stays inside this project; the work is
understanding and replaying the original's drawing, bit for bit at 320x200
first, and the library's calls (sprites, HUD) need the same treatment.

**B. Render from the game state with the Reimp's renderer.** The Reimp's
`vg_ds` (`src/core/vgame_ds.h`) is a 64 KB image of VGAME's data segment
with the original's own offsets, "the engine's simulation state" in the
Reimp's words. A snapshot of the machine's data segment at a step boundary
is therefore already a complete game state in the Reimp's format, and the
Reimp has a renderer (`raster.c`, `model.c`, `scene.c`, `camera.c`) that
takes a `vg_ds` and draws from it at parameterised sizes. Two consecutive
snapshots give the two states to interpolate between. This would reuse
verified code instead of re-deriving it, but it makes this project depend on
the Reimp's renderer and on assets it loads, the data segment is not the
only state a frame reads (models and terrain come from the game's files),
and the Reimp's renderer is held to its own oracle, not to this machine's
instruction-level one.

**Decided (5 October 2026, by the owner): A, observe and replay.** The
picture is made from the original's own primitives, captured in this
project, with no dependency on the Reimp's renderer. B stays recorded as the
road not taken; Stage 0's observer serves A directly (it says what the
original drew, which Stage 1 must reproduce bit for bit).

## Stages

Each is judged by a check, in the manner of the rest of this project.

**Stage 0 - measure (this page's open questions).** An observer in the
recompiled engine that logs, per frame, every call to the drawing
primitives with its arguments. Output: a census of what draws what, how
many primitives a frame holds, what the transformed camera-space vertices hold, whether the
order is stable. Check: the log of two runs of a route is identical, and
turning the observer on leaves every checkpoint and final hash unchanged.

**Stage 1 - draw lists.** Capture a frame's primitives (polygons with
colour and vertices, lines, sprites with their driver arguments, text) into
a list instead of only into pixels. The pixels the original still draws are
the reference. Check: replaying the list at 320x200 with our own rasteriser
reproduces the original's frame bit for bit, on every frame of every route.
That check is the proof the list is complete, and it is the hardest part of
the whole item: it needs the rasteriser to match the original's pixel rules
(top-left, fill order, the 16-bit edge arithmetic) exactly.

**Stage 2 - high-resolution re-draw.** Draw the same list at N times the
resolution (the vertices scaled, edges drawn exactly at the new grid). The
HUD and text are bitmapped and stay as they are, or are scaled with
integer nearest-neighbour. Check: at N = 1 it is Stage 1's reference, bit
exact; at N > 1 every N x N block of the output agrees with the N = 1 pixel
wherever the edges do not cross it.

**Stage 3 - interpolation to 60+ fps.** Between two consecutive draw lists,
pair primitives and move their vertices linearly in time; draw at the host's
display rate. The simulation is not advanced: the machine keeps its own
steps and the in-between pictures are only drawn. Cost to acknowledge:
the picture is one logic step behind (about 110 ms at the measured 9 steps a second, 67 ms at the clamp of 15), the standard
price of interpolation. Check: at the instant of a logic frame the output is
exactly that frame (so the original's picture is always reproducible), and
the in-between frames have no primitive that is absent from both
neighbours.

**Stage 4 - polish.** Frame pacing, vsync, a setting for the picture's age,
HUD handling, and a switch that restores the original picture exactly.

## Why this belongs after (and needs) Phase 2

Stages 1 and 2 need the drawing path *understood*: what each primitive's
arguments mean, so that they can be recorded and re-drawn. The matched
routines are that understanding, with the Reimp's names where they apply.
Every drawing routine matched in Phase 2 becomes one that the observer can
be attached to without guessing. So the two phases are not independent: the
work in P2 on the polygon, span, clip and sprite routines is the shortest
path into Stage 1.

## Risks

- **Stage 1's bit-exact replay may be the wall.** If the original's
  rasteriser depends on state we cannot see in the draw list (a clip window
  held in a register, a dither phase), the replay will miss pixels and the
  list is incomplete. That is a finding, not a failure of method: it says
  what else the observer must record.
- **Interpolation of things that are not smooth.** Explosions, tracer
  lines, the HUD, a cut between views: the pairing step has to leave them
  alone (hold the previous frame or switch on the logic frame).
- **Display-rate drift.** Interpolation against the host clock needs one
  agreed time base with the machine's; the mission-clock invariant gives it.

## What is decided and what is not

Decided, by the machine's design and measured facts above: presentation is
an observer; the machine keeps its own steps; interpolation, not faster
simulation, gives 60+ fps.

Open: whether real sub-pixel "4K" (re-projecting the 32-bit camera-space
vertices at the new resolution) is worth its cost against the cheaper scaled
picture. The data for it exists; the clip and span stages would have to be
redone at the new resolution.
