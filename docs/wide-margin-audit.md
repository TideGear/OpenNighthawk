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
| 4a | the weapon lock marker on a ground target (display mode 2), recoloured 6 when the station cannot use it | drawn when the target projects | **owner decision (11 Nov: recommendation accepted)**: in the margins a plain marker, the game's own unlocked form (the four-sided box of `0B171` with locked = 0, in the HUD pen), no lock indication; the real game offers no lock on a target off its 320 screen |
| 4b, 9 | the detection ring: a closing marker with the detector's name (ground site, or air unit) while `DETECT_COUNTDOWN` runs (`B4F5`) | projected at the detector | should show (the rings the owner saw) |
| 7 | the weapon lock marker on an air target (size 9), HUD pen near the centre, locks past depth / -8 | drawn when the unit projects | **owner decision (11 Nov)**: as 4a, the plain box, size 9, HUD pen |
| 10 | bomb impact point (size 5, locked, 29h) with a line from the flight-path marker; bomb bracket and diamond on the target | projected; the impact point is pinned to the bottom of the window when it falls off the screen | **owner decision (11 Nov: recommendation accepted)**: when the impact point falls off the view, pin it to the bottom of the wide picture |
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

## Draft design of the 2D mechanism (11 Nov 2026; for the build stage; nothing built)

What the code shows: the flat things are the game's own library calls (line entry 31, sprite entries 73/18/71/19 and 22, blit 42, fills) onto its 320 x 200
pages, positioned from `[4A10]`/`[4A18]`, which `B5DC` stores only when the point lies inside 0..13Fh across and the view's rows down (else `[4A10]` = FFFFh and the
true x is kept in `[952C]`). The picture layer replays those calls (drawlist.c, tags DT_SPRITE, DT_SPRITE_RLE, line, text) onto a page model of the game's pages and
the wide picture puts that page in the 4:3 middle. Two changes make the margins show them:

1. **True positions.** In the copy, `B5DC` must give the true screen x and y for any point inside the wide picture: widen its x test to the wide factor (the
   cone test is already widened for models; the copy's x row division would move to a pure position scale instead of the 3x cone trick of R9/R20, which is what
   squashed the dots) and not set `[4A10]` to FFFFh for a point that is inside the wide picture but outside 0..13Fh. The depth value `[DEBE]` needs no change
   (z only; `air_depth` in the interim patch predicts it exactly).
2. **A wide page for the flat tags.** The replay of the flat primitives that come from view-anchored routines (the lists above) must target a page wider than 320
   and not clip at 0..319, so a sprite or ring centred at x = -80 or 400 is drawn in full into the margin. The HUD, text and panels keep the 320 page.
   The tags to route there are those emitted between the dispatcher's view passes (`1450` onward to the end of `9EFA`); the HUD pass `1058:0016` and panel pass
   `79BE` stay on the 320 page. (The routing point is the stage markers the observer already has: `frame_phase`, 'o' for the bands, 'h' for the HUD, 'p' for the
   panel pass.)

Parity checks for the build: with the widened `B5DC` and a wide page, a frame whose flat things all lie inside the 4:3 area must equal today's picture
byte for byte (the 901 checksums and the live CPU/GPU checks, with the 4:3 view); in the wide view the middle must equal the 4:3 picture's middle for the same
frame; the margins are checked against the turned-view reference.

Risks to settle before building: the replay's page model is 320 wide in `drawlist.c` (the wide page is new code); the GPU compositor takes the picture layer as a
320-wide game picture (its replace pass), so the wide flat layer needs its own path there; the library's own clip rectangle (viewport block at `[0344]`) is applied
by the game's drivers, so replaying with a wider clip must reproduce each driver's pixel rule (entries 22, 71/19, 31) exactly.

## Closing the open reading items (11 Nov 2026)

**The 25h+ marker sprites of `DC46` (read).** `[7D7E]` is not a table of kinds: `model_fill_stream` (`120A:0CE9`, the `line_edge` code at `0D78`) sets it to the edge record
(`5EEAh + 24 * number`) of the last edge it drew, whose first words are that edge's projected screen position and whose word +2 bit 7 means clipped away. In
`DC46`'s `generic_draw` (matched.c, after the shape draw `DEFE`): for a shape id `[4986]` below 100h (the small special shapes, for example 18h, a ground vehicle)
the walk adds a flat icon: ids above 25h take a sprite id from the word table at `[4ED2 + 2 * (id - 25h)]` (eight words in this theatre's dump: 6, 8, 0Ch, 10h, 14h, 18h,
1Eh, 24h) and the others take the edge record's two words, both skipped when bit 7 of either byte of the record's second word is set; the sprite is drawn with
`1B2E` (the block page) at a position made from the edge record's screen position (record word -A9h across and the object's byte +4 less 6Eh down). **These are
flat icons at projected screen positions: the 2D mechanism must carry them too.** Which game objects have shape ids below 100h in a flight (the ground vehicles
18h and the 26h to 2Dh kinds) is data; the code path is the same in every theatre.

**Theatres (item 10), view codes (item 9).** All the passes above are code, and none of it differs by theatre: the theatres change tables (model words, ring
tables, palettes, night settings), never which routines run, so no path can be missing in one theatre that is present in the audited one. What can differ is which of the
flat kinds occurs (for example other theatres' ground vehicles, or sprite model words with bit 400h in their tables, none of which occur in the Libya table I read).
Every view code (`[C0A6]`: 0 and 44h forward, 41h to 43h side looks, 84h, 85h, 87h, 88h, 89h, 8Bh, 8Ch) goes through the same dispatcher `03CE`, so the same passes and
the same hooks apply; section 10 of the flight display pass and the locks are gated to the forward cockpit view by the game itself.

**Items 7 and 8.** Remastered's own rasteriser replaces the original's clip and fill stages for 3D objects (item 7), and the per-object-state culls are game state
(item 8): neither can differ between the middle and the margins. Read at header level; the turned-view reference run is the check of both and is part of the
confirmation stage.

**State of the audit.** Reading: complete for the passes, the culls that depend on position, the flat drawing and its sources. Confirmation by running: not done
(the turned-view reference, the 2D overlay comparison on the owner's flight). The list of what must be added to the margins is final as far as the reading goes:
(1) far aircraft and projectile dots, (2) smoke-trail puff sprites, (3) the icon sprites of `DC46` (shape ids below 100h), (4) the flight display pass: weapon markers,
tracers, blast sprite, detection rings (ground and air), locks, bomb symbology, beacons.

## Better design for the 2D mechanism: margin-only camera-space overlays (11 Nov 2026, replaces the "wide page" draft above)

The wide-page draft needs a wider replay page, a new GPU path for the flat layer and an exact reproduction of every driver's clip rule. Remastered already draws camera-space
dots, lines and polygons through its own pipeline (`SCENE_DOTS`, `SCENE_LINES`, `remaster_prim`, both compositors), so the flat kinds can enter as camera-space primitives
instead, and no page changes:

- **Position.** Each flat kind has a world or camera-space anchor the game projects with `B5DC` (the three arguments are on the stack at the call; `air_depth` in the interim
  patch already evaluates the camera-space z exactly; x' and y' come from the same matrix with the undivided x row). The overlay is a primitive at that camera-space point,
  so the standard perspective projection puts it at the true place, the margins included, with no clamp and no squash.
- **Where it shows.** Only outside the 4:3 window (a screen-space mask on the primitive: it draws where the screen x is outside the original picture), so the middle stays
  the game's own picture, pixel for pixel, with no double drawing; inside the 4:3 area nothing changes by construction.
- **Look.** Per kind, from the matched code: a far dot is one game pixel (a block of the present scale) in the game's colour (15, or 29h/13 for projectiles); a tracer is the
  line between its two projected points in 0Dh/29h; the weapon box is `B171` (size 6, colour 11h/0Ah); the ring and label `B4F5`; the blast and puff sprites from the game's
  sprite pages; each parameterised by the arguments the game passed.
- **Hooks.** Read-only, at the return addresses of the `B5DC` calls (`9A04`, `9BDF`, `9D89`, `9DFD` and the `9EFA` sites) and at the drawing routines' entries, in the render
  copy only: they record (kind, camera-space point, colour, size) into the frame; the game's own drawing and its logic are untouched.
- **Parity checks.** With the overlay disabled the picture is today's; with it enabled and every flat thing inside the 4:3 area, the picture is byte-identical (the mask); the
  margins are checked against the turned-view reference for position.

First slice to build: the far aircraft and projectile dots (the owner's R41), then the puff sprites, then the flight display pass kind by kind.

## First measurement for the build (11 Nov 2026): the true dot position is exact, and how many dots are missing

`F117_WIDE_AIR_PROBE=1` (rerun.c, diagnostic; the game's matrix is left alone) hooks the return of the aircraft loop's call of `B5DC` (VGAME `9BDF`), recomputes the
point's camera-space x', y', z' from the unmodified matrix (the same fixed-point sums as `camera_body_axis`, which the depth predictor already reproduced exactly) and its
screen x as `(x' * 256) / z' + 0A0h`, and compares it with the `[4A10]` the routine stored. Over clocks 17.9 to 18.5 billion of the owner's flight (Remastered wide): **437 of 437
on-screen points match exactly.** The game put 3,500 aircraft points off screen in that window: 2,770 behind the eye (correctly skipped), 40 in front with the true x below -160,
347 at 320 to 639 and 120 at 640 or more (the right margin: the flight turned right of the aircraft), and 223 inside 0..319 across but outside the view's rows. So **507 aircraft
points in 0.6 billion clocks (about 10 seconds) were in the wide picture's margins and missing**, which is the owner's R41/R42 for the far dots. (The 223 in 0..319 are
cut by the original's row limit; the wide picture shows more rows, so some of them belong in the picture as well.)

## Margin dots built (11 Nov 2026): `F117_WIDE_DOTS=1`, default off

What is drawn and how it is proven (`rerun.c`, `wide_dot_at_entry`; `remaster_live.c`, `remaster_live_overlay_dot`):

- **Matrix per call.** Every call of `B5DC` sees the game's own matrix, so dots, smoke puffs (sized from `[DEBE]`), the flare's distance test and the flight display
  pass's markers keep the original's placement, size and cull to the pixel. Only a call that draws a 3D model gets a changed matrix: the aircraft and projectile loops, when the
  Remastered reach says model, get the reach-divided matrix with the widened x row; a flare model gets the widened x row alone. (Before this, the reach halved `[DEBE]` for
  every caller: R44 item 3, to be measured on the smoke puffs' size in the 4:3 area against the original.)
- **Position.** At the entry of a `9BDF` or `9D89` call, the point's true screen position from the game's own matrix, the routine's own arithmetic (camera offset in the
  outside views, `[294B]` halving, `x = x' * 256 / z' + A0h`, `y = q - (q >> 2)` plus the row offset). **45,500 dot calls predicted against the routine's own `[4A10]`/`[4A18]`,
  0 differ** (`F117_WIDE_DOTS_LOG=1`).
- **Look.** One game pixel: a square polygon, because a dot primitive is 1.5 pixels wide and centred on the corner. Colour 15 (aircraft), 29h (projectile slots 0 to 7), 13 (slots 8 to 11),
  from `9B16` and `9D39` (pen `886A`, then the far line call).
- **Where.** Only outside the game's 320 x 200 picture; inside it the game's own flat picture stands (no double drawing).
- **Owner's two dots at 18.207 billion** (the 12:58:48 screenshot): aircraft at true x 391-392 (the wide picture ends at 373 at 1280 wide, so correctly off screen) and x 33-35
  (behind the left cockpit pillar). The cyan ring around the left one is drawn by `AD26` at the same x: the ring is not yet drawn in the margins.
- **Found on the way.** Two return hooks took the far scenery walk's slot in the hook table (`EXEC_HOOKS_MAX` = live hooks + 2) and the city lights vanished from the margins
  with no message; the dots now work from the single entry hook and a warning prints when a render-copy hook does not fit.
- **Draw-distance gaps found while reading** (R43): aircraft are skipped at 4,800 units (`9B56`) at every draw distance; flares and smoke puffs are dropped at fixed depths; the
  Terrain dots setting stops dark dots at Near. Far size (`far_width`) should apply to all of them once they are drawn beyond the original's cut.

## Flight display pass: facts read from the matched code (11 Nov 2026), before building

All read in `src/matched/matched.c`; nothing here is inferred from a run.

- **`B171` `weapon_lock_marker(x, y, size, locked, pen)`** (matched.c 23667): `locked` = 0 draws a four-sided box, non-zero a six-sided hexagon. Half-width = size; half-height h = size - (size >> 2);
  hexagon vertices: top (x, y-h), (x+size, y-h/2), (x+size, y+h/2), bottom (x, y+h), and the mirror on the left (h/2 = h >> 1). With `[294B]` set the size is halved first. Nothing is drawn when `[368C]` is 0
  (the cockpit view's flag). The pen is set through `886A` (the raw palette index, both text contexts).
- **Callers of `B171`** (the symbology routine): weapon in flight, size 6, unlocked, pen 11h for slots fired at the player (the first player weapon index and above are the player's: pen 0Ah); ground-target
  marker (section 4a), size effect + 5 (9 with no effect), locked or not, pen `FR_MARKER_PEN` (the HUD pen; 29h tight lock; 6 when the weapon is no use); air-target marker (7), size 9, pen the HUD pen or
  29h tight; bomb impact (10), size 5, locked, pen 29h; and `B4F5` below.
- **`B4F5` `panel_marker_label(text, pen, margin)`** (24061): nothing when `[4A10]` = FFFFh. The hexagon (locked, size `margin`, the given pen) is drawn only when the point is more than `margin` from
  every edge of the 320 x 92 window: `margin < x < 13Fh - margin` and `margin < y < 5Ch - margin`. The text (`898F`, colour `[98A2]`) is written only inside x 14h..118h and y 1..4Ch, centred under the
  point (x minus twice its length, y + 5). Callers: the ground site's detection ring (4b, `A50D`) and the air unit's (9, `AD44`), both with `margin = [FRAME_RATE] - [DETECT_COUNTDOWN]` and pen `[LABEL_COLOUR]`
  while `DETECT_COUNTDOWN > 0`; the site is projected at z = 0 (ground) or at the unit's position (air). `DETECT_COUNTDOWN > 0` and a ground `DETECTOR` >= 0 or an air one < 0 are game state: a ring in a margin
  shows only what the game's own state says (a detection), never a decision of the copy.
- **Consequence for the margins.** A ring whose true x is outside 0..13Fh is dropped by `B5DC` (`[4A10]` = FFFFh) and by the window test; the overlay draws the same hexagon at the true position, size `margin`,
  pen `[LABEL_COLOUR]`, when it fits in the wide picture (the game's own edge rule applied to the wide window). The text label is drawn by a text routine into the 320 x 200 page: the margins need a text overlay
  (open: the font, from the game's own `898F`) and until it exists a margin ring has no name, which is a stated limitation, not a design choice.
- **The owner's ring at 18.207 billion** is the air unit's ring at true x 33-35 (inside 0..13Fh, so the game drew it itself); in the squashed build it was shown at the wrong x, in the margin-dot build the
  game's own flat picture holds it at x 33-35, and in the frames checked no cyan pixel shows there: not yet verified whether the cockpit pillar covers it or the ring is missing (to check against the original's
  picture at that clock in the turned-view reference run).
- **HUD pen** `[2CA0]`: 10 (light green) when `[43DE]` is non-zero, else 15 (white), set at VGAME 09D8; `[43DE]` = 1 when `[368A]` is 0 (flight start, 00F0); `[368A]` = byte +33h of the pilot record.
  Pens 10h to 4Fh are stepped toward a target palette by `frame_palette_cycle` (04E5F), so their colours depend on the time of day; the renderer takes them from the live palette (`dac_colour`).

**Owner decision (11 Nov 2026), far size:** one size-by-depth rule for every light and dot (terrain lights and dots, aircraft, missiles, bullets, flares): one game pixel x reference / camera depth beyond a single reference depth, never under one screen pixel, the same reference for every kind, so an aircraft light and a terrain light at the same depth are the same size. Open before building: read in the matched code how ring units relate to map units and to camera depth, and choose the reference from the original's own reach (today `far_width` uses 8191 units of the object's ring; the aircraft range 4,800 is a flat octagonal map distance, not a depth).

## Turned-view reference, first run (11 Nov 2026): owner's flight, clocks 18.205 to 18.259 billion

Method (`F117_TURN_VIEW=N`, `F117_VIEW_LOG=1`, `private/owner-flight-1110/turnrun.sh` and `turned.py`): the render copy's pilot is turned by N units of the game's angle by adding to the heading `[2DEE]` before the frame is drawn (matched.c `compose_forward`: `[E478]` = `[2DEE]`, the scene's view matrix at `49BE` is built from it), so the game draws the same world from a heading turned by N: its own 4:3 picture of the turned view, in Remastered wide's middle. Rotation about the eye has no parallax, so a margin pixel of the unturned wide picture maps to a pixel of the turned picture through the two view matrices and the screen formulas (x = A0h + 256 x' / z', y = off + 192 y' / z'); the median colour of each 36 x 36 pixel block of the margin is compared with the warped turned picture's.

Facts found while doing it: the matrix at `B082` (what `B5DC` uses) is copied from the aircraft's own state and is NOT changed by the heading poke, so the turned view tests the scene (terrain, models, sky and ground bands, drawn from `49BE` and the camera at `7B28`), not the flat `B5DC` things, which have their own exact check; `B082` and `49BE` differ by the sign of the altitude axis (M_B = D M_49 D, D = diag(1, -1, 1)).

Result, 10 shots: turned left (the left margin) 250 blocks compared, 0 differ; turned right (the right margin) 180 blocks, 10 differ: the same one block in every shot, at game x 355-367, rows 36-48, where the wide picture's sky is dark blue (28, 28, 56) and the turned view's is purple (78, 35, 80): a sky band colour at the extreme right edge (the wide picture extends each band's lines straight along their own direction; the original's band boundaries are the game's own straight-line approximation relative to its own view, so at that angle the two views' bands are not the same lines). No missing terrain, model or building in either margin at this moment.

Limits, stated: one moment of one flight (a coast at dusk); the comparison covers about half of each margin (what the turned 4:3 view can see at 25 degrees) in the view's upper 90 rows; blocks of medians do not see single dots, thin lines or lights (those have the exact prediction check); the HUD of the turned picture is in the warp, which is why medians are used.

Second run (`turnrun2.sh`, `session-20261108-211031`, the gate's clocks 9.5 and 10.35 billion, 2 shots each, `--model-detail full`, turned left and right): 196 blocks compared, 1 differs (turned left, clock 9.5 billion, game x about -17, row 72: dark land (15, 24, 28) against sea blue (13, 24, 74)), on the coastline edge where the land strip is a few screen pixels thicker in one picture: resampling at an edge, not a missing object. Over both runs: no terrain, model or building missing from either margin; one sky band colour difference at the extreme right edge (above). The 3D side of the margins is confirmed at these four moments within the stated limits; the reading-based conclusion ("no 3D gap") stands. Still to run: the 2D overlay comparison (needs the flight display pass built first), and the runway at its own clock.

## The owner's "ping" ring at 18.207 billion (11 Nov 2026): what the original draws

Rendered the same clocks (18.210 to 18.258 billion, 9 shots each) as the original picture (`PRES=original`, 320 x 200) and as Remastered 4:3, and counted the ring's cyan pixels in the 3D view's rows: **0 in both**; in the current default wide build also 0 near the ring's true position (game x about 34, y 85, from `F117_WIDE_DOTS_LOG=2`: caller `AD26`, the air unit's ring, true position inside the 320 x 200 picture, prediction "on"). The ring in the owner's "before" video was drawn at a wrong place by the old squashed matrix (a side effect of R41, like the dots), so there is no ring missing from the margins at that moment: the real game's picture has none there. **Not yet explained from the code:** why the original does not draw it although a reading of `B4F5`'s own box test (x > margin, x < 13Fh - margin, margin < y < 5Ch - margin, margin = `[FRAME_RATE] - [DETECT_COUNTDOWN]`, about 5 here) passes at (34, 85). Settle it in the ring slice by logging `B4F5`'s arguments and `[4A10]`, `[4A18]` at its entry in the copy, before building the ring overlay; the overlay must follow what the original does, not what a reading predicts.

## The HUD clip box: why the ring is not in the picture (11 Nov 2026, read and measured)

**Measured** (`F117_RING_PROBE=1`, hooks at `B4F5` and `B171` in the copy, clocks 18.203 to 18.24 billion): the game DOES call the ring label and the lock marker for the owner's air unit every frame: `B4F5` with margin 0, 1, 2, ... (`[3D98]` countdown 15 down, `[368E]` S = 15, detector -2 = air unit 1), then `B171` at x 34, y 85, size = margin, locked = 1, pen 11 (cyan). The unit's true position (34, 85) is inside the 320 x 200 picture. Yet the original picture, Remastered 4:3, and the render copy's own picture show no cyan there.

**Cause, from the code** (matched.c 14330, `vgame_panel_hud_line`, VGAME `881F`): "line inside the HUD's current clip box. Compact mode ([294B] set) uses (99, 30)-(220, 90), full mode (48, 15)-(271, 90)". Every line a HUD marker draws goes through `881F`: the lock marker (`B171`, so also the detection ring `B4F5`, the weapon boxes, the bomb impact marker), the diamond `B0C2`, the tape and ladder lines. The game itself clips them to that box, so in the real game **no marker, ring or box is ever drawn left of x 48, right of x 271, above y 15 or below y 90, even inside the 4:3 picture.** The ring at x 34 is outside the box: clipped away entirely.

**Consequence:** "what the 4:3 area draws" of the flight display pass is limited to that box. The margin ring/marker overlays I planned would draw things the original never draws anywhere outside the box, the middle included. This is a decision for the owner, not a build detail (asked 11 Nov, see handoff). Tracers (`9F94`, line clipped to the active block, and "the gun's reach is the window"), the far dots (`9B2C`), the blast and puff sprites and the `DC46` icons have their own clips and are not covered by this box: to be read one by one.

## Smoke puffs: facts read from the matched code (11 Nov 2026), and the overlay built from them

All read, none inferred. The trail loop (`9A04`, matched.c 34606) calls `B5DC` for each of the 16 puffs of the trail table and draws only when `[4A10]` is not FFFFh and -256 < `[DEBE]` < 0. The size is `camimage_scale(age, |256 / [DEBE]|)` (the IDIV at 9A21, `EE0C` the absolute value, far `114A:0461`, matched.c 16732: age 0: v/2; 1: v/2 + v/8; 2: v/2 + v/4; 3: v/2 + v/4 + v/8; any other age: v). `D9FE` (matched.c 61410) is called with nine words: the model word 400h + ((age & 14) >> 1), the east and north longs, the height, and the three words x = `[4A10]`, y = `[4A18]`, size; it culls and defers, and the compose walk `DC46` (matched.c 44651) draws a deferred record with bit 400h by `1AE9`(x = +10h, y = +12h, size = +14h, kind = model & 7), the sprite blit `compose_draw_sprite` (matched.c 9096): the sprite is the far pointer `[kind * 4 + 920A]:[kind * 4 + 9208]`, top left (x - half(size), y - half(t)), drawn width size and height t = size - (size >> 2) (half(v) = (v - sign v) >> 1). The blitter (`MGRAPHIC 0C95`, matched.c 83986) takes the sprite's width and height words and a record per row ([n: word][k: word], n pixel bytes, colour 0 transparent) and scales with Bresenham error terms (vertical: start -t, add the sprite's height per drawn row, step the source row while not negative, subtracting t; horizontal the same with the widths); it clips against the block rectangle (`0BC0`, matched.c 84519) which in the renderer is the game's 3D clip window `[85FA]..[8600]`.

Built (`rerun.c` `wide_puff_overlay`, called from `wide_dot_at_entry` for the call returning to `9A04`; `remaster_live_overlay_rect`): the whole sprite is generated by that algorithm and only its pixels outside x 0..319 are added, runs of one colour as one rectangle, rows inside the 3D clip window, in front of the scene. The 4:3 picture is untouched (a puff the game dropped at the edge is not drawn inside it either). `F117_WIDE_PUFF_FULL=1` (diagnostic) draws the whole sprite over the game's own: the picture must not change when the decode, scaling and placement are exact; that proof, and the first puff in a margin on a real flight, are still to be made. Known limitations, stated: the overlay is in front of everything, where the game depth-sorts the deferred sprite with the other objects (a puff behind a nearer object would show over it in the margin); and the margin puff takes the game's own matrix (R44 item 3 is the check of the puff size in the 4:3 area).

## One far-size rule: the reference depth (read 11 Nov 2026, to settle before building R43)

From the code, not measured: a ring-n terrain object's reach is 8191 units of its own ring (`remaster_live.c` `far_width`; `rerun.c` 1019: "the finest level where its camera depth is under the game's reach, 8191 units"), its real depth is the depth in ring units times 4^(n-1) (`remaster_live.c` 1831), and a ring-1 cell is 4096 world units (`rerun.c` 372), so the nearest ring's reach is 8191 world units of camera depth, a ring-2 object's 4 times that, and so on. `B5DC` measures its z' in the same world units (the offsets `[C0D0] - x`, `(alt - [2DF4]) >> 5` times the matrix with 1.0 = 32767 through `fixmul`), and the aircraft test's range (`unit_range`, 4,800) is in the same map units but is a flat octagonal distance, not a depth. **Assumption, to verify on a real frame before relying on it:** one ring-1 unit is one world unit of `B5DC`'s z'.

Proposed single rule (one game pixel x R / camera depth beyond R, never under one screen pixel): R = 8191 world units, the nearest ring's own reach, for aircraft, projectiles, flares, puffs and every ring-1 terrain light and dot, so an aircraft light and a ring-1 terrain light at the same depth are the same size. A light on a coarser ring's object keeps its own ring's larger reach (it is full size to 4, 16, 64 times 8191 in the original; shrinking it at 8191 would change what the original draws inside its own range). The owner's rule "same depth, same size" therefore holds between fine objects; for coarse ones it conflicts with "inside the original's distance it stays one game pixel". Reported to the owner as a limit of the single rule, not hidden.

## The two clip rules for flat lines (11 Nov 2026, read and measured)

**Measured** (`F117_WIDE_DOTS_LOG=2`, the active text block's rectangle read from the record at `[4010]`/`[4028]`, words +14, +16, +18, +20, matched.c 13934 `vgame_raster_line_block`, over 18.2 billion clocks, every caller): rows 0..90 and columns 0..319, always, at every call tried (callers 9BDF, 9F49, AD26, and the walk). So `114A:0006` (the tracer line `9F94`, and the far dot's line `9B2C` if it goes through the same routine) clips to the 320-pixel width and the 3D view's 91 rows, which is the 4:3 picture itself.

**Two different clips, so:** HUD marker lines (`B171` hexagon and box, `B0C2` diamond, the ring) go through `881F` and its clip box (48, 15)-(271, 90) (compact (99, 30)-(220, 90)); tracers and far dots clip to the whole 4:3 view (0..319 x 0..90). A margin overlay for tracers and dots therefore follows the second rule (the picture's whole width, any row the wide picture shows), which is what the margin dots do; HUD marker overlays follow the first, and for them the margin is empty (the pending owner decision, option A/B/C).

**Owner decision (11 Nov 2026 19:20): option A.** HUD symbology (`B171` markers and the detection ring `B4F5`, weapon boxes, the bomb impact marker, the diamond `B0C2`) follows the original's clip box (48, 15)-(271, 90) in the margins as everywhere: nothing is added for them, so the owner's three earlier symbology answers (plain box in the margins, bomb pin at the wide picture's bottom) become moot for these kinds. Margin overlays continue for what the original draws with the full-width clip: far dots, tracers (`9F94`), and next the blast sprite (`1A98`), beacons and the `DC46` sprites, each after reading its own clip. B (draw everywhere) is a possible later off-by-default setting.

## Gun tracers in the margins (11 Nov 2026): built, margin path proven on a real tracer

Facts read from the matched code (display pass section 2, matched.c 68428): the tail is projected by the `B5DC` call returning to `A203`, the head (half a velocity step ahead) by the call returning to `A236`; if either ends off the 320 screen the slot is skipped entirely, "the gun's reach is the window"; otherwise the pen (0Dh player slots, below `[B198]`; 29h enemy) is set and `114A:0006` draws the line from head to tail, clipped to the 320 x 91 active block (measured 0..319 x 0..90). The hit tests are game logic and run only for tracers the game draws; a tracer in a margin cannot hit, as in the original.

Built (`rerun.c` `wide_tracer_line`, `rerun_collector.overlay_line`, `remaster_live_overlay_line`, `F117_WIDE_TRACERS=0` disables): for a tracer with both ends in front of the eye and either end off the 320 screen, the part of the line with x < 0 and the part with x >= 320 is added in the game's pen, one game pixel wide, in front of the scene; the 4:3 picture is untouched.

Proof: on `session-20261110-125342` there is a real tracer at clock 11.0597 billion (slot 17, an enemy gun, pen 29h, tail (148.5, 66.5), head (148.5, 67.5)); with `F117_WIDE_TRACER_SHIFT=-170` (diagnostic: its x moved 170 game pixels) the overlay drew it at (-21.5, 66.5)-(-21.5, 67.5) and exactly the predicted pixels changed in the wide picture (x 94..96, y 198..203, red) in one frame and none in the other eleven; with the shift off no pixel of any of twelve frames changes (the overlay is under the game's own pixels). Limits, stated: a tracer with one end behind the eye is not drawn (the game does not either; a true clip against the eye plane is not done); no tracer ended in a margin in either scanned flight, so the unshifted margin case has not been seen on real data; the first version of this code never ran (an early return) and the earlier "0 differ" scans covered dots only: the scan after the fix covers tracer calls too.

## Smoke puffs: proven on staged puffs (11 Nov 2026)

No recorded flight has a smoke trail (ten long sessions scanned, and the committed `airair_type5` route replayed to 11.7 billion clocks: no puff call produced). So the proof stages puffs: `F117_STAGE_PUFFS=1` (rerun.c `stage_puffs`, render copy only, the real game untouched) writes four puffs per frame into the game's own trail table (DS:3A08, eight bytes each: east, north, altitude, age) at world positions solved from the live matrix `B082` to project to chosen screen points at [DEBE] = -8; the game's own puff loop (`9A04`) then draws them through its normal sprite path. Staged sizes and kinds agreed with the rule read from the code (x 8: size 20, kind 0; x 312: 24, kind 1; ages 0 to 3 give sizes 16, 20, 24, 28).

Result for the puff at x 8 (size 20, rows 33..47, left edge at x -2), against a run without staged puffs: **the generated sprite and the game's own footprint are the same pixels in the 4:3 area: 216 of 216, 0 only in the game's, 0 only in mine, the shape identical row by row; each of my three palette indices (76, 77, 78) maps to exactly one colour in the game's picture ((16,16,32), (12,12,24), (8,8,16)) across all 216 pixels.** The margin run adds exactly the 99 screen pixels (11 game pixels, x -2 to -0.3) outside the picture in those same colours, and nothing inside it. So the run-length decode, the Bresenham scaling (`0C95`), the placement and the size rule (`camimage_scale`) are exact for these ages.

Limits, stated: ages 0 to 3 only (the four staged), the sprite kinds 0 and 1; the overlay is in front of everything where the game depth-sorts the sprite with other objects; the same proof on a real smoking aircraft or missile trail is impossible until a flight has one. `F117_WIDE_PUFFS=1` stays off by default until the owner has seen the before/after pictures.

## Real game-emitted puffs, the blast, and layering (11 Nov 2026, owner's requests)

**Real puffs.** `F117_EMIT_PUFFS=START:DSBASE:ZC:XC` (host `main.c`, diagnostic) sets only the game's own smoke-source state on the real machine: `[3D96]` = 0 (the object whose trail `frame_trail`, matched.c 2023, draws) and that object's position at `B2D0`/`B2D2`, solved through the live view matrix to a point at view depth ZC and lateral offset +-XC (the side flips every 40 million clocks). Nothing is written to the trail table and nothing is drawn by the diagnostic: `frame_trail` makes a puff every eighth frame at that position, rises and ages them, and the game's own loop (`9A04`) draws them. (VGAME's data segment is `37AE` here, from the view log; an earlier guess of `37DE` was wrong.) On the owner's flight near 1.2 to 1.5 billion the aircraft is on the ground (altitude 0), where the game's fixed puff altitude 40h is just above eye level.

**Placement against the game's own puffs** (sources at depth -200 and lateral 70, inside the picture): over 24 frames **all 1,284 pixels the game drew for its puffs in the 4:3 area are pixels my generated sprites cover at the same position, 0 uncovered**; the overlay adds pixels only in the margins and none in the middle (with the sources at depth -120, lateral 90). Colours were verified one-to-one on the single staged puff and the blast (above), not in this multi-puff run (its sprite union merges calls).

**Blast sprite** (`1A98`, same driver call as a puff's, its own table at `91B8`, size `256 / [DEBE]` doubled on the ground, phase `10h - |[9D3C]|`): on a staged blast at screen x 100, **67 of 67 pixels identical** to the game's own blast in the 4:3 area, 14 palette indices each one game colour; at x -30 it adds 603 pixels in the left margin and none inside.

**Whole sprite where the game dropped it.** The game drops a puff or blast whose centre projects off its 320 screen; the first overlay then drew only the outside part, leaving a hard vertical cut at the picture edge. Now: centre on screen (the game draws it): the overlay adds only the outside part, continuing the game's; centre off screen: the whole sprite, including the part overhanging into the 4:3 picture (a limit of "the 4:3 middle is identical": there the middle gains the overhang the original, dropping the sprite, does not draw; nothing is drawn twice).

**Layering audit (owner, 11 Nov 21:02, after lights showed through a margin puff).** Cause: the overlay polygons carried owner `FFFFFFFFh`; the rasteriser (CPU `remaster_raster.c` 264-273 and the GPU shaders `polys*.hlsl`, `strokes*.hlsl`) takes an owner with bit 11 (`800h`) set for ground, which never hides another object's lines, so every distant light drew over the overlay. Fixed: owner `FFFFF7FFh`. Draw order of the original, kept by one depth layer per kind (0.2 percent apart, so a tie cannot reorder them): far dots (moving-objects pass `983E`, flat) at the back, then puffs (deferred, drawn by the compose walk `D87C`), then tracers and last the blast (display pass `9EFA`, the last thing the frame draws). Known layering limits: a puff is depth-sorted with other deferred objects in the original (an aircraft model nearer than the puff covers it), the overlay is in front of all models; the puff and blast overlay clip rows to the 3D window `[85FA..8600]`, which is read in the renderer but NOT verified against the blitter's own block rectangle (the staged and emitted puffs stay inside rows 33..90, so the clip has not been exercised).

## Beacons and the DC46 icon sprites: what they are (11 Nov 2026, read from the matched code)

**Radio beacons** (display pass section 11, matched.c 70743): a diamond (`B0C2`, "the projected diamond at SCREEN_X/Y") on the selected target and the label "Radio Beacon" (`B4F5` with a pulsing margin) plus messages. Both are HUD routines whose lines go through `881F` and its clip box: under the owner's option A (11 Nov) they need and get no margin overlay.

**DC46 icon sprites** (compose walk, matched.c 44648 to 44812): after an object's model is drawn (`120A:00CF`), kinds 25h and above, and kinds below 100h, take a four-byte record through `[7D7E]` (the model renderer's last edge record, an x word and a y word; the sprite is dropped when `y & 8080h`, i.e. the point is outside the renderer's range) and draw a small label sprite with `1B2E` (the same driver sprite call as a puff's, page `[4070]` / `[0344]`) at that point less (6Eh, A9h): the sprite index is `[record + 6] >> 7` (kinds below 25h) or the word at `[(kind - 25h) * 2 + 4ED2]` (above). So the position is a vertex of the object's own projected model, set inside the model renderer's clip stage, not a `B5DC` point. Open (not built): the label's anchor in the wide picture is the same model vertex in camera space, which lives in the scene primitives the native path already builds, but which vertex `[7D7E]` ends at depends on the model's edge order and has not been traced for any model; needs a flight with such an object to prove. Stated plainly as the one 2D kind still missing from the margins.

**DC46 icons, the anchor (read 11 Nov 2026, matched.c 54346 `model_fill_stream`).** `[7D7E]` is written only by the stream's LINE command (40h + n edges, label `line_edge`, 0D78): for each edge number it sets `[7D7E]` = the edge record `5EEAh + 24 * number` (whether or not the edge is then drawn; word +2 bit 7 = clipped away). So the icon anchors at the last line edge of the object's model, in the order the model's command stream lists it, a property of the model data, reproducible from it. DC46 then reads the record's words 0 and 2 (rejecting `word2 & 8080h`) and 4, and calls `1B2E(word0 - A9h, word4 - 6Eh, ...)`; which screen coordinates words 0 and 4 hold (and relative to what origin) is the next thing to read (`model_edge_spans` / the edge projection), before building.

## Missiles and bullets: how deep they get (11 Nov 2026, measured on both recorded flights)

`[proj-depth]` (`F117_WIDE_DOTS_LOG=1`, rerun.c, in the projectile call's hook, world units of depth = -z'): flight 1 (session-20261110-125342, to clock 12.2 billion): deepest 634; flight 2 (session-20261108-211031, to 10.4 billion): deepest 5,376. Both under the far-size reference 8,191, so the one-game-pixel dot the game draws stays exact for them in these flights; the far-size rule would not change any projectile here. Limits: two flights, one projectile record each; longer or other missions could go deeper (the weapon table's longest lock range is 150, about 9,600 map units). Decision carried: keep the game's one-pixel dot for missiles and bullets; re-measure if a longer flight shows a projectile past 8,191.

## R44 item 3 measured (12 Nov 2026): puff size and place do not depend on the Remastered reach

Same game-emitted puffs (`F117_EMIT_PUFFS=1200000000:37AE0:-120:90`, owner's flight, Remastered wide, from clock 1.24e9), `F117_WIDE_DOTS_LOG=3` (new: logs every puff, inside the 4:3 picture too), run once with the game's own distance (`F117_WIDE_AIR_REACH=100`) and once with the default reach (200):
**1,296 puff records (x, y, size, kind) identical in both, and the four screenshots (clocks 1.255e9 to 1.273e9) are byte-identical.** Sizes seen: 8, 10, 12, 14, 15, 17, 18. So in the 4:3 area the puffs
take the game's own matrix and so the original's size and place (the copy runs the game's own code on the game's own matrix; no matrix write happens for a puff call). The same run with `F117_WIDE_DOTS=0`
(the old path, which divides every caller's matrix by the reach) gives different pictures at all four clocks, so the halving did change what was drawn; its puff sizes were not logged (the puff overlay
exists only with dots on), so the "about 1.4 times" figure stays an estimate and is not a measurement. Not done: a render in the original presentation of the same state (the equality above is between
two Remastered runs on the copy's matrix, which is the game's own).

## Far smoke puffs built (12 Nov 2026), far falling wreck built but unproven and OFF

**Far puffs (on with `F117_WIDE_PUFFS`, default on).** The trail loop draws a puff only when -256 < `[DEBE]` < 0 (matched.c, 9A0E/9A15). For a puff past that (`[DEBE]` = z' >> 3 at or below -256, 32-bit, so the game's
16-bit wrap beyond 32,767 does not matter), `wide_puff_far` (`rerun.c`) draws a square of the true size, 256 / |`[DEBE]`| times the age factor (1/2, 5/8, 3/4, 7/8, 1) game pixels, centred on the true
projected point (the routine's own arithmetic), with the compositor's minimum of one screen pixel, in the puff sprite's **most frequent non-transparent colour** (kind `(age & 14) >> 1`, table 9208/920A), rows clipped to the 3D window,
layer 2, middle and margins alike. The colour is a choice, not a game value: below one game pixel the blitter's footprint is one source pixel, often transparent, so the game has no answer; the mode is
the one that stays closest to what a coverage-weighted reduction of the sprite would show. Also not modelled: the sprite's transparent fraction (a far puff is an opaque pixel).
Proof, game-emitted puffs (`F117_EMIT_PUFFS`) at view depth -3000, -2200 and -9000 (`[DEBE]` -375, -275, -1125): the log predicts x, y and size (`F117_WIDE_DOTS_LOG=3`, x 245 for XC 1000 as computed by hand:
160 + 1000 * 256 / 3000 = 245.3); against the same flights with `F117_WIDE_PUFFS=0` the pictures differ in **1 to 2 pixels per frame, all at the predicted place, none elsewhere** (nothing else changed): in the
margin (x 29 and 1253 at 1280 x 600) and inside the 4:3 area (x 182 and 1100). A puff whose place lands on a HUD line (the pitch ladder's row) is hidden by the line, as the original draws HUD after
puffs. At dusk the smoke colour (index 77, RGB 12 16 28) is almost the sky's, so the pixel is hard to see; the picture `Desktop\ONShots\20261112-115546-far-smoke-puffs-off-vs-on.png` marks it.
Also in this slice: `F117_WIDE_DOTS_LOG=3` logs every puff (R44 item 3, above).

**Far wreck (`F117_WIDE_WRECK=1`, default OFF).** `9DFD` is the call for the falling wreck (matched.c 34803 to 34828: `[C62E]` > 0, point at `[C0D2]`, `[C0E4]`, height `[C62E]`; model 11 by `BB0D`, only when -256 < `[DEBE]` < 0). For `[DEBE]` at or
below -256 the render copy divides every row of the widened matrix by k = |`[DEBE]`| / 128 + 1 before the call (the same mechanism as the aircraft reach: screen position and perspective size are ratios and
do not move; the game's own test then passes and the model is drawn at its true place). `F117_EMIT_WRECK=START:DSBASE:ZC:XC:HEIGHT` (diagnostic, `main.c`) puts the game into the state `vgame_ai_destroy` sets (`C0D2`, `C0E4`, `C62E`, `B782` = 80h),
re-set every 20,000 clocks because `vgame_free_fall` (04958) ends the fall within frames. **Not proven, therefore off:** with that state the game reaches the call (`[wreck-call]` log: depth -38 and -188, model 1, height held) and calls `BB0D`, but
**no pixel changed, even at `[DEBE]` -38, in the original presentation either** (320 x 200 shots with and without the state, 0 differing). So the staged state does not reproduce a visible wreck: something `vgame_ai_destroy` sets besides those four words
(or the model's own data) is missing, or model 11 is invisible in this scene. Next: read what `BB0D` does with shape 11, or find a real kill in a recorded flight, before the far path can be judged.

## DC46 icons: what the edge record holds (read 12 Nov 2026, not built)

The edge records at 5EEAh + 24 * n are written by `model_prepare_edge` (12BDB, matched.c 10566): both endpoints in front: each endpoint's projected x and y plus the screen origin (`[8602]`, `[8604]`) go to the record (+0, +4 for the first endpoint, +8, +0Ch for the second) and then the clipper (130D:033F) writes the clipped ends to +10h and +14h; an endpoint behind the eye (x word 8000h) takes the near-plane cut path and sets 4000h/4040h in +2; both behind sets 8080h. So word 0 and word 4 of the record `[7D7E]` points at are the
first endpoint's screen x and y in the model renderer's own pixel frame (origin included), and `DC46`'s reject test `word2 & 8080h` is "both ends behind, or clipped away". Consequence for the wide picture: the render copy runs this code with the widened view, so the icon call `1B2E` already receives
wide-canvas coordinates; what is missing in the margins is only the sprite blitter's clip to the 320-pixel block, exactly the puff case. The build is therefore a second sprite overlay at the `1B2E` call (generate the sprite by the same Bresenham routine, add the pixels outside 0..319), plus a check that the clipper's window ([7D72]..[7D78]) is wide in the copy (otherwise an edge ending in a margin is flagged 8080h and the game drops the icon first). Open: a free exec-hook slot for `1B2E`'s entry (the copy's limit is the live hooks plus 2, both used by the `B5DC` entry and the `04E6` walk), the sprite-index arithmetic (`[record + 6] >> 7`, or the word at `[4ED2 + 2 * (id - 25h)]`), which objects in a real flight have shape ids below 100h (scan), and a staged proof.

## Verification pass, 12 Nov 2026 (13:30 to 14:00): R44 against the original, the wreck, what is still not known

**R44, same state in the original presentation and in Remastered wide (done).** The same game-emitted puffs (`F117_EMIT_PUFFS=1200000000:37AE0:-120:30`, owner's flight, from clock 1.25e9, 8 shots), each presentation against a control with no staging. Original (320 x 200): the staged state changes two blobs,
game x 92..100, y 46..52 and x 219..229, y 44..52 (the second puff grows to 91..101, 219..230 at the later frames). Remastered wide (1280 x 600, 3 pixels per game pixel, origin -53.33): the two blobs sit at the same game boxes and every changed original pixel maps onto its 3 x 3 block (0 changed pixels outside the mapped blocks; one stray pixel in the last frame). In all but 4 blocks per frame
the block colour equals the original's pixel; the 4 exceptions are blocks the HUD's grey line (RGB 170,170,170) crosses in Remastered, partly covered, so they are not size or place errors. The copy's own log gives the same puffs (x 96 y 49 size 8; x 224 y 48 size 10). **So the puff size and place in the 4:3 area equal the original's.** (Scripts: `private/owner-flight-1110/r44v.sh`, `r44cc.py`, `r44px.py`.)

**Far wreck: still unproven; what is now known.** (1) The staged state (`F117_EMIT_WRECK`) reaches `9DFD` with the game's own matrix at `[DEBE]` -10, -38, -80 and -188; `BB0D`'s own range test passes (`[wreck-call]` log, `F117_WIDE_DOTS_LOG=3`: relative x, y, the scale exponent and the 16-bit test are computed from the same words as matched.c 48553). (2) No picture changed, in the original or the copy, at any of those depths. (3) The Remastered copy emits model 11's faces ("added 4") 871 times in the staged run and 871 in the no-wreck control, 93 requests each at the same positions: model 11 is ordinary scenery, not the wreck, and the staged wreck request is not among the faces (or is dropped before the face stage). (4) The next gate after BB0D's range test is `E2B6`'s camera cull `E3BF` (`camera_cull`, `[48E6]` range, `[48F6 + 2 * [49AA]]` near limit, frustum): not yet read to the end. (5) The recorded kill flight (`sess_airair5`, replayed to 11.7e9 in Remastered wide) never reaches the wreck call (0 `[wreck-call]` lines, so `[C62E]` was never above zero when the call was reached): that route's kill does not leave a falling wreck, so it cannot serve as the proof. **A real wreck needs another recorded kill** (an aircraft shot down in a flight with `[C62E]` above zero), or the `E3BF` reading. Until then `F117_WIDE_WRECK` stays off.

## DC46 icons are camera-MFD labels, not forward-view objects (read 12 Nov 2026, matched.c)

`1B2E`, the call `DC46` makes for an icon, is `vgame_draw_sprite_block` = `draw_sprite(m, 0x4070, 0x1B2E)` (matched.c 9140): the sprite goes to the page held in `[4070]`, the block page of the reconnaissance / camera MFD image (`vgame_camera_image`, VGAME BCEF, matched.c 34853: it stamps `[4070]` at 34887/34888 and clears its window with A9h, F5h, 6Eh, A9h on that page at 35049). `DC46`'s label draw subtracts that window's origin from the icon point (`word4 - 6Eh`, `word0 - A9h`, matched.c 44798). The smoke puff's `1AE9` uses `[0344]` instead. So the icons are labels inside the camera MFD's own picture, never in the forward view: **no margin work, item closed.** The earlier text above ("flat icons at projected screen positions: the 2D mechanism must carry them too", and the earlier "not in the margins" lines) was written from the walk's structure, not from the page: superseded by this section.
The F3 navigation grid (`vgame_navigation_grid`, VGAME 8A90) is a separate MFD display: grid axes, air units, weapon trails, world objects, ownship and countermeasures, each world line clipped to its own inset rectangle (`navgrid_line_args`: A9h, 6Eh, 96h, 4Ah).

## Far wreck: where the original stops drawing it (measured 12 Nov 2026, staged `F117_EMIT_WRECK`, original presentation, height 64)

View depth z' (the staged ZC; `[DEBE]` = z' >> 3) against the pixels the staged wreck changes in the game's own 320 x 200 picture, against a no-wreck control:
z' 16: 24 pixels; 24: 14; 32: 7; 40: 8; 48: 5; 49: 5; 50: 5; **51 to 128: 0**. With the lateral offset 0 the same cut: z' 48 draws 3 pixels, 52 and beyond none. So the original draws the wreck up to a sharp depth (50 to 51 here) and nothing past it, at any lateral offset: a draw-distance cut,
well inside the `[DEBE]` -256 test of `9DFD` (matched.c 34805/34807) and inside `BB0D`'s 16-bit range test (the `[wreck-call]` log says "range in" at every depth tried). The next gate in `E2B6` is `camera_cull` (matched.c 13347, `E3BF`): the z high word against `[48E6]` (13332 here) and against the detail class's near limit `[48F6 + 2 * [49AA]]` (-3, -67, -263, -526, ... at 48F6; logged with `[wreck-cull]`), then the frustum; the exact comparison that cuts at 50/51 is not yet derived from the code (the class `[49AA]` is set per model by `E2B6` from the table at 0A60 for model 11). **Colour:** every pixel the wreck changes in the original is black (RGB 0,0,0, palette index 0), one dark red (40,0,0) at z' 16. So the wreck is a black silhouette: past the cut a dot would be black, hard to see on a dark sky and visible on lit terrain.
Open before building the dot: (1) derive the cut from `camera_cull` and the model's class (so the dot starts exactly where the game's model stops, a number from the code, not 50); (2) check index 0 is drawn by the overlay as black (the puff overlay treats 0 as transparent); (3) the staged wreck stays at `B782` 128 with the fall held: a real falling wreck changes height every frame; (4) how the dot's position is predicted (the point `B5DC` projects: `[4A10]`/`[4A18]`).

**The cut, derived from the code (12 Nov 2026, matched.c 13347 `camera_transform`, `camera_cull` 13328, `compose_emit_object` `E2B6`).** `E2B6` stores the wreck's position relative to the camera (`4994`, `4996`, `4998` = the model's x, y, z less the scene origin `49AC`..`49B0`, which `BB0D` set with `D9E7(0, 0, -z')`), calls `E3BF`, and ends the routine for an object `E3BF` culls (no draw). `E3BF` turns the offset into camera space (the matrix at 49BE; x, y, z at 4988, 498C, 4990, each the sum of three products doubled), then `camera_cull`: (1) z high word above `[48E6]`: culled; (2) z high word below the class's near limit `[48F6 + 2 * [49AA]]`: culled; (3) the cockpit frustum (`[438E]` clear): |x| against `(|z| + [E56A] + 2 * class) >> (1 - [294B])` times 1.25, |y| against the vertical equivalent (times 9/16 with `[368C]` set); (4) the range metric `[49A8] = ((|x| + |y|) >> 2) + |z|` (high words), kept unless it is **above `[48E6]` (13,332 in this flight)**. In the staged runs the metric crosses 13,332 between z' 50 and 51, where the game stops drawing: the staging puts the wreck 64 high against a player at altitude 0, so |y| is part of the metric. **The cut is therefore a combined range metric, not a pure depth: for a real wreck it depends on its height relative to the player.** To place the far dot where the game's model stops, the render copy has to evaluate the same arithmetic on the same words (or observe that the game did not draw the wreck); a depth number is not enough.

## Far wreck dot built and proven (12 Nov 2026) - supersedes the "Far wreck (`F117_WIDE_WRECK=1`, default OFF)" paragraph above

The matrix-division path is removed. `wreck_game_draws` (`rerun.c`) evaluates, on the copy's own words, the arithmetic the game runs before drawing the wreck: `9DE7`..`9E0F` (matched.c 34803: model 11 only when `[C62E]` is not zero and -256 < `[DEBE]` < 0), `BB0D` (48553: x32 = `[C0D2]` << 5, y32 = `[C0E4]` << 5, z = `[C62E]`, less the view origin, shifted by the scale (5 if `[B782]` is zero else 6, less 3, or 2 with `[294B]`), dropped when the low word of x or y has |v| >= 7FFFh), `D9E7` (set_scene_origin: z origin = -z'), `E2B6` (the point x low, -y low, z' + 1 goes to `E3BF`; class = low 3 bits of the byte at `[0A60 + 2 * (model >> 8)] + (model & 7Fh)`) and `camera_cull` (the z high word against `[48E6]` and `[48F6 + 2 * class]`, the cockpit frustum, the range metric `((|x| + |y|) >> 2) + |z|` against `[48E6]`). Not modelled (no dot is drawn, the game's picture stands): the second view (bit 7 of `[C0A6]`) and the second frustum (`[438E]` set).
Where the game does not draw the wreck (it is outside -256 < `[DEBE]` < 0, or `BB0D` or `E3BF` drops it), `wide_wreck` draws a black dot (palette index 0: every pixel the wreck changes in the original is black, one dark red at depth 16) at the point `B5DC` projects (the routine's own arithmetic), one game pixel up to depth 8,191 and x 8191 / depth beyond, at least one screen pixel, layer 3 (behind the HUD, as the original draws HUD after models), only where the original's screen allows (inside the picture: rows 0..60h or C7h; margins: 0..199). `F117_WIDE_WRECK=0` turns it off; default on (part of the owner-approved plan 12 Nov; the picture is `Desktop\ONShots\20261112-152108-far-wreck-dot-off-vs-on.png`).
**Proofs (staged `F117_EMIT_WRECK`, the game's own state; original presentation = the game's picture, Remastered wide = the copy):** (1) the predicted draw/cull against the original's pixels at the picture's frame: draws at staged depths 40, 49, 50 (5 to 8 pixels), culled from 51 (0 pixels at 51, 52, 54, 80); the metric at those depths (log `[wreck-calc]`, `F117_WIDE_DOTS_LOG=3`) is 13,114 (49), 13,370 (50), 13,626 (51) against `[48E6]` 13,332; (2) moving wrecks (`F117_EMIT_WRECK_ONCE=1`: staged once, the game's own rise and fall runs): 6 staged depth/height cases, **94 of 94 on-screen frames agree** with the original (a draw appears in the original exactly when the predictor says the game draws), including a draw-to-cull transition; the original's picture lags the copy's call by 0.5M to 1.0M clocks (the pairing offset is measured, not read); (3) dot placement: ON against OFF differs only at the predicted place: depth 60: 9 pixels at game (126, 46); 200 and 1,000: 9 pixels at (84, 51) and (84, 52); 3,000: 9 pixels at (32, 52); 9,000: 6 pixels at (104, 52) (0.91 game pixel); depth 30 (the game draws it): ON equals OFF, 0 pixels differ; (4) where the dot would fall on the HUD's grey line the line stays on top (several depths, correct layering, no dot visible there). Limits: the staged wreck is thrown up (`[B782]` 128) and leaves the top of the picture within 10 frames, so a long on-screen fall was not staged; the pairing offset above; the dot is black (as the wreck's pixels are), so it is only visible against a lit background.

## Layering audit of every margin overlay (12 Nov 2026, after the owner's third layering finding)

The rule (owner): layering is always considered. Each overlay: how the ORIGINAL draws it, what must hide it, what the build does now, and the status. "Read" = from the matched code, "unverified" = still to be read or proven; nothing here is inferred from this table's own earlier text.
| Overlay | Original (matched code) | Build now (rerun.c / remaster_live.c) | Status |
|---|---|---|---|
| far aircraft / projectile dots | flat pixel through the far call at `9B2C` / `9D39`, at the call; whether later scene objects overwrite it is unverified | layer 3, in front of all models | UNVERIFIED: read where the dot is drawn relative to the scene walk (`D87C`/`DC46`) and what a model drawn later does to it |
| smoke puffs and far puffs | deferred through `D9FE`, drawn in the scene walk `DC46` in depth order with the other deferred objects (matched.c 61410, 44651) | layer 2, in front of all models | WRONG for hangars and models nearer than the puff; documented limit until the depth fix |
| impact blast | same deferred sprite path as puffs (`1A98` via the blast record) | layer 0, in front | same as puffs |
| gun tracers | line drawn in the display pass (`9F94`), after the scene | layer 1 | believed flat-and-last: confirm the order against the HUD pass |
| far wreck dot | the wreck is a model (`BB0D` -> `E2B6`), depth-tested | layer 3, in front of all | WRONG (owner saw it over a near hangar, 12 Nov); fix: true depth, proven behind and in front |
| far aircraft past 4,800 (off) | the model/dot of the loop; the game does not draw it | layer 3 | off by default; same check before it is turned on |
The depth the compositor compares is `inv_depth_scale / z` (`gpu_compose.c` 447, `remaster_raster.c` 35); an overlay with a true depth needs the (z, scale) pair of the object it replaces: `[faces-z]` (`F117_MODEL_LOG=11`) prints the real wreck polygon's pair for the proof.

## Depth fix for the wreck dot and the puffs (12 Nov 2026, after the owner's hangar report) and what is still open

`remaster_live_overlay_rect` takes a negative `layer` as a true depth: the polygon gets z / inv_depth_scale = 8191 x the call's B5DC depth (-z'), measured from the game's own wreck model polygons at staged depths 20 to 49 (`[faces-z]`: z 4,193,792 per unit of depth with inv_depth_scale 512 at ring 4: 8191 per unit), with z held near 2^22 by the scale. The wreck dot, the far puffs and the margin puffs use it (the blast stays flat on top: below); positions use the exact projection (not the integer one) so a depth-tested dot cannot land on the wrong side of a thin edge. Proof (staged `F117_EMIT_WRECK`, Remastered wide): the wreck at 3,000 units, drawn over a near building before, is hidden by it after (`Desktop\ONShots\20261112-160609-wreck-dot-behind-building-fixed.png`; 9 differing pixels before, 0 after); at 4,000 and 9,000 units on a clear spot the dot shows; the far puffs that vanished are behind the same near building (probe: the building's polygon at the puff's exact point is nearer). The in-picture puff comparison with the original (R44) is unchanged.
**Not proven, stated:** (1) the original does not depth-test: it paints in order (terrain walk `04E6`, moving objects `983E`, the deferred objects `D87C`, then the display pass `9EFA`, matched.c 54750), so what hides a puff or a wreck is whatever is painted after it, decided by the deferred class (bit 7 of the class byte in `E2B6`) and not by depth alone; an immediate (non-deferred) object nearer than the puff would NOT hide it in the original. The depth test is the better of the two available approximations for far puffs and the wreck dot; its exactness against the original's order is unproven (needs a staged puff or wreck behind an immediate and a deferred object of known class, original presentation against the copy). (2) The far aircraft and projectile dots (`983E`, drawn before `D87C`) stay in front of everything in the margins: any deferred object painted later covers them in the original; the rule (which models are deferred) is not read yet. (3) Tracers and the blast are drawn by `9EFA`, the last pass: flat on top is faithful for tracers; the blast is a sprite through `1A98` in the same pass, so it is on top in the original: the margin blast keeps its flat layer 0 (the depth change was reverted before the gate).

## Far dots: layering rule built and checked (12 Nov 2026)

**Original (matched.c 54750, class table `[0A60]`):** the dots (`9B2C`/`9D39`, in `983E`) are flat pixels painted before the deferred pass `D87C`, which paints nearly all scenery (class bit 7: 41 of the models of table 0, 39 of table 1, 25 of table 2 in a dump) over them whatever its depth; the ground the terrain walk paints earlier does not hide them. **Measured on the original's own pixels** (owner flight, clocks 18.1e9 to 18.3e9, 52 on-screen dots, all over scenery, none over sky): the original shows the dot in 12 cases and hides it in 40; the Remastered 4:3 middle agrees with the original in 47 of the 52 (it shows the game's own pixels there).
**Built:** margin dots are drawn through `remaster_live_overlay_rect` layer 99: an ordinary object (owner bit 11 clear) at an infinite depth (z 4,194,304, inv_depth_scale 1/20,000), so the rasteriser's existing rules apply: any scenery object hides it, ground (bit 11) never does (`remaster_raster.c` 264-273), and it beats the sky bands (depth 0). **Checked in the margins** with test switches `F117_WIDE_DOT_ROW=Y` (a row of dots through the same layer) and `F117_WIDE_DOT_SWEEP=1` (a margin dot moved across the scene), Remastered wide: over sky every dot shows; over ground (rows 66 and 74 at the runway start) they show in the margins; in the frame with the hangars at the left edge the row at game y 48 shows only in the sky notches between the hangars and is hidden wherever a hangar covers it (`Desktop\ONShots`, row picture); a real margin dot (clock 17.69e9, game x 369 to 370, y 69) is a white 3 x 3 block over the sky, none where it is not.
**Limits, stated:** (1) the 4:3 middle cannot test this (Remastered shows the game's own pixels there, an overlay never appears): the margin check is by construction and by the hangar picture, not a pixel comparison with the original; (2) the 12 cases where the original shows the dot over scenery are over thin outlines or ground; the rasteriser draws object outlines wider than the original's one-pixel lines, so a margin dot can be hidden under an outline the original would not draw over it; (3) an immediate object (class bit 7 clear) is painted before the dots in the original and would not hide a dot, the build hides it like any object.
**Puffs (deferred, `D87C`, depth-sorted with the other deferred objects):** the true-depth test equals the original's order against deferred objects (a nearer deferred object hides a puff, a puff hides a farther one) and against ground (an object is drawn over ground); it differs only for immediate objects (the original paints the puff over them, the depth test hides it behind a nearer one): not staged, stated as a limit. **Blast and tracers** are drawn by the last pass (`9EFA`): flat on top, unchanged.
