# Scenery distance: current evidence

The Remastered wide view now makes a three-cell terrain pass in its private
render copy. It fills a small part of the widened land view that the normal
walk leaves empty. Original and Enhanced keep their existing drawing path and
live machine hook limit. This is partial R6 progress: distant buildings,
several land views, and broad movement/performance coverage are still open.

The default applies only when Remastered uses a view wider than 4:3. The
diagnostic `F117_WIDE_SCENERY=0` disables the pass; values 1 through 6 select
the cell shift and are clamped to that range. The installed ONTests build was
not refreshed. No full machine gate was run.

## Implementation and regression

VGAME's terrain walk at `04E6` takes eight words: heading, pitch, and three
32-bit camera coordinates (X, Y, height). The matched implementation reads
the full 16-byte argument block. The extra call forwards every word before
adjusting SP; the earlier two-word experiment read unrelated stack contents
as its camera position and was not valid evidence.

The extra pass shifts a sector's cell table along the current heading, disables
the three finer rings, raises the far plane to 30000, lowers the side allowance
when needed to avoid 16-bit depth overflow, then restores the copied machine's
registers and modified table values. The caller itself is the speculative
Remastered render copy, never the live game machine.

The first land replay exposed a second failure mode: the live renderer already
occupied seven of its eight execution hooks, so the copy installed the
aircraft hook but had no slot for the terrain walk. `machine_t` now has two
additional hook slots for the render copy. Live sound and Remastered hooks
remain capped at eight, preserving their existing behavior. The focused
`test_rerun` fills all eight live slots and checks that both copy-only hooks
still work; it also checks all eight camera arguments, stack wraparound, and
that the real machine and its full memory remain unchanged. `build/test_rerun.exe`
passes on the final build.

## Fresh airborne land replay, 3 November

The comparison uses a newly recorded North Cape strike route generated through
the game's normal menus and takeoff controls. Captures are from current build
SHA-256 `858d3f00f6a307bba891d36b86a032caf25fb1ca2eb162b7d5aae044c5529ad3`;
none of the older renderer screenshots were used. Private files are under
`private/scenery-20261103/north-cape-route/`.

At clock 3,700,000,000 the placement trace reports camera position
`109237,330178,7116` and a live aircraft altitude of about 7,116 feet. The
fresh frame shows land beside the coast. In the 173% wide view:

| Terrain pass | Extra walk values | Primitives | Model requests | Difference from control |
|---|---:|---:|---:|---|
| Off | 0 | 79 | 29,102 | — |
| 3 cells (default) | 7,070 | 103 | 32,273 | 177 pixels at the widened edge |
| 6 cells | 421 | 81 | 29,205 | pixel-identical |
| 12 cells (before the diagnostic clamp) | 0 | 79 | 29,278 | pixel-identical |

For the three-cell result, the differing pixels occupy `x=1120..1274`,
`y=466..506` in the 1280 x 600 capture, within the wide land margin. The
default-without-an-environment-override capture is pixel-identical to the
explicit three-cell capture. The tested six-cell offset draws less useful
geometry; twelve cells finds none in this view, so neither is selected.

At clock 3,900,000,000, after a turn, the aircraft is at about 10,694 feet
and looking into sky. The extra walk returns with zero geometry and the image
matches the control exactly, so this sample shows no terrain bleeding into
the sky. It does not substitute for continuity checks across multiple land
headings or sector/detail transitions.

The short replay's requests rose about 11% and primitives about 30% with the
three-cell pass. End-to-end wall time in single runs was 54.6 seconds for the
control and 54.8 seconds for three cells; this is too little timing evidence
to establish a general performance cost. A later performance pass should use
the project's measured renderer workflow.

The 1 MiB snapshots at clock 3.8 billion differed between the control and the
three-cell run at two bytes (`0x4CD8` and `0x4CE4`); the same two offsets also
varied between duplicate control runs. Do not cite those snapshots as proof
of exact machine parity. The native rerun isolation regression checks the
copy's full-memory isolation directly. The new work is confined to the
Remastered render copy and the live machine hook cap is unchanged.

## Earlier negative controls

The prior comparisons were useful for finding mistakes but did not establish
a visible distance improvement:

- The corrected pass increased model requests in taxi/airfield samples while
  producing pixel-identical images.
- The first land flight at 6 and 12 cells also appeared unchanged. Its trace
  showed `walk=-1`: the scenery hook had not been installed because the eight
  live hook slots were full. Those samples are not evidence that the pass was
  ineffective.
- The original two-word-call sample was invalid because its camera arguments
  were incomplete.

Native positive/negative call coverage and the original disabled-pass
comparisons are in `private/scenery-20261103/argument-regression.json`,
`camera-call-comparison/`, and `direction-isolation/`.

## Remaining work

- Verify additional distant buildings and terrain at known world distances on
  more than one land-heavy route and across cell sectors/detail-ring changes.
- Capture moving land scenes while turning, and check for gaps, duplicates,
  pop-in, occlusion, and interaction with the coastline correction.
- Measure the pass's renderer cost across representative flights and display
  sizes; the current request/primitive counts and one end-to-end timing are
  not a performance acceptance.
- Review the result in ONTests after a future authorized refresh.

Original game behavior and simulation parity remain hard boundaries. Keep
future distance changes in Remastered's private presentation copy, and keep
the progress credit partial until the remaining evidence is available.
