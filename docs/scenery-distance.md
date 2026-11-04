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

1920 x 1080, scale 3, software output, threads on, the owner's runway replay (clock 3.9 to 4.8 billion, session 20261103-154444), pictures a second:
Original 59.8, Near 55.8, Far 50.0, Maximum 22.9. Per copy, native placement: Near about 200 objects, 0.24 ms; Far about 2,200 (and 0.7 ms choosing);
Maximum about 10,000, 9 ms. The compositor (the lines stage in particular) is the larger part at Far and Maximum: composite 6.7 ms (Original), 7.8 (Near),
11.4 (Far), 19 (Maximum, threads on). Near's whole cost is about 2 ms a frame. One native object costs about 0.7 microseconds, against about 35 through the game's own
placement: most of that was the renderer's mesh cache, which decoded a model again for every distinct depth and hashed two thousand bytes of the bank on every
call (`remaster_scene.c`: keyed on the detail level now, with the bank signature kept); that also speeds the live game's own model drawing.

## Limits and what is not shown

- Beyond about 524,000 units nothing small is drawn (ring 4's reach); that is half the map.
- The shape of a distant model comes from the game's own level of detail at the coarser level's depth (nearer in its units, so a more detailed level than
  the game would have chosen at the real distance, shrunk back to size).
- Multi-part and shared-vertex models (a few percent of objects, for instance a spinning radar dish) still go through the game's placement every copy.
- Maximum is renderer-bound: the lines stage draws thousands of tiny strokes. A cheaper path for strokes under a pixel is possible and not built.
- No moving-sequence capture of a building appearing earlier; the evidence is the placement and reach logs and the oracle above. The owner judges Near, Far and Maximum.

## History

- 3 Nov, earlier: a shifted three-cell block over ring 4, then rings 2 and 3, then explicit cells fed to the game's walker out to its 16-bit limit, with a
  coarse-scale pass (ring-1 objects through ring 2's scale via merged tile lists in scratch memory). Replaced by the native path above: the walker's
  16-bit cell offsets, its per-cell and per-object overhead, and the scratch-memory trick are gone. Still there behind `F117_FAR_WALKER=1`.
- Negative controls kept for the record: a native view-cone filter on cells was not exact (objects extend across many cells); taxi/airfield samples with the
  first walk were pixel-identical (it had no hook slot).
