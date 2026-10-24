# Fifteen-step play-mode validation — 18 October 2026

The candidate is **15 updates/s at normal speed, 30 during 2x compression,
S=15 and a 60-MIPS virtual instruction budget**, with all fifteen repairs.
These hidden checks test capacity, determinism and presentation; they do not
recover an undocumented historical fixed target or equate virtual MIPS with
a period CPU. They support **15/60** for normal play, and on 18 October the
owner approved it as the application default. The results were obtained
before that promotion, without runtime changes. The previous eight/20 mode
remains available explicitly; reference runners and legacy recordings keep
their original defaults. 15/40 is not promoted: its undamaged compressed
North Cape capacity failure stands
([speed-sweep.md](speed-sweep.md#production-fifteen-step-candidate-accepted-18-october)).

Evidence: `D:/f117-gate/cadence15-broad-20261018/` (B below);
`B/acceptance.json` records the technical acceptance, the recommendation and
the limits. The API and runners are frozen from `2245c14`, and the frozen
files stayed unchanged throughout. Ordinary controls are recorded,
observations are read-only and both engines run every case. No
invulnerability, memory staging, change to compression cancellation or
relaxed acceptance threshold.

## Timing windows

All twelve interpreter/recompiled pairs agree on final state, timing windows,
recorded inputs and sampled telemetry. The rule: S must stay 15; each settled
ten-second window allows at most 1.1 updates or mission seconds from the rate
implied by its actual duration and observed mode; every selected sample must
be at least 100 feet above terrain, with positive signed height; a request
for compression is never counted as mode 2.

The result: **960 airborne windows, 9,600 unique checked seconds (160
minutes), zero cadence failures**, including **2,140 seconds of actual
compressed airborne flight** (paired engines counted once). Every normal
window has exactly 150 updates / 10 mission seconds and every compressed
window exactly 300 / 20: **176,100 checked updates**, with no lost update or
mission-second deficit.

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

- **Exposure.** The exposure cases contain **183 official enemy launches**;
  the mission pilots' enemy launches were not counted separately. Damage and
  terminal outcomes are in the raw results.
- **Home-orbit attempts** failed as compression fixtures (the original game
  kept normal speed); they count as sustained normal combat, not as a
  compression pass.
- **Clean cruises** fly away from the objective at a requested 12,000 feet
  with ordinary gear, throttle and stick inputs; each exceeds 600 compressed
  seconds.
- **Landing** ends at `79698609194 / a0f5585e5728c15f`, mission result 0,
  pilot status 3 and the normal VGAME exit. The first attempt is retained but
  excluded: the private harness scaled its takeoff strings twice and left it
  on the ground; the correction scales executable numeric deadlines
  separately from route strings. Discrete pilot commands hold for 100 ms;
  authored stick/takeoff holds are unchanged.
- **Air-to-air type 7.** An additional pilot released at 4,000 rather than
  2,500 map units with the normal release key; no weapon-rule change or
  successful release is claimed.
- **Compression soak.** Both engines pass 1,800 compressed seconds: exactly
  54,000 updates, 3,600 mission seconds and S15. All twenty
  clock/dialog/transition case hashes agree, including normal/restored rates,
  pause/resume, quit/cancel, twelve settled transitions and 120 rapid changes.

## Presentation

Native 4K60 captures use D3D11 3840x2160 at 59.990002 Hz, vsync on and scale
9, judged by the unchanged checker
([presentation.md](presentation.md#fullscreen-4k60-timing-and-the-threaded-presentation)).

- **The repaired six-minute combat capture** passes: all 5,396 logic frames
  and the final state exact, p99 16.9439 ms, no intervals over 25 ms. The
  owner watched part of a later flight and found it fine apart from the
  map-edge specks repaired since; that was a partial visual review.
- **A six-minute Central America excerpt** with 230 checked compressed
  airborne seconds. Before the display test, both scan engines and copied
  rerun match the interpreter's checkpoints and final state: 8,992 exact
  logic frames, 8,990 exact pairs and 25,189 isolated extra draws. The first
  native attempt (`B/display-4k60-compression/`) is invalid and retained: its
  renderer briefly changed to 1920x1080 at 50 Hz during setup, when the owner
  had connected through remote desktop, and the checker rejects ambiguous
  display metadata. The retry (`B/display-4k60-compression-retry1/`) passes:
  21,565 flight pictures, median 16.6678 ms, p99 **16.9220 ms**, maximum
  17.8535 ms, zero intervals over 25 ms, 21.84 ms guest drift and no
  interpolation reversals; all 8,992 logic frames and the final state
  `39987947854 / 9c0d852db7e04a66` match the interpreter reference. It took
  700.30 host seconds including setup.

Finish all hidden workers before a display measurement. Logged submission
pacing does not establish perceived motion or physical panel behaviour.

## Limits

The checks support the candidate's timing and capacity. They do not
establish a successful automated air-to-air mission or a complete
strike-and-return mission; those controller failures are retained. The
separate landing succeeds.
