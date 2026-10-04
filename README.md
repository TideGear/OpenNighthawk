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

- **The whole game boots and plays**: SETUP, the MicroProse logo, the intro,
  the front end (roster, pilot form, briefing, arming, hangar), takeoff and
  flight, with AdLib music and speech, Roland MT-32 output over MIDI, mouse,
  keyboard and joystick.
- **Translated:** 82,025 instructions across all 17 code files. On a full
  scripted session, 99.9% of executed instructions run as recompiled code;
  the rest is the LZEXE decompressors, which run once per program start.
- **Parity, measured:** that session (2.7 billion clocks, boot to quit) gives
  identical memory and register state at every checkpoint under the
  interpreter and the recompiled code, and a recorded input log replays on
  either to the same final state.
- **The translator is checked against silicon:** 90,900 8088 and 94,200
  80286 hardware test vectors run through generated code with 0 unexplained
  differences.

How the parity claim is built and checked: [docs/architecture.md](docs/architecture.md).

## You need your own copy of the game

This repository contains **source code only**: no game data, no original
executables, and none of the recompiler's output. The recompiler reads your
own installation (GOG, Steam or original media) at build time and writes its
output to a work directory on your machine, outside the repository. The game
is still sold; please buy it.

## Building

Requirements: Windows, Visual Studio 2026 Build Tools (MSVC), CMake 3.24+,
Ninja, and Python 3.12 with `capstone` (`py -m pip install capstone`).

```bat
py tools\build_recomp.py --data "D:\GOG\F-117A"
```

That one command translates the game, builds it, plays the scripted routes
in `tools/routes/` to gather coverage, translates and builds again, and
verifies parity between the engines. The result is `build\f117a.exe`.
`build.cmd` alone builds an interpreter-only executable.

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
