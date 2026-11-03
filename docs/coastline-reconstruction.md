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

## 3 November stopping investigation

Matched Phase 2 code was used as a guide and cross-checked against original
decompressed VGAME instruction bytes and runtime observations. The terrain
walker clamps eye height to at least two native units separately in each
detail ring (07FD..0805: compare AX with 2, replace lower values with 2).
At world eye height 24, rings 1/2 therefore see world height 24, ring 3 sees
32, and ring 4 sees 128. Position rounding also differs by ring. This is a
confirmed rendering discrepancy and a candidate contributor, not proof of
the whole shoreline cause. The map-class bits remain unproven as water impact
classification; no authoritative water collision boundary has been established.

A presentation-only ring-eye correction and independent projection tests were
tried. The first comparison changed pixels but did not establish a stable
coastline under the hangars. The experimental renderer/math source was removed
from the final build and preserved with its tests in the local unfinished
snapshot described in [p3-current-work.md](p3-current-work.md). Fine generic
ground patches overwriting coarser water geometry remain another lead; verify
geometry, transforms and classification before choosing a shared land/water
edge. No simulation or collision changes are authorized for this visual fix.

Local evidence: `private/settings-20261103/coast-diagnostic/process.log`,
`original-ground-instructions.txt`, and `coast-captures/`. The capture run is
partial/failed: one requested clock was sampled one instruction later and
failed the script's exact-clock assertion. Do not describe it as a completed
twelve-view validation. Saved experimental executable and images are research
artifacts, not a deployment candidate. Original game binaries remain ignored.

The owner's additional report is ground not lining up in widescreen margins.
This remains OPEN alongside shoreline motion; the earlier near-plane fix only
addresses the false brown bottom strip. Check ground joins across center and
side margins while banking, moving at ground level and crossing the coast.
