# Scenery distance: current evidence

The Remastered wide view now makes a three-cell terrain pass in its private
render copy. It fills a small part of the widened land view that the normal
walk leaves empty. Original and Enhanced keep their existing drawing path and
live machine hook limit. This is partial R6 progress: distant buildings,
several land views, and broad movement/performance coverage are still open.

The default applies only when Remastered uses a view wider than 4:3. The
diagnostic `F117_WIDE_SCENERY=0` disables the pass; values 1 through 6 select
the cell shift and are clamped to that range. The installed ONTests build was
not refreshed. No full machine gate was run.

## Implementation and regression

VGAME's terrain walk at `04E6` takes eight words: heading, pitch, and three
32-bit camera coordinates (X, Y, height). The matched implementation reads
the full 16-byte argument block. The extra call forwards every word before
adjusting SP; the earlier two-word experiment read unrelated stack contents
as its camera position and was not valid evidence.

The extra pass shifts a sector's cell table along the current heading, disables
the three finer rings, raises the far plane to 30000, lowers the side allowance
when needed to avoid 16-bit depth overflow, then restores the copied machine's
registers and modified table values. The caller itself is the speculative
Remastered render copy, never the live game machine.

The first land replay exposed a second failure mode: the live renderer already
occupied seven of its eight execution hooks, so the copy installed the
aircraft hook but had no slot for the terrain walk. `machine_t` now has two
additional hook slots for the render copy. Live sound and Remastered hooks
remain capped at eight, preserving their existing behavior. The focused
`test_rerun` fills all eight live slots and checks that both copy-only hooks
still work; it also checks all eight camera arguments, stack wraparound, and
that the real machine and its full memory remain unchanged. `build/test_rerun.exe`
passes on the final build.

## Fresh airborne land replay, 3 November

The comparison uses a newly recorded North Cape strike route generated through
the game's normal menus and takeoff controls. Captures are from current build
SHA-256 `858d3f00f6a307bba891d36b86a032caf25fb1ca2eb162b7d5aae044c5529ad3`;
none of the older renderer screenshots were used. Private files are under
`private/scenery-20261103/north-cape-route/`.

At clock 3,700,000,000 the placement trace reports camera position
`109237,330178,7116` and a live aircraft altitude of about 7,116 feet. The
fresh frame shows land beside the coast. In the 173% wide view:

| Terrain pass | Extra walk values | Primitives | Model requests | Difference from control |
|---|---:|---:|---:|---|
| Off | 0 | 79 | 29,102 | — |
| 3 cells (default) | 7,070 | 103 | 32,273 | 177 pixels at the widened edge |
| 6 cells | 421 | 81 | 29,205 | pixel-identical |
| 12 cells (before the diagnostic clamp) | 0 | 79 | 29,278 | pixel-identical |

For the three-cell result, the differing pixels occupy `x=1120..1274`,
`y=466..506` in the 1280 x 600 capture, within the wide land margin. The
default-without-an-environment-override capture is pixel-identical to the
explicit three-cell capture. The tested six-cell offset draws less useful
geometry; twelve cells finds none in this view, so neither is selected.

At clock 3,900,000,000, after a turn, the aircraft is at about 10,694 feet
and looking into sky. The extra walk returns with zero geometry and the image
matches the control exactly, so this sample shows no terrain bleeding into
the sky. It does not substitute for continuity checks across multiple land
headings or sector/detail transitions.

The short replay's requests rose about 11% and primitives about 30% with the
three-cell pass. End-to-end wall time in single runs was 54.6 seconds for the
control and 54.8 seconds for three cells; this is too little timing evidence
to establish a general performance cost. A later performance pass should use
the project's measured renderer workflow.

The 1 MiB snapshots at clock 3.8 billion differed between the control and the
three-cell run at two bytes (`0x4CD8` and `0x4CE4`); the same two offsets also
varied between duplicate control runs. Do not cite those snapshots as proof
of exact machine parity. The native rerun isolation regression checks the
copy's full-memory isolation directly. The new work is confined to the
Remastered render copy and the live machine hook cap is unchanged.

## Finer rings (3 November; rings 2-3 on by default in Remastered wide views)

The terrain map is static, not loaded around the player: level 4 is an 8 x 8 byte map at `B03C` with a populated 4 x 4 block, level 3 a
16 x 16 map at `9F98` (RAM dump at clock 3.7 billion, VGAME DS `37BC`). The three-cell ring-4 pass therefore already reaches everything the
coarsest ring holds. What the game limits is rings 1-3: each holds its own smaller objects and visits only a 3 x 3 neighbourhood of its
own, finer cells, so small and medium scenery appears late. `F117_WIDE_FINE_CELLS=N` (0 default, clamp 6) adds a second extra walk
over rings 1-3 shifted N cells ahead (`walk_pass` in `src/present/rerun.c`).

Measured on the recon route (private `fine-compare/`, candidate exe, 1920 x 1080 Remastered): primitives 102 -> 198 at N=3 and 127 at N=6
at clock 3.7 billion; 353 -> 425, 102 -> 175 and 102 -> 201 at 3.3, 3.5 and 3.6 billion. The changed pixels (232 to 1285 a frame) are
single black specks scattered over the sea toward the horizon, which look like the sea-detail dots the game draws only near the
aircraft. No building, tower or terrain polygon appeared in any sampled frame, so this is **not** an accepted improvement: it is
held back at that point until a land-heavy frame with objects in the finer rings can be compared against the original's own drawing of the same
place, and its extra cost (requests rose 45% to 60%) measured.

Land check (recon route, clocks 6, 7, 8 and 9 billion, over desert and coast, 3 cells against off): primitives 78 -> 116, 63 -> 100,
68 -> 182 and 82 -> 156; changed pixels 335 to 2,419. Again the additions are small dark specks on the ground toward the horizon
(ground clutter dots, not buildings or terrain polygons), and model requests per run rise about 1.9x (108k -> 215k, 192k -> 362k,
249k -> 468k, 305k -> 559k). That is a large renderer cost for a faint effect that has not been compared with the original's
nearer drawing of the same dots, so ring 1 stays off (see the rings 2-3 section below). Evidence: private `scenery-20261103/fine-compare/pair-*.png`.

## Owner's land flight, 3 November (session 20261103-145810)

The owner took seven Tab shots after scenery popped in on the ground below (clocks 5.22, 5.71, 6.07, 6.41, 6.67, 6.97 and 7.77 billion).
Replayed from that session's `save-start` (a replay without it diverged completely), the pass-off frame matches each shot within
900 to 6,600 pixels, the pass-on frame within 1,500 to 7,100. The shots show rows of white and yellow lights (airfield lights) and black
ground specks. With `F117_WIDE_FINE_CELLS=3` the same frames gain further light rows and specks farther out, for example at the right
margin at 6.41 and 6.67 billion and across the ground ahead at 5.22 billion: the first evidence of a real distance gain for lit objects.
Evidence: private `scenery-20261103/owner-compare/sheet-*.png` (owner shot, pass on, changed pixels in red).

Cost, 1920 x 1080 scale 3, software output, the same replay from clock 6.4 to 6.9 billion, one run each: pass off 60.0 pictures a
second (p95 interval 19.3 ms); 1 cell 49.5 (25.8 ms); 2 cells 49.5 (26.2 ms); 3 cells 54.6 (23.7 ms). The runs are single and the
ordering is not monotonic, so treat it as "every variant misses 60 here", not a ranking. The all-rings pass was held back until its cost was reduced
(it reruns the whole terrain walk a second time per copy) and measured with the protocol in remaster-performance.md.

## Rings 2-3 only: on by default (3 November, later)

Logging every placement through the owner's session (`F117_REMASTER_PLACEMENT_LOG_ALL=1`, private `placements/`, `placements2/`) shows
where the cost and the benefit are. Ring 1 (the specks and lights, models 282, 293, 295) nearly doubles with the all-rings pass but is
the least visible; ring 3 (the mid-size objects) is where pop-in distance changes most. Furthest distance each model is drawn, units, without
the pass and with rings 2-3 only: ring 3 model 271 3,439 -> 11,183; 299 1,999 -> 12,976; 316, 317, 318 about 10,000-12,000 -> about 20,000;
265 and 266 appear only with the pass (to about 20,000); ring 2 model 279 10,680 -> 17,566, 277 11,805 -> 15,278, 338 10,330 -> 13,700,
276 and 278 up to 12,405 and 13,150. Ring 1 and ring 4 models are unchanged.

`F117_WIDE_FINE_RINGS` (a mask, default 12 = rings 2 and 3; 14 adds ring 1) selects the rings; `F117_WIDE_FINE_CELLS` (default 3, 0 turns
the whole extra pass off). Cost, 1920 x 1080 scale 3, two runs each, clock 6.4 to 6.9 billion: pass off 60.1 and 60.2 pictures a second
(p95 20.8 and 20.2 ms), rings 2-3 60.2 and 60.0 (20.7 and 19.3 ms): no measurable cost on this machine. The all-rings pass was 49.5 to 54.6.

What is not shown: at the owner's seven shot clocks the rings 2-3 pass changed no pixels at 6.41 and 6.67 billion and only 128 at 5.22 billion
(no building-class objects were in range), and at clocks 4.04 and 4.30 billion where it draws ring-3 models 5,000 to 11,000 units out it
changed 480 and 208 pixels along the horizon, because objects that far are a few pixels across. I have no frame in which a building is seen
to appear earlier; the evidence for the buildings is the placement distances above. The owner should judge it in ONTests. Unit tests:
`presentation_rerun_isolation` now expects three walk calls (the game's, the ring-4 pass, the rings 2-3 pass), each with all eight words.

## Earlier negative controls

The prior comparisons were useful for finding mistakes but did not establish
a visible distance improvement:

- The corrected pass increased model requests in taxi/airfield samples while
  producing pixel-identical images.
- The first land flight at 6 and 12 cells also appeared unchanged. Its trace
  showed `walk=-1`: the scenery hook had not been installed because the eight
  live hook slots were full. Those samples are not evidence that the pass was
  ineffective.
- The original two-word-call sample was invalid because its camera arguments
  were incomplete.

Native positive/negative call coverage and the original disabled-pass
comparisons are in `private/scenery-20261103/argument-regression.json`,
`camera-call-comparison/`, and `direction-isolation/`.

## Remaining work

- Verify additional distant buildings and terrain at known world distances on
  more than one land-heavy route and across cell sectors/detail-ring changes.
- Capture moving land scenes while turning, and check for gaps, duplicates,
  pop-in, occlusion, and interaction with the coastline correction.
- Measure the pass's renderer cost across representative flights and display
  sizes; the current request/primitive counts and one end-to-end timing are
  not a performance acceptance.
- Review the result in ONTests after a future authorized refresh.

Original game behavior and simulation parity remain hard boundaries. Keep
future distance changes in Remastered's private presentation copy, and keep
the progress credit partial until the remaining evidence is available.
