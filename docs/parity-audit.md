# Parity audit, 4 October 2026

The interpreter and recompiler share the CPU semantics and PC model. Equal
machine hashes establish agreement between those engines; the DOSBox checks
below independently test parts of that shared model. A completed route is
not evidence that every mission outcome or physical device works.

## Confirmed checks and fixes

| Check | Result |
|---|---|
| All sixteen existing routes | Identical checkpoints and final states; route milestones pass |
| Instruction lockstep | 89,276 starts, 5,713,152 states, 512 declined, zero mismatching starts |
| Machine against GOG DOSBox | 1,210 comparable answers agree, zero differences |
| Four new random-flight seeds, 71–74 | All 548 checkpoints and final states agree; 19.5 simulated minutes in flight, two flights end early after uncontrolled inputs |
| Overlapping input and device boundaries | 543 applied inputs, including 143 stress events; 144 checkpoints/final agree; 51 checkpoints in VGAME |
| Roster editing against GOG DOSBox | All three 802-byte saves match exactly: DOSBox, interpreter, recompiler |
| Host stall/resume | Found and fixed the sign of the catch-up offset; an 800 ms suspension now adds about 718 ms, with 100 ms catch-up allowed |
| SDL keyboard mapping | Extended arrows/modifiers, keypad digits/operators, Enter and Divide pass direct mapping checks |
| Rendered AdLib level | Found the missing DOSBox `SetScale(2.0)` gain; fixed before speaker mixing and clipping |
| Full reconnaissance return | Both photo events and intact targets; raised home-runway stop; independent observers' input/flight logs and final states identical |
| Earned career award | Ten-mission Overseas Long Tour Ribbon; 316 checkpoints/final and all 802 save bytes identical between engines |

The save hash is
`c734685994ed7755412e0c690489b0fb5be54073133860c38d09b68590038e1d`.
The input stress final machine hash is `e88736099423c983` at
3,600,000,000 clocks. Device stress includes emulated joystick connection,
centre, corners, both buttons and disconnection, plus mouse relative motion
and simultaneous buttons. It does not establish physical calibration.

The CI pacing integration check now excludes SDL startup and uses a
1 MHz, four-second ROM-free loop, to avoid confusing interpreter throughput
on a shared runner with clock pacing. Two local repeats add 734 ms for an
800 ms suspension; the deterministic clock tests retain the exact 100 ms
catch-up limit. Failed CI runs preserve the application logs and result.

`recon_return` matches at 272 checkpoints and final hash `c403d0542430b898`
at 13,639,324,268 clocks. It lands at (19196,9454), inside home base 36's
approach box, altitude/ground 128, speed/throttle zero, gear down, brakes on,
fuel 5009, parent mission result 0/status 3. Two exposures produce exactly
one primary 8Ah event and one secondary 4Ah event; both targets remain intact.
The return observers' input logs and CSVs are byte-identical between engines.

`recon_career` ends at 15,800,000,000 clocks with hash `f8e2e8955bb74467`.
The award page names the Overseas Long Tour Ribbon for ten missions. Saved
mission score is 275, total rises from 2669 to 2944, sorties from nine to ten
and tour ribbon from zero to one. Both saves have SHA-256
`349e4cd5a0560ea903bdf54d4dc02bd09279624b3a53f4d4ba6bf6abf4a5a443`.
This does not yet independently compare the completed sortie with DOSBox.
The first adaptive reference attempt credited the primary photo but crashed
on the next leg at 44% throttle, speed 164, after about 711 seconds. This is
retained as a failed check (`dosbox-recon-02`), not a successful return. The
diagnostic pilot now restores power below 240 knots, and the observer waits
through DSWAP for END. Attempts 04 and 05 did not complete both objectives:
04 credited the primary but failed to acquire the moving secondary; 05
circled the primary with the scratch object designated for its full
1,900-second limit. These are retained as controller failures. Attempt 06
uses the physical nose angle for designation and adds the camera mounting
offset only after acquiring the target, matching the two original routines.
The corrected pilot's new normal-input return passes both engines' independent
observers: input logs, flight CSVs and reports are identical, ending at
DSWAP clock 13,836,105,984, hash `f4ae7ae4e610850d`. Both photo-credit events,
intact targets, raised home stop and countdown 3 with S=8 pass. Fuel is 4910
and parent result/status is 0/3. Fine read-only sampling captures the final
countdown before the next executable reuses memory.
An adaptive
recompiled rerun with the correction still completes both photos and the
return with the established `c403d0542430b898` hash and no acceptance errors.

The Serge career candidate selected through the roster UI completed both
photos and returned. Its debrief saved score 217, total 66 -> 283 and sorties
1 -> 2, but rank stayed zero. The original first-promotion thresholds are
total 300, average 100 and two sorties, with a separate training gate.
This candidate is not a verified promotion. A further normal sortie starts
from the earned 802-byte roster, copied unchanged into a fresh save directory.
The candidate's 350 checkpoints/final and all saved bytes agree between
engines; final hash is `a069f6775d65b94b` at 17,500,000,000 clocks.

Serge's third sortie earns First Lieutenant and another Airman's Medal.
Saved rank is 1, total 500, sorties 3 and medal counter 2 (previously 1).
All 354 checkpoints/final and the complete 802-byte save match between
engines, hash `2cbd245e873655d5` at 17,700,000,000 clocks. Promotion and
medal pages were reviewed. Public `career_serge` / `career_promotion`
fixtures contain only ordinary keyboard/mouse inputs. The second route
earns the first route's roster afresh through the selected engine rather
than depending on a distributed saved file.
The fresh public recompilation chain reproduces all 354 promotion hashes
and the verified save, with its prerequisite roster matching the verified
second-sortie save. Adding both career routes' interpreter coverage raises
the measured union to 138,584 / 230,052 bytes (60.2%).

## Independent comparisons with differences

The added `strike` route destroys Libya training primary target 3 using
Mavericks through normal controls. Two release events, one matching primary
hit event, target damage, credit flag and stores 2 -> 0 pass independent
observation; fuel 4566, altitude 2355, speed 610 and no ejection/crash at the
end. Both observers' recorded input and flight CSVs are byte-identical.
All 175 checkpoints plus final state agree, ending at 8,797,594,941 clocks
with hash `197c6b398d6fbb9f`; the credit screenshot was reviewed. This is
an airborne primary completion, not a verified return. No interpreted
instruction starts inside known game modules were recorded on its recompiled
replay. Adding its interpreter coverage raises the measured union to
133,926 / 230,052 bytes (58.2%) before adding its return coverage.

The separate `strike_return` route retains that primary damage and hit credit
through a normal landing at home 33, (9792,1536), ground altitude zero.
Speed/throttle zero, gear/brakes, fuel 2414, countdown 3 with S=8 and parent
mission result 0/status 3 pass. Both observers' input logs, flight CSVs and
result JSON are byte-identical. All 297 checkpoints and the final state
agree at 14,872,594,982 clocks, hash `a4f05231eacf9d75`. The stopped cockpit
was reviewed. Fine read-only sampling at idle home captures the final
countdown increment before DOS exit; the acceptance threshold is unchanged.
Adding this interpreter coverage raises the union to 134,132 / 230,052
bytes (58.3%); recon return/career coverage remains pending.

`secret_airstrip` completes a type-4 primary by normally landing at Persian
Gulf target 24. The aircraft stops at (10497,3809), within the 9-by-40
half-widths of its approach box, ground altitude zero, idle, gear/brakes,
fuel 7261 and no loss or damage. Event 8Bh, primary flag and mission store
1 -> 0 are independently required. Both observers' input logs, CSVs and
reports are byte-identical. All 167 checkpoints and final state agree at
8,373,782,617 clocks, hash `5762b7e88b22cda9`. The cargo-delivered cockpit
was reviewed. The aircraft remains at the strip; no home return is claimed.
The first approach touched down outside the shorter box and earned no
credit; that failure is retained. Only the normal control aim was adjusted.
Adding the strip's interpreter coverage raises the route union to 138,771 /
230,052 bytes (60.3%).

The normal `cargo` route reproduces D5 in a generated Persian Gulf type-3
mission. One supply crate, class 26h/weapon 18, is released into player slot
11 and crosses ground altitude at (10500,3951), height -5. The original
octagonal distance is 113 from target 24, below 256; mission time 588 is
before deadline 1094. Impact globals match that slot's coordinates, TTL
drops from 985 to zero, and primary flag/event stay absent for another
1.2 seconds. Fuel remains 7075 and the aircraft recovers airborne to height
444. This preserves the original bug rather than claiming objective credit.
Both read-only observers' input/CSV/result files match exactly; 149
checkpoints and final hash `20b78dd06d275670` agree at 7,498,892,689 clocks.
The impact cockpit was reviewed. Five acceptance regressions reject expiry,
hovering at height one, unmatched impact globals, wrong slots/weapons,
late/outside-area impacts, loss and unexpected credit. All 64 Python tests
pass. Coverage rises to 139,059 / 230,052 bytes (60.4%), across 24 routes.
Independent DOSBox cargo reproduction and the home return remain open.

Independent GOG recon attempt 06 ended after fuel exhaustion: no photos,
parent result 1/status 1, nine failed acceptance checks, 104,607 observations.
Its completed failed result is retained, and independent full-sortie parity
remains open. No shared game-model defect has been isolated from these
controller attempts.

The optional `--acquisition level` diagnostic holds a nonnegative nose and
requests designation near the original's constant 640-unit ray, with range
and heading gates. Camera pointing still uses its separate mounting angle
after acquisition. Its normal recompiled recon sortie completes both photos
and the home return; both engines' replay observers have identical input,
CSV and result files, final hash `91b4bdf6f00ff61d` at 13,672,636,250 clocks.
GOG attempt 07 using that approach independently completes both photo
credits and the raised-home return, then enters END. All acceptance checks
pass across 104,757 observations. The aircraft stops at (19199,9452), inside
home 36's box, altitude/ground 128, zero speed/throttle, gear/brakes, fuel
3718, countdown 2 with S=9 and parent result/status 0/3. Both photo targets
remain intact with exactly one matching event each. Inputs follow wall time
since each program starts; the generated primary and flight trajectory differ
from the recompiled fixture. This establishes original-game completion with
the same acceptance criteria, rather than exact dynamic-state equivalence.

**Video:** current-build replay against the saved GOG video still matches
1,319 exact RGB pictures in order. Seven reference pictures and 28 of ours
remain unmatched, all for a single sample. The end transition is about
571 ms later here. Neither difference is dismissed as a pass.

**Sound:** `audio_render` uses the application's actual audio implementation;

Optional Munt integration now builds using libmt32emu 2.8.3, commit
`6e7c01fba7e1d50c8fa705834889fd0eac136075`, from a separate source checkout.
The enabled interpreter build passes nine ROM-free CTests; the default
generated build passes ten, plus all 64 Python regressions. Missing ROMs and
conflicting/incomplete output options fail explicitly. MIDI stream tests cover
running status, real-time interleaving, bounded SysEx and recovery. The Roland
boot route's 26 checkpoints/final agree, hash `f075fcb3add06eb2`; all 13,788
captured clock/byte records match. A seven-second tone render retains all
308,700 stereo frames byte-for-byte under both DBOPL and Nuked. Headless MIDI
capture and offline Munt rendering are available, but no Roland PCM or
listening success is claimed without the user-supplied ROMs.

`tools/audio_compare.py` extracts PCM from the DOSBox AVI and aligns RMS
envelopes. Over approximately 130.75 seconds, reference RMS is 0.100150 and
our corrected RMS is 0.100761, a difference of about 0.6%. Before the gain
fix ours was 0.050381. Median spectral cosine similarity is 0.9813, envelope
correlation 0.9137. The waveform is unequal. These mono, resampled measurements
are diagnostics, not exact stereo parity or a listening verdict. Nuked OPL3
and DOSBox's synthesizer remain different.

**Subsequent synthesis change:** the app now defaults to GOG's supplied
DOSBox 0.74-2.1 DBOPL chip core, with its 44,100 Hz rate and 2x gain.
Nuked remains available through `--opl nuked`. Both backend paths pass
timestamped-write/chunk-boundary tests; speaker gate and DBOPL tone release
tests pass. The new app-rendered intro RMS is 0.100662 versus 0.100150,
median spectral similarity 0.9826 and envelope correlation 0.9165. Exact
PCM still differs. Source identity is not a full audio-parity verdict.
An independent 512-sample renderer and the app's sample-sized renderer
differ in 270 stereo frames of the 6,394,500-frame log, around channel
silencing; that DBOPL block behavior is retained and documented.

**ROM-free tone probe:** `tools/opl_probe.py` creates its own 16-bit COM
program with two sustained OPL notes and key-off, captures GOG DOSBox PCM,
and renders the runtime's writes through both app backends. It exposed the
reference mixer's 14-bit interpolation remainder even at equal rates.
Matching that rounding makes the first 86,524 stereo frames identical
(1.962 seconds, until the reference changes frequency). Before this fix,
only 22,979 frames of the first second matched; after it, all 44,100 do.
The full probe remains unequal because BIOS-tick phase gives different
note transition times. Its comparator preserves that failure and reports
the exact prefix separately. No game bytes are needed for this check.

**Flight state:** `tools/dosbox_flight.py` locates DOSBox's guest RAM using
its ROM signature and validates the owning MCB/PSP. Its process handle has
read/query permissions only. The original game is unmodified, and inputs
use the normal Windows keyboard and mouse path on a private install copy.
`tools/flight_state_compare.py` checks the same fields through our read-only
machine API. All sixteen initialized mission/aircraft fields agree.
Flight-time checkpoints differ. At approximately 90 seconds in the matched
intro-skip trial, reference/ours speed is 149/143 knots, altitude 9,057/8,494
feet, fuel 9,412/9,424; both report frame-rate class 15. RNG and counter states
also differ. Reference input uses wall-time deadlines and roughly 100 ms
polling, rather than exact instruction clocks; guest RAM is not frozen while
reading. This does not isolate a flight-model defect or prove exact AI,
missile, radar or objective parity. The comparator reports observed
differences and returns failure when they exist.

## Reproduction and evidence

All game-dependent artifacts are under
`%USERPROFILE%/f117-recomp-local/parity-audit-20261004/`, outside the public
repository. Video replay is `video/intro-fpfq6g83`, against
`video/intro-p6mrjdbw`. Top-level `parity-audit-*.log` files retain command
outputs. `roster-comparison.json`, `input-stress/result.json`,
`host-timing-final/result.json`, `audio-gain-comparison.json` and
`flight-compare-skip/comparison.json` retain detailed results.

```powershell
py tools/build_recomp.py --data D:/GOG/F-117A --parity-only --work PRIVATE_DIR
py tools/fidelity.py --data D:/GOG/F-117A --diffs-only --work PRIVATE_DIR/fidelity
py tools/input_stress.py --data D:/GOG/F-117A --out PRIVATE_DIR/input-stress
py tools/host_timing.py --out PRIVATE_DIR/host-timing
py tools/dosbox_flight.py --data D:/GOG/F-117A --out PRIVATE_DIR/reference --skip-intro
py tools/flight_state_compare.py --data D:/GOG/F-117A --reference PRIVATE_DIR/reference/flight.csv --out PRIVATE_DIR/comparison --skip-intro
```

`audio_render OPL.LOG OUT.WAV 9000000 END_CLOCK` renders a runner's OPL log.
The sound comparator requires NumPy and ffmpeg. DOSBox observation requires
Windows and pywin32. Failed trials are retained separately, not overwritten
or counted as successful outcomes.

## Still requiring separate evidence

Earned promotions/retirement, weapon hits and AI countermeasures,
supply drops, secret airstrips, harder landing settings, in-flight video,
digitized speech and speaker output against DOSBox remain open. The known
original supply-drop credit bug must be preserved in parity mode.

No WinMM joystick is attached on this machine, so physical calibration,
deadzones and control feel require hardware and a player. MT-32 PCM requires
the user's ROMs and the planned Munt integration; the Roland boot route
alone cannot validate its rendered sound.
