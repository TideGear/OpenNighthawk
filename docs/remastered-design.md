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
