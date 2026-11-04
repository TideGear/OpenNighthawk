# Scenery distance

Remastered's wide view draws scenery farther than the original, with an adjustable reach. It happens only in the Remastered render copy
(`src/present/rerun.c`): the game's logic, the live machine and `matched.c` are untouched, and Original and Enhanced keep the game's own
short reach. The setting is **Draw distance** in the settings menu (Maximum, Far, Near, Original), applied at once; `--draw-distance` and
`draw-distance =` in `f117a.ini` take the same names or a number of world units. Default: Maximum.

## How the game limits it, and what the copy does

The game's terrain walk (VGAME `04E6`) draws scenery on four **rings**: four grids over the one static world, each object belonging to exactly
one ring by size. Ring 4 is the coarsest (an 8 x 8 map of cells 262,144 units wide, the big landmarks); ring 1 the finest (a 256 x 256 map of
cells 4,096 units wide: the small things, such as lit windows and roofs, patches of colour, the spinning radar dish); rings 3 and 2 are between.
Each ring visits only nine cells of a 4 x 4 block around the viewer's own cell (the sector table at `[034C]`, offsets -1..2), so small objects
appear a few cells out, which is the pop-in.

The guest keeps a cell's position as a 16-bit `offset << 12`, so any offset beyond -8..7 cells wraps. That is the far limit of every ring: 28,672
units on ring 1, 114,688 on ring 2, 458,752 on ring 3 and the whole map on ring 4. It is also why shifting the sector table by 6 or 12 cells
drew nothing (an earlier experiment, and the first versions of this work, which also covered only about 4 cells).

In the copy, before the game's own walk, `walk_second` runs extra walks that feed the walker **explicit cells**: row r of the table is the X
offsets of sector r and the Y offsets of sector r + 2 (nine pairs), so each pass writes up to nine chosen (dx, dy) offsets into the current
sector's rows, enables one ring, and calls `04E6` with all eight original words (pitch forced level, because a steep downward pitch reads the near
tables `[04CC]` instead). The far plane is raised to 30,000 and the side allowances lowered so their sum with the depth stays under 7FFFh.

Which cells: every cell within the setting's reach on each ring (radius = reach / cell width, at least 2, at most 7), except the nine the
game draws itself. Before a pass is built the map is read natively (`cell_tile`, matching `0871`; `cell_base`, matching `0810`), and a cell is
left out if it lies outside the map or its tile holds no objects (`[061E + 2 (32 ring + tile)]` is 0). The outside-the-map test matters: the guest
answers tile 0 for a cell outside the map, and tile 0 is a real tile, so asking for such a cell would draw ghost copies of its objects. This
filter is exact: pictures are pixel-identical with and without it (three clocks, 0 differing pixels).

Reach, in world units: Maximum 4,194,304 (every ring to its limit), Far 20,000, Near 12,000 (ring 1 reaches 5 and 3 cells; the coarser rings
keep their minimum of 2 cells), Original 0 (no extra walks). Diagnostics: `F117_WIDE_DISTANCE` (units, overrides), `F117_WIDE_SCENERY=0` (off),
`F117_WIDE_SCENERY_FAR` (the far plane), `F117_WIDE_WALK_STATS=1` (cost per copy, per ring, every 300th copy), `F117_REMASTER_PLACEMENT_LOG_FROM`
with `F117_REMASTER_PLACEMENT_LOG_ALL=1` (every placement: ring, model, position).

## What it brings in

On the owner's runway flight (session 20261103-154444, replayed from its `save-start`; the replay matches the owner's shots) the Maximum
setting draws about four times as much as the earlier shifted-block pass: the horizon fills with specks, lit building clusters appear on the
left, and a dotted lit line appears along the runway edge, 1,600 to 2,500 changed pixels a frame at the three runway clocks. In the owner's first
flight (session 20261103-145810) the placement logs show ring 3 model 271 drawn out to 3,439 units without extra walks and 11,183 with, model 299
1,999 and 12,976, models 316 to 318 about 12,000 and 20,000, models 265 and 266 only with them. The owner confirmed the lit dot rows are buildings
(dark roofs, lit windows) and that these, the colour patches and the radar dish were popping in.

## Cost (measured)

1920 x 1080, scale 3, software output, the same replay (clock 3.9 to 4.8 billion, runway taxi and take-off), one run each, two for Original, Near
and Far: Original 54.4 and 53.5 pictures a second (p95 interval 25.3 and 26.3 ms), Near 47.2 and 45.5, Far 42.5 and 42.3, Maximum 39.2 and 37.4
(34.4 and 38.7 ms). At Maximum an extra walk is 35 passes, about 357,000 emulated instructions and 4.2 ms a copy (down from 96 passes, 771,000 and
6.8 ms before the object filter); ring 1 is 216 cells and 164,000 of those instructions, ring 4 (heavy models) 131,000. **So Maximum costs about
15 pictures a second on this scene on this machine; the 60 fps target is not met with it on.** Original keeps the game's own cost.

Tried and removed: a native view-cone filter, to skip cells behind or beside the viewer. It was not exact: objects on every ring extend across many
cells (the shoreline models especially), and a margin wide enough to be exact keeps nearly every cell. A cheaper walk needs the copy's emulated work
reduced or run in parallel; that is the open performance work.

## Limits and what is not shown

- Ring 1 (the small buildings) cannot be pushed past the 16-bit limit of 28,672 units without a different technique (drawing it at a coarser
  scale about the eye); it is not infinite for small objects. Ring 4's landmarks cover the whole map.
- No frame was captured of an object being seen to appear earlier in a moving sequence; the evidence is stills at known clocks and the placement
  distances. The owner should judge Maximum, Far and Near in ONTests.
- Steep downward pitch (past 30 degrees) was not tested with the extra walks.
- The cost numbers are single software-output runs on one machine.

## Regression

`presentation_rerun_isolation` (`tests/test_rerun.c`) runs the copy with every ring enabled and a populated map: the game's own walk once, extra
walks each with all eight words (pitch 0), none when the map holds no objects, and the live machine and its full memory unchanged. `machine_t`
has two extra hook slots for the copy so the walk hook installs even when the live machine's eight are full.

## History

- 3 Nov, earlier: a three-cell shifted block over ring 4 only (177 pixels at the widened edge on one North Cape frame), then rings 2 and 3, then
  all rings (all three at three cells), replaced by the explicit-cell walk above. The shifted blocks could not reach past about 5 cells.
- Negative controls kept for the record: taxi/airfield samples with the first walk were pixel-identical (it had no hook slot); a two-word-call
  sample was invalid (incomplete camera arguments); the sea and desert frames showed only specks until the owner's runway flight showed buildings.
