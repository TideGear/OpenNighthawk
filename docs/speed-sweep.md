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
