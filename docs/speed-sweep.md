# Emulated CPU speed and VGAME's frame-rate controller: measurements

How the emulated machine's speed changes the original game's frame rate,
world clock and combat (measured 9-10 October 2026). The defects and fixes
these measurements support are in [bugs.md](bugs.md) (D1 and its family).
Each section gives the setup, the key figures, the conclusion and where the
evidence is; a superseded result says so. Private evidence lives under
`D:/f117-gate/` as cited.

## Current conclusions

- **Speed default.** The app (`f117a`) runs at 20 million instructions a
  second; `f117run` and every recorded route keep GOG DOSBox's 9 million. No
  limiter was shipped at the time of these measurements. Since 17 October,
  normal application play enables all fixes, including D1REAL and D1PROX;
  use `--fix none` to reproduce the original measurements
  ([Fast-machine default](#fast-machine-default-9-october-2026)).
- **No CPU speed keeps the world clock at real time** under the original's
  adaptive controller: about 1.29 times real time at GOG's 9 MIPS on the
  parked jet, about 1.11 from 16 MIPS up. The switchable `--fix D1REAL`
  gives 1.000
  ([Optional real-time cadence](#optional-real-time-cadence-10-oct-controlled-airborne-checks-complete)).
- **D1's reported 15-to-3 swing is not reproduced** in 199 flights (3-40
  MIPS and the 386 profile): the model that predicted it left out the
  original's frame wait.
- **Combat.** Without fixes, higher speeds give fewer proximity bursts and
  less damage on matched missions. With all five D1-family options and a
  step-aligned pilot, 12 matched pairs at 9 and 20 MIPS show no high-speed
  enemy weakness; this is not a proof of equivalence in every battle.
- **The S self-calibration audit candidate is closed**: S never exceeds 15
  in direct measurement ([audit](#ss-self-calibration-audit-10-oct-closed)).

Reproduce the basic sweep with
`py tools/speed_sweep.py --data D:/GOG/F-117A --out DIR --speeds 3 5 7 9 14 16 20 40 386 --seconds 300`
(each route's 9 MIPS run first, then the others; `--table-only` reprints the
tables).

## Original game clock on 86Box (17 October 2026)

An independent run of the original DOS game, outside the recompilation,
used 86Box's OPTi 495SX / 386DX33 profile with 1 MB RAM, MS-DOS 5.00 and
VGA. The VM ran offscreen on a private disk copy. The existing harness
staged START's mission RNG seed for a repeatable front end; it applied no
game fix or flight input. After 30 seconds settling, 601 samples covered
119.994917 seconds of 86Box emulated time, measured by its TSC-based hook.
The mission clock advanced 152 seconds: **1.26672 mission seconds per
emulated second**. S stayed 5, the frame counter advanced 764 (6.36694 fps),
and the time-compression multiplier stayed 1 (normal; 2 is accelerated).

This independently confirms a clock mismatch on this original-game hardware
profile. It is not a host wall-clock stopwatch test or proof of how every
period PC behaved. The earlier sweep's `386` entries are our timing model,
not independent 86Box measurements; do not conflate the two. The original's
per-second normalization and normal/accelerated time controls suggest a
real-time goal, but developer intent remains an inference. Neither this run
nor the GOG S=9 parked baseline establishes a canonical update rate or
warhead radius. D1REAL's eight-step cadence and D1PROX's S=9 calibration
are project choices, enabled for normal play at the owner's request.

Evidence: `D:/f117-gate/default-fixes-20261017/clock86/` (`loop.state`,
`samples.json`, `verdict.json`, private profile and trace), with
`analyze_clock86.py` beside it. The capture initially tripped a harness
assertion expecting compression 0; normal is 1. Offline analysis retained
the samples and corrected that guard without rerunning or changing the game.

## The controller (read from the VGAME listing)

- **Frame loop** `0x1BCE`: `[0x262A]=1` at `0x1BFA`, player flight `0x1EB0`,
  mission `0x3ADA`. The frame counter `[0x3D8E]` steps once a frame
  (`0x4359`); the mission clock `[0x9912]` steps once every S frames
  (`0x436C`). `[0x3DA6]` (`0x4423`) counts the same frames, reset each
  window.
- **Game tick** `0x1E79` (from the IRQ0 handler `0x1C9B`): `[0x2646]`,
  `[0x264A]`, `[0x2648]` +1. The timer is calibrated against vertical
  retrace at install (`0x1D88`: reload `[0x2632]` = 17024, 70.09 Hz,
  accepted by the 4..6 check at `0x1DE4`). Every 20th tick `0x1D01` polls
  until retrace and restarts the PIT there; if it had to wait (CX non-zero,
  `0x1D7A`) it increments the divider `[0x2640]` (`0x1D82`), so the next
  interrupt is not a game tick. Measured game ticks a second: 70.08 at 7-8.5
  MIPS, 67.9-68.2 at 9-10, 66.75-66.8 at 11 MIPS and up (20 ticks per 21
  frames), 69.2 on the 386 profile.
- **Controller** `0x441D` (every 4S frames): `ticks = [0x2646] + 2S(1 -
  2[0x43E8])`, floor 4; `rate = 960S / ticks`; S becomes `(rate+2)>>2` when
  `|4S - rate| > 3`; `0x0D441` clamps S to `[4 - [0x3DA8], 15]` and sets
  `[0x43E8]` = `clamp((9 - 120/S)>>1, 1, 4)` only when the raw S was above
  15, else 0.
- **Frame wait** `0x0409`: `push [0x43E8]; call 0x04C7`, which spins until
  the tick byte `[0x264A]` has advanced by `[0x43E8]`. Traced running in this
  build at 40 MIPS (`boot_to_flight`, no fixes): `0x441D`, then `0x0409`,
  then `0x04C7` spinning while `[0x264A]` goes 133, 134, 135 (`[0x43E8]` =
  2), about 1.0 M of the frame's 2.4 M instructions. A second wait, `0x0D866`
  on `[0x262A]`, holds each frame's sky draw to the next game tick.

**Why S lags the frames drawn.** At equilibrium S is about `60F / (R + F/2)`
for F frames a second and R game ticks a second: the controller assumes 60
ticks a second (240 per 4S frames) while the retrace-locked tick is
66.75-70.1, and the `+2S` term adds half a tick a frame. With S below F,
every per-second quantity divided by S (and the mission clock) runs F/S
times real time: 1.29 at GOG's 9 MIPS on `boot_to_flight` (S 9 at 11.6 fps).
When the raw S passes 15, `[0x43E8]` both adds that many ticks of wait to
each frame and removes them again from the measurement, so S stays clamped
at 15 and the frame rate settles near 16.7; the world then runs 16.7/15 =
1.11 times real time. On the parked jet that happens from 16 MIPS up; most
airborne scenes are already there at 9 MIPS. At 66.75-70.09 Hz a four-tick
wait alone is about 16.69-17.52 fps, not 15.

**D1's oscillation is not reproduced.** No run at 3 to 40 MIPS or on the
386 profile showed S fall from 13 or more to 5 or below (`swings` 0 in all
199 flights). The `[0x43E8]` wait keeps the window's ticks above the
correction, so the unsigned underflow the old D1 entry described cannot
occur in the original's own loop; the Reimp's sweep that predicted it ran
the controller without the `0x0409` wait (Reimp `src/core/compose.h:166`).

## Results (300 s flights, no fixes, recomp engine)

Two routes (S and `[0x43E8]` as % of samples where mixed; clock is mission
seconds per emulated second; ticks are game ticks a second):

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

Means over the nine typed flight routes (`boot_to_flight` and the eight
theatre routes):

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

Above 8 MIPS the frame rate depends on the scene far more than on the speed:
in the air most routes already draw about 16.7 frames a second at 8-9 MIPS
(S 15), while the parked jet draws 11.6 at 9 MIPS. No speed gives a clock
ratio of 1.00; on the 386 profile the original runs its world about
1.28-1.38 times real time.

**Caveats.**

- **Missions differ by speed** below 9 MIPS, on the 386 profile and on some
  routes above 9 (the table flags DIFFERENT MISSION): the front end's timing
  seeds the generator. The timing figures are not affected; the engagement
  figures are.
- **All AI, launches and guidance run per drawn frame scaled by S**, not on
  the tick (VGAME `0x5046`, `0x5852`, `0x683E`). This sweep's launch counts
  are too sparse to compare speeds; combat is measured by the threat-profile
  sections below.
- **Not run:** a GOG DOSBox measurement of the game tick rate in flight
  (this machine loses one tick in 21 frames at 11 MIPS and up; whether
  DOSBox does depends on its interrupt latency at the retrace poll).

## D1 follow-up (9 October 2026)

**Setup.** 90 flights: all nine typed routes, 300 guest seconds, at 9, 12,
16, 20 and 40 MIPS, with and without D1:

```
py tools/speed_sweep.py --data D:/GOG/F-117A --out D:/f117-gate/speedsweep/d1 --speeds 9 12 16 20 40 --fix none D1 --seconds 300 --jobs 16
```

**Result.** No errors and no 15-to-3 swing. All nine D1 flights at 9 MIPS
have exactly the same final hashes as their unfixed baselines: the fix is
inactive there. Means over complete flights (first five seconds left out):

| MIPS | complete pairs | no fix: fps / S mean / clock per second | D1: fps / S mean / clock per second |
|---|---|---|---|
| 9 | 9 | 16.273 / 14.100 / 1.1718 | 16.273 / 14.100 / 1.1718 |
| 12 | 9 | 16.347 / 14.241 / 1.1552 | 11.601 / 9.000 / 1.2882 |
| 16 | 9 | 16.793 / 14.957 / 1.1232 | 11.600 / 9.000 / 1.2882 |
| 20 | 9 | 16.753 / 14.989 / 1.1179 | 11.600 / 9.000 / 1.2882 |
| 40 | 8 | 16.716 / 15.000 / 1.1144 | 11.600 / 9.000 / 1.2882 |

Korea at 40 MIPS leaves VGAME for END after about 40-45 sampled flight
seconds with and without D1; those two short flights stay in the raw data
and are left out of the means. Mission identifiers differ across speeds, so
these figures establish pacing, not combat.

**Conclusion.** D1 reproduces the parked jet's GOG pacing on fast machines,
not every scene's GOG pacing. In complete flights it reduces frames drawn
but speeds the world clock up relative to the same fast CPU without the fix.
It does not make the world run at real time.

## Control-response measurements

**Setup.** `tools/stick_response.py` (`--ips`, `--fix`, `--time-us`, a typed
`--front-route`) records raw pitch and roll after 200 ms stick taps, with
mission identifiers and early exits. To fly one mission at every speed, the
boot clock is offset by 0, 110, 110, 220 and 330 ms at 9, 12, 16, 20 and 40
MIPS: START seeds its generator from the BIOS tick (`0x7379` calls `0x8607`,
then srand `0x96BC`), and these offsets give the same mission in all ten
flights without writing guest memory. Example (40 MIPS with D1):

```
py tools/stick_response.py --machine machine --data D:/GOG/F-117A --out OUT --ips 40000000 --fix D1 --front-route tools/routes/middle_east_strike.args --time-us 700000000330000
```

**Result** (original angle units, 65536 a turn):

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

**Conclusion.** Unfixed pitch and roll responses fall within the same spread
at 9, 16, 20 and 40 MIPS. These are responses of progressively diverging
flights, not identical starts, so they do not establish a safe speed. A
typed 386 run selected another mission and never left the ground, so there
is no airborne 386 comparison yet. Artifacts:
`D:/f117-gate/speedsweep/controls-typed`, `controls-aligned`.

## Actual 15 FPS limiter experiment (9 October 2026)

**Setup.** An isolated build changed only D1's frame cap (`D1_FPS_X10` in
`src/fixes/fixes.c`) from 11.6 to 15 fps, inactive at 9 MIPS and below; this
is not the shipped D1. 45 fixed flights (nine routes at 9, 12, 16, 20, 40
MIPS, 300 s) against the D1 follow-up's unfixed rows. Source delta, command
and DLL hash: `D:/f117-gate/speed15/experiment.json`.

**Result.** No errors or swings. Means over complete flights:

| MIPS | complete flights | drawn FPS | S mean | mission seconds per emulated second |
|---|---|---|---|---|
| 9 (cap inactive) | 9 | 16.273 | 14.100 | 1.1718 |
| 12 | 9 | 14.794 | 11.868 | 1.2464 |
| 16 | 9 | 15.000 | 12.000 | 1.2501 |
| 20 | 9 | 14.999 | 12.000 | 1.2509 |
| 40 | 8 | 15.000 | 12.000 | 1.2500 |

All nine 9 MIPS hashes equal the baselines, and interpreter and recompiled
runs of two routes at 40 MIPS agree on all 30,000 observation rows and the
final hashes (`D:/f117-gate/speed15/pair-interp/`).

**Conclusion: rejected.** A 15 fps cap does not make the clock real: S
settles at 12 (the controller's `60F / (R + F/2)` is about 12.1 at F = 15,
R = 66.75) and the mission clock runs about 1.25 times real time, worse
than the default's 1.12. A cap also cannot raise slower scenes to 15 fps.

## Fast-machine default (9 October 2026)

**CPU-speed decision (still current; fix defaults changed 17 October).** The app (`f117a`) runs at 20 million instructions a
second (`MACHINE_APP_IPS` in `src/machine/machine.h`; `--ips` and `ips =`
override it). `f117run` and every recorded route keep GOG DOSBox's 9
million, so no route hash changes. At this decision fixes stayed off and no limiter was added: on
every measured route from 16 MIPS up the game's own clamp holds S at 15 and
the drawn rate near 16.7 frames a second, a clock of about 1.12 at 20 MIPS.
At 9 MIPS a recompiled flight ran at about 68.7 million instructions a
second on the development machine, so 20 million is about 3.4 times real
time there.

Since 17 October normal play enables all fixes, including D1REAL's eight-step
cadence. The figures below describe the unfixed controller; reproduce them
with `--fix none`. CPU speed and simulation cadence are separate settings.

**Matrix.** Nine typed routes, eight boot-clock offsets (0, 3, 7, 11, 15, 19,
23 and 27 s, each a different mission seed), 9, 16 and 20 MIPS: 214 flights
of 300 guest seconds, no fixes. A change of speed also changes the seed, so
each speed is compared as a distribution.

```
py tools/speed_sweep.py --data D:/GOG/F-117A --out D:/f117-gate/fastdefault/matrix --speeds 9 16 20 --offsets-ms 0 3000 7000 11000 15000 19000 23000 27000 --seconds 300 --jobs 24
py tools/speed_compare.py D:/f117-gate/fastdefault/matrix/runs.jsonl
```

| MIPS | complete flights | drawn FPS | S mean | swings | mission clock per second |
|---|---|---|---|---|---|
| 9 (GOG pace) | 70 | 15.27 | 12.95 | 0 | 1.202 |
| 16 | 68 | 16.77 | 14.88 | 0 | 1.129 |
| 20 (default) | 70 | 16.77 | 14.98 | 0 | 1.120 |

Combat in this matrix (about a quarter of a launch per flight, player hit in
1-2 flights of 70 at each speed) showed no difference between speeds, but
its observer was later found faulty (next section).

**What this does not establish.** The power is very low: these routes rarely
launch at the player, so a halving or doubling of combat would not show.
The matrix supports "no large change in combat rate or in hits", not
"combat is unchanged"; the controlled threat profile below measures that.
Errors and early exits are all on Korea's route, at seeds where the scripted
front end does not reach VGAME or the mission ends near 40 seconds; both
occur at 9 MIPS too, so they are not speed effects.

## Controlled threat-profile combat measurement (9-10 Oct 2026)

**Status: superseded** by the corrected cohorts below. `tools/threat_profile.py`
drives VGAME with live telemetry: it approaches and strikes the primary
target as `strike_pilot.py` does, then orbits the target at a fixed radius
and altitude (no enemy site's position is read). Its first run (144 flights,
four theatres, 9/16/20 MIPS) suggested a large launch deficit at high speed,
but its observer hid fresh SA-5 launches at S >= 14 (lifetimes above 32767),
missed burst resets, and dropped damage counters on early exits, and the
pilot's timing and orbit steering were wrong. The corrected cohort did not
confirm the launch deficit. The tools now use the game's launch counter and
valid unsigned lifetimes, and `tests/test_threat_profile.py` checks the
orbit geometry. Raw data: `D:/f117-gate/threatmatrix/`.

## Independent combat investigation (10 October 2026)

**The Reimp's design** (commit `9e0716dc`) separates simulation from
rendering, paces simulation from S and starts S at 5. Its clock tests check
tick counts, not combat; its `frame_weapons.c` keeps the signed lifetime
guard, which its low starting S avoids.

**Mechanisms found** (listing plus staged machines; each is now a fix in
[bugs.md](bugs.md)):

- **Signed lifetime guard (D1TTL).** SA-5 life `150*S*16` is 33,600/36,000
  at S 14/15, but `0x6F19` uses a signed JLE. A natural 600 s Middle East
  flight at 20 MIPS gains an SA-5 burst at 293 s with the fix.
- **Proximity reach shrinks with S (D1PROX).** Reach `(speed<<3)/S` at
  `0x6EF5` is both a tunnelling allowance and the near-miss radius: a staged
  SA-12 at equal geometry damages at S 8 and misses at S 15.
- **Lifetime underflow (D1SLOT).** Seeker-abandoned slots sit at FFFF
  (about 1,100-1,300 slot-seconds over a flight), and launches at `0x54B4`
  and `0x65C3` need their slot at zero life, so one abandoned slot can block
  a launcher. Confirmed in both engines at S 8 and 15.

**Cohort.** Corrected pilot and observer; four theatres; six seed families
(0-12,500 ms in 2,500 ms steps); START clock shifts of +110/+220 ms at 16/20
MIPS make all 24 mission identities match across arms. No guest memory is
staged; early exits stay in. "Damage selections" count damage events, not
impacts (one impact can add difficulty+1).

| Arm | Flights | Early exits | Launches/flight | Bursts/flight | Damage selections/flight | Orbit seconds |
|---|---|---|---|---|---|---|
| 9 | 24 | 9 | 8.5417 | 4.1250 | 8.2500 | 4734.8 |
| 16 | 24 | 5 | 8.4583 | 3.5000 | 7.0000 | 5023.6 |
| 20 | 24 | 6 | 7.7500 | 2.7500 | 5.5000 | 5305.2 |
| 20 + D1TTL | 24 | 6 | 7.7083 | 2.8333 | 5.6667 | 5296.4 |

**Result.** Paired bootstrap (5,000 resamples, 95%), 20 minus 9 MIPS:
launches -0.7917 (-2.0417 to +0.4583), bursts -1.3750 (-2.1667 to -0.6250),
damage -2.7500 (-4.3333 to -1.2500). The earlier launch deficit is not
established; a burst and damage deficit remains, and D1TTL alone does not
close it (bursts -1.2917). Adding the first D1PROX version at 20 MIPS
narrowed it (bursts -0.7500, -1.4167 to -0.1667). A held-out check (seed
families 15000-20000, 12 pairs, 9 MIPS unfixed against 20 MIPS with
TTL+PROX) gave bursts -0.5000 (-1.7500 to +0.6667); pooling all 36 pairs,
-0.6667 (-1.2500 to -0.0833). The first D1PROX version is withdrawn; its
successor is below.

**Reproduce.** `threat_profile.py` into one OUT directory: 9 MIPS offsets
0-12,500; 16 MIPS 110-12,610; 20 MIPS 220-12,720 (and again with `--fix
D1TTL`); all four default routes, `--seconds 600`. Then
`py tools/threat_compare.py OUT/runs.jsonl --shift-ms 16=110 20=220`, which
pairs only identical mission fields and keeps early exits. Artifacts:
`D:/f117-gate/threat-aligned-v5/`, `threat-proximity/`, `threat-heldout/`.

## Cancelled-slot repair and remaining distance defect (10 Oct)

**SLOT cohort.** Four routes x six seed families, TTL+PROX (first version)
with and without D1SLOT at 9 and 20 MIPS; all 24 mission identities match.
SLOT adds +2.1667 bursts a flight at 20 MIPS (95% +1.1667 to +3.2083) and
+2.3750 at 9 (+1.2917 to +3.5000), and fixed traces show no abandoned slots.
With identical options, 20 minus 9 MIPS: launches -0.8333 (-3.0833 to
+1.4167), bursts -0.9583 (-2.2083 to +0.2500), damage -1.9167 (-4.4167 to
+0.5000). Artifacts: `D:/f117-gate/threat-slot/`, `ghost-slot/`.

**Distance probe.** A staged straight pass (SA-12 speed frozen at 28, no
turning, TTL+PROX+SLOT) damages at S 8 and misses at S 15: the original reach
is 28 at S 8 and the first PROX floor only 24, so the first version was
still a speed-dependent band. This led to PROX v2.

**Acceleration probe.** An SA-5 at its normal ground-launch speed 1
(`0x54ED`), terminal speed 28, steering disabled: after two simulated
seconds (16 steps at S 8, 30 at S 15) its speed is 9 against 16. Odd-frame
acceleration at `0x7209`-`0x722B` is not constant per simulated second; it
favours high S, so it cannot alone explain weaker high-speed combat. This
led to D1ACCEL. Artifacts: `proximity-pass-*.json`,
`acceleration-{interp,recomp}.json` under `D:/f117-gate/`.

## Swept proximity and simulated-time acceleration (10 Oct)

**Setup.** D1PROX v2 and D1ACCEL as described in
[bugs.md](bugs.md#d1prox-incoming-proximity-distance-shrinks-with-s). After
two simulated seconds a speed-1, terminal-28 missile reaches 5/9/10/16 at S
4/8/9/15 originally and 10 throughout with the fix, including an S 8-to-15
change in flight. Frozen SA-12 passes at lateral 20 hit at all four S and at
lateral 22/28 miss at all four; the original's outcomes vary, including a
point-sampling miss at S 9.

**Cohort.** 9 and 20 MIPS with TTL+PROX+SLOT+ACCEL, 32 matching mission
pairs:

| Physics v2 arm | Flights | Early exits | Launches/flight | Bursts/flight | Damage selections/flight | Orbit seconds |
|---|---|---|---|---|---|---|
| 9 + TTL + PROX + SLOT + ACCEL | 36 | 22 | 12.1389 | 7.0000 | 14.0000 | 5437.2 |
| 20 + TTL + PROX + SLOT + ACCEL | 36 | 17 | 10.5000 | 5.5556 | 11.1111 | 5441.8 |

**Result.** 20 minus 9 MIPS over 32 pairs: launches -1.4688 (95% -3.1562
to +0.0625), bursts -1.5312 (-2.6250 to -0.5312), damage -3.0625 (-5.2500 to
-1.0625). Physics consistency alone does not
close CPU-dependent combat; the rest came from the simulation cadence and
the pilot's input phase (next section). Artifacts:
`D:/f117-gate/threat-physics-v2/`. Staged checks: `py tools/d1physics_check.py
--data DIR --engine ENGINE`, then again with `--fix D1PROX --fix D1ACCEL`.

## Optional real-time cadence (10 Oct; controlled airborne checks complete)

**Setup.** D1REAL pins S at 8 (4 under the original's 2x compression) and
admits eight simulation frames per machine second; the pacer keeps elapsed
time through overruns, and the original pause and quit dialog reset its
deadline. Its staged clock and dialog checks are in
[bugs.md](bugs.md#d1real-real-time-simulation-cadence) (artifacts
`D:/f117-gate/realtime-checks/`).

**First cohort (five options: TTL+PROX v2+SLOT+ACCEL+REAL).** 24 flights, 12
matching mission pairs, no guest memory staged. S is 8 in every sampled row,
and with the frozen death or exit sequences left out the in-flight clock
runs 1.0001-1.0003 times real time at 8.00 fps: pacing is lossless. Natural
Middle East flights match between interpreter and recompiled code at both
speeds. The burst gap narrowed (20 minus 9: -1.2500, 95% -2.6667 to
+0.1667) but two confounds remained:

- **VGAME's own seed.** VGAME `0xC880` reads the INT 1Ah BIOS tick, stores
  it at DS:9540 and seeds its 32-bit RNG at `0xEE1A`; initialisation
  `0x497D`-`0x4987` draws the initial frame counter from it. Aligning
  START's mission does not align this seed (all 12 pairs differed by 25-33
  BIOS ticks). Delaying only the final hangar confirmation aligns it without
  staging memory.
- **Input phase.** The 200 ms observer sampled different parts of a 125 ms
  simulation step. The step-aligned pilot (`--align-steps`) samples once per
  step and schedules whole-step holds while the guest waits. It is a
  different pilot, so its means are not a before/after comparison.

**Step-aligned cohort (current).** All 12 pairs have identical START mission
identities and VGAME combat seeds:

| Step-aligned arm | Flights | Early exits | Launches/flight | Bursts/flight | Damage selections/flight | Orbit seconds |
|---|---|---|---|---|---|---|
| 9 + five options | 12 | 6 | 11.0000 | 6.3333 | 12.6667 | 1711.3 |
| 20 + five options | 12 | 7 | 11.0000 | 6.5000 | 13.0000 | 1657.2 |

All 12 pairs have identical launch counts; 11 also have identical bursts,
damage and orbit time. The twelfth (Korea, family 22500) matches through
2,136 samples, then a partial-frame sample changes the feedback pilot's
commands. Paired 20 minus 9: launches exactly zero, bursts +0.1667 (0 to
+0.5000), damage +0.3333 (0 to +1.0000).

**Conclusion.** With the five options this cohort shows no high-speed enemy
weakness. It is not a proof that every battle is the same: CPU execution
time still decides which frame reads a key, so whole-flight cross-CPU
determinism is limited by the original's input polling. The original
behaviour stays the default.

**Reproduce.** The five options, `--align-steps`, seed families 2500, 22500
and 25000 at 9 MIPS and +220 ms boot offsets at 20 MIPS, with these delays
on the final START action at 20 MIPS:

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

`--match-flight-seed` rejects absent or unequal VGAME seeds, and the
comparator keeps step-aligned and earlier pilots in separate arms.
Artifacts: `D:/f117-gate/threat-realtime/` and
`D:/f117-gate/threat-phase-corrected.jsonl` (not `threat-phase.jsonl`, whose
Kuwait seeds differ).

## S's self-calibration audit (10 Oct): closed

**Setup.** A static sweep of every VGAME place that multiplies S
(`[0x368E]`), using the recompiler's decoder (`recompiler/x86dec.py`) from
all 523 census-known VGAME entries; script
`D:/f117-gate/audit-s-calibration/audit_s_scaling.py`. Most hits are safe
(seconds-to-frames conversions; 32-bit products through the shift helper
`0xEF68`; `0x446C`'s x960 is protected by `sub dx,dx`).

**Candidate.** In the controller (`0x443C`-`0x4488`), a value derived from
`[0x43E8]` goes negative once S > 15 and feeds an unsigned `MUL [0x368E]` at
`0x4447`; an isolated arithmetic trace drives S to 0 for every starting S
from 16 to 40, which would reach the unguarded divide at `0x4435`.

**Result.** `py tools/d1_check.py --data DIR --ips 40000000 --engine interp
--no-fix` measured S over a full 161.66 s flight at 40 MIPS with every fix
off: S = 15 in all 71,852 samples, never above, and the 90-flight D1 sweep's
unfixed 40 MIPS flights showed no fault.

**Conclusion.** Closed, no fix warranted: a raw S above 15 never reaches this
branch in real play. The audit's other candidate, `0x2BFE` inside
`game_input` (a terrain-distance value truncated after a signed IMUL), was
never pursued and remains an open, unweighted lead.
