# F-117A Recomp

A static recompilation of the PC DOS game *F-117A Nighthawk Stealth Fighter
2.0* (MicroProse, 1991) for Windows. The original's machine code is
translated, instruction by instruction, into C and compiled natively. It
runs on an emulated PC timed like the GOG release's DOSBox, and every
instruction is checked against a reference interpreter validated on real
silicon.

**1:1 parity with the original is the first priority.** The game does
exactly what the DOS original does, including its bugs, which are tracked in
[docs/bugs.md](docs/bugs.md) so they can be fixed later as switchable
options. Enhancements come after parity, never instead of it.

## Status

- **The whole game loop runs**: SETUP, the MicroProse logo, the intro, the
  front end (roster, pilot form, briefing, arming, hangar), takeoff, flight,
  the debriefing (END) and back to the front end. All three sound drivers
  work: AdLib (music and digitised speech through an OPL emulator), the PC
  speaker, and Roland (MIDI sent to a Windows MIDI device; not yet listened
  to on an MT-32).
- **Translated:** 89,276 instructions across all 17 code files and the
  LZEXE decompressor: 96% of the bytes of the code areas, and no
  untranslated stretch left that decodes as code (`tools/census.py`; the
  rest is strings, tables and variables). On the scripted sessions no game
  instruction is interpreted; only the emulated BIOS's own stubs are.
- **Parity, measured:** fifteen scripted sessions (boot to flight; a full
  sortie through the debriefing and back; boots under the speaker and Roland
  drivers, where the programs load at other addresses; transfer and flight
  routes across all nine theatres and all four mission categories; pilot
  creation, editing, erasure and saving; roster dialogs and direct maintenance;
  a takeoff, return and landing at the home base)
  end in
  identical state under the interpreter and the recompiled code - all of memory, the
  registers, and every byte sent to the sound card, the palette, MIDI and
  the disk - checked at intervals along the way. Planted defects in the
  generated code are caught (see `tools/mutation_check.py`). A recorded input log replays on either
  engine, and in the windowed game, to the same final state.
- **Random flying, held to the interpreter:** `tools/random_flights.py`
  takes off and hands the aircraft to a seeded random pilot (stick,
  throttle, weapons, views, systems keys) under both engines, hashing the
  whole machine every 50 million instructions. Every session so far is
  identical at every hash.
- **The emulated PC is held to GOG's DOSBox,** the machine the game is sold
  on: `tools/fidelity.py` runs one probe program under both and compares
  1,210 answers - the DOS memory layout, PSP and environment the game gets,
  every register after every DOS and BIOS call it makes, what each call
  costs in time, the BIOS data area, the VGA registers, the devices, the
  clocks, an EXEC'd child and the return from it. All agree (a 4 KB file
  read's cost within 5%, the clock rates within 2%). DOSBox-X is a second
  reference (`--reference dosbox-x`) for where DOSBox 0.74 itself is
  questionable.
- **The game's music against GOG's DOSBox:** `tools/dosbox_compare.py`
  captures the AdLib's register writes in DOSBox and here over the logo
  and intro: 22,687 writes, 100.7 s of music, identical in order and
  value, timing within 36 ms.
- **The picture comparison now runs:** `tools/video_compare.py` captures
  DOSBox's lossless video and compares exact RGB pictures against our
  completed four-part scanout frames at the native VGA period. A 130.8 s
  reference matches 1,319 pictures in order; seven reference pictures and
  28 shots remain unmatched,
  each lasting one sample. Scanout and transition timing remain under
  investigation; the apparent roster cursor changes were screenshot aliasing.
- **Every translated instruction, not only the ones the sessions reach:**
  the sessions run about half the game's code (`tools/exercised.py`).
  `tests/insn_lockstep.c` runs each of the 89,216 translated instructions
  from 64 random machine states through the generated code and the
  interpreter and compares everything it can change: 5.7 million
  comparisons, 0 differences.
- **The translator is checked against silicon:** 90,900 8088 and 94,200
  80286 hardware test vectors run through generated code with 0 unexplained
  differences.
- **Steam's release works too:** its game files are byte-identical to GOG's
  (`tools/verify_install.py`), and the app finds its install.
- **Not yet done:** play by a person at the keyboard; full frame parity
  with DOSBox; the Roland (MT-32) output listened to (Munt is
  planned).

How the parity claim is built and checked: [docs/architecture.md](docs/architecture.md).
What is done and what is left: [docs/roadmap.md](docs/roadmap.md).
The [Windows build check](https://github.com/TideGear/OpenNighthawk/actions/workflows/windows.yml)
builds the full interpreter application and runs ROM-free CPU, machine and
comparator tests on pushes and pull requests. Game-dependent parity checks
run locally against the user's installation.

## You need your own copy of the game

This repository contains **source code only**: no game data, no original
executables, and none of the recompiler's output. The recompiler reads your
own installation (GOG, Steam or original media) at build time and writes its
output to a work directory on your machine, outside the repository. The game
is still sold; please buy it.

Every result here was measured on GOG's installer
`setup_f-117a_nighthawk_stealth_fighter_2.0_1.0_(28044).exe`, which ships
MicroProse's final 473.04 update already applied (its VGAME.EXE is
byte-identical to the one in `f11704 (473.04 Update).zip`).
`py tools/verify_install.py --data "D:\GOG\F-117A"` checks your copy
against the SHA-256 of every file the project reads.

## Building

Requirements: Windows, Visual Studio 2026 Build Tools with the C++ tools and
Windows SDK, CMake 3.24+, Ninja, Git, and Python 3.12. CMake, Ninja, Git and
Python must be available in the terminal. The build fetches SDL 3.2.10 on
its first configure; it needs an internet connection then.

From a new PowerShell terminal, in the directory where you keep source:

```powershell
git clone https://github.com/TideGear/OpenNighthawk.git OpenNighthawk
Set-Location OpenNighthawk
py -m pip install capstone Pillow
py tools/build_recomp.py --data "D:/GOG/F-117A" --work "$env:USERPROFILE/f117-recomp-local/OpenNighthawk"
```

Replace the data path with your installed game. `capstone` is needed by the
translator; Pillow enables the picture comparator tests. Generated C,
coverage and saves go in the supplied work directory. On first build,
`build.cmd` creates a `build` junction to a local directory under
`$env:USERPROFILE/f117-recomp-local`, with a separate CMake cache for each
checkout. No generated files or game assets belong in the source tree.
This sequence was verified from a fresh public clone, including coverage,
both builds, the six routes present in that clone and the instruction lockstep. Translation
counts depend on accumulated coverage: that run produced 89,276 starts,
compared with 89,216 in the earlier local build.

For a checkout that is already set up, the default work directory works too:

```bat
py tools\build_recomp.py --data "D:\GOG\F-117A"
```

That one command translates the game, builds it, plays the scripted routes
in `tools/routes/` to gather coverage, translates and builds again, and
verifies parity between the engines. The result is `build\f117a.exe`.
`build.cmd` alone builds an interpreter-only executable.

After a runtime change, `py tools\build_recomp.py --data "D:\GOG\F-117A"
--parity-only` checks the existing build without translating or rebuilding.
It compares checkpoints every 50 million clocks as well as final states,
then runs the instruction lockstep.

## Running

```bat
build\f117a.exe --data "D:\GOG\F-117A"
```

| Option | Meaning |
|---|---|
| `--data DIR` | the game's folder (found automatically for common GOG/Steam paths) |
| `--save DIR` | where the game's own saves go (default: your user profile); the install is never written |
| `--engine recomp\|interp` | recompiled code (default) or the reference interpreter only |
| `--ips N` | emulated CPU speed, instructions per second (default 9,000,000: GOG DOSBox's `cycles=9000`) |
| `--midi N` | send the Roland driver's MIDI to Windows MIDI device N (-1: the mapper) |
| `--scale N`, `--fullscreen`, `--no-aspect` | window size; fullscreen; square pixels instead of 4:3 |

The game asks its original SETUP questions at each start (joystick, sound
card), as it did in 1991. Answer 2 for AdLib, or 3 for Roland with `--midi`.

Host keys (chosen so as not to collide with the game's):

| Keys | Action |
|---|---|
| Alt+Enter | toggle fullscreen |
| Ctrl+Alt+P | pause / resume |
| Ctrl+Alt+F12 | quit |

## Verifying parity yourself

Every claim above can be re-run on your own copy:

```bat
rem The CPU core against the 8088 and 80286 silicon vectors (cached locally):
py tests\sstest.py
py tests\sst286.py
rem The recompiler's generated code against the same vectors:
py tests\sst_recomp.py --per-file=300
rem Both engines on every scripted route (part of build_recomp.py):
py tools\build_recomp.py --data "D:\GOG\F-117A"
rem Plant defects in the generated code and check the comparison sees them:
py tools\mutation_check.py --data "D:\GOG\F-117A" --random 8
```

Your own play sessions are recorded too: each run of `f117a.exe` writes
`sessions\<date-time>\` (the input log and the save folder as it began)
next to its save folder. `build\f117run.exe --replay input.log` replays one
under either engine (`--engine interp` or `recomp`), and the two must end in
the same state.

The landing route also has a stronger observer that checks ground contact
inside the home approach box, a stop at idle, gear/brakes, the original
countdown and the parent flight record's successful-return result:

```bat
py tools\landing_pilot.py --data "D:\GOG\F-117A" --engine interp --replay tools/routes/landing.input --out C:/landing-check
```

## Repository layout

| Path | What |
|---|---|
| `src/cpu/` | the CPU: interpreter and shared instruction semantics |
| `src/machine/` | the emulated PC: DOS, BIOS, devices, the run loop |
| `src/recomp/` | the recompiled code's run-time |
| `src/host/` | the SDL3 window, audio, input; the headless runner |
| `recompiler/` | the translator (Python) |
| `tools/` | the build pipeline, unpacker, scripted routes |
| `tests/` | silicon-vector harnesses for the interpreter and for generated code |
| `docs/` | [architecture](docs/architecture.md), [original-game bugs](docs/bugs.md), [provenance](docs/provenance.md) |

## Licence

GPL-3.0 (see [LICENSE](LICENSE)). It covers this repository's code only and
grants nothing over the game, its data, or anything derived from them.
Nuked OPL3 is LGPL-2.1; see [docs/provenance.md](docs/provenance.md).

*F-117A Nighthawk Stealth Fighter 2.0* and its assets belong to their rights
holder. This project is not affiliated with or endorsed by Atari, MicroProse
or anyone else. The name is used only to say what the software is compatible
with.

## Acknowledgements

- **The F-117A Reimp (OpenF-117A)**, the same owner's oracle-verified
  reimplementation, whose interpreter, DOS model, unpacker and two months of
  reverse engineering this project is built on.
- **SingleStepTests**, for the 8088 and 80286 hardware vectors.
- **Nuked OPL3** (nukeykt), for the AdLib's chip.
- **debugcom**, for the mission-generator and secret-airstrip analysis the
  bug tracker cites.
