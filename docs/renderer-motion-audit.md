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
