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

### Same-scene CPU comparison

A controlled follow-up held the OPTi 495SX chipset, VGA, DOS installation,
game files, mission and parked aircraft state constant, changing only the
386DX clock. Each private disk copy settled for 30 seconds, then supplied
901 samples over three consecutive 60-second windows. All eleven recorded
scene fields agreed throughout every run; compression stayed at 1.

| 386DX clock | S | Actual updates/s | Mission seconds in 180.007 emulated seconds | Clock ratio |
|---|---|---|---|---|
| 25 MHz | 4 | 5.2276 | 235 | 1.30551 |
| 33.333 MHz | 5 | 6.3720 | 229 | 1.27218 |
| 40 MHz | 6 | 7.0053 | 210 | 1.16662 |

There is no exact common multiplier on these configurations. The differences
are much larger than the approximately 0.0056 ratio uncertainty from a
one-second mission-clock count over 180 seconds. This compares the same
scene on one hardware family, not every historical PC, and still cannot
establish intent. Retaining the original clock remains a preservation choice;
normal play currently keeps D1REAL enabled. Evidence:
`D:/f117-gate/clock-matrix-20261017/{matrix.py,matrix.json,verdict.json}`.

## Historical intent and faster-than-real-time simulation (17 October 2026)

The closest contemporary written evidence is the predecessor's designers'
notes. MicroProse's *F-19 Stealth Fighter* manual (1988), in its discussion of
automatic CPU-speed adjustment, says **“the simulation moves at the same
real-time speed”** across a 4.77 MHz 8088 and a 25 MHz 80386; graphics
smoothness and detail are what change. This explicitly states hardware-independent
real-time intent for F-19. Applying that intent to F-117A is an inference,
not a recovered statement about its precise clock or update frequency.
Source: [original manual, Designers' Notes](https://www.scribd.com/doc/39413077/F-19-Stealth-Fighter).

Deliberately accelerated flight simulations did exist, for different purposes:

- **Travel compression in games:** F-19 and F-117A describe a selectable
  doubling of time, with normal time restored for combat/landing. That explains
  their explicit acceleration control, not an undocumented CPU-dependent
  multiplier at normal speed. [F-117A manual, Accelerate Time](https://steamcdn-a.akamaihd.net/steam/apps/328920/manuals/Manual.pdf).
- **Training at a deliberately higher pace:** a 1994 F-16 simulator study
  used 1.0x, 1.5x, 2.0x and mixed acceleration with 24 mission-capable pilots,
  followed by real-time tests. A later NASA report traces this research to
  Kolf (1973) and Hoey (1976). This establishes intentional faster-than-real-time
  simulation before and around this game's era, but supplies no connection
  to MicroProse's design. [1994 paper](https://ntrs.nasa.gov/citations/19940027968),
  [2000 report](https://ntrs.nasa.gov/search.jsp?R=20000017960).
- **Later games:** *JetFighter III*'s 1996 handbook explicitly defines its
  default as 1:1 real time, with selectable 2x and 3x compression. Modern
  Microsoft Flight Simulator documentation likewise defines simulation rate
  1 as one simulated second per real second, independently of visual frame
  rate. [JetFighter III handbook, Time Compression](https://www.scribd.com/document/505351766/Jetfighter-III-R-D-F-Handbook),
  [Microsoft SDK](https://docs.flightsimulator.com/msfs2024/html/6_Programming_APIs/Environment_Variables.htm).

This search found no primary evidence that F-117A's measured variable
normal-speed acceleration was deliberate. F-19's explicit statement strengthens
the case for real-time intent in this family; it does not prove D1REAL's
eight-step choice, establish a canonical proximity radius, or show that every
original timing discrepancy is an unintended defect.

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
determinism is limited by the original's input polling. These were opt-in
checks when run; normal play enables all fifteen fixes since 17 October.

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

### Comparing fixed cadences (17 October 2026)

The owner authorized a hidden comparison of 8 and 15 updates/s. A private
build changes only D1REAL's integer divisor and fractional pacer; 14 Hz is
an additional control because 15 cannot halve exactly under the original's
2x compression scheme. Production code and the eight-step default are
unchanged. All repairs remain enabled except D1 in the valid higher-rate
comparison: its independent 11.6 Hz compatibility limiter otherwise caps
both candidates. At eight, excluding D1 leaves the guest results unchanged.

The experimental eight-step build first reproduces the existing build's
seven clock/dialog cases and scripted flight hashes exactly in both
engines. Every higher-rate clock, flight, input and isolated weapon result
is also checked between the interpreter and recompiled engines. A failed
clock assertion remains a failure, even where the prototype's integer
arithmetic explains it.

At the normal 20 MIPS setting, the seven-case clock check passes at 8 and
14 Hz. At 15 Hz only compression fails: S becomes 7 and 70 seconds advance
150 mission seconds instead of 140 (15/7 = 2.142857x). Normal time gives
exactly 30 mission seconds and 450 updates in 30 seconds. Fixing 15 Hz
compression would require another timing change; this experiment does not
silently make it.

| Cadence | Normal updates in 30 s | Mission seconds in 70 s at 2x | Throttle response median / maximum |
|---|---|---|---|
| 8 Hz | 240 | 140 | 64.5 / 112 ms |
| 14 Hz | 420 | 140 | 31.5 / 68 ms |
| 15 Hz | 450 | 150 (fails) | 35.0 / 67 ms |

Response uses 24 normal-input throttle pulses at varied phases, read at
1 ms resolution; every pulse is observed. This measures guest control
state, not input-to-photon delay or subjective handling. The small sample
does not rank 14 against 15. Both higher rates reduce the waiting interval
relative to eight. Interpolation's additional picture age is separate.

At 9 MIPS, eight still passes. Both higher targets fall behind in the
parked scene: only 348 updates in 30 seconds (11.6/s), giving 25 mission
seconds at S=14 and 23 at S=15. Their compression cases both give 116
mission seconds in 70 seconds. This is an emulated CPU execution-budget
limit, distinct from host fullscreen rendering. It supports the original
conservative choice of eight for the supported slower configuration.

The same normal-input takeoff script produces different speed and climb at
about 55 seconds. This demonstrates a gameplay change; it does not establish
which cadence is authentic or isolate integer rounding from input sampling.
In the isolated parked-aircraft missile pass, all three rates hit at offset
20 and miss at 22 and 28 with D1PROX, and the tested missile accelerates
from speed 1 to 10 in two seconds with D1ACCEL. These are staged boundary
fixtures, not a recovered warhead radius or proof of identical guidance.

Six exploratory normal-input flights use the same two missions and their
original combat seeds, at 20 MIPS, with the existing 5 Hz feedback pilot.
All three Middle East flights end early; all three Korea flights remain
active through 600 seconds. Outcomes differ. Keep those early endings:
different trajectories, input sampling and exposure prevent interpreting
the counts as a lethality or equivalence verdict. No subjective handling
or fullscreen acceptance is claimed.

Evidence: `D:/f117-gate/cadence-20261017/`, with the comparison excluding
D1 under `without-d1/`. The initial all-fixes cap results, unfinished
confounded combat runs and unsuccessful cannon probes are retained and
must not be presented as successful higher-rate or weapon tests. A separate
staged guidance probe did not acquire/steer and does not establish guidance
equivalence. The retained comparison verdict checks engine agreement and
control integrity, not acceptance of every candidate. Cannon behavior and
controlled guidance equivalence remain unverified by these probes.

**Recommendation from this first comparison (before the follow-up below):** keep eight as the tested default. Fourteen is a stronger
candidate than fifteen for further higher-cadence work because its divisor
supports exact 2x compression, but it still needs sufficient emulated CPU
speed, separate original-rate compatibility handling, and broader gameplay
and physical-display assessment. Neither candidate recovers developer intent.

### Fifteen-step compression follow-up (17 October 2026)

The owner's target is the best stable responsiveness the original could
provide, at consistent intended world speed across systems; legitimate bug
repairs do not count as unfair advantages. Eight is a tested compatibility
baseline, not a recovered developer target. Two further private prototypes
therefore keep normal play at 15 Hz/S15 and investigate compression.
Neither changes the production default.

**Normal play:** 450 updates and 30 mission seconds in 30 elapsed seconds
at both 20 and 40 MIPS, with interpreter/recompiled hashes identical. The
first prototype's normal-only missile and throttle probes retain the prior
fifteen-step hashes: 24 throttle pulses, median 35 ms, maximum 67 ms.
Both normal-input flight scenarios also retain their prior hashes and
agree between engines (Middle East `1936ec547dcedc78`, Korea
`8e612cbc086ebd5f`). These do not prove a recovered intended cadence,
guidance equivalence or physical-display performance.

**15 Hz/S15 normal, 14 Hz/S7 compressed:** all seven settled clock/dialog
cases pass at 20 and 40 MIPS, as do six compression round trips measured
after settling. However, a 120-transition normal-input stress run advances
82 mission seconds in 48 elapsed seconds, against 72.469 seconds obtained
by integrating the observed compression mode. The eight-step control
advances 72 against 72.000. The changing global-counter divisor and pacing
phase need further correction; reject this prototype despite its steady
clock results.

**15 Hz/S15 normal, 30 Hz/S15 compressed:** a second prototype holds the
simulation step size constant and doubles actual updates under compression.
It retains the next deadline when changing cadence. At 20 MIPS the parked
scene cannot supply 30 updates/s: the 70-second compressed window produces
1,636 updates / 109 mission seconds, then the restored normal window catches
up (234 updates / 15 mission seconds in ten elapsed seconds). At 40 MIPS,
the seven steady clock/dialog checks pass, including 2,100 updates and
140 mission seconds in 70 compressed seconds. This is an execution-capacity
finding, not proof that 40 MIPS is the minimum or sufficient in every scene.
At 40 MIPS six settled round trips also pass, but rapid toggling still loses
1.957 mission seconds against 69.957 integrated seconds (68 counted).
Retaining an old-rate deadline does not preserve the remaining fraction
of a step when changing the rate. This second prototype also fails the
transition stress requirement.

**Corrected constant-step candidate:** at the existing rate-change hook,
scale the deadline's remaining time (or overdue work) by old/new cadence.
At 40 MIPS this passes all seven clock/dialog cases, all twelve settled
windows in six round trips, and all 120 rapid transitions, with every case
and final hash equal between engines. The rapid test counts 68 mission
seconds against 68.012 integrated seconds (mode sampled every millisecond),
within the integer mission counter's resolution. Initial normal-flight
clock hashes remain unchanged. Twelve final consistency/acceptance checks
pass in `constant-step/phase-corrected/verdict.json`.

This makes normal fifteen-step timing a viable candidate, with constant-S
compression and sufficient execution capacity, rather than proving that
fourteen is the highest usable rate. Forty MIPS is the tested configuration;
the earlier 20 MIPS capacity failure is not repaired by changing deadline
arithmetic. This prototype still uses private packed pacer metadata. A
production implementation needs explicit per-machine cadence state, the
full gate, presentation checks and physical-display assessment before
adoption. Fast original-hardware measurements remain necessary to assess
the owner's fidelity target; these tests establish consistency, not intent.

Both private pacers pass 32 simulated hour-long fractional-clock schedules,
event waits, state-copy, overrun and reset checks. Fixes-off boot/quit runs
retain all 54 checkpoints and the final hash between pre-experiment and
candidate libraries and both engines. D1 is excluded because its separate
11.6 Hz limiter conflicts; the other fourteen repairs are enabled. The
first attempt at the reference harness assumed exact checkpoint stops;
matched execution can cross the requested boundary, so corrected captures
compare actual stops. Failed harness logs are retained separately.

Evidence: `D:/f117-gate/cadence15-followup-20261017/`, with the constant-step
variant under `constant-step/`. These are API experiments; production source
and default settings remain unchanged, and no fullscreen acceptance was run.

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
