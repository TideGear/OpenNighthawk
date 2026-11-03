# Scenery distance: current evidence

General terrain/building reach is unfinished and disabled. The installed
ONTests build remains the accepted settings/coastline build. Aircraft already
have a separate model-to-dot distance extension in the widened render copy;
that is not completion of the general scenery requirement.

## Correct the experimental camera call first

The original VGAME walker at 04E6 takes eight words: heading, pitch, and three
32-bit camera coordinates (X, Y, height). Its matched implementation reads
BP+4 through BP+12h; original callers push all sixteen argument bytes.
The disabled `F117_WIDE_SCENERY` experiment previously passed only the two
angles. The missing coordinates came from unrelated caller-stack contents,
so the earlier negative distance experiment was not reliable evidence.

The extra call now forwards all eight words before modifying SP. Native
`presentation_rerun_isolation` exercises both calls with sentinel arguments,
including a stack wrapping through zero, and checks that the real machine and
its entire memory remain unchanged. A separately compiled old-call negative
control fails this test; the corrected call passes. All five focused
rerun/Remastered CTests pass. The optional bounded scenery trace reports the
actual camera, sector, extra draw values and return status; it is off normally.

This defect affected only the explicitly enabled extra-pass experiment. It
does not explain normal-play coastline motion, layer sorting or aircraft
culling, and the correction is not a claim to fix those independent issues.

## Replay results on 3 November

Private evidence is under `private/scenery-20261103/`; it contains no files
that should be distributed with the repository.

- `argument-regression.json`: healthy call passes, old two-word call fails.
- `camera-call-comparison/`: session `20261101-224822`, clock 10000000000.
  Corrected extra pass raises cumulative model requests from 49200 to 54770;
  the displayed and raw reconstructed samples are pixel-identical.
- `direction-isolation/`: session `20261102-141108`, displayed clock 900000000.
  Corrected extra pass raises the diagnostic model requests from 980 to 1120
  and primitives from 631 to 641. Both the displayed sample and the separate
  raw reconstructed sample remain pixel-identical to the disabled control.
  The diagnostic primitive count alone therefore does not establish a visible
  benefit. The bounded trace confirms a complete-camera call returning normally.
  Live 1 MiB RAM at clock 1000000000 is identical across the prior installed
  executable with the experiment enabled, the corrected candidate enabled,
  and the corrected candidate disabled (SHA-256
  `05f1c0fc4bccd632f8d3db2ce9f3415a6c73e2f7145d66d4aabf5ab9a18d4b71`).
  Candidate SHA-256:
  `43209b7fe79ce7cfda0caa84825e1f9e845818505ffb65a37a8eed9cf4982d81`.

No full machine gate was run. These are focused test and single-clock RAM
comparisons, not a replacement for machine parity or broad moving acceptance.

The owner suggested testing in the air. `private/scenery-20261103/airborne.py`
started two private recon-route replays (extra pass 0/3), but the owner then
requested a committed handoff and stop. Both owned children were stopped and
the supervisor exited with the expected nonzero result. `airborne-recon/` is
incomplete evidence, with no passing report, sampled flight state or distance
conclusion. Retry in a fresh directory and establish airborne state first.

## Work still needed

Trace requested cells and their finished geometry on an airborne flight over
distant scenery. Establish additional visible buildings/terrain at known world
distances, continuity while moving and turning, and measured performance cost.
Do not infer that extra requests or a larger far plane alone prove success.
Review ring detail transitions, duplicated cells and the fixed-size placement
metadata buffer before enabling a larger walk. Preserve coastline, occlusion,
light, margin and live-simulation isolation checks. Enable accepted changes
only as Remastered presentation overrides; retain Original/Enhanced controls.

The distance score remains unchanged until the actual feature is demonstrated.
