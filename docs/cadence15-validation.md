# Fifteen-step play-mode validation — 18 October 2026

The candidate is **15 updates/s at normal speed, 30 during 2x compression,
S=15 and a 60-MIPS virtual instruction budget**, with all fifteen repairs.
This checks capacity, determinism and presentation. It does not recover an
undocumented historical fixed target or equate virtual MIPS with a period CPU.
The completed technical checks support **15/60** for normal play. After this
validation, the owner approved making it the application default on 18 October.
The results below were obtained before promotion, without runtime changes.
The previous eight/20 mode remains available explicitly; reference runners
and legacy recordings retain their original defaults.

Evidence: `D:/f117-gate/cadence15-broad-20261018/` (B). The API and runners
are frozen from `2245c14`; production code is unchanged. Ordinary controls
are recorded, observations are read-only, and both engines run every case.
No invulnerability, memory staging or changes to compression cancellation.

## Completed hidden checks

All twelve interpreter/recompiled pairs agree on final state, timing windows,
recorded inputs and sampled telemetry. Frozen binaries remain unchanged.
There are **960 ten-second airborne windows, 9,600 unique checked seconds
(160 minutes), zero cadence failures**, including **2,140 seconds of actual
compressed airborne flight**. Paired engines are counted once in these totals.
S must stay 15; each settled window allows at most 1.1 updates or mission
seconds from the rate implied by its actual duration and observed mode.
Every selected sample must be at least 100 feet above terrain; signed height
must remain positive. Requests for compression are never substituted for mode 2.
In fact every normal window has exactly 150 updates /10 mission seconds,
and every compressed window exactly 300 /20: **176,100 checked updates**,
with no lost update or mission-second deficit in these windows.

| Case | Checked airborne seconds | Compressed seconds | Outcome |
| --- | ---: | ---: | --- |
| Middle East, new seed | 350 | 0 | 22 enemy launches; natural early ending |
| Korea, new seed | 470 | 0 | 18 launches; natural early ending |
| Kuwait, new seed | 440 | 0 | 23 launches; natural early ending |
| North Cape, new seed | 420 | 0 | 14 launches; natural early ending |
| Home-orbit attempt, seed 0 | 1,160 | 0 | 74 launches; full 20-minute limit |
| Home-orbit attempt, seed 3000 | 1,160 | 0 | 32 launches; full 20-minute limit |
| Clean cruise, Central America | 1,150 | 1,070 | Full 20-minute limit; no enemy launches |
| Clean cruise, North Cape | 1,150 | 1,070 | Full 20-minute limit; no enemy launches |
| Strike and attempted return | 1,760 | 0 | Two releases, one primary hit/credit; return incomplete |
| Landing | 1,020 | 0 | Successful home landing and normal debrief handoff |
| Air-to-air type 5 | 430 | 0 | Shot down; no player release or objective credit |
| Air-to-air type 7 | 90 | 0 | Early ending; no player release or objective credit |

The exposure cases contain **183 official enemy launches**, excluding the
mission pilots, whose enemy launches were not separately counted. Damage and
terminal outcomes remain in the raw results. The home-orbit attempts failed
as compression fixtures: the original game kept normal speed. Their sustained
normal combat is useful evidence, not a compression pass. The clean cruises
fly away from the objective at a requested 12,000 feet, with ordinary gear,
throttle and stick inputs; each independently exceeds 600 compressed seconds.

The corrected landing ends at `79698609194 / a0f5585e5728c15f`, with mission
result 0, pilot status 3 and the normal VGAME exit. The first landing attempt
is retained but excluded: the private harness scaled its takeoff strings
twice, leaving it on the ground. The correction scales executable numeric
deadlines separately from route strings. Discrete pilot commands have
100-ms holds; authored stick/takeoff holds remain unchanged. An additional
type-7 pilot tried releasing at 4,000 rather than 2,500 map units, using the
same normal release key; no weapon-rule change or successful release is claimed.

Both engines also pass a **1,800-second compression soak**: exactly 54,000
updates, 3,600 mission seconds and S15. All twenty clock/dialog/transition
case hashes agree, including normal/restored rates, pause/resume, quit/cancel,
twelve settled transitions and 120 rapid changes.

## Presentation

The earlier repaired six-minute combat capture passes native 4K60 pacing
([presentation.md](presentation.md#verification)). It retains all 5,396 logic
frames and final state exactly, p99 16.9439 ms and no intervals over 25 ms.
The owner watched part of a later flight and found it fine apart from the
subsequently repaired map-edge specks; that was a partial visual review.

A new six-minute Central America excerpt contains 230 checked compressed
airborne seconds. Before its display test, both scan engines and copied rerun
match the interpreter's checkpoints/final state: 8,992 exact logic frames,
8,990 exact pairs and 25,189 isolated extra draws. Frozen files stay unchanged.
The first native attempt (`B/display-4k60-compression/`) is invalid: its
renderer briefly changed from 3840x2160 /59.99 Hz to 1920x1080 /50 Hz and
back during setup. It was stopped and preserved; the unchanged checker
rejects ambiguous display metadata. The owner subsequently confirmed connecting
through remote desktop during that attempt. A fresh retry under `B/display-4k60-compression-retry1/` passes the unchanged checker:
21,565 flight pictures, median 16.6678 ms, p99 **16.9220 ms**, maximum
17.8535 ms, zero intervals over 25 ms, 21.84 ms guest drift and no
interpolation reversals. The target stays D3D11 3840x2160 /59.990002 Hz,
vsync 1, scale 9. All 8,992 logic frames and final
`39987947854 / 9c0d852db7e04a66` match the interpreter reference. It takes
700.30 host seconds including setup; frozen files remain unchanged.
Finish all hidden workers before a display measurement. Logged submission
pacing does not establish perceived motion or physical panel behavior.

## Practical limits

The completed checks support the candidate's timing and capacity. They do
not establish successful automated air-to-air missions or a complete
strike-and-return mission: retain those controller failures. The separate
landing succeeds. `B/acceptance.json` records the technical acceptance and
recommendation, with these limits. Do not promote 15/40: its undamaged compressed North Cape
capacity failure remains valid. No acceptance threshold has been relaxed.
