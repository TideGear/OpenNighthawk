# Scenery distance

Remastered draws far scenery farther than the original, with an adjustable reach. It happens only in the Remastered render copy
(`src/present/rerun.c`, `remaster_live.c`): the game's logic, the live machine and `matched.c` are untouched, and Original and Enhanced keep
the game's own reach. The setting is **Draw distance** in the settings menu (Near, Far, Maximum, Original), applied at once;
`--draw-distance` and `draw-distance =` in `f117a.ini` take the same names or a number of world units. Default: Near.

## Why buildings pop in

The terrain is four **rings**: four grids over the one static world, each object on exactly one ring by size. Ring 4 is the coarsest (an 8 x 8
map of cells 262,144 units wide: the big landmarks); ring 1 the finest (256 x 256 cells 4,096 wide: small buildings, lit windows, the radar
dish); rings 3 and 2 are between. A ring works in its own unit, 4 times coarser each step (`0810`), so the guest's 16-bit camera-relative words
reach 4 times farther on each ring.

The game draws a terrain model only while its camera depth is under 8,191 units **of its own ring** (the range test, VGAME `120A:02B2`: it fails
when `2 * (|depth| + 1)` reaches the size word `[7D5E]`, which is 16,383 for every model in the replays checked). That is about 8,000 real units for ring 1,
32,000 for ring 2, 131,000 for ring 3 and 524,000 for ring 4. Ring-1 buildings therefore appear at about 8,000 units, while the ring-2 and ring-3
versions of the same area (dots) show beyond: groups of buildings pop in with dots already visible past them. Widening the cells the walk visits
cannot move this (the owner's session 20261103-205615, and `F117_REACH_LOG`, which lists each model's farthest drawn depth); the earlier work that did
(extra terrain walks, 3 and 4 Nov) mostly drew objects the test then rejected.

## What the copy does

An object drawn at a **coarser level** has a smaller depth in that level's units, so it passes the same test 4 times farther; its model is shrunk by
the same factor so it keeps its real size (the renderer shrinks the vertices about the object's origin, `finish_model` in `remaster_live.c`). The copy
does this for every object, once per object, in `far_scenery` (`rerun.c`), at the entry of the game's own terrain walk (the live walk then draws its
nine cells per ring as always):

1. **The world, once.** Every object of every ring is listed from the map and tile lists (position in its ring's units: cell * 4096 + 2048 + the entry's
   offsets, which is the walker's own sum), keyed on a hash of that data and indexed by cell. About 74,000 objects on the 3 Nov map.
2. **The level.** For each object in the cells within reach, the finest level (its ring or coarser, up to the setting) at which its camera depth is
   under 8,000 of that level's units and its offset fits the guest's 16-bit words. The setting is how many coarser levels are allowed: Near 1, Far 2,
   Maximum 3 (the whole map; ring 4 reaches about 524,000 units, the map is 1,048,576 across). The objects in the game's own nine cells per ring are
   left to it, where the ring is drawn at all.
3. **The cull.** The game's own camera transform and cull (`E3BF`, ported exactly: `far_cull`), with the side allowance divided by the level
   difference, since it is in the object's own units; and the darkening byte the game keeps in `DS:48C4` (`far_detail`).
4. **The pose, natively.** The first object of each model in a copy is placed through the game's own `E2B6`, which makes the renderer take a
   *template* of the game's pose state (`DS:7B28 .. 7DA0`). Every later object of that model patches only what depends on its place: the position words, the
   eye (the position negated, and for a model with one base part also through the part's matrix, `1452:021B`), the 32-bit translation (`1452:0330`) and the
   darkening byte; then the renderer builds the primitives (`remaster_scene_add`) exactly as it does for the game's own placements. Models with two or
   more parts, with shared vertices, or in orthographic views keep the game's own placement (about 3 to 4 percent of objects).

The game's placement of an extra object also skips its own face drawing (the renderer draws the model from its geometry): zero differing pixels over three
runway moments (3 Nov).

## Checked against the game

`F117_NATIVE_CHECK=1` builds each native object's primitives aside and compares them, field by field, with the game's own drawing of the same model at the
same position (paired by bank, model and position, since a deferred object is drawn later): 1.6 million objects over the runway replay, 0 differing. One
field is left out when it cannot agree: the shade, when `DS:7D8A` differs; that byte is the game's per-face fill colour, so the renderer's "base shade"
read from it depends on what the game drew just before. Diagnostics: `F117_FAR_WALKER=1` (the old extra terrain walks), `F117_WIDE_WALK_STATS=1` (cost per copy),
`F117_NATIVE_TIMING=1` (per phase), `F117_NATIVE_SIZES=1` (projected size of drawn objects), `F117_REACH_LOG=1` (the cap per model),
`F117_WIDE_DISTANCE=UNITS` (overrides the setting), `F117_WIDE_SCENERY=0` (off).

## Cost (measured)

Two numbers matter and the first measurements mixed them up. **Presented intervals** are paced to the display (60 Hz): Original reads exactly 16.7 ms, which is the cap, not what the machine can do.
**Work per frame** is what one loop iteration does before it presents (the machine's step, the copy, the composite, the upload): the column `cost_ns` of `--present-log` (added 3 Nov), and
`tools`-free to read (`private/.../p95cost.py` style: sort the column over the runway replay's clocks 3.9 to 4.8 billion; the headroom the owner asked for is this at the 95th percentile).

1920 x 1080, scale 3, **software** renderer (the owner's window uses the GPU renderer; every number here is from a headless software run on the development machine), threads on, session
20261103-154444, work per frame, ms:

| | mean | p50 | p95 | p99 |
|---|---|---|---|---|
| before the composite moved to the worker (Original / Near / Far / Maximum) | 9.0 / 10.2 / 12.2 / 34.2 | 8.5 / 9.2 / 11.7 / 38.5 | 11.5 / 14.7 / 16.5 / 44.6 | 13.4 / 16.3 / 17.6 / 48.0 |
| after, two runs (same four) | 5.9 to 6.0 / 6.5 to 6.6 / 7.8 to 8.1 / 28.6 to 29.4 | 4.6 to 6.3 / 5.7 to 6.6 / 7.3 to 7.6 / 33.5 to 34.1 | 8.9 to 10.2 / 9.4 to 10.2 / 12.4 to 12.7 / 39.1 to 39.5 | 12.5 to 13.2 / 12.9 to 13.1 / 13.8 / 41.4 to 41.9 |

So against the owner's goal (p95 under 12 ms): Original and **Near are met** (9.4 to 10.2); **Far misses by 0.4 to 0.7 ms** (12.4 to 12.7); Maximum is far off (39 ms) and needs a cheaper path for tiny strokes or the GPU.

Where the main thread's time went (F117_LOOP_TIMING, per frame, Original/Near): the machine's step 1.4 ms; the copy 3.5 to 4.6 ms (the game's own frame composition, run on the main thread: `drawlive_prepare_rerun`);
**the Remastered composite 3.6 to 4.2 ms (`remaster_live_compose`, on the main thread, serial with the copy)**; the upload 0.2 ms. The composite was the cheapest thing to move: it now runs on the present worker, right after the
worker renders the Enhanced picture (`present_worker_main`, `remaster_apply_ex(..., on_worker=1)`), and the worker is released just before the present call (`pw_release`) so the buffers the main thread uploads are not
overwritten under it. Only a picture from a rerun copy is composed there (the live machine's frame is written by the main thread meanwhile); others are composed by the main thread as before. The composite reads
the frame's own palette snapshot (`remaster_live_set_palette`, a plain variable because the composite's row helpers read it too; a thread-local one let some rows read the live machine's palette, which differs
in a fade). `F117_WORKER_COMPOSE_CHECK=1` recomputes each worker composite on the main thread from the same inputs and compares every output byte: 0 differing of 800 (Near) and 600 (Far) composites after the
palette fix (3 of 400 before it). `F117_NO_WORKER_COMPOSE=1` returns to the old arrangement.

The copy itself (profile, `F117_PROFILE=1`, per copy at Near, 4.4 ms): copy start to the terrain walk 0.7 ms (189,000 original instructions), the far scenery 0.8 ms (26,000), the game's own terrain walk and scene 2.2 ms
(437,000), the HUD pass to the end 0.6 ms (3.3 million instructions, which is the game's frame drawing). One native far object costs about 0.5 microseconds (the renderer's mesh cache is keyed on the detail level and keeps
the bank signature; it was about 7 before and about 35 through the game's own placement); `F117_WIDE_WALK_STATS=1` prints the far scenery's choosing and placing times, `F117_NATIVE_TIMING=1` the phases of one native object.

## Native terrain walk (experimental, off)

`F117_NATIVE_WALK=1` skips the game's own terrain walk in the copy and places its nine cells a ring through the same native path (and the game's override table, `far_override`): the copy falls from 4.4 to 2.7 ms at Near.
It is **not exact**: the picture differs from the game's walk by 2,770, 1,998 and 277 pixels at the three runway clocks 4.0, 4.4 and 4.7 billion, mostly a strip along the horizon where the original draws sea (12, 28, 85)
and the native walk draws ground (36, 28, 11). Tried and ruled out, each leaving the three counts unchanged: the override table, deferral of the game's own cells, draw order by ring, the game's whole-cell cull
(`E3AC` on each cell's centre, with the game's own far plane and allowances). Not yet looked at: what else the walk leaves behind (`DS:0504`, `49AC..49B0`, `950A`), per-object comparison of what each path draws (log both,
diff the sets), whether the sea is a flat object whose order against the coast depends on the walk's step order (the walk visits a sector table's cells in a fixed order; mine sorts by level and distance).

## Level of detail

The game picks a model's level of detail from its depth. An object drawn at a coarser scale is at a depth 4 times smaller in that scale's units, so it got a finer level than the game would at the real distance, and the
detail dropped at the moment the object switched back to its own scale (the owner saw a group of lights thin out and come back as they flew closer). `remaster_scene_lod_shift` now scales the depth used for the level by the
same factor, so the level follows the real distance (monotonic). The level is not worth keeping for speed (a few thousand polygons); it is worth keeping for stability (tiny lit windows drawn in full detail crawl). A "LOD
bias" setting (finer detail farther out) is easy and would bring the shimmer back; not built. The remaining 1-pixel light shimmer is aliasing: a coverage fade for sub-pixel points (lit ones with a floor) is the modern fix, not built, the owner's call.

## Limits and what is not shown

- Beyond about 524,000 units nothing small is drawn (ring 4's reach); that is half the map.
- Multi-part and shared-vertex models (a few percent of objects, for instance a spinning radar dish) still go through the game's placement every copy.
- Maximum is renderer-bound: the lines stage draws thousands of tiny strokes. A cheaper path for strokes under a pixel is possible and not built.
- The deferred-draw list: the far scenery used to fill the game's 50-entry list (full in 116 of 1,392 copies of the owner's banking flight), dropping its nearest entries (lights and buildings flickered while banking). While the
  far scenery is placed the defer bit of the classes it places is cleared and restored after; the list is empty after it in all 1,484 copies of that flight (`F117_FAR_TRACE=FROM` prints it).
- No moving-sequence capture of a building appearing earlier; the evidence is the placement and reach logs and the oracle. The owner judges Near, Far and Maximum, the banking flicker, and the lights (3 Nov late: the owner reported
  lights becoming tiny flickery points far away, groups thinning then thickening on approach, and some buildings still popping in at a distance; the LOD fix above answers the second; the third is Near's reach, one step: Far is two).

## History

- 3 Nov, earlier: a shifted three-cell block over ring 4, then rings 2 and 3, then explicit cells fed to the game's walker out to its 16-bit limit, with a
  coarse-scale pass (ring-1 objects through ring 2's scale via merged tile lists in scratch memory). Replaced by the native path above: the walker's
  16-bit cell offsets, its per-cell and per-object overhead, and the scratch-memory trick are gone. Still there behind `F117_FAR_WALKER=1`.
- Negative controls kept for the record: a native view-cone filter on cells was not exact (objects extend across many cells); taxi/airfield samples with the
  first walk were pixel-identical (it had no hook slot).
