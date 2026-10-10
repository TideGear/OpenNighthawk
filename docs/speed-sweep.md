# Emulated CPU speed and VGAME's frame-rate controller: measurements

Partial results of a sweep of the emulated machine's speed (9 October 2026), for choosing the
app's default speed or a frame limiter. The default was decided on 9 October 2026 (the section at
the end). Reproduce with
`py tools/speed_sweep.py --data D:/GOG/F-117A --out DIR --speeds 3 ... 40 386 --seconds 300`
(each route's 9 MIPS run first, then the others; `--table-only` reprints).

## The controller (read from the VGAME listing)

- **Frame loop** `0x1BCE`: `[0x262A]=1` at `0x1BFA`, player flight `0x1EB0`, mission `0x3ADA`.
  The frame counter `[0x3D8E]` steps once a frame (`0x4359`); the mission clock `[0x9912]` steps
  once every S frames (`0x436C`). `[0x3DA6]` (`0x4423`) counts the same frames, reset each window.
- **Game tick** `0x1E79` (from the IRQ0 handler `0x1C9B`): `[0x2646]`, `[0x264A]`, `[0x2648]` +1.
  The timer is calibrated against vertical retrace at install (`0x1D88`: reload `[0x2632]` =
  17024, 70.09 Hz, accepted by the 4..6 check at `0x1DE4`). Every 20th tick `0x1D01` polls until
  retrace and restarts the PIT there; if it had to wait (CX non-zero, `0x1D7A`) it increments the
  divider `[0x2640]` (`0x1D82`), so the next interrupt is not a game tick. Measured game ticks a
  second: 70.08 at 7-8.5 MIPS, 67.9-68.2 at 9-10, 66.75-66.8 at 11 MIPS and up (20 ticks per 21
  frames), 69.2 on the 386 profile.
- **Controller** `0x441D` (every 4S frames): `ticks = [0x2646] + 2S(1 - 2[0x43E8])`, floor 4;
  `rate = 960S / ticks`; S becomes `(rate+2)>>2` when `|4S - rate| > 3`; `0x0D441` clamps S to
  `[4 - [0x3DA8], 15]` and sets `[0x43E8]` = `clamp((9 - 120/S)>>1, 1, 4)` only when the raw S was
  above 15, else 0.
- **Frame wait** `0x0409`: `push [0x43E8]; call 0x04C7`, which spins until the tick byte
  `[0x264A]` has advanced by `[0x43E8]`. Verified running in this build: an f117run trace at 40
  MIPS (boot_to_flight, no fixes, VGAME + 30 s) shows the frame end `0x441D`, then `0x0409`, then
  `0x04C7` spinning while `[0x264A]` goes 133, 134, 135 (`[0x43E8]` = 2): about 1.0 M of the
  frame's 2.4 M instructions. A second wait, `0x0D866` on `[0x262A]`, holds each frame's sky draw
  to the next game tick.

**Why S lags the frames drawn.** At equilibrium S is about `60F / (R + F/2)` for F frames a
second and R game ticks a second: the controller assumes 60 ticks a second (240 per 4S frames)
while the retrace-locked tick is 66.75-70.1, and the `+2S` term adds half a tick a frame. With
S below F, every per-second quantity divided by S (and the mission clock) runs F/S times real
time: 1.29 at GOG's 9 MIPS on boot_to_flight (S 9 at 11.6 fps). When the raw S passes 15,
`[0x43E8]` both adds that many ticks of wait to each frame and removes them again from the
measurement (`-4S[0x43E8]` of the correction), so S stays clamped at 15 and the frame rate
settles near 16.7 at any speed from about 13 MIPS up; the world then runs 16.7/15 = 1.11 times
real time.

**D1's oscillation is not reproduced.** No run at 3 to 40 MIPS or on the 386 profile showed a
fall of S from 13+ to 5 or below (`swings` 0 in all 199 flights). The `[0x43E8]` wait keeps the
window's ticks above the correction, so the unsigned underflow the D1 entry describes cannot
occur in the original's own loop; the Reimp's sweep that predicted it ran the controller without
the `0x0409` wait (Reimp `src/core/compose.h:166` omits it).

## Results (300 s flights, no fixes, recomp engine)

Two routes (S and `[0x43E8]` as % of samples; clock is mission seconds per emulated second;
ticks are game ticks per second):

| route | speed | fps | S | [0x43E8] | clock/s | fps/S | ticks/s |
|---|---|---|---|---|---|---|---|
| boot_to_flight | 386 | 6.91 | 5 | 0 | 1.383 | 1.382 | 69.20 |
| boot_to_flight | 3 | 4.23 | 4 | 0 | 1.058 | 1.058 | 69.58 |
| boot_to_flight | 5 | 6.88 | 5 | 0 | 1.373 | 1.375 | 69.88 |
| boot_to_flight | 7 | 9.87 | 8 (89%), 7 | 0 | 1.254 | 1.250 | 70.09 |
| boot_to_flight | 9 | 11.60 | 9 | 0 | 1.288 | 1.289 | 67.89 |
| boot_to_flight | 14 | 16.68 | 13 | 0 | 1.281 | 1.284 | 66.75 |
| boot_to_flight | 16 | 16.68 | 15 | 1 | 1.112 | 1.112 | 66.75 |
| boot_to_flight | 20 | 16.72 | 15 | 2 | 1.115 | 1.115 | 66.75 |
| boot_to_flight | 40 | 16.69 | 15 | 3 | 1.115 | 1.113 | 66.75 |
| middle_east_strike | 386 | 6.35 | 5 | 0 | 1.271 | 1.270 | 67.42 |
| middle_east_strike | 9 | 16.91 | 15 (95%), 8 | 2 (80%), 1, 0 | 1.159 | 1.145 | 68.06 |
| middle_east_strike | 14 | 16.82 | 15 (95%), 13 | 3 (95%) | 1.132 | 1.129 | 66.78 |
| middle_east_strike | 16 | 16.86 | 15 (95%), 13 | 3 (94%) | 1.136 | 1.131 | 66.78 |
| middle_east_strike | 20 | 16.73 | 15 | 2 | 1.115 | 1.115 | 66.78 |
| middle_east_strike | 40 | 16.71 | 15 | 4 (95%), 3 | 1.112 | 1.114 | 66.78 |

Means over the nine typed flight routes (boot_to_flight, the eight theatre routes):

| speed | fps | S (frame-weighted) | clock/s | fps/S |
|---|---|---|---|---|
| 386 profile | 8.60 | 6.79 | 1.284 | 1.276 |
| 3 | 8.80 | 7.18 | 1.246 | 1.218 |
| 5 | 11.28 | 8.83 | 1.318 | 1.290 |
| 7 | 13.11 | 10.83 | 1.238 | 1.225 |
| 8 | 15.98 | 13.28 | 1.235 | 1.216 |
| 9 (GOG) | 16.27 | 14.10 | 1.172 | 1.160 |
| 10 | 16.52 | 14.27 | 1.173 | 1.163 |
| 12 | 16.35 | 14.24 | 1.155 | 1.152 |
| 14 | 16.77 | 14.49 | 1.162 | 1.162 |
| 16 | 16.79 | 14.96 | 1.123 | 1.123 |
| 20 | 16.75 | 14.99 | 1.118 | 1.118 |
| 40 | 16.64 | 15.00 | 1.108 | 1.109 |

The frame rate depends on the scene far more than on the speed above 8 MIPS: in the air most
routes already draw about 16.7 frames a second at 8-9 MIPS (S 15), while the parked jet of
boot_to_flight draws 11.6 at 9 MIPS. No speed gives a clock ratio of 1.00; the original on the
386 profile runs its world about 1.28-1.38 times real time.

## Caveats and what is not established

- **Missions differ by speed** below 9 MIPS, on the 386 profile and on some routes above 9
  (the table flags DIFFERENT MISSION): the front end's timing seeds the generator. Per-route
  comparisons across speeds are therefore of different missions; the timing figures are not
  affected, the engagement figures are.
- **Enemy effectiveness is not measured.** The tracker of weapons fired at the player (slots 0-7
  at `0x3C3A`) was corrected during the sweep (a free slot can read -1), so the launch counts in
  this sweep's `runs.jsonl` mix both versions; the typed routes also draw too few launches (0-80
  a flight, almost no proximity bursts) to compare speeds. A controlled threat profile over many
  missions is still to do. All AI, launches and guidance run per drawn frame scaled by S, not on
  the tick (Reimp `src/core/frame_weapons.c`, VGAME `0x5046`, `0x5852`, `0x683E`), so the
  "player on frames, AI on the tick" explanation does not apply.
- **Not run:** the pilots (own hits, kills, deliveries); a controlled threat profile;
  a GOG DOSBox measurement of
  the game tick rate in flight (this machine loses one tick in 21 frames at 11 MIPS and up;
  whether DOSBox does depends on its interrupt latency at the retrace poll).

## D1 follow-up (9 October 2026)

Fresh sweep, 90 flights: all nine typed routes, 300 guest seconds requested,
9, 12, 16, 20 and 40 MIPS, with and without D1. The corrected launch tracker
is used throughout. Command:

```
py tools/speed_sweep.py --data D:/GOG/F-117A --out D:/f117-gate/speedsweep/d1 --speeds 9 12 16 20 40 --fix none D1 --seconds 300 --jobs 16
```

Raw results and samples are in that output directory. No run had an error
or the reported 15-to-3 swing. All nine D1 flights at 9 MIPS have exactly
the same final hashes as their unfixed baselines: the fix is inactive there.

Means over complete flights (the first five seconds omitted as above):

| MIPS | complete pairs | no fix: fps / S mean / clock per second | D1: fps / S mean / clock per second |
|---|---|---|---|
| 9 | 9 | 16.273 / 14.100 / 1.1718 | 16.273 / 14.100 / 1.1718 |
| 12 | 9 | 16.347 / 14.241 / 1.1552 | 11.601 / 9.000 / 1.2882 |
| 16 | 9 | 16.793 / 14.957 / 1.1232 | 11.600 / 9.000 / 1.2882 |
| 20 | 9 | 16.753 / 14.989 / 1.1179 | 11.600 / 9.000 / 1.2882 |
| 40 | 8 | 16.716 / 15.000 / 1.1144 | 11.600 / 9.000 / 1.2882 |

Korea at 40 MIPS leaves VGAME for END after 45.25 sampled flight seconds
without D1 and 40.39 with it. Those two short flights remain in the raw data
and are excluded from the means. Mission identifiers differ across speeds,
so these figures establish pacing, not relative combat effectiveness.

**Implication for the default:** D1 reproduces the parked jet's GOG pacing
on fast machines, not every scene's GOG pacing. In the complete flights it
reduces frames drawn but increases world-clock speed relative to the same
fast CPU without the fix. It does not make the world run at real time. The
default remains unchanged pending the owner's decision and the remaining
combat measurements.

**The 15 fps hypothesis needs an actual 15 fps experiment.** Four game ticks
are not four 60 Hz ticks: the measured tick rate is 66.75-70.09 Hz, so a
four-tick wait alone corresponds to about 16.69-17.52 fps. The earlier
"15 fps (four ticks)" wording assumed the controller's nominal 60 Hz rather
than the timer's measured rate. A cap at 15 fps also does not by itself prove
S will stay at 15. The actual experiment below measures that controller.

## Control-response measurements

`stick_response.py` now accepts `--ips`, `--fix`, `--time-us` and a typed
`--front-route`. It retains raw pitch/roll samples, mission identifiers, the
number of completed taps and early exits; angular differences wrap at one
turn. Fixes are refused for the 386 profile while they still charge
instruction clocks there. Existing reference-machine modes keep their
original input path.

The first sweep used `cargo_pilot.input` at all speeds. Absolute replay
times selected different missions (and at 12 MIPS a different theatre), so
those results cannot compare control response on one mission. Program-relative
inputs from `middle_east_strike.args` keep the requested menus aligned, but
the random seed still changes with speed.

A read-only probe found START `0x7379` calling `0x8607` for the BIOS tick
and `0x96BC` for srand. At boot clock 700000000000000, the initial random
states at DS:AE8C are 31233, 31231, 31231, 31229 and 31227 at 9, 12, 16, 20
and 40 MIPS. The BIOS tick's low word matches each seed. Offsetting only
the boot clock by 0, 110, 110, 220 and 330 milliseconds produced the same
mission in all ten flights: primary type 1, target 2 at (26080, 9408),
secondary type 1, target 17, departure and home 41. The finer seed reader
captured 31233 in both final 40 MIPS runs; the earlier eight reports predate
that reader and have a null `start_seed`. No guest-memory writes are used.

For example, the aligned 40 MIPS D1 run is reproduced with:

```
py tools/stick_response.py --machine machine --data D:/GOG/F-117A --out D:/f117-gate/speedsweep/controls-check --ips 40000000 --fix D1 --front-route tools/routes/middle_east_strike.args --time-us 700000000330000
```

Descriptive results for the 200 ms taps, in original angle units (65536 per
turn). Arrays retain each observed response rather than hiding differences
between repetitions in a mean:

| MIPS / fix | taps measured / planned | S during taps | pitch deltas | roll deltas |
|---|---|---|---|---|
| 9 / none or D1 | 29 / 30 | 14, 15 | -505, -255, -116 | -1787, -1794, -905 |
| 12 / none | 27 / 30 | 15 | -378, -272, -193 | -1789, -1801 |
| 12 / D1 | 24 / 30 | 9 | -421, -224 | -1989, -1992 |
| 16 / none | 24 / 30 | 15 | -379, -259 | -1789, -2400 |
| 16 / D1 | 24 / 30 | 9 | -427, -269 | -1990, -1992 |
| 20 / none | 24 / 30 | 15 | -382, -357 | -1790, -2397 |
| 20 / D1 | 29 / 30 | 9 | -420, -191, 240 | -2983, -2986, -2991 |
| 40 / none | 23 / 30 | 15 | -382, -305 | -1788, -1795 |
| 40 / D1 | 24 / 30 | 9 | -419, -221 | -1988, -1992 |

Artifacts: `D:/f117-gate/speedsweep/controls` (absolute replay),
`controls-typed` (program-relative, common boot clock), and `controls-aligned`
(adjusted boot clocks). The repeated taps can end the flight before all 30
are observed; a response's `each` array and `taps_measured` show its actual
sample count. These are observed responses of progressively diverging
flights, not independent identical starting states for each tap. More
missions and controlled combat runs are still required before claiming a
highest speed at which nothing breaks.

The common-clock typed 386 run selected another mission, stayed at AGL 128
and reported zero response to every tap. It did not establish an airborne
control comparison; its takeoff inputs need checking before using that run.

## Actual 15 FPS limiter experiment (9 October 2026)

An isolated checkout at `28f2a50`, `D:/f117-wt/speed15-experiment`, changes
only `D1_FPS_X10` from 116 to 150 in `src/fixes/fixes.c`. It uses the same
frame-entry wait as D1, the original controller is untouched, and the wait
remains inactive at 9 MIPS and below. The experiment is not the shipped D1
and does not change the app default.

Build that checkout with `build.cmd -DF117R_BUILD_APP=OFF
-DF117R_GEN_DIR=C:/Users/Tideg/f117-recomp-local/gen`, then run:

```
$env:F117R_MACHINE_API = 'D:/f117-wt/speed15-experiment/build/f117machine_api.dll'
py tools/speed_sweep.py --data D:/GOG/F-117A --out D:/f117-gate/speed15/sweep --speeds 9 12 16 20 40 --fix D1 --seconds 300 --jobs 8
Remove-Item Env:F117R_MACHINE_API
```

`D:/f117-gate/speed15/experiment.json` records the exact source delta,
command and DLL SHA-256. Forty-five unfixed rows from the preceding D1
sweep were retained as baselines, rather than rerun; those rows are not
new experimental flights. The output directory is separate from both the
unfixed and production-D1 studies. `runs.jsonl` and `samples/` retain the
new fixed flights; `summarize.py` reports complete flights separately from
early exits.

All 45 experimental flights finished without errors or 15-to-3 swings.
Means over complete flights, omitting the first five seconds:

| MIPS | complete flights | drawn FPS | S mean | mission seconds per emulated second |
|---|---|---|---|---|
| 9 (wait inactive) | 9 | 16.273 | 14.100 | 1.1718 |
| 12 | 9 | 14.794 | 11.868 | 1.2464 |
| 16 | 9 | 15.000 | 12.000 | 1.2501 |
| 20 | 9 | 14.999 | 12.000 | 1.2509 |
| 40 | 8 | 15.000 | 12.000 | 1.2500 |

All nine 9 MIPS final hashes equal the retained unfixed baselines. At 40
MIPS Korea leaves VGAME for END after 41.50 sampled flight seconds; that
short flight is retained in the raw data and excluded from this table.
Mission identifiers still differ across CPU speeds, as in the other pacing
sweeps. A cap is an upper bound: it cannot raise slower scenes to 15 FPS.

**The cap does not make the clock real.** At 15 drawn FPS the controller
settles at S = 12, with no overrun correction, and the mission clock runs
about 1.25 times real time. This agrees with the controller's arithmetic:
`60F / (R + F/2)` is about 12.12 at F = 15 and R = 66.75, and its integer
estimate rounds to 12. The earlier hypothesis assumed a 15 FPS cap would
hold S at its clamp of 15; it does not. This result rejects that hypothesis,
not the original's higher frame rate. Combat effectiveness remains a
separate measurement on matching missions.

**Independent engine check.** The same experimental DLL also flew
`boot_to_flight` and `middle_east_strike` at 40 MIPS under the interpreter
for 300 seconds each. All 30,000 observation rows per flight and the final
machine hashes are identical to recompilation: `05a9b3747a918a85` and
`814b693377e773f9`. Artifacts: `D:/f117-gate/speed15/pair-interp/` and
`compare_pairs.py`. The two interpreter flights took 318 and 407 s beside
the full gate. These checks verify this timing observation in both engines;
they do not establish combat equivalence at different speeds.

## Fast-machine default (9 October 2026)

**Decision.** The app (`f117a`) now runs its instruction model at 20 million instructions a second (`MACHINE_APP_IPS`, `--ips` and `ips =` override it). `f117run` and every recorded route keep GOG DOSBox's 9 million, so no route hash changes. The fix D1 stays off. No limiter is added: above about 13 MIPS the game's own clamp holds its frame estimate S at 15, and the drawn rate settles near 16.7 frames a second.

**Correction to the earlier 15 fps note.** The experiment above (a 15 fps limiter on top of the fast machine) is not the shipped behaviour and is worse: S settles at 12 and the mission clock runs about 1.25 times real time. "The 15 cap" in the default is the game's own clamp, reached without any limiter; at 20 MIPS it gives a clock of about 1.12.

**Matrix.** Nine typed routes, eight boot-clock offsets (0, 3, 7, 11, 15, 19, 23 and 27 seconds, each a different mission seed), and speeds 9, 16 and 20 MIPS: 214 flights of 300 guest seconds, no fixes, recomp engine. Command:

```
py tools/speed_sweep.py --data D:/GOG/F-117A --out D:/f117-gate/fastdefault/matrix --speeds 9 16 20 --offsets-ms 0 3000 7000 11000 15000 19000 23000 27000 --seconds 300 --jobs 24
py tools/speed_compare.py D:/f117-gate/fastdefault/matrix/runs.jsonl
```

`--offsets-ms` moves the boot clock, which moves START's seed, so each offset draws its own missions. Missions cannot be paired across speeds (a change of speed also changes the seed), so each speed is a sample of missions from the same generator and is compared as a distribution.

| MIPS | complete flights | drawn FPS | S mean | swings | mission clock per second |
|---|---|---|---|---|---|
| 9 (GOG pace) | 70 | 15.27 | 12.95 | 0 | 1.202 |
| 16 | 68 | 16.77 | 14.88 | 0 | 1.129 |
| 20 (default) | 70 | 16.77 | 14.98 | 0 | 1.120 |

The 9 MIPS mean includes the parked jet (11.6 frames a second); the 16 and 20 MIPS figures hold the parked jet at 16.7.

Combat, complete flights, with 95% bootstrap intervals:

| MIPS | launches per flight | bursts per flight | misses per flight | player hits per flight | flights with a hit |
|---|---|---|---|---|---|
| 9 | 0.26 (0.11 to 0.43) | 0.04 | 0.17 | 0.057 | 2 of 70 |
| 16 | 0.18 (0.07 to 0.29) | 0.03 | 0.10 | 0.029 | 1 of 68 |
| 20 | 0.30 (0.14 to 0.49) | 0.03 | 0.20 | 0.029 | 1 of 70 |

Differences from 9 MIPS: launches −0.08 (−0.28 to +0.11) at 16, +0.04 (−0.19 to +0.29) at 20; player hits −0.03 at both (−0.14 to +0.06). No difference is detected.

**What this does not establish.** The power is very low. These routes give about a quarter of a launch per flight and rarely hit the player, so a halving or doubling of combat would not show here. The matrix supports "no large change in combat rate or in hits". It does not support "combat is unchanged". A controlled threat profile (missions that engage the player on purpose, many flights per speed) is still needed for that claim.

**Errors and early exits.** Every error and every early exit is in Korea's route, at a seed where the scripted front end does not reach VGAME (one error at 9 MIPS, one at 16 MIPS) or where the mission ends near 40 seconds (one at 9, two at 16, one at 20 MIPS). Both kinds appear at 9 MIPS as well, so they are not speed effects. Two Korea flights at seed 23 seconds were not run at 16 or 20 MIPS, because their 9 MIPS baseline never reaches VGAME.

**Mission mix.** Objective 1 appears in 9 of 70 flights at 9 MIPS, 9 of 68 at 16 and 14 of 70 at 20. Mission generation depends on the seed, so this is a sample difference, not a speed effect that has been tested. It is recorded here so it is not forgotten.

**Control response.** The aligned tap measurements in the control-response table above (middle_east_strike, 9 to 40 MIPS, no fix) show pitch and roll responses within the same spread at 9, 16, 20 and 40 MIPS. The sample is 23 to 29 taps per speed, with the usual divergence along the sequence.

**Real-time headroom.** A single recompiled run of `boot_to_flight` at 9 MIPS (the gate's route check, run while other work shared the machine) reported 68.7 million instructions a second on this machine. 20 million is about 3.4 times real time there.

**Analysis.** `tools/speed_compare.py` reads `runs.jsonl` and prints the per-speed summary, the bootstrap intervals and the mission mix; the tables above are its output. The sweep is `tools/speed_sweep.py` (with `--offsets-ms`, added for this matrix).

## Controlled threat-profile combat measurement (9-10 Oct 2026)

**Superseded observer, corrected 10 Oct.** The figures in this historical
section do not establish the claimed size or cause of a combat gap. The
observer discarded lifetimes above 32767, hiding fresh SA-5 launches at
S >= 14, and detected bursts only when sampled life equalled exactly `2*S`,
missing more at higher frame rates. It also discarded damage counters on
early exits. The latest tools use the game's official launch counter,
observe valid unsigned lifetimes, exclude seeker-abandoned FFFF slots,
recognise the burst reset across samples, and retain the last damage counter
on exit. `damage_hits` is a legacy field name for damage selections, not
missile impacts: one impact can increment it difficulty+1 times, and other
damage sources can increment it too. The adaptive pilot also scheduled its
initial throttle/rotation/observation at fixed instruction counts, rotating
at 8.5 seconds at 20 MIPS versus 18.9 at 9 MIPS; matched missions exposed
pre-engagement crashes. These startup times now preserve their authored
9 MIPS seconds at every speed. Older combat tables throughout this document
share the observer limitations; the threat matrix additionally has this
takeoff confound. Timing and position samples remain valid observations of
the flights actually flown, rather than equivalent input schedules.
The old orbit controller also mishandled negative word differences and
steered outward when outside its requested radius. Both are corrected;
`tests/test_threat_profile.py` checks wrapping and radial correction in
eight directions. Similar mean radii in the old matrix did not establish
equivalent controlled exposure.

The matrix above flies a fixed scripted path that passes the primary target once and
then holds a straight heading; it draws about a quarter of a launch a flight, too
sparse to say anything about combat. `tools/threat_profile.py` instead drives VGAME
with live telemetry: it approaches and strikes the primary target as `strike_pilot.py`
does, then - once within 6,000 units of it or on primary credit - orbits the target's
own coordinates at a fixed radius and altitude for the rest of the flight, so repeated
passes through whatever defends the target give many more chances for an enemy launch
than one pass-through. No enemy site's position is read or assumed; the orbit is
centred on the same point the scripted routes also fly toward. The weapons-at-player
telemetry (slots 0-7 of the table at 0x3C3A) is the same reading `speed_sweep.py` uses.

```
py tools/threat_profile.py --data D:/GOG/F-117A --out OUT --routes middle_east_strike korea_strike kuwait_strike north_cape_strike --speeds 9 16 20 --seconds 600 --offsets-ms 0 2500 5000 ... 27500 --jobs 24
```

Twelve boot-clock offsets (0-27,500 ms) times four ground-strike theatres times three
speeds: 144 flights, all complete, no errors (interrupted partway through to free the
machine for another job - see `f117-check-resources-before-parallel-jobs` in the
session's notes - and finished afterward with `runs.jsonl` picking up where it left
off). A far richer sample than the matrix above:

| MIPS | flights | mean launches/flight | mean bursts/flight | flights with a burst |
|---|---|---|---|---|
| 9 | 48 | 9.08 | 2.10 | 38 of 48 |
| 16 | 48 | 6.48 | 1.10 | 21 of 48 |
| 20 | 48 | 3.81 | 0.58 | 12 of 48 |

30-36 times the matrix's launch rate, and a burst (a proximity detonation, not just a
launch) on most 9 MIPS flights. **A real difference by speed also shows here, and it
does not reduce to a sampling artefact.** More flights end early at higher speed (9
MIPS: 13 of 48 left VGAME before the 600 s budget; 16 MIPS: 15 of 48; 20 MIPS: 18 of
48 - consistent with the mission clock running faster than real time at higher speed,
so a fixed assumed-second budget holds more in-game flight time and reaches bingo fuel
sooner), which shortens orbit exposure and would alone lower the per-flight count. But
normalising by exposure time (launches and bursts per 100 orbit-seconds, excluding
flights that never reached the orbit) still shows the same direction and a similar
size:

| MIPS | orbit-seconds | launches/100 orbit-s | bursts/100 orbit-s |
|---|---|---|---|
| 9 | 10,438 | 3.40 | 0.70 |
| 16 | 8,981 | 2.51 | 0.40 |
| 20 | 7,395 | 2.18 | 0.30 |

A bootstrap over the 48-flight samples (`random.seed(12345)`, 5,000 resamples) gives a
95% interval for the mean launches/flight that excludes zero for both differences from
9 MIPS: 16 MIPS -2.62 (-4.67 to -0.56), 20 MIPS -5.25 (-7.02 to -3.46). **This is a real
effect, not noise**, on top of the exposure-time-normalised rate also falling by the
same amount. It is not yet explained: it could be the frame-rate dependence this
document's controller section already describes (AI launches and guidance run per
drawn frame scaled by S, and S and the drawn frame rate both differ by speed), or a
further effect of the mission-clock scaling, or something else; the mechanism has not
been traced in the game's listing, and that is the natural next step - not another
sweep, since the rate difference is already well established by this one.

**What this does not establish:** each speed still draws different missions from
different seeds (the standing caveat across every sweep in this document), so this
measures "launch rate on this generator's mix of missions at this speed," not a single
matched mission's rate at three speeds. Raw data: `D:/f117-gate/threatmatrix/runs.jsonl`
and `samples/`.

## Independent combat investigation (10 October 2026)

The Reimp (`9e0716dc502cd64501b0d52030ebef041825f907`) separates
simulation and rendering, paces simulation from S, and starts S at 5
(`vginit.c`). Its default game timer is 60 Hz; original-world-speed mode
uses 70.086 Hz with a corresponding step-rate adjustment (`main.c`). Its
simulation-clock tests check tick counts across display rates, not combat.
`frame_weapons.c` still uses signed lifetime in the proximity guard.
Its low-S default avoids that overflow; this is not a direct lifetime fix.

Independent listing and staged-machine checks found two mechanisms:

- SA-5 initial life is `150*S*16`: 33,600/36,000 at S=14/15, but VGAME
  `6F19` uses signed JLE after comparing life with `4*S`. Active life is
  tested against zero at `708D`. `D1TTL` corrects valid unsigned lifetimes,
  preserving suppression of seeker-abandoned FFFF slots. Seven staged
  lifetime cases pass under both engines with and without the fix; engine
  hashes match at every case. A natural 600-second Middle East flight at
  20 MIPS, clock offset 2720 ms, restores an SA-5 burst at 293.32 seconds.
  Its fixed interpreter/recomp hash is `0559ff3b220737ef`; launch records
  and all telemetry rows match exactly.
- Proximity reach is `(speed<<3)/S` at `6EF5`, compared unsigned with slant
  at `6EF9`/`6EFB`. This is both a tunnelling allowance and the admitted
  near-miss radius, which shrinks as S rises. A staged positive-life SA-12
  at equal starting geometry damages the aircraft at S=8 but misses at
  S=15. The separate experimental `D1PROX` option puts an S=9 floor under
  incoming reach, retaining the original larger allowance below S=9.
  S=9 is an explicit balance reference, not an original warhead constant.

The corrected pilot/observer flew four theatres, six seed families
(0..12500 ms, step 2500), and four arms: 9/16/20 MIPS and 20+D1TTL.
All 96 flights have usable observations and all 24 groups have identical
recorded mission identities. START clock shifts of +110/+220 ms at 16/20
MIPS align the seed families. No guest memory is staged in these flights.
Early exits stay in the results. Damage below subtracts the first observed
counter from the last and counts damage selections, not missile impacts.

| Arm | Flights | Early exits | Launches/flight | Bursts/flight | Damage selections/flight | Orbit seconds |
|---|---|---|---|---|---|---|
| 9 | 24 | 9 | 8.5417 | 4.1250 | 8.2500 | 4734.8 |
| 16 | 24 | 5 | 8.4583 | 3.5000 | 7.0000 | 5023.6 |
| 20 | 24 | 6 | 7.7500 | 2.7500 | 5.5000 | 5305.2 |
| 20 + D1TTL | 24 | 6 | 7.7083 | 2.8333 | 5.6667 | 5296.4 |

Paired bootstrap (5,000 resamples, seed 12345), 20 minus 9 MIPS:
launches -0.7917 (95% -2.0417 to +0.4583), bursts -1.3750 (-2.1667
to -0.6250), damage selections -2.7500 (-4.3333 to -1.2500).
The corrected evidence does not establish the earlier large upstream
launch deficit. A burst/damage deficit remains, and D1TTL alone does not
close it: its burst difference from 9 is -1.2917 (-2.0417 to -0.5833).
The proximity-distance option was then tested on the same 24 seed families
at 20 MIPS, with both D1TTL and D1PROX enabled: 7 early exits, 7.3333
launches, 3.3750 bursts and 6.7500 damage selections per flight, with 4669.8
orbit seconds. Compared with D1TTL alone, bursts increase +0.5417
(95% paired CI +0.2083 to +0.8750) and damage selections +1.0833
(+0.4167 to +1.7500). The gap from 9 MIPS is reduced, **not eliminated**:
bursts -0.7500 (-1.4167 to -0.1667), damage selections -1.5000
(-2.8333 to -0.3333). This option is useful partial mitigation, not a claim
that combat is CPU-independent. Extra damage changes later trajectories
and survival time, so per-flight counts and post-damage orbit exposure
must not be mistaken for unchanged encounters.

Both engines also replay the combined natural Middle East flight at offset
2720 identically: hash `7b5cc08d65027187`, 10 launches, 6 bursts, 14 total
damage selections; every CSV row and launch record matches. All five staged
proximity cases agree between engines, with unchanged S=8/9 control hashes.
The 9 MIPS cohort's burst-time S distribution is 7:23, 8:46, 9:11, 10:7,
11:3, 12:1, 13:2, 14:3, 15:3 (mean 8.6465); its launch-time mean is 8.9561.
Thus a parked S=9 reference is not the whole low-speed combat distribution.
Further calibration should use held-out missions, rather than tune until
this cohort's mean matches. A difference interval including zero is not
proof of combat equivalence; trajectories and the original's clocks vary.

A read-only follow-up traced raw lifetimes for the Middle East seed family
2500 at 9/20 MIPS (`D:/f117-gate/ghost_trace.py`, outputs `ghost-trace/`).
Both replay hashes match the cohort exactly: `2ae3a7380acfa83d` and
`99bf21292c32d48d`. Seeker-abandoned slots accumulate 1104.8/1894.0
slot-seconds above 36000, but the 9 MIPS flight exits at 472.9 seconds;
over that common window the totals are 1104.8/1295.2. These values measure
occupancy, not suppressed launches. Ground launch `54B4` and air launch
`65C3` require their chosen `(unit_index & 7)` slot to have zero lifetime,
so even one abandoned slot can block a particular launcher. Both flights
still record 11 launches. This is a confirmed lifetime-underflow defect,
also retained in Reimp, and a candidate for a separate saturating-countdown
fix; one trajectory-diverged pair does not establish its contribution to
the CPU-speed deficit. D1TTL currently guards damage from these slots but
does not reclaim them.

The separate staged reproduction `D:/f117-gate/slot_underflow_probe.py`
confirms positive SA-5 life 1000 becoming FFFF after a lost-lock seeker
check at both S=8 and S=15. Both engines agree, including case hashes
`76d07a07e0d330d9` / `7828a2d70478e5a1`, with no damage. This proves the
underflow mechanism independently of the natural-flight trace; it does
not measure the effect of repairing it. A future repair should preserve
zero at the countdown, with positive-life controls and same-option-set
9/20 MIPS cohorts. Current D1TTL's bound cannot distinguish a sufficiently
old abandoned counter once it decays below 36000.

Held-out check (no calibration changes): four routes x seeds
15000/17500/20000 x (9 unmodified, 20+D1TTL+D1PROX), 600 seconds, eight
workers. All 24 flights completed without tool errors; all 12 mission
pairs match. Each arm has three early exits. At 9/20 MIPS the means are
9.8333/8.5833 launches, 4.5000/4.0000 bursts and 9.0000/8.0000 damage
selections, with 2579.2/2257.4 orbit seconds. Paired 20-minus-9 intervals:
launches -1.2500 (-2.6667 to 0), bursts -0.5000 (-1.7500 to +0.6667),
damage -1.0000 (-3.5000 to +1.3333). This small check does not demonstrate
equivalence. Pooling the original and held-out 36 pairs gives burst
difference -0.6667 (95% -1.2500 to -0.0833), still partial mitigation.
Artifacts: `D:/f117-gate/threat-heldout/`, private driver `threat_heldout.py`,
report `threat-heldout-summary.txt`; measured wall time 9.9 minutes.

Artifacts: `D:/f117-gate/threat-aligned-v5/{runs.jsonl,*.csv}` and
`threat-aligned-v5-summary.txt`. Public reproduction uses
`threat_profile.py` four times into one OUT directory: 9 MIPS with offsets
0..12500; 16 with 110..12610; 20 with 220..12720; then the same 20 offsets
with `--fix D1TTL`. Use all four default routes and `--seconds 600`.
Analyse with:

```
py tools/threat_compare.py OUT/runs.jsonl --shift-ms 16=110 20=220
```

The combined follow-up is in `D:/f117-gate/threat-proximity/`, with summary
`D:/f117-gate/threat-proximity-summary.txt`. Reproduce with the same 20 MIPS
offsets and both `--fix D1TTL --fix D1PROX`, then pass both cohort files to
the comparator. These options add no frame limiter; both remain off by
default, as do the other original-game fixes.

The comparator rejects old telemetry, pairs only identical mission fields,
and retains early exits; its pairing/interval rules and the pilot's orbit
geometry have unit regressions.

## Cancelled-slot repair and remaining distance defect (10 Oct)

The optional D1SLOT now prevents zero-to-FFFF underflow at VGAME 6CA6.
It preserves DEC flags/count and declines positive life and player slots.
All 15 staged cases agree across interpreter/recomp with and without the
option; all eleven control hashes are unchanged. Incoming slots 0 and 7
stay zero after lost lock with the option, versus FFFF without it. This
prevents new abandoned slots; enabling it mid-flight does not reclaim old ones.

A new 72-flight cohort completes four routes x six seed families x three
arms, reusing the prior 24-flight 20+TTL+PROX arm only after reproducing
its Middle East control hash exactly. All 24 mission identities match
across all four arms. No guest memory is staged; early exits are retained.

| Arm | Flights | Early exits | Launches/flight | Bursts/flight | Damage selections/flight | Orbit seconds |
|---|---|---|---|---|---|---|
| 9 + TTL + PROX | 24 | 9 | 8.3750 | 4.1250 | 8.2500 | 4596.2 |
| 20 + TTL + PROX | 24 | 7 | 7.3333 | 3.3750 | 6.7500 | 4669.8 |
| 9 + TTL + PROX + SLOT | 24 | 15 | 12.4167 | 6.5000 | 13.0000 | 3588.0 |
| 20 + TTL + PROX + SLOT | 24 | 11 | 11.5833 | 5.5417 | 11.0833 | 3717.4 |

Adding SLOT increases bursts at 20 MIPS by +2.1667 (95% paired bootstrap
+1.1667..+3.2083) and at 9 MIPS by +2.3750 (+1.2917..+3.5000).
With identical options, 20-minus-9 differences are launches -0.8333
(-3.0833..+1.4167), bursts -0.9583 (-2.2083..+0.2500), damage selections
-1.9167 (-4.4167..+0.5000). Intervals include zero but do not establish
equivalence. Added damage changes survival and trajectories; counts per
flight cannot be treated as equal-exposure impact probabilities.

The fixed natural Middle East 20 MIPS offset2720 replay is identical in
both engines: hash `1f85212a6ffbf801`, every CSV byte and launch record,
16 launches, 9 bursts, 20 total damage selections, early exit. Raw fixed
9/20 MIPS seed2500 traces have zero abandoned-slot episodes and zero
slot-seconds above 36000; their hashes are `17725de70b98ba07` and
`1f85212a6ffbf801`. These traces establish reclamation, not CPU equivalence.

A separate staged straight-pass probe freezes an SA-12's speed at 28 and
turn agility at zero, starts it 22 lateral units, 70 longitudinal units
and 100 altitude units away, and compares S=8/15 with TTL+PROX+SLOT.
S=8 damages the aircraft (two selections), S=15 does not. Both engines
match exactly, case hashes `f2a3a1eb93590c5a` / `a3d3b2e93b8afe35`.
This removes acceleration and steering as explanations for this test:
the original reach is 28 at S=8, while the PROX S=9 floor is only 24.
Thus the floor is still a speed-dependent hit band, not a physical
frame-independent collision test. CPU/combat remains open. A repair must
separate movement sampling from a constant physical collision band rather
than merely tune this cohort's mean. Guidance/acceleration and integer
rounding also require independent review.

Artifacts: `D:/f117-gate/threat-slot/`, `threat-slot-summary.txt`,
`ghost-slot/`, `d1slot-final-*.json`, `proximity-pass-*.json`; private drivers
`threat_slot.py`, `ghost_trace_slot.py`, `proximity_pass_probe.py`.
The 72-flight batch took 10.4 minutes on 12 workers. Its attempted ctypes
affinity call was later found ineffective; use the pointer-width signatures
and verify the resulting mask for future runs.
Reproduce using `threat_profile.py` at 9 MIPS offsets 0..12500 and 20 MIPS
offsets 220..12720, all four routes, 600 seconds, TTL+PROX with and without
SLOT. Compare the JSONL files with `threat_compare.py --shift-ms 20=220`.

A second controlled probe checks the unscaled incoming acceleration at
7209..722B. An SA-5 starts at its normal ground-launch speed1, with its
original terminal speed28; only steering is disabled and it is kept far
from the aircraft. After two simulated seconds (16 steps at S8, 30 at S15),
its speed is 9 versus 16. Both engines match case hashes
`c48afd58d9ae6ea2` / `fc9ee748c9424f67`. Artifacts:
`D:/f117-gate/acceleration-{interp,recomp}.json`, `acceleration_probe.py`.
Ground launch 54ED sets speed1; air launch 662A starts at terminal speed.
Thus every-other-frame acceleration is not invariant per simulated second.
Its direction benefits high-S acceleration, so it cannot alone explain
weaker high-speed combat. Normalizing it is a separate physical timing
repair, not grounds for inflating high-speed hit probability.


## Swept proximity and simulated-time acceleration (10 Oct)

D1PROX v2 uses a continuous sweep of the original octagonal horizontal
metric plus altitude/32, with band = terminal weapon speed*8/9. This is an
explicit S9 balance reference, not a recovered warhead constant. Endpoints
and five piecewise-linear breakpoints give an exact rational minimum.
Consecutive live weapon records track relative player movement; new launches,
changed position/type, frame gaps and process epochs reset continuity.
Original guidance, movement, lifetime/damage guards and player weapons remain.
The independent C oracle clips16 half-spaces and checks20,000 paths,
reversal, subdivision, wrapped coordinates and strict boundaries.

D1ACCEL integrates incoming acceleration at9/2 speed units per simulated
second using exact integer fractions for S1..15, retaining the original
S9 odd-frame pattern and terminal-speed clamp. Both options are off by default.
`tools/d1physics_check.py` passes all21 staged cases in both engines +/-
options, with unchanged first S9/player control hashes. After two simulated
seconds, initial1/terminal28 speed is originally5/9/10/16 at S4/8/9/15;
fixed speed is10 throughout, including a mid-flight S8-to-S15 change.
Frozen SA-12 straight passes lateral20 hit at all four S; lateral22/28
miss at all four S. Original outcomes vary, including a point-sampling
miss at S9. Incoming slot7 is covered. Staged cases isolate physics,
not natural combat balance. The legacy five-case proximity check passes too.

Natural Middle East20 offset2720 matches both engines: hash
`3893586487063407`, every CSV byte and launch record,16 launches,9 bursts,
20 total damage selections and early exit at152.2 orbit seconds. Native9
at offset2500 has hash`9087c775185e262f`,17 launches,11 bursts,24 total
damage selections and138.6 orbit seconds. Orbit-clock rates are1.1038
and1.241 respectively. Regenerated native sentinels reproduce every hash,
launch record and CSV byte. Artifacts: `D:/f117-gate/physics-final-*.json`,
`d1prox-v2-*.json`, `physics-sentinel-*.{json,csv}`.

The72-flight cohort took23.7 minutes with eight workers sharing the gate's
24-CPU affinity mask. All24 original mission pairs match; fresh families
22500/25000 add eight matching pairs. All four27500 pairs generated different
missions and are excluded from paired comparisons:32 matching pairs, not36.

| Physics v2 arm | Flights | Early exits | Launches/flight | Bursts/flight | Damage selections/flight | Orbit seconds |
|---|---|---|---|---|---|---|
| 9 + TTL + PROX + SLOT + ACCEL | 36 | 22 | 12.1389 | 7.0000 | 14.0000 | 5437.2 |
| 20 + TTL + PROX + SLOT + ACCEL | 36 | 17 | 10.5000 | 5.5556 | 11.1111 | 5441.8 |

Across32 matching missions,20-minus-9 differences are launches-1.4688
(95% paired bootstrap-3.1562..+0.0625), bursts-1.5312
(-2.6250..-0.5312), damage selections-3.0625(-5.2500..-1.0625).
Original24 pairs give bursts-1.3333(-2.6250..-0.1667); eight matching
fresh pairs give-2.1250(-4.3750..-0.5000). Physics consistency alone
has not closed CPU-dependent combat. Against prior PROX(v1)+SLOT on the
original24 missions, v2+ACCEL changes bursts by+0.1250 at9 and-0.2500
at20; the20 interval is-0.8750..+0.2083. These repairs follow isolated
defects rather than tuning cohort means.

Full sampled mission-clock weighted rates are1.2327 (9) and1.1239 (20):
20,536/19,658 world seconds over16,659.4/17,491.2 machine seconds. These
include startup and pre-orbit samples; some early exits never reach orbit.
Survival, trajectory and exposure differ, so counts per flight or pooled
counts per world minute are descriptive rather than impact probabilities.

Artifacts: `D:/f117-gate/threat-physics-v2/`, reports
`threat-physics-v2-{original,heldout,all}-summary.txt`. Reproduce original
seed families0..12500 plus22500/25000/27500 at9 MIPS and+220ms at20,
four routes,600 seconds, TTL+PROX+SLOT+ACCEL; use the public comparator.
Reproduce staged checks with `py tools/d1physics_check.py --data DIR
--engine ENGINE`, then repeat with `--fix D1PROX --fix D1ACCEL`.


## Optional real-time cadence (10 Oct; controlled airborne checks complete)

D1REAL pins S=8 normally and S=4 under original 2x compression, admitting
eight simulation frames per machine second. Original D441 derives dependent
rates. The existing interpolated presentation can display at host refresh;
at8Hz it adds about125ms of picture delay. Original default20MIPS and all
fixes-off behavior remain unchanged. CPUs unable to execute eight frames
per second can still fall behind.

A first airborne batch exposed lost simulation time from a two-period
resynchronization rule: one9MIPS orbit averaged7.9534fps. That batch/gate
were stopped; artifacts are saved as `*discarding-v1*`. The corrected pacer
retains elapsed time through overruns, while original pause and quit-dialog
entry explicitly reset its deadline. Fractional clocks and per-machine
program/speed resets have an independent unit check.

The corrected `d1real_check.py` passes seven normal-input cases at9/20/40
MIPS under both engines, every case hash identical within each CPU speed.
Normal30seconds gives240frames/30mission seconds; compressed10seconds
80/20; restored10seconds80/10; pause3seconds0/0 and resume3seconds24/3;
quit-dialog3seconds0/0 and cancellation3seconds24/3. Six parallel boots
took about3.8minutes. Artifacts `D:/f117-gate/realtime-checks/`.

The corrected 24-flight cohort completed in 263.2 seconds with eight workers
on the shared 24-CPU affinity mask. All 12 mission identities match. Each
arm uses TTL+PROX(v2)+SLOT+ACCEL+REAL(v2); no guest memory is staged.

| MIPS | Flights | Early exits | Launches/flight | Bursts/flight | Damage selections/flight | Orbit seconds |
|---|---|---|---|---|---|---|
| 9 | 12 | 6 | 12.1667 | 7.1667 | 14.5000 | 1826.2 |
| 20 | 12 | 5 | 10.0000 | 5.9167 | 11.8333 | 1802.6 |

20-minus-9 paired differences: launches -2.1667 (95% bootstrap
-4.0833..-0.4167), bursts -1.2500 (-2.6667..+0.1667), damage selections
-2.6667 (-5.5000..+0.1667). On these same 12 missions without REAL, the
burst difference was -2.0000 (-3.5000..-0.9167). REAL changes bursts by
-0.7500 at 9 and 0 at 20; neither within-speed interval excludes zero.
This does not establish combat equivalence.

S is 8 in every sampled row. Full sampled clock rates are 0.998452 at 9
and 0.999159 at 20, including frozen death/exit sequences. Removing the
last three seconds of exited flights only gives pooled active rates
1.000065 and 1.000292, with frame rates 8.001003 and 7.999416. Individual
active frame rates are 7.998466..8.001964. Integer mission-second endpoints
limit precision. This supports lossless pacing during flight; the endpoint
exclusion is diagnostic and is not applied to combat counts.

Natural Middle East seed-family 2500 matches interpreter/native at both
speeds: hashes `793c45a47070b53d` (9) and `8729c698e4bed4e5` (20), every
launch record and CSV byte identical. Artifacts: `D:/f117-gate/threat-realtime/`,
`threat-realtime-summary.txt`, `realtime-sentinel-*-interp.{json,csv}`.

Independent source inspection found an additional comparison confound.
VGAME C880 reads INT 1Ah's BIOS tick, stores it at DS:9540 and seeds its
32-bit RNG at EE1A. Initialization 497D..4987 draws the initial frame
counter from that RNG. Aligning START's mission generation does not align
this later combat seed: all 12 pairs have different flight seeds, by 25-33
BIOS ticks. The Middle East pair starts at 39037 versus 39004. Delaying
only the final hangar confirmation at 20 MIPS by 1813 ms makes both 39037,
without changing the mission or staging memory. Nine of twelve computed
delays match immediately; the three Kuwait delays overshoot by one tick.

Input timing adds another confound: VGAME's initialization takes different
amounts of real time, while the pilot schedules controls from program load.
Anchoring to the first mission second aligns frame counters, but the 200 ms
observer still samples different portions of a 125 ms simulation step.
An aligned pair has identical initial position, altitude, heading and speed,
then differs in the following sample because one CPU has completed more
work inside the frame. Short arrow-key holds also interact with this phase.
A follow-up pilot samples once per step and schedules whole-step holds
while the guest is waiting. This is a different pilot from the earlier
5 Hz cohort, so its means are not a before/after balance comparison.

The 24-flight follow-up completed in 269.9 seconds with eight workers.
Three Kuwait flights were then repeated at the corrected delay: the
hangar's polling phase makes 1318/1373 ms straddle the desired seed;
1345 ms matches it. All 12 final pairs have identical START mission
identities and VGAME combat seeds. No guest memory is staged.

| Step-aligned arm | Flights | Early exits | Launches/flight | Bursts/flight | Damage selections/flight | Orbit seconds |
|---|---|---|---|---|---|---|
| 9 + five options | 12 | 6 | 11.0000 | 6.3333 | 12.6667 | 1711.3 |
| 20 + five options | 12 | 7 | 11.0000 | 6.5000 | 13.0000 | 1657.2 |

All 12 pairs have identical launch counts; 11 also have identical burst
counts, damage counters and orbit durations. Korea family 22500 has 7/9
bursts and 16/20 total damage selections at 9/20 MIPS. Its aircraft states
match through 2136 samples; a partial-frame sample at 288.112 seconds then
changes the feedback pilot's subsequent commands. A mapped replay of the
9 MIPS input sequence at 20 MIPS follows the same initial path but crashes
before combat. CPU execution time still affects which frame reads a key.
Lossless pacing preserves time through a slow frame, but cannot make every
original input poll instantaneous. This is a limit of whole-flight
cross-CPU determinism, distinct from the repaired weapon calculations.

The final paired 20-minus-9 launch difference is exactly zero in every
flight. Bursts average +0.1667 (95% paired bootstrap 0..+0.5000), damage
selections +0.3333 (0..+1.0000). This cohort shows no high-speed enemy
weakness. It is not a statistical proof of universal combat equivalence.
Together with the isolated unsigned-lifetime, slot, swept-distance and
acceleration checks, it validates the identified repairs and real-time
cadence at the tested speeds. Default original behavior remains available;
CPU/input phase and random startup can still change an individual battle.

Public reproduction now records `flight_seed` (telemetry v6) separately
from the mission. Use the same five options, `--align-steps`, seed families
2500/22500/25000 at 9 MIPS and +220 ms boot offsets at 20. Delays apply only
to the final START action after mission generation:

| Route | 20 MIPS launch delays (ms), families 2500 / 22500 / 25000 |
|---|---|
| Middle East | 1813 / 1483 / 1483 |
| Korea | 1813 / 1813 / 1813 |
| Kuwait | 1345 / 1345 / 1345 |
| North Cape | 1758 / 1813 / 1758 |

For example, run the Middle East pair into separate output directories:

```
py tools/threat_profile.py --data DIR --out OUT9 --routes middle_east_strike --speeds 9 --offsets-ms 2500 --seconds 600 --align-steps --fix D1TTL --fix D1SLOT --fix D1PROX --fix D1ACCEL --fix D1REAL
py tools/threat_profile.py --data DIR --out OUT20 --routes middle_east_strike --speeds 20 --offsets-ms 2720 --launch-delay-ms 1813 --seconds 600 --align-steps --fix D1TTL --fix D1SLOT --fix D1PROX --fix D1ACCEL --fix D1REAL
py tools/threat_compare.py OUT9/runs.jsonl OUT20/runs.jsonl --shift-ms 20=220 --match-flight-seed
```

The comparator keeps step-aligned and earlier pilots in separate arms,
and the optional flight-seed requirement rejects absent or unequal seeds.
Nine unit checks cover pairing, input phase, launch delay, cache identity
and existing orbit geometry. Artifacts: `D:/f117-gate/threat-phase-corrected.jsonl`,
`flight-seed-*-600-phase.{json,csv}`, `flight-inputs-*.log`,
`replay-korea_strike-22500-20.{json,csv}`. The initial phase cohort's Kuwait
records have unequal seeds; use the corrected JSONL, not `threat-phase.jsonl`.



Final source gate PASS:24.1minutes (1447seconds),17selected tests,35identical
route pairs, both5,718,912-state instruction profiles, all three765-address
matched seeds, all routes-only evidence and no event-limit overruns.
`D:/f117-gate/gate-physics-realtime.log`. This verifies parity and the staged
checks; the later airborne evidence above uses the same game source.
