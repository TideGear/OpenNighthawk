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
changes world speed with machine speed, and long-lived SA-5 missiles can
lose proximity damage at high frame estimates; its
digitised speech can hang the game; supply drops never earn credit;
secret-airstrip missions are disabled outside the Persian Gulf; and long
missions can overrun an unbounded table and corrupt the game's state. The
catalogue in [docs/bugs.md](docs/bugs.md) has the evidence for each.
Keeping the game playable, faithfully, on today's machines is a
game-preservation job, and this project treats it as one.

**1:1 parity with the original is the first priority.** With fixes off, the
game does exactly what the DOS original does, including its bugs, which are tracked in
[docs/bugs.md](docs/bugs.md) so they can be fixed as switchable options.
Fifteen catalogued fixes and balance options are enabled by default for normal
play, including D1REAL and D1PROX. `--fix none` restores the original behavior.
The headless reference runner and ordinary parity gates keep fixes off;
recorded sessions restore their recorded fix set (older logs mean no fixes).

## Status

- **The whole game loop runs**: SETUP, the MicroProse logo, the intro, the
  front end (roster, pilot form, briefing, arming, hangar), takeoff, flight,
  the debriefing (END) and back to the front end. All three sound drivers
  work: AdLib (music and digitised speech through an OPL emulator), the PC
  speaker, and Roland (Munt synthesis with your MT-32 ROMs, or Windows
  MIDI; exact rendered Roland parity is still open).
- **Translated:** all 17 code files and the LZEXE decompressor, 89,365
  instruction starts, 96% of the bytes of the code areas; the rest is
  strings, tables and variables. On the scripted sessions no game
  instruction is interpreted under the default timing profile; only the
  emulated BIOS's own stubs are.
- **Named and matched:** 1,006 routine addresses have explained C equivalents
  checked against their original bodies, covering 148,294 of 184,201 creditable
  bytes in the expanded seven-program scope. The historical census has 964
  rows wholly covered and 293 in part; executed code outside its rows now
  counts too, along with reviewed static code. Unreviewed discoveries and other modules remain an explicit
  scope backlog ([how progress is measured](docs/progress.md)).
- **Parity, measured:** 45 scripted routes, run as 44 sessions, end in
  identical state under the reference interpreter and the recompiled code -
  all of memory, the registers and every byte sent to the sound card, the
  palette, MIDI and the disk - checked every 50 million clocks along the
  way. They cover boot to flight, a full sortie through the debriefing and
  back, the speaker and Roland drivers, all nine theatres and all four
  mission categories, pilot creation, editing, erasure and saving, every
  primary objective type, supply drops, secret airstrips, air-to-air kills,
  landing at the home base, and the career awards and retirement. Every
  translated instruction, reached or not, is also compared from random
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
- **Not yet done:** the remaining named C routines; smooth 60+ fps motion
  without artefacts, and physical-display checks; play by a person at the
  keyboard; rendered Roland output against an independent reference, and
  listening checks.

The figures and evidence for each check are in
[docs/roadmap.md](docs/roadmap.md) (what is done and left) and
[docs/architecture.md](docs/architecture.md) (how the parity claim is
built and checked); [docs/parity-audit.md](docs/parity-audit.md) is an
earlier, dated audit of the reference-machine comparisons. The
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
py -m pip install capstone Pillow
py tools/build_recomp.py --data "C:/GOG Games/F-117A" --work "$env:USERPROFILE/f117-recomp-local/OpenNighthawk"
```

Replace the data path with your installed game. `capstone` is needed by the
translator; Pillow enables the picture comparator tests. Generated C,
coverage and saves go in the work directory (default
`%USERPROFILE%\f117-recomp-local`). On first build, `build.cmd` creates a
`build` junction to a local directory under `%USERPROFILE%\f117-recomp-local`,
with a separate CMake cache for each checkout. No generated files or game
assets belong in the source tree. This sequence has been verified from a
fresh public clone. Translation counts depend on accumulated coverage, so a
first build can translate slightly fewer instruction starts (that run
produced 89,276) than the figures above.

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
| `--ips N` | emulated CPU speed under `dosbox` timing, instructions per second (20,000,000 in `f117a`; 9,000,000 in `f117run` and route checks, GOG DOSBox's `cycles=9000`). Default D1REAL separately targets eight simulation steps per second |
| `--sim-hz 8\|15` | D1REAL cadence (default 8). Experimental 15 uses 15 updates/s normally and 30 during 2x compression; 40,000,000 instructions/s missed compressed-flight capacity; 60,000,000 passes the tested simulation checks but 4K presentation still fails pacing limits. Replays restore their recorded choice; legacy logs use 8 |
| `--roland munt\|windows\|off` | where Roland music goes: Munt's MT-32 emulation (needs `--mt32-roms`), the Windows MIDI synthesizer (General MIDI: wrong instruments), or nowhere |
| `--mt32-roms DIR` | the folder holding your MT-32 control and PCM ROMs, recognised by content whatever they are called (see [MT-32 ROMs](#mt-32-roms)); on its own it means `--roland munt` |
| `--midi N` | send Roland MIDI to Windows MIDI device N instead of the mapper |
| `--mt32-control FILE --mt32-pcm FILE` | name the two ROM files instead of a folder |
| `--opl dbopl\|nuked` | GOG DOSBox's OPL2 synthesizer (default), or Nuked OPL3 in OPL2 mode; both output at 44,100 Hz |
| `--speaker realsound\|pwm` | smoothed digitised speaker speech (default), or the original PWM carrier |
| `--scale N`, `--fullscreen`, `--no-aspect` | window size; fullscreen; square pixels instead of 4:3 |
| `--present scan\|replay\|interp\|rerun` | the picture in flight; see [Presentation](#presentation) |
| `--present-scale N` | with `replay`, `interp` or `rerun`: the picture N times finer, 1-16 |
| `--present-line-width N` | fine HUD stroke width in output pixels, 0-20; 0 (default) scales with `--present-scale`, 1 is a thin stroke; text and artwork keep their original pixels |
| `--present-age interp\|extrapolate` | with `interp`: trail the original by one logic step and be exact at its frames (default), or predict beyond the latest frame (can overshoot a sudden change) |
| `--config FILE`, `--no-config` | read settings from FILE instead of `f117a.ini` beside the executable; or read none |
| `--fix ID`, `--list-fixes` | enable a fix; `all` enables every fix, `none` clears the set. Normal play starts with all enabled; `--fix none --fix D5` selects only D5. Listing prints the current selection |
| `--record FILE`, `--no-record`, `--replay FILE` | record this session's input to FILE, or record nothing; play a recorded input log back |

Any option the app does not recognise (for example `--help`) prints the full
list, including diagnostic options not shown here.

The default D1REAL option uses eight simulation steps per real second;
`--present interp` interpolates their display. D1REAL replaces the original adaptive simulation
cadence; see [the timing option](docs/bugs.md#d1real-real-time-simulation-cadence).
The separate `D1TTL`, `D1SLOT`, `D1PROX` and `D1ACCEL` options repair incoming
missile lifetime, slot reuse, proximity sampling and acceleration.

Host keys (chosen so as not to collide with the game's):

| Keys | Action |
|---|---|
| Alt+Enter | toggle fullscreen |
| Ctrl+Alt+P | pause / resume |
| Ctrl+Alt+F12 | quit |

### Settings file

Every option can also be kept in a settings file: copy `f117a.example.ini`
(written beside `f117a.exe` by the build) to `f117a.ini` in the same folder
and edit it - one option per line without its dashes (`data = C:\GOG Games\F-117A`,
`roland = munt`, `mt32-roms = C:\ROMs`, `fullscreen = yes`, `fix = none`).
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
  text, sprites and cockpit artwork kept. It runs at the game's own frame
  rate with no in-between frames, and is the verified, sharp mode. Scale 9
  fills a 4K screen's 2160 lines at 4:3.
- `--present interp` adds in-between frames at the display's own rate, one
  logic step behind. It is experimental: some objects still move unevenly.
- `--present rerun` draws intermediate poses with the original's own,
  unmodified drawing code on a disposable machine copy, then refines them
  at the chosen scale. Its smoothed playback clock reduces uneven motion;
  `F117_PRESENT_CLOCK=raw` selects the older clock. It remains experimental:
  clock skips, higher refresh rates and physical-display motion need review.

**Higher resolution (`--present-scale` above 1) and higher frame rates
(`interp`, `rerun`) are experimental.** Both redraw what the original draws,
so they keep the original renderer's quirks: roads, coastlines and other
edges jitter as they do in the original; objects can appear in front of or
behind others in the wrong order, as the original's drawing order puts them;
lines and dots keep the same thickness at any distance; and there may be
others.

**Frame rate.** `interp` and `rerun` are the modes that raise it. The window
presents one picture per display refresh (vsync), so the frame rate follows
the refresh rate Windows has set for that display: 60 fps at 60 Hz, 144 fps
at 144 Hz, if the PC keeps up. The game's own logic still runs at its
original rate (about 15-17 steps a second); the extra pictures are drawn
between its steps, one step behind. `scan` and `replay` also present at the
display's refresh but only show a new picture when the game draws one.

The optional preset [f117a.4k.ini](f117a.4k.ini) (copied beside `f117a.exe`)
selects fullscreen, `present = interp` and scale 9:
`build\f117a.exe --config f117a.4k.ini --data INSTALL_DIR`. Select
3840x2160 at 60 Hz in Windows' display settings first. The build copies the
repository's `f117a.4k.ini` into `build\` whenever it changes, replacing the
copy there: edit the repository's file and rebuild, or keep your own file
and pass it with `--config`. For the sharp mode
without in-between frames, use `--present replay --present-scale 9`
instead. Design, status and measurements:
[docs/presentation.md](docs/presentation.md).

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
milestones cannot: a real landing (ground contact inside the home approach
box, a stop at idle, gear and brakes, the original countdown and the
successful-return result), a credited reconnaissance photograph, or a secret
airstrip's delivery. For example:

```powershell
py tools/landing_pilot.py --data "C:\GOG Games\F-117A" --engine interp --replay tools/routes/landing.input --out C:/landing-check
py tools/recon_pilot.py --data "C:\GOG Games\F-117A" --engine interp --replay tools/routes/recon.input --out C:/recon-check
py tools/recon_pilot.py --data "C:\GOG Games\F-117A" --engine interp --complete --replay tools/routes/recon_return.input --out C:/recon-return-check
py tools/airstrip_check.py --data "C:\GOG Games\F-117A" --replay tools/routes/secret_airstrip.input --steps 8373782617 --out PRIVATE_DIR
```

Every route and its observer:
[docs/architecture.md](docs/architecture.md#observing-flights-through-normal-controls)
and [tools/routes/README.md](tools/routes/README.md). Career routes chain
actual earned saves; no saved game is distributed. The
[parity audit](docs/parity-audit.md) documents independent DOSBox saves,
flight observations, input stress, host pacing and rendered audio, including
the remaining differences and checks requiring hardware or ROMs. Every
process the project repeats, and what it costs, is listed in
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
| `tools/` | the build pipeline, unpacker, scripted routes, checks |
| `tests/` | silicon-vector harnesses for the interpreter and for generated code; lockstep and unit tests |
| `docs/` | [architecture](docs/architecture.md), [roadmap](docs/roadmap.md), [original-game bugs](docs/bugs.md), [presentation](docs/presentation.md), [parity audit](docs/parity-audit.md), [repeated processes](docs/repeated-processes.md), [provenance](docs/provenance.md) |

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

- **SingleStepTests**, for the 8088 and 80286 hardware vectors.
- **Nuked OPL3** (nukeykt), for the AdLib's chip.
- **DOSBox DBOPL** (The DOSBox Team), for the reference OPL2 synthesizer.
- **debugcom**, for the mission-generator and secret-airstrip analysis the
  bug tracker cites.
- **MicroProse / MPS Labs**, for the game, and for release notes that are
  still telling us how it works.
