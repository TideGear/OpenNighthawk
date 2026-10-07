# Bugs in the original game

This is the tracker for defects in the original DOS release of *F-117A
Nighthawk Stealth Fighter 2.0* (473.03 with the 473.04 VGAME update, which is
what GOG ships). It is for anyone deciding which bugs to fix later and how.

**Every bug listed here is present in the recompilation, deliberately.** The
recompiled code is the original's machine code translated instruction for
instruction, so it does what the original did, defects included. That is
what 1:1 parity means. Fixes are switchable patches layered on top (see
[How a fix will attach](#how-a-fix-will-attach)), each off unless named with
`--fix ID`, and parity stays the default reference. D1, D2, D4, D5, D11, D12
and D34 have fixes; `--list-fixes` prints what is available.

Most entries were established by the F-117A Reimp project. They are restated
here with the Reimp's evidence; the detail lives in its
`docs/bugs/catalogue.md` (cited as *Reimp catalogue*, with line numbers as of
Reimp commit `cfb8cec9`). Addresses are **always given with their program**:
START, VGAME and END all load at the same segment, so a bare address is
ambiguous. They are image offsets of the unpacked load module unless marked
otherwise.

Status values:

- **Confirmed**: traced to instructions in the shipped binaries.
- **Reported**: known from players, vendors or MicroProse, not yet located.
- **Recomp**: found or confirmed by this project.

## Summary

| ID | Status | Program | Defect | Fix |
|---|---|---|---|---|
| D1 | Confirmed | VGAME | Frame-rate controller oscillates above ~16-18 fps, degrading AI and guidance | `--fix D1` |
| D2 | Confirmed | ASOUND.117 | Digitised speech busy-waits; can hang the game | `--fix D2` |
| D3 | Reported | SETUP | Crash on the sound screen when keypad keys go undetected (GOG/DOSBox) | - |
| D4 | Confirmed | START + .WLD | Secret-airstrip missions disabled in Libya, North Cape, Middle East | `--fix D4` |
| D5 | Confirmed | VGAME | Supply drops never award credit (despite the 473.04 note) | `--fix D5` |
| D6 | Confirmed | world data + VGAME | "Stealth mountains": 20 terrain sectors where nothing can detect you | `--fix D6` |
| D7 | Explained | VGAME (via D8) | Strike training mission does not complete correctly: the bomb only hits from a 80-150 unit release | `--fix D8` |
| D8 | Confirmed | VGAME | Laser-guided bomb pitch clamped at -11.25 degrees | `--fix D8` |
| D10 | Reported | VGAME (shared code) | Mountain-collision routine stack corruption (fixed only on Mac 2.3.0) | - |
| D11 | Reported | START | Fragile saves: ROSTER.FIL truncated in place | `--fix D11` |
| D12 | Confirmed | END | Signed/unsigned mix in the hidden campaign rating tally | `--fix D12` |
| D34 | Confirmed | VGAME | Destroyed-object table has no bound; long missions corrupt state | `--fix D34` |
| D35 | Reported | ASOUND.LOG and others | Fast machines stop after the intro (delay-loop calibration) | - |
| D36 | Reported | RSOUND.117 | Later MT-32/MT-100 models play no engine sound | - |
| Q2-Q13 | Confirmed | VGAME, START | Smaller latent defects (below) | - |
| R1 | Recomp | - | Open questions from this project (below) | - |

## The confirmed and reported defects

### D1. The frame-rate controller is unstable on fast machines

- **What happens.** The flight model steps once per drawn frame and divides
  every per-second rate by S = `[0x368E]`, frames per second. AI and weapon
  guidance run on the 60 Hz tick. The controller clamps S to [4 -
  time_scale, 15]. Above about 16-18 fps the correction carried in
  `[0x43E8]` underflows the unsigned tick count, and S oscillates between 15
  and 3 every four seconds. Missiles miss and enemy AI degrades. Slow
  machines get coarse controls instead.
- **Where.** VGAME `0x441D` (the controller, tail of `0x3ADA`); clamp at
  `0x0D441`; the unsigned divide at `0x4435`; floor at `0x4451`; `imul
  ax,[0x368E],0x3C0` at `0x446C`; S initialised at `0x3B79`. A latent divide
  by S=0 at `0x4435` is never reached because `0x0D441` raises the floor
  first.
- **In this recompilation.** Whether D1 shows depends on the emulated CPU
  speed, exactly as on 1991 hardware. The default (9 million instructions a
  second, GOG DOSBox's `cycles=9000`) is the speed GOG players have. A higher
  `--ips` reproduces the fast-machine behaviour faithfully.
- **Fix options.** Pin the controller's S, or step the flight model on a
  fixed clock (the Reimp's choice). Either is a patch over `0x441D`.
- **Measured here.** Flying `boot_to_flight`'s inputs (input times scaled
  to the machine's speed), GOG's 9 MIPS settles at S 9 and draws 11.6
  frames a second; the mission clock advances 209 in 162 s. At 20 MIPS the
  machine draws 16.8 a second, S sits clamped at 15 and the clock advances
  181: the world runs 13% behind GOG's. That flight does not show the 15/3
  swing.
- **Fix available: `--fix D1`,** a frame limiter. An override at VGAME
  `0x441D`, the controller's entry, which every frame passes once, holds
  a frame that arrives before its slot (time passes and interrupts are
  serviced, as in a HLT loop), so no machine draws faster than GOG's 11.6
  frames a second. The controller is untouched and keeps measuring, so S
  settles where GOG's does. Pinning S instead, without pacing, ran the
  mission clock three times too fast at 40 MIPS (600 against 209). At 9
  MIPS and below the fix never waits and the run is the original's, hash
  for hash. `tools/d1_check.py` flies both machines and requires S 9, the
  mission clock within 2 and frames a second within 0.2 of GOG's at 40
  MIPS, where both engines give identical hashes; the gate plays route
  `d1_fast_machine` (the same flight with the fix) under both engines.
- **Detail.** Reimp catalogue:330-449; Reimp `docs/re/01-timing-architecture.md`.

### D2. AdLib digitised speech can hang the game

- **What happens.** ASOUND `0x2552` hands each spoken line to `0x1DBA`,
  which silences six voices and spins with no timeout until their state
  bytes read zero. Only the sequencer, on the timer interrupt, clears them.
  Playback then paces samples on PIT counter 2 (mode 2, divisor 150, 7955 Hz)
  writing the OPL carrier level once per sample (loop at `0x266F`). The busy
  waits can race and freeze the game.
- **Where.** ASOUND.117 `0x2552`, `0x1DBA`, `0x266F`; Sound Blaster arm's IRQ
  flag `[0x190D]`. The samples are SPEECH.117 (8-bit, 8 kHz).
- **In this recompilation.** Reproduced, including the pause the busy-wait
  causes. The speech itself plays as the card played it: one OPL write per
  sample, rendered at the sample the write's emulated time falls on.
- **Fix options.** A non-blocking speech path (the Reimp's mixer), as a patch
  on `0x2552`.
- **Fix available: `--fix D2`.** An override at ASOUND `0x2552` (AdLib arm,
  `[17F2]` = 0; the Sound Blaster arms are untouched) queues the driver's
  own OPL writes on the machine's schedule and returns at once: 0x25BB's
  channel-0 setup, 0x2649's counter-0 wait, one level write to 43h per
  sample at the PIT counter-2 rate (150 PIT clocks, 7955 Hz), and 0x2593's
  key-off. Nothing masks the timer or spins, so the game and the music keep
  running while the word plays. The music's own channel-0 writes are held
  off meanwhile, and the channel is then restored from the driver's
  register shadow (DS:1A06). Channel 1's key-off at 0x2593 is left out
  because the music is still playing. On the strike route the scheduled
  6,464 writes to 43h equal the original's value for value at the same
  per-sample interval, under both engines. Newer DOSBox releases may avoid
  the hang on their own; this fix removes the wait however the emulator
  times it.
- **Detail.** Reimp catalogue:452-503; Reimp `docs/re/10-sound.md`.

### D3. Keypad keys on the sound-driver screen (GOG, reported)

- **What happens.** GOG support reported SETUP crashing on the sound
  selection screen when DOSBox does not detect keypad keys.
- **Where.** Not traced to an instruction.
- **In this recompilation.** The keyboard is a set-1 scancode stream with a
  real BIOS translation, so keypad keys reach SETUP as they would on a PC.
  Measured on the sound prompt ("Select Sound Driver", recomp engine, 7 Oct
  2026): with NumLock off, keypad 1 or 2 makes SETUP terminate with code 0,
  and F117.COM then terminates to DOS, so the player is returned to DOS
  rather than crashing; with NumLock on, keypad 1 selects the driver as
  main-row 1 does; a main-row x is ignored. The routine that takes the exit
  is not located yet, so no fix is offered; a fix would change what the
  original does with a BIOS extended key, which is a policy decision.
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
- **Fix options.** Widen the mask to `0x027` and correct the two world-data
  bytes at load time (debugcom's fan patch, packaged by damsonn, is the same
  8-byte change). Data patches, no code.
- **Fix available: `--fix D4`,** as the Reimp decided: Libya, the Gulf,
  North Cape and the Middle East. The world half corrects five bytes as DOS
  reads the files (class byte LB.WLD `0x53A`, ME.WLD `0x597`, NC.WLD
  `0x607` 0 to 1; NC.WLD `0x1F1` and `0x201` 08 to 09), each pinned by file
  name, size and the shipped byte. The table half is an override at START's
  entry `0000:8EDC`: START is LZEXE-packed, so its four masks (DGROUP
  `0A95:11DA`, first word of entries 0-3) become 0027h in memory once it has
  unpacked, and the original instruction then runs. Measured: Libya's
  generator offers a supply drop at airstrip target 26 (startup clock
  700000036000000, normal input) with the fix, which the shipped mask cannot;
  VGAME loads North Cape's airstrips with flags 09 instead of 08. Flying an
  airstrip mission outside the Gulf remains to do.
- **Detail.** Reimp catalogue:524-622; Reimp `docs/re/04-mission-generator.md`.

### D5. Supply drops never award credit

- **What happens.** The impact gate admits only weapon types 1E, 1D and 1C.
  A supply drop (type `0x26`) jumps past it, so the credit path at `0x6E4C`
  is dead code. The sibling test at `0x077F2` does list `0x26`. The 473.04
  patch note claims a fix; the 473.04 VGAME still has the defect.
- **Where.** VGAME gate `0x06D1C`, `cmp [bx+0x3C4A],0x26` at `0x6D35`, credit
  `0x6E4C`, skip `0x6EBB`, sibling `0x077F2`, saved type read at `0x70A0`.
- **Fix options.** Add `0x26` to the gate (the Reimp's proposal, awaiting the
  owner's decision there).
- **Fix available: `--fix D5`.** A code override on the gate's fall-through
  `jmp` at VGAME `0000:6D2E` (473.04 file only) continues type 26h to
  `0x6D31`, as the three admitted types do; every other type still jumps to
  `0x6EBB`. The original's own test at `0x6D35` then takes the credit path.
  Replaying the unchanged `cargo.input` with the fix on, the impact earns
  primary flag 4000h and one 8Bh event under both engines (route
  `cargo_d5_fixed`, 149 checkpoints, final `61fc0505fcee1ba3`); with it off
  the run still ends at the original's `20b78dd06d275670`. The recompiler
  gives `0x6D2E` a region of its own, so nothing else is interpreted for
  it.
- **Detail.** Reimp catalogue:626-675.
- **Normal-input reproduction.** `tools/routes/cargo.args` drops the loaded
  supply crate in a generated type-3 Persian Gulf mission. Player slot 11,
  class 26h/weapon 18, impacts at (10500,3951), height -5: octagonal distance
  113 from target 24, below 256, mission time 588 before deadline 1094.
  Primary flag/event remain absent. `cargo_check.py` requires actual impact
  rather than counting consumed stores or expiry. This is engine replay
  evidence. Independent GOG DOSBox supply-mission generation is reproduced
  (exact type-3/target-24/departure-58/home-51 triple at a -275 ms seed
  compensating about 275 ms of autoexec guest time); open-loop replay then
  diverges in flight. A closed-loop pilot (`tools/cargo_pilot.py`, gate route
  `cargo_pilot`) now flies the same mission in our emulator, selects the
  crate and releases it when the aircraft's own state is inside the release
  window, and `cargo_check.py` gives one timely impact in the delivery area
  with no credit. Independent delivery on DOSBox-X remains open.
- **Approach trap.** A shallower trial reached height 1, then pitch became
  zero with slot-owner field zero, and the crate expired instead of crossing
  ground. The shipped instructions at 6C23..6C4C apply the owner pitch floor
  to any weapon at height 1. A steeper normal release skips that height and
  reaches the actual impact handler. No game semantics were changed.

### D6. "Stealth mountains"

- **What happens.** Detection quality is cover x bias x range term, where
  cover = `[0xB1A0 + sector] & 0x0C`. Twenty sectors have cover 0 (12 in
  CE.WLD, 7 in NC.WLD, 1 in CU.WLD), so no sensor can ever detect the player
  there. Whether these are what players called "stealth mountains", and what
  473.04 changed about them, is not established.
- **Measured since (Reimp, 3 October 2026), leaning towards a defect:** the
  cover lookup reads the *player's* position (the same `[0xC0D0]`/`[0xC0DE]`
  the function uses for range), so the effect is the same against every
  sensor from every bearing, unlike terrain masking, which is directional;
  every object placed in the 20 cells has `z = 0` and 5 cells have none, so
  they are flat, not mountains; none overlaps a friendly base or carrier;
  15 of the 20 share their ground pattern with unmasked cells; the manual
  and the three theatres' briefings name no radar gap. Status unchanged:
  the mechanism is measured, the cause is not proven.
- **In this recompilation.** Reproduced exactly: it is the game's own
  arithmetic on the shipped world data, translated instruction for
  instruction.
- **Fix available: `--fix D6`** (off by default; whether the cells are a
  defect is not proven, so the original's behaviour stays the default). An
  override at `0x55EB` replaces the load and the AND: a sector whose cover
  bits are 0 reads as 4, the lowest nonzero cover, and every other sector runs
  the original's two instructions. `tools/d6_check.py` shows it at work: the
  table VGAME holds in Central Europe has the 12 cover-0 sectors the Reimp
  found; with the player's own sector made one, the fixed run logs `[fix D6]
  cover 0 read as 4` and the plain run logs nothing. With the fix off every
  route is unchanged.
- **Where.** VGAME `0x5582` (detection), `0x55B8`, `0x55CB`/`0x55E8` (the
  player's sector), `0x55EB`, `0x5654`.
- **Detail.** Reimp catalogue:679-752, at Reimp commit `9e0716dc` (the
  addendum post-dates the `cfb8cec9` copy in provenance.md).

### D7. Strike training does not complete (reported)

- **What happens.** Players report the training mission not completing
  ("Strike Training vs Strike Mission"); a community workaround exists. Not
  located, and may not exist in this build.
- **Measured here (6 Oct 2026), inconclusive.** Central America / Cold War /
  Ground Strike Training at the default startup clock generates a
  reconnaissance primary (type 1, target 1, camera on station 0), not a
  strike. Two adaptive recon flights (nose and level acquisition) passed
  within 3 map units of target 1 at about 2,500 ft, but the nose-ray
  designation stayed on object 0, so no photo cue or exposure was made and
  the sortie ran out its 30 minutes. That is the pilot's designation, not
  yet evidence about the game: whether the training target can be
  designated and credited is still open.
- **A strike training mission, found (7 Oct).** The start clock seeds the
  mission generator. Of eight clocks 0-7 s past the default, 5 s and 7 s give
  Ground Strike Training a strike primary (type 2, targets 1 and 21), the
  others a reconnaissance. At +5 s the target is designated normally (lock 1
  inside 2,900 map units) and the loaded weapon is a laser-guided bomb
  (weapon 13, class 28, two stores). `strike_pilot.py --front-route
  routes/central_america_strike_training.front --time-us 700000005000000`
  flies it: the bomb is released (one launch event; at the target, or at 2,500
  units with `--release-range 2500`), the target is not damaged, nothing is
  credited and no hit event is logged. The pilot was written for a Maverick
  (a seeker lock), so the first releases did not separate a bad release from
  the game not crediting the training bomb.
- **Result (7 Oct): the mission completes; the bomb has a narrow window.**
  Releasing once designated and within N map units of the target, at about
  177 ft above the ground, the target is destroyed, one hit event is logged
  and the objective is credited (flags 0x4000) for N = 80, 120 and 150, and
  not for N = 40, 190, 220, 250, 400, 600, 900 or 2,500 (a release at the
  target, or at 2,500, left it undamaged; the bomb fell short or long). So the
  training strike cannot be shown to be impossible: it is credited, but only
  from a release within roughly 80-150 map units, which is what D8's clamp
  predicts (the bomb's guidance can steepen its fixed 11.25-degree dive but
  never flatten it, so its reach is a fraction of the release height; the
  Reimp's arithmetic gives about height / 6.4). A player who releases at the
  range the other strike weapons use, or does not hold the designation to
  that point, sees a bomb that never hits and a mission that does not
  complete, which fits the report. Reproduce: `strike_pilot.py --front-route
  routes/central_america_strike_training.front --time-us 700000005000000
  --release-range 120`.
- **Detail.** Reimp catalogue:724-727.

### D8. Laser-guided bomb pitch clamp

- **What happens.** After guidance writes a pitch demand, `0x6C17` forces any
  pitch below -2048 (11.25 degrees down) to -2048: a demand of -500 still
  leaves the bomb at -2048 on every frame measured. HARM guidance was
  cleared: with a lock it guides and hits; released without one nose-down it
  keeps diving (one-sided correction at `0x6BDE`), which may be intended.
- **Where.** VGAME bomb test `0x69E5`, `0x6A16`, clamp `0x6C17`; HARM
  `0x782B`, `0x6BDE`; release refusal past 70 degrees of bank at `0x7670`.
- **Fix available: `--fix D8`.** The compare and the store at `0x6C0F-0x6C17`
  are skipped (an override at `0x6C0F` continues at `0x6C1D`), so the bomb's
  guidance can flatten its dive. Measured on Ground Strike Training (D7,
  start clock +5 s): without the fix a release hits only from 80-150 map units;
  with it releases from 250, 600, 1,200 and 2,500 units all destroy the target
  and earn the objective credit (flag 0x4000, one hit event). Route
  `strike_training_d8` replays the 1,200-unit release with the fix on;
  `strike_training` is the same mission without it. With the fix off every
  other route is unchanged.
- **Detail.** Reimp catalogue:731-808; Reimp `docs/re/21-bomb-guidance.md`.

### D10. Mountain-collision stack corruption (Mac fix, DOS unknown)

- **What happens.** Macintosh 2.3.0's notes: "An error in an assembler
  routine that checks mountains for collisions and distance could corrupt the
  stack." The DOS engine shares the code and never got a fix; no DOS
  equivalent has been located yet.
- **Detail.** Reimp catalogue:830-841.

### D11. Fragile saves

- **What happens.** START writes ROSTER.FIL (802 bytes: a 2-byte header and
  ten 80-byte records) by truncating it in place; an interruption loses the
  roster. GOG once shipped without CLIMBIN.PIC.
- **Where.** START read `0x07218`, write `0x07272`.
- **In this recompilation.** The game's own writes go to a save directory
  overlaid on the install (the install is never written), but each write is
  still the original's truncate-and-write.
- **Fix options.** Make the host's file layer write atomically (temporary
  file, then rename) - invisible to the program.
- **Fix available: `--fix D11`.** A file the program creates (INT 21h
  AH=3Ch) in the save directory is written to `<name>.f117r-tmp` and moved
  over the real file when the handle closes (`MoveFileEx` with replace on
  Windows, `rename` elsewhere). An interruption before the close leaves the
  previous roster as it was. The program sees the same reads and writes:
  the roster_edit route reaches the same final hash `2bb553498fc8b735` and
  saves a byte-identical roster with the fix on, leaving no temporary file.
- **Detail.** Reimp catalogue:845-889.

### D12. END's hidden rating tally mixes signed and unsigned

- **What happens.** The best-rating compare is unsigned (`jae`), so the first
  negative rating always replaces the initial 0, and the running total
  zero-extends a negative rating (`sub dx,dx`): -12 adds as +65524. Nothing
  displays these fields.
- **Where.** END loop `0x003D9`; `0x00441`-`0x0045A`; far pointer
  `DS:0x55DE`.
- **Fix available: `--fix D12`.** Two overrides in END: 0x0443 compares the
  best rating signed, and 0x0450 sign-extends the rating before it joins the
  total (`cwd` for `sub dx,dx`). Both clocks are unchanged. Checked on the
  `career_serge` route (END's tally after a survived mission, record at ES:BX
  020E:1272 on this route): unstaged, fixed and unfixed runs agree exactly
  (best 66 -> 218, total 284, so a positive rating is untouched). With one
  staged write, the best set to 0xFFF0 (-16) before the tally, the original
  keeps 65,520 as the best rating and the fix replaces it with 218; the run
  log names it (`[fix D12] best -16, rating 218: replaced`). The total's
  sign extension only differs for a negative rating, which no route reaches;
  that half is checked by reading the code, not by a run.
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
- **Fix options.** Cap the table at 30 records (the Reimp's fix), as a patch
  at the two writers.
- **Fix available: `--fix D34`,** the Reimp's design: the first 30 records
  stay in the table, later ones go to an extension outside the guest.
  Overrides cover every place a record is reached - the append `0x0F97`,
  the lookup `0x0FBD`, the type write `0x0F7E` and read `0x0D5E` through
  the index the lookup leaves in `[0x950C]` - and VGAME's entry empties the
  extension. Each declines until the table is full, so a fixed flight is the
  original's instruction for instruction until record 31: the strike route
  ends at its committed `197c6b398d6fbb9f` either way. Staged check
  (`tools/d34_check.py --stage [--fix D34]`, the count set to 30 after
  mission setup): unfixed, the Maverick
  hit makes record 31 and its type byte lands on the F7/F8 flag `B838` as
  0x4A, the value the Reimp measured on the original; fixed, the count stays
  30, `B834`-`B838` are untouched, the log shows record 31 kept past the
  table and found by the game's lookup 0.66 million clocks later, the target
  stays destroyed, and both engines end at `13110a036af486ba`.
- **Detail.** Reimp catalogue:1331-1404.

### D35. Fast machines stop after the intro (reported)

- **What happens.** Reported (DOS Days): the game hangs after the intro on
  anything faster than a 486SX-25 unless caches are disabled. Suspects: the
  retrace calibrations in START and PLAYER, and the AdLib logo driver's
  delay-loop calibration (ASOUND.LOG `0x00FE`), which averages sixteen PIT
  deltas across `loop 0x100` and divides `0x5140` by the result. At a high
  enough speed the delta is 0: integer divide by zero, "R6003".
- **In this recompilation.** The speed is a setting, so this is
  reproducible: raise `--ips` far enough and the calibration's measured
  delta reaches zero, as on a fast machine. The same driver's AdLib detection
  (`0x0140`: start OPL timer 1, poll the status port 200 times, expect
  `0xC0`) is why this project models ISA I/O timing at all (see
  [architecture.md](architecture.md#time)).
- **Checked here (5 October 2026):** not reproduced. At 200 million
  instructions a second the game runs through the intro into START, and at
  1,000 million MPS_LOGO and its AdLib calibration finish normally; the
  modelled ISA I/O delays keep the calibration's measured PIT delta above
  zero. No fix is offered for a failure this machine does not show.
- **Detail.** Reimp catalogue:1408-1426.

### D36. Later MT-32s play no engine sound (reported)

- **What happens.** RSOUND.117 depends on first-generation MT-32 sample
  addresses; later models and the MT-100 play no engine sound.
- **Detail.** Reimp catalogue:1430-1439.

## Smaller confirmed defects (Q-series and quirks)

These are reproduced, and some are visible. Each has its evidence in the
Reimp catalogue's Q sections and the cited Reimp documents.

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

## R1. Open questions from this project

- **Alt+Q in flight, answered Y, returns to DOS.** VGAME's quit prompt
  (`0x01F5F` -> `0x02071`, `flight_end(7)` at `0x020CD`) records outcome 7
  in the pilot record, but the program then exits with code 0, and F117.COM
  treats 0 as "leave the game": no debriefing. The reference interpreter
  does the same on the same inputs, so this is the original's behaviour; it
  is very likely intended ("quit") rather than a defect. Flying into the
  ground instead exits with 129 and runs END. Still to confirm against GOG
  DOSBox.
- **Data inside code regions.** F117.COM keeps variables after code that
  discovery walks into; VGAME's thunk table in DGROUP is patched at start;
  MGRAPHIC uses part of MISC.EXE's image as a buffer. Not defects in the
  game: they cost the recompilation speed, never correctness, and are
  tracked in [architecture.md](architecture.md).

## How a fix will attach

The recompilation keeps the original's behaviour as the reference, and every
fix will be optional, so a player can always choose 1:1. The mechanisms, in
order of preference:

1. **Data patches** (D4): bytes corrected as the file is read, through the
   machine's `file_data` hook. The code is untouched. Implemented.
2. **Host-side fixes** (D11): the emulated PC's file layer, invisible to the
   program.
3. **Code overrides** (D1, D2, D5, D34): a hand-written C function registered
   for a module address replaces that address's code under either engine
   (`src/fixes/fixes.c`, mechanism in `src/recomp/recomp.c`). Each is pinned
   to the shipped file it was written for by hash and switched at run time.
   Implemented; D5 is the first.

The Reimp grouped its fixes in tiers (MicroProse's own patches, fan fixes,
its own). Those tiers can carry over unchanged.
