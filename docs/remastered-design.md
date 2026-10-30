# Remastered presentation (design)

Status: design only, nothing built, nothing credited. Written 29 October 2026 after a feasibility pass over what
the repository already has. The owner's brief (24 October 2026, [roadmap.md](roadmap.md) "Re-release features"):
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
