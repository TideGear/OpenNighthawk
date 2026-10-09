# Emulated CPU speed and VGAME's frame-rate controller: measurements

Partial results of a sweep of the emulated machine's speed (9 October 2026), for choosing the
app's default speed or a frame limiter. Nothing here changes the app's default. Reproduce with
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
- **Not run:** the pilots (own hits, kills, deliveries) and `stick_response.py` per speed; the
  `--fix D1` sweep; the candidate 15 fps limiter (4 ticks a frame); a GOG DOSBox measurement of
  the game tick rate in flight (this machine loses one tick in 21 frames at 11 MIPS and up;
  whether DOSBox does depends on its interrupt latency at the retrace poll).
