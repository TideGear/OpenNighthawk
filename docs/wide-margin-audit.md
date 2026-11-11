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

## The symbology routine (sections from its own header in matched.c)

All at screen positions on the flat picture: NOT in the margins. OPEN: each section's projection and cull, read in full.

1. a marker on every weapon in flight
2. the gun tracers: line; hits on an air unit, the player, the ground
3. the impact blast and its countdown (blast sprite `1A98`)
4. 4a the weapon lock on a ground target; 4b the detection ring of a ground site
5. the target camera (panel, not the view)
6. the release trigger (no drawing)
7. the weapon lock on an air target
8. the air target camera (panel)
9. the detection ring of an air unit (the rings the owner saw)
10. bomb symbology: impact point and steering bracket
11. radio beacons

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
| 1 | The object cull (`E3AC`/`E3BF`, `camera_transform`): far plane, near limit, side and vertical frustum, range metric | `compose_emit_object`, `compose_defer_model` | READ, findings below |
| 2 | The terrain walker (`04E6`): the four rings, the 4 x 4 cell block, the cell wrap at -8..7 cells, the detail value `[990E]`, the far plane `[48E6]` | `vgame_scene_finish_walk` and callees | OPEN |
| 3 | Per-model detail selection and size thresholds (`scene_model_depth`, the size word `[7D5E]`, `[DEBE]` class tests, the dot-versus-model switch) | model draw `120A:00CF`, `BB0D` | OPEN |
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
