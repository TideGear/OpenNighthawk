# OpenNighthawk (F-117A Recompilation)

> **Not affiliated with anyone.** This is an unofficial, fan-made project. It
> is not affiliated with, authorized by, endorsed by, or associated with
> Atari Interactive, Inc., MicroProse, or any other rights holder.
> *F-117A Nighthawk Stealth Fighter* and all related marks are the property
> of their respective owners. **You must own a legal copy of the original
> game to use this software.** It is sold on
> [Steam](https://store.steampowered.com/app/328920/) and on
> [GOG](https://www.gog.com/en/game/f117a_nighthawk_stealth_fighter_20). Please buy it.

This is a fully **human-driven**, AI-powered recompilation **AND**
decompilation (both lockstep-verified) of the PC DOS classic *F-117A
Nighthawk Stealth Fighter 2.0* (MicroProse, 1991) for Windows. The
original's machine code is translated, instruction by instruction, into C
and compiled natively. Its routines are then progressively replaced by
named, explained C, each proven to behave identically to the original
instructions, register for register and clock for clock. It is not a
byte-matching decompilation: it does not rebuild the original EXE. Its
emulated PC supports the GOG release's DOSBox timing and a 386DX/33 profile
measured against 86Box. Every translated instruction is checked against a
reference interpreter validated on real silicon.

**This is not a lazy, fire-and-forget project.** I decide what gets
built, what counts as proof, which results to distrust and measure again,
and what is accepted. Nothing is called done because it compiles or looks
right; it is done when it matches the original under checks that are
written down here and that you can re-run on your own copy. F-117A is
near and dear to my heart, and I will keep supporting this game, well
past the first playable build.

**Why it matters.** The original DOS release is buggy and has not aged well.
It needs an emulator to run at all today. Its frame-rate controller
changes world speed with machine speed, and on a fast PC long-lived SA-5
missiles cannot cause proximity damage; its digitised speech can hang the
game; supply drops never earn credit; secret-airstrip missions are disabled
outside the Persian Gulf; and long missions can overrun an unbounded table
and corrupt the game's state. The catalogue in [docs/bugs.md](docs/bugs.md)
has the evidence for each. Keeping the game playable, faithfully, on today's
machines is a game-preservation job, and this project treats it as one.

**1:1 parity with the original is the first priority.** With fixes off, the
game does exactly what the DOS original does, including its bugs, which are
tracked in [docs/bugs.md](docs/bugs.md) so they can be fixed as switchable
options. Thirty-four catalogued fixes and balance options are enabled by
default for normal play, including D1REAL and D1PROX. `--fix none` restores
the original behavior. The headless reference runner and ordinary parity
gates keep fixes off; recorded sessions restore their recorded fix set
(older logs mean no fixes). The separate `--fix QUIZ` option restores the
original aircraft-identification quiz. It is off by default and is enabled
only when named explicitly; `--fix all` does not enable it.

## Status

Progress: **P1 100.00%, P2 95.94%, P3 85.08%, All 94.99%**. Every commit
title starts with the current figures (`py tools/progress.py --title`);
[docs/progress.md](docs/progress.md) says what they measure and
[docs/roadmap.md](docs/roadmap.md) what is done and what is left.

- **The whole game loop runs**: SETUP, the MicroProse logo, the intro, the
  front end (roster, pilot form, briefing, arming, hangar), takeoff, flight,
  the debriefing (END) and back to the front end. All three sound drivers
  work: AdLib (music and digitised speech through an OPL emulator), the PC
  speaker, and Roland (Munt synthesis with your MT-32 ROMs, or Windows
  MIDI; exact rendered Roland parity is still open).
- **Translated:** all 17 code files and the LZEXE decompressor, 89,443
  instruction starts, 96% of the bytes of the code areas; the rest is
  strings, tables and variables. On the scripted sessions no game
  instruction is interpreted under the default timing profile; only the
  emulated BIOS's own stubs are.
- **Named and matched:** 1,487 routines have named, explained C equivalents
  proved against the original's instructions. They run 156,432 of the
  163,907 instruction bytes the scripted routes execute; with 2,631 bytes of
  reviewed code that stays original by design, that is the gameplay score,
  97.04%. Across all 17 code files, named code covers 184,279 of 207,438
  creditable bytes (88.84%), and reviewed reference entries account for
  41,749 of the 46,112 bytes outside the routes (90.54%).
- **Parity, measured:** 63 scripted routes, run as 62 sessions, end in
  identical state under the reference interpreter and the recompiled code -
  all of memory, the registers and every byte sent to the sound card, the
  palette, MIDI and the disk - checked every 50 million clocks along the
  way. They cover boot to flight, full sorties through the debriefing, all
  three sound drivers, all nine theatres, every primary objective type, the
  pilot roster and the career to retirement
  ([every route](tools/routes/README.md)).
  Every translated instruction, reached or not, is also compared from random
  machine states (0 differences), the translator is checked against 8088 and
  80286 silicon test vectors, and planted defects in the generated code are
  caught.
- **The emulated PC is held to other machines:** GOG's DOSBox (the machine
  the game is sold on; the machine probe, the intro's pictures and music),
  DOSBox-X, and 86Box running a 386DX/33 with MS-DOS 5.00. The 386DX/33
  timing profile plays at a period PC's pace.
- **Steam's release works too:** its game files are byte-identical to GOG's
  (`tools/verify_install.py`), and the app finds its install.
- **Optional sharper and smoother pictures:** finer polygon geometry and HUD
  strokes up to 4K, with the original bitmap text and artwork kept; see
  [Presentation](#presentation).
- **What is left:** matching the remaining 2,342 route-executed bytes (most
  of them in the sound drivers) and the code no route runs, reviewed entries
  for the 2,187 bytes outside the routes that have none, and a closing review
  of indirect entries ([Phase 2](docs/roadmap.md#phase-2---understood-code));
  the re-release features below. Not scored: play by a person at the
  keyboard, rendered Roland output against an independent reference and
  listening checks, refresh rates above 60 Hz and other hardware
  ([Phase 4](docs/roadmap.md#phase-4---checked-by-people-and-independent-references)).
- **Re-release features** (mostly planned;
  [roadmap](docs/roadmap.md#phase-3---fixes-and-enhancements-switchable)):
  two settings, *Presentation* (Original, Enhanced, or a widescreen
  Remastered without the original's jitter) and *Fixes* (Original,
  Recommended with credited fan-community patches, or Custom); an in-game
  settings menu, controllers and HOTAS, a hybrid of MT-32 music with AdLib
  effects, flight replays and accessibility options. Built so far:
  `--presentation original|enhanced`, `--fixes original|recommended` and
  saves that remember their fix set (below). The game's logic is the same
  under every presentation.

How the parity claim is built and checked:
[docs/architecture.md](docs/architecture.md). The
[Windows build check](https://github.com/TideGear/OpenNighthawk/actions/workflows/windows.yml)
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
Python must be available in the terminal. The build fetches SDL 3.2.10 (and
Munt, below) on its first configure; it needs an internet connection then.

From a new PowerShell terminal, in the directory where you keep source:

```powershell
git clone https://github.com/TideGear/OpenNighthawk.git OpenNighthawk
Set-Location OpenNighthawk
py -m pip install capstone Pillow numpy
py tools/build_recomp.py --data "C:/GOG Games/F-117A" --work "$env:USERPROFILE/f117-recomp-local/OpenNighthawk"
```

Replace the data path with your installed game. `capstone` writes the
disassembly comments in the generated C; Pillow and NumPy are needed by the
picture and sound comparators (`tools/pc_parity.py`). Generated C, coverage
and saves go in the work directory (default `%USERPROFILE%\f117-recomp-local`).
On first build, `build.cmd` creates a `build` junction to a local directory
under `%USERPROFILE%\f117-recomp-local`, with a separate CMake cache for each
checkout. No generated files or game assets belong in the source tree. This
sequence was last verified from a fresh public clone on 4 October 2026.
Translation counts depend on accumulated coverage, so a first build can
translate slightly fewer instruction starts than the figures above.

That one command translates the game, builds it, plays the scripted routes
in `tools/routes/` to gather coverage, translates and builds again, and
verifies parity between the engines. It always builds the app,
`build\f117a.exe`. Run on its own, `build.cmd` reuses the checkout's existing
CMake settings; a build without generated C supports only the interpreter.

After a runtime change, `py tools\build_recomp.py --data "C:\GOG Games\F-117A"
--parity-only` checks the existing build without translating or rebuilding.

## Running

```bat
build\f117a.exe --data "C:\GOG Games\F-117A"
```

The game asks its original SETUP questions at each start (joystick, sound
card), as it did in 1991. Answer 2 for AdLib, or 3 for Roland with `roland`
set (and, for Munt, `mt32-roms`).

| Option | Meaning |
|---|---|
| `--data DIR` | the game's folder (found automatically for common GOG/Steam paths) |
| `--save DIR` | where the game's own saves go (default: `%APPDATA%\F117Recomp\F-117A\save`); the install is never written |
| `--engine recomp\|interp` | recompiled code (default) or the reference interpreter only |
| `--timing dosbox\|386` | the emulated PC's speed model: `dosbox` (default) is this machine's instruction model at `--ips`; `386` models a 1991 386DX/33 (original flight typically 5-9 fps with fixes off) |
| `--ips N` | emulated CPU speed under `dosbox` timing, instructions per second (60,000,000 in `f117a`; 9,000,000 in `f117run` and route checks, GOG DOSBox's `cycles=9000`). Default D1REAL separately targets fifteen simulation steps per second |
| `--sim-hz 8\|15` | the D1REAL simulation rate (below): 15 (default) or the earlier 8; `--sim-hz 8 --ips 20000000` is the previous play mode. Replays restore their recorded rate; logs without one use 8 |
| `--roland munt\|windows\|off` | where Roland music goes: Munt's MT-32 emulation (needs `--mt32-roms`), the Windows MIDI synthesizer (General MIDI: wrong instruments), or nowhere |
| `--mt32-roms DIR` | the folder holding your MT-32 control and PCM ROMs, recognised by content whatever they are called (see [MT-32 ROMs](#mt-32-roms)); on its own it means `--roland munt` |
| `--midi N` | send Roland MIDI to Windows MIDI device N instead of the mapper |
| `--mt32-control FILE --mt32-pcm FILE` | name the two ROM files instead of a folder |
| `--opl dbopl\|nuked` | GOG DOSBox's OPL2 synthesizer (default), or Nuked OPL3 in OPL2 mode; both output at 44,100 Hz |
| `--speaker realsound\|pwm` | smoothed digitised speaker speech (default), or the original PWM carrier |
| `--scale N`, `--fullscreen`, `--no-aspect` | window size; fullscreen; square pixels instead of 4:3 |
| `--presentation original\|enhanced` | the picture as a single choice: `original` is the exact 320x200 picture, sharp, at 4:3 (the default); `enhanced` draws in-between frames at the display's refresh rate, as fine as the display's height allows at 4:3 (`--present rerun` with a matching `--present-scale`). A later `--present` option refines it. `remastered` is planned ([roadmap](docs/roadmap.md#phase-3---fixes-and-enhancements-switchable)) |
| `--present scan\|replay\|interp\|rerun` | the picture in flight; see [Presentation](#presentation) |
| `--present-scale N` | with `replay`, `interp` or `rerun`: the picture N times finer, 1-16 |
| `--present-line-width N` | fine HUD stroke width in output pixels, 0-20; 0 (default) scales with `--present-scale`, 1 is a thin stroke; text and artwork keep their original pixels |
| `--present-age interp\|extrapolate` | with `interp` or `rerun`: trail the original by one logic step and be exact at its frames (default), or predict beyond the latest frame (can overshoot a sudden change) |
| `--config FILE`, `--no-config` | read settings from FILE instead of `f117a.ini` beside the executable; or read none |
| `--fixes original\|recommended` | the fix set as a single choice: `original` plays the shipped game with every fix off; `recommended` enables the thirty-four normal-play fixes (the default). Later `--fix ID` options make a custom set. The saves remember the set that last wrote them (a `.fixset` file beside the save folder), and the app warns once when a session switches between the original game and a fixed set on the same saves |
| `--fix ID`, `--list-fixes` | enable a fix; `all` enables the thirty-four normal-play fixes, `none` clears the set. Normal play starts with all thirty-four enabled; `--fix none --fix D5` selects only D5. `QUIZ` requires its own explicit selection. Listing prints the current selection |
| `--record FILE`, `--no-record`, `--replay FILE` | record this session's input to FILE, or record nothing; play a recorded input log back |

Any option the app does not recognise (for example `--help`) prints a usage
summary, which also lists diagnostic options not shown here.

The default D1REAL fix replaces the original adaptive simulation cadence with
fifteen simulation steps per real second, thirty during 2x time compression;
it is validated at the default 60,000,000 ips
([validation and 4K60 results](docs/speed-sweep.md#fifteen-step-play-mode-validation); 40,000,000 ips
cannot keep up in compressed flight). See
[the timing option](docs/bugs.md#d1real-real-time-simulation-cadence). The
separate `D1TTL`, `D1SLOT`, `D1PROX` and `D1ACCEL` fixes repair incoming
missile lifetime, slot reuse, proximity sampling and acceleration.

Host keys (chosen so as not to collide with the game's):

| Keys | Action |
|---|---|
| Alt+Enter | toggle fullscreen |
| Ctrl+Alt+S | settings menu (pauses the game) |
| Ctrl+Alt+K | the game's key list, from its manual (pauses the game) |
| Ctrl+Alt+P | pause / resume |
| + / - / 0 (watching a replay) | faster / slower / normal speed |
| Ctrl+Alt+R (watching a replay) | start the replay again |
| Ctrl+Alt+F12 | quit |

### Settings file

Any option except `--config`, `--no-config` and `--list-fixes` can also be
kept in a settings file: copy `f117a.example.ini` (copied beside `f117a.exe`
by the build) to `f117a.ini` in the same folder and edit it - one option per
line without its dashes (`data = C:\GOG Games\F-117A`, `roland = munt`,
`mt32-roms = C:\ROMs`, `fullscreen = yes`, `presentation = enhanced`,
`fixes = original`).
Fix entries apply in order: `fix = none` followed by `fix = D4, D5` selects
only those two; leaving fix settings out keeps all fixes enabled for normal play.
Relative paths are taken from the file's folder, and an option given on the
command line still wins (any Roland option there replaces all of the file's).
`--config FILE` reads another file; `--no-config` ignores it.

### Presentation

By default (`--present scan`) you see exactly what the emulated VGA scanned
out, at 320x200. The other modes are optional and never change the game
itself:

- `--present replay --present-scale N` redraws the same picture from the
  original's own draw records, N times finer: the 3-D scene's polygons from
  their vertices and the HUD's lines on the output grid, with the original
  text, sprites and cockpit artwork kept. It shows a new picture only when
  the game draws one, and is the verified, sharp mode. Scale 9 fills a 4K
  screen's 2160 lines at 4:3.
- `--present rerun` adds in-between pictures, one logic step behind, drawn
  by the original's own unmodified drawing code on a disposable copy of the
  machine, then refined at the chosen scale. `--presentation enhanced`
  selects it.
- `--present interp` adds in-between pictures by moving matched shapes
  between two frames; anything without a match holds and then switches
  halfway. Superseded by `rerun`; kept as an experiment.

**Higher resolution (`--present-scale` above 1) and higher frame rates
(`interp`, `rerun`) are experimental.** Both redraw what the original draws,
so they keep the original renderer's quirks: roads, coastlines and other
edges jitter as they do in the original; objects can appear in front of or
behind others in the wrong order, as the original's drawing order puts them;
lines and dots keep the same thickness at any distance; and there may be
others. The window presents one picture per display refresh (vsync), so
`interp` and `rerun` run at the refresh rate Windows has set for the display
(60 fps at 60 Hz, 144 fps at 144 Hz) if the PC keeps up; the game's own
logic still steps at its own rate (fifteen a second under the default
D1REAL), and the extra pictures fall between its steps. Design, status and
measurements: [docs/presentation.md](docs/presentation.md).

The optional preset [f117a.4k.ini](f117a.4k.ini) (copied beside `f117a.exe`)
selects fullscreen, `present = rerun` and scale 9:
`build\f117a.exe --config f117a.4k.ini --data INSTALL_DIR`. Select
3840x2160 at 60 Hz in Windows' display settings first. The build replaces
the copy in `build\` whenever the repository's file changes: edit the
repository's file and rebuild, or keep your own file and pass it with
`--config`. For the sharp mode without in-between frames, use
`--present replay --present-scale 9` instead.

### Roland and Munt

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
missing ROMs fail explicitly.

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

## Verifying parity yourself

Every claim above can be re-run on your own copy:

```bat
rem The CPU core against the 8088 and 80286 silicon vectors (cached locally;
rem each script's --fetch downloads them first):
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
same for the machine-behaviour probe. DOSBox-X and 86Box are built from
source for it; the steps are in
[tools/ref86box/build_dosbox_x.md](tools/ref86box/build_dosbox_x.md) and
[tools/ref86box/build_86box.md](tools/ref86box/build_86box.md). The
386DX/33 profile can be run headless with
`build\f117run.exe --timing 386 --engine recomp --data INSTALL_DIR`.

Your own play sessions are recorded too: each run of `f117a.exe` writes
`sessions\session-<date-time>\` beside its save folder: the input log
(`input.log`) and the save folder as the session began (`save-start`).
`build\f117run.exe --data INSTALL_DIR --save COPY_OF_SAVE_START --replay input.log`
replays one under either engine (`--engine interp` or `recomp`), and the two
must end in the same state. A session played with fixes on names them in its
log (`# f117r-fixes D5`), and replaying it switches the same fixes on.

Some routes also have a stronger observer that checks what a route's own
milestones cannot: a real landing, a credited reconnaissance photograph, or
a secret airstrip's delivery. For example:

```powershell
py tools/landing_pilot.py --data "C:\GOG Games\F-117A" --engine interp --replay tools/routes/landing.input --out C:/landing-check
```

Every route, its observer and the observer's command:
[tools/routes/README.md](tools/routes/README.md) and
[docs/architecture.md](docs/architecture.md#observing-flights-through-normal-controls).
Career routes chain actual earned saves; no saved game is distributed. The dated
[parity audit](docs/parity-audit.md) records the DOSBox save, flight, input
stress, host pacing and rendered audio comparisons, with their differences.
Every process the project repeats, and what it costs, is listed in
[docs/repeated-processes.md](docs/repeated-processes.md).

## Repository layout

| Path | What |
|---|---|
| `src/cpu/` | the CPU: interpreter and shared instruction semantics |
| `src/machine/` | the emulated PC: DOS, BIOS, devices, the run loop |
| `src/recomp/` | the recompiled code's run-time |
| `src/matched/` | the named, matched C routines |
| `src/fixes/` | the switchable fixes for the original's bugs |
| `src/host/` | the SDL3 window, audio and input; the headless runner |
| `src/present/` | the optional sharper and smoother pictures (`replay`, `interp`, `rerun`) |
| `recompiler/` | the translator (Python) |
| `third_party/` | DOSBox DBOPL and Nuked OPL3, the two OPL synthesizers |
| `tools/` | the build pipeline, unpacker, scripted routes, checks |
| `tests/` | silicon-vector harnesses for the interpreter and for generated code; lockstep and unit tests |
| `docs/` | [architecture](docs/architecture.md), [roadmap](docs/roadmap.md), [progress scoring](docs/progress.md), [original-game bugs](docs/bugs.md), [presentation](docs/presentation.md), [parity audit](docs/parity-audit.md), [repeated processes](docs/repeated-processes.md), [provenance](docs/provenance.md), [other versions](docs/other-versions.md) (Macintosh, Amiga) |

## Licence

GPL-3.0 (see [LICENSE](LICENSE)). It covers this repository's code only and
grants nothing over the game, its data, or anything derived from them.
DOSBox DBOPL is GPL-2-or-later and Nuked OPL3 is LGPL-2.1-or-later; see
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

- **MicroProse / MPS Labs**, for the game, and for release notes that are
  still telling us how it works.
- **86Box** (the 86Box team), for the 386DX/33 reference PC the timing profile
  is held to, and the source its cycle costs come from.
- **DOSBox** (the DOSBox Team), for GOG's release, the timing reference; for the
  DOS services, mixer and VGA behaviour ported from its source; and for DBOPL,
  the reference OPL2 synthesizer.
- **DOSBox-X** (Jonathan Campbell and contributors), for a second reference
  machine.
- **Munt** (the Munt team), for the Roland MT-32 emulation.
- **Nuked OPL3** (nukeykt), for the AdLib's chip.
- **SDL** (Sam Lantinga and the SDL contributors), for the window, input and
  audio.
- **SingleStepTests**, for the 8088 and 80286 hardware vectors.
- **Capstone**, for the disassembly comments in the generated C and in the
  matched-routine drafts.
- **debugcom** and **damsonn**, for the mission-generator and secret-airstrip analysis (debugcom) and the fan patch for it (packaged by damsonn) that the
  bug tracker cites.
- **GOG** and **Steam**, for keeping the game on sale.
