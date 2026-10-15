# Emulated CPU speed and VGAME's frame-rate controller: measurements

Measurements of how the emulated machine's speed changes the original game's frame rate, world
clock and combat (9-10 October 2026). The defects and fixes these measurements support are in
[bugs.md](bugs.md) (D1 and its family). Each experiment below gives its setup, result, conclusion
and current status; a superseded conclusion is marked as such and kept only for the comparison.

## Current conclusions

- **Speed default.** The app (`f117a`) runs at 20 million instructions a second; `f117run` and
  every recorded route keep GOG DOSBox's 9 million. No limiter; D1 stays off
  ([Fast-machine default](#fast-machine-default-9-october-2026)).
- **No CPU speed keeps the world clock at real time** under the original's adaptive controller:
  about 1.29 at GOG's 9 MIPS on the parked jet, about 1.11 from 16 MIPS up. Only the optional
  `--fix D1REAL` gives 1.000 ([Optional real-time cadence](#optional-real-time-cadence-10-oct-controlled-airborne-checks-complete)).
- **D1's reported 15-to-3 swing is not reproduced** in 199 flights (3-40 MIPS and the 386
  profile): the model that predicted it omitted the original's frame wait.
- **Combat.** Unfixed, higher speeds give fewer proximity bursts and less damage on matched
  missions. With all five D1-family options and a step-aligned pilot, 12 matched pairs at 9 and 20
  MIPS show no high-speed enemy weakness; this is not a proof of universal equivalence.
- **The S self-calibration audit candidate is closed**: S never exceeds 15 in direct measurement
  ([audit](#ss-self-calibration-audit-10-oct-closed)).

Reproduce the basic sweep with
`py tools/speed_sweep.py --data D:/GOG/F-117A --out DIR --speeds 3 ... 40 386 --seconds 300`
(each route's 9 MIPS run first, then the others; `--table-only` reprints). Private evidence lives
under `D:/f117-gate/` as cited below.

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
real time. Four game ticks are not four 60 Hz ticks: at 66.75-70.09 Hz a four-tick wait alone is
about 16.69-17.52 fps.

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

**Caveats.**

- **Missions differ by speed** below 9 MIPS, on the 386 profile and on some routes above 9
  (the table flags DIFFERENT MISSION): the front end's timing seeds the generator. The timing
  figures are not affected, the engagement figures are.
- **All AI, launches and guidance run per drawn frame scaled by S**, not on the tick (Reimp
  `src/core/frame_weapons.c`, VGAME `0x5046`, `0x5852`, `0x683E`), so the "player on frames, AI
  on the tick" explanation does not apply. This sweep's launch counts mix two versions of the
  weapons-at-player tracker (slots 0-7 at `0x3C3A`; a free slot can read -1) and are too sparse
  to compare speeds; combat is measured by the threat-profile sections below.
- **Not run:** a GOG DOSBox measurement of the game tick rate in flight (this machine loses one
  tick in 21 frames at 11 MIPS and up; whether DOSBox does depends on its interrupt latency at
  the retrace poll).

## D1 follow-up (9 October 2026)

**Setup.** 90 flights: all nine typed routes, 300 guest seconds requested, 9, 12, 16, 20 and
40 MIPS, with and without D1, corrected launch tracker:

```
py tools/speed_sweep.py --data D:/GOG/F-117A --out D:/f117-gate/speedsweep/d1 --speeds 9 12 16 20 40 --fix none D1 --seconds 300 --jobs 16
```

**Result.** No run had an error or the reported 15-to-3 swing. All nine D1 flights at 9 MIPS
have exactly the same final hashes as their unfixed baselines: the fix is inactive there. Means
over complete flights (first five seconds omitted):

| MIPS | complete pairs | no fix: fps / S mean / clock per second | D1: fps / S mean / clock per second |
|---|---|---|---|
| 9 | 9 | 16.273 / 14.100 / 1.1718 | 16.273 / 14.100 / 1.1718 |
| 12 | 9 | 16.347 / 14.241 / 1.1552 | 11.601 / 9.000 / 1.2882 |
| 16 | 9 | 16.793 / 14.957 / 1.1232 | 11.600 / 9.000 / 1.2882 |
| 20 | 9 | 16.753 / 14.989 / 1.1179 | 11.600 / 9.000 / 1.2882 |
| 40 | 8 | 16.716 / 15.000 / 1.1144 | 11.600 / 9.000 / 1.2882 |

Korea at 40 MIPS leaves VGAME for END after 45.25 sampled flight seconds without D1 and 40.39
with it; those two short flights stay in the raw data and are excluded from the means. Mission
identifiers differ across speeds, so these figures establish pacing, not combat.

**Conclusion.** D1 reproduces the parked jet's GOG pacing on fast machines, not every scene's
GOG pacing. In complete flights it reduces frames drawn but increases world-clock speed relative
to the same fast CPU without the fix. It does not make the world run at real time.

## Control-response measurements

**Setup.** `stick_response.py` accepts `--ips`, `--fix`, `--time-us` and a typed `--front-route`,
keeps raw pitch/roll samples, mission identifiers, completed taps and early exits; angular
differences wrap at one turn. Fixes are refused for the 386 profile while they still charge
instruction clocks there. To fly one mission at every speed: START `0x7379` calls `0x8607` for
the BIOS tick and `0x96BC` for srand; at boot clock 700000000000000 the initial random states at
DS:AE8C are 31233, 31231, 31231, 31229 and 31227 at 9, 12, 16, 20 and 40 MIPS. Offsetting only
the boot clock by 0, 110, 110, 220 and 330 ms gives the same mission in all ten flights (primary
type 1, target 2 at (26080, 9408), secondary type 1, target 17, departure and home 41). No
guest-memory writes are used. Example (aligned 40 MIPS D1 run):

```
py tools/stick_response.py --machine machine --data D:/GOG/F-117A --out D:/f117-gate/speedsweep/controls-check --ips 40000000 --fix D1 --front-route tools/routes/middle_east_strike.args --time-us 700000000330000
```

**Result.** 200 ms taps, in original angle units (65536 per turn); arrays keep each observed
response:

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

**Conclusion.** Unfixed pitch and roll responses are within the same spread at 9, 16, 20 and 40
MIPS. These are responses of progressively diverging flights, not independent identical starts;
repeated taps can end a flight before all 30 are observed. A typed 386 run selected another
mission, stayed at AGL 128 and responded to nothing, so there is no airborne 386 comparison yet.
Artifacts: `D:/f117-gate/speedsweep/controls` (absolute replay, which flew different missions and
is not comparable), `controls-typed`, `controls-aligned`.

## Actual 15 FPS limiter experiment (9 October 2026)

**Setup.** An isolated checkout at `28f2a50` (`D:/f117-wt/speed15-experiment`) changes only
`D1_FPS_X10` from 116 to 150 in `src/fixes/fixes.c`: D1's frame-entry wait at 15 fps, inactive at
9 MIPS and below. Not the shipped D1. Built with `build.cmd -DF117R_BUILD_APP=OFF
-DF117R_GEN_DIR=C:/Users/Tideg/f117-recomp-local/gen`, then:

```
$env:F117R_MACHINE_API = 'D:/f117-wt/speed15-experiment/build/f117machine_api.dll'
py tools/speed_sweep.py --data D:/GOG/F-117A --out D:/f117-gate/speed15/sweep --speeds 9 12 16 20 40 --fix D1 --seconds 300 --jobs 8
Remove-Item Env:F117R_MACHINE_API
```

`D:/f117-gate/speed15/experiment.json` records the source delta, command and DLL SHA-256. The
45 unfixed baselines are the D1 follow-up's rows, not new flights.

**Result.** All 45 flights finished without errors or swings. Means over complete flights,
first five seconds omitted:

| MIPS | complete flights | drawn FPS | S mean | mission seconds per emulated second |
|---|---|---|---|---|
| 9 (wait inactive) | 9 | 16.273 | 14.100 | 1.1718 |
| 12 | 9 | 14.794 | 11.868 | 1.2464 |
| 16 | 9 | 15.000 | 12.000 | 1.2501 |
| 20 | 9 | 14.999 | 12.000 | 1.2509 |
| 40 | 8 | 15.000 | 12.000 | 1.2500 |

All nine 9 MIPS hashes equal the baselines. Korea at 40 MIPS leaves VGAME after 41.50 sampled
seconds and is excluded. The same DLL flew `boot_to_flight` and `middle_east_strike` at 40 MIPS
under the interpreter for 300 s each: all 30,000 observation rows per flight and the final
hashes (`05a9b3747a918a85`, `814b693377e773f9`) equal the recompiled runs
(`D:/f117-gate/speed15/pair-interp/`, `compare_pairs.py`).

**Conclusion: rejected.** A 15 fps cap does not make the clock real: S settles at 12 (the
controller's `60F / (R + F/2)` is about 12.12 at F = 15, R = 66.75) and the mission clock runs
about 1.25 times real time, worse than the default's 1.12. The hypothesis that a 15 fps cap holds
S at its clamp of 15 is wrong. A cap also cannot raise slower scenes to 15 fps.

## Fast-machine default (9 October 2026)

**Decision (current).** The app (`f117a`) runs its instruction model at 20 million instructions a
second (`MACHINE_APP_IPS`; `--ips` and `ips =` override it). `f117run` and every recorded route
keep GOG DOSBox's 9 million, so no route hash changes. D1 stays off. No limiter is added: above
about 13 MIPS the game's own clamp holds S at 15 and the drawn rate settles near 16.7 frames a
second, a clock of about 1.12 at 20 MIPS.

**Matrix.** Nine typed routes, eight boot-clock offsets (0, 3, 7, 11, 15, 19, 23 and 27 seconds,
each a different mission seed), speeds 9, 16 and 20 MIPS: 214 flights of 300 guest seconds, no
fixes, recomp engine. Missions cannot be paired across speeds (a change of speed also changes the
seed), so each speed is compared as a distribution.

```
py tools/speed_sweep.py --data D:/GOG/F-117A --out D:/f117-gate/fastdefault/matrix --speeds 9 16 20 --offsets-ms 0 3000 7000 11000 15000 19000 23000 27000 --seconds 300 --jobs 24
py tools/speed_compare.py D:/f117-gate/fastdefault/matrix/runs.jsonl
```

| MIPS | complete flights | drawn FPS | S mean | swings | mission clock per second |
|---|---|---|---|---|---|
| 9 (GOG pace) | 70 | 15.27 | 12.95 | 0 | 1.202 |
| 16 | 68 | 16.77 | 14.88 | 0 | 1.129 |
| 20 (default) | 70 | 16.77 | 14.98 | 0 | 1.120 |

The 9 MIPS mean includes the parked jet (11.6 frames a second); 16 and 20 MIPS hold it at 16.7.

Combat, complete flights, with 95% bootstrap intervals (observer later found limited; see the
next section):

| MIPS | launches per flight | bursts per flight | misses per flight | player hits per flight | flights with a hit |
|---|---|---|---|---|---|
| 9 | 0.26 (0.11 to 0.43) | 0.04 | 0.17 | 0.057 | 2 of 70 |
| 16 | 0.18 (0.07 to 0.29) | 0.03 | 0.10 | 0.029 | 1 of 68 |
| 20 | 0.30 (0.14 to 0.49) | 0.03 | 0.20 | 0.029 | 1 of 70 |

Differences from 9 MIPS: launches −0.08 (−0.28 to +0.11) at 16, +0.04 (−0.19 to +0.29) at 20;
player hits −0.03 at both (−0.14 to +0.06). No difference is detected.

**What this does not establish.** The power is very low. These routes give about a quarter of a
launch per flight and rarely hit the player, so a halving or doubling of combat would not show
here. The matrix supports "no large change in combat rate or in hits", not "combat is
unchanged"; the controlled threat profile below measures that.

- **Errors and early exits** are all in Korea's route, at a seed where the scripted front end does
  not reach VGAME (one error at 9 MIPS, one at 16) or where the mission ends near 40 seconds (one
  at 9, two at 16, one at 20). Both kinds appear at 9 MIPS, so they are not speed effects. Two
  Korea flights at seed 23 seconds were not run at 16 or 20 MIPS, because their 9 MIPS baseline
  never reaches VGAME.
- **Mission mix.** Objective 1 appears in 9 of 70 flights at 9 MIPS, 9 of 68 at 16 and 14 of 70
  at 20: a sample difference from the seed, not a tested speed effect.
- **Real-time headroom.** A recompiled `boot_to_flight` at 9 MIPS ran 68.7 million instructions a
  second on this machine (while sharing it), so 20 million is about 3.4 times real time there.

## Controlled threat-profile combat measurement (9-10 Oct 2026)

**Status: superseded.** The observer in this section discarded lifetimes above 32767 (hiding
fresh SA-5 launches at S >= 14), detected bursts only when sampled life equalled exactly `2*S`
(missing more at higher frame rates), and discarded damage counters on early exits. The pilot
scheduled its initial throttle/rotation at fixed instruction counts (rotating at 8.5 s at 20
MIPS against 18.9 s at 9), and its orbit controller mishandled negative word differences and
steered outward. All are corrected: the tools now use the game's launch counter, valid unsigned
lifetimes, exclude seeker-abandoned FFFF slots, recognise the burst reset across samples, keep
the last damage counter on exit, keep authored 9 MIPS start times at every speed, and
`tests/test_threat_profile.py` checks orbit wrapping and radial correction in eight directions.
`damage_hits` is a legacy field name for damage selections, not missile impacts (one impact can
add difficulty+1). The figures below are kept for comparison only; they do not establish the size
or cause of a combat gap.

**Setup.** `tools/threat_profile.py` drives VGAME with live telemetry: it approaches and strikes
the primary target as `strike_pilot.py` does, then (within 6,000 units or on primary credit)
orbits the target's coordinates at a fixed radius and altitude for the rest of the flight. No
enemy site's position is read or assumed.

```
py tools/threat_profile.py --data D:/GOG/F-117A --out OUT --routes middle_east_strike korea_strike kuwait_strike north_cape_strike --speeds 9 16 20 --seconds 600 --offsets-ms 0 2500 5000 ... 27500 --jobs 24
```

**Result (historical).** 144 flights (12 offsets x four theatres x three speeds), all complete:

| MIPS | flights | mean launches/flight | mean bursts/flight | flights with a burst |
|---|---|---|---|---|
| 9 | 48 | 9.08 | 2.10 | 38 of 48 |
| 16 | 48 | 6.48 | 1.10 | 21 of 48 |
| 20 | 48 | 3.81 | 0.58 | 12 of 48 |

| MIPS | orbit-seconds | launches/100 orbit-s | bursts/100 orbit-s |
|---|---|---|---|
| 9 | 10,438 | 3.40 | 0.70 |
| 16 | 8,981 | 2.51 | 0.40 |
| 20 | 7,395 | 2.18 | 0.30 |

Early exits before 600 s: 13, 15 and 18 of 48. Bootstrap launch differences from 9 MIPS: 16 MIPS
-2.62 (-4.67 to -0.56), 20 MIPS -5.25 (-7.02 to -3.46). The large launch deficit this suggested
was **not** confirmed by the corrected cohort below. Raw data: `D:/f117-gate/threatmatrix/`.

## Independent combat investigation (10 October 2026)

**The Reimp's design** (`9e0716dc502cd64501b0d52030ebef041825f907`) separates simulation and
rendering, paces simulation from S and starts S at 5 (`vginit.c`). Its default game timer is
60 Hz; original-world-speed mode uses 70.086 Hz with a step-rate adjustment (`main.c`). Its
clock tests check tick counts, not combat. `frame_weapons.c` still uses the signed lifetime
guard; its low starting S avoids the overflow.

**Mechanisms found** (listing plus staged machines; now fixes in [bugs.md](bugs.md)):

- **Signed lifetime guard (D1TTL).** SA-5 life `150*S*16` is 33,600/36,000 at S=14/15, but
  `6F19` uses signed JLE after comparing life with `4*S`. Seven staged cases pass in both engines
  with and without the fix. A natural 600 s Middle East flight at 20 MIPS, offset 2720 ms, gains
  an SA-5 burst at 293.32 s with the fix (interp/recomp hash `0559ff3b220737ef`).
- **Proximity reach shrinks with S (D1PROX).** Reach `(speed<<3)/S` at `6EF5`, compared unsigned
  with slant at `6EF9`/`6EFB`, is both a tunnelling allowance and the near-miss radius. A staged
  SA-12 at equal geometry damages the aircraft at S=8 and misses at S=15. The first D1PROX (v1)
  put an S=9 floor under the reach; v2 replaced it (below).
- **Lifetime underflow (D1SLOT).** A raw-lifetime trace (Middle East family 2500 at 9/20 MIPS,
  `D:/f117-gate/ghost_trace.py`, hashes `2ae3a7380acfa83d` / `99bf21292c32d48d`) shows
  seeker-abandoned slots at FFFF (1104.8/1295.2 slot-seconds above 36000 over the common window);
  launches at `54B4` and `65C3` need their `(unit_index & 7)` slot at zero life, so one abandoned
  slot can block a launcher. `D:/f117-gate/slot_underflow_probe.py` confirms life 1000 becoming
  FFFF after a lost-lock seeker check at S=8 and S=15 in both engines (`76d07a07e0d330d9` /
  `7828a2d70478e5a1`).

**Cohort setup.** Corrected pilot/observer; four theatres; six seed families (0..12500 ms, step
2500); arms 9/16/20 MIPS and 20+D1TTL. START clock shifts of +110/+220 ms at 16/20 MIPS align the
seed families; all 24 groups have identical mission identities. No guest memory is staged; early
exits stay in. Damage counts damage selections, not impacts.

| Arm | Flights | Early exits | Launches/flight | Bursts/flight | Damage selections/flight | Orbit seconds |
|---|---|---|---|---|---|---|
| 9 | 24 | 9 | 8.5417 | 4.1250 | 8.2500 | 4734.8 |
| 16 | 24 | 5 | 8.4583 | 3.5000 | 7.0000 | 5023.6 |
| 20 | 24 | 6 | 7.7500 | 2.7500 | 5.5000 | 5305.2 |
| 20 + D1TTL | 24 | 6 | 7.7083 | 2.8333 | 5.6667 | 5296.4 |

**Result.** Paired bootstrap (5,000 resamples, seed 12345), 20 minus 9 MIPS: launches -0.7917
(95% -2.0417 to +0.4583), bursts -1.3750 (-2.1667 to -0.6250), damage selections -2.7500
(-4.3333 to -1.2500). The earlier large launch deficit is not established; a burst/damage deficit
remains, and D1TTL alone does not close it (bursts -1.2917, -2.0417 to -0.5833). Adding D1PROX v1
at 20 MIPS (7 early exits, 7.3333 launches, 3.3750 bursts, 6.7500 damage selections, 4669.8 orbit
seconds) raises bursts +0.5417 (+0.2083 to +0.8750) and damage +1.0833 (+0.4167 to +1.7500) over
D1TTL alone; the gap from 9 MIPS shrinks but stays (bursts -0.7500, -1.4167 to -0.1667; damage
-1.5000, -2.8333 to -0.3333). Combined natural Middle East flight at 2720: both engines identical,
hash `7b5cc08d65027187`, 10 launches, 6 bursts, 14 damage selections. The 9 MIPS cohort's
burst-time S distribution is 7:23, 8:46, 9:11, 10:7, 11:3, 12:1, 13:2, 14:3, 15:3 (mean 8.6465;
launch-time mean 8.9561), so a parked S=9 reference is not the whole low-speed distribution.

**Held-out check** (no calibration changes): seeds 15000/17500/20000, 9 unmodified against
20+D1TTL+D1PROX, 24 flights, all 12 pairs matched, three early exits per arm. Means 9.8333/8.5833
launches, 4.5000/4.0000 bursts, 9.0000/8.0000 damage, 2579.2/2257.4 orbit seconds; differences
launches -1.2500 (-2.6667 to 0), bursts -0.5000 (-1.7500 to +0.6667), damage -1.0000 (-3.5000 to
+1.3333). Pooling the 36 pairs gives bursts -0.6667 (-1.2500 to -0.0833).

**Conclusion (superseded in part).** TTL plus PROX v1 was partial mitigation, not CPU
independence. PROX v1 is replaced by v2 and the full picture is in the later sections. Extra
damage changes trajectories and survival, so per-flight counts are not equal-exposure
probabilities; a difference interval including zero is not proof of equivalence.

**Reproduce.** `threat_profile.py` four times into one OUT directory: 9 MIPS offsets 0..12500; 16
with 110..12610; 20 with 220..12720; the same 20 offsets with `--fix D1TTL` (and `--fix D1PROX`
for the combined arm); all four default routes, `--seconds 600`. Then
`py tools/threat_compare.py OUT/runs.jsonl --shift-ms 16=110 20=220`. The comparator rejects old
telemetry, pairs only identical mission fields and keeps early exits; its rules and the orbit
geometry have unit regressions. Artifacts: `D:/f117-gate/threat-aligned-v5/`,
`threat-proximity/`, `threat-heldout/` and their `*-summary.txt`.

## Cancelled-slot repair and remaining distance defect (10 Oct)

**Setup.** D1SLOT keeps a cancelled incoming life at zero at VGAME `6CA6` (DEC flags and count
preserved; positive life and player slots declined). All 15 staged cases agree across engines
with and without it; all eleven control hashes are unchanged. Cohort: four routes x six seed
families x four arms (the 20+TTL+PROX arm reused after reproducing its Middle East control hash);
all 24 mission identities match across arms.

| Arm | Flights | Early exits | Launches/flight | Bursts/flight | Damage selections/flight | Orbit seconds |
|---|---|---|---|---|---|---|
| 9 + TTL + PROX | 24 | 9 | 8.3750 | 4.1250 | 8.2500 | 4596.2 |
| 20 + TTL + PROX | 24 | 7 | 7.3333 | 3.3750 | 6.7500 | 4669.8 |
| 9 + TTL + PROX + SLOT | 24 | 15 | 12.4167 | 6.5000 | 13.0000 | 3588.0 |
| 20 + TTL + PROX + SLOT | 24 | 11 | 11.5833 | 5.5417 | 11.0833 | 3717.4 |

**Result.** SLOT raises bursts by +2.1667 (95% paired bootstrap +1.1667..+3.2083) at 20 MIPS and
+2.3750 (+1.2917..+3.5000) at 9. With identical options, 20-minus-9: launches -0.8333
(-3.0833..+1.4167), bursts -0.9583 (-2.2083..+0.2500), damage -1.9167 (-4.4167..+0.5000).
Fixed natural Middle East 20 MIPS offset 2720: both engines identical, hash `1f85212a6ffbf801`,
16 launches, 9 bursts, 20 damage selections. Fixed 9/20 MIPS seed-2500 traces have zero
abandoned-slot episodes (hashes `17725de70b98ba07`, `1f85212a6ffbf801`): reclamation works.

**Distance probe.** A staged straight pass (SA-12 speed frozen at 28, no turning, starting 22
lateral, 70 longitudinal and 100 altitude units away, TTL+PROX+SLOT) damages at S=8 and misses at
S=15 (hashes `f2a3a1eb93590c5a` / `a3d3b2e93b8afe35`): original reach is 28 at S=8, the PROX v1
floor only 24, so v1 was still a speed-dependent band. This led to PROX v2.

**Acceleration probe.** An SA-5 at its normal ground-launch speed 1 (`54ED`; air launch `662A`
starts at terminal speed), terminal speed 28, steering disabled, far from the aircraft: after two
simulated seconds (16 steps at S8, 30 at S15) its speed is 9 against 16 (hashes
`c48afd58d9ae6ea2` / `fc9ee748c9424f67`). Odd-frame acceleration at `7209..722B` is not
invariant per simulated second; it favours high S, so it cannot alone explain weaker high-speed
combat. This led to D1ACCEL.

**Reproduce.** `threat_profile.py` at 9 MIPS offsets 0..12500 and 20 MIPS offsets 220..12720, four
routes, 600 s, TTL+PROX with and without SLOT; `threat_compare.py --shift-ms 20=220`. Artifacts:
`D:/f117-gate/threat-slot/`, `ghost-slot/`, `d1slot-final-*.json`, `proximity-pass-*.json`,
`acceleration-{interp,recomp}.json`. Its ctypes affinity call was ineffective; use pointer-width
signatures and verify the mask in future runs.

## Swept proximity and simulated-time acceleration (10 Oct)

**Setup.** D1PROX v2: a continuous sweep of the original octagonal horizontal metric plus
altitude/32, band = terminal weapon speed*8/9 (an explicit S9 balance reference), exact rational
minimum from endpoints and five breakpoints; consecutive live weapon records track the player's
relative movement, and new launches, changed position/type, frame gaps and process epochs reset
it. D1ACCEL: 9/2 speed units per simulated second with exact integer fractions for S1..15,
keeping the S9 odd-frame pattern and terminal clamp. Both off by default. Checks: the C oracle
(`tests/test_weapon_sweep.c`) and `tools/d1physics_check.py` (21 staged cases), described in
[bugs.md](bugs.md#d1prox-incoming-proximity-distance-shrinks-with-s). After two simulated seconds
a speed-1/terminal-28 missile reaches 5/9/10/16 at S4/8/9/15 originally and 10 throughout with the
fix, including a mid-flight S8-to-S15 change. Frozen SA-12 passes at lateral 20 hit at all four S
and at lateral 22/28 miss at all four; original outcomes vary, including a point-sampling miss at
S9.

**Cohort.** 72 flights, 9 and 20 MIPS with TTL+PROX+SLOT+ACCEL. All 24 original mission pairs
match; fresh families 22500/25000 add eight; all four 27500 pairs generated different missions and
are excluded: 32 matching pairs.

| Physics v2 arm | Flights | Early exits | Launches/flight | Bursts/flight | Damage selections/flight | Orbit seconds |
|---|---|---|---|---|---|---|
| 9 + TTL + PROX + SLOT + ACCEL | 36 | 22 | 12.1389 | 7.0000 | 14.0000 | 5437.2 |
| 20 + TTL + PROX + SLOT + ACCEL | 36 | 17 | 10.5000 | 5.5556 | 11.1111 | 5441.8 |

**Result.** Across 32 pairs, 20-minus-9: launches -1.4688 (-3.1562..+0.0625), bursts -1.5312
(-2.6250..-0.5312), damage -3.0625 (-5.2500..-1.0625); original 24 pairs bursts -1.3333
(-2.6250..-0.1667), eight fresh pairs -2.1250 (-4.3750..-0.5000). Against PROX v1+SLOT on the
original 24 missions, v2+ACCEL changes bursts by +0.1250 at 9 and -0.2500 at 20 (-0.8750..+0.2083).
Sampled mission-clock rates 1.2327 (9) and 1.1239 (20). Natural Middle East 20 MIPS offset 2720:
both engines identical, hash `3893586487063407`, 16 launches, 9 bursts, 20 damage selections,
early exit at 152.2 orbit seconds; native 9 MIPS offset 2500 hash `9087c775185e262f`, 17
launches, 11 bursts, 24 damage selections, 138.6 orbit seconds (orbit-clock rates 1.1038 and
1.241).

**Conclusion.** Physics consistency alone does not close CPU-dependent combat; the remaining gap
came from the simulation cadence and the pilot's input phase (next section). Artifacts:
`D:/f117-gate/threat-physics-v2/`, `physics-final-*.json`, `d1prox-v2-*.json`,
`physics-sentinel-*`. Reproduce staged checks with `py tools/d1physics_check.py --data DIR
--engine ENGINE`, then again with `--fix D1PROX --fix D1ACCEL`.

## Optional real-time cadence (10 Oct; controlled airborne checks complete)

**Setup.** D1REAL pins S=8 (S=4 under the original's 2x compression) and admits eight
simulation frames per machine second; the original `0x0D441` derives dependent rates. The pacer
keeps elapsed time through overruns; the original pause and quit-dialog entry reset its deadline.
(A first version lost time on a two-period resynchronisation rule and was discarded.) Details and
the `d1real_check.py` results: [bugs.md](bugs.md#d1real-real-time-simulation-cadence)
(artifacts `D:/f117-gate/realtime-checks/`).

**Cohort with D1REAL (five options: TTL+PROX v2+SLOT+ACCEL+REAL).** 24 flights, all 12 mission
identities match, no guest memory staged:

| MIPS | Flights | Early exits | Launches/flight | Bursts/flight | Damage selections/flight | Orbit seconds |
|---|---|---|---|---|---|---|
| 9 | 12 | 6 | 12.1667 | 7.1667 | 14.5000 | 1826.2 |
| 20 | 12 | 5 | 10.0000 | 5.9167 | 11.8333 | 1802.6 |

20-minus-9: launches -2.1667 (-4.0833..-0.4167), bursts -1.2500 (-2.6667..+0.1667), damage
-2.6667 (-5.5000..+0.1667); on the same 12 missions without REAL the burst difference was -2.0000
(-3.5000..-0.9167). S is 8 in every sampled row. Full sampled clock rates are 0.998452 (9) and
0.999159 (20) including frozen death/exit sequences; without the last three seconds of exited
flights, 1.000065 and 1.000292 at 8.001003 and 7.999416 fps (individual flights 7.998466..8.001964):
pacing is lossless in flight. Natural Middle East family 2500 matches interpreter/native at both
speeds (`793c45a47070b53d`, `8729c698e4bed4e5`). Artifacts: `D:/f117-gate/threat-realtime/`.

**Two comparison confounds.**

- **VGAME's own seed.** VGAME `C880` reads the INT 1Ah BIOS tick, stores it at DS:9540 and seeds
  its 32-bit RNG at `EE1A`; initialisation `497D..4987` draws the initial frame counter from it.
  Aligning START's mission does not align this seed: all 12 pairs differed by 25-33 BIOS ticks.
  Delaying only the final hangar confirmation aligns it without staging memory (the hangar's
  polling phase can straddle the target; Kuwait needs 1345 ms, not 1318/1373).
- **Input phase.** The 200 ms observer sampled different portions of a 125 ms simulation step,
  and VGAME's initialisation takes different real time per CPU. The step-aligned pilot
  (`--align-steps`) samples once per step and schedules whole-step holds while the guest waits.
  It is a different pilot from the 5 Hz one, so its means are not a before/after comparison.

**Step-aligned cohort.** All 12 final pairs have identical START mission identities and VGAME
combat seeds:

| Step-aligned arm | Flights | Early exits | Launches/flight | Bursts/flight | Damage selections/flight | Orbit seconds |
|---|---|---|---|---|---|---|
| 9 + five options | 12 | 6 | 11.0000 | 6.3333 | 12.6667 | 1711.3 |
| 20 + five options | 12 | 7 | 11.0000 | 6.5000 | 13.0000 | 1657.2 |

**Result (current).** All 12 pairs have identical launch counts; 11 also have identical bursts,
damage counters and orbit durations. Korea family 22500 (7/9 bursts, 16/20 damage at 9/20 MIPS)
matches through 2136 samples, then a partial-frame sample at 288.112 s changes the feedback
pilot's commands; a mapped replay of the 9 MIPS inputs at 20 MIPS crashes before combat. Paired
20-minus-9: launches exactly zero in every flight, bursts +0.1667 (0..+0.5000), damage +0.3333
(0..+1.0000).

**Conclusion.** With the five options this cohort shows no high-speed enemy weakness. It is not
a statistical proof of universal equivalence: CPU execution time still decides which frame reads
a key, so whole-flight cross-CPU determinism is limited by the original's input polling, and
random startup can still change an individual battle. The original behaviour stays the default.

**Reproduce.** Telemetry v6 records `flight_seed` separately from the mission. Use the five
options, `--align-steps`, seed families 2500/22500/25000 at 9 MIPS and +220 ms boot offsets at 20,
with these delays on the final START action at 20 MIPS:

| Route | 20 MIPS launch delays (ms), families 2500 / 22500 / 25000 |
|---|---|
| Middle East | 1813 / 1483 / 1483 |
| Korea | 1813 / 1813 / 1813 |
| Kuwait | 1345 / 1345 / 1345 |
| North Cape | 1758 / 1813 / 1758 |

```
py tools/threat_profile.py --data DIR --out OUT9 --routes middle_east_strike --speeds 9 --offsets-ms 2500 --seconds 600 --align-steps --fix D1TTL --fix D1SLOT --fix D1PROX --fix D1ACCEL --fix D1REAL
py tools/threat_profile.py --data DIR --out OUT20 --routes middle_east_strike --speeds 20 --offsets-ms 2720 --launch-delay-ms 1813 --seconds 600 --align-steps --fix D1TTL --fix D1SLOT --fix D1PROX --fix D1ACCEL --fix D1REAL
py tools/threat_compare.py OUT9/runs.jsonl OUT20/runs.jsonl --shift-ms 20=220 --match-flight-seed
```

The comparator keeps step-aligned and earlier pilots in separate arms, and
`--match-flight-seed` rejects absent or unequal seeds; nine unit checks cover pairing, input
phase, launch delay, cache identity and orbit geometry. Artifacts:
`D:/f117-gate/threat-phase-corrected.jsonl` (not `threat-phase.jsonl`, whose Kuwait seeds
differ), `flight-seed-*-600-phase.{json,csv}`, `flight-inputs-*.log`,
`replay-korea_strike-22500-20.{json,csv}`. The source gate passed with these fixes in the tree
(`D:/f117-gate/gate-physics-realtime.log`).

## S's self-calibration audit (10 Oct): closed

**Setup.** A static audit swept every VGAME place that multiplies S (`[0x368E]`), with the
recompiler's decoder (`recompiler/x86dec.py`), from all 523 census-known VGAME entries (24,297
reachable instructions); script `D:/f117-gate/audit-s-calibration/audit_s_scaling.py`. Most hits
are safe (seconds-to-frames conversions; four 32-bit products through the shift helper `0xEF68`;
`0x446C`'s `×960` is protected by `sub dx,dx`, truncating only at S>=69).

**Candidate.** In the controller inside `0x3ADA` (`0x443C`-`0x4488`), a value derived from
`[0x43E8]` goes negative once S>15 and feeds an unsigned `MUL [0x368E]` at `0x4447` whose high
word `shl ax,1` discards; after the floor of 4, the `0x446C` multiply/divide and the `[1,255]`
clamp (`vgame_clamp3`, `0x0C67A`), the result can be written back to S. A bit-exact Python trace
(`trace_s_calib.py`) drives S to 0 for every starting S from 16 to 40. `0x3ADA` is called every
frame from the main flight loop at `0x1BCE`, and `0xD441` takes its S<=15 branch for S=0 without
dividing, so a persisted S=0 would reach the unguarded `div [0x368E]` at `0x4435`.

**Result.** `py tools/d1_check.py --data DIR --ips 40000000 --engine interp --no-fix` measured S
over a full 161.66 s flight at 40 MIPS with every fix off: **S=15 in all 71,852 samples, never
above 15**, and the 90-flight D1 sweep's unfixed 40 MIPS flights showed no fault.

**Conclusion.** Closed, no fix warranted: the precondition (a raw S above 15 reaching this branch)
does not arise in real play, whatever the isolated arithmetic says. The audit's other candidate,
`0x2BFE` inside `game_input` (a terrain-distance value truncated after a signed IMUL), was never
pursued and remains an open, unweighted lead.
