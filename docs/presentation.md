# 60+ fps and 4K: design

Roadmap Phase 3 asks for 60+ frames a second and 4K output. This page says
what constrains each, what is known and what is not, and the order of work.
Nothing here is built yet; the plan is staged so that each stage is useful
alone and none can change the emulated machine.

## The constraint that matters: the presentation must not touch the machine

Phase 1's proof is that the recompiled game is the original, instruction for
instruction, held to GOG DOSBox. A presentation path that changes a single
guest write or clock tick breaks that, and the routes would say so. So every
stage below is an **observer**: it reads what the machine already does and
draws something from it. It never writes guest memory, never changes the
instruction count and never feeds back into the machine. The parity routes
and `func_lockstep` therefore keep their meaning, and a presentation bug can
only draw the wrong picture, never change the game.

## What is known

- **The original draws at most 15 pictures a second.** The flight model
  steps once per drawn frame and divides every per-second rate by
  S = `[0x368E]`, which the frame-rate controller clamps to 15
  ([bugs.md](bugs.md) D1). 60 fps cannot come from the original drawing
  faster: it must come from *interpolating between its states*. Raising S
  is not an option either: the flight model is 16-bit integer, and rates
  smaller than S round to zero (that is D1's missed missiles).
- **The host shows the last frame the VGA scanned out** (`present_capture`
  on each vertical retrace, `src/host/main.c`) and scales it to the window
  with SDL (`--scale N`, `--fullscreen`, aspect correction). So a large
  window or a 4K display already works as *a scaled 320x200 picture*. That
  is not 4K rendering: every scene edge is still on the 320x200 grid.
- **Time is one real second per game second** (the mission-clock invariant),
  so interpolating game state against the host clock is meaningful.

## What is not known (and has to be measured first)

1. **Where the scene is drawn.** The matched routines already name part of
   the path: the model fill setup (`1377:01E0`), the polygon row spans
   (`130D:00B6`, `130D:00D1`), the span tables (`0FB2:051F`), the clip
   outcode (`130D:0671`), plane shading (`120A:0674`) and, for sprites, the
   graphics driver's slot at `1E42:0188`. Text, the HUD, the map and the
   cockpit panels go elsewhere. A census of every routine that writes the
   frame buffer, by caller, is the first piece of work.
2. **What precision the scene has before it is flattened.** The projected
   vertices that reach the polygon filler might carry fractional bits (the
   clip outcode is 32-bit); if they do, a higher-resolution re-draw gets
   real sub-pixel edges. If they are whole pixels, a re-draw at 4K can only
   reproduce the same blocky shapes sharper. This decides what "4K" can
   honestly mean.
3. **Whether successive frames draw the same objects in the same order.**
   Interpolating between two states needs a pairing: object A in frame n
   with object A in frame n+1. If the draw order is stable (the original
   sorts by depth, so it can reorder) or every object carries an id, the
   pairing is easy; otherwise it needs a matching step.

## Stages

Each is judged by a check, in the manner of the rest of this project.

**Stage 0 - measure (this page's open questions).** An observer in the
recompiled engine that logs, per frame, every call to the drawing
primitives with its arguments. Output: a census of what draws what, how
many primitives a frame holds, whether vertices carry fractions, whether the
order is stable. Check: the log of two runs of a route is identical, and
turning the observer on leaves every checkpoint and final hash unchanged.

**Stage 1 - draw lists.** Capture a frame's primitives (polygons with
colour and vertices, lines, sprites with their driver arguments, text) into
a list instead of only into pixels. The pixels the original still draws are
the reference. Check: replaying the list at 320x200 with our own rasteriser
reproduces the original's frame bit for bit, on every frame of every route.
That check is the proof the list is complete, and it is the hardest part of
the whole item: it needs the rasteriser to match the original's pixel rules
(top-left, fill order, the 16-bit edge arithmetic) exactly.

**Stage 2 - high-resolution re-draw.** Draw the same list at N times the
resolution (the vertices scaled, edges drawn exactly at the new grid). The
HUD and text are bitmapped and stay as they are, or are scaled with
integer nearest-neighbour. Check: at N = 1 it is Stage 1's reference, bit
exact; at N > 1 every N x N block of the output agrees with the N = 1 pixel
wherever the edges do not cross it.

**Stage 3 - interpolation to 60+ fps.** Between two consecutive draw lists,
pair primitives and move their vertices linearly in time; draw at the host's
display rate. The simulation is not advanced: the machine keeps its 15 Hz
steps and the in-between pictures are only drawn. Cost to acknowledge:
the picture is one logic step behind (up to 67 ms at 15 Hz), the standard
price of interpolation. Check: at the instant of a logic frame the output is
exactly that frame (so the original's picture is always reproducible), and
the in-between frames have no primitive that is absent from both
neighbours.

**Stage 4 - polish.** Frame pacing, vsync, a setting for the picture's age,
HUD handling, and a switch that restores the original picture exactly.

## Why this belongs after (and needs) Phase 2

Stages 1 and 2 need the drawing path *understood*: what each primitive's
arguments mean, so that they can be recorded and re-drawn. The matched
routines are that understanding, with the Reimp's names where they apply.
Every drawing routine matched in Phase 2 becomes one that the observer can
be attached to without guessing. So the two phases are not independent: the
work in P2 on the polygon, span, clip and sprite routines is the shortest
path into Stage 1.

## Risks

- **Stage 1's bit-exact replay may be the wall.** If the original's
  rasteriser depends on state we cannot see in the draw list (a clip window
  held in a register, a dither phase), the replay will miss pixels and the
  list is incomplete. That is a finding, not a failure of method: it says
  what else the observer must record.
- **Interpolation of things that are not smooth.** Explosions, tracer
  lines, the HUD, a cut between views: the pairing step has to leave them
  alone (hold the previous frame or switch on the logic frame).
- **Display-rate drift.** Interpolation against the host clock needs one
  agreed time base with the machine's; the mission-clock invariant gives it.

## What is decided and what is not

Decided, by the machine's design and measured facts above: presentation is
an observer; the machine keeps its 15 Hz steps; interpolation, not faster
simulation, gives 60+ fps.

Open, and the first thing Stage 0 answers: what "4K" can honestly be (a
sharper scaled picture, or real sub-pixel edges) depends on whether the
projected vertices carry fractional bits.
