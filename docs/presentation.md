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

**The draw list (Stage 1, capture).** The observer now records a frame's
primitives as the original hands them to its rasteriser: `G` an edge
prepared (its slot and the two projected vertex records it came from, so an
edge leads back to camera-space vertices), `E` an edge of a filled polygon
(130D:004A: slot, x0, y0, x1, y1), `F` the polygon's fill (130D:0116: the
colour word), `B`/`L` an outline polygon begun (1377:004C) and its edges drawn
as lines (1377:0055), and `V` each vertex with the addresses of its records.
The high word of an edge's x0 holds the edge stage's markers (4040h clipped
at one end, 8080h both ends behind the eye), so a reader masks it. Over 3
million instructions of the strike route's flight (eight phases, about four
steps): 1,889 vertices, 2,474 prepared edges, 388 filled polygons from 1,595
edges, and 1,032 outline polygons with 459 drawn edges; the run's final
hash is the same with the observer on. Not captured yet: the resident
graphics library's calls (sprites, HUD), and polygons whose edges were
prepared by the original because the matched routine declined.

**Replaying the edges (Stage 1, first check).** `tools/drawlist_spans.py`
takes the captured edges of every filled polygon and scan-converts them into
an empty span table by the original's rules - the model edge walk (1377:072B:
order by x, drop an edge that leaves the vertical clip, steep and shallow
arms with their exact error terms) and the polygon accumulator (130D:004A:
status OR, the y ranges that left through each side, the near-clip count) -
written here from their documented behaviour. The fill hook records the
span rows the original built, and a hook where the fill starts painting
(1377:005E) records the final rows, after the fill entry has added the
near-clip join (the one edge it hands the rasteriser when two crossings were
collected, already clipped, so no clipper needs replaying) and the border
runs (one column outside the viewport, over the y range that left through
each side). On 30 million instructions of the strike route's flight: **3,678
polygons, 8,897 edge rows and every accumulator exact; 2,719 painted
polygons, every final row exact, including all 155 with a near-clip join.**
**And to pixels.** The painting hook also records the page bytes each row
covers (clamped to the viewport) before the fill, and the next event the
observer sees records them after; the checker paints the row in the fill's
style - from the colour word the fill really uses (`[8606]`, after its fade):
FF solid, FE AND, FD OR, FB discard - and compares. **11,293 of 11,293
painted rows are exact** (solid 4,474, OR 3,592, AND 1,903, discard 1,324),
so the model polygons replay from the draw list to the original's pixels
bit for bit. Not yet replayed: the dither and stipple styles (not seen in
this window), outline polygons' lines, the sky and ground, and everything
the resident graphics library draws (sprites, HUD, text).

**The library's lines.** Graphics entry 31 (the jump slot at 1E42:01B5,
patched at load) draws 40 or so lines a frame; entries 32 and 33 set their
colour. The observer logs each line's endpoints and colour, keeps its
bounding box's page bytes (a pixel is page:rowtab[y] + origin + x, all three
read from the driver's own code segment), and at the next event logs the
pixels the line changed. The checker draws the line by the library's
documented rules (an unsigned sort on x, the single-pixel case decided by
"were the two x's equal", a half-step error term) and requires every changed
pixel to be on it, in its colour: **1,348 of 1,348 lines consistent.** The
test is one-sided - a pixel that already had the line's colour does not
change, so it cannot show a pixel the replay draws and the original does not.

**The library's span fills.** Entry 37 (1E42:01D3) fills rows ya..yb from
two bound tables in the current colour; the observer logs the rows' bounds
(a left of FFFFh is an empty row) and the changed pixels as for a line. A
fill whose first row is negative fills nothing and is skipped. **2,183 of
2,183 span fills consistent** in the same window, all mode 0 (the same
one-sided test).

**Every other graphics entry is logged** ('X', the entry number), so what
draws between two captured primitives is known, and a pending line or span
capture is closed before it - but not a model fill's capture, because the
fill calls entry 46 while it paints (closing on it cut 546 rows short). In
the 30 million instructions: 26 (1,948 calls), 62 (1,085), 42 the blit
(407), 5 (389), 1 (370), 46 (232), 41 (230), 73 (222), then 21 more entries
under 120 calls each, 18 (the transparent blit) among them at 74. Those
are the ones still to capture.

**What the other phase draws.** With `F117R_OBSERVE_PAGES=1` an entry no
hook captures also keeps the whole page it draws to, and the next event logs
every byte that changed ('x'). In the phase that draws the cockpit and HUD
(origin 8A29h at its start) the blit, entry 42, changes about 5,000 of 6,670
bytes, then entries 73 (480), 5 (about 390), 1 (163), 4, 11, 18, 3 and 71.
Entries 24 and 26 only set the origin (to 0, and to their argument - the
HUD phase moves it mid-phase), 62 computes an address, 65 sets a variable
and 46 writes the DAC and the CRTC start; none draws in its own body. A
nested state setter can still split a parent entry that resumes drawing after
it. (Crediting each
changed byte to the last capture that touched it suggested the 3-D phase was
fully captured; it was not - see the replay below. The earlier 82% / 2%
labels by origin were right.)

**The blit.** Entry 42 is `(src_page, sx, sy, dst_page, dx, dy, w, h)`, far,
eight words; a page is an index into the driver's page table at cs:[0787]:
0 the display (A000), 1 the work page (47BD, the one dumped), 2 the cockpit
art (09C0). Each HUD phase restores instrument backgrounds from page 2 into
page 1 and copies regions of page 1 to the display. Logged ('C') with the
source bytes in accounting mode: **77 of 77 blits exact** (35,971 bytes) -
every changed byte inside the destination and equal to its source.

**Whole phases rebuilt from the draw list.** `tools/drawlist_frame.py`
starts each phase from the page dump at its start, replays every captured
primitive in order and compares the result with the dump at its end, byte
for byte over the whole 64 KB page. Model fill rows are painted in their
style from the replay's own bytes; library lines, span fills and blits by
their rules (a blit within the page from the replay itself, from the art page
from its logged bytes); outline polygons' edges by VGAME's own line routine
(1377:046F, reached through the style table at [85F2]: endpoints from the
edge slot, rows from [861C], colour [8606] - the writer the attribution
above had hidden, and the last 222 bytes a 3-D phase); origin changes by
entries 24 and 26. What has no rule yet is applied from the original's
result and counted: the HUD entries above. **On 30 million instructions of
the strike flight, 73 of 73 phases are rebuilt exactly**; of 486,067 bytes
changed, 83,444 (17%, all in the HUD phases) come from those recorded
results and the rest are replayed. The 3-D phases are replayed completely.
An off-by-one in the outline rule's error term makes all 14 phases of a
6-million-instruction window inexact.

**Text.** Entries 5, 4, 3 and 1 are the library's text: a parameter block in
SS (page, mode, foreground, background, x, y, font, a row clip and a width
clip) and a NUL-terminated string, 5 taking both as arguments and the others
at BP and BX; 3 first cuts the string at the width clip (the last character
narrowed by the overshoot), 1 first clips the rows. The painter (driver
0x61A) draws 1-bit glyph rows (16-bit, high bit first) from a font in the
driver's data segment (the table at 00D0: first and last character, a shift
giving the glyph stride, a fixed width or a width table, height, spacing,
extra rows), transparent or, in mode 1, opaque; a negative character code
switches the foreground. The observer logs each call with its string and its
font ('T'), so the replay needs no game data. With text replayed, **73 of 73
phases are still exact and the bytes copied from the original fall to 49,432
of 486,067 (10%)**: sprites (entries 73 and 18), the HUD drawer (11) and 71.
Every call in this window is transparent, so the opaque branch is unchecked;
dropping the row clip makes 7 of 14 phases inexact, but the width clip never
cuts a string here.

**Sprites, tick scales, and the page.** Entries 73 and 18 copy a sprite
(driver 0EE6: a block with the source segment, the source and destination
corners, the destination page and the size; colour 0 is transparent), 71 and
19 clip it first (0E41: signed limits, the far end compared with the
original near end); the observer logs the block and the source bytes ('S').
Entry 11 draws a tick scale (071A: from row BX upward in steps of two, a
long tick every tenth and a medium one every fifth, leftward or rightward,
limits and x from a table in the driver's data segment; logged with them,
'H'). Entry 40 is entry 37 with mode 0. And the library's page moves:
entries 12-16 point it at another page (the HUD draws some lines and span
fills straight to the display), so each line and span fill now carries the
page and the origin it was drawn at. **With these, 73 of 73 phases are
rebuilt exactly and nothing is copied from the original's results** - every
changed byte of the work page in 30 million instructions of flight is
replayed from the draw list. Dropping sprite transparency makes 7 of 14
phases of the short window inexact; the tick scale's CL variant never occurs
there. The display page is also checked byte for byte at all 73 phase
boundaries in the strike-flight trace; some direct writes from graphics
entries not yet decoded are replayed from their captured byte deltas. The
opaque text, width clip and dithered-fill branches remain unexercised.

**The display page.** The observer also dumps the display (A000, 'Y') at each
`game_draw`, and the replay keeps one copy per page, each primitive drawn on
the page it names. Entry 44 is the present (driver 11F1: unless the driver
flips pages, cs:[11DC], it copies AX words - 42E0h, the 107 rows of the 3-D
window - from offset 0 of page 1 to the display); 48 and 79 are whole-page
copies and a dissolve. The observer logs entry 44's source bytes at the copy
and the display bytes changed by the original call. It also keeps the display
page while an uncaptured graphics entry runs, because an entry can write to
A000 directly even when the driver's active page points elsewhere.

The first mismatch at A000:81C1h (row 103) was an entry 41 write after the
present. A later 49-pixel mismatch came from an outer entry 41 resuming after
nested entry 46 had flushed its page snapshot. Capturing both the active page
and A000 across those nested boundaries accounts for both cases. On the
30-million-instruction strike-flight trace, work-page replay is **73 of 73
phases exact** (486,067 changed bytes), and display-page replay is **73 of 73
phases exact** (22,493 changed bytes). For the one phase recaptured after the
nested-boundary fix, the 64 KB work and 64,000-byte display snapshots at both
phase boundaries match the full trace byte for byte; replacing that event
slice in the full log preserves exact replay across all 73 phases. The
unexercised drawing branches and other routes remain open.

**How much of a frame is captured.** With `F117R_OBSERVE_PAGES=1` the
observer dumps the library's whole 64 KB page segment at each `game_draw`
entry, and painted pixels are logged by their exact offset in it. A phase is
then rebuilt from the previous dump plus every captured primitive and
compared with the next dump. The two `game_draw` phases of a step alternate
between two buffers in the one segment (origin 0 and origin 8A29h - so the
"two phases" are double buffering as much as anything), and each changes
about 6,650 bytes. On the strike route's flight, polygons and lines alone
account for **82%** of the origin-0 phase changes (1,166 bytes a phase left)
and about 2% in the origin-8A29h phases. The rest comes from the HUD and
library entries described above. After recording those entries and direct
display writes, the full work and display pages replay exactly on the 73
captured phases; drawing rules for several raw library entries remain open.

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
