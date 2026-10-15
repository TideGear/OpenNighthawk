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
| 1 | Draw lists: a frame's primitives as a list, replayed at 320x200 | the replay reproduces the original's work and display pages bit for bit, every phase of every route | done: all 45 complete routes and staged original-instruction fixtures for rare branches; C and Python fill rules agree |
| 2 | Re-draw the list at N times the resolution | N = 1 is Stage 1 exactly; at N > 1 verified geometry has smooth boundaries and keeps the original viewport, fill styles and artwork | model polygons re-drawn from their sub-pixel vertices (`hires_subpixel.py`, and in C `hires.c`, live with `--present-scale N`); HUD lines/ticks redraw on the fine grid with scale-proportional or chosen thickness; original bitmap text/artwork stay scaled by the owner's choice; verified near-plane crossings now refined, incompatible geometry safely scaled; the HUD tapes, heading strip and pitch ladder are drawn from the game state, not from the original's rounded calls ([below](#the-state-driven-hud-14-october-2026)), and so are the sky and ground bands ([below](#the-state-driven-sky-and-ground-bands-15-october-2026)) |
| 3 | Interpolate between consecutive draw lists to the host display rate | at a logic frame the output is exactly that frame; no in-between primitive absent from both neighbours | 320x200 study done (`interp_frame.py`, below): exact at both ends on seven windows, no violations; live path built at 320x200 (below): the feed, the presentation from the replay, and the interpolation in C, exact at both ends of 9,843 live frame pairs |
| 4 | Pacing, vsync, a picture-age setting, HUD handling, a switch to the original picture | pictures at the display's refresh with t on the mission clock; the age setting measured | pacing at the host's vsync, age setting and Reimp-style HUD handling built (below); `--present scan` is the switch; real-display checks remain |

Interpolation shows the picture one logic step behind (about 110 ms at 9
steps a second, 67 ms at the clamp of 15), the standard price. Stage 1 needs
the drawing path understood, which is what the matched routines in Phase 2
supply, so the two phases are not independent.

## Fill styles and rare driver branches (13 October 2026)

All model fill styles now replay by rule. Dither alternates the colour
word's bytes by logical row/column parity; stipple paints the opposite-parity
cells and preserves the background in the others. The clipped column, not
the destination's byte alignment, determines the pattern. Post-fill `a`
records are evidence only. The fine renderer keeps each pattern cell the
size of an original pixel and refines the polygon's boundary; interpolation
can move these polygons too. C and Python use the same rules.

`test_present_fills` compares 2,619 staged cases with the original VGAME
instructions, including all fill style classes, clipped spans, odd page
alignment and N=1/4/9 mirrored output. Python also matches 2,187 exported
original rows. `test_present_driver` matches 3,953 staged cases against the
original resident driver: opaque/transparent text, variable/fixed fonts and
all text clips; transparent sprite clips; scaled RLE clipping, leading and
internal transparency, empty rows and both flips; both tick-scale variants.
These close the previously unexercised branch list below. They are synthetic
drawing fixtures, separate from normal-play route evidence.

The camera-plane polygon regression checks coarse-sized pattern cells over
an existing background at N=1/4/9, including transparent stipple, equal
colours and discard. It also checks moving dither/stipple polygons at both
interpolation endpoints and three intermediate times, with reused edge
slots. All 66 replay/fine/interpolation checks across 11 retained windows
pass; live boot N=4/9 each has 368 exact logic frames, 366 exact endpoint
pairs, unchanged guest checkpoints/final hash and zero dropped frames.

`tools/presentation_check.py` compares complete presentation routes with the
full gate's saved interpreter scan baselines. It checks every guest hash,
route milestone, closed logic frame and interpolation endpoint, rejects
dropped/unseeded frames, and requires positive pairs on flight routes.
The owner's target is **4K at 60 Hz**, a 16.67 ms host-frame budget. These
correctness checks do not establish physical refresh or motion quality.
Commands and retained evidence: [repeated-processes.md](repeated-processes.md).

Complete-route N=1 validation passes all 45 routes: 259,240 closed logic
frames and 259,166 interpolation pairs exact, all guest checkpoints/final
hashes equal to the interpreter scan baseline, and no dropped or unseeded
frames. This completes Stage 1's replay coverage. Stage 2's fill-style gap is
also closed; 4K/60 performance and display polish remain open.

The fresh full gate passes 58 tests, all 45 routes (44 fresh interpreter
sessions), both 5,718,848-state instruction profiles and three 831-address
matched seeds, with zero mismatches and passing reachability/event checks
(1,969 seconds). Private evidence: `D:/f117-gate/p3-styles`.

## Joined strokes, outline edges and span-rasteriser polygons (13 October 2026)

The owner's first 4K review found three kinds of roughness, each with its own cause.

**Strokes that did not join.** Strokes were cut square across their direction at both ends
and centred on a cell's corner, so two strokes meeting at an angle left a notch (the
canopy corner, the bearing-tape marker) and every stroke sat half a cell up and left of the
cells it stands for. They now run between the centres of their end cells with whole end
caps (see the line rule above). **Outline edges** (record `L`: ground markings, the weapon
target box) were still scaled blocks; they use the same stroke.

**The horizon and the sky and ground gradient.** These are polygons the game fills through its
own span rasteriser (fill_polygon, VGAME `0FB2:01E6`), not model polygons, so the fine
renderer only ever saw their span fills. A hook at that routine's entry now logs each
polygon's corners (`U`: colour, count, x and y words in the rasteriser's own coordinates,
which are far outside the picture for a horizon strip) before its span fill (`Q`). The fine
renderer validates the corners against the original span footprint. At N > 1 it draws their
cell-centred geometry with half a fine pixel of inclusive coverage, clipped to the span table's
bounds. A narrow region keeps its two boundaries; only a polygon with zero area becomes a
stroke. The earlier 60%-of-footprint rule made narrow regions into overly thick lines. A
polygon whose footprint is not within 3 pixels of its corners stays scaled. Interpolation
pairs `U` records by colour and order and moves their corners with the span fills.

**Smooth terrain and triangles.** Fine polygons used to be clipped row by row to the original
coarse footprint. That retained the original staircase and could cut teeth into a slanted
edge. Ordinary verified model polygons now clip to their viewport and draw the actual
geometry, with half a fine pixel of inclusive coverage to join adjacent faces. N=1 still
paints the exact original footprint. Near-plane polygons were clamped to their footprint with a
seam repair until 14 October; they are drawn from their verified geometry like the others (see
"Footprints the original's clipper gets wrong"), and incompatible geometry still stays scaled. The flat-cell
guard excludes verified fine geometry so it cannot put coarse stairs back. Dither/stipple cells, bitmap
lettering, cockpit art and map symbols remain the original artwork.

**The map is refined.** Its terrain is drawn once into the work page, then copied through the
cockpit-art page (segment `09C0`) and back every frame. The replay does not hold that art page.
A bounded cache of eight fine rectangles now carries those rows through the round trip,
restoring a rectangle only when its logged coarse bytes still match. It survives replay-state
copies and resets with the state; ordinary writes and changed source bytes retain the coarse
fallback. A display blit is also logged as an `x` byte-change record: for entries 42 and 46,
repeated bytes that already agree no longer flatten the fine rows the `C` record carried.

The prototype's cell displacement came from orthographic alignment: this projection rounds
to the nearest integer, which names a cell centre. Adding that half-cell alignment, rather
than treating the rounded coordinate as a corner, fixes the displaced bottom band and top
right tab. Flattening cells afterwards had left sawteeth and is not used. The map's triangles
now follow their geometry, including during the normal cockpit page-copy cycle.

`test_present_map` pins a straight orthographic triangle edge and its rounded lower vertex,
the fine rectangle's round trip through an unseeded page, independent ownership after a
replay copy, repeated display-blit bytes and stale-byte fallback, at N=1/4/9.
`test_present_strips` covers thick, narrow, degenerate and incompatible span polygons;
`test_near_polygons` checks that adjacent near-clipped faces leave no hole and that the near-clip tolerance accepts
and refuses what it should.
Fresh in-flight captures inspect the runway, map and banked terrain. Validation evidence is
kept outside the repository in `D:/f117-gate/map-refill`; current tallies are in the handoff.

## Fully clipped views, reused vertices and orthographic maps (12-13 October 2026)

The remaining horizon fallbacks in the corrected strike capture were not
missing geometry. The fine renderer inferred the origin from unclipped
edges, retaining the previous view's origin when every edge was clipped.
The recorded G snapshots already supplied the correct camera endpoints
and origin. Both C and Python now use those snapshots for ordinary polygons
too, avoiding stale V records when vertex storage is reused. Legacy short
records retain the existing path; near crossings still require exact
verification against the original.

The retained synthetic diamond crosses the viewport on every edge and
deliberately supplies stale, behind-eye V records. At N=1/4/9 the correct G
snapshots reconstruct it; the N=1 picture is exact, finer edges stay within
the original footprint, and legacy records retain scaling.

Recorded strike, landing and air-to-air windows pass at N=1/4/9. Strike now
refines all 17,058 evaluated polygons (previously 79 fallbacks); landing
refines all 4,916. Air-to-air refines 4,110 of 4,111. All 560 interpolation
endpoints and 840 intermediate pictures pass at each of N=4 and N=9.
Some intermediate polygons hold when their metadata or topology cannot
move together; these endpoint checks do not replace a visual motion check.

A full boot-to-flight capture found 27 further fallbacks: orthographic map
objects legitimately have Z=0, but G did not record the projection mode.
G now appends the byte at `[7D8E]` as field 18: zero for perspective,
otherwise the orthographic shift. Fields 1-17 remain unchanged. C and
Python reproduce the original high-word shift and rounding and require
both coarse coordinates to match the capture before using fine positions.
Shifts 16-31 retain full fixed-point precision; shifts 1-15 preserve the
original wrapped integer vertices and draw their edges on the fine grid.
Unknown modes and inconsistent coordinates retain scaling. Interpolation
preserves 17- and 18-field records and moves projection metadata only when
its mode agrees across the pair; a changed mode holds the snapshot.

All 196 orthographic endpoint projections in the fresh boot capture match
the original. Both live N=4/9 sentinels now refine all 48,856 model polygons,
preserve 368 exact logic frames and 366 exact endpoint pairs, and retain all
54 checkpoints and the final guest hash. Recorded boot replay also passes
at N=1/4/9. The observer regression checks field contents with ordinary,
VGA and wrapped memory placements while requiring unchanged CPU state,
instruction clock and memory. Synthetic tests cover signed rounding,
zero depth, low-shift wrapping, incompatible modes and corrupt projections.

The remaining air-to-air case has verified near crossings with extremely
large coordinates: one x=229,549 becomes signed -32,595 when the original
line clipper uses its low word. Its coarse silhouette differs substantially
from the full camera geometry. Keeping the original scaled polygon preserves
that arithmetic artifact; removing the compatibility check would change
what the original draws. Missing metadata in older or partial captures also
retains scaling. Text and cockpit artwork remain original by choice.
Real-display refresh, motion and 4K checks remain Phase 4 work.

Final integration checks (13 October): the earlier full gate passes 53 tests,
41 routes and both instruction profiles. The final observer revision repeats
all 53 tests and all 41 routes with 40 fresh interpreter sessions, zero
mismatches, passing reachability/event checks, in 1,271 s; unchanged
translation/matched coverage is reused from the earlier full gate.
Independent C/Python replay of the orthographic prefix agrees on all 45
polygons, 40,467 footprint pixels and 619,517 fine-grid paint pixels at N=4.

## Camera-plane reconstruction and shared seams (11 October 2026)

*Superseded on 14 October: near-clipped polygons are drawn from their geometry, so the coverage tracking and
seam repair described below (`drawlist_finish_models`, `DT_SEAM`) were removed; the crossing verification stays.*

The current C and Python renderers also refine near-clipped convex polygons.
G records carry both endpoints' camera coordinates, projected coordinates,
vertex identities, behind markers and projection origin. Legacy short G records
keep their existing fallback. `nearclip.c` repeats the original wrapped
ADD/ADC-plus-one midpoint and signed shift, including DEC DX's sign test,
then keeps x/128 and y/128 before the original discards precision. Each
crossing must reproduce the original retained coarse crossing exactly; a
mismatch or unsupported shape keeps the scaled picture. Two valid cuts close
the visible polygon. Observer-on/off controls preserve all guest hashes;
5,014 crossings across strike, landing and air-to-air match the original.

Interpolation binds each E to its preceding G within its batch, because slots
are reused with different vertex identities. Camera snapshots and retained
crossings move together only when the paired polygon moves and every snapshot
is compatible; changed origins, markers, identities or topology hold the
skeleton's records. Fine endpoint checks at N=4 and N=9 compare against each
neighbour's own fine rendering: 444 strike, 62 landing and 54 air-to-air
endpoints per scale, plus three intermediate pictures per pair, all pass.

Individual coarse footprints can disagree with the shared fine boundary.
Two adjacent clipped triangles exposed six interior holes at N=9, independently
reproduced in Python. Each page now tracks true fine geometry, actual model
paint (including black), and coarse model footprint unions. An unpainted fine
pixel inside both unions retains its original coarse owner's colour. Existing
paint and outer-edge detail survive, and no pixel is added outside the original
footprint union. The synthetic fixture restores exactly those six pixels;
N=4 needs none. HUD masks are respected. Page copies deep-copy coverage,
replacing primitives finalize and clear it, and reseeding resets it. Passive
observer diagnostics do not end a model layer. Finalization for viewing is
idempotent and keeps coverage available for a subsequent adjacent face.

Independent C/Python N=4 repair counts agree: strike 10,010, landing 1,534,
air-to-air 2,941. These compare geometry and repair counters; the older Python
HUD path does not establish whole-picture equality with the C fine HUD.
Final isolated tests pass 20/20. Live repaired N=4/9 sentinels both preserve
368 logic frames, 366 exact endpoint pairs, all 54 checkpoints and final hash
`48a10e901505e2a9`. Main integration's full gate passes 21 tests, all 35 routes, both instruction
profiles and three matched seeds, with reachability/event-limit checks. Difficult horizon
slivers and incompatible clipping retain scaling; real-display checks remain.
The following sections retain the earlier measurements and their limitations.

## Stage 0 and 1: the observer and the draw list

`src/matched/observe.c` hooks `game_draw`'s entry, VGAME's fill and outline
paths, matched/original projections and every graphics-library entry. It is a pure
reader: with it on, every checkpoint and final hash is unchanged.
`f117run --observe FILE:FROM:TO` writes the log; with `F117R_OBSERVE_PAGES=1`
it also dumps the library's whole page segment (`Z`, work page) and the
display (`Y`, A000) at each `game_draw`.

Original-path observer extension (13 October, integrated and gated): all five
projection return paths now emit V, and the original edge-preparation entry
emits G. Passive observers run in both engines and under the 386 profile,
including at addresses shared with matched bodies. They do not reset timing
bookkeeping. Orthographic V records use range 3, read the actual projected
pixels even at Z=0 and leave projection-mode reconstruction to G metadata.
The legacy perspective path rejects those records rather than inventing a
divisor; V-only interpolation holds them while compatible G snapshots move.

The default paired boot captures have all 217,022 V and 300,637 G payloads
identical, including far/mid/behind-eye/orthographic cases. All 54 checkpoints
and final hash match the previous baseline. Matched V timestamps charge RET
before recording; the original hook records just before RET, so this payload
comparison excludes their clocks. Under 386 both engines execute the original
projection: the complete 751,862,587-byte logs are byte-identical, including
446,978 V, 618,966 G, 80 checkpoints and final hash. The first 386 comparison
exposed a missing shared-address observer even though guest hashes matched;
the retained regression now checks that dispatch explicitly. Live N=4/9
rechecks preserve all 48,856 refined polygons, 368 exact logic frames and
366 exact endpoint pairs. These are automated checks; real-display assessment
remains Phase 4. The fresh full gate passes 53 tests, all 41 routes with 40 fresh interpreter
sessions, both instruction profiles and three matched seeds, zero mismatches
and passing reachability/event checks (1,661 s). Private audited verdict:
`D:/f117-gate/observer-original/final-gate-verdict.json`.

Record kinds: `P` phase, `V` projected vertex (camera-space x, y, z and the
pixels the original made of it), `G` edge prepared, `E` edge of a filled
polygon, `F`/`R` the polygon's fill and row spans, `b`/`a` fill rows (solid
or AND/OR/discard styles; `a` is dither/stipple), `B`/`L` outline polygon and
its edges, `N` library line, `K` colour, `Q` span fill, `C` blit, `T` text
(with its font, so the replay needs no game data), `S` sprite, `W` scaled RLE
sprite, `H` tick scale, `D` page copies (44 present, 48 copy, 79 dissolve),
`M` colour replace (entry 41), `X` any other entry, `x` changed byte deltas, `J` the display palette (768 six-bit DAC
components, logged beside `Y` with `F117R_OBSERVE_PAGES=1`). The state records `h`, `p` (`F117R_OBSERVE_HUD=1`) and
`o` (`F117R_OBSERVE_BANDS=1`) are described with the state-driven HUD and bands below. `F117R_OBSERVE_KINDS=LETTERS`
keeps only those kinds in the log (a corpus of a few kinds over a whole flight).

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

The original windows did not exercise dither/stipple, opaque/width-clipped
text, sprite clip edge cases or the tick scale's CL variant. The staged
original-driver checks above now cover those branches. Complete-route live
replay checks cover the rest of each scripted flight separately from these
phase-by-phase capture windows.

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

**The HUD, text and sprites at 4K (owner's choice, 10 October 2026): like the Reimp.**
HUD lines and tick marks are rasterised on the output grid; text glyphs, sprites and
cockpit artwork keep their original N x N pixel blocks. `--present-line-width N`
(`present-line-width =` in f117a.ini) selects 1–20 output pixels; 0, the default,
uses the render scale. At N = 1 the original replay is used exactly, including
its line quirks, regardless of the width setting. `--present scan` retains the
original picture at every scale.

The line rule began as the Reimp's `src/core/hires.c` (canonical endpoint order,
Bresenham on the output grid, a square nib), which matched it byte for byte on 360
strokes, but with that source's perpendicular end cuts. The owner's 4K review (13
October) found the cost: strokes that share an end left a notch (the canopy corner,
the bearing-tape marker), and each stroke sat half a cell up and left of the cells it
stands for. The rule is now Bresenham between the **centres of the end cells** with a
square nib and **whole end caps**: an axis-aligned stroke of the default width covers
exactly the original's cells, and joined strokes meet solidly. It departs from the
Reimp's strokes on purpose. The same stroke now draws the original's outline edges
(record `L`: ground markings, the weapon target box), which were scaled blocks before.
No Reimp code or asset is required at runtime. The graphics driver's already-clipped
endpoints and page origin come from the draw records; the observer never changes guest
state or timing.

Colour replacement, OR/AND/shaded spans and page copies operate on the fine
pixels already drawn. A small bit mask carries the cells crossed by fine strokes
through copies, so the polygon flat-pixel guard preserves deliberate HUD detail,
including wide strokes. Overlapping self-blits snapshot their fine source before
the coarse writes mirror over it. Bitmap text and sprite painting retain the
original per-pixel rules. `presentation_hud_lines` tests the coarse identity,
octants, reversal, dots, width, origins, guard, colour operations, artwork and copies.
The eight corrected `p3-live/logs41/w_*.log` flight windows pass carried replay
and N = 1/4/9 checks: 447 exact work phases and 447 exact display phases.
The live `boot_to_flight` sentinels at N = 4/9 each retain 368 exact logic
frames and 366 exact interpolation pairs, including fine endpoints; all 54
guest checkpoints match both fresh scan engines and each other, with final
hash `48a10e901505e2a9`. The full gate passes all 35 routes, 18 unit tests,
both instruction profiles, three matched seeds and event/reachability checks.
They took 146.9/112.8 seconds while sharing the machine with the gate; these
are correctness runs, not isolated rendering-performance measurements.
Both hosts accept widths 0/1/20 and reject -1/21/non-numbers/trailing letters.
`test_hires --line-width N --dump PREFIX` also renders
the last checked frame as coarse and fine PPMs using the recorded palette.

## Fullscreen 4K/60 timing and the threaded presentation (13 October 2026)

The flat-cell guard checks safe unaligned 8/4-byte groups and tails. A scalar
regression preserves every output byte and all repair counters at N=1..16, and
all 66 retained-window checks still pass.

**Where the time went.** A single-threaded N=9 strike run (fullscreen Direct3D 11,
3840x2160, 59.99 Hz, vsync) kept every logic frame exact and the guest hash
unchanged, but its flight pictures had p99 24.30 ms and a worst interval of
30.95 ms: the retained checker rejects p99 above 20.833 ms. Timers around each
stage showed why. Every frame in which a logic step closed took 21-25 ms,
because the replay of that frame at N=9 (6.4 ms) and its copies ran in that
one host frame; the other frames took 16.7 ms. Even without those spikes the
work outside the vsync wait had a median of 14.8 ms of the 16.67 ms budget:
the in-between picture 7.6 ms, palette conversion and texture upload about 3 ms,
the replay 2.3 ms and the machine 1.7 ms. Replaying records as they arrive
(below) took p99 to 22.6 ms; the rest had to leave the main thread.

**The threaded presentation** (`--present interp`; `src/present/drawfeed.c`,
`src/host/main.c`). The machine, the observer's feed and the replay stay on the
main thread; the in-between picture is drawn one host frame ahead on a worker.

- `drawlive_collect` replays the records of the open frame as they arrive (after
  every 1 ms machine slice) and, when a frame closes, stages it: the replay's
  start, the picture, its parsed records and its pairing with the step before
  it go into a free step from a pool of five. `drawlive_publish` then makes the
  staged steps the two published ones, in order, which is only pointer changes.
  `drawlive_update` is both, so the headless checks are unchanged.
- `drawlive_plan_interp` decides what to show (the older, the newer, the replay,
  or an in-between picture at t) from the published steps and the machine;
  `drawlive_render` draws it. The worker only reads published steps; the main
  thread publishes only while the worker is idle (after joining it, before the
  next plan), and a collect that finds no free step waits for a publish.
- Palette conversion of the 5.2 million fine pixels runs on three helper
  threads beside the main thread; the texture upload stays on the main thread.
- File-scope scratch that both threads reach (`row_at`, the polygon and span
  tables in `hires.c` and `interp.c`, the seed buffer) is thread-local (`tls.h`).
- Cost and meaning: the picture shown was planned a frame earlier, for the
  machine clock at that moment, so interpolation shows one host frame (16.7 ms)
  older than before, on top of the step it already shows behind. Extrapolation
  is a prediction anyway and plans a frame ahead. `F117_PRESENT_THREADS_OFF=1`
  runs everything on the main thread for comparison.

**Results** (`tools/presentation_timing.py`, isolated, one run at a time, fresh
save, dummy audio, Direct3D 11 3840x2160 at 59.990002 Hz, vsync on, N=9):

| scene | flight pictures | interval median / p99 / max (ms) | over 25 ms | logic frames exact | final guest hash |
|---|---|---|---|---|---|
| strike to 3.5B | 7,719 | 16.6677 / 16.7092 / 17.063 | 0 | 2,062 of 2,062 | `bc8e3ae5a872405d` |
| air-to-air type 6 to 5.9B | 19,000 | 16.6675 / 16.7091 / 21.3108 | 0 | 3,679 of 3,679 | `b050542466c9a616` |
| landing to 10.0B | 50,440 | 16.6677 / 16.7090 / 17.2537 | 0 | 13,408 of 13,408 | `efe080faa364f8f4` |

Both pass the checker (median within 10% of the refresh period, p99 within
125%, no interval above 150% of the period, guest clock within 100 ms of the
host; drift +18.8, +6.2 and +2.8 ms). In-between pictures are 96% of the pictures
shown. Each final hash equals a fresh headless run of the same input and stop count.

The strike flight also passes with the process pinned to four logical CPUs
(p99 16.71 ms, worst 16.82 ms, none missed, same hash), so the thread design has
headroom on core count. What this does not establish: the picture on a physical
panel (tearing, the look of motion), the cargo and career flights' timing, and a
slower CPU clock or weaker GPU than this machine's. The owner's motion review remains Phase 4 work.

`tools/presentation_timing.py` checks the logged display settings (a log that
changes display mid-run is rejected as ambiguous), intervals, interpolation and
guest drift; malformed or incomplete captures fail. `f117a.4k.ini` is an optional
ready-to-run fullscreen preset. Commands and limits are in
[repeated-processes.md](repeated-processes.md).

## Where the blockiness comes from: the pixel-origin diagnostic (14 October 2026)

`F117_PRESENT_DIAG=DIR` (with `--present interp` or `replay` and `--present-scale N > 1`; off when unset) gives
every replayed page a byte per fine pixel naming the path that last wrote it (`src/present/diagtag.h`: refined
model polygon, refined span-rasteriser polygon, each fallback by reason, guard repairs, strokes, text,
sprites, blits from pages the replay does not hold, copies). The plane follows page copies, blits and the map's
overlay cache as the fine pixels do. Beside each `--shots`/`--screen` picture, `f117run` writes `BASE.tag`, `BASE.diag.ppm`
(false colour), `BASE.holes` and `BASE.txt`; `F117_PRESENT_DIAG_HIST=FILE` appends the per-tag counts of a window
(`F117_PRESENT_DIAG_RECT=x0,y0,x1,y1`, default the 3-D view 0,0,320,91; `F117_PRESENT_DIAG_HIST_ONLY=1` skips the
images) followed by the window's two hole counts, `F117_PRESENT_DIAG_TRACE=FILE` logs each polygon refused for a
footprint beyond 3 px, and the final
`[diag]` lines print the builder's and the interpolator's counters. `tools/present_diag.py` makes the side-by-side
PNGs and class tables. Guest hashes and presented pictures are identical with it on (strike to 2.598B,
N=4: `a8bc78f294805e14`, four shots byte-identical).
The hole counts (`diag_holes`, `headless.c`) are over the window, in fine pixels: *unwritten* is a pixel no path
wrote in this frame (the seeded copy of an earlier frame shows); *stray* is a pixel a scene path wrote whose colour
is none of the nine coarse cells around its own, so no boundary shift of up to a cell explains it (a gap showing what
lies under the polygons, or geometry drawn where the original drew another colour). `BASE.holes` marks them (1, 2).

Measured before the fixes below, on the strike, landing and air-to-air type 6 flights (N=4, 3-D view, 144/167/126
frames sampled every 5 s of emulated time, 3-D window = 0,0,320,91):
the view was 88 to 89% refined geometry (model polygons 9 to 34%, sky/ground span polygons 55 to 83%), strokes 2.5 to 5%,
text 2%, canopy-frame sprites 3.4%, the rest scaled fallbacks. Two things left visible blocks.

- **Near-clipped polygons were clamped to the original footprint** (`hires.c`, `geometric = n > 1 && !nclip`).
  Their pixels (`DT_MODEL_NEAR`, 4 to 28% of the view) were cut to the original's coarse rows, so their edge against the
  sky strips was a staircase of one-cell steps: 32 to 65% of the colour changes along that edge lay on the cell grid
  (a refined edge gives 25% at N=4).
- **A polygon refused for "footprint beyond 3 px" was drawn as the scaled copy whole.** The test was exact equality of the
  true shape plus 3 px with the original's rows, so missing 1 pixel in 15,000 refused a large near-clipped sea/ground
  polygon or the sky polygon (corners 20,000 to 30,000 px out). It happened in 6 to 8% of the sampled frames,
  in bursts: the sky or the sea, up to 83% of the view, became cell-sized blocks for those frames.

Both are fixed (next section). Evidence stays outside git in `D:/f117-gate/present-v2/fix3d`.

HUD lines come from integer endpoints (`N` records: AX..DX of the library line call, already clipped); the in-between
frame interpolates each endpoint with `rnd(lerp())` back to an integer, so a line moves in whole cells, and a group
whose size changed between steps is held (18% of HUD lines in the strike flight) and switches at t = 0.5.
Text and tick marks are held whole until t = 0.5. The next section replaces that for the tapes, the heading strip
and the pitch ladder.

## The state-driven HUD (14 October 2026)

At N > 1 the tape labels and tick marks, the heading labels and tick strip, the pitch ladder with its labels, the
flight-path marker and the vertical-speed and stall bars are drawn from the game's state at fine-pixel precision, and the
state is interpolated between logic steps. The original
rounds every one of these to whole cells before it calls the library (the ladder's rungs by up to 5 cells), so the
picture built from its calls jitters at 4K however it is interpolated. N = 1 is unchanged: the original's calls are
replayed as before. The analysis is `D:/f117-gate/present-v2/hud-analysis.md` (private; the formulas are in
`src/present/hud_state.c`, ported from its `hudmodel.py` and `closedform.py`).

**The state.** VGAME's HUD pass (1058:0016) is a pure function of its data segment at entry. The observer's new
record `h` (`observe_hud_pass`, called by the matched pass at its entry; only with `f117_observer.hud`, which the live
feed sets and `F117R_OBSERVE_HUD=1` sets for `--observe`) copies the 1,560 words 0x2080-0x2CAE (tables, layout
constants, text blocks), 19 scattered words (heading, pitch, roll, altitude, raw and whole-knot speed, the ladder
flag, the G text) and the graphics driver's tick tables (the x column and row limits of library entry 11, not in
the data segment): 1,593 values, `hud_state.h` has the layout. A second record, `p` (`observe_panel_pass`, at the entry of the
matched panel pass 0000:79BE, which runs before the HUD pass), holds the words the flight-path marker and the two bars are
made from: `[98F6]`, `[E586]`, `[E474]`, `[B07E]`, `[B194]` and the HUD mode and size flags. It reads guest memory directly and changes nothing: the
route's final guest hash is the same with and without it, the `--observe` log with `h` records removed is
byte-identical to the log without them (`p` records included), and that equals the log of the build before this change
(fly3 route, 2.79-2.81B, 88,585 records, 57,589,444 bytes; final hash `3a1030e87d4f1807`).

**Two modes** (`hud_state.c`). EXACT is the original's integer pipeline (the clipper `0FB2:0330` included); it gives
the library calls of the pass in order (K colour, N line, T text with its eleven block words, H tick scale, the two
entry-18 blits). FINE carries the same formulas in floating point from the continuous state, with no final rounding:
airspeed from `[B07E] / 27` (the raw word is finer than the HUD's knot), altitude, and heading, pitch and roll as
fractional angle words. Lines are strokes between vertices at cell centres, bitmap text is placed by its top-left
corner (the convention that keeps the original's alignment between tick and glyph centre), and clips are the
original's, at cell edges.

**Drawing** (`hud_overlay.c`, entered from `drawlist_record` through one hook). At an `h` record the replay draws the
FINE primitives on the work page's fine picture: strokes of the HUD line width (`--present-line-width`, default one
cell) as oriented rectangles with square caps, so an axis-aligned stroke at cell centres covers exactly the original's
cells; glyphs from the font the `T` records carried (kept per font index), each source pixel an N x N block at the
glyph's fine position. A cover (`hud_cover.c`) then follows the records of the pass that arrive and recognises each
of the EXACT list's in order, by kind and every value. The ones FINE replaces still run through the coarse replay
(so the coarse picture, the colour and text state and the exactness check against the display are the original's) with
every page's fine mirror off. Counted per replay (`[present] state-driven HUD ...` line of `f117run`):
passes drawn, records replaced and kept, records that left the prediction (the cover then ends and the rest of that
pass keeps the original path), passes left on the original path (no tick table, no font yet, no page). The panel pass's
items need no list: after a `p`, a record is taken for the flight-path marker (entry 71, 11 x 7 from (55, 77) to x = 154,
y = 48 + `[98F6]` >> 6) or a bar (library line at x = 247 from row 52, or at x = 72 down to row 81) when its geometry and its
value are what the integer formulas give for that state; the marker is then drawn from the record's own art as N x N
blocks at y = 48 + `[98F6]` / 64, and a bar as a stroke in the library colour of the moment, its free end at
52 - `[E586]` / 16 (vertical speed, rows 20 to 81) or 81 - ((command speed - speed) * 2 / 5 + 29) (stall margin, the
speed being `[B07E]` / 27).
`F117_PRESENT_HUD=old` turns it off. The ink is tagged 240/241 in the diagnostic plane (`hud_overlay.h`;
`tools/present_diag.py` does not class them).

**Interpolation.** `INTERP_HUD` (set with a scale above 1): the HUD lines the state draws are left out of the HUD-line
pairing, and the skeleton's `h` record is regenerated with speed, altitude, heading, pitch and roll moved the same
fraction (angles by the shortest arc, `hud_blend`; a hair from a logic frame it is the frame's own record, so the
endpoints stay exact). Layout, mode, the readout flag, colours and the ladder switch are the skeleton's, as every other
discrete item (the nearer frame, switching at t = 0.5); if two frames disagree on them the skeleton's state is used
unmoved. Fixed-point override values: 1/1024 kt, 1/256 ft, 1/256 of an angle word.

**What is covered, and what is not.**

| covered (state-driven at N > 1) | still the original's records |
|---|---|
| airspeed and altitude labels (both altitude scales, full and small HUD), their tick marks | frame outline and the pointer marks beside the tapes (constants, already crisp strokes) |
| heading labels and the heading tick strip (modes 0 and 1) | centre reticle (fixed art) and the G meter text (changes once per step) |
| pitch ladder rungs, all rung shapes, rotated by roll, clipped to the ladder window; rung labels | the rest of the panel pass: digital speed and altitude, heading cue, station and gun text, tracking cross, ILS, sink marker |
| flight-path marker (sub-cell height), vertical-speed bar, stall-margin bar | weapon and target symbology, cockpit art (canopy posts, attitude ball) |

**Checks** (`tests/test_hud_state.c`, `tests/test_hud_live.c`, `tests/test_hud_jitter.c`; the corpus of 288 passes and
the windows are private under `D:/f117-gate/present-v2/`).

- EXACT against the original's log, seven scripted flights (full HUD climbing, on the runway, small HUD, steep pitch,
  48 degrees of bank, mode 1 inverted, mode 2 diving), 288 passes: 12,921 library calls, 0 mismatches (K 1,111, N 4,640,
  T 6,217, H 418, entry-18 blits 535).
- FINE where it must agree with the original's integers: 1,447 tape labels equal to the float's floor (614 more are
  outside the row clip and not drawn); 418 tick lattices identical to the driver's marks; 849 heading labels with
  integer minus float in [0, 2) (139 clipped); 6,836 ladder vertices with integer minus floor of float in x [-3, 5],
  y [-3, 1], the analysis's windows (one pass at a rung-numbering boundary left out); the ladder's label-offset tables equal
  to their analytic form; the tick scale's colour equals the tape text's in all 418 H records. The ladder sits up to 3
  cells higher in the original than in the float: its pitch is cut to a quarter degree and floored twice.
- The panel pass's items, from the snapshot words: 504 of 504 records of the marker's and the bars' geometry are
  recognised by the integer formulas (marker 258, vertical-speed bar 179, stall bar 67); integer minus float: marker top
  [-0.97, 0.00] cells (a floor), vertical-speed bar end [0.00, 0.94] (the original shifts right, a floor of the speed, so
  it ends up to a cell lower than the float), stall bar top [-1.17, 0.79].
- Through the live path (drawfeed, drawlive, interpolation; strike 8.53B and four fly3/fly1 windows, N = 4): every logic
  frame equal to the display at its close (35, 37, 37, 39 and 74 frames), every pair exact at both ends, coarse and
  fine (33, 35, 35, 37, 72), 0 records off the prediction, panel items replaced 74, 37, 40 and 148 in the four fly windows
  (the stall bar, whose n was 0 in them, only in the corpus), 0 replaced records that changed the fine picture (checked by
  copying the fine page around each, `--leak-check`). The machine live (`run_route.py fly3.args -- --steps 2810000000
  --present interp --present-scale 4`, headless): 789 HUD passes all drawn from the state, 789 logic frames equal to the
  display at their close, 787 pairs exact at both ends, 0 records off the prediction, 1,790 panel items replaced, final
  guest hash `3a1030e87d4f1807`, the same as without presentation. The `boot_to_flight` sentinel at N = 4 keeps its
  368 exact logic frames, 366 exact pairs and final hash `48a10e901505e2a9` with 369 passes drawn from the state. Cost: with N = 9 a window with the state-driven HUD takes the
  same time as without (3.2 s against 3.4 s for 37 frames, 105 in-between pictures and the end checks).
- Unchanged where it must be: the eight retained windows give identical output from `test_drawlist` (carried and not),
  `test_hires` at N = 1, 2, 4 and 9 (carried) and `test_interp` (default and `--fine 4`) before and after: 64 of 64.

**Motion** (`test_hud_jitter`): the same pair of frames is interpolated as before (integer HUD lines, text and ticks held)
and with the state, sampled four times per logic step; the second difference of a feature's position along the samples,
in fine pixels at N = 4 (a smooth path has second differences near 0; 4 is one cell):

| window | feature | before: rms, max | after: rms, max |
|---|---|---|---|
| fly1, climb 2.86B (72 pairs) | airspeed labels, y | 1.99, 4.00 | 0.01, 0.07 |
| | altitude labels, y | 2.13, 4.00 | 0.01, 0.10 |
| | heading labels, x | 0.67, 4.00 | 0.03, 0.48 |
| | ladder labels, x / y | 1.05, 8.00 / 3.78, 28.00 | 0.00, 0.02 / 0.01, 0.05 |
| | ladder endpoint, x / y | 0.37, 4.00 / 0.74, 8.00 | 0.11, 1.65 / 0.38, 5.86 |
| | flight-path marker, y | 4.32, 40.00 | 1.10, 10.22 |
| | vertical-speed bar end, y | 1.42, 4.00 | 0.19, 1.88 |
| fly3 f3b, inverted bank, mode 1 (35 pairs) | heading labels, x | 1.72, 4.00 | 0.06, 0.66 |
| | flight-path marker, y | 3.60, 28.00 | 0.80, 6.53 |
| | ladder labels, x / y | 4.65, 16.00 / 4.58, 12.00 | 0.35, 2.12 / 0.49, 3.30 |
| | ladder endpoint, x / y | 2.18, 8.00 / 2.18, 8.00 | 0.22, 2.14 / 0.15, 1.72 |
| fly3 f3a, steady 48 degree bank (35 pairs) | heading labels, x | 1.33, 4.00 | 0.00, 0.01 |
| | altitude labels, y | 0.68, 4.00 | 0.00, 0.00 |

The residue of the new path is the flight state itself: the original repeats a state in consecutive logic frames and
the next frame jumps (the 5.86 is one such repeat; the marker's 10.22 was not examined), which interpolation between real frames cannot hide. In the
f3a window the ladder does not move (constant pitch and roll) and both paths are still. Pictures at N = 4 are in
`D:/f117-gate/present-v2/hud/img`.

**Not established.** Colour-display HUD (`[43DE]` set: tape and text colour 10, ladder 47) was not in any capture; the
fine strokes take their colour from the state and the tape's text block, the strip's from the tape colour, and the
original's art may differ. Altitude above 10,000 feet reads the label table past what the captures reached;
`[E56E] = 0` (ladder off), the small HUD in modes 1 and 2 and rungs at |n| >= 9 are modelled from the disassembly and
not reproduced against a log. A pass is left on the original path when no `T` record has yet carried a font it
needs (none happened in these windows). Nothing here has been seen on a physical display.

## Footprints the original's clipper gets wrong (14 October 2026)

**What changed.** At N > 1 every verified model polygon, near-clipped ones included, is drawn from its geometry
(`geometry_cover`, half a fine pixel of inclusive coverage, clipped to the viewport); the footprint only decides
whether the geometry reproduces the original. The footprint clamp, the coverage tracking and the seam repair it needed
are gone (`DT_SEAM`, `drawlist_finish_models`). The exact test (footprint reproduced within 3 px) is kept; what
changed is what happens beyond it, by polygon kind:

| Kind | Rule at N > 1 | Why |
|---|---|---|
| Near-clipped model polygon (`nclip` 2, both crossings already verified against the original's) | accepted when the footprint lies within 16 px (Chebyshev, steps 4, 6, 9, 12, 16) of the geometry and the geometry spills over the footprint by at most a tenth of the footprint's size (`NEAR_CLIP_TOLERANCE`) | measured below |
| Span-rasteriser polygon with an edge whose 16-bit difference overflows (two corners more than 32,767 apart) | accepted, and drawn from its corners | every refusal has one |
| Zero-height span strip (all corners on one row) with a one-row footprint | the original's row is the rendition: tagged `DT_SPAN_LINE`, no refusal | there is nothing to refine; 28,672 of the 31,439 trace lines of one run were these |
| Zero-area span polygon with a taller footprint | kept scaled, `DT_SPAN_DEGENERATE` | the original's own artefact (two to four per flight run) |
| Anything else beyond 3 px (a non-clipped model polygon whose geometry sits rows from its footprint, no overflowing edge) | kept scaled | geometry that does not explain the picture |

At N = 1 nothing changes: the footprint is the picture, and `refill_span_polygon` returns before any of this.

**What the measurements say about the cause.** Refusals were traced over the strike, landing and air-to-air runs up to
the sampled frames (`F117_PRESENT_DIAG_TRACE`, with the footprint's distance to the geometry). Span polygons: every
non-degenerate refusal (863 to 49,000 per run) has an edge between corners more than 32,767 apart, which sends the
original's clipper (`0FB2:0757`, `vgame_span_edge_clipped`) down its halve-both-axes path (RCR/SAR on the differences,
losing their low bits) before the 32-bit-product divide that places the clipped endpoint. The footprints that result
lie up to 8 px (most), 128 px (a few) from the true band; in the example colour-128 band the true rows are around 66
and the original's footprint is three full rows at 70 to 72. Overflow alone does not predict refusal: 316,560
overflowing polygons in one run matched within 3 px. Why only some are off was not established; the clipper was read,
not reproduced offline. Model polygons: all 833 refused near-clipped polygons have a crossing 4,800 to 280,000 px
outside the picture (826 beyond 32,767; the original's 16-bit edge record holds the wrapped value, -279,589 appearing as
about -17,447), the original clips such an edge with a 32-step bisection and 16-bit slopes (`vgame_mc32_bisect_one`,
`vgame_mclip_point`), and the footprint is within 4 px of the geometry for 82% of them, 6 px for 90%, 16 px for all,
with the geometry spilling over the footprint by 0.5 to 4.6%. The 4,198 refused non-clipped polygons are different:
mostly tiny polygons whose geometry lies rows from the footprint (corners at y 61.2, footprint at row 64), which is a
geometry mismatch, not clipper error, and stay scaled.

**Before and after** (N=4, 3-D window, the same sampling as above; before is the commit's own `46e1d7c` build with
the hole counters added):

| | strike (144) | landing (167) | air-to-air (126) |
|---|---|---|---|
| frames with more than 0.5% of the window in fallbacks | 12 (8.3%) to 4 (2.8%) | 12 (7.2%) to 10 (6.0%) | 7 (5.6%) to 1 (0.8%) |
| frames above 5% / above 20% | 10 / 7 to 0 / 0 | 7 / 7 to 0 / 0 | 7 / 4 to 0 / 0 |
| worst frame | 83.6% to 1.7% | 55.6% to 3.3% | 42.4% to 0.7% |
| mean fallback share | 2.60% to 0.04% | 2.04% to 0.09% | 1.32% to 0.01% |
| unwritten pixels | 0 to 0 | 0 to 0 | 0 to 0 |
| mean stray pixels (share of the window) | 10,823 (2.3%) to 12,275 (2.6%) | 10,413 (2.2%) to 11,618 (2.5%) | 5,355 (1.1%) to 6,574 (1.4%) |
| worst stray frame | 7.1% to 14.6% | 7.5% to 17.4% | 4.9% to 22.9% |

The fallbacks that remain are `DT_SPAN_DEGENERATE` polygons (0.03%), non-clipped model polygons and
`DT_FB_NEAR_CROSSING` (0.06% of landing). Over the sixteen diagnostic frames (`present_diag.py`'s set), the window's
colour changes between two scene polygons that lie on a whole-cell run of the cell grid fall from 54,488 of 378,633
(14.4%) to 5,404 of 379,446 (1.4%); the four burst frames go from 36.3, 83.6, 52.2 and 17.4% fallback to 0, 0.25, 0.70
and 0. One-to-two-pixel slivers of colour between two polygons beside a near-clipped one, which a gap would make,
number 457 before and 141 after.

The stray count rises where the original's own clipper mis-drew a polygon: the picture now shows the polygon the game
asked for, which differs from the original's N=1 picture by whole rows in those frames (the horizon and sky bands in
the burst frames; up to 23% of the window in the worst). That is the intended result of "true geometry wins", and it
means that in such frames the fine picture is not a refinement of the original's, so the owner's parity judgement
applies to it. N = 1 is untouched.

**A hard turn.** Scripted routes turn at 2 to 3 deg/s. Holding Left, then pulsing Down every 3 s after the takeoff
climb, gives a steep bank (the horizon near vertical) at 3 to 7 g and 17 to 27 deg/s of yaw for about 12 s before the aircraft hits
the ground (heading words sampled each 4.5M instructions; the sustained window starts 450M after the climb; a second
script gives 10 to 40 deg/s). The two windows (30 and 29 frames at 0.5 s) show sea, terrain and a near-vertical
horizon. Fallbacks there were already small: mean 0.01% / 0.02% before and after, worst 0.3% / 0.7%; the scene
cell-stair share goes 1.6% to 0.9% and 1.6% to 1.0%; unwritten pixels 0. So these hard turns do not reproduce the
bursts, which occurred at small bank angles near the horizon.

**Checks.** `ctest -E func_lockstep`: 76 of 76. Sixteen retained windows (`logs41` eight, the map and span captures,
three current-flight windows): replay, `test_hires` at N=1/4/9 and `test_interp --fine` at 4/9, 96 of 96 pass (N=1: 69
of 69 phases equal in `w_strike_853`). `boot_to_flight` at N=4 and 9: 368 logic frames and 366 pairs exact, final hash
`48a10e901505e2a9`, equal to the run without presentation. The strike window 3.255B to 3.29B at N=1 gives
byte-identical pictures with the commit's own build. `test_present_strips` adds overflow, line and degenerate cases;
`test_near_polygons` the tolerance.

`tools/hires_subpixel.py`, the Python mirror of the builder, still has the earlier footprint/seam rule for near-clipped
polygons and the 3 px test alone; it no longer mirrors `hires.c` on them.

**What is not settled.** The original clipper's exact error was not reproduced, so the tolerances (16 px, a tenth,
the overflow criterion) are bounds from the measured distribution on three flights, not derived limits: a flight
with a larger error would be refused (and scaled) rather than drawn wrongly. A span polygon accepted on its
overflow is drawn inside the bounding box of its footprint, so a band whose true rows lie outside the original's table
loses that part. Zero-area polygons with a tall footprint, non-clipped model polygons whose geometry mismatches
and the stipple/dither cells stay scaled.

## Projected outlines, point markers and one stroke pen (15 October 2026)

At N > 1, unclipped world outlines and degenerate edges (the single-pixel scenery and light markers) now draw from
exact G camera snapshots. Their slot, projected endpoints and origin must match the captured L; stale, clipped,
behind-camera and unsupported records retain the earlier path. Paired outlines interpolate their camera coordinates
when both snapshots name the same vertices. An internal L suffix carries that geometry without changing the original
ten replay fields. The original 1x picture and all guest execution remain unchanged.

Perspective uses the full fixed-point ratio, `256*x/z`, before the original shifts its numerator and depth. The earlier
fine renderer kept the final divide remainder but still rounded depth, and its positive modulo jumped at each negative
projected integer. Full projection removes both sources of motion steps, for polygons as well as strokes. Orthographic
and verified near-plane crossing rules are unchanged. C and the Python study use the same projection.

Captured HUD lines, ticks, world outlines, thin strips and the state-driven HUD now share a pen with perpendicular width
and square caps. The square nib previously made a diagonal up to 1.4 times thicker. Default width remains one original
cell; `--present-line-width` retains its fine-pixel override. Different widths by drawing role have **not** been assigned:
identifying ground markings separately from object outlines and tuning those widths remain open. The owner keeps all
original sprites and artwork, including the map's 4x4, eight-heading aircraft arrow; no vector replacement is requested.

`test_world_lines` retains coarse equality at N=1/4/9, signed-coordinate and depth-boundary continuity, stale-slot and
vertex-identity rejection, and equal-width horizontal/diagonal strokes. `test_world_jitter LOG N` measures paired
outlines and dot markers at eighth-step intervals within each skeleton half, isolating interpolation rounding from
logic-step acceleration and cuts. On the retained strike window at N=9, RMS second differences fall from 1.680 to 0.0039
fine pixels for outlines (4,592 coordinate samples), and 2.756 to 0.0140 for dot markers (8,828). Negative polygon axes
fall from 0.496 to 0.0634 (76,753). A replay of the owner's flight also checks 19 exact frames / 17 exact pairs; its
negative-axis RMS falls from 0.877 to 0.00049. These are coordinate measurements, not a claim that every visible coast
or topology transition is resolved. Shoreline boundary measurements in the two sampled windows improve little;
coastline transitions and per-class width tuning remain open. Horizon-band motion is addressed below.

Evidence: `D:/f117-gate/present-v2/world-lines`. Final route, live-scale and timing checks are recorded in
[repeated-processes.md](repeated-processes.md).

The isolated N=9 strike replay also passes 3840x2160 / 59.99 Hz / vsync:
7,718 flight pictures, median 16.6675 ms, p99 16.7123, maximum 16.8153,
zero missed intervals and 17.5 ms guest drift. All 2,062 logic frames are exact;
the final guest hash equals the fresh headless reference (`bc8e3ae5a872405d`).

## The state-driven sky and ground bands (15 October 2026)

The high-detail background now follows the same approach as the HUD: read the pass's original state, predict its
integer calls exactly, then draw the same quadrilaterals with their rounding removed. It keeps the original 16 sky
and 16 ground colours and the original graphics. N=1 stays on the captured calls. Low detail retains its original
path; wrapped corners and unusable geometry retain a conservative fallback. `F117_PRESENT_BANDS=old` restores the
previous band renderer.

The observer emits `o` once at VGAME `1450`: height, pitch/roll sine-cosine pairs, the offsets, view centre, window,
flags and shifts. The EXACT model follows the original signed arithmetic, rounded multiplies, divides and wraps.
The FINE model uses the same formulas without rounding. Interpolation moves height linearly and angles along their
shortest arc; view, detail, colours and other discrete settings stay on the selected logic frame. Band `U` records
stay exact while the state moves. A band with a wrapped corner keeps that record too, so it cannot break recognition
of later bands. The old-renderer switch retains its original integer-corner interpolation.

**Evidence.** All 1,514,560 original quadrilaterals match over 47,516 captured passes, including 186 low-detail and
1,609 flat passes. There are no mismatched or truncated passes. On 47,313 same-view pairs, endpoint and midpoint
checks pass; six discrete-state changes decline. The recorded owner's flight keeps all 69 guest checkpoints and
final hash `31c259b992340414`. Its live replay checks 2,101 logic frames / 2,099 pairs exactly; the retained window
checks another 19 frames / 17 pairs. A ROM-free observer test checks identical records and no clock, VGA bus or guest
memory changes under both timing profiles, including wrapped stack/view offsets.

At N=9, in the owner's window, horizon-height second differences at columns 40/160 fall from RMS 3.27/3.29 to
0.44/0.35 fine pixels; all band edges together fall from 3.65 to 0.41 (4,174 samples). Strike: all band edges 3.67
to 1.26 (61,216); its visible horizon columns 160/280 fall from 3.56/3.65 to 1.00/1.34. These measurements include
logic-step changes and camera acceleration, not just rounding within one pair. A canonical sea/land-boundary probe
in the owner's window is unchanged (0.642 fine-pixel RMS, maximum 2): coastline topology transitions remain open.

**Capture replay correction.** Post-call `x` snapshots for copies/fills are measurements of the original picture.
Applying them to a regenerated picture painted coarse ghosts at the original edge positions. Interpolation now
omits snapshots for known entries 42/44/46; the C/D and model records draw their result. Raw frames still apply genuine
changed-byte fallbacks, and duplicate presents preserve carried fine pixels. The map fallback regression is retained.
A synthetic moving-dot regression fails without this correction; the wrapped-band and old-switch regressions reject
the earlier drafts too. At the sampled horizon, 2,592 fine pixels of coarse snapshot residue disappear. The old mode
matches the prior renderer byte for byte when redundant diagnostic snapshots are removed from its input.

Validation: 108 selected CTests, all 45 N=1 routes (259,240 frames / 259,166 pairs), live boot N=4/9 (368/366 each),
96 retained-window checks and 10 additional band-live checks. Proof and the isolated timing result:
`D:/f117-gate/present-v2/band-finish`; [repeated-processes.md](repeated-processes.md).

The isolated strike replay passes Direct3D 11 at 3840x2160 / 59.99 Hz / vsync / N=9: 7,718 flight pictures,
median 16.6679 ms, p99 16.7314, worst 17.0385, no missed intervals and 11.6 ms guest drift. All 2,062 logic frames
are exact; final hash `bc8e3ae5a872405d` matches the fresh headless reference. Physical-panel motion remains an owner check.

## The remaining HUD and original map-marker movement (15 October 2026)

The state-driven HUD now also covers its frame, reticle and G readout, plus the panel pass's station/gun text,
digital readouts, cue, outlines and messages. Bitmap fonts and sprites are the original art. The map aircraft
marker remains the original 4x4 bitmap selected from eight headings; only its position moves on the fine grid.
Its continuous position comes from the original 32-bit aircraft coordinates, with the map's zoom, origin and
16-bit subtraction wrap preserved. No replacement graphics are introduced.

Continuous values interpolate; numbers, text, switches and the selected arrow bitmap stay with the older logic
frame until the newer frame arrives. The original calls of the selected frame still update the coarse replay.
Extended panel records preserve the earlier eight-word format, and missing fonts or sprite art retain the original
path until available. `F117_PRESENT_HUD=old` retains the existing off switch. Horizon interpolation and discrete HUD
holding use separate option bits.

Fresh captures cover all six full/small HUD modes, three altitude bands, ladder-off passes and steep-pitch rungs.
The exact models match 29,594 HUD calls over 711 passes, 11,490 panel calls over 710 passes, and all 714 map-marker
blits. The corpus checker rejects an empty capture: seven files in the stopped draft had contained no records.
ROM-free tests check all eight synthetic arrow bitmaps pixel for pixel, transparency, coarse replay, fine movement,
heading timing and wrapped map origins. The pre-fix map-origin model fails that rendered-pixel regression. Observer
tests verify every sprite byte and unchanged guest memory, registers and clocks under both timing profiles.

Validation: 111 selected CTests; fresh full gate with all 45 routes / 44 fresh interpreter sessions, both 5,718,848-state
instruction profiles and three 891-address matched seeds, zero mismatches, reachability/event checks passing.
All 45 N=1 presentation routes retain 259,240 frames / 259,166 pairs; boot N=4/9 retains 368/366 each. All 96 retained
checks and 34 live checks over 17 fresh windows pass. The owner's complete N=9 replay retains 69 checkpoints and
final hash `31c259b992340414`, with 2,101 exact logic frames / 2,099 exact pairs and no drops or prediction mismatches.

N=9 fly1: the map arrow's horizontal second-difference RMS falls 0.75 -> 0.00 fine pixels (286 samples), and the
vertical-speed bar 3.19 -> 0.43. Small-HUD loop: the arrow's vertical RMS falls 0.84 -> 0.00 (458 samples).
The full owner replay also confirms the map's terrain is refined geometry. A short window that begins after the
map was cached can instead show its original bitmap; that window alone cannot prove the live map is blocky.
Do not force `F117R_OBSERVE_PAGES=1` on a live presentation run: it reseeds every phase and prevents pairing.

The owner requires background checks and no visible/fullscreen tests without asking first. The interrupted
fullscreen timing attempt supplies no acceptance result. A hidden Direct3D 11 3840x2160 target measures 119.47,
127.45 and 150.10 fps over strike, mode-2 and small-HUD windows; regeneration/raster p99 stays below 5 ms.
Occasional upload stalls reach about 100 ms. This is uncapped throughput, **not** steady-refresh acceptance;
fullscreen pacing for this batch remains unchecked. Evidence: `D:/f117-gate/present-v2/hud2`.

## Consolidate world geometry before more clipping exceptions (15 October 2026)

The owner's concern about spot treatments changes the next priority: build one state-driven world-geometry path,
rather than land another exception for coastline clipping. The visible-edge capture already contains raw camera
coordinates (`G`); precision alone is not the missing input. It lacks a complete model before visibility filtering
and persistent instance/face identities. Pairing clipped command lists can therefore hold or switch geometry when
an edge enters the viewport, a face disappears or a model changes its submitted vertices. In the owner's retained
window, the old diagnostic reports 135 held clipping changes and 27 unpaired polygons over 51 intermediate frames.
A private clip-specific prototype passes an analytic moving-triangle regression and 112 selected tests, but it is
set aside: it cannot by itself supply absent faces or stable model identity. No prototype runtime change is merged.

**Capture boundary found in the matched original.** VGAME `120A:00CF` takes the model index, position and orientation,
selects its original detail level, sets up parts (`0317`) and planes (`0561`), then submits vertices (`06B5`), edges
(`0A33`) and faces (`0C76`). Visibility is already filtering the later inputs: `06B5` skips vertices whose `7A02`
byte is zero, and `0CA8` skips hidden faces. Therefore adding precision only to `G`/`E` cannot recover the whole
model. Start the observer at the model request/selected-data boundary, before those filters, and record the camera
and original model inputs. Inspect upstream scene-cell/aircraft-slot identities; a reused projected vertex address
or draw order is not a persistent object ID.

Implementation order and acceptance:

1. Record complete original model inputs and stable instance identity, scoped to a view/camera/viewport, including
   orthographic map views. Cache original mesh data by its content; retain original part transforms, face/edge
   topology, material/fill commands, detail selection and submission order. Observer reads must preserve guest
   memory, clocks and bus timing in both profiles. Assets and captures stay private.
2. Decode the original model stream once into presentation geometry. With the original integer state and visibility
   rules, compare its submitted vertices, edges, faces, colours and styles against original calls. Cover positive,
   zero and shared-world vertex counts, part switches, hidden faces and all material commands. Capture completeness
   is a requirement: do not call a collection of visible edges a full scene.
3. Interpolate camera and model poses by stable identity, transform shared vertices once, and project/clip geometry
   at the displayed resolution. Filled terrain, roads, outlines and dots use the same transforms and clipping rules.
   Keep complete geometry available across visibility changes. Original detail switches, text, sprites, explosions
   and other discrete events retain their logic-frame policy; all original artwork remains.
4. Verify camera-plane and viewport crossings, entering/disappearing faces, shared-edge tiling, moving objects,
   reused storage, view changes and detail changes with analytic geometry and real captures. Preserve all N=1
   routes and guest hashes; compare interpolation endpoints to each new fine frame and measure visible coastline
   motion across skeleton switches and logic boundaries, including features missing from one old capture. Check
   hidden 4K throughput. Physical refresh remains separate and needs approval before any visible/fullscreen run.

Reuse the existing fine fill/style rules, stroke pen, map-copy handling and presentation worker. This is a broader
presentation change, not a replacement of the game simulation or its artwork. Legacy capture replay remains the
original-resolution oracle and a fallback for data not yet covered; it does not define completion of the new path.
Evidence and preserved prototype: `D:/f117-gate/present-v2/coast-finish`.

### Complete model-input capture

`F117R_OBSERVE_SCENE=N` enables a bounded research capture with the headless runner's `--observe` clock window.
Keep both `i` (request) and `j` (memory) records when filtering kinds. Each request records the original model,
position/orientation arguments, camera/view metadata, CPU registers, complete DS and model-bank segments, and
256 stack bytes starting at SP. Reads bypass device accessors so capturing VGA-backed addresses cannot charge
guest bus cycles. The app does not enable this capture. Original bytes stay private; this is intentionally a
research boundary, not the final per-frame scene representation or a persistent object identity.

`py tools/scene_capture.py LOG --out JSON` reconstructs the inputs and rejects missing/duplicate bytes, mismatched
segments/clocks, out-of-range words and metadata inconsistent with the captured memory. Empty captures fail.
ROM-free tests check logical/physical address wrapping, every captured byte and unchanged guest memory/registers/
clocks under both timing profiles; parser regressions check completeness and reject malformed captures.

The owner's complete replay captures 64 requests (23 model indices, perspective and orthographic-map views):
8,404,992 original input bytes and all request metadata/clocks are identical between the interpreter and recompiled
engines. Both retain the baseline's 69 checkpoints and final `31c259b992340414`. Evidence:
`D:/f117-gate/present-v2/scene/pair-verdict.json`. This proves inputs for requests the game submits, not every
world candidate or stable scene identity. The shared renderer is still to be implemented.

A second paired replay captures 128 in-flight requests after clock 2.5B: all 16,809,984 input bytes, request metadata
and clocks agree, and both engines retain the same 69 baseline checkpoints/final state. Its selected detail pointers
and all geometry stream boundaries/counts agree with original-byte execution for all 128 requests. Evidence:
`D:/f117-gate/present-v2/scene/flight/pair-verdict.json` and `flight/layout-verdict.json`.

`tools/scene_model.py` decodes original parts, planes, vertex lists, ordering trees, vertices, edges and material
commands. It derives original detail selection directly from request position and camera state; all 192 selections
agree with original-byte execution. All 191 requests that reach geometry also agree at the part/plane/vertex/edge/
face stream boundaries and counts. The other request returns at the original reach test. From original prepared
part poses, complete camera geometry agrees on 2,330 submitted vertices, including articulated transforms; face
tests agree on 488 planes, 349 visible shades and their vertex flags. The decoder retains the 108 hidden vertices
that the original omits. This is not independent pose preparation or rendered material/style parity.

Controlled original-byte fixtures also exercise all 205 reachable detail variants in the two banks, including six
articulated and nine zero-vertex variants: every stream boundary/count, 1,722 plane tests/shades and 3,870 transformed
vertices agree; 32 unreferenced vertices remain available. The fixture deliberately selects a level and enables the
original map projection, so it is format/geometry proof, not a real-route result. Original execution harnesses and
assets stay private (`controlled-layout-verdict.json` and the two `original-stream-boundaries.json` files).

Eleven ROM-free model tests cover detail thresholds and signed depth, articulated and zero-vertex topology,
positive/shared vertices, the cache's padded 16-byte stride, part switches without planes, signed/wrapped camera
sums, conditional plane visibility/shading, logical address wrapping and malformed material/LOD streams. Shared
vertices are synthetic coverage here: none occurs in the captured banks, so that path still needs real proof.
Unproven extended ordering-tree leaves fail explicitly. Material commands are decoded but not yet rendered.

Validation of the observer runtime: 112 selected CTests; fresh full gate (1,706 s) with all 45 routes / 44 fresh
interpreter sessions, both 5,718,848-state instruction profiles and three 891-address matched seeds, zero mismatches;
reachability/event checks pass. Seven capture-parser and eleven model tests pass. All 45 N=1 presentation routes
retain 259,240 exact frames / 259,166 exact pairs; boot N=4/9 each retains 368 frames / 366 pairs. No runner changes,
dropped frames or endpoint mismatches. Consolidated evidence: `D:/f117-gate/present-v2/scene/foundation-verdict.json`.

### Unlocked rendering and control latency

Gameplay/logic parity remains the first requirement. Rendering may run independently, but must not advance the
original simulation faster, retime its input reads or feed predicted poses back into gameplay. Resolution alone
does not require interpolation delay. Latest-state presentation can avoid that delay while retaining the original
motion steps; smooth presentation without waiting requires visual prediction, whose errors need measurement.
The shared scene path should make pose/camera prediction possible and avoid unnecessary queued frames. Exact
interpolation still requires known states and carries its documented picture age; the existing extrapolation's
measured errors are not a solved latency result. No default or owner configuration is changed by this work.

## Identity pairing and the step clock (15 October 2026)

The owner's recorded flight (`session-20261015-103956`, frozen privately with its draw capture) still showed
road edges that pop and building patches that jump. `tools/motion_check.py LOG` now measures what the exactness
checks cannot see: the share of polygons and outline edges with no partner in the next logic step (they switch at
t = 0.5 instead of moving), held steps, and how unevenly the steps are timed.

**Identity pairing (an experiment, off; it made motion worse).** With `F117R_OBSERVE_IDENT=1` (and in the live
presentation when `F117_PRESENT_IDENT=1`) the observer logs a
compact record `I` for every model request at `120A:00CF` (`scene_capture.h`): the model, the caller chain, the
scene walk's ring (`DS:E328`) and world-cell words. A batch's key is its model, ring and cell; batches whose keys
and vertex counts are equal and unique in both frames pair first, whatever their order or motion (`interp.c`
`ident_key`; mirrored in `interp_frame.py`), and the existing order/motion pairing handles the rest. In a 100M-clock
window of the flight the key agreed with every existing pair (0 contradictions in 728) and matched 334 batches the
old pairing missed. Same window, without / with identity: polygons unpaired 7.78% / 3.98%, outline edges unpaired
2.51% / 0.78%, held steps 7 / 4. Raising the 60 px motion limit instead recovered under half as much and risks wrong
pairs. A replay of 2.70B-2.72B holds 8,368 unpaired polygons instead of 11,490, with all 1,497 pairs exact at both
ends and the guest hash unchanged. Remaining: two of the same model in one cell (needs the entry ordinal), detail
changes (another mesh) and batches with no request (map, HUD).

The owner saw more jitter with it, and `motion_check.py`'s deformation measure shows why: the extra pairs join the
same object's batches when they hold different visible vertices (the original drops hidden vertices before
drawing), so the object deforms between the steps instead of moving. Of the 316 pairs only identity made, 295 deformed
by over 100 px (median 743 px; agreed pairs: median 2 px); paired batches deforming over 100 px rose from 3.78% to
20.53%. Fewer unpaired shapes was the wrong goal. Identity pairing is off by default (`F117_PRESENT_IDENT=1` turns the
experiment on); a usable version needs per-vertex identity (which model vertex each projected one is), which the
state-driven scene renderer has by construction.

**The step clock.** The game moves the aircraft a nearly fixed distance per logic step (mostly 46 units in the
flight) while a step takes 42-86 ms by scene cost; 24% of step boundaries change duration by more than 20%. The
in-between frame's t is the time since the newest step closed over the previous step's duration
(`drawlive_plan_interp`), so a longer next step freezes the picture at t = 1 and a shorter one skips forward when it
arrives. Simulated over the flight's real step times at 60 Hz (1,186 frames): the current clock gives 29 freezes,
6 skips and 21.4% of frames whose speed changes by more than 20% (p99 429%); a playback clock advanced by host time
over a smoothed step duration, held about one step behind the newest and never past it, gives 2 freezes, 0 skips and
1.6% (p99 41%), for about 0.3 step (17 ms) more picture age. Not yet implemented: it needs one more step and its
pairing kept alive in the threaded pipeline; it will be switchable and judged by `motion_check.py`.

**The state-driven scene renderer** remains the main path for world geometry: drawn from interpolated poses, it
needs no primitive pairing (object identity still names moving objects). The original's resident world is four
rings of 32 tile slots (`DS:E330`, counts at `DS:061E`; 18-25 populated in the capture), so a longer view would need
our own world loader; the owner chose not to extend the draw distance for now.

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
- **Remaining geometry fallbacks.** Verified near-plane crossings now refine
  on the fine grid and interpolate with retained camera-space geometry
  (see the reconstruction results above). Exact snapshots now also resolve
  fully clipped view origins and stale vertex records. The retained windows
  leave one extreme original clipping-arithmetic artifact scaled; missing
  metadata also retains scaling. The chosen HUD/text
  presentation is built; refresh pacing, motion and 4K output still need
  the real-display check.
