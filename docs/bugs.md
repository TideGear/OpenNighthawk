# Bugs in the original game

This is the tracker for defects in the original DOS release of *F-117A
Nighthawk Stealth Fighter 2.0*, as GOG ships it (with MicroProse's final
473.04 update applied). It is for anyone deciding which bugs to fix and how.

**Confirmed defects in the shipped DOS code are preserved deliberately.** The
recompiled code is the original's machine code translated instruction for
instruction, so it does what the original did, defects included. That is
what 1:1 parity means. Fixes are switchable patches layered on top (see
[How a fix attaches](#how-a-fix-attaches)). Normal play enables all fifteen
by default, including the D1REAL timing and D1PROX balance choices (owner's
decision, 17 October 2026). `--fix none` restores the original; follow it with
`--fix ID` to select a subset. The headless runner starts with fixes off, and
ordinary parity routes explicitly clear them. Fix-specific checks enable
their selected fixes. Replays restore the recorded set; old logs without a
fix header mean the original. This keeps original behavior independently testable.
`--list-fixes` prints the fifteen available fixes: D1, D1TTL, D1SLOT, D1PROX,
D1REAL, D1ACCEL, D2, D3, D4, D5, D6, D8, D11, D12 and D34.

Most entries were first established by the Reimp, the author's earlier
private project (see [provenance.md](provenance.md)). They are restated here
with its evidence; the detail lives in its bug catalogue, cited as *Reimp
catalogue* with line numbers as of Reimp commit `cfb8cec9` unless another
commit is named. Addresses are **always given with their program**: START,
VGAME and END all load at the same segment, so a bare address is ambiguous.
They are image offsets of the unpacked load module unless marked otherwise.

Status values:

- **Confirmed**: traced to instructions in the shipped binaries.
- **Reported**: known from players, vendors or MicroProse, not yet located.
- **Recomp**: found or confirmed by this project.
- **Explained**: a reported symptom traced to another entry's defect.
- **Option**: a deliberate change to the original's timing or balance, not a
  repair of a single faulty instruction.
- **Closed**: investigated historical reports, not established for DOS or
  not reproduced in this machine; evidence and reopening criteria are kept.

## Summary

| ID | Status | Program | Defect | Fix |
|---|---|---|---|---|
| D1 | Confirmed | VGAME | Frame-rate estimate and its clamp at 15 make world speed depend on machine speed; the reported oscillation is not reproduced | `--fix D1` |
| D1TTL | Recomp | VGAME | SA-5 lifetime crosses the signed boundary at S >= 14, suppressing proximity damage | `--fix D1TTL` |
| D1REAL | Option (timing) | VGAME | Original simulation clock varies with CPU speed | `--fix D1REAL` |
| D1SLOT | Recomp | VGAME | Seeker-cleared life underflows to FFFF, keeping the launcher's slot occupied | `--fix D1SLOT` |
| D1ACCEL | Recomp | VGAME | Incoming missile acceleration is per frame rather than per simulated second | `--fix D1ACCEL` |
| D1PROX | Option (balance) | VGAME | Proximity hit distance and movement sampling depend on S | `--fix D1PROX` |
| D2 | Confirmed | ASOUND.117 | Digitised speech busy-waits; can hang the game | `--fix D2` |
| D3 | Confirmed | SETUP | A keypad digit with NumLock off quits to DOS from the sound screen | `--fix D3` |
| D4 | Confirmed | START + .WLD | Secret-airstrip missions disabled in Libya, North Cape, Middle East | `--fix D4` |
| D5 | Confirmed | VGAME | Supply drops never award credit (despite the 473.04 note) | `--fix D5` |
| D6 | Confirmed | world data + VGAME | "Stealth mountains": 20 terrain sectors where nothing can detect you | `--fix D6` |
| D7 | Explained | VGAME (via D8) | Strike training seems not to complete: the bomb only hits from an 80-150 unit release | `--fix D8` |
| D8 | Confirmed | VGAME | Laser-guided bomb pitch clamped at -11.25 degrees | `--fix D8` |
| D10 | Closed: Mac only | Macintosh | Mountain-collision stack corruption; no DOS equivalent established | - |
| D11 | Reported | START | Fragile saves: ROSTER.FIL truncated in place | `--fix D11` |
| D12 | Confirmed | END | Signed/unsigned mix in the hidden campaign rating tally | `--fix D12` |
| D34 | Confirmed | VGAME | Destroyed-object table has no bound; long missions corrupt state | `--fix D34` |
| D35 | Closed: not reproduced | DOS startup | Historical fast-machine startup failure; cause unconfirmed | - |
| D36 | Closed: not reproduced | RSOUND.117 / Roland | Historical missing engine sound; not reproduced on available MT-32 ROMs | - |
| Q2-Q13 | Confirmed | VGAME, START | Smaller latent defects (below) | - |
| R1 | Recomp | - | Open questions from this project (below) | - |

## The confirmed and reported defects

### D1. The frame-rate controller does not keep world time at real time

- **What happens.** The flight model, AI and weapon guidance step once per
  drawn frame, with per-second rates divided by S = `[0x368E]`, the
  controller's frame-rate estimate. S is clamped to [4 - time_scale, 15] and
  can differ from the actual frame rate, so world time depends on machine
  speed, and no tested speed keeps the mission clock at real time. The
  earlier claim that S swings between 15 and 3 above about 16-18 fps came
  from a model that left out the original's frame wait (Reimp
  `src/core/compose.h:166`); it was not reproduced in 199 flights at 3-40
  MIPS and on the 386 profile.
- **Where.** VGAME `0x441D` (the controller, tail of `0x3ADA`); clamp at
  `0x0D441`; the unsigned divide at `0x4435`; floor at `0x4451`; `imul
  ax,[0x368E],0x3C0` at `0x446C`; S initialised at `0x3B79`. A latent divide
  by S=0 at `0x4435` is never reached because `0x0D441` raises the floor
  first.
- **The frame wait.** `[0x43E8]` is the original's frame wait, not only an
  overrun correction. At `0x0409` the frame set-up calls `0x04C7`, which
  waits until the tick byte `[0x264A]` has advanced by `[0x43E8]` ticks.
  `0x0D441` sets `[0x43E8]` = clamp((9 + trunc(-120/S)) >> 1, 1, 4) when the
  measured S is above 15, and 0 otherwise, so the frame period is at least
  N ticks: N = 1 for S 16-23, 2 for 24-35, 3 for 40 and up. S itself is
  clamped to 15 at `0x0D479`. The game tick is 66.75-70.09 Hz, not 60, so
  the wait's own cap is not 15 fps.
- **AI and weapons.** Ground launch routine `0x5046` runs every frame; its
  visibility guard at `0x5050`-`0x5059` skips only the cockpit bar repaint
  (branching to the site loop at `0x50CA`). Aircraft AI also runs every
  frame, but its firing decisions at `0x6275`-`0x629D` are staggered: each
  aircraft is checked once per `16*S` frames, at a phase derived from its
  index. Awareness changes per firing check. `detect_evaluate` (`0x5582`)
  has no direct S input: it uses range, terrain, difficulty, heading aspect,
  airspeed, altitude and realism. `[0xB080]` is the active-air-unit budget
  compared against difficulty `[0x3686]`, not theatre tension (`[0x3688]`).
  Incoming missiles have their own S defects: D1TTL, D1SLOT, D1PROX and
  [D1ACCEL](#d1accel-incoming-missile-acceleration-depends-on-s).
- **In this recompilation.** The original controller and frame wait both
  run. `f117run` and the recorded routes run at 9 million instructions a
  second (GOG DOSBox's `cycles=9000`); the app `f117a` runs at 60 million;
  `--ips` changes either.
- **Measured.** On `boot_to_flight` (the parked jet), GOG's 9 MIPS settles
  at S 9 and draws 11.6 frames a second, so the mission clock runs 1.29
  times real time. From 16 MIPS up S sits at its clamp of 15 and the frame
  rate near 16.7, so the clock runs about 1.11 times real time, about 13%
  behind GOG's; at 14 MIPS the parked jet's clock is within 1% of GOG's.
  Most airborne scenes already draw about 16.7 fps at 9 MIPS. An isolated
  15 fps cap settles at S 12 with a clock of about 1.25: a cap alone does
  not correct it. Figures and methods:
  [speed-sweep.md](speed-sweep.md).
- **Fix available: `--fix D1`,** a frame limiter. An override at VGAME
  `0x441D`, which every frame passes once, holds a frame that arrives before
  its slot (time passes and interrupts are serviced, as in a HLT loop), so
  no machine draws faster than the parked jet does at GOG's speed (11.6
  frames a second). The controller is untouched and keeps measuring, so S
  settles at 9 as GOG's does on the parked jet. At 9 MIPS and below the fix
  never waits and the run is the original's, hash for hash. It holds the
  parked-jet pacing, not GOG's pacing in every scene, and does not make the
  world run at real time: above 9 MIPS fixed flights draw about 11.6 fps
  with S 9 and a mission clock of 1.2882
  ([90-flight sweep](speed-sweep.md#d1-follow-up-9-october-2026)). Pinning S
  instead, without pacing, ran the mission clock three times too fast at 40
  MIPS. `tools/d1_check.py` flies both machines at 40 MIPS and requires the
  fixed one to match GOG's settled S, its mission clock to within 2 and its
  frames a second to within 0.2; the gate plays route `d1_fast_machine` (the
  same flight with the fix) under both engines.
- **Detail.** Reimp catalogue:330-449; Reimp `docs/re/01-timing-architecture.md`.

### D1REAL. Real-time simulation cadence

- **Normal-play cadence:** `--sim-hz 15` selects S15 in both modes, with 15
  updates/s normally and 30 during 2x compression. It rescales the remaining
  deadline or overdue work when compression changes; the older D1 limiter
  yields to D1REAL in this mode. The optional eight-step mode retains its existing
  limiter behavior for replay compatibility. The choice is per machine and
  recorded in input logs; an absent header means eight. Core/headless/API
  still require explicit fix selection. Forty MIPS fails an undamaged
  compressed-flight capacity check; the 60-MIPS default passes the
  [broader timing and mission checks](cadence15-validation.md). Fifteen is
  not a recovered developer target; it is the validated normal-play default.
  Twenty MIPS also failed to sustain the prototype's compressed rate.
- **Legacy eight-step timing: `--fix D1REAL --sim-hz 8`.** Pins the simulation divisor S
  at 8 and admits eight frames per machine second; under the original's 2x
  time compression S becomes 4 at the same frame cadence. The original
  `0x0D441` code still derives every dependent rate. The frame counter at
  `0x4359` waits at event boundaries, so timer interrupts continue normally.
  Fractional time is kept, overruns catch up without losing time, and
  entering the original pause (`0x3A4E`) or quit dialog (`0x2071`) clears
  the deadline. State belongs to each machine and resets at program changes.
  This replaces the original adaptive cadence on purpose. Eight was chosen
  for this project, below the slowest 9 MIPS cohort's average orbit
  frame rate (8.663 fps). Use `--present interp` to display interpolated
  frames at the host refresh rate; at 8 Hz that adds about 125 ms of picture
  delay. A CPU unable to run eight simulation frames a second still falls
  behind. Eight is not a recovered developer target, and interpolated or rerun
  presentation can run at 60 Hz without this timing change. An independent
  original-game 86Box check also finds a clock mismatch, but does not establish
  intent; see [the reference measurement](speed-sweep.md#original-game-clock-on-86box-17-october-2026).
- **Checks.** `tools/d1real_check.py` passes at 9, 20 and 40 MIPS in both
  engines, every case hash matching within each speed: normal flight gives
  240 frames and 30 mission seconds in 30 seconds; 2x compression 80 frames
  and 20 mission seconds in 10; restored rate 80/10; pause 0/0 over 3
  seconds, then resume 24/3; quit-dialog waiting and cancellation also 0/0
  and 24/3. A separate pacer test covers fractional time, event-boundary
  waits, lossless catch-up, program and speed changes and the dialog reset.
  On 17 October the same seven normal-input cases also passed at 20 MIPS
  with all fifteen fixes enabled together, with identical case hashes in both
  engines (69.17 s). Evidence:
  `D:/f117-gate/default-fixes-20261017/all-fixes-clock/verdict.json`.
  Production also supports opt-in `--sim-hz 15`: S15 with 15 normal /30
  compressed updates and phase-correct transitions; the older D1 cap yields
  to this mode. The 40-MIPS budget fails an undamaged compressed-flight
  capacity check. The [60-MIPS candidate's broader checks](cadence15-validation.md)
  pass 960 airborne windows, including 35 min 40 s actually compressed,
  paired clock/transition cases and a successful landing. This is a tested
  project cadence within the original S limit, not a recovered fixed target.
- **Natural flights.** With all five D1-family options, a 24-flight cohort
  holds S 8 and an in-flight mission clock of 1.000 at 9 and 20 MIPS. With
  VGAME's own seed and the pilot's input phase aligned, all 12 mission pairs
  have identical launch counts and 11 also match bursts, damage and orbit
  time: no high-speed enemy weakness. Whole-flight cross-CPU determinism is
  still limited by the original's input polling.
  [Measurements and reproduction](speed-sweep.md#optional-real-time-cadence-10-oct-controlled-airborne-checks-complete).

### D1TTL. Long-lived SA-5 missiles cannot cause proximity damage

- **What happens.** A ground SA-5 (weapon type 2, range 150) starts with
  `150*S*16` frames of life: 31,200 at S=13, 33,600 at S=14 and 36,000 at
  S=15. Its slot stays active while this word is nonzero, but the hit guard
  compares it with `4*S` as a signed word, so at S=15 it cannot
  proximity-hit until 3,233 frames have passed, about 194 seconds at the
  measured frame rate. The Reimp's `frame_weapons.c` has the same guard; its
  starting S of 5 avoids the boundary.
- **Where.** VGAME `0x708D` tests active life against zero; `0x6F15`
  compares life with `4*S`; signed `JLE` at `0x6F19` skips damage.
- **Fix available: `--fix D1TTL`.** Uses an unsigned comparison for valid
  shipped lifetimes (up to 36,000) and keeps the original signed branch above
  that, so seeker-abandoned FFFF missiles (see D1SLOT) stay harmless. It is
  not slot reclamation: a sufficiently old abandoned counter could decay into
  the admitted range; D1SLOT prevents that underflow. Motion, guidance,
  clocks and the suppression during the final `4*S` frames are kept. It adds
  no frame limiter.
- **Check.** `tools/d1ttl_check.py` stages a nearby missile after a normal
  boot at 20 MIPS and tests positive-lifetime controls, the final `4*S`
  window, lifetimes across 32768 and the abandoned-slot sentinel under both
  engines. It is a hit-boundary regression, not a natural combat flight.

### D1SLOT. Cancelled incoming missiles keep their launcher's slot occupied

- **What happens.** Seeker `0x72A5` clears an incoming missile's lifetime
  when the target is well behind its nose, but the common movement path
  still decrements it at `0x6CA6`: zero becomes FFFF. Ground launch `0x54B4`
  and aircraft launch `0x65C3` need their `(unit_index & 7)` slot at zero
  life, so the abandoned missile can block that launcher. This happens at
  low S too, and remains in the Reimp.
- **Fix available: `--fix D1SLOT`.** Keeps a cancelled incoming lifetime at
  zero when the countdown reaches it, with DEC's flags and instruction count;
  positive lifetimes and player-weapon slots run the original DEC. It
  prevents new underflows; it does not reclaim slots already abandoned
  before it was switched on.
- **Check.** `tools/d1slot_check.py` stages S=8 and 15, zero, one and
  positive signed and unsigned lifetime controls, incoming slots 0 and 7 and
  player slot 8: all 15 cases agree across engines, the 11 control hashes are
  unchanged, and cancellation leaves zero with the fix and FFFF without it.
  In 24 matched missions per arm it adds 2.17 (20 MIPS) and 2.38 (9 MIPS)
  bursts per flight, and fixed traces show no abandoned slots. Combat still
  depends on S without the other options; see [speed-sweep.md](speed-sweep.md).

### D1PROX. Incoming proximity distance shrinks with S

- **Where.** VGAME `0x6EF5` divides `speed<<3` by S, then `0x6EF9`/`0x6EFB`
  admit a hit when that unsigned reach exceeds the slant distance. This
  allowance for movement between frames also acts as the hit radius, so a
  near miss that damages the aircraft at low S can miss at high S.
- **Switchable balance change: `--fix D1PROX` (on for normal play).** A continuous swept octagonal
  slant band (overrides at `0x6C50` and `0x6EFB`), calibrated to terminal
  weapon speed x 8/9. The S=9 reference is an explicit balance choice, not a
  recovered warhead radius. S=9 came from the parked-aircraft GOG-speed
  baseline; it is a compatibility reference, not evidence of the developers'
  preferred hardware or lethality. No authoritative numeric radius has been
  recovered from the inspected code or manual. Relative movement includes the player's previous
  and current positions while the same live missile persists across
  consecutive frames; fresh launches and program changes reset it. Guidance,
  movement, frame pacing, player weapons and the lifetime and damage guards
  keep their original code. This is the second version; the first, a floor
  at the S=9 reach, still admitted a wider band below S 9 and could miss
  between samples, and is withdrawn.
- **Check.** `tests/test_weapon_sweep.c` compares 20,000 paths against
  independent half-space clipping and checks strict boundaries, reversal,
  subdivision and wrapped deltas. `tools/d1physics_check.py` flies complete
  frozen-speed, frozen-heading passes at S 4, 8, 9 and 15: all 21 staged
  cases pass in both engines, including slot 7 and an S change in flight,
  with the S9 and player control hashes unchanged. The older five-case
  `tools/d1prox_check.py` also passes. Cohort figures:
  [speed-sweep.md](speed-sweep.md#independent-combat-investigation-10-october-2026).

### D1ACCEL. Incoming missile acceleration depends on S

- **Where.** Ground missiles start at speed 1 (`0x54ED`), air missiles at
  terminal speed (`0x662A`). `0x7209`-`0x722B` adds one speed unit on odd
  frames (`0x7221` tests frame parity, `0x722B` increments) without dividing
  by S, so a normal SA-5 reaches speed 9 after two simulated seconds at S 8
  and 16 at S 15. Both engines reproduce this. It favours high S, so it does
  not by itself explain weaker high-speed combat.
  [Probe](speed-sweep.md#cancelled-slot-repair-and-remaining-distance-defect-10-oct).
- **Optional timing repair: `--fix D1ACCEL`.** Integrates 9/2 speed units
  per simulated second, the original S=9 rate, with exact integer fractions
  for every allowed S; the starting parity keeps the S=9 increment pattern.
  State is per machine and resets on a new program, launch or frame gap.
  Terminal speed is kept; player weapons use their original path.
- **Check.** `tools/d1physics_check.py` (above) measures equal simulated
  time; all 21 cases agree in both engines with unchanged control hashes.
  Natural combat with all five D1 options:
  [speed-sweep.md](speed-sweep.md#optional-real-time-cadence-10-oct-controlled-airborne-checks-complete).

### D2. AdLib digitised speech can hang the game

- **What happens.** ASOUND's speech entry `0x2552` (AdLib arm) calls
  `0x1DBA`, which silences six voices and spins with no timeout until their
  state bytes read zero; only the sequencer, on the timer interrupt, clears
  them. `0x2577` then programs channel 0 (`0x25BB`) and PIT counter 2 (mode
  2, divisor 150, 7955 Hz) and disables interrupts, and `0x2661` writes the
  OPL carrier level once per sample; `0x2593` keys off and restores the
  interrupt mask. The busy-waits can race and freeze the game.
- **Where.** ASOUND.117 `0x2552`, `0x1DBA`, `0x2577`, `0x2661`, `0x2593`; the
  Sound Blaster arms' IRQ flag `[0x190D]`. The samples are SPEECH.117 (8-bit,
  8 kHz).
- **In this recompilation.** Reproduced, including the pause the busy-wait
  causes. The speech plays as the card played it: one OPL write per sample,
  rendered at the sample its emulated time falls on.
- **Fix available: `--fix D2`.** A non-blocking speech path (the Reimp's
  approach), an override at ASOUND `0x2552` for the AdLib arm only
  (`[17F2]` = 0). It queues the driver's own OPL writes on the machine's
  schedule and returns at once: the channel-0 setup, `0x2649`'s counter-0
  wait, one level write to 43h per sample at the PIT counter-2 rate, and the
  channel-0 key-off. Nothing masks the timer or spins, so the game and the
  music keep running while the word plays. The music's own channel-0 writes
  are held off meanwhile, and the channel is then restored from the
  driver's register shadow (DS:1A06); channel 1's key-off is left out
  because the music is still playing. On the strike route the 6,464
  scheduled writes to 43h equal the original's, value for value, at the same
  per-sample interval, under both engines.
- **Detail.** Reimp catalogue:452-503; Reimp `docs/re/10-sound.md`.

### D3. Keypad keys on the sound-driver screen quit to DOS

- **What happens.** GOG support reported SETUP crashing on the sound
  selection screen when DOSBox does not detect keypad keys. The cause: SETUP
  reads the answer through MISC.EXE's `mov ah,1; int 21h; retf` (DOS read
  with echo), which leaves AH 1, and keeps the whole word
  (`0x00422`-`0x00427`). At `0x0042D` a word of 100h calls `exit(0)`
  (`0x00434`-`0x00437`): it is the 0 that DOS returns first for an extended
  key. With NumLock off a keypad digit is an extended key (keypad 1 is End,
  4F00h), so SETUP ends and F117.COM returns to DOS; a main-row 1 reads as
  0131h and selects the driver. DOSBox may also report keypad keys without
  NumLock, which is the GOG report.
- **In this recompilation.** Keypad keys reach SETUP as they would on a PC
  (set-1 scancodes, a real BIOS translation, and DOS's console holding an
  extended key's scan code for the next read): keypad 1 with NumLock off
  quits 355 instructions after the key; with NumLock on it selects the
  driver as main-row 1 does.
- **Fix available: `--fix D3`.** At `0x0042D`, a word of 100h whose held scan
  code is a keypad digit's (47h-52h) is replaced by the word the main-row
  digit reads as, and the original compare runs on it; the scan code is
  consumed. Keypad 1 then finishes SETUP at the same clock and with the same
  exit code (36) as main-row 1, on both engines; any other extended key still
  quits.
- **Detail.** Reimp catalogue:507-520.

### D4. Secret-airstrip missions are disabled outside the Gulf

- **What happens.** Three independent blocks:
  1. START's mission table entries 0-3 (codes 0x2F landing, 0x3A supply)
     carry theatre mask `0x002`, Persian Gulf only.
  2. The airstrip object's class byte is 0 in LB.WLD (0x154), ME.WLD (0x151)
     and NC.WLD (0x149); PG.WLD (0x150) has 1.
  3. North Cape's airstrip flag byte is `08`, not `09`, so the landing-credit
     bit is missing (`nc.wld` offsets 0x1F1/0x201).
  The rule that landing missions need an opponent above Green is design, not
  a defect.
- **Where.** START DS:`11DA` (82 entries of 12 bytes); the `.WLD` object-class
  table; NC.WLD flags.
- **Fix available: `--fix D4`,** as the Reimp decided: Libya, the Gulf,
  North Cape and the Middle East (debugcom's fan patch, packaged by damsonn,
  is the same 8-byte change). The world half corrects five bytes as DOS reads
  the files (class byte LB.WLD `0x53A`, ME.WLD `0x597`, NC.WLD `0x607` 0 to
  1; NC.WLD `0x1F1` and `0x201` 08 to 09), each pinned by file name, size and
  the shipped byte. The table half is an override at START's entry
  `0000:8EDC`: START is LZEXE-packed, so its four masks (DGROUP `0A95:11DA`,
  first word of entries 0-3) become 0027h in memory once it has unpacked, and
  the original instruction then runs. Measured: with the fix, Libya's
  generator offers a supply drop at airstrip target 26 (start clock
  700000036000000, normal input), which the shipped mask cannot; VGAME loads
  North Cape's airstrips with flags 09 instead of 08. An airstrip mission
  outside the Gulf has not yet been flown with the fix.
- **Detail.** Reimp catalogue:524-622; Reimp `docs/re/04-mission-generator.md`.

### D5. Supply drops never award credit

- **What happens.** The impact gate admits only weapon types 1E, 1D and 1C.
  A supply drop (type `0x26`) jumps past it, so the credit path at `0x6E4C`
  is dead code. The sibling test at `0x077F2` does list `0x26`. The 473.04
  patch note claims a fix; the 473.04 VGAME still has the defect.
- **Where.** VGAME gate `0x06D1C`, `cmp [bx+0x3C4A],0x26` at `0x6D35`, credit
  `0x6E4C`, skip `0x6EBB`, sibling `0x077F2`, saved type read at `0x70A0`.
- **Reproduced with normal input.** Route `cargo` drops the loaded supply
  crate in a generated type-3 Persian Gulf mission: player slot 11, class
  26h/weapon 18, impact at (10500,3951), height -5, octagonal distance 113
  from target 24 (below 256), mission time 588 before deadline 1094; no
  primary flag or event follows. `cargo_check.py` requires an actual impact,
  not consumed stores or expiry. The closed-loop pilot `tools/cargo_pilot.py`
  (gate route `cargo_pilot`) gives one timely impact in the delivery area
  and no credit in this machine, on DOSBox-X (`tools/dosbox_cargo_pilot.py`)
  and on 86Box (`tools/b86_cargo_pilot.py`). GOG DOSBox generates the same
  mission (seed set 275 ms early to offset its autoexec), but open-loop
  replay diverges there in flight.
- **Fix available: `--fix D5`.** Adds `0x26` to the gate (the Reimp's
  proposal): an override on the gate's fall-through `jmp` at VGAME
  `0000:6D2E` continues type 26h to `0x6D31`, as the three admitted types do;
  every other type still jumps to `0x6EBB`, and the original's own test at
  `0x6D35` then takes the credit path. Replaying the unchanged `cargo.input`
  with the fix (route `cargo_d5_fixed`), the impact earns primary flag 4000h
  and one 8Bh event under both engines; with it off the run is the
  original's.
- **Approach trap (not a defect fixed here).** The shipped code at
  `6C23`-`6C4C` applies the owner pitch floor (slot field +22, zero for the
  crate) to any weapon at height 1, so a crate whose height lands on exactly
  1 gets pitch 0, stays there and expires without reaching the impact
  handler. A near-level release falls 2-3 units a frame on a 9 MIPS PC and
  one unit a frame on 86Box's 386DX/33 (S 4), where it traps almost always.
  `cargo_pilot.py` therefore releases in a steep dive (aircraft pitch -1200
  or lower, 150 or more above the ground, 60-300 map units short of the
  target), which falls several units a frame on every machine.
- **Detail.** Reimp catalogue:626-675.

### D6. "Stealth mountains"

- **What happens.** Detection quality is cover x bias x range term, where
  cover = `[0xB1A0 + sector] & 0x0C`. Twenty sectors have cover 0 (12 in
  CE.WLD, 7 in NC.WLD, 1 in CU.WLD), so no sensor can ever detect the player
  there. Whether these are what players called "stealth mountains", and what
  473.04 changed about them, is not established.
- **Evidence leaning towards a defect** (Reimp, 3 October 2026): the cover
  lookup reads the *player's* position (the same `[0xC0D0]`/`[0xC0DE]` the
  function uses for range), so the effect is the same against every sensor
  from every bearing, unlike terrain masking, which is directional; every
  object placed in the 20 cells has `z = 0` and 5 cells have none, so they
  are flat, not mountains; none overlaps a friendly base or carrier; 15 of
  the 20 share their ground pattern with unmasked cells; the manual and the
  three theatres' briefings name no radar gap. The mechanism is measured,
  the cause is not proven.
- **Where.** VGAME `0x5582` (detection), `0x55B8`, `0x55CB`/`0x55E8` (the
  player's sector), `0x55EB`, `0x5654`.
- **In this recompilation.** Reproduced exactly: it is the game's own
  arithmetic on the shipped world data.
- **Fix available: `--fix D6`** (whether the cells are a defect is not
  proven, so the original stays the default). An override at `0x55EB`
  replaces the load and the AND: a sector whose cover bits are 0 reads as 4,
  the lowest nonzero cover; every other sector runs the original's two
  instructions. `tools/d6_check.py` finds Central Europe's 12 cover-0
  sectors in the table VGAME holds, makes the player's own sector one, and
  requires the fixed run to log `[fix D6] cover 0 read as 4` and the plain
  run to log nothing.
- **Detail.** Reimp catalogue:679-752, at Reimp commit `9e0716dc` (the
  3 October addendum is later than `cfb8cec9`).

### D7. Strike training does not complete (reported)

- **Fixed by:** D8.
- **What happens.** Players report the training mission not completing
  ("Strike Training vs Strike Mission"); a community workaround exists.
- **Finding the mission.** The start clock seeds the mission generator. At
  the default clock, Central America / Cold War / Ground Strike Training
  generates a reconnaissance primary, not a strike; of start clocks 0-7 s
  past the default, +5 s and +7 s give a strike primary (type 2, targets 1
  and 21). At +5 s the target is designated normally and the loaded weapon
  is a laser-guided bomb (weapon 13, class 28, two stores).
- **Finding.** The mission completes, but the bomb has a narrow window.
  Released once designated, at about 177 ft above the ground, it destroys
  the target and earns the objective credit (flags 0x4000, one hit event)
  from 80, 120 and 150 map units, and not from 40 or from 190-2,500 (short or
  long). This is what D8's clamp predicts: the bomb's guidance can steepen
  its fixed 11.25-degree dive but never flatten it, so its reach is a
  fraction of the release height (about height / 6.4 by the Reimp's
  arithmetic). A player who releases at the range other strike weapons use,
  or loses the designation before that point, sees a bomb that never hits
  and a mission that does not complete, which fits the report. Reproduce:
  `strike_pilot.py --front-route routes/central_america_strike_training.front
  --time-us 700000005000000 --release-range 120`.
- **Detail.** Reimp catalogue:724-727.

### D8. Laser-guided bomb pitch clamp

- **What happens.** After guidance writes a pitch demand, `0x6C17` forces any
  pitch shallower than -2048 (11.25 degrees down) to -2048: a demand of -500
  still leaves the bomb at -2048 on every frame measured. HARM guidance was
  cleared: with a lock it guides and hits; released nose-down without one it
  keeps diving (one-sided correction at `0x6BDE`), which may be intended.
- **Where.** VGAME bomb test `0x69E5`, `0x6A16`, clamp `0x6C17`; HARM
  `0x782B`, `0x6BDE`; release refused past 70 degrees of bank at `0x7670`.
- **Fix available: `--fix D8`.** The compare and the store at
  `0x6C0F`-`0x6C17` are skipped (an override at `0x6C0F` continues at
  `0x6C1D`), so the guidance can flatten the dive. On Ground Strike Training
  (D7, start clock +5 s), releases from 250, 600, 1,200 and 2,500 units then
  all destroy the target and earn the credit. Route `strike_training_d8`
  replays the 1,200-unit release with the fix; `strike_training` is the same
  mission without it.
- **Detail.** Reimp catalogue:731-808; Reimp `docs/re/21-bomb-guidance.md`.

### D10. Mountain-collision stack corruption (Macintosh)

- **Disposition.** Closed: not established for DOS. MicroProse's Macintosh
  release notes for version 2.3.0 (22 July 1994), recovered from
  `F117a_Update_2.3.1.sit`, say: "An error in an assembler routine that
  checks mountains for collisions and distance could corrupt the stack."
  They establish a Macintosh defect. The earlier assertion that DOS shared it
  was an inference, and is withdrawn.
- **Searched for DOS.** The DOS contact test is in the model renderer: model
  draws set the ground-contact flag `[0xC6B2]` at `0x1223F`
  (`model_fill_stream`, `120A:00CF`, hand-written assembly), which ends the
  flight through `[0x9F96]`. `tools/stack_balance.py` walks every path of
  all 490 VGAME census entries, counting callee-popped arguments: no game
  routine returns with the stack moved. The five it flags are C runtime code
  that moves SP by design (start-up, the exit family's shared tail, the
  stack probe, a file write's error tail, an exit call that never returns);
  in the renderer's segment the only uneven depth is the depth-order walk's
  own work stack (`120A:0E5B`, balanced by its count in BP). A stray write
  into the stack, as opposed to a push/pop imbalance, is not excluded, and
  successful collision routes cannot show that every collision is safe.
- **Reopen when:** a reproducible DOS collision corrupts the stack, or a
  specific DOS instruction path demonstrates an invalid stack write.
- **Detail.** Reimp catalogue:830-841. Evidence and commands:
  [legacy-reports.md](legacy-reports.md).

### D11. Fragile saves

- **What happens.** START writes ROSTER.FIL (802 bytes: a 2-byte header and
  ten 80-byte records) by truncating it in place; an interruption loses the
  roster. GOG once shipped without CLIMBIN.PIC.
- **Where.** START read `0x07218`, write `0x07272`.
- **In this recompilation.** The game's writes go to a save directory
  overlaid on the install (the install is never written), but each write is
  still the original's truncate-and-write.
- **Fix available: `--fix D11`.** The host's file layer writes atomically,
  invisibly to the program: a file the program creates (INT 21h AH=3Ch) in
  the save directory is written to `<name>.f117r-tmp` and moved over the
  real file when the handle closes (`MoveFileEx` with replace on Windows,
  `rename` elsewhere), so an interruption before the close leaves the
  previous roster as it was. The program sees the same reads and writes: the
  `roster_edit` route reaches the same final state and saves a
  byte-identical roster with the fix on, leaving no temporary file.
- **Detail.** Reimp catalogue:845-889.

### D12. END's hidden rating tally mixes signed and unsigned

- **What happens.** The best-rating compare is unsigned (`jae`), so the first
  negative rating always replaces the initial 0, and the running total
  zero-extends a negative rating (`sub dx,dx`): -12 adds as +65524. Nothing
  displays these fields.
- **Where.** END loop `0x003D9`; `0x00441`-`0x0045A`; far pointer
  `DS:0x55DE`.
- **Fix available: `--fix D12`.** Two overrides in END: `0x0443` compares
  the best rating signed, and `0x0450` sign-extends the rating before it
  joins the total (`cwd` for `sub dx,dx`). Both clocks are unchanged.
  Checked on the `career_serge` route: unstaged, fixed and unfixed runs agree
  exactly (best 66 -> 218, total 284; a positive rating is untouched). With
  the best staged to 0xFFF0 (-16) before the tally, the original keeps
  65,520 as the best rating and the fix replaces it with 218, logging
  `[fix D12] best -16, rating 218: replaced`. No route reaches a negative
  rating, so the total's sign extension is checked by reading the code, not
  by a run.
- **Detail.** Reimp catalogue:893-940.

### D34. The destroyed-object table has no end

- **What happens.** Every distinct destroyed or replaced object appends a
  5-byte record, without a limit. Record 31 overwrites the target camera's
  depth and the F7/F8 navigation flag `0xB838`; from record 134 the
  world-lookup result `0xBA3A`-`0xBA51` is overwritten; from 139 the event log
  `0xBA56+`. Past 256 records a byte-compare leak (Q3) makes every miss read
  as a hit. The likely cause of "phantom planes, invisible mountains, and
  finally a crash" on long missions; the crash itself is not measured.
- **Where.** VGAME `0x00F3D`; table `[0xB79E]`, count `[0x9932]`; writers
  `0x07549`, `0x043EC`; readers `0x006E3`, `0x00D43`, `0x00FBD`.
- **Fix available: `--fix D34`,** the Reimp's design: the first 30 records
  stay in the table, later ones go to an extension outside the guest.
  Overrides cover every place a record is reached - the append `0x0F97`, the
  lookup `0x0FBD`, and the type write `0x0F7E` and read `0x0D5E` through the
  index the lookup leaves in `[0x950C]` - and VGAME's entry (`0xE6BE`)
  empties the extension. Each declines until the table is full, so a fixed
  flight is the original's, instruction for instruction, until record 31:
  the `strike` route ends in the same final state either way. Staged check
  (`tools/d34_check.py --stage [--fix D34]`, the count set to 30 after
  mission setup): unfixed, the Maverick hit makes record 31 and its type
  byte lands on the F7/F8 flag `B838` as 0x4A, the value the Reimp measured
  on the original; fixed, the count stays 30, `B834`-`B838` are untouched,
  record 31 is kept past the table and found again by the game's lookup, the
  target stays destroyed, and both engines agree.
- **Detail.** Reimp catalogue:1331-1404.

### D35. Fast machines stop after the intro (reported)

- **Disposition.** Closed: not reproduced in this machine. The community
  report remains historical evidence, not a demonstrated MicroProse diagnosis.
- **What happens.** Reported (DOS Days): the game hangs after the intro on
  anything faster than a 486SX-25 unless caches are disabled. Suspects: the
  retrace calibrations in START and PLAYER, and the AdLib logo driver's
  delay-loop calibration (ASOUND.LOG `0x00FE`), which averages sixteen PIT
  deltas across `loop 0x100` and divides `0x5140` by the result: a delta of
  0 would be an integer divide by zero, "R6003".
- **In this recompilation.** The modelled ISA I/O delays keep the
  calibration's measured PIT delta above zero, so raising `--ips` does not
  by itself reproduce the fault. The same driver's AdLib detection
  (`0x0140`: start OPL timer 1, poll the status port 200 times, expect
  `0xC0`) is why this project models ISA I/O timing at all (see
  [architecture.md](architecture.md#time)).
- **Checked:** not reproduced. All 18 fresh startup probes reach START at
  200, 1,000 and 5,000 MIPS, under the interpreter and the recompiled code,
  with speaker, AdLib and Roland. They skip the intros with normal Enter
  inputs, so they verify driver initialisation and the move to START, not
  complete movie playback at every speed. No speculative calibration fix or
  speed cap is added.
- **Reopen when:** an identified DOS version and machine configuration
  reproduce the failure, with a trace identifying the failed wait or
  calibration. A reference-hardware reproduction may expose a machine-model
  gap even if our engines agree.
- **Detail.** Reimp catalogue:1408-1426. Evidence and commands:
  [legacy-reports.md](legacy-reports.md).

### D36. Later MT-32s play no engine sound (reported)

- **Disposition.** Closed: not reproduced on available MT-32 ROMs.
  [DOS Days](https://www.dosdays.co.uk/topics/Games/game_f117.php) reports
  silence while flying on newer MT-32s and the MT-100. Its moved-sample-address
  explanation is unconfirmed for these ROMs; it is not an established defect
  in RSOUND.117. Physical units and MT-100/CM-32L PCM ROMs were not tested.
- **How the engine sound is made.** VGAME sends sound request 0x17 every
  frame with the airspeed in knots (`[0xB07E]` / 27, `0x02802`). RSOUND's
  entry `0x124C` turns it into channel 8's pitch bend (`0x1F40` + 5 x knots)
  and volume (`0x39` + knots / 16); its timer tick (`0x0BAD`) starts the
  engine sequence on that channel once the engine flag is set and the speed
  is above zero: program 7, which the start-up upload makes memory timbre 7
  "Jet Engine" (three PCM partials on waves 24, 7 and 24, one synth partial;
  a 24-semitone bend range), note 23 held. A jet standing braked sends the
  updates but never starts the note.
- **Checked with Munt:** not reproduced. Control ROMs 2.04 and 2.07 have the
  same PCM wave map as 1.07 (all 128 entries equal; waves 24 and 7 loop in
  both). The landing flight's engine renders steadily at about 1,155 RMS on
  1.05 and 1.07, and unsteadily at 750-970 RMS on 2.04 and 2.07: about 3.4 dB
  quieter, not silent. A staged replay holding the first engine note for 20
  seconds produces sound on all seven available control versions (levels in
  [legacy-reports.md](legacy-reports.md)). Munt models the firmware's tables,
  not all of its code, so a real 2.0x unit can still differ; no fix or
  speculative timbre replacement is offered.
- **Reopen when:** a flying recording on an identified physical MT-32/MT-100
  or matching independent emulator reproduces silence with its ROM identities
  and MIDI stream, or an incompatible mapping is demonstrated on an untested
  model.
- **Detail.** Reimp catalogue:1430-1439. Evidence and commands:
  [legacy-reports.md](legacy-reports.md).

## Smaller confirmed defects (Q-series and quirks)

These are reproduced, and some are visible. Each has its evidence in the
Reimp catalogue's Q sections and the cited Reimp documents. None has a fix.

| ID | Program | Defect | Where |
|---|---|---|---|
| Q2 | VGAME | Pitch-ladder labels: x tested on the rung index, y on the vertex index; rungs 11-13 mix near and far sides | `0x10E9F`, `0x10EBA` |
| Q3 | VGAME | Override lookup returns in AL with AH stale: from 256 overrides, "not found" reads as found | `0x0FBD`; caller `0x06EC` |
| Q4 | VGAME | Stick axis scaling divides by the calibrated span with no guard; a zero span would raise INT 0 | `0x11289`, `0x112B6`, `0x112E3` |
| Q5 | VGAME | Depth-sort tree treats a negative child[0] as a leaf, a negative child[1] as a node | `0x12F6A`, `0x12F7A` |
| Q7 | VGAME | Zoomed frustum reaches the shared tail without reloading SI/DI: its "range" is not a distance (LOD and far clip) | `0x0E3BF`, tail `0x0E513` |
| Q8 | VGAME | Model visibility bound adds z twice (`adc`): `2z + r` instead of `z + r` | `0x12352`, `0x12367` |
| Q9 | VGAME | Padlock cameras' per-kind zooms are overwritten before use (dead stores) | `0x032CF`, `0x03304`, `0x03341` |
| Q10 | VGAME | The fixed camera never writes the heading `[0xE478]` | `0x03522`-`0x0355E` |
| Q11 | VGAME | Object range cull: 16-bit `abs` of a 32-bit value; objects at 0x7FFF, 0x17FFF... culled, far ones pass | `0x0BC00`, `abs` `0x0EE0C` |
| Q12 | VGAME | Cannon ground eligibility called with no argument: the "target" is whatever SI the HUD left | `0x0A153` -> `0x0792E`; also `0x0707B` |
| Q13a | VGAME | A sensorless unit's range and bearing are read uninitialised from the stack | `0x063F2`, `0x05588` |
| Q13b | START | Written orders test the departure record for the secondary target's name fallback | `0x022E7`, `0x022F4` |
| D17 | START | Date roll: the month table is read past its end ('P' of "Photograph"): "Jan 40, 1987" | `0x025D9`, `0x02621` |
| D18 | START | NULL passed to `sprintf %s` prints "(null)" | `0x02651`, `0x0267B` |
| D44 | ASOUND.117 | Noise take-over's second arm tests the first generator's count (copy-paste) | `0x202E`, `0x2089` |
| - | ASOUND.117 | A "level changed" local is uninitialised and leaks between voices | `0x2EC0` |
| - | RSOUND.117 | Volume and pan are sent unmasked: music fades send values of 80h and more (pan from FFh down in steps of 4, volume 82h-89h; 31 in one strike session), which a real MT-32 reads as status bytes | volume `0x0490`, pan `0x04E1` |
| D61 | VGAME | A ladder rung past the 23-entry label table reads bytes outside it | MGRAPHIC `047E:064F` |
| D63 | MGRAPHIC | Right clip is an unsigned compare: a string at negative x is dropped whole | entry 6, `0x031D` |
| D75 | VGAME | Stale `[bp-0x30]`/`[bp-0x2E]` read when the impact point is skipped past 0x2000 of roll | `0x0AEB5`, `0x0AD81` |
| D97 | START | The name-typing loop draws one more random number than the name is long (shifts the RNG stream) | `0x06F17` |
| D98 | START | Box 14 is never swapped back | `0x039D5` |
| D101 | START | Entry 42 reads a stale slot's y past the row table and writes off-screen | `0x05CAA` |
| D103 | START/VGAME | The secondary objective block persists between missions | `0x04B73`, `0x047C0` |
| - | START | Roster fix-up masks record 0's +0x42 ten times instead of each record once | `0x06D07` |
| - | START | Armoury shortage tests one index and writes another, so fewer weapons are refused than drawn | `0x00E23`, `0x00E6B`, `0x00E74` |
| - | VGAME | Overdue objective string copied, then the old copy displayed | `0x09690`-`0x0969F` |
| - | END | END's six-in-seven tick skip omits VGAME's `xor ax,ax`; four-interrupt mode from its seventh tick | END timer |
| - | VGAME | Terrain lookup above level 4 falls off the decrement chain and returns `level - 4` | `0x0969` |
| - | START | A briefing popup can stick on the map | `0x01C2F`, restore `0x03588` |
| - | START | The Enemy Troops overlay shows one marker: it stores X/Y into its own loop counters | `0x01AB6`, `0x01AF7` |

Also present: GOG's START.EXE has the aircraft-identification quiz (the copy
protection) disabled: bytes `0x0001D`-`0x0001F` are `90 90 90` where retail
has `E8 E3 06` (call `0x0703`). This recompilation translates whichever
START.EXE the user owns, so a retail copy asks the quiz and GOG's does not.
The option **`--fix QUIZ`** restores it on GOG's copy (an override at START `0x001D`
pushes `0x0020` and runs the quiz routine at `0x0703`, as the retail `call` does).
It is an option, not a repair: **off by default, and `--fix all` and normal play never
switch it on**; it is named explicitly, and `--fix none` clears it. The routes
`quiz_right` and `quiz_wrong` run it (docs/progress.md: the quiz code is in the
Phase 2 denominator, not in `docs/census-dead.tsv`).

## R1. Open questions from this project

- **Alt+Q in flight, answered Y, returns to DOS.** VGAME's quit prompt
  (`0x01F5F` -> `0x02071`, `flight_end(7)` at `0x020CD`) records outcome 7
  in the pilot record, but the program then exits with code 0, and F117.COM
  treats 0 as "leave the game": no debriefing. The interpreter does the same
  on the same inputs, so this is the original's behaviour and very likely
  intended ("quit") rather than a defect. Flying into the ground instead
  exits with 129 and runs END. Not yet confirmed on GOG DOSBox.
- **Data inside code regions.** F117.COM keeps variables after code that
  discovery walks into; VGAME's thunk table in DGROUP is patched at start;
  MGRAPHIC uses part of MISC.EXE's image as a buffer. Not defects in the
  game: they cost the recompilation speed, never correctness, and are
  tracked in [architecture.md](architecture.md).

## How a fix attaches

The recompilation keeps the original's behaviour as the reference, and every
fix is optional, so a player can always choose 1:1. All three mechanisms are
implemented in `src/fixes/fixes.c`:

1. **Data patches** (D4's world half): bytes corrected as the file is read,
   through the machine's `file_data` hook, each pinned by file name, size
   and the byte it replaces. The code is untouched.
2. **Host-side fixes** (D11): the emulated PC's file layer, invisible to the
   program.
3. **Code overrides** (the D1 family, D2, D3, D4's table half, D5, D6, D8,
   D12, D34): a hand-written C function registered for a module address
   replaces that address's code under either engine (mechanism in
   `src/recomp/recomp.c`). Each is pinned by hash to the shipped file it was
   written for and switched at run time. The recompiler reads the override
   table (`recompiler/recomp.py`) and gives each overridden address a region
   of its own, so nothing else is interpreted because of it.

With a fix off, its override declines and the original code runs.
