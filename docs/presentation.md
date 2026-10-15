# 60+ fps and 4K: design and status

Roadmap Phase 3 asks for 60+ frames a second and 4K output; the owner's target is **4K at 60 Hz** (a 16.67 ms
host-frame budget) and, for the method, any scale and any refresh rate. This page says what constrains each, the
current plan, the rules each stage of the presentation follows and the measurements behind them. Every stage is
useful alone and none can change the emulated machine. Commands, costs and retained evidence of the checks are in
[repeated-processes.md](repeated-processes.md); the latest gate tallies are in the handoff.

## The constraint that matters: the presentation must not touch the machine

Phase 1's proof is that the recompiled game is the original, instruction for
instruction. A presentation path that changes a single guest write or clock
tick breaks that, and the routes would say so. So every stage is an
**observer**: it reads what the machine already does and draws something from
it. It never writes guest memory, never changes the instruction count and
never feeds back into the machine. The parity routes and `func_lockstep`
keep their meaning, and a presentation bug can only draw the wrong picture.

## What is known

- **The original runs at a low, speed-dependent rate.** The flight model
  steps once per drawn frame and divides every per-second rate by
  S = `[0x368E]`, which the frame-rate controller clamps to at most 15
  ([bugs.md](bugs.md) D1). Its pacing varies with scene and flight state:
  the nine-route study averages 16.27 drawn frames a second at 9 MIPS,
  with mean S 14.10. D1 limits faster CPUs to about 11.6 drawn frames a
  second and S 9, reproducing the parked GOG baseline rather than every
  airborne scene ([speed-sweep.md](speed-sweep.md)). 60 fps cannot come
  from the original drawing faster; it must come from pictures *between its
  states*, 4 to 7 per step. Raising S is not an option: the flight model is
  16-bit integer and rates smaller than S round to zero.
- **The host shows the last frame the VGA scanned out** and scales it with SDL
  (`--scale N`, `--fullscreen`, aspect correction). A large window or a 4K
  display already works as a scaled 320x200 picture; every scene edge is
  still on the 320x200 grid.
- **Presentation uses the machine clock.** The host paces emulated seconds
  against real seconds; the original's mission clock runs faster at every
  speed measured (the default, 20 million instructions a second, puts the world
  at about 1.12 times real time; [speed-sweep.md](speed-sweep.md)).
  Interpolation follows the machine's frame timestamps without changing that
  world speed.
- **One logic step is two `game_draw` phases** (`game_draw` is `0x01450`):
  one just after the step and one about half a step later. They alternate
  between two buffers in the library's 64 KB page segment (origin 0 and
  8A29h), draw different work (the scene is walked, transformed and cut into
  edges in one; spans are built and filled in the other) and repeat the same
  pattern step after step. An observer must collect across both before it has
  a frame's polygons. (The Stage 3 study found a frame closes at the phase that
  presents, and counted one phase a step without a sensor display, two with one;
  see [Stage 3](#stage-3-interpolation-a-320x200-study-8-october-2026).)
- **The frame has two rasterisers.** VGAME's own model rasteriser (transform
  `0x128B5`, projection `0x129A2`, edge and span tables, the row loops at
  `0x1418C` and `0x1420C`) draws the 3-D scene; the resident graphics library
  in segment `0889`, reached through the `1E42:0188...` jump slots (each
  `JMP FAR 0:0` in the file, patched at load), draws sprites, text, lines,
  span fills and the HUD. About a fifth of a flight's instructions run in the
  library.
- **The scene is flattened to integer pixels early.** `model_xform_vertex`
  gives 32-bit camera-space coordinates; the projection divides into an
  integer pixel, and the clip, edge accumulation, span tables and fill all
  work in whole pixels. Sub-pixel information exists only before the divide.
- **Interpolation shows the picture one logic step behind** (about 110 ms at 9
  steps a second, 67 ms at the clamp of 15), the standard price. Stage 1 needs
  the drawing path understood, which is what the matched routines in Phase 2
  supply, so the two phases are not independent.

## The decision: observe the draw path (5 October 2026, owner)

Two ways to get a high-resolution, interpolated picture were considered.
**A**, chosen: capture the original's own primitives in this project and
re-draw them. **B**, not taken: render from the data segment with the Reimp's
renderer, which would reuse code but make this project depend on the Reimp's
renderer and assets, and on a renderer held to its own oracle rather than to
this machine's instruction-level one.

## The current plan for 60+ fps (15 October 2026, owner)

- **Stop improving primitive pairing.** Interpolating between two captured draw lists (Stage 3) switches shapes that
  have no partner and deforms shapes paired across different visible-vertex sets
  ([Identity pairing and the step clock](#identity-pairing-and-the-step-clock-15-october-2026)).
- **The safe sharp mode meanwhile** is `--present replay --present-scale N`: the original's own frames refined N
  times, at the game's own frame rate, no in-between frames; already verified.
- **The chosen method:** re-run the original's own, unmodified 3-D draw on a throwaway copy of the machine at each
  in-between moment, with only its inputs nudged: the camera (view angles `E478`/`C0A8`/`B79C`, position
  `B2B4`/`B77E`/`B788`, view mode `C0A6`, documented on `vgame_frame_compose` 2F9D) and moving objects by their table
  slots (aircraft traffic 5852, weapons in flight 683E; a reused slot steps instead of gliding). The existing uprez
  refines those draws as it does logic frames, and a smoothed playback clock (the step clock, below) times them. It
  must work at any `--present-scale` and any refresh rate (the owner asked for lower and higher than 4K/60, not just
  4K/60).
- **Safety proof required:** guest checkpoints/final hash unchanged on every route, plus a new check that the real
  machine's memory is byte-identical before and after each extra draw; nothing in the copy may reach files, sound or
  devices.
- **Feasibility, measured (15 Oct 2026, experiment branch `draw-rerun-probe`, `F117_RERUN_PROBE=FILE`):** at each
  2F9D entry the machine is copied; between run slices the copy runs 2F9D to its return. On the owner's flight
  (1,609 logic frames): every re-run returns; 1,607 reproduce the real draw records exactly (two adjacent pre-flight
  frames differ, probably an observer-buffering artefact, unconfirmed); the real machine's memory and state hash never
  change, and the final hash equals a run without the probe (`71bc32c7d96fe6b9`). Nudging the inputs 2F9D reads (the
  aircraft's 32-bit position `BA3C`/`C056`, attitude `2DEE`/`2DF0`/`2DF2`, altitude `2DF4`/`2DF6`; 2F9D copies them
  into the camera words) halfway to the next frame puts 400,569 of 412,022 vertices (97.2%) within tolerance of the
  two real frames' midpoint (1,207 frames with matching vertex lists). Cost per extra draw: 1.08M instructions (max
  1.63M), **6.2 ms (max 8.8 ms) on one core** with translated code. Estimate from that: at a 57 ms step, 60 Hz needs
  about 2.5 extra draws a step (~27% of a core), 144 Hz about 7.5 (~80%), so higher rates need several copies in
  parallel, plus the existing refinement per picture.
- **Follow-up (15 Oct):** compared per object (batches aligned by vertex count across both real frames and the
  nudged copy), 98.4% of the nudged vertices on the owner's flight match an object in both frames and 98.0% of those
  sit at the midpoint. Every unequal re-run (2 of 1,609 there; 861 of 3,679 on air-to-air type 6) differs only by the
  observer's lazy `n` records (its own pending-line global, not the machine's): without them 3,679 of 3,679 are exact.
  Aircraft slots (24h bytes at `C16A`, `[DEFC]` of them; +2/+4 are the 32-bit X/Y >> 5) can be nudged too: on
  air-to-air type 6, scene-walk objects at the midpoint rise from 97.7% to 98.9%; a small `DF03` group (about 1,700
  vertices, upstream `C2C8`) drops from 98.6% to 96.6% and is open. Weapons in flight (twelve 28-byte records at
  `3C3A`; from the movement code +0/+2/+4 X/Y/height, +6 pitch, +8 heading, +0Eh life, +10h type) nudge too: the
  deferred-object group (`D883:DF03`) at the midpoint rises from 565,904 to 566,381 of 573,592. The final hash is
  unchanged in every run.
- **Requirements found:** a copy must redirect every back-pointer to its machine (`cpu.user` pointed at the real
  machine at first, so the copy's port reads and writes reached the real devices), clear hooks, the log, the input
  recorder and the OPL schedule, and disable coverage. The draw observer (`g_f117_observer`), the recompiled-code
  runtime (`g_rt`) and the code-write bits are process globals, so copies must run on the machine's thread until
  those are made per machine. Moving objects (slots) were not nudged yet.
- **Status:** feasible; not yet implemented in the presentation.
- **Integration design (15 Oct):** an in-between picture is frame k's own draw records with the part from the 2F9D
  entry to its return replaced by the nudged copy's records (the copy's record count is that boundary exactly; the
  probe shows the unnudged copy reproduces it). Records before 2F9D and after its return (cockpit, panels) stay as
  frame k drew them; the HUD and bands already come from state at the in-between moment. The spliced list goes
  through the existing replay and refinement at any scale; the smoothed clock picks the moments; copies run on the
  machine thread between slices until the observer and runtime are per machine.
- **First pictures (15 Oct):** `F117_RERUN_PICS=DIR:FIRST:COUNT` lets the nudged copy run on to the original's own
  present and dumps that 320x200 picture beside the two real frames; on the owner's flight (frames 1450-1462,
  `D:/f117-gate/present-v2/rerun-probe/pics/strip.png`, `view.png`) the horizon, pitch ladder and HUD sit between the
  two real frames, drawn entirely by the original.
- **Next:** the spliced list through the refinement for N > 1 pictures; the `DF03`/`C2C8` group; make the observer/runtime per machine; then a switchable implementation with the smoothed clock, judged by
  `tools/motion_check.py` (deformation included) and the owner.
- **In reserve:** camera reprojection of one step's own draw list. No longer the main path: this project's own scene
  renderer (the unconnected `src/present/scene_mesh.c` draft;
  [Consolidate world geometry](#consolidate-world-geometry-before-more-clipping-exceptions-15-october-2026)).
  Draw distance is not extended.

## Settings and switches

| Setting | Effect |
|---|---|
| `--present scan` (`present = scan` in `f117a.ini`) | the scanned-out picture, the original at every scale |
| `--present replay` | the replayed display, one logic frame at a time ([live path](#the-live-path-first-part-feed-and-replay-9-october-2026)) |
| `--present interp` | in-between frames, drawn on a worker thread ([threaded presentation](#fullscreen-4k60-timing-and-the-threaded-presentation-13-october-2026)) |
| `--present-scale N` (`present-scale =`) | the picture N times finer (off by default; N = 9 for 4K) |
| `--present-age interp\|extrapolate` (`present-age =`) | `interp` (default) or prediction past the newest frame ([Stage 4](#stage-4-pacing-and-the-pictures-age-9-october-2026)) |
| `--present-line-width N` (`present-line-width =`) | stroke width 1-20 output pixels; 0, the default, is one original cell |
| `f117a.4k.ini` | optional ready-to-run fullscreen preset |
| `F117_PRESENT_HUD=old`, `F117_PRESENT_BANDS=old` | the captured-call HUD / sky and ground instead of the state-driven ones |
| `F117_PRESENT_IDENT=1` | the identity-pairing experiment (off by default) |
| `F117_PRESENT_THREADS_OFF=1` | everything on the main thread, for comparison |
| `F117_PRESENT_DIAG=DIR` and its `_HIST`, `_RECT`, `_HIST_ONLY`, `_TRACE` | the pixel-origin diagnostic ([below](#where-the-blockiness-comes-from-the-pixel-origin-diagnostic-14-october-2026)) |
| `F117R_OBSERVE_PAGES`, `_HUD`, `_BANDS`, `_KINDS`, `_SCENE`, `_IDENT` | observer log options for `f117run --observe` ([record kinds](#stage-0-and-1-the-observer-and-the-draw-list)) |

At N = 1 every mode uses the original replay exactly, line quirks included. Do not force `F117R_OBSERVE_PAGES=1` on a
live presentation run: it reseeds every phase and prevents pairing.

## Stages

Each stage is judged by a check, as in the rest of the project.

| Stage | What | Check | Status |
|---|---|---|---|
| 0 | Observer: log every call to the drawing primitives per frame | the log is identical across runs; hashes unchanged with it on | done |
| 1 | Draw lists: a frame's primitives as a list, replayed at 320x200 | the replay reproduces the original's work and display pages bit for bit, every phase of every route | done: all 45 complete routes and staged original-instruction fixtures for rare branches; C and Python fill rules agree |
| 2 | Re-draw the list at N times the resolution | N = 1 is Stage 1 exactly; at N > 1 verified geometry has smooth boundaries and keeps the original viewport, fill styles and artwork | done and live (`--present-scale N`): model and span-rasteriser polygons from their camera-space geometry, near-plane crossings verified against the original's; one stroke pen for HUD lines, ticks and outlines; original bitmap text/artwork scaled by the owner's choice; HUD, panel items, map marker and sky/ground bands drawn from the game state ([HUD](#the-state-driven-hud-14-october-2026), [bands](#the-state-driven-sky-and-ground-bands-15-october-2026)); incompatible geometry safely scaled |
| 3 | Interpolate between consecutive draw lists to the host display rate | at a logic frame the output is exactly that frame; no in-between primitive absent from both neighbours | 320x200 study done; live interpolation in C, exact at both ends of every pair; primitive pairing frozen (15 Oct): in-between pictures are to come from re-running the original's draw ([plan](#the-current-plan-for-60-fps-15-october-2026-owner)) |
| 4 | Pacing, vsync, a picture-age setting, HUD handling, a switch to the original picture | pictures at the display's refresh with t on the mission clock; the age setting measured | pacing at the host's vsync, age setting, Reimp-style HUD handling and the threaded presentation built; isolated 4K/60 runs pass the timing checker; `--present scan` is the switch; smoothed step clock designed, not built; physical-display checks remain |

## Stage 0 and 1: the observer and the draw list

`src/matched/observe.c` hooks `game_draw`'s entry, VGAME's fill and outline
paths, matched/original projections and every graphics-library entry. It is a pure
reader: with it on, every checkpoint and final hash is unchanged.
`f117run --observe FILE:FROM:TO` writes the log; with `F117R_OBSERVE_PAGES=1`
it also dumps the library's whole page segment (`Z`, work page) and the
display (`Y`, A000) at each `game_draw`.

**Observer rules.**
- Observers are passive in both engines and under the 386 profile, including at addresses shared with matched
  bodies, and do not reset timing bookkeeping. They read guest memory directly, never through `mem_read8`, which
  charges the 386 profile's VGA bus cycles (an observer's reads of A000 would have moved the clock); page snapshots
  are `memcpy`s.
- Where a matched routine is placed at an observer hook's address, the matched routine runs and the hook does not
  (the first override placed at an address is the one called, and matched routines are registered first). So a
  matched routine at a hook address calls the observer at its entry itself, as the projection and
  `model_prepare_edge` do for `V` and `G`, and `vgame_model_edge_spans` (130D:004A) and `vgame_model_poly_finish`
  (130D:0116) do for `E` and `F`. When those two were first matched without it, the replay stayed exact (the fill
  rows carry the pixels) and the interpolation's checks passed, but it paired no polygon.
- The observer's hooks exist only in the recompiled engine; under the interpreter the feed sees nothing.
- All five projection return paths emit `V`, and the original edge-preparation entry emits `G`. Orthographic `V`
  records use range 3, read the actual projected pixels even at Z=0 and leave projection-mode reconstruction to `G`
  metadata; the legacy perspective path rejects them rather than inventing a divisor, and `V`-only interpolation
  holds them while compatible `G` snapshots move.

**Purity evidence.** The default paired boot captures have all 217,022 `V` and 300,637 `G` payloads identical between
the engines, including far/mid/behind-eye/orthographic cases (matched `V` timestamps charge RET before recording and
the original hook records just before RET, so this comparison excludes their clocks). Under 386 both engines execute
the original projection: the complete 751,862,587-byte logs are byte-identical, including 446,978 `V`, 618,966 `G`, 80
checkpoints and the final hash. The first 386 comparison exposed a missing shared-address observer although guest
hashes matched; a retained regression checks that dispatch explicitly. Private verdict:
`D:/f117-gate/observer-original/final-gate-verdict.json`.

**Record kinds.** `P` phase, `V` projected vertex (camera-space x, y, z and the pixels the original made of it), `G`
edge prepared (with both endpoints' camera snapshots; field 18 the projection mode), `E` edge of a filled polygon,
`F`/`R` the polygon's fill and row spans, `b`/`a` fill rows (solid or AND/OR/discard styles; `a` is dither/stipple),
`B`/`L` outline polygon and its edges, `N` library line, `K` colour, `Q` span fill, `U` span-rasteriser polygon
corners, `C` blit, `T` text (with its font, so the replay needs no game data), `S` sprite, `W` scaled RLE sprite, `H`
tick scale, `D` page copies (44 present, 48 copy, 79 dissolve), `M` colour replace (entry 41), `X` any other entry, `x`
changed byte deltas, `J` the display palette (768 six-bit DAC components, logged beside `Y` with
`F117R_OBSERVE_PAGES=1`). State records: `h`, `p` (`F117R_OBSERVE_HUD=1`) and `o` (`F117R_OBSERVE_BANDS=1`), with the
state-driven HUD and bands below; `i`/`j` (`F117R_OBSERVE_SCENE=N`) and `I` (`F117R_OBSERVE_IDENT=1`), with the scene
capture and identity pairing. `F117R_OBSERVE_KINDS=LETTERS` keeps only those kinds in the log (a corpus of a few kinds
over a whole flight).

`tools/drawlist_frame.py LOG` rebuilds each phase from the previous page dump
plus every captured primitive in order and compares it, byte for byte over
the whole 64 KB page, with the dump at the phase's end; `drawlist_spans.py`
checks primitives one by one; `hires_spans.py` is the Stage 2 check. Facts the
replay rules rest on:

- **Model polygons** replay to the original's pixels bit for bit: the model
  edge walk (`1377:072B`), the polygon accumulator (`130D:004A`), the near-clip
  join and border runs, and fill styles from the colour word the fill really
  uses (`[8606]`: FF solid, FE AND, FD OR, FB discard). Outline polygons use
  VGAME's own line routine (`1377:046F`, via the style table at `[85F2]`).
- **Library entries**: lines (31), span fills (37, 40), blits (42; a page is an
  index into the page table at `cs:[0787]`: 0 display, 1 work page, 2 cockpit
  art), text (entries 1-6: a parameter block and a NUL-terminated string with
  width and row clips and a left skip; glyph rows from the driver's font table),
  sprites (73 and 18; 71 and 19 clip first), tick scales (11), the scaled
  flippable RLE sprite (22, with Bresenham-style column and row stepping), the
  page switches (12-16, so lines and span fills carry the page and origin they
  were drawn at), the present (44) and the colour replace (41, driver 1188: in a
  rectangle x0..x1, y0..y1 of a page from the page table, every byte equal to one
  colour becomes another; rows from the row table, no origin; the HUD and the
  cockpit's displays use it on the display every phase). Entry 46 writes only
  the DAC and CRTC start, so its byte deltas are the fill's own rows. Entries 24
  and 26 set the origin. Entries 13-16, 24-26, 45, 46, 62 and 65 have no rule: they
  set state (pages, origin, DAC, CRTC start), and primitives already carry their page
  and origin.
- **Capture traps:** a nested state setter can split a parent entry that resumes
  drawing, and an entry can write to A000 directly even when the active page points
  elsewhere, so both page snapshots restart after every graphics entry; a pending
  line or span capture closes at the next entry but a model fill's does not (the
  fill calls entry 46 while it paints).

### Coverage (30M-instruction windows)

With 0 bytes copied from the original, every phase rebuilds exactly on both
pages on these routes (phases in brackets) at the windows tried:

- Flights from `ordnance.pic` onward: strike (73), recon (34), Central Europe
  air-to-air (29), landing (38), cargo (38), Korea, Kuwait, Middle East and
  North Cape strikes (39, 33, 28, 27), Central America ground training (27),
  Persian Gulf air training (32), Vietnam air-to-air (27), secret airstrip (32).
- Later in flights: cargo at 7.30B (45), strike at 8.53B (69) and at the impact
  8.72B (56), landing at 9.90B (18), `recon_return` at 14.0B (69),
  `strike_return` on the landing approach at 14.99B (45), and the AMRAAM launch
  and kill windows of `airair_type6` (5.092B, 5.240B: 59, 57 phases) and
  `airair_type5` (6.793B, 6.914B: 57, 66).

Command per route: `F117R_OBSERVE_PAGES=1 py tools/run_route.py
tools/routes/NAME.args --data DIR --out OUT -- --observe LOG:FROM:TO`, with
FROM just after the flight's `ordnance.pic` open, then `drawlist_frame.py LOG`.

These windows did not exercise dither/stipple, opaque/width-clipped text, sprite clip edge cases or the tick scale's
CL variant; the staged original-driver checks below cover those branches, and the complete-route live replay checks
cover the rest of each scripted flight.

### The replay in C

`src/present/drawlist.c` is `drawlist_frame.py` in C, rule for rule: it is fed one record at a
time (`drawlist_record`, a log line's values without the clock) and draws on the pages it was
seeded with (`drawlist_seed`, the work page and the display as the observer dumps them). It reads
nothing from the machine. Where the Python replay copies a page copy's logged source bytes (the
present, entry 44), the C replay uses its own copy of the source page when it holds it, so the
display check also covers the work page at the instant of the present; `prefer_logged` does as
the Python does.

`build/test_drawlist.exe LOG [--carry] [--logged]` replays a log and prints
`drawlist_frame.py`'s lines, so the two outputs diff. `--carry` seeds each page once, from its
first dump, and carries the replay from phase to phase, as a live replay must. On eight 30M
windows (strike 8.53B and 8.70B, landing 9.70B and 9.89B, `airair_type6` 5.08B and 5.23B,
`airair_type5` 6.793B, a night take-off roll 2.50B; 447 phases; retained as `p3-live/logs41/w_*.log`), the output is
identical to the Python replay's in all three modes, and every phase is exact on both pages, carried too: what the
original draws on the two pages in flight is all in the records. A log of 75 MB replays in under a second.

Everything in the records is replayed by a rule, with every `x` record removed. The colour replace (entry 41, `M`; 0
to 330 a window) was the last rule: without its records 0 of 69 (strike) and 1 of 57 (`airair_type5`) display phases
are exact.

### Fill styles and rare driver branches (13 October 2026)

All model fill styles replay by rule. Dither alternates the colour word's bytes by logical row/column parity; stipple
paints the opposite-parity cells and preserves the background in the others. The clipped column, not the
destination's byte alignment, determines the pattern. Post-fill `a` records are evidence only. The fine renderer
keeps each pattern cell the size of an original pixel and refines the polygon's boundary; interpolation can move
these polygons too. C and Python use the same rules.

`test_present_fills` compares 2,619 staged cases with the original VGAME instructions, including all fill style
classes, clipped spans, odd page alignment and N=1/4/9 mirrored output; Python also matches 2,187 exported original
rows. `test_present_driver` matches 3,953 staged cases against the original resident driver: opaque/transparent text,
variable/fixed fonts and all text clips; transparent sprite clips; scaled RLE clipping, leading and internal
transparency, empty rows and both flips; both tick-scale variants. These are synthetic drawing fixtures, separate
from normal-play route evidence. The camera-plane polygon regression checks coarse-sized pattern cells over an
existing background at N=1/4/9 (transparent stipple, equal colours, discard) and moving dither/stipple polygons at
both interpolation endpoints and three intermediate times, with reused edge slots.

## Stage 2: what the list holds and what it needs

The edge records carry integer screen endpoints, already rounded to the
320x200 grid by the original's projection and clip, so scaling them by N and
running the same edge walk gives only an N x N nearest-neighbour picture.
Real sub-pixel detail needs the camera-space vertices before the divide (the
`V` records), and the clip (which drops a whole edge that leaves the window
rather than clipping it) and the span stage must be redone at the new
resolution.

`hires_spans.py` re-walks every filled polygon's edges on an N-times grid and
tests the stage's rule on the 3,332 coarse pixels whose eight neighbours are
inside the polygon (so no edge crosses them): N = 1 agrees on all, **N = 2 on
88.8%, N = 4 on 85.8%**. Every disagreement is in 36 four-edge polygons whose
edges carry the clipper's statuses 0x80 and 0x41 (an edge rejected to borders
only, or near-clipped): the original clips whole edges in integer coordinates
and records their borders from y ranges, and those rules do not scale. So the
plain scaled walk is right for unclipped polygons and the clip stage is what
has to be redone.

### First Stage 2 build (8 October 2026)

`tools/hires_frame.py LOG [N] [OUT_DIR|-] [--floor]` replays a log with the Stage 1 replay and keeps beside each page a
picture N times finer: every write is mirrored as an N x N block, except a model polygon's fill, whose edges are
re-walked on the fine grid (coarse pixels no edge can cross are covered whole; the clipped ones, where the walk does
not reach a fine row, keep the plain scaled span). Page copies and blits carry the fine rows across.

It showed why the integer edge walk cannot be refined: two polygons meeting along an edge left cracks (each fine walk
puts its boundary at a coarse pixel's top-left corner; strike window, N = 2: 84.9% of flat coarse pixels kept, 8.4% of
the picture unlike the scaled copy), and with `--floor` (each fine fill laid over the nearest-neighbour fill) the
picture is correct and no sharper (100% kept, 0.13% unlike). Nothing in the integer list says where the true shared
edge lies; that needs the camera-space vertices and a fill rule that tiles, the sub-pixel re-projection.

### Sub-pixel re-projection (8 October 2026)

`tools/hires_subpixel.py LOG [N] [-] [--grow G] [--crops DIR] [--no-guard]` fills each model polygon from its true
geometry on the N-times grid over the same replay; `src/present/hires.c` is its builder in C, and `drawlist.c` keeps
the fine picture (`HiPage`, `drawlist_set_scale`: every write mirrored as an N x N block unless the page is paused while
a refilled polygon's rows arrive; blits and page copies between held pages carry the fine rows).
`build/test_hires.exe LOG N [--carry]` prints the study's report.

- **Vertices.** An `E` edge record names its slot; the `G` record of that slot names the two vertex records; each `V`
  record has the camera-space x, y, z and the pixel the original made. The projection (`0x129A2`) is `x >> 8` over the
  high word of z (or `x >> 1` over `(z >> 8) >> 1` below 0x100): every `V` record of a strike window reproduces under
  that formula (13,402 of 13,402). Since 15 October the fine position is the full fixed-point ratio
  ([one stroke pen](#projected-outlines-point-markers-and-one-stroke-pen-15-october-2026)); the first rule (the
  divide's remainder) is superseded.
- **Fill rule.** A fine pixel is painted when its centre is inside the true polygon, left and top inclusive, so
  polygons sharing a vertex pair tile. At N = 1 the footprint is the original's, so the picture is Stage 1's. The first
  rule also clipped every polygon to the original's footprint with a measured overshoot; superseded on 13-14 October:
  verified polygons clip to their viewport with half a fine pixel of inclusive coverage
  ([Joined strokes](#joined-strokes-outline-edges-and-span-rasteriser-polygons-13-october-2026),
  [Footprints](#footprints-the-originals-clipper-gets-wrong-14-october-2026)).
- **The flat-cell guard** restores the coarse value on a flat block the fine fill disagrees with (the horizon residue:
  a ground polygon whose true top lies half a row below where the original's inclusive spans started it); it counts
  what it restores. It excludes verified fine geometry and HUD strokes, so it cannot put coarse stairs back, and it
  checks safe unaligned 8/4-byte groups and tails (a scalar regression preserves every output byte and repair counter
  at N=1..16).
- **Left scaled:** geometry that does not reproduce the original (below), fills with no replay rule, and by choice the
  original bitmap text, sprites and cockpit art.

The 8-9 October measurements of the first rule (five Python windows, eight C windows at N = 1, 2, 4 and 10, C and
Python identical 24 of 24, 447 of 447 display pages at N = 1) are in the git history. Their lesson stands: the refined
picture differs from the scaled copy several times more than the floor mode does, growing with N, at hill edges,
ground bands and the horizon, and the polygon stage is cheap; what is expensive is everything else drawn at N.

`tools/hires_subpixel.py` still has the earlier footprint/seam rule for near-clipped polygons and the 3 px test alone;
it no longer mirrors `hires.c` on them.

### Stage 2 live (9 October 2026)

`--present-scale N` (`f117run`, `f117a`, `present-scale =` in `f117a.ini`; off by default) runs the sub-pixel builder
beside the live replay and presents the N-times picture with the scan's palette (the shots and `--screen` are 320N x
200N). The guard is applied to every picture shown. With interpolation, an in-between frame moves the vertices of every
paired batch in camera space (`INTERP_VERTICES`), so a moved polygon is refilled from moved geometry; the replay state
carried for in-between frames includes the builder's. Endpoint checks at a scale use t = 1e-9 (1e-6 of a step can move
a camera-space coordinate by a rounding unit, which a finer grid shows: 5 of the 9,843 strike pairs at N = 2, 50 at N
= 9). Strike, the whole flight: 9,843 of 9,843 pairs exact at both ends, coarse and fine, at N = 1, 2, 4 and 9; hashes
unchanged at every scale.

N = 9 is the choice for a 4K screen: the picture is 2880 x 1800, and shown 4:3 (each row 1.2 times as tall) it fills
the 2,160 lines.

### Camera snapshots, near-plane crossings and orthographic maps (11-13 October 2026)

- **Camera snapshots.** `G` records carry both endpoints' camera coordinates, projected coordinates, vertex
  identities, behind markers and projection origin. C and Python use them for every polygon: inferring the origin from
  unclipped edges kept the previous view's origin when every edge was clipped, and `V` records go stale when vertex
  storage is reused. Legacy short `G` records keep the scaled fallback.
- **Near-plane crossings.** `nearclip.c` repeats the original wrapped ADD/ADC-plus-one midpoint and signed shift,
  including DEC DX's sign test, then keeps x/128 and y/128 before the original discards precision. Each crossing must
  reproduce the original's retained coarse crossing exactly; a mismatch or unsupported shape keeps the scaled picture.
  Two valid cuts close the visible polygon. 5,014 crossings across strike, landing and air-to-air match the original.
  *Superseded on 14 October:* the 11 October footprint clamp, coverage tracking and seam repair
  (`drawlist_finish_models`, `DT_SEAM`) were removed when near-clipped polygons began to be drawn from their geometry.
- **Orthographic maps.** Map objects legitimately have Z=0, so `G` appends the byte at `[7D8E]` as field 18: zero for
  perspective, otherwise the orthographic shift (fields 1-17 unchanged). C and Python reproduce the original high-word
  shift and rounding and require both coarse coordinates to match the capture before using fine positions. Shifts
  16-31 keep full fixed-point precision; shifts 1-15 preserve the original wrapped integer vertices and draw their
  edges on the fine grid. Unknown modes and inconsistent coordinates keep scaling. All 196 orthographic endpoint
  projections in a fresh boot capture match the original; independent C/Python replay of the orthographic prefix
  agrees on 45 polygons, 40,467 footprint pixels and 619,517 fine-grid paint pixels at N=4.
- **Interpolation** binds each `E` to its preceding `G` within its batch (slots are reused with different vertex
  identities), preserves 17- and 18-field records, and moves camera snapshots, retained crossings and projection
  metadata only when the pair agrees (same mode, origin, markers, identities, topology); otherwise the skeleton's
  records hold. Endpoint checks do not replace a visual motion check.
- **Results.** Recorded strike, landing and air-to-air windows pass at N=1/4/9: strike refines all 17,058 evaluated
  polygons, landing all 4,916, air-to-air 4,110 of 4,111. Live boot N=4/9 refines all 48,856 model polygons. The one
  air-to-air case left has verified near crossings with extremely large coordinates (x=229,549 becomes signed -32,595
  when the original line clipper uses its low word); its coarse silhouette differs substantially from the camera
  geometry, so the original scaled polygon is kept. Missing metadata in older or partial captures also keeps scaling.
- **Tests.** A synthetic diamond crossing the viewport on every edge, with stale behind-eye `V` records, reconstructs
  at N=1/4/9 (N=1 exact). The observer regression checks field contents with ordinary, VGA and wrapped memory
  placements while requiring unchanged CPU state, instruction clock and memory; synthetic tests cover signed rounding,
  zero depth, low-shift wrapping, incompatible modes and corrupt projections.

## Joined strokes, outline edges and span-rasteriser polygons (13 October 2026)

The owner's first 4K review found three kinds of roughness, each with its own cause.

**Strokes that did not join.** Strokes were cut square across their direction at both ends and centred on a cell's
corner, so two strokes meeting at an angle left a notch (the canopy corner, the bearing-tape marker) and every stroke
sat half a cell up and left of the cells it stands for. They now run between the centres of their end cells with whole
end caps, and **outline edges** (record `L`: ground markings, the weapon target box), which were scaled blocks, use the
same stroke. The current pen is described in
[Projected outlines, point markers and one stroke pen](#projected-outlines-point-markers-and-one-stroke-pen-15-october-2026).

**The horizon and the sky and ground gradient.** These are polygons the game fills through its own span rasteriser
(fill_polygon, VGAME `0FB2:01E6`), not model polygons, so the fine renderer only saw their span fills. A hook at that
routine's entry logs each polygon's corners (`U`: colour, count, x and y words in the rasteriser's own coordinates,
which are far outside the picture for a horizon strip) before its span fill (`Q`). The fine renderer validates the
corners against the original span footprint and at N > 1 draws their cell-centred geometry with half a fine pixel of
inclusive coverage, clipped to the span table's bounds. A narrow region keeps its two boundaries; only a polygon with
zero area becomes a stroke (an earlier 60%-of-footprint rule made narrow regions overly thick lines). Interpolation
pairs `U` records by colour and order and moves their corners with the span fills. (At high detail the bands are now
drawn from state: [below](#the-state-driven-sky-and-ground-bands-15-october-2026).)

**Smooth terrain and triangles.** Fine polygons used to be clipped row by row to the original coarse footprint, which
kept the original staircase and could cut teeth into a slanted edge. Ordinary verified model polygons now clip to
their viewport and draw the actual geometry, with half a fine pixel of inclusive coverage to join adjacent faces. N=1
still paints the exact original footprint. Near-plane polygons are drawn the same way since 14 October (see
"Footprints the original's clipper gets wrong"); incompatible geometry stays scaled. Dither/stipple cells, bitmap
lettering, cockpit art and map symbols remain the original artwork.

**The map is refined.** Its terrain is drawn once into the work page, then copied through the cockpit-art page
(segment `09C0`) and back every frame; the replay does not hold that art page. A bounded cache of eight fine
rectangles carries those rows through the round trip, restoring a rectangle only when its logged coarse bytes still
match. It survives replay-state copies and resets with the state; ordinary writes and changed source bytes keep the
coarse fallback. A display blit is also logged as an `x` byte-change record: for entries 42 and 46, repeated bytes
that already agree do not flatten the fine rows the `C` record carried. The map's orthographic projection rounds to the
nearest integer, which names a cell centre, so vertices take that half-cell alignment rather than being treated as
corners (this fixed a displaced bottom band and top-right tab; flattening cells afterwards left sawteeth and is not
used). The map's triangles follow their geometry through the cockpit page-copy cycle.

**Tests.** `test_present_map` pins a straight orthographic triangle edge and its rounded lower vertex, the fine
rectangle's round trip through an unseeded page, independent ownership after a replay copy, repeated display-blit
bytes and stale-byte fallback, at N=1/4/9. `test_present_strips` covers thick, narrow, degenerate and incompatible span
polygons; `test_near_polygons` checks that adjacent near-clipped faces leave no hole and that the near-clip tolerance
accepts and refuses what it should. Evidence: `D:/f117-gate/map-refill`.

## Where the blockiness comes from: the pixel-origin diagnostic (14 October 2026)

`F117_PRESENT_DIAG=DIR` (with `--present interp` or `replay` and `--present-scale N > 1`; off when unset) gives
every replayed page a byte per fine pixel naming the path that last wrote it (`src/present/diagtag.h`: refined
model polygon, refined span-rasteriser polygon, each fallback by reason, guard repairs, strokes, text,
sprites, blits from pages the replay does not hold, copies). The plane follows page copies, blits and the map's
overlay cache as the fine pixels do. Beside each `--shots`/`--screen` picture, `f117run` writes `BASE.tag`,
`BASE.diag.ppm` (false colour), `BASE.holes` and `BASE.txt`; `F117_PRESENT_DIAG_HIST=FILE` appends the per-tag counts of
a window (`F117_PRESENT_DIAG_RECT=x0,y0,x1,y1`, default the 3-D view 0,0,320,91; `F117_PRESENT_DIAG_HIST_ONLY=1` skips
the images) followed by the window's two hole counts, `F117_PRESENT_DIAG_TRACE=FILE` logs each polygon refused for a
footprint beyond 3 px, and the final `[diag]` lines print the builder's and the interpolator's counters.
`tools/present_diag.py` makes the side-by-side PNGs and class tables. Guest hashes and presented pictures are identical
with it on (strike to 2.598B, N=4: `a8bc78f294805e14`, four shots byte-identical).

The hole counts (`diag_holes`, `headless.c`) are over the window, in fine pixels: *unwritten* is a pixel no path
wrote in this frame (the seeded copy of an earlier frame shows); *stray* is a pixel a scene path wrote whose colour
is none of the nine coarse cells around its own, so no boundary shift of up to a cell explains it (a gap showing what
lies under the polygons, or geometry drawn where the original drew another colour). `BASE.holes` marks them (1, 2).

Before the fixes in the next section, on the strike, landing and air-to-air type 6 flights (N=4, 144/167/126 frames
sampled every 5 s of emulated time, 3-D window 0,0,320,91), the view was 88 to 89% refined geometry (model polygons 9
to 34%, sky/ground span polygons 55 to 83%), strokes 2.5 to 5%, text 2%, canopy-frame sprites 3.4%, the rest scaled
fallbacks. Two things left visible blocks: near-clipped polygons clamped to the original footprint (4 to 28% of the
view; 32 to 65% of the colour changes along their edge against the sky lay on the cell grid, where a refined edge gives
25% at N=4), and polygons refused for "footprint beyond 3 px" drawn whole as the scaled copy (the test was exact, so
1 missing pixel in 15,000 refused a large sea/ground or sky polygon; in 6 to 8% of sampled frames, in bursts, up to
83% of the view became cell-sized blocks). Evidence: `D:/f117-gate/present-v2/fix3d`. HUD lines interpolated as
integer endpoints, moving in whole cells; the state-driven HUD replaces that.

## Footprints the original's clipper gets wrong (14 October 2026)

**The rule.** At N > 1 every verified model polygon, near-clipped ones included, is drawn from its geometry
(`geometry_cover`, half a fine pixel of inclusive coverage, clipped to the viewport); the footprint only decides
whether the geometry reproduces the original. The exact test (footprint reproduced within 3 px) is kept; beyond it,
by polygon kind:

| Kind | Rule at N > 1 | Why |
|---|---|---|
| Near-clipped model polygon (`nclip` 2, both crossings already verified against the original's) | accepted when the footprint lies within 16 px (Chebyshev, steps 4, 6, 9, 12, 16) of the geometry and the geometry spills over the footprint by at most a tenth of the footprint's size (`NEAR_CLIP_TOLERANCE`) | measured below |
| Span-rasteriser polygon with an edge whose 16-bit difference overflows (two corners more than 32,767 apart) | accepted, and drawn from its corners | every refusal has one |
| Zero-height span strip (all corners on one row) with a one-row footprint | the original's row is the rendition: tagged `DT_SPAN_LINE`, no refusal | there is nothing to refine; 28,672 of the 31,439 trace lines of one run were these |
| Zero-area span polygon with a taller footprint | kept scaled, `DT_SPAN_DEGENERATE` | the original's own artefact (two to four per flight run) |
| Anything else beyond 3 px (a non-clipped model polygon whose geometry sits rows from its footprint, no overflowing edge) | kept scaled | geometry that does not explain the picture |

At N = 1 nothing changes: the footprint is the picture, and `refill_span_polygon` returns before any of this.

**The cause, as measured.** Refusals were traced over the strike, landing and air-to-air runs
(`F117_PRESENT_DIAG_TRACE`). Span polygons: every non-degenerate refusal (863 to 49,000 per run) has an edge between
corners more than 32,767 apart, which sends the original's clipper (`0FB2:0757`, `vgame_span_edge_clipped`) down its
halve-both-axes path (RCR/SAR on the differences, losing their low bits) before the 32-bit-product divide that places
the clipped endpoint. The resulting footprints lie up to 8 px (most), 128 px (a few) from the true band (example: a
colour-128 band with true rows around 66 and the original's footprint three full rows at 70 to 72). Overflow alone does
not predict refusal: 316,560 overflowing polygons in one run matched within 3 px; why only some are off was not
established (the clipper was read, not reproduced offline). Model polygons: all 833 refused near-clipped polygons have
a crossing 4,800 to 280,000 px outside the picture (826 beyond 32,767; the 16-bit edge record holds the wrapped value,
-279,589 appearing as about -17,447); the original clips such an edge with a 32-step bisection and 16-bit slopes
(`vgame_mc32_bisect_one`, `vgame_mclip_point`), and the footprint is within 4 px of the geometry for 82% of them, 6 px
for 90%, 16 px for all, with the geometry spilling over by 0.5 to 4.6%. The 4,198 refused non-clipped polygons are
mostly tiny polygons whose geometry lies rows from the footprint (corners at y 61.2, footprint at row 64): a geometry
mismatch, not clipper error; they stay scaled.

**Before and after** (N=4, 3-D window, the same sampling; before is `46e1d7c` with the hole counters added):

| | strike (144) | landing (167) | air-to-air (126) |
|---|---|---|---|
| frames with more than 0.5% of the window in fallbacks | 12 (8.3%) to 4 (2.8%) | 12 (7.2%) to 10 (6.0%) | 7 (5.6%) to 1 (0.8%) |
| frames above 5% / above 20% | 10 / 7 to 0 / 0 | 7 / 7 to 0 / 0 | 7 / 4 to 0 / 0 |
| worst frame | 83.6% to 1.7% | 55.6% to 3.3% | 42.4% to 0.7% |
| mean fallback share | 2.60% to 0.04% | 2.04% to 0.09% | 1.32% to 0.01% |
| unwritten pixels | 0 to 0 | 0 to 0 | 0 to 0 |
| mean stray pixels (share of the window) | 10,823 (2.3%) to 12,275 (2.6%) | 10,413 (2.2%) to 11,618 (2.5%) | 5,355 (1.1%) to 6,574 (1.4%) |
| worst stray frame | 7.1% to 14.6% | 7.5% to 17.4% | 4.9% to 22.9% |

The remaining fallbacks are `DT_SPAN_DEGENERATE` polygons (0.03%), non-clipped model polygons and
`DT_FB_NEAR_CROSSING` (0.06% of landing). Over the sixteen `present_diag.py` frames, colour changes between two scene
polygons that lie on a whole-cell run of the cell grid fall from 54,488 of 378,633 (14.4%) to 5,404 of 379,446 (1.4%);
the four burst frames go from 36.3, 83.6, 52.2 and 17.4% fallback to 0, 0.25, 0.70 and 0. One-to-two-pixel slivers
between two polygons beside a near-clipped one number 457 before and 141 after.

The stray count rises where the original's own clipper mis-drew a polygon: the picture now shows the polygon the game
asked for, which differs from the original's N=1 picture by whole rows in those frames (the horizon and sky bands in
the burst frames; up to 23% of the window in the worst). That is the intended result of "true geometry wins": in such
frames the fine picture is not a refinement of the original's, so the owner's parity judgement applies to it. N = 1 is
untouched.

**A hard turn** (Left held, Down pulsed every 3 s after the takeoff climb: a steep bank at 3 to 7 g and 17 to 27 deg/s
of yaw for about 12 s; a second script gives 10 to 40 deg/s; scripted routes turn at 2 to 3 deg/s): fallbacks were
already small (mean 0.01% / 0.02% before and after, worst 0.3% / 0.7%), scene cell-stair share 1.6% to 0.9% and 1.6%
to 1.0%, unwritten pixels 0. Hard turns do not reproduce the bursts, which occurred at small bank angles near the
horizon.

**Not settled.** The original clipper's exact error was not reproduced, so the tolerances (16 px, a tenth, the
overflow criterion) are bounds from the measured distribution on three flights, not derived limits: a flight with a
larger error would be refused (and scaled) rather than drawn wrongly. A span polygon accepted on its overflow is drawn
inside the bounding box of its footprint, so a band whose true rows lie outside the original's table loses that part.
Zero-area polygons with a tall footprint, non-clipped model polygons whose geometry mismatches and the stipple/dither
cells stay scaled. `test_present_strips` has the overflow, line and degenerate cases; `test_near_polygons` the
tolerance.

## Projected outlines, point markers and one stroke pen (15 October 2026)

At N > 1, unclipped world outlines and degenerate edges (the single-pixel scenery and light markers) draw from exact
`G` camera snapshots. Their slot, projected endpoints and origin must match the captured `L`; stale, clipped,
behind-camera and unsupported records keep the earlier path. Paired outlines interpolate their camera coordinates
when both snapshots name the same vertices. An internal `L` suffix carries that geometry without changing the original
ten replay fields. The original 1x picture and all guest execution are unchanged.

**Projection.** Perspective uses the full fixed-point ratio, `256*x/z`, before the original shifts its numerator and
depth. The earlier fine renderer kept the final divide remainder but still rounded depth, and its positive modulo
jumped at each negative projected integer; full projection removes both sources of motion steps, for polygons as well
as strokes. Orthographic and verified near-plane crossing rules are unchanged. C and the Python study use the same
projection.

**The pen.** Captured HUD lines, ticks, world outlines, thin strips and the state-driven HUD share one pen: strokes
between the centres of their end cells with perpendicular width and square caps (the earlier square nib made a
diagonal up to 1.4 times thicker), so an axis-aligned stroke of the default width covers exactly the original's
cells and joined strokes meet solidly. Default width is one original cell; `--present-line-width` keeps its
fine-pixel override. Different widths by drawing role are **not** assigned: identifying ground markings separately
from object outlines and tuning those widths remain open. The owner keeps all original sprites and artwork, including
the map's 4x4, eight-heading aircraft arrow; no vector replacement is requested.

**Checks.** `test_world_lines` keeps coarse equality at N=1/4/9, signed-coordinate and depth-boundary continuity,
stale-slot and vertex-identity rejection, and equal-width horizontal/diagonal strokes. `test_world_jitter LOG N`
measures paired outlines and dot markers at eighth-step intervals within each skeleton half, isolating interpolation
rounding from logic-step acceleration and cuts. On the retained strike window at N=9, RMS second differences fall from
1.680 to 0.0039 fine pixels for outlines (4,592 coordinate samples), and 2.756 to 0.0140 for dot markers (8,828).
Negative polygon axes fall from 0.496 to 0.0634 (76,753). A replay of the owner's flight also checks 19 exact frames /
17 exact pairs; its negative-axis RMS falls from 0.877 to 0.00049. These are coordinate measurements, not a claim that
every visible coast or topology transition is resolved: shoreline boundary measurements in the two sampled windows
improve little, and coastline transitions and per-class width tuning remain open. Evidence:
`D:/f117-gate/present-v2/world-lines`.

## Stage 3: interpolation, a 320x200 study (8 October 2026)

`tools/interp_frame.py LOG` (`--check`, `--pairing`, `--primitives`, `--geometry`, `--predict`, `--strip DIR`) pairs
each logic frame of an observer log with the next and builds in-between frames with the Stage 1 replay itself (fed a
synthetic list; no replay rule is copied). `src/present/interp.c` is the same in C (below).

**The logic frame.** The phases that present (graphics entry 44) close a frame: with a sensor display up (a Maverick's
MFD) a step is two phases (the main scene in the 64 KB page at origin 0, then the inset scene, the HUD text and the
present, at another origin), otherwise one. Measured at 9 MIPS: 8.8-10.6 steps a second in the strike and air-to-air
windows, 10.1 and 13.6 in the landing windows, 22.8 in a night take-off roll.

**Pairing.** The projection delivers a model chunk's vertices to one buffer from `C6B4` (a *batch*: one run of `V`
records); edges are numbered inside it (`G` slot, 18h apart from `5EEA`) and a polygon is the list of edge slots its `E`
records carry. So identity is (batch, edge slots):
1. batches are aligned between the frames in order by longest common subsequence. A pair needs an equal vertex count,
   or (chunks of three vertices or more) a count within 10% with 70% of the shared leading vertices within a quarter of
   their depth of each other in camera space, and a mean screen motion under 60 px; look-alikes go to the smaller
   motion;
2. painted polygons inside a paired batch pair by equal edge-slot lists (LCS), colour ignored (the skeleton's colour
   is used);
3. span fills (`Q`) pair by the colour set before them, their mode and their order; outline edges (`L`) by batch, slot
   and colour; HUD library lines (`N`) by colour, page and order when the group has the same size in both frames and
   the line moved under 30 px.

**Interpolation.** An in-between frame at t takes the nearer frame's list as its *skeleton* and moves every paired
primitive the fraction w of the way (w = t from frame n, 1 - t from frame n + 1). Everything else (text, sprites,
blits, ticks, an unpaired polygon, a polygon whose clip status or fill style differs) stays as the skeleton has it,
which is a switch at t = 0.5. A moved polygon has its edges moved, its span rows rebuilt by the original's edge walk
and is painted in the skeleton's fill style; the present copies the replayed work page, not the bytes the original
logged. Edges whose ends project from front-range vertices are interpolated in camera space and projected by the
original's divide; the others interpolate integer screen endpoints. Camera space is the right choice: of the edge ends
of polygons present in frames n, n + 1 and n + 2, 90.3% land exactly where frame n + 1 really has them when n and n + 2
are interpolated in camera space (strike window), 79.8% in screen space and 73.0% holding frame n; on the 9.89B landing
98.0%, 95.4% and 95.3%. A frame pair is a *cut* (held whole) when under half the polygons pair or the median vertex
motion exceeds 40 px.

**Checks** (`--check`, `--primitives`; seven windows of 28 to 73 logic frames): the replay of every frame, and the
machinery at t = 1e-6 and 1 - 1e-6, are exact on the work page and the display in every frame; provenance: every record
of an in-between list is a record of frame n or n + 1 or the regeneration of one paired in both: 0 violations in 741
in-between frames (in 20 of them the display holds a colour index neither neighbour does, from AND and OR fills over a
changed background; the rule does not forbid that).

| window (instruction clock) | steps/s | polygons paired | cuts held | vertex motion per step, px (mean / median / p95 / max) | edge ends exact: hold n -> interpolated | display pixels an in-between frame changes |
|---|---|---|---|---|---|---|
| strike 8.53B | 10.6 | 91.1% | 0 / 33 | 2.6 / 0 / 9 / 308 | 73.0% -> 90.3% | 0.13% |
| strike impact 8.70B | 9.4 | 86.2% | 1 / 29 | 4.9 / 1 / 19 / 5165 | 25.3% -> 40.3% | 0.86% |
| landing approach 9.70B | 10.1 | 98.0% | 0 / 31 | 1.5 / 1 / 6 / 116 | 50.6% -> 66.9% | 0.38% |
| landing 9.89B | 13.6 | 95.6% | 0 / 33 | 0.6 / 0 / 1 / 202 | 95.3% -> 98.0% | 0.00% |
| AMRAAM launch 5.08B | 9.3 | 98.5% | 0 / 29 | 1.0 / 0 / 3 / 226 | 77.3% -> 83.0% | 0.06% |
| AMRAAM kill 5.23B | 8.8 | 97.6% | 0 / 27 | 2.3 / 1 / 12 / 292 | 64.2% -> 60.7% | 0.91% |
| take-off roll 2.50B (night) | 22.8 | 29.9% (87 painted) | 3 / 72 | 11.6 / 5 / 32 / 1002 | - | 0.18% |

The motion is not uniform: in the kill window frames n and n + 1 are often equal and n + 2 jumps, so the midpoint of n
and n + 2 is worse than holding n (the 60.7%). The night take-off roll has almost no filled polygons (the HUD, the
moving map and outline edges carry the motion); its three cuts are frames with 0-5 polygons.

**What is held** (painted polygons not moved, 1.5% to 14% by window): a model chunk with no partner (an object entering
or leaving, a vertex count changed by more than a tenth, one drawn as a single chunk in one step and two in the next,
as the MFD's target model, 87 + 82 against 169 vertices, an explosion's chunks); a clip status that differs (an edge
crossing the window or the near plane between steps; 6-141 polygons a window); an HUD group that changes size; every
`T`, `S`, `W`, `C`, `H` and `D` record. The strips in `D:/f117-gate/p3-stage3/png` show numbers and text changing at
the middle, target boxes jumping and the impact window's terrain silhouette detail changing at the middle while its
position moves smoothly. A smarter match would need the vertex stream rather than the batch as identity, the clipper
redone for polygons that gain or lose an edge to the near plane, and explosion and tracer sprites switched rather than
paired. (The owner has since frozen primitive pairing: [the current plan](#the-current-plan-for-60-fps-15-october-2026-owner).)

## The live path, first part: feed and replay (9 October 2026)

- **The feed** (`src/present/drawfeed.c`) is an observer that keeps every record in memory, a logic frame at a time: a
  frame closes at the first phase after one that presented (entry 44), as `interp_frame.py` groups them, and waits in
  a ring of 8 until the host takes it. At the close it copies A000 into the frame, which is what the replay must equal.
  Two observer fields serve it (`observe.h`): `sources` has the records carry a blit's and a page copy's source bytes,
  as `F117R_OBSERVE_PAGES` gives a log, and `want_pages` asks for the page dumps at one phase, which the feed does once
  to seed the replay (and again after a frame differs or one is dropped). The feed takes no page snapshots: every
  drawing entry has a rule, and the state-only entries' byte changes are the model fill's own rows. Were one to draw,
  the frame would differ from A000 at its close and the replay would be seeded again.
- **The replay** (`drawlive`, same file) runs `drawlist.c` on each closed frame, carried from the one seed, and
  compares the display with the frame's copy of A000. A frame that differs is not shown and the replay is seeded again.
  Strike, the whole flight (2.33B to 8.85B): 9,845 of 9,845 logic frames equal the display at their close, none
  dropped; `boot_to_flight` 368 of 368.
- **Unchanged machine.** `boot_to_flight` and `strike`, each under both engines, with and without
  `--present replay`: the `--hash-every 50000000` checkpoints (54 and 177) and the final hashes are identical.

**Presenting from the replay** (`--present replay`, `present = replay`). In flight the screen is the replayed display
with the scanned frame's palette; elsewhere (another program, text mode, a CRTC start other than 0, or no frame closed
for a quarter second) the scanned-out picture. A frame is presented from its close, so the picture is up to one step
later than the scan shows it. `f117run` checks it at every VGA frame and prints a `[present]` summary; `--frame-log`
gains a `replay` column, and `--screen` and the shots write the presented picture. Strike, the whole flight: 68,918 VGA
frames, 50,620 presented from the replay, none unsettled; 9,843 of the 9,845 logic frames equal a scanned-out picture
at some VGA frame (the rest of the differences are scans of the next frame or of the original drawing on A000 over
several VGA frames).

**Interpolation** (`src/present/interp.c`) is `interp_frame.py` in C: the same parse, pairing and in-between list,
drawn by the C replay. `build/test_interp.exe LOG` prints `interp_frame.py LOG --pairing --check --primitives`'s report;
on the eight windows it is identical, line for line (273 frame pairs exact at both ends, 819 in-between frames, no
provenance violation). Live, `drawlive` keeps the last two replayed frames, each with the replay as it stood at its
start, and pairs them as the newer closes; `--present interp` draws the in-between frame at
t = (now - close of the newer) / (close of the newer - close of the older) on the machine's clock, so the picture is a
step behind the replay's. `f117run` also draws every pair at its two ends and compares them with the two replayed
pictures: strike, the whole flight, 9,843 of 9,843 pairs exact at both ends; `boot_to_flight` 366 of 366.

**Cost** (recompiled strike route, runner CPU time, 9 October, after entry 41 removed the page snapshots): scan 78.8 s,
replay 106.7 s (+35%), interp 121.5 s (+54%); the replay still runs at 83 M instructions a second, nine times the
original's 9 MIPS. What the feed costs is the records themselves (every vertex, edge and fill row) and the copy of A000
at each frame's close. The host's speed varied by a third between runs that day, so only figures from one set compare.

## Stage 4: pacing and the picture's age (9 October 2026)

**Pacing.** With `--present interp`, `f117a` draws the in-between frame in its main loop just before it presents (with
vsync), at the machine's clock at that moment, instead of at the emulated VGA's retrace; the machine's clock is kept
level with the wall clock, so t follows emulated time at the host display's own rate. `--present-log FILE` writes, for
every frame presented, the host clock after the present, the machine's clock, the two logic frames shown and t. Within
a step t only advances. (Headless runs through SDL's dummy driver pace in software and say nothing about a real
display; its misses at N = 4 came from the software renderer, not from drawing, which takes about 3 ms an in-between
frame at N = 4. Real-display timing is the next section.) A prediction keeps span fills on the screen (an extrapolated
span fill once walked tens of thousands of off-screen pixels: a 628 ms stall).

**Picture age** (`--present-age interp|extrapolate`, `present-age =`; `interp` by default). `interp` shows the
in-between frame of the last two logic frames: exact at every logic frame, one step (about 110 ms) behind.
`extrapolate` shows each frame from its close and moves the paired primitives on past it as they moved from the frame
before (t from 1 to 2): no age added, but the picture between closes is a prediction, and it jumps to the real frame at
the next close. Its error just before that close (`test_interp LOG --extrapolate`), beside the frame before held (what
`interp` shows then, late but real), share of pixels that differ:

| window | 3-D window: extrapolated / held | whole display: extrapolated / held | prediction nearer / as near / further |
|---|---|---|---|
| strike 8.53B | 1.86% / 1.76% | 2.94% / 2.86% | 7 / 0 / 24 |
| strike 8.70B (impact) | 26.2% / 23.4% | 16.3% / 14.7% | 4 / 0 / 23 |
| landing 9.70B | 14.5% / 10.7% | 7.8% / 5.7% | 3 / 0 / 26 |
| landing 9.89B | 0.49% / 0.45% | 0.26% / 0.24% | 7 / 5 / 19 |
| air-to-air type 6, 5.08B | 2.56% / 1.73% | 2.12% / 1.58% | 0 / 0 / 27 |
| air-to-air type 6, 5.23B | 20.1% / 17.9% | 12.2% / 10.9% | 2 / 0 / 23 |
| air-to-air type 5, 6.793B | 1.22% / 1.06% | 2.04% / 1.88% | 4 / 0 / 21 |
| take-off 2.50B | 6.50% / 6.12% | 3.52% / 3.30% | 16 / 26 / 28 |

The prediction is further from the real frame than the old frame is in most steps (191 of 265), most where the scene
turns or jumps: the original's motion between steps is not steady, and a pairing that is harmless between two real
positions is carried past them. So `interp` stays the default; `extrapolate` trades a misprediction, corrected with a
jump at each close, for the step of latency.

**Cost of the scale** (strike route, recompiled, runner CPU time; the check draws each pair twice more): scan 95.4 s,
replay 120.8 s, interp 148.1 s; interp at N = 2 253.2 s, N = 4 345.2 s, N = 9 645.1 s; replay at N = 9 166.4 s. An
in-between frame costs about 1.6 ms more at N = 2 than at N = 1, 2.9 ms at N = 4 and 7.4 ms at N = 9 (the fine pages
copied and redrawn); at N = 9 that is 5.2 million pixels through the palette and up to the GPU each frame, which is why
the presentation is threaded.

**The HUD, text and sprites at 4K (owner's choice, 10 October 2026): like the Reimp.** HUD lines and tick marks are
rasterised on the output grid; text glyphs, sprites and cockpit artwork keep their original N x N pixel blocks.
`--present-line-width N` (`present-line-width =` in f117a.ini) selects 1-20 output pixels; 0, the default, uses the
render scale; both hosts accept 0/1/20 and reject -1/21/non-numbers/trailing letters. At N = 1 the original replay is
used exactly, including its line quirks, regardless of the width setting; `--present scan` keeps the original picture
at every scale. The line rule began as the Reimp's (`src/core/hires.c`: Bresenham on the output grid, a square nib,
byte-identical on 360 strokes) and departs from it on purpose since the owner's 4K review (the current pen:
[one stroke pen](#projected-outlines-point-markers-and-one-stroke-pen-15-october-2026)). No Reimp code or asset is
required at runtime. The graphics driver's already-clipped endpoints and page origin come from the draw records.

Colour replacement, OR/AND/shaded spans and page copies operate on the fine pixels already drawn. A small bit mask
carries the cells crossed by fine strokes through copies, so the flat-cell guard preserves deliberate HUD detail,
including wide strokes. Overlapping self-blits snapshot their fine source before the coarse writes mirror over it.
Bitmap text and sprite painting keep the original per-pixel rules. `presentation_hud_lines` tests the coarse identity,
octants, reversal, dots, width, origins, guard, colour operations, artwork and copies. `test_hires --line-width N
--dump PREFIX` renders the last checked frame as coarse and fine PPMs using the recorded palette.

## Fullscreen 4K/60 timing and the threaded presentation (13 October 2026)

**Where the time went.** A single-threaded N=9 strike run (fullscreen Direct3D 11, 3840x2160, 59.99 Hz, vsync) kept
every logic frame exact and the guest hash unchanged, but had p99 24.30 ms and a worst interval of 30.95 ms (the
checker rejects p99 above 20.833 ms). Every frame in which a logic step closed took 21-25 ms, because the replay of
that frame at N=9 (6.4 ms) and its copies ran in that one host frame; even without those spikes the work outside the
vsync wait had a median of 14.8 ms of the 16.67 ms budget (the in-between picture 7.6 ms, palette conversion and
texture upload about 3 ms, the replay 2.3 ms, the machine 1.7 ms). Replaying records as they arrive took p99 to
22.6 ms; the rest had to leave the main thread.

**The threaded presentation** (`--present interp`; `src/present/drawfeed.c`, `src/host/main.c`). The machine, the
observer's feed and the replay stay on the main thread; the in-between picture is drawn one host frame ahead on a
worker.

- `drawlive_collect` replays the records of the open frame as they arrive (after every 1 ms machine slice) and, when a
  frame closes, stages it: the replay's start, the picture, its parsed records and its pairing with the step before it
  go into a free step from a pool of five. `drawlive_publish` then makes the staged steps the two published ones, in
  order, which is only pointer changes. `drawlive_update` is both, so the headless checks are unchanged.
- `drawlive_plan_interp` decides what to show (the older, the newer, the replay, or an in-between picture at t) from
  the published steps and the machine; `drawlive_render` draws it. The worker only reads published steps; the main
  thread publishes new ones only while the worker is idle (after joining it, before the next plan), and a collect that
  finds no free step waits for a publish.
- Palette conversion of the 5.2 million fine pixels runs on three helper threads beside the main thread; the texture
  upload stays on the main thread.
- File-scope scratch that both threads reach (`row_at`, the polygon and span tables in `hires.c` and `interp.c`, the
  seed buffer) is thread-local (`tls.h`).
- Cost and meaning: the picture shown was planned a frame earlier, for the machine clock at that moment, so
  interpolation shows one host frame (16.7 ms) older than before, on top of the step it already shows behind.
  Extrapolation is a prediction anyway and plans a frame ahead. `F117_PRESENT_THREADS_OFF=1` runs everything on the
  main thread for comparison.

**Results** (`tools/presentation_timing.py`, isolated, one run at a time, fresh save, dummy audio, Direct3D 11
3840x2160 at 59.990002 Hz, vsync on, N=9):

| scene | flight pictures | interval median / p99 / max (ms) | over 25 ms | logic frames exact | final guest hash |
|---|---|---|---|---|---|
| strike to 3.5B | 7,719 | 16.6677 / 16.7092 / 17.063 | 0 | 2,062 of 2,062 | `bc8e3ae5a872405d` |
| air-to-air type 6 to 5.9B | 19,000 | 16.6675 / 16.7091 / 21.3108 | 0 | 3,679 of 3,679 | `b050542466c9a616` |
| landing to 10.0B | 50,440 | 16.6677 / 16.7090 / 17.2537 | 0 | 13,408 of 13,408 | `efe080faa364f8f4` |

All pass the checker (median within 10% of the refresh period, p99 within 125%, no interval above 150% of the period,
guest clock within 100 ms of the host; drift +18.8, +6.2 and +2.8 ms). In-between pictures are 96% of the pictures
shown. Each final hash equals a fresh headless run of the same input and stop count. The latest isolated N=9 strike
run (after the state-driven bands, 15 October) passes too: 7,718 flight pictures, median 16.6679 ms, p99 16.7314,
worst 17.0385, no missed intervals, 11.6 ms guest drift, 2,062 exact logic frames, `bc8e3ae5a872405d`. The strike
flight also passes pinned to four logical CPUs (p99 16.71 ms, worst 16.82 ms, same hash), so the thread design has
headroom on core count.

The batch that completed the HUD (15 October) has **no fullscreen acceptance**: the owner stopped the visible timing
run. A hidden Direct3D 11 3840x2160 target measures 119.47, 127.45 and 150.10 fps over strike, mode-2 and small-HUD
windows, with regeneration/raster p99 below 5 ms but occasional upload stalls of about 100 ms. That is uncapped
throughput, not steady-refresh acceptance. Evidence: `D:/f117-gate/present-v2/hud2`. **Owner rule:** checks run in the
background; no visible or fullscreen test without asking first.

`tools/presentation_timing.py` checks the logged display settings (a log that changes display mid-run is rejected as
ambiguous), intervals, interpolation and guest drift; malformed or incomplete captures fail. Not established: the
picture on a physical panel (tearing, the look of motion at 60 and 144 Hz), the cargo and career flights' timing, and
a slower CPU or weaker GPU than this machine's. The owner's motion review remains Phase 4 work.

## The state-driven HUD (14 October 2026)

At N > 1 the HUD is drawn from the game's state at fine-pixel precision, and the state is interpolated between logic
steps. The original rounds every HUD item to whole cells before it calls the library (the ladder's rungs by up to 5
cells), so a picture built from its calls jitters at 4K however it is interpolated. N = 1 is unchanged: the original's
calls are replayed. The analysis is `D:/f117-gate/present-v2/hud-analysis.md` (private; the formulas are in
`src/present/hud_state.c`, ported from its `hudmodel.py` and `closedform.py`). `F117_PRESENT_HUD=old` turns it off.

**The state.** VGAME's HUD pass (1058:0016) is a pure function of its data segment at entry. The observer's record `h`
(`observe_hud_pass`, called by the matched pass at its entry; only with `f117_observer.hud`, which the live feed sets
and `F117R_OBSERVE_HUD=1` sets for `--observe`) copies the 1,560 words 0x2080-0x2CAE (tables, layout constants, text
blocks), 19 scattered words (heading, pitch, roll, altitude, raw and whole-knot speed, the ladder flag, the G text) and
the graphics driver's tick tables (the x column and row limits of library entry 11, not in the data segment): 1,593
values, `hud_state.h` has the layout. Record `p` (`observe_panel_pass`, at the entry of the matched panel pass
0000:79BE, which runs before the HUD pass) holds the words the flight-path marker and the two bars are made from:
`[98F6]`, `[E586]`, `[E474]`, `[B07E]`, `[B194]` and the HUD mode and size flags. Both read guest memory directly and
change nothing: with and without them the route's final guest hash is the same and the `--observe` log, minus the new
records, is byte-identical (fly3 route, 2.79-2.81B, 88,585 records, 57,589,444 bytes; final hash `3a1030e87d4f1807`).

**Two modes** (`hud_state.c`). EXACT is the original's integer pipeline (the clipper `0FB2:0330` included); it gives
the library calls of the pass in order (K colour, N line, T text with its eleven block words, H tick scale, the two
entry-18 blits). FINE carries the same formulas in floating point from the continuous state, with no final rounding:
airspeed from `[B07E] / 27` (the raw word is finer than the HUD's knot), altitude, and heading, pitch and roll as
fractional angle words. Lines are strokes between vertices at cell centres, bitmap text is placed by its top-left
corner (the convention that keeps the original's alignment between tick and glyph centre), and clips are the
original's, at cell edges.

**Drawing** (`hud_overlay.c`, entered from `drawlist_record` through one hook). At an `h` record the replay draws the
FINE primitives on the work page's fine picture: strokes in the shared pen at the HUD line width; glyphs from the font
the `T` records carried (kept per font index), each source pixel an N x N block at the glyph's fine position. A cover
(`hud_cover.c`) then follows the records of the pass that arrive and recognises each of the EXACT list's in order, by
kind and every value. The ones FINE replaces still run through the coarse replay (so the coarse picture, the colour and
text state and the exactness check against the display are the original's) with every page's fine mirror off. A
record that leaves the prediction ends the cover, and the rest of that pass keeps the original path; a pass with no
tick table, font or page yet stays on the original path. The `[present] state-driven HUD ...` line of `f117run` counts
each case. The panel pass's items need no list: after a `p`, a record is taken for the flight-path marker (entry 71,
11 x 7 from (55, 77) to x = 154, y = 48 + `[98F6]` >> 6) or a bar (library line at x = 247 from row 52, or at x = 72 down
to row 81) when its geometry and value are what the integer formulas give for that state; the marker is then drawn
from the record's own art as N x N blocks at y = 48 + `[98F6]` / 64, and a bar as a stroke in the library colour of the
moment, its free end at 52 - `[E586]` / 16 (vertical speed, rows 20 to 81) or 81 - ((command speed - speed) * 2 / 5 + 29)
(stall margin, the speed being `[B07E]` / 27). The ink is tagged 240/241 in the diagnostic plane (`hud_overlay.h`;
`tools/present_diag.py` does not class them).

**Interpolation.** `INTERP_HUD` (set with a scale above 1): the HUD lines the state draws are left out of the HUD-line
pairing, and the skeleton's `h` record is regenerated with speed, altitude, heading, pitch and roll moved the same
fraction (angles by the shortest arc, `hud_blend`; a hair from a logic frame it is the frame's own record, so the
endpoints stay exact). Layout, mode, the readout flag, colours and the ladder switch are the skeleton's, as every other
discrete item (switching at t = 0.5); if two frames disagree on them the skeleton's state is used unmoved. Fixed-point
override values: 1/1024 kt, 1/256 ft, 1/256 of an angle word.

**What is covered.** State-driven at N > 1: airspeed and altitude labels (both altitude scales, full and small HUD) and
their tick marks; heading labels and the heading tick strip (modes 0 and 1); pitch ladder rungs (all shapes, rotated by
roll, clipped to the ladder window) and rung labels; flight-path marker (sub-cell height), vertical-speed bar,
stall-margin bar; and since 15 October the frame, reticle, G readout and the panel pass's station/gun text, digital
readouts, cue, outlines and messages ([below](#the-remaining-hud-and-original-map-marker-movement-15-october-2026)).
The 14 October list of items left on the original's records also named the tracking cross, ILS, sink marker and weapon
and target symbology; the 15 October follow-up does not name them. Cockpit art (canopy posts, attitude ball) and all
bitmap fonts and sprites stay the original art.

**Checks** (`tests/test_hud_state.c`, `tests/test_hud_live.c`, `tests/test_hud_jitter.c`; corpus and windows private
under `D:/f117-gate/present-v2/`).
- EXACT against the original's log, seven scripted flights (full HUD climbing, on the runway, small HUD, steep pitch,
  48 degrees of bank, mode 1 inverted, mode 2 diving), 288 passes: 12,921 library calls, 0 mismatches (K 1,111, N
  4,640, T 6,217, H 418, entry-18 blits 535).
- FINE where it must agree with the original's integers: 1,447 tape labels equal to the float's floor (614 more are
  outside the row clip); 418 tick lattices identical to the driver's marks; 849 heading labels with integer minus float
  in [0, 2) (139 clipped); 6,836 ladder vertices with integer minus floor of float in x [-3, 5], y [-3, 1] (one pass at
  a rung-numbering boundary left out); the ladder's label-offset tables equal to their analytic form; the tick scale's
  colour equals the tape text's in all 418 H records. The ladder sits up to 3 cells higher in the original than in the
  float: its pitch is cut to a quarter degree and floored twice.
- Panel items: 504 of 504 records of the marker's and the bars' geometry are recognised by the integer formulas
  (marker 258, vertical-speed bar 179, stall bar 67); integer minus float: marker top [-0.97, 0.00] cells (a floor),
  vertical-speed bar end [0.00, 0.94], stall bar top [-1.17, 0.79].
- Live path (strike 8.53B and four fly3/fly1 windows, N = 4): every logic frame and every pair exact at both ends,
  coarse and fine, 0 records off the prediction, 0 replaced records that changed the fine picture (`--leak-check`). The
  machine live (`run_route.py fly3.args -- --steps 2810000000 --present interp --present-scale 4`, headless): 789 HUD
  passes all drawn from the state, 789 frames / 787 pairs exact, final guest hash `3a1030e87d4f1807`, the same as
  without presentation. Cost at N = 9: the same as without (3.2 s against 3.4 s for a 37-frame window).

**Motion** (`test_hud_jitter`): the same pair of frames interpolated as before (integer HUD lines, text and ticks held)
and with the state, sampled four times per logic step; the second difference of a feature's position, in fine pixels
at N = 4 (a smooth path has second differences near 0; 4 is one cell):

| window | feature | before: rms, max | after: rms, max |
|---|---|---|---|
| fly1, climb 2.86B (72 pairs) | airspeed labels, y | 1.99, 4.00 | 0.01, 0.07 |
| | altitude labels, y | 2.13, 4.00 | 0.01, 0.10 |
| | heading labels, x | 0.67, 4.00 | 0.03, 0.48 |
| | ladder labels, x / y | 1.05, 8.00 / 3.78, 28.00 | 0.00, 0.02 / 0.01, 0.05 |
| | ladder endpoint, x / y | 0.37, 4.00 / 0.74, 8.00 | 0.11, 1.65 / 0.38, 5.86 |
| | flight-path marker, y | 4.32, 40.00 | 1.10, 10.22 |
| | vertical-speed bar end, y | 1.42, 4.00 | 0.19, 1.88 |
| fly3 f3b, inverted bank, mode 1 (35 pairs) | heading labels, x | 1.72, 4.00 | 0.06, 0.66 |
| | flight-path marker, y | 3.60, 28.00 | 0.80, 6.53 |
| | ladder labels, x / y | 4.65, 16.00 / 4.58, 12.00 | 0.35, 2.12 / 0.49, 3.30 |
| | ladder endpoint, x / y | 2.18, 8.00 / 2.18, 8.00 | 0.22, 2.14 / 0.15, 1.72 |
| fly3 f3a, steady 48 degree bank (35 pairs) | heading labels, x | 1.33, 4.00 | 0.00, 0.01 |
| | altitude labels, y | 0.68, 4.00 | 0.00, 0.00 |

The residue is the flight state itself: the original repeats a state in consecutive logic frames and the next frame
jumps (the 5.86 is one such repeat; the marker's 10.22 was not examined), which interpolation between real frames
cannot hide. Pictures at N = 4: `D:/f117-gate/present-v2/hud/img`.

**Not established.** The colour-display HUD (`[43DE]` set: tape and text colour 10, ladder 47) was not in any capture;
the fine strokes take their colour from the state and the tape's text block, and the original's art may differ.
Altitude above 10,000 feet reads the label table past what the 14 October captures reached (the 15 October captures
add three altitude bands, ladder-off passes and steep-pitch rungs, below). Nothing here has been seen on a physical
display.

## The remaining HUD and original map-marker movement (15 October 2026)

The state-driven HUD also covers its frame, reticle and G readout, plus the panel pass's station/gun text, digital
readouts, cue, outlines and messages. Bitmap fonts and sprites are the original art. The map aircraft marker remains
the original 4x4 bitmap selected from eight headings; only its position moves on the fine grid. Its continuous
position comes from the original 32-bit aircraft coordinates, with the map's zoom, origin and 16-bit subtraction wrap
preserved. No replacement graphics are introduced.

Continuous values interpolate; numbers, text, switches and the selected arrow bitmap stay with the older logic frame
until the newer frame arrives. The original calls of the selected frame still update the coarse replay. Extended panel
records preserve the earlier eight-word format, and missing fonts or sprite art keep the original path until
available. Horizon interpolation and discrete HUD holding use separate option bits.

**Evidence.** Fresh captures cover all six full/small HUD modes, three altitude bands, ladder-off passes and
steep-pitch rungs. The exact models match 29,594 HUD calls over 711 passes, 11,490 panel calls over 710 passes, and all
714 map-marker blits. The corpus checker rejects an empty capture (seven files in a stopped draft had contained no
records). ROM-free tests check all eight synthetic arrow bitmaps pixel for pixel, transparency, coarse replay, fine
movement, heading timing and wrapped map origins (the pre-fix map-origin model fails that regression). Observer tests
verify every sprite byte and unchanged guest memory, registers and clocks under both timing profiles. 34 live checks
over 17 fresh windows pass; the owner's complete N=9 replay keeps 69 checkpoints and final hash `31c259b992340414`, with
2,101 exact logic frames / 2,099 exact pairs and no drops or prediction mismatches.

N=9 fly1: the map arrow's horizontal second-difference RMS falls 0.75 -> 0.00 fine pixels (286 samples), and the
vertical-speed bar 3.19 -> 0.43. Small-HUD loop: the arrow's vertical RMS falls 0.84 -> 0.00 (458 samples). The full
owner replay confirms the map's terrain is refined geometry; a short window that begins after the map was cached can
instead show its original bitmap, so that window alone cannot prove the live map is blocky. Evidence:
`D:/f117-gate/present-v2/hud2`.

## The state-driven sky and ground bands (15 October 2026)

The high-detail background follows the same approach as the HUD: read the pass's original state, predict its integer
calls exactly, then draw the same quadrilaterals with their rounding removed. It keeps the original 16 sky and 16
ground colours and the original graphics. N=1 stays on the captured calls. Low detail keeps its original path; wrapped
corners and unusable geometry keep a conservative fallback. `F117_PRESENT_BANDS=old` restores the previous band
renderer (with its original integer-corner interpolation).

The observer emits `o` once at VGAME `1450`: height, pitch/roll sine-cosine pairs, the offsets, view centre, window,
flags and shifts. The EXACT model follows the original signed arithmetic, rounded multiplies, divides and wraps. The
FINE model uses the same formulas without rounding. Interpolation moves height linearly and angles along their
shortest arc; view, detail, colours and other discrete settings stay on the selected logic frame. Band `U` records stay
exact while the state moves. A band with a wrapped corner keeps that record too, so it cannot break recognition of
later bands.

**Evidence.** All 1,514,560 original quadrilaterals match over 47,516 captured passes, including 186 low-detail and
1,609 flat passes; no mismatched or truncated passes. On 47,313 same-view pairs, endpoint and midpoint checks pass; six
discrete-state changes decline. The recorded owner's flight keeps all 69 guest checkpoints and final hash
`31c259b992340414`; its live replay checks 2,101 logic frames / 2,099 pairs exactly, and the retained window another 19
frames / 17 pairs. A ROM-free observer test checks identical records and no clock, VGA bus or guest memory changes under
both timing profiles, including wrapped stack/view offsets. 10 band-live checks pass. Evidence:
`D:/f117-gate/present-v2/band-finish`.

At N=9, in the owner's window, horizon-height second differences at columns 40/160 fall from RMS 3.27/3.29 to
0.44/0.35 fine pixels; all band edges together fall from 3.65 to 0.41 (4,174 samples). Strike: all band edges 3.67 to
1.26 (61,216); its visible horizon columns 160/280 fall from 3.56/3.65 to 1.00/1.34. These include logic-step changes
and camera acceleration, not just rounding within one pair. A canonical sea/land-boundary probe in the owner's window
is unchanged (0.642 fine-pixel RMS, maximum 2): coastline topology transitions remain open.

**Capture replay rule.** Post-call `x` snapshots for copies/fills are measurements of the original picture; applied to
a regenerated picture they painted coarse ghosts at the original edge positions. Interpolation omits snapshots for
known entries 42/44/46; the C/D and model records draw their result. Raw frames still apply genuine changed-byte
fallbacks, and duplicate presents preserve carried fine pixels. A synthetic moving-dot regression fails without this
rule; the wrapped-band and old-switch regressions reject the earlier drafts too. At the sampled horizon, 2,592 fine
pixels of coarse snapshot residue disappear. The old mode matches the prior renderer byte for byte when redundant
diagnostic snapshots are removed from its input.

## Consolidate world geometry before more clipping exceptions (15 October 2026)

*Superseded as the main path later on 15 October by [the current plan](#the-current-plan-for-60-fps-15-october-2026-owner)
(re-run the original's draw). The findings and the integrated scene-input capture below stay valid; the own scene
renderer (`src/present/scene_mesh.c`: mesh/pose/visibility/order/material evaluation with a synthetic CTest) is an
unconnected draft, not a finished fix.*

The owner's concern about spot treatments set the rule: no more one-off clipping exceptions (for coastlines or
anything else). The visible-edge capture already contains raw camera coordinates (`G`); precision alone is not the
missing input. It lacks a complete model before visibility filtering and persistent instance/face identities, so
pairing clipped command lists can hold or switch geometry when an edge enters the viewport, a face disappears or a
model changes its submitted vertices (in the owner's retained window, 135 held clipping changes and 27 unpaired
polygons over 51 intermediate frames). A private clip-only prototype passes an analytic moving-triangle regression and
112 selected tests but is set aside and not integrated: it cannot supply absent faces or stable model identity. Its
test can be adapted to a broader path. Evidence and prototype: `D:/f117-gate/present-v2/coast-finish`.

**Capture boundary found in the matched original.** VGAME `120A:00CF` takes the model index, position and orientation,
selects its original detail level, sets up parts (`0317`) and planes (`0561`), then submits vertices (`06B5`), edges
(`0A33`) and faces (`0C76`). Visibility already filters the later inputs: `06B5` skips vertices whose `7A02` byte is
zero, and `0CA8` skips hidden faces. So adding precision only to `G`/`E` cannot recover the whole model; a complete
capture starts at the model request/selected-data boundary, before those filters, with the camera and original model
inputs. A reused projected vertex address or draw order is not a persistent object ID.

The shared-scene plan was: record complete model inputs and stable instance identity per view; decode the original
model stream once and compare it against the original's submitted vertices, edges, faces, colours and styles; then
interpolate camera and model poses by identity and project/clip all world geometry (terrain, roads, outlines, dots) at
the displayed resolution, keeping the original's detail switches, text, sprites, explosions and artwork on their
logic-frame policy; verified with analytic geometry and real captures, all N=1 routes and guest hashes, endpoint checks
and visible coastline motion. Legacy capture replay remains the original-resolution oracle and the fallback for data
not covered.

### Complete model-input capture

`F117R_OBSERVE_SCENE=N` enables a bounded research capture with the headless runner's `--observe` clock window. Keep
both `i` (request) and `j` (memory) records when filtering kinds. Each request records the original model,
position/orientation arguments, camera/view metadata, CPU registers, complete DS and model-bank segments, and 256 stack
bytes starting at SP. Reads bypass device accessors so capturing VGA-backed addresses cannot charge guest bus cycles.
The app does not enable this capture. Original bytes stay private; this is a research boundary, not a per-frame scene
representation or a persistent object identity.

`py tools/scene_capture.py LOG --out JSON` reconstructs the inputs and rejects missing/duplicate bytes, mismatched
segments/clocks, out-of-range words and metadata inconsistent with the captured memory; empty captures fail. ROM-free
tests check logical/physical address wrapping, every captured byte and unchanged guest memory/registers/clocks under
both timing profiles; parser regressions check completeness and reject malformed captures.

Two paired owner replays (interpreter against recompiled) capture 192 requests in all: 64 over the complete replay
(23 model indices, perspective and orthographic-map views; 8,404,992 input bytes) and 128 in flight after clock 2.5B
(16,809,984 bytes). All input bytes, request metadata and clocks agree, and both engines keep the baseline's 69
checkpoints and final `31c259b992340414`. This proves inputs for requests the game submits, not every world candidate
or stable scene identity. Evidence: `D:/f117-gate/present-v2/scene/pair-verdict.json`,
`flight/pair-verdict.json`, `flight/layout-verdict.json`, `foundation-verdict.json`.

`tools/scene_model.py` decodes original parts, planes, vertex lists, ordering trees, vertices, edges and material
commands, and derives original detail selection from request position and camera state: all 192 selections agree with
original-byte execution. All 191 requests that reach geometry agree at the part/plane/vertex/edge/face stream
boundaries and counts (the other returns at the original reach test). From original prepared part poses, camera
geometry agrees on 2,330 submitted vertices, including articulated transforms; face tests agree on 488 planes, 349
visible shades and their vertex flags. The decoder keeps the 108 hidden vertices the original omits. This is not
independent pose preparation or rendered material/style parity.

Controlled original-byte fixtures exercise all 205 reachable detail variants in the two banks, including six
articulated and nine zero-vertex variants: every stream boundary/count, 1,722 plane tests/shades and 3,870 transformed
vertices agree; 32 unreferenced vertices remain available. The fixture deliberately selects a level and enables the
original map projection, so it is format/geometry proof, not a real-route result (`controlled-layout-verdict.json` and
the two `original-stream-boundaries.json` files; harnesses and assets private).

Eleven ROM-free model tests cover detail thresholds and signed depth, articulated and zero-vertex topology,
positive/shared vertices, the cache's padded 16-byte stride, part switches without planes, signed/wrapped camera sums,
conditional plane visibility/shading, logical address wrapping and malformed material/LOD streams. Shared vertices
are synthetic coverage only: none occurs in the captured banks. Unproven extended ordering-tree leaves fail
explicitly. Material commands are decoded but not rendered.

### Unlocked rendering and control latency

Gameplay/logic parity remains the first requirement. Rendering may run independently, but must not advance the
original simulation faster, retime its input reads or feed predicted poses back into gameplay. Resolution alone
does not require interpolation delay. Latest-state presentation can avoid that delay while keeping the original
motion steps; smooth presentation without waiting requires visual prediction, whose errors need measurement.
Exact interpolation requires known states and carries its documented picture age; the existing extrapolation's
measured errors are not a solved latency result. No default or owner configuration is changed by this work.

## Identity pairing and the step clock (15 October 2026)

The owner's recorded flight (`session-20261015-103956`, frozen privately with its draw capture in
`D:/f117-gate/present-v2/owner-roads-20261015`) still showed road edges that pop and building patches that jump.
`tools/motion_check.py LOG` measures what the exactness checks cannot see: the share of polygons and outline edges
with no partner in the next logic step (they switch at t = 0.5 instead of moving), how much paired batches deform
between steps, held steps, and how unevenly the steps are timed.

**Identity pairing (an experiment, off; it made motion worse).** With `F117R_OBSERVE_IDENT=1` (and in the live
presentation when `F117_PRESENT_IDENT=1`) the observer logs a compact record `I` for every model request at
`120A:00CF` (`scene_capture.h`): the model, the caller chain, the scene walk's ring (`DS:E328`) and world-cell words. A
batch's key is its model, ring and cell; batches whose keys and vertex counts are equal and unique in both frames pair
first, whatever their order or motion (`interp.c` `ident_key`; mirrored in `interp_frame.py`), and the existing
order/motion pairing handles the rest. In a 100M-clock window of the flight the key agreed with every existing pair (0
contradictions in 728) and matched 334 batches the old pairing missed. Same window, without / with identity: polygons
unpaired 7.78% / 3.98%, outline edges unpaired 2.51% / 0.78%, held steps 7 / 4. Raising the 60 px motion limit instead
recovered under half as much and risks wrong pairs. A replay of 2.70B-2.72B holds 8,368 unpaired polygons instead of
11,490, with all 1,497 pairs exact at both ends and the guest hash unchanged. Not covered: two of the same model in one
cell (needs the entry ordinal), detail changes (another mesh) and batches with no request (map, HUD).

The owner saw more jitter with it, and `motion_check.py`'s deformation measure shows why: the extra pairs join the
same object's batches when they hold different visible vertices (the original drops hidden vertices before drawing),
so the object deforms between the steps instead of moving. Of the 316 pairs only identity made, 295 deformed by over
100 px (median 743 px; agreed pairs: median 2 px); paired batches deforming over 100 px rose from 3.78% to 20.53%.
**Lesson:** judge pairing by deformation, not by unpaired counts. A usable identity needs per-vertex identity (which
model vertex each projected one is).

**The step clock.** The game moves the aircraft a nearly fixed distance per logic step (mostly 46 units in the flight)
while a step takes 42-86 ms by scene cost; 24% of step boundaries change duration by more than 20%. The in-between
frame's t is the time since the newest step closed over the previous step's duration (`drawlive_plan_interp`), so a
longer next step freezes the picture at t = 1 and a shorter one skips forward when it arrives. Simulated over the
flight's real step times at 60 Hz (1,186 frames): the current clock gives 29 freezes, 6 skips and 21.4% of frames
whose speed changes by more than 20% (p99 429%); a playback clock advanced by host time over a smoothed step duration,
held about one step behind the newest and never past it, gives 2 freezes, 0 skips and 1.6% (p99 41%), for about 0.3
step (17 ms) more picture age. Not yet implemented: it needs one more step and its pairing kept alive in the threaded
pipeline; it will be switchable (the owner has not set defaults) and judged by `motion_check.py`. It is part of the
current plan.

**World extent.** The original's resident world is four rings of 32 tile slots (`DS:E330`, counts at `DS:061E`; 18-25
populated in the capture), so a longer view would need our own world loader; the owner chose not to extend the draw
distance.

## Verification

A presentation change is held to these checks (commands and costs: [repeated-processes.md](repeated-processes.md);
the latest full-gate tally: the handoff):

- **The full gate** for anything that can reach the machine (every CTest, all routes, both instruction profiles,
  matched seeds, reachability/event checks). A presentation-only change may use `tools/present_light_check.py --base
  REV` instead: every CTest, the 45-route N=1 presentation check and boot at N=4/9; it refuses if any changed file is
  not presentation-only.
- **`tools/presentation_check.py`** compares complete presentation routes with the full gate's saved interpreter scan
  baselines: every guest hash, route milestone, closed logic frame and interpolation endpoint; it rejects
  dropped/unseeded frames and requires positive pairs on flight routes. Current: all 45 N=1 routes, 259,240 closed
  logic frames and 259,166 interpolation pairs exact, no dropped or unseeded frames.
- **Live boot sentinels** (`boot_to_flight` at N=4 and N=9): 368 exact logic frames and 366 exact endpoint pairs each,
  54 checkpoints and final hash `48a10e901505e2a9`, equal to the run without presentation.
- **Retained windows:** 96 replay/fine/interpolation checks over sixteen windows (the eight `logs41` windows, the map
  and span captures and current-flight windows): `test_drawlist` carried and not, `test_hires` at N=1/4/9 and
  `test_interp --fine` at 4/9.
- **The owner's flight** (`session-20261015-103956` replay at N=9): 69 checkpoints, final `31c259b992340414`, 2,101
  exact logic frames / 2,099 exact pairs.
- **Motion:** `tools/motion_check.py` (pairing, deformation, step timing), `test_world_jitter`, `test_hud_jitter`.
  **Timing:** `tools/presentation_timing.py`, isolated and, for fullscreen, only with the owner's approval.

These are correctness checks; they do not establish physical refresh or the look of motion.

## Open questions and risks

- **In-between pictures.** Batch-alignment pairing covers 86-99% of painted polygons in the study windows, but
  unpaired shapes switch and wrongly paired ones deform; primitive pairing is frozen and the in-betweens are to come
  from re-running the original's draw ([plan](#the-current-plan-for-60-fps-15-october-2026-owner)), whose isolation
  and cost are not yet measured.
- **Things that are not smooth** (explosions, tracers, discrete HUD readouts, a cut between views) must be left alone:
  hold the previous frame or switch on the logic frame.
- **The wall.** If the original's rasteriser depends on state not in the draw list (a clip window held in a register,
  a dither phase), the replay misses pixels; that is a finding that says what else to record, not a failure of method.
- **Uneven step timing** freezes and skips the picture until the smoothed playback clock is built.
- **Remaining geometry fallbacks.** One extreme original clipping-arithmetic artifact (air-to-air), polygons whose
  geometry does not explain their footprint, zero-area span polygons and missing metadata stay scaled. The clipper
  tolerances are measured bounds, not derived limits. Coastline topology transitions and per-role stroke widths are
  open.
- **Real display.** Refresh pacing on a physical panel, the look of motion at 60 and 144 Hz, fullscreen acceptance of
  the latest HUD batch, cargo/career flights and slower machines all remain owner (Phase 4) checks.
