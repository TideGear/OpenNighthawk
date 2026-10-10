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
  ([bugs.md](bugs.md) D1). Its pacing varies with scene and flight state:
  the nine-route study averages 16.27 drawn frames a second at 9 MIPS,
  with mean S 14.10. D1 limits faster CPUs to about 11.6 drawn frames a
  second and S 9, reproducing the parked GOG baseline rather than every
  airborne scene ([speed-sweep.md](speed-sweep.md)). 60 fps cannot come
  from the original drawing faster; it must come
  from *interpolating between its states*, 4 to 7 times. Raising S is not an
  option: the flight model is 16-bit integer and rates smaller than S round to
  zero.
- **The host shows the last frame the VGA scanned out** and scales it with SDL
  (`--scale N`, `--fullscreen`, aspect correction). A large window or a 4K
  display already works as a scaled 320x200 picture; every scene edge is
  still on the 320x200 grid.
- **Presentation uses the machine clock.** The host paces emulated seconds
  against real seconds; the original's mission clock runs faster at every
  speed measured ([speed-sweep.md](speed-sweep.md)). Interpolation follows
  the machine's frame timestamps without changing that world speed.
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
| 1 | Draw lists: a frame's primitives as a list, replayed at 320x200 | the replay reproduces the original's work and display pages bit for bit, every phase of every route | done for the windows below, in Python and in C (`src/present/drawlist.c`); unexercised branches listed below |
| 2 | Re-draw the list at N times the resolution | N = 1 is Stage 1 exactly; at N > 1 every N x N block agrees with the N = 1 pixel wherever no edge crosses it | model polygons re-drawn from their sub-pixel vertices (`hires_subpixel.py`, and in C `hires.c`, live with `--present-scale N`): N = 1 exact, flat agreement 99.96-100% before a guard, 100% after; text, sprites, HUD and 3-4% of the polygons stay scaled |
| 3 | Interpolate between consecutive draw lists to the host display rate | at a logic frame the output is exactly that frame; no in-between primitive absent from both neighbours | 320x200 study done (`interp_frame.py`, below): exact at both ends on seven windows, no violations; live path built at 320x200 (below): the feed, the presentation from the replay, and the interpolation in C, exact at both ends of 9,843 live frame pairs |
| 4 | Pacing, vsync, a picture-age setting, HUD handling, a switch to the original picture | pictures at the display's refresh with t on the mission clock; the age setting measured | pacing at the host's vsync and the age setting built (below); HUD handling is the owner's choice (options below); `--present scan` is the switch |

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
`M` colour replace (entry 41), `X` any other entry, `x` changed byte deltas, `J` the display palette (768 six-bit DAC
components, logged beside `Y` with `F117R_OBSERVE_PAGES=1`).

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
  were drawn at), the present (44) and the colour replace (41, driver 1188: in a
  rectangle x0..x1, y0..y1 of a page from the page table, every byte equal to one
  colour becomes another; rows from the row table, no origin; the HUD and the
  cockpit's displays use it on the display every phase). Entry 46 writes only
  the DAC and CRTC start, so its byte deltas are the fill's own rows. Entries 24
  and 26 set the origin.
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

### The replay in C

`src/present/drawlist.c` is `drawlist_frame.py` in C, rule for rule: it is fed one record at a
time (`drawlist_record`, a log line's values without the clock) and draws on the pages it was
seeded with (`drawlist_seed`, the work page and the display as the observer dumps them). It reads
nothing from the machine. Where the Python replay copies a page copy's logged source bytes (the
present, entry 44), the C replay uses its own copy of the source page when it holds it, so the
display check also covers the work page at the instant of the present; `prefer_logged` does as
the Python does.

`build/test_drawlist.exe LOG [--carry] [--logged]` replays a log and prints
`drawlist_frame.py`'s lines, so the two outputs diff. `--carry` seeds each page once, from its
first dump, and carries the replay from phase to phase, as a live replay must. On eight 30M
windows (strike 8.53B and 8.70B, landing 9.70B and 9.89B, `airair_type6` 5.08B and 5.23B,
`airair_type5` 6.793B, a night take-off roll 2.50B; 447 phases), the output is identical to the
Python replay's in all three modes, and every phase is exact on both pages, carried too: seeded
once, the replay stays exact for the whole window, so what the original draws on the two pages
in flight is all in the records. A log of 75 MB replays in under a second.

Everything in the records is replayed by a rule. Until graphics entry 41 (the colour replace)
was decoded, the display leaned on its logged byte changes ('x'): it draws on the display in
every phase of the strike and air-to-air windows, and without its records 0 of 69 (strike) and 1
of 57 (`airair_type5`) display phases were exact. With its rule ('M'; 0 to 330 a window), the
same eight windows, re-recorded, are exact on both pages in every phase, seeded each phase or
carried, with every 'x' record removed from the logs, and the C and Python outputs are
identical; dropping the 'M' records instead leaves 0 of 69 and 1 of 57 display phases exact.

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

### Sub-pixel re-projection (8 October 2026)

`tools/hires_subpixel.py LOG [N] [-] [--grow G] [--crops DIR] [--no-guard]` fills each model polygon
from its true geometry on the N-times grid, over the same replay as `hires_frame.py`:

- **Vertices.** An `E` edge record names its slot; the `G` record of that slot names the two vertex
  records; each `V` record has the camera-space x, y, z and the pixel the original made. The projection
  (`0x129A2`) is `x >> 8` over the high word of z (or `x >> 1` over `(z >> 8) >> 1` below 0x100): every `V` record
  of a strike window reproduces under that formula (13,402 of 13,402). The divide's remainder is the sub-pixel
  position; the divide truncates toward zero, so a vertex left of the centre is moved into the pixel
  the original chose (the position is `px` plus the remainder taken floor-wise), which keeps every vertex inside its
  own coarse pixel. The origin ([8602], [8604]) is 159, 52 in these windows, read from the polygon's
  unclipped edges.
- **Fill rule.** A fine pixel is painted when its centre is inside the true polygon (left and top
  inclusive: two polygons sharing a vertex pair compute the same crossing, so they tile), and inside the original's
  own footprint for that polygon (the rows its fill painted); a coarse pixel with all eight neighbours in the
  footprint is covered whole. The clip is therefore the original's own: the footprint carries its borders,
  and nothing is added outside it. The polygon's overshoot is what the original's inclusive spans add to
  the true shape (both end pixels of every edge row count): it is measured per polygon at N = 1 as the least of
  0, 0.5, 1, 1.5, 2, 3 pixels with which the true shape covers the whole footprint, and kept at that many
  pixels of the grid being drawn. At N = 1 the footprint is exactly the original's, so the picture is Stage 1's
  (25/25, 31/31, 18/18, 59/59 and 20/20 display pages equal on the windows below); at N > 1 an edge lies within
  that many fine pixels of its true place (0.5 for 90% of the ground windows' polygons, and for 47% in the air-to-air window with the missile, where 1 takes
  most of the rest). A pure
  top-left rule (`--grow 0`) tiles without overshoot but differs from the original at N = 1 in 0.7% of the pixels.
- **Left scaled** (counted by the tool): polygons with a near-clipped edge (the crossing is the original's own
  approximation, x/128 at a plane at z of about 0x10000, which is no sub-pixel geometry), with a vertex behind the
  eye, with a footprint more than 3 pixels from its geometry (edge-on slivers at the horizon, border runs),
  and fills in a style with no replay rule. Text, sprites, lines, blits and the HUD are scaled copies.

Windows (30M instructions from just after `ordnance.pic`, except landing at 9.90B and airair_type6 at the AMRAAM launch
5.092B; `F117R_OBSERVE_PAGES=1`), N = 2 / N = 4. "Flat" is the stage's agreement over the display pages; "rule alone" counts it before the guard
(which restores the value on a flat block that disagrees); "unlike scaled" is the share of the fine picture differing from
the plain scaled copy after the guard, beside the floor mode's (`hires_frame.py --floor`) and the first build's walk:

| window | polygons (refilled) | flat, rule alone | guard restores | unlike scaled | floor mode | first walk |
|---|---|---|---|---|---|---|
| strike | 3,125 (3,021) | 100% / 100% | 0 / 0 | 0.245% / 0.450% | 0.127% / 0.144% | 84.9% flat, 8.4% unlike |
| recon | 2,503 (2,333) | 100% / 99.966% | 0 / 281 | 0.114% / 0.233% | 0.077% / 0.081% | 80.3% flat, 11.2% unlike |
| landing (9.90B) | 1,479 (1,434) | 100% / 100% | 0 / 0 | 0.178% / 0.242% | 0.059% / 0.060% | 78.0% flat, 10.8% unlike |
| air-to-air type 6 (5.092B) | 3,522 (3,426) | 99.997% / 99.988% | 35 / 137 | 0.058% / 0.118% | 0.020% / 0.022% | 99.7% flat, 0.2% unlike |
| air-to-air Central Europe | 4,603 (4,401) | 99.965% / 99.956% | 187 / 238 | 0.310% / 0.568% | 0.112% / 0.122% | 85.0% flat, 8.0% unlike |

The picture differs from the scaled copy by 1.5 to 3.0 times what the floor mode changes (N = 2; recon is the
weakest at 1.5) and 2.9 to 5.4 times (N = 4), and the difference grows with N, as real extra detail should; the first walk's large figure is
cracks, not detail. The residue of the flat disagreements (0.00-0.04% of the flat pixels) is the horizon: a ground
polygon whose true top lies half a coarse row below the row the original's inclusive spans started it in, while
the pixel above it in the original is the same ground colour (drawn by an earlier phase): the fine row between them
keeps the sky. They are restored by the guard and counted, not hidden. Side-by-side crops (scaled copy,
floor mode, this rule, and where the rule differs) are written by `--crops DIR`; edges of the hills, the ground
bands and the horizon are the visible gain, and the rest of the picture is unchanged.
The pass takes about 5 s a window at N = 2 in Python (only the 0.1-0.6% of pixels near an edge change), so the
cost of the polygon stage is small; what is not cheap is everything still scaled (the HUD and its text, sprites and
the cockpit art, which are most of the picture's remaining coarseness).

### Sub-pixel re-projection in C (9 October 2026)

`src/present/hires.c` is `hires_subpixel.py`'s builder in C, and `drawlist.c` gained the fine picture
`HiPage` keeps (`drawlist_set_scale`: every write mirrored as an N x N block unless the page is paused
while a refilled polygon's rows arrive; blits and page copies between held pages carry the fine rows).
`build/test_hires.exe LOG N [--carry]` prints the study's report; on the eight windows of the C replay
(re-recorded with entry 41) it is identical to `hires_subpixel.py`'s at N = 1, 2 and 4 (24 of 24).
At N = 1 every display page is the replay's own, seeded each phase and carried (447 of 447 both ways).

| window | polygons (refilled) | N = 2: flat, rule alone / guard restores / unlike scaled | N = 4 | N = 10 (C only) | N = 2 carried |
|---|---|---|---|---|---|
| strike 8.53B | 2,601 (2,347) | 100% / 0 / 0.022% | 100% / 0 / 0.033% | 99.999% / 13 / 0.040% | 99.999% / 16 / 0.286% |
| strike 8.70B | 3,022 (2,882) | 100% / 0 / 0.015% | 100% / 0 / 0.027% | 100% / 0 / 0.034% | 100% / 2 / 0.076% |
| landing 9.70B | 4,916 (4,876) | 100% / 0 / 0.216% | 100% / 2 / 0.403% | 100% / 2 / 0.532% | 100% / 0 / 0.216% |
| landing 9.89B | 2,585 (2,498) | 100% / 0 / 0.177% | 100% / 0 / 0.243% | 100% / 0 / 0.306% | 100% / 0 / 0.177% |
| air-to-air type 6, 5.08B | 3,305 (3,204) | 99.997% / 37 / 0.054% | 99.989% / 137 / 0.115% | 99.981% / 229 / 0.159% | 99.994% / 75 / 0.443% |
| air-to-air type 6, 5.23B | 3,037 (2,704) | 100% / 1 / 0.057% | 100% / 1 / 0.114% | 100% / 5 / 0.154% | 100% / 2 / 0.324% |
| air-to-air type 5, 6.793B | 4,111 (3,771) | 100% / 2 / 0.060% | 99.997% / 32 / 0.104% | 99.997% / 38 / 0.137% | 99.990% / 116 / 0.348% |
| take-off 2.50B | 139 (133) | 100% / 0 / 0.000% | 100% / 0 / 0.001% | 100% / 0 / 0.001% | 100% / 0 / 0.000% |

"Carried" seeds the pages once, as the live replay does, instead of from each phase's dumps: the fine
detail of what a phase does not redraw then stays, so more of the picture differs from the scaled
copy, and the horizon residue the guard restores is counted over the carried picture.

### Stage 2 live (9 October 2026)

`--present-scale N` (`f117run`, `f117a`, `present-scale =` in `f117a.ini`; off by default) runs the
sub-pixel builder beside the live replay and presents the N-times picture with the scan's palette
(the shots and `--screen` are 320N x 200N). The study's guard is applied to every picture shown. With
interpolation, an in-between frame moves the vertices of every paired batch of equal size too
(`INTERP_VERTICES`: camera space, projected by the original's divide), so a moved polygon is
refilled from moved geometry; the replay state carried for the in-between frames includes the
builder's. Strike, the whole flight, `--present interp` (checked at t = 1e-6 and 1 - 1e-6 at N = 1;
with a scale at 1e-9, since 1e-6 of a step can move a camera-space coordinate by a rounding unit,
which a finer grid shows: 5 of the 9,843 pairs at N = 2, 50 at N = 9):

| N | polygons refilled from vertices | flat coarse pixels kept on all N x N fine pixels (rule alone), over the 9,845 logic frames | pairs exact at both ends, coarse and fine |
|---|---|---|---|
| 1 | 378,961 of 435,826 | 100% (247,579,281) | 9,843 of 9,843 |
| 2 | 378,961 | 99.998% (4,898 restored by the guard) | 9,843 of 9,843 |
| 4 | 378,961 | 99.994% (15,765 restored) | 9,843 of 9,843 |
| 9 | 378,961 | 99.990% (24,720 restored) | 9,843 of 9,843 |

At N = 1 the presented picture is the replay's own; the hashes are unchanged at every scale
(`boot_to_flight` and strike, 54 and 177 checkpoints). N = 9 is the choice for a 4K screen: the
picture is 2880 x 1800, and shown 4:3 (each row 1.2 times as tall) it fills the 2,160 lines.

## Stage 3: interpolation, a 320x200 study (8 October 2026)

`tools/interp_frame.py LOG` (`--check`, `--pairing`, `--primitives`, `--geometry`, `--predict`,
`--strip DIR`) pairs each logic frame of an observer log with the next and builds in-between frames
with the Stage 1 replay itself (`drawlist_frame.main`, fed a synthetic list; no replay rule is
copied). Windows are 30M instructions, taken as in Stage 1.

**The logic frame.** A step is not always two phases. The phases that present (graphics entry 44)
close a frame: with a sensor display up (a Maverick's MFD) a step is two phases (the main scene in
the 64 KB page at origin 0, then the inset scene, the HUD text and the present, at another origin),
otherwise one. Measured at 9 MIPS: 8.8-10.6 steps a second in the strike and air-to-air windows,
10.1 and 13.6 in the landing windows, 22.8 in a night take-off roll.

**Pairing.** The projection delivers a model chunk's vertices to one buffer from `C6B4` (a *batch*:
one run of `V` records); edges are numbered inside it (`G` slot, 18h apart from `5EEA`) and a
polygon is the list of edge slots its `E` records carry. So identity is (batch, edge slots):
1. batches are aligned between the frames in order by longest common subsequence. A pair needs an
   equal vertex count, or (chunks of three vertices or more) a count within 10% with 70% of the
   shared leading vertices within a quarter of their depth of each other in camera space, and a
   mean screen motion under 60 px; look-alikes go to the smaller motion;
2. painted polygons inside a paired batch pair by equal edge-slot lists (LCS), colour ignored (the
   skeleton's colour is used);
3. span fills (`Q`) pair by the colour set before them, their mode and their order; outline edges
   (`L`) by batch, slot and colour; HUD library lines (`N`) by colour, page and order when the group
   has the same size in both frames and the line moved under 30 px.

**Interpolation.** An in-between frame at t takes the nearer frame's list as its *skeleton* and
moves every paired primitive the fraction w of the way (w = t from frame n, 1 - t from frame
n + 1). Everything else (text, sprites, blits, ticks, an unpaired polygon, a polygon whose clip
status or fill style differs) stays as the skeleton has it, which is a switch at t = 0.5. A moved
polygon has its edges moved, its span rows rebuilt by the original's edge walk
(`drawlist_spans.Spans`, exact on all painted polygons) and is painted in the skeleton's fill style;
the present copies the replayed work page, not the bytes the original logged. Edges that are
unclipped and whose ends project from front-range vertices are interpolated in camera space and
projected by the original's divide (`project`, equal to the logged pixels for every vertex); the
others interpolate integer screen endpoints. Camera space is the right choice: of the edge ends of
polygons present in frames n, n + 1 and n + 2, 90.3% land exactly where frame n + 1 really has
them when n and n + 2 are interpolated in camera space (strike window), 79.8% in screen space and
73.0% holding frame n; on the 9.89B landing 98.0%, 95.4% and 95.3%. A frame pair is a *cut* (held
whole) when under half the polygons pair or the median vertex motion exceeds 40 px.

**Checks** (`--check`, `--primitives`; seven windows of 28 to 73 logic frames):
- the Stage 1 replay of every frame is exact on the work page and the display, and so is the
  machinery at t = 1e-6 and 1 - 1e-6 (every paired polygon, span fill, outline edge and HUD line
  regenerated and moved by almost nothing): every frame of all seven windows, bit for bit;
- provenance: every record of an in-between list is a record of frame n or n + 1 or the
  regeneration of one paired in both (a moved polygon asserts it): 0 violations in 741 in-between
  frames. In 20 of them the display holds a colour index neither neighbour does (AND and OR fills
  over a changed background); the rule does not forbid that.

| window (instruction clock) | steps/s | polygons paired | cuts held | vertex motion per step, px (mean / median / p95 / max) | edge ends exact: hold n -> interpolated | display pixels an in-between frame changes |
|---|---|---|---|---|---|---|
| strike 8.53B | 10.6 | 91.1% | 0 / 33 | 2.6 / 0 / 9 / 308 | 73.0% -> 90.3% | 0.13% |
| strike impact 8.70B | 9.4 | 86.2% | 1 / 29 | 4.9 / 1 / 19 / 5165 | 25.3% -> 40.3% | 0.86% |
| landing approach 9.70B | 10.1 | 98.0% | 0 / 31 | 1.5 / 1 / 6 / 116 | 50.6% -> 66.9% | 0.38% |
| landing 9.89B | 13.6 | 95.6% | 0 / 33 | 0.6 / 0 / 1 / 202 | 95.3% -> 98.0% | 0.00% |
| AMRAAM launch 5.08B | 9.3 | 98.5% | 0 / 29 | 1.0 / 0 / 3 / 226 | 77.3% -> 83.0% | 0.06% |
| AMRAAM kill 5.23B | 8.8 | 97.6% | 0 / 27 | 2.3 / 1 / 12 / 292 | 64.2% -> 60.7% | 0.91% |
| take-off roll 2.50B (night) | 22.8 | 29.9% (87 painted) | 3 / 72 | 11.6 / 5 / 32 / 1002 | - | 0.18% |

The last column is the share of display pixels an in-between frame (t = 1/4, 1/2, 3/4) differs from
its skeleton frame's own picture: at 320x200 an in-between frame is nearly its neighbour, since
vertices move a pixel or two a step and an in-between frame moves a fraction of that. The motion
is not uniform either: in the kill window frames n and n + 1 are often equal and n + 2 jumps, so
the midpoint of n and n + 2 is worse than holding n for those edges (the 60.7%); that is a limit
of the test, not of the moved edges. The night take-off roll has almost no filled polygons (the
HUD, the moving map and outline edges carry the motion); its three cuts are frames with 0-5
polygons.

**What is held and why** (painted polygons not moved, 1.5% to 14% by window): a polygon with no pair
is a model chunk with no partner: an object entering or leaving, a chunk whose vertex count
changed by more than a tenth, one drawn as a single chunk in one step and two in the next (the
MFD's target model, 87 + 82 against 169 vertices), an explosion's chunks. A clip status that
differs (an edge crossing the window or the near plane between steps; 6-141 polygons a window)
would change the span rows by rules this study does not redo. An HUD group that changes size (the
heading tape adds and drops ticks) holds, and every `T`, `S`, `W`, `C`, `H` and `D` record. The
strips in `D:/f117-gate/p3-stage3/png` show it: numbers and text change at the middle, the
Maverick's "FIRED" appears there, target boxes jump, and in the impact window the terrain
silhouette's detail changes at the middle while its position moves smoothly.

**What would need a smarter match:** models drawn in a different number of chunks from step to
step (the vertex stream, not the batch, would be the identity); polygons that gain or lose an edge
to the near plane (the clipper, `model_prepare_edge`, redone); explosion and tracer sprites, which
cannot be paired and should switch.

**What the live path would take** from the machine each logic step: the batches (camera-space
vertices, ranges and the edge table), each painted polygon's edges with status words and clip
data, fill style and colour, the span fills, outline edges, HUD lines and every other draw record
in order, with the page each draws to, and the palette: what the observer logs now without the
page dumps, plus the cockpit art read once. The host keeps two consecutive steps, pairs them (the
alignment is a few thousand table cells a step) and draws the display frame at host time h from
the pair (n, n + 1) with t = (h - t_n) / (t_n+1 - t_n) taken on the mission clock, one step
behind. It needs a rasteriser for the model polygons, lines, spans and the other primitives:
Stage 1's replay ported, or Stage 2's if the picture is to be finer, since only on a finer grid
does motion of a fraction of a pixel show. It stays an observer: the machine is asked for nothing
it does not already compute.

## The live path, first part: feed and replay (9 October 2026)

What the live path takes from the machine, in process:

- **The feed** (`src/present/drawfeed.c`) is an observer that keeps every record in memory, a
  logic frame at a time: a frame closes at the first phase after one that presented (entry 44),
  as `interp_frame.py` groups them, and waits in a ring of 8 until the host takes it. At the
  close it copies A000 into the frame, which is what the replay must equal. Two observer fields
  serve it (`observe.h`): `sources` has the records carry a blit's and a page copy's source
  bytes, as `F117R_OBSERVE_PAGES` gives a log, and `want_pages` asks for the page dumps at one
  phase, which the feed does once to seed the replay (and again after a frame differs or one is
  dropped). The feed takes no page snapshots: since entry 41 is decoded every drawing entry has a
  rule, and only a log keeps the pages through each graphics entry, for the byte changes ('x')
  the rules are checked against. Entries 13-16, 24-26, 45, 46, 62 and 65 have no rule; they set
  state (pages, origin, DAC, CRTC start); in the eight windows only entry 46 has byte changes, and
  they are the model fill's own rows (the fill calls it while it paints), which the replay draws
  from the 'b' records, so they need no snapshot either. Were one to draw, the frame would
  differ from A000 at its close and the replay would be seeded again.
- **The replay** (`drawlive`, same file) runs `drawlist.c` on each closed frame, carried from the
  one seed, and compares the display with the frame's copy of A000. A frame that differs is not
  shown and the replay is seeded again.
- Where a matched routine is placed at an observer hook's address, the matched routine runs and the
  hook does not (the first override placed at an address is the one called, and the matched routines
  are registered first). Phase 2 placed `vgame_model_edge_spans` (130D:004A) and
  `vgame_model_poly_finish` (130D:0116) on the hooks that log a polygon's edges ('E') and its fill
  entry ('F'), so from that merge until the fix below the feed had neither: the replay stayed exact
  (the fill rows carry the pixels) and the interpolation's checks passed, but it paired no polygon.
  The two matched routines now call the observer at their entry, as the projection and
  `model_prepare_edge` do for 'V' and 'G'; the strike 8.53B log is again byte for byte the one
  recorded before the merge.
- The observer now reads guest memory directly, never through `mem_read8`, which charges the 386
  profile's VGA bus cycles: under `--timing 386` an installed observer's reads of A000 would have
  moved the clock. Its page snapshots are `memcpy`s; the logs it writes are byte for byte those
  of the earlier observer (strike 8.53B, landing 9.89B).

**Unchanged machine.** `boot_to_flight` and `strike`, each under both engines, with and without
`f117run --present replay`: the `--hash-every 50000000` checkpoints (54 and 177) and the final
hashes are identical, and equal between the engines. The observer's hooks exist only in the
recompiled engine, so under the interpreter the feed sees nothing.

**Cost.** The recompiled strike route (8.85B instructions), the runner's CPU time, least of three
runs one at a time:

| presentation | with page snapshots (entry 41 undecoded) | without (9 October) |
|---|---|---|
| scan | 78.8 s | 78.8 s |
| replay | 158.6 s (+101%) | 106.7 s (+35%) |
| interp | 179.2 s (+127%) | 121.5 s (+54%) |

The host's speed varied by a third between runs that day (the scan route took 78.8 to 104.1 s),
so only figures from the same set compare; an earlier wall-clock set gave 66.6, 100.0 and
111.8 s. At 106.7 s the replay still runs at 83 M instructions a second, nine times the original's
9 MIPS. What the feed still costs is the records themselves (every vertex, edge and fill row, the
fill rows with their page bytes) and the copy of A000 at each frame's close.

**The replay live.** Over the whole strike flight (2.33B to 8.85B) 9,845 logic frames close; the
replay, seeded once at the first, equals the display at every frame's close: 9,845 of 9,845, none
dropped. `boot_to_flight`: 368 of 368.

**Presenting from the replay** (`f117run --present replay`, `f117a --present replay`, or
`present = replay` in `f117a.ini`). In flight the screen is the replayed display with the scanned
frame's palette; elsewhere (another program, text mode, a CRTC start other than 0, or no frame
closed for a quarter second) the scanned-out picture. A frame is presented from its close, so the
picture is up to one step later than the scan shows it. `f117run` checks it at every VGA frame
and prints a `[present]` summary; `--frame-log` gains a `replay` column, and `--screen` and the
shots write the presented picture. Strike, the whole flight: 68,918 VGA frames, 50,620 of them
presented from the replay; 36,632 equal to the scanned-out picture bit for bit, 5,406 a scan of
the next logic frame (equal to the next replayed frame: the replay presents it at its close),
8,582 a scan taken while the original was drawing the next frame on the display (the present
and the HUD drawn on A000 over several VGA frames), none unsettled; 9,843 of the 9,845 logic
frames are equal to a scanned-out picture at some VGA frame. With `--shots-vga` over the 8.53B
window, 189 of 234 shots are byte-identical to the scanned run's. The window, headless
(`SDL_VIDEODRIVER=dummy`, `SDL_AUDIO_DRIVER=dummy`) replaying `strike.input` to 3.5B, ends on the
same hash with `--present replay` as without and as `f117run` (`bc8e3ae5a872405d`); its log's
`[present]` line: 2,062 of 2,062 logic frames exact, 9,030 VGA frames shown from the replay.

**Interpolation** (`src/present/interp.c`) is `interp_frame.py` in C: the same parse into batches
and polygons, the same pairing (batch and polygon alignment, span fills, outline edges, HUD
lines), the same in-between list (polygons re-walked by the original's edge rules, camera-space
edges projected by the original's divide) drawn by the C replay from the pages at the skeleton's
start. `build/test_interp.exe LOG` prints `interp_frame.py LOG --pairing --check --primitives`'s
report; on the eight windows above it is identical, line for line: 273 frame pairs exact at both
ends (t = 1e-6 and 1 - 1e-6), 819 in-between frames, no provenance violation.

Live, `drawlive` keeps the last two replayed frames, each with the replay as it stood at its
start, and pairs them as the newer closes. `--present interp` (`f117run`, `f117a`,
`present = interp`) draws, at each VGA frame, the in-between frame at
t = (now - close of the newer) / (close of the newer - close of the older) on the machine's clock,
which paces emulated time against real time: the older frame at the
newer one's close, the newer one a step later, so the picture is a step behind the replay's.
`f117run` also draws every pair at its two ends and compares them with the two replayed
pictures. Strike, the whole flight: 9,843 of 9,843 pairs exact at both ends; of 50,620 VGA frames
presented, 47,708 are in-between frames; the hashes are unchanged (`boot_to_flight`: 366 of 366
pairs). Drawing an in-between frame at every VGA frame costs the recompiled strike route a
further 14% of the scan route's time (the table above). The window, headless, to 3.5B with `--present interp`: the same hash
again, 8,332 of its 9,030 replayed VGA frames in-between frames.

**Left for the live path.** Pacing to the host's display rather than the emulated VGA's (the
in-between frames are drawn at the VGA's 70 Hz) and Stage 2's finer grid. The camera-space vertices of a frame are
in its records ('V'), so a finer grid needs no more from the machine.

## Stage 4: pacing and the picture's age (9 October 2026)

**Pacing.** With `--present interp`, `f117a` draws the in-between frame in its main loop just before
it presents (with vsync), at the machine's clock at that moment, instead of at the emulated VGA's
retrace; the machine's clock is kept level with the wall clock, so t follows emulated time at
the host display's own rate. The faster mission clock is separate from that time base.
`--present-log FILE` writes, for every frame presented, the host clock
after the present, the machine's clock, the two logic frames shown and t. No visible window was
opened: SDL's dummy video driver, which paces presents at 60 Hz in software, and the dummy audio
driver. `strike.input` to 3.5B (the first 1.2B instructions of the flight), one run at a time;
every run ends on the hash `f117run` has there (`bc8e3ae5a872405d`):

| run | frames presented (from the replay / in-between) | host frame interval, median / p99 / max (ms) | refreshes missed (over 20 ms) | machine clock against the wall clock at a present, p99 | host frames a logic step is shown over (mean) |
|---|---|---|---|---|---|
| scan | 23,336 | 16.667 / 16.668 / 17.8 | 0 | - | - |
| interp, N = 1 | 23,336 (7,733 / 7,429) | 16.667 / 16.668 / 23.2 | 1 | 0.026 ms | 3.75 |
| interp, N = 2 | 23,336 (7,733 / 7,425) | 16.667 / 16.668 / 17.0 | 0 | 0.029 ms | 3.75 |
| extrapolate, N = 1 | 23,336 (7,733 / 7,722) | 16.667 / 16.670 / 23.9 | 1 | 0.025 ms | 3.75 |
| interp, N = 4 | 12,623 (4,765 / 4,567) | 16.667 / 151 / 396 | 3,113 | fell 2.3 s behind | 2.57 |
| replay, N = 4 | 17,255 (9,030 / 0) | 16.667 / 99.8 / 147 | 2,344 | - | - |

Within a step t only advances (5,671 advances at N = 1, none backwards), and every frame of a step
is drawn at the clock it is presented at. At N = 4 the misses come from showing a 1280 x 800 picture
through the dummy driver's software renderer (the replay alone, drawing no in-between frame, misses
as often), not from drawing it: `f117run` draws an in-between frame at N = 4 in about 3 ms. The first
extrapolate run had a 628 ms stall: a span fill carried past its two positions left the screen by
tens of thousands of pixels and the span rule walked them; a prediction now keeps span fills on the
screen (interpolation never leaves the range between two real fills, so the study is unchanged).

What needs the owner's eyes on a real display: whether frames are steady at the monitor's refresh
(vsync on a GPU renderer, not the dummy driver's software pacing), the look of motion at 60 and
144 Hz, and the window at 4K.

**Picture age** (`--present-age interp|extrapolate`, `present-age =`; `interp` by default).
`interp` shows the in-between frame of the last two logic frames: exact at every logic frame, one
step (about 110 ms) behind. `extrapolate` shows each frame from its close and moves the paired
primitives on past it as they moved from the frame before (t from 1 to 2): no age added, but the
picture between closes is a prediction, and it jumps to the real frame at the next close. Its error
just before that close, the prediction a whole step on against the frame the original then draws
(`test_interp LOG --extrapolate`), beside the frame before held (what `interp` shows then, late but
real), share of pixels that differ:

| window | 3-D window: extrapolated / held | whole display: extrapolated / held | prediction nearer / as near / further |
|---|---|---|---|
| strike 8.53B | 1.86% / 1.76% | 2.94% / 2.86% | 7 / 0 / 24 |
| strike 8.70B (impact) | 26.2% / 23.4% | 16.3% / 14.7% | 4 / 0 / 23 |
| landing 9.70B | 14.5% / 10.7% | 7.8% / 5.7% | 3 / 0 / 26 |
| landing 9.89B | 0.49% / 0.45% | 0.26% / 0.24% | 7 / 5 / 19 |
| air-to-air type 6, 5.08B | 2.56% / 1.73% | 2.12% / 1.58% | 0 / 0 / 27 |
| air-to-air type 6, 5.23B | 20.1% / 17.9% | 12.2% / 10.9% | 2 / 0 / 23 |
| air-to-air type 5, 6.793B | 1.22% / 1.06% | 2.04% / 1.88% | 4 / 0 / 21 |
| take-off 2.50B | 6.50% / 6.12% | 3.52% / 3.30% | 16 / 26 / 28 |

The prediction is further from the real frame than the old frame is in most steps (191 of 265),
most where the scene turns or jumps (the impact, the approach): the original's motion between steps
is not steady (a step often repeats a position and the next jumps, Stage 3), and a pairing that is
harmless between two real positions is carried past them. So `interp` stays the default;
`extrapolate` trades a misprediction, corrected with a jump at each close, for the step of latency.

**Cost of the scale** (the strike route, recompiled, the runner's CPU time, one run at a time; the
check draws each pair twice more): scan 95.4 s, replay 120.8 s, interp 148.1 s; interp at N = 2
253.2 s, N = 4 345.2 s, N = 9 645.1 s; replay at N = 9 166.4 s. Over the 67,394 in-between frames the
run draws, one costs about 1.6 ms more at N = 2 than at N = 1, 2.9 ms at N = 4 and 7.4 ms at N = 9
(the fine pages copied and redrawn); showing it then means 5.2 million pixels at N = 9 through the
palette and up to the GPU each frame, which with the in-between frame is on the edge of a 60 Hz frame
on one thread.

**The HUD, text and sprites at 4K: the owner's call.** They stay scaled copies (each original pixel
a 9 x 9 block at N = 9). The sources they could come from instead, with what each costs:

| source | what changes | cost |
|---|---|---|
| scaled copies (now) | nothing: the original's pixels, large | none |
| the library's line, tick, span and colour-replace records redrawn on the fine grid | the HUD ladder, tapes, boxes and markers become thin and sharp at the same places; a line one original pixel wide becomes one fine pixel, unless drawn N wide (a choice of look) | a few hundred lines of C beside the existing rules, held by the N = 1 identity and the flat-pixel check; little run time |
| a pixel-art upscaler (Scale2x/EPX, xBR) on text glyphs and sprites | the original's shapes, smoothed diagonals; deterministic | small for glyphs (cached per glyph); for sprites and the cockpit art per draw, or cached per source image |
| an upscaler on the whole non-polygon layer | one step for everything not refilled | needs the layers kept apart; tens of ms a frame at 4K on one thread, so a GPU shader or threads |
| new high-resolution fonts and art | the look of a remaster, not the original's pixels | artwork and licensing; outside this project's own sources |

## Open questions and risks

- **Pairing for interpolation.** Done for 86-99% of painted polygons by batch alignment
  (Stage 3 above); what is left is chunks whose vertex count changes by more than a tenth or
  that are split differently between steps.
- **Things that are not smooth** (explosions, tracers, the HUD, a cut between
  views) must be left alone: hold the previous frame or switch on the logic
  frame.
- **The wall.** If the original's rasteriser depends on state not in the draw
  list (a clip window held in a register, a dither phase), the replay misses
  pixels; that is a finding that says what else to record, not a failure of
  method.
- **Time base.** Interpolation and host pacing use the machine clock. The
  original's faster mission clock is set by the speed: the default (20 million a second)
  puts the world at about 1.12 times real time (`docs/speed-sweep.md`).
- **Is sub-pixel "4K" worth its cost** against the cheaper scaled picture? For the 3-D scene the polygon stage is
  cheap and verified (above); what remains is whether the HUD, text and sprites also need a finer source than their
  scaled copies, and the near-clipped polygons (3-4% of the polygons, no sub-pixel rule yet).
