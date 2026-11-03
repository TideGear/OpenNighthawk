# Host integration acceptance

Machine parity proves CPU/memory/timing and simulation equivalence for the
covered inputs. It does not prove that the window, mixer or input dispatcher
delivered the intended result. These checks supplement existing gates; they
do not replace any proof, route check or release requirement.

Run this suite before deploying changes to host sound, rendering or shortcuts,
and before release. `present_light_check.py` also requires `--host-sessions`
and runs it when host/presentation source changed. Full machine gates retain
their existing scope: their pass is not a host acceptance pass.

```
py tools/host_acceptance.py --app build/f117a.exe --data D:/GOG/F-117A --sessions C:/Users/Tideg/AppData/Roaming/F117Recomp/F-117A/sessions --out D:/f117-gate/host-acceptance-NEW
```

Use a new/empty output folder. The local recordings and `save-start` folders
are required; they contain game data and are not checked into Git. Exact input
hashes are pinned in [host-acceptance.json](host-acceptance.json). Missing or
changed inputs fail, rather than silently skipping a scenario. Every process
uses a private save copy, dummy video/audio and a timeout; only processes started
by the suite are managed. Inherited `F117_*` diagnostics are cleared.

| Case | Automated evidence |
|---|---|
| Sound start/acceleration | Real app replay, engine request, changing speed and actual startup PCM |
| Zero-thrust coast | At least 70 correctly parameterized updates, uninterrupted audible sidecar **and final mixed PCM**, changing engine OPL pitch |
| Overlapping effect | Normal bay-door key presses, routed effect 31, non-engine OPL key-on and mixed PCM while moving |
| Stop | Normal brake key, sustained zero speed/thrust, quiet engine PCM after settling |
| Audio observer isolation | Same derived input with diagnostics off; complete 1 MB guest RAM snapshot must match |
| Shoreline below hangars | Ground remains in the reviewed foreground throughout the approach; stale native water footprints fail |
| Hangar motion | Whole cream-light and flag components over a three-second sequence |
| Rear hangars | No leaked yellow door strips throughout motion |
| Transport/edge motion | Near aircraft surface continues to occlude the gray ground in the reviewed region |
| Water crossing | Both lower side margins remain water after the genuine shoreline crossing |
| Render workers | Eight workers at scale 9, in wide and 4:3 moving scenes: every output pixel equals a single-worker redraw of that same frame |
| Live/replay/watched-replay shortcuts | Real SDL event loop: multiple Tab presses, Ctrl+Alt+C, paused unique filenames and ignored held repeat |
| Menu | Tab switching, pause/resume, close, and text contained within the actual top-bar bounds |

The sound input is derived reproducibly from session `20261102-154150`:
retain the recorded zero-thrust coast, add ordinary bay-door keys for overlap,
then apply the brake and let the aircraft stop. It does not call the driver or
edit guest state directly. The bay-door fixture is deliberate: cannon keyboard
and controller injection did not fire a gun in this recorded control setup,
and the checker correctly rejected those trial fixtures. Do not claim cannon
or every weapon is covered by this profile.

`F117_HOST_ACCEPT_LOG=FILE` records forwarding outcomes, original speed/thrust,
actual OPL writes, and PCM sample intervals/energies in the sidecar and final
mixer. It is optional and off during ordinary play. PCM intervals are absolute
sample indices, so audio cannot pass merely because request logs look right;
missing output, timeline gaps and channel misalignment fail. The snapshot
isolation comparison is a targeted observer check, not a substitute for full
machine parity.

Moving PNGs, animations, process logs and `report.json` stay in the evidence
folder; `index.html` is the review gallery. The manifest's image regions and
thresholds describe particular reviewed regressions, not arbitrary scenes.
The flag region follows the pole positions throughout the recorded turn;
clipping a subject out of a test region must not be mistaken for a defect.
There is no automatic screenshot-baseline regeneration.

The worker case runs with presentation threads enabled, using 64-row groups
on the large raster. It requires consecutive comparison records, zero differing
pixels and changing interpolation plans across the recorded interval. The
single-worker reference draws the same captured frame; different host scheduling
cannot excuse a mismatch. The case is part of `--only render` and full acceptance,
not a throughput benchmark. `F117_REMASTER_THREADCHECK=3` logs every comparison;
`F117_REMASTER_THREADCHECK_FROM=CLOCK` avoids expensive comparisons during seek.
These diagnostics are off during ordinary play.

For development, `--only sound`, `--only render` or `--only input` runs a
subset. Subset reports explicitly are **not** full-suite acceptance. Before
using an existing report for deployment, bind it to the exact executable and
current checker/manifest:

```
py tools/host_acceptance.py --app build/f117a.exe --verify-report D:/f117-gate/host-acceptance-NEW/report.json
py tests/test_host_acceptance.py
```

Report verification rejects partial/missing cases and stale executable,
recording, manifest or checker hashes. The ROM-free checker tests run in
Windows CI and deliberately reject absent routing, dropped parameters, silent
PCM, fixed pitch, missing effects, stuck voices, light stripes, missing flag
endpoints, leaked yellow lines, water cutoffs, frozen motion and menu overflow.
The shoreline foreground check also rejects water pasted back over corrected
ground; it supplements the full twelve-view spatial review, rather than proving
every shoreline or theatre from one image region.
CI's ROM-free tests are **not** the local recorded-flight acceptance run.

Always report three independent outcomes: **machine parity**, **host
integration**, and **owner/reference comparison**. This suite reports machine
parity as `not_run` and owner/GOG comparison as `pending`, even when every host
check passes. Human review still covers whole-object shape/occlusion, inherited
GOG culling, hangar/coast alignment, perceived motion and sound fidelity.
Roland music/speech with ROMs, Macintosh recordings, physical controls and
hardware output need their own applicable reference checks before release;
the engine/effect profile does not certify them.

For each new owner report, add the regression at the layer where it escaped:
real input dispatch for shortcuts, captured forwarding plus OPL/PCM for sound,
and moving visual evidence plus a specific invariant for rendering. Keep a
known-broken negative test. A green result at another layer is insufficient.
