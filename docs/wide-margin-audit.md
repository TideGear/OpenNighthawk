# Wide-margin audit: what the game draws in the 3D view, and whether the Remastered margins show it

Owner, 10 November 2026: "the margins should be able to draw everything the main rendering area does" (R41, R42 in
[owner-issues.md](owner-issues.md)). This audit lists, from the matched code (`src/matched/matched.c`), every way the
game puts something into the 3D view, how its position is made, what culls it, and whether the Remastered wide margins
can show it today. It is the code reading; runs on flights only confirm it. **Work in progress: sections marked OPEN are
not yet read.**

## How the Remastered picture is made (why 2D matters)

The wide picture is built from two sources: the 3D scene records Remastered rebuilds from the game's model draws (every
polygon, line and dot of a model, at its camera-space position) and the game's own flat 320 x 200 picture, which carries
the HUD, panels, text, sprites and blits (`remaster_live.c`: "Strokes, text, sprites and blits (the HUD, the panels, the
cockpit art) stay"). The flat picture covers only the 4:3 middle. So anything the game draws flat, at a screen position,
can never reach the margins as things stand, and anything drawn as a model can, if the game's culling lets it through.

The render copy widens three of the game's culls for the margins (`rerun.c`): the cone cull of objects (sideways
allowance, `F117_FAR_WIDE_ACROSS`), the vertical allowance (`F117_WIDE_UP_ALLOW`), and the point test `0xB5DC` (the x
row of its matrix divided by the width factor; `wide_air_exec`).

## The point projection 0xB5DC and its callers

`0xB5DC` (`camera_effect_point`) puts a world point on the 320-pixel screen: `[4A10]` = x, `[4A18]` = y, `[DEBE]` =
depth; `[4A10]` = FFFFh when the point is behind, outside the 45-degree cone or off the 320 x 96 view. Its matrix
(DS:B082) is used by nothing else. Callers, all in `vgame_moving_objects` (VGAME `983E`) and the symbology routine:

| Caller (return) | What | How it is drawn | Margins today |
|---|---|---|---|
| `9A04` smoke-trail puffs | the 16 puffs of `frame_trail` (`48B8`) | deferred through `D9FE` with the model word 400h + (age & 0Eh) / 2, so the scene walk draws each as a scaled 2D sprite (`compose_draw_sprite`, `1AE9`) whose centre is the projected `[4A10]`,`[4A18]` and size 256 / `[DEBE]` | NO (flat sprite); and misplaced by the widened matrix like the far dots (R41) |
| `9BDF` aircraft | other aircraft | near: a 3D model (`BB0D`); far (`[DEBE]` shifted by the type's byte at `[bp-38]` at or below -32): a 2D dot at `[4A10]`,`[4A18]` (far call at `9B2C`, colour 15) | models yes; far dots NO (flat; and misplaced by the widened matrix: R41) |
| `9D89` projectiles | missiles and bullets (12 slots) | near: a 3D model (`BB0D`); far (`[DEBE]` at or below -32): a 2D dot (colour 29h or 13) | models yes; far dots NO |
| `9DFD` flares | the flare | a 3D object (`BB0D`) when `[DEBE]` is between -256 and 0 | while the widened test passes |
| symbology | see below | | |

## The scene object walk (VGAME `DC46`, `vgame_compose_walk`)

Every deferred object is drawn here. Kind bit 400h: drawn as a scaled 2D sprite (`compose_draw_sprite`, `1AE9`, the
blitter entry 22) at its projected centre: NOT in the margins (flat). Kinds above 25h: also a marker sprite at a
projected position (`1B2E`, block page): NOT in the margins. Every other kind: a 3D shape (`120A:00CF`): in the margins
when the widened cone cull passes. OPEN: which game objects have kind bit 400h (explosions, fires and smoke clouds are
expected). Read so far: the smoke-trail puffs are kind 400h + frame (the trail code above); the other deferred objects come from `compose_emit_object` (`E2B6`, 7 callers) and are OPEN and what the kind-25h+ markers are.

## The symbology routine (sections from its own header in matched.c; each read)

All drawn flat at screen positions from `FN_PROJECT` (`B5DC`): NOT in the margins today. What each is, and what showing it in
the margins would mean:

| Section | What | Placement and gate (from the code) | In the margins |
|---|---|---|---|
| 1 | a box (size 6) over every weapon in flight; colour 11h for slots 0..7 (fired at the player), 0Ah for 8..11 | projected; drawn when it lands on the screen | should show: a physical object |
| 2 | gun tracers, tail-to-head lines (0Dh the player's, 29h an enemy's) and their hit tests | both points projected; if either is off the 320 screen the slot is skipped, drawing and hit test alike ("the gun's reach is the window") | the line should show; the hit test is game logic and stays the real game's (a tracer in the margin cannot hit, as in the original) |
| 3 | the impact blast sprite (`1A98`), size 100h / depth, phase 10h - abs(clock) | projected at the effect (following a named unit while above ground) | should show |
| 4a | the weapon lock marker on a ground target (display mode 2), recoloured 6 when the station cannot use it | drawn when the target projects | **owner decision**: the real game offers no lock on a target off its 320 screen; a margin marker drawn by the copy could show a lock state the real game does not have |
| 4b, 9 | the detection ring: a closing marker with the detector's name (ground site, or air unit) while `DETECT_COUNTDOWN` runs (`B4F5`) | projected at the detector | should show (the rings the owner saw) |
| 7 | the weapon lock marker on an air target (size 9), HUD pen near the centre, locks past depth / -8 | drawn when the unit projects | **owner decision**, as 4a |
| 10 | bomb impact point (size 5, locked, 29h) with a line from the flight-path marker; bomb bracket and diamond on the target | projected; the impact point is pinned to the bottom of the window when it falls off the screen | **owner decision**: pin to the wide window or the original's |
| 11 | radio beacons: diamond, "Radio Beacon" label with a pulsing marker, and messages | projected at z = 0 | should show |
| 5, 8, 12 | the camera displays | the right-hand panel, not the view | not applicable |

Rule for every margin overlay (from the owner's parity boundary): what is drawn must reflect the real game's state. The render
copy's own decisions (lock, hit, kill) are thrown away and must not be shown where they could differ.

## OPEN

- Every other routine of the frame that calls a drawing entry of the graphics library (84 entries at 1E42:011A + 5n)
  while the 3D view is drawn: to be listed from the frame's call tree, so the list above is shown complete.
- Every 3D cull the wide view relaxes, and any it does not (object kinds, rings of scenery, near and far planes, size
  thresholds), for every theatre.

## The 3D audit checklist (owner, 10 Nov: "be thorough with the 3D audit too")

Method: read every test in the matched code that can stop a 3D thing reaching the picture, for every object class and
theatre; for each, record what it compares (inputs, thresholds, units), which hook of the render copy relaxes it for the
margins and by how much, and whether anything of that class can still be culled while inside the wide view. Flight replays
and the turned-view reference only confirm the reading. A line is DONE only when the code is read and the margin result is
stated; nothing is "believed fine".

| # | Test | Where | State |
|---|---|---|---|
| 1 | The object cull (`E3AC`/`E3BF`, `camera_transform`): far plane, near limit, side and vertical frustum, range metric | `compose_emit_object`, `compose_defer_model` | READ and MEASURED: the range metric never binds in practice (below) |
| 2 | The terrain walker (`04E6`): the four rings, the 4 x 4 cell block, the cell wrap at -8..7 cells, the detail value `[990E]`, the far plane `[48E6]` | `vgame_scene_walk` (matched.c), the copy's `far_scenery_body` (rerun.c) | READ, see below |
| 3 | Per-model detail selection and size thresholds (`scene_model_depth`, the size word `[7D5E]`, `[DEBE]` class tests, the dot-versus-model switch) | model draw `120A:00CF`, `BB0D` | READ (depth only): see below; the dot-versus-model switch of aircraft is item 4 |
| 4 | The aircraft test `B5DC` and its matrix (R9, R20, R41) | `wide_air_exec` | R41 cause found; fix not final |
| 5 | Ground objects, buildings, lights, lit dots and patches: the ring each lives on and its draw distance | terrain walker, `remaster_live.c` far scenery | OPEN |
| 6 | Sky and ground bands, horizon, sea (`vgame_horizon_bands`, `1450`) across the full width | `1450`, `drawfeed`/`remaster_live` | OPEN |
| 7 | The clipping and rasteriser stages: outcodes (`04BC`), the viewport rectangle (xmin..xmax of the polygon fill), near-plane clip, span tables | graphics library entries, `130D`, `0FB2` | OPEN |
| 8 | Culls by object state: destroyed, hidden-class, `[C05C]`, bit 7 of the class byte (deferred list), `[49F6]` flags | `compose_walk` (`DC46`) | OPEN |
| 9 | Views other than the cockpit front view (`[C0A6]`: outside, rear, weapon, map) | the draw routines per view code | OPEN |
| 10 | Every theatre and mission type: which of the above differ (scenery tables, night, weather) | per-theatre data | OPEN |

Confirmation runs, after the reading: the turned-view reference (the copy's heading turned by the margin's angle so the
margin falls inside the game's own 4:3 view, the original renderer drawing it) compared object by object with the wide
picture, on the owner's flight and the runway, at both edges and at the corners.

### Item 1: the object cull `E3BF` (read in matched.c, `vgame_camera_transform`; the render copy's changes read in `rerun.c`)

An object is culled when (a) its z is beyond the far plane `[48E6]`, (b) it is nearer than its detail class's limit
`[48F6 + 2 * [49AA]]`, (c) it is outside the frustum: |x'| over 1.25 x ((|z'| + T_across) >> (1 - [294B])), or |y'| over
(|z'| + T_up) >> ..., tables at `[E56A]` and `[E570]` (the second frustum, with `[438E]` set, uses `[4966]`/`[4976]`), or (d)
its range metric `(|x'| + |y'|) / 4 + |z'|`, stored at `[49A8]`, is beyond the far plane `[48E6]`.
The copy raises every T to 18,000 (or less when the far plane is raised), moves each near limit back by the class's own
allowance (`F117_WIDE_NEAR`), and leaves the far plane to the draw-distance setting.

- **(c) side and up: relaxed**, so a margin object is not culled for its sideways position, up to the allowance: a limit of
  about 18,000 units beyond |z'| (an object centre more than about that far outside the cone is still culled; it matters only
  for the widest views, to be quantified per width).
- **(d) the range metric is NOT relaxed.** It adds a quarter of |x'| and |y'| to the distance, so the farther sideways an object
  is, the nearer it is culled by distance: at the original's own view edge (|x'| = 1.25 |z'|) the reach is already about 24 %
  shorter than straight ahead, and for a margin object at |x'| = 2.5 |z'| about 38 % shorter. So in the wide margins everything
  that has a distance limit (scenery, buildings, lights, aircraft) pops out at a shorter distance than the same thing in the
  middle. **Candidate defect: far objects vanish earlier toward the margin edges.** To confirm by measurement (the distance at
  which a known object disappears at the centre and at the margin edge, in a replay) and then decide the rule (use |z'| plus the
  metric's own contribution only up to the original's edge angle).

### Item 2: the terrain walker (read in matched.c `vgame_scene_walk` and in rerun.c `far_scenery_body`)

The game's own walk (`04E6`) visits, per ring (4 coarse to 1 fine, only where `[04FA + 2 * ring]` is set), at most 16 cells per sector from the sector
table `[034C]` (the sector is `(1000h - heading) >> 13`: eight 45-degree sectors; nine entries a sector, or the near tables `[04CC]`/`[04DE]` when the pitch
is steeper than -60 degrees), each cell going through the cull `E3AC`, so it draws a block of cells ahead and nothing behind or aside beyond that block.
The render copy's far scenery (`far_scenery_body`, the menu's Draw distance) does NOT use the sector table: it takes every cell of a square around the eye
on each ring (radius from the reach, cells clipped to the map), picks each object once at the finest level whose camera depth is under the reach (8000
units), skips only the cells the game's own walk draws, and lets the guest cull `E3BF` decide with the far plane `F117_WIDE_SCENERY_FAR` (default 30000) and the
widened side and up allowances. **So the walk's cell coverage is all round, not the forward sector: no direction is missing by construction.** What can
still hide a margin object: (a) the pick's depth reach (8000 units on the picked level), which is the same for the middle and the margins (depth, not
sideways); (b) the cull's range metric (item 1), with the far plane 30000 it only binds beyond 4 x (30000 - |z'|), i.e. not at 8000 depth; for the game's own
nine cells the far plane is the game's own (`(detail + 1) x 0D05h`, 13,332 at detail 3), where the metric does bind for margin objects close to the reach;
(c) the cell wrap (an offset beyond -8..7 cells wraps), which limits every ring in every direction alike. To measure, not assume: item 1's test.

### Item 3: the detail level and the size thresholds (read in `scene_mesh.c`, `scene_model_depth`)

The detail level of a model is chosen from `scene_model_depth(position, camera)`: the position times the camera matrix's z column, summed in 32 bits, shifted
by 16, magnitude: a function of depth z' only. It does not depend on the sideways position, so a model in the margin gets the same detail as one in the middle
at the same depth. The size indicator `[DEBE]` (z' >> 3) that gates model versus dot for aircraft and projectiles is also depth only. Result: no margin
difference from this class of test. (The aircraft's dot-versus-model switch is audited with item 4: the matrix division by the reach halves `[DEBE]`.)

#### Item 1, measured (11 Nov 2026): the range metric did not reject anything

`F117_CULL_WHY=1` (the copy's native cull, `far_cull` in rerun.c, counts which check rejects each own-level object first, in the order far plane, near plane,
across, up, metric) over the screenshot window of the owner's flight (clocks 17.9 to 18.5 billion, Remastered wide, Maximum draw distance): far plane 0, near
plane 378,338, across 1,771,104, up 152,324, **metric 0**, visible 578,234. The metric is the last test, so a metric rejection would be an object that passed every
other test and was dropped only for its sideways distance: there were none. The far plane the copy sets for the walks (30000, or the detail's own when the game
draws) leaves no room for the metric to bind within the reach (8000 units of depth). The candidate defect above is therefore **not observed** on this flight;
it stays a possibility only for objects of the coarser levels (shift > 0, not counted by this switch) and for a lower far plane, and for a far plane the owner's
Draw distance setting lowers. The across test is the dominant rejection (about 60 % of the tests): objects outside the widened cone, i.e. outside the wide
picture, as intended; with the allowance at 18,000 units the cone half-angle is wider than any screen shape (about 76 degrees at 8000 depth).

## The complete list of what draws into the 3D view (read 11 Nov 2026; this closes the "prove nothing else draws flat" question for the view itself)

The 3D window is drawn by one routine, the scene dispatcher VGAME `03CE` (matched.c, "VGAME 03CE: the scene dispatcher"; the composer `2F9D` only chooses the
angles and the view code `[C0A6]` and calls it from `1BCE`). In order it runs: the window set-up and frame pacing; `D826` (`vgame_set_view_and_sky`: viewport,
camera matrices, scene origin, then the sky and ground bands `1450`); the terrain walk `04E6` (objects through `E2B6`, culled by `E3AC`, drawn or deferred);
`983E` (`vgame_moving_objects`: the player's own model in outside views, the 16 smoke-trail puffs, the other aircraft, the 12 projectiles, the flare, the
exterior player model); `D87C` (`vgame_scene_finish_walk`, which composes the deferred objects through `DC46`, `vgame_compose_walk`, the scene-object walk with its
400h sprites and 25h+ marker sprites); and `9EFA`, the flight display pass (weapon markers, tracers, blast, locks, detection rings, bomb symbology, beacons, and the
right-hand display), the last thing the frame draws. The HUD (`1058:0016`) and the panels (`79BE`) are separate passes outside the 3D window (flat by design; they
stay in the middle). So the passes that put something into the view are exactly these seven stages, and every one of them is accounted for above:

| Stage | What | Margins today |
|---|---|---|
| bands `1450` | sky and ground bands | full width (R6, R24, owner-confirmed) |
| terrain walk `04E6` and `D87C`/`DC46` models | 3D objects | yes, all round (item 2), culls relaxed (item 1) |
| `DC46` kind 400h and 25h+ | flat sprites and marker sprites | NO: needs the 2D mechanism |
| `983E` | smoke-puff sprites, far dots (aircraft, projectiles), models | models yes; flat things NO |
| `9EFA` | markers, tracers, blast, rings, locks, bomb symbology, beacons | NO |

Still to read for the 3D side (items 4 to 10 of the checklist): the 400h and 25h+ object kinds on the terrain (which object types are sprites: the table at `[0510]`
read from game memory), the cull of the aircraft test (item 4, R9/R20/R41 known), the clip and fill stages (item 7), the object-state culls (item 8), the other view codes
(item 9, `[C0A6]`: 41h to 43h side looks, 84h, 85h, 87h, 88h/89h/8Bh, 8Ch) and the per-theatre data (item 10).

## Items 4 to 10 (11 Nov 2026): what is read, what is not

**Which objects are flat sprites (the 400h kind of `DC46`).** The scene walk draws a deferred object as a flat scaled sprite when its model word has bit 400h.
The terrain's object types are read from the model word table at `DS:[0510]` (128 words): from a memory dump of the owner's flight at clock 12.0 billion (DS
37AEh found through the string "Radio Beacon" at DS:42D8) **none of the 128 types has bit 400h**: every terrain object of this theatre is a 3D model. The only
emitter of 400h model words found is the smoke-trail puff code (`983E` -> `D9FE`, model word 400h + (age & 0Eh) / 2). The other callers of `compose_emit_object`
(`E2B6`) are the camera-image routines (`camimage_place_near_effect` `0BC5F`, `camimage_aircraft_glint` `0C4BA`), which draw the target-camera panel, not the
view. **Not yet checked: the other theatres' tables (item 10); the kind-25h+ marker sprites (`1B2E`, a four-byte marker table through `[7D7E]`): which objects
carry such kinds is OPEN.**

**Item 4, the aircraft test `B5DC`.** Read (R9, R20, R41 above and in owner-issues). Known and understood; the fix is part of the 2D mechanism.

**Item 5, ground objects, buildings, lights.** All are 3D models on the terrain walk (this theatre), covered by items 1 and 2. The scene walk gives each a ring by size;
the far scenery reaches 8000 units of depth at the finest level where it fits, equal in the middle and the margins. No sideways-dependent limit found beyond the
widened cone. (Not yet verified object type by object type; the turned-view comparison will check it.)

**Item 6, the sky and ground bands (`1450`).** Full width in Remastered (R6, R24, owner-confirmed 3 Nov). Nothing to add.

**Item 7, the clip and fill stages.** Remastered does not use the original's screen clipping: it takes each model's vertices in camera space from the scene
mesh (`remaster_scene.c`, "from a model request to camera-space primitives") and rasterises them itself (`remaster_raster.c`), so the original's 320 x 96 clip
rectangle and outcode routine (`04BC`) do not apply to the wide picture. Read at the level of the headers; the per-model material commands
(`scene_mesh_materials`) are the same for every object and not sideways-dependent. Not read line by line.

**Item 8, per-object-state culls.** Destroyed, hidden-class and class-byte culls (`[C05C]`, bit 7 of the class byte, `[49F6]` flags) are game state and the same in
the middle and the margins. Not read line by line.

**Item 9, the other views.** Every view code goes through the same scene dispatcher (`03CE`); `9EFA`'s sections are gated by the view (the weapon lock and the bomb
symbology need the forward cockpit view, section 10 states it). The wide picture is for the cockpit view; how the other view codes (side looks, rear, tracked
object) show in the wide picture is OPEN: to read and to check in a replay of each.

**Item 10, the theatres.** OPEN: the model word tables, the ring tables and the night and weather settings differ per theatre; the one memory dump is Libya.

Confirmation still to run (after the reading): the turned-view reference and the 2D overlay comparison on the owner's flight.
