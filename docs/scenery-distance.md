# Scenery distance

Remastered draws far scenery farther than the original, with an adjustable reach. It happens only in the Remastered render copy
(`src/present/rerun.c`, `remaster_live.c`): the game's logic, the live machine and `matched.c` are untouched, and Original and Enhanced keep
the game's own reach. The setting is **Draw distance** in the settings menu (Near, Far, Maximum, Original), applied at once;
`--draw-distance` and `draw-distance =` in `f117a.ini` take the same names or a number of world units. Default: Near.

## Why buildings pop in

The terrain is four **rings**: four grids over the one static world, each object on exactly one ring by size. Ring 4 is the coarsest (an 8 x 8
map of cells 262,144 units wide: the big landmarks); ring 1 the finest (256 x 256 cells 4,096 wide: small buildings, lit windows, the radar
dish); rings 3 and 2 are between. A ring works in its own unit, 4 times coarser each step (`0810`), so the guest's 16-bit camera-relative words
reach 4 times farther on each ring.

The game draws a terrain model only while its camera depth is under 8,191 units **of its own ring** (the range test, VGAME `120A:02B2`: it fails
when `2 * (|depth| + 1)` reaches the size word `[7D5E]`, which is 16,383 for every model in the replays checked). That is about 8,000 real units for ring 1,
32,000 for ring 2, 131,000 for ring 3 and 524,000 for ring 4. Ring-1 buildings therefore appear at about 8,000 units, while the ring-2 and ring-3
versions of the same area (dots) show beyond: groups of buildings pop in with dots already visible past them. Widening the cells the walk visits
cannot move this (the owner's session 20261103-205615, and `F117_REACH_LOG`, which lists each model's farthest drawn depth); the earlier work that did
(extra terrain walks, 3 and 4 Nov) mostly drew objects the test then rejected.

## What the copy does

An object drawn at a **coarser level** has a smaller depth in that level's units, so it passes the same test 4 times farther; its model is shrunk by
the same factor so it keeps its real size (the renderer shrinks the vertices about the object's origin, `finish_model` in `remaster_live.c`). The copy
does this for every object, once per object, in `far_scenery` (`rerun.c`), at the entry of the game's own terrain walk (the live walk then draws its
nine cells per ring as always):

1. **The world, once.** Every object of every ring is listed from the map and tile lists (position in its ring's units: cell * 4096 + 2048 + the entry's
   offsets, which is the walker's own sum), keyed on a hash of that data and indexed by cell. About 74,000 objects on the 3 Nov map.
2. **The level.** For each object in the cells within reach, the finest level (its ring or coarser, up to the setting) at which its camera depth is
   under 8,000 of that level's units and its offset fits the guest's 16-bit words. The setting is how many coarser levels are allowed: Near 1, Far 2,
   Maximum 3 (the whole map; ring 4 reaches about 524,000 units, the map is 1,048,576 across). The objects in the game's own nine cells per ring are
   left to it, where the ring is drawn at all.
3. **The cull.** The game's own camera transform and cull (`E3BF`, ported exactly: `far_cull`), with the side allowance divided by the level
   difference, since it is in the object's own units; and the darkening byte the game keeps in `DS:48C4` (`far_detail`).
4. **The pose, natively.** The first object of each model in a copy is placed through the game's own `E2B6`, which makes the renderer take a
   *template* of the game's pose state (`DS:7B28 .. 7DA0`). Every later object of that model patches only what depends on its place: the position words, the
   eye (the position negated, and for a model with one base part also through the part's matrix, `1452:021B`), the 32-bit translation (`1452:0330`) and the
   darkening byte; then the renderer builds the primitives (`remaster_scene_add`) exactly as it does for the game's own placements. Models with two or
   more parts, with shared vertices, or in orthographic views keep the game's own placement (about 3 to 4 percent of objects).

The game's placement of an extra object also skips its own face drawing (the renderer draws the model from its geometry): zero differing pixels over three
runway moments (3 Nov).

## Checked against the game

`F117_NATIVE_CHECK=1` builds each native object's primitives aside and compares them, field by field, with the game's own drawing of the same model at the
same position (paired by bank, model and position, since a deferred object is drawn later): 1.6 million objects over the runway replay, 0 differing. One
field is left out when it cannot agree: the shade, when `DS:7D8A` differs; that byte is the game's per-face fill colour, so the renderer's "base shade"
read from it depends on what the game drew just before. Diagnostics: `F117_FAR_WALKER=1` (the old extra terrain walks), `F117_WIDE_WALK_STATS=1` (cost per copy),
`F117_NATIVE_TIMING=1` (per phase), `F117_NATIVE_SIZES=1` (projected size of drawn objects), `F117_REACH_LOG=1` (the cap per model),
`F117_WIDE_DISTANCE=UNITS` (overrides the setting), `F117_WIDE_SCENERY=0` (off).

## Cost (measured)

Two numbers matter and the first measurements mixed them up. **Presented intervals** are paced to the display (60 Hz): Original reads exactly 16.7 ms, which is the cap, not what the machine can do.
**Work per frame** is what one loop iteration does before it presents (the machine's step, the copy, the composite, the upload): the column `cost_ns` of `--present-log` (added 3 Nov), and
`tools`-free to read (`private/.../p95cost.py` style: sort the column over the runway replay's clocks 3.9 to 4.8 billion; the headroom the owner asked for is this at the 95th percentile).

1920 x 1080, scale 3, **software** renderer (the owner's window uses the GPU renderer; every number here is from a headless software run on the development machine), threads on, session
20261103-154444, work per frame, ms:

| | mean | p50 | p95 | p99 |
|---|---|---|---|---|
| before the composite moved to the worker (Original / Near / Far / Maximum) | 9.0 / 10.2 / 12.2 / 34.2 | 8.5 / 9.2 / 11.7 / 38.5 | 11.5 / 14.7 / 16.5 / 44.6 | 13.4 / 16.3 / 17.6 / 48.0 |
| after, two runs (same four) | 5.9 to 6.0 / 6.5 to 6.6 / 7.8 to 8.1 / 28.6 to 29.4 | 4.6 to 6.3 / 5.7 to 6.6 / 7.3 to 7.6 / 33.5 to 34.1 | 8.9 to 10.2 / 9.4 to 10.2 / 12.4 to 12.7 / 39.1 to 39.5 | 12.5 to 13.2 / 12.9 to 13.1 / 13.8 / 41.4 to 41.9 |

So against the owner's goal (p95 under 12 ms): Original and **Near are met** (9.4 to 10.2); **Far misses by 0.4 to 0.7 ms** (12.4 to 12.7); Maximum is far off (39 ms) and needs a cheaper path for tiny strokes or the GPU.

Where the main thread's time went (F117_LOOP_TIMING, per frame, Original/Near): the machine's step 1.4 ms; the copy 3.5 to 4.6 ms (the game's own frame composition, run on the main thread: `drawlive_prepare_rerun`);
**the Remastered composite 3.6 to 4.2 ms (`remaster_live_compose`, on the main thread, serial with the copy)**; the upload 0.2 ms. The composite was the cheapest thing to move: it now runs on the present worker, right after the
worker renders the Enhanced picture (`present_worker_main`, `remaster_apply_ex(..., on_worker=1)`), and the worker is released just before the present call (`pw_release`) so the buffers the main thread uploads are not
overwritten under it. Only a picture from a rerun copy is composed there (the live machine's frame is written by the main thread meanwhile); others are composed by the main thread as before. The composite reads
the frame's own palette snapshot (`remaster_live_set_palette`, a plain variable because the composite's row helpers read it too; a thread-local one let some rows read the live machine's palette, which differs
in a fade). `F117_WORKER_COMPOSE_CHECK=1` recomputes each worker composite on the main thread from the same inputs and compares every output byte: 0 differing of 800 (Near) and 600 (Far) composites after the
palette fix (3 of 400 before it). `F117_NO_WORKER_COMPOSE=1` returns to the old arrangement.

The copy itself (profile, `F117_PROFILE=1`, per copy at Near, 4.4 ms): copy start to the terrain walk 0.7 ms (189,000 original instructions), the far scenery 0.8 ms (26,000), the game's own terrain walk and scene 2.2 ms
(437,000), the HUD pass to the end 0.6 ms (3.3 million instructions, which is the game's frame drawing). One native far object costs about 0.5 microseconds (the renderer's mesh cache is keyed on the detail level and keeps
the bank signature; it was about 7 before and about 35 through the game's own placement); `F117_WIDE_WALK_STATS=1` prints the far scenery's choosing and placing times, `F117_NATIVE_TIMING=1` the phases of one native object.

At Maximum, a 1920 × 1080 software-rendered replay over clocks 3.9–4.8
billion measured 41.02 ms p95 before stroke changes. Bypassing row-edge
intersection for tiny stroke triangles measured 40.90 ms. That experiment
was removed. Rejecting strokes whose padded endpoints lie outside a
worker's row band or the canvas measured 40.58 ms. The paired runs had
449–454 presented frames. All three builds produced the same frame at
clock 3,900,000,000. The early rejection leaves the existing raster and
depth rules in place for all strokes that can reach the worker's rows.
These single-run timing differences are insufficient to establish a
repeatable end-to-end gain.

A separate Maximum profile of the same window measured 41.45 ms p95 with
default rows and 50.58 ms with 16-row tiles. The checkpoint matched in both
runs. During the expensive part of the default run, the copy averaged
31–33 ms per frame over successive log intervals and worker waits were
under 1 ms. At 300 copies the far stage averaged 2.15 ms selecting and
11.58 ms placing 9,693 survivors. Of those, 1,402 still used game placement.
Classify those fallbacks and time them before treating the compositor as
the only bottleneck. The ground-classification early exit was pixel-exact
at the checkpoint but measured 42.33 ms p95 and was removed.

With fallback timing enabled at Maximum, the first 300 copies averaged
5.72 ms in game placement out of 11.86 ms spent placing terrain. By
3.2 million native placements the collector had rejected 1,490,025
templates as stale, versus 668 absent templates and zero cached
unsupported templates. The first six stale reports all showed a changed
matrix with unchanged light. The diagnostic build matched the baseline
frame at clock 3,900,000,000. Trace the producer of DS:7B28 and its
relationship to the terrain camera before broadening template reuse.
The timings are available through F117_WIDE_WALK_STATS and the rejection
counts through F117_NATIVE_TIMING.

The terrain-walk camera at DS:49BE is the culling matrix. Model draw
120A:00CF separately uses the prepared projection matrix at DS:7B28.
Using 49BE as the template key raised stale rejections from 109,124 to
125,314 by 540,000 successful native placements in the seek to 4.7
billion. Refreshing only the projection matrix for templates with zero
parts gave the baseline count of 109,124. Both experiments matched the
baseline pixels at 4,700,000,001 and were removed. The single-base-part
path at 120A:0317 composes the current projection and the base
orientation into DS:7B4C and writes the orientation transpose at DS:7B5E. A refresh
for those templates needs to reproduce that setup and check it against
the guest pose before reuse.

The opt-in F117_NATIVE_REFRESH_PART experiment reuses that orientation
transpose to rebuild the single-part projection matrix from the current
DS:7B28. It keeps the light check and uses the game's 1.15 fixed-point
matrix product, including wraparound. At the 4.7 billion checkpoint,
the baseline, the refresh and the guest comparison run produced the
same pixels. Through 680,000 native placements the refresh logged zero
stale rejections. The guest comparison reported 460,000 matches,
zero differences and 213,820 candidates without a matching guest draw.
The refresh also matched the baseline at clocks 4,000,000,000,
4,400,000,001 and 4,800,000,000 in single-threaded paired captures.
The unmatched candidates and broader replay parity still need checking.
A separate Maximum software replay over clocks 3.9–4.8 billion measured
19.04 ms p95 over 682 frames. At 300 copies, placement averaged 5.97 ms,
with about two guest placements per copy taking 0.08 ms. The earlier
baseline measured 40.58 ms p95. The refresh remains experimental.

The default decal renderer now rasterizes strokes once. The former two
passes used the same depth and ordering rules. The legacy F117_LINES=old
diagnostic still uses its separate whole-stroke pass. With the single-part
refresh enabled in both Maximum builds, the paired runway runs measured
19.25 and 18.54 ms p95 over 679 and 738 frames. At the 720-composite
profile report, mean line time fell from 5.56 to 2.46 ms. The frame at
3,900,000,000 was pixel-identical. Seven focused renderer tests passed.

With both experimental switches enabled and the duplicate stroke pass
removed, the 1920 × 1080 software runway replay measured 7.88 ms p95 at
Near, 8.46 ms at Far and 17.94 ms at Maximum. These were single runs with
902, 902 and 769 frames respectively. Maximum's far-terrain stage averaged
8.13 ms at the 600-copy report. During the costly stretch the copy
averaged 11.7–11.9 ms and worker waits averaged under 1.1 ms. The target
still needs final parity and timing checks with the intended renderer.

Caching the model word and class during each selection pass avoids
reading the guest tables for each candidate ring. Each object resolves
its override once. A paired Maximum run with both experimental switches
enabled measured 17.86 ms before and 17.69 ms after (764 and 771 frames).
At the 600-copy report, mean selection fell from 2.19 to 2.10 ms.
The frame at 3,900,000,000 matched byte for byte. Seven focused tests passed.

The scene pose reader now bulk-copies setting and material words on
little-endian hosts and splits copies at the guest segment boundary.
The scalar reader remains available on other hosts. The regression covers
an odd table address with a word split across DS:FFFF. At Maximum with
both experimental terrain switches, the software replay measured 16.25
ms p95 over 828 frames, compared with 18.38 ms in the instrumented
baseline. Mean native scene add fell from 0.45 to 0.25 microseconds per
object. The frame at 3,900,000,000 matched byte for byte. Seven focused
tests passed. A fused vertex finishing loop was pixel-identical there
but changed p95 from 18.38 to 18.42 ms and was removed.

Native primitive emission now initializes its metadata and writes only
the vertex slots it uses. The projected flags alone cannot drive vertex
omission because material emission can reference other present vertices.
In the paired 1920 × 1080 Maximum replay with both experimental terrain
switches, p95 fell from 16.39 to 15.87 ms over 824 and 844 frames. Mean
native scene add fell from 0.26 to 0.23 microseconds per object.
The frame at 3,900,000,000 matched byte for byte. Seven focused tests passed.

Native placements reuse each ring's camera correction while the eye
and all projection matrix words match. With both experimental terrain
switches, Maximum p95 was 15.98 ms before and 15.74 ms after (841 and
852 frames). Mean per-object finishing time fell from 0.12 to 0.08
microseconds. The 3,900,000,000 checkpoint matched byte for byte and
seven focused tests passed.

Far terrain selection now calculates the game's wrapped camera depth
before resolving an object's model or transforming the other axes. This
keeps the same ring-selection rule. The Maximum software replay with
both experimental terrain switches measured 15.16 ms p95 before and
14.50 ms after (864 and 880 frames). Mean selection at the 600-copy
report fell from 2.10 to 1.88 ms. The 3,900,000,000 checkpoint matched
byte for byte and seven focused tests passed.

A Maximum profile with both experimental terrain switches measured
14.85 ms p95. Far terrain averaged 5.56 ms in the latest copy profile.
During the costly stretch the render copy averaged up to 9.70 ms and
worker waits up to 0.90 ms. The next Maximum acceleration needs to
reduce render-copy work as well as any compositor work.

Within each ring, terrain picks now sort by integer squared distance.
The previous square root and ring scale gave the same order within
that ring. In the paired Maximum software run with both experimental
switches, selection fell from 1.88 to 1.71 ms at the 600-copy report.
Full-frame p95 moved from 14.41 to 14.30 ms (888 and 893 frames).
The checkpoint at 3,900,000,000 matched byte for byte. Seven focused
tests passed.

The pose and geometry evaluators now clear their validity flags and
write only defined entries. Callers use camera coordinates only for
present vertices and shade and visibility only for actual mesh planes.
The paired Maximum replay with both experimental switches measured
15.03 ms before and 14.34 ms after (874 and 887 frames). Mean scene add
fell from 0.23 to 0.18 microseconds per object. The 3,900,000,000
checkpoint matched byte for byte and seven focused tests passed.

With both experimental switches enabled, the banked flight in session
20261103-235538 matched the flags-off build at clocks 11,024,560,842
and 12,281,642,382. These are fixed-clock single-threaded Maximum
captures. They extend coverage beyond the runway approach.

Small single-part meshes without shared vertices now cache local
camera-space offsets by decoded mesh and the nine part matrix words.
Each instance still adds its own translation with the same 32-bit
wraparound. With both experimental terrain switches, paired Maximum
p95 measured 15.15 ms before and 14.60 ms after (875 and 886 frames).
Mean scene add was 0.20 and 0.19 microseconds per object respectively.
The 3,900,000,000 checkpoint matched byte for byte and seven focused
tests passed.

A per-mesh plane shade cache matched the 3,900,000,000 checkpoint.
In the paired Maximum replay with both experimental switches enabled,
native scene add rose from 0.24 to 0.30 microseconds per object while
p95 varied from 17.41 to 17.11 ms. The cache was removed because it
increased the cost it was meant to reduce.

The placement oracle now keys candidates by bank, model, position,
scale and shrink, and expires pending comparisons before the next
render copy. The Maximum comparison through 4,700,000,001 reported
1,785,000 same-copy matches and zero differences at the last periodic
report. It separately counted 71,784 expired entries and 777,541
evictions from the 256-entry queue. Evictions do not establish whether
the game drew a candidate. Remove that diagnostic capacity limit or
account for candidates at guest placement return before treating
unmatched objects as culls. Seven focused tests passed.

The oracle queue now grows on demand and never evicts a candidate.
In the same Maximum seek the last periodic report showed 1,785,000
matches, zero differences and 842,479 expired candidates, all with
native geometry. The remaining gap is a candidate for guest-side
range or visibility tracing. It must not be counted as parity or
assumed to be harmless. No production renderer behavior changed.

The opt-in oracle now also observes guest terrain placement return.
At 2,600,000 checked returns it classified 826,118 as a model request
that never reached face capture, with zero captured-but-unpaired,
no-request, deferred, or failed-return cases. The comparison again
reported 1,785,000 matches and zero differences at its last interval.
Trace the original 120A:02B2 model-range gate next. These counts show
where the draws disappear, without yet proving the exact rejection
predicate. Seven focused renderer tests passed.

The opt-in placement trace mirrors VGAME 120A:02B2 against the game's
prepared translation and actual detail record. At 2,600,000 returns,
all 826,118 requests that failed to reach face capture were rejected
by the mirror, with zero accepted or unknown in that group. This
validates the diagnosis for the sampled missing draws. Before using
the predicate to omit geometry, predict it from the native pose and
verify both rejected and accepted outcomes against guest placement.
Seven focused tests passed. This diagnostic does not cull production
geometry.

The range oracle now predicts 120A:02B2 from the patched native pose
before running the independent guest placement. The Maximum seek
reported 2,600,000 agreements with zero false rejections, false
acceptances or unknowns. The optional F117_NATIVE_RANGE_CULL path
uses the predicate before native geometry generation. Oracle mode
still executes guest placement independently, regardless of that
switch. The predicate uses the guest detail record in native ring
units before Remastered selects visual detail by world distance.
With the other two experimental switches enabled, the 1920 × 1080
software replay measured 13.08 ms p95 over 890 frames. The 600-copy
report averaged 2.09 ms for placement. The 3,900,000,000 checkpoint
matched byte for byte. Seven focused tests passed.

With the range gate toggled and both other experimental switches
held on, all three fixed-clock Maximum captures matched byte for
byte. The runway checkpoint was 4,800,000,000. The separate banked
flight checkpoints were 11,024,560,842 and 12,281,642,382. The
diagnostic pose snapshot now runs only in oracle mode, after the
native pose has been prepared and before the game independently
draws the candidate.

## Worker count and runway repeatability

The 1920 × 1080 software runway benchmark used session 20261103-154444,
clocks 3.9–4.8 billion, and all three experimental switches
(`F117_NATIVE_SKIP_OWN_FACES`, `F117_NATIVE_REFRESH_PART`, and
`F117_NATIVE_RANGE_CULL`). With profiling enabled, Maximum measured
12.75 ms p95 at four workers and 11.60 ms at eight. In a separate
accuracy run, the eight-worker composite matched the single-worker
composite on all 556 checked pictures.

With profiling off, Near and Far measured 10.61 and 10.56 ms p95 at
four workers. The two eight-worker Maximum runs measured 11.46 and
13.03 ms p95. The four runs contained 901–903 rendered frames each,
and their full game memory dumps matched byte for byte at clock
4,800,000,000. The Maximum timing still crosses the target between
runs. Keep the worker count and native options explicit while
investigating the slow-frame tail and validating against guest output.


The combined switches produced identical final captures and game memory
at clocks 3.9, 4.2 and 4.5 billion. At 4.8 billion, seeking directly
to the checkpoint captured a 320 × 200 fallback frame. A one-second
lead-in produced full 1280 × 600 captures, but the all-off control
itself gave different images on two runs with synchronous presentation
and the same clock, step pair (878/879) and interpolation fraction
(0.193555). That late checkpoint cannot establish switch parity
until the source of capture variation is isolated.


The opt-in `F117_REPLAY_FIXED_CADENCE=1` advances one 60 Hz clock step
per replay iteration after the seek. With `F117_PRESENT_HASH_FROM=3900000000`,
both eight-worker Maximum runs produced 901 matching clock, image-size
and RGB checksum entries through clock 4.8 billion, with all three
experimental switches off versus on. The full game memory dumps at the
final clock matched byte for byte. The checksums are for the composed
image before SDL scaling. Run performance benchmarks with hashing off.


## Skip duplicate terrain faces in the render copy (experimental, off)

`F117_NATIVE_SKIP_OWN_FACES=1` keeps the game's terrain walk and prepared
poses. After the native collector captures a terrain model, the private
render copy skips the game's face rasterizer for that model. The live
collector still runs the game's draw code. This uses the existing face
bypass for extra placements. Uncaptured models still take the guest path.

In the 1920 × 1080 software-rendered Far benchmark for runway session
20261103-154444 (clocks 3.9–4.8 billion), the two baselines measured 10.88
and 12.77 ms p95. The two candidate runs measured 12.09 and 11.75 ms,
with 901–902 frames per run. The profile's stretch from far scenery to the
HUD fell from 2.30 ms to 1.74–1.75 ms, about 118,000 fewer guest
instructions in that stretch. The full-frame p95 still straddles 12 ms.

At clocks 4,000,000,000, 4,400,000,001 and 4,700,000,001, the raw native
rasters and the final composed frames were pixel-identical in paired
captures. These three samples used one presentation thread. Verify
additional moving and banked scenes before enabling the bypass by default.
The compositor uses the original outline footprint as a fallback for
some strokes, so compare final frames as well as captured geometry.

## Native terrain walk (experimental, off)

`F117_NATIVE_WALK=1` skips the game's own terrain walk in the copy and places its nine cells a ring through the same native path (and the game's override table, `far_override`): the copy falls from 4.4 to 2.7 ms at Near.
It is **not exact**: the picture differs from the game's walk by 2,770, 1,998 and 277 pixels at the three runway clocks 4.0, 4.4 and 4.7 billion, mostly a strip along the horizon where the original draws sea (12, 28, 85)
and the native walk draws ground (36, 28, 11). Tried and ruled out, each leaving the three counts unchanged: the override table, deferral of the game's own cells, draw order by ring, the game's whole-cell cull
(`E3AC` on each cell's centre, with the game's own far plane and allowances). Not yet looked at: what else the walk leaves behind (`DS:0504`, `49AC..49B0`, `950A`), per-object comparison of what each path draws (log both,
diff the sets), whether the sea is a flat object whose order against the coast depends on the walk's step order (the walk visits a sector table's cells in a fixed order; mine sorts by level and distance).

At clock 4,000,000,000 in runway session 20261103-154444, the guest walk
submitted 1,114 primitives and the experimental native walk submitted 991.
In the projection probe covering x 0–320 and y 42–105, all 515 native
records matched the guest records as a multiset of model, primitive kind,
vertex count, material, projected bounds and first vertex depth. The guest
path had 61 additional records in that region, including model 39 and 41
lights. Investigate omitted placements before changing depth or draw order.
The original walk has a ring-4 path at VGAME 0000:0725 that submits a tile's
first entry separately. It also has a different step limit in some views.
The nine-cell shortcut needs to account for both branches.

A frame-local cache of the cull matrix and frustum words did not establish
a Far speedup. In an A/B/B/A run over clocks 3.9–4.8 billion at 1920 × 1080,
the baseline p95 values were 12.56 and 11.31 ms. The candidate measured
12.68 and 10.40 ms. Each run included 902 frames. The experiment was
removed.

## Level of detail

The game picks a model's level of detail from its depth. An object drawn at a coarser scale is at a depth 4 times smaller in that scale's units, so it got a finer level than the game would at the real distance, and the
detail dropped at the moment the object switched back to its own scale (the owner saw a group of lights thin out and come back as they flew closer). `remaster_scene_lod_shift` now scales the depth used for the level by the
same factor, so the level follows the real distance (monotonic). The level is not worth keeping for speed (a few thousand polygons); it is worth keeping for stability (tiny lit windows drawn in full detail crawl). A "LOD
bias" setting (finer detail farther out) is easy and would bring the shimmer back; not built. The remaining 1-pixel light shimmer is aliasing: a coverage fade for sub-pixel points (lit ones with a floor) is the modern fix, not built, the owner's call.

## Limits and what is not shown

- Beyond about 524,000 units nothing small is drawn (ring 4's reach); that is half the map.
- Multi-part and shared-vertex models (a few percent of objects, for instance a spinning radar dish) still go through the game's placement every copy.
- Maximum is renderer-bound: the lines stage draws thousands of tiny strokes. A cheaper path for strokes under a pixel is possible and not built.
- The deferred-draw list: the far scenery used to fill the game's 50-entry list (full in 116 of 1,392 copies of the owner's banking flight), dropping its nearest entries (lights and buildings flickered while banking). While the
  far scenery is placed the defer bit of the classes it places is cleared and restored after; the list is empty after it in all 1,484 copies of that flight (`F117_FAR_TRACE=FROM` prints it).
- No moving-sequence capture of a building appearing earlier; the evidence is the placement and reach logs and the oracle. The owner judges Near, Far and Maximum, the banking flicker, and the lights (3 Nov late: the owner reported
  lights becoming tiny flickery points far away, groups thinning then thickening on approach, and some buildings still popping in at a distance; the LOD fix above answers the second; the third is Near's reach, one step: Far is two).

## History

- 3 Nov, earlier: a shifted three-cell block over ring 4, then rings 2 and 3, then explicit cells fed to the game's walker out to its 16-bit limit, with a
  coarse-scale pass (ring-1 objects through ring 2's scale via merged tile lists in scratch memory). Replaced by the native path above: the walker's
  16-bit cell offsets, its per-cell and per-object overhead, and the scratch-memory trick are gone. Still there behind `F117_FAR_WALKER=1`.
- Negative controls kept for the record: a native view-cone filter on cells was not exact (objects extend across many cells); taxi/airfield samples with the
  first walk were pixel-identical (it had no hook slot).

## Native terrain options on by default (4 November 2026)

`F117_NATIVE_SKIP_OWN_FACES`, `F117_NATIVE_REFRESH_PART` and `F117_NATIVE_RANGE_CULL` are now on unless set to `0`
(`env_default_on` in `remaster_live.c`). Parity: Maximum, eight workers, `F117_REPLAY_FIXED_CADENCE=1`, all three forced
off versus the new defaults: the composed-image checksums of all 901 pictures from clock 3.9 billion are identical.
Timing (1920 x 1080, software renderer, runway session 20261103-154444, clocks 3.9 to 4.8 billion, work per frame, p95 ms;
replayed with `--config` of the ONTests ini, because without its fix list the replay crashes at about clock 3.54 billion):

| | Near | Far | Maximum |
|---|---|---|---|
| switches off | 9.12 | 10.73 | 38.48 |
| new defaults, run 1 / run 2 | 8.43 / 8.36 | 12.62 / 8.53 | 10.07 / 10.77 |

Far run 1 missed 12 ms once and its repeat did not; treat Far as met but noisy. This is a software-renderer proxy,
not the owner's GPU window, and the native renderer goal is still open (the copy still runs the game's frame composition).
