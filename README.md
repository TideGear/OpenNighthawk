# F-117A Recomp

This is a fully **human-driven**, AI-coded recompilation (**not a
reimplementation or a decompilation**) of the PC DOS classic *F-117A
Nighthawk Stealth Fighter 2.0* (MicroProse, 1991) for Windows. The
original's machine code is translated, instruction by instruction, into C
and compiled natively. It runs on an emulated PC timed like the GOG
release's DOSBox, and every instruction is checked against a reference
interpreter validated on real silicon.

**This is not a lazy, fire-and-forget project.** A person directs every
step: what gets built, what counts as proof, which results to distrust and
measure again, and what is accepted. Nothing is called done because it
compiles or looks right; it is done when it matches the original under
checks that are written down here and that you can re-run on your own copy.
F-117A is near and dear to my heart, and I will keep supporting this game,
well past the first playable build.

**Why it matters.** The original DOS release is buggy and has not aged well.
It needs an emulator to run at all today. Its frame-rate controller
misbehaves on fast machines, degrading enemy AI and weapon guidance; its
digitised speech can hang the game; supply drops never earn credit;
secret-airstrip missions are disabled outside the Persian Gulf; and long
missions can overrun an unbounded table and corrupt the game's state. The
catalogue in
[docs/bugs.md](docs/bugs.md) has the evidence for each. Keeping the game
playable, faithfully, on today's machines is a game-preservation job, and
this project treats it as one.

**1:1 parity with the original is the first priority.** The game does
exactly what the DOS original does, including its bugs, which are tracked in
[docs/bugs.md](docs/bugs.md) so they can be fixed later as switchable
options. Enhancements come after parity, never instead of it.

> **Not affiliated with anyone.** This is an unofficial, fan-made project. It
> is not affiliated with, authorized by, endorsed by, or associated with
> Atari Interactive, Inc., MicroProse, or any other rights holder.
> *F-117A Nighthawk Stealth Fighter* and all related marks are the property
> of their respective owners. **You must own a legal copy of the original
> game to use this software.** It is [sold on
> Steam](https://store.steampowered.com/app/328920/) and on
> [GOG](https://www.gog.com/). Please buy it.

## Status

- **The whole game loop runs**: SETUP, the MicroProse logo, the intro, the
  front end (roster, pilot form, briefing, arming, hangar), takeoff, flight,
  the debriefing (END) and back to the front end. All three sound drivers
  work: AdLib (music and digitised speech through an OPL emulator), the PC
  speaker, and Roland (Munt synthesis with your MT-32 ROMs, or Windows
  MIDI; live synthesis tested, exact rendered parity still open).
- **Translated:** 89,276 instructions across all 17 code files and the
  LZEXE decompressor: 96% of the bytes of the code areas, and no
  untranslated stretch left that decodes as code (`tools/census.py`; the
  rest is strings, tables and variables). On the scripted sessions no game
  instruction is interpreted; only the emulated BIOS's own stubs are.
- **Parity, measured:** nineteen scripted sessions (boot to flight; a full
  sortie through the debriefing and back; boots under the speaker and Roland
  drivers, where the programs load at other addresses; transfer and flight
  routes across all nine theatres and all four mission categories; pilot
  creation, editing, erasure and saving; roster dialogs and direct maintenance;
  a destroyed ground-strike primary with consumed Mavericks and hit credit;
  a takeoff, return and landing at the home base; a credited reconnaissance
  photograph)
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
  with DOSBox; rendered Roland (MT-32) output checked with user-supplied ROMs.

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

**Test evidence follows the same rule.** Routes commit only the inputs a
player would type or click; every capture, screenshot, recording, save and
memory dump they produce is written to a private work directory, never into
the repository. If you find anything in this repository that breaches this,
please open an issue: that is a bug, and a serious one.

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
| `--mt32-control FILE --mt32-pcm FILE` | render Roland through Munt using your control/PCM ROM pair; choose this or `--midi` |
| `--opl dbopl\|nuked` | GOG DOSBox's OPL2 synthesizer (default), or Nuked OPL3 in OPL2 mode; both output at 44,100 Hz |
| `--scale N`, `--fullscreen`, `--no-aspect` | window size; fullscreen; square pixels instead of 4:3 |
| `--fix ID`, `--list-fixes` | switch on a fix for one of the original's bugs (`all` for every one); every fix is off unless named, so the default is the original, bugs included |

The game asks its original SETUP questions at each start (joystick, sound
card), as it did in 1991. Answer 2 for AdLib, or 3 for Roland with `--midi`
or the Munt ROM options.

Munt is part of the default build; no ROMs are included. CMake fetches
[libmt32emu](https://github.com/munt/munt/tree/master/mt32emu) at a pinned
commit and builds it as a shared library (`mt32emu-2.dll`), copied beside the
Windows executables. Pass `-DF117R_MT32EMU_SOURCE=C:/path/to/munt/mt32emu` to
use a checkout instead, `-DF117R_MT32EMU_SYSTEM=ON` for an installed CMake
package, or `-DF117R_WITH_MT32EMU=OFF` to leave Munt out (a build directory
configured before Munt became the default keeps its old setting until you
pass `-DF117R_WITH_MT32EMU=ON`). Munt is LGPL-2.1-or-later; its source and
license come with the fetched checkout (`build/_deps/munt-src`).
Version 2.8.3 at commit `6e7c01fba7e1d50c8fa705834889fd0eac136075` was built
and checked with first-generation MT-32 1.07 control and PCM ROMs. Invalid or
missing ROMs fail explicitly. Original game MIDI renders offline and live;
exact reference PCM comparison, flight sound and listening checks remain open.

For offline validation, `f117run --midi-log FILE` records each MPU byte with
its instruction clock. `f117run --speaker-log FILE` records each speaker hook
event with the guest clock, port 61h low bits, PIT2 reload and mode, and PIT2
epoch in PIT clocks. `audio_render` merges OPL, speaker, and optional
MIDI events by guest clock; for example, render a complete OPL/speaker replay
through a known end clock with:

```bat
build\audio_render.exe opl.log flight.wav 9000000 9799924671 --speaker-log speaker.log
```

To include Roland, add `--mt32` and the control and PCM ROM paths as in this
example:

```bat
build\audio_render.exe midi.log roland.wav 9000000 1300000000 --mt32 "C:\ROMs\CONTROL.ROM" "C:\ROMs\PCM.ROM"
```

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
the same state. A session played with fixes on names them in its log
(`# f117r-fixes D5`), and replaying it switches the same fixes on.

The landing route also has a stronger observer that checks ground contact
inside the home approach box, a stop at idle, gear/brakes, the original
countdown and the parent flight record's successful-return result:

```bat
py tools\landing_pilot.py --data "D:\GOG\F-117A" --engine interp --replay tools/routes/landing.input --out C:/landing-check
```

The reconnaissance route earns primary photo credit and stops airborne.
Its observer checks the exposure count, photo-credit event, intact target
and retained camera:

```bat
py tools\recon_pilot.py --data "D:\GOG\F-117A" --engine interp --replay tools/routes/recon.input --out C:/recon-check
```

`recon_return` completes both photos and stops on the raised home runway;
`recon_career` continues through debriefing, earns the ten-mission tour
ribbon and saves the updated career. Both engines match. The stronger return
observer checks both original credit events, intact targets and the parent
result block:

```powershell
py tools/recon_pilot.py --data "D:\GOG\F-117A" --engine interp --complete --replay tools/routes/recon_return.input --out C:/recon-return-check
```

The [parity audit](docs/parity-audit.md) documents independent DOSBox saves,
flight observations, input stress, host pacing and rendered audio, including
the remaining differences and checks requiring hardware or ROMs.

`career_promotion` earns Serge's starting roster through `career_serge`,
then flies another normal sortie to earn First Lieutenant and an Airman's
Medal. Its `# seed-roster` prerequisite is replayed in a fresh process;
only the resulting roster is carried forward. No saved game is distributed.

`secret_airstrip` lands normally at a Persian Gulf secret strip and earns
the primary's original event and store consumption. It stops at the strip.
Use `tools/airstrip_check.py --replay tools/routes/secret_airstrip.input
--steps 8373782617 --data INSTALL_DIR --out PRIVATE_DIR` for the stronger
read-only acceptance check.

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
DOSBox DBOPL is GPL-2-or-later and Nuked OPL3 is LGPL-2.1; see
[docs/provenance.md](docs/provenance.md).

**This project takes no money.** There is no donation link, no sponsorship,
no paid build and no monetisation of any kind, and there will not be. If you
want to spend money on F-117A, spend it on
[the game](https://store.steampowered.com/app/328920/).

## Legal

*F-117A Nighthawk Stealth Fighter 2.0* and its assets are the property of
their rights holder. Atari Interactive re-acquired the MicroProse air-combat
catalogue in April 2023 and **the game is still sold**: this is not an
abandonware project, and it is not treated as one.

This project distributes no copyrighted material and requires you to supply
your own legally obtained copy. It exists for interoperability and
preservation: so that people who own the game can still run it, faithfully,
on hardware it was never written for, and later without its original
defects.

The game's name is used only to say truthfully what this software is
compatible with. No MicroProse or Atari logo, box art or trade dress is
used, and no claim of association is made or implied.

**Please do not use this project to help anyone pirate the game.** Issues,
pull requests and discussions asking where to download game data will be
closed. Buy it; it is inexpensive and still available.

## Acknowledgements

- **The F-117A Reimp (OpenF-117A)**, the same owner's oracle-verified
  reimplementation, whose interpreter, DOS model, unpacker and two months of
  reverse engineering this project is built on.
- **SingleStepTests**, for the 8088 and 80286 hardware vectors.
- **Nuked OPL3** (nukeykt), for the AdLib's chip.
- **DOSBox DBOPL** (The DOSBox Team), for the reference OPL2 synthesizer.
- **debugcom**, for the mission-generator and secret-airstrip analysis the
  bug tracker cites.
- **MicroProse / MPS Labs**, for the game, and for release notes that are
  still telling us how it works.
