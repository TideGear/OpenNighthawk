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
   not yet established: the per-theatre `*.WLD` files are only 2-3 KB (world tables, with the object-class bytes
   the D4 fix patches), and the per-theatre `*.3DX` files (40-65 KB) are the likely candidates for the tiles and
   models. Identifying it is the first job of R1.
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
| R1 world | find where terrain and placed objects live (`*.3DX`, `*.WLD`), then read them without the guest | tiles equal the guest's resident rings (`DS:E330`) wherever both exist, on every route |
| R2 instances | per-frame object list from the model requests, read-only | the list equals the original's draws one for one on the route captures |
| R3 meshes | connect `scene_mesh.c`; draw models with a depth buffer at 320x200 | pixel-compare with the original on frames where its whole-object sort is already right |
| R4 terrain | terrain, sky, sea with sub-pixel projection | no jitter on the road and coastline frames the owner marked; near field matches R3 |
| R5 frame | widescreen window, widened field of view, HUD and panel placement | the same route frames at 4:3 match R3/R4; 16:9 shows more, not different, world |
| R6 reach | longer draw distance from R1's loader | far terrain and objects appear, near ones unchanged |

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

R1: identify the terrain source (start from the `*.3DX` files, 40-65 KB per theatre, and the `*.WLD` tables), then a loader for one theatre compared tile for tile with the resident rings
on a captured route. It needs the game files and a capture of the resident tables, both of which exist, and it
has no effect on the running game.
