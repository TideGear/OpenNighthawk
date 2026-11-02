# Remastered presentation (design)

Status (30 October 2026): the renderer is not in the app and nothing is credited. Built so far: the terrain, world and model
decoders (checked against the game's memory), an offline depth-buffer model sheet, the exact projection, a depth-correct
triangle fill and the request-to-primitives glue, all standalone ("Progress" below; current state in
[p3-handoff.md](p3-handoff.md)). Written 29 October 2026 after a feasibility pass over what the repository already has. The owner's brief (24 October 2026, [roadmap.md](roadmap.md) "Re-release features"):
widescreen with a widened field of view, a longer draw distance, depth-correct drawing (the original sorts whole
objects, so a far hangar's edge can be drawn over a nearer one), and no road or coastline jitter. Everything else
about the game, above all its simulation, stays as it is.

## What exists

- **Model geometry.** `src/present/scene_mesh.c` decodes a complete original model (parts, vertices, planes,
  edges, faces, conditions) from a model bank, independent of the guest. It is a draft with a synthetic CTest
  and is not connected to anything ([presentation.md](presentation.md#world-geometry-and-the-scene-input-capture)).
- **Scene-input capture.** `F117R_OBSERVE_SCENE=N` with `f117run --observe` records each model request (model,
  position, orientation, camera) with the data segment and model bank; `tools/scene_capture.py` and
  `tools/scene_model.py` decode it. A research tool; the app never enables it.
- **HUD, panel and text.** Drawn from game state at any scale (`hud_state.h`, `hud_overlay.h`), keeping the
  original pixels as blocks.
- **Observer rule.** Presentation reads the machine and never writes to it
  ([presentation.md](presentation.md)); `rerun` runs on a throwaway copy of the machine.

## What does not exist

1. **A world loader.** The original keeps four rings of 32 tile slots resident (`DS:E330`); a longer view needs
   this project to read the world data itself and build terrain beyond those rings. Which file holds the terrain is
   What a first look at the shipped files shows (29 October 2026, the GOG install, nine theatres):
   the per-theatre `*.3DG` is exactly 1,810 bytes in every theatre (magic `22`, then data that differs by
   theatre) and `*.3DT` is 2.7-3.9 KB (magic `11`, words that include five 0x20s, then tables); a test route
   that removes `LB.3DG` or zeroes `LB.3DT`'s magic raises START's terrain-file error
   ([roadmap.md](roadmap.md) batch ai), so these two are the terrain files. They are far too small to hold
   heights or tiles at the resident rings' detail; the terrain is therefore built largely at run time from
   small tables, or its bulk lives in the model bank. The per-theatre `*.3DX` (40-65 KB) begins with an
   ascending table of word offsets, a bank of models (what `scene_mesh.c` decodes); `*.WLD` (2-3 KB) begins
   `BN` plus counts: object placements and classes (the object-class bytes the D4 fix patches). The format of
   `*.3DG` and `*.3DT` is not yet known; reading it, against the guest's resident rings, is the first job of R1.
2. **Object instances.** Which model is where, per frame, with a stable identity. The draw capture cannot give
   this (a reused projected vertex address is not an identity); the scene-input capture can, at the model request.
3. **A rasteriser with a depth buffer** at output resolution, with the original's flat shading, light and
   material rules reproduced from the model bytecode.
4. **Sky, sea and terrain drawing** without the original's whole-pixel flattening (the jitter comes from the
   integer projection and edge accumulation, [presentation.md](presentation.md)).
5. **A widescreen frame:** window and aspect handling, a wider projection, and the HUD and panel placed for it.

## Stages, and what proves each one

Each stage is its own branch and gate; none is credited before its proof, and the owner's eyes decide the last.

| Stage | Build | Proof |
|---|---|---|
| R1 world | decode `*.3DG` and `*.3DT` (terrain) and `*.WLD` (placements) without the guest | tiles equal the guest's resident rings (`DS:E330`) wherever both exist, on every route |
| R2 instances | per-frame object list from the model requests, read-only | the list equals the original's draws one for one on the route captures |
| R3 meshes | connect `scene_mesh.c`; draw models with a depth buffer at 320x200 | pixel-compare with the original on frames where its whole-object sort is already right |
| R4 terrain | terrain, sky, sea with sub-pixel projection | no jitter on the road and coastline frames the owner marked; near field matches R3 |
| R5 frame | widescreen window, widened field of view, HUD and panel placement | the same route frames at 4:3 match R3/R4; 16:9 shows more, not different, world |
| R6 reach | longer draw distance from R1's loader | far terrain and objects appear, near ones unchanged |

## Output: its own resolution and frame rate

Remastered does not replay or re-run the original's draw. It draws the scene from world and object state (R1-R2)
with its own rasteriser (R3-R4), so resolution and frame rate belong to it, not to the original's 320x200 draw:

- **Resolution.** Rasterised at the output size with sub-pixel projection; there is no whole-pixel flattening to
  jitter. R3 first draws at 320x200 so it can be compared pixel for pixel with the original, then at any size.
- **Frame rate.** The simulation steps at its own rate (15 a second by default) and is never touched. Each display
  refresh, Remastered interpolates the camera and the objects' positions and orientations between the two latest
  steps (state read from the machine, as `rerun`'s snapshots are) and draws that. The limit is the display's
  refresh, or none if the player asks for uncapped; it does not depend on the step rate. This is simpler than
  `rerun`, which has to run the original's draw code on a machine copy with nudged inputs.
- **HUD and panel.** The existing game-state HUD and panel code (`hud_state.h`, `hud_overlay.h`) at the output
  scale, placed for the wider frame (R5).
- **Simulation.** Unchanged and checked as in "Rules that make it safe".
- **Latency.** The simulation's input handling is untouched, so control timing is the original's. The picture is
  another matter: interpolating between the two latest steps shows step N while step N+1 is being computed, so
  the display runs up to one step behind (about 66 ms at 15 steps a second, 33 ms in 2x compression), as
  Enhanced's `--present-age interp` does now. Remastered offers the same choice as a setting: interpolate
  (smooth, that delay) or extrapolate (project the camera and objects forward from their last velocity: no added
  delay, but it can overshoot on a sudden change such as a stick reversal or a hit; `--present-age extrapolate`
  today). The default is the owner's choice once both can be seen in the cockpit.

## Rules that make it safe

- **Simulation untouched.** A route run under Original, Enhanced and Remastered must give identical simulation
  state on every logic frame (the roadmap's rule). The check hashes the guest's data segment per frame under
  each presentation, as the existing gates do for the other two.
- **Observer only.** Remastered reads machine memory through the same snapshot path as `rerun` and never writes
  to it; it runs from a copy where a draw needs the guest.
- **Off by default.** `--presentation remastered` stays refused until R3 passes, then is offered as an
  experimental setting; the shipped default is unchanged.
- **No one-off clipping exceptions** (owner, 15 October 2026): faces and clipping follow the same rules for
  coastlines and everything else.
- **Cost.** R1 and R2 are research with a hard oracle (the guest). R3 to R6 are rendering work whose last step,
  looking right, only the owner can confirm. A realistic estimate is many sessions; the first useful result is R1.

## First step

R1: decode the terrain files (`*.3DG`, `*.3DT`, with `*.WLD`), then a loader for one theatre compared tile for tile with the resident rings
on a captured route. It needs the game files and a capture of the resident tables, both of which exist, and it
has no effect on the running game.

## Progress

- **R1, terrain files (29 October 2026).** `tools/world_decode.py` decodes `*.3DG` (the level-4, level-3 and three
  512-byte tile tables) and `*.3DT` (five lists of up to 32 entries; each entry a run of points, three words and a
  kept byte) from the layouts START's matched loaders read; every byte of all nine theatres is accounted for.
  `tools/world_check.py` compares the decoded tables with a memory dump from a run: the tables are contiguous at
  `DS:B3A0..BAB0` in START's data segment with the point table following at `BAB0`, 7 bytes a point. Libya's
  tables and all 376 points match the guest byte for byte (the `boot_to_flight` route, stopped just after START
  reads `LB.3DT`, `f117run --steps N --dump 0:100000`). North Cape (368 points), Persian Gulf (346) and Vietnam (446)
  match the same way, so four of the nine theatres are checked against the guest; the other five decode exactly from
  their files but have not been dumped. Not done in R1: `*.WLD` placements, and the loader that goes beyond the
  resident rings.
- **R2, world files (30 October 2026).** `*.WLD` decodes with `tools/world_decode.py` from START's `world_read`
  (`0x054BB`): four header words, the target records (16 bytes, the count is the second header word), a slot count
  and 36-byte slots, three 128-byte tables, the 256-byte sector grid, and the place names (NUL-separated, the file
  holds fewer than the 750 bytes the loader asks for). Every byte of all nine files is accounted for; the names are
  real places (North Cape: Pechenga, Polyarnyy, Kilpyaur; Libya: Tripoli, Port Brega, Misratah). `world_check.py`
  compares them with START's memory at the loader's DS offsets: Libya, North Cape and Persian Gulf match byte for
  byte; Vietnam matches except six bytes in one target record, which the game writes after loading. Still to do for
  R2: what the target and slot fields mean, the sector grid's use, the model bank `*.3DX`, and the per-frame object list.
  What the pieces are, from the mission generator's names (`start_mission_generate`, `matched.c`) and the bug
  tracker: the target records are the ground targets (count at `DS:D33C`; the third header word, `E084`, is the
  regular-target count); the 36-byte slots are the air units (count `D890`, table `D3BE`); the third 128-byte
  table (`D2A6`) is the object-class table. Independent check of the decoded layout: fix D4's world-file offsets
  (`LB.WLD 0x53A`, `ME.WLD 0x597`, `NC.WLD 0x607`) fall in table 2 at bytes 84, 81 and 73, and `NC.WLD 0x1F1`
  and `0x201` fall in target records 30 and 31, byte 9 (the class byte), exactly where this decode puts them.
- **R3 groundwork, the model banks (30 October 2026).** `tools/model_census.c` runs `src/present/scene_mesh.c`'s
  decoder over the real model files: the `*.3DX` file, loaded unchanged at the start of a zero-filled 64 KB page
  (word 0 bounds the model index; the word at 2 + 2 x model points at the model's detail table), decodes every model
  at every one of the eight detail levels in all 11 banks (nine theatres, `STFLT.3DX`, `PHOTO.3DX`): 492 models,
  3,936 decodes, no failure. The largest mesh is 130 vertices, 46 planes, 188 edges. Some detail levels are empty
  (models with no geometry at that distance). Until now the decoder had only been run on synthetic fixtures.
  Checked against the game: a memory dump from a flight (`boot_to_flight`, Libya) holds `LB.3DX`, `PHOTO.3DX` and
  `STFLT.3DX` byte for byte (100% of each file), so the game loads the files unchanged and the decoder reads the
  game's own page. Not yet checked: the decoded meshes against drawn polygons.
- **R3 first look, the models drawn (30 October 2026).** `tools/model_sheet.c` draws every model of a bank into a
  contact sheet with a depth buffer (triangle fans, flat shading by the plane's normal, no part articulation, no
  original materials). `STFLT.3DX` gives recognisable aircraft, a missile, an airship and a parachute; `LB.3DX`
  gives, first, about thirty flat ground patches (plain tiles, coast and road shapes), then buildings (boxes,
  pyramids, a mast). The faces and vertices decode to real shapes, and the depth buffer draws a nearer face over a
  farther one whatever their order. Inference, not yet proved: the terrain is a grid of these flat patches picked
  by the `*.3DG` tile maps, which would make the road and coastline jitter a property of how the original projects
  and fills those patches. Not yet: part angles (articulation), the original's colours and materials, placement.

## R3 plan revised (30 October 2026): read the pose the game has already prepared

The earlier plan (and `presentation.md`'s "pose preparation is not independent") assumed R3 must rebuild each model's
part matrices from angles, which means reimplementing a chain of fixed-point routines in segment `1452` and `120A`
(`rotation_from_angles`, the row composition, transpose and vector transform, documented in `matched.c`). It does not
have to. The game prepares the pose itself before it projects a model's vertices, and everything the decoder
needs sits in a few small ranges of the data segment at that moment (`scene_pose_read`, `scene_mesh.c`): the part
matrices (a 9-word matrix a part at the pointer in `DS:7D50`, 36 bytes apart), the part translations (`7CDE + 12 x part`,
or `7CD2` for a model without parts), the eye and light vectors, the settings table (`7D4E`) and the palette (`7D80`).
`scene_mesh_eval` already turns that pose and a decoded mesh into the camera-space vertices with the original's exact
integer arithmetic. So the live Remastered path is read-only, in the same spirit as `rerun` and the other observers:

1. At each model request (VGAME `120A:00CF`, `observe_scene_model`) note the model index (stack argument, `SS:SP+4`) and the
   bank segment (`DS:8590`).
2. At the request's first projected vertex (`observe_vertex`, after the game has prepared the pose) copy those small DS
   ranges. This needs one new optional observer callback (a `pose` event given the machine, read-only); it is the only
   change to `src/matched/observe.c`, and like any change that can reach the machine it needs the full P2 gate.
3. Decode the mesh from the bank (`scene_mesh_decode`, checked on all 492 models), evaluate it with the captured pose for
   every vertex, not only the ones the original kept after culling, and rasterise at the output size with a depth buffer
   and a floating-point projection fitted to the original's (the `vertex` event gives both the camera-space coordinates
   and the pixels the original made of them, so the fit can be checked).
4. Sky and ground bands come from the band pass's own state (`band_state.h`); terrain is the ground-patch models
   placed by the game, so it arrives as ordinary model requests; the HUD and panel are the existing game-state
   renderers.

What this removes: the pose-preparation rewrite and its oracle harness. What it leaves: the new callback (small), a frame
assembler that gathers a frame's requests between two `frame_phase` events, the projection fit, shading and materials
(`scene_mesh_materials` decodes the original's material commands), widescreen framing, the longer reach (needs the
world loader beyond the resident rings, R1), and the settings entry. Nothing here is built yet.

- **R3, the projection is known exactly (30 October 2026).** `src/present/remaster_project.c` implements the game's
  perspective projection (VGAME `0x129A2`, as documented in `matched.c`): for a vertex whose z has a high word of at least
  `0x100`, the pixel offset is `(x >> 8) / z_high`; from 1 to `0xFF`, `(x >> 1) / ((z >> 8) >> 1)`; below 1 the vertex is
  behind the eye. Both are `256 x / z`, and the remainder the game throws away is the sub-pixel position that the new
  renderer keeps (`remaster_project`). Checked against a real flight: a one-second window of `V` observer records
  (`f117run --observe FILE:FROM:TO` with `F117R_OBSERVE_KINDS=V`, the `boot_to_flight` route at about 2.44 billion
  clocks) holds 7,359 projected vertices, and the integer rule reproduces all 14,718 pixel coordinates exactly (both
  ranges, both axes); a least-squares fit gives the same scale (255.9 for x). The pixels are offsets from the view's centre
  (the view origin is added later), which also settles widescreen: keep the scale of 256 and draw more columns, and the
  field of view widens by itself. Test: `presentation_remaster_project` (the real-log check needs `F117_VERTEX_LOG`).
- **R3, the triangle fill (30 October 2026).** `src/present/remaster_raster.c` fills convex polygons given in camera space
  through that projection, at the output's own resolution, with a per-pixel depth of 1/z and a near-plane clip. Tested
  (`presentation_remaster_raster`): a nearer face covers a farther one in either draw order, including two faces that
  cross; a polygon behind the eye draws nothing and one across the near plane is clipped; a 80-column frame and a
  64-column frame of the same scene agree on the shared columns (a wider frame adds columns and moves nothing, the
  widescreen rule); a whole-pixel move shifts the picture by exactly that, and a half-pixel move changes the edge
  pixels' coverage, which the game's whole-pixel positions cannot do. Scales are per axis (the game's pixels are not square:
  256 H / 240 across and 256 H / 200 up for an output H high that shows the original's frame at 4:3). Still to do for R3:
  the glue from a decoded mesh and a captured pose to polygons (needs the `pose` callback described above), the original's
  colours and shading, and the frame assembler.
- **Sky and ground need no new work (30 October 2026).** `band_state.h` already models the horizon bands as a function of the
  camera's height and its pitch and roll sines and cosines, in floating point ("FINE") as well as the original's exact
  16-bit arithmetic ("EXACT", checked against captured logs by `test_band_state`); the HUD and panel are state-driven too
  (`hud_state.h`). Remastered draws the bands from the same state at its own resolution and keeps the horizon at its true
  position instead of a quantised one, so only the model geometry is new.
- **R3, from a request to camera-space primitives (30 October 2026).** `src/present/remaster_scene.c` does the work between the
  `pose` callback and the fill, as pure code on the guest's memory: it picks the game's detail level from the request's
  position words and the camera matrix (`scene_model_depth`), decodes the model once into a cache (a content signature of the
  bank page keys it, so a new theatre starts afresh), reads the pose where it lies (`scene_pose_read` straight on the data
  segment, no copy), evaluates every vertex in camera space with the game's integer arithmetic, and runs the model's material
  commands, turning each polygon's edge chain into an ordered vertex loop. Tested on the synthetic fixture placed in a fake
  guest memory (`presentation_remaster_scene`). Not yet checked on real data, which needs the callback running in a build: the
  next step is a headless consumer of `observe_pose` that collects a frame and draws it with the fill.

### First live frame (30 October 2026)

`F117_REMASTER_FRAME=N:FILE.ppm` (f117a, `src/present/remaster_live.c`) draws the Nth frame's geometry from a running game.
It uses the machine's execution hooks (`machine_t.exec_hook`, a table of up to eight CS:IP entries; the same mechanism
hybrid sound uses, in `src/machine/pc.c`), not matched code, so it works in a build without generated code. It watches
the model request (VGAME `120A:00CF`), the five projection returns (`120A:08D5`, `0901`, `0937`, `096A`, `0970`: the pose is
prepared by the first one a request reaches, and `remaster_scene_add` reads it there) and the per-frame draw routine
(`0000:1450`). The frame is drawn at 960 x 720 with the game's own screen origin (`[8602]`, `[8604]`) and clip window (`[85FA]` to
`[8600]`), the sky and ground bands from the state model of `band_state.h` (read at the same `0000:1450` entry; the quads are
drawn far behind everything, depth reset before the models), and the models' polygons through the depth-correct fill, with
colour words read as `model_fill` reads them (high byte FF one colour, darkened by `[48C4]` from A0h; below FB a dither pair,
drawn here as the average; FB draws nothing; FE/FD/FC drawn as the low byte). Frame 150 of the recorded takeoff matches the
original's horizon, hangar positions and dusk colours; `F117_REMASTER_FRAME=N:FILE.ppm` also writes the original's last
picture beside it as `FILE.ppm.orig.ppm`. Not yet: the runway and terrain from the tile maps, lines and dots, the face shade
(`[7D8A]`) in the colours, detail on the hangars, widescreen, longer reach, the HUD and cockpit (2D overlays, not geometry). Nothing is wired to
`--presentation remastered` yet, so the feature stays at 0%.

### How the original draws the scene (read from the matched code, 30 October 2026)

- **The frame** (`vgame_scene_dispatch`, `0x03CE`) sets the view up, draws the sky and ground bands (`vgame_horizon_bands`,
  `0000:1450`: 16 sky quads in palette colours `80h + k` (flat `88h` when both illumination flags are set) and 16 ground quads
  from the same offsets, each a four-corner polygon to the span rasteriser), then walks the scene and finishes it
  (`vgame_scene_finish_walk`, `0D87C`).
- **The terrain walk** (`vgame_scene_walk`, `04E6`) goes ring by ring (five rings, 16 cells a sector) round the viewpoint.
  Each tile owns a list of seven-byte object entries from the theatre's scenery file (`vgame_load_tile_objects`, `0B05`), and
  each entry is drawn by `compose_emit_object` (`0E2B6`): placed relative to the camera, culled (`0E3BF`), then either put on
  the deferred list (class bit 7, `0DB3C`, drawn by the finish walk) or drawn by the model request `120A:00CF`. So the ground
  patches, runway strips and buildings are models; nothing in this walk draws terrain polygons of its own.
- **The other objects** (`vgame_compose_walk`, `0DC46`): aircraft, ground vehicles and specials from the object table
  (`[4E6C]`), through `1452:03AB`, `0E289` and `120A:00CF` as well; an object whose kind has bit `400h` set goes to a different
  drawer (`1AE9`), and kinds above `25h` also draw a label (`1B2E`). Those other drawers do not make model requests, so the
  live capture does not see them.
- **Each model** is drawn by `model_faces` (`120A:0C76`) face by face, each face's command stream by `model_fill_stream`
  (`120A:0CE9`): polygons (edges through `130D:004A`, the colour byte picking a word of the model's palette table `[7D80]`,
  filled by `model_poly_finish`, `130D:0116`), line sets and point sets. The fill style is the high byte of the colour word
  (`model_fill`, `1377:0B09`).
- **What is still not found:** the source of the strip of sea at the right of the horizon in the takeoff frame. It is not drawn
  by the live capture. It is either a model drawn by a request the capture does not see (the `1AE9` drawer, or a request made
  outside `compose_emit_object`/`compose_walk`) or part of the ground bands' neighbourhood. Next: list the polygon fills of a
  frame that happen outside any model request, with their colour words, and match them to the missing regions.

## Handoff state (30 October 2026, late)

Built and unit-tested (3/3 remaster tests), **not yet verified in a flight**: the AND, OR and stipple fills (`remaster_polygon_logic`,
palette indices carried in `remaster_target.index`, `word_style()` in `remaster_live.c`). They are meant to fix two visible
defects, both traced to the original drawing a fill that combines with what is already on the page, which this renderer had
drawn as a flat colour: the strip through the hangars (a polygon painted with the horizon colour) and the missing sea (model 15,
colour word FE6F: AND of the ground band colours 70h-7Fh with 6Fh). Faces are fed in the original's back-to-front order
(`scene_mesh_materials` calls `scene_mesh_order`, a port of 120A:0E5B), so overlays draw after the surfaces they modify.

To check: run the takeoff replay (`--replay` of a recorded takeoff, `--presentation remastered --present-scale 3`, with
`F117_PRESENT_SHOTS`) and the Enhanced baseline, then `py tools/remaster_diff.py ENHANCED.ppm REMASTERED.ppm`. The hangar and
sea regions should shrink or vanish. Only then refresh the screenshot on the owner's Desktop.

Validator mismatches (`F117_REMASTER_VALIDATE`; models 3, 30, 51, 50) are **explained (31 October 2026): a validator artifact, not a placement error**. `F117_REMASTER_VALIDATE=2` adds a line per mismatching instance and the report buckets the differences (within 1 px, within 8, farther). On the recorded takeoff 2,224 of 24,799 vertices differ, always 4 to 13 vertices of an instance, always far from the game's number, and the game's number does not move with the instance: three instances of model 51 at x = 382, 638 and 894 give the same record for vertex 11 (`93,0`) while this renderer's value moves (-91, -143, -196). The game projects a vertex lazily, only when a face it draws uses it, so the record at `DS:D6B4 + 8 * id` of an unused vertex is left from an earlier draw. The comparison is only valid for vertices a drawn face uses; the other 91% match exactly. Do not chase these as pose errors; a full in-order read of the original render path (120A:00CF through 0C76, 130D clipper, 1377 fills,
0FB2 span rasteriser, scene dispatch/walk/finish) with notes here; a per-model polygon-accounting check (original fills against
this renderer's) as a second net for missed objects; face shade `[7D8A]` in colours (**closed 31 October 2026: the original never uses it for a colour**: a scan of the whole VGAME image finds three byte pairs `8A 7D`, one is `model_face`'s own store (`mov [7D8A], al`, `120A:0CA8`) and two are data; no instruction reads it, and the polygon's colour is word `[7D80] + 2 * index` darkened by `[48C4]` in `raster_model_fill_begin` (`1377:01E0`). A face's tone comes from the model's own conditional commands (`80h + k` against the settings table `[7D4E]`, already read by `scene_mesh`) and the palette word, so nothing is missing from the colours; the plane shade byte `[DI + 7802]` (stored by `plane_shade`, `120A:0674`) is read once, by `model_face`, and ends in `[7D8A]`; the visibility byte is the separate `[DI + 7702]`); interpolation between logic steps; widescreen.
The branch has debug blocks ([tags], [pixel], [col], [blue], [raw], [validate]) worth trimming. P2 work is resuming: keep to
worktrees, no concurrent full builds or gates.

**First task for the next session: read the original P2 rendering code in full** (`src/matched/matched.c`, the matched routines
for the render path: 120A:00CF, 0317, 0561, 06B5, 0902, 098C, 0A33, 0B3B, 0C76, 0CA8, 0CE9; the 130D clipper; the 1377 fills;
the 0FB2 span rasteriser; 1452 and 1039; the scene dispatch, walk and finish routines). The owner asked for this explicitly, and
the Remastered renderer has so far been built from partial reads, which is how the AND/OR/stipple fills were missed. Write notes
into this file as you go, saying for each fact whether it comes from code, a comment or observation. Do this before more fixes.

## Findings from the headless shot tool (30 to 31 October 2026)

These came from `f117run --remaster-shot`, a research tool built in a parallel session on `master` and retired when it was
merged with this branch (its code is at git tag `remaster-view-tool-20261031`; the findings stand and several answer
questions listed as open above, for example the missing sea strip and the per-model polygon accounting). Where a
sentence names `--remaster-shot`, `remaster_view`, `F117_RM_*` or `remaster_wide_check.py`, that is the retired tool.

- **R3, first frame from a real flight (30 October 2026).** `f117run --remaster-shot FILE.ppm:FROM` (research aid in
  `headless.c`; read-only) installs the pose callback, gathers the model requests of ten view compositions after clock
  FROM (a composition draws only one or two models, so a single one is not a picture), fills them with
  `remaster_polygon` at 1280x960 about the projection centre the game publishes (`DS:8602/8604`, viewport
  `DS:85FA..8600`, in 320x200 pixels), and writes the result beside the original's frame. On `boot_to_flight` (Libya,
  about 2.45 billion clocks) the horizon, the runway and taxiway edges and a distant aircraft land where the original has
  them. Not right yet: the colours (a polygon's palette word is only the base: the original's pixels are ramp entries
  that depend on distance and shade, e.g. word `FFCB` over original index 129/138), the hangars (not among the
  requests gathered; they may be drawn by another path), and the sky and ground (flat stand-ins until `band_state`
  is used). `F117_RM_DUMP=1` prints each request and primitive with the original pixel under it.
- **R3, colour rule and lines (30 October 2026).** A primitive's pixel colour is the original's `raster_model_fill_begin`
  rule (VGAME `1377:01E0`, `matched.c`): a palette word `FFxx` with `xx >= A0` is darkened by `DS:48C4`, never below
  `xx & F0` (unsigned compare, so a borrow wraps and stays, as in the original); other words use the low byte
  (`remaster_prim.pixel`). `remaster_line` draws the model's line commands. In the Libya flight the runway centreline's
  dashes now appear where the original has them. Still wrong: the ground's brown/grey banding and the sky gradient
  (they come from the band pass, `band_state.h`), and the hangars.
- **R3, sky and ground from the band state (30 October 2026).** `--remaster-shot` builds the band pass's `o` record from the
  data segment words (`band_rec_off`), runs `band_fine` and paints the 32 quadrilaterals unrounded before the models. In
  the Libya flight the sky gradient and the brown ground bands match the original, smoothly instead of in steps. Left: the
  hangars, and the runway's dark surface colour.
- **R3, the original does not redraw the whole scene each step (30 October 2026).** Counting the model requests over 30
  million clocks of the Libya flight (about 7 logic steps) gives 26 ground-patch requests (model 30, 191 primitives),
  21 of model 3 (bank `575E`, a distant structure), 11 of model 5, 8 of model 37 and a handful of others; none is a near
  hangar, yet the original's frame shows large hangars on both sides. Composition passes (`compose`) come about every
  0.77 million clocks in runs of content separated by empty passes, and one pass draws only one or two models. So the
  original appears to keep its picture on the work page and draw what changed (an inference, not yet proved); the
  Remastered renderer then cannot take a frame from one step's requests. It needs a retained scene (the latest request per object, kept until the original redraws
  or drops it) fed from the same callback. Open: what identifies an object across steps (the position words move with
  the camera) and when the original drops one. `F117_RM_DUMP=1` and `F117_RM_WINDOW` (compositions gathered; the
  frame-run mode is the default) help study it. Shots from these runs go to the owner's `Desktop\ONShots`, named
  with a date-time prefix.
- **Smooth gradients (owner, 30 October 2026): "gradients like the sky should be smooth without visible bands".** The band
  quadrilaterals are painted flat into float colour per class (sky, ground), blurred vertically across the bands with a
  normalised triangle kernel (the horizon stays sharp) and dithered on output, so the sky and ground ramps show no
  steps. Rule for the Remastered renderer: any gradient the original quantises (bands, shade ramps) is drawn
  continuous and dithered.
- **Fit rule for the in-app Remastered view (owner, 30 October 2026): the cockpit and HUD must fit the screen, not be
  cut off, with the crosshair still centred.** The 3D view fills the whole window (it widens at the sides: the scale
  stays 256, more columns, nothing moves). The cockpit and HUD, which are 320x200 art, are drawn as one block scaled
  uniformly to fit inside the window (the smaller of width/320 and height/200, never more than the window allows),
  centred horizontally and anchored to the bottom, so no edge is cropped. The crosshair is drawn at the 3D view's
  projection centre (`DS:8602/8604` mapped through the same transform), so it stays on the aim point whatever the
  window shape. That is the better answer than shrinking by a fixed factor: it is exact at every window size and aspect.
- **R3, the fill style is the palette word's high byte (30 October 2026).** From `vg4_fill_select` (VGAME `1377:0B09`,
  `matched.c`): a word `AHAL` below `FBxx` is a colour pair (a dither of AL and AH), `FFxx` one colour (darkened by
  `DS:48C4` when AL >= A0, `vgame_fill_begin`), `FExx` AND, `FDxx` OR, `FCxx` stipple, and **`FBxx` clears without
  drawing**. The dark red slab over the ground was polygons with word `FB36`, which the original paints nothing for.
  `--remaster-shot` now skips `FB`, draws a pair as the average of its two colours (the smooth equivalent of the dither)
  and leaves AND/OR/stipple unmodelled. Also: requests made while the viewport (`DS:85FA..8600`) is under 300 wide
  belong to the cockpit's map display and are left out; requests are painted far to near with depth tested only
  inside one request (the original's painter order across models); a later polygon wins a depth tie. With these the
  Libya frame has the runway, taxiway stripes, hangars with their yellow markings, the aircraft and the centreline
  where the original has them. The shot now shows the whole original frame on the right, uncropped (owner request).
- **R3, what the blend proves and what it does not (30 October 2026).** `--remaster-shot` writes `FILE.blend.ppm`, the
  render at 50% over the original's viewport pixels. On a single view composition (about forty requests; the flip bit
  at `DS:0346` toggles per composition) the hangars, runway edges, taxiway stripes and centreline dashes coincide with
  the original's, so the geometry, pose and projection are registered. That is a registration check only: the render is
  meant to differ where the original is wrong (stepped edges, whole-object sort, quantised gradients, jitter), and
  the remaining ghosting is the original's pixel steps against the render's smooth edges. Depth: one depth buffer across
  every model, a tie going to the later draw in the game's own order (`F117_RM_PAINTER=1` reproduces the original's
  whole-object sort for comparison; on the Libya frame the two agree, as expected where the original's sort is right).
  Fill styles follow `vg4_fill_logic` (VGAME `1377:0A89`/`0A09`): OR and AND combine with the palette index already on
  the page (no depth), which is how the water strip at the horizon (`FD80` polygons of models 1, 3, 5, 6) is made;
  `--remaster-shot` keeps a palette-index buffer for it. Still to model: stipple (`FCxx`), models 11 and 26 (single
  streams that emit nothing; `F117_RM_MESH=1` prints their bytes), and everything past the 3-D view.
- **R3, a face is its edge set, not a chain (owner: "some polys are still missing", 30 October 2026).** From
  `model_edge_spans` (VGAME `130D:004A`, `matched.c`): the original takes each edge of a face alone, accumulating flag
  words and span rows, so a face's command may list its edges in any order. `emit` had required them to chain end to
  end and silently dropped a face that did not. It now collects the edge set's vertices and orders them round the
  face (an end-to-end walk when the edges form one closed loop, else by angle about the centre in the face's plane); a
  lines command gives one segment per edge. Counted against the original's own fill log, models 37 and 51 had made
  about half the original's fills; with the change the hangars have their arched bodies, the tower at the horizon
  appears and more of the water. A one-edge "polygon" (the unit fixture's) is no face and gives nothing. `--remaster-shot`
  also takes `F117_RM_WIDTH=1707` (16:9 at this height): the scale stays and columns are added about the projection
  centre.
- **R5 proof, widescreen (30 October 2026).** `tools/remaster_wide_check.py` compares an `f117run --remaster-shot` render at
  the default 1280 columns with one made with `F117_RM_WIDTH=1707` (16:9 at the same height) of the same composition: the
  middle 1280 columns of the wide render equal the narrow one (0 of 665,600 pixels differ by more than 10 levels; the
  tolerance only absorbs the sky's dither, which is keyed by pixel index), and the extra columns show more of the
  world (more hangars, runway, water, ground and sky). The scale (256) stays, more columns are added about the
  projection centre, and the band quadrilaterals extend with them.

## The fit rule in the app: widescreen (31 October 2026)

In a window wider than 4:3, `--presentation remastered` now draws the world across the whole window and keeps the game's own
320 x 200 picture (HUD, cockpit, text) at its size in the middle, so nothing is cut off and the crosshair stays at the centre
(`remaster_live_wide`, `wide_make` in `main.c`). The world is drawn at the game's own projection scale (256) with its origin shifted
by half the extra width; the sky and ground bands' outer edges run off every side (`draw_scene_ex`, `wide`); the pixels Remastered
did not replace (the mask of `remaster_live_composite`) are laid in from the game's picture. The picture is built in the shape
the fine picture has before the 4:3 stretch (1.2 times the window's aspect, 200n rows) and shown over the whole window.
Checked on the recorded takeoff in a 1920 x 1080 window (`F117_WINDOW=1920x1080`, a diagnostic override): shot
`Desktop/ONShots/20261031-210224-inapp-widescreen-16x9-remastered.png`; the heading tape, both HUD columns, the gun and weapon
lines and the cockpit are whole, and the crosshair is at x = 958 of 1920.

Known limits: the game culls whole models by its own 320-pixel view before asking for them, so an object that is entirely
outside the original's field of view is not drawn in the margins (it pops in as it enters the middle); the terrain polygons
that are not model requests yet stay as the original drew them inside the middle; the mouse maps through the wide picture (`window_to_guest`, `g_wide_shown`: the window's centre is the game's (160, 100); worked out, not yet tried with a real mouse);
the cockpit's own
sides are not extended, the world shows around it.

## A moving takeoff (31 October 2026)

The recorded takeoff with twelve throttle presses added (`D:/f117-gate/remaster-flight-20261031/rec/input-go.log`: `K t 0D` and `8D`
pairs from clock 2.46 billion; the aircraft reached about 80 knots by clock 2.6 billion), replayed in Enhanced and Remastered
(`--presentation ... --present-scale 3`, frames at 2.600 to 2.602 billion). The sky and ground bands, the runway, the hangars and
the distant aircraft agree between the two at every frame looked at, and nothing flickers at the edges of the replaced
regions at that speed. Interpolation between logic steps is therefore not shown to be needed at taxi speed; it is still open for
fast, turning flight (the composite takes its mask from the Enhanced picture's interpolated tags and fills it from the last
completed frame, so a fast pan could misalign the two; this has not been tried).

## The hangar doors (31 October 2026): lines, the original's outline pixels and depth

The owner saw it first: the doors' yellow markings (a stem and a bar, model 51's line primitives, word `FF96`) were drawn twice and the
bar was thin. Two causes. (1) The composite replaced the original's model-polygon pixels but kept the original's model-line pixels
(origin tag `DT_OUTLINE_FINE`, 22: all 210 yellow pixels of the door were kept), so the picture held the original's door at the
interpolated position and Remastered's at the last logic frame's. Fix: outline pixels are replaced like polygon pixels where
Remastered has geometry. (2) A line lies on the surface it marks, at that surface's depth, and near the horizon one pixel of height
is a large step in depth, so the ground polygon just below a line is 'nearer' than the line and hides its lower half; a constant
bias (tried 1.002, 1.2, 1.5, 50) either did not help or showed far hangars' lines through nearer hangars. Fix: the scene's lines are
drawn after its polygons twice (`remaster_target.line_strong`, huge bias, every line whole; and with the default bias 1.5 into
`color`), and the composite takes the whole line only for pixels within three fine pixels of an original outline pixel, i.e. where
the original shows a line, and the depth-tested pixel elsewhere. Result: one stem, a full-width bar and no stray lines
(`Desktop/ONShots/20261031-223058-hangar-door-*`). Lesson recorded in handoff.md: check lines and dots, not only polygons, and
look at zoomed crops of the 3D view, not whole frames.

## What Remastered still leaves to the original (31 October 2026): the kept-pixel report

`F117_REMASTER_KEPT=1` prints, every 120th composite, the pixels of the 3D window left as the original drew them, by origin tag
(model tags 1 to 20 and the outline tags are marked `!`: a primitive Remastered does not draw); `=2` also writes `kept-<clock>.ppm`,
the frame with those pixels in magenta. `F117_REMASTER_PROBE=X,Y` lists the primitives whose screen box holds a point.
First run on the moving takeoff: tags 1 and 3 kept 1,200 to 1,900 and 1,600 to 2,700 pixels a frame (about 2% of the 3D view), as
a strip along the horizon (the sea and far ground), the distant aircraft and the dotted taxiway edges. The probe showed the strip
is model 1, 3 and 29 polygons with fill word `FD80` (OR): Remastered applies a logic fill to the bands under it, but the bands
sit at no depth and the composite takes 'no depth' for 'no Remastered geometry'. Fix: a pixel a logic fill changes gets a hair of
depth (`remaster_polygon_logic`). Now 350 to 460 and 160 to 390 pixels a frame, outlines about 300: thin lines where Remastered has
nothing under them, mostly around the distant aircraft and the taxiway edges. Still open.

## Plan: Remastered from the rerun copy (interpolation, the second image, the margins, longer reach) - 31 October 2026

**The owner's choice (31 Oct): the safe route.** The live machine is never changed. Remastered's model requests for a picture come from
the same throwaway machine copy that `--present rerun` already runs for every in-between picture (docs/presentation.md, "The rerun
mode"), with a wider view for the margins.

**Why the copy.** Today Remastered fills from the live machine's last completed frame (`remaster_live.done`) but takes its mask from the
Enhanced picture, which is the copy's in-between picture. The two are different moments: the second image that lags when moving, and the
lag between the original's lines and Remastered's. The copy already runs the original's own drawing at exactly the picture's moment (nudged
pose, computer-flown aircraft and weapons), so collecting the model requests in the same run gives geometry, mask and tags from one instant.

**Steps** (each checked before the next):
1. `rerun.c` runs the copy with its hooks cleared (`memset(&s->hooks, 0, ...)`); the snapshot keeps `exec_hook[]` (copied with the machine).
   Add `rerun_set_collector(begin, exec, end, user)`: the copy gets `hooks.exec = exec`, and `begin`/`end` run around `machine_run`. The
   collector is a second `remaster_live` (its own scene, frame and band state; the live one is untouched). The sound hook (index 0) is not
   called in the copy. `end` finishes the frame (the copy's `frame` becomes its `done`, with the band state captured at its frame start).
2. Check: at t = 0 the copy's frame equals the live frame of the same step (primitive count and vertices), over the recorded takeoff.
3. `drawfeed.c`: `drawlive_render` marks `shown_copy` when the picture came from a copy; `remaster_apply` composites with the copy's frame when
   set, else the live one (the picture of a logic step, t = 0 or 1, which the live frame matches). The single-slot hazard is the same as
   `inter_hi`'s and is resolved the same way: in the threaded loop the picture is composited after the worker joins and before the next
   `drawlive_prepare_rerun`.
4. Widescreen margins and R6: before the copy runs, widen the view it sees (the view window words DS:85FA..8600 and whatever the walk's
   out-of-view test, VGAME E3AC with D9E7, reads; to be read in `matched.c`, `vgame_scene_walk` and its callees). The extra model requests
   exist only in the copy; the live machine and its clock are untouched. Isolation is checked by the existing `f117run` copy checks
   (real machine, all 1 MiB and the fixes' state equal before and after).
5. Cost: the copy already runs once per displayed in-between picture; the collector adds the scene decode and the render, nothing more to run.
   Measure instructions and microseconds of a widened copy against an ordinary one (the `draw-rerun-probe` experiment's harness did this
   for the ordinary copy: `D:/f117-archive/orphans-20261031.bundle`, `8849ce4`).

**What step 4 will change (read 1 Nov 2026).** The scene walk's out-of-view test is VGAME `0x0E3BF` (the matched `vgame_cull`, register arguments a, b, n). It rotates the point into view space (matrix words `[49BE..49CE]`, results `[4988..4992]`, depth in DI) and rejects it when the depth is above the far plane `[48E6]` or below the near plane `[48F6 + 2*[49AA]]`, then tests the two side planes against the tables at `[4966 + 2*mode]` and `[4976 + 2*mode]` (or `[E56A]`/`[E570]` when `[438E]` is 0). The far plane is set once by `0x0D4A5` as (`[990E]` + 1) x 0xD05 (13,332 in the owner's takeoff, mode 3); the side tables are constants of the view cone. So a wider copy means widening those words on the copy only (the sides for the margins, `[48E6]` for reach). First experiment: poking `[48E6]` to 0x7000 on the live machine at the takeoff (`F117_POKE=5050000000:0x3C3C6:0070`, a diagnostic) changed no pixel of the original's picture over the next 0.7 G clocks, so on that scene the far plane is not what limits the view (the world's own cell radius `[48D6..]` and each model's range test at `0x0BC0B` are the other limits): the reach row needs a scene with distant objects (a high-altitude strike run) and the cell radius read before it is worth wiring. The side-plane widening has not been tried.

**Step 4, first measurements (1 Nov 2026, `F117_WIDE_VIEW=FAR:SIDE` in `rerun.c`, a diagnostic that scales the copy's far plane `[48E6]` and the side-plane tables `[4906..4985]`; `F117_WIDE_VIEW_DIAG=1` prints the copy's draw-value count; the owner's takeoff in Enhanced at scale 3, last 300 copies, mean values per copy):** baseline 253,369; far plane x4 253,369 (no change: the far plane is not what limits this scene); side tables x2 255,938 (+1.0%: more objects, the margin's); side tables x4 204,463 (-19%: the same words also choose each object's detail, so scaling them as a whole coarsens the scene). So the margins can be widened through the cull's side planes, but not by scaling the tables: the cull's own comparison (VGAME `0x0E43B`-`0x0E4A7`, in the matched `vgame_cull`) has to be widened without touching the words the detail estimate reads (`0x0E4B7` onwards). Next: a copy-only variant of that comparison (an override placed in `src/present`, active only while the copy runs), checked by the draw-value count rising with the factor while a known object's detail stays put.
**Step 4, corrected (1 Nov 2026, later).** The x4 drop above was not the detail choice: the cull adds `|z| + table` in 16 bits, and a table value near 0x7FFF made the sum wrap negative and cull everything. With the widened words capped at 0x4A00 the draw data rises steadily with the factor (x1 253,369; x2 255,938; x4 256,433; x8 256,847 values a copy). But the tables are only the object-radius allowance in the side test: on the path the owner's flights take (`[438E]` = 0, VGAME `0x0E4B7`), an object is culled when |x'| > 1.25 x ((|z'| + table[`[E56A]` + 2 x mode]) >> cl) with cl = `[294B]` xor 1, and likewise for y with the second table. The cone's angle is the factor on |z'|, which no data word controls (`[294B]` flips the cone between two shapes and the projection reads it too), and the matrix words that make x' (`[49BE..49CA]`) are the camera rotation the projection uses as well. So a real angular widening needs the comparison itself changed, for the copy only. Plan: an override at `0x0E3AC` that declines unless its machine is the rerun copy (checked by pointer, so the live machine, which shares the table, never sees it), and in the copy runs the cull with |x'| scaled down by the canvas's width factor; first proved at factor 1 against the unmodified copy (the same records out), then the draw-value count and the margin pictures at the owner's window shapes. The matched `vgame_cull` is in `matched.c` (the P2 session's file), so the override sits in `src/present` and leaves it alone. Not started.

**Not changed by this plan:** the Enhanced and original presentations, the live machine's timing, recordings and replays.
