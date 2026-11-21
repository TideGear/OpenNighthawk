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
