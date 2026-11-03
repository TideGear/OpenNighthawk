# Remastered shoreline correction

The owner confirmed that the moving coastline also occurs in the original
GOG release. Remastered must fix it; preserving that visual behavior is not
the desired result. Original and Enhanced retain their existing rendering.
The separate false brown strip at the bottom of wide water margins is fixed.

The shoreline should be fixed in world coordinates, with consistent land
under fixed objects as the camera moves. Where the game has an authoritative
land/water collision boundary, that boundary should anchor the drawing. First
verify whether such a boundary exists; a visual edge, map class and aircraft
impact rule must not be assumed equivalent. The renderer must not alter the
simulation to make a shoreline comparison pass.

## Original ground behavior confirmed by the owner (3 November)

The owner briefly tested the installed correction and reports that the
shoreline seems fixed; that first visual check had crashes disabled. They
then enabled crashes and confirmed slow taxiing on both dirt and water,
followed by a crash after accelerating on either surface. The owner explicitly
clarified that these live tests were in the recomp, not the original GOG game.
Record them as recomp runtime evidence, not an independent GOG comparison.
This agrees with
the matched original ground-contact routine and its original instruction
bytes: the off-field check at 42A2 compares speed with 0x32 (50 knots), and
skips that crash at or below the threshold, subject to its other state checks.
There is no land/water test in this taxi-ground branch. The owner's test
confirms the shared behavior, not an exact measured threshold or all other
landing/rescue paths. Preserve it for original-game parity; the Remastered
shoreline correction adds no water physics or collision rules.

## Owner evidence

The latest twelve screenshots are from `session-20261102-154150`, clocks
12391636686 through 16896927060. The sequence shows hangar backs apparently
over water, a shoreline shifting underneath them on approach, and the older
horizontal cutoff in the wide margins before takeoff. Inspection contact sheet:
`D:/f117-gate/coast-reference-20261102/owner-last12.png`.
The matching input recording and private save-start copy remain available.

The mission caption says Tripoli, but the departure world record is 34,
Halfar, at (30784, 7744) in the Libya theatre. The observed aircraft near
the hangars is at (30778, 7766). Use the departure location when investigating
the coast, rather than the mission target's name.

## Investigation, not a completed fix

The ground-contact path inspected so far, VGAME 0000:40C1..42D5, establishes
the airfield/deck contact box and handles an off-field aircraft at zero height.
Its crash decision includes aircraft settings, scenario state and speed;
this inspected path does not look up a separate water boundary. This is not
proof that no other water-specific outcome exists.

VGAME 0000:BA56 and END 0000:00A1 use low map-class bits from a 16-wide grid.
Their precise relationship to water impact, rescue outcomes and the detailed
coastline remains to be established. Do not use those bits as a shoreline mask
without proving their meaning. The displayed terrain has a separate hierarchy:
VGAME 0000:0871 selects cells, and 0000:0C38 loads the terrain tables. The
camera-dependent rings and their transitions are a lead for the moving shore,
not an established diagnosis.

Acceptance needs the whole twelve-view sequence plus moving comparisons:
stable shoreline position relative to hangars, a consistent crossing under
the camera, no new gaps or overlaps across terrain detail transitions, and
agreement with any verified gameplay water boundary. Keep the existing depth,
lights, hidden-door-line and margin-water regressions. Broader theatres and
viewpoints still need coverage before calling a general correction complete.

## 3 November correction and verification

The matched Phase 2 walker was cross-checked against original VGAME bytes and
runtime state. VGAME 07FD..0805 clamps each ring's eye height to two native
units. At world height 24, rings 1/2 see height 24, ring 3 sees 32 and ring 4
sees 128; X/Y also round per ring. Remastered now applies the inverse rounding
translation after model evaluation, using the prepared view basis. Placement
metadata follows deferred models. Dynamic placements receive no terrain offset.
All rings share the finest ring's minimum height of two world units. Ground
near clipping also scales with native units, so a coarse ring does not expose
a larger hole directly beneath the camera. Native tests cover low heights,
all four rings, both wide bottom corners and banked ground.

The earlier camera-only experiment appeared ineffective because the compositor
kept native model pixels wherever the new picture had only background. A raw
render placed the shore correctly, but the composite pasted the old blue strip
back. A complete reconstructed scene now owns those cleared model footprints;
HUD, artwork and text remain protected. Failed model decoding/dropped geometry
or an unsupported background disable this broader replacement. A native live
compositor test exercises complete, failed-model and low-detail captures.

This fixes projection/composition rather than redefining terrain topology.
Land and water retain the authored world-space edges and use the same camera.
No authoritative water-impact boundary has been found, and no collision or
simulation rule has changed. Original and Enhanced retain their prior behavior.

The installed build is SHA-256
`A26310FF2A8DE6DB548B3E91395A05A937B699B444EA24FBBDC41B2F90A8B86E`.
The reported twelve-view sequence was captured and visually reviewed: ground
stays below the hangars, the shoreline no longer slides down their sides, and
the subsequent water crossing fills the wide margins without the old cutoff.
The new moving foreground-ground regression passes. Both diagnostic controls
(native ring eye and native footprint retention) restore the faulty strip and
fail that regression. All eleven host cases, nine focused native tests and 24
Python tests pass. Identical full 1 MB RAM snapshots across Original, Enhanced,
Remastered and the two controls demonstrate isolation at the sampled clock.
This does not replace the unchanged full machine gates or owner visual review.

Local evidence under `private/settings-20261103/`: `coast-final-twelve/`
(sheet, all images, requested/actual clocks and executable hash),
`coast-host-final/` (complete bound report and moving gallery), and
`coast-isolation/` (five RAM comparisons and rejected visual controls).
One twelve-view sample is one instruction later than requested; the report
records it explicitly. This completed run supersedes the earlier failed exact-
clock script, whose failure was a capture assertion rather than a game failure.
`coast-geometry/render.png` and its composite show the stale-footprint cause;
`coast-diagnostic/process.log` and `original-ground-instructions.txt` establish
the original eye floor. Research binaries and original game data stay ignored.

Owner confirmation remains pending. Continue broader theatre/mission coverage
and fast turns, including centre/margin ground joins while banked and near the
surface, before claiming every terrain/motion case is complete.
