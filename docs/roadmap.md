# Roadmap

What is done, what is in progress, and what is left. The game's own bugs
are tracked separately in [bugs.md](bugs.md); how parity is checked is in
[architecture.md](architecture.md). Updated with every piece of work.

The order of the phases is the project's: **1:1 parity first**, then
understood (named) code, then fixes and enhancements - each switchable,
never instead of parity.

## Phase 1 - recompiled, 1:1 with the original

### Done

- All 17 code files and the LZEXE decompressor translated: 89,216
  instructions, 96.2% of the code-area bytes; the rest is strings, tables
  and variables (`tools/census.py`). On every route and the longest random
  flight the recompiled build interprets no game instruction.
- Every translated instruction held to the interpreter from random states
  (`tests/insn_lockstep.c`, 5.7 million comparisons, 0 differences); the
  translator held to 8088 and 80286 silicon vectors.
- Six scripted routes identical between the interpreter and the recompiled
  code: boot to flight; a full sortie through the debriefing; the speaker
  and Roland drivers; Korea (strike, carrier start); Vietnam (air-to-air,
  runway). The routes execute 52.3% of the code area (`tools/exercised.py`).
- Seeded random flights (`tools/random_flights.py`): every session
  identical at every hash; 30.5 minutes of flight in the 16-session batch.
- Planted defects caught by the lockstep (`tools/mutation_check.py
  --lockstep`): 12 of 12, 72 of 72, 60 of 60 - 144 of 144, of all four
  kinds (removed instruction, flipped CF, ZF, AX bit), most at
  instructions no route runs.
- The emulated PC held to GOG's DOSBox 0.74-2.1 (`tools/fidelity.py`): DOS
  memory, PSP, environment, EXEC and terminate, every DOS/BIOS service the
  game uses and its cost in time, BIOS data, VGA registers, devices,
  clocks. All comparable answers agree. DOSBox-X as a second reference.
- The game's music against GOG's DOSBox (`tools/dosbox_compare.py`): the
  logo and intro, 22,687 AdLib writes over 100.7 s, identical, timing
  within 36 ms.
- The game version verified: GOG's installer (build 28044) with the 473.04
  update; Steam's release byte-identical (`tools/verify_install.py`).

### In progress

- (none)

### Left

- [ ] **The picture against DOSBox, frame by frame.** DOSBox's video capture
      (ZMBV) of the hands-off intro, decoded with ffmpeg, against `f117run
      --shots`; the music comparison's driver already starts DOSBox and its
      captures.
- [ ] **Roland through Munt** (libmt32emu, LGPL-2.1+): the MT-32 music in
      the game itself, and its output rendered and checked automatically.
      Users supply their own MT-32 ROMs.
- [ ] **A landing route:** fly back and land. No route or random flight has
      landed yet.
- [ ] **The other theatres and mission types:** Central America, North Cape,
      Central Europe, Libya, the Middle East, the Persian Gulf (Korea,
      Vietnam and the default theatre are flown).
- [ ] **The rest of the front end:** creating and retiring pilots, the CO's
      office, maintenance, awards, saving and loading a roster.
- [ ] **A person playing it:** controls, joystick and mouse, saves, the feel.
      Sessions record themselves for replay.
- [ ] **Timing details still DOSBox's own:** its stub code at its own
      addresses (vectors F000:xxxx), its per-millisecond slicing, its 386
      against this machine's 286 (flag bits 12-14).
- [ ] **A sound mismatch to explain or accept:** the OPL emulator is Nuked
      OPL3, DOSBox's is its own; the register stream is identical, the
      synthesis is not compared.

## Phase 2 - understood code

- [ ] Name the translated routines and data, with explanations, drawing on
      the Reimp's mapping; matched functions replacing translations one at a
      time, each held to the same parity checks.

## Phase 3 - fixes and enhancements (switchable)

- [ ] Code overrides: hand-written C registered for a module address,
      replacing that address's translation - how the fixes attach.
- [ ] Fixes for the original's bugs in [bugs.md](bugs.md) (D1 frame-rate
      AI, D6 undetectable cells, ...), each on a switch.
- [ ] 60+ fps and 4K presentation.

## Housekeeping

- [ ] Split `src/machine/dos.c` (about 2,300 lines) into memory, programs,
      files, keyboard, video and mouse.
- [ ] Build-from-scratch steps in the README, tested on a fresh clone.
- [ ] `.gitattributes` for line endings.
- [ ] An automated build on GitHub (the CPU tests need no game files).
