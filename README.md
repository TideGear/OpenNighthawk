# F-117A Recomp

This is a fully **human-driven**, AI-coded recompilation (**not a
reimplementation or a decompilation**) of the PC DOS classic *F-117A
Nighthawk Stealth Fighter 2.0* (MicroProse, 1991) for Windows. The
original's machine code is translated, instruction by instruction, into C
and compiled natively. Its emulated PC supports the GOG release's DOSBox
timing and a 386DX/33 profile measured against 86Box. Every translated
instruction is checked against a reference interpreter validated on real
silicon.

**This is not a lazy, fire-and-forget project.** I decide what gets
built, what counts as proof, which results to distrust and measure again,
and what is accepted. Nothing is called done because it compiles or looks
right; it is done when it matches the original under checks that are
written down here and that you can re-run on your own copy. F-117A is
near and dear to my heart, and I will keep supporting this game, well
past the first playable build.

**Why it matters.** The original DOS release is buggy and has not aged well.
It needs an emulator to run at all today. Its frame-rate controller
changes world speed with machine speed, and long-lived SA-5 missiles can
lose proximity damage at high frame estimates; its
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
options. Twelve catalogued fixes and balance options are available now, all off by default.

> **Not affiliated with anyone.** This is an unofficial, fan-made project. It
> is not affiliated with, authorized by, endorsed by, or associated with
> Atari Interactive, Inc., MicroProse, or any other rights holder.
> *F-117A Nighthawk Stealth Fighter* and all related marks are the property
> of their respective owners. **You must own a legal copy of the original
> game to use this software.** It is sold on
> [Steam](https://store.steampowered.com/app/328920/) and on
> [GOG](https://www.gog.com/en/game/f117a_nighthawk_stealth_fighter_20). Please buy it.

## Status

- **The whole game loop runs**: SETUP, the MicroProse logo, the intro, the
  front end (roster, pilot form, briefing, arming, hangar), takeoff, flight,
  the debriefing (END) and back to the front end. All three sound drivers
  work: AdLib (music and digitised speech through an OPL emulator), the PC
  speaker, and Roland (Munt synthesis with your MT-32 ROMs, or Windows
  MIDI; live synthesis tested, exact rendered parity still open).
- **Translated:** 89,366 instruction starts in the latest checked build,
  across all 17 code files and the
  LZEXE decompressor: 96% of the bytes of the code areas, and no
  untranslated stretch left that decodes as code (`tools/census.py`; the
  rest is strings, tables and variables). On the scripted sessions no game
  instruction is interpreted under the default timing profile; only the
  emulated BIOS's own stubs are.
- **Named and matched:** 765 routine addresses (760 census functions,
  63,917 of 179,213 code bytes) have explained C equivalents checked against
  their original bodies. Another 775 census functions remain. Under the
  386 profile, matched entries use their original translated bodies until
  their handwritten clocks gain cycle costs.
- **Parity, measured:** thirty-five scripted sessions (boot to flight; a full
  sortie through the debriefing and back; boots under the speaker and Roland
  drivers, where the programs load at other addresses; transfer and flight
  routes across all nine theatres and all four mission categories; pilot
  creation, editing, erasure and saving; roster dialogs and direct maintenance;
  a destroyed ground-strike primary with consumed Mavericks and hit credit;
  a takeoff, return and landing at the home base; a credited reconnaissance
  photograph; every primary objective type, supply drops, secret airstrips,
  air-to-air kills, and the career awards and retirement)
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
- **Two more reference machines, headless:** `tools/pc_parity.py` plays the
  intro on GOG's DOSBox (a saved capture), on DOSBox-X and on 86Box at once,
  with no window and no sound, and checks the pictures, the music and a
  scripted pilot-roster session's saved file against measured limits: DOSBox-X matches 1,237 exact pictures in order (timing within
  about 0.2 s). The 86Box reference is now a 386DX/33 running MS-DOS 5.00
  with Microsoft MOUSE.COM 6.26. Its intro pictures match in VGA DAC values
  except two documented samples between consecutive pictures; the check
  rejects any other difference. Builds and notes: `tools/ref86box/`.
- **386 timing on both engines:** the latest calibration matches all 93
  saved 86Box instruction and service probes (the DOS time query varies by
  four cycles between runs). Paired flight sessions have 574 identical
  machine-state checkpoints and stick responses. File loading still drifts
  against the reference VM's disk model; fast loads remain the default.
  This profile is currently exposed through the headless runner:
  `build\f117run.exe --timing 386 --engine recomp --data INSTALL_DIR`.
- **Every translated instruction, not only the ones the sessions reach:**
  the sessions run about half the game's code (`tools/exercised.py`).
  `tests/insn_lockstep.c` runs each of the 89,366 translated instruction starts
  from 64 random machine states through the generated code and the
  interpreter and compares everything it can change: 5.7 million
  comparisons per timing profile, 0 differences. The 386 check includes
  prefetch and unfinished REP state; a second seed also checks code in VGA
  memory.
- **The translator is checked against silicon:** 90,900 8088 and 94,200
  80286 hardware test vectors run through generated code with 0 unexplained
  differences.
- **Steam's release works too:** its game files are byte-identical to GOG's
  (`tools/verify_install.py`), and the app finds its install.
- **Not yet done:** the remaining named C routines; live 60+ fps and 4K
  presentation (offline studies exist); full frame parity; remaining bug
  fixes; play by a person at the keyboard; rendered Roland output against
  an independent reference and listening checks.

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
`py tools/verify_install.py --data "C:\GOG Games\F-117A"` checks your copy
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
py tools/build_recomp.py --data "C:/GOG Games/F-117A" --work "$env:USERPROFILE/f117-recomp-local/OpenNighthawk"
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
py tools\build_recomp.py --data "C:\GOG Games\F-117A"
```

That one command translates the game, builds it, plays the scripted routes
in `tools/routes/` to gather coverage, translates and builds again, and
verifies parity between the engines. With `F117R_BUILD_APP=ON` (the CMake
default), the result includes `build\f117a.exe`. If this checkout was
configured for headless work, enable the app after the gate with
`build.cmd -DF117R_BUILD_APP=ON`. A build without generated C supports only
the interpreter; `build.cmd` reuses the checkout's existing CMake settings.

After a runtime change, `py tools\build_recomp.py --data "C:\GOG Games\F-117A"
--parity-only` checks the existing build without translating or rebuilding.
It compares checkpoints every 50 million clocks as well as final states,
then runs instruction checks under both timing profiles, matched-routine
checks, and confirms every routine that needs route coverage was exercised.

## Running

```bat
build\f117a.exe --data "C:\GOG Games\F-117A"
```

| Option | Meaning |
|---|---|
| `--data DIR` | the game's folder (found automatically for common GOG/Steam paths) |
| `--save DIR` | where the game's own saves go (default: your user profile); the install is never written |
| `--engine recomp\|interp` | recompiled code (default) or the reference interpreter only |
| `--ips N` | emulated CPU speed, instructions per second (default 20,000,000 in `f117a`, which plays at 16.7 frames a second in flight; 9,000,000 in `f117run` and the route checks, GOG DOSBox's `cycles=9000`) |
| `--roland munt\|windows\|off` | where Roland music goes: Munt's MT-32 emulation (needs `--mt32-roms`), the Windows MIDI synthesizer, or nowhere |
| `--mt32-roms DIR` | the folder holding your MT-32 control and PCM ROMs, recognised by content whatever they are called (see [MT-32 ROMs](#mt-32-roms)); on its own it means `--roland munt` |
| `--midi N` | send Roland MIDI to Windows MIDI device N instead of the mapper |
| `--mt32-control FILE --mt32-pcm FILE` | name the two ROM files instead of a folder |
| `--opl dbopl\|nuked` | GOG DOSBox's OPL2 synthesizer (default), or Nuked OPL3 in OPL2 mode; both output at 44,100 Hz |
| `--speaker realsound\|pwm` | smoothed digitised speaker speech (default), or the original PWM carrier |
| `--scale N`, `--fullscreen`, `--no-aspect` | window size; fullscreen; square pixels instead of 4:3 |
| `--config FILE`, `--no-config` | read settings from FILE instead of `f117a.ini` beside the executable; or read none |
| `--fix ID`, `--list-fixes` | switch on a fix for one of the original's bugs (`all` for every one); every fix is off unless named, so the default is the original, bugs included |

Every option can also be kept in a settings file: copy `f117a.example.ini`
(written beside `f117a.exe` by the build) to `f117a.ini` in the same folder
and edit it - one option per line without its dashes (`data = C:\GOG Games\F-117A`,
`roland = munt`, `mt32-roms = C:\ROMs`, `fullscreen = yes`, `fix = D4, D5`).
Relative paths are taken from the file's folder, and an option given on the
command line still wins (any Roland option there replaces all of the file's).
`--config FILE` reads another file; `--no-config` ignores it.

The game asks its original SETUP questions at each start (joystick, sound
card), as it did in 1991. Answer 2 for AdLib, or 3 for Roland with `roland`
set (and, for Munt, `mt32-roms`).

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

### MT-32 ROMs

You supply two ROM files from a Roland MT-32 (or CM-32L): the **control ROM**
(the unit's firmware) and the **PCM ROM** (its sampled sounds). Put them in
one folder and point `mt32-roms` at it. The files are recognised by their
SHA-1 (Munt's own table), so their names do not matter; with several in the
folder, the first match in this order is used. F-117A (1991) wants the
first-generation ("old") MT-32 with control ROM 1.0x. Rendering
this game's own MIDI (a run from boot into flight) agrees: control ROMs 1.04
and 1.07 give bit-identical output, while 2.04 plays the music much the same
but the in-flight effects about 5 dB quieter and 5-7 dB duller above 3 kHz.
Any 1.0x ROM is right; 2.0x and the CM-32L are accepted as fallbacks.

| Control ROM | Size | SHA-1 |
|---|---|---|
| MT-32 1.07 | 64 KB | `b083518fffb7f66b03c23b7eb4f868e62dc5a987` |
| MT-32 1.06 | 64 KB | `a553481f4e2794c10cfe597fef154eef0d8257de` |
| MT-32 1.05 | 64 KB | `e17a3a6d265bf1fa150312061134293d2b58288c` |
| MT-32 1.04 | 64 KB | `5a5cb5a77d7d55ee69657c2f870416daed52dea7` |
| MT-32 BlueRidge | 64 KB | `7b8c2a5ddb42fd0732e2f22b3340dcf5360edf92` |
| MT-32 2.07 | 128 KB | `47b52adefedaec475c925e54340e37673c11707c` |
| MT-32 2.06 | 128 KB | `2869cf4c235d671668cfcb62415e2ce8323ad4ed` |
| MT-32 2.04 | 128 KB | `2c16432b6c73dd2a3947cba950a0f4c19d6180eb` |
| MT-32 2.03 | 128 KB | `5837064c9df4741a55f7c4d8787ac158dff2d3ce` |
| CM-32L 1.02 | 64 KB | `a439fbb390da38cada95a7cbb1d6ca199cd66ef8` |
| CM-32L 1.00 | 64 KB | `73683d585cd6948cc19547942ca0e14a0319456d` |
| CM-32LN 1.00 | 64 KB | `dc1c5b1b90a4646d00f7daf3679733c7badc7077` |

| PCM ROM | Size | SHA-1 | Goes with |
|---|---|---|---|
| MT-32 | 512 KB | `f6b1eebc4b2d200ec6d3d21d51325d5b48c60252` | every MT-32 control ROM |
| CM-32L | 1 MB | `289cc298ad532b702461bfc738009d9ebe8025ea` | the CM-32L and CM-32LN control ROMs |

Each ROM must be a single whole file; the split halves some dumps come as
(`..._a` / `..._b`, `..._l` / `..._h`) are not combined. On Windows,
`certutil -hashfile FILE SHA1` shows a file's SHA-1.

For offline validation, `f117run --midi-log FILE` records each MPU byte with
its instruction clock. `f117run --speaker-log FILE` records each speaker hook
event with the guest clock, port 61h low bits, PIT2 reload and mode, PIT2
epoch in PIT clocks and PIT2's null-count flag (1 after a control word).
`audio_render` merges OPL, speaker, and optional MIDI events by guest clock;
for example, render a complete OPL/speaker replay
through a known end clock with:

```bat
build\audio_render.exe opl.log flight.wav 9000000 9799924671 --speaker-log speaker.log
```

`--speaker-model pwm` renders the speaker's digitised speech with its 15 kHz
carrier instead of without it (`realsound`, the default; `speaker` in
`f117a.ini`).

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
py tools\build_recomp.py --data "C:\GOG Games\F-117A"
rem Plant defects in the generated code and check the comparison sees them:
py tools\mutation_check.py --data "C:\GOG Games\F-117A" --random 8
```

The machine model itself is checked against other PCs with
`py tools\pc_parity.py --data "C:\GOG Games\F-117A"`, which plays the intro on
GOG's DOSBox (a saved capture), DOSBox-X and 86Box, headless, and compares
the pictures, the music and a saved roster; `tools\fidelity_all.py` does the
same for the machine-behaviour probe. DOSBox-X and 86Box are built from source for it; the steps are in
[tools/ref86box/build_dosbox_x.md](tools/ref86box/build_dosbox_x.md) and
[tools/ref86box/build_86box.md](tools/ref86box/build_86box.md). Every process the
project repeats, and what it costs, is listed in
[docs/repeated-processes.md](docs/repeated-processes.md).

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
py tools\landing_pilot.py --data "C:\GOG Games\F-117A" --engine interp --replay tools/routes/landing.input --out C:/landing-check
```

The reconnaissance route earns primary photo credit and stops airborne.
Its observer checks the exposure count, photo-credit event, intact target
and retained camera:

```bat
py tools\recon_pilot.py --data "C:\GOG Games\F-117A" --engine interp --replay tools/routes/recon.input --out C:/recon-check
```

`recon_return` completes both photos and stops on the raised home runway;
`recon_career` continues through debriefing, earns the ten-mission tour
ribbon and saves the updated career. Both engines match. The stronger return
observer checks both original credit events, intact targets and the parent
result block:

```powershell
py tools/recon_pilot.py --data "C:\GOG Games\F-117A" --engine interp --complete --replay tools/routes/recon_return.input --out C:/recon-return-check
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
