# Parity audit, 4 October 2026

*A dated record of the reference-machine comparisons, kept for its findings,
methods and the differences it found. Current parity figures, including the
DOSBox-X and 86Box checks, are in [roadmap.md](roadmap.md).*

The interpreter and recompiler share the CPU semantics and PC model. Equal
machine hashes establish agreement between those engines; the DOSBox checks
below independently test parts of that shared model. A completed route is
not evidence that every mission outcome or physical device works.

## Confirmed checks and fixes

| Check | Result |
|---|---|
| Four new random-flight seeds, 71–74 | All 548 checkpoints and final states agree; 19.5 simulated minutes in flight, two flights end early after uncontrolled inputs |
| Overlapping input and device boundaries | 543 applied inputs, including 143 stress events; 144 checkpoints/final agree; 51 checkpoints in VGAME |
| Roster editing against GOG DOSBox | All three 802-byte saves match exactly: DOSBox, interpreter, recompiler |
| Host stall/resume | Found and fixed the sign of the catch-up offset; an 800 ms suspension now adds about 718 ms, with 100 ms catch-up allowed |
| SDL keyboard mapping | Extended arrows/modifiers, keypad digits/operators, Enter and Divide pass direct mapping checks |
| Rendered AdLib level | Found the missing DOSBox `SetScale(2.0)` gain; fixed before speaker mixing and clipping |
| Full reconnaissance return | Both photo events and intact targets; raised home-runway stop; independent observers' input/flight logs and final states identical |
| Earned career award | Ten-mission Overseas Long Tour Ribbon; 316 checkpoints/final and all 802 save bytes identical between engines |

- **Roster save** hash
  `c734685994ed7755412e0c690489b0fb5be54073133860c38d09b68590038e1d`.
- **Input stress** final machine hash `e88736099423c983` at 3,600,000,000
  clocks. Device stress covers emulated joystick connection, centre, corners,
  both buttons and disconnection, plus mouse relative motion and simultaneous
  buttons. It does not establish physical calibration.
- **Host pacing in CI** excludes SDL startup and uses a 1 MHz, four-second
  ROM-free loop, so shared-runner interpreter throughput is not mistaken for
  clock pacing. Two local repeats add 734 ms for an 800 ms suspension; the
  deterministic clock tests keep the exact 100 ms catch-up limit. Failed CI
  runs keep the application logs and result.

## Routes added by the audit

Each route's two engines agree at every checkpoint and the final state, and
both read-only observers' input logs, flight CSVs and results are
byte-identical, unless stated otherwise. The cockpit or award screens named
were reviewed.

| Route | What it shows | Checkpoints, final hash, clocks |
|---|---|---|
| `recon_return` | Lands at (19196,9454) inside home base 36's approach box, altitude/ground 128, speed/throttle zero, gear down, brakes on, fuel 5009, parent result 0/status 3; exactly one primary 8Ah and one secondary 4Ah event, both targets intact | 272, `c403d0542430b898`, 13,639,324,268 |
| `recon_career` | Award page names the Overseas Long Tour Ribbon for ten missions; score 275, total 2669 -> 2944, sorties nine -> ten, tour ribbon 0 -> 1; both saves SHA-256 `349e4cd5a0560ea903bdf54d4dc02bd09279624b3a53f4d4ba6bf6abf4a5a443` | 316, `f8e2e8955bb74467`, 15,800,000,000 |
| `career_serge` | Roster-UI candidate completes both photos and returns; score 217, total 66 -> 283, sorties 1 -> 2, rank stays zero (first promotion needs total 300, average 100, two sorties, plus a training gate) | 350, `a069f6775d65b94b`, 17,500,000,000 |
| `career_promotion` | Third sortie from the earned roster: First Lieutenant and another Airman's Medal; rank 1, total 500, sorties 3, medal counter 2 (was 1); promotion and medal pages reviewed. It earns the first route's roster afresh rather than using a distributed save, and the public chain reproduces all 354 hashes and the verified save | 354, `2cbd245e873655d5`, 17,700,000,000 |
| `strike` | Destroys Libya training primary 3 with Mavericks: two releases, one matching hit event, damage, credit flag, stores 2 -> 0; fuel 4566, altitude 2355, speed 610, no ejection/crash. An airborne completion, not a return; no interpreted instruction starts in known game modules on its recompiled replay | 175, `197c6b398d6fbb9f`, 8,797,594,941 |
| `strike_return` | Keeps that credit through a normal landing at home 33, (9792,1536): speed/throttle zero, gear/brakes, fuel 2414, countdown 3 with S=8, parent result 0/status 3 | 297, `a4f05231eacf9d75`, 14,872,594,982 |
| `secret_airstrip` | Type-4 primary by landing at Persian Gulf target 24: stops at (10497,3809) within the 9-by-40 half-widths of the approach box, idle, gear/brakes, fuel 7261; event 8Bh, primary flag and mission store 1 -> 0. No home return claimed. A first approach outside the shorter box earned no credit; only the control aim was adjusted | 167, `5762b7e88b22cda9`, 8,373,782,617 |
| `secret_airstrip_return` | Adds a ground turn, taxi, takeoff and return: home 58 at (2880,11786), ground 0, speed/throttle 0, fuel 3000, gear/brakes, countdown complete, parent 0/3, VGAME exits 129. The strip is intact at delivery and damaged later with the player 8655 units away and no releases; the observer reports that separately | 372, `c7b303ee1aa60173`, 18,635,798,708 |
| `cargo` | Reproduces D5 ([bugs.md](bugs.md)) in a Persian Gulf type-3 mission: crate (class 26h/weapon 18, player slot 11) crosses ground at (10500,3951), height -5, octagonal distance 113 from target 24 (below 256), mission time 588 before deadline 1094; impact globals match the slot, TTL 985 -> 0, primary flag/event absent for another 1.2 s; fuel 7075, aircraft recovers to height 444. Five acceptance regressions reject expiry, hovering at height one, unmatched impact globals, wrong slots/weapons, late/outside-area impacts, loss and unexpected credit | 149, `20b78dd06d275670`, 7,498,892,689 |

The public `career_serge` / `career_promotion` fixtures contain only
ordinary keyboard/mouse inputs.

**Earned sorties 7-13** were paired with the same tool: each agrees at 380
checkpoints, final state, input/CSV/result files and all 802 saved bytes.
Sortie 8: score 219, total 1370 -> 1589, sorties 7 -> 8, hash
`ce0867bc52a1995b`, roster equal to an independently run private career
batch. Sortie 13: rank 2, score 219, total 2684, count 13, hash
`4cd28bafd25577e6` at 19,000,000,000 clocks. Pilot status is record offset
4Eh (Serge file offset 800); an earlier offset-778 assertion checked the
theatre instead, and the fixtures now assert both fields. This establishes
earned continuation, not retirement. Sortie 14 ran only under the
recompilation and is not paired evidence.

## Comparisons with GOG DOSBox, with differences

**Reconnaissance sortie.** The adaptive pilot needed two corrections before
the original completed the sortie: restoring power below 240 knots, and
designating with the physical nose angle, adding the camera mounting offset
only after acquiring the target (as the two original routines do). The
earlier failed attempts (`dosbox-recon-02`, 04, 05, 06) were pilot failures;
no game-model defect was isolated from them. With the corrected pilot the
recompiled return still reaches `c403d0542430b898`, and a normal-input return passes both engines'
observers at DSWAP clock 13,836,105,984, hash `f4ae7ae4e610850d` (fuel 4910,
countdown 3 with S=8, parent 0/3). The optional `--acquisition level` mode
(nonnegative nose, designation near the original's constant 640-unit ray,
range and heading gates) completes the recompiled sortie at hash
`91b4bdf6f00ff61d` (13,672,636,250 clocks), and **GOG attempt 07 with it
completes both photo credits and the raised-home return, then enters END**:
all acceptance checks pass across 104,757 observations; the aircraft stops
at (19199,9452) inside home 36's box, altitude/ground 128, fuel 3718,
countdown 2 with S=9, parent 0/3; each target intact with exactly one
matching event. Inputs follow wall time from each program's start, so the
generated primary and trajectory differ from the recompiled fixture: this is
original-game completion under the same acceptance criteria, not exact
dynamic-state equivalence.

**Sound.** `audio_render` uses the application's actual audio
implementation. `tools/audio_compare.py` extracts PCM from the DOSBox AVI and
aligns RMS envelopes over about 130.75 seconds. With GOG's DOSBox 0.74-2.1
DBOPL core (now the default, 44,100 Hz, 2x gain; Nuked via `--opl nuked`)
the app-rendered intro RMS is 0.100662 against the reference's 0.100150
(0.050381 before the gain fix), median spectral similarity 0.9826, envelope
correlation 0.9165. Exact PCM still differs; these mono, resampled figures
are diagnostics, not stereo parity or a listening verdict. An independent
512-sample renderer and the app's sample-sized renderer differ in 270 stereo
frames of a 6,394,500-frame log, around channel silencing; that DBOPL block
behaviour is kept and documented.

**OPL tone probe.** `tools/opl_probe.py` builds its own 16-bit COM program
(two sustained OPL notes and key-off), captures GOG DOSBox PCM and renders
the runtime's writes through both backends. It exposed the reference mixer's
14-bit interpolation remainder; matching that rounding makes the first
86,524 stereo frames identical (1.962 s, until the reference changes
frequency), where only 22,979 of the first second matched before. The full
probe stays unequal because BIOS-tick phase moves note transitions; the
comparator reports that failure and the exact prefix separately. No game
bytes are needed.

**Flight state.** `tools/dosbox_flight.py` finds DOSBox's guest RAM by its
ROM signature, validates the owning MCB/PSP and reads with read/query
permissions only; the game is unmodified and inputs use the normal Windows
keyboard and mouse path on a private install copy.
`tools/flight_state_compare.py` reads the same fields through our read-only
machine API. All sixteen initialised mission/aircraft fields agree.
Flight-time checkpoints differ: at about 90 s into the intro-skip trial,
reference/ours speed is 149/143 knots, altitude 9,057/8,494 feet, fuel
9,412/9,424, both frame-rate class 15; RNG and counters differ too. Reference
input uses wall-time deadlines and about 100 ms polling, and guest RAM is not
frozen while read, so this does not isolate a flight-model defect or prove
AI, missile, radar or objective parity. The comparator returns failure when
differences exist.

## Roland (Munt)

Munt (libmt32emu 2.8.3, commit `6e7c01fba7e1d50c8fa705834889fd0eac136075`)
was integrated during the audit. MIDI stream tests cover running status,
real-time interleaving, bounded SysEx and recovery; missing ROMs and
conflicting/incomplete output options fail explicitly. The Roland boot
route's 26 checkpoints/final agree (hash `f075fcb3add06eb2`) and all 13,788
captured clock/byte records match. A seven-second tone render keeps all
308,700 stereo frames byte-for-byte under both DBOPL and Nuked. Supplied
MT-32 1.07 control and PCM ROMs match Munt's known SHA1 digests; the Roland
MIDI log renders 6,370,000 stereo frames at 44,100 Hz. A live 30-second
replay contains sound (peak 9698, RMS 807.47) and is exactly silent without
a synth. This found and fixed a host startup bug: recreating audio after
loading the ROMs discarded Munt; the replay clock is now read before audio is
created once. Exact reference PCM and flight engine sound were not
established.

## Reproduction and evidence

Game-dependent artifacts are under
`%USERPROFILE%/f117-recomp-local/parity-audit-20261004/`, outside the public
repository: video replay `video/intro-fpfq6g83` against
`video/intro-p6mrjdbw`; top-level `parity-audit-*.log` command outputs;
`roster-comparison.json`, `input-stress/result.json`,
`host-timing-final/result.json`, `audio-gain-comparison.json` and
`flight-compare-skip/comparison.json`. Failed trials are kept separately,
never overwritten or counted as successes.

```powershell
py tools/build_recomp.py --data D:/GOG/F-117A --parity-only --work PRIVATE_DIR
py tools/fidelity.py --data D:/GOG/F-117A --diffs-only --work PRIVATE_DIR/fidelity
py tools/input_stress.py --data D:/GOG/F-117A --out PRIVATE_DIR/input-stress
py tools/host_timing.py --out PRIVATE_DIR/host-timing
py tools/dosbox_flight.py --data D:/GOG/F-117A --out PRIVATE_DIR/reference --skip-intro
py tools/flight_state_compare.py --data D:/GOG/F-117A --reference PRIVATE_DIR/reference/flight.csv --out PRIVATE_DIR/comparison --skip-intro
```

`audio_render OPL.LOG OUT.WAV 9000000 END_CLOCK` renders a runner's OPL log.
The sound comparator needs NumPy and ffmpeg; DOSBox observation needs Windows
and pywin32.

## Left open by the audit

Items the audit left open and that are settled are in [roadmap.md](roadmap.md)
(earned retirement, the supply-drop return, the speech reference, air-to-air
objectives). Still open:

- AI countermeasures; harder landing settings; in-flight video against a
  reference.
- Physical controls: no WinMM joystick is attached on this machine, so
  calibration, deadzones and control feel need hardware and a player.
- MT-32 PCM needs a reference comparison with the supplied ROMs; the Roland
  boot route alone cannot validate rendered parity.
