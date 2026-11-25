# Renderer motion audit (20 November 2026)

R45 is a picture fault. The owner reports that it is worst in flight, especially
banking, climbing and diving. Compare motion with the default renderer; a still
picture or a count of pixels toggling back is insufficient. The owner asked us to
inspect it directly, using onion skins and heat maps, rather than request more
visual diagnosis from him.

## Method

Existing fixed-cadence sequences cover runway clocks 1.645 and 3.886 billion and
the late flight's banks at 7.874 and 9.336 billion. Each contains 90 consecutive
pictures at half resolution, from a 4K window. D is default, S adds Stage 2,
H adds hardware rasterisation. Analysis and fresh experimental captures are kept
privately in `private/r45/`.

The capture filename is the loop's machine clock, not the time of the displayed
picture. Search temporal offsets before comparing images: both moving sequences
match the default best at -2 pictures with the tested pipeline and delay settings.
This alignment is for diagnosis, not an acceptance of that display delay.

Use magenta/green onion skins, spatial difference maps, and temporal acceleration
maps. Exclude the cockpit and HUD when comparing scenery. A temporal acceleration
map can locate a fault but cannot judge overall quality: normal edges, aliasing
and different pixel coverage also contribute. Track individual features as well.

## Light pairing and freezing

The runway flight at 3.886 billion shows rows of lights moving smoothly in D but
forming a sawtooth in S. An x-versus-time strip exposes the repeated holds and
jumps, which the previous toggle counter missed.

There are two causes in this sample:

* `stage2_blend_part` freezes a line/dot when its displacement exceeds 1.5 game
  pixels. Disabling that with `F117_STAGE2_LIGHT_MAX=1000000000` improves the
  movement of the leftmost tracked light but leaves other lights weaving.
* Pairing uses model, face, kind, vertex count and colour. Many different edges
  emitted by one material command share all of those. Nearest-edge matching can
  therefore move a light towards its neighbour and switch back at a step boundary.

Experimental `F117_STAGE2_MESH_KEY=1` includes the material command offset and the
edge index in the pairing key. Metadata is generated before visibility/filtering;
it survives native batch commits and scene copies. The flag also bypasses the
light-displacement freeze. It is off by default and changes no game state.

Across 80 pictures of the tracked leftmost light (half-resolution pixels):

| Path | Largest forward step | Mean absolute second difference |
|---|---:|---:|
| Default | 2 | 0.205 |
| Stage 2 before | 3 | 1.346 |
| Freeze disabled alone | 4 | 0.372 |
| Mesh key and no freeze | 2 | 0.154 |

The mesh-key strip removes the weaving as well as the holds in this sampled row.
This is evidence of a specific repair, not a claim that all in-flight jitter is
fixed. The bank at 7.874 billion still has differences against D: the whole-margin
mean colour residual rises from 0.209 to 0.275 levels with the mesh key, despite
the runway track improvement. Inspect motion and identities rather than declaring
that aggregate image error a pass. Serial/parallel pairing agreed in all 380
reported late-flight pairings. That run produced 42 scene frame hashes and ended
at clock 8.074 billion with machine hash `49271428ff50b9ed`.

## Hardware sky seams

At runway clock 1.645 billion, H has thin dark horizontal lines absent from S/D.
At half-resolution pixel (1000,213), D is RGB (81,38,95), H is (40,19,47), with
the neighbouring rows correct: this is a coverage gap, not a gradient-rounding
difference. A second line is visible near row 227.

The wide sky strips extend tens of thousands of pixels outside the canvas. New
experimental `F117_HW_CLIP_BANDS=1` clips shaded sky/ground polygons in double
precision to the viewport before conversion to hardware coordinates, carrying
their unrounded gradient colours through clipping. It is off by default.

A fresh 90-picture runway capture with this switch restores (1000,213) exactly
to (81,38,95), and both dark lines disappear in the compared crops. This is not
yet acceptance for banked/climbing/diving views. No performance claim is made.

## R46: parked aircraft beside the tower

The owner clarified that this is the parked aircraft left of the runway, near the
tower. Default-renderer scout pictures reproduce it: visible at clock
2,351,016,397, absent at 2,359,016,397, well inside the left margin. A second
aircraft by the tower stays visible. The disappearing aircraft is model 3; the
nearer request at position (636,-95,1020) disappears while the farther request
near (636,-95,2044) continues. Investigate the aircraft point test's vertical cut
as well as its horizontal cone; the terrain margin allowance is not that test.

## Validation status

The full gate passed: 845/845 unit tests, every route identical, both instruction
timing profiles (5,764,032 states each) and all three matched-routine seeds
(1,657 routines, about 6.99 million states each), zero mismatches. Additional
presentation checks passed 63/63 routes at N=1 (260,613 exact frames / 260,527
pairs) and boot N=4/9 (736 frames / 732 pairs); the runner did not change.
Host integration passed 12/12 and its evidence matches the executable.

A fresh 48-picture bank capture at 9.336 billion, with both experimental switches,
does **not** establish an overall motion pass. Its margin residual is higher
than the previous H capture (0.182 versus 0.055 mean levels in the sampled mask).
Scene frame hashes also show skipped step captures during this loaded run; the
existing Stage 2 fallback can extrapolate, clamp at t=1.5, then hold a scene.
Use `F117_STAGE2_TRACE` to distinguish stale/extrapolated scenes from pairing
errors in the next screen. The displayed plan's timestamps alone do not identify
which scene that fallback used. Do not dismiss the differences as PC load: the
native path must maintain its motion under normal use.

Neither experimental switch is enabled by default. R45 remains open. The R46
vertical-cull experiment and per-aircraft point-test logging are being built in
the isolated `codex/r46-aircraft-cull` worktree, not in the gated main tree.

A traced replay at 9.336 billion produced 101 Stage 2 decisions. Of 19
extrapolations, 17 occurred with both scenes available and two with a missing
scene. No held frames were logged. A candidate that polled for completed
pairings again immediately before scene selection passed 42 host and
presentation tests but did not pass the replay screen. It extrapolated on
26 of 101 decisions, 25 with both scenes available. The candidate was reverted.
Collect scene publication and pairing launch/completion times together next
to distinguish insufficient lead time from the cost of building the pairs.

A follow-up replay with both event and selection traces located the deadline
miss. It extrapolated three times (steps 2084, 2085 and 2087). All three scene
copies took three display iterations to finish and published on the very
iteration that first needed their new pairing. Pairing started immediately
after publication and finished 6.43 to 7.34 ms later. In the preceding
two-iteration cases the pair was already ready by the first selection.
The renderer needs the scene ready earlier, or needs reusable pairing work
prepared before its final scene is published. Tightening the display loop's
polling alone cannot recover that lead time. The earlier runs' higher miss
counts also show why a single capture is insufficient for acceptance.

A controlled replay with the existing asynchronous copy thread and the same
bank settings reported 101/101 proper blend decisions, versus 98/101 with
sliced copies in the event-trace run. The 44 aligned pictures had identical
margin residual scores against the default (0.05965 spatial, 0.06842 temporal).
The final machine hashes differed at the identical stop clock 9,436,000,000:
sliced ff17cf5012db1926, threaded 513e772390302dc8. This is a failed parity
screen, not evidence to enable the copy thread. Reproduce and locate the first
machine-state divergence before treating threaded deadline gains as usable.

Live replay checkpoints reproduced the machine-state divergence. With
F117_REPLAY_HASH_EVERY=100000000, the sliced and threaded runs shared 95
clock samples. The first 89 matched. The first mismatch was 9,000,009,691,
after both matched at 8,900,009,691 and after scene composition resumed at
8,936,000,000. The final sliced hash repeated exactly (ff17cf5012db1926).
The threaded hash differed both from sliced and from the earlier threaded
run (9e1239d952f44062 on repeat). This rules out a shutdown-only mismatch.
The next parity probe should compare live memory, CPU, I/O and scanout
separately within that window.

An aligned 46-picture temporal residual map shows the remaining visible
differences concentrated on the sloping horizon, small lights below it,
and the HUD. The analysis subtracts the default at each matched game time,
then measures how the residual changes from picture to picture.

The tighter replay probe compared 71 live checkpoints from clocks
8,935,009,691 through 9,005,009,691. All RAM hashes, IP, flags, and the two
logged scanout counters matched. The I/O accumulator first diverged at
8,954,009,691 and differed at 52 checkpoints. It folds port writes (including
their instruction clock) and DOS write bytes, so a shift in the clock of a
port write can change it without changing RAM. Other CPU registers and scanout
buffers were not separately fingerprinted in this probe.

The runtime audit found a shared verification cache keyed by module generation
rather than by the memory being verified. The snapshot thread could therefore
trust a live-memory verdict or publish a snapshot verdict for live execution.
Snapshot writes also bumped the live generation. The experimental repair
gives the existing copy thread a snapshot mode: verify translated regions
directly against private bytes, skip live invalidation and dispatch counters,
and avoid creating or dropping floating instances. This closes the identified
cache path, but parity and copy cost must be screened before crediting the fix.

The direct-verification snapshot candidate built and ran to completion,
but failed the narrowed parity screen. Over 71 identical checkpoint clocks
the first I/O mismatch was still at 8,954,009,691 (52 differing samples),
and RAM now differed at four samples. Final machine hashes at 9,005,000,000
were 5e6eb14eb9a9fc24 (sliced) and 27ea0de14676edf9 (candidate).
The candidate was reverted. The identified cache sharing alone does not
explain the divergence. Trace the live I/O events around the first
mismatch before choosing the next runtime change.

The host-only port trace reproduced the divergence with 179 writes in each
mode. The first mismatch was write 154, a MIDI byte E7 to port 0330 from
058D:0C82. The sliced run began the write at 8,957,137,883 and the threaded
run at 8,957,135,913, 1,970 instruction clocks earlier. The next three bytes
(65, 53, B7) had the same offset and values. The bus delay was still 43 clocks.
The immediately preceding two PIC writes and their cumulative I/O hashes
matched. This narrows the observed divergence to when the MIDI burst begins.
The trace covers CPU output callbacks, not BIOS calls that invoke pc_io_write
directly, or DOS file writes.

The owner's 20 Nov 14:28 runs (menus 2 and 3 with both earlier fixes) still showed motion that
advances and then steps back. The present log's (older + t) is the plan, not the drawn picture, so
F117_STAGE2_TRACE was replayed on the owner's runway session (clocks 3.22 to 3.47 billion). Only 1 to
24% of pictures had their own pairing; the rest extrapolated (clamped at t 1.5) or held the newest
pairing, which lies at least half a step behind the last extrapolation. The drawn position went back
half a step 6 to 44 times in 251 pictures (private/r45/s2_score.py). Event times showed why: a step's
scene was usable about six display frames after the step ended and was needed after about three. The
copy is about 7 ms of work, but at 4K its adaptive slices got the 1.5 ms minimum, the next copy started
a frame after the last finished, and a 0.3 ms pairing was noticed a frame late.

F117_STAGE2_EAGER (off by default) takes bits: 1 starts the next copy in the same frame, 2 gives the
needed copy a larger slice (F117_STAGE2_EAGER_SLICE), 4 waits up to 3 ms for the needed pairing.
Value 5 gave 69 to 71% own pairing, no held pictures and no backward steps in three runs, with 0, 0
and 2 frame gaps over 20 ms (baseline 0). Value 7 reached 100% but made one frame per step late (62 of
251) even with a 4 ms slice. The remaining misses are a step's first picture, extrapolated an eighth of
a step. Reaching 100% within the latency budget needs the copy off the main thread.

The owner's 15:03 look with EAGER=5: better; menu 2 still some scrub jitter, menu 3 none seen (the
runway stripes flicker there); in both, lights, some lines and edges still go wrong when banking.
F117_BLEND_CHECK with F117_STAGE2_TRUTH=1 judges Stage 2's own pairing and blend of the copies at
t = 0 and 1 against the copy at t (default renderer, owner's runway bank, 210 pictures, game pixels;
one is nine screen pixels at 4K). Paired lights: 61.5% within 0.5, 6.7% off by 4 to 16, 0.2% by 16 or
more. Unpaired lights (8% of lights, the fast movers): 7.2% off by 16 or more; unpaired polygons 4.1%.
Along each paired primitive's own a-to-b path the truth's median place follows t on average (0.119 at
0.127, 0.372 at 0.377, 0.631 at 0.624, 0.880 at 0.877), but within one picture the quartiles spread
widely (at t 0.377 about 0.21 to 0.38). The camera's turn and travel inside a step do not share one
time profile, so no single screen-space blend (or remap of t) can follow both; with wrong partners and
unpaired snaps this is what banks show. Scenery that does not move in the world could instead be
reprojected: back to world space with step a's camera, forward with the camera at t, no pairing.

Reprojection (F117_STAGE2_REPROJECT=1, off by default). Each copy now keeps the view matrix at DS:7B28
and the attitude at DS:2DEE..2DF2 from its first model request (one matrix per frame in 1,900 frames).
Fitted on 714 matrices: M = R diag(1, ys, 1) with R = diag(1, -1, -1) Ry(heading) Rx(pitch) Rz(roll),
ys about 0.75, worst element error 0.0009; a camera-space vertex is v = M^T w. A blended picture takes
each scenery primitive of the nearer step back to world directions with that step's camera, moves it by
the camera's travel (t of the way; the median of w_a - w_b over paired primitives per depth scale,
computed on the pairing thread), and forward with the camera at t (each angle moved as the rerun moves
it). A pair whose own travel differs from the median by more than the rounding tolerance but less than
a quarter terrain cell (2^18 in these units) is treated as moving and blended in world space. HUD and
flat overlays keep the screen blend.

The offline model check exposed the wrong partners: within a depth scale the per-pair travel clusters
at the true travel and at offsets of one terrain cell (2^20 units) in x or z, repeated lights (models
37 and 39) paired with the same light in the next cell during fast banks. Against the default
renderer's copy at t (owner's runway bank, 210 pictures): paired lights within 0.5 game pixels 61.5% to
90.8%, off by 4 or more 6.9% to 1.0%; paired polygons 61.3% to 88.8%; unpaired lights 13.9% to 61.9%.
By depth scale 4, 16 and 64: 97.6 to 98.7% within 0.5. Scale 1 (the nearest ring) is at 71.6%; its
worst cases are primitives at depths 10 to 120 with vertices far off the screen, and a few near lights
whose place in the reference copy is not linear in t. Live (EAGER=5): blend 0.56 ms a picture, pairing
0.7 to 1.0 ms (was 0.3), own pairing 60 to 71% across three runs (host timing varies).

Owner's 16:07 look (menus 1, 2, 3 with reprojection) and the comparison against the default. Capture
harness: private/r45/cmp.sh (every picture of a 120-picture window at the same clocks in each mode,
half size, on D:), cmp_tags.py and cmp_analyze.py. The default against a second default capture:
0 differing pixels and 0 flips in six windows, so the comparison is exact. Findings:
- With reprojection the ground in the left margin went blue in the bank (100% of the margin wrong in
  6 pictures): a primitive clipped at its own step's near plane has an edge that moved with another
  camera opens a gap. Primitives with a vertex nearer than 4 near-plane units (F117_STAGE2_REPROJECT_NEAR)
  now keep the screen blend: worst margin 100% to 0.9%, the bank window's extra flips 965k to 35k.
- Forced-ready (EAGER=7 with an unlimited slice) menus 2 and 3 agree with the live runs, so what
  remains is in the pictures, not in late scenes. Menu 3 against menu 2: within 5% on flips and
  0.01% on wrong pixels (the hardware path is in use; the stripe flicker is local, not yet isolated).
- F117_STAGE2_DELAY=0.25 (a deliberate display delay) shows as about one picture of lag against the
  default (0.88% to 0.67% wrong pixels at delay 0).
- Reprojection adds flips along the horizon and runway edges in the takeoff windows (47k to 133k at
  2.9 billion, 28k to 72k at 2.6 billion), measured in 27-level steps at half-pixel edges, about 0.05
  game pixel: far below visible. Fine truth buckets (game pixels, takeoff window): reprojected paired
  scenery within 0.1 for 80% and within 0.25 for 100% (screen blend: 74% and 97%); unpaired lights
  within 0.25 for 94%, 4 or more off for 4 to 5%; held pictures 48% and 87%. The angles are now moved
  in whole units as rerun.c does; the 0.1 to 0.25 band did not move.

Still open: menu 2's scrub jitter (30% of pictures use the extrapolation until the copy thread has
parity), menu 3's painted-stripe flicker, the coastline jitter, a distant light in the default and in
both menus, and R46.

Copy thread parity, 20 Nov 18:40 to 19:10. Port-write traces over the same 25-million-clock window
(private/r45/bank_ports.sh): the live machine's 179 port writes with the thread off and with the
waiting mode (F117_COPY_THREAD=2) are identical to the clock; the free-running thread (1) differs,
and differs at a different place in each run, so a race, not a computation. A per-slice trace of the
live machine's registers (F117_REPLAY_SLICE_TRACE=FROM,UNTIL, in the machine log) over 425 slices: of
eight thread-on runs seven part from the thread-off run at slice 307 (clock 8,953,069,691, a loop at
087b:0f2a whose counters differ, ending the slice at the same clock) and one is identical, and the
seven differ from each other there. So the live machine does a different amount of that loop's work
in the same clocks: one run executes it through a different engine or with a different clock charge.
Ruled out: the retrace status (port 3DA) is a function of the machine's own clock; matched.c has no
writable statics; a private copy of the runtime's per-region verdicts and code-write generations for
the copy thread (reset each job) did not change it (six runs, all part at slice 307; reverted).
Not yet tried: the copy thread interpreted only, to separate the recompiled runtime from the CPU
layer; instruction-level traces of slice 307 in two runs.

R46 fixed (default, off with F117_WIDE_AIR_UP=100). The aircraft loop's point test (VGAME B5DC from 9BDF) cuts on the
screen rows the original cockpit hides; beside the cockpit those rows are visible in the wide margins, so the parked
jet (model 3) vanished whenever its centre was low in the view. For an aircraft outside the game's 45-degree cone
(|x'| > |z'| from the game's own matrix) the y row of the test is divided by 3; inside the cone nothing changes.
Runway replay 2,350 to 2,440 million (90 pictures, default renderer): the jet is drawn in the margin where it was
missing; the 4:3 area changes only where the jet's own wing tips cross its left edge (7,995 pixels in 18 pictures).
Presentation tests 29/29.

Far white dot on takeoff (owner, 21:40). The margin dot of a far aircraft (units past 4,800: the game draws none) had three faults.
(1) rerun.c built the aircraft matrix array the other way round and multiplied by its transpose; the verified margin-dot
prediction uses the game's [component][axis] orientation (the word at B082 + 6c + 2a). (2) B082 is copied from the aircraft's
state at each game step, so within a step all four pictures put the dot in one place (logged 327,337 x4 then 306,346 x4) while
the scene's matrix at 7B28 changes every picture (measured with F117_FAR_AIR_MATRIX_LOG: B082 constant, 7B28 moving). Relation
measured on the takeoff bank: B082[c][a] = 2 x 7B28 word, the second column (vertical) divided by 0.75, rows 1 and 2 negated
(0.8%). The dot is now projected at the overlay flush from 7B28 (324,337 319,340 314,342 ...), landing where the step matrix
does at each step. (3) The hardware path (menu 3) drew the dot as a distant polygon: ground earlier in the frame hid most of it
and the strokes, which the hardware path draws after all polygons, covered the rest. Now HK_DOT draws it ignoring depth with
stencil 1; every other polygon sets stencil 0 where it lands; the strokes skip stencil 1 (the compute path's owner F7FE rules).
Menu 3 draws the full square like the default (close-ups at 2,610, 2,640 and 2,660 million). Open: the game's own dots and tests
for aircraft inside 4,800 still use the step's B082 in the in-between pictures.

Owner's 22:23-22:24 shots (menu 2) and the open items, 20 Nov 23:30. Replay screens on the owner's runway session
(scripts in private/r45/owner-motion, captures moved to D:/F117A-Scratch-Files/r45-owner-motion: run.sh, five 60-picture windows at 1,638, 2,868, 3,378, 3,466 and 4,100 million, fixed cadence, half size,
rows 0-879), measured against the default renderer one picture back by feature tracks (features.py: per-picture mean edge distance of a
feature mask in half-size pixels; series.py prints the per-picture values; water.py counts sea interruptions per column).
Four switches, all off by default and set in private/partb/try_fastpath.bat for the owner's look:
- F117_STAGE2_TARGET_VIEW=1: the scenery the terrain walk placed (prim flag `terrain`) and flat ground objects are moved from
  the cached scene's camera to the camera at the picture's own time (eye BA3C/C056, height 2DF4 + 24, attitude 2DEE..2DF2,
  moved from the two step snapshots as rerun.c nudges them), and the sky bands are rebuilt for that camera. An extrapolated
  picture or a late scene no longer slides between steps. Back and forward both go through the fitted camera model
  (s2_cam_at); with the copy's true matrix for the inverse (F117_STAGE2_VIEW_TRUE=1) the model's error did not cancel and the
  lights sat a quarter pixel off. With the target view the scene switch stays on the older step until t 1
  (F117_STAGE2_STABLE_NB=0: at 0.5 as before; measured: no difference).
  Road edge near 3.6G (22-24-02): error 1.07, p95 4.20 to 0.07, 0.15. Gray area (22-24-15): 0.11, p95 0.46 to 0.01, 0.07.
  Coast water jerk p95 0.03 to 0.01. Brown band in the sea (22-23-54): worst picture 424 sea interruptions (default 157) to 156.
  Building lights (22-24-04): the 'scurry' is one picture at 3,482 million where the crosshair cluster was 2.09 pixels off;
  now 0.53 at most; mean 0.12 to 0.09 (crosshair cluster), 0.06 to 0.11 (left cluster, jerk p95 0.24 to 0.19).
- F117_FAR_AIR_DEPTH=1 (all menus): the far aircraft dot is an ordinary depth-tested polygon (owner F7FD) at its own depth
  instead of a layer painted over what the terrain walk drew, so nearer terrain dots and the coastline cover it: 16 to 14
  pixels in every picture of the takeoff window in menus 1, 2 and 3 (the two covered pixels are the coastline's).
- F117_HW_GROUND_TIES=1 (menu 3): a ground polygon (owner bit 11) also wins over earlier ground within 3% of depth (the
  exact compositor's k = 0.97 rule), drawn as a second pass of the ground segment limited to stencil bit 2 (ground); other
  polygons clear the bit. Runway window: the hardware path drew 0 stripe pixels in all 60 pictures, now 5,642 (default 5,642).
  The far dot before takeoff did not flicker at fixed cadence in either build (16 and 15 pixels in all 60 pictures, 1,400 million); at live cadence (F117_REPLAY_FIXED_CADENCE unset, menu 3) it is missing for a stretch, 1,405 to 1,440 million (27 of 40 pictures old build, 34 of 40 new): a whole run of scenes without it, not per-picture flicker. Not chased: the owner ranks it lowest (20 Nov 23:19, 'not sure it's an issue').
- The hardware segment list fails over to the compute path instead of silently dropping segments past 8,192.
The game state is identical in every screen (final hash 543f276fcf80a018 at 4,210 million). The default renderer is unchanged
with the switches off (session-start exe against this build, menu 1: 0 differing pixels over 30 pictures, same machine hash).
Diagnostic: F117_PRESENT_SHOTS_FROM takes a comma list (COUNT pictures per clock), F117_PRESENT_SHOTS_ROWS=N keeps the top N rows.

Owner's 23:32-23:33 shots (menu 3 run 20261120-233336-path3; clocks from its present log, about 1.8, 2.4, 4.3 and 5.0-5.2 G), 21 Nov 00:30.
Captures: D:/F117A-Scratch-Files/r45-owner-motion/s4_* (four windows, 180 pictures each), flick.py (wrong pixels against the
default one picture back, and excess flips: pixels changing between pictures more often than in the default). The hardware path
(menu 3) still approximated the exact compositor's depth rules (remaster_raster.c 260-277, decal_hidden). Now, all on with
F117_HW_GROUND_TIES (each with its own off switch):
- Over the ground (owner bit 11) a polygon always wins (k = 0): HK_UNDER, a pre-pass of each polygon segment that sets the depth of
  ground pixels it covers to the far plane and clears their mark (F117_HW_GROUND_UNDER=0 off). The gray pads around the buildings
  vanished into the green field in menu 3 (owner: "patches of color around the buildings flickered"); now as the default; window
  4.95 G excess flips 275,851 to 213,594 (menu 2 198,873).
- Ground over another object only where clearly in front (k = 1.03): object polygons (owner not 0, HK_OBJ) mark stencil bit 2;
  ground is drawn normally where the bit is clear and at depth / 1.03 where it is set (HK_GROUND_OBJ).
- A ground object never hides another object's lines: a second line pass over ground pixels without the depth test
  (HK_LINE_GROUND). The hangars' yellow door stripes (one game pixel, lines) had a gray row of the apron through them (owner
  23-32-26: "thin horizontal gray line through the yellow line"); rows 489-490 at x 2664 were 36,36,36, now 199,174,4 as the default.
- A later face of the same object wins a tie within 0.01% (k = 0.9999): its depth is scaled when built (F117_HW_POLY_TIE=0 off).
- Lines have screen-aligned square ends, as the default stamps them (owner: "the ends of the line should be squares that are flat
  on top"): each line is the hull of its two end squares (F117_HW_SQUARE_LINES=0: the old rotated quad).
- One render pass until each depth reset instead of one per segment.
Window results (median wrong pixels a picture against the default, before these rules to after): runway 1.74 G 1,784 to 1,169;
parked jets 2.33 G 6,399 to 6,099; green field 4.26 G 5,209 to 5,204; buildings 4.95 G 6,303 to 6,275; excess flips lower in all four.
Stage 2 (menus 2 and 3): with the target view, models that stay still in the world (the parked jets) now take the same camera as
the terrain instead of the scene pair's (F117_STAGE2_VIEW_STATIC=0 off); F117_STAGE2_FORCE_EXTRAP=T (diagnostic) makes pictures
at t below T use the previous interval's pairing, as when the copy is late. With half the pictures extrapolated, the parked jets'
area: median 204 to 158, mean 328 to 255 wrong pixels. The green field at 4.3 G (owner 23-33-08) appears over four pictures
identically in the default, menu 2 and menu 3 (0, 546, 1,633, 2,488 pixels): the game's own ring boundary, not reproduced as a
fast-path fault.
4K 60 check (owner's request, 21 Nov 00:30): visible 4K window, vsync, menu 3 settings, runway replay 1.5 to 5.2 G, ABAB with
the rules above off and on (private/partb/vis2.sh, vis2.py; D:/F117A-Scratch-Files/vis2/r1). Per run about 3,700 frames,
median 16.59 to 16.61 ms, p99 19.4 to 19.7 ms both ways; frames over 20.9 ms: off 0 and 2, on 1 and 3; one frame of 36 ms in
the second "on" run (none over 31.7 in the others). The GPU raster pass, sampled 11 to 13 pictures a run: median 2.8 to 9.9 ms
(both ways within that range), the sample too small to resolve a difference.

Parked jets jitter during takeoff, menus 2 and 3 (owner, 21 Nov 13:40; everything else on the 00:40 checklist confirmed fixed).
Cause: the camera's travel between two step scenes is measured per depth scale from a sample of about 4,096 paired primitives
(s2_travel), and a scale with fewer than 8 samples was dropped. Near the tower the parked jets are the only primitives at scale 128
(31 faces), so from about 2.41 G on the scale was dropped and the jets fell back to the screen blend with their partners, while the
ground took the camera at the picture's own time. On time (fixed cadence) the two agree; at live speed, with late scenes extrapolated
to t 1.5 during the rotation's pitch bounce (game pitch 768, 1024, 648), they do not. Fix (default on in Stage 2,
F117_STAGE2_TRAVEL_SCALE=0 restores the old way): a scale without a measured travel takes the nearest measured scale's travel times the
ratio of the scales. Where both were measured the rule holds: 128 = 2 x 64 (753,848 against 755,924 along the travel) and 64 = 4 x 16
(0.3%); scale 1 (the game's own drawing, a mix) does not fit and is used only when nothing else was measured.
Diagnostic F117_STAGE2_PATH_LOG=1: per blended picture each model object that is neither terrain nor flat, with the blend path of its
faces (p0 terrain view, p1 still model on the terrain's camera, p2 moving, p4/p5 screen blend, p6 no travel for its scale, p7/p8 unpaired),
its mean screen place, nearest vertex, scale, and `dev`, the largest distance in game pixels of a drawn first vertex from the still-model
place; with [s2-travel] lines comparing each scale's measured travel with the best-sampled one's.
Live runway replay 2.25 to 2.48 G (private/r45/owner-motion/jets4.sh, D:/F117A-Scratch-Files/r45-owner-motion/jd_*), two runs each way,
jet faces on screen: old way 11 and 11 pictures more than 1 game pixel off the ground's camera, worst 25.2 and 23.9 game pixels; new way
0 and 0, worst 0.3. At fixed cadence menu 2 matched menu 1 picture for picture before and after (16-picture strip at 2.404 G).
The parked jets' size is the game's: the aircraft loop (VGAME 9A7C) draws aircraft at twice the size, and as models out to twice the
distance, only in the cockpit view ([368C]) with altitude plus the nearest airfield's distance ([9922]) above 1,500; on the runway it does not.

The far white dot before takeoff (owner's circle, 21 Nov 14:21: the dot on the horizon far right, half-size picture about x 1725, y 236).
What it is (F117_WIDE_DOTS_LOG, which now also prints the unit's place and the eye's): unit 3 of the aircraft table, model class 2, at
(25,670, 15,400), first seen at altitude 18 and climbing (223 by 1.459 G) while moving south: an aircraft taking off from another
field. The game draws no aircraft past 4,800 (octagonal range); this one is at range 10,237 (about 9,260 straight-line map units, depth
7,679), so only the Remastered far-aircraft overlay shows it (Draw distance Maximum), at one game pixel (depth under 8,191, the one
far-size rule). Its rate, 60 map units and 205 altitude units in about 2.9 s of game time, is a believable climb-out (about 400 knots,
4,200 ft/min) if an altitude unit is a foot and a map unit 32 feet (the game's alt >> 5), which puts it near 90 km away; inferred from
that consistency, not from a stated scale. Geometry: 7 map units up against the eye's 0.75 at 9,260 away is 0.04 degrees above the
horizon, 0.17 game pixel: it belongs on the horizon row, and in the game's flat world nothing lies between, so drawing it is
consistent with the overlay's rule. As realism it is generous: a fighter at that range is below the eye's resolution, at dusk with no
lights. The original's own dots (one game pixel out to 4,800, about 47 km) are the same kind of aid.
The blinking (menus 2 and 3, live speed only): whole step scenes had no far dots (F117_STAGE2_PATH_LOG [s2-fardot]: dots 0). The kept
dots (rerun.c g_pend, g_air_late) were one shared list; a Stage 2 scene copy runs in slices across display frames, and when another
copy reached its flush between the scene's walk hook and its own flush it took the scene's dots (102 such flushes in a 95-million-clock
window). Now each kept dot carries its machine, a flush takes only its own machine's, a machine's new frame drops what its last frame
left, and the lists are per thread (copy thread); F117_FAR_AIR_SHARED=1 restores the shared list. Live replay 1.38 to 1.475 G, 50
pictures each: missing in 23 (menu 2) and 19 (menu 3) before, 0 and 0 after; menu 1 at fixed cadence 0 differing pixels over 80
pictures, machine hash 17f6c1d6c7063363 both; presentation and host tests 42/42.

Far aircraft dots by real size (owner, 21 Nov 14:32, the recommendation taken). Far light size Perspective (the default): an aircraft beyond the
game's 4,800 is one game pixel at depth 256, where the aircraft loop swaps its model for a dot ([DEBE] = z' >> 3 above -32, matched.c 34700,
VGAME 9BDF), and 256 / depth beyond, never under one screen pixel (remaster_live_overlay_rect grows it): at 4K one screen pixel from about
2,300, so every far aircraft. Lights keep the shared rule (one game pixel to 8,191, then 8,191 / depth): a light is seen by its brightness, an
aircraft at dusk by its size. Original: the shared rule for both, unchanged. Menu 1, 1.38 to 1.46 G, 80 pictures: only the dot's 24 half-size
pixels a picture change (the 9 x 9 block at 4K becomes one pixel; Desktop\ONShots\20261121-1449-far-aircraft-dot-before-after.png); with
Original 0 differing pixels; machine hash 17f6c1d6c7063363 in all four runs. Note: an aircraft crossing 4,800 still changes from the game's own
full-game-pixel dot to one screen pixel; the game's own dots inside 4,800 are untouched (option b would shrink those too).
Stage 2 blend: each part looks the travel of a depth scale up once (same values). Tried and reverted: preferring a well-sampled scale (32+) over
a scale's own few samples made the parked jets worse (17 to 33 live pictures over a game pixel off a run). Still open: in some pairings 8 of a
parked jet's ~30 faces are judged moving (its own scale measured from 8 samples, all the jet's), and in a held stretch (t 1.000 for five
pictures) they slide up to 34 game pixels; 0, 0, 2, 5 and 1 live pictures in five runs (11 a run before the travel fix).
Rigid-object vote (21 Nov 15:45, src/present/remaster_live.c): model faces vote still or moving per object request (s2_rigid_objects). Before the
parallel blend, the faces of each model object vote; the majority of those with a test decides for all faces of that object.
Guard added: a face judged moving whose partner is missing or not reprojectable falls back to still (cannot slide without partner).
Live runway replay 2.25 to 2.48 G (private/r45/owner-motion/rigid.sh, jr_1..jr_4): in four runs (753-756 on-screen jet pictures each),
pictures with any face judged moving dropped to 0 across all four runs (0, 0, 0, 0; baseline 11/run; travel fix alone 0-5/run).
Over 1 game pixel off: 3, 2, 2, 2 pictures. Diagnostic F117_BLEND_TIMING added to instrument the vote and parallel blend duration.
Residual explained (24 Nov): every one of those pictures is the same three faces of the near parked jet (model 3) as the camera passes it,
2.43 to 2.45 G: depth down to 2.8 units, so s2_reprojectable refuses them (a vertex nearer than F117_STAGE2_REPROJECT_NEAR, 4: the near-plane
gap rule of 20 Nov) and, unpaired, they are held (path 7). F117_STAGE2_PATH_LOG now prints the screen box of the faces over a game pixel off
(devfaces): x -13,509 to -1,088, y 249 to 2,703 game pixels in every such picture, wholly outside the wide view (about x -53 to 373), so no
screen pixel shows them; the jet's other 30 faces are 0.00 off. Four live runs with that build (jd_1..jd_4, 797-806 pictures each): faces judged
moving 0, on-screen faces over a game pixel off 0 (off-screen only: 3, 2, 3, 3 pictures); final machine hash 6f1c98e5411558b6 in all, as
before the vote. Menu 1 against HEAD (2e31094, built from it): 32 pictures 2.25 to 2.48 G byte-identical, same machine hash. Also fixed: the
F117_BLEND_TIMING line had a literal newline in its string and used SDL_GetTicksNS in a library f117run links without SDL (the tree did not
build); it now uses now_ms. Captures: D:/F117A-Scratch-Files/r45-owner-motion/jd_*, m1_head, m1_new.

R39 and 4K 60 on the owner's late flight (session-20261108-211031, 21 Nov 14:27-14:47; private/partb/late4k.sh, late4k.py, slowsplit.py;
D:/F117A-Scratch-Files/late4k). Visible 4K window, vsync on (60 Hz), Maximum, Model detail Full, wide, ABAB, CPU load 5-7% mean. Frames over
20.9 ms (a missed vsync) by stretch, two runs each:
  menu 1 (default): 2-8 G 12-24%; 8-9 G 84-85% (median 22.4 ms); 9-10 G 100% (25.6); 10-10.4 G 100% (27.7). Whole flight 31.7%.
  menu 3 (Stage 2 + hardware raster, try_fastpath.bat): 2-9 G 0.6-2.7% (median 16.2-16.4); 9-10 G 15-16% (17.6); 10-10.4 G 27-31% (19.0-19.3).
  Whole flight 4.0-4.3%. Both: about 30 frames over 31.7 ms in the first stretch (loading).
R39 reproduced in the default: the cost climbs with the scene from 7 G. The fast path holds 60 to 9 G and not after. Traced menu 3
(F117_SLOW_FRAME_TRACE, frames costing over 12 ms): the worker wait (GPU side) is the same early and late (median 11.7 and 11.1 ms); what grows
is main-thread CPU: the Stage 2 blend p90 1.1 to 5.5 ms and the copy's commit p90 5.3 to 8.7 ms. The travel fix is not the cause (with it off:
blend median 1.40 both, 735 against 744 slow frames). So 4K 60 is not safe at the end of this flight; the remedies are the planned ones: the copy
off the main thread (the copy thread's parity race, slice 307) and the blend on more helpers or off the critical path.

Step 2a, the late blend cost (24 Nov; private/partb/late4k_diag.sh: late4k2.sh's menu 3, hidden, F117_SLOW_FRAME_TRACE and F117_BLEND_TIMING=0,
D:/F117A-Scratch-Files/late4k/d1..d3). The trace's "blend" phase in main.c is drawlive_stage2_view plus the whole remaster_live_stage2_blend call.
Split (d3, per billion clocks, median/p90 ms): through 8 G the call is 0.90/1.13 to 1.49/1.77 (vote 0.15 to 0.25, parallel blend 0.76 to 1.24,
both growing with the primitives, 35,000 to 63,000) and drawlive_stage2_view 0.03. From 9 G the call's p90 is 5.38 (10 G: 5.71) while the vote
(p90 0.32) and the parallel blend (p90 1.67) stay small: the rest is the output array's growth. remaster_live_stage2_blend grew its output to
exactly the new count (realloc to nr->n); late in the flight the scene grows nearly every step (63,000 to 75,000 primitives, about 250 bytes
each), so 256 of 867 pictures at 9 G and 153 of 310 after 10 G reallocated and moved about 18 MB, 3.6 to 4.0 ms each. Not helper contention
and not unsplit work (the blend ran on the copy's pool, 8 parts, every time). The vote added 0.15 to 0.3 ms (d2 against d1, the build before it).
Fix: grow by half again (s2_grow_cap; F117_EXACT_GROW=1 the old way), also in remaster_live_done_clone (the scene copies grew the same way).
Result (d4, same script): growths 256 at 9 G to 4, 153 after 10 G to 0; the blend phase median/p90 after 10 G 2.30/5.70 to 1.80/2.10 ms (9 G:
1.80/5.40 to 1.70/2.00); before 9 G unchanged. Exact: late flight at fixed cadence (private/partb/late_exact.sh), 32 pictures from 9.5 G
byte-identical with F117_EXACT_GROW=1 and without (two runs), final machine hash 172ae2ba9497b651 in all three; presentation and host tests
42/42. The d4 run's machine load was 17.8% mean (d1 2.9%), so its frame totals are not an M1 number. The vote (0.31/0.37 ms after 10 G) is
left serial: too small to be worth splitting. What still grows late is the copy's slice (p90 5.5 to 5.8 ms after 9 G): step 3, the copy thread.

Step 3, the copy thread's parity race: cause found and fixed (24 Nov; private/r45/bank_slice2.sh, D:/F117A-Scratch-Files/copy-race). The copy run
interpreted only (F117_COPY_INTERP=1, diagnostic: the copy never enters the recompiled-code runtime) still parted from the thread-off run (four runs:
at slice 307 and at 8,957,069,691), so the runtime's shared tables were not the cause. The parted slices showed the live machine ahead in the same
clocks: one slice in other code (087b:06d2/06ec, MGRAPHIC, against 0f2a), the next at the same block copy (087b:121c) with less of it left. The cause:
rerun.c's g_walk_busy (rerun_extra_active: the copy's far-scenery placement is running) was a plain static beside the thread-local g_coarse_busy. With
the copy on its own thread, the live machine's model hook (remaster_live_exec, k == 1) saw the copy's placement as its own and skipped the live
game's drawing of that model's faces (it pops the return), so the live game drew less, ran ahead and its screen and machine hash changed; it also
skipped the live frame's camera capture (k == 0). Only with F117_COPY_THREAD=1 (off by default): the default and the sliced copy were never
affected. Fix: g_walk_busy is thread-local (F117_TLS), as is the diagnostic remaster_prim_source. Slice traces 8.935 to 8.96 G, copy thread on,
four runs: 425 of 425 slices identical to the thread-off run, final hash 3371c8426c7bbb59 in all (without the fix every run differed: 2 to 4
slices, four different final hashes). New diagnostic: F117_REPLAY_INSN_TRACE=FROM,UNTIL,FILE (the live machine stepped and logged per instruction).
Proof: whole late flight to 10.37 G at fixed cadence, menu 3 (private/partb/late_exact.sh with F117_REPLAY_HASH_EVERY=100000000), copy thread
on twice and off once: 104 of 104 hash checkpoints identical, final hash e2868f7906ab544e in all three; copy-thread crash loop 6 of 6. Full gate
gate_ct2: everything passed except the far-aircraft guards, whose 13 Nov baseline predated b22d0c9's dot size (the build before 24 Nov gives the
same 28 differing pictures, and all 2,703 of its checksums equal this build's; with the far aircraft off all equal the original baseline): baseline
regenerated from 2e31094 (old kept as baseline-farair-20261113), and this gate's CPU and GPU guard outputs equal it at all three distances.

Visible 4K 60 A/B on the late flight, 24 Nov 16:25-16:32 (private/partb/late4k_diag.sh HIDDEN=0, menu 3, vsync, one valid pair; the owner quit the two b runs,
so they exited at the logo). Frames over 20.9 ms, whole / 9-10 G / 10-10.4 G: copy thread off 0.70% /
1.90% / 2.70% (2-9 G 0 to 0.5%); copy thread on 1.93% / 5.33% / 8.89% (from 6 G 1 to 4.4%). So on screen the copy thread makes it worse; with
it off, after the 24 Nov blend fix, the end of the flight is near the goal (21 Nov: 15-16% and 27-31%). Earlier runs that day reported as
visible (late4k v1, ab_*) were hidden: the app hides the window whenever F117_HIDDEN_WINDOW is set, even to 0 (script fixed), so they do not count.

R47, mountain lines that darken on approach (owner, 24 Nov 16:31; late flight, menus 1, 2 and 3 alike, copy thread off and on). Found with
F117_LINE_SERIES=FROM,UNTIL (new diagnostic: per composed picture, lines by shrink and source and the far-size width sums by shrink) and Tab shots
every 100 M (private/partb/late_shots.sh): no sudden change in line counts, but at 8,908,669,691 exactly 188 lines move from shrink 4 to shrink 2
(total unchanged), and the coast's mountains turn from light grey faces to dark faces with heavy outlines between 8.8 and 8.9 G (owner confirmed on
the sheet). Their line width is continuous (width sums 48.1 before, 48.3 after), so not the far-size rule. Cause: rerun.c gives each far pick the
game's fade by depth (far_detail, VGAME 1039:011A, the darken byte that darkens shaded colours) from zh in the drawn level's units, 4 times coarser
per level, so far objects read 4 or 16 times nearer, are faded too little (too light), and darken at once when they move to a finer level, again
at each level change. Switch F117_FAR_FADE (rerun.c): 0 the drawn level's depth (today, the default until the owner chooses); 1 the own ring's depth
(the game's fade carried on past its reach); 2 the own ring's depth held at the game's reach (8191). Late flight (D:/F117A-Scratch-Files/late-shots/
fade0..2): brightest land (90th percentile) at 8.2 / 8.6 / 8.906 / 9.25 G: 0: 28 / 28 / 28 / 23.7; 1: 21 / 21 / 22 / 23.7; 2: 21 / 22 / 22 / 23.7: with
1 or 2 the far mountains already have the shade they have close up, so nothing pops; all three agree close up. Machine hash identical in all three;
the cost is a shift and a compare per far pick, the drawing is the same. Runway flight (also dusk, day_fade0..2): options 1 and 2 change 600 to
2,500 pixels a picture after takeoff, none black. Sheets: Desktop\ONShots\20261124-165529-R47-mountain-fade-far-vs-close.png and
20261124-170024-R47-mountain-fade-daylight.png (dusk despite the name). Recommended: 1 (the game's own rule by true distance, no invented cut-off),
after a check on a daylight flight that the farthest mountains do not go too dark (2 never darkens past the game's reach edge).

## R48: see-through ridge at draw distance Maximum (24 Nov 2026)

Owner crop, Persian Gulf route secret_airstrip_return, 4.0 G: far sea and coast lights inside a near ridge. Original 320 x 200 and Remastered at draw distance Original: solid ridge. Probe (F117_REMASTER_PROBE_BOX=162,56,180,68, PROBE_FROM 4.0 G): the ridge is model 10 at true depth 6.4 M with flat = 1 (a ground object); the far scenery over it is models 40 and 42 at 60 to 80 M, not flat. The ground rule (remaster_raster.c, polys*.hlsl, strokes*.hlsl, hw.hlsl: a ground object never hides another object) let them through. The ground test (remaster_scene.c) called an object level when every polygon was within 25.8 degrees (cosine 0.9). F117_FLAT_LOG=1 on that flight and on the runway flight 20261103-154444: ground objects (runways, aprons, roads, sea, desert tiles; models 0-8, 14-36, 59-62, 71, 79-84) have least cosine 0.9727 or more; the hills (models 10, 11, and 9 on the runway flight) 0.9627 or less. Threshold now 0.9677 (14.6 degrees), F117_FLAT_COS overrides. Result: 4.0 G ridge solid; of four shots 3.5-5.0 G two change (4.0, 4.5: lights and edges along the hills now hidden), two identical; machine hash e235e56696343634 both ways; F117_FLAT_COS=0.9 equals the earlier default picture. Limits: two flights measured; a future level object tilted between 13.4 and 14.6 degrees, or a hill under 14.6, would still be classed by angle alone.

## Flicker scanner, standing check (25 Nov 2026)

Tools (private/partb/flicker, untracked): scan.sh FLIGHT EXE [STEP] (fixed cadence, menus 1 and 3, 24 consecutive pictures per window), analyse.py, selftest.py, live.sh (live pace). Two measures. **A-B-A** (a pixel that differs from both neighbours while they agree, 16-pixel blocks, menu 3 at least 12 events and menu 1 at most 2) finds true back-and-forth flicker. **TEXTURE** (new, from R49: its fine lines on a hangar door gave 0 A-B-A events) flags blocks whose median edge-pixel count over the window is at least 96 of 256 in menu 3 and at most 24 in menu 1, i.e. dense fine structure on a face menu 1 draws plain; an outline that moved a pixel (menu 3 draws the picture at its own time) does not reach it. `selftest.py` (no game run) checks: clean data flags nothing, an injected A-B-A flicker is flagged by A-B-A, injected steady fine lines are flagged by TEXTURE and not by A-B-A, the stored R49 old-way capture flags its door (796 edge pixels in menu 3, 0 in menu 1) and the fixed capture flags nothing. Not proven: that A-B-A flags the runway-stripe flicker of 20 Nov (`F117_HW_GROUND_TIES=0` gave 0 events at 1.1-2.6 G and 3.7-4.2 G; the clocks of that flicker are not recorded).

First scan of all seven test flights (exe with the R49 fix, 0.5 G steps, 1 G on the two long flights): **0 A-B-A events everywhere**; TEXTURE regions after the density rule: late 2, runway 7, busy 18, airair 19, secret 0, long1 6, long2 15 (not yet all triaged). Most sit on the horizon row (menu 3 draws more far lights and ground detail than menu 1 there: content, not flicker; to be sorted by looking). **Finding F1 (a real menu-3-only difference):** long2 session 20261110-125342, 14.1 G: the game draws a yellow flash and a dark aircraft silhouette (HIT BY SA-N-4) for 18 consecutive pictures (about 139,000 yellow pixels, menu 1); menu 3 never draws it in that window. Probe at the first picture (F117_REMASTER_PROBE_BOX=0,40,140,120): menu 1's frame has 11 model-15 primitives (owner AA621000, scale 32, true depth 28,000 to 60,000, one polygon behind the eye, colour word FF0D); menu 3's frame has none. Explanation consistent with it, NOT proven: the effect is drawn in only a few of the game's views (18 pictures is about 1.03 M clocks) and Stage 2 builds its scene once per 4 M-clock step, so a short effect between two step scenes is never in one. Needs the owner's decision (it is the design of Stage 2, not a defect of one rule). **F1 cause CONFIRMED (25 Nov 12:30, F117_STAGE2_TRACE at 14.1 G, long2, menu 3):** step scenes 3306 and 3307 are built at about 14.0975 G and 14.1015 G (a step is 4 M clocks); the flash lives from 14.1 G + 57,073 to about 14.1011 G, entirely between them, so no step scene contains it and the in-between pictures (interval 3306-3307, t 0.64 to 0.88, 'proper') show only the blend of two flash-free scenes. It is a game object with a sub-step life. Options with no added latency, none built: (1) a per-picture legacy copy (menu 1's way, the NO-BLEND path) only on pictures where a short-lived object exists, about 20 ms on those pictures (about 18 late frames at a hit event); (2) a restricted copy that walks only the short-lived objects and is composed over the blend in depth order (needs which game table holds the effect, not yet read; layering to be proven behind and in front); (3) leave it as a Stage 2 limit. Rejected: a scene per game view (undoes Stage 2's speed), delaying the picture to the next step (adds latency, catches only effects that span a boundary). Scan outputs: D:/F117A-Scratch-Files/flicker/scanall (reports re_*.txt, crops in each flight's regions folder).
