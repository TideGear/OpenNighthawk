# 60+ fps and 4K: design and status

Roadmap Phase 3 asks for 60+ frames a second and 4K output; the owner's target is **4K at 60 Hz** (a 16.67 ms
host-frame budget) and, for the method, any scale and any refresh rate, lower or higher. This page describes the
optional presentation modes, the rules each part follows, the decisions that constrain them and the measurements
behind them. None of it can change the emulated machine. Commands, costs and retained evidence of the checks are in
[repeated-processes.md](repeated-processes.md); the latest gate tallies are in the handoff.

## In short

| Mode | What you see | Frame rate | Status |
|---|---|---|---|
| `--present scan` (default) | exactly what the emulated VGA scanned out, scaled up whole (blocky at 4K) | a new picture when the game draws one | the original |
| `--present replay` | the same picture rebuilt from the original's own drawing commands; with `--present-scale N` the 3-D scene, HUD and sky/ground are redrawn N times finer | a new picture when the game draws one | verified: the "sharp mode" |
| `--present interp` | adds pictures between the game's steps by moving the shapes of two consecutive steps | a new picture every display refresh | experimental: shapes it cannot match switch halfway |
| `--present rerun` | adds pictures between the game's steps drawn by the original's own drawing code, run on a throwaway copy of the machine | a new picture every display refresh | experimental: integrated and gated; the owner accepted its motion at 4K "for now" (15 Oct) |

- **Frame rate.** The app presents one picture per display refresh (vsync on), so the rate is whatever refresh the
  display is set to: 60 Hz gives 60 pictures a second, 144 Hz gives 144, if the PC keeps up. The game itself still
  steps fifteen times a second normally and thirty during 2x compression with the default D1REAL fix (18 October); `interp` and `rerun` fill the gaps.
  With `--fix none`, the original controller gives about 17 steps a second at the historical 20 MIPS setting,
  fewer in slow scenes. The presentation modes do not require D1REAL.
- **The price of in-between pictures** is age: they show the game one logic step late (about 60 ms at 17 steps a
  second, or about 67 ms at the default fifteen), plus one display frame for the threaded drawing,
  plus about a third of a step for `rerun`'s smoothed clock.
- **What never changes.** Original text, sprites and cockpit artwork keep their pixels (each becomes an N x N block);
  nothing is replaced with new graphics. The finer modes keep the original renderer's quirks: road and coastline
  edges jitter as they do in the original, objects can overlap in the original's drawing order, and lines and dots
  keep one thickness at any distance.
- **4K.** `--present-scale 9` gives a 2880 x 1800 picture that, shown 4:3, fills 2,160 lines. The preset `f117a.4k.ini`
  selects fullscreen, `present = interp` and scale 9.

## The constraint that matters: the presentation must not touch the machine

Phase 1's proof is that the recompiled game is the original, instruction for instruction. A presentation path that
changes a single guest write or clock tick breaks that, and the routes would say so. So every part is an
**observer**: it reads what the machine already does and draws something from it. It never writes guest memory,
never changes the instruction count and never feeds back into the machine. The parity routes and `func_lockstep`
keep their meaning, and a presentation bug can only draw the wrong picture. The `rerun` mode runs code, but only on
a copy, and is checked for exactly that ([below](#the-rerun-mode-in-between-pictures-drawn-by-the-original)).

## What is known

- **The original runs at a low, speed-dependent rate.** The flight model steps once per drawn frame and divides
  every per-second rate by S = `[0x368E]`, which the frame-rate controller clamps to at most 15
  ([bugs.md](bugs.md) D1). Pacing varies with scene and flight state: the nine-route study averages 16.27 drawn
  frames a second at GOG's 9 MIPS and 16.75 at the former 20 MIPS default; a parked jet draws 11.6
  ([speed-sweep.md](speed-sweep.md)). 60 fps cannot come from the original drawing faster; it must come from
  pictures *between its states*, about 4 to 7 per step at 60 Hz. Raising S is not an option: the flight model is 16-bit
  integer and rates smaller than S round to zero.
- **The host shows the last frame the VGA scanned out** and scales it with SDL (`--scale N`, `--fullscreen`, aspect
  correction unless `--no-aspect`). A 4K display already works as a scaled 320x200 picture; every scene edge stays
  on the 320x200 grid.
- **Presentation uses the machine clock.** The host keeps emulated time level with real time; the original's mission
  clock runs faster than real time at every speed measured (about 1.12 times at the former 20 MIPS default;
  [speed-sweep.md](speed-sweep.md)). In-between pictures follow the machine's frame timestamps without changing that.
- **A logic frame closes at the drawing phase that presents** (graphics entry 44): one `game_draw` (`0x01450`)
  phase a step, or two with a sensor display up (the main scene in the 64 KB page at origin 0, then the inset scene,
  HUD text and present at another origin).
- **The frame has two rasterisers.** VGAME's own model rasteriser (transform `0x128B5`, projection `0x129A2`, edge
  and span tables, the row loops at `0x1418C` and `0x1420C`) draws the 3-D scene; the resident graphics library in
  segment `0889`, reached through the `1E42:0188...` jump slots (each `JMP FAR 0:0` in the file, patched at load),
  draws sprites, text, lines, span fills and the HUD. About a fifth of a flight's instructions run in the library.
- **The scene is flattened to integer pixels early.** `model_xform_vertex` gives 32-bit camera-space coordinates;
  the projection divides into an integer pixel, and the clip, edge accumulation, span tables and fill all work in
  whole pixels. Sub-pixel information exists only before the divide.
- **The resident world is small.** Four rings of 32 tile slots (`DS:E330`, counts at `DS:061E`; 18-25 populated in
  one capture): a longer view would need this project's own world loader.

## Decisions that constrain the work

- **Observe the draw path (owner, 5 October 2026).** Capture the original's own drawing primitives in this project
  and redraw them. Not taken: rendering from the data segment with the Reimp's renderer, which would make this
  project depend on the Reimp's renderer and assets, held to its own oracle rather than to this machine's.
- **Keep the original art (owner, 10 October 2026, reaffirmed 15 October).** HUD lines and tick marks are drawn on
  the output grid, as the Reimp does; text glyphs, sprites, cockpit artwork and the map's 4x4 aircraft arrow keep
  their original pixels as N x N blocks. No vector or replacement graphics.
- **Stop improving primitive pairing (owner, 15 October 2026).** Interpolating two captured draw lists switches
  shapes without a partner and deforms shapes paired across different visible-vertex sets; identity pairing stays
  off ([below](#identity-pairing-and-the-step-clock-15-october-2026)). In-between pictures come from `rerun`.
- **The jitter is the original's; no smoothing (owner, 15 October 2026).** Road and coastline jitter shows in the GOG
  DOSBox release and in the sharp mode without in-between pictures. On 15 October (21:23) the owner cancelled the
  plan to smooth it, at N > 1 too: the finer modes are experimental and keep the original renderer's quirks. So
  coastline topology transitions and refining clipped or off-screen outlines (6.1% of outline strokes in the owner's
  sharp session, drawn from scaled whole-pixel endpoints) are not planned.
- **Do not extend the draw distance (owner, 15 October 2026).**
- **Timing and visible tests (owner).** Checks run in the background; no visible or fullscreen test without asking
  first. Configuration defaults change only on the owner's call.

## The current plan for 60+ fps (15 October 2026, owner)

- **Safe sharp mode:** `--present replay --present-scale N`, the original's own frames refined N times at the game's
  own rate, no in-between pictures. Verified.
- **The method:** re-run the original's own, unmodified 3-D draw on a throwaway copy of the machine at each in-between
  moment, with only its inputs nudged: the aircraft's position and attitude (from which the view composition
  `vgame_frame_compose`, VGAME 2F9D, derives the camera) and moving objects by their table slots (aircraft traffic,
  VGAME 5852; weapons in flight, VGAME 683E). The existing refinement draws those pictures at any `--present-scale`,
  and a smoothed playback clock times them. It must work at any scale and refresh rate.
- **Safety proof required:** guest checkpoints and final hash unchanged on every route, plus a check that the real
  machine's memory is byte-identical before and after each extra draw; nothing in the copy may reach files, sound or
  devices. Met: [the rerun mode](#the-rerun-mode-in-between-pictures-drawn-by-the-original).
- **Status:** integrated as `--present rerun` and gated (15 October). Defaults are unchanged; `interp` remains.
- **Next:** Stage 3's clock work is closed (19 October; the owner accepted the A/B). Making the observer,
  recompiled-code runtime and fix state per machine for parallel copies, higher refresh rates and further
  hardware checks remain Phase 4 possibilities; visible tests still require the owner's approval.
- **In reserve:** camera reprojection of one step's own draw list. This project's own scene renderer (the
  unconnected `src/present/scene_mesh.c` draft;
  [world geometry](#consolidate-world-geometry-before-more-clipping-exceptions-15-october-2026)) is no longer the
  main path.

## Settings and switches

The same `--present...` options work in `f117a` (the app) and `f117run` (headless); in `f117a.ini` each is written
without its dashes (`present = rerun`).

| Setting | Effect |
|---|---|
| `--present scan` | the scanned-out picture, the original at every scale (default) |
| `--present replay` | the replayed display, one logic frame at a time ([live path](#the-live-path-feed-replay-pacing-and-picture-age)) |
| `--present interp` | in-between pictures by moving paired primitives ([Stage 3](#stage-3-interpolation-between-logic-frames)), drawn on a worker thread in the app |
| `--present rerun` | in-between pictures drawn by the original on a disposable machine copy, then refined on the worker ([rerun](#the-rerun-mode-in-between-pictures-drawn-by-the-original)) |
| `--present-scale N` | the picture N times finer, 1 to 16, with `replay`, `interp` or `rerun` (off by default; 9 for 4K) |
| `--present-age interp\|extrapolate` | `interp` (default) shows the step before the newest; `extrapolate` predicts past the newest ([picture age](#the-live-path-feed-replay-pacing-and-picture-age)). With `rerun` and its smoothed clock nothing is predicted; the clock is only read one display frame later |
| `--present-line-width N` | at N > 1, stroke width of HUD lines, ticks, world outlines, dots and thin span strips: 1-20 pixels of the fine picture; 0, the default, is one original cell ([pen](#projected-outlines-point-markers-and-one-stroke-pen)) |
| `--present-log FILE` | app only: per presented picture, the host clock, the machine's clock, the logic frames shown and t, plus the display mode |
| `f117a.4k.ini` | optional fullscreen preset: `scale = 9`, `present = interp`, `present-scale = 9` |
| `F117_PRESENT_HUD=old`, `F117_PRESENT_BANDS=old` (or `0`, `off`) | the captured-call HUD / sky and ground instead of the state-driven ones |
| `F117_PRESENT_IDENT=1` | the identity-pairing experiment (off by default) |
| `F117_PRESENT_CLOCK=raw` | `rerun` uses the plain step clock instead of the smoothed playback clock |
| `F117_PRESENT_CLOCK_ALPHA`, `F117_PRESENT_CLOCK_PULL` | tuning only, each in (0, 1]: weight of a new step duration (default 0.4) and pull towards one step behind the newest (default 0.025) |
| `F117_PRESENT_THREADS_OFF` (any value) | app: everything on the main thread, for comparison |
| `F117_PRESENT_SHOTS=PREFIX`, `_FROM`, `_COUNT` | app: writes the pictures it presents as `PREFIX<clock>.ppm` from machine clock `_FROM`, `_COUNT` of them (default 60); the only capture of the threaded live path |
| `F117_PRESENT_DIAG=DIR` and its `_HIST`, `_RECT`, `_HIST_ONLY`, `_TRACE` | the pixel-origin diagnostic ([below](#where-the-blockiness-comes-from-the-pixel-origin-diagnostic)) |
| `F117_RERUN_DEBUG` (any value) | `rerun` diagnostics on stderr: the smoothed clock's position at each plan, and (in `f117run`, first twelve steps) where an unnudged copy's records first differ from its step's |
| `F117R_OBSERVE_PAGES`, `_HUD`, `_BANDS`, `_KINDS`, `_SCENE`, `_IDENT` | observer log options for `f117run --observe` ([record kinds](#stage-0-and-1-the-observer-and-the-draw-list)) |

At N = 1 every logic-frame replay is exact, line quirks included. Do not force `F117R_OBSERVE_PAGES=1` on a live
presentation run: it reseeds every phase and prevents pairing. `f117run` with `interp` or `rerun` also checks every
pair at both ends (and, for `rerun`, every copy), which the app does not; its `--shots`, `--screen` and `--frame-log`
record the presented picture.

## Stages

Each stage is judged by a check, as in the rest of the project.

| Stage | What | Check | Status |
|---|---|---|---|
| 0 | Observer: log every call to the drawing primitives per frame | the log is identical across runs; hashes unchanged with it on | done |
| 1 | Draw lists: a frame's primitives as a list, replayed at 320x200 | the replay reproduces the original's work and display pages bit for bit, every phase of every route | done: all 45 complete routes and staged original-instruction fixtures for rare branches; C and Python fill rules agree |
| 2 | Re-draw the list at N times the resolution | N = 1 is Stage 1 exactly; at N > 1 verified geometry has smooth boundaries and keeps the original viewport, fill styles and artwork | done and live (`--present-scale N`): polygons from camera-space geometry, near-plane crossings verified against the original's; one stroke pen; original text/artwork as pixel blocks; HUD, panel items, map marker and sky/ground bands from game state; incompatible geometry scaled |
| 3 | Draw intermediate poses at the host display rate | logic frames exact; extra draws leave live state unchanged; an unnudged copy reproduces its frame | `interp` live, exact at both ends of every pair, pairing frozen; `rerun` integrated with full route and isolation checks |
| 4 | Pacing, vsync, picture age, HUD handling, a switch to the original picture | pictures at the display's refresh with t on the mission clock; the age setting measured | built: vsync pacing, age setting, threaded presentation (isolated 4K/60 timing passed for `interp`), smoothed clock for `rerun`, `--present scan` as the switch. Open: skip/age tuning, `rerun` fullscreen timing, physical-display checks |

## Stage 0 and 1: the observer and the draw list

`src/matched/observe.c` hooks `game_draw`'s entry, VGAME's fill and outline paths, the matched and original
projections, the view composition (2F9D, for `rerun`) and every graphics-library entry. It is a pure reader: with
it on, every checkpoint and final hash is unchanged. `f117run --observe FILE:FROM:TO` writes the log; with
`F117R_OBSERVE_PAGES=1` it also dumps the library's whole page segment (`Z`, work page) and the display (`Y`, A000)
at each `game_draw`.

**Observer rules.**
- Observers are passive in both engines and under the 386 profile, including at addresses shared with matched
  bodies, and do not reset timing bookkeeping. They read guest memory directly, never through `mem_read8`, which
  charges the 386 profile's VGA bus cycles; page snapshots are `memcpy`s.
- Where a matched routine sits at an observer hook's address, the matched routine runs and the hook does not
  (matched routines are registered first, and the first override at an address is the one called). So such a
  routine calls the observer at its entry itself: the projection and `model_prepare_edge` for `V` and `G`,
  `vgame_model_edge_spans` (130D:004A) and `vgame_model_poly_finish` (130D:0116) for `E` and `F`, the HUD and panel
  passes for `h` and `p`, and `vgame_frame_compose` for the `rerun` snapshot. Without it the replay stays exact (the
  fill rows carry the pixels) but nothing pairs.
- The hooks exist only in the recompiled engine; under the interpreter the feed sees nothing.
- All five projection return paths emit `V`, and the original edge-preparation entry emits `G`. Orthographic `V`
  records use range 3, read the actual projected pixels even at Z=0 and leave projection-mode reconstruction to `G`
  metadata; the legacy perspective path rejects them rather than inventing a divisor.

**Purity evidence.** The default paired boot captures have all 217,022 `V` and 300,637 `G` payloads identical between
the engines (matched `V` timestamps charge RET before recording, so the comparison excludes their clocks). Under the
386 profile both engines execute the original projection, and the complete 751,862,587-byte logs are byte-identical,
including 80 checkpoints and the final hash. A retained regression checks shared-address dispatch explicitly.
Private verdict: `D:/f117-gate/observer-original/final-gate-verdict.json`.

**Record kinds.** `P` phase, `V` projected vertex (camera-space x, y, z and the pixels the original made of it), `G`
edge prepared (with both endpoints' camera snapshots; field 18 the projection mode), `E` edge of a filled polygon,
`F`/`R` the polygon's fill and row spans, `b`/`a` fill rows (solid or AND/OR/discard styles; `a` is dither/stipple),
`B`/`L` outline polygon and its edges, `N` library line, `K` colour, `Q` span fill, `U` span-rasteriser polygon
corners, `C` blit, `T` text (with its font, so the replay needs no game data), `S` sprite, `W` scaled RLE sprite, `H`
tick scale, `D` page copies (44 present, 48 copy, 79 dissolve), `M` colour replace (entry 41), `X` any other entry, `x`
changed byte deltas, `J` the display palette (768 six-bit DAC components, logged beside `Y`). State records: `h`, `p`
(`F117R_OBSERVE_HUD=1`) and `o` (`F117R_OBSERVE_BANDS=1`) for the state-driven HUD and bands; `i`/`j`
(`F117R_OBSERVE_SCENE=N`, N requests, 1-65536) and `I` (`F117R_OBSERVE_IDENT=1`) for the scene capture and identity
pairing. `F117R_OBSERVE_KINDS=LETTERS` keeps only those kinds in the log.

**The replay rules.** `tools/drawlist_frame.py LOG` rebuilds each phase from the previous page dump plus every
captured primitive in order and compares it, byte for byte over the whole 64 KB page, with the dump at the phase's
end; `drawlist_spans.py` checks primitives one by one. Every record is replayed by a rule; none of the `x` byte
deltas is needed.

- **Model polygons** replay to the original's pixels bit for bit: the model edge walk (`1377:072B`), the polygon
  accumulator (`130D:004A`), the near-clip join and border runs, and fill styles from the colour word the fill really
  uses (`[8606]`: FF solid, FE AND, FD OR, FB discard). Outline polygons use VGAME's own line routine (`1377:046F`,
  via the style table at `[85F2]`).
- **Library entries**: lines (31), span fills (37, 40), blits (42; a page is an index into the page table at
  `cs:[0787]`: 0 display, 1 work page, 2 cockpit art), text (entries 1-6: a parameter block and a NUL-terminated
  string with width and row clips and a left skip; glyph rows from the driver's font table), sprites (73 and 18; 71
  and 19 clip first), tick scales (11), the scaled flippable RLE sprite (22), the page switches (12-16, so lines and
  span fills carry the page and origin they were drawn at), the present (44) and the colour replace (41, driver
  1188: in a rectangle of a page, every byte of one colour becomes another; the HUD and the cockpit's displays use it
  every phase). Entry 46 writes only the DAC and CRTC start, so its byte deltas are the fill's own rows. Entries
  13-16, 24-26, 45, 46, 62 and 65 only set state (pages, origin, DAC, CRTC start); primitives carry their page and
  origin.
- **Capture traps:** a nested state setter can split a parent entry that resumes drawing, and an entry can write to
  A000 directly even when the active page points elsewhere, so both page snapshots restart after every graphics
  entry; a pending line or span capture closes at the next entry but a model fill's does not (the fill calls entry
  46 while it paints).
- **Fill styles.** Dither alternates the colour word's bytes by logical row/column parity; stipple paints the
  opposite-parity cells and keeps the background in the others. The clipped column, not the destination's byte
  alignment, sets the pattern. Post-fill `a` records are evidence only. At N > 1 each pattern cell stays the size of
  an original pixel while the polygon's boundary is refined.

**Coverage.** With 0 bytes copied from the original, every phase rebuilds exactly on both pages in 30M-instruction
windows across thirteen flights from `ordnance.pic` onward and ten later-flight windows (cargo, strike and its
impact, landing, `recon_return`, `strike_return`, AMRAAM launch and kill on `airair_type5` and `airair_type6`). The
complete-route live replay checks cover the rest of every scripted flight. Command per window:
`F117R_OBSERVE_PAGES=1 py tools/run_route.py tools/routes/NAME.args --data DIR --out OUT -- --observe LOG:FROM:TO`,
then `drawlist_frame.py LOG`. Branches no window exercised are covered by staged fixtures run on the original
instructions: `test_present_fills` (2,619 cases against VGAME's fill selector: every style class, clipped spans, odd
page alignment, N=1/4/9; Python matches 2,187 exported original rows) and `test_present_driver` (3,953 cases against
the resident driver: opaque/transparent text, fonts and clips, sprite clips, scaled RLE clipping, transparency and
flips, both tick-scale variants). These are synthetic fixtures, separate from route evidence.

**The replay in C.** `src/present/drawlist.c` is `drawlist_frame.py` in C, rule for rule: fed one record at a time
(`drawlist_record`) on the pages it was seeded with (`drawlist_seed`); it reads nothing from the machine. Where the
Python replay copies a present's logged source bytes, the C replay uses its own copy of the source page, so the
display check also covers the work page at the present (`prefer_logged` does as Python does).
`build/test_drawlist.exe LOG [--carry] [--logged]` prints `drawlist_frame.py`'s lines so the two diff; `--carry` seeds
each page once and carries the replay from phase to phase, as the live path must. On eight retained 30M windows (447
phases, `p3-live/logs41/w_*.log`) the two agree in all modes and every phase is exact on both pages, carried too. A
75 MB log replays in under a second.

## Stage 2: the picture N times finer

### Why the integer list is not enough

The edge records carry integer endpoints already rounded to the 320x200 grid, so scaling them by N and running the
same edge walk gives only a nearest-neighbour picture. Two early studies showed what is needed instead.
`tools/hires_spans.py` re-walked filled polygons on an N-times grid: N = 1 agrees on every flat pixel, N = 2 and 4 on
88.8% and 85.8%, all disagreements in polygons the original clips (it clips whole edges in integer coordinates, and
those rules do not scale). `tools/hires_frame.py` refilled polygons from their integer edges and left cracks wherever
two polygons share an edge: nothing in the integer list says where the true shared edge lies. So the refinement
uses the camera-space vertices from before the divide, with a fill rule that tiles.

### Sub-pixel re-projection

`src/present/hires.c` refills each model polygon from its true geometry on the N-times grid, and `drawlist.c` keeps
the fine picture beside each page (`drawlist_set_scale`: every write is mirrored as an N x N block unless a refilled
polygon's rows are arriving; blits and page copies between held pages carry the fine rows).
`build/test_hires.exe LOG N [--carry]` reports on a log.

- **Vertices.** An `E` edge record names its slot; that slot's `G` record carries both endpoints' camera
  coordinates. The projection (`0x129A2`) is `x >> 8` over the high word of z (or `x >> 1` over `(z >> 8) >> 1` below
  0x100); every `V` record of a strike window reproduces under that formula (13,402 of 13,402). The fine position is
  the full fixed-point ratio `256*x/z`, taken before the original shifts its numerator and depth.
- **Fill rule.** A fine pixel is painted when its centre is inside the true polygon, left and top inclusive, so
  polygons sharing a vertex pair tile. Verified polygons clip to their viewport with half a fine pixel of inclusive
  coverage, which joins adjacent faces. At N = 1 the footprint is the original's, so the picture is Stage 1's.
- **The flat-cell guard** restores the coarse value on a flat block the fine fill disagrees with (the horizon
  residue: a ground polygon whose true top lies half a row below where the original's inclusive spans started it),
  and counts what it restores. It excludes verified fine geometry and HUD strokes, so it cannot put coarse stairs
  back (a scalar regression checks every output byte and counter at N=1..16).
- **Left scaled:** geometry that does not reproduce the original (below), fills with no replay rule, and by choice
  the original bitmap text, sprites and cockpit art.

`tools/hires_subpixel.py` (the Python study) still has an earlier footprint/seam rule for near-clipped polygons and
no longer mirrors `hires.c` on them.

**Live and at 4K.** `--present-scale N` runs the builder beside the live replay and presents the N-times picture with
the scanned frame's palette; the guard is applied to every picture shown, and `--shots`/`--screen` write 320N x 200N.
In-between pictures move the vertices of every paired batch in camera space (`INTERP_VERTICES`), so a moved polygon
is refilled from moved geometry. Endpoint checks at a scale use t = 1e-9, because 1e-6 of a step can move a
camera-space coordinate by a rounding unit, which a finer grid shows. At N = 9 the picture is 2880 x 1800; shown 4:3
(each row 1.2 times as tall) it fills 2,160 lines, and the app stretches it to the window with linear filtering.

### Camera snapshots, near-plane crossings and orthographic maps

- **Camera snapshots.** `G` records carry both endpoints' camera coordinates, projected coordinates, vertex
  identities, behind markers and projection origin; C and Python use them for every polygon (inferring the origin
  from unclipped edges failed when every edge was clipped, and `V` records go stale when vertex storage is reused).
  Legacy short `G` records keep the scaled fallback.
- **Near-plane crossings.** `nearclip.c` repeats the original wrapped ADD/ADC-plus-one midpoint and signed shift,
  including DEC DX's sign test, then keeps x/128 and y/128 before the original discards precision. Each crossing must
  reproduce the original's retained coarse crossing exactly; a mismatch or unsupported shape keeps the scaled
  picture. 5,014 crossings across strike, landing and air-to-air match the original. Near-clipped polygons are drawn
  from their geometry like any other (the earlier footprint clamp and seam repair were removed on 14 October).
- **Orthographic maps.** Map objects legitimately have Z=0, so `G` appends the byte at `[7D8E]` as field 18: zero for
  perspective, otherwise the orthographic shift. C and Python reproduce the original high-word shift and rounding and
  require both coarse coordinates to match the capture before using fine positions. Shifts 16-31 keep full
  fixed-point precision; shifts 1-15 keep the original wrapped integer vertices and draw their edges on the fine
  grid. All 196 orthographic endpoint projections in a fresh boot capture match the original.
- **Interpolation** binds each `E` to its preceding `G` within its batch and moves camera snapshots, retained
  crossings and projection metadata only when the pair agrees (same mode, origin, markers, identities, topology);
  otherwise the skeleton's records hold.
- **Results.** Recorded strike, landing and air-to-air windows pass at N=1/4/9: strike refines all 17,058 evaluated
  polygons, landing all 4,916, air-to-air 4,110 of 4,111; live boot N=4/9 refines all 48,856 model polygons. The one
  air-to-air case left has a crossing so far out (x=229,549 becomes -32,595 in the original's 16-bit line clipper)
  that its coarse silhouette differs from the camera geometry, so the original scaled polygon is kept.
- **Tests.** A synthetic diamond crossing the viewport on every edge, with stale behind-eye `V` records,
  reconstructs at N=1/4/9 (N=1 exact); the observer regression checks fields with ordinary, VGA and wrapped memory
  placements while requiring unchanged CPU state, clock and memory; synthetic tests cover signed rounding, zero
  depth, low-shift wrapping, incompatible modes and corrupt projections.

### Span-rasteriser polygons, smooth terrain and the map

- **The horizon and the sky and ground gradient** are polygons the game fills through its own span rasteriser
  (fill_polygon, VGAME `0FB2:01E6`), not model polygons. A hook at that routine's entry logs each polygon's corners
  (`U`: colour, count, x and y words in the rasteriser's coordinates, far outside the picture for a horizon strip)
  before its span fill (`Q`). At N > 1 the corners are validated against the original span footprint and drawn as
  cell-centred geometry with half a fine pixel of inclusive coverage, clipped to the span table's bounds; only a
  polygon with zero area becomes a stroke. Interpolation pairs `U` records by colour and order. At high detail the
  bands are drawn from state instead ([bands](#the-state-driven-sky-and-ground-bands)).
- **Smooth terrain.** Verified model polygons clip to their viewport and draw the actual geometry, not the original
  coarse footprint's staircase. Dither/stipple cells, bitmap lettering, cockpit art and map symbols remain the
  original artwork.
- **The map.** Its terrain is drawn once into the work page, then copied through the cockpit-art page (segment
  `09C0`) and back every frame; the replay does not hold that page. A bounded cache of eight fine rectangles carries
  those rows through the round trip, restoring a rectangle only while its logged coarse bytes still match; otherwise
  the coarse fallback shows. The map's orthographic projection rounds to the nearest integer, which names a cell
  centre, so its vertices take that half-cell alignment.
- **Tests.** `test_present_map` (a straight orthographic edge, the rectangle round trip, ownership after a replay
  copy, repeated display-blit bytes, stale-byte fallback, N=1/4/9); `test_present_strips` (thick, narrow, degenerate
  and incompatible span polygons, overflow and line cases); `test_near_polygons` (no hole between adjacent
  near-clipped faces; the near-clip tolerance). Evidence: `D:/f117-gate/map-refill`.

### Where the blockiness comes from: the pixel-origin diagnostic

Normal-width captured integer line strokes are clipped to their endpoint
cells. A rotated square cap otherwise extends just beyond that rectangle;
at the moving map's edge it leaves fine specks outside the restored map.
Explicitly wider pens keep their extra coverage. The regression covers all
octants and translated page origins at N=4/9, while the original coarse
line rule stays unchanged. Evidence: `D:/f117-gate/map-specks-20261018/`.
Before/after captures retain all 953 exact logic frames /951 endpoint pairs
and the interpreter checkpoint hash; affected lower-border remnants fall
from 22/32/38 pixels to zero. The presentation-only check passes all 243
tests, all 45 N=1 routes and N=4/9 boot, with unchanged runner hashes.

`F117_PRESENT_DIAG=DIR` (with `--present-scale N > 1`; off when unset) gives every replayed page a byte per fine
pixel naming the path that last wrote it (`src/present/diagtag.h`: refined model polygon, refined span polygon, each
fallback by reason, guard repairs, strokes, text, sprites, blits from pages the replay does not hold, copies); the
plane follows copies, blits and the map cache as the fine pixels do. Beside each `f117run` `--shots`/`--screen`
picture it writes `BASE.tag`, `BASE.diag.ppm` (false colour), `BASE.holes` and `BASE.txt`.
`F117_PRESENT_DIAG_HIST=FILE` appends the per-tag counts of a window (`F117_PRESENT_DIAG_RECT=x0,y0,x1,y1`, default the
3-D view 0,0,320,91; `F117_PRESENT_DIAG_HIST_ONLY=1` skips the images) and its two hole counts;
`F117_PRESENT_DIAG_TRACE=FILE` logs each polygon refused for a footprint beyond 3 px; the final `[diag]` lines print
the builder's and interpolator's counters. `tools/present_diag.py` makes side-by-side PNGs and class tables. Guest
hashes and presented pictures are identical with it on.

The hole counts (`diag_holes`, `headless.c`) are in fine pixels: *unwritten* is a pixel no path wrote this frame (an
earlier frame shows through); *stray* is a pixel a scene path wrote whose colour is none of the nine coarse cells
around it (a gap, or geometry drawn where the original drew another colour). `BASE.holes` marks them.

The diagnostic found the two sources of visible blocks on 14 October: near-clipped polygons clamped to the original
footprint, and large polygons refused by an exact 3 px footprint test and drawn whole as the scaled copy (in 6 to 8%
of sampled frames, up to 83% of the view). The next section is the fix. Evidence: `D:/f117-gate/present-v2/fix3d`.

### Footprints the original's clipper gets wrong

**The rule.** At N > 1 every verified model polygon, near-clipped ones included, is drawn from its geometry
(`geometry_cover`); the footprint only decides whether the geometry reproduces the original. The exact test
(footprint within 3 px) stays; beyond it, by kind:

| Kind | Rule at N > 1 | Why |
|---|---|---|
| Near-clipped model polygon (`nclip` 2, both crossings verified against the original's) | accepted when the footprint lies within 16 px (Chebyshev) of the geometry and the geometry spills over the footprint by at most a tenth of its size (`NEAR_CLIP_TOLERANCE`) | measured below |
| Span-rasteriser polygon with an edge whose 16-bit difference overflows (corners more than 32,767 apart) | accepted, and drawn from its corners | every refusal has one |
| Zero-height span strip with a one-row footprint | the original's row is the rendition (`DT_SPAN_LINE`) | nothing to refine |
| Zero-area span polygon with a taller footprint | kept scaled, `DT_SPAN_DEGENERATE` | the original's own artefact |
| Anything else beyond 3 px | kept scaled | geometry that does not explain the picture |

At N = 1 nothing changes: the footprint is the picture.

**The cause, as measured** (refusals traced with `F117_PRESENT_DIAG_TRACE` over strike, landing and air-to-air). Span
polygons: every non-degenerate refusal has an edge between corners more than 32,767 apart, which sends the original's
clipper (`0FB2:0757`, `vgame_span_edge_clipped`) down its halve-both-axes path, losing low bits before it places the
clipped endpoint; the footprints land up to 8 px (most) and 128 px (a few) from the true band. Overflow alone does not
predict a refusal (316,560 overflowing polygons in one run matched within 3 px); why only some are off was not
established. Model polygons: all 833 refused near-clipped polygons have a crossing 4,800 to 280,000 px outside the
picture, which the original clips with a 32-step bisection and 16-bit slopes (`vgame_mc32_bisect_one`,
`vgame_mclip_point`); their footprints lie within 16 px of the geometry. The 4,198 refused non-clipped polygons are
mostly tiny polygons whose geometry lies rows from the footprint: a mismatch, so they stay scaled.

**Before and after** (14 October, N=4, 3-D window, frames sampled every 5 s of emulated time):

| | strike (144) | landing (167) | air-to-air (126) |
|---|---|---|---|
| frames with more than 0.5% of the window in fallbacks | 12 (8.3%) to 4 (2.8%) | 12 (7.2%) to 10 (6.0%) | 7 (5.6%) to 1 (0.8%) |
| frames above 5% / above 20% | 10 / 7 to 0 / 0 | 7 / 7 to 0 / 0 | 7 / 4 to 0 / 0 |
| worst frame | 83.6% to 1.7% | 55.6% to 3.3% | 42.4% to 0.7% |
| mean fallback share | 2.60% to 0.04% | 2.04% to 0.09% | 1.32% to 0.01% |
| unwritten pixels | 0 to 0 | 0 to 0 | 0 to 0 |
| mean stray pixels (share of the window) | 10,823 (2.3%) to 12,275 (2.6%) | 10,413 (2.2%) to 11,618 (2.5%) | 5,355 (1.1%) to 6,574 (1.4%) |
| worst stray frame | 7.1% to 14.6% | 7.5% to 17.4% | 4.9% to 22.9% |

Colour changes between scene polygons that lie on the coarse cell grid fell from 14.4% to 1.4% over sixteen sampled
frames; a hard turn (3 to 7 g, up to 40 deg/s) does not reproduce the bursts. The stray count rises where the
original's clipper mis-drew a polygon: the picture now shows the polygon the game asked for, which differs from the
original's N=1 picture by whole rows in those frames ("true geometry wins"). N = 1 is untouched.

**Not settled.** The original clipper's exact error was not reproduced offline, so the tolerances (16 px, a tenth, the
overflow criterion) are bounds from three flights, not derived limits: a flight with a larger error would be refused
(and scaled) rather than drawn wrongly. A span polygon accepted on its overflow is drawn inside the bounding box of
its footprint, so a band whose true rows lie outside the original's table loses that part.

### Projected outlines, point markers and one stroke pen

At N > 1, unclipped world outlines and degenerate edges (single-pixel scenery and light markers, drawn as dots) draw
from exact `G` camera snapshots, when their slot, projected endpoints and origin match the captured `L`; stale,
clipped, behind-camera, off-screen and unsupported records keep the scaled whole-pixel endpoints. Paired outlines
interpolate their camera coordinates when both snapshots name the same vertices.

**Projection.** Perspective uses the full fixed-point ratio for polygons and strokes alike; an earlier rule (the
divide's remainder, depth still rounded) jumped at each negative projected integer.

**The pen.** Captured HUD lines, ticks, world outlines, thin span strips and the state-driven HUD share one stroke:
between the centres of its end cells, with perpendicular width and square caps, so an axis-aligned stroke one cell
wide covers exactly the original's cells and joined strokes meet solidly (the Reimp's square nib, the starting
point, made diagonals up to 1.4 times thicker). The width is one original cell (N fine pixels) unless
`--present-line-width` sets 1-20 fine pixels; that setting applies to HUD lines, ticks, the state-driven HUD,
world outlines, dots and thin span strips (a strip as thick as its footprint, one or two cells, takes the setting
per cell). Widths by drawing role (ground markings against object
outlines) are not assigned. Text, sprites and artwork never change with the width.

**Checks.** `test_world_lines` (coarse equality at N=1/4/9, signed-coordinate and depth-boundary continuity,
stale-slot and vertex-identity rejection, equal-width horizontal/diagonal strokes); `presentation_hud_lines` (coarse
identity, octants, reversal, dots, width, origins, guard, colour operations, artwork and copies). `test_world_jitter
LOG N` measures paired outlines and dots at eighth-step intervals within each step: on the retained strike window at
N=9 the RMS second differences fall from 1.680 to 0.0039 fine pixels for outlines and 2.756 to 0.0140 for dots, and
negative polygon axes from 0.496 to 0.0634. These are coordinate measurements of rounding removed, not a claim about
visible coastline motion. Evidence: `D:/f117-gate/present-v2/world-lines`.

## The state-driven HUD

At N > 1 the HUD is drawn from the game's state at fine-pixel precision, and the state is interpolated between logic
steps. The original rounds every HUD item to whole cells before it calls the library (the ladder's rungs by up to 5
cells), so a picture built from its calls jitters at 4K however it is interpolated. N = 1 is unchanged: the original's
calls are replayed. `F117_PRESENT_HUD=old` turns it off. The formulas are in `src/present/hud_state.c` (analysis:
`D:/f117-gate/present-v2/hud-analysis.md`, private).

**The state.** VGAME's HUD pass (1058:0016) is a pure function of its data segment at entry. Record `h`
(`observe_hud_pass`, called by the matched pass at its entry) copies the 1,560 words 0x2080-0x2CAE (tables, layout
constants, text blocks), 19 scattered words (heading, pitch, roll, altitude, raw and whole-knot speed, the ladder
flag, the G text) and the graphics driver's tick tables: 1,593 values (`hud_state.h`). Record `p`
(`observe_panel_pass`, at the matched panel pass 0000:79BE, which runs first) holds the words the flight-path marker
and the two bars are made from (`[98F6]`, `[E586]`, `[E474]`, `[B07E]`, `[B194]`) and the HUD mode and size flags,
extended with the panel state. Both read guest memory directly and change nothing (fly3 route: final guest hash and
the rest of the `--observe` log byte-identical with and without them).

**Two models** (`hud_state.c`). EXACT is the original's integer pipeline (its clipper `0FB2:0330` included) and gives
the library calls of the pass in order. FINE carries the same formulas in floating point from the continuous state,
with no final rounding: airspeed from `[B07E] / 27` (the raw word is finer than the HUD's knot), altitude, and
heading, pitch and roll as fractional angle words. Lines are strokes between cell centres, bitmap text is placed by
its top-left corner, and clips are the original's, at cell edges.

**Drawing** (`hud_overlay.c`, entered from `drawlist_record`). At an `h` record the replay draws the FINE primitives
on the work page's fine picture: strokes in the shared pen; glyphs from the font the `T` records carried, each source
pixel an N x N block at the glyph's fine position. A cover (`hud_cover.c`) then recognises each arriving record of
the pass against the EXACT list, by kind and every value; the ones FINE replaces still run through the coarse replay
(so the coarse picture and the exactness check against the display stay the original's) with the fine mirror off. A
record that leaves the prediction ends the cover and the rest of that pass keeps the original path, as does a pass
with no tick table, font or page yet. The panel's flight-path marker is drawn from the record's own art at
y = 48 + `[98F6]` / 64, and the bars as strokes ending at 52 - `[E586]` / 16 (vertical speed) and
81 - ((command speed - speed) * 2 / 5 + 29) (stall margin, speed `[B07E]` / 27). `f117run`'s `[present] state-driven
HUD ...` line counts each case; the ink is tagged 240/241 in the diagnostic plane.

**Interpolation.** With a scale above 1 (`INTERP_HUD`), the skeleton's `h` record is regenerated with speed,
altitude, heading, pitch and roll moved the same fraction (angles by the shortest arc, `hud_blend`; a hair from a
logic frame it is the frame's own record, so the endpoints stay exact). Numbers, text, switches and the map arrow's
bitmap stay with the older logic frame until the newer arrives (`INTERP_HUD_HOLD`). Layout, mode, the readout flag,
colours and the ladder switch are the skeleton's; if two frames disagree on them the skeleton's state is used unmoved.

**What is covered.** State-driven at N > 1: airspeed and altitude labels and ticks (both altitude scales, full and
small HUD); heading labels and tick strip; pitch-ladder rungs (all shapes, rotated by roll, clipped to the ladder
window) and labels; the frame, reticle and G readout; and the panel pass's station and gun text, flight-path marker
(sub-cell height), tracking cross, stall-margin and vertical-speed bars, sink marker, speed/altitude/heading readouts,
outlines, acceleration and training text, cue and messages. Still drawn from the original's records (scaled): the
ILS, the air-target box and envelope ring, the cockpit lamps, the navigation display and the weapon symbology.
Cockpit art and all bitmap fonts and sprites stay the original art.

**The map marker** stays the original 4x4 bitmap selected from eight headings; only its position moves on the fine
grid, from the original 32-bit aircraft coordinates with the map's zoom, origin and 16-bit subtraction wrap kept.
The heading's bitmap switches with the logic frame.

**Checks** (`tests/test_hud_state.c`, `test_hud_live.c`, `test_hud_jitter.c`, `test_map_marker.c`; corpus and windows
private under `D:/f117-gate/present-v2/`).
- EXACT against the original's log: all six full/small HUD modes, three altitude bands, ladder-off passes and
  steep-pitch rungs; 29,594 HUD calls over 711 passes, 11,490 panel calls over 710 passes and all 714 map-marker blits
  match. The corpus checker rejects an empty capture.
- FINE where it must agree with the original's integers: tape labels equal the float's floor; tick lattices equal
  the driver's marks; heading labels differ by [0, 2); ladder vertices by x [-3, 5], y [-3, 1] (the original cuts
  pitch to a quarter degree and floors twice); marker, vertical-speed bar and stall bar within about a cell.
- Live (`test_hud_live LOG 4 --leak-check`, strike and fly windows, N = 4): every logic frame and pair exact at both
  ends, coarse and fine, 0 records off the prediction, 0 replaced records changing the fine picture. 34 live checks
  over 17 windows pass; the owner's N=9 replay keeps its 69 checkpoints and final hash. Cost at N = 9 is the same as
  without (3.2 s against 3.4 s for a 37-frame window).
- ROM-free tests check the eight arrow bitmaps pixel for pixel, transparency, coarse replay, fine movement, heading
  timing and wrapped map origins; observer tests verify unchanged guest memory, registers and clocks under both
  timing profiles.

**Motion** (`test_hud_jitter`: the same pairs interpolated with the old integer HUD and with the state, four samples
per step; RMS / max second difference of a feature's position, fine pixels at N = 4, where 4 is one cell):

| window | feature | old | state-driven |
|---|---|---|---|
| fly1, climb (72 pairs) | airspeed labels, y | 1.99, 4.00 | 0.01, 0.07 |
| | altitude labels, y | 2.13, 4.00 | 0.01, 0.10 |
| | heading labels, x | 0.67, 4.00 | 0.03, 0.48 |
| | ladder labels, x / y | 1.05, 8.00 / 3.78, 28.00 | 0.00, 0.02 / 0.01, 0.05 |
| | ladder endpoint, x / y | 0.37, 4.00 / 0.74, 8.00 | 0.11, 1.65 / 0.38, 5.86 |
| | flight-path marker, y | 4.32, 40.00 | 1.10, 10.22 |
| | vertical-speed bar end, y | 1.42, 4.00 | 0.19, 1.88 |
| fly3, inverted bank, mode 1 (35 pairs) | heading labels, x | 1.72, 4.00 | 0.06, 0.66 |
| | flight-path marker, y | 3.60, 28.00 | 0.80, 6.53 |
| | ladder labels, x / y | 4.65, 16.00 / 4.58, 12.00 | 0.35, 2.12 / 0.49, 3.30 |
| fly3, steady 48 degree bank (35 pairs) | heading labels, x | 1.33, 4.00 | 0.00, 0.01 |

At N=9 on fly1 the map arrow's horizontal RMS falls 0.75 -> 0.00 fine pixels and the vertical-speed bar 3.19 -> 0.43.
The residue is the flight state itself: the original repeats a state in consecutive logic frames and then jumps,
which interpolation between real frames cannot hide. Evidence: `D:/f117-gate/present-v2/hud`, `hud2`.

**Not established.** The colour-display HUD (`[43DE]` set: tape and text colour 10, ladder 47) was in no capture; the
fine strokes take their colour from the state and the tape's text block, and the original's art may differ. Nothing
here has been judged on a physical display.

## The state-driven sky and ground bands

At high detail and N > 1 the background follows the HUD's approach: read the pass's state, predict its integer calls
exactly, then draw the same quadrilaterals without their rounding, in the original 16 sky and 16 ground colours. N=1
stays on the captured calls; low detail keeps its original path; wrapped corners and unusable geometry keep a
conservative fallback. `F117_PRESENT_BANDS=old` restores the earlier band renderer (integer-corner interpolation).

The observer emits `o` once at VGAME `1450`: height, pitch/roll sine-cosine pairs, the offsets, view centre, window,
flags and shifts. EXACT follows the original's signed arithmetic, rounded multiplies, divides and wraps; FINE uses
the same formulas without rounding. Interpolation moves height linearly and angles along their shortest arc; view,
detail, colours and other discrete settings stay on the selected logic frame. Band `U` records stay exact while the
state moves, and a band with a wrapped corner keeps its record, so it cannot break recognition of later bands.

**Capture replay rule.** Post-call `x` snapshots of copies and fills measure the original picture; applied to a
regenerated picture they painted coarse ghosts at the original edge positions. Interpolation therefore omits
snapshots for entries 42/44/46, whose `C`/`D` and model records draw their result; raw frames still apply genuine
changed-byte fallbacks, and duplicate presents keep carried fine pixels. A synthetic moving-dot regression fails
without this rule.

**Evidence.** All 1,514,560 original quadrilaterals match over 47,516 captured passes, including 186 low-detail and
1,609 flat passes. On 47,313 same-view pairs, endpoint and midpoint checks pass. The owner's flight keeps all 69 guest
checkpoints and final hash `31c259b992340414`. A ROM-free observer test checks identical records and no clock, VGA
bus or guest memory change under both timing profiles. 10 band-live checks pass. At N=9 in the owner's window,
horizon-height second differences fall from RMS about 3.3 to 0.4 fine pixels; strike, all band edges 3.67 to 1.26
(these include logic-step changes and camera acceleration). Evidence: `D:/f117-gate/present-v2/band-finish`.

## Stage 3: interpolation between logic frames

This is the `--present interp` mode, and the fallback when a `rerun` copy fails. `tools/interp_frame.py LOG`
(`--check`, `--pairing`, `--primitives`, `--geometry`, `--predict`, `--strip DIR`) was the 320x200 study;
`src/present/interp.c` is the same in C, and `build/test_interp.exe LOG` prints the study's report line for line.

**Pairing.** The projection delivers a model chunk's vertices to one buffer from `C6B4` (a *batch*: one run of `V`
records); edges are numbered inside it (`G` slot, 18h apart from `5EEA`) and a polygon is the list of edge slots its
`E` records carry. So identity is (batch, edge slots):
1. batches are aligned between frames in order by longest common subsequence. A pair needs an equal vertex count,
   or (three vertices or more) a count within 10% with 70% of the shared leading vertices within a quarter of their
   depth in camera space, and a mean screen motion under 60 px; look-alikes go to the smaller motion;
2. painted polygons in a paired batch pair by equal edge-slot lists (colour ignored);
3. span fills (`Q`) pair by colour, mode and order; outline edges (`L`) by batch, slot and colour; HUD library lines
   (`N`) by colour, page and order when the group has the same size in both frames and moved under 30 px.

**Interpolation.** A picture at t takes the nearer frame's list as its *skeleton* and moves every paired primitive
the fraction of the way. Everything else (text, sprites, blits, ticks, an unpaired polygon, a polygon whose clip
status or fill style differs) stays as the skeleton has it, which is a switch at t = 0.5. A moved polygon is refilled
by the original's edge walk in the skeleton's fill style. Edge ends from front-range vertices are interpolated in
camera space and projected by the original's divide, the rest in screen space: in the strike study window 90.3% of
edge ends land where the middle frame really has them (79.8% in screen space, 73.0% holding the first frame). A frame
pair is a *cut* (held whole) when under half the polygons pair or the median vertex motion exceeds 40 px.

**Study figures** (8 October, 9 MIPS, seven windows; the replay of every frame and the machinery a hair from each
end exact on both pages; every in-between record is a record of one of the two frames or a regeneration of a pair):

| window | steps/s | polygons paired | cuts held | vertex motion per step, px (mean / median / p95 / max) | edge ends exact: hold -> interpolated |
|---|---|---|---|---|---|
| strike 8.53B | 10.6 | 91.1% | 0 / 33 | 2.6 / 0 / 9 / 308 | 73.0% -> 90.3% |
| strike impact 8.70B | 9.4 | 86.2% | 1 / 29 | 4.9 / 1 / 19 / 5165 | 25.3% -> 40.3% |
| landing approach 9.70B | 10.1 | 98.0% | 0 / 31 | 1.5 / 1 / 6 / 116 | 50.6% -> 66.9% |
| landing 9.89B | 13.6 | 95.6% | 0 / 33 | 0.6 / 0 / 1 / 202 | 95.3% -> 98.0% |
| AMRAAM launch 5.08B | 9.3 | 98.5% | 0 / 29 | 1.0 / 0 / 3 / 226 | 77.3% -> 83.0% |
| AMRAAM kill 5.23B | 8.8 | 97.6% | 0 / 27 | 2.3 / 1 / 12 / 292 | 64.2% -> 60.7% |
| take-off roll 2.50B (night) | 22.8 | 29.9% (87 painted) | 3 / 72 | 11.6 / 5 / 32 / 1002 | - |

**What is held** (1.5% to 14% of painted polygons by window): a model chunk with no partner (an object entering or
leaving, a vertex count changed by more than a tenth, a model drawn as one chunk in one step and two in the next, an
explosion); a clip status that differs (an edge crossing the window or the near plane between steps); an HUD group
that changes size; every `T`, `S`, `W`, `C`, `H` and `D` record. Numbers and text change at the middle and target
boxes jump (`D:/f117-gate/p3-stage3/png`). Pairing is frozen by the owner's decision; `rerun` avoids it.

## The rerun mode: in-between pictures drawn by the original

`--present rerun` (`src/present/rerun.c`, `drawfeed.c`) draws each in-between picture with the original's own,
unmodified drawing code.

**How it works.**
- When the view composition (`vgame_frame_compose`, VGAME 2F9D) is entered for the frame after a present, the feed
  snapshots the machine: its structure, all 1 MiB of memory and the fixes' host state (D1's frame limiter, D34's
  extension table). Each closed step keeps its snapshot.
- For a picture at t between steps a and b, the main thread copies a's snapshot into a private machine and nudges the
  inputs the composition reads t of the way towards b's values: the aircraft's 32-bit position (`BA3C`/`C056`) and
  heading, pitch, roll, height and altitude (`2DEE`-`2DF6`); computer-flown aircraft (24h-byte slots at `C16A`,
  `[DEFC]` of them; only a slot of the same type and state in both steps); weapons in flight (twelve 1Ch-byte records
  at `3C3A`; only one alive with the same type in both). A slot that changed occupant is not nudged, so it steps
  instead of gliding. A slot's heading, pitch and roll move as one orientation: the game keeps pitch within a quarter
  turn, so a unit pitching through the vertical (an aircraft in a dive) is stored with heading and roll half a
  turn away and the pitch reflected (`psi + 8000h`, `8000h - theta`, `phi + 8000h`), and its heading and roll flip by about
  half a turn at every step while the model barely moves. Each angle moved by its own shortest difference would twist the
  model half a turn about the vertical between two steps, so the later attitude is taken in whichever spelling is
  nearer the earlier one (`nudge_att`).
- The copy runs under its own observer until the frame closes (the phase after its present), with a watchdog of a
  quarter emulated second or 4 million instructions, whichever is more. Its records, from its first phase on, are
  replayed on a's start pages and refined at the chosen scale like any logic frame; the HUD and bands come from the
  copy's own state records, so they move with the nudged inputs. A copy that fails falls back to Stage 3
  interpolation and is counted (`[present] rerun:` in the log).
- In the app the copy runs on the machine's thread just before the worker draws; the observer, the recompiled-code
  runtime and some fix state are process globals, so copies cannot yet run in parallel.

**Isolation.** The copy's memory, CPU and machine back-pointers point only at the copy (`cpu.user` once pointed at the
real machine, so a copy's port reads and writes reached the real devices); hooks, open files, the log, the input
recorder, the OPL schedule, coverage and traps are cleared. Any DOS call other than console status and the time of
day stops the copy (int 21h, E3h, 20h, including a call chained through the native stub), so it cannot reach a file.
The fixes' state is loaded from the snapshot and the live state restored afterwards: guest-memory equality alone was
not enough, because D1's limiter lived in process globals and a copy advanced its live deadline. Private speech
schedules a fix creates in the copy are freed. `presentation_rerun_isolation` (`tests/test_rerun.c`) checks private
memory and devices and rejected DOS calls under both timing profiles.

**Checks** (`f117run`, every copy): before and after each extra draw, the real machine structure, all 1 MiB of
memory and the fixes' state are compared; at t = 0 the unnudged copy must draw its step's own picture, coarse and
fine. `tools/presentation_check.py --mode rerun` runs this over every route against the scan baselines.

**Status (15 October 2026, commit `fe20913`).** The full gate passes (116 CTests, 45 routes identical, both instruction
profiles, three matched seeds, reachability/event checks). The 45-route rerun N=1 check keeps 259,240 frames / 259,166
pairs and every guest hash exact, with 1,638,579 copies, 1,378,030 in-between pictures drawn by the original, 0
changes to the real machine and 0 blocked DOS calls; 259,787 unchanged-copy frames are exact, 0 unequal. Boot N=4/9
is exact. A hidden threaded owner-flight smoke run keeps 1,610 of 1,610 logic frames exact with 4,933 original
in-between pictures and 3 fallbacks. Evidence: `D:/f117-gate/present-v2/rerun-accept/final-gate-verdict.json`,
`app-verdict.json`.

**Feasibility measurements** (15 October, research harness on branch `draw-rerun-probe`, `F117_RERUN_PROBE=FILE`;
not on master). On the owner's flight (1,609 logic frames) every re-run returned and reproduced the real draw
records exactly apart from the observer's own lazy line records; the real machine and final hash never changed.
Nudging halfway put 97.2% of vertices within tolerance of the two real frames' midpoint (98.0% of those matched to an
object in both frames). Nudging aircraft slots raised air-to-air scene-walk objects at the midpoint from 97.7% to
98.9%; a small deferred-object group (`DF03`, upstream `C2C8`, about 1,700 vertices; the generic deferred-object draw
in a secondary camera's nested terrain view) fell from 98.6% to 96.6%; that group is the air-target camera's inset, and the
fall was one target in a vertical dive (see the nudge above; with the attitude nudged as one orientation the same flight's
group is 100% within tolerance, `D:/f117-gate/df03-research/`). Cost per extra draw: 1.08M
instructions (max 1.63M), **6.2 ms (max 8.8 ms) on one core**; at a 57 ms step, 60 Hz needs about 2.5 extra draws a
step (~27% of a core) and 144 Hz about 7.5 (~80%), so high refresh rates need parallel copies. `F117_RERUN_PICS` on
that branch dumped the copy's own 320x200 pictures beside the two real frames
(`D:/f117-gate/present-v2/rerun-probe/pics`). An earlier design spliced only the composition's records into frame
k's list; it was replaced by replaying the copy's whole frame.

## The live path: feed, replay, pacing and picture age

- **The feed** (`src/present/drawfeed.c`) is an observer that keeps every record in memory, a logic frame at a time:
  a frame closes at the first phase after one that presented (entry 44), or after eight phases without one, and
  waits in a ring of eight until the host takes it. At the close it copies A000 into the frame, which is what the
  replay must equal. It asks for page dumps only to seed the replay (at the start, and after a frame differs or is
  dropped); it takes no page snapshots, since every drawing entry has a rule.
- **The replay** (`drawlive`, same file) runs `drawlist.c` on each closed frame, carried from the one seed, and
  compares the display with the frame's copy of A000. A frame that differs is not shown and the replay is seeded
  again.
- **Presenting.** In flight the screen is the replayed display with the scanned frame's palette; elsewhere (another
  program, text mode, a CRTC start other than 0, or no frame closed for a quarter second) the scanned-out picture. A
  frame is presented from its close, so `replay` is up to one step later than the scan. In the app `replay` is
  updated at each emulated VGA retrace; `interp` and `rerun` are drawn in the host loop.
- **In-between pictures.** `drawlive` keeps the last published steps, each with the replay as it stood at its start,
  and pairs each with the one before. `interp` draws at t = (now - close of the newer) / (close of the newer - close
  of the older) on the machine's clock, so the picture is a step behind the replay. `rerun` uses the smoothed clock
  ([step clock](#identity-pairing-and-the-step-clock-15-october-2026)).
- **Pacing and frame rate.** The app runs vsync on and presents once per pass of its main loop, so it shows one
  picture per display refresh; with `interp` and `rerun` each is a new picture at the machine's clock at that moment.
  The machine's clock is kept level with the wall clock, so t follows emulated time at the display's own rate. The
  app reads the display mode's refresh rate (60 Hz if unknown) only to size `extrapolate`'s one-frame look-ahead.
  Within a step t only advances. `--present-log FILE` records every presented picture. Headless runs through SDL's
  dummy driver say nothing about a real display.

**Picture age** (`--present-age`). `interp` shows the in-between picture of the last two logic frames: exact at every
logic frame, one step behind. `extrapolate` shows each frame from its close and moves the paired primitives on past
it as they moved from the frame before (t from 1 to 2): no step of age, but the picture between closes is a
prediction that jumps to the real frame at the next close. With `rerun`, the smoothed clock always stays behind the
newest step (`extrapolate` only reads the clock one display frame later); with `F117_PRESENT_CLOCK=raw`,
`extrapolate` gives `interp`'s prediction, not original draws. Error just
before the close (`test_interp LOG --extrapolate`), share of pixels that differ from the real frame, against holding
the frame before:

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

The prediction is further from the real frame than the old frame in most steps (191 of 265), most where the scene
turns or jumps: the original's motion between steps is not steady. So `interp` stays the default. Any lower-latency
design must keep the original's simulation and input timing untouched (never advance it faster, retime its input
reads or feed predicted poses back); latest-state presentation avoids the delay but keeps the original's motion
steps, and smooth presentation without waiting needs prediction, whose errors are as above.

**Cost** (strike route, recompiled, runner CPU time, 9 October; that day's host speed varied by a third, so only one
set compares): scan 95.4 s, replay 120.8 s, interp 148.1 s; interp at N = 2 253.2 s, N = 4 345.2 s, N = 9 645.1 s. An
in-between picture costs about 1.6 ms more at N = 2 than at N = 1, 2.9 ms at N = 4 and 7.4 ms at N = 9; at N = 9 that
is 5.2 million pixels through the palette and up to the GPU each frame, which is why the presentation is threaded.

## Fullscreen 4K/60 timing and the threaded presentation (13 October 2026)

**Where the time went.** A single-threaded N=9 strike run (fullscreen Direct3D 11, 3840x2160, 59.99 Hz, vsync) kept
every logic frame exact but had p99 24.30 ms (the checker rejects p99 above 20.833 ms): every frame in which a logic
step closed took 21-25 ms, because that frame's replay at N=9 (6.4 ms) and its copies ran in one host frame, and the
rest of the work had a median of 14.8 ms of the 16.67 ms budget (in-between picture 7.6 ms, palette conversion and
upload about 3 ms, replay 2.3 ms, machine 1.7 ms).

**The threaded presentation** (`--present interp` and `rerun`; `src/present/drawfeed.c`, `src/host/main.c`). The
machine, the observer's feed, the replay and (for `rerun`) the machine copies stay on the main thread; the picture is
drawn one host frame ahead on a worker.

- `drawlive_collect` replays the records of the open frame as they arrive (after every 1 ms machine slice) and, when
  a frame closes, stages it (the replay's start, the picture, its parsed records, its pairing with the step before)
  into a free step from a pool of five. `drawlive_publish` makes staged steps the published ones, in order, by
  pointer changes. `drawlive_update` is both, for the single-threaded path and the headless checks.
- `drawlive_plan_interp` decides what to show (the older, the newer, the replay, or an in-between picture at t) from
  the published steps and the machine; `drawlive_prepare_rerun` runs the copy; `drawlive_render` draws it on the
  worker. The worker only reads published steps; the main thread publishes only while the worker is idle (after
  joining it, before the next plan), and a collect that finds no free step waits for a publish.
- Palette conversion of the fine pixels runs on three helper threads beside the main thread; the texture upload
  stays on the main thread. File-scope scratch both threads reach is thread-local (`tls.h`).
- Cost: the picture shown was planned a frame earlier, for the machine clock then, so in-between pictures are one
  host frame (16.7 ms at 60 Hz) older. `F117_PRESENT_THREADS_OFF` runs everything on the main thread.

**Results** for the `interp` build (`tools/presentation_timing.py`, isolated, one run at a time, fresh save, dummy
audio, Direct3D 11 3840x2160 at 59.990002 Hz, vsync on, N=9):

| scene | flight pictures | interval median / p99 / max (ms) | over 25 ms | logic frames exact | final guest hash |
|---|---|---|---|---|---|
| strike to 3.5B | 7,719 | 16.6677 / 16.7092 / 17.063 | 0 | 2,062 of 2,062 | `bc8e3ae5a872405d` |
| air-to-air type 6 to 5.9B | 19,000 | 16.6675 / 16.7091 / 21.3108 | 0 | 3,679 of 3,679 | `b050542466c9a616` |
| landing to 10.0B | 50,440 | 16.6677 / 16.7090 / 17.2537 | 0 | 13,408 of 13,408 | `efe080faa364f8f4` |

All pass the checker (median within 10% of the refresh period, p99 within 125%, no interval above 150%, guest clock
within 100 ms of the host). In-between pictures are 96% of those shown; each final hash equals a headless run of the
same input. An isolated N=9 strike run after the state-driven bands (15 October) passes too (p99 16.7314 ms, worst
17.0385, same hash), and the strike flight passes pinned to four logical CPUs (p99 16.71 ms), so the thread design has
headroom on core count.

**Fullscreen pacing of `rerun`, and how the 4K60 failures were repaired (17-18 October 2026, the owner's 4K 60 Hz
screen).** Settings: the owner's `4k-rerun.bat` (`--present rerun --present-age interp`, scale 9, fullscreen,
Direct3D 11, 3840x2160 at 59.99 Hz, vsync on, sound off), flown by recorded inputs; `tools/presentation_timing.py`
checks the logged display settings (a log that changes display mid-run is rejected), intervals, interpolation and
guest drift. The checker cannot judge tearing or the look of motion; that stays the owner's eye.

| date | scene | result | cause / change |
|---|---|---|---|
| 17 Oct | strike, landing (3.3B instructions) | pass (p99 16.95 / 17.38 ms, 0.09% / 0.08% over 25 ms) | |
| 17 Oct | air-to-air type 6, short and then 5.9B | fail: p99 22.15 ms; long run p99 28.70 ms, 3.70% over 25 ms, sustained 150-270 s into the flight | CPU, not GPU: hidden profile shows main work rising from median 11.8 to 18.0 ms, copied-machine redraw 7.0 to 12.3 ms, 46% of samples in `recomp_override_step` (two linear scans of the override table) |
| 18 Oct | 15 Hz/60 MIPS type-5 pursuit, 6 minutes | fail: p99 32.93 ms, 12.95% over 25 ms, one startup rewind | same cost plus a startup playback-clock reversal |
| 18 Oct | same, after dispatch lookup + clock fix | p99 21.58 ms, 0.033% over 25 ms, no rewind: fails p99 only | override placements are now sorted with a binary lookup (1,000,000-call benchmark 501 to 27 ms); the playback clock keeps the first usable pair |
| 18 Oct | same, after observer row copies | **pass**: 21,330 pictures, p99 16.94 ms, max 21.05 ms, zero over 25 ms, drift 15.94 ms | observer snapshots/line-flush scans were 906 of 4,645 app samples; bulk row copies split at both address wraps, delta scans skip unchanged 64-byte blocks (benchmark 79 to 4 ms), span-record buffer fixed at 220 rows |
| 18 Oct | compression excerpt (230 compressed seconds) from the broader validation | **pass**: 21,565 pictures, p99 16.92 ms, max 17.85 ms, zero over 25 ms, drift 21.84 ms | constant 4K/59.99-Hz target (an attempt that switched to 1080p/50 during setup is retained as invalid) |

Every row keeps all logic frames exact and the final guest hash equal to a headless replay. Each repair also passed
the full gate, the 45-route rerun check, boot N=4/9 and the same-record copy-isolation checks; evidence is under
`D:/f117-gate/present-v2/stage4-fullscreen-20261017`, `stage4-airair-long-20261017`, `cadence15-production-20261018/`,
`4k60-fix-20261018/` and `airair-profile-20261017/` (instrumented sources, executable and matching PDB; offline symbol
resolution needs the preferred base 0x140000000 and `SymSetOptions(0)`). Window capture timed out on every fresh
selection, so visible motion is unverified; one encounter does not establish all-scene pacing. Eight was never
measured on the type-5 workload. Earlier hidden throughput: 3840x2160 D3D11 runs at 119-150 fps uncapped with
occasional ~100 ms upload stalls (`D:/f117-gate/present-v2/hud2`). Not established: the picture on a physical panel
(tearing, the look of motion at 60 and 144 Hz), cargo and career flights, and a slower CPU or weaker GPU.

**Capacity on long flights and on fewer cores (19 October 2026, hidden).** Timing only: the visual judgement stays
with the owner. A frozen private build of the app's main loop with phase timers (the 18 Oct profile harness; hidden
3840x2160 Direct3D 11 window, vsync off, a 60 Hz sleep cap, dummy audio, `--present rerun --present-age interp
--present-scale 9`, BelowNormal priority, machine load 1.1-1.4% beyond the harness) ran four whole flights once each and
one-minute windows were cut from them (about 330,000 pictures). Every full run ended on the original's guest hash. The
legacy recordings (`cargo_return`, `career_promotion`) are keyed to 9 MIPS / 8 Hz and cannot be replayed at the app
default, so the two long flights recorded at 15 Hz / 60 MIPS with all fifteen fixes (adaptive-pilot landing and strike
from `cadence15-broad-20261018/pilots`) were added; no cargo or career flight exists at the default. *Main work* is the
main thread's wall time per picture; the checker's limits are p99 20.83 ms and nothing over 25 ms.

| flight | length | per-minute median main work | worst one-minute p99 | outliers |
|---|---|---|---|---|
| cargo_return (legacy) | 25.4 min | 7.2-8.0 ms | about 11.9 ms | one 4-picture episode at 28-32 ms |
| career_promotion (legacy) | 20.7 min | 7.0-8.4 ms | about 12.5 ms | one 4-picture episode at 28-31 ms |
| landing, 15 Hz / 60 MIPS | 17.9 min | 5.2-7.2 ms | 16.34 ms (final approach) | one 104 ms texture-upload stall |
| strike, 15 Hz / 60 MIPS | 30 min | 5.9-10.2 ms | 18.4 ms (combat, 76.4-83.7 G) | one 104 ms texture-upload stall |

Medians are flat from takeoff to landing and the emulated clock never falls behind (ratio 1.000). The heaviest minute
(strike combat) has median 10.2 ms and p99 18.4 ms unpinned: about 3% of pictures over 16.67 ms, 0.1-0.2% over
20.83 ms, none over 25 ms. **Fewer cores, same window (a proxy for a slower machine, not the same as one):** 4 CPUs
median 8.75 / p99 15.6 ms; 2 CPUs 9.29 / 16.0 ms (4 pictures over 16.67 ms); 1 CPU 13.6 / 20.87 ms (954 over 16.67 ms
and 37 over 20.83 ms, 1.2%). The landing approach on one CPU is p99 18.4 ms. **Software renderer (a proxy for having no
GPU acceleration):** fails, median 30 ms and p99 43 ms, because the present call costs 17.4 ms against 0.08 ms with
Direct3D 11; the emulated clock keeps pace and only the picture rate collapses. Unpinned runs were slower than runs
pinned to a few cores (p99 18.4-18.9 against 15.6-16.1 ms); the cause is untested (frequency ramp or thread migration are
suspects), and the unpinned figure is the realistic one on this CPU. If every phase scaled with core speed, the combat
window on four cores would reach p99 16.67 ms with a core about 7% slower and 20.83 ms with one about 33% slower
(an estimate, not a measurement). Not shown: tearing, the look of motion, the real fullscreen swapchain path, other
combat densities, flights over 30 minutes, Roland, per-core speed of an older CPU and a weaker GPU's fill rate. Evidence,
commands and raw logs: `D:/f117-gate/stage4-capacity/` (`results.md`).

**Announced fullscreen long-flight run (19 October 2026, the owner's 4K screen).** The 15 Hz / 60 MIPS landing pilot
recording (17.9 minutes of guest flight, 24 minutes of wall time), `f117a.exe --present rerun --present-age interp
--present-scale 9 --scale 9 --fullscreen`, D3D11 3840x2160 at 59.99 Hz, vsync on, sound off: 64,338 flight pictures,
interval median 16.6676 ms, p99 16.9513 ms, maximum 21.4635 ms, zero missed intervals, 10 ms guest drift, 48,252
interpolation advances; all 16,088 logic frames equal to the display at their close and the final guest clock and hash
(`79698609194`, `a0f5585e5728c15f`) equal to the original's. Passes the checker unchanged. This is submitted-picture
timing: the look of the motion on the panel stays the owner's judgement. Evidence
`D:/f117-gate/stage4-capacity/display-long-landing/` (`display_long.py`, `summary.json`).

**The palette the threaded path presents (fixed 15 October 2026).** The main loop shows the worker's last picture
with that picture's palette, and then handed the same frame, palette included, to the worker for the next plan, so
the first palette the threaded path captured was carried forward for as long as pictures kept coming. Every palette
change was frozen in threaded `interp` and `rerun` at N>1: the graphics driver (MGRAPHIC, `0889:129C`) cycles entry
13, the pen of the player's gun rounds, yellow / light red / dark red / light red once a VGA frame, and the owner saw
solid dark red; fades and flashes froze the same way. The next plan now takes the machine's own capture. Headless
runs and the single-threaded path were never affected, which is why no route check saw it: 26 app frames over the
owner's gun burst showed only dark red before the fix and all three colours after (`F117_PRESENT_SHOTS`).

## Identity pairing and the step clock (15 October 2026)

The owner's recorded flight (`session-20261015-103956`, frozen privately with its draw capture in
`D:/f117-gate/present-v2/owner-roads-20261015`) showed road edges that pop and building patches that jump.
`tools/motion_check.py LOG` measures what the exactness checks cannot: the share of polygons and outline edges with no
partner in the next step (they switch at t = 0.5), how much paired batches deform between steps, held steps, and how
unevenly the steps are timed. (The road and coastline jitter itself turned out to be the original's.)

**Identity pairing (an experiment, off).** With `F117R_OBSERVE_IDENT=1` (live: `F117_PRESENT_IDENT=1`) the observer
logs a record `I` for every model request at `120A:00CF` (`scene_capture.h`): the model, the caller chain, the scene
walk's ring (`DS:E328`) and world-cell words. Batches whose model, ring, cell and vertex count are equal and unique in
both frames pair first (`interp.c` `ident_key`, mirrored in `interp_frame.py`). It halved unpaired shapes (polygons
7.78% -> 3.98%, outline edges 2.51% -> 0.78% in a 100M-clock window) with all pairs exact at both ends, but the owner
saw more jitter: the extra pairs join an object's batches that hold different visible vertices (the original drops
hidden vertices before drawing), so the object deforms instead of moving. Paired batches deforming over 100 px rose
from 3.78% to 20.53%. **Lesson:** judge pairing by deformation, not by unpaired counts; a usable identity needs
per-vertex identity.

**The step clock.** The game moves the aircraft a nearly fixed distance per logic step (mostly 46 units in that
flight) while a step takes 42-86 ms by scene cost; 24% of step boundaries change duration by more than 20%. The plain
clock sets t from the time since the newest step closed over the previous step's duration, so a longer next step
freezes the picture at t = 1 and a shorter one skips forward. Simulated over the flight's real step times at 60 Hz
(1,186 frames): 29 freezes, 6 skips and 21.4% of frames whose speed changes by more than 20%.

**The smoothed playback clock** (`rerun` only; `plan_smooth` in `drawfeed.c`). Three steps are published instead of
two. A playback position, counted in steps, advances with the machine's clock over a smoothed step duration (each new
duration weighted 0.4, `F117_PRESENT_CLOCK_ALPHA`), with its rate pulled towards one step behind the newest (0.025 per
step of error, `F117_PRESENT_CLOCK_PULL`; never below 0.2 of normal speed). It never passes the newest step nor drops
behind the oldest published one, and names the two consecutive steps it lies between. At startup, if the first usable
pair starts ahead of that position, playback starts at its older end. Missing copy snapshots use draw-list interpolation
at the same position. This avoids changing clocks and then rewinding as smoothing catches up. If no pair exists,
the plain clock plans. In simulation it gives 2 freezes, 0 skips and 1.6% (p99 41% against 429%), for about 0.3 step
(17 ms) more picture age. `F117_PRESENT_CLOCK=raw` switches it off.

**Live results** (hidden owner-flight runs; run lengths differ between sets, so compare within one): on `c7b4d77`, two
scale-2 runs, the plain clock gave 274 freezes, 12 skips and 18.4% >20% speed changes, the smoothed clock 17-21, 23-34
and 3.3-5.0% (`rerun-mode/app7`). On the gated build, one run each: defaults 6 freezes and 2.5%; alpha 0.4 / pull
0.025, 3 and 1.3%; alpha 0.4 / pull 0.05, 4 and 1.3%; picture age not measured (`clock-tune`). A private simulation
sweep (`rerun-accept/clock_sweep.py`) also suggests 0.4 / 0.025 at 30, 60 and 144 Hz. On 19 October the defaults became 0.4 / 0.025 (the owner delegated the decision). Hidden app replays of the two owner flights, two runs per setting (`D:/f117-gate/present-v2/clock-live`, frames whose speed is under 0.1 of the median count as freezes, over twice the median as skips; picture age was not measured): roads flight 0.2 / 0.05 gave 25 and 25 freezes, 15 and 16 skips, 2.4-2.6% >20% speed changes; 0.4 / 0.025 gave 14 and 15, 8 and 5, 1.3-1.9%. Sharp flight: 21 and 22 freezes, 17 and 16 skips, 3.5-4.5% against 17 and 13, 12 and 5, 3.4% and 1.9%. Skips against picture age remain to tune. Owner's A/B of the roads flight in a 960x720 window, new against old, 19 October: looked the same. Most of the freezes counted above are one block at mission start (frames 4-15 after the replay begins, 14 of 28 old and 13 of 17 new in the first run, the same in both settings); outside it the old setting had about 14 and the new about 4. **Known, accepted (owner, 19 October): the mission opens with about 7 presented frames (just over 100 ms) at the original 320x200, with the 3D view still missing from the cockpit, before the first logic step completes and the high-resolution pictures start,** because a high-resolution picture needs a completed pair of steps. It is one process switching mode (no exit or reload); a hold or fade over those frames is possible but not wanted. Captured with `F117_PRESENT_SHOTS`. The owner played rerun fullscreen at 4K on 15 October and
accepted the motion "at least for now" (apart from the original's own jitter).

**Long flight and loaded machine (20 October 2026, hidden, Phase 4 coverage notes, unscored).** The `cargo_return` route
(17.09 G clocks, a supply drop and the return, 9 MIPS as recorded) played through the real app (`--present rerun`, scale 2,
dummy SDL, software renderer) for 31.7 minutes: exit 0; 22,096 logic frames, every one equal to the display at its close;
91,109 VGA frames presented, 91,066 of them in-between pictures drawn by the original on a copy, 30 fell back to interpolation
(0.03%); the machine held its 9.0 MIPS in every five-minute window (the run took 1,899 s for 1,899 s of play: no drift);
the working set was flat at 136 MB from minute 9 to the end (no leak); outside the mission-start block there were 42 frames
whose speed was under a tenth of the median (0.05%) and 12 over twice it, spread evenly over the six windows
(`D:/f117-gate/present-v2/long-cargo/`). The owner's A/B of the roads flight looked the same for the old and new clock.

**A loaded machine (20 October 2026, hidden, unscored).** The roads flight (143 s of play at 20 MIPS) with the app pinned to
one core that one, two or three busy loops share (the app gets about 50%, 33% and 25% of the core;
`D:/f117-gate/present-v2/clock-live/share/`): with one competitor the run took 143 s at the full 19.9 MIPS and the
app presented 3,300 pictures (it presents what it can: 5,660 on an idle machine), 6 copy draws fell back; with two, 155 s,
18.4 MIPS, 1,611 pictures, 3 fallbacks; with three the machine fell behind real time (280 s for 143 s of play, 10.2 MIPS) and
presented 1,611 pictures. Every run exited 0 and ended on the same logic frames: a starved host slows the game and thins the
in-between pictures; it does not skip simulation steps, corrupt a picture or crash. A real older CPU or GPU is a physical
test this PC cannot do (Phase 4).

## Consolidate world geometry before more clipping exceptions (15 October 2026)

*Superseded as the main path the same day by [the current plan](#the-current-plan-for-60-fps-15-october-2026-owner).
The findings and the scene-input capture stay valid; this project's own scene renderer (`src/present/scene_mesh.c`,
with a synthetic CTest) is an unconnected draft, not a finished fix.*

**The rule that stands (owner):** no one-off clipping exceptions, for coastlines or anything else. A clip-only
prototype was set aside because it cannot supply faces the original never submitted or stable model identity
(`D:/f117-gate/present-v2/coast-finish`).

**Why the draw capture is not a complete scene.** VGAME `120A:00CF` takes the model index, position and orientation,
selects the detail level, sets up parts (`0317`) and planes (`0561`), then submits vertices (`06B5`), edges (`0A33`)
and faces (`0C76`). Visibility already filters those inputs (`06B5` skips vertices whose `7A02` byte is zero, `0CA8`
skips hidden faces), so adding precision to `G`/`E` cannot recover the whole model; a complete capture starts at the
model request, before those filters. A reused projected vertex address or draw order is not a persistent object ID.

**Scene-input capture** (research tool; the app never enables it). `F117R_OBSERVE_SCENE=N` with `f117run --observe`
records up to N requests: the original model, position/orientation arguments, camera/view metadata, CPU registers,
complete DS and model-bank segments and 256 stack bytes (`i` and `j` records; keep both when filtering kinds). Reads
bypass device accessors, so capturing VGA-backed addresses charges no guest bus cycles. `py tools/scene_capture.py LOG
--out JSON` reconstructs the inputs and rejects incomplete or inconsistent captures. Two paired owner replays
(interpreter against recompiled) capture 192 requests with identical bytes, metadata and clocks, and both keep the 69
checkpoints and final `31c259b992340414`. `tools/scene_model.py` decodes the original parts, planes, vertex lists,
ordering trees, vertices, edges and material commands and derives the detail selection: all 192 selections and all 191
requests that reach geometry agree with original-byte execution, as do 2,330 camera vertices, 488 plane tests and all
205 reachable detail variants in controlled fixtures. Material commands are decoded but not rendered; pose
preparation is not independent. Evidence: `D:/f117-gate/present-v2/scene`.

## Verification

The fullscreen results are in the pacing table above.

A presentation change is held to these checks (commands and costs: [repeated-processes.md](repeated-processes.md);
the latest full-gate tally: the handoff):

- **The full gate** for anything that can reach the machine (every CTest, all routes, both instruction profiles,
  matched seeds, reachability/event checks). A read-only presentation change may use `tools/present_light_check.py
  --base REV` instead: every CTest, the 45-route N=1 presentation check and boot at N=4/9; it refuses if any changed
  file is not presentation-only, and `drawfeed.c/.h` and `rerun.c/.h` (which snapshot and run machines) never are.
- **`tools/presentation_check.py`** compares complete presentation routes with the full gate's saved interpreter scan
  baselines: every guest hash, route milestone, closed logic frame and interpolation endpoint; it rejects
  dropped/unseeded frames and requires positive pairs on flight routes. All 45 N=1 routes: 259,240 closed logic frames
  and 259,166 pairs exact. `--mode rerun` adds the copy checks and requires positive original in-between draws;
  changes to `drawfeed`/`rerun` need it on every route.
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

- **`rerun` motion.** The smoothed clock still skips; its tuning against picture age, the owner's verdict on a physical
  display remains open. Parallel copies for refresh rates above 60 Hz (per-machine observer, runtime and fix state)
  moved to the unscored Phase 4 on 17 October 2026: the owner has no convenient way to test above 60 Hz.
- **`interp`.** Batch pairing covers 86-99% of painted polygons in the study windows, but unpaired shapes switch and
  wrongly paired ones deform; pairing is frozen.
- **Things that are not smooth** (explosions, tracers, discrete HUD readouts, a cut between views) are left alone:
  held, or switched on the logic frame.
- **The wall.** If the original's rasteriser depends on state not in the draw list (a clip window held in a register,
  a dither phase), the replay misses pixels; that says what else to record, not that the method fails.
- **Remaining geometry fallbacks.** One extreme clipping-arithmetic case (air-to-air), polygons whose geometry does
  not explain their footprint, zero-area span polygons, missing metadata and the HUD items not drawn from state stay
  scaled. The clipper tolerances are measured bounds, not derived limits. Widths by drawing role are not
  assigned.
- **Real display.** Refresh pacing on a physical panel, `rerun` fullscreen timing, the look of motion at 60 and
  144 Hz, fullscreen acceptance of the latest HUD batch, cargo/career flights and slower machines all remain owner
  (Phase 4) checks.
